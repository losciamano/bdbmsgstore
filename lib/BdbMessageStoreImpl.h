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

#ifndef _BdbMessageStoreImpl_
#define _BdbMessageStoreImpl_

#include <string>

#include "db-inc.h"
#include "Cursor.h"
#include "IdDbt.h"
#include "IdSequence.h"
#include "JournalImpl.h"
#include "jrnl/jcfg.hpp"
#include "PreparedTransaction.h"
#include "qpid/broker/Broker.h"
#include "qpid/broker/MessageStore.h"
#include "qpid/management/Manageable.h"
#include "qmf/com/redhat/rhm/store/Store.h"
#include "TxnCtxt.h"
#include <boost/asio.hpp>
#include <boost/thread.hpp>

// Assume DB_VERSION_MAJOR == 4
#if (DB_VERSION_MINOR == 2)
#include <errno.h>
#define DB_BUFFER_SMALL ENOMEM
#endif

namespace qpid { namespace sys {
class Timer;
}}

namespace mrg {
namespace msgstore {

/**
 * An implementation of the MessageStore interface based on Berkeley DB
 */
class BdbMessageStoreImpl : public qpid::broker::MessageStore, public qpid::management::Manageable
{
  public:
    typedef boost::shared_ptr<Db> db_ptr;
    typedef boost::shared_ptr<DbEnv> dbEnv_ptr;

    struct StoreOptions : public qpid::Options {
        StoreOptions(const std::string& name="Store Options");
        std::string clusterName;
        std::string storeDir;
        bool      truncateFlag;
	bool 	  compactFlag;
    };

  protected:
    typedef std::map<u_int64_t, qpid::broker::RecoverableQueue::shared_ptr> queue_index;
    typedef std::map<u_int64_t, qpid::broker::RecoverableExchange::shared_ptr> exchange_index;
    typedef std::map<u_int64_t, qpid::broker::RecoverableMessage::shared_ptr> message_index;

    typedef LockedMappings::map txn_lock_map;
    typedef boost::ptr_list<PreparedTransaction> txn_list;

    // Structs for Transaction Recover List (TPL) recover state
    struct TplRecoverStruct {
        u_int64_t rid; // rid of TPL record
        bool deq_flag;
        bool commit_flag;
        bool tpc_flag;
        TplRecoverStruct(const u_int64_t _rid, const bool _deq_flag, const bool _commit_flag, const bool _tpc_flag);
    };
    typedef TplRecoverStruct TplRecover;
    typedef std::pair<std::string, TplRecover> TplRecoverMapPair;
    typedef std::map<std::string, TplRecover> TplRecoverMap;
    typedef TplRecoverMap::const_iterator TplRecoverMapCitr;

    typedef std::map<std::string, JournalImpl*> JournalListMap;
    typedef JournalListMap::iterator JournalListMapItr;

    // Default store settings
    static const bool      defTruncateFlag = false;

    static const std::string storeTopLevelDir;
    static qpid::sys::Duration defJournalGetEventsTimeout;
    static qpid::sys::Duration defJournalFlushTimeout;
    
    std::list<db_ptr> dbs;
    dbEnv_ptr dbenv;
    db_ptr queueDb;
    db_ptr configDb;
    db_ptr exchangeDb;
    db_ptr mappingDb;
    db_ptr bindingDb;
    db_ptr generalDb;

    // Pointer to Transaction Prepared List (TPL) journal instance
    boost::shared_ptr<TplJournalImpl> tplStorePtr;
    //TplRecoverMap tplRecoverMap;//TODO:implement transaction
    qpid::sys::Mutex tplInitLock;
    JournalListMap journalList;
    qpid::sys::Mutex journalListLock;

    IdSequence queueIdSequence;
    IdSequence exchangeIdSequence;
    IdSequence generalIdSequence;
    IdSequence messageIdSequence;
    std::string storeDir;
    bool      truncateFlag;
    bool      compactFlag;
    u_int64_t highestRid;
    bool isInit;
    const char* envPath;
    qpid::sys::Timer& timer;

    qmf::com::redhat::rhm::store::Store* mgmtObject;
    qpid::management::ManagementAgent* agent;
    
    boost::asio::io_service bdbserv;
    boost::asio::io_service::work bdbwork;
    boost::shared_ptr<boost::thread> servThread;

    std::list<JournalImpl*> recoveredJournal;

    

    // Parameter validation and calculation
    void init();

    void recoverQueues(TxnCtxt& txn,
                       qpid::broker::RecoveryManager& recovery,
                       queue_index& index,
                       txn_list& locked,
                       message_index& messages);
    uint64_t recoverMessages(qpid::broker::RecoveryManager& recovery,
                         qpid::broker::RecoverableQueue::shared_ptr& queue,
                         txn_list& locked,
                         message_index& prepared,
                         long& rcnt,
                         long& idcnt,
			 long& tcnt);
    qpid::broker::RecoverableMessage::shared_ptr getExternMessage(qpid::broker::RecoveryManager& recovery,
                                                                  uint64_t mId,
                                                                  unsigned& headerSize);
    void recoverExchanges(TxnCtxt& txn,
                          qpid::broker::RecoveryManager& recovery,
                          exchange_index& index);
    void recoverBindings(TxnCtxt& txn,
                         exchange_index& exchanges,
                         queue_index& queues);
    void recoverGeneral(TxnCtxt& txn,
                        qpid::broker::RecoveryManager& recovery);
    int enqueueMessage(TxnCtxt& txn,
                       IdDbt& msgId,
                       qpid::broker::RecoverableMessage::shared_ptr& msg,
                       queue_index& index,
                       txn_list& locked,
                       message_index& prepared);
    void readTplStore();
    void recoverTplStore();
    void recoverLockedMappings(txn_list& txns);
    TxnCtxt* check(qpid::broker::TransactionContext* ctxt);
    u_int64_t msgEncode(std::vector<char>& buff, const boost::intrusive_ptr<qpid::broker::PersistableMessage>& message);
    void store(const qpid::broker::PersistableQueue* queue,
               TxnCtxt* txn,
               const boost::intrusive_ptr<qpid::broker::PersistableMessage>& message,
               bool newId);
    void async_dequeue(qpid::broker::TransactionContext* ctxt,
                       const boost::intrusive_ptr<qpid::broker::PersistableMessage>& msg,
                       const qpid::broker::PersistableQueue& queue);
    void destroy(db_ptr db,
                 const qpid::broker::Persistable& p);
    bool create(db_ptr db,
                IdSequence& seq,
                const qpid::broker::Persistable& p);
    void completed(TxnCtxt& txn,
                   bool commit);
    void deleteBindingsForQueue(const qpid::broker::PersistableQueue& queue);
    void deleteBinding(const qpid::broker::PersistableExchange& exchange,
                       const qpid::broker::PersistableQueue& queue,
                       const std::string& key);

    void put(db_ptr db,
             DbTxn* txn,
             Dbt& key,
             Dbt& value);
    void open(db_ptr db,
              DbTxn* txn,
              const char* file,
              bool dupKey);
    void closeDbs();

    // journal functions
    void createJrnlQueue(const qpid::broker::PersistableQueue& queue);
    u_int32_t bHash(const std::string str);
    std::string getJrnlDir(const qpid::broker::PersistableQueue& queue); //for exmaple /var/rhm/ + queueDir/
    std::string getJrnlHashDir(const std::string& queueName);
    std::string getJrnlBaseDir();
    std::string getBdbBaseDir();
    std::string getTplBaseDir();
    inline void checkInit() {
        // TODO: change the default dir to ~/.qpidd
        if (!isInit) { init("/tmp"); isInit = true; }
    }
    void chkTplStoreInit();

    // debug aid for printing XIDs that may contain non-printable chars
    static std::string xid2str(const std::string xid) {
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (unsigned i=0; i<xid.size(); i++) {
            if (isprint(xid[i]))
                oss << xid[i];
            else
                oss << "/" << std::setw(2) << (int)((char)xid[i]);
        }
        return oss.str();
    }

  public:
    typedef boost::shared_ptr<BdbMessageStoreImpl> shared_ptr;

    BdbMessageStoreImpl(qpid::sys::Timer& timer, const char* envpath = 0);

    virtual ~BdbMessageStoreImpl();

    bool init(const qpid::Options* options);

    bool init(const std::string& dir,const bool truncateFlag=false);

    void truncateInit(const bool saveStoreContent = false);

    void initManagement (qpid::broker::Broker* broker);

    void finalize();

    void create(qpid::broker::PersistableQueue& queue,
                const qpid::framing::FieldTable& args);

    void destroy(qpid::broker::PersistableQueue& queue);

    void create(const qpid::broker::PersistableExchange& queue,
                const qpid::framing::FieldTable& args);

    void destroy(const qpid::broker::PersistableExchange& queue);

    void bind(const qpid::broker::PersistableExchange& exchange,
              const qpid::broker::PersistableQueue& queue,
              const std::string& key,
              const qpid::framing::FieldTable& args);

    void unbind(const qpid::broker::PersistableExchange& exchange,
                const qpid::broker::PersistableQueue& queue,
                const std::string& key,
                const qpid::framing::FieldTable& args);

    void create(const qpid::broker::PersistableConfig& config);

    void destroy(const qpid::broker::PersistableConfig& config);

    void recover(qpid::broker::RecoveryManager& queues);

    void stage(const boost::intrusive_ptr<qpid::broker::PersistableMessage>& msg);

    void destroy(qpid::broker::PersistableMessage& msg);

    void appendContent(const boost::intrusive_ptr<const qpid::broker::PersistableMessage>& msg,
                       const std::string& data);

    void loadContent(const qpid::broker::PersistableQueue& queue,
                     const boost::intrusive_ptr<const qpid::broker::PersistableMessage>& msg,
                     std::string& data,
                     u_int64_t offset,
                     u_int32_t length);

    void enqueue(qpid::broker::TransactionContext* ctxt,
                 const boost::intrusive_ptr<qpid::broker::PersistableMessage>& msg,
                 const qpid::broker::PersistableQueue& queue);

    void dequeue(qpid::broker::TransactionContext* ctxt,
                 const boost::intrusive_ptr<qpid::broker::PersistableMessage>& msg,
                 const qpid::broker::PersistableQueue& queue);

    void flush(const qpid::broker::PersistableQueue& queue);

    u_int32_t outstandingQueueAIO(const qpid::broker::PersistableQueue& queue);

    void collectPreparedXids(std::set<std::string>& xids);

    std::auto_ptr<qpid::broker::TransactionContext> begin();

    std::auto_ptr<qpid::broker::TPCTransactionContext> begin(const std::string& xid);

    void prepare(qpid::broker::TPCTransactionContext& ctxt);

    void localPrepare(TxnCtxt* ctxt);

    void commit(qpid::broker::TransactionContext& ctxt);

    void abort(qpid::broker::TransactionContext& ctxt);

    qpid::management::ManagementObject* GetManagementObject (void) const
        { return mgmtObject; }

    inline qpid::management::Manageable::status_t ManagementMethod (u_int32_t, qpid::management::Args&)
        { return qpid::management::Manageable::STATUS_OK; }

    std::string getStoreDir() const;

  private:
    void journalDeleted(JournalImpl&);

}; // class BdbMessageStoreImpl

} // namespace msgstore
} // namespace mrg

#endif
