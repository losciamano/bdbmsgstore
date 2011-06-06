namespace qpid{
namespace store{
namespace bdb{
class BdbStoreProvider : public qpid::store::StorageProvider
{
	protected:
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
	virtual void flush(const PersistableQueue& queue) {};

	/**
	 * Returns the number of outstanding AIO's for a given queue
	 *
	 * If 0, than all the enqueue / dequeues have been stored
	 * to disk
	 *
	 * @param queue the name of the queue to check for outstanding AIO
	 */
	virtual uint32_t outstandingQueueAIO(const PersistableQueue& queue)
	{return 0;}
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
	virtual void recoverTransactions(qpid::broker::RecoveryManager& recoverer,
				PreparedTransactionMap& dtxMap);
};

/**
*	Static instance of the BdbStorePlugin
**/
static BdbStoreProvider instance; // Static initialization.

