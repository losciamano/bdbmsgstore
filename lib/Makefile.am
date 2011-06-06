# Copyright (c) 2007, 2008, 2009 Red Hat, Inc.
#
# This file is part of the Qpid async store library msgstore.so.
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
# USA
#
# The GNU Lesser General Public License is available in the file COPYING.

AM_CXXFLAGS = $(WARNING_CFLAGS) $(APR_CXXFLAGS) $(QPID_CXXFLAGS) \
  -I$(srcdir)/gen -DRHM_CLEAN -pthread

# Definitions for client and daemon plugins
PLUGINLDFLAGS=-no-undefined -module -avoid-version
dmoduledir=$(libdir)/qpid/daemon
dmodule_LTLIBRARIES =libmessagestoreplugin.la bdbstoreprovider.la 
 
libmessagestoreplugin_la_LIBADD =            \
  $(APR_LIBS)                   \
  $(LIB_DLOPEN)                 \
  $(LIB_BERKELEY_DB)            \
  $(LIB_CLOCK_GETTIME)          \
  $(QPID_LIBS)

libmessagestoreplugin_la_LDFLAGS =           \
  $(PLUGINLDFLAGS)
  
libmessagestoreplugin_la_SOURCES =           \
	MessageStorePlugin.cpp	\
	StorageProvider.h	\
	MessageStorePlugin.h	\
	StoreException.h

bdbstoreprovider_la_DEPENDENCIES = \
  libmessagestoreplugin.la

bdbstoreprovider_la_LIBADD =            \
  $(APR_LIBS)                   \
  $(LIB_DLOPEN)                 \
  $(LIB_BERKELEY_DB)            \
  $(LIB_CLOCK_GETTIME)     	\
  libmessagestoreplugin.la		\
  $(QPID_LIBS)			

bdbstoreprovider_la_LDFLAGS =           \
  -L$(dmoduledir)			\
  $(PLUGINLDFLAGS)			

bdbstoreprovider_la_SOURCES =           \
  StorageProvider.h	\
  MessageStorePlugin.h	\
  StoreException.h	\
  BindingDbt.cpp                \
  BufferValue.cpp               \
  IdDbt.cpp                     \
  IdSequence.cpp                \
  JournalImpl.cpp               \
  TxnCtxt.cpp                   \
  BindingDbt.h                  \
  BufferValue.h                 \
  Cursor.h                      \
  IdDbt.h                       \
  IdSequence.h                  \
  JournalImpl.h                 \
  TxnCtxt.h                     \
  AsyncOperationLogger.cpp	\
  AsyncOperationLogger.h	\
  PidTracker.cpp		\
  PidTracker.h			\
  jrnl/jdir.cpp                 \
  jrnl/jerrno.cpp               \
  jrnl/jexception.cpp           \
  jrnl/slock.cpp                \
  jrnl/smutex.cpp               \
  jrnl/time_ns.cpp              \
  jrnl/enums.hpp                \
  jrnl/jcfg.hpp                 \
  jrnl/jdir.hpp                 \
  jrnl/jerrno.hpp               \
  jrnl/jexception.hpp           \
  jrnl/slock.hpp                \
  jrnl/smutex.hpp               \
  jrnl/time_ns.hpp              \
  gen/qmf/com/atono/server/qpid/bdbstore/StorageProvider.h \
  gen/qmf/com/atono/server/qpid/bdbstore/Journal.h \
  gen/qmf/com/atono/server/qpid/bdbstore/StorageProvider.cpp \
  gen/qmf/com/atono/server/qpid/bdbstore/Journal.cpp \
  gen/qmf/com/atono/server/qpid/bdbstore/EventEnqThresholdExceeded.h \
  gen/qmf/com/atono/server/qpid/bdbstore/EventCreated.h \
  gen/qmf/com/atono/server/qpid/bdbstore/EventFull.h \
  gen/qmf/com/atono/server/qpid/bdbstore/EventRecovered.h \
  gen/qmf/com/atono/server/qpid/bdbstore/EventEnqThresholdExceeded.cpp \
  gen/qmf/com/atono/server/qpid/bdbstore/EventCreated.cpp \
  gen/qmf/com/atono/server/qpid/bdbstore/EventFull.cpp \
  gen/qmf/com/atono/server/qpid/bdbstore/EventRecovered.cpp \
  gen/qmf/com/atono/server/qpid/bdbstore/Package.h \
  gen/qmf/com/atono/server/qpid/bdbstore/Package.cpp \
  BdbStoreProvider.cpp

BUILT_SOURCES = db-inc.h
db-inc.h: Makefile.in
	echo '#include <$(DB_CXX_HEADER_PREFIX)db_cxx.h>' > $@-t
	mv $@-t $@
