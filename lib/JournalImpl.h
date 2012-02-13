/**********************************************************************************
*
*   bdbmsgstore: BDB-based Message Store Plugin for Apache Qpid C++ Broker
*   Copyright (C) 2011 Dario Mazza (dariomzz@gmail.com)
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, see <http://www.gnu.org/licenses/>. 
*
*********************************************************************************/


#ifndef _JournalImpl_
#define _JournalImpl_

#include <list>
#include <boost/thread.hpp>
#include <vector>
#include "jrnl/enums.hpp"
#include <qpid/broker/PersistableQueue.h>
#include <qpid/sys/Timer.h>
#include <qpid/sys/Time.h>
#include <boost/ptr_container/ptr_list.hpp>
#include <boost/intrusive_ptr.hpp>
#include "qpid/management/Manageable.h"
#include "qmf/com/atono/server/qpid/bdbstore/Journal.h"
#include "db-inc.h"
#include <boost/asio/error.hpp>
#include "AsyncOperationLogger.h"

namespace qpid { namespace sys {
class Timer;
}}

namespace qpid {
namespace store {
namespace bdb {

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
    //qpid::sys::Timer& timer;
    /**
    *	Flag that indicates if a write activity has been done on the database.
    **/
    bool writeActivityFlag;
    /**
    *	Flag that indicates if a flush has been triggered for the database. This operates in negative logic.
    **/
    bool flushTriggeredFlag;
    /**
    *	Pointer to the timer task for the flush 
    **/
    //boost::intrusive_ptr<qpid::sys::TimerTask> inactivityFireEventPtr;

    // temp local vars for loadMsgContent below
    /**
    *	Flag that indicates if the Journal is initialized.
    **/
    bool _is_init;
    
    /**
    *	Pointer to the Berkeley Database Handle used for the persistence
    **/
    //boost::shared_ptr<Db> messageDb;
    /**
    *	Pointer to the Berkeley Database Environment of the Store Database.
    **/
    boost::shared_ptr<DbEnv> dbEnv;
    /**
    *	Name of the Journal 
    **/
    std::string journalName;
    /**
    *	Persistence Id of the associated Queue
    **/
    uint64_t queuePid;
    /**
    *	Directory of the Journal.
    **/
    std::string journalDirectory;
    /**
    *	Base directory for all Berkeley Databases.
    **/
    std::string bdbDir;
	
    int num_jfiles;

    bool create_bdb;

    /**
    *	Pointer to the Management Agent
    **/
    qpid::management::ManagementAgent* _agent;
    /**
    *	Pointer to the management object for the Journal.
    **/
    qmf::com::atono::server::qpid::bdbstore::Journal* _mgmtObject;
    /**
    *	Delete Callback that will execute when Journal is deleted.
    **/
    DeleteCallback deleteCallback;
    /**
    *	List containing persistence Id of the transient messages that will be deleted on the next call of discard_transient_message() method
    **/
    std::list<uint64_t> transientList;
    /**
    *	List containing persistence Id of the accepted messages that will be deleted on the next call of discard_accepted_message() method
    **/
    std::list<uint64_t> acceptedList;

    std::vector< boost::shared_ptr<Db> > messageDbs;
    
  public:
    /**
    *	Constructor that initializes all attributes, starts the timer for flush operation, create and register the management object for the Journal
    *	@param	timer			QPID Timer used to create timeout
    *	@param	journalId		Journal Identifier used as Journal Name
    *	@param	queuePid		Persistence id of the Queue
    *	@param	journalDirectory	Directory for this Journal
    *	@param	bdbDir			Base directory for all Berkeley Databases
    *	@param	num_jfiles		Number of journal files.
    *	@param	flushTimeout		Duration for the flush timeout
    *	@param	agent			Agent for the managemnt
    *	@param	de			Pointer to the Berkeley Database Environment used in the Store
    *	@param	deleteCallback		Function called when Journal is deleted.
    **/
    JournalImpl(//qpid::sys::Timer& timer,
                const std::string& journalId,
		const uint64_t queuePid,
                const std::string& journalDirectory,
                const std::string& bdbDir,
		const int num_jfiles,
		const bool create_bdb,
 //               const qpid::sys::Duration getEventsTimeout,
                //const qpid::sys::Duration flushTimeout,
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
    *	The method returns the persistence identifier of the queue
    **/
    inline uint64_t pid() const { return queuePid; }
    /**
    *	The method controlls if the message identified by a given persistence Id is contained in the Journal.
    *	@param	rid		Persistence Id of the message to check if enqueue
    *	@param 	ignore_lock	Unused parameter
    **/
    bool is_enqueued(const u_int64_t rid, bool ignore_lock = false);


    /**
    *	The method is used by the Store to load chunk of message of a certain length.
    *	This method reads from the database, in a transaction context, the data associated with the persistence id partially decode 
    *	the data and append to the given buffer (string reference) the chunk identified by offset and length.
    *	@param	rid	Persistence Id used as key for querying the database
    *	@param	data	Reference to a buffer where the method append the message chunk loaded from the persistence
    *	@param	length	Lenght of the data chunk to load
    *	@param	offset	Offset of the data chunk to load, calculated from the begining of the encoded message data.
    *	@return	True if data chunk is loaded successfully, false if some database error occurred or if the persistence Id is not found 
    *		inside the database.
    **/
    bool loadMsgContent(u_int64_t rid, std::string& data, size_t length, size_t offset = 0);

    // Overrides for write inactivity timer
    /**
    *	The method is used by the Store to store message data inside the database.
    *	This method calls the store_msg method to register the enqueue. The signature is designed to support chunk based storing but, in this
    *	implementation, only entire message can be stored.
    *	@param	data_buff	Pointer to a buffer containing the data to store in the database
    *	@param	tot_data_len	Unused parameter. This may be used, when storing data in chunck,to indicate total length of the message data.
    *	@param	this_data_len	The size of the data contained in the buffer. This may be used, when storing data in chunk, to indicate the length
    *				of the current chunk. However in this implementation this indicates the size of the data to store.
    *	@param	pid		Persistence Id of the message to store. This represent the key of a message inside Berkeley Database.
    *	@param	xid		Unused parameter. This may represent the identifier of the QPID transaction for the enqueue operation
    *	@param	transient	Unused parameter. This may be used to trace transient inside Journal Data Structure. In this implementation
    *				transient messages have a flag set in the first byte of encoded form and discrimination will be done on recovery
    *				phase.
    *	@throw	mrg::journal::jexception	Throws on generic database error.
    *	\todo	Implement Chunk storing to support more efficient operation
    *	\todo	Create structures for transaction Id to support QPID transaction system
    **/
    void enqueue_data(char* data_buff, const size_t tot_data_len, const size_t this_data_len, 
					uint64_t pid,const std::string& xid, const bool transient=false);
    /**
    *	The method is used by the Store to delete message data from the database.
    *	This method calls the remove_msg method to register the dequeue.
    *	@param	pid		Persistence Id of the message to remove. This represent the key of a message inside Berkeley Database
    *	@param	xid		Unused parameter. This may represent the identifier of the QPID transaction for the dequeue operation
    *	@param 	txn_coml_commit	Unused parameter.
    *	@throw 	mrg::journal::jexception	Throws on generic database error.
    *	\todo	Create structures for transaction Id to support QPID transaction system
    **/
    void dequeue_data(const uint64_t pid, const std::string& xid, const bool txn_coml_commit = false);
    
    /**
    *	This method always throws a mrg::journal::jexception for unimplemented method.
    *	@param	xid	Unused parameter. This may represent the identifier of the QPID transaction to abort.
    *	@throw	mrg::journal::jexception	Throws exception for unimplemented method
    *	\todo	Implement this method to support QPID transaction abort
    **/
    void txn_abort(const std::string& xid);

    /**
    *	This method always throws a mrg::journal::jexception for unimplemented method.
    *	@param	xid	Unused parameter. This may represent the identifier of the QPID transaction to commit.
    *	@throw	mrg::journal::jexception	Throws exception for unimplemented method.
    *	\todo 	Implement this method to support QPID transaction commit
    **/
    void txn_commit(const std::string& xid);
    
    /**
    *	The method, used to stop the Journal, stops the timer for flush operation and destroys the management object.
    *	@param	block_till_aio_cmpl	Unused parameter. This may be used to support wait for asynchronous dequeue.
    **/
    void stop(bool block_till_aio_cmpl = false);

    // Logging
    /**
    *	Convenience Logging function used to log through QPID logging system a Journal System message.
    *	The format of the log message sends to the QPID Logging System is: Journal "<i>Journal_Name</i>": <i>message</i>
    *	@param	level		The QPID log level
    *	@param	log_stmt	Reference to a string containing the message to log through the QPID Logging System.
    **/
    void log(mrg::journal::log_level level, const std::string& log_stmt) const;
    /**
    *	Convenience Logging function used to log through QPID logging system a Journal System message.
    *	The format of the log message sends to the QPID Logging System is: Journal "<i>Journal_Name</i>": <i>message</i>
    *	@param	level		The QPID log level
    *	@param	log_stmt	Char array containing the message to log through the QPID Logging System.
    **/
    void log(mrg::journal::log_level level, const char* const log_stmt) const;

    // Overrides for get_events timer
    /**
    *	Method used to flush cached data to the database.
    *	This method uses the synchronization methodos of the Berkeley Database API to flush data.
    *	@return	True if the database synchronization success, False otherwise.
    **/
    bool flush();

    // TimerTask callback
    //void getEventsFire();
    /**
    *	Callback method called after the inactivity timeout.
    *	This method check for write activity on the database (using the writeActivityFlag) and if there's any write activity, turn off the
    *	writeActivityFlag and trigger the flush through the flushTriggeredFlag; if there's no write activity, checks if flush is triggered and
    *	then calls the flush method (and turns off the flushTriggeredFlag). Before exit, it resets the timeout, restarting timer.
    **/
    void flushFire();
    
    /**
    *	The method checks if the Journal is initialized.
    *	@return	True if the Journal is initialized, False otherwise.
    **/
    inline bool is_ready() const { return _is_init; }
    /**
    *	This method always returns true. This is because the transaction is not supported so the transaction data structure is always
    *	synchronized.
    *	@return	Always True
    *	\todo	After implementing the transaction data structure to support QPID transaction system,add a check for synchronization.
    **/
    inline bool is_txn_synced(const std::string& /*xid*/) { return true; /*TODO: implement transaction*/ }
    
    /**
    *	The method is used to remove transient messages from the database.
    *	This method is called at the end of recovery phase to remove all transient (not persistent) message stored to implement flow_to_disk policy;
    *	transient messages are recovered and inserted in the internal data structure of the Journal and when this method is called, it iterates over
    *	the structure to delete messages. After deletion, the data structure is cleared and database synchronized.
    **/
    void discard_transient_message();
    /**
    *	The method is used to remove accepted messages from the database.
    *	This method is called at the end of the recovery phase to remove all message stored and accepted while the broker is down.
    **/
    void discard_accepted_message();
    /**
    *	The method compacts the Berkeley Database used by the Journal (if the option is enabled)
    *	This method uses the Berkeley Database API to execute the operation; a compacted database will free pages returning them to the filesystem.
    **/
    void compact_message_database();

    /**
    *	The method returns the management object associated with the Journal.
    *	@return	The management Object for the Journal.
    **/
    qpid::management::ManagementObject* GetManagementObject (void) const
    { return _mgmtObject; }
    /**
    *	The method permits to execute a Management Method over the management object for the Journal.
    *	Currently no management method is supported by the Journal Management Object.
    *	@param	method_id	The identifier of the management method to invoke
    *	@param	args		Unused parameter. This may contain arguments for the method.
    *	@param	text		Unused parameter. This may contain the response of the management method invoked.
    *	@return	The status of the Management Method invocation.
    **/
    qpid::management::Manageable::status_t ManagementMethod (uint32_t method_id,
                                                             qpid::management::Args& args,
                                                             std::string& text);
    /**
    *	The method reset the delete callback and assigns the default callback.
    **/
    void resetDeleteCallback() { deleteCallback = DeleteCallback(); }
    /**
    *	The method is called by the Store when find transient messages that have to be deleted.
    *	This method takes some message's persistence Id and inserts it in the internal data structure for the transient messages and all the messages
    *	will be deleted at the next call of the discard_transient_message method.
    *	@param	trList	The vector containings the persistence Ids of the messages to register as transient.
    **/
    void register_as_transient(std::vector<uint64_t>& trList);
    /*+
    *	The methos is called by the Store when find messages that have been accepted while Broker is down and those message have to be deleted.
    *	This method takes some message's persistence Id and inserts it in the internal data structure for the accepted messages and all the messages
    *	will be deleted at the next call of the discard_accepted_message method.
    *	@param	trList	The vector containings the persistence Ids of the messages to register as accepted.
    **/
    void register_as_accepted(std::vector<uint64_t>& acList);
    /**
    *	The method is called by the Store when a queue is destroyed.
    *	This method close the Berkeley Database, removes it from the Environment and the delete from the filesystem the directory containing the
    *	Journal Database files.
    **/
    void delete_jrnl_files();

  private:
    /** 
    *	The method stores inside the Journal Berkeley Database a key-value pair.
    *	This method opens a Berkeley Database Transaction Context and then execute the insert of the Persistence Id and the Persistence Data.
    *	@return True if the insert has been done successfully, otherwise False if the key already exists in the database or a generic database error
    *		occurred.
    **/
    bool store_msg(boost::shared_ptr<Db> db, const uint64_t pid, Dbt& p);
    /**
    *	The method removes from the Journal Berkeley Database a key identified by the given Persistence Id.
    *	This method opens a Berkeley Database Transaction Context and then execute the delete.
    **/
    void remove_msg(boost::shared_ptr<Db> db, const uint64_t pid);
    /**
    *	The method initializes the Berkeley Database used by the Journal to store the messages.
    *	This method uses the Database Environments of the Store to open Database as Binary Tree inside a Transaction Context; before opening the
    *	database, the Binary Tree Compare Function is set.
    *	@see	bt_compare_func
    **/
    void init_message_db();

    const std::string getSubDbName(int sid);
    const std::string getSubDbPath(int sid);
    const std::string getJournalDirPath();



    /*inline void setGetEventTimer()
    {
        getEventsFireEventsPtr->setupNextFire();
        timer.add(getEventsFireEventsPtr);
        getEventsTimerSetFlag = true;
    }*/

}; // class JournalImpl

}}} // namespace qpid::store::bdb

/**
*	Compare function used by the Berkeley Binary Tree Database to sort the element inside its data structures.
*	This function executes a difference between the value1 and the value2 that is known to be the persistence Ids (keys inside database);
*	the difference is faster than any comparison and allow to return the right value to the database system.
*	@param	db	Pointer to the database handle. This parameter is unused because Berkeley Database specification suggests to avoid
*			use of the database handle in comparison check.
*	@param	value1	Pointer to the value of the first key (known to be a Persistence Id of type uint64_t)
*	@param	value2	Pointer to the value of the second key (known to be a Persistence Id of type uint64_t)
*	@return Zero, greater than zero or less the zero value to indicate rispectively that the keys are equals, the first one is greater than
*		the second one or the first is less than the second one.
**/
int bt_compare_func(Db *db,const Dbt* value1,const Dbt* value2);
#endif
