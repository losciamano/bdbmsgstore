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

#ifndef PENDING_OPERATION_SET_H
#define PENDING_OPERATION_SET_H

#include "PendingOperationType.h"
#include <set>
#include <map>

namespace qpid {
namespace store {
namespace bdb {

typedef std::map< uint64_t,PendingAsyncOperation > PendingOperationSubset;
typedef std::map< uint64_t, PendingOperationSubset  > PendingOperationSet;
typedef std::map< uint64_t,PendingAsyncDequeue > PendingDequeueSubset;
typedef std::map< uint64_t, PendingDequeueSubset> PendingDequeueSet;
typedef std::map< uint64_t,PendingAsyncEnqueue > PendingEnqueueSubset;
typedef std::map< uint64_t, PendingEnqueueSubset> PendingEnqueueSet;

}}} // namespace qpid::store::bdb


#endif //~PENDING_OPERATION_SET_H
