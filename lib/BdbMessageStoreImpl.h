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
    /**
    *	Convenience type definition for a shared pointer to a Berkeley Database Handle
    **/
    typedef boost::shared_ptr<Db> db_ptr;
    /**
    *	Convenience type definition for a shared pointer to a Berkeley Database Environment
    **/
    typedef boost::shared_ptr<DbEnv> dbEnv_ptr;
    /**
    *	This struct contains information about plugin options
    */
    struct StoreOptions : public qpid::Options {
    	/**
	*	Base constructor for options struct
	**/
        StoreOptions(const std::string& name="Store Options");
	/**
	*	Cluster Name Option
	*/
        std::string clusterName;
	/**
	*	Path of the store Directory
	**/
        std::string storeDir;
	/**
	*	Truncate flag option.
	*       If true, will truncate the store (discard any existing records). If no|false|0, will preserve the existing store files for recovery.
	**/
        bool      truncateFlag;
	/**
	*	Compact flag option.
	*	If true, will compact the Berkeley Db after recovery and transient message deletion; this operation return to filesystem useless db
	*	pages.
	**/
	bool 	  compactFlag;
    };

  protected:
    /**
    *	Convenience type definition for a map that keeps track of association between a persistence ID and a queue.
    **/
    typedef std::map<u_int64_t, qpid::broker::RecoverableQueue::shared_ptr> queue_index;
    /**
    *	Convenience type definition for a map that keeps track of association between a persistence ID and an exchange.
    **/
    typedef std::map<u_int64_t, qpid::broker::RecoverableExchange::shared_ptr> exchange_index;
    /**
    *	Convenience type definition for a map that keeps track of association between a persistence ID and a message.
    **/
    typedef std::map<u_int64_t, qpid::broker::RecoverableMessage::shared_ptr> message_index;
    /**
    *	Convenience type definition for map of Locked Transaction
    **/
    typedef LockedMappings::map txn_lock_map;
    /**
    *	Convenience type definition for a Boost pointer list to Prepared Transaction object.
    **/
    typedef boost::ptr_list<PreparedTransaction> txn_list;

    /**
    *	Structs for Transaction Recover List (TPL) recover state
    **/
    struct TplRecoverStruct {
        /**
	*	rid of TPL record
	*/
        u_int64_t rid;
	bool deq_flag;
        bool commit_flag;
        bool tpc_flag;
        TplRecoverStruct(const u_int64_t _rid, const bool _deq_flag, const bool _commit_flag, const bool _tpc_flag);
    };
    /**
    *	Convenience type definition to avoid use of struct keyword.
    **/
    typedef TplRecoverStruct TplRecover;
    /**
    *	Convenience type definition for a pair of XID (string) and TplRecover Struct.
    **/
    typedef std::pair<std::string, TplRecover> TplRecoverMapPair;
    /**
    *	Convenience type definition for a map of TplRecover Struct identified by a XID (string)
    **/
    typedef std::map<std::string, TplRecover> TplRecoverMap;
    /**
    *	Convenience type definition for a map constant iterator
    **/
    typedef TplRecoverMap::const_iterator TplRecoverMapCitr;
    /**
    *	Convenience type definition for a map of pointer to a JournalImpl identified by the queue name
    **/
    typedef std::map<std::string, JournalImpl*> JournalListMap;
    /**
    *	Convenience type definition for a map iterator
    **/
    typedef JournalListMap::iterator JournalListMapItr;

    //Default store settings
    /**
    *	Default truncate flag option value.
    **/
    static const bool      defTruncateFlag = false;
    /**
    *	Default store directory
    **/
    static const std::string storeTopLevelDir;
    /**
    *	Default Timeout for Get Events
    **/
    static qpid::sys::Duration defJournalGetEventsTimeout;
    /**
    *	Default Timeout for Journal Flushing
    **/
    static qpid::sys::Duration defJournalFlushTimeout;
    
    /**
    *	List containing pointer to all db opened
    **/
    std::list<db_ptr> dbs;
    /**
    *	Pointer to the Berkeley Database Environment
    **/
    dbEnv_ptr dbenv;
    /**
    *	Pointer to the BDB containing information about queues
    **/
    db_ptr queueDb;
    /**
    *	Pointer to the BDB containing information about configuration
    **/
    db_ptr configDb;
    /**
    *	Pointer to the BDB containing information about exchanges
    **/
    db_ptr exchangeDb;
    /**
    *	Pointer to the BDB containing information about mappings
    **/
    db_ptr mappingDb;
    /**
    *	Pointer to the BDB containing information about bindings
    **/
    db_ptr bindingDb;
    /**
    *	Pointer to the BDB containing information about general configurations
    **/
    db_ptr generalDb;

    /**
    *	Pointer to Transaction Prepared List (TPL) Journal instance
    */
    boost::shared_ptr<TplJournalImpl> tplStorePtr;
    //TplRecoverMap tplRecoverMap;//TODO:implement transaction
    /**
    *	Lock over TPL Journal initialization
    **/
    qpid::sys::Mutex tplInitLock;
    /**
    *	Data structure containings association between queue name e JournalImpl object
    **/
    JournalListMap journalList;
    /**
    *	Lock over Journal List access
    **/
    qpid::sys::Mutex journalListLock;

    /**
    *	Sequence used for generating new queue id
    **/
    IdSequence queueIdSequence;
    /**
    *	Sequence used for generating new exchange id
    **/
    IdSequence exchangeIdSequence;
    /**
    *	Sequence used for generating new general id
    **/
    IdSequence generalIdSequence;
    /**
    *	Sequence used for generating new message persistence id
    **/
    IdSequence messageIdSequence;
    /**
    *	String rappresenting store directory for the plugin
    **/
    std::string storeDir;
    /**
    *	Truncate flag value
    **/
    bool      truncateFlag;
    /**
    *	Compact flag value
    **/
    bool      compactFlag;
    /**
    *	Highest value of the persistence ID
    **/
    u_int64_t highestRid;
    /**
    *	Flag indicating if the Message Store is initialized
    **/
    bool isInit;
    /**
    *	Environment Path
    **/
    const char* envPath;
    /**
    *	Reference to the Timer Object used for timeout
    **/
    qpid::sys::Timer& timer;

    /**
    *	Management object of the Store used for QMF
    **/
    qmf::com::redhat::rhm::store::Store* mgmtObject;
    /**
    *	Management Agent used to communicate with QMF
    **/
    qpid::management::ManagementAgent* agent;
    
    /**
    *	Boost I/O service used for asynchronous dequeue
    **/
    boost::asio::io_service bdbserv;
    /**
    *	Boost Work used for keep service alive when there's no dequeue in act
    **/
    boost::asio::io_service::work bdbwork;
    /**
    *	Boost Thread that executes the Boost I/O service
    **/
    boost::shared_ptr<boost::thread> servThread;

    /**
    *	List containing pointer to all recovered Journal
    **/
    std::list<JournalImpl*> recoveredJournal;

    

    /**
    *	Initialization Method used when NO RECOVERY task is needed.
    *	This Method open the BDB environments and all the databases; this method starts also the boost I/O service for asynchronous dequeue
    **/
    void init();
    /**
    *	Method for recovering queue information from the Berkeley Database.
    *	This method reads information from the database and, for each queue recovered, calls recoverMessages.
    *	@param	txn		Transaction context indicating the transaction for the recovery
    *	@param	recovery	Reference to the Recovery Manager used to recover QPID Objects
    *	@param	index		Reference to the sequence used to associate index and queues
    *	@param	locked		Locked Transaction List
    *	@param	messages	Reference to the sequence that will be passed to recoverMessages method
    **/
    void recoverQueues(TxnCtxt& txn,
                       qpid::broker::RecoveryManager& recovery,
                       queue_index& index,
                       txn_list& locked,
                       message_index& messages);
    /**
    *	Method for recovering message informations from the Journal
    *	This method uses the recoverMessages method of the JournalImpl to get messages from the Berkeley Db.
    *	@param	recovery	Reference to the Recovery Manager used to recover QPID Objects
    *	@param	queue		Qpid queue that will contains the message recovered
    *	@param	prepared 	Prepared Transaction List (unused)
    *	@param	messages	Unused parameter containing reference to the sequence for messages id
    *	@param	rcnt		Reference used as output parameter for recovered message counter
    *	@param	idcnt		Reference used as output parameter for in doubt message counter
    *	@param	tcnt		Reference used as output parameter for transient message counter
    *	@return	The highest persistence id read from the db
    **/
    uint64_t recoverMessages(qpid::broker::RecoveryManager& recovery,
                         qpid::broker::RecoverableQueue::shared_ptr& queue,
                         txn_list& prepared,
                         message_index& messages,
                         long& rcnt,
                         long& idcnt,
			 long& tcnt);
    /**
    *	Unimplemented method, always throws mrg::journal::jexception 
    **/
    qpid::broker::RecoverableMessage::shared_ptr getExternMessage(qpid::broker::RecoveryManager& recovery,
                                                                  uint64_t mId,
                                                                  unsigned& headerSize);
    /**
    *	Method for recovering exchange information from the Berkeley Database.
    *	This method reads information from the database and recover data using the given RecoveryManager.
    *	@param	txn		Transaction context indicating the transaction for the recovery
    *	@param	recovery	Reference to the Recovery Manager used to recover QPID Objects
    *	@param	index		Reference to the sequence used to associate index and exchange
    **/
    void recoverExchanges(TxnCtxt& txn,
                          qpid::broker::RecoveryManager& recovery,
                          exchange_index& index);
    /**
    *	Method for recovering binding information from the Berkeley Database.
    *	This method reads information from the database and recover data using the given RecoveryManager.
    *	@param	txn		Transaction context indicating the transaction for the recovery
    *	@param	exchanges	Reference to the sequence used to associate binding to exchanges
    *	@param	queues		Reference to the sequence used to associate bingind to queues
    **/
    void recoverBindings(TxnCtxt& txn,
                         exchange_index& exchanges,
                         queue_index& queues);
    /**
    *	Method for recovering general information from the Berkeley Database.
    *	This method reads information from the database and recover data using the given RecoveryManager.
    *	@param	txn		Transaction context indicating the transaction for the recovery
    *	@param	recovery	Reference to the Recovery Manager used to recover QPID Objects
    **/
    void recoverGeneral(TxnCtxt& txn,
                        qpid::broker::RecoveryManager& recovery);
    /*int enqueueMessage(TxnCtxt& txn,
                       IdDbt& msgId,
                       qpid::broker::RecoverableMessage::shared_ptr& msg,
                       queue_index& index,
                       txn_list& locked,
                       message_index& prepared);*/
    /**
    *	Method to read the Transaction Prepared List.
    *	This method is empty in this implementation.
    **/
    void readTplStore();
    /**
    *	Method to recover the Transaction Prepared List.
    *	This method is empty in this implementation.
    **/
    void recoverTplStore();
    /**
    *	Method to Recover Locked Mappings.
    *	This method is empty in this implementation.
    **/
    void recoverLockedMappings(txn_list& txns);
    /**
    *	Method for checking if a qpid transaction context may be dynamic cast to a TxnCtxt (message store transaction context)
    *	@param ctxt	The qpid transaction context to check and cast
    *	@return	A pointer to the transaction context casted to type TxnCtxt
    **/
    TxnCtxt* check(qpid::broker::TransactionContext* ctxt);
    /**
    *	Method used to encode a qpid message in a char buffer.
    *	The structure of the buffer is the following:
    *	1 u_int8_t that contains a flag for transient message (0 if persistent, 1 if transient)
    *	1 u_int64_t that contains the header size obtained through the encodedHeaderSize() of the qpid Message Object
    *	1 section containig the result of encode() method of the qpid Message object
    *	@param 	buff	Reference to the buffer that will be filled with encoded data (this buffer will be initialized inside this method)
    *	@param 	message	Reference to the QPID message to encode.
    *	@return	The u_int64_t containing the size of the buffer
    **/
    u_int64_t msgEncode(std::vector<char>& buff, const boost::intrusive_ptr<qpid::broker::PersistableMessage>& message);
   
    /**
    *	Method used to store a message in the external store (a JournalImpl Object) associated with the given queue.
    *	@param	queue	Pointer to the queue containing the message
    *	@param 	txn	Transaction context for the operation
    *	@param 	message	Reference to the QPID message that will be stored
    *	@param	newId	Unused parameter.
    **/
    void store(const qpid::broker::PersistableQueue* queue,
               TxnCtxt* txn,
               const boost::intrusive_ptr<qpid::broker::PersistableMessage>& message,
               bool newId);
    /**
    *	Method used to register a dequeue asynchronous.
    *	This method call with the Boost I/O service the method dequeue of the queue's external store (a JournalImpl object).
    *	@param	ctxt	Qpid Transaction Context for the operation
    *	@param	msg	Message to dequeue
    *	@param	queue	Reference to the queue that contains the message
    */
    void async_dequeue(qpid::broker::TransactionContext* ctxt,
                       const boost::intrusive_ptr<qpid::broker::PersistableMessage>& msg,
                       const qpid::broker::PersistableQueue& queue);
    /**
    *	Method used to delete a generic persistable object identified by a Persistence Id from the given database.
    *	@param	db	Pointer to the database
    *	@param	p	Reference to the persistable object that will be deleted
    */
    void destroy(db_ptr db,
                 const qpid::broker::Persistable& p);
    /**
    *	Method used to insert a generic persistable object in a given database and assign the next id in the sequence as the persistence Id of the
    *	object.
    *	@param	db	Pointer to the database
    *	@param	seq	Reference to the Sequence used to obtain the new Persistence Id
    *	@param	p	Reference to the persistable object that will be inserted
    *	@return	True if the object has been inserted correctly, False if the persistence id is already in the database.
    **/
    bool create(db_ptr db,
                IdSequence& seq,
                const qpid::broker::Persistable& p);
    /**
    *	This method is empty in the current implementation.
    *	@param	txn	Unused parameter.
    *	@param  commit	Unused parameter.
    **/
    void completed(TxnCtxt& txn,
                   bool commit);
    /**
    *	Method used to delete from database all bindings associated with the given queue.
    *	@param	queue	Reference to the queue
    **/
    void deleteBindingsForQueue(const qpid::broker::PersistableQueue& queue);
    /**
    *	Method used to delete from database a binding identified by a key, associated with a queue and an exchange
    *	@param	exchange	Reference to the exchange
    *	@param	queue		Reference to the queue
    *	@param	key		Reference to the key of the binding to delete
    **/
    void deleteBinding(const qpid::broker::PersistableExchange& exchange,
                       const qpid::broker::PersistableQueue& queue,
                       const std::string& key);
    /**
    *	Method used to insert an entry in the database. Transactional put is supported.
    *	@param	db	Pointer to the database handle
    *	@param	txn	Transaction context for the operation. If null/0 the operation will be executed outside a transaction
    *	@param	key	Reference to the key of the database entry
    *	@param	value	Reference to the value of the database entry
    **/
    void put(db_ptr db,
             DbTxn* txn,
             Dbt& key,
             Dbt& value);
    /**
    *	Method used to open a database.
    *	@param	db	Pointer to the database handle
    *	@param	txn	Transaction context for the operation. If null/0 the operation will be executed outside transaction
    *	@param	file	Char array containing the path to the file containing the database to open
    *	@param	dupKey	If this flag is set to True the database will accept duplicate key, otherwise (if set to False) the database will
    *			return an error if trying to insert an element with a key already contained in the database.
    **/
    void open(db_ptr db,
              DbTxn* txn,
              const char* file,
              bool dupKey);
    /**
    *	Method for closing all the databases used by the store.
    **/
    void closeDbs();

    // journal functions
    //void createJrnlQueue(const qpid::broker::PersistableQueue& queue);
    /**
    *	The method calculates the hash of a string using the Daniel Bernstein hash function.
    *	@param	str	String to hash
    *	@return The hash of the string.
    **/
    u_int32_t bHash(const std::string str);
    /**
    *	The method returns the Journal Directory for a given queue. This name is calculated from the queue name.
    *	@param	queue	Reference to the queue associated with the Journal
    *	@return	The Directory Name of the Journal.
    **/
    std::string getJrnlDir(const qpid::broker::PersistableQueue& queue); //for exmaple /var/rhm/ + queueDir/
    /**
    *	The method returns the hashed name for a directory associated with a given queue name
    *	@param	queueName	String containing queueName
    *	@return	String containing the hash name for the directory.
    **/
    std::string getJrnlHashDir(const std::string& queueName);
    /**
    *	The method returns the base directory for all the Journal.
    *	@return	The Journal base directory.
    **/
    std::string getJrnlBaseDir();
    /**
    *	The method returns the base directory for all berkeley databases.
    *	@return	The base directory for the bdb
    **/
    std::string getBdbBaseDir();
    /**
    *	The method returns the base direcotry for the TPL Journal database.
    *	@return The base directory for the TPL database.
    **/
    std::string getTplBaseDir();
    /**
    *	This method is used to check if the store is initialized. If it's not initialized, the init() method will be call.
    **/
    inline void checkInit() {
        // TODO: change the default dir to ~/.qpidd
        if (!isInit) { init("/tmp"); isInit = true; }
    }
    /**
    *	This method is empty in this implementation. The method should check the initialization of the TPL Journal store.
    **/
    void chkTplStoreInit();

    /**
    *	Debug aid for printing XIDs that may contain non-printable chars
    *	@param	xid	XID to convert to string
    *	@return	String reppresenting the XID
    **/
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
    /**
    *	Convenience type definition for a shared pointer to this class.
    **/
    typedef boost::shared_ptr<BdbMessageStoreImpl> shared_ptr;

    /**
    *	Costructor for the BDB-based Message store.
    *	@param	timer	Reference to a Timer object used for timeout
    *	@param	envpath	Environment path.
    **/
    BdbMessageStoreImpl(qpid::sys::Timer& timer, const char* envpath = 0);
    /**
    *	Virtual destructor for the class.
    **/
    virtual ~BdbMessageStoreImpl();
    
    /**
    *	Initialization method that check the options and then call the init(const std::string&,bool) method with the right parameter.
    *	@param	options	Pointer to an Options Struct
    *	@return	True if the initialization has been done, false otherwise.
    **/
    bool init(const qpid::Options* options);
    /**
    *	Initialization method that handles the truncate option and decides to call truncateInit(bool) or init() method.
    *	@param	dir		Store Base directory
    *	@param	truncateFlag 	Truncate Flag to handle
    *	@return True if the store has been initialized, false otherwise.
    **/
    bool init(const std::string& dir,const bool truncateFlag=false);
    /**
    *	Truncate Initialization method that close all databases, stop all Journals.
    *	@param	saveStoreContent	Flag to enable Journal backup (in this implementation, Journal backup isn't available)
    **/
    void truncateInit(const bool saveStoreContent = false);
    /**
    *	The method initializes the management object and register the agent to the given broker.
    *	@param	broker	Pointer to the QPID Broker used for registering.
    **/
    void initManagement (qpid::broker::Broker* broker);
    /**
    *	Finalization method that delete all callbacks, stop all Journals and destroy management objects.
    **/
    void finalize();
    /**
    *	The method creates a new Queue inside the store system, writing it into the queues' Berkeley Database.
    *	This method creates a new JournalImpl object, initializes it and then set it as the external queue storage for the given queue.
    *	@param	queue	The QPID Persistent Queue to store inside the persistence 
    *	@param	args	Unused Parameter. This could be used to pass extra arguments for the queue creation
    **/
    void create(qpid::broker::PersistableQueue& queue,
                const qpid::framing::FieldTable& args);
    /**
    *	The method destroys a Queue removing it from the database. Also the binding for the queue will be destroyed.
    *	@param	queue	Reference to the QPID Queue to destroy
    **/
    void destroy(qpid::broker::PersistableQueue& queue);
    /**
    *	The method create a new Exchange inside the store system, writing it into the exchanges' Berkeley Database.
    *	@param	exchange	The QPID Exchange to store inside the persistence-
    *	@param	args	Unused Parameter. This could be used to pass extra arguments for the exchange creation
    **/
    void create(const qpid::broker::PersistableExchange& exchange,
                const qpid::framing::FieldTable& args);
    /**
    *	The method destroys an Exchange, removing it from the database. Also the binding associated with the exchange persistence id will be
    *	removed.
    *	@param	exchange	Reference to the QPID Exchange to destroy.
    **/
    void destroy(const qpid::broker::PersistableExchange& exchange);

    /**
    *	The method register a bind operation inside the store system, writing the association between exchange, queue and a given key into the 
    *	bindings' Berkeley Database. The exchange's persistence ID is used as key inside bindings' database.
    *	@param	exchange	The exchange associated to the binding that has to be registered.
    *	@param	queue		The queue associated to the binding that has to be registered.
    *	@param	key		The routing key used to identify the binding
    *	@param	args		Unused parameter.
    **/
    void bind(const qpid::broker::PersistableExchange& exchange,
              const qpid::broker::PersistableQueue& queue,
              const std::string& key,
              const qpid::framing::FieldTable& args);
    /**
    *	The method delete binding from the persistence system. This method will delete from the database the binding representig assiciation between
    *	an exchange and a queue identified by a given key.
    *	@param	exchange	Reference to the exchange associated with the bind
    *	@param	queue		Reference to the queue associated with the bind
    *	@param	key		Reference to the string that contains routing key for the bind
    *	@param	args		Unused parameter.
    **/
    void unbind(const qpid::broker::PersistableExchange& exchange,
                const qpid::broker::PersistableQueue& queue,
                const std::string& key,
                const qpid::framing::FieldTable& args);
    /**
    *	The method will register into the persistence system the general configuration by writing it into a database.
    *	@param	config	Reference to the configuration to store.
    **/
    void create(const qpid::broker::PersistableConfig& config);
    /**
    *	The method will delete a general configuration from the database.
    *	@param	config	Reference to the configuration to destroy.
    **/
    void destroy(const qpid::broker::PersistableConfig& config);
    /**
    *	Recovery Methos that used the QPID Recovery Manager to recover all the objects saved inside the persistence store.
    *	This method recover from the databases the following objects:Queues (and messages for each queue), Exchanges, Bindings and General
    *	Configurations. All this recovery operation will be done inside a transactional context except for message recovering due to 
    *	lock limitation of the Berkeley Database Environment.
    *	After the recovery phase, this method drops all the transient messages (using discard_transient_message) and compact the database for
    *	each Journal recovered.
    *	@param	registry	Reference to QPID recovery manager used to recover QPID objects.
    **/
    void recover(qpid::broker::RecoveryManager& registry);
    /**
    *	In this implementation this method alway throud a mrg::journal::jexception for unimplemented method.
    *	@param	msg	Unused parameter
    **/
    void stage(const boost::intrusive_ptr<qpid::broker::PersistableMessage>& msg);
    /**
    *	In this implementation this method alway throud a mrg::journal::jexception for unimplemented method.
    *	@param	msg	Unused parameter
    **/
    void destroy(qpid::broker::PersistableMessage& msg);
    /**
    *	In this implementation this method alway throud a mrg::journal::jexception for unimplemented method.
    *	@param	msg	Unused parameter.
    *	@param	data	Unused Parameter.
    **/
    void appendContent(const boost::intrusive_ptr<const qpid::broker::PersistableMessage>& msg,
                       const std::string& data);
    /**
    *	The method recovers some data belonging to a message contained ina queue's external store.
    *	This method is generally used by the broker when flow to disk policy is active and it requires loading of the message data
    *	directly from the persistence database.
    *	After checking if the message is enqueued (is present inside the Journal), the method mrg::msgstore::JournalImpl::loadMsgContent
    *	is called.
    *	@param	queue	Reference to the queue that contains the message to load
    *	@param	msg	Reference to the QPID message to load
    *	@param  data	Reference to the data loaded
    *	@param	offset	Offset from the begining of the encoded message data
    *	@param	length	Lenght of the data to load
    *	@throws mrg::journal::jexception	In the following case: message without persistence id, message not found inside Journal,
    *						message external (no data in the database, only a key), generic database failure.
    **/
    void loadContent(const qpid::broker::PersistableQueue& queue,
                     const boost::intrusive_ptr<const qpid::broker::PersistableMessage>& msg,
                     std::string& data,
                     u_int64_t offset,
                     u_int32_t length);
    /**
    *	Method called from the QPID Broker to register the enqueue of a message inside a queue.
    *	This method create a Berkeley Database Transaction Context and call the store method to put the message in the appropriate
    *	Journal's external store (a berkeley db).
    *	@param	ctxt	The QPID transaction Context for the enqueue operation
    *	@param	msg	Reference to the message that has been enqueued and has to be store in the Journal
    *	@param	queue	Reference to the queue that contains the message.
    **/
    void enqueue(qpid::broker::TransactionContext* ctxt,
                 const boost::intrusive_ptr<qpid::broker::PersistableMessage>& msg,
                 const qpid::broker::PersistableQueue& queue);
    /**
    *	Method called from the QPID Broker to register the dequeue of a message from a queue.
    *	This method create a Berkeley Database Transaction Context and call the async_dequeue methos to delete asynchronously the message
    *	from the Database. 
    *	@param	ctxt	The QPID Transaction Context for the dequeue operation
    *	@param	msg	Reference to the message that has been extracted from the queue and has to be deleted from the Journal Database.
    *	@param	queue	Reference to the queue from which the message has been dequeued.
    **/
    void dequeue(qpid::broker::TransactionContext* ctxt,
                 const boost::intrusive_ptr<qpid::broker::PersistableMessage>& msg,
                 const qpid::broker::PersistableQueue& queue);
    /**
    *	Method called from the QPID Broker to force a flush of the cached data for Journal associated with a given queue.
    *	This method calls the mrg::msgstore::JournalImpl::flush method.
    *	@param	queue	Reference to the queue whose Journal has to be flushed.
    **/
    void flush(const qpid::broker::PersistableQueue& queue);
    /**
    *	This method always return zero after checkInit() call.
    *	@param	queue	Unused Parameter.
    *	@return	Zero
    **/
    u_int32_t outstandingQueueAIO(const qpid::broker::PersistableQueue& queue);
    /**
    *	This method is empty in this implementation.
    *	@param	xids	Unused parameter
    *	\todo	Implement Transaction
    **/
    void collectPreparedXids(std::set<std::string>& xids);
    /**
    *	This method will always throws mrg::journal::jexception for unimplemented method.
    *	@return	Always throws, no return
    *	@throws	mrg::journal::jexception	Unimplemented Method.
    *	\todo	Implement method to support transaction
    **/
    std::auto_ptr<qpid::broker::TransactionContext> begin();
    /**
    *	This metod will always throws mrg::journal::jexception for unimplemented method
    *	@param	xid	Unused parameter.
    *	@return Always throws, no return
    *	@throws mrg::journal::jexception	Unimplemented method
    *	\todo	Implement method to support transaction
    **/
    std::auto_ptr<qpid::broker::TPCTransactionContext> begin(const std::string& xid);
    /**
    *	This metod will always throws mrg::journal::jexception for unimplemented method
    *	@param	ctxt	Unused parameter.
    *	@throws mrg::journal::jexception	Unimplemented method
    *	\todo	Implement method to support transaction
    **/
    void prepare(qpid::broker::TPCTransactionContext& ctxt);
    /**
    *	This metod will always throws mrg::journal::jexception for unimplemented method
    *	@param	ctxt	Unused parameter.
    *	@throws mrg::journal::jexception	Unimplemented method
    *	\todo	Implement method to support transaction
    **/
    void localPrepare(TxnCtxt* ctxt);
    /**
    *	This metod will always throws mrg::journal::jexception for unimplemented method
    *	@param	ctxt	Unused parameter.
    *	@throws mrg::journal::jexception	Unimplemented method
    *	\todo	Implement method to support transaction
    **/
    void commit(qpid::broker::TransactionContext& ctxt);
    /**
    *	This metod will always throws mrg::journal::jexception for unimplemented method
    *	@param	ctxt	Unused parameter.
    *	@throws mrg::journal::jexception	Unimplemented method
    *	\todo	Implement method to support transaction
    **/
    void abort(qpid::broker::TransactionContext& ctxt);
    /**
    *	Management method to get the management object associated with the store.
    *	@return	The store management object.
    **/
    qpid::management::ManagementObject* GetManagementObject (void) const
        { return mgmtObject; }
    /**
    *	Management method to get management status.
    *	@return	Always return qpid::management::Manageable::STATUS_OK
    **/
    inline qpid::management::Manageable::status_t ManagementMethod (u_int32_t, qpid::management::Args&)
        { return qpid::management::Manageable::STATUS_OK; }
    /**
    *	Method to obtain the Store Directory 
    *	@return	String contains the Store Directory
    **/
    std::string getStoreDir() const;

  private:
    /**
    *	The method registers when a Journal is deleted and removes it from the internal data structures.
    *	@param	j	Reference to the Journal that has been deleted.
    **/
    void journalDeleted(JournalImpl& j);

}; // class BdbMessageStoreImpl

} // namespace msgstore
} // namespace mrg

#endif
