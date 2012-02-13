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

#ifndef PENDING_OPERATION_TYPE_H
#define PENDING_OPERATION_TYPE_H

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


}}} //namespace qpid::store::bdb
#endif //~PENDING_OPERATION_TYPE_H
