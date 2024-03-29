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

#ifndef _Cursor_
#define _Cursor_

#include <boost/shared_ptr.hpp>
#include "db-inc.h"
#include "qpid/log/Statement.h"

namespace mrg{
namespace msgstore{

class Cursor
{
    Dbc* cursor;
public:
    typedef boost::shared_ptr<Db> db_ptr;

    Cursor() : cursor(0) {}
    virtual ~Cursor() { if(cursor) cursor->close(); }

    void open(db_ptr db, DbTxn* txn, u_int32_t flags = 0) { db->cursor(txn, &cursor, flags); }
    void close() { if(cursor) cursor->close(); cursor = 0; }
    Dbc* get() { return cursor; }
    Dbc* operator->() { return cursor; }
    bool next(Dbt& key, Dbt& value) 
    { 
    	int res=cursor->get(&key, &value, DB_NEXT); 
	if (res!=0 && res!=DB_NOTFOUND) 
	{
		std::stringstream ss;
		ss<<"CURSOR => "<<res;
		QPID_LOG(error,ss.str());
	}
	return res==0;
    }
    bool current(Dbt& key, Dbt& value) {  return cursor->get(&key, &value, DB_CURRENT) == 0; }
};

}}

#endif
