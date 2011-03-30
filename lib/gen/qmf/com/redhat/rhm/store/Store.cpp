
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
#include "Store.h"

#include <iostream>
#include <sstream>

using namespace qmf::com::redhat::rhm::store;
using           qpid::management::ManagementAgent;
using           qpid::management::Manageable;
using           qpid::management::ManagementObject;
using           qpid::management::Args;
using           qpid::management::Mutex;
using           std::string;

string  Store::packageName  = string ("com.redhat.rhm.store");
string  Store::className    = string ("store");
uint8_t Store::md5Sum[MD5_LEN]   =
    {0xd2,0xb5,0xf8,0x2,0xc9,0xeb,0x1a,0xfe,0x53,0x29,0x7b,0x32,0xff,0x35,0x81,0xcd};

Store::Store (ManagementAgent*, Manageable* _core, ::qpid::management::Manageable* _parent) :
    ManagementObject(_core)
{
    brokerRef = _parent->GetManagementObject ()->getObjectId ();
    location = "";
    tplIsInitialized = 0;
    tplDirectory = "";



}

Store::~Store ()
{

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

void Store::registerSelf(ManagementAgent* agent)
{
    agent->registerClass(packageName, className, md5Sum, writeSchema);
}

void Store::writeSchema (std::string& schema)
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
    buf.putShort       (4); // Config Element Count
    buf.putShort       (0); // Inst Element Count
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
    ft[NAME] = "tplIsInitialized";
    ft[TYPE] = TYPE_BOOL;
    ft[ACCESS] = ACCESS_RO;
    ft[IS_INDEX] = 0;
    ft[IS_OPTIONAL] = 0;
    ft[DESC] = "Transaction prepared list has been initialized by a transactional prepare";
    buf.putMap(ft);

    ft.clear();
    ft[NAME] = "tplDirectory";
    ft[TYPE] = TYPE_SSTR;
    ft[ACCESS] = ACCESS_RO;
    ft[IS_INDEX] = 0;
    ft[IS_OPTIONAL] = 0;
    ft[DESC] = "Transaction prepared list directory";
    buf.putMap(ft);


    // Statistics

    // Methods

    {
        uint32_t _len = buf.getPosition();
        buf.reset();
        buf.getRawData(schema, _len);
    }
}




uint32_t Store::writePropertiesSize() const
{
    uint32_t size = writeTimestampsSize();

    size += 16;  // brokerRef
    size += (1 + location.length());  // location
    size += 1;  // tplIsInitialized
    size += (1 + tplDirectory.length());  // tplDirectory

    return size;
}

void Store::readProperties (const std::string& _sBuf)
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
    tplIsInitialized = buf.getOctet()==1;
    buf.getShortString(tplDirectory);


    delete [] _tmpBuf;
}

void Store::writeProperties (std::string& _sBuf) const
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
    buf.putOctet(tplIsInitialized?1:0);
    buf.putShortString(tplDirectory);


    uint32_t _bufLen = buf.getPosition();
    buf.reset();

    buf.getRawData(_sBuf, _bufLen);
}

void Store::writeStatistics (std::string& _sBuf, bool skipHeaders)
{
    const int _bufSize=65536;
    char _msgChars[_bufSize];
    ::qpid::management::Buffer buf(_msgChars, _bufSize);

    Mutex::ScopedLock mutex(accessLock);
    instChanged = false;



    if (!skipHeaders) {
        std::string _tbuf;
        writeTimestamps (_tbuf);
        buf.putRawData(_tbuf);
    }



    // Maintenance of hi-lo statistics



    uint32_t _bufLen = buf.getPosition();
    buf.reset();

    buf.getRawData(_sBuf, _bufLen);
}

void Store::doMethod (string&, const string&, string& outStr)
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

std::string Store::getKey() const
{
    std::stringstream key;

    key << brokerRef.getV2Key();
    return key.str();
}



void Store::mapEncodeValues (::qpid::types::Variant::Map& _map,
                                              bool includeProperties,
                                              bool includeStatistics)
{
    using namespace ::qpid::types;
    Mutex::ScopedLock mutex(accessLock);

    if (includeProperties) {
        configChanged = false;
    _map["brokerRef"] = ::qpid::types::Variant(brokerRef);
    _map["location"] = ::qpid::types::Variant(location);
    _map["tplIsInitialized"] = ::qpid::types::Variant(tplIsInitialized);
    _map["tplDirectory"] = ::qpid::types::Variant(tplDirectory);

    }

    if (includeStatistics) {
        instChanged = false;






    // Maintenance of hi-lo statistics


    }
}

void Store::mapDecodeValues (const ::qpid::types::Variant::Map& _map)
{
    ::qpid::types::Variant::Map::const_iterator _i;
    Mutex::ScopedLock mutex(accessLock);

    if ((_i = _map.find("brokerRef")) != _map.end()) {
        brokerRef = _i->second;
    }
    if ((_i = _map.find("location")) != _map.end()) {
        location = (_i->second).getString();
    }
    if ((_i = _map.find("tplIsInitialized")) != _map.end()) {
        tplIsInitialized = _i->second;
    }
    if ((_i = _map.find("tplDirectory")) != _map.end()) {
        tplDirectory = (_i->second).getString();
    }

}

void Store::doMethod (string&, const ::qpid::types::Variant::Map&, ::qpid::types::Variant::Map& outMap)
{
    Manageable::status_t status = Manageable::STATUS_UNKNOWN_METHOD;
    std::string          text;


    outMap["_status_code"] = (uint32_t) status;
    outMap["_status_text"] = Manageable::StatusText(status, text);
}
