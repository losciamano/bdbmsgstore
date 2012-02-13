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

#ifndef QPID_STORE_STOREEXCEPTION_H
#define QPID_STORE_STOREEXCEPTION_H

#include <exception>
#include <boost/format.hpp>
#include "StorageProvider.h"

namespace qpid {
namespace store {

class StoreException : public std::exception
{
    std::string text;
public:
    StoreException(const std::string& _text) : text(_text) {}
    StoreException(const std::string& _text, const std::exception& cause)
    	: text(_text + ": "+cause.what()){}
    StoreException(const std::string& _text, const std::string& cause)
    	: text(_text+ ": "+cause) {}
    StoreException(const std::string& _text,
                   const StorageProvider::Exception& cause)
        : text(_text + ": " + cause.what()) {}
    virtual ~StoreException() throw() {}
    virtual const char* what() const throw() { return text.c_str(); }
};

#define THROW_STORE_EXCEPTION(MESSAGE) throw qpid::store::StoreException(boost::str(boost::format("%s (%s:%d)") % (MESSAGE) % __FILE__ % __LINE__))
#define THROW_STORE_EXCEPTION_2(MESSAGE, EXCEPTION) throw qpid::store::StoreException(boost::str(boost::format("%s (%s:%d)") % (MESSAGE) % __FILE__ % __LINE__), EXCEPTION)

}}  // namespace qpid::store

#endif /* QPID_STORE_STOREEXCEPTION_H */
