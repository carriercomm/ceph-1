// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2004-2009 Sage Weil <sage@newdream.net>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software 
 * Foundation.  See file COPYING.
 * 
 */

#ifndef __AUTHPROTOCOL_H
#define __AUTHPROTOCOL_H

#include <map>
#include <set>
using namespace std;

#include "include/types.h"

#include "config.h"

/*
  Ceph X protocol

  First, the principal has to authenticate with the authenticator. A
  shared-secret mechanism is being used, and the negotitaion goes like this:

  A = Authenticator
  P = Principle
  S = Service

  1. Obtaining principal/auth session key

  (Authenticate Request)
  p->a : principal, principal_addr.  authenticate me!

 ...authenticator does lookup in database...

  a->p : A= {principal/auth session key, validity}^principal_secret (*)
         B= {principal ticket, validity, principal/auth session key}^authsecret

  
  [principal/auth session key, validity] = service ticket
  [principal ticket, validity, principal/auth session key] = service ticket info

  (*) annotation: ^ signifies 'encrypted by'

  At this point, if is genuine, the principal should have the principal/auth
  session key at hand. The next step would be to request an authorization to
  use some other service:

  2. Obtaining principal/service session key

  p->a : B, {principal_addr, timestamp}^principal/auth session key.  authorize
         me!
  a->p : E= {service ticket}^svcsecret
         F= {principal/service session key, validity}^principal/auth session key

  principal_addr, timestamp = authenticator

  service ticket = principal name, client network address, validity, principal/service session key

  Note that steps 1 and 2 are pretty much the same thing; contacting the
  authenticator and requesting for a key.

  Following this the principal should have a principal/service session key that
  could be used later on for creating a session:

  3. Opening a session to a service

  p->s : E + {principal_addr, timestamp}^principal/service session key
  s->p : {timestamp+1}^principal/service/session key

  timestamp+1 = reply authenticator

  Now, the principal is fully authenticated with the service. So, logically we
  have 2 main actions here. The first one would be to obtain a session key to
  the service (steps 1 and 2), and the second one would be to authenticate with
  the service, using that ticket.
*/

#define CEPHX_PRINCIPAL_AUTH            0x0001
#define CEPHX_PRINCIPAL_MON             0x0002
#define CEPHX_PRINCIPAL_OSD             0x0004
#define CEPHX_PRINCIPAL_MDS             0x0008
#define CEPHX_PRINCIPAL_CLIENT          0x0010

#define CEPHX_PRINCIPAL_TYPE_MASK       0x00FF

/* authenticate requests */
#define CEPHX_GET_AUTH_SESSION_KEY      0x0100
#define CEPHX_GET_PRINCIPAL_SESSION_KEY 0x0200

/* authorize requests */
#define CEPHX_OPEN_SESSION              0x0300

#define CEPHX_REQUEST_TYPE_MASK         0x0F00

class Monitor;

struct EntityName {
  uint32_t entity_type;
  string name;

  void encode(bufferlist& bl) const {
    ::encode(entity_type, bl);
    ::encode(name, bl);
  }
  void decode(bufferlist::iterator& bl) {
    ::decode(entity_type, bl);
    ::decode(name, bl);
  }

  void to_str(string& str) const {
    switch (entity_type) {
      case CEPHX_PRINCIPAL_AUTH:
        str = "auth";
        break;
      case CEPHX_PRINCIPAL_MON:
        str = "mon";
        break;
      case CEPHX_PRINCIPAL_OSD:
        str = "osd";
        break;
      case CEPHX_PRINCIPAL_MDS:
        str = "mds";
        break;
      case CEPHX_PRINCIPAL_CLIENT:
        str = "client";
        break;
      default:
        str = "???";
        break;
    }
    str.append(".");
    str.append(name);
  }
  string to_str() const {
    string s;
    to_str(s);
    return s;
  }

  bool from_str(string& s) {
    int pos = s.find('.');

    if (pos < 0)
      return false;
    if (pos >= (int)s.size())
      return false;
   
    string pre = s.substr(0, pos);
    const char *pres = pre.c_str();

    set_type(pres);

    name = s.substr(pos + 1);

    return true;
  }

  void set_type(const char *type) {
    if (strcmp(type, "auth") == 0) {
      entity_type = CEPHX_PRINCIPAL_AUTH;
    } else if (strcmp(type, "mon") == 0) {
      entity_type = CEPHX_PRINCIPAL_MON;
    } else if (strcmp(type, "osd") == 0) {
      entity_type = CEPHX_PRINCIPAL_OSD;
    } else if (strcmp(type, "mds") == 0) {
      entity_type = CEPHX_PRINCIPAL_MDS;
    } else {
      entity_type = CEPHX_PRINCIPAL_CLIENT;
    }
  }
  void from_type_id(const char *type, const char *id) {
    set_type(type);
    name = id;
  }
};
WRITE_CLASS_ENCODER(EntityName);

inline bool operator<(const EntityName& a, const EntityName& b) {
  return (a.entity_type < b.entity_type) || (a.entity_type == b.entity_type && a.name < b.name);
}

struct CephXPremable {
  uint32_t trans_id;

  void encode(bufferlist& bl) const {
    ::encode(trans_id, bl);
  }

  void decode(bufferlist::iterator& bl) {
    ::decode(trans_id, bl);
  }
};
WRITE_CLASS_ENCODER(CephXPremable)

/* 
  Ceph X-Envelope protocol
*/
struct CephXEnvRequest1 {
  EntityName name;

  void encode(bufferlist& bl) const {
    ::encode(name, bl);
  }
  void decode(bufferlist::iterator& bl) {
    ::decode(name, bl);
  }
};
WRITE_CLASS_ENCODER(CephXEnvRequest1)

struct CephXEnvResponse1 {
  uint64_t server_challenge;

  void encode(bufferlist& bl) const {
    ::encode(server_challenge, bl);
  }
  void decode(bufferlist::iterator& bl) {
    ::decode(server_challenge, bl);
  }
};
WRITE_CLASS_ENCODER(CephXEnvResponse1);

struct CephXEnvRequest2 {
  uint64_t client_challenge;
  uint64_t key;
  char piggyback; /* do we piggyback X protocol */

  void encode(bufferlist& bl) const {
    ::encode(client_challenge, bl);
    ::encode(key, bl);
    ::encode(piggyback, bl);
  }
  void decode(bufferlist::iterator& bl) {
    ::decode(client_challenge, bl);
    ::decode(key, bl);
    ::decode(piggyback, bl);
  }
};
WRITE_CLASS_ENCODER(CephXEnvRequest2);


struct CephXRequestHeader {
  uint16_t request_type;

  void encode(bufferlist& bl) const {
    ::encode(request_type, bl);
  }
  void decode(bufferlist::iterator& bl) {
    ::decode(request_type, bl);
  }
};
WRITE_CLASS_ENCODER(CephXRequestHeader);

struct CephXResponseHeader {
  uint16_t request_type;
  int32_t status;

  void encode(bufferlist& bl) const {
    ::encode(request_type, bl);
    ::encode(status, bl);
  }
  void decode(bufferlist::iterator& bl) {
    ::decode(request_type, bl);
    ::decode(status, bl);
  }
};
WRITE_CLASS_ENCODER(CephXResponseHeader);


#endif