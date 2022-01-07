/*
    EIBD eib bus access and management daemon
    Copyright (C) 2005-2011 Martin Koegler <mkoegler@auto.tuwien.ac.at>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

/**
 * @file
 * @ingroup KNX_03_03_03
 * Network Layer
 * @{
 */

#ifndef ROUTER_H
#define ROUTER_H

#include <unordered_map>

#include <ev++.h>

#include "link.h"
#include "lowlevel.h"
#include "lpdu.h"

class BaseServer;
class GroupCache;
class LinkConnect;
class Router;
class RouterHigh;
class RouterLow;

/** "RouterHigh" (subclass of Driver) and "RouterLow" (subclass of LinkConnect_)
 * are internal classes of Router, declared in libserver/router.cpp.
 * They are used for bracketing global filters.
 */
using RouterLowPtr = std::shared_ptr<RouterLow>;
using RouterHighPtr = std::shared_ptr<RouterHigh>;

/** stores a registered busmonitor callback */
struct Busmonitor_Info
{
  L_Busmonitor_CallBack *cb;
};

struct IgnoreInfo
{
  CArray data;
  timestamp_t end;
};

class Router : public BaseRouter
{
  friend class RouterLow;
  friend class RouterHigh;

public:
  /** parser support */
  static bool readaddr (const std::string& addr, eibaddr_t& parsed);
  static bool readaddrblock (const std::string& addr, eibaddr_t& parsed, int &len);
  
  Router(IniData& d, std::string sn);
  virtual ~Router();

  /** my name */
  std::string servername;

  /** the server's own address */
  eibaddr_t addr = 0;

  /** group cache */
  std::shared_ptr<GroupCache> getCache()
  {
    return cache;
  }
  void setCache(std::shared_ptr<GroupCache> cache)
  {
    this->cache = cache;
  }

  /** read and apply settings */
  bool setup();
  /** start up */
  void start();
  /** shut down*/
  void stop(bool err);

  /** second step, after hitting the global queue */
  void start_();
  void stop_(bool err);

  /** last step, after hitting the global queue */
  void started();
  void stopped(bool err);

  /** callbacks from LinkConnect */
  void linkStateChanged(const LinkConnectPtr& link);

  /** register a new link. Must be fully linked and setup() must be OK. */
  bool registerLink(const LinkConnectPtr& link, bool transient = false);
  /** unregister a new link */
  bool unregisterLink(const LinkConnectPtr& link);

  /** register a busmonitor callback, return true, if successful*/
  bool registerBusmonitor (L_Busmonitor_CallBack * c);
  /** register a vbusmonitor callback, return true, if successful*/
  bool registerVBusmonitor (L_Busmonitor_CallBack * c);

  /** deregister a busmonitor callback, return true, if successful*/
  bool deregisterBusmonitor (L_Busmonitor_CallBack * c);
  /** deregister a vbusmonitor callback, return true, if successful*/
  bool deregisterVBusmonitor (L_Busmonitor_CallBack * c);

  /** Get a free dynamic address */
  eibaddr_t get_client_addr (TracePtr t);
  /** … and release it */
  void release_client_addr (eibaddr_t addr);

  /** check if any interface knows this address. */
  bool hasAddress (eibaddr_t addr, LinkConnectPtr& link, bool quiet = false) const;
  /** check if any interface accepts this address.
      'l2' says which interface NOT to check. */
  bool checkAddress (eibaddr_t addr, LinkConnectPtr l2 = nullptr) const;
  /** check if any interface accepts this group address.
      'l2' says which interface NOT to check. */
  bool checkGroupAddress (eibaddr_t addr, LinkConnectPtr l2 = nullptr) const;

  /** accept a L_Data frame */
  void recv_L_Data (LDataPtr l, LinkConnect& link);
  /** accept a L_Busmonitor frame */
  void recv_L_Busmonitor (LBusmonPtr l);
  /** packet buffer is empty */
  void send_Next();

  /** Look up a filter by name */
  FilterPtr get_filter(const LinkConnectPtr_ &link, IniSectionPtr& s, const std::string& filtername);

  /** Create a temporary dummy driver stack to test arguments for filters etc.
   * Testing the calling driver's config args is the caller#s job.
   */
  bool checkStack(IniSectionPtr& cfg);

  /** name of our main section */
  std::string main;

  bool hasClientAddrs(bool complain = true) const;

  /** eventual exit code. Inremebted on fatal error */
  int exitcode = 0;

  /** allow unparsed tags in the config file? */
  bool unknown_ok = false;
  /** flag whether systemd has passed us any file descriptors */
  bool using_systemd = false;

  bool isIdle()
  {
    return !some_running;
  }
  bool isRunning()
  {
    return all_running;
  }

private:
  Factory<Server>& servers;
  Factory<Driver>& drivers;
  Factory<Filter>& filters;

  bool do_server(ServerPtr &link, IniSectionPtr& s, const std::string& servername, bool quiet = false);
  bool do_driver(LinkConnectPtr &link, IniSectionPtr& s, const std::string& servername, bool quiet = false);

  RouterLowPtr r_low;
  RouterHighPtr r_high;

  void send_L_Data(LDataPtr l1); // called by RouterHigh
  void queue_L_Data(LDataPtr l1); // called by RouterLow
  void queue_L_Busmonitor (LBusmonPtr l); // called by RouterLow

  /** loop counter for keeping track of iterators */
  int seq = 1;

  /** Markers to continue sending */
  bool low_send_more = false;
  bool high_send_more = false;
  bool high_sending = false;

  /** create a link */
  LinkConnectPtr setup_link(std::string& name);

  /** interfaces */
  std::unordered_map<int, LinkConnectPtr> links;

  /** queue of interfaces which called linkChanged() */
  Queue<LinkConnectPtr> linkChanges;

  // libev
  ev::async trigger;
  void trigger_cb (ev::async &w, int revents);
  ev::async mtrigger;
  void mtrigger_cb (ev::async &w, int revents);
  ev::async state_trigger;
  void state_trigger_cb (ev::async &w, int revents);

  /** buffer queues for receiving from L2 */
  Queue < LDataPtr > buf;
  Queue < LBusmonPtr > mbuf;
  /** buffer for packets to ignore when repeat flag is set */
  std::vector < IgnoreInfo > ignore;

  /** Start of address block to assign dynamically to clients */
  eibaddr_t client_addrs_start;
  /** Length of address block to assign dynamically to clients */
  int client_addrs_len = 0;
  int client_addrs_pos;
  std::vector<bool> client_addrs;

  /** busmonitor callbacks */
  std::vector < Busmonitor_Info > busmonitor;
  /** vbusmonitor callbacks */
  std::vector < Busmonitor_Info > vbusmonitor;

  /** flag whether some driver is active */
  bool some_running = false;
  /** suppress "still foo" messages */
  int in_link_loop = 0;
  /** flag whether new drivers should be active */
  bool want_up = false;
  /** flag whether all drivers are active */
  bool all_running = false;
  /** flag to signal systemd */
  bool running_signal = false;

  /** treat route count 7 as per EIB spec? */
  bool force_broadcast = false;

  /** iterators are evil */
  bool links_changed = false;

  ev::async cleanup;
  void cleanup_cb (ev::async &w, int revents);
  /** to-be-closed client connections*/
  Queue < LinkBasePtr > cleanup_q;

  /** group cache */
  std::shared_ptr<GroupCache> cache;

  /** error checking */
  bool has_send_more(LinkConnectPtr i);
};

#endif

/** @} */
