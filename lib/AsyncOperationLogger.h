#ifndef ASYNC_OPERATION_LOGGER_H
#define ASYNC_OPERATION_LOGGER_H
#include <iostream>
#include <fstream>
#include <vector>
#include <set>
#include "boost/thread.hpp"
#include "StoreException.h"
#include "PendingOperationType.h"
#include "PendingOperationSet.h"

namespace qpid {
namespace store {
namespace bdb {

class AsyncOperationLogger 
{
	private:
		int enqueueFileIndex;
		int dequeueFileIndex;
		std::ofstream enqueueFile;
		std::ofstream dequeueFile;
		boost::mutex enqueueMutex;
		boost::mutex dequeueMutex;
		std::string logBaseDir;
		char* deqStartBuff;
		char* deqCompleteBuff;
		char* enqRawBuff;
		char* enqDataPreambleBuff;
		const size_t deqStartSize;
		const size_t deqCompleteSize;
		const size_t enqCompleteSize;
		const size_t enqDataPreambleSize;
		std::string buildEnqLogName(int index,bool temp=false);
		std::string buildDeqLogName(int index,bool temp=false);
		void extractPendingEnqueueFromFile(PendingEnqueueSet& aeset,int fileIndex,PendingOperationSet& lostSet);
		void extractPendingDequeueFromFile(PendingDequeueSet& adset,int fileIndex,PendingOperationSet& lostSet);
		int log_dequeue_start_on_file(uint64_t pid,uint64_t qid,std::ofstream* logFile,char* wbuff);
		int log_enqueue_start_on_file(uint64_t pid,
						uint64_t qid,
						std::vector<char>& buff,
						uint64_t buffsize,
						bool transient,
						std::ofstream* logFile,
						char* wbuff);
	public:
		static const char aolog_dequeue_start=0x02;
		static const char aolog_dequeue_complete=0x03;
		static const char aolog_enqueue_start=0x04;
		static const char aolog_enqueue_complete=0x05;
		static const int min_clean_interval=30;
		
		AsyncOperationLogger();
		AsyncOperationLogger(std::string& basedir);
		~AsyncOperationLogger();
		int log_dequeue_start(uint64_t pid,uint64_t qid);
		int log_dequeue_start(PendingOperationId opid) { return log_dequeue_start(opid.first,opid.second); }
		int log_dequeue_complete(uint64_t pid,uint64_t qid);
		int log_dequeue_complete(PendingOperationId opid) { return log_dequeue_complete(opid.first,opid.second); }
		int log_enqueue_start(uint64_t pid,uint64_t qid,std::vector<char>& buff,uint64_t buffsize,bool transient);
		int log_enqueue_complete(uint64_t pid,uint64_t qid);
		int log_enqueue_complete(PendingOperationId opid ) { return log_enqueue_complete(opid.first,opid.second); }
		int log_mass_dequeue_complete(std::vector<PendingOperationId>& pid_list); 
		int log_mass_enqueue_complete(std::vector<PendingOperationId>& pid_list);
		bool log_mass_enqueue_dequeue_complete(std::vector<PendingOperationId>& pid_list);
		int recoverAsyncDequeue(PendingDequeueSet& adset);
		int recoverAsyncEnqueue(PendingEnqueueSet& aeset);
		void cleanEnqueueLog(int intervalInSeconds,int warningSize);
		void cleanDequeueLog(int intervalInSeconds,int warningSize);
};

}}}
#endif //~ASYNC_OPERATION_LOGGER_H
