#include "PidTracker.h"
#include <iostream>

using namespace std;
using namespace qpid::store::bdb;

PidRecord::PidRecord(const PidRecord& ref)
{
	boost::mutex::scoped_lock lock(this->pidMutex);
	this->lastPid=ref.lastPid;
	this->gapPidSet = ref.gapPidSet;
}

bool PidTracker::addPid(uint64_t pid,uint64_t qid)
{
	bool updated=false;
	{
		boost::mutex::scoped_lock lock(dupMutex);
		QMMap::iterator mit = dupMap.find(qid);
		if (mit!=dupMap.end())
		{
			MessageIdSet::iterator sit = mit->second.find(pid);
			if (sit!=mit->second.end())
			{
				mit->second.erase(sit);
				if (mit->second.empty())
				{
					dupMap.erase(mit);
				}
				updated=true;
			}
		}
	}
	if (updated) 
	{
		duplicateNotEnqueued.notify_all();
	} else 
	{
		boost::mutex::scoped_lock lock(mainRecord.pidMutex);
		if ((static_cast<int>(pid-mainRecord.lastPid))>1)
		{
			if (pid>mainRecord.lastPid)
			{
				mainRecord.gapPidSet.insert(pid);
			}
		} else 
		{
			mainRecord.lastPid=std::max(pid,mainRecord.lastPid);
			std::set<uint64_t>::iterator pidIt=mainRecord.gapPidSet.begin();					
			while((static_cast<int>(*pidIt-mainRecord.lastPid)<=1)&&(pidIt!=mainRecord.gapPidSet.end())) 
			{
				mainRecord.lastPid=std::max(*pidIt,mainRecord.lastPid);
				mainRecord.gapPidSet.erase(pidIt++);
			}
			updated=true;
		}
	}
	if (updated) mainRecord.notYetEnqueued.notify_all();
	return updated;
}

/**
*	The main record track only the first enqueue of a persistence Id. If more than one message with the same persistence id is enqueue
*	the main record fail and the async dequeue method will try to delete a unexistent key. To avoid this, we keep track of duplicate id.
**/
bool PidTracker::addDuplicate(uint64_t pid,uint64_t qid) 
{
	boost::mutex::scoped_lock lock(dupMutex);
	QMMap::iterator mit=dupMap.find(qid);
	if (mit!=dupMap.end())
	{
		mit->second.insert(pid);
	} else 
	{
		MessageIdSet localset;
		localset.insert(pid);
		dupMap.insert(QMMap::value_type(qid,localset));
	}
	return true;
}
int PidTracker::waitForPid(uint64_t pid,uint64_t qid)
{
	int try_count=0;
	{
		boost::mutex::scoped_lock lock(dupMutex);
		bool notNow=true;
		while(notNow)
		{
			QMMap::iterator mit = dupMap.find(qid);
			if (mit!=dupMap.end())
			{
				MessageIdSet::iterator sit = mit->second.find(pid);
				if (sit != mit->second.end())
				{
					duplicateNotEnqueued.wait(lock);
					notNow=true;
					try_count++;
				} else
				{
					notNow=false;
				}
			} else {
				notNow=false;
			}
		}
	}
	{
		boost::mutex::scoped_lock lock(mainRecord.pidMutex);
		while (pid>mainRecord.lastPid)
		{
			mainRecord.notYetEnqueued.wait(lock);
			try_count++;
		}
	}
	return try_count;
}
bool PidTracker::willEnqueue(uint64_t pid,uint64_t qid)
{
	//cout<<"willEnqueue("<<pid<<","<<qid<<")"<<endl;
	{
		boost::mutex::scoped_lock lock(pendMutex);
		QMMap::iterator mit = pendMap.find(qid);
		if (mit!=pendMap.end())
		{
			return mit->second.insert(pid).second;
		} else 
		{
			MessageIdSet localset;
			localset.insert(pid);
			pendMap.insert(QMMap::value_type(qid,localset));
			return true;
		}
	}
}
bool PidTracker::dequeueCheck(uint64_t pid,uint64_t qid)
{
	//cout<<"dequeueCheck("<<pid<<","<<qid<<");"<<endl;
	{
		boost::mutex::scoped_lock lock(pendMutex);
		QMMap::iterator mit = pendMap.find(qid);
		if (mit!=pendMap.end())
		{
			MessageIdSet::iterator sit = mit->second.find(pid);
			if (sit!=mit->second.end())
			{
				//Message found => delete it
				mit->second.erase(sit);
				//cout<<"Dequeue Not Required => "<<pid<<","<<qid<<endl;
				return false;	//Dequeue not required !
			} else 
			{
				return true;	//Dequeue Required
			}
		} else
		{
			return true; //Dequeue required
		}
	}
}
bool PidTracker::enqueueCheck(uint64_t pid,uint64_t qid)
{
	//cout<<"enqueueCheck("<<pid<<","<<qid<<");"<<endl;
	{
		boost::mutex::scoped_lock lock(pendMutex);
		QMMap::iterator mit = pendMap.find(qid);
		if (mit!=pendMap.end())
		{
			MessageIdSet::iterator sit = mit->second.find(pid);
			if (sit!=mit->second.end())
			{
				//Message found => delete it
				mit->second.erase(sit);
				//cout<<"Enqueue Required => "<<pid<<","<<qid<<endl;
				return true; //Enqueue required (no dequeue requested)
			} else 
			{
				return false; //Enqueue not required (a dequeue has been requested)
			}
		} else
		{
			return false; //Enqueue not required (a dequeue has been requested)
		}
	}
}
