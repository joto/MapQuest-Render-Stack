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

#ifndef RENDERMQ_MEMCACHED_STORAGE_HPP
#define RENDERMQ_MEMCACHED_STORAGE_HPP

#include <string>
#include <ctime>
#include "tile_storage.hpp"

class memcached_st;

namespace rendermq
{

/*
 *  Metatile storage in memcached.
 *
 *  Metatiles are stored in memcached in the same format used elsewhere.
 *
 */
class memcached_storage : public tile_storage
{
public:
   class handle : public tile_storage::handle
   {
   public:
      handle(const std::pair<metatile_reader::iterator_type, metatile_reader::iterator_type>& p) : tile_data(p.first, p.second) {}
      virtual ~handle() {}
      virtual bool exists() const { return true; }
      virtual std::time_t last_modified() const { return 0; }
      virtual bool data(std::string &) const;
      virtual bool expired() const { return false; }
   private:
      std::string tile_data;
   };
   friend class handle;

   memcached_storage(const std::string& options, int expire_in_minutes);
   virtual ~memcached_storage();

   boost::shared_ptr<tile_storage::handle> get(const tile_protocol &tile) const;
   bool get_meta(const tile_protocol &tile, std::string &data) const;
   bool put_meta(const tile_protocol &tile, const std::string &buf) const;
   bool expire(const tile_protocol &tile) const;
private:
   int expire_in_seconds;
   memcached_st* memcache;

   std::string key_string(const tile_protocol &tile) const;
};

}

#endif // RENDERMQ_MEMCACHED_STORAGE_HPP
