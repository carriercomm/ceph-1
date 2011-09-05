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

#include "OSDMap.h"

#include "common/config.h"
#include "common/Formatter.h"


void osd_info_t::dump(Formatter *f) const
{
  f->dump_int("last_clean_first", last_clean_first);
  f->dump_int("last_clean_last", last_clean_last);
  f->dump_int("up_from", up_from);
  f->dump_int("up_thru", up_thru);
  f->dump_int("down_at", down_at);
  f->dump_int("lost_at", lost_at);
}

void OSDMap::dump_json(ostream& out) const
{
  JSONFormatter jsf(true);
  jsf.open_object_section("osdmap");
  dump(&jsf);
  jsf.close_section();
  jsf.flush(out);
}

void OSDMap::dump(Formatter *f) const
{
  f->dump_int("epoch", get_epoch());
  f->dump_stream("fsid") << get_fsid();
  f->dump_stream("created") << get_created();
  f->dump_stream("modified") << get_modified();
  f->dump_string("flags", get_flag_string());
  f->dump_string("cluster_snapshot", get_cluster_snapshot());
  f->dump_int("max_osd", get_max_osd());

  f->open_array_section("pools");
  for (map<int,pg_pool_t>::const_iterator p = pools.begin(); p != pools.end(); ++p) {
    f->open_object_section("pool");
    f->dump_int("pool", p->first);
    p->second.dump(f);
    f->close_section();
  }
  f->close_section();

  f->open_array_section("osds");
  for (int i=0; i<get_max_osd(); i++)
    if (exists(i)) {
      f->open_object_section("osd_info");
      f->dump_int("osd", i);
      f->dump_int("up", is_up(i));
      f->dump_int("in", is_in(i));
      stringstream ss, ss_cluster, ss_hb;
      ss << get_addr(i);
      f->dump_string("addr", ss.str().c_str());
      ss_cluster << get_cluster_addr(i);
      f->dump_string("cluster_addr", ss_cluster.str().c_str());
      ss_hb << get_hb_addr(i);
      f->dump_string("hb_addr", ss_hb.str().c_str());
      
      get_info(i).dump(f);
      f->close_section();
    }
  f->close_section();

  f->open_array_section("pg_temp");
  for (map<pg_t,vector<int> >::const_iterator p = pg_temp.begin();
       p != pg_temp.end();
       p++) {
    f->open_array_section("osds");
    for (vector<int>::const_iterator q = p->second.begin(); q != p->second.end(); ++q)
      f->dump_int("osd", *q);
    f->close_section();
  }
  f->close_section();
  
  f->open_array_section("blacklist");
  for (hash_map<entity_addr_t,utime_t>::const_iterator p = blacklist.begin();
       p != blacklist.end();
       p++) {
    stringstream ss;
    ss << p->first;
    f->dump_stream(ss.str().c_str()) << p->second;
  }
  f->close_section();
}

string OSDMap::get_flag_string() const
{
  string s;
  if (test_flag(CEPH_OSDMAP_NEARFULL))
    s += ",nearfull";
  if (test_flag(CEPH_OSDMAP_FULL))
    s += ",full";
  if (test_flag(CEPH_OSDMAP_PAUSERD))
    s += ",pauserd";
  if (test_flag(CEPH_OSDMAP_PAUSEWR))
    s += ",pausewr";
  if (test_flag(CEPH_OSDMAP_PAUSEREC))
    s += ",pauserec";
  if (s.length())
    s = s.erase(0, 1);
  return s;
}

struct qi {
  int item;
  int depth;
  float weight;
  qi() {}
  qi(int i, int d, float w) : item(i), depth(d), weight(w) {}
};

void OSDMap::print(ostream& out) const
{
  out << "epoch " << get_epoch() << "\n"
      << "fsid " << get_fsid() << "\n"
      << "created " << get_created() << "\n"
      << "modifed " << get_modified() << "\n";

  out << "flags " << get_flag_string() << "\n";
  if (get_cluster_snapshot().length())
    out << "cluster_snapshot " << get_cluster_snapshot() << "\n";
  out << "\n";
 
  for (map<int,pg_pool_t>::const_iterator p = pools.begin(); p != pools.end(); ++p) {
    std::string name("<unknown>");
    map<int32_t,string>::const_iterator pni = pool_name.find(p->first);
    if (pni != pool_name.end())
      name = pni->second;
    out << "pg_pool " << p->first
	<< " '" << name
	<< "' " << p->second << "\n";
    for (map<snapid_t,pool_snap_info_t>::const_iterator q = p->second.snaps.begin();
	 q != p->second.snaps.end();
	 q++)
      out << "\tsnap " << q->second.snapid << " '" << q->second.name << "' " << q->second.stamp << "\n";
    if (!p->second.removed_snaps.empty())
      out << "\tremoved_snaps " << p->second.removed_snaps << "\n";
  }
  out << std::endl;

  out << "max_osd " << get_max_osd() << "\n";
  for (int i=0; i<get_max_osd(); i++) {
    if (exists(i)) {
      out << "osd" << i;
      out << (is_up(i) ? " up  ":" down");
      out << (is_in(i) ? " in ":" out");
      if (is_in(i))
	out << " weight " << get_weightf(i);
      const osd_info_t& info(get_info(i));
      out << " " << info;
      if (is_up(i))
	out << " " << get_addr(i) << " " << get_cluster_addr(i) << " " << get_hb_addr(i);
      out << "\n";
    }
  }
  out << std::endl;

  for (map<pg_t,vector<int> >::const_iterator p = pg_temp.begin();
       p != pg_temp.end();
       p++)
    out << "pg_temp " << p->first << " " << p->second << "\n";
  
  for (hash_map<entity_addr_t,utime_t>::const_iterator p = blacklist.begin();
       p != blacklist.end();
       p++)
    out << "blacklist " << p->first << " expires " << p->second << "\n";
  
  // ignore pg_swap_primary
}

void OSDMap::print_osd_line(int cur, ostream& out) const
{
  out << "osd." << cur << "\t";
  if (!exists(cur))
    out << "DNE\t\t";
  else {
    if (is_up(cur))
      out << "up\t";
    else
      out << "down\t";
    out << (exists(cur) ? get_weightf(cur):0) << "\t";
  }
}

void OSDMap::print_tree(ostream& out) const
{
  out << "# id\tweight\ttype name\tup/down\treweight\n";
  set<int> touched;
  set<int> roots;
  crush.find_roots(roots);
  for (set<int>::iterator p = roots.begin(); p != roots.end(); p++) {
    list<qi> q;
    q.push_back(qi(*p, 0, crush.get_bucket_weight(*p) / (float)0x10000));
    while (!q.empty()) {
      int cur = q.front().item;
      int depth = q.front().depth;
      float weight = q.front().weight;
      q.pop_front();

      out << cur << "\t" << weight << "\t";
      for (int k=0; k<depth; k++)
	out << "\t";

      if (cur >= 0) {
	print_osd_line(cur, out);
	out << "\n";
	touched.insert(cur);
	continue;
      }

      int type = crush.get_bucket_type(cur);
      out << crush.get_type_name(type) << " " << crush.get_item_name(cur) << "\n";

      // queue bucket contents...
      int s = crush.get_bucket_size(cur);
      for (int k=s-1; k>=0; k--)
	q.push_front(qi(crush.get_bucket_item(cur, k), depth+1,
			(float)crush.get_bucket_item_weight(cur, k) / (float)0x10000));
    }
  }

  set<int> stray;
  for (int i=0; i<max_osd; i++)
    if (exists(i) && touched.count(i) == 0)
      stray.insert(i);
  
  if (!stray.empty()) {
    out << "\n";
    for (set<int>::iterator p = stray.begin(); p != stray.end(); ++p) {
      out << *p << "\t0\t";
      print_osd_line(*p, out);
      out << "\n";
    }
  }
    
}

void OSDMap::print_summary(ostream& out) const
{
  out << "e" << get_epoch() << ": "
      << get_num_osds() << " osds: "
      << get_num_up_osds() << " up, " 
      << get_num_in_osds() << " in";
  if (test_flag(CEPH_OSDMAP_FULL))
    out << " full";
  else if (test_flag(CEPH_OSDMAP_NEARFULL))
    out << " nearfull";
}


void OSDMap::build_simple(CephContext *cct, epoch_t e, ceph_fsid_t &fsid,
			  int nosd, int ndom, int pg_bits, int pgp_bits, int lpg_bits)
{
  ldout(cct, 10) << "build_simple on " << num_osd
	   << " osds with " << pg_bits << " pg bits per osd, "
	   << lpg_bits << " lpg bits" << dendl;
  epoch = e;
  set_fsid(fsid);
  created = modified = ceph_clock_now(cct);

  set_max_osd(nosd);

  // pgp_num <= pg_num
  if (pgp_bits > pg_bits)
    pgp_bits = pg_bits; 
  
  // crush map
  map<int, const char*> rulesets;
  rulesets[CEPH_DATA_RULE] = "data";
  rulesets[CEPH_METADATA_RULE] = "metadata";
  rulesets[CEPH_RBD_RULE] = "rbd";
  
  for (map<int,const char*>::iterator p = rulesets.begin(); p != rulesets.end(); p++) {
    int pool = ++pool_max;
    pools[pool].v.type = CEPH_PG_TYPE_REP;
    pools[pool].v.size = cct->_conf->osd_pool_default_size;
    pools[pool].v.crush_ruleset = p->first;
    pools[pool].v.object_hash = CEPH_STR_HASH_RJENKINS;
    pools[pool].v.pg_num = nosd << pg_bits;
    pools[pool].v.pgp_num = nosd << pgp_bits;
    pools[pool].v.lpg_num = lpg_bits ? (1 << (lpg_bits-1)) : 0;
    pools[pool].v.lpgp_num = lpg_bits ? (1 << (lpg_bits-1)) : 0;
    pools[pool].v.last_change = epoch;
    pool_name[pool] = p->second;
  }

  build_simple_crush_map(cct, crush, rulesets, nosd, ndom);

  for (int i=0; i<nosd; i++) {
    set_state(i, 0);
    set_weight(i, CEPH_OSD_OUT);
  }
}

void OSDMap::build_simple_crush_map(CephContext *cct, CrushWrapper& crush,
	map<int, const char*>& rulesets, int nosd, int ndom)
{
  // new
  crush.create();

  crush.set_type_name(0, "osd");
  crush.set_type_name(1, "domain");
  crush.set_type_name(2, "pool");

  const md_config_t *conf = cct->_conf;
  int minrep = conf->osd_min_rep;
  int maxrep = conf->osd_max_rep;
  assert(maxrep >= minrep);
  if (!ndom)
    ndom = MAX(maxrep, conf->osd_max_raid_width);
  if (ndom > 1 &&
      nosd >= ndom*3 &&
      nosd > 8) {
    int ritems[ndom];
    int rweights[ndom];

    int nper = ((nosd - 1) / ndom) + 1;
    ldout(cct, 0) << ndom << " failure domains, " << nper << " osds each" << dendl;
    
    int o = 0;
    for (int i=0; i<ndom; i++) {
      int items[nper], weights[nper];      
      int j;
      rweights[i] = 0;
      for (j=0; j<nper; j++, o++) {
	if (o == nosd) break;
	ldout(cct, 20) << "added osd" << o << dendl;
	items[j] = o;
	weights[j] = 0x10000;
	//w[j] = weights[o] ? (0x10000 - (int)(weights[o] * 0x10000)):0x10000;
	//rweights[i] += w[j];
	rweights[i] += 0x10000;
      }

      crush_bucket *domain = crush_make_bucket(CRUSH_BUCKET_STRAW, CRUSH_HASH_DEFAULT, 1, j, items, weights);
      ritems[i] = crush_add_bucket(crush.crush, 0, domain);
      ldout(cct, 20) << "added domain bucket i " << ritems[i] << " of size " << j << dendl;

      char bname[10];
      snprintf(bname, sizeof(bname), "dom%d", i);
      crush.set_item_name(ritems[i], bname);
    }
    
    // root
    crush_bucket *root = crush_make_bucket(CRUSH_BUCKET_STRAW, CRUSH_HASH_DEFAULT, 2, ndom, ritems, rweights);
    int rootid = crush_add_bucket(crush.crush, 0, root);
    crush.set_item_name(rootid, "root");

    // rules
    for (map<int,const char*>::iterator p = rulesets.begin(); p != rulesets.end(); p++) {
      int ruleset = p->first;
      crush_rule *rule = crush_make_rule(3, ruleset, CEPH_PG_TYPE_REP, minrep, maxrep);
      crush_rule_set_step(rule, 0, CRUSH_RULE_TAKE, rootid, 0);
      crush_rule_set_step(rule, 1, CRUSH_RULE_CHOOSE_LEAF_FIRSTN, CRUSH_CHOOSE_N, 1); // choose N domains
      crush_rule_set_step(rule, 2, CRUSH_RULE_EMIT, 0, 0);
      int rno = crush_add_rule(crush.crush, rule, -1);
      crush.set_rule_name(rno, p->second);
    }
    
  } else {
    // one bucket

    int items[nosd];
    int weights[nosd];
    for (int i=0; i<nosd; i++) {
      items[i] = i;
      weights[i] = 0x10000;
    }

    crush_bucket *b = crush_make_bucket(CRUSH_BUCKET_STRAW, CRUSH_HASH_DEFAULT, 1, nosd, items, weights);
    int rootid = crush_add_bucket(crush.crush, 0, b);
    crush.set_item_name(rootid, "root");

    // replication
    for (map<int,const char*>::iterator p = rulesets.begin(); p != rulesets.end(); p++) {
      int ruleset = p->first;
      crush_rule *rule = crush_make_rule(3, ruleset, CEPH_PG_TYPE_REP, conf->osd_min_rep, maxrep);
      crush_rule_set_step(rule, 0, CRUSH_RULE_TAKE, rootid, 0);
      crush_rule_set_step(rule, 1, CRUSH_RULE_CHOOSE_FIRSTN, CRUSH_CHOOSE_N, 0);
      crush_rule_set_step(rule, 2, CRUSH_RULE_EMIT, 0, 0);
      int rno = crush_add_rule(crush.crush, rule, -1);
      crush.set_rule_name(rno, p->second);
    }

  }

  crush.finalize();

  ldout(cct, 20) << "crush max_devices " << crush.crush->max_devices << dendl;
}

