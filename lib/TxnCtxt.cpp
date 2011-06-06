#include "TxnCtxt.h"

#include <sstream>

#include "jrnl/jexception.hpp"
#include "StoreException.h"

namespace qpid {
namespace store {
namespace bdb {

void TxnCtxt::completeTxn(bool commit) {
    sync();
    for (ipqItr i = impactedQueues.begin(); i != impactedQueues.end(); i++) {
        commitTxn(static_cast<JournalImpl*>(*i), commit);
    }
    impactedQueues.clear();
    if (preparedXidStorePtr)
        commitTxn(preparedXidStorePtr, commit);
}

void TxnCtxt::commitTxn(JournalImpl* jc, bool commit) {
    if (jc && loggedtx) { /* if using journal */
        /*boost::intrusive_ptr<DataTokenImpl> dtokp(new DataTokenImpl);
        dtokp->addRef();
        dtokp->set_external_rid(true);
        dtokp->set_rid(loggedtx->next());*/
        try {
            if (commit) {
                jc->txn_commit(getXid());
                sync();
            } else {
                jc->txn_abort(getXid());
            }
        } catch (const mrg::journal::jexception& e) {
            THROW_STORE_EXCEPTION(std::string("Error commit") + e.what());
        }
    }
}

// static
uuid_t TxnCtxt::uuid;

// static
IdSequence TxnCtxt::uuidSeq;

// static
bool TxnCtxt::staticInit = TxnCtxt::setUuid();

// static
bool TxnCtxt::setUuid() {
    ::uuid_generate(uuid);
    return true;
}

TxnCtxt::TxnCtxt(IdSequence* _loggedtx) : loggedtx(_loggedtx), preparedXidStorePtr(0), txn(0) {
    if (loggedtx) {
//         // Human-readable tid: 53 bytes
//         // uuit_t is a char[16]
//         tid.reserve(53);
//         u_int64_t* u1 = (u_int64_t*)uuid;
//         u_int64_t* u2 = (u_int64_t*)(uuid + sizeof(u_int64_t));
//         std::stringstream s;
//         s << "tid:" << std::hex << std::setfill('0') << std::setw(16) << uuidSeq.next() << ":" << std::setw(16) << *u1 << std::setw(16) << *u2;
//         tid.assign(s.str());

        // Binary tid: 24 bytes
        tid.reserve(24);
        u_int64_t c = uuidSeq.next();
        tid.append((char*)&c, sizeof(c));
        tid.append((char*)&uuid, sizeof(uuid));
    }
}

TxnCtxt::TxnCtxt(std::string _tid, IdSequence* _loggedtx) : loggedtx(_loggedtx),preparedXidStorePtr(0), tid(_tid), txn(0) {}

TxnCtxt::~TxnCtxt() { abort(); }

void TxnCtxt::sync() {
    if (loggedtx) {
        try {
            for (ipqItr i = impactedQueues.begin(); i != impactedQueues.end(); i++)
                jrnl_flush(static_cast<JournalImpl*>(*i));
            if (preparedXidStorePtr)
                jrnl_flush(preparedXidStorePtr);
            for (ipqItr i = impactedQueues.begin(); i != impactedQueues.end(); i++)
                jrnl_sync(static_cast<JournalImpl*>(*i));
            if (preparedXidStorePtr)
                jrnl_sync(preparedXidStorePtr);
        } catch (const mrg::journal::jexception& e) {
            THROW_STORE_EXCEPTION(std::string("Error during txn sync: ") + e.what());
        }
    }
}

void TxnCtxt::jrnl_flush(JournalImpl* jc) {
    if (jc && !(jc->is_txn_synced(getXid())))
        jc->flush();
}

void TxnCtxt::jrnl_sync(JournalImpl* jc) {
    if (!jc || jc->is_txn_synced(getXid())) {
        return;
    }
}

void TxnCtxt::begin(DbEnv* env, bool sync) {
    int err;
    try { err = env->txn_begin(0, &txn, 0); }
    catch (const DbException&) { txn = 0; throw; }
    if (err != 0) {
        std::ostringstream oss;
        oss << "Error: Env::txn_begin() returned error code: " << err;
        THROW_STORE_EXCEPTION(oss.str());
    }
    if (sync)
        globalHolder = AutoScopedLock(new qpid::sys::Mutex::ScopedLock(globalSerialiser));
}

void TxnCtxt::commit() {
    if (txn) {
        txn->commit(0);
        txn = 0;
        globalHolder.reset();
    }
}

void TxnCtxt::abort(){
    if (txn) {
        txn->abort();
        txn = 0;
        globalHolder.reset();
    }
}

DbTxn* TxnCtxt::get() { return txn; }

bool TxnCtxt::isTPC() { return false; }

const std::string& TxnCtxt::getXid() { return tid; }

void TxnCtxt::addXidRecord(qpid::broker::ExternalQueueStore* queue) { impactedQueues.insert(queue); }

void TxnCtxt::complete(bool commit) { completeTxn(commit); }

bool TxnCtxt::impactedQueuesEmpty() { return impactedQueues.empty(); }

TPCTxnCtxt::TPCTxnCtxt(const std::string& _xid, IdSequence* _loggedtx) : TxnCtxt(_loggedtx), xid(_xid) {}

}}}
