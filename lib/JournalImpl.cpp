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

#include "JournalImpl.h"

#include "jrnl/jerrno.hpp"
#include "jrnl/jexception.hpp"
#include "qpid/log/Statement.h"
#include "jrnl/jdir.hpp"
#include "qpid/management/ManagementAgent.h"
#include "qmf/com/redhat/rhm/store/EventCreated.h"
#include "qmf/com/redhat/rhm/store/EventEnqThresholdExceeded.h"
#include "qmf/com/redhat/rhm/store/EventFull.h"
#include "qmf/com/redhat/rhm/store/EventRecovered.h"
#include "qpid/sys/Monitor.h"
#include "qpid/sys/Timer.h"
#include "qpid/framing/Buffer.h"
#include "StoreException.h"
#include "Cursor.h"
#include "TxnCtxt.h"

#include <vector>
#include <utility>

using namespace mrg::msgstore;
using namespace mrg::journal;
using qpid::management::ManagementAgent;

namespace _qmf = qmf::com::redhat::rhm::store;

InactivityFireEvent::InactivityFireEvent(JournalImpl* p, const qpid::sys::Duration timeout):
    qpid::sys::TimerTask(timeout, "JournalInactive:"+p->id()), _parent(p) {}

void InactivityFireEvent::fire() { qpid::sys::Mutex::ScopedLock sl(_ife_lock); if (_parent) _parent->flushFire(); }

GetEventsFireEvent::GetEventsFireEvent(JournalImpl* p, const qpid::sys::Duration timeout):
    qpid::sys::TimerTask(timeout, "JournalGetEvents:"+p->id()), _parent(p) {}

void GetEventsFireEvent::fire() { qpid::sys::Mutex::ScopedLock sl(_gefe_lock); if (_parent) _parent->getEventsFire(); }

JournalImpl::JournalImpl(qpid::sys::Timer& timer_,
                         const std::string& journalId,
                         const std::string& journalDirectory,
                         const std::string& bdbDir,
                         const qpid::sys::Duration getEventsTimeout,
                         const qpid::sys::Duration flushTimeout,
                         qpid::management::ManagementAgent* a,
			 boost::shared_ptr<DbEnv>& de,
                         DeleteCallback onDelete)
			 :
                         timer(timer_),
                         getEventsTimerSetFlag(false),
                         writeActivityFlag(false),
                         flushTriggeredFlag(true),
			 _is_init(false),
			 dbEnv(de),
			 journalName(journalId),
			 journalDirectory(journalDirectory),
			 bdbDir(bdbDir),
                         _agent(a),
                         _mgmtObject(0),
                         deleteCallback(onDelete)
{
    getEventsFireEventsPtr = new GetEventsFireEvent(this, getEventsTimeout);
    inactivityFireEventPtr = new InactivityFireEvent(this, flushTimeout);
    {
        timer.start();
        timer.add(inactivityFireEventPtr);
    }

    if (_agent != 0)
    {
        _mgmtObject = new _qmf::Journal
            (_agent, (qpid::management::Manageable*) this);

        _mgmtObject->set_name(journalId);
        _mgmtObject->set_directory(journalDirectory);
        _mgmtObject->set_bdbDir(bdbDir);

        // The following will be set on initialize(), but being properties, these must be set to 0 in the meantime

        _agent->addObject(_mgmtObject, 0, true);
    }

    log(LOG_NOTICE, "Created");
    std::ostringstream oss;
    oss << "Journal directory = \"" << journalDirectory << "\"; Bdb Directory = \"" << bdbDir << "\"";
    log(LOG_DEBUG, oss.str());
}

JournalImpl::~JournalImpl()
{
    if (deleteCallback) deleteCallback(*this);
    /*if (_init_flag && !_stop_flag){
    	try { stop(true); } // NOTE: This will *block* until all outstanding disk aio calls are complete!
        catch (const jexception& e) { log(LOG_ERROR, e.what()); }
	}*/
    getEventsFireEventsPtr->cancel();
    inactivityFireEventPtr->cancel();

    if (_mgmtObject != 0) {
        _mgmtObject->resourceDestroy();
        _mgmtObject = 0;
    }
    if(messageDb.get()) {
	    messageDb->close(0);
    } else {
    	log(LOG_DEBUG,"Null message store");
    }
    log(LOG_DEBUG, "Destroyed");
}

void JournalImpl::delete_jrnl_files() {
	if (messageDb.get()) {
		messageDb->close(0);
		std::stringstream ss;
		ss <<this->journalDirectory<<this->journalName<<".db";
		dbEnv->dbremove(0,ss.str().c_str(),0,DB_AUTO_COMMIT);
		QPID_LOG(debug,"Message database "+ss.str()+"destroyed");
		messageDb.reset();
	}
	std::stringstream ss;
	ss << bdbDir << journalDirectory;
	mrg::journal::jdir::delete_dir(ss.str());
}

void JournalImpl::init_message_db() {
    // Databases are constructed here instead of the constructor so that the DB_RECOVER flag can be used
    // against the database environment. Recover can only be performed if no databases have been created
    // against the environment at the time of recovery, as recovery invalidates the environment.
    messageDb.reset(new Db(dbEnv.get(), 0));

    messageDb->set_bt_compare(&bt_compare_func);

    std::stringstream ss;
    ss << this->journalDirectory;
    ss << this->journalName<<".db";
    
    TxnCtxt txn;
    txn.begin(dbEnv.get(), false);            
    try {
    	messageDb->open(txn.get(),ss.str().c_str(), 0, DB_BTREE, DB_CREATE | DB_THREAD, 0);
	txn.commit();
	QPID_LOG(debug,"Message database opened at "+ss.str());
    } catch (...) { txn.abort(); throw; }

    _is_init=true;
}

void
JournalImpl::initialize()
{
    this->init_message_db();
    
    log(LOG_DEBUG, "Initialization complete");

    if (_mgmtObject != 0)
    {
        /*_mgmtObject->set_initialFileCount(_lpmgr.num_jfiles());
        _mgmtObject->set_autoExpand(_lpmgr.is_ae());
        _mgmtObject->set_currentFileCount(_lpmgr.num_jfiles());
        _mgmtObject->set_maxFileCount(_lpmgr.ae_max_jfiles());
        _mgmtObject->set_dataFileSize(_jfsize_sblks * JRNL_SBLK_SIZE * JRNL_DBLK_SIZE);
        _mgmtObject->set_writePageSize(wcache_pgsize_sblks * JRNL_SBLK_SIZE * JRNL_DBLK_SIZE);
        _mgmtObject->set_writePages(wcache_num_pages);*/
    }
    if (_agent != 0)
        _agent->raiseEvent(qmf::com::redhat::rhm::store::EventCreated(journalName),qpid::management::ManagementAgent::SEV_NOTE);
    
}

void
JournalImpl::recover(/*const u_int16_t num_jfiles,
                     const bool auto_expand,
                     const u_int16_t ae_max_jfiles,
                     const u_int32_t jfsize_sblks,
                     const u_int16_t wcache_num_pages,
                     const u_int32_t wcache_pgsize_sblks,
                     mrg::journal::aio_callback* const cbp,
                     boost::ptr_list<msgstore::PreparedTransaction>* prep_tx_list_ptr,*/
                     u_int64_t& highest_rid,
                     u_int64_t queue_id)
{
    std::ostringstream oss1;
    oss1 << " queue_id = 0x" << std::hex << queue_id << std::dec;
    log(LOG_DEBUG, oss1.str());

    if (_mgmtObject != 0)
    {
        /*_mgmtObject->set_initialFileCount(_lpmgr.num_jfiles());
        _mgmtObject->set_autoExpand(_lpmgr.is_ae());
        _mgmtObject->set_currentFileCount(_lpmgr.num_jfiles());
        _mgmtObject->set_maxFileCount(_lpmgr.ae_max_jfiles());
        _mgmtObject->set_dataFileSize(_jfsize_sblks * JRNL_SBLK_SIZE * JRNL_DBLK_SIZE);
        _mgmtObject->set_writePageSize(wcache_pgsize_sblks * JRNL_SBLK_SIZE * JRNL_DBLK_SIZE);
        _mgmtObject->set_writePages(wcache_num_pages);*/
    }

    //TODO:implement transaction
    /*if (prep_tx_list_ptr) {
        // Create list of prepared xids
        std::vector<std::string> prep_xid_list;
        for (msgstore::PreparedTransaction::list::iterator i = prep_tx_list_ptr->begin(); i != prep_tx_list_ptr->end(); i++) {
            prep_xid_list.push_back(i->xid);
        }

        jcntl::recover(num_jfiles, auto_expand, ae_max_jfiles, jfsize_sblks, wcache_num_pages, wcache_pgsize_sblks,
                cbp, &prep_xid_list, highest_rid);
    } else {
        jcntl::recover(num_jfiles, auto_expand, ae_max_jfiles, jfsize_sblks, wcache_num_pages, wcache_pgsize_sblks,
                cbp, 0, highest_rid);
    }

    // Populate PreparedTransaction lists from _tmap
    if (prep_tx_list_ptr)
    {
        for (msgstore::PreparedTransaction::list::iterator i = prep_tx_list_ptr->begin(); i != prep_tx_list_ptr->end(); i++) {
            txn_data_list tdl = _tmap.get_tdata_list(i->xid); // tdl will be empty if xid not found
            for (tdl_itr tdl_itr = tdl.begin(); tdl_itr < tdl.end(); tdl_itr++) {
                if (tdl_itr->_enq_flag) { // enqueue op
                    i->enqueues->add(queue_id, tdl_itr->_rid);
                } else { // dequeue op
                    i->dequeues->add(queue_id, tdl_itr->_drid);
                }
            }
        }
    }*/
    std::ostringstream oss2;
    oss2 << "Recover phase 1 complete; highest rid found = 0x" << std::hex << highest_rid;
    //oss2 << std::dec << "; emap.size=" << _emap.size() << "; tmap.size=" << _tmap.size();
    oss2 << "; journal now read-only.";
    log(LOG_DEBUG, oss2.str());

    if (_mgmtObject != 0)
    {
     /*   _mgmtObject->inc_recordDepth(_emap.size());
        _mgmtObject->inc_enqueues(_emap.size());
        _mgmtObject->inc_txn(_tmap.size());
        _mgmtObject->inc_txnEnqueues(_tmap.enq_cnt());
        _mgmtObject->inc_txnDequeues(_tmap.deq_cnt());*/
    }
}

void JournalImpl::recoverMessages(std::vector< std::pair < uint64_t,std::string> >&recovered) {
	this->init_message_db();
	Cursor messages;
	messages.open(messageDb, 0); //Open cursor outside transaction	
	IdDbt key;
    	Dbt value;
    	//read all messages
	while (messages.next(key, value)) {
		std::string buffer(reinterpret_cast<char*>(value.get_data()),value.get_size());
		recovered.push_back(std::pair < uint64_t ,std::string >(key.id,buffer));
	}
	messages.close();
}

void
JournalImpl::recover_complete()
{
    discard_transient_message();
    log(LOG_DEBUG, "Recover phase 2 complete; journal now writable.");
    if (_agent != 0)
        _agent->raiseEvent(qmf::com::redhat::rhm::store::EventRecovered(journalName),qpid::management::ManagementAgent::SEV_NOTE);
}

//#define MAX_AIO_SLEEPS 1000000 // tot: ~10 sec
//#define AIO_SLEEP_TIME_US   10 // 0.01 ms
// Return true if content is recovered from store; false if content is external and must be recovered from an external store.
// Throw exception for all errors.
bool
JournalImpl::loadMsgContent(u_int64_t rid, std::string& data, size_t length, size_t offset)
{
	size_t preambleLength = sizeof(u_int32_t)+sizeof(u_int8_t)/*header size + transient flag*/;
	Dbc* messages;
	messageDb->cursor(0,&messages,0);	
	u_int64_t id(rid);	
	Dbt key(&id, sizeof(id));
    	Dbt value;
	int dbErr=messages->get(&key,&value,DB_SET);
	messages->close();
	if(dbErr) return false;
	qpid::framing::Buffer msgBuff(reinterpret_cast<char*>(value.get_data()),value.get_size());
	msgBuff.getOctet(); //Make buffer cursor slide by 8 bit. Transient flag is useless in this context. Transient or not this message must be loaded!
	u_int32_t headerSize=msgBuff.getLong();
	uint32_t contentOffset = headerSize + preambleLength;
	uint64_t contentSize = msgBuff.getSize() - contentOffset;
	if (contentOffset + offset + length > msgBuff.getSize()) {
		data.append(msgBuff.getPointer() + contentOffset+offset, contentSize-offset);
	} else {
		data.append(msgBuff.getPointer()+ contentOffset+offset,length);
	}
	return true;
}

bool JournalImpl::store_msg(boost::shared_ptr<Db> db,
                             const uint64_t pid,
                             Dbt& p)
{
    writeActivityFlag=true;
    uint64_t id(pid);
    Dbt key(&id, sizeof(id));
    int status;
    TxnCtxt txn;
    txn.begin(dbEnv.get(), true);
    try {
        status = db->put(txn.get(), &key, &p, DB_NOOVERWRITE);
        txn.commit();
    } catch (...) {
        txn.abort();
        throw;
    }
    if (status == DB_KEYEXIST) {
        return false;
    } else {
        return true;
    }
}

void JournalImpl::register_as_transient(std::vector<uint64_t>& trList) {
	this->transientList.insert(transientList.begin(),trList.begin(),trList.end());
	writeActivityFlag=true;
}

void JournalImpl::discard_transient_message() {
	int transientCount=0;
	bool errorFlag=false;
	if (transientList.empty()) {
		return;
	}
	std::stringstream ss;
	ss << "Start deleting "<<transientList.size()<<" transient messages...";
	log(LOG_INFO,ss.str());
	for (std::list<uint64_t>::iterator it=transientList.begin();it!=transientList.end();it++) {
		uint64_t id(*it);
		Dbt key(&id,sizeof(id));
		if (messageDb->del(0,&key,DB_AUTO_COMMIT)!=0) {
			errorFlag=true;
		} else {
			transientCount++;
		}
			
	}
	transientList.clear();
	messageDb->sync(0);
	std::stringstream ss2;
	ss2<<transientCount<<" transient message deleted from Db.";
	if (errorFlag) {
		ss2<<" Some messages have encountered an error during deleting.";
	}
	log(LOG_INFO,ss2.str());
}

void JournalImpl::compact_message_database() {
	DB_COMPACT* cmpt= new DB_COMPACT();
	if (messageDb->compact(0,0,0,cmpt,DB_FREE_SPACE,0)!=0) {
		log(LOG_WARN,"Error in compacting message database");
	} else {
		uint32_t pgsize=0;
		messageDb->get_pagesize(&pgsize);
		std::stringstream ss;
		ss << "Compacted! Page Size: "<<pgsize<<";Pages examined: "<<cmpt->compact_pages_examine<<";Pages freed: "<<cmpt->compact_pages_free;
		ss << ";Pages returned to filesystem: "<<cmpt->compact_pages_truncated;
		log(LOG_INFO,ss.str());
	}
}

void JournalImpl::remove_msg(boost::shared_ptr<Db> db, const uint64_t pid)
{
    writeActivityFlag=true;
    uint64_t id(pid);
    Dbt key(&id,sizeof(id));
    TxnCtxt txn;
    txn.begin(dbEnv.get(), true);
    try {
	    db->del(txn.get(), &key, 0);
	    txn.commit();
    } catch (...) {
    	txn.abort();
	throw;
    }
}

void JournalImpl::enqueue_data(char* data_buff, const size_t /*tot_data_len*/, const size_t this_data_len, 
					uint64_t pid,const std::string&/* xid*/, const bool /*transient*/) {
	try {
		Dbt msgValue(data_buff,this_data_len);
		store_msg(messageDb,pid,msgValue);
	} catch (const DbException& e) {
        	THROW_STORE_EXCEPTION_2("Error storing the message", e);
    	}

}

void JournalImpl::dequeue_data(const uint64_t pid, const std::string&/* xid*/, const bool /*txn_coml_commit*/){
	try {
		remove_msg(messageDb,pid);
		std::stringstream ss;
		ss << "Dequeue done for "<<pid;
		log(LOG_INFO,ss.str());
	} catch (const DbException& e) {
		THROW_STORE_EXCEPTION_2("Error removing the message",e);
	}
}

void
JournalImpl::txn_abort(const std::string&/* xid*/)
{
    throw mrg::journal::jexception(mrg::journal::jerrno::JERR__NOTIMPL, "BdbMessageStoreImpl", "txn_abort");
    /*TODO: implement transaction
    handleIoResult(jcntl::txn_abort(dtokp, xid));

    if (_mgmtObject != 0)
    {
        _mgmtObject->dec_txn();
        _mgmtObject->inc_txnAborts();
    }*/
}

void
JournalImpl::txn_commit(const std::string& /*xid*/)
{
    throw mrg::journal::jexception(mrg::journal::jerrno::JERR__NOTIMPL, "BdbMessageStoreImpl", "txn_commit");
    /*TODO: implement transaction
    handleIoResult(jcntl::txn_commit(dtokp, xid));

    if (_mgmtObject != 0)
    {
        _mgmtObject->dec_txn();
        _mgmtObject->inc_txnCommits();
    }**/
}

void
JournalImpl::stop(bool/* block_till_aio_cmpl*/)
{
    InactivityFireEvent* ifep = dynamic_cast<InactivityFireEvent*>(inactivityFireEventPtr.get());
    assert(ifep); // dynamic_cast can return null if the cast fails
    ifep->cancel();
    /*jcntl::stop(block_till_aio_cmpl);*/

    if (_mgmtObject != 0) {
        _mgmtObject->resourceDestroy();
        _mgmtObject = 0;
    }
}

bool
JournalImpl::flush()
{
    if (messageDb->sync(0)==0) {
    	return true;
    } else {
    	return false;
    }
}

bool JournalImpl::is_enqueued(const uint64_t rid,bool /*ignore_lock*/) {
	u_int64_t id(rid);	
	Dbt key(&id, sizeof(id));
	if (messageDb->exists(0,&key,0)!=DB_NOTFOUND) {
		return true;
	} else {
		return false;
	}
}

void
JournalImpl::log(mrg::journal::log_level ll, const std::string& log_stmt) const
{
    log(ll, log_stmt.c_str());
}

void
JournalImpl::log(mrg::journal::log_level ll, const char* const log_stmt) const
{
    switch (ll)
    {
        case LOG_TRACE:  QPID_LOG(trace, "Journal \"" << journalName << "\": " << log_stmt); break;
        case LOG_DEBUG:  QPID_LOG(debug, "Journal \"" << journalName << "\": " << log_stmt); break;
        case LOG_INFO:  QPID_LOG(info, "Journal \"" << journalName << "\": " << log_stmt); break;
        case LOG_NOTICE:  QPID_LOG(notice, "Journal \"" << journalName << "\": " << log_stmt); break;
        case LOG_WARN:  QPID_LOG(warning, "Journal \"" << journalName << "\": " << log_stmt); break;
        case LOG_ERROR: QPID_LOG(error, "Journal \"" << journalName << "\": " << log_stmt); break;
        case LOG_CRITICAL: QPID_LOG(critical, "Journal \"" << journalName << "\": " << log_stmt); break;
    }
}

void
JournalImpl::getEventsFire()
{
    qpid::sys::Mutex::ScopedLock sl(_getf_lock);
    getEventsTimerSetFlag = false;
    //if (_wmgr.get_aio_evt_rem()) { jcntl::get_wr_events(0); }
    //if (_wmgr.get_aio_evt_rem()) { setGetEventTimer(); }
}

void
JournalImpl::flushFire()
{
    if (writeActivityFlag) {
        writeActivityFlag = false;
        flushTriggeredFlag = false;
    } else {
        if (!flushTriggeredFlag) {
            flush();
            flushTriggeredFlag = true;
        }
    }
    inactivityFireEventPtr->setupNextFire();
    {
        timer.add(inactivityFireEventPtr);
    }
}

qpid::management::Manageable::status_t JournalImpl::ManagementMethod (uint32_t methodId,
                                                                      qpid::management::Args& /*args*/,
                                                                      std::string& /*text*/)
{
    Manageable::status_t status = Manageable::STATUS_UNKNOWN_METHOD;
    if (methodId) {
	    switch (methodId)
	    {
	
		default:
	        status = Manageable::STATUS_NOT_IMPLEMENTED;
	        break;
	    }
    }
    return status;
}

int bt_compare_func(Db* /*db*/,const Dbt* value1,const Dbt* value2) {
	uint64_t first_key=0;
	uint64_t second_key=0;
	memcpy(&first_key,value1->get_data(),value1->get_size());
	memcpy(&second_key,value2->get_data(),value2->get_size());
	int diff=static_cast<int>(first_key-second_key);
	return diff;
}
