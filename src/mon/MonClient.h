// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2004-2006 Sage Weil <sage@newdream.net>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software 
 * Foundation.  See file COPYING.
 * 
 */

#ifndef CEPH_MONCLIENT_H
#define CEPH_MONCLIENT_H

#include "msg/Dispatcher.h"
#include "msg/Messenger.h"

#include "MonMap.h"

#include "common/Timer.h"

#include "auth/AuthClientHandler.h"
#include "auth/RotatingKeyRing.h"

#include "messages/MMonSubscribe.h"

#include <memory>

class MonMap;
class MMonMap;
class MMonGetVersion;
class MMonGetVersionReply;
class MMonSubscribeAck;
class MAuthReply;
class MAuthRotating;
class LogClient;

enum MonClientState {
  MC_STATE_NONE,
  MC_STATE_NEGOTIATING,
  MC_STATE_AUTHENTICATING,
  MC_STATE_HAVE_SESSION,
};

class MonClient : public Dispatcher {
public:
  MonMap monmap;
private:
  MonClientState state;

  Messenger *messenger;

  string cur_mon;
  Connection *cur_con;

  EntityName entity_name;

  entity_addr_t my_addr;

  Mutex monc_lock;
  SafeTimer timer;

  LogClient *log_client;

  set<__u32> auth_supported;

  bool ms_dispatch(Message *m);
  bool ms_handle_reset(Connection *con);
  void ms_handle_remote_reset(Connection *con) {}

  void handle_monmap(MMonMap *m);

  void handle_auth(MAuthReply *m);


  // monitor session
  bool hunting;

  struct C_Tick : public Context {
    MonClient *monc;
    C_Tick(MonClient *m) : monc(m) {}
    void finish(int r) {
      monc->tick();
    }
  };
  void tick();
  void schedule_tick();

  Cond auth_cond;

  void handle_auth_rotating_response(MAuthRotating *m);
  // monclient
  bool want_monmap;

  uint32_t want_keys;

  uint64_t global_id;

  // authenticate
private:
  Cond map_cond;
  int authenticate_err;

  list<Message*> waiting_for_session;

  void _finish_hunting();
  void _reopen_session();
  void _pick_new_mon();
  void _send_mon_message(Message *m, bool force=false);

public:
  void set_entity_name(EntityName name) { entity_name = name; }

  int _check_auth_tickets();
  int _check_auth_rotating();
  int wait_auth_rotating(double timeout);

  int authenticate(double timeout=0.0);

  // mon subscriptions
private:
  map<string,ceph_mon_subscribe_item> sub_have;  // my subs, and current versions
  utime_t sub_renew_sent, sub_renew_after;

  void _renew_subs();
  void handle_subscribe_ack(MMonSubscribeAck* m);

  bool _sub_want(string what, version_t start, unsigned flags) {
    if (sub_have.count(what) &&
	sub_have[what].start == start &&
	sub_have[what].flags == flags)
      return false;
    sub_have[what].start = start;
    sub_have[what].flags = flags;
    return true;
  }
  void _sub_got(string what, version_t got) {
    if (sub_have.count(what)) {
      if (sub_have[what].flags & CEPH_SUBSCRIBE_ONETIME)
	sub_have.erase(what);
      else
	sub_have[what].start = got + 1;
    }
  }

  // auth tickets
public:
  AuthClientHandler *auth;
public:
  void renew_subs() {
    Mutex::Locker l(monc_lock);
    _renew_subs();
  }
  bool sub_want(string what, version_t start, unsigned flags) {
    Mutex::Locker l(monc_lock);
    return _sub_want(what, start, flags);
  }
  void sub_got(string what, version_t have) {
    Mutex::Locker l(monc_lock);
    _sub_got(what, have);
  }
  
  KeyRing *keyring;
  RotatingKeyRing *rotating_secrets;

 public:
  MonClient(CephContext *cct_);
  ~MonClient();

  int init();
  void shutdown();

  void set_log_client(LogClient *clog) {
    log_client = clog;
  }

  static int build_initial_monmap(CephContext *cct, MonMap &monmap);

  int build_initial_monmap();
  int get_monmap();
  int get_monmap_privately();

  void send_mon_message(Message *m) {
    Mutex::Locker l(monc_lock);
    _send_mon_message(m);
  }
  void reopen_session() {
    Mutex::Locker l(monc_lock);
    _reopen_session();
  }

  entity_addr_t get_my_addr() const {
    return my_addr;
  }

  const ceph_fsid_t& get_fsid() {
    return monmap.fsid;
  }

  entity_addr_t get_mon_addr(unsigned i) {
    Mutex::Locker l(monc_lock);
    if (i < monmap.size())
      return monmap.get_addr(i);
    return entity_addr_t();
  }
  entity_inst_t get_mon_inst(unsigned i) {
    Mutex::Locker l(monc_lock);
    if (i < monmap.size())
      return monmap.get_inst(i);
    return entity_inst_t();
  }
  int get_num_mon() {
    Mutex::Locker l(monc_lock);
    return monmap.size();
  }

  uint64_t get_global_id() const {
    return global_id;
  }

  void set_messenger(Messenger *m) { messenger = m; }

  void send_auth_message(Message *m) {
    _send_mon_message(m, true);
  }

  void set_want_keys(uint32_t want) {
    want_keys = want;
    if (auth)
      auth->set_want_keys(want | CEPH_ENTITY_TYPE_MON);
  }

  void add_want_keys(uint32_t want) {
    want_keys |= want;
    if (auth)
      auth->add_want_keys(want);
  }

  // version requests
public:
  void is_latest_map(string map, version_t cur_ver, Context *onfinish);

private:
  struct version_req_d {
    Context *context;
    version_t version;
    version_req_d(Context *con, version_t ver) : context(con), version(ver) {}
  };

  map<tid_t, version_req_d*> version_requests;
  tid_t version_req_id;
  void handle_get_version_reply(MMonGetVersionReply* m);


  MonClient(const MonClient &rhs);
  MonClient& operator=(const MonClient &rhs);
};

#endif
