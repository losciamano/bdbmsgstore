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
#include "qmf/com/atono/server/qpid/bdbstore/EventCreated.h"
#include "qmf/com/atono/server/qpid/bdbstore/EventEnqThresholdExceeded.h"
#include "qmf/com/atono/server/qpid/bdbstore/EventFull.h"
#include "qmf/com/atono/server/qpid/bdbstore/EventRecovered.h"
#include "qpid/sys/Monitor.h"
#include "qpid/sys/Timer.h"
#include "qpid/framing/Buffer.h"
#include "StoreException.h"
#include "Cursor.h"
#include "IdDbt.h"
#include "TxnCtxt.h"

#include <vector>  
#include <iomanip>
#include <utility>

using namespace mrg::msgstore;
using namespace mrg::journal;
using namespace qpid::store::bdb;
using qpid::management::ManagementAgent;

namespace _qmf = qmf::com::atono::server::qpid::bdbstore;

/**
*	\todo Change warning rate for timer (ref qpid::sys::Timer::warn) 
*/
InactivityFireEvent::InactivityFireEvent(JournalImpl* p, const qpid::sys::Duration timeout):
    qpid::sys::TimerTask(timeout, "JournalInactive:"+p->id()), _parent(p) {}

void InactivityFireEvent::fire() { qpid::sys::Mutex::ScopedLock sl(_ife_lock); if (_parent) _parent->flushFire(); }

JournalImpl::JournalImpl(/*qpid::sys::Timer& timer_,*/
                         const std::string& journalId,
			 const uint64_t queuePid,
                         const std::string& journalDirectory,
                         const std::string& bdbDir,
			 const int num_jfiles,
                         //const qpid::sys::Duration getEventsTimeout,
                         //const qpid::sys::Duration flushTimeout,
                         qpid::management::ManagementAgent* a,
                         boost::shared_ptr<DbEnv>& de,
                         DeleteCallback onDelete)
                         :
                         //timer(timer_),
                         //getEventsTimerSetFlag(false),
                         writeActivityFlag(false),
                         flushTriggeredFlag(true),
                         _is_init(false),
                         dbEnv(de),
                         journalName(journalId),
			 queuePid(queuePid),
                         journalDirectory(journalDirectory),
                         bdbDir(bdbDir),
			 num_jfiles(num_jfiles),
                         _agent(a),
                         _mgmtObject(0),
                         deleteCallback(onDelete),
			 messageDbs(num_jfiles)
{
    //getEventsFireEventsPtr = new GetEventsFireEvent(this, getEventsTimeout);
    /*inactivityFireEventPtr = new InactivityFireEvent(this, flushTimeout);
    {
        timer.start();
        timer.add(inactivityFireEventPtr);
    }*/

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
    oss << "Journal directory = \"" << journalDirectory << "\"; Bdb Directory = \"" << bdbDir << "\"; Num JFiles : "<<num_jfiles<<"";
    log(LOG_DEBUG, oss.str());
}

JournalImpl::~JournalImpl()
{
	if (deleteCallback) deleteCallback(*this);
	//inactivityFireEventPtr->cancel();
    
	if (_mgmtObject != 0) 
	{
        	_mgmtObject->resourceDestroy();
        	_mgmtObject = 0;
    	}
    	for (int k=0;k<this->num_jfiles;k++)
	{
		if(messageDbs[k].get()) 
		{
	            	messageDbs[k]->close(0);
	    	} else 
		{
	        	log(LOG_DEBUG,"Null message store");
		}
	}
	this->messageDbs.clear();
    	log(LOG_DEBUG, "Destroyed");
}

void JournalImpl::delete_jrnl_files() 
{
	for (int k=0;k<this->num_jfiles;k++) 
	{
		if (messageDbs[k].get()) 
		{
	        	messageDbs[k]->close(0);
	                dbEnv->dbremove(0,getSubDbPath(k).c_str(),0,DB_AUTO_COMMIT);
	                QPID_LOG(debug,"Message database "+getSubDbPath(k)+"destroyed");
	                messageDbs[k].reset();
		}
        }
        mrg::journal::jdir::delete_dir(getJournalDirPath());
}

const std::string JournalImpl::getJournalDirPath() {
	std::stringstream ss;
	ss << bdbDir<< journalDirectory;
	return ss.str();
}

void JournalImpl::init_message_db() 
{
	// Databases are constructed here instead of the constructor so that the DB_RECOVER flag can be used	
	// against the database environment. Recover can only be performed if no databases have been created
	// against the environment at the time of recovery, as recovery invalidates the environment.
	if (!_is_init) 
	{
		for (unsigned int k=0;k<this->messageDbs.size();k++) 
		{
			messageDbs[k].reset(new Db(dbEnv.get(), 0));
			messageDbs[k]->set_bt_compare(&bt_compare_func);
    			TxnCtxt txn;
			txn.begin(dbEnv.get(), false);            
			try 
			{
				messageDbs[k]->open(txn.get(),getSubDbPath(k).c_str(), 0, DB_BTREE, DB_CREATE | DB_THREAD, 0);
				txn.commit();
				QPID_LOG(debug,"Message database opened at "+getSubDbPath(k));
			} catch (...) { txn.abort(); throw; }
		}
	    	_is_init=true;
	}
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
        _agent->raiseEvent(_qmf::EventCreated(journalName),qpid::management::ManagementAgent::SEV_NOTE);
    
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

void JournalImpl::recoverMessages(std::vector< std::pair < uint64_t,std::string> >&recovered) 
{
	this->init_message_db();
        std::vector<Cursor> messageCursors(this->num_jfiles);
	std::vector<bool> cursorsDone(this->num_jfiles,false);
	for (int k=0;k<this->num_jfiles;k++) {
	        messageCursors[k].open(messageDbs[k], 0); //Open cursor outside transaction  
	}	
        IdDbt key;
        Dbt value;
        //read all messages
	int sindex=0;
	int doneCnt=0;
	while (doneCnt<this->num_jfiles) 
	{
		if (!cursorsDone[sindex])
		{
		        if (messageCursors[sindex].next(key, value)) 
			{
		                std::string buffer(reinterpret_cast<char*>(value.get_data()),value.get_size());
		                recovered.push_back(std::pair < uint64_t ,std::string >(key.id,buffer));
			} else 
			{
				doneCnt++;
				cursorsDone[sindex]=true;
			}
		}
		sindex = (sindex+1)%this->num_jfiles;
			
        }
	for (int k=0;k<this->num_jfiles;k++) {
		messageCursors[k].close();
	}
}

void
JournalImpl::recover_complete()
{
    discard_transient_message();
    discard_accepted_message();
    log(LOG_DEBUG, "Recover phase 2 complete; journal now writable.");
    if (_agent != 0)
        _agent->raiseEvent(_qmf::EventRecovered(journalName),qpid::management::ManagementAgent::SEV_NOTE);
}

//#define MAX_AIO_SLEEPS 1000000 // tot: ~10 sec
//#define AIO_SLEEP_TIME_US   10 // 0.01 ms
// Return true if content is recovered from store; false if content is external and must be recovered from an external store.
// Throw exception for all errors.
bool
JournalImpl::loadMsgContent(u_int64_t rid, std::string& data, size_t length, size_t offset)
{
	boost::shared_ptr<Db> messageDb(messageDbs[rid%num_jfiles]);
        size_t preambleLength = sizeof(u_int32_t)+sizeof(u_int8_t)/*header size + transient flag*/;
        u_int64_t id(rid);      
        IdDbt key(id);
        Dbt value;
	value.set_flags(DB_DBT_MALLOC);
        int dbErr=messageDb->get(0,&key,&value,0);
	if ((dbErr!=0)/*&&(dbErr!=DB_NOTFOUND)*/) 
	{
		log(LOG_ERROR,"LoadMsgContent "+boost::lexical_cast<std::string>(rid)+" => Dbc->get Exit Status : "+boost::lexical_cast<std::string>(dbErr));
	}
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
	free(value.get_data());
	value.set_data(0x0);
        return true;
}

bool JournalImpl::store_msg(boost::shared_ptr<Db> db,
                             const uint64_t pid,
                             Dbt& p)
{
	writeActivityFlag=true;
        uint64_t id(pid);
        IdDbt key(id);
        int status;
        TxnCtxt txn;
        txn.begin(dbEnv.get(), true);
        try 
        {
                status = db->put(txn.get(), &key, &p, DB_NOOVERWRITE);
                txn.commit();
        } catch (...) 
        {
                txn.abort();
                throw;
        }
        if (status!=0)
        {
		if (status!=DB_KEYEXIST)
		{
	                log(LOG_ERROR,"store_msg("+boost::lexical_cast<std::string>(pid)+") => Db->put Error Code : "+boost::lexical_cast<std::string>(status));
		} else
		{
	                log(LOG_INFO,"store_msg("+boost::lexical_cast<std::string>(pid)+") => Message is already in the journal");
		}
        }
        if (status == DB_KEYEXIST) 
        {
                return false;
        } else 
        {
                return true;
        }
}

void JournalImpl::register_as_transient(std::vector<uint64_t>& trList) 
{
        this->transientList.insert(transientList.begin(),trList.begin(),trList.end());
        writeActivityFlag=true;
}

void JournalImpl::register_as_accepted(std::vector<uint64_t>& acList) 
{
        this->acceptedList.insert(acceptedList.begin(),acList.begin(),acList.end());
        writeActivityFlag=true;
}

void JournalImpl::discard_transient_message() 
{
        int transientCount=0;
        bool errorFlag=false;
        if (transientList.empty()) 
        {
                return;
        }
        std::stringstream ss;
        ss << "Start deleting "<<transientList.size()<<" transient messages...";
        log(LOG_INFO,ss.str());
        for (std::list<uint64_t>::iterator it=transientList.begin();it!=transientList.end();it++) 
        {
                uint64_t id(*it);
                IdDbt key(id);		
                int dbErr = messageDbs[id%num_jfiles]->del(0,&key,DB_AUTO_COMMIT);
                if (dbErr!=0)
                {
                        log(LOG_ERROR,"Del "+boost::lexical_cast<std::string>(id)+" => Db->del Exit Status : "+boost::lexical_cast<std::string>(dbErr));
                }
                if (dbErr!=0) 
                {
                        errorFlag=true;
                } else 
                {
                        transientCount++;
                }
        }
        transientList.clear();
	for (int k=0;k<this->num_jfiles;k++)
	{
	        messageDbs[k]->sync(0);
	}
        std::stringstream ss2;
        ss2<<transientCount<<" transient message deleted from Dbs.";
        if (errorFlag)
	{
                ss2<<" Some messages have encountered an error during deleting.";
        }
        log(LOG_INFO,ss2.str());
}

void JournalImpl::discard_accepted_message()
{
        int acceptedCount=0;
        bool errorFlag=false;
        if (acceptedList.empty())
	{
                return;
        }
        std::stringstream ss;
        ss << "Start deleting "<<acceptedList.size()<<" accepted messages...";
        log(LOG_INFO,ss.str());
        for (std::list<uint64_t>::iterator it=acceptedList.begin();it!=acceptedList.end();it++) 
	{
                uint64_t id(*it);
                IdDbt key(id);
		int resp = messageDbs[id%num_jfiles]->del(0,&key,DB_AUTO_COMMIT);
                if ((resp!=0)&&(resp!=DB_NOTFOUND))
		{
                        errorFlag=true;
                } else
		{
                        acceptedCount++;
                }
        }
        acceptedList.clear();
        for (int k=0;k<this->num_jfiles;k++)
	{
	        messageDbs[k]->sync(0);
	}

        std::stringstream ss2;
        ss2<<acceptedCount<<" accepted message deleted from Db.";
        if (errorFlag)
	{
                ss2<<" Some messages have encountered an error during deleting.";
        }
        log(LOG_INFO,ss2.str());
}
void JournalImpl::compact_message_database() 
{
	return;
	/*
        DB_COMPACT* cmpt= new DB_COMPACT();
        if (messageDb->compact(0,0,0,cmpt,DB_FREE_SPACE,0)!=0)
	{
                log(LOG_WARN,"Error in compacting message database");
        } else 
	{
                uint32_t pgsize=0;
                messageDb->get_pagesize(&pgsize);
                std::stringstream ss;
                ss << "Compacted! Page Size: "<<pgsize<<";Pages examined: "<<cmpt->compact_pages_examine<<";Pages freed: "<<cmpt->compact_pages_free;
                ss << ";Pages returned to filesystem: "<<cmpt->compact_pages_truncated;
                log(LOG_INFO,ss.str());
        }*/
}

void JournalImpl::remove_msg(boost::shared_ptr<Db> db, const uint64_t pid)
{
	writeActivityFlag=true;
	uint64_t id(pid);
	IdDbt key(id);
	TxnCtxt txn;
	txn.begin(dbEnv.get(), true);
	try 
	{
		int resp = db->del(txn.get(), &key, 0);
		if (resp!=0) 
		{
			log(LOG_ERROR,"remove_msg("+boost::lexical_cast<std::string>(pid)+") => Db->del Error Code : "+boost::lexical_cast<std::string>(resp));
		}
		txn.commit();
	} catch (...) 
	{
		txn.abort();
		throw;
    	}
}

void JournalImpl::enqueue_data(char* data_buff, const size_t /*tot_data_len*/, const size_t this_data_len, 
                                        uint64_t pid,const std::string&/* xid*/, const bool /*transient*/) 
{
        //log(LOG_INFO,"enqueue_data("+boost::lexical_cast<std::string>(pid)+")");	
        try
        {
                Dbt msgValue(data_buff,this_data_len);
                store_msg(messageDbs[pid%num_jfiles],pid,msgValue);
        } catch (const DbException& e)
        {
                THROW_STORE_EXCEPTION("Error storing the message");
        }

}

void JournalImpl::dequeue_data(const uint64_t pid, const std::string&/* xid*/, const bool /*txn_coml_commit*/)
{

        //log(LOG_INFO,"dequeue_data("+boost::lexical_cast<std::string>(pid)+")");
        try 
        {
                remove_msg(messageDbs[pid%num_jfiles],pid);
        } catch (const DbException& e) 
        {
                THROW_STORE_EXCEPTION("Error removing the message");
        }
}

void
JournalImpl::txn_abort(const std::string&/* xid*/)
{
    throw mrg::journal::jexception(mrg::journal::jerrno::JERR__NOTIMPL, "BdbMessageStoreImpl", "txn_abort");
}

void
JournalImpl::txn_commit(const std::string& /*xid*/)
{
    throw mrg::journal::jexception(mrg::journal::jerrno::JERR__NOTIMPL, "BdbMessageStoreImpl", "txn_commit");
}

void
JournalImpl::stop(bool/* block_till_aio_cmpl*/)
{
    //InactivityFireEvent* ifep = dynamic_cast<InactivityFireEvent*>(inactivityFireEventPtr.get());
    //assert(ifep); // dynamic_cast can return null if the cast fails
    //ifep->cancel();
    /*jcntl::stop(block_till_aio_cmpl);*/

    if (_mgmtObject != 0) 
    {
        _mgmtObject->resourceDestroy();
        _mgmtObject = 0;
    }
}

bool
JournalImpl::flush()
{
	//TODO: make specific flush flag for each database
	timespec start,end;
	clock_gettime(CLOCK_REALTIME,&start);
	bool dbOk=true;
    	int ret=0;
	for (int k=0;k<this->num_jfiles;k++) 
	{
		//ret = messageDbs[k]->sync(0);
		if (dbOk && ret!=0) dbOk=false;
	}
	clock_gettime(CLOCK_REALTIME,&end);
	double diff = end.tv_nsec/1000000.0-start.tv_nsec/1000000.0;
	log(((dbOk)?LOG_WARN:LOG_ERROR),"flush()  Duration : "+boost::lexical_cast<std::string>(diff)+" msec");
	if (dbOk) 
	{
        	return true;
	} else 
	{
        	return false;
	}
}

bool JournalImpl::is_enqueued(const u_int64_t rid,bool /*ignore_lock*/)
{
        u_int64_t id(rid);      
        IdDbt key(id);
        //log(LOG_INFO,"is_enqueued("+boost::lexical_cast<std::string>(rid)+")");
        if (messageDbs[rid%num_jfiles]->exists(0,&key,0)!=DB_NOTFOUND) 
        {
                return true;
        } else 
        {
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
JournalImpl::flushFire()
{
    if (writeActivityFlag) 
    {
        writeActivityFlag = false;
        flushTriggeredFlag = false;
    } else
    {
        if (!flushTriggeredFlag) 
	{
            flush();
            flushTriggeredFlag = true;
        }
    }
    /*inactivityFireEventPtr->setupNextFire();
    {
        timer.add(inactivityFireEventPtr);
    }*/
}

const std::string JournalImpl::getSubDbPath(int sid) {
	std::stringstream ss;
	ss<<this->journalDirectory<<getSubDbName(sid);
	return ss.str();
}

const std::string JournalImpl::getSubDbName(int sid)
{
	std::stringstream ss;
	ss<<this->journalName<<"_s"<<std::setfill('0')<<std::setw(3)<<sid<<".db";
	return ss.str();
}

qpid::management::Manageable::status_t JournalImpl::ManagementMethod (uint32_t methodId,
                                                                      qpid::management::Args& /*args*/,
                                                                      std::string& /*text*/)
{
    Manageable::status_t status = Manageable::STATUS_UNKNOWN_METHOD;
    if (methodId) 
    {
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
