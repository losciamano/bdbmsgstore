/*
  Copyright (c) 2007, 2008, 2009 Red Hat, Inc.

  This file is part of the Qpid async store library msgstore.so.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
  USA

  The GNU Lesser General Public License is available in the file COPYING.
*/

#ifndef _JournalImpl_
#define _JournalImpl_

#include <set>
#include "jrnl/enums.hpp"
#include "PreparedTransaction.h"
#include <qpid/broker/PersistableQueue.h>
#include <qpid/sys/Timer.h>
#include <qpid/sys/Time.h>
#include <boost/ptr_container/ptr_list.hpp>
#include <boost/intrusive_ptr.hpp>
#include "qpid/management/Manageable.h"
#include "qmf/com/redhat/rhm/store/Journal.h"
#include "db-inc.h"
#include <boost/asio/error.hpp>

namespace qpid { namespace sys {
class Timer;
}}

namespace mrg {
namespace msgstore {

class JournalImpl;

class InactivityFireEvent : public qpid::sys::TimerTask
{
    JournalImpl* _parent;
    qpid::sys::Mutex _ife_lock;

  public:
    InactivityFireEvent(JournalImpl* p, const qpid::sys::Duration timeout);
    virtual ~InactivityFireEvent() {}
    void fire();
    inline void cancel() { qpid::sys::Mutex::ScopedLock sl(_ife_lock); _parent = 0; }
};

class GetEventsFireEvent : public qpid::sys::TimerTask
{
    JournalImpl* _parent;
    qpid::sys::Mutex _gefe_lock;

  public:
    GetEventsFireEvent(JournalImpl* p, const qpid::sys::Duration timeout);
    virtual ~GetEventsFireEvent() {}
    void fire();
    inline void cancel() { qpid::sys::Mutex::ScopedLock sl(_gefe_lock); _parent = 0; }
};

class TxnCtxt;

class JournalImpl : public qpid::broker::ExternalQueueStore
{
  public:
    typedef boost::function<void (JournalImpl&)> DeleteCallback;
    
  private:
    static qpid::sys::Mutex _static_lock;
    static u_int32_t cnt;

    qpid::sys::Timer& timer;
    bool getEventsTimerSetFlag;
    boost::intrusive_ptr<qpid::sys::TimerTask> getEventsFireEventsPtr;
    qpid::sys::Mutex _getf_lock;

    u_int64_t lastReadRid; // rid of last read msg for loadMsgContent() - detects out-of-order read requests
    std::vector<u_int64_t> oooRidList; // list of out-of-order rids (greater than current rid) encountered during read sequence

    bool writeActivityFlag;
    bool flushTriggeredFlag;
    boost::intrusive_ptr<qpid::sys::TimerTask> inactivityFireEventPtr;

    // temp local vars for loadMsgContent below
    bool _external;
    bool _is_init;

    boost::shared_ptr<Db> messageDb;
    boost::shared_ptr<DbEnv> dbEnv;
    std::string journalName;
    std::string journalDirectory;
    std::string bdbDir;

    qpid::management::ManagementAgent* _agent;
    qmf::com::redhat::rhm::store::Journal* _mgmtObject;
    DeleteCallback deleteCallback;
    
  public:
    
    JournalImpl(qpid::sys::Timer& timer,
                const std::string& journalId,
                const std::string& journalDirectory,
                const std::string& bdbDir,
                const qpid::sys::Duration getEventsTimeout,
                const qpid::sys::Duration flushTimeout,
                qpid::management::ManagementAgent* agent,
		boost::shared_ptr<DbEnv>& de,
                DeleteCallback deleteCallback=DeleteCallback()
		);

    virtual ~JournalImpl();

    void initialize();

    void recover(/*const u_int16_t num_jfiles,
                 const bool auto_expand,
                 const u_int16_t ae_max_jfiles,
                 const u_int32_t jfsize_sblks,
                 const u_int16_t wcache_num_pages,
                 const u_int32_t wcache_pgsize_sblks,
                 mrg::journal::aio_callback* const cbp,
                 boost::ptr_list<msgstore::PreparedTransaction>* prep_tx_list_ptr,*/
                 u_int64_t& highest_rid,
                 u_int64_t queue_id);

    void recoverMessages(TxnCtxt& txn,std::vector< std::pair < uint64_t,std::string> >& recovered);
    void recover_complete();


    inline const std::string& id() const { return journalName; }



    bool is_enqueued(const u_int64_t rid, bool ignore_lock = false);


    // Temporary fn to read and save last msg read from journal so it can be assigned
    // in chunks. To be replaced when coding to do this direct from the journal is ready.
    // Returns true if the record is extern, false if local.
    bool loadMsgContent(u_int64_t rid, std::string& data, size_t length, size_t offset = 0);

    // Overrides for write inactivity timer
    void enqueue_data(char* data_buff, const size_t tot_data_len, const size_t this_data_len, 
					uint64_t pid,const std::string& xid, const bool transient=false);
    void dequeue_data(const uint64_t pid, const std::string& xid, const bool txn_coml_commit = false);

    void txn_abort(const std::string& xid);

    void txn_commit(const std::string& xid);
    
    void stop(bool block_till_aio_cmpl = false);

    // Logging
    void log(mrg::journal::log_level level, const std::string& log_stmt) const;
    void log(mrg::journal::log_level level, const char* const log_stmt) const;

    // Overrides for get_events timer
    bool flush();

    // TimerTask callback
    void getEventsFire();
    void flushFire();

    inline bool is_ready() const { return _is_init; }
    inline bool is_txn_synced(const std::string& xid) { return true; /*TODO: implement transaction*/ }

    qpid::management::ManagementObject* GetManagementObject (void) const
    { return _mgmtObject; }

    qpid::management::Manageable::status_t ManagementMethod (uint32_t,
                                                             qpid::management::Args&,
                                                             std::string&);

    void resetDeleteCallback() { deleteCallback = DeleteCallback(); }
    
    void delete_jrnl_files();

  private:
    
    bool store_msg(boost::shared_ptr<Db> db, const uint64_t pid, Dbt& p);
    void remove_msg(boost::shared_ptr<Db> db, const uint64_t pid);
    void init_message_db();



    inline void setGetEventTimer()
    {
        getEventsFireEventsPtr->setupNextFire();
        timer.add(getEventsFireEventsPtr);
        getEventsTimerSetFlag = true;
    }

}; // class JournalImpl

class TplJournalImpl : public JournalImpl
{
  public:
    TplJournalImpl(qpid::sys::Timer& timer,
                   const std::string& journalId,
                   const std::string& journalDirectory,
                   const std::string& journalBaseFilename,
                   const qpid::sys::Duration getEventsTimeout,
                   const qpid::sys::Duration flushTimeout,
                   qpid::management::ManagementAgent* agent,
		   boost::shared_ptr<DbEnv>& de) :
        JournalImpl(timer, journalId, journalDirectory, journalBaseFilename, getEventsTimeout, flushTimeout, agent,de)
    {}

    ~TplJournalImpl() {}
/*
    // Special version of read_data_record that ignores transactions - needed when reading the TPL
    inline mrg::journal::iores read_data_record(void** const datapp, std::size_t& dsize,
                                                void** const xidpp, std::size_t& xidsize, bool& transient, bool& external,
                                                mrg::journal::data_tok* const dtokp) {
        return JournalImpl::read_data_record(datapp, dsize, xidpp, xidsize, transient, external, dtokp, true);
    }
    inline void read_reset() { _rmgr.invalidate(); }*/
}; // class TplJournalImpl

} // namespace msgstore
} // namespace mrg

#endif
