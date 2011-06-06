
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

#include "qpid/management/Manageable.h"
#include "qpid/management/Buffer.h"
#include "qpid/types/Variant.h"
#include "qpid/amqp_0_10/Codecs.h"
#include "qpid/management/ManagementAgent.h"
#include "StorageProvider.h"

#include <iostream>
#include <sstream>

using namespace qmf::com::atono::server::qpid::bdbstore;
using           qpid::management::ManagementAgent;
using           qpid::management::Manageable;
using           qpid::management::ManagementObject;
using           qpid::management::Args;
using           qpid::management::Mutex;
using           std::string;

string  StorageProvider::packageName  = string ("com.atono.server.qpid.bdbstore");
string  StorageProvider::className    = string ("storageprovider");
uint8_t StorageProvider::md5Sum[MD5_LEN]   =
    {0x61,0xa,0xce,0x9f,0xf5,0x3d,0x35,0xf8,0x45,0x1e,0x48,0xdb,0x60,0x67,0x46,0x6c};

StorageProvider::StorageProvider (ManagementAgent*, Manageable* _core, ::qpid::management::Manageable* _parent) :
    ManagementObject(_core)
{
    brokerRef = _parent->GetManagementObject ()->getObjectId ();
    location = "";
    bdbBaseDir = "";
    type = "";
    num_jfiles = 0;
    num_thread_enqueue = 0;
    num_thread_dequeue = 0;
    compact = 0;
    smart_async = 0;
    acceptRecovery = 0;
    mongoHost = "";
    mongoPort = "";
    mongoDbName = "";
    mongoCollection = "";
    enqLogCleanerTimeInterval = 0;
    deqLogCleanerTimeInterval = 0;
    enqLogCleanerWarningSize = 0;
    deqLogCleanerWarningSize = 0;



    perThreadStatsArray = new struct PerThreadStats*[maxThreads];
    for (int idx = 0; idx < maxThreads; idx++)
        perThreadStatsArray[idx] = 0;

}

StorageProvider::~StorageProvider ()
{

    for (int idx = 0; idx < maxThreads; idx++)
        if (perThreadStatsArray[idx] != 0)
            delete perThreadStatsArray[idx];
    delete[] perThreadStatsArray;

}

namespace {
    const string NAME("name");
    const string TYPE("type");
    const string ACCESS("access");
    const string IS_INDEX("index");
    const string IS_OPTIONAL("optional");
    const string UNIT("unit");
    const string MIN("min");
    const string MAX("max");
    const string MAXLEN("maxlen");
    const string DESC("desc");
    const string ARGCOUNT("argCount");
    const string ARGS("args");
    const string DIR("dir");
    const string DEFAULT("default");
}

void StorageProvider::registerSelf(ManagementAgent* agent)
{
    agent->registerClass(packageName, className, md5Sum, writeSchema);
}

void StorageProvider::writeSchema (std::string& schema)
{
    const int _bufSize=65536;
    char _msgChars[_bufSize];
    ::qpid::management::Buffer buf(_msgChars, _bufSize);
    ::qpid::types::Variant::Map ft;

    // Schema class header:
    buf.putOctet       (CLASS_KIND_TABLE);
    buf.putShortString (packageName); // Package Name
    buf.putShortString (className);   // Class Name
    buf.putBin128      (md5Sum);      // Schema Hash
    buf.putShort       (18); // Config Element Count
    buf.putShort       (16); // Inst Element Count
    buf.putShort       (0); // Method Count

    // Properties
    ft.clear();
    ft[NAME] = "brokerRef";
    ft[TYPE] = TYPE_REF;
    ft[ACCESS] = ACCESS_RO;
    ft[IS_INDEX] = 1;
    ft[IS_OPTIONAL] = 0;
    buf.putMap(ft);

    ft.clear();
    ft[NAME] = "location";
    ft[TYPE] = TYPE_SSTR;
    ft[ACCESS] = ACCESS_RO;
    ft[IS_INDEX] = 0;
    ft[IS_OPTIONAL] = 0;
    ft[DESC] = "Logical directory on disk";
    buf.putMap(ft);

    ft.clear();
    ft[NAME] = "bdbBaseDir";
    ft[TYPE] = TYPE_SSTR;
    ft[ACCESS] = ACCESS_RO;
    ft[IS_INDEX] = 0;
    ft[IS_OPTIONAL] = 0;
    ft[DESC] = "Directory used for Berkeley Db Environment";
    buf.putMap(ft);

    ft.clear();
    ft[NAME] = "type";
    ft[TYPE] = TYPE_SSTR;
    ft[ACCESS] = ACCESS_RO;
    ft[IS_INDEX] = 1;
    ft[IS_OPTIONAL] = 0;
    ft[DESC] = "Provider Name";
    buf.putMap(ft);

    ft.clear();
    ft[NAME] = "num_jfiles";
    ft[TYPE] = TYPE_U32;
    ft[ACCESS] = ACCESS_RO;
    ft[IS_INDEX] = 0;
    ft[IS_OPTIONAL] = 0;
    ft[DESC] = "Default number of files used for Journal";
    buf.putMap(ft);

    ft.clear();
    ft[NAME] = "num_thread_enqueue";
    ft[TYPE] = TYPE_U32;
    ft[ACCESS] = ACCESS_RO;
    ft[IS_INDEX] = 0;
    ft[IS_OPTIONAL] = 0;
    ft[DESC] = "Number of threads used for enqueue thread pool";
    buf.putMap(ft);

    ft.clear();
    ft[NAME] = "num_thread_dequeue";
    ft[TYPE] = TYPE_U32;
    ft[ACCESS] = ACCESS_RO;
    ft[IS_INDEX] = 0;
    ft[IS_OPTIONAL] = 0;
    ft[DESC] = "Number of threads used for dequeue thread pool";
    buf.putMap(ft);

    ft.clear();
    ft[NAME] = "compact";
    ft[TYPE] = TYPE_BOOL;
    ft[ACCESS] = ACCESS_RO;
    ft[IS_INDEX] = 0;
    ft[IS_OPTIONAL] = 0;
    ft[DESC] = "Compact flag option";
    buf.putMap(ft);

    ft.clear();
    ft[NAME] = "smart_async";
    ft[TYPE] = TYPE_BOOL;
    ft[ACCESS] = ACCESS_RO;
    ft[IS_INDEX] = 0;
    ft[IS_OPTIONAL] = 0;
    ft[DESC] = "Smart Async flag option";
    buf.putMap(ft);

    ft.clear();
    ft[NAME] = "acceptRecovery";
    ft[TYPE] = TYPE_BOOL;
    ft[ACCESS] = ACCESS_RO;
    ft[IS_INDEX] = 0;
    ft[IS_OPTIONAL] = 0;
    ft[DESC] = "Accept Recovery option";
    buf.putMap(ft);

    ft.clear();
    ft[NAME] = "mongoHost";
    ft[TYPE] = TYPE_SSTR;
    ft[ACCESS] = ACCESS_RO;
    ft[IS_INDEX] = 0;
    ft[IS_OPTIONAL] = 0;
    ft[DESC] = "Mongo Database Host for Accept Recovery";
    buf.putMap(ft);

    ft.clear();
    ft[NAME] = "mongoPort";
    ft[TYPE] = TYPE_SSTR;
    ft[ACCESS] = ACCESS_RO;
    ft[IS_INDEX] = 0;
    ft[IS_OPTIONAL] = 0;
    ft[DESC] = "Mongo Database Port for Accept Recovery";
    buf.putMap(ft);

    ft.clear();
    ft[NAME] = "mongoDbName";
    ft[TYPE] = TYPE_SSTR;
    ft[ACCESS] = ACCESS_RO;
    ft[IS_INDEX] = 0;
    ft[IS_OPTIONAL] = 0;
    ft[DESC] = "Mongo Database Name for Accept Recovery";
    buf.putMap(ft);

    ft.clear();
    ft[NAME] = "mongoCollection";
    ft[TYPE] = TYPE_SSTR;
    ft[ACCESS] = ACCESS_RO;
    ft[IS_INDEX] = 0;
    ft[IS_OPTIONAL] = 0;
    ft[DESC] = "Mongo Collection Name for Accept Recovery";
    buf.putMap(ft);

    ft.clear();
    ft[NAME] = "enqLogCleanerTimeInterval";
    ft[TYPE] = TYPE_U32;
    ft[ACCESS] = ACCESS_RO;
    ft[IS_INDEX] = 0;
    ft[IS_OPTIONAL] = 0;
    ft[UNIT] = "second";
    ft[DESC] = "Enqueue log cleaner time interval";
    buf.putMap(ft);

    ft.clear();
    ft[NAME] = "deqLogCleanerTimeInterval";
    ft[TYPE] = TYPE_U32;
    ft[ACCESS] = ACCESS_RO;
    ft[IS_INDEX] = 0;
    ft[IS_OPTIONAL] = 0;
    ft[UNIT] = "second";
    ft[DESC] = "Dequeue Log cleaner time interval";
    buf.putMap(ft);

    ft.clear();
    ft[NAME] = "enqLogCleanerWarningSize";
    ft[TYPE] = TYPE_U32;
    ft[ACCESS] = ACCESS_RO;
    ft[IS_INDEX] = 0;
    ft[IS_OPTIONAL] = 0;
    ft[UNIT] = "byte";
    ft[DESC] = "Enqueue Log cleaner warning size";
    buf.putMap(ft);

    ft.clear();
    ft[NAME] = "deqLogCleanerWarningSize";
    ft[TYPE] = TYPE_U32;
    ft[ACCESS] = ACCESS_RO;
    ft[IS_INDEX] = 0;
    ft[IS_OPTIONAL] = 0;
    ft[UNIT] = "byte";
    ft[DESC] = "Dequeue Log cleaner warning size";
    buf.putMap(ft);


    // Statistics
    ft.clear();
    ft[NAME] = "pendingAsyncEnqueue";
    ft[TYPE] = TYPE_U32;
    ft[DESC] = "Async Enqueue operations currently in the thread pool";
    buf.putMap(ft);

    ft.clear();
    ft[NAME] = "pendingAsyncDequeue";
    ft[TYPE] = TYPE_U32;
    ft[DESC] = "Async Dequeue operations currently in the thread pool";
    buf.putMap(ft);

    ft.clear();
    ft[NAME] = "executedEnqueue";
    ft[TYPE] = TYPE_U32;
    ft[DESC] = "Number of Enqueue executed by the store";
    buf.putMap(ft);

    ft.clear();
    ft[NAME] = "executedDequeue";
    ft[TYPE] = TYPE_U32;
    ft[DESC] = "Number of Dequeue executed by the store";
    buf.putMap(ft);

    ft.clear();
    ft[NAME] = "skippedEnqueue";
    ft[TYPE] = TYPE_U32;
    ft[DESC] = "Number of Enqueue skipped due to smart async";
    buf.putMap(ft);

    ft.clear();
    ft[NAME] = "skippedDequeue";
    ft[TYPE] = TYPE_U32;
    ft[DESC] = "Number of Dequeue skipped due to smart async";
    buf.putMap(ft);

    ft.clear();
    ft[NAME] = "totalEnqueueTime";
    ft[TYPE] = TYPE_U64;
    ft[DESC] = "Total time spent in enqueue operations";
    buf.putMap(ft);

    ft.clear();
    ft[NAME] = "totalDequeueTime";
    ft[TYPE] = TYPE_U64;
    ft[DESC] = "Total time spent in dequeue operations";
    buf.putMap(ft);

    ft.clear();
    ft[NAME] = "enqueueTimeSamples";
    ft[TYPE] = TYPE_DELTATIME;
    ft[DESC] = " (Samples)";
    buf.putMap(ft);

    ft.clear();
    ft[NAME] = "enqueueTimeMin";
    ft[TYPE] = TYPE_DELTATIME;
    ft[DESC] = " (Min)";
    buf.putMap(ft);

    ft.clear();
    ft[NAME] = "enqueueTimeMax";
    ft[TYPE] = TYPE_DELTATIME;
    ft[DESC] = " (Max)";
    buf.putMap(ft);

    ft.clear();
    ft[NAME] = "enqueueTimeAverage";
    ft[TYPE] = TYPE_DELTATIME;
    ft[DESC] = " (Average)";
    buf.putMap(ft);

    ft.clear();
    ft[NAME] = "dequeueTimeSamples";
    ft[TYPE] = TYPE_DELTATIME;
    ft[DESC] = " (Samples)";
    buf.putMap(ft);

    ft.clear();
    ft[NAME] = "dequeueTimeMin";
    ft[TYPE] = TYPE_DELTATIME;
    ft[DESC] = " (Min)";
    buf.putMap(ft);

    ft.clear();
    ft[NAME] = "dequeueTimeMax";
    ft[TYPE] = TYPE_DELTATIME;
    ft[DESC] = " (Max)";
    buf.putMap(ft);

    ft.clear();
    ft[NAME] = "dequeueTimeAverage";
    ft[TYPE] = TYPE_DELTATIME;
    ft[DESC] = " (Average)";
    buf.putMap(ft);


    // Methods

    {
        uint32_t _len = buf.getPosition();
        buf.reset();
        buf.getRawData(schema, _len);
    }
}


void StorageProvider::aggregatePerThreadStats(struct PerThreadStats* totals) const
{
    totals->pendingAsyncEnqueue = 0;
    totals->pendingAsyncDequeue = 0;
    totals->executedEnqueue = 0;
    totals->executedDequeue = 0;
    totals->skippedEnqueue = 0;
    totals->skippedDequeue = 0;
    totals->totalEnqueueTime = 0;
    totals->totalDequeueTime = 0;
    totals->enqueueTimeCount = 0;
    totals->enqueueTimeMin   = std::numeric_limits<uint64_t>::max();
    totals->enqueueTimeMax   = std::numeric_limits<uint64_t>::min();
    totals->enqueueTimeTotal = 0;
    totals->dequeueTimeCount = 0;
    totals->dequeueTimeMin   = std::numeric_limits<uint64_t>::max();
    totals->dequeueTimeMax   = std::numeric_limits<uint64_t>::min();
    totals->dequeueTimeTotal = 0;

    for (int idx = 0; idx < maxThreads; idx++) {
        struct PerThreadStats* threadStats = perThreadStatsArray[idx];
        if (threadStats != 0) {
            totals->pendingAsyncEnqueue += threadStats->pendingAsyncEnqueue;
            totals->pendingAsyncDequeue += threadStats->pendingAsyncDequeue;
            totals->executedEnqueue += threadStats->executedEnqueue;
            totals->executedDequeue += threadStats->executedDequeue;
            totals->skippedEnqueue += threadStats->skippedEnqueue;
            totals->skippedDequeue += threadStats->skippedDequeue;
            totals->totalEnqueueTime += threadStats->totalEnqueueTime;
            totals->totalDequeueTime += threadStats->totalDequeueTime;
            totals->enqueueTimeCount += threadStats->enqueueTimeCount;
            if (totals->enqueueTimeMin > threadStats->enqueueTimeMin)
                totals->enqueueTimeMin = threadStats->enqueueTimeMin;
            if (totals->enqueueTimeMax < threadStats->enqueueTimeMax)
                totals->enqueueTimeMax = threadStats->enqueueTimeMax;
            totals->enqueueTimeTotal += threadStats->enqueueTimeTotal;
            totals->dequeueTimeCount += threadStats->dequeueTimeCount;
            if (totals->dequeueTimeMin > threadStats->dequeueTimeMin)
                totals->dequeueTimeMin = threadStats->dequeueTimeMin;
            if (totals->dequeueTimeMax < threadStats->dequeueTimeMax)
                totals->dequeueTimeMax = threadStats->dequeueTimeMax;
            totals->dequeueTimeTotal += threadStats->dequeueTimeTotal;

        }
    }
}



uint32_t StorageProvider::writePropertiesSize() const
{
    uint32_t size = writeTimestampsSize();

    size += 16;  // brokerRef
    size += (1 + location.length());  // location
    size += (1 + bdbBaseDir.length());  // bdbBaseDir
    size += (1 + type.length());  // type
    size += 4;  // num_jfiles
    size += 4;  // num_thread_enqueue
    size += 4;  // num_thread_dequeue
    size += 1;  // compact
    size += 1;  // smart_async
    size += 1;  // acceptRecovery
    size += (1 + mongoHost.length());  // mongoHost
    size += (1 + mongoPort.length());  // mongoPort
    size += (1 + mongoDbName.length());  // mongoDbName
    size += (1 + mongoCollection.length());  // mongoCollection
    size += 4;  // enqLogCleanerTimeInterval
    size += 4;  // deqLogCleanerTimeInterval
    size += 4;  // enqLogCleanerWarningSize
    size += 4;  // deqLogCleanerWarningSize

    return size;
}

void StorageProvider::readProperties (const std::string& _sBuf)
{
    char *_tmpBuf = new char[_sBuf.length()];
    memcpy(_tmpBuf, _sBuf.data(), _sBuf.length());
    ::qpid::management::Buffer buf(_tmpBuf, _sBuf.length());
    Mutex::ScopedLock mutex(accessLock);

    {
        std::string _tbuf;
        buf.getRawData(_tbuf, writeTimestampsSize());
        readTimestamps(_tbuf);
    }


    {std::string _s; buf.getRawData(_s, brokerRef.encodedSize()); brokerRef.decode(_s);};
    buf.getShortString(location);
    buf.getShortString(bdbBaseDir);
    buf.getShortString(type);
    num_jfiles = buf.getLong();
    num_thread_enqueue = buf.getLong();
    num_thread_dequeue = buf.getLong();
    compact = buf.getOctet()==1;
    smart_async = buf.getOctet()==1;
    acceptRecovery = buf.getOctet()==1;
    buf.getShortString(mongoHost);
    buf.getShortString(mongoPort);
    buf.getShortString(mongoDbName);
    buf.getShortString(mongoCollection);
    enqLogCleanerTimeInterval = buf.getLong();
    deqLogCleanerTimeInterval = buf.getLong();
    enqLogCleanerWarningSize = buf.getLong();
    deqLogCleanerWarningSize = buf.getLong();


    delete [] _tmpBuf;
}

void StorageProvider::writeProperties (std::string& _sBuf) const
{
    const int _bufSize=65536;
    char _msgChars[_bufSize];
    ::qpid::management::Buffer buf(_msgChars, _bufSize);

    Mutex::ScopedLock mutex(accessLock);
    configChanged = false;

    {
        std::string _tbuf;
        writeTimestamps(_tbuf);
        buf.putRawData(_tbuf);
    }



    {std::string _s; brokerRef.encode(_s); buf.putRawData(_s);};
    buf.putShortString(location);
    buf.putShortString(bdbBaseDir);
    buf.putShortString(type);
    buf.putLong(num_jfiles);
    buf.putLong(num_thread_enqueue);
    buf.putLong(num_thread_dequeue);
    buf.putOctet(compact?1:0);
    buf.putOctet(smart_async?1:0);
    buf.putOctet(acceptRecovery?1:0);
    buf.putShortString(mongoHost);
    buf.putShortString(mongoPort);
    buf.putShortString(mongoDbName);
    buf.putShortString(mongoCollection);
    buf.putLong(enqLogCleanerTimeInterval);
    buf.putLong(deqLogCleanerTimeInterval);
    buf.putLong(enqLogCleanerWarningSize);
    buf.putLong(deqLogCleanerWarningSize);


    uint32_t _bufLen = buf.getPosition();
    buf.reset();

    buf.getRawData(_sBuf, _bufLen);
}

void StorageProvider::writeStatistics (std::string& _sBuf, bool skipHeaders)
{
    const int _bufSize=65536;
    char _msgChars[_bufSize];
    ::qpid::management::Buffer buf(_msgChars, _bufSize);

    Mutex::ScopedLock mutex(accessLock);
    instChanged = false;


    struct PerThreadStats totals;
    aggregatePerThreadStats(&totals);


    if (!skipHeaders) {
        std::string _tbuf;
        writeTimestamps (_tbuf);
        buf.putRawData(_tbuf);
    }

    buf.putLong(totals.pendingAsyncEnqueue);
    buf.putLong(totals.pendingAsyncDequeue);
    buf.putLong(totals.executedEnqueue);
    buf.putLong(totals.executedDequeue);
    buf.putLong(totals.skippedEnqueue);
    buf.putLong(totals.skippedDequeue);
    buf.putLongLong(totals.totalEnqueueTime);
    buf.putLongLong(totals.totalDequeueTime);
    buf.putLongLong(totals.enqueueTimeCount);
    buf.putLongLong(totals.enqueueTimeCount ? totals.enqueueTimeMin : 0);
    buf.putLongLong(totals.enqueueTimeMax);
    buf.putLongLong(totals.enqueueTimeCount ? totals.enqueueTimeTotal / totals.enqueueTimeCount : 0);
    buf.putLongLong(totals.dequeueTimeCount);
    buf.putLongLong(totals.dequeueTimeCount ? totals.dequeueTimeMin : 0);
    buf.putLongLong(totals.dequeueTimeMax);
    buf.putLongLong(totals.dequeueTimeCount ? totals.dequeueTimeTotal / totals.dequeueTimeCount : 0);


    // Maintenance of hi-lo statistics


    for (int idx = 0; idx < maxThreads; idx++) {
        struct PerThreadStats* threadStats = perThreadStatsArray[idx];
        if (threadStats != 0) {
        threadStats->enqueueTimeCount = 0;
        threadStats->enqueueTimeTotal = 0;
        threadStats->enqueueTimeMin   = std::numeric_limits<uint64_t>::max();
        threadStats->enqueueTimeMax   = std::numeric_limits<uint64_t>::min();
        threadStats->dequeueTimeCount = 0;
        threadStats->dequeueTimeTotal = 0;
        threadStats->dequeueTimeMin   = std::numeric_limits<uint64_t>::max();
        threadStats->dequeueTimeMax   = std::numeric_limits<uint64_t>::min();

        }
    }


    uint32_t _bufLen = buf.getPosition();
    buf.reset();

    buf.getRawData(_sBuf, _bufLen);
}

void StorageProvider::doMethod (string&, const string&, string& outStr, const string&)
{
    Manageable::status_t status = Manageable::STATUS_UNKNOWN_METHOD;
    std::string          text;

    bool _matched = false;

    const int _bufSize=65536;
    char _msgChars[_bufSize];
    ::qpid::management::Buffer outBuf(_msgChars, _bufSize);



    if (!_matched) {
        outBuf.putLong(status);
        outBuf.putShortString(Manageable::StatusText(status, text));
    }

    uint32_t _bufLen = outBuf.getPosition();
    outBuf.reset();

    outBuf.getRawData(outStr, _bufLen);
}

std::string StorageProvider::getKey() const
{
    std::stringstream key;

    key << brokerRef.getV2Key() << ",";
    key << type;
    return key.str();
}



void StorageProvider::mapEncodeValues (::qpid::types::Variant::Map& _map,
                                              bool includeProperties,
                                              bool includeStatistics)
{
    using namespace ::qpid::types;
    Mutex::ScopedLock mutex(accessLock);

    if (includeProperties) {
        configChanged = false;
    _map["brokerRef"] = ::qpid::types::Variant(brokerRef);
    _map["location"] = ::qpid::types::Variant(location);
    _map["bdbBaseDir"] = ::qpid::types::Variant(bdbBaseDir);
    _map["type"] = ::qpid::types::Variant(type);
    _map["num_jfiles"] = ::qpid::types::Variant(num_jfiles);
    _map["num_thread_enqueue"] = ::qpid::types::Variant(num_thread_enqueue);
    _map["num_thread_dequeue"] = ::qpid::types::Variant(num_thread_dequeue);
    _map["compact"] = ::qpid::types::Variant(compact);
    _map["smart_async"] = ::qpid::types::Variant(smart_async);
    _map["acceptRecovery"] = ::qpid::types::Variant(acceptRecovery);
    _map["mongoHost"] = ::qpid::types::Variant(mongoHost);
    _map["mongoPort"] = ::qpid::types::Variant(mongoPort);
    _map["mongoDbName"] = ::qpid::types::Variant(mongoDbName);
    _map["mongoCollection"] = ::qpid::types::Variant(mongoCollection);
    _map["enqLogCleanerTimeInterval"] = ::qpid::types::Variant(enqLogCleanerTimeInterval);
    _map["deqLogCleanerTimeInterval"] = ::qpid::types::Variant(deqLogCleanerTimeInterval);
    _map["enqLogCleanerWarningSize"] = ::qpid::types::Variant(enqLogCleanerWarningSize);
    _map["deqLogCleanerWarningSize"] = ::qpid::types::Variant(deqLogCleanerWarningSize);

    }

    if (includeStatistics) {
        instChanged = false;


        struct PerThreadStats totals;
        aggregatePerThreadStats(&totals);



    _map["pendingAsyncEnqueue"] = ::qpid::types::Variant(totals.pendingAsyncEnqueue);
    _map["pendingAsyncDequeue"] = ::qpid::types::Variant(totals.pendingAsyncDequeue);
    _map["executedEnqueue"] = ::qpid::types::Variant(totals.executedEnqueue);
    _map["executedDequeue"] = ::qpid::types::Variant(totals.executedDequeue);
    _map["skippedEnqueue"] = ::qpid::types::Variant(totals.skippedEnqueue);
    _map["skippedDequeue"] = ::qpid::types::Variant(totals.skippedDequeue);
    _map["totalEnqueueTime"] = ::qpid::types::Variant(totals.totalEnqueueTime);
    _map["totalDequeueTime"] = ::qpid::types::Variant(totals.totalDequeueTime);
    _map["enqueueTimeCount"] = ::qpid::types::Variant(totals.enqueueTimeCount);
    _map["enqueueTimeMin"] = (totals.enqueueTimeCount ? ::qpid::types::Variant(totals.enqueueTimeMin) : ::qpid::types::Variant(0));
    _map["enqueueTimeMax"] = ::qpid::types::Variant(totals.enqueueTimeMax);
    _map["enqueueTimeAvg"] = (totals.enqueueTimeCount ? ::qpid::types::Variant((totals.enqueueTimeTotal / totals.enqueueTimeCount)) : ::qpid::types::Variant(0));
    _map["dequeueTimeCount"] = ::qpid::types::Variant(totals.dequeueTimeCount);
    _map["dequeueTimeMin"] = (totals.dequeueTimeCount ? ::qpid::types::Variant(totals.dequeueTimeMin) : ::qpid::types::Variant(0));
    _map["dequeueTimeMax"] = ::qpid::types::Variant(totals.dequeueTimeMax);
    _map["dequeueTimeAvg"] = (totals.dequeueTimeCount ? ::qpid::types::Variant((totals.dequeueTimeTotal / totals.dequeueTimeCount)) : ::qpid::types::Variant(0));


    // Maintenance of hi-lo statistics


        for (int idx = 0; idx < maxThreads; idx++) {
            struct PerThreadStats* threadStats = perThreadStatsArray[idx];
            if (threadStats != 0) {
        threadStats->enqueueTimeCount = 0;
        threadStats->enqueueTimeTotal = 0;
        threadStats->enqueueTimeMin   = std::numeric_limits<uint64_t>::max();
        threadStats->enqueueTimeMax   = std::numeric_limits<uint64_t>::min();
        threadStats->dequeueTimeCount = 0;
        threadStats->dequeueTimeTotal = 0;
        threadStats->dequeueTimeMin   = std::numeric_limits<uint64_t>::max();
        threadStats->dequeueTimeMax   = std::numeric_limits<uint64_t>::min();

            }
        }

    }
}

void StorageProvider::mapDecodeValues (const ::qpid::types::Variant::Map& _map)
{
    ::qpid::types::Variant::Map::const_iterator _i;
    Mutex::ScopedLock mutex(accessLock);

    if ((_i = _map.find("brokerRef")) != _map.end()) {
        brokerRef = _i->second;
    }
    if ((_i = _map.find("location")) != _map.end()) {
        location = (_i->second).getString();
    }
    if ((_i = _map.find("bdbBaseDir")) != _map.end()) {
        bdbBaseDir = (_i->second).getString();
    }
    if ((_i = _map.find("type")) != _map.end()) {
        type = (_i->second).getString();
    }
    if ((_i = _map.find("num_jfiles")) != _map.end()) {
        num_jfiles = _i->second;
    }
    if ((_i = _map.find("num_thread_enqueue")) != _map.end()) {
        num_thread_enqueue = _i->second;
    }
    if ((_i = _map.find("num_thread_dequeue")) != _map.end()) {
        num_thread_dequeue = _i->second;
    }
    if ((_i = _map.find("compact")) != _map.end()) {
        compact = _i->second;
    }
    if ((_i = _map.find("smart_async")) != _map.end()) {
        smart_async = _i->second;
    }
    if ((_i = _map.find("acceptRecovery")) != _map.end()) {
        acceptRecovery = _i->second;
    }
    if ((_i = _map.find("mongoHost")) != _map.end()) {
        mongoHost = (_i->second).getString();
    }
    if ((_i = _map.find("mongoPort")) != _map.end()) {
        mongoPort = (_i->second).getString();
    }
    if ((_i = _map.find("mongoDbName")) != _map.end()) {
        mongoDbName = (_i->second).getString();
    }
    if ((_i = _map.find("mongoCollection")) != _map.end()) {
        mongoCollection = (_i->second).getString();
    }
    if ((_i = _map.find("enqLogCleanerTimeInterval")) != _map.end()) {
        enqLogCleanerTimeInterval = _i->second;
    }
    if ((_i = _map.find("deqLogCleanerTimeInterval")) != _map.end()) {
        deqLogCleanerTimeInterval = _i->second;
    }
    if ((_i = _map.find("enqLogCleanerWarningSize")) != _map.end()) {
        enqLogCleanerWarningSize = _i->second;
    }
    if ((_i = _map.find("deqLogCleanerWarningSize")) != _map.end()) {
        deqLogCleanerWarningSize = _i->second;
    }

}

void StorageProvider::doMethod (string&, const ::qpid::types::Variant::Map&, ::qpid::types::Variant::Map& outMap, const string&)
{
    Manageable::status_t status = Manageable::STATUS_UNKNOWN_METHOD;
    std::string          text;


    outMap["_status_code"] = (uint32_t) status;
    outMap["_status_text"] = Manageable::StatusText(status, text);
}
