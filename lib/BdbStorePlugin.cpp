/*
 Copyright (c) 2007, 2008 Red Hat, Inc.

 This file is part of the Qpid async store library msgstore.so.

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
 USA

 The GNU Lesser General Public License is available in the file COPYING.
 */

#include "qpid/broker/Broker.h"
#include "qpid/Plugin.h"
#include "qpid/Options.h"
#include "qpid/DataDir.h"
#include "BdbMessageStoreImpl.h"


namespace qpid {
namespace broker {

using namespace std;

/**
*	Struct extending QPID Plugin used to declare the bdbmsgstore as QPID Broker Plugin.
**/
struct BdbStorePlugin : public Plugin {

    /**
    *	Store plugin options
    **/
    mrg::msgstore::BdbMessageStoreImpl::StoreOptions options;
    
    /**
    *	Method to obtain the plugin options.
    *	@return	Pointer to the plugin options.
    **/
    Options* getOptions() { return &options; }

    /**
    *	This method is called by the broker before starting and check the options and initialize the plugin.
    *	@param	target	A reference to a QPID plugin Target
    **/
    void earlyInitialize (Plugin::Target& target)
    {
        Broker* broker = dynamic_cast<Broker*>(&target);
        if (!broker) return;
        boost::shared_ptr<qpid::broker::MessageStore> store(new mrg::msgstore::BdbMessageStoreImpl (broker->getTimer()));
        DataDir& dataDir = broker->getDataDir ();
        if (options.storeDir.empty ())
        {
            if (!dataDir.isEnabled ())
                throw Exception ("bdbmsgstore: If --data-dir is blank or --no-data-dir is specified, --store-dir must be present.");

            options.storeDir = dataDir.getPath ();
        }
        MessageStore* sp = store.get();
        static_cast<mrg::msgstore::BdbMessageStoreImpl*>(sp)->init(&options);
        broker->setStore (store);
        target.addFinalizer(boost::bind(&BdbStorePlugin::finalize, this));
        static_cast<mrg::msgstore::BdbMessageStoreImpl*>(sp)->initManagement(broker);
    }

    /**
    *	This method is intentionally left blank
    **/
    void initialize(Plugin::Target&)
    {
        // This function intentionally left blank
    }
    /**
    *	This method is intentionally left blank
    **/
    void finalize()
    {
        // This function intentionally left blank
    }
    /**
    *	The method returns the identifier of the plugin.
    *	@return	Char array containing the string "BdbStorePlugin"
    **/
    const char* id() {return "BdbStorePlugin";}
};

/**
*	Static instance of the BdbStorePlugin
**/
static BdbStorePlugin instance; // Static initialization.

}} // namespace qpid::broker
