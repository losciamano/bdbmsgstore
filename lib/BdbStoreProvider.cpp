#include "qpid/broker/Queue.h"
#include "StoreOptions.h"
#include "MessageStorePlugin.h"
#include "qpid/broker/Broker.h"
#include "qpid/Plugin.h"
#include "qpid/Options.h"
#include "qpid/DataDir.h"
#include "qpid/broker/MessageStore.h"
#include "StoreException.h"
#include "db-inc.h"
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include "jrnl/jdir.hpp"
#include "jrnl/jexception.hpp"
#include "IdSequence.h"
#include "IdDbt.h"
#include "BufferValue.h"
#include "TxnCtxt.h"
#include "JournalImpl.h"
#include "BindingDbt.h"
#include "Cursor.h"
#include "mongo/client/dbclient.h"
#include "mongo/bson/util/builder.h"
#include "mongo/client/connpool.h"
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <assert.h>
#include "ext/boost/threadpool.hpp"
#include "qmf/com/atono/server/qpid/bdbstore/Package.h"
#include "qmf/com/atono/server/qpid/bdbstore/StorageProvider.h"
#include "AsyncOperationLogger.h"
#include "qpid/management/Manageable.h"
#include "PidTracker.h"

using namespace mrg::msgstore;
using namespace qpid::broker;
namespace _qmf = qmf::com::atono::server::qpid::bdbstore;

namespace qpid{
namespace store{
namespace bdb{

qpid::sys::Mutex TxnCtxt::globalSerialiser;

class BdbStoreProvider : public qpid::store::StorageProvider, public qpid::management::Manageable
{
	protected:
		/**
	         *	Convenience type definition for a shared pointer to a Berkeley Database Handle
		**/
		typedef boost::shared_ptr<Db> DbPtr; 
		/**
		 *	Convenience type definition for a map of pointer to a JournalImpl identified by the queue name
		 **/
		typedef std::map<uint64_t, JournalImpl*> JournalListMap;
		/**
		 *	Convenience type definition for a map iterator
		 **/
		typedef JournalListMap::iterator JournalListMapItr;

		void finalizeMe();
		void dump();
	public:
    		BdbStoreProvider();
    		~BdbStoreProvider();

		virtual qpid::Options* getOptions() { return &options; }	
		virtual void earlyInitialize (Plugin::Target& target);
		virtual void initialize(Plugin::Target& target);

		/**
		 * Receive notification that this provider is the one that will actively
		 * handle provider storage for the target. If the provider is to be used,
		 * this method will be called after earlyInitialize() and before any
		 * recovery operations (recovery, in turn, precedes call to initialize()).
		 */
		virtual void activate(MessageStorePlugin &store);

		/**
		* @name Methods inherited from qpid::broker::MessageStore
		*/
		//@{
		/**
		* If called after init() but before recovery, will discard the database
		* and reinitialize using an empty store dir. If @a pushDownStoreFiles
		* is true, the content of the store dir will be moved to a backup dir
		* inside the store dir. This is used when cluster nodes recover and must
		* get thier content from a cluster sync rather than directly fromt the
		* store.
		*
		* @param pushDownStoreFiles If true, will move content of the store dir
		*                           into a subdir, leaving the store dir
		*                           otherwise empty.
		*/
		virtual void truncateInit(const bool pushDownStoreFiles = false);

		/**
		 * Record the existence of a durable queue
		 */
		virtual void create(PersistableQueue& queue, const qpid::framing::FieldTable& args);
		/**
		 * Destroy a durable queue
		 */
		virtual void destroy(PersistableQueue& queue);
		/**
		 * Record the existence of a durable exchange
		 */
		virtual void create(const PersistableExchange& exchange, const qpid::framing::FieldTable& args);
		/**
		 * Destroy a durable exchange
		 */
		virtual void destroy(const PersistableExchange& exchange);

		/**
		 * Record a binding
		 */
		virtual void bind(const PersistableExchange& exchange,
			      const PersistableQueue& queue,
			      const std::string& key,
			      const qpid::framing::FieldTable& args);

		/**
		 * Forget a binding
		 */
		virtual void unbind(const PersistableExchange& exchange,
				const PersistableQueue& queue,
				const std::string& key,
				const qpid::framing::FieldTable& args);

		/**
		 * Record generic durable configuration
		 */
		virtual void create(const PersistableConfig& config);

		/**
		 * Destroy generic durable configuration
		 */
		virtual void destroy(const PersistableConfig& config);

		/**
		 * Stores a messages before it has been enqueued
		 * (enqueueing automatically stores the message so this is
		 * only required if storage is required prior to that
		 * point). If the message has not yet been stored it will
		 * store the headers as well as any content passed in. A
		 * persistence id will be set on the message which can be
		 * used to load the content or to append to it.
		*/
		virtual void stage(const boost::intrusive_ptr<PersistableMessage>& msg);

		/**
		 * Destroys a previously staged message. This only needs
		 * to be called if the message is never enqueued. (Once
		 * enqueued, deletion will be automatic when the message
		 * is dequeued from all queues it was enqueued onto).
		 */
		virtual void destroy(PersistableMessage& msg);

		/**
		 * Appends content to a previously staged message
		 */
		virtual void appendContent(const boost::intrusive_ptr<const PersistableMessage>& msg,
			       const std::string& data);

		/**
		 * Loads (a section) of content data for the specified
		 * message (previously stored through a call to stage or
		 * enqueue) into data. The offset refers to the content
		 * only (i.e. an offset of 0 implies that the start of the
		 * content should be loaded, not the headers or related
		 * meta-data).
		 */
		virtual void loadContent(const qpid::broker::PersistableQueue& queue,
				     const boost::intrusive_ptr<const PersistableMessage>& msg,
				     std::string& data,
				     uint64_t offset,
				     uint32_t length);

		/**
		 * Enqueues a message, storing the message if it has not
		 * been previously stored and recording that the given
		 * message is on the given queue.
		 *
		 * Note: that this is async so the return of the function does
		 * not mean the opperation is complete.
		 *
		 * @param msg the message to enqueue
		 * @param queue the name of the queue onto which it is to be enqueued
		 * @param xid (a pointer to) an identifier of the
		 * distributed transaction in which the operation takes
		 * place or null for 'local' transactions
		 */
		virtual void enqueue(qpid::broker::TransactionContext* ctxt,
				 const boost::intrusive_ptr<PersistableMessage>& msg,
				 const PersistableQueue& queue);

		/**
		 * Dequeues a message, recording that the given message is
		 * no longer on the given queue and deleting the message
		 * if it is no longer on any other queue.
		 *
		 * Note: that this is async so the return of the function does
		 * not mean the opperation is complete.
		 *
		 * @param msg the message to dequeue
		 * @param queue the name of the queue from which it is to be dequeued
		 * @param xid (a pointer to) an identifier of the
		 * distributed transaction in which the operation takes
		 * place or null for 'local' transactions
		 */
		virtual void dequeue(qpid::broker::TransactionContext* ctxt,
				 const boost::intrusive_ptr<PersistableMessage>& msg,
				 const PersistableQueue& queue);

		/**
		 * Flushes all async messages to disk for the specified queue
		 *
		 * Note: this is a no-op for this provider.
		 *
		 * @param queue the name of the queue from which it is to be dequeued
		 */
		virtual void flush(const PersistableQueue& queue);

		/**
		 * Returns the number of outstanding AIO's for a given queue
		 *
		 * If 0, than all the enqueue / dequeues have been stored
		 * to disk
		 *
		 * @param queue the name of the queue to check for outstanding AIO
		 */
		virtual uint32_t outstandingQueueAIO(const PersistableQueue&/* queue*/)
		{
			return 0;
		}
		//@}

		/**
		 * @name Methods inherited from qpid::broker::TransactionalStore
		 */
		//@{
		virtual std::auto_ptr<qpid::broker::TransactionContext> begin();
		virtual std::auto_ptr<qpid::broker::TPCTransactionContext> begin(const std::string& xid);
		virtual void prepare(qpid::broker::TPCTransactionContext& txn);
		virtual void commit(qpid::broker::TransactionContext& txn);
		virtual void abort(qpid::broker::TransactionContext& txn);
		virtual void collectPreparedXids(std::set<std::string>& xids);
		//@}

		virtual void recoverConfigs(qpid::broker::RecoveryManager& recoverer);
		virtual void recoverExchanges(qpid::broker::RecoveryManager& recoverer,
					  ExchangeMap& exchangeMap);
		virtual void recoverQueues(qpid::broker::RecoveryManager& recoverer,
				       QueueMap& queueMap);
		virtual void recoverBindings(qpid::broker::RecoveryManager& recoverer,
					 const ExchangeMap& exchangeMap,
					 const QueueMap& queueMap);
		virtual void recoverMessages(qpid::broker::RecoveryManager& recoverer,
					 MessageMap& messageMap,
					 MessageQueueMap& messageQueueMap);
		virtual void recoverTransactions(qpid::broker::RecoveryManager&/* recoverer*/,
					PreparedTransactionMap& /*dtxMap*/){}
		const char* id() {return "BdbStoreProvider";}
		virtual qpid::management::ManagementObject* GetManagementObject() const { return this->mgmtObject; }

	private:
		StoreOptions options;
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
		*	List containing pointer to all db opened
		**/
		std::list< DbPtr > dbs;
		/**
		*	Pointer to the Berkeley Database Environment
		**/
		boost::shared_ptr<DbEnv> dbenv;
		/**
		*	Pointer to the BDB containing information about queues
		**/
		DbPtr queueDb;
		/**
		*	Pointer to the BDB containing information about configuration
		**/
		DbPtr configDb;
		/**
		*	Pointer to the BDB containing information about exchanges
		**/
		DbPtr exchangeDb;
		/**
		*	Pointer to the BDB containing information about mappings
		**/
		DbPtr mappingDb;
		/**
		*	Pointer to the BDB containing information about bindings
		**/
		DbPtr bindingDb;
		/**
		*	Pointer to the BDB containing information about general configurations
		**/
		DbPtr generalDb;
	    	/**
    		*	Highest value of the persistence ID
    		**/
   		u_int64_t highestRid;
    		/**
    		*	Flag indicating if the Message Store is initialized
    		**/
    		bool isInit;
		/**
		*	Boost Thread that handle unused log delete
		**/
		boost::shared_ptr<boost::thread> eraserThread;
   		 /**
		  *	List containing pointer to all recovered Journal
		 **/
		std::list<qpid::broker::RecoverableQueue::shared_ptr> recoveredJournal;
		/**
		*	Vector containing accepted messages loaded from mongo
		**/
		std::vector<std::string> acceptedFromMongo;
		/**
		*	Number of file composing the journal
		**/
		int num_jfiles;
		int num_thread_enqueue;
		int num_thread_dequeue;
		/**
		 *	Data structure containings association between queue name e JournalImpl object
		 **/
		JournalListMap journalList;
		/**
		 *	Lock over Journal List access
   		 **/	
		qpid::sys::Mutex journalListLock;
		/**
		*	Last inserted Persistence id
		**/
		uint64_t lastPid;
		/**
		*	Set containing gap separated id
		**/
		std::set<uint64_t> gapPidSet;
		/**
		*	Lock over Last Pid access
		**/
		qpid::sys::Mutex lastPidLock;
		/**
		*	Condition Variable for not enqueued
		**/
		qpid::sys::Condition notYetEnqueued;
		/**
		*	Management Agent used to communicate with QMF
		**/
		qpid::management::ManagementAgent* agent;
    		qmf::com::atono::server::qpid::bdbstore::StorageProvider* mgmtObject;

		boost::threadpool::pool enqueuePool;
		boost::threadpool::pool dequeuePool;
		AsyncOperationLogger* aologger;	
		PidTracker tracker;
		boost::shared_ptr<boost::thread> enqLogCleanerThread;
		boost::shared_ptr<boost::thread> deqLogCleanerThread;
		qpid::broker::Broker* broker;

		/**
		*	Default store directory
		**/
		static const std::string storeTopLevelDir;

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
		 *	This method is used to check if the store is initialized. If it's not initialized, the init() method will be call.
		 **/
		inline void checkInit() 
		{
        		if (!isInit) 
			{ 
				init("/tmp"); 
				isInit = true;
			}
		}
		/**
		*	The method registers when a Journal is deleted and removes it from the internal data structures.
		*	@param	j	Reference to the Journal that has been deleted.
		**/
		void journalDeleted(JournalImpl& j);

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
		*	The method returns the base directory for async log environment.
		*	@return	The base directory for async log environment
		**/
		std::string getAsyncEnvDir();

		/**
		*	Method to obtain the Store Directory 
		*	@return	String contains the Store Directory
		**/
		std::string getStoreDir() const;	
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
	    	*	Method used to open a database.
	    	*	@param	db	Pointer to the database handle
	    	*	@param	txn	Transaction context for the operation. If null/0 the operation will be executed outside transaction
	    	*	@param	file	Char array containing the path to the file containing the database to open
	    	*	@param	dupKey	If this flag is set to True the database will accept duplicate key, otherwise (if set to False) the database will
	    	*			return an error if trying to insert an element with a key already contained in the database.
	    	**/
	    	void open(DbPtr db, DbTxn* txn, const char* file, bool dupKey);
	    	/**
	    	*	Method for closing all the databases used by the store.
	    	**/	
	    	void closeDbs();
    		/**
		 *	Method used to insert a generic persistable object in a given database and assign the next id in the sequence as 
		 *	the persistence Id of the object.
		 *	@param	db	Pointer to the database
		 *	@param	seq	Reference to the Sequence used to obtain the new Persistence Id
		 *	@param	p	Reference to the persistable object that will be inserted
	 	 *	@return	True if the object has been inserted correctly, False if the persistence id is already in the database.
		**/
		bool create(DbPtr db, IdSequence& seq,const qpid::broker::Persistable& p);
		/**
		*	Method used to insert an entry in the database. Transactional put is supported.
		*	@param	db	Pointer to the database handle
		*	@param	txn	Transaction context for the operation. If null/0 the operation will be executed outside a transaction
		*	@param	key	Reference to the key of the database entry
		*	@param	value	Reference to the value of the database entry
		**/
		void put(DbPtr db,
			 DbTxn* txn,
			 Dbt& key,
			 Dbt& value);
		/**
    		*	Method used to delete a generic persistable object identified by a Persistence Id from the given database.
		*	@param	db	Pointer to the database
		*	@param	p	Reference to the persistable object that will be deleted
		*/
		void destroy(DbPtr db, const qpid::broker::Persistable& p);
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
		*	@param	jc		Pointer to the Journal Object where the message has to be stored
		*	@param	buff		Buffer containing the encoded message
		*	@param	size		Encoded size of the message
		*	@param	pid		Persistence Id of the message
		*	@param	transient	Flag that indicates if message is transient or persistent
		**/
		void store(JournalImpl* jc,
                           std::vector<char>& buff,
			   const uint64_t size,
                           uint64_t pid,
                           bool transient);
		/**
		*	Method used to register a dequeue asynchronous.
		*	@param	msgId	Persistence identifier of the Message to dequeue
		*	@param	jc	Pointer to the queue that contains the message
		*/
		void async_dequeue(uint64_t msgId,
				   JournalImpl* jc);
		/**
		 *	Method for recovering message informations from the Journal
		 *	This method uses the recoverMessages method of the JournalImpl to get messages from the Berkeley Db.
		 *	@param	recovery	Reference to the Recovery Manager used to recover QPID Objects
		 *	@param	queue		Qpid queue that will contains the message recovered
		 *	@param	messageMap	Map containing reference between id and messages
		 *	@param	messageQueueMap	Map containing reference between msgid and queue id
		 *	@param	foundMsg	Reference to the vector that will contain the message not recovered due to thiers accepted state.
		 *	@param	adset		Reference to the async dequeue set
		 *	@param	aeset		Reference to the async enqueue set
		 *	@param	rcnt		Reference used as output parameter for recovered message counter
		 *	@param	acnt		Reference used as output parameter for accepted message counter
		 *	@param	tcnt		Reference used as output parameter for transient message counter
		 *	@return	The highest persistence id read from the db
		 **/
		uint64_t decodeAndRecoverMsg(qpid::broker::RecoveryManager& recovery,
					qpid::broker::RecoverableQueue::shared_ptr queue,
					MessageMap& messageMap,
					MessageQueueMap& messageQueueMap,
					std::vector<std::string>& foundMsg,
					std::set<PendingAsyncDequeue>* adset,
					std::set<PendingAsyncEnqueue>* aeset,
                                      	long& rcnt,
				      	long& acnt,
				      	long& tcnt);
		void deleteUnusedLog();
		void initManagement();
};

/**
*	Static instance of the BdbStoreProvider
**/
static BdbStoreProvider instance; // Static initialization.

const std::string BdbStoreProvider::storeTopLevelDir("bdbstore");

BdbStoreProvider::BdbStoreProvider() : 	highestRid(0),
                                 	isInit(false),
                                 	num_jfiles(4),
					num_thread_enqueue(2),
					num_thread_dequeue(2),
					lastPid(0),
					agent(0)
{
}

BdbStoreProvider::~BdbStoreProvider()
{
	finalizeMe();
	try 
	{
        	closeDbs();
	} catch (const DbException& e) 
	{
	        QPID_LOG(error, "Error closing BDB databases: " <<  e.what());
	} catch (const mrg::journal::jexception& e) 
	{
		QPID_LOG(error, "Error: " << e.what());
	} catch (const std::exception& e) 
	{
       		QPID_LOG(error, "Error: " << e.what());
	} catch (...) 
	{
	        QPID_LOG(error, "Unknown error in BdbMessageStoreImpl::~BdbMessageStoreImpl()");
	}
}

void
BdbStoreProvider::earlyInitialize(Plugin::Target &target)
{
	MessageStorePlugin *store = dynamic_cast<MessageStorePlugin *>(&target);
	if (store) 
	{
        	// If the database init fails, report it and don't register; give
        	// the rest of the broker a chance to run.
        	//
        	// Don't try to initConnection() since that will fail if the
        	// database doesn't exist. Instead, try to open a connection without
        	// a database name, then search for the database. There's still a
        	// chance this provider won't be selected for the store too, so be
        	// be sure to close the database connection before return to avoid
        	// leaving a connection up that will not be used.
        	try 
		{
			this->broker = store->getBroker();
			DataDir& dataDir = broker->getDataDir ();
		        if (options.storeDir.empty ())
        		{
		        	if (!dataDir.isEnabled ())
                			throw StoreException ("bdbmsgstore: If --data-dir is blank or --no-data-dir is specified, "
							"--store-dir must be present.");
			        options.storeDir = dataDir.getPath ();
		        }
			if (init(getOptions()))
			{
				store->providerAvailable("BDB", this);
			} else
			{ 
				return;
			}
				
        	} catch (qpid::Exception &e) 
		{
            		QPID_LOG(error, e.what());
            		return;
        	}
        	store->addFinalizer(boost::bind(&BdbStoreProvider::finalizeMe, this));
    	}
}

bool BdbStoreProvider::init(const qpid::Options* /*opts*/)
{
    // Pass option values to init(...)
    return init(options.storeDir);
}

bool BdbStoreProvider::init(const std::string& dir,const bool truncateFlag)
{
	if (isInit) return true;
	if (dir.size()>0) storeDir = dir;
	if (storeDir.empty())
	{
		struct passwd *pw = getpwuid(geteuid());
		const char *homedir = pw->pw_dir;
		storeDir = std::string(homedir);
	}
	initManagement();
	if (truncateFlag) 
	{
        	truncateInit(false);
	} else
	{
		const int retryMax = 3;
		int bdbRetryCnt = 0;
    		do 
		{
        		if (bdbRetryCnt++ > 0)
        		{
            			closeDbs();
            			::usleep(1000000); // 1 sec delay
            			QPID_LOG(error, "Previoius BDB store initialization failed, retrying (" << bdbRetryCnt << " of " << retryMax << ")...");
        		}

			try 
			{
				std::string asyncDir=getAsyncEnvDir();
				mrg::journal::jdir::create_dir(getBdbBaseDir());
				mrg::journal::jdir::create_dir(asyncDir);
				aologger = new AsyncOperationLogger(asyncDir);
				dbenv.reset(new DbEnv(0));
				dbenv->set_errpfx("bdbmsgstore");
				dbenv->set_lg_regionmax(256000); // default = 65000
				dbenv->set_lk_detect(DB_LOCK_DEFAULT);
				uint32_t open_flags = DB_THREAD | DB_CREATE | DB_INIT_TXN | DB_INIT_LOCK | DB_INIT_LOG | DB_INIT_MPOOL | DB_USE_ENVIRON | DB_RECOVER;
				dbenv->open(getBdbBaseDir().c_str(), open_flags, 0);
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
				try 
				{
					open(queueDb, txn.get(), "queues.db", false);
					open(configDb, txn.get(), "config.db", false);
					open(exchangeDb, txn.get(), "exchanges.db", false);
					open(mappingDb, txn.get(), "mappings.db", true);
					open(bindingDb, txn.get(), "bindings.db", true);
					open(generalDb, txn.get(), "general.db",  false);
					txn.commit();
				} catch (...) { txn.abort(); throw; }

				this->enqueuePool=boost::threadpool::pool(num_thread_enqueue);
				this->dequeuePool=boost::threadpool::pool(num_thread_dequeue);
				this->eraserThread=boost::shared_ptr<boost::thread>(new boost::thread
				(
					boost::bind(&BdbStoreProvider::deleteUnusedLog,this)
				));
				isInit = true;
			} catch (const DbException& e) 
			{
				if (e.get_errno() == DB_VERSION_MISMATCH)
				{
					QPID_LOG(error, "Database environment mismatch: This version of db4 does not match that which created the store"
							"database.: " << e.what());
					THROW_STORE_EXCEPTION("Database environment mismatch: This version of db4 does not match that which created"
							"the store database. (If recovery is not important, delete the contents of the store directory."
							"Otherwise, try upgrading the database using db_upgrade or using db_recover - but the db4-utils"
							"package must also be installed to use these utilities.)");
				}
				QPID_LOG(error, "BDB exception occurred while initializing store: " << e.what());
				if (bdbRetryCnt >= retryMax)
					THROW_STORE_EXCEPTION("BDB exception occurred while initializing store");
			} catch (const StoreException&) 
			{
				throw;
			} catch (const mrg::journal::jexception& e) 
			{
				QPID_LOG(error, "Journal Exception occurred while initializing store: " << e);
				THROW_STORE_EXCEPTION("Journal Exception occurred while initializing store");
			} catch (...) 
			{
            			QPID_LOG(error, "Unknown exception occurred while initializing store.");
            			throw;
			}
		} while (!isInit);
	}
	return isInit;
}
void BdbStoreProvider::initialize(Plugin::Target& /*target*/) 
{
}
void BdbStoreProvider::activate(MessageStorePlugin& /*store*/) 
{
	QPID_LOG(info,"Berkeley Db Store Provider activated");
}
void BdbStoreProvider::truncateInit(const bool /*pushDownStoreFiles*/) 
{
	if (isInit) 
	{
        	{
		        qpid::sys::Mutex::ScopedLock sl(journalListLock);
		        if (journalList.size())  // check no queues exist
			{
	               		std::ostringstream oss;
			        oss << "truncateInit() called with " << journalList.size() << " queues still in existence";
			        THROW_STORE_EXCEPTION(oss.str());
		        }
	        }
        	closeDbs();
	        dbs.clear();
        	dbenv->close(0);
        	isInit = false;
    	}
    	init(getOptions());

}
void BdbStoreProvider::finalizeMe()
{
    enqueuePool.clear();
    dequeuePool.clear();
    enqueuePool.wait(0);
    dequeuePool.wait(0);
    {
        qpid::sys::Mutex::ScopedLock sl(journalListLock);
        for (JournalListMapItr i = journalList.begin(); i != journalList.end(); i++)
        {
            JournalImpl* jQueue = i->second;
	    if (jQueue!=0x0) 
	    {
	            jQueue->resetDeleteCallback();
	            if (jQueue->is_ready()) jQueue->stop(true);
	    }
	    journalList.erase(i);
        }
    }
    if (aologger)
    {
	    delete aologger;
	    aologger=0x0;
    }
}
void BdbStoreProvider::initManagement ()
{
    if (broker != 0) {
       	    agent = broker->getManagementAgent();
            _qmf::Package packageInitializer(agent);
            mgmtObject = new _qmf::StorageProvider(agent, this, broker);
            mgmtObject->set_location(storeDir);
	    mgmtObject->set_bdbBaseDir(getBdbBaseDir());
    	    mgmtObject->set_type("BDB");
    	    mgmtObject->set_num_jfiles(this->num_jfiles);
	    mgmtObject->set_num_thread_dequeue(this->num_thread_dequeue);
	    mgmtObject->set_num_thread_enqueue(this->num_thread_enqueue);
	    mgmtObject->set_compact(this->options.compactFlag);
	    mgmtObject->set_smart_async(this->options.enableSmartAsync);
	    mgmtObject->set_acceptRecovery(this->options.enableAcceptRecoveryFlag);
	    mgmtObject->set_mongoHost(this->options.acceptRecoveryMongoHost);
	    mgmtObject->set_mongoPort(this->options.acceptRecoveryMongoPort);
	    mgmtObject->set_mongoDbName(this->options.acceptRecoveryMongoDb);
	    mgmtObject->set_mongoCollection(this->options.acceptRecoveryMongoCollection);
	    mgmtObject->set_enqLogCleanerTimeInterval(this->options.enqLogCleanerTimeInterval);
	    mgmtObject->set_deqLogCleanerTimeInterval(this->options.deqLogCleanerTimeInterval);
	    mgmtObject->set_enqLogCleanerWarningSize(this->options.enqLogCleanerWarningSize);
	    mgmtObject->set_deqLogCleanerWarningSize(this->options.deqLogCleanerWarningSize);
            agent->addObject(mgmtObject, 0, true);
    }
}
std::string BdbStoreProvider::getJrnlBaseDir()
{
	std::ostringstream dir;
    	dir << "jrnl/";
    	return dir.str();
}

std::string BdbStoreProvider::getBdbBaseDir()
{
    	std::ostringstream dir;
   	dir << storeDir << "/" << storeTopLevelDir << "/dat/" ;
    	return dir.str();
}

std::string BdbStoreProvider::getAsyncEnvDir()
{
    	std::ostringstream dir;
   	dir << storeDir << "/" << storeTopLevelDir << "/async/" ;
    	return dir.str();
}


std::string BdbStoreProvider::getJrnlDir(const qpid::broker::PersistableQueue& queue) //for exmaple /var/rhm/ + queueDir/
{
	return getJrnlHashDir(queue.getName().c_str());
}

u_int32_t BdbStoreProvider::bHash(const std::string str)
{
	// Daniel Bernstein hash fn
	u_int32_t h = 0;
	for (std::string::const_iterator i = str.begin(); i < str.end(); i++)
        	h = 33*h + *i;
	return h;
}

std::string BdbStoreProvider::getJrnlHashDir(const std::string& queueName) //for exmaple /var/rhm/ + queueDir/
{
	std::stringstream dir;
	dir << getJrnlBaseDir() << std::hex << std::setfill('0') << std::setw(4);
	dir << (bHash(queueName.c_str()) % 29); // Use a prime number for better distribution across dirs
	dir << "/" << queueName << "/";
	return dir.str();
}

std::string BdbStoreProvider::getStoreDir() const { return storeDir; }

void BdbStoreProvider::journalDeleted(JournalImpl& j) {
	qpid::sys::Mutex::ScopedLock sl(journalListLock);
    	journalList.erase(j.pid());
}

void BdbStoreProvider::open(DbPtr db, DbTxn* txn, const char* file,bool dupKey)
{
	if(dupKey) db->set_flags(DB_DUPSORT);
    	db->open(txn, file, 0, DB_BTREE, DB_CREATE | DB_THREAD, 0);
}

void BdbStoreProvider::closeDbs()
{
    	for (std::list<DbPtr >::iterator i = dbs.begin(); i != dbs.end(); i++) {
        	(*i)->close(0);
    	}
    	dbs.clear();
}
void BdbStoreProvider::deleteUnusedLog() {
	int ret;
	char** list;	
	char** begin;
	while (true) 
	{
		if ((ret = dbenv->txn_checkpoint(0,0,0))!=0) {
			dbenv->err(ret,"txn checkpoint");
			THROW_STORE_EXCEPTION("Error generating a checkpoint");
		}
		int log_count=0;
		if ((ret = dbenv->log_archive(&list, DB_ARCH_ABS)) != 0) 
		{
			dbenv->err(ret, "log_archive");
			THROW_STORE_EXCEPTION("Error getting unused log files");
		}
		/* Remove the log files. */
		if (list != NULL) 
		{
			for (begin = list; *list != NULL; ++list) 
			{
				log_count++;
				if ((ret = remove(*list)) != 0) 
				{
					dbenv->err(ret, "remove %s", *list);
					THROW_STORE_EXCEPTION("Error removing unused log files");
				}
			}
			free (begin);
		}
		QPID_LOG(info,"Deleted "+boost::lexical_cast<std::string>(log_count)+" unused log files");
		boost::this_thread::sleep(boost::posix_time::seconds(300));
	}
}

bool BdbStoreProvider::create(DbPtr db, IdSequence& seq, const qpid::broker::Persistable& p)
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

void BdbStoreProvider::put(DbPtr db,
                          DbTxn* txn,
                          Dbt& key,
                          Dbt& value)
{
	try 
	{
        	int status = db->put(txn, &key, &value, DB_NODUPDATA);
	        if (status == DB_KEYEXIST) 
		{
		        THROW_STORE_EXCEPTION("duplicate data");
	        } else if (status)
		{	
            		THROW_STORE_EXCEPTION(DbEnv::strerror(status));
	        }
	} catch (const DbException& e) 
	{
        	THROW_STORE_EXCEPTION(e.what());
	}
}
void BdbStoreProvider::destroy(DbPtr db, const qpid::broker::Persistable& p)
{
    	IdDbt key(p.getPersistenceId());
    	db->del(0, &key, DB_AUTO_COMMIT);
}

void BdbStoreProvider::create(PersistableQueue& queue,const qpid::framing::FieldTable& /*args*/)
{
	checkInit();
    	if (queue.getPersistenceId()) 
	{
        	THROW_STORE_EXCEPTION("Queue already created: " + queue.getName());
    	}
    	JournalImpl* jQueue = 0;
    	qpid::framing::FieldTable::ValuePtr value;

    	if (queue.getName().size() == 0)
    	{
        	QPID_LOG(error, "Cannot create store for empty (null) queue name - ignoring and attempting to continue.");
        	return;
    	}
    	try {
        	if (!create(queueDb, queueIdSequence, queue)) 
		{
           		THROW_STORE_EXCEPTION("Queue already exists: " + queue.getName());
	        }
	} catch (const DbException& e) 
	{
        	THROW_STORE_EXCEPTION("Error creating queue named  " + queue.getName());
    	}
	std::stringstream ss;
    	ss << getBdbBaseDir() << getJrnlDir(queue);
    	mrg::journal::jdir::create_dir(ss.str());
    	jQueue = new JournalImpl(/*timer,*/ queue.getName(),queue.getPersistenceId(),getJrnlDir(queue),getBdbBaseDir(),this->num_jfiles,
                             /*defJournalGetEventsTimeout, defJournalFlushTimeout,*/ agent,dbenv,
                             boost::bind(&BdbStoreProvider::journalDeleted, this, _1));
    	queue.setExternalQueueStore(dynamic_cast<qpid::broker::ExternalQueueStore*>(jQueue));
    	try {
        	// init will create the deque's for the init...
        	jQueue->initialize();
    	} catch (const mrg::journal::jexception& e) {
        	THROW_STORE_EXCEPTION(std::string("Queue ") + queue.getName() + ": create() failed: " + e.what());
    	}
	{
        	qpid::sys::Mutex::ScopedLock sl(journalListLock);
        	journalList[queue.getPersistenceId()]=jQueue;
    	}

}

void BdbStoreProvider::destroy(PersistableQueue& queue)
{
	checkInit();
	destroy(queueDb, queue);
	deleteBindingsForQueue(queue);
	qpid::broker::ExternalQueueStore* eqs = queue.getExternalQueueStore();
	if (eqs) 
	{
		JournalImpl* jQueue = static_cast<JournalImpl*>(eqs);
		jQueue->delete_jrnl_files();
		queue.setExternalQueueStore(0); // will delete the journal if exists
		{
		    qpid::sys::Mutex::ScopedLock sl(journalListLock);
		    journalList.erase(queue.getPersistenceId());
		}
	}
}
void BdbStoreProvider::create(const qpid::broker::PersistableExchange& exchange,const qpid::framing::FieldTable& /*args*/)
{
	checkInit();
	if (exchange.getPersistenceId()) 
	{
        	THROW_STORE_EXCEPTION("Exchange already created: " + exchange.getName());
    	}
    	try 
	{
        	if (!create(exchangeDb, exchangeIdSequence, exchange)) 
		{
	            THROW_STORE_EXCEPTION("Exchange already exists: " + exchange.getName());
        	}
	} catch (const DbException& e) 
	{
        	THROW_STORE_EXCEPTION_2("Error creating exchange named " + exchange.getName(), e);
	}
}

void BdbStoreProvider::destroy(const qpid::broker::PersistableExchange& exchange)
{
	checkInit();
	destroy(exchangeDb, exchange);
	//need to also delete bindings
	IdDbt key(exchange.getPersistenceId());
	bindingDb->del(0, &key, DB_AUTO_COMMIT);
}

void BdbStoreProvider::bind(const qpid::broker::PersistableExchange& e,
                           const qpid::broker::PersistableQueue& q,
                           const std::string& k,
                           const qpid::framing::FieldTable& a)
{
	checkInit();
	IdDbt key(e.getPersistenceId());
	BindingDbt value(e, q, k, a);
	TxnCtxt txn;
	txn.begin(dbenv.get(), true);
	try 
	{
		put(bindingDb, txn.get(), key, value);
		txn.commit();
	} catch (...) 
	{
		txn.abort();
		throw;
	}
}

void BdbStoreProvider::unbind(const qpid::broker::PersistableExchange& e,
                             const qpid::broker::PersistableQueue& q,
                             const std::string& k,
                             const qpid::framing::FieldTable&)
{
    	checkInit();
    	deleteBinding(e, q, k);
}

void BdbStoreProvider::create(const qpid::broker::PersistableConfig& general)
{
	checkInit();
    	if (general.getPersistenceId()) 
	{
		THROW_STORE_EXCEPTION("General configuration item already created");
	}
	try 
	{
		if (!create(generalDb, generalIdSequence, general)) 
		{
		    	THROW_STORE_EXCEPTION("General configuration already exists");
		}
	} catch (const DbException& e) 
	{
		THROW_STORE_EXCEPTION_2("Error creating general configuration", e);
	}
}

void BdbStoreProvider::destroy(const qpid::broker::PersistableConfig& general)
{
	checkInit();
	destroy(generalDb, general);
}

void BdbStoreProvider::stage(const boost::intrusive_ptr<qpid::broker::PersistableMessage>& /*msg*/)
{
	QPID_LOG(error,"Calling unimplemented Method stage()");
	throw mrg::journal::jexception(mrg::journal::jerrno::JERR__NOTIMPL, "BdbStoreProvider", "stage");
}

void BdbStoreProvider::destroy(qpid::broker::PersistableMessage& /*msg*/)
{
    	QPID_LOG(error,"Calling unimplemented Method destroy(PersistableMessage&)");
	throw mrg::journal::jexception(mrg::journal::jerrno::JERR__NOTIMPL, "BdbStoreProvider", "destroy");
}

void BdbStoreProvider::appendContent(const boost::intrusive_ptr<const qpid::broker::PersistableMessage>& /*msg*/,
                                    const std::string& /*data*/)
{
	QPID_LOG(error,"Calling unimplemented Method appendContent()");
	throw mrg::journal::jexception(mrg::journal::jerrno::JERR__NOTIMPL, "BdbStoreProvider", "appendContent");
}

void BdbStoreProvider::loadContent(const qpid::broker::PersistableQueue& queue,
                                  const boost::intrusive_ptr<const qpid::broker::PersistableMessage>& msg,
                                  std::string& data,
                                  u_int64_t offset,
                                  u_int32_t length)
{
	boost::posix_time::ptime start= boost::posix_time::microsec_clock::local_time();
	checkInit();
    	u_int64_t messageId (msg->getPersistenceId());
	uint64_t queueId (queue.getPersistenceId());
	
	if (messageId != 0) 
	{
    		//QPID_LOG(warning,"Start loading content "+boost::lexical_cast<std::string>(messageId)+" from "+queue.getName());
		tracker.waitForPid(messageId,queueId);
	        try 
		{
        		JournalImpl* jc = static_cast<JournalImpl*>(queue.getExternalQueueStore());
		        if (jc) 
			{
		                if (!jc->loadMsgContent(messageId, data, length, offset)) 
				{
			                std::ostringstream oss;
			                oss << "Queue " << queue.getName() << ": loadContent() failed: Message " << messageId << " is extern or not enqueued";
			                THROW_STORE_EXCEPTION(oss.str());
                		}
	        	} else 
			{
				std::ostringstream oss;
		                oss << "Queue " << queue.getName() << ": loadContent() failed: Message " << messageId << " not enqueued";
                		THROW_STORE_EXCEPTION(oss.str());
		        }
		} catch (const mrg::journal::jexception& e) 
		{
		        THROW_STORE_EXCEPTION(std::string("Queue ") + queue.getName() + ": loadContent() failed: " + e.what());
	        }
	} else 
	{
        	THROW_STORE_EXCEPTION("Cannot load content. Message not known to store!");
    	}
	boost::posix_time::time_duration diff = boost::posix_time::time_period(start,boost::posix_time::microsec_clock::local_time()).length();
	QPID_LOG(debug,"loadContent "+boost::lexical_cast<std::string>(messageId)+" from "+queue.getName()+" ; Duration : "+boost::lexical_cast<std::string>(diff.total_milliseconds())+" msec");
}

void BdbStoreProvider::enqueue(qpid::broker::TransactionContext* ctxt,
                              const boost::intrusive_ptr<qpid::broker::PersistableMessage>& msg,
                              const qpid::broker::PersistableQueue& queue)
{
	boost::posix_time::ptime start= boost::posix_time::microsec_clock::local_time();
	checkInit();
        u_int64_t queueId (queue.getPersistenceId());
        u_int64_t messageId (msg->getPersistenceId());
        if (queueId == 0) 
        {
		THROW_STORE_EXCEPTION("Queue not created: " + queue.getName());
        }

	if (ctxt)
	{
		QPID_LOG(warning,"Transaction not supported by the BDB Storage Provider!");
	}

	bool newId = false;
	if (messageId == 0) 
	{
		messageId = messageIdSequence.next();
		msg->setPersistenceId(messageId);
		newId = true;
	} else 
	{
		tracker.addDuplicate(messageId,queueId);
	}
	//store(&queue, txn, msg, newId);	

	std::vector<char> buff;
	u_int64_t size =msgEncode(buff, msg);
	aologger->log_enqueue_start(messageId,queueId,buff,size,!msg->isPersistent());
	if (this->mgmtObject) this->mgmtObject->inc_pendingAsyncEnqueue();
	if (this->options.enableSmartAsync) this->tracker.willEnqueue(messageId,queueId);
	JournalImpl* jc = static_cast<JournalImpl*>(queue.getExternalQueueStore());
	if(!enqueuePool.schedule(boost::bind(&BdbStoreProvider::store,this,jc,buff,size,messageId,!msg->isPersistent()))) 
	{
		THROW_STORE_EXCEPTION("Unable to dispatch the enqueue!");
	}
	msg->enqueueComplete();
	boost::posix_time::time_duration diff = boost::posix_time::time_period(start,boost::posix_time::microsec_clock::local_time()).length();
	QPID_LOG(debug,"enqueue "+boost::lexical_cast<std::string>(messageId)+" from "+queue.getName()+" ; Duration : "+boost::lexical_cast<std::string>(diff.total_milliseconds())+" msec");
}

void BdbStoreProvider::dequeue(qpid::broker::TransactionContext* ctxt,
                              const boost::intrusive_ptr<qpid::broker::PersistableMessage>& msg,
                              const qpid::broker::PersistableQueue& queue)
{
	checkInit();
	u_int64_t queueId (queue.getPersistenceId());
	uint64_t messageId (msg->getPersistenceId());
	if (messageId == 0) 
	{
		THROW_STORE_EXCEPTION("Error dequeuing message, persistence id not set");
	}
	if (queueId == 0) 
	{
		THROW_STORE_EXCEPTION("Queue not created: " + queue.getName());
	}

	if (ctxt)
	{
		QPID_LOG(warning,"Transaction not supported by the BDB Storage Provider!");
	}
	JournalImpl* jc = static_cast<JournalImpl*>(queue.getExternalQueueStore());
	bool toExec = true;
	if (this->options.enableSmartAsync) toExec = tracker.dequeueCheck(messageId,queueId);
	if (toExec)
	{
		aologger->log_dequeue_start(messageId,queueId);
		if (this->mgmtObject) this->mgmtObject->inc_pendingAsyncDequeue();
		if(!dequeuePool.schedule(boost::bind(&BdbStoreProvider::async_dequeue,this,messageId,jc))) 
		{
			THROW_STORE_EXCEPTION("Unable to dispatch the dequeue!");
		}
	} else
	{
		if (this->mgmtObject) this->mgmtObject->inc_skippedDequeue();
	}
    	msg->dequeueComplete();
}

std::auto_ptr<qpid::broker::TransactionContext> BdbStoreProvider::begin()
{
	QPID_LOG(error,"Calling unimplemented Method begin()");
   	throw mrg::journal::jexception(mrg::journal::jerrno::JERR__NOTIMPL, "BdbStoreProvider", "begin");
}

std::auto_ptr<qpid::broker::TPCTransactionContext> BdbStoreProvider::begin(const std::string& /*xid*/)
{
	QPID_LOG(error,"Calling unimplemented Method begin(const std::string&)");
    	throw mrg::journal::jexception(mrg::journal::jerrno::JERR__NOTIMPL, "BdbStoreProvider", "begin");
}	

void BdbStoreProvider::prepare(qpid::broker::TPCTransactionContext& /*ctxt*/)
{
	QPID_LOG(error,"Calling unimplemented Method prepare(qpid::broker::TPCTransactionContext&)");
	throw mrg::journal::jexception(mrg::journal::jerrno::JERR__NOTIMPL, "BdbStoreProvider", "prepare");
}

void BdbStoreProvider::commit(qpid::broker::TransactionContext& /*ctxt*/)
{
	QPID_LOG(error,"Calling unimplemented Method commit(qpid::broker::TransactionContext&)");
	throw mrg::journal::jexception(mrg::journal::jerrno::JERR__NOTIMPL, "BdbStoreProvider", "commit");
}

void BdbStoreProvider::abort(qpid::broker::TransactionContext& /*ctxt*/)
{
	QPID_LOG(error,"Calling unimplemented Method abort(qpid::broker::TPCTransactionContext&)");
    	throw mrg::journal::jexception(mrg::journal::jerrno::JERR__NOTIMPL, "BdbStoreProvider", "abort");
}

void BdbStoreProvider::collectPreparedXids(std::set<std::string>&/* xids*/)
{
	QPID_LOG(error,"Calling unimplemented Method abort(std::set<std::string>&)");
    	throw mrg::journal::jexception(mrg::journal::jerrno::JERR__NOTIMPL, "BdbStoreProvider", "collectPreparedXids");
}

void BdbStoreProvider::recoverConfigs(qpid::broker::RecoveryManager& registry)
{
	Cursor items;
	items.open(generalDb,0x0);
	u_int64_t maxGeneralId(1);
	IdDbt key;
	Dbt value;
	//read all items
	while (items.next(key, value)) 
	{
		qpid::framing::Buffer buffer(reinterpret_cast<char*>(value.get_data()), value.get_size());
		//create instance
		qpid::broker::RecoverableConfig::shared_ptr config = registry.recoverConfig(buffer);
		//set the persistenceId and update max as required
		config->setPersistenceId(key.id);
		maxGeneralId = std::max(key.id, maxGeneralId);
	}
	generalIdSequence.reset(maxGeneralId + 1);
}

void BdbStoreProvider::recoverExchanges(qpid::broker::RecoveryManager& recoverer,
                                ExchangeMap& exchangeMap)
{
	//TODO: this is a copy&paste from recoverQueues - refactor!
	Cursor exchanges;
	exchanges.open(exchangeDb, 0x0);
    	u_int64_t maxExchangeId(1);
	IdDbt key;
	Dbt value;
	//read all exchanges
	while (exchanges.next(key, value)) 
	{
        	qpid::framing::Buffer buffer(reinterpret_cast<char*>(value.get_data()), value.get_size());
	        //create a Exchange instance
        	qpid::broker::RecoverableExchange::shared_ptr exchange = recoverer.recoverExchange(buffer);
	        if (exchange)
		{ //set the persistenceId and update max as required
            		exchange->setPersistenceId(key.id);
			exchangeMap[key.id] = exchange;
	        }
        	maxExchangeId = std::max(key.id, maxExchangeId);
    	}
    	exchangeIdSequence.reset(maxExchangeId + 1);
}

void BdbStoreProvider::recoverQueues(qpid::broker::RecoveryManager& recoverer,
                             QueueMap& queueMap)
{
	Cursor queues;
    	queues.open(queueDb, 0x0);
    	if (this->options.enableAcceptRecoveryFlag) //Check if enable_accept_recover is true			
	{
		std::string collection=this->options.acceptRecoveryMongoDb+"."+this->options.acceptRecoveryMongoCollection;
		std::string mongo_addr=this->options.acceptRecoveryMongoHost + ":" + this->options.acceptRecoveryMongoPort;
		boost::shared_ptr<mongo::ScopedDbConnection> con = boost::shared_ptr<mongo::ScopedDbConnection>(new mongo::ScopedDbConnection(mongo_addr));
		mongo::DBClientBase* c = con->get();
		QPID_LOG(notice,"Loading accepted messages from MongoDb "+collection+" at "+mongo_addr);
		auto_ptr<mongo::DBClientCursor> cursor =c->query(collection,mongo::Query("{}"));
	    	while (cursor->more()) 
		{
			acceptedFromMongo.push_back(std::string(cursor->next().getStringField("UID")));
	    	}
	    	con->done();
	}
    	u_int64_t maxQueueId(1);

	IdDbt key;
    	Dbt value;
   	//read all queues
    	while (queues.next(key, value)) 
	{
        	qpid::framing::Buffer buffer(reinterpret_cast<char*>(value.get_data()), value.get_size());
		//create a Queue instance
        	qpid::broker::RecoverableQueue::shared_ptr queue = recoverer.recoverQueue(buffer);
        	//set the persistenceId and update max as required
        	queue->setPersistenceId(key.id);

        	const std::string queueName = queue->getName().c_str();
        	JournalImpl* jQueue = 0;
       		if (queueName.size() == 0)
        	{
            		QPID_LOG(error, "Cannot recover empty (null) queue name - ignoring and attempting to continue.");
            		break;
        	}
        	jQueue = new JournalImpl(/*timer,*/ queueName,key.id, getJrnlHashDir(queueName), getBdbBaseDir(),num_jfiles,
                                 /*defJournalGetEventsTimeout, defJournalFlushTimeout,*/agent,dbenv,
                                 boost::bind(&BdbStoreProvider::journalDeleted, this, _1));
        	{
	            qpid::sys::Mutex::ScopedLock sl(journalListLock);
        	    journalList[key.id] = jQueue;
	        }	
        	queue->setExternalQueueStore(dynamic_cast<ExternalQueueStore*>(jQueue));
		recoveredJournal.push_back(queue);
		queueMap[key.id] = queue;

        	maxQueueId = max(key.id, maxQueueId);
    	}
    	queueIdSequence.reset(maxQueueId + 1);
}

void BdbStoreProvider::recoverBindings(qpid::broker::RecoveryManager&/* recoverer*/,
				const ExchangeMap& exchangeMap,
			        const QueueMap& queueMap)
{
	Cursor bindings;
    	bindings.open(bindingDb,0x0);

	IdDbt key;
	Dbt value;
	while (bindings.next(key, value)) 
	{
        	qpid::framing::Buffer buffer(reinterpret_cast<char*>(value.get_data()), value.get_size());
        	if (buffer.available() < 8) 
		{
            		QPID_LOG(error, "Not enough data for binding: " << buffer.available());
		        THROW_STORE_EXCEPTION("Not enough data for binding");
	        }
		uint64_t queueId = buffer.getLongLong();
	        std::string queueName;
	        std::string routingkey;
		qpid::framing::FieldTable args;
	        buffer.getShortString(queueName);
	        buffer.getShortString(routingkey);
	        buffer.get(args);
        	ExchangeMap::const_iterator exchange = exchangeMap.find(key.id);
	        QueueMap::const_iterator queue = queueMap.find(queueId);
        	if (exchange != exchangeMap.end() && queue != queueMap.end()) {
			//could use the recoverable queue here rather than the name...
		        exchange->second->bind(queueName, routingkey, args);
        	} else {
	            	//stale binding, delete it
			QPID_LOG(warning, "Deleting stale binding");
		        bindings->del(0);
	        }
    	}
}

void BdbStoreProvider::recoverMessages(qpid::broker::RecoveryManager& recoverer,
                               MessageMap& messageMap,
			       MessageQueueMap& messageQueueMap)
{
	//Recover async Enqueue
	std::set<PendingAsyncDequeue> adset;
	std::set<PendingAsyncEnqueue> aeset;
	aologger->recoverAsyncDequeue(adset);
	aologger->recoverAsyncEnqueue(aeset);
    	std::vector< std::string > toBeDeleteFromMongo;
	for (std::list<qpid::broker::RecoverableQueue::shared_ptr>::iterator it=recoveredJournal.begin();it !=recoveredJournal.end();it++) 
	{
		const std::string queueName =(*it)->getName().c_str();
		try
        	{
            		long rcnt = 0L;     // recovered msg count
           		long acnt = 0L;    // accepted msg count
	    		long tcnt = 0L;	//transient msg count 
            		u_int64_t thisHighestRid = 0ULL;
			thisHighestRid=decodeAndRecoverMsg(recoverer, *it, messageMap,messageQueueMap,toBeDeleteFromMongo,&adset,&aeset, rcnt, acnt,tcnt);
	    		if (highestRid == 0ULL)
			        highestRid = thisHighestRid;
		        else if (thisHighestRid - highestRid < 0x8000000000000000ULL) // RFC 1982 comparison for unsigned 64-bit
		                highestRid = thisHighestRid;
	            	QPID_LOG(notice, "Recovered queue \"" << queueName << "\": " << rcnt << " messages recovered; " << acnt << " accepted while down; "<<tcnt <<" mark as transient.");
        	} catch (const mrg::journal::jexception& e) 
		{
		        THROW_STORE_EXCEPTION(std::string("Queue ") + queueName + ": recoverQueues() failed: " + e.what());
        	}		

	}
	if (this->options.enableAcceptRecoveryFlag) { //Check if enable_accept_recover is true			
	    	string collection=this->options.acceptRecoveryMongoDb+"."+this->options.acceptRecoveryMongoCollection;
	    	string mongo_addr=this->options.acceptRecoveryMongoHost + ":" + this->options.acceptRecoveryMongoPort;
	    	boost::shared_ptr<mongo::ScopedDbConnection> con = boost::shared_ptr<mongo::ScopedDbConnection>(new mongo::ScopedDbConnection(mongo_addr));
	    	mongo::DBClientBase* c = con->get();
	    	QPID_LOG(notice,"Deleting accepted messages from MongoDb "+collection+" at "+mongo_addr);
	    	for (std::vector<std::string>::iterator acIt=toBeDeleteFromMongo.begin();acIt!=toBeDeleteFromMongo.end();acIt++) 
		{
	    		c->remove(collection,BSON("UID"<<*acIt));
	    	}
	    	con->done();
   	}
	for (std::list<qpid::broker::RecoverableQueue::shared_ptr>::iterator it=recoveredJournal.begin();it !=recoveredJournal.end();it++) 
	{
		JournalImpl* jc = static_cast<JournalImpl*>((*it)->getExternalQueueStore());
		jc->discard_transient_message();
		jc->discard_accepted_message();
		if (this->options.compactFlag)
		{
			jc->compact_message_database();			
		}
		jc->recover_complete();
	}
	acceptedFromMongo.clear();
	recoveredJournal.clear();
    	// NOTE: highestRid is set by both recoverQueues() and recoverTplStore() as
	// the messageIdSequence is used for both queue journals and the tpl journal.
	messageIdSequence.reset(highestRid + 1);
	this->lastPid=highestRid;
	tracker.reset(highestRid);
	QPID_LOG(info, "Most recent persistence id found: 0x" << std::hex << highestRid << std::dec);
	QPID_LOG(notice,"Resurrecting Async operation Pool..");
	int resEnqCount=0;
	std::set<PendingAsyncDequeue>::iterator adit;
	std::vector<PendingOperationId> uselessList;
	for (std::set<PendingAsyncEnqueue>::iterator it=aeset.begin();it!=aeset.end();)
	{
		adit = adset.find(PendingOperationId(it->msgId,it->queueId));
		if (adit!=adset.end())
		{
			adset.erase(adit);
			aeset.erase(it++);
			uselessList.push_back(it->opId());
		} else 
		{
			JournalImpl* jc=0x0;
			{
				qpid::sys::Mutex::ScopedLock lock(journalListLock);
				jc = journalList[it->queueId];
			}
			if (this->mgmtObject) this->mgmtObject->inc_pendingAsyncEnqueue();
			if (this->options.enableSmartAsync) this->tracker.willEnqueue(it->msgId,it->queueId);
			if(enqueuePool.schedule(boost::bind(&BdbStoreProvider::store,this,jc,it->buff,it->size,it->msgId,it->transient))) 
			{	
				resEnqCount++;
			}
			++it;
		}
	}
	int resDeqCount=0;
	for (std::set<PendingAsyncDequeue>::iterator it=adset.begin();it!=adset.end();it++)
	{
		JournalImpl* jc=0x0;
		{
			qpid::sys::Mutex::ScopedLock lock(journalListLock);
			jc = journalList[it->queueId];
		}
		if (jc!=0x0) 
		{
			if (this->mgmtObject) this->mgmtObject->inc_pendingAsyncDequeue();
			if(dequeuePool.schedule(boost::bind(&BdbStoreProvider::async_dequeue,this,it->msgId,jc))) 
			{	
				resDeqCount++;
			}
		} else
		{
			QPID_LOG(error,"No Journal with queue id "+boost::lexical_cast<std::string>(it->queueId));
		}

	}
	aologger->log_mass_enqueue_dequeue_complete(uselessList);
	uselessList.clear();
	QPID_LOG(notice,"Resurrected "+boost::lexical_cast<std::string>(resEnqCount)+" async enqueues "
			"and "+boost::lexical_cast<std::string>(resDeqCount)+" async dequeues");
	int enqCleanTimeInterval=this->options.enqLogCleanerTimeInterval;
	int enqCleanSizeInterval=this->options.enqLogCleanerWarningSize;
	int deqCleanTimeInterval=this->options.deqLogCleanerTimeInterval;
	int deqCleanSizeInterval=this->options.deqLogCleanerWarningSize;
	this->enqLogCleanerThread=boost::shared_ptr<boost::thread>(new boost::thread
	(
		boost::bind(&AsyncOperationLogger::cleanEnqueueLog,this->aologger,enqCleanTimeInterval,enqCleanSizeInterval)
	));
	this->deqLogCleanerThread=boost::shared_ptr<boost::thread>(new boost::thread
	(
		boost::bind(&AsyncOperationLogger::cleanDequeueLog,this->aologger,deqCleanTimeInterval,deqCleanSizeInterval)
	));
}

void BdbStoreProvider::flush(const qpid::broker::PersistableQueue& queue)
{
	if (queue.getExternalQueueStore() == 0) return;
	checkInit();
	std::string qn = queue.getName();
	try 
	{
		/****************************************************************************/
		JournalImpl* jc = static_cast<JournalImpl*>(queue.getExternalQueueStore());
	        if (jc)
		{
			jc->flush();
			QPID_LOG(info,"Call flush on "+queue.getName()+"!");
		
		}
		/***************************************************************************/
	} catch (const mrg::journal::jexception& e) 
	{
        	THROW_STORE_EXCEPTION(std::string("Queue ") + qn + ": flush() failed: " + e.what() );
	}

}


void BdbStoreProvider::deleteBindingsForQueue(const qpid::broker::PersistableQueue& queue)
{
	TxnCtxt txn;
	txn.begin(dbenv.get(), true);
	try 
	{
		{
	    		Cursor bindings;
		    	bindings.open(bindingDb, txn.get());

			IdDbt key;
			Dbt value;
			while (bindings.next(key, value)) 
			{
				qpid::framing::Buffer buffer(reinterpret_cast<char*>(value.get_data()), value.get_size());
				if (buffer.available() < 8) 
				{
					THROW_STORE_EXCEPTION("Not enough data for binding");
				}
				uint64_t queueId = buffer.getLongLong();
				if (queue.getPersistenceId() == queueId) 
				{
		    			bindings->del(0);
		    			QPID_LOG(debug, "Deleting binding for " << queue.getName() << " " << key.id << "->" << queueId);
				}
			}
		}
		txn.commit();
	} catch (const std::exception& e) 
	{
		txn.abort();
		THROW_STORE_EXCEPTION_2("Error deleting bindings", e.what());
	} catch (...) {
		txn.abort();
		throw;
	}
	QPID_LOG(debug, "Deleted all bindings for " << queue.getName() << ":" << queue.getPersistenceId());
}

void BdbStoreProvider::deleteBinding(const qpid::broker::PersistableExchange& exchange,
				    const qpid::broker::PersistableQueue& queue,
				    const std::string& bkey)
{
    	TxnCtxt txn;
    	txn.begin(dbenv.get(), true);
    	try 
	{
		{
	    		Cursor bindings;
	    		bindings.open(bindingDb, txn.get());
		
	    		IdDbt key(exchange.getPersistenceId());
	    		Dbt value;

	    		for (int status = bindings->get(&key, &value, DB_SET); status == 0; status = bindings->get(&key, &value, DB_NEXT_DUP)) 
			{
				qpid::framing::Buffer buffer(reinterpret_cast<char*>(value.get_data()), value.get_size());
				if (buffer.available() < 8) 
				{
					THROW_STORE_EXCEPTION("Not enough data for binding");
				}
				uint64_t queueId = buffer.getLongLong();
				if (queue.getPersistenceId() == queueId) 
				{
					std::string q;
			    		std::string k;	
			    		buffer.getShortString(q);
		    			buffer.getShortString(k);
		    			if (bkey == k) 
					{
						bindings->del(0);
						QPID_LOG(debug, "Deleting binding for " << queue.getName() << " " << key.id << "->" << queueId)	;
		    			}
				}
	    		}
		}
		txn.commit();
	} catch (const std::exception& e) 
	{
		txn.abort();
		THROW_STORE_EXCEPTION_2("Error deleting bindings", e.what());
	} catch (...) 
	{
		txn.abort();
		throw;
    	}
}

u_int64_t BdbStoreProvider::msgEncode(std::vector<char>& buff, const boost::intrusive_ptr<qpid::broker::PersistableMessage>& message)
{
	u_int32_t headerSize = message->encodedHeaderSize();
	u_int64_t size = message->encodedSize() + sizeof(u_int32_t)+ sizeof(u_int8_t);
	try 
	{ 
		buff = std::vector<char>(size);  // byte (transient flag) + long(header size) + headers + content
	} catch (const std::exception& e) 
	{
		std::ostringstream oss;
		oss << "Unable to allocate memory for encoding message; requested size: " << size << "; error: " << e.what();
		THROW_STORE_EXCEPTION(oss.str());
	}
	qpid::framing::Buffer buffer(&buff[0],size);
	buffer.putOctet(message->isPersistent()?0:1);
	buffer.putLong(headerSize);
	message->encode(buffer);
	return size;
}

void BdbStoreProvider::store(JournalImpl* jc,
                            std::vector<char>& buff,
			    const uint64_t size,
                            uint64_t pid,
                            bool transient)
{
	std::string tid; //Empty Transaction Id 'cause transactions are not supported!
	try 
	{
		if (jc) 
		{
			/********************************************/
			boost::posix_time::ptime start= boost::posix_time::microsec_clock::local_time();
			bool toExec=true;
			if (this->options.enableSmartAsync) toExec = tracker.enqueueCheck(pid,jc->pid());
			if (toExec) 
			{
		    		jc->enqueue_data(&buff[0], size, size, pid,tid,transient);
			}
			//cout << "Enqueue of #"<<pid<<" from "<<jc->id()<<"("<<jc->pid()<<")"<<endl;
			aologger->log_enqueue_complete(pid,jc->pid());
			tracker.addPid(pid,jc->pid());
			boost::posix_time::time_duration diff = boost::posix_time::time_period(start,boost::posix_time::microsec_clock::local_time()).length();
			if (this->mgmtObject) 
			{
				this->mgmtObject->dec_pendingAsyncEnqueue();
				this->mgmtObject->inc_totalEnqueueTime(diff.total_milliseconds());
				this->mgmtObject->set_enqueueTime(diff.total_microseconds()*10);
				if (toExec)
				{
					this->mgmtObject->inc_executedEnqueue();
				} else
				{
					this->mgmtObject->inc_skippedEnqueue();
				}
			}
			QPID_LOG(debug,"enqueue "+string(toExec?"executed":"skipped")+" "+boost::lexical_cast<std::string>(pid)+" on "+jc->id() +" STORE: "+boost::lexical_cast<std::string>(diff.total_milliseconds())+"ms");
			/********************************************/
		} else 
		{
			THROW_STORE_EXCEPTION(std::string("BdbStoreProvider::store() failed: queue NULL."));
		}
	} catch (const mrg::journal::jexception& e) 
	{
		THROW_STORE_EXCEPTION(std::string("Queue ") + jc->id() + ": BdbStoreProvider::store() failed: " +e.what());
	}
}

void BdbStoreProvider::async_dequeue(uint64_t msgId,
                                    JournalImpl* jc)
{
	boost::posix_time::ptime start = boost::posix_time::microsec_clock::local_time();
	std::string tid; //Empty Transaction Id 'cause transactions are not supported!
    	try 
	{
		tracker.waitForPid(msgId,jc->pid());		
		/******************************************************/
		//cout << "Dequeue of #"<<msgId<<" from "<<jc->id()<<"("<<jc->pid()<<")"<<endl;
        	jc->dequeue_data(msgId, tid,false);
		aologger->log_dequeue_complete(msgId,jc->pid());						
		boost::posix_time::time_duration diff = boost::posix_time::time_period(start, boost::posix_time::microsec_clock::local_time()).length();
		if (this->mgmtObject) 
		{
			this->mgmtObject->dec_pendingAsyncDequeue();
			this->mgmtObject->inc_totalDequeueTime(diff.total_milliseconds());
			this->mgmtObject->set_dequeueTime(diff.total_microseconds()*10);
			this->mgmtObject->inc_executedDequeue();
		}
		QPID_LOG(debug,"dequeue "+boost::lexical_cast<std::string>(msgId)+" on "+jc->id() +" ASYNC_DEQUEUE: "+boost::lexical_cast<std::string>(diff.total_milliseconds())+"ms");
		/******************************************************/

	} catch (const mrg::journal::jexception& e) 
	{
        	THROW_STORE_EXCEPTION(std::string("Queue ") + jc->id() + ": async_dequeue() failed: " + e.what());
	}
}

uint64_t BdbStoreProvider::decodeAndRecoverMsg(qpid::broker::RecoveryManager& recovery,
					qpid::broker::RecoverableQueue::shared_ptr queue,
					MessageMap& messageMap,
					MessageQueueMap& messageQueueMap,
					std::vector<std::string>& foundMsg,
					std::set<PendingAsyncDequeue>* adset,
					std::set<PendingAsyncEnqueue>* aeset,
                                      	long& rcnt,
				      	long& acnt,
				      	long& tcnt)
{
	uint64_t maxRid=0ULL;
	try 
	{
		size_t preambleLength = sizeof(u_int32_t)+sizeof(u_int8_t)/*header size + transient flag*/;
		JournalImpl* jc = static_cast<JournalImpl*>(queue->getExternalQueueStore());
		std::vector< std::pair <uint64_t,std::string> > recovered;
		std::vector< uint64_t > transientMsg;
		std::vector< uint64_t > innerAcceptedMsg;
		std::set<PendingAsyncDequeue>::iterator adit;
		jc->recoverMessages(recovered);
		std::vector<PendingOperationId> transient_async; //Delete only from enq
		std::vector<PendingOperationId> useless_async; //Delete from both enq and deq
		std::vector<PendingOperationId> dequeue_completed_async; //Delete only from deq
		//Iterate over pending enqueue to add messages to the recovered list
		for(std::set<PendingAsyncEnqueue>::iterator mit = aeset->begin();mit!=aeset->end();)
		{
			PendingAsyncEnqueue pae=*mit;
			std::set<PendingAsyncDequeue>::iterator dmi=adset->find(pae.opId());
			if (dmi!=adset->end()) //Dequeue start before Enqueue complete => no recover
			{
				useless_async.push_back(pae.opId());
				aeset->erase(mit++);
				adset->erase(dmi);
				innerAcceptedMsg.push_back(pae.msgId);
			} else if(pae.transient) //Transient message => no recover
			{
				transient_async.push_back(pae.opId());
				aeset->erase(mit++);
			} else	//Not Transient and not Dequeue Start => recover
			{	
				recovered.push_back(std::pair<uint64_t,std::string>(pae.msgId,std::string(&pae.buff[0],pae.size)));
				++mit;
			}
		}
		for (std::vector< std::pair <uint64_t,std::string> >::iterator it=recovered.begin();it<recovered.end();it++) 
		{	
			PendingOperationId opid(it->first,jc->pid());
			char* rawData= new char[it->second.size()];
			memcpy(rawData,it->second.data(),it->second.size());
			RecoverableMessage::shared_ptr msg;
			qpid::framing::Buffer msgBuff(rawData,it->second.size());
			u_int8_t transientFlag=msgBuff.getOctet();
			if (!transientFlag)
			{
				u_int32_t headerSize=msgBuff.getLong();
				qpid::framing::Buffer headerBuff(msgBuff.getPointer()+preambleLength,headerSize);
				bool toRecover=true;
				if (this->options.enableAcceptRecoveryFlag) //Check if enable_accept_recover is true
				{
					boost::shared_ptr<qpid::broker::Message> message(new qpid::broker::Message());
					message->decodeHeader(headerBuff);
					headerBuff.reset();
					std::string msgUid=message->getApplicationHeaders()->getAsString("UID");
					if (!msgUid.empty()) 
					{
						std::vector<std::string>::iterator msgIt=std::find(acceptedFromMongo.begin(),acceptedFromMongo.end(),msgUid);	
						if (msgIt!=acceptedFromMongo.end()) 
						{
							std::set<PendingAsyncEnqueue>::iterator emi=aeset->find(opid);
							std::set<PendingAsyncDequeue>::iterator dmi=adset->find(opid);
							if (emi!=aeset->end())
							{
								aeset->erase(emi);
								if (dmi!=adset->end())
								{
									adset->erase(dmi);
									useless_async.push_back(opid);
								} else 
								{
									transient_async.push_back(opid);
								}
							} else
							{
								innerAcceptedMsg.push_back(it->first);
							}
							foundMsg.push_back(*msgIt);
							acnt++;
							toRecover=false;
						}
					}
				}
				adit = adset->find(opid);
				if (adit!=adset->end())
				{
					if (queue->getPersistenceId()!=adit->queueId)
					{
						QPID_LOG(warning,"Message #"+boost::lexical_cast<std::string>(it->first)+" enqueued Queue "
								"#"+boost::lexical_cast<std::string>(adit->queueId)+" but recovered from Queue "
								"#"+boost::lexical_cast<std::string>(queue->getPersistenceId())+"! Executing async "
								"dequeue anyway");
					}
					innerAcceptedMsg.push_back(it->first);
					adset->erase(adit);
					dequeue_completed_async.push_back(opid);
					toRecover=false;
				}
				maxRid=max(it->first,maxRid);
				if (toRecover) 
				{
					msg = recovery.recoverMessage(headerBuff);
					msg->setPersistenceId(it->first);
					msg->setRedelivered();
					uint32_t contentOffset = headerSize + preambleLength;
					uint64_t contentSize = msgBuff.getSize() - contentOffset;
					if (msg->loadContent(contentSize)) 
					{
					    //now read the content
					    qpid::framing::Buffer contentBuff(msgBuff.getPointer() + contentOffset, contentSize);
					    msg->decodeContent(contentBuff);
					    rcnt++;
					    messageMap[it->first]=msg;
					    messageQueueMap[it->first].push_back(qpid::store::QueueEntry(queue->getPersistenceId()));
					}
				}
			} else 
			{
				transientMsg.push_back(it->first);
				tcnt++;
			}
			delete [] rawData;
		}
		jc->register_as_transient(transientMsg);
		jc->register_as_accepted(innerAcceptedMsg);
		transientMsg.clear();
		innerAcceptedMsg.clear();
		recovered.clear();
		aologger->log_mass_enqueue_complete(transient_async);
		aologger->log_mass_dequeue_complete(dequeue_completed_async);
		aologger->log_mass_enqueue_dequeue_complete(useless_async);
		transient_async.clear();
		dequeue_completed_async.clear();
		useless_async.clear();
    	} catch (const mrg::journal::jexception& e) 
	{
	        THROW_STORE_EXCEPTION(std::string("Queue ") + queue->getName() + ": recoverMessages() failed: " + e.what());
	}
	return maxRid;
}

}}} //namespace qpid::store::bdb
