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

#include <list>
#include <vector>
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

/**
*	Class that implements a timer that calls flush during inactivity phase.
**/
class InactivityFireEvent : public qpid::sys::TimerTask
{
    /**
    *	Journal to flush during inactivity
    **/
    JournalImpl* _parent;
    /**
    *	Lock over parent object.
    **/
    qpid::sys::Mutex _ife_lock;

  public:
    /**
    *	Constructor that sets the JournalImpl to flush and set the internal timer to fire at timeout
    *	@param	p	JournalImpl to Flush
    *	@param	timeout	Timeout for the inactivity phase.
    **/
    InactivityFireEvent(JournalImpl* p, const qpid::sys::Duration timeout);
    /**
    *	Virtual Empty desctructor
    **/
    virtual ~InactivityFireEvent() {}
    /**
    *	Fire method that will be called on timeout.
    *	This method get lock over Journal and then flush it.
    **/
    void fire();
    /**
    *	Method to cancel an event. This method gets the lock over the Journal and the nullify its reference
    */
    inline void cancel() { qpid::sys::Mutex::ScopedLock sl(_ife_lock); _parent = 0; }
};

/*
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
*/

class TxnCtxt;

/**
*	Class implementing an External Store for QPID Queue based on a Berkeley Database.
*	This class grants persistence to the messages of the queue by storing it in the database. This enables flow_to_disk policy too.
**/
class JournalImpl : public qpid::broker::ExternalQueueStore
{
  public:
    /**
    *	Convenienct type definition for a pointer to a void function taking a reference to a JournalImpl as argument.
    *	This function is called on Journal Delete.
    **/
    typedef boost::function<void (JournalImpl&)> DeleteCallback;
    
  private:
    /**
    *	Timer used to create timeout
    **/
    qpid::sys::Timer& timer;
    /*bool getEventsTimerSetFlag;
    boost::intrusive_ptr<qpid::sys::TimerTask> getEventsFireEventsPtr;
    qpid::sys::Mutex _getf_lock;*/
    /**
    *	Flag that indicates if a write activity has been done on the database.
    **/
    bool writeActivityFlag;
    /**
    *	Flag that indicates if a flush has been triggered for the database.
    **/
    bool flushTriggeredFlag;
    /**
    *	Pointer to the timer task for the flush 
    **/
    boost::intrusive_ptr<qpid::sys::TimerTask> inactivityFireEventPtr;

    // temp local vars for loadMsgContent below
    /**
    *	Flag that indicates if the Journal is initialized.
    **/
    bool _is_init;
    
    /**
    *	Pointer to the Berkeley Database Handle used for the persistence
    **/
    boost::shared_ptr<Db> messageDb;
    /**
    *	Pointer to the Berkeley Database Environment of the Store Database.
    **/
    boost::shared_ptr<DbEnv> dbEnv;
    /**
    *	Name of the Journal 
    **/
    std::string journalName;
    /**
    *	Directory of the Journal.
    **/
    std::string journalDirectory;
    /**
    *	Base directory for all Berkeley Databases.
    **/
    std::string bdbDir;
    /**
    *	Pointer to the Management Agent
    **/
    qpid::management::ManagementAgent* _agent;
    /**
    *	Pointer to the management object for the Journal.
    **/
    qmf::com::redhat::rhm::store::Journal* _mgmtObject;
    /**
    *	Delete Callback that will execute when Journal is deleted.
    **/
    DeleteCallback deleteCallback;
    /**
    *	List containing persistence Id of the transient messages that will be deleted on the next call of discard_transient_message() method
    **/
    std::list<uint64_t> transientList;
    
  public:
    /**
    *	Constructor that initializes all attributes, starts the timer for flush operation, create and register the management object for the Journal
    *	@param	timer			QPID Timer used to create timeout
    *	@param	journalId		Journal Identifier used as Journal Name
    *	@param	journalDirectory	Directory for this Journal
    *	@param	bdbDir			Base directory for all Berkeley Databases
    *	@param	flushTimeout		Duration for the flush timeout
    *	@param	agent			Agent for the managemnt
    *	@param	de			Pointer to the Berkeley Database Environment used in the Store
    *	@param	deleteCallback		Function called when Journal is deleted.
    **/
    JournalImpl(qpid::sys::Timer& timer,
                const std::string& journalId,
                const std::string& journalDirectory,
                const std::string& bdbDir,
 //               const qpid::sys::Duration getEventsTimeout,
                const qpid::sys::Duration flushTimeout,
                qpid::management::ManagementAgent* agent,
		boost::shared_ptr<DbEnv>& de,
                DeleteCallback deleteCallback=DeleteCallback()
		);
    /**
    *	Virtual destructor that calls the DeleteCallback, stops the timer for flush operation, destroys the management object for the Journal and
    *	then close the Berkeley Database.
    **/
    virtual ~JournalImpl();
    /**
    *	Initialization method that initialize the database and set some management object attributes.
    *	\todo	Add some attributes to QMF Object Schema and then give them a value to support Journal users
    **/
    void initialize();
    /**
    *	Recover method called from Store during recovery implementig phase one tasks. 
    *	In this implementation the method does nothing, simply log some information.
    *	@param	highest_rid	Reference to the current highest persistence id
    *	@param	queue_id	Persistence Id for the queue.
    *	\todo	Implement Transaction recovery in this method to support QPID transaction
    **/
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
    /**
    *	Phase Two Recover Method that reads messages from the database.
    *	This method open a cursor on Berkeley Database outside any Transaction Context and fills the vector with data loaded from the persistence.
    *	All the read operations are done outside transaction because there're limitations on the number of lock in the Berkeley Database Environments.
    *	However transaction are useless for concurrency in this scenario because no write operation are allowed in recovery phase. For fault tollerance
    *	issues, keep in mind that this is a recovery phase so if a crash occours during recovery then recovery starts from the begining in any case.
    *	NOTE: A write operation is done at the end of recovery phase, when deleting the transient message but at that moment the read cursor will be
    *	already closed.
    *	@param	recovered	Reference to a vector that will be filled by this method with pair of persistence Id (uint64_t) e encoded 
    *				message (string)
    **/
    void recoverMessages(std::vector< std::pair < uint64_t,std::string> >& recovered);
    /**
    *	The method closes the recover phase calling discard_transient_message and deletes the transient messages inserted in the database to allow
    *	flow to disk policy.
    **/
    void recover_complete();
    /**
    *	The method returns the identifier of the journal.
    **/
    inline const std::string& id() const { return journalName; }
    /**
    *	The method controlls if the message identified by a given persistence Id is contained in the Journal.
    *	@param	rid		Persistence Id of the message to check if enqueue
    *	@param 	ignore_lock	Unused parameter
    **/
    bool is_enqueued(const u_int64_t rid, bool ignore_lock = false);


    /**
    *	The method is used by the Store to load chunk of message of a certain lenght.
    *	This method reads from the database, in a transaction context, the data associated with the persistence id partially decode 
    *	the data and append to the given buffer (string reference) the chunk identified by offset and lenght.
    *	@param	rid	Persistence Id used as key for querying the database
    *	@param	data	Reference to a buffer where the method append the message chunk loaded from the persistence
    *	@param	length	Lenght of the data chunk to load
    *	@param	offset	Offset of the data chunk to load, calculated from the begining of the encoded message data.
    *	@return	True if data chunk is loaded successfully, false if some database error occurred or if the persistence Id is not found 
    *		inside the database.
    **/
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
    //void getEventsFire();
    void flushFire();

    inline bool is_ready() const { return _is_init; }
    inline bool is_txn_synced(const std::string& /*xid*/) { return true; /*TODO: implement transaction*/ }
    void discard_transient_message();
    void compact_message_database();

    qpid::management::ManagementObject* GetManagementObject (void) const
    { return _mgmtObject; }

    qpid::management::Manageable::status_t ManagementMethod (uint32_t,
                                                             qpid::management::Args&,
                                                             std::string&);

    void resetDeleteCallback() { deleteCallback = DeleteCallback(); }
    
    void register_as_transient(std::vector<uint64_t>& trList);


    void delete_jrnl_files();

  private:
    
    bool store_msg(boost::shared_ptr<Db> db, const uint64_t pid, Dbt& p);
    void remove_msg(boost::shared_ptr<Db> db, const uint64_t pid);
    void init_message_db();



    /*inline void setGetEventTimer()
    {
        getEventsFireEventsPtr->setupNextFire();
        timer.add(getEventsFireEventsPtr);
        getEventsTimerSetFlag = true;
    }*/

}; // class JournalImpl

class TplJournalImpl : public JournalImpl
{
  public:
    TplJournalImpl(qpid::sys::Timer& timer,
                   const std::string& journalId,
                   const std::string& journalDirectory,
                   const std::string& journalBaseFilename,
                   //const qpid::sys::Duration getEventsTimeout,
                   const qpid::sys::Duration flushTimeout,
                   qpid::management::ManagementAgent* agent,
		   boost::shared_ptr<DbEnv>& de) :
        JournalImpl(timer, journalId, journalDirectory, journalBaseFilename, /*getEventsTimeout,*/ flushTimeout, agent,de)
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

int bt_compare_func(Db *db,const Dbt* value1,const Dbt* value2);
#endif
