
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
#include "EventCreated.h"

using namespace qmf::com::redhat::rhm::store;
using           qpid::management::ManagementAgent;
using           qpid::management::Manageable;
using           qpid::management::ManagementObject;
using           qpid::management::Args;
using           qpid::management::Mutex;
using           std::string;

string  EventCreated::packageName  = string ("com.redhat.rhm.store");
string  EventCreated::eventName    = string ("created");
uint8_t EventCreated::md5Sum[16]   =
    {0x32,0x0,0x49,0x34,0xc8,0xdd,0xdd,0x2f,0x3c,0x3,0xd2,0x59,0x56,0x3c,0x80,0x97};

EventCreated::EventCreated (const std::string& _jrnlId) :
    jrnlId(_jrnlId)
{}

namespace {
    const string NAME("name");
    const string TYPE("type");
    const string DESC("desc");
    const string ARGCOUNT("argCount");
    const string ARGS("args");
}

void EventCreated::registerSelf(ManagementAgent* agent)
{
    agent->registerEvent(packageName, eventName, md5Sum, writeSchema);
}

void EventCreated::writeSchema (std::string& schema)
{
    const int _bufSize = 65536;
    char _msgChars[_bufSize];
    ::qpid::management::Buffer buf(_msgChars, _bufSize);
    ::qpid::types::Variant::Map ft;

    // Schema class header:
    buf.putOctet       (CLASS_KIND_EVENT);
    buf.putShortString (packageName); // Package Name
    buf.putShortString (eventName);   // Event Name
    buf.putBin128      (md5Sum);      // Schema Hash
    buf.putShort       (1); // Argument Count

    // Arguments
    ft.clear();
    ft[NAME] = "jrnlId";
    ft[TYPE] = TYPE_SSTR;
    ft[DESC] = "Journal Id";
    buf.putMap(ft);


    {
        uint32_t _len = buf.getPosition();
        buf.reset();
        buf.getRawData(schema, _len);
    }
}

void EventCreated::encode(std::string& _sBuf) const
{
    const int _bufSize=65536;
    char _msgChars[_bufSize];
    ::qpid::management::Buffer buf(_msgChars, _bufSize);

    buf.putShortString(jrnlId);


    uint32_t _bufLen = buf.getPosition();
    buf.reset();

    buf.getRawData(_sBuf, _bufLen);
}

void EventCreated::mapEncode(::qpid::types::Variant::Map& map) const
{
    using namespace ::qpid::types;
    map["jrnlId"] = ::qpid::types::Variant(jrnlId);

}
