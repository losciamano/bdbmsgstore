#ifndef STORE_OPTIONS_H
#define STORE_OPTIONS_H

#include <string>

#include "qpid/broker/Broker.h"
#include "qpid/log/Statement.h"
#include "AsyncOperationLogger.h"

namespace qpid {
namespace store {
namespace bdb {
struct StoreOptions : public qpid::Options 
{
    	/**
	*	Base constructor for options struct
	**/
        StoreOptions(const std::string& name="Berkeley Db Store Provider Options");
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
        bool    truncateFlag;
	/**
	*	Compact flag option.
	*	If true, will compact the Berkeley Db after recovery and transient message deletion; this operation return to filesystem useless db
	*	pages.
	**/
	bool 	compactFlag;
	/**
	*	Enable Accept Recovery option
	*	If true, will discard messages accepted while the broker is down.
	**/
	bool 	enableAcceptRecoveryFlag;
	/**
	*	 Mongo Database Host for Accept Recovery
	**/
	std::string 	acceptRecoveryMongoHost;
	/**
	*	Mongo Database Port for Accept Recovery
	**/
	std::string 	acceptRecoveryMongoPort;
	/**
	*	Mongo Database Name for Accept Recovery
	**/
	std::string 	acceptRecoveryMongoDb;
	/**
	*	Mongo Collection Name for Accept Recovery
	**/
	std::string 	acceptRecoveryMongoCollection;
	/**
	*	Enqueue log cleaner time interval
	*/
	int	enqLogCleanerTimeInterval;
	/**
	*	Dequeue Log cleaner time interval
	**/
	int 	deqLogCleanerTimeInterval;
	/**
	*	Enqueue Log cleaner warning size
	**/
	int 	enqLogCleanerWarningSize;
	/**
	*	Dequeue Log cleaner warning size
	**/
	int 	deqLogCleanerWarningSize;
	/**
	*	Smart Async Enable Flag
	**/	
	bool 	enableSmartAsync;
};

StoreOptions::StoreOptions(const std::string& name) :
					qpid::Options(name),
                                        truncateFlag(false),
					compactFlag(true),
					enableAcceptRecoveryFlag(false),
					acceptRecoveryMongoHost("localhost"),
					acceptRecoveryMongoPort("27017"),
					acceptRecoveryMongoDb("distributed_commons"),
					acceptRecoveryMongoCollection("accept_recovery"),
					enqLogCleanerTimeInterval(120),
					deqLogCleanerTimeInterval(120),
					enqLogCleanerWarningSize(13107200),
					deqLogCleanerWarningSize(13107200),
					enableSmartAsync(true)
{
    std::string min_str=boost::lexical_cast<std::string>(AsyncOperationLogger::min_clean_interval);
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
	/*("enable_accept_recovery",qpid::optValue(enableAcceptRecoveryFlag,"yes|no"),
		"If yes|true|1, will discard messages accepted while the broker is down. During this operation the mongo database will be "
		"queryed to get accepted messages.")
	("accept_recovery_mongo_host",qpid::optValue(acceptRecoveryMongoHost,"host"),
		"Host name where the plugin can find a MongoDb for accept recovery.")
	("accept_recovery_mongo_port",qpid::optValue(acceptRecoveryMongoPort,"port"),
		"Port where the plugin can find a MongoDb for accept recovery.")
	("accept_recovery_mongo_db",qpid::optValue(acceptRecoveryMongoDb,"name"),
		"Name of the Mongo Database where the plugin can find information about messages accepted while the broker is down.")
	("accept_recovery_mongo_collection",qpid::optValue(acceptRecoveryMongoCollection,"name"),
		"Name of the collection contained in a Mongo Database where the plugin can find information about messages accepted while the "
		"broker is down")*/
	("enq_clean_interval",qpid::optValue(enqLogCleanerTimeInterval,"sec"),
		std::string("Time interval in seconds of the thread that cleans the enqueue log. This value is normalized by defect "
		"at "+min_str+" seconds. The minimum allowed interval is "+min_str+" seconds.").c_str())
	("enq_clean_warning_size",qpid::optValue(enqLogCleanerWarningSize,"byte"),
		std::string("Warning size for the enqueue log cleaner. The cleaner controlls every "+min_str+" seconds the log file size and if its "
		"greater than the warning size, a clean operation is performed even if the time interval hasn't exceeded.").c_str())
	("deq_clean_interval",qpid::optValue(deqLogCleanerTimeInterval,"sec"),
		std::string("Time interval in seconds of the thread that cleans the dequeue log. This value is normalized by defect "
		"at "+min_str+" seconds. The minimum allowed interval is "+min_str+" seconds.").c_str())
	("deq_clean_warning_size",qpid::optValue(deqLogCleanerWarningSize,"byte"),
		std::string("Warning size for the dequeue log cleaner. The cleaner controlls every "+min_str+" seconds the log file size and if its "
		"greater than the warning size, a clean operation is performed even if the time interval hasn't exceeded.").c_str())
	("enable_smart_async",qpid::optValue(enableSmartAsync,"yes|no"),
		"If yes|true|1, will use a data structure to avoid enqueue db operation if a dequeue has been requested for the same message."
		"This operation reduces the number of db operation and the number of pending async operation; on the other hand, this makes"
		"enqueue and dequeue operation slower.")
	;
}

}}} //namespace qpid::store::bdb
#endif //~STORE_OPTIONS_H
