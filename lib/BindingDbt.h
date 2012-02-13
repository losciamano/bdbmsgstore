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

#ifndef _BindingDbt_
#define _BindingDbt_

#include "db-inc.h"
#include <qpid/broker/PersistableExchange.h>
#include <qpid/broker/PersistableQueue.h>
#include <qpid/framing/Buffer.h>
#include <qpid/framing/FieldTable.h>

namespace mrg{
namespace msgstore{

class BindingDbt : public Dbt
{
    char* data;
    qpid::framing::Buffer buffer;

    static uint32_t encodedSize(const qpid::broker::PersistableExchange& e,
                                const qpid::broker::PersistableQueue& q,
                                const std::string& k,
                                const qpid::framing::FieldTable& a);

public:
    BindingDbt(const qpid::broker::PersistableExchange& e,
               const qpid::broker::PersistableQueue& q,
               const std::string& k,
               const qpid::framing::FieldTable& a);

    virtual ~BindingDbt();

};

}}

#endif
