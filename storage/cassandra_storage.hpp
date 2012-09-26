/*------------------------------------------------------------------------------
 *
 *  This file is part of rendermq
 *
 *  Author: jochen@topf.org
 *
 *  Copyright 2012 Jochen Topf
 *
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 *-----------------------------------------------------------------------------*/

#ifndef RENDERMQ_CASSANDRA_STORAGE_HPP
#define RENDERMQ_CASSANDRA_STORAGE_HPP

#include <string>
#include <ctime>
#include "tile_storage.hpp"

#include <Thrift.h>
#include <transport/TSocket.h>
#include <transport/TTransport.h>
#include <transport/TBufferTransports.h>
#include <protocol/TProtocol.h>
#include <protocol/TBinaryProtocol.h>
#include "cassandra/Cassandra.h"

//using namespace org::apache::cassandra;

namespace rendermq
{

/*
 *  Metatile storage in cassandra.
 *
 *  Metatiles are stored in cassandra in the same format used elsewhere.
 *
 */
class cassandra_storage : public tile_storage
{
   std::string key_string(const tile_protocol &tile) const;
public:
   class handle : public tile_storage::handle
   {
   public:
      handle(const std::pair<metatile_reader::iterator_type, metatile_reader::iterator_type>&);
      virtual ~handle();
      virtual bool exists() const;
      virtual std::time_t last_modified() const;
      virtual bool data(std::string &) const;
      virtual bool expired() const;
   private:
      std::string tile_data;
   };
   friend class handle;

   cassandra_storage(const std::string& server, int port, const std::string& keyspace_prefix);
   virtual ~cassandra_storage();

   boost::shared_ptr<tile_storage::handle> get(const tile_protocol &tile) const;
   bool get_meta(const tile_protocol &tile, std::string &) const;
   bool put_meta(const tile_protocol &tile, const std::string &buf) const;
   bool expire(const tile_protocol &tile) const;
private:
   std::string keyspace_prefix;
   boost::shared_ptr< ::apache::thrift::transport::TTransport > transport;
   boost::shared_ptr< ::apache::thrift::protocol::TProtocol > protocol;

   std::string get_keyspace(const tile_protocol &tile) const;
   std::string get_column_family(const tile_protocol &tile) const;
   std::string get_key(const tile_protocol &tile) const;
};

}

#endif // RENDERMQ_CASSANDRA_STORAGE_HPP
