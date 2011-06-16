#ifndef PID_TRACKER_H
#define PID_TRACKER_H

#include "boost/thread.hpp"
#include <map>
#include <set>

namespace qpid {
namespace store {
namespace bdb {

typedef std::set<uint64_t> MessageIdSet;
typedef std::map<uint64_t,MessageIdSet> QMMap;

class PidRecord
{
	public:
		boost::mutex pidMutex;
		boost::condition_variable notYetEnqueued;
		uint64_t lastPid;
		std::set<uint64_t> gapPidSet;
		PidRecord(uint64_t lastPid=0):
			lastPid(lastPid)
		{}
		PidRecord(const PidRecord& ref);
		~PidRecord()
		{
			//boost::mutex::scoped_lock lock(this->pidMutex);
			gapPidSet.clear();
		}
		PidRecord& operator=(const PidRecord& other)
		{
			if (this == &other) return *this; //same object
			{
				boost::mutex::scoped_lock lock(this->pidMutex);
				this->lastPid=other.lastPid;
				gapPidSet.clear();
				gapPidSet.insert(other.gapPidSet.begin(),other.gapPidSet.end());
			}
			return *this;
		}
		int operator==(const PidRecord&)
		{
			return 1;
		}
};
	

class PidTracker
{
	private:
		boost::mutex dupMutex;
		boost::condition_variable duplicateNotEnqueued;
		QMMap dupMap;
		boost::mutex pendMutex;
		QMMap pendMap;
		PidRecord mainRecord;
	public :
		PidTracker()
		{}
		~PidTracker()
		{
			dupMap.clear();
			pendMap.clear();
		}
		void reset(uint64_t pid=0)
		{
			boost::mutex::scoped_lock(mainRecord.pidMutex);
			mainRecord.lastPid=pid;
			mainRecord.gapPidSet.clear();
		}
		bool addPid(uint64_t pid,uint64_t qid);
		bool addDuplicate(uint64_t pid,uint64_t qid);
		int waitForPid(uint64_t pid,uint64_t qid);
		bool willEnqueue(uint64_t pid,uint64_t qid);
		bool dequeueCheck(uint64_t pid,uint64_t qid);
		bool enqueueCheck(uint64_t pid,uint64_t qid);
};

}}} //namespace qpid::store::bdb
#endif //~PID_TRACKER_H
