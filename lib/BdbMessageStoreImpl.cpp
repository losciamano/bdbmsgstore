/*
 Copyright (c) 2007, 2008, 2009, 2010 Red Hat, Inc.

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

#include "BdbMessageStoreImpl.h"

#include "BindingDbt.h"
#include "BufferValue.h"
#include "IdDbt.h"
#include "jrnl/jdir.hpp"
#include "jrnl/jexception.hpp"
#include "qpid/log/Statement.h"
#include "qmf/com/redhat/rhm/store/Package.h"
#include "StoreException.h"
#include <dirent.h>
#include <vector>
#include <utility>

#define MAX_AIO_SLEEPS 100000 // tot: ~1 sec
#define AIO_SLEEP_TIME_US  10 // 0.01 ms

using namespace mrg::msgstore;
using namespace qpid::broker;
using boost::static_pointer_cast;
using boost::intrusive_ptr;

using std::auto_ptr;
using std::max;
using qpid::framing::Buffer;
using qpid::framing::FieldTable;
using qpid::management::ManagementAgent;
namespace _qmf = qmf::com::redhat::rhm::store;

const std::string BdbMessageStoreImpl::storeTopLevelDir("rhm"); // Sets the top-level store dir name
// FIXME aconway 2010-03-09: was 10
qpid::sys::Duration BdbMessageStoreImpl::defJournalGetEventsTimeout(1 * qpid::sys::TIME_MSEC); // 10ms
qpid::sys::Duration BdbMessageStoreImpl::defJournalFlushTimeout(500 * qpid::sys::TIME_MSEC); // 0.5s
qpid::sys::Mutex TxnCtxt::globalSerialiser;

BdbMessageStoreImpl::TplRecoverStruct::TplRecoverStruct(const u_int64_t _rid,
                                                              const bool _deq_flag,
                                                              const bool _commit_flag,
                                                              const bool _tpc_flag) :
                                                              rid(_rid),
                                                              deq_flag(_deq_flag),
                                                              commit_flag(_commit_flag),
                                                              tpc_flag(_tpc_flag)
{}

BdbMessageStoreImpl::BdbMessageStoreImpl(qpid::sys::Timer& timer_, const char* envpath) :
                                 truncateFlag(false),
				 compactFlag(true),
                                 highestRid(0),
                                 isInit(false),
                                 envPath(envpath),
                                 timer(timer_),
                                 mgmtObject(0),
                                 agent(0),
				 bdbwork(bdbserv)
{}

void BdbMessageStoreImpl::initManagement (Broker* broker)
{
    if (broker != 0) {
        agent = broker->getManagementAgent();
        if (agent != 0) {
            _qmf::Package packageInitializer(agent);
            mgmtObject = new _qmf::Store(agent, this, broker);

            mgmtObject->set_location(storeDir);

            agent->addObject(mgmtObject, 0, true);
        }
    }
}

bool BdbMessageStoreImpl::init(const qpid::Options* options)
{
    // Extract and check options
    const StoreOptions* opts = static_cast<const StoreOptions*>(options);
    this->compactFlag=opts->compactFlag;

    // Pass option values to init(...)
    return init(opts->storeDir);
}

// These params, taken from options, are assumed to be correct and verified
bool BdbMessageStoreImpl::init(const std::string& dir,
                           const bool truncateFlag)
{
    if (isInit) return true;

    // Set geometry members (converting to correct units where req'd)
    if (dir.size()>0) storeDir = dir;

    if (truncateFlag)
        truncateInit(false);
    else
        init();

    QPID_LOG(notice, "Store module initialized; store-dir=" << dir);

    return isInit;
}

void BdbMessageStoreImpl::init()
{
    const int retryMax = 3;
    int bdbRetryCnt = 0;
    do {
        if (bdbRetryCnt++ > 0)
        {
            closeDbs();
            ::usleep(1000000); // 1 sec delay
            QPID_LOG(error, "Previoius BDB store initialization failed, retrying (" << bdbRetryCnt << " of " << retryMax << ")...");
        }

        try {
            journal::jdir::create_dir(getBdbBaseDir());

            dbenv.reset(new DbEnv(0));
            dbenv->set_errpfx("bdbmsgstore");
            dbenv->set_lg_regionmax(256000); // default = 65000
	    dbenv->set_lk_detect(DB_LOCK_DEFAULT);
	    //dbenv->set_lk_max_locks(50000);
	    //dbenv->set_lk_max_objects(50000);
            dbenv->open(getBdbBaseDir().c_str(), DB_THREAD | DB_CREATE | DB_INIT_TXN | DB_INIT_LOCK | DB_INIT_LOG | DB_INIT_MPOOL | DB_USE_ENVIRON | DB_RECOVER, 0);

            // Databases are constructed here instead of the constructor so that the DB_RECOVER flag can be used
            // against the database environment. Recover can only be performed if no databases have been created
            // against the environment at the time of recovery, as recovery invalidates the environment.
            queueDb.reset(new Db(dbenv.get(), 0));
            dbs.push_back(queueDb);
            configDb.reset(new Db(dbenv.get(), 0));
            dbs.push_back(configDb);
            exchangeDb.reset(new Db(dbenv.get(), 0));
            dbs.push_back(exchangeDb);
            mappingDb.reset(new Db(dbenv.get(), 0));
            dbs.push_back(mappingDb);
            bindingDb.reset(new Db(dbenv.get(), 0));
            dbs.push_back(bindingDb);
            generalDb.reset(new Db(dbenv.get(), 0));
            dbs.push_back(generalDb);

            TxnCtxt txn;
            txn.begin(dbenv.get(), false);
            try {
                open(queueDb, txn.get(), "queues.db", false);
                open(configDb, txn.get(), "config.db", false);
                open(exchangeDb, txn.get(), "exchanges.db", false);
                open(mappingDb, txn.get(), "mappings.db", true);
                open(bindingDb, txn.get(), "bindings.db", true);
                open(generalDb, txn.get(), "general.db",  false);
                txn.commit();
            } catch (...) { txn.abort(); throw; }

            tplStorePtr.reset(new TplJournalImpl(timer, "TplStore", getTplBaseDir(), "tpl", defJournalGetEventsTimeout, defJournalFlushTimeout, agent,dbenv));
	    
	    this->servThread=boost::shared_ptr<boost::thread>(new boost::thread(boost::bind(&boost::asio::io_service::run, &bdbserv)));
	    QPID_LOG(debug,"Journal io_service run");

            isInit = true;
        } catch (const DbException& e) {
            if (e.get_errno() == DB_VERSION_MISMATCH)
            {
                QPID_LOG(error, "Database environment mismatch: This version of db4 does not match that which created the store database.: " << e.what());
                THROW_STORE_EXCEPTION_2("Database environment mismatch: This version of db4 does not match that which created the store database. "
                                        "(If recovery is not important, delete the contents of the store directory. Otherwise, try upgrading the database using "
                                        "db_upgrade or using db_recover - but the db4-utils package must also be installed to use these utilities.)", e);
            }
            QPID_LOG(error, "BDB exception occurred while initializing store: " << e.what());
            if (bdbRetryCnt >= retryMax)
                THROW_STORE_EXCEPTION_2("BDB exception occurred while initializing store", e);
        } catch (const StoreException&) {
            throw;
        } catch (const journal::jexception& e) {
            QPID_LOG(error, "Journal Exception occurred while initializing store: " << e);
            THROW_STORE_EXCEPTION_2("Journal Exception occurred while initializing store", e.what());
        } catch (...) {
            QPID_LOG(error, "Unknown exception occurred while initializing store.");
            throw;
        }
    } while (!isInit);
}

void BdbMessageStoreImpl::finalize()
{
    if (tplStorePtr.get() && tplStorePtr->is_ready()) tplStorePtr->stop(true);
    {
        qpid::sys::Mutex::ScopedLock sl(journalListLock);
        for (JournalListMapItr i = journalList.begin(); i != journalList.end(); i++)
        {
            JournalImpl* jQueue = i->second;
            jQueue->resetDeleteCallback();
            if (jQueue->is_ready()) jQueue->stop(true);
        }
    }

    if (mgmtObject != 0) {
        mgmtObject->resourceDestroy();
        mgmtObject = 0;
    }
}

void BdbMessageStoreImpl::truncateInit(const bool saveStoreContent)
{
    if (isInit) {
        {
            qpid::sys::Mutex::ScopedLock sl(journalListLock);
            if (journalList.size()) { // check no queues exist
                std::ostringstream oss;
                oss << "truncateInit() called with " << journalList.size() << " queues still in existence";
                THROW_STORE_EXCEPTION(oss.str());
            }
        }
        closeDbs();
        dbs.clear();
        if (tplStorePtr->is_ready()) tplStorePtr->stop(true);
        dbenv->close(0);
        isInit = false;
    }
    std::ostringstream oss;
    oss << storeDir << "/" << storeTopLevelDir;
    if (saveStoreContent) {
        std::string dir = mrg::journal::jdir::push_down(storeDir, storeTopLevelDir, "cluster");
        QPID_LOG(notice, "Store directory " << oss.str() << " was pushed down (saved) into directory " << dir << ".");
    } else {
        mrg::journal::jdir::delete_dir(oss.str().c_str());
        QPID_LOG(notice, "Store directory " << oss.str() << " was truncated.");
    }
    init();
}

void BdbMessageStoreImpl::chkTplStoreInit()
{
    // Prevent multiple threads from late-initializing the TPL
    qpid::sys::Mutex::ScopedLock sl(tplInitLock);
    if (!tplStorePtr->is_ready()) {
        journal::jdir::create_dir(getTplBaseDir());
        //tplStorePtr->initialize(tplNumJrnlFiles, false, 0, tplJrnlFsizeSblks, tplWCacheNumPages, tplWCachePgSizeSblks)
        if (mgmtObject != 0) mgmtObject->set_tplIsInitialized(true);
    }
}

void BdbMessageStoreImpl::open(db_ptr db,
                           DbTxn* txn,
                           const char* file,
                           bool dupKey)
{
    if(dupKey) db->set_flags(DB_DUPSORT);
    db->open(txn, file, 0, DB_BTREE, DB_CREATE | DB_THREAD, 0);
}

void BdbMessageStoreImpl::closeDbs()
{
    for (std::list<db_ptr >::iterator i = dbs.begin(); i != dbs.end(); i++) {
        (*i)->close(0);
    }
    dbs.clear();
}

BdbMessageStoreImpl::~BdbMessageStoreImpl()
{
    finalize();
    try {
        closeDbs();
    } catch (const DbException& e) {
        QPID_LOG(error, "Error closing BDB databases: " <<  e.what());
    } catch (const journal::jexception& e) {
        QPID_LOG(error, "Error: " << e.what());
    } catch (const std::exception& e) {
        QPID_LOG(error, "Error: " << e.what());
    } catch (...) {
        QPID_LOG(error, "Unknown error in BdbMessageStoreImpl::~BdbMessageStoreImpl()");
    }

    if (mgmtObject != 0) {
        mgmtObject->resourceDestroy();
        mgmtObject = 0;
    }
}

void BdbMessageStoreImpl::create(PersistableQueue& queue,
                             const FieldTable&/* args*/)
{
    checkInit();
    if (queue.getPersistenceId()) {
        THROW_STORE_EXCEPTION("Queue already created: " + queue.getName());
    }
    JournalImpl* jQueue = 0;
    FieldTable::ValuePtr value;

    if (queue.getName().size() == 0)
    {
        QPID_LOG(error, "Cannot create store for empty (null) queue name - ignoring and attempting to continue.");
        return;
    }
    std::stringstream ss;
    ss << getBdbBaseDir() << getJrnlDir(queue);
    mrg::journal::jdir::create_dir(ss.str());
    jQueue = new JournalImpl(timer, queue.getName(), getJrnlDir(queue),getBdbBaseDir(),
                             defJournalGetEventsTimeout, defJournalFlushTimeout, agent,dbenv,
                             boost::bind(&BdbMessageStoreImpl::journalDeleted, this, _1));
    {
        qpid::sys::Mutex::ScopedLock sl(journalListLock);
        journalList[queue.getName()]=jQueue;
    }

    queue.setExternalQueueStore(dynamic_cast<ExternalQueueStore*>(jQueue));
    try {
        // init will create the deque's for the init...
        jQueue->initialize();
    } catch (const journal::jexception& e) {
        THROW_STORE_EXCEPTION(std::string("Queue ") + queue.getName() + ": create() failed: " + e.what());
    }
    try {
        if (!create(queueDb, queueIdSequence, queue)) {
            THROW_STORE_EXCEPTION("Queue already exists: " + queue.getName());
        }
    } catch (const DbException& e) {
        THROW_STORE_EXCEPTION_2("Error creating queue named  " + queue.getName(), e);
    }
}

void BdbMessageStoreImpl::destroy(PersistableQueue& queue)
{
    checkInit();
    destroy(queueDb, queue);
    deleteBindingsForQueue(queue);
    qpid::broker::ExternalQueueStore* eqs = queue.getExternalQueueStore();
    if (eqs) {
        JournalImpl* jQueue = static_cast<JournalImpl*>(eqs);
        jQueue->delete_jrnl_files();
        queue.setExternalQueueStore(0); // will delete the journal if exists
        {
            qpid::sys::Mutex::ScopedLock sl(journalListLock);
            journalList.erase(queue.getName());
        }
    }
}

void BdbMessageStoreImpl::create(const PersistableExchange& exchange,
                             const FieldTable& /*args*/)
{
    checkInit();
    if (exchange.getPersistenceId()) {
        THROW_STORE_EXCEPTION("Exchange already created: " + exchange.getName());
    }
    try {
        if (!create(exchangeDb, exchangeIdSequence, exchange)) {
            THROW_STORE_EXCEPTION("Exchange already exists: " + exchange.getName());
        }
    } catch (const DbException& e) {
        THROW_STORE_EXCEPTION_2("Error creating exchange named " + exchange.getName(), e);
    }
}

void BdbMessageStoreImpl::destroy(const PersistableExchange& exchange)
{
    checkInit();
    destroy(exchangeDb, exchange);
    //need to also delete bindings
    IdDbt key(exchange.getPersistenceId());
    bindingDb->del(0, &key, DB_AUTO_COMMIT);
}

void BdbMessageStoreImpl::create(const PersistableConfig& general)
{
    checkInit();
    if (general.getPersistenceId()) {
        THROW_STORE_EXCEPTION("General configuration item already created");
    }
    try {
        if (!create(generalDb, generalIdSequence, general)) {
            THROW_STORE_EXCEPTION("General configuration already exists");
        }
    } catch (const DbException& e) {
        THROW_STORE_EXCEPTION_2("Error creating general configuration", e);
    }
}

void BdbMessageStoreImpl::destroy(const PersistableConfig& general)
{
    checkInit();
    destroy(generalDb, general);
}

bool BdbMessageStoreImpl::create(db_ptr db,
                             IdSequence& seq,
                             const Persistable& p)
{
    u_int64_t id (seq.next());
    Dbt key(&id, sizeof(id));
    BufferValue value (p);

    int status;
    TxnCtxt txn;
    txn.begin(dbenv.get(), true);
    try {
        status = db->put(txn.get(), &key, &value, DB_NOOVERWRITE);
        txn.commit();
    } catch (...) {
        txn.abort();
        throw;
    }
    if (status == DB_KEYEXIST) {
        return false;
    } else {
        p.setPersistenceId(id);
        return true;
    }
}

void BdbMessageStoreImpl::destroy(db_ptr db, const Persistable& p)
{
    IdDbt key(p.getPersistenceId());
    db->del(0, &key, DB_AUTO_COMMIT);
}


void BdbMessageStoreImpl::bind(const PersistableExchange& e,
                           const PersistableQueue& q,
                           const std::string& k,
                           const FieldTable& a)
{
    checkInit();
    IdDbt key(e.getPersistenceId());
    BindingDbt value(e, q, k, a);
    TxnCtxt txn;
    txn.begin(dbenv.get(), true);
    try {
        put(bindingDb, txn.get(), key, value);
        txn.commit();
    } catch (...) {
        txn.abort();
        throw;
    }
}

void BdbMessageStoreImpl::unbind(const PersistableExchange& e,
                             const PersistableQueue& q,
                             const std::string& k,
                             const FieldTable&)
{
    checkInit();
    deleteBinding(e, q, k);
}

void BdbMessageStoreImpl::recover(RecoveryManager& registry)
{
    checkInit();
    txn_list prepared;
    recoverLockedMappings(prepared);

    queue_index queues;//id->queue
    exchange_index exchanges;//id->exchange
    message_index messages;//id->message

    TxnCtxt txn;
    txn.begin(dbenv.get(), false);
    try {
        //read all queues, calls recoversMessages
        recoverQueues(txn, registry, queues, prepared, messages);

        //recover exchange & bindings:
        recoverExchanges(txn, registry, exchanges);
        recoverBindings(txn, exchanges, queues);

        //recover general-purpose configuration
        recoverGeneral(txn, registry);

        txn.commit();
    } catch (const DbException& e) {
        txn.abort();
        THROW_STORE_EXCEPTION_2("Error on recovery", e);
    } catch (...) {
        txn.abort();
        throw;
    }
    
    //Delete transient message from recovered journal db
    for (std::list<JournalImpl*>::iterator it=this->recoveredJournal.begin();it!=recoveredJournal.end();it++) {
    	(*it)->discard_transient_message();
	if (compactFlag) {
		(*it)->compact_message_database();
	}
    }
    this->recoveredJournal.clear();    
    
    //recover transactions:
    //TODO: SUPPORT TRANSACTION
    /*for (txn_list::iterator i = prepared.begin(); i != prepared.end(); i++) {
        if (mgmtObject != 0) {
            mgmtObject->inc_tplTransactionDepth();
            mgmtObject->inc_tplTxnPrepares();
        }

        std::string xid = i->xid;

        // Restore data token state in TxnCtxt
        TplRecoverMapCitr citr = tplRecoverMap.find(xid);
        if (citr == tplRecoverMap.end()) THROW_STORE_EXCEPTION("XID not found in tplRecoverMap");

        // If a record is found that is dequeued but not committed/aborted from tplStore, then a complete() call
        // was interrupted part way through committing/aborting the impacted queues. Complete this process.
        bool incomplTplTxnFlag = citr->second.deq_flag;

        if (citr->second.tpc_flag) {
            // Dtx (2PC) transaction
            TPCTxnCtxt* tpcc = new TPCTxnCtxt(xid, &messageIdSequence);
            std::auto_ptr<TPCTransactionContext> txn(tpcc);
            tpcc->recoverDtok(citr->second.rid, xid);
            tpcc->prepare(tplStorePtr.get());

            RecoverableTransaction::shared_ptr dtx;
            if (!incomplTplTxnFlag) dtx = registry.recoverTransaction(xid, txn);
            if (i->enqueues.get()) {
                for (LockedMappings::iterator j = i->enqueues->begin(); j != i->enqueues->end(); j++) {
                    tpcc->addXidRecord(queues[j->first]->getExternalQueueStore());
                    if (!incomplTplTxnFlag) dtx->enqueue(queues[j->first], messages[j->second]);
                }
            }
            if (i->dequeues.get()) {
                for (LockedMappings::iterator j = i->dequeues->begin(); j != i->dequeues->end(); j++) {
                    tpcc->addXidRecord(queues[j->first]->getExternalQueueStore());
                    if (!incomplTplTxnFlag) dtx->dequeue(queues[j->first], messages[j->second]);
                }
            }

            if (incomplTplTxnFlag) {
                tpcc->complete(citr->second.commit_flag);
            }
        } else {
            // Local (1PC) transaction
            boost::shared_ptr<TxnCtxt> opcc(new TxnCtxt(xid, &messageIdSequence));
            opcc->recoverDtok(citr->second.rid, xid);
            opcc->prepare(tplStorePtr.get());

            if (i->enqueues.get()) {
                for (LockedMappings::iterator j = i->enqueues->begin(); j != i->enqueues->end(); j++) {
                    opcc->addXidRecord(queues[j->first]->getExternalQueueStore());
                }
            }
            if (i->dequeues.get()) {
                for (LockedMappings::iterator j = i->dequeues->begin(); j != i->dequeues->end(); j++) {
                    opcc->addXidRecord(queues[j->first]->getExternalQueueStore());
                }
            }
            if (incomplTplTxnFlag) {
                opcc->complete(citr->second.commit_flag);
            } else {
                completed(*opcc.get(), citr->second.commit_flag);
            }
        }
    }*/
    registry.recoveryComplete();
    QPID_LOG(info,"Restore complete!");
}

void BdbMessageStoreImpl::recoverQueues(TxnCtxt& txn,
                                    RecoveryManager& registry,
                                    queue_index& queue_index,
                                    txn_list& prepared,
                                    message_index& messages)
{
    Cursor queues;
    queues.open(queueDb, txn.get());

    u_int64_t maxQueueId(1);

    IdDbt key;
    Dbt value;
    //read all queues
    while (queues.next(key, value)) {
        Buffer buffer(reinterpret_cast<char*>(value.get_data()), value.get_size());
        //create a Queue instance
        RecoverableQueue::shared_ptr queue = registry.recoverQueue(buffer);
        //set the persistenceId and update max as required
        queue->setPersistenceId(key.id);

        const std::string queueName = queue->getName().c_str();
        JournalImpl* jQueue = 0;
        if (queueName.size() == 0)
        {
            QPID_LOG(error, "Cannot recover empty (null) queue name - ignoring and attempting to continue.");
            break;
        }
        jQueue = new JournalImpl(timer, queueName, getJrnlHashDir(queueName), getBdbBaseDir(),
                                 defJournalGetEventsTimeout, defJournalFlushTimeout,agent,dbenv,
                                 boost::bind(&BdbMessageStoreImpl::journalDeleted, this, _1));
        {
            qpid::sys::Mutex::ScopedLock sl(journalListLock);
            journalList[queueName] = jQueue;
        }
        queue->setExternalQueueStore(dynamic_cast<ExternalQueueStore*>(jQueue));
	recoveredJournal.push_back(jQueue);
	
        try
        {
            long rcnt = 0L;     // recovered msg count
            long idcnt = 0L;    // in-doubt msg count
            u_int64_t thisHighestRid = 0ULL;
            thisHighestRid=recoverMessages(registry, queue, prepared, messages, rcnt, idcnt);
	    if (highestRid == 0ULL)
                highestRid = thisHighestRid;
            else if (thisHighestRid - highestRid < 0x8000000000000000ULL) // RFC 1982 comparison for unsigned 64-bit
                highestRid = thisHighestRid;
            QPID_LOG(info, "Recovered queue \"" << queueName << "\": " << rcnt << " messages recovered; " << idcnt << " messages in-doubt.");
            //jQueue->recover_complete(); // start journal.
        } catch (const journal::jexception& e) {
            THROW_STORE_EXCEPTION(std::string("Queue ") + queueName + ": recoverQueues() failed: " + e.what());
        }
        //read all messages: done on a per queue basis if using Journal

        queue_index[key.id] = queue;
        maxQueueId = max(key.id, maxQueueId);
    }

    // NOTE: highestRid is set by both recoverQueues() and recoverTplStore() as
    // the messageIdSequence is used for both queue journals and the tpl journal.
    messageIdSequence.reset(highestRid + 1);
    QPID_LOG(info, "Most recent persistence id found: 0x" << std::hex << highestRid << std::dec);

    queueIdSequence.reset(maxQueueId + 1);

}


void BdbMessageStoreImpl::recoverExchanges(TxnCtxt& txn,
                                       RecoveryManager& registry,
                                       exchange_index& index)
{
    //TODO: this is a copy&paste from recoverQueues - refactor!
    Cursor exchanges;
    exchanges.open(exchangeDb, txn.get());

    u_int64_t maxExchangeId(1);
    IdDbt key;
    Dbt value;
    //read all exchanges
    while (exchanges.next(key, value)) {
        Buffer buffer(reinterpret_cast<char*>(value.get_data()), value.get_size());
        //create a Exchange instance
        RecoverableExchange::shared_ptr exchange = registry.recoverExchange(buffer);
        if (exchange) {
            //set the persistenceId and update max as required
            exchange->setPersistenceId(key.id);
            index[key.id] = exchange;
        }
        maxExchangeId = max(key.id, maxExchangeId);
    }
    exchangeIdSequence.reset(maxExchangeId + 1);
}

void BdbMessageStoreImpl::recoverBindings(TxnCtxt& txn,
                                      exchange_index& exchanges,
                                      queue_index& queues)
{
    Cursor bindings;
    bindings.open(bindingDb, txn.get());

    IdDbt key;
    Dbt value;
    while (bindings.next(key, value)) {
        Buffer buffer(reinterpret_cast<char*>(value.get_data()), value.get_size());
        if (buffer.available() < 8) {
            QPID_LOG(error, "Not enough data for binding: " << buffer.available());
            THROW_STORE_EXCEPTION("Not enough data for binding");
        }
        uint64_t queueId = buffer.getLongLong();
        std::string queueName;
        std::string routingkey;
        FieldTable args;
        buffer.getShortString(queueName);
        buffer.getShortString(routingkey);
        buffer.get(args);
        exchange_index::iterator exchange = exchanges.find(key.id);
        queue_index::iterator queue = queues.find(queueId);
        if (exchange != exchanges.end() && queue != queues.end()) {
            //could use the recoverable queue here rather than the name...
            exchange->second->bind(queueName, routingkey, args);
        } else {
            //stale binding, delete it
            QPID_LOG(warning, "Deleting stale binding");
            bindings->del(0);
        }
    }
}

void BdbMessageStoreImpl::recoverGeneral(TxnCtxt& txn,
                                     RecoveryManager& registry)
{
    Cursor items;
    items.open(generalDb, txn.get());

    u_int64_t maxGeneralId(1);
    IdDbt key;
    Dbt value;
    //read all items
    while (items.next(key, value)) {
        Buffer buffer(reinterpret_cast<char*>(value.get_data()), value.get_size());
        //create instance
        RecoverableConfig::shared_ptr config = registry.recoverConfig(buffer);
        //set the persistenceId and update max as required
        config->setPersistenceId(key.id);
        maxGeneralId = max(key.id, maxGeneralId);
    }
    generalIdSequence.reset(maxGeneralId + 1);
}

uint64_t BdbMessageStoreImpl::recoverMessages(qpid::broker::RecoveryManager& recovery,
                                      qpid::broker::RecoverableQueue::shared_ptr& queue,
                                      txn_list& /*prepared*/,
                                      message_index& /*messages*/,
                                      long& rcnt,
				      long& /*idcnt*/
				      )

{
	uint64_t maxRid=0ULL;
	try {
		size_t preambleLength = sizeof(u_int32_t)+sizeof(u_int8_t)/*header size + transient flag*/;
		JournalImpl* jc = static_cast<JournalImpl*>(queue->getExternalQueueStore());
		std::vector< std::pair <uint64_t,std::string> > recovered;
		std::vector< uint64_t > transientMsg;
		jc->recoverMessages(recovered);
		for (std::vector< std::pair <uint64_t,std::string> >::iterator it=recovered.begin();it<recovered.end();it++) {
			char* rawData= new char[it->second.size()];
			memcpy(rawData,it->second.data(),it->second.size());
			RecoverableMessage::shared_ptr msg;
			Buffer msgBuff(rawData,it->second.size());
			u_int8_t transientFlag=msgBuff.getOctet();
			if (!transientFlag) {
				u_int32_t headerSize=msgBuff.getLong();
				Buffer headerBuff(msgBuff.getPointer()+preambleLength,headerSize);
				msg = recovery.recoverMessage(headerBuff);
				msg->setPersistenceId(it->first);
				maxRid=max(it->first,maxRid);
				msg->setRedelivered();
				uint32_t contentOffset = headerSize + preambleLength;
				uint64_t contentSize = msgBuff.getSize() - contentOffset;
				if (msg->loadContent(contentSize)) {
				    //now read the content
				    Buffer contentBuff(msgBuff.getPointer() + contentOffset, contentSize);
				    msg->decodeContent(contentBuff);
				    rcnt++;
				    queue->recover(msg);
				}
			} else {
				transientMsg.push_back(it->first);
			}
			delete [] rawData;
		}
		jc->register_as_transient(transientMsg);
		transientMsg.clear();
		recovered.clear();
    	} catch (const journal::jexception& e) {
	        THROW_STORE_EXCEPTION(std::string("Queue ") + queue->getName() + ": recoverMessages() failed: " + e.what());
	}
	return maxRid;
}

RecoverableMessage::shared_ptr BdbMessageStoreImpl::getExternMessage(qpid::broker::RecoveryManager& /*recovery*/,
                                                                 uint64_t /*messageId*/,
                                                                 unsigned& /*headerSize*/)
{
    throw mrg::journal::jexception(mrg::journal::jerrno::JERR__NOTIMPL, "BdbMessageStoreImpl", "getExternMessage");
}

int BdbMessageStoreImpl::enqueueMessage(TxnCtxt& txn,
                                    IdDbt& msgId,
                                    RecoverableMessage::shared_ptr& msg,
                                    queue_index& index,
                                    txn_list& prepared,
                                    message_index& messages)
{
    Cursor mappings;
    mappings.open(mappingDb, txn.get());

    IdDbt value;

    int count(0);
    for (int status = mappings->get(&msgId, &value, DB_SET); status == 0; status = mappings->get(&msgId, &value, DB_NEXT_DUP)) {
        if (index.find(value.id) == index.end()) {
            QPID_LOG(warning, "Recovered message for queue that no longer exists");
            mappings->del(0);
        } else {
            RecoverableQueue::shared_ptr queue = index[value.id];
            if (PreparedTransaction::isLocked(prepared, value.id, msgId.id)) {
                messages[msgId.id] = msg;
            } else {
                queue->recover(msg);
            }
            count++;
        }
    }
    mappings.close();
    return count;
}

void BdbMessageStoreImpl::readTplStore()
{
//TODO: implement transaction
/*
    tplRecoverMap.clear();
    journal::txn_map& tmap = tplStorePtr->get_txn_map();
    DataTokenImpl dtok;
    void* dbuff = NULL; size_t dbuffSize = 0;
    void* xidbuff = NULL; size_t xidbuffSize = 0;
    bool transientFlag = false;
    bool externalFlag = false;
    bool done = false;
    try {
        unsigned aio_sleep_cnt = 0;
        while (!done) {
            dtok.reset();
            dtok.set_wstate(DataTokenImpl::ENQ);
            mrg::journal::iores res = tplStorePtr->read_data_record(&dbuff, dbuffSize, &xidbuff, xidbuffSize, transientFlag, externalFlag, &dtok);
            switch (res) {
              case mrg::journal::RHM_IORES_SUCCESS: {
                // Every TPL record contains both data and an XID
                assert(dbuffSize>0);
                assert(xidbuffSize>0);
                std::string xid(static_cast<const char*>(xidbuff), xidbuffSize);
                bool is2PC = *(static_cast<char*>(dbuff)) != 0;

                // Check transaction details; add to recover map
                journal::txn_data_list txnList = tmap.get_tdata_list(xid); //  txnList will be empty if xid not found
                if (!txnList.empty()) { // xid found in tmap
                    unsigned enqCnt = 0;
                    unsigned deqCnt = 0;
                    u_int64_t rid = 0;

                    // Assume commit (roll forward) in cases where only prepare has been called - ie only enqueue record exists.
                    // Note: will apply to both 1PC and 2PC transactions.
                    bool commitFlag = true;

                    for (journal::tdl_itr j = txnList.begin(); j<txnList.end(); j++) {
                        if (j->_enq_flag) {
                            rid = j->_rid;
                            enqCnt++;
                        } else {
                            commitFlag = j->_commit_flag;
                            deqCnt++;
                        }
                    }
                    assert(enqCnt == 1);
                    assert(deqCnt <= 1);
                    tplRecoverMap.insert(TplRecoverMapPair(xid, TplRecoverStruct(rid, deqCnt == 1, commitFlag, is2PC)));
                }

                ::free(xidbuff);
                aio_sleep_cnt = 0;
                break;
                }
              case mrg::journal::RHM_IORES_PAGE_AIOWAIT:
                if (++aio_sleep_cnt > MAX_AIO_SLEEPS)
                    THROW_STORE_EXCEPTION("Timeout waiting for AIO in BdbMessageStoreImpl::recoverTplStore()");
                ::usleep(AIO_SLEEP_TIME_US);
                break;
              case mrg::journal::RHM_IORES_EMPTY:
                done = true;
                break; // done with all messages. (add call in jrnl to test that _emap is empty.)
              default:
                std::ostringstream oss;
                oss << "readTplStore(): Unexpected result from journal read: " << mrg::journal::iores_str(res);
                THROW_STORE_EXCEPTION(oss.str());
            } // switch
        }
    } catch (const journal::jexception& e) {
        THROW_STORE_EXCEPTION(std::string("TPL recoverTplStore() failed: ") + e.what());
    }*/
}

void BdbMessageStoreImpl::recoverTplStore()
{
    //TODO: implement transaction
    /*if (journal::jdir::exists(tplStorePtr->jrnl_dir() + tplStorePtr->base_filename() + ".jinf")) {
        u_int64_t thisHighestRid = 0ULL;
        tplStorePtr->recover(tplNumJrnlFiles, false, 0, tplJrnlFsizeSblks, tplWCachePgSizeSblks, tplWCacheNumPages, 0, thisHighestRid, 0);
        if (highestRid == 0ULL)
            highestRid = thisHighestRid;
        else if (thisHighestRid - highestRid  < 0x8000000000000000ULL) // RFC 1982 comparison for unsigned 64-bit
            highestRid = thisHighestRid;

        // Load tplRecoverMap by reading the TPL store
        readTplStore();

        tplStorePtr->recover_complete(); // start journal.
    }*/
}

void BdbMessageStoreImpl::recoverLockedMappings(txn_list& /*txns*/)
{
    //TODO: implement transaction
    /*
    if (!tplStorePtr->is_ready())
        recoverTplStore();

    // Abort unprepared xids and populate the locked maps
    for (TplRecoverMapCitr i = tplRecoverMap.begin(); i != tplRecoverMap.end(); i++) {
        LockedMappings::shared_ptr enq_ptr;
        enq_ptr.reset(new LockedMappings);
        LockedMappings::shared_ptr deq_ptr;
        deq_ptr.reset(new LockedMappings);
        txns.push_back(new PreparedTransaction(i->first, enq_ptr, deq_ptr));
    }*/
}

void BdbMessageStoreImpl::collectPreparedXids(std::set<std::string>&/* xids*/)
{
    //TODO: implement transaction
    /*
    if (tplStorePtr->is_ready()) {
        tplStorePtr->read_reset();
        readTplStore();
    } else {
        recoverTplStore();
    }
    for (TplRecoverMapCitr i = tplRecoverMap.begin(); i != tplRecoverMap.end(); i++) {
        // Discard all txns that are to be rolled forward/back and 1PC transactions
        if (!i->second.deq_flag && i->second.tpc_flag)
            xids.insert(i->first);
    }*/
}

void BdbMessageStoreImpl::stage(const intrusive_ptr<PersistableMessage>& /*msg*/)
{
    throw mrg::journal::jexception(mrg::journal::jerrno::JERR__NOTIMPL, "BdbMessageStoreImpl", "stage");
}

void BdbMessageStoreImpl::destroy(PersistableMessage& /*msg*/)
{
    throw mrg::journal::jexception(mrg::journal::jerrno::JERR__NOTIMPL, "BdbMessageStoreImpl", "destroy");
}

void BdbMessageStoreImpl::appendContent(const intrusive_ptr<const PersistableMessage>& /*msg*/,
                                    const std::string& /*data*/)
{
    throw mrg::journal::jexception(mrg::journal::jerrno::JERR__NOTIMPL, "BdbMessageStoreImpl", "appendContent");
}

void BdbMessageStoreImpl::loadContent(const qpid::broker::PersistableQueue& queue,
                                  const intrusive_ptr<const PersistableMessage>& msg,
                                  std::string& data,
                                  u_int64_t offset,
                                  u_int32_t length)
{
    checkInit();
    u_int64_t messageId (msg->getPersistenceId());
    if (messageId != 0) {
        try {
            JournalImpl* jc = static_cast<JournalImpl*>(queue.getExternalQueueStore());
            if (jc && jc->is_enqueued(messageId) ) {
                if (!jc->loadMsgContent(messageId, data, length, offset)) {
                    std::ostringstream oss;
                    oss << "Queue " << queue.getName() << ": loadContent() failed: Message " << messageId << " is extern";
                    THROW_STORE_EXCEPTION(oss.str());
                }
            } else {
                std::ostringstream oss;
                oss << "Queue " << queue.getName() << ": loadContent() failed: Message " << messageId << " not enqueued";
                THROW_STORE_EXCEPTION(oss.str());
            }
        } catch (const journal::jexception& e) {
            THROW_STORE_EXCEPTION(std::string("Queue ") + queue.getName() + ": loadContent() failed: " + e.what());
        }
    } else {
        THROW_STORE_EXCEPTION("Cannot load content. Message not known to store!");
    }
}

void BdbMessageStoreImpl::flush(const qpid::broker::PersistableQueue& queue)
{
    if (queue.getExternalQueueStore() == 0) return;
    checkInit();
    std::string qn = queue.getName();
    try {
        JournalImpl* jc = static_cast<JournalImpl*>(queue.getExternalQueueStore());
        if (jc) {
            // TODO: check if this result should be used...
            //mrg::journal::iores res = 
	    				jc->flush();
        }
    } catch (const journal::jexception& e) {
        THROW_STORE_EXCEPTION(std::string("Queue ") + qn + ": flush() failed: " + e.what() );
    }
}

void BdbMessageStoreImpl::enqueue(TransactionContext* ctxt,
                              const intrusive_ptr<PersistableMessage>& msg,
                              const PersistableQueue& queue)
{
    checkInit();
    u_int64_t queueId (queue.getPersistenceId());
    u_int64_t messageId (msg->getPersistenceId());
    if (queueId == 0) {
        THROW_STORE_EXCEPTION("Queue not created: " + queue.getName());
    }

    TxnCtxt implicit;
    TxnCtxt* txn = 0;
    if (ctxt) {
        txn = check(ctxt);
    } else {
        txn = &implicit;
    }

    bool newId = false;
    if (messageId == 0) {
        messageId = messageIdSequence.next();
        msg->setPersistenceId(messageId);
        newId = true;
    }
    store(&queue, txn, msg, newId);

    // add queue* to the txn map..
    if (ctxt) txn->addXidRecord(queue.getExternalQueueStore());
}

u_int64_t BdbMessageStoreImpl::msgEncode(std::vector<char>& buff, const intrusive_ptr<PersistableMessage>& message)
{
    u_int32_t headerSize = message->encodedHeaderSize();
    u_int64_t size = message->encodedSize() + sizeof(u_int32_t)+ sizeof(u_int8_t);
    try { buff = std::vector<char>(size); } // byte (transient flag) + long(header size) + headers + content
    catch (const std::exception& e) {
        std::ostringstream oss;
        oss << "Unable to allocate memory for encoding message; requested size: " << size << "; error: " << e.what();
        THROW_STORE_EXCEPTION(oss.str());
    }
    Buffer buffer(&buff[0],size);
    buffer.putOctet(message->isPersistent()?0:1);
    buffer.putLong(headerSize);
    message->encode(buffer);
    return size;
}

void BdbMessageStoreImpl::store(const PersistableQueue* queue,
                            TxnCtxt* txn,
                            const intrusive_ptr<PersistableMessage>& message,
                            bool /*newId*/)
{
    std::vector<char> buff;
    u_int64_t size = msgEncode(buff, message);

    try {
        if (queue) {
            uint64_t pid=message->getPersistenceId();
            JournalImpl* jc = static_cast<JournalImpl*>(queue->getExternalQueueStore());
	    //bdbserv.dispatch(boost::bind(&JournalImpl::enqueue_data,jc,&buff[0],size,size,pid,txn->getXid(),!message->isPersistent()));

            jc->enqueue_data(&buff[0], size, size, pid,txn->getXid(), !message->isPersistent());
        } else {
            THROW_STORE_EXCEPTION(std::string("BdbMessageStoreImpl::store() failed: queue NULL."));
       }
    } catch (const journal::jexception& e) {
        THROW_STORE_EXCEPTION(std::string("Queue ") + queue->getName() + ": BdbMessageStoreImpl::store() failed: " +
                              e.what());
    }
}

void BdbMessageStoreImpl::dequeue(TransactionContext* ctxt,
                              const intrusive_ptr<PersistableMessage>& msg,
                              const PersistableQueue& queue)
{
    checkInit();
    u_int64_t queueId (queue.getPersistenceId());
    u_int64_t messageId (msg->getPersistenceId());
    if (messageId == 0) {
        THROW_STORE_EXCEPTION("Error dequeuing message, persistence id not set");
    }
    if (queueId == 0) {
        THROW_STORE_EXCEPTION("Queue not created: " + queue.getName());
    }

    TxnCtxt implicit;
    TxnCtxt* txn = 0;
    if (ctxt) {
        txn = check(ctxt);
    } else {
        txn = &implicit;
    }

    // add queue* to the txn map..
    if (ctxt) txn->addXidRecord(queue.getExternalQueueStore());
    async_dequeue(ctxt, msg, queue);

    msg->dequeueComplete();
}

void BdbMessageStoreImpl::async_dequeue(TransactionContext* ctxt,
                                    const intrusive_ptr<PersistableMessage>& msg,
                                    const PersistableQueue& queue)
{
    std::string tid;
    if (ctxt) {
        TxnCtxt* txn = check(ctxt);
        tid = txn->getXid();
    }
    try {
        JournalImpl* jc = static_cast<JournalImpl*>(queue.getExternalQueueStore());
        //jc->dequeue_data(msg->getPersistenceId(), tid);
	bdbserv.dispatch(boost::bind(&JournalImpl::dequeue_data,jc,msg->getPersistenceId(),tid,false));
    } catch (const journal::jexception& e) {
        THROW_STORE_EXCEPTION(std::string("Queue ") + queue.getName() + ": async_dequeue() failed: " + e.what());
    }
}

u_int32_t BdbMessageStoreImpl::outstandingQueueAIO(const qpid::broker::PersistableQueue& /*queue*/)
{
    checkInit();
    return 0;
}

void BdbMessageStoreImpl::completed(TxnCtxt& /*txn*/, bool /*commit*/)
{
   //TODO: implement transaction
   /*
    try {
        chkTplStoreInit(); // Late initialize (if needed)

        // Nothing to do if not prepared
        if (txn.getDtok()->is_enqueued()) {
            txn.incrDtokRef();
            DataTokenImpl* dtokp = txn.getDtok();
            dtokp->set_dequeue_rid(dtokp->rid());
            dtokp->set_rid(messageIdSequence.next());
            tplStorePtr->dequeue_txn_data_record(txn.getDtok(), txn.getXid(), commit);
        }
        txn.complete(commit);
        if (mgmtObject != 0) {
            mgmtObject->dec_tplTransactionDepth();
            if (commit)
                mgmtObject->inc_tplTxnCommits();
            else
                mgmtObject->inc_tplTxnAborts();
        }
    } catch (const std::exception& e) {
        QPID_LOG(error, "Error completing xid " << txn.getXid() << ": " << e.what());
        throw;
    }*/
}

auto_ptr<TransactionContext> BdbMessageStoreImpl::begin()
{
    throw mrg::journal::jexception(mrg::journal::jerrno::JERR__NOTIMPL, "BdbMessageStoreImpl", "begin");
    /*TODO: implement transaction
    checkInit();
    // pass sequence number for c/a
    return auto_ptr<TransactionContext>(new TxnCtxt(&messageIdSequence));*/
}

std::auto_ptr<qpid::broker::TPCTransactionContext> BdbMessageStoreImpl::begin(const std::string& /*xid*/)
{
    throw mrg::journal::jexception(mrg::journal::jerrno::JERR__NOTIMPL, "BdbMessageStoreImpl", "begin");
    /*TODO: implement transaction
    checkInit();
    IdSequence* jtx = &messageIdSequence;
    // pass sequence number for c/a
    return auto_ptr<TPCTransactionContext>(new TPCTxnCtxt(xid, jtx));*/
}

void BdbMessageStoreImpl::prepare(qpid::broker::TPCTransactionContext& /*ctxt*/)
{
    throw mrg::journal::jexception(mrg::journal::jerrno::JERR__NOTIMPL, "BdbMessageStoreImpl", "prepare");
    /*TODO: implement transaction
    checkInit();
    TxnCtxt* txn = dynamic_cast<TxnCtxt*>(&ctxt);
    if(!txn) throw InvalidTransactionContextException();
    localPrepare(txn);
    */
}

void BdbMessageStoreImpl::localPrepare(TxnCtxt* /*ctxt*/)
{
    //TODO: implement transaction
    /*
    try {
        chkTplStoreInit(); // Late initialize (if needed)

        // This sync is required to ensure multi-queue atomicity - ie all txn data
        // must hit the disk on *all* queues before the TPL prepare (enq) is written.
        ctxt->sync();

        ctxt->incrDtokRef();
        DataTokenImpl* dtokp = ctxt->getDtok();
        dtokp->set_external_rid(true);
        dtokp->set_rid(messageIdSequence.next());
        char tpcFlag = static_cast<char>(ctxt->isTPC());
        tplStorePtr->enqueue_txn_data_record(&tpcFlag, sizeof(char), sizeof(char), dtokp, ctxt->getXid(), false);
        ctxt->prepare(tplStorePtr.get());
        // make sure all the data is written to disk before returning
        ctxt->sync();
        if (mgmtObject != 0) {
            mgmtObject->inc_tplTransactionDepth();
            mgmtObject->inc_tplTxnPrepares();
        }
    } catch (const std::exception& e) {
        QPID_LOG(error, "Error preparing xid " << ctxt->getXid() << ": " << e.what());
        throw;
    }*/
}

void BdbMessageStoreImpl::commit(TransactionContext& /*ctxt*/)
{
    throw mrg::journal::jexception(mrg::journal::jerrno::JERR__NOTIMPL, "BdbMessageStoreImpl", "commit");
    /*TODO: implement transaction
    checkInit();
    TxnCtxt* txn(check(&ctxt));
    if (!txn->isTPC()) {
        if (txn->impactedQueuesEmpty()) return;
        localPrepare(dynamic_cast<TxnCtxt*>(txn));
    }
    completed(*dynamic_cast<TxnCtxt*>(txn), true);
    */
}

void BdbMessageStoreImpl::abort(TransactionContext& /*ctxt*/)
{
    throw mrg::journal::jexception(mrg::journal::jerrno::JERR__NOTIMPL, "BdbMessageStoreImpl", "abort");
    /* TODO: implement transaction
    checkInit();
    TxnCtxt* txn(check(&ctxt));
    if (!txn->isTPC()) {
        if (txn->impactedQueuesEmpty()) return;
        localPrepare(dynamic_cast<TxnCtxt*>(txn));
    }
    completed(*dynamic_cast<TxnCtxt*>(txn), false);*/
}

TxnCtxt* BdbMessageStoreImpl::check(TransactionContext* ctxt)
{
    TxnCtxt* txn = dynamic_cast<TxnCtxt*>(ctxt);
    if(!txn) throw InvalidTransactionContextException();
    return txn;
}

void BdbMessageStoreImpl::put(db_ptr db,
                          DbTxn* txn,
                          Dbt& key,
                          Dbt& value)
{
    try {
        int status = db->put(txn, &key, &value, DB_NODUPDATA);
        if (status == DB_KEYEXIST) {
            THROW_STORE_EXCEPTION("duplicate data");
        } else if (status) {
            THROW_STORE_EXCEPTION(DbEnv::strerror(status));
        }
    } catch (const DbException& e) {
        THROW_STORE_EXCEPTION(e.what());
    }
}

void BdbMessageStoreImpl::deleteBindingsForQueue(const PersistableQueue& queue)
{
    TxnCtxt txn;
    txn.begin(dbenv.get(), true);
    try {
        {
            Cursor bindings;
            bindings.open(bindingDb, txn.get());

            IdDbt key;
            Dbt value;
            while (bindings.next(key, value)) {
                Buffer buffer(reinterpret_cast<char*>(value.get_data()), value.get_size());
                if (buffer.available() < 8) {
                    THROW_STORE_EXCEPTION("Not enough data for binding");
                }
                uint64_t queueId = buffer.getLongLong();
                if (queue.getPersistenceId() == queueId) {
                    bindings->del(0);
                    QPID_LOG(debug, "Deleting binding for " << queue.getName() << " " << key.id << "->" << queueId);
                }
            }
        }
        txn.commit();
    } catch (const std::exception& e) {
        txn.abort();
        THROW_STORE_EXCEPTION_2("Error deleting bindings", e.what());
    } catch (...) {
        txn.abort();
        throw;
    }
    QPID_LOG(debug, "Deleted all bindings for " << queue.getName() << ":" << queue.getPersistenceId());
}

void BdbMessageStoreImpl::deleteBinding(const PersistableExchange& exchange,
                                    const PersistableQueue& queue,
                                    const std::string& bkey)
{
    TxnCtxt txn;
    txn.begin(dbenv.get(), true);
    try {
        {
            Cursor bindings;
            bindings.open(bindingDb, txn.get());

            IdDbt key(exchange.getPersistenceId());
            Dbt value;

            for (int status = bindings->get(&key, &value, DB_SET); status == 0; status = bindings->get(&key, &value, DB_NEXT_DUP)) {
                Buffer buffer(reinterpret_cast<char*>(value.get_data()), value.get_size());
                if (buffer.available() < 8) {
                    THROW_STORE_EXCEPTION("Not enough data for binding");
                }
                uint64_t queueId = buffer.getLongLong();
                if (queue.getPersistenceId() == queueId) {
                    std::string q;
                    std::string k;
                    buffer.getShortString(q);
                    buffer.getShortString(k);
                    if (bkey == k) {
                        bindings->del(0);
                        QPID_LOG(debug, "Deleting binding for " << queue.getName() << " " << key.id << "->" << queueId);
                    }
                }
            }
        }
        txn.commit();
    } catch (const std::exception& e) {
        txn.abort();
        THROW_STORE_EXCEPTION_2("Error deleting bindings", e.what());
    } catch (...) {
        txn.abort();
        throw;
    }
}

std::string BdbMessageStoreImpl::getJrnlBaseDir()
{
    std::ostringstream dir;
    //dir << getBdbBaseDir() << "jrnl/" ;
    dir << "jrnl/";
    return dir.str();
}

std::string BdbMessageStoreImpl::getBdbBaseDir()
{
    std::ostringstream dir;
    dir << storeDir << "/" << storeTopLevelDir << "/dat/" ;
    return dir.str();
}

std::string BdbMessageStoreImpl::getTplBaseDir()
{
    std::ostringstream dir;
    dir << storeDir << "/" << storeTopLevelDir << "/tpl/" ;
    return dir.str();
}

std::string BdbMessageStoreImpl::getJrnlDir(const qpid::broker::PersistableQueue& queue) //for exmaple /var/rhm/ + queueDir/
{
    return getJrnlHashDir(queue.getName().c_str());
}

u_int32_t BdbMessageStoreImpl::bHash(const std::string str)
{
    // Daniel Bernstein hash fn
    u_int32_t h = 0;
    for (std::string::const_iterator i = str.begin(); i < str.end(); i++)
        h = 33*h + *i;
    return h;
}

std::string BdbMessageStoreImpl::getJrnlHashDir(const std::string& queueName) //for exmaple /var/rhm/ + queueDir/
{
    std::stringstream dir;
    dir << getJrnlBaseDir() << std::hex << std::setfill('0') << std::setw(4);
    dir << (bHash(queueName.c_str()) % 29); // Use a prime number for better distribution across dirs
    dir << "/" << queueName << "/";
    return dir.str();
}

std::string BdbMessageStoreImpl::getStoreDir() const { return storeDir; }

void BdbMessageStoreImpl::journalDeleted(JournalImpl& j) {
    qpid::sys::Mutex::ScopedLock sl(journalListLock);
    journalList.erase(j.id());
}

BdbMessageStoreImpl::StoreOptions::StoreOptions(const std::string& name) :
                                             qpid::Options(name),
                                             truncateFlag(defTruncateFlag),
					     compactFlag(true)
{
    addOptions()
        ("store-dir", qpid::optValue(storeDir, "DIR"),
                "Store directory location for persistence (instead of using --data-dir value). "
                "Required if --no-data-dir is also used.")
        ("truncate", qpid::optValue(truncateFlag, "yes|no"),
                "If yes|true|1, will truncate the store (discard any existing records). If no|false|0, will preserve "
                "the existing store files for recovery.")
	("bdbstore-compact",qpid::optValue(compactFlag,"yes|no"),
		"If yes|true|1, will compact the store and return bdb pages to filesystem. The compaction will be done at the end of the "
		"queue recovery phase. NOTE: for big databases this operation may take a long time to end.")
        ;
}

