
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
#include "Journal.h"

#include <iostream>
#include <sstream>

using namespace qmf::com::redhat::rhm::store;
using           qpid::management::ManagementAgent;
using           qpid::management::Manageable;
using           qpid::management::ManagementObject;
using           qpid::management::Args;
using           qpid::management::Mutex;
using           std::string;

string  Journal::packageName  = string ("com.redhat.rhm.store");
string  Journal::className    = string ("journal");
uint8_t Journal::md5Sum[MD5_LEN]   =
    {0xd5,0xff,0xe3,0xd6,0xad,0x46,0x4,0x9b,0x1e,0xdd,0x1e,0x3,0xd2,0x73,0x85,0x98};

Journal::Journal (ManagementAgent*, Manageable* _core) :
    ManagementObject(_core)
{
    
    queueRef = ::qpid::management::ObjectId();
    name = "";
    directory = "";
    bdbDir = "";



}

Journal::~Journal ()
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

void Journal::registerSelf(ManagementAgent* agent)
{
    agent->registerClass(packageName, className, md5Sum, writeSchema);
}

void Journal::writeSchema (std::string& schema)
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
    ft[NAME] = "queueRef";
    ft[TYPE] = TYPE_REF;
    ft[ACCESS] = ACCESS_RO;
    ft[IS_INDEX] = 0;
    ft[IS_OPTIONAL] = 0;
    buf.putMap(ft);

    ft.clear();
    ft[NAME] = "name";
    ft[TYPE] = TYPE_SSTR;
    ft[ACCESS] = ACCESS_RO;
    ft[IS_INDEX] = 1;
    ft[IS_OPTIONAL] = 0;
    buf.putMap(ft);

    ft.clear();
    ft[NAME] = "directory";
    ft[TYPE] = TYPE_SSTR;
    ft[ACCESS] = ACCESS_RO;
    ft[IS_INDEX] = 0;
    ft[IS_OPTIONAL] = 0;
    ft[DESC] = "Directory containing journal files";
    buf.putMap(ft);

    ft.clear();
    ft[NAME] = "bdbDir";
    ft[TYPE] = TYPE_SSTR;
    ft[ACCESS] = ACCESS_RO;
    ft[IS_INDEX] = 0;
    ft[IS_OPTIONAL] = 0;
    ft[DESC] = "Berkeley DB directory";
    buf.putMap(ft);


    // Statistics

    // Methods

    {
        uint32_t _len = buf.getPosition();
        buf.reset();
        buf.getRawData(schema, _len);
    }
}




uint32_t Journal::writePropertiesSize() const
{
    uint32_t size = writeTimestampsSize();

    size += 16;  // queueRef
    size += (1 + name.length());  // name
    size += (1 + directory.length());  // directory
    size += (1 + bdbDir.length());  // bdbDir

    return size;
}

void Journal::readProperties (const std::string& _sBuf)
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


    {std::string _s; buf.getRawData(_s, queueRef.encodedSize()); queueRef.decode(_s);};
    buf.getShortString(name);
    buf.getShortString(directory);
    buf.getShortString(bdbDir);


    delete [] _tmpBuf;
}

void Journal::writeProperties (std::string& _sBuf) const
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



    {std::string _s; queueRef.encode(_s); buf.putRawData(_s);};
    buf.putShortString(name);
    buf.putShortString(directory);
    buf.putShortString(bdbDir);


    uint32_t _bufLen = buf.getPosition();
    buf.reset();

    buf.getRawData(_sBuf, _bufLen);
}

void Journal::writeStatistics (std::string& _sBuf, bool skipHeaders)
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

void Journal::doMethod (string&, const string&, string& outStr)
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

std::string Journal::getKey() const
{
    std::stringstream key;

    key << name;
    return key.str();
}



void Journal::mapEncodeValues (::qpid::types::Variant::Map& _map,
                                              bool includeProperties,
                                              bool includeStatistics)
{
    using namespace ::qpid::types;
    Mutex::ScopedLock mutex(accessLock);

    if (includeProperties) {
        configChanged = false;
    _map["queueRef"] = ::qpid::types::Variant(queueRef);
    _map["name"] = ::qpid::types::Variant(name);
    _map["directory"] = ::qpid::types::Variant(directory);
    _map["bdbDir"] = ::qpid::types::Variant(bdbDir);

    }

    if (includeStatistics) {
        instChanged = false;






    // Maintenance of hi-lo statistics


    }
}

void Journal::mapDecodeValues (const ::qpid::types::Variant::Map& _map)
{
    ::qpid::types::Variant::Map::const_iterator _i;
    Mutex::ScopedLock mutex(accessLock);

    if ((_i = _map.find("queueRef")) != _map.end()) {
        queueRef = _i->second;
    }
    if ((_i = _map.find("name")) != _map.end()) {
        name = (_i->second).getString();
    }
    if ((_i = _map.find("directory")) != _map.end()) {
        directory = (_i->second).getString();
    }
    if ((_i = _map.find("bdbDir")) != _map.end()) {
        bdbDir = (_i->second).getString();
    }

}

void Journal::doMethod (string&, const ::qpid::types::Variant::Map&, ::qpid::types::Variant::Map& outMap)
{
    Manageable::status_t status = Manageable::STATUS_UNKNOWN_METHOD;
    std::string          text;


    outMap["_status_code"] = (uint32_t) status;
    outMap["_status_text"] = Manageable::StatusText(status, text);
}
