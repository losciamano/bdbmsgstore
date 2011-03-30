
#ifndef _MANAGEMENT_STORE_
#define _MANAGEMENT_STORE_

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
namespace redhat {
namespace rhm {
namespace store {


class Store : public ::qpid::management::ManagementObject
{
  private:

    static std::string packageName;
    static std::string className;
    static uint8_t     md5Sum[MD5_LEN];


    // Properties
    ::qpid::management::ObjectId brokerRef;
    std::string location;
    bool tplIsInitialized;
    std::string tplDirectory;

    // Statistics


  public:
    static void writeSchema(std::string& schema);
    void mapEncodeValues(::qpid::types::Variant::Map& map,
                         bool includeProperties=true,
                         bool includeStatistics=true);
    void mapDecodeValues(const ::qpid::types::Variant::Map& map);
    void doMethod(std::string&           methodName,
                  const ::qpid::types::Variant::Map& inMap,
                  ::qpid::types::Variant::Map& outMap);
    std::string getKey() const;

    uint32_t writePropertiesSize() const;
    void readProperties(const std::string& buf);
    void writeProperties(std::string& buf) const;
    void writeStatistics(std::string& buf, bool skipHeaders = false);
    void doMethod(std::string& methodName,
                  const std::string& inBuf,
                  std::string& outBuf);


    writeSchemaCall_t getWriteSchemaCall() { return writeSchema; }

    // Stub for getInstChanged.  There are no statistics in this class.
    bool getInstChanged() { return false; }
    bool hasInst() { return false; }


    Store(::qpid::management::ManagementAgent* agent,
                            ::qpid::management::Manageable* coreObject, ::qpid::management::Manageable* _parent);
    ~Store();

    

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
    inline void set_tplIsInitialized (bool val) {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        tplIsInitialized = val;
        configChanged = true;
    }
    inline bool get_tplIsInitialized() {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        return tplIsInitialized;
    }
    inline void set_tplDirectory (const std::string& val) {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        tplDirectory = val;
        configChanged = true;
    }
    inline const std::string& get_tplDirectory() {
        ::qpid::management::Mutex::ScopedLock mutex(accessLock);
        return tplDirectory;
    }

};

}}}}}

#endif  /*!_MANAGEMENT_STORE_*/
