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

#include "BindingDbt.h"

using namespace mrg::msgstore;
using qpid::broker::PersistableExchange;
using qpid::broker::PersistableQueue;
using qpid::framing::FieldTable;
using std::string;


BindingDbt::BindingDbt(const PersistableExchange& e, const PersistableQueue& q, const string& k, const FieldTable& a)
  : data(new char[encodedSize(e, q, k, a)]),
    buffer(data, encodedSize(e, q, k, a))
{
    buffer.putLongLong(q.getPersistenceId());
    buffer.putShortString(q.getName());
    buffer.putShortString(k);
    buffer.put(a);

    set_data(data);
    set_size(encodedSize(e, q, k, a));
}

BindingDbt::~BindingDbt()
{
  delete [] data;
}

uint32_t BindingDbt::encodedSize(const PersistableExchange& /*not used*/, const PersistableQueue& q, const string& k, const FieldTable& a)
{
    return 8 /*queue id*/ + q.getName().size() + 1 + k.size() + 1 + a.encodedSize();
}