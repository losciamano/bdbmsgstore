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
