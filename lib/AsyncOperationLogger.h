#ifndef ASYNC_OPERATION_LOGGER_H
#define ASYNC_OPERATION_LOGGER_H
#include <iostream>
#include <fstream>
#include <vector>
#include <set>
#include "boost/thread.hpp"
#include "StoreException.h"

namespace qpid {
namespace store {
namespace bdb {

struct PendingAsyncOperation;

int operator==(const PendingAsyncOperation& left,const PendingAsyncOperation& right);
int operator!=(const PendingAsyncOperation& left,const PendingAsyncOperation& right);
int operator<(const PendingAsyncOperation& left,const PendingAsyncOperation& right);
int operator>(const PendingAsyncOperation& left,const PendingAsyncOperation& right);
int operator<=(const PendingAsyncOperation& left,const PendingAsyncOperation& right);
int operator>=(const PendingAsyncOperation& left,const PendingAsyncOperation& right);

typedef std::pair<uint64_t,uint64_t> PendingOperationId;

struct PendingAsyncOperation 
{
	uint64_t msgId;
	uint64_t queueId;
	PendingAsyncOperation(const uint64_t mid=0,const uint64_t qid=0):
				msgId(mid),
				queueId(qid)
	{}
	PendingAsyncOperation(const PendingAsyncOperation& ref):
				msgId(ref.msgId),
				queueId(ref.queueId)
	{}
	PendingAsyncOperation& operator=(const PendingAsyncOperation& other)
	{
		if (this == &other) return *this; //same object
		msgId=other.msgId;
		queueId=other.queueId;
		return *this;
	}
	PendingOperationId opId() const { return PendingOperationId(msgId,queueId); }
	friend int operator==(const PendingAsyncOperation& left,const PendingAsyncOperation& right);
	friend int operator!=(const PendingAsyncOperation& left,const PendingAsyncOperation& right);
	friend int operator<(const PendingAsyncOperation& left,const PendingAsyncOperation& right);
	friend int operator>(const PendingAsyncOperation& left,const PendingAsyncOperation& right);
	friend int operator<=(const PendingAsyncOperation& left,const PendingAsyncOperation& right);
	friend int operator>=(const PendingAsyncOperation& left,const PendingAsyncOperation& right);
};

struct PendingAsyncDequeue: public PendingAsyncOperation
{
	PendingAsyncDequeue(const uint64_t mid=0,const uint64_t qid=0):
			PendingAsyncOperation(mid,qid)
	{}
	PendingAsyncDequeue(PendingOperationId opid):
			PendingAsyncOperation(opid.first,opid.second)
	{}
	PendingAsyncDequeue(const PendingAsyncDequeue& other):
		PendingAsyncOperation(other.msgId,other.queueId)
	{}
	PendingAsyncDequeue& operator=(const PendingAsyncDequeue& other)
	{
		if (this == &other) return *this; //same object
		msgId=other.msgId;
		queueId=other.queueId;
		return *this;
	}
};

struct PendingAsyncEnqueue: public PendingAsyncOperation
{
	std::vector<char> buff;
	uint64_t size;
	bool transient;
	PendingAsyncEnqueue(const uint64_t mid=0,const uint64_t qid=0,bool transient=false):
		PendingAsyncOperation(mid,qid),
		size(0),
		transient(transient)
	{}
	PendingAsyncEnqueue(PendingOperationId opid, bool transient = false):
		PendingAsyncOperation(opid.first,opid.second),
		size(0),
		transient(transient)
	{}
	PendingAsyncEnqueue(const PendingAsyncEnqueue& other):
		PendingAsyncOperation(other.msgId,other.queueId),
		buff(other.buff),
		size(other.size),
		transient(other.transient)
	{}
	PendingAsyncEnqueue& operator=(const PendingAsyncEnqueue& other)
	{
		if (this == &other) return *this; //self assignment
		msgId=other.msgId;
		queueId=other.queueId;
		buff=std::vector<char>(other.buff);
		size=other.size;
		transient=other.transient;
		return *this;
	}
	~PendingAsyncEnqueue()
	{
		buff.clear();
	}
};

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
		void extractPendingEnqueueFromFile(std::set<PendingAsyncEnqueue>& aeset,int fileIndex,std::set<PendingOperationId>& lostSet);
		void extractPendingDequeueFromFile(std::set<PendingAsyncDequeue>& adset,int fileIndex,std::set<PendingOperationId>& lostSet);
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
		int recoverAsyncDequeue(std::set<PendingAsyncDequeue>& adset);
		int recoverAsyncEnqueue(std::set<PendingAsyncEnqueue>& aeset);
		void cleanEnqueueLog(int intervalInSeconds,int warningSize);
		void cleanDequeueLog(int intervalInSeconds,int warningSize);
};

}}}
#endif //~ASYNC_OPERATION_LOGGER_H
