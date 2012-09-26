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

#include <sstream>
#include <cassert>
#include <boost/make_shared.hpp>

#include "cassandra_storage.hpp"
#include "null_handle.hpp"

using std::string;
using std::time_t;
using boost::shared_ptr;

namespace rendermq
{

namespace
{

tile_storage* create_cassandra_storage(boost::property_tree::ptree const& pt,
                                       boost::optional<zmq::context_t &> ctx)
{
   std::string server = pt.get<std::string>("server", "127.0.0.1");
   int port = pt.get<int>("port", 9160);
   std::string keyspace_prefix = pt.get<std::string>("keyspace_prefix", "tiles_");
   return new cassandra_storage(server, port, keyspace_prefix);
}

const bool registered = register_tile_storage("cassandra", create_cassandra_storage);

} // anonymous namespace

cassandra_storage::handle::handle(const std::pair<metatile_reader::iterator_type, metatile_reader::iterator_type>& p)
   : tile_data(p.first, p.second)
{
}

cassandra_storage::handle::~handle()
{
}

bool
cassandra_storage::handle::exists() const
{
   return true;
}

std::time_t
cassandra_storage::handle::last_modified() const
{
   return 0;
}

bool
cassandra_storage::handle::expired() const
{
   return false;
}

bool
cassandra_storage::handle::data(string& output) const
{
   output.assign(tile_data);
   return true;
}

cassandra_storage::cassandra_storage(const std::string& server, int port, const std::string& ksp) : keyspace_prefix(ksp)
{
   boost::shared_ptr< ::apache::thrift::transport::TTransport > socket = boost::shared_ptr< ::apache::thrift::transport::TSocket >(new ::apache::thrift::transport::TSocket(server, port));
   transport = boost::shared_ptr< ::apache::thrift::transport::TFramedTransport >(new ::apache::thrift::transport::TFramedTransport(socket)); 
   protocol = boost::shared_ptr< ::apache::thrift::protocol::TBinaryProtocol >(new ::apache::thrift::protocol::TBinaryProtocol(transport));
   transport->open();
}

cassandra_storage::~cassandra_storage()
{
   transport->close();
}

shared_ptr<tile_storage::handle>
cassandra_storage::get(const tile_protocol &tile) const
{
   std::cerr << "cassandra_storage::get style=" << tile.style << " z=" << tile.z << " x=" << tile.x << " y=" << tile.y << "\n";

   std::string data;
   if (!get_meta(tile, data))
   {
      std::cerr << "  not found\n";
      return shared_ptr<tile_storage::handle>(new null_handle());
   }

   metatile_reader reader(data, tile.format);
   std::pair<metatile_reader::iterator_type, metatile_reader::iterator_type> tile_data = reader.get(tile.x, tile.y);
   if (tile_data.first == tile_data.second)
   {
      std::cerr << "  metatile format corrupt\n";
      return shared_ptr<tile_storage::handle>(new null_handle());
   }
   std::cerr << "  ok\n";
   return boost::make_shared<handle>(tile_data);
}

/* Create a string from the tile data that is used as the Cassandra "keyspace".
 */
std::string cassandra_storage::get_keyspace(const tile_protocol &tile) const
{
   std::ostringstream keyspace;
   keyspace << keyspace_prefix << tile.style;
   return keyspace.str();
}

/* Create a string from the tile data that is used as the Cassandra "column family".
 */
std::string cassandra_storage::get_column_family(const tile_protocol &tile) const
{
   return "tiles";
}

/* Create a string from the tile data that can be used as key for lookup in Cassandra.
 * The string will look very similar to the usual file path/URL for tiles. But there is
 * an important difference: Because we store metatiles, the key contains the coordinates
 * of the first tile in the metatile.
 */
std::string cassandra_storage::get_key(const tile_protocol &tile) const
{
   std::pair<int, int> coordinates = xy_to_meta_xy(tile.x, tile.y);
   std::ostringstream key;
   key << "/" << tile.z << "/" << coordinates.first << "/" << coordinates.second << "." << file_type_for(tile.format);
   return key.str();
}

bool cassandra_storage::get_meta(const tile_protocol &tile, std::string &data) const
{
   try {
      std::cerr << "cassandra_storage::get_meta style=" << tile.style << " z=" << tile.z << " x=" << tile.x << " y=" << tile.y << "\n";

      ::org::apache::cassandra::CassandraClient client(protocol);
      client.set_keyspace(get_keyspace(tile));

      ::org::apache::cassandra::ColumnPath column_path;
      column_path.column = "tile";
      column_path.__isset.column = true;
      column_path.column_family = get_column_family(tile);
      column_path.super_column = "";

      ::org::apache::cassandra::ColumnOrSuperColumn result;
      std::string key = get_key(tile);
      client.get(result, key, column_path, ::org::apache::cassandra::ConsistencyLevel::ONE);
      data = result.column.value;
      return true;

   } catch (::apache::thrift::transport::TTransportException& te) {
      printf("Exception: %s  [%d]\n", te.what(), te.getType());
      return false;
   } catch (::org::apache::cassandra::InvalidRequestException& ire) {
      printf("Exception: %s  [%s]\n", ire.what(), ire.why.c_str());
      return false;
   } catch (::org::apache::cassandra::NotFoundException& nfe) {
      printf("Exception: %s\n", nfe.what());
      return false;
   }
}


/**
 * Get current timestamp in milliseconds. This is needed when writing data to
 * Cassandra.
 */
int64_t get_timestamp_in_milliseconds()
{
   struct timeval td;
   gettimeofday(&td, NULL);
   int64_t ms = td.tv_sec;
   ms = ms * 1000;
   int64_t usec = td.tv_usec;
   usec = usec / 1000;
   ms += usec;
   return ms;
}

bool cassandra_storage::put_meta(const tile_protocol &tile, const std::string &buf) const
{
   try {
      std::cerr << "cassandra_storage::put_meta style=" << tile.style << " z=" << tile.z << " x=" << tile.x << " y=" << tile.y << "\n";

      ::org::apache::cassandra::CassandraClient client(protocol);
      client.set_keyspace(get_keyspace(tile));

      ::org::apache::cassandra::ColumnParent cparent;
      cparent.column_family = get_column_family(tile);

      ::org::apache::cassandra::Column column;
      column.name = "tile";
      column.value = buf;
      column.__isset.value = true;
      column.timestamp = get_timestamp_in_milliseconds();
      column.__isset.timestamp = true;

      std::string key = get_key(tile);
      client.insert(key, cparent, column, ::org::apache::cassandra::ConsistencyLevel::ONE);

      return true;
   } catch (::apache::thrift::transport::TTransportException& te) {
      printf("Exception: %s  [%d]\n", te.what(), te.getType());
      return false;
   } catch (::org::apache::cassandra::InvalidRequestException& ire) {
      printf("Exception: %s  [%s]\n", ire.what(), ire.why.c_str());
      return false;
   } catch (::org::apache::cassandra::NotFoundException& nfe) {
      printf("Exception: %s\n", nfe.what());
      return false;
   }

}

/*
 * Marking a tile as expired is a no-op.
 */
bool cassandra_storage::expire(const tile_protocol &tile) const
{
   return true;
}

}

