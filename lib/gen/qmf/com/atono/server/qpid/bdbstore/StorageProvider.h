
#ifndef _MANAGEMENT_STORAGEPROVIDER_
#define _MANAGEMENT_STORAGEPROVIDER_

//
// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
// 
//   http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//

// This source file was created by a code generator.
// Please do not edit.

#include "qpid/management/ManagementObject.h"

namespace qpid {
    namespace management {
        class ManagementAgent;
    }
}

namespace qmf {
namespace com {
namespace atono {
namespace server {
namespace qpid {
namespace bdbstore {


class StorageProvider : public ::qpid::management::ManagementObject
{
  private:

    static std::string packageName;
    static std::string className;
    static uint8_t     md5Sum[MD5_LEN];


    // Properties
    ::qpid::management::ObjectId brokerRef;
    std::string location;
    std::string bdbBaseDir;
    std::string type;
    uint32_t num_jfiles;
    uint32_t num_thread_enqueue;
    uint32_t num_thread_dequeue;
    bool compact;
    bool smart_async;
    bool acceptRecovery;
    std::string mongoHost;
    std::string mongoPort;
    std::string mongoDbName;
    std::string mongoCollection;
    uint32_t enqLogCleanerTimeInterval;
    uint32_t deqLogCleanerTimeInterval;
    uint32_t enqLogCleanerWarningSize;
    uint32_t deqLogCleanerWarningSize;

    // Statistics


    // Per-Thread Statistics
    struct PerThreadStats {
        uint32_t  pendingAsyncEnqueue;
        uint32_t  pendingAsyncDequeue;
        uint32_t  executedEnqueue;
        uint32_t  executedDequeue;
        uint32_t  skippedEnqueue;
        uint32_t  skippedDequeue;
        uint64_t  totalEnqueueTime;
        uint64_t  totalDequeueTime;
        uint64_t  enqueueTimeCount;
        uint64_t  enqueueTimeTotal;
        uint64_t  enqueueTimeMin;
        uint64_t  enqueueTimeMax;
        uint64_t  dequeueTimeCount;
        uint64_t  dequeueTimeTotal;
        uint64_t  dequeueTimeMin;
        uint64_t  dequeueTimeMax;

    };

    struct PerThreadStats** perThreadStatsArray;

    inline struct PerThreadStats* getThreadStats() {
        int idx = getThreadIndex();
        struct PerThreadStats* threadStats = perThreadStatsArray[idx];
        if (threadStats == 0) {
            threadStats = new(PerThreadStats);
            perThreadStatsArray[idx] = threadStats;
            threadStats->pendingAsyncEnqueue = 0;
            threadStats->pendingAsyncDequeue = 0;
            threadStats->executedEnqueue = 0;
            threadStats->executedDequeue = 0;
            threadStats->skippedEnqueue = 0;
            threadStats->skippedDequeue = 0;
            threadStats->totalEnqueueTime = 0;
            threadStats->totalDequeueTime = 0;
            threadStats->enqueueTimeCount = 0;
            threadStats->enqueueTimeMin   = std::numeric_limits<uint64_t>::max();
            threadStats->enqueueTimeMax   = std::numeric_limits<uint64_t>::min();
            threadStats->enqueueTimeTotal = 0;
            threadStats->dequeueTimeCount = 0;
            threadStats->dequeueTimeMin   = std::numeric_limits<uint64_t>::max();
            threadStats->dequeueTimeMax   = std::numeric_limits<uint64_t>::min();
            threadStats->dequeueTimeTotal = 0;

        }
        return threadStats;
    }

    void aggregatePerThreadStats(struct PerThreadStats*) const;

  public:
    static void writeSchema(std::string& schema);
    void mapEncodeValues(::qpid::types::Variant::Map& map,
                         bool includeProperties=true,
                         bool includeStatistics=true);
    void mapDecodeValues(const ::qpid::types::Variant::Map& map);
    void doMethod(std::string&           methodName,
                  const ::qpid::types::Variant::Map& inMap,
                  ::qpid::types::Variant::Map& outMap,
                  const std::string& userId);
    std::string getKey() const;

    uint32_t writePropertiesSize() const;
    void readProperties(const std::string& buf);
    void writeProperties(std::string& buf) const;
    void writeStatistics(std::string& buf, bool skipHeaders = false);
    void doMethod(std::string& methodName,
                  const std::string& inBuf,
                  std::string& outBuf,
                  const std::string& userId);


    writeSchemaCall_t getWriteSchemaCall() { return writeSchema; }


    StorageProvider(::qpid::management::ManagementAgent* agent,
                            ::qpid::management::Manageable* coreObject, ::qpid::management::Manageable* _parent);
    ~StorageProvider();

    

    static void registerSelf(::qpid::management::ManagementAgent* agent);
    std::string& getPackageName() const { return packageName; }
    std::string& getClassName() const { return className; }
    uint8_t* getMd5Sum() const { return md5Sum; }

    // Method IDs

    // Accessor Methods
    inline void set_brokerRef (const ::qpid::management::ObjectId& val) {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        brokerRef = val;
        configChanged = true;
    }
    inline const ::qpid::management::ObjectId& get_brokerRef() {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        return brokerRef;
    }
    inline void set_location (const std::string& val) {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        location = val;
        configChanged = true;
    }
    inline const std::string& get_location() {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        return location;
    }
    inline void set_bdbBaseDir (const std::string& val) {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        bdbBaseDir = val;
        configChanged = true;
    }
    inline const std::string& get_bdbBaseDir() {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        return bdbBaseDir;
    }
    inline void set_type (const std::string& val) {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        type = val;
        configChanged = true;
    }
    inline const std::string& get_type() {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        return type;
    }
    inline void set_num_jfiles (uint32_t val) {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        num_jfiles = val;
        configChanged = true;
    }
    inline uint32_t get_num_jfiles() {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        return num_jfiles;
    }
    inline void set_num_thread_enqueue (uint32_t val) {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        num_thread_enqueue = val;
        configChanged = true;
    }
    inline uint32_t get_num_thread_enqueue() {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        return num_thread_enqueue;
    }
    inline void set_num_thread_dequeue (uint32_t val) {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        num_thread_dequeue = val;
        configChanged = true;
    }
    inline uint32_t get_num_thread_dequeue() {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        return num_thread_dequeue;
    }
    inline void set_compact (bool val) {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        compact = val;
        configChanged = true;
    }
    inline bool get_compact() {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        return compact;
    }
    inline void set_smart_async (bool val) {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        smart_async = val;
        configChanged = true;
    }
    inline bool get_smart_async() {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        return smart_async;
    }
    inline void set_acceptRecovery (bool val) {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        acceptRecovery = val;
        configChanged = true;
    }
    inline bool get_acceptRecovery() {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        return acceptRecovery;
    }
    inline void set_mongoHost (const std::string& val) {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        mongoHost = val;
        configChanged = true;
    }
    inline const std::string& get_mongoHost() {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        return mongoHost;
    }
    inline void set_mongoPort (const std::string& val) {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        mongoPort = val;
        configChanged = true;
    }
    inline const std::string& get_mongoPort() {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        return mongoPort;
    }
    inline void set_mongoDbName (const std::string& val) {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        mongoDbName = val;
        configChanged = true;
    }
    inline const std::string& get_mongoDbName() {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        return mongoDbName;
    }
    inline void set_mongoCollection (const std::string& val) {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        mongoCollection = val;
        configChanged = true;
    }
    inline const std::string& get_mongoCollection() {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        return mongoCollection;
    }
    inline void set_enqLogCleanerTimeInterval (uint32_t val) {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        enqLogCleanerTimeInterval = val;
        configChanged = true;
    }
    inline uint32_t get_enqLogCleanerTimeInterval() {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        return enqLogCleanerTimeInterval;
    }
    inline void set_deqLogCleanerTimeInterval (uint32_t val) {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        deqLogCleanerTimeInterval = val;
        configChanged = true;
    }
    inline uint32_t get_deqLogCleanerTimeInterval() {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        return deqLogCleanerTimeInterval;
    }
    inline void set_enqLogCleanerWarningSize (uint32_t val) {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        enqLogCleanerWarningSize = val;
        configChanged = true;
    }
    inline uint32_t get_enqLogCleanerWarningSize() {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        return enqLogCleanerWarningSize;
    }
    inline void set_deqLogCleanerWarningSize (uint32_t val) {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        deqLogCleanerWarningSize = val;
        configChanged = true;
    }
    inline uint32_t get_deqLogCleanerWarningSize() {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        return deqLogCleanerWarningSize;
    }
    inline void inc_pendingAsyncEnqueue (uint32_t by = 1) {
        getThreadStats()->pendingAsyncEnqueue += by;
        instChanged = true;
    }
    inline void dec_pendingAsyncEnqueue (uint32_t by = 1) {
        getThreadStats()->pendingAsyncEnqueue -= by;
        instChanged = true;
    }
    inline void inc_pendingAsyncDequeue (uint32_t by = 1) {
        getThreadStats()->pendingAsyncDequeue += by;
        instChanged = true;
    }
    inline void dec_pendingAsyncDequeue (uint32_t by = 1) {
        getThreadStats()->pendingAsyncDequeue -= by;
        instChanged = true;
    }
    inline void inc_executedEnqueue (uint32_t by = 1) {
        getThreadStats()->executedEnqueue += by;
        instChanged = true;
    }
    inline void dec_executedEnqueue (uint32_t by = 1) {
        getThreadStats()->executedEnqueue -= by;
        instChanged = true;
    }
    inline void inc_executedDequeue (uint32_t by = 1) {
        getThreadStats()->executedDequeue += by;
        instChanged = true;
    }
    inline void dec_executedDequeue (uint32_t by = 1) {
        getThreadStats()->executedDequeue -= by;
        instChanged = true;
    }
    inline void inc_skippedEnqueue (uint32_t by = 1) {
        getThreadStats()->skippedEnqueue += by;
        instChanged = true;
    }
    inline void dec_skippedEnqueue (uint32_t by = 1) {
        getThreadStats()->skippedEnqueue -= by;
        instChanged = true;
    }
    inline void inc_skippedDequeue (uint32_t by = 1) {
        getThreadStats()->skippedDequeue += by;
        instChanged = true;
    }
    inline void dec_skippedDequeue (uint32_t by = 1) {
        getThreadStats()->skippedDequeue -= by;
        instChanged = true;
    }
    inline void inc_totalEnqueueTime (uint64_t by = 1) {
        getThreadStats()->totalEnqueueTime += by;
        instChanged = true;
    }
    inline void dec_totalEnqueueTime (uint64_t by = 1) {
        getThreadStats()->totalEnqueueTime -= by;
        instChanged = true;
    }
    inline void inc_totalDequeueTime (uint64_t by = 1) {
        getThreadStats()->totalDequeueTime += by;
        instChanged = true;
    }
    inline void dec_totalDequeueTime (uint64_t by = 1) {
        getThreadStats()->totalDequeueTime -= by;
        instChanged = true;
    }
    inline void set_enqueueTime (uint64_t val) {
        getThreadStats()->enqueueTimeCount++;
        getThreadStats()->enqueueTimeTotal += val;
        if (getThreadStats()->enqueueTimeMin > val)
            getThreadStats()->enqueueTimeMin = val;
        if (getThreadStats()->enqueueTimeMax < val)
            getThreadStats()->enqueueTimeMax = val;
        instChanged = true;
    }
    inline void set_dequeueTime (uint64_t val) {
        getThreadStats()->dequeueTimeCount++;
        getThreadStats()->dequeueTimeTotal += val;
        if (getThreadStats()->dequeueTimeMin > val)
            getThreadStats()->dequeueTimeMin = val;
        if (getThreadStats()->dequeueTimeMax < val)
            getThreadStats()->dequeueTimeMax = val;
        instChanged = true;
    }

};

}}}}}}

#endif  /*!_MANAGEMENT_STORAGEPROVIDER_*/
