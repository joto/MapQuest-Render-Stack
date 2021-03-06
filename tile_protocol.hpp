/*------------------------------------------------------------------------------
 *
 *  This file is part of rendermq  
 *
 *  Author: artem@mapnik-consulting.com
 *
 *  Copyright 2010-1 Mapquest, Inc.  All Rights reserved.
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


#ifndef TILE_PROTOCOL_HPP
#define TILE_PROTOCOL_HPP

#include <ostream>
#include <zmq.hpp>
#include <boost/unordered_map.hpp>
#include <boost/foreach.hpp>
#include <cstring> // for memcpy
#include <ctime> // for std::time_t
#include "tile_utils.hpp"
#include "proto/tile.pb.h"
#include "storage/meta_tile.hpp" // for METATILE size
#include "logging/logger.hpp"
#include <iostream>
#include <vector>
#include <map>

namespace rendermq
{

enum protoCmd { 
   cmdIgnore,    
   cmdRender,     // render with normal priority
   cmdDirty,      // expire a tile (and submit for re-rendering?)
   cmdDone,       // worker says command completed successfully
   cmdNotDone, 
   cmdRenderPrio, // render with higher priority
   cmdRenderBulk, // render with lower priority, and don't expect a response.
   cmdStatus      // request the status of a tile
};

class tile_protocol
{

public:
   typedef std::map<std::string, std::string> parameters_t;

   tile_protocol()
      : status(cmdRenderPrio), x(0), y(0), z(0), id(0), style(""), parameters(), format(fmtPNG), last_modified(0), request_last_modified(0), priority(-1) {}
   tile_protocol(protoCmd status_,int x_,int y_, int z_, int64_t id_, const std::string & style_, protoFmt format_, std::time_t last_mod_=0, std::time_t req_last_mod_=0, uint32_t priority_=-1)
      : status(status_), x(x_), y(y_), z(z_), id(id_), style(style_), parameters(), format(format_), last_modified(last_mod_), request_last_modified(req_last_mod_), priority(priority_=-1) {}
   tile_protocol(tile_protocol const& other)
      : status(other.status), 
        x(other.x), y(other.y), 
        z(other.z), id(other.id),
        style(other.style),
        parameters(other.parameters),
        format(other.format),
        last_modified(other.last_modified),
        request_last_modified(other.request_last_modified),
        priority(other.priority),
        data_(other.data_)
      {}

   const tile_protocol& operator=(tile_protocol const& other) {
      status = other.status;
      x = other.x;
      y = other.y;
      z = other.z;
      id = other.id;
      style = other.style;
      parameters = other.parameters;
      format = other.format;
      last_modified = other.last_modified;
      request_last_modified = other.request_last_modified;
      priority = other.priority;
      data_ = other.data_;
      return *this;
   }

   std::string const& data() const
      {
         return data_;
      }
   void set_data(std::string const& data)
      {
         data_ = data;
      }

   // Return priority for this tile. If it was not set explicitly it is
   // derived from the status.
   int32_t get_priority() const {
      if (priority >= 0) {
         return priority;
      }

      if (status == cmdRenderBulk) return 0;
      else if (status == cmdDirty) return 50;
      else if (status == cmdRenderPrio) return 150;

      return 100;
   }

   protoCmd status;
   int x;
   int y;
   int z;
   int64_t id;
   std::string style;
   parameters_t parameters;
   protoFmt format;
   std::time_t last_modified;
   std::time_t request_last_modified;
   int32_t priority;

private:
   std::string data_;
}; // class tile_protocol

inline std::ostream& operator<< (std::ostream& out, tile_protocol const& t)
{
   out << "TILE PROTO " << t.z << ":" << t.x << ":" << t.y << " status=";

   if (t.status == cmdIgnore) { out << "cmdIgnore"; }
   else if (t.status == cmdRender) { out << "cmdRender"; }
   else if (t.status == cmdDirty) { out << "cmdDirty"; }
   else if (t.status == cmdDone) { out << "cmdDone"; }
   else if (t.status == cmdNotDone) { out << "cmdNotDone"; }
   else if (t.status == cmdRenderPrio) { out << "cmdRenderPrio"; }
   else if (t.status == cmdRenderBulk) { out << "cmdRenderBulk"; }
   else if (t.status == cmdStatus) { out << "cmdStatus"; }
   else { out << "[[unrecognised_command]]"; }

   { // output the format in a nice, human-readable way.
      out << " fmt=";
      std::vector<protoFmt> protoFmts = get_formats_vec(t.format);
      for(std::vector<protoFmt>::const_iterator protoFmt = protoFmts.begin(); protoFmt != protoFmts.end(); protoFmt++)
      {
         if(protoFmt != protoFmts.begin())
            out << ",";
         out << file_type_for(*protoFmt);
      }
   }

   if (t.last_modified > 0) { out << " last_modified=" << t.last_modified; }
   if (t.request_last_modified > 0) { out << " request_last_modified=" << t.request_last_modified; }

   out << " id=" << t.id << " style=" << t.style;
   if (!t.parameters.empty()) {
      out << " (parameters:";
      BOOST_FOREACH(tile_protocol::parameters_t::value_type p, t.parameters) {
         out << " " << p.first << "=" << p.second;
      }
      out << ")";
   }
   out << " priority=" << t.priority << " data.size()=" << t.data().size();
   return out;
}

inline bool operator==(tile_protocol const &a, tile_protocol const &b) {
   // note: we're missing out the status/priority, since that's expected to change.
   return 
      a.x == b.x && a.y == b.y && a.z == b.z && 
      a.id == b.id && a.style == b.style && a.parameters == b.parameters &&
      a.format == b.format;
}

inline bool operator!=(tile_protocol const &a, tile_protocol const &b) {
   return !operator==(a, b);
}

inline size_t hash_value(const tile_protocol &tp) {
   size_t seed = 0;
   // note: client id is deliberately excluded from this because we want
   // tiles for multiple clients to be hashed to the same value for 
   // distribution to and collapsing within the brokers. the same goes
   // for the format - the worker will do all the formats which are
   // available on that style anyway (e.g: no JPEG for transparent styles,
   // no JSON for non-clickable styles).
   boost::hash_combine(seed, tp.style);
   boost::hash_combine(seed, tp.z);
   boost::hash_combine(seed, tp.x & ~(METATILE - 1));
   boost::hash_combine(seed, tp.y & ~(METATILE - 1));
   return seed;
}

inline bool serialise(const tile_protocol &tile, std::string &buf) {
   proto::tile t;
   t.set_command(tile.status);
   t.set_x(tile.x);
   t.set_y(tile.y);
   t.set_z(tile.z);
   t.set_id(tile.id);
   t.set_image(tile.data());
   t.set_style(tile.style);
   t.set_format(tile.format);

   t.set_priority(tile.get_priority());
   if (tile.last_modified != 0) { t.set_last_modified(tile.last_modified); }
   if (tile.request_last_modified != 0) { t.set_request_last_modified(tile.request_last_modified); }

   BOOST_FOREACH(tile_protocol::parameters_t::value_type p, tile.parameters) {
      if (p.second != "") {
         proto::parameter* pp = t.add_parameters();
         pp->set_key(p.first);
         pp->set_value(p.second);
      }
   }

   return t.SerializeToString(&buf);
}

inline bool unserialise(const std::string &buf, tile_protocol &tile) {
   proto::tile t;
   bool result = t.ParseFromString(buf);
   if (result) {
      tile.status = static_cast<rendermq::protoCmd>(t.command());
      tile.x = t.x();
      tile.y = t.y();
      tile.z = t.z();
      tile.id = t.id();
      tile.set_data(t.image());
      tile.style = t.style();
      tile.format = static_cast<rendermq::protoFmt>(t.format());
      tile.last_modified = t.has_last_modified() ? t.last_modified() : 0;
      tile.request_last_modified = t.has_request_last_modified() ? t.request_last_modified() : 0;
      tile.priority = t.has_priority() ? t.priority() : -1;

      for (int i=0; i < t.parameters_size(); ++i) {
         const proto::parameter& p = t.parameters(i);
         tile.parameters.insert(std::make_pair<std::string, std::string>(p.key(), p.value()));
      }
   }
   return result;
}

} // namespace rendermq

#endif // TILE_PROTOCOL_HPP
