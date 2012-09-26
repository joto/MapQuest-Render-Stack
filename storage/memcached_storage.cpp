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

#include <libmemcached/memcached.h>

#include "logging/logger.hpp"
#include "null_handle.hpp"
#include "memcached_storage.hpp"

using std::string;
using std::time_t;
using boost::shared_ptr;

namespace rendermq
{

namespace
{

tile_storage * create_memcached_storage(boost::property_tree::ptree const& pt,
                                        boost::optional<zmq::context_t &> ctx)
{
   std::string options = pt.get<std::string>("options", "--SERVER=localhost");
   int expire_in_minutes = pt.get<int>("expire", 0);

   /* Memcached interprets an expire time of more than 30 days as an absolute
    * time stamp, we don't want this so we set this to 0 to mean "no expiry".
    * In this case tiles are only expired when there is not enough memory (LRU). */
   if (expire_in_minutes < 0 || expire_in_minutes > 30 * 24 * 60) {
      expire_in_minutes = 0;
   }

   return new memcached_storage(options, expire_in_minutes);
}

const bool registered = register_tile_storage("memcached", create_memcached_storage);

} // anonymous namespace

bool
memcached_storage::handle::data(string& output) const
{
   output.assign(tile_data);
   return true;
}

memcached_storage::memcached_storage(const std::string& options, int expire_in_minutes) :
   expire_in_seconds(expire_in_minutes * 60),
   memcache(memcached(options.c_str(), options.size()))
{
   LOG_INFO(boost::format("Initializing memcached storage with expire=[%1% minutes], options=[%2%].") % expire_in_minutes % options);
   if (!memcache) {
      LOG_ERROR("Can not initialize memcached storage. (This might be due to a missing or wrong 'options' setting in the config file.)");
      throw std::runtime_error("Failed to create memcached storage.");
   }
}

memcached_storage::~memcached_storage()
{
   if (memcache) {
      memcached_free(memcache);
   }
}

shared_ptr<tile_storage::handle>
memcached_storage::get(const tile_protocol &tile) const
{
   LOG_DEBUG(boost::format("memcached_storage::get style=%1% z=%2% x=%3% y=%4%") % tile.style % tile.z % tile.x % tile.y);

   std::string data;
   if (!get_meta(tile, data))
   {
      LOG_DEBUG("memcached_storage::get tile not found");
      return shared_ptr<tile_storage::handle>(new null_handle());
   }

   metatile_reader reader(data, tile.format);
   std::pair<metatile_reader::iterator_type, metatile_reader::iterator_type> tile_data = reader.get(tile.x, tile.y);
   if (tile_data.first == tile_data.second)
   {
      std::cerr << "  metatile format corrupt\n";
      LOG_ERROR(boost::format("Metatile corrupt (style=%1% z=%2% x=%3% y=%4%).") % tile.style % tile.z % tile.x % tile.y);
      return shared_ptr<tile_storage::handle>(new null_handle());
   }

   LOG_DEBUG("memcached_storage::get tile found");
   return boost::make_shared<handle>(tile_data);
}

/* Create a string from the tile data that can be used as key for lookup in the memcache.
 * The string will look very similar to the usual file path/URL for tiles. But there is
 * an important difference: Because we store metatiles, the key contains the coordinates
 * of the first tile in the metatile.
 */
std::string memcached_storage::key_string(const tile_protocol &tile) const
{
   std::pair<int, int> coordinates = xy_to_meta_xy(tile.x, tile.y);
   std::ostringstream key;
   key << "/" << tile.style << "/" << tile.z << "/" << coordinates.first << "/" << coordinates.second << "." << file_type_for(tile.format);
   return key.str();
}

bool memcached_storage::get_meta(const tile_protocol &tile, std::string &data) const
{
   LOG_DEBUG(boost::format("memcached_storage::get_meta style=%1% z=%2% x=%3% y=%4%") % tile.style % tile.z % tile.x % tile.y);
   std::string key = key_string(tile);
   size_t value_length;
   uint32_t flags;
   memcached_return_t error;
   char* value = memcached_get(memcache, key.c_str(), key.size(), &value_length, &flags, &error);
   if (value == NULL)
   {
      return false;
   }
   data.append(value, value_length);
   free(value);
   return true;
}

bool memcached_storage::put_meta(const tile_protocol &tile, const std::string &buf) const
{
   LOG_DEBUG(boost::format("memcached_storage::put_meta style=%1% z=%2% x=%3% y=%4%") % tile.style % tile.z % tile.x % tile.y);
   std::string key = key_string(tile);
   memcached_return_t rc = memcached_set(memcache, key.c_str(), key.size(), buf.c_str(), buf.size(), expire_in_seconds, (uint32_t)0);
   if (rc != MEMCACHED_SUCCESS)
   {
      LOG_ERROR(boost::format("Can not store metatile in memcached (%1%).") % key);
      return false;
   }
   return true;
}

/*
 * A metatile is expired in memcached by deleting it.
 */
bool memcached_storage::expire(const tile_protocol &tile) const
{
   LOG_DEBUG(boost::format("memcached_storage::expire style=%1% z=%2% x=%3% y=%4%") % tile.style % tile.z % tile.x % tile.y);
   std::string key = key_string(tile);
   memcached_return_t rc = memcached_delete(memcache, key.c_str(), key.size(), 0);
   if (rc != MEMCACHED_SUCCESS)
   {
      return false;
   }
   return true;
}

}

