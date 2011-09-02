
#include "osd_types.h"

// -- osd_reqid_t --
void osd_reqid_t::encode(bufferlist &bl) const
{
  __u8 struct_v = 1;
  ::encode(struct_v, bl);
  ::encode(name, bl);
  ::encode(tid, bl);
  ::encode(inc, bl);
}

void osd_reqid_t::decode(bufferlist::iterator &bl)
{
  __u8 struct_v;
  ::decode(struct_v, bl);
  ::decode(name, bl);
  ::decode(tid, bl);
  ::decode(inc, bl);
}


// -- pg_t --

int pg_t::print(char *o, int maxlen) const
{
  if (preferred() >= 0)
    return snprintf(o, maxlen, "%d.%xp%d", pool(), ps(), preferred());
  else
    return snprintf(o, maxlen, "%d.%x", pool(), ps());
}

bool pg_t::parse(const char *s)
{
  int ppool;
  int pseed;
  int pref;
  int r = sscanf(s, "%d.%xp%d", &ppool, &pseed, &pref);
  if (r < 2)
    return false;
  v.pool = ppool;
  v.ps = pseed;
  if (r == 3)
    v.preferred = pref;
  else
    v.preferred = -1;
  return true;
}

ostream& operator<<(ostream& out, const pg_t &pg)
{
  out << pg.pool() << '.';
  out << hex << pg.ps() << dec;

  if (pg.preferred() >= 0)
    out << 'p' << pg.preferred();

  //out << "=" << hex << (__uint64_t)pg << dec;
  return out;
}


// -- coll_t --

bool coll_t::is_pg(pg_t& pgid, snapid_t& snap) const
{
  const char *cstr(str.c_str());

  if (!pgid.parse(cstr))
    return false;
  const char *snap_start = strchr(cstr, '_');
  if (!snap_start)
    return false;
  if (strncmp(snap_start, "_head", 5) == 0)
    snap = CEPH_NOSNAP;
  else
    snap = strtoull(snap_start+1, 0, 16);
  return true;
}

void coll_t::encode(bufferlist& bl) const
{
  __u8 struct_v = 3;
  ::encode(struct_v, bl);
  ::encode(str, bl);
}

void coll_t::decode(bufferlist::iterator& bl)
{
  __u8 struct_v;
  ::decode(struct_v, bl);
  switch (struct_v) {
  case 1: {
    pg_t pgid;
    snapid_t snap;

    ::decode(pgid, bl);
    ::decode(snap, bl);
    // infer the type
    if (pgid == pg_t() && snap == 0)
      str = "meta";
    else
      str = pg_and_snap_to_str(pgid, snap);
    break;
  }

  case 2: {
    __u8 type;
    pg_t pgid;
    snapid_t snap;
    
    ::decode(type, bl);
    ::decode(pgid, bl);
    ::decode(snap, bl);
    switch (type) {
    case 0:
      str = "meta";
      break;
    case 1:
      str = "temp";
      break;
    case 2:
      str = pg_and_snap_to_str(pgid, snap);
      break;
    default: {
      ostringstream oss;
      oss << "coll_t::decode(): can't understand type " << type;
      throw std::domain_error(oss.str());
    }
    }
    break;
  }

  case 3:
    ::decode(str, bl);
    break;
    
  default: {
    ostringstream oss;
    oss << "coll_t::decode(): don't know how to decode version "
	<< struct_v;
    throw std::domain_error(oss.str());
  }
  }
}

// ---

std::string pg_state_string(int state)
{
  ostringstream oss;
  if (state & PG_STATE_CREATING)
    oss << "creating+";
  if (state & PG_STATE_ACTIVE)
    oss << "active+";
  if (state & PG_STATE_CLEAN)
    oss << "clean+";
  if (state & PG_STATE_CRASHED)
    oss << "crashed+";
  if (state & PG_STATE_DOWN)
    oss << "down+";
  if (state & PG_STATE_REPLAY)
    oss << "replay+";
  if (state & PG_STATE_STRAY)
    oss << "stray+";
  if (state & PG_STATE_SPLITTING)
    oss << "splitting+";
  if (state & PG_STATE_DEGRADED)
    oss << "degraded+";
  if (state & PG_STATE_SCRUBBING)
    oss << "scrubbing+";
  if (state & PG_STATE_SCRUBQ)
    oss << "scrubq+";
  if (state & PG_STATE_INCONSISTENT)
    oss << "inconsistent+";
  if (state & PG_STATE_PEERING)
    oss << "peering+";
  if (state & PG_STATE_REPAIR)
    oss << "repair+";
  if (state & PG_STATE_SCANNING)
    oss << "scanning+";
  string ret(oss.str());
  if (ret.length() > 0)
    ret.resize(ret.length() - 1);
  else
    ret = "inactive";
  return ret;
}



// -- pool_snap_info_t --
void pool_snap_info_t::dump(Formatter *f) const
{
  f->dump_unsigned("snapid", snapid);
  f->dump_stream("stamp") << stamp;
  f->dump_string("name", name);
}

void pool_snap_info_t::encode(bufferlist& bl) const
{
  __u8 struct_v = 1;
  ::encode(struct_v, bl);
  ::encode(snapid, bl);
  ::encode(stamp, bl);
  ::encode(name, bl);
}

void pool_snap_info_t::decode(bufferlist::iterator& bl)
{
  __u8 struct_v;
  ::decode(struct_v, bl);
  ::decode(snapid, bl);
  ::decode(stamp, bl);
  ::decode(name, bl);
}


// -- pg_pool_t --

void pg_pool_t::dump(Formatter *f) const
{
  f->dump_int("type", get_type());
  f->dump_int("size", get_size());
  f->dump_int("crush_ruleset", get_crush_ruleset());
  f->dump_int("object_hash", get_object_hash());
  f->dump_int("pg_num", get_pg_num());
  f->dump_int("pg_placement_num", get_pgp_num());
  f->dump_int("localized_pg_num", get_lpg_num());
  f->dump_int("localized_pg_placement_num", get_lpgp_num());
  f->dump_stream("last_change") << get_last_change();
  f->dump_unsigned("auid", get_auid());
  f->dump_string("snap_mode", is_pool_snaps_mode() ? "pool" : "selfmanaged");
  f->dump_unsigned("snap_seq", get_snap_seq());
  f->dump_unsigned("snap_epoch", get_snap_epoch());
  f->open_object_section("pool_snaps");
  for (map<snapid_t, pool_snap_info_t>::const_iterator p = snaps.begin(); p != snaps.end(); ++p) {
    f->open_object_section("pool_snap_info");
    p->second.dump(f);
    f->close_section();
  }
  f->close_section();
  f->dump_stream("removed_snaps") << removed_snaps;
}


int pg_pool_t::calc_bits_of(int t)
{
  int b = 0;
  while (t > 0) {
    t = t >> 1;
    b++;
  }
  return b;
}

void pg_pool_t::calc_pg_masks()
{
  pg_num_mask = (1 << calc_bits_of(v.pg_num-1)) - 1;
  pgp_num_mask = (1 << calc_bits_of(v.pgp_num-1)) - 1;
  lpg_num_mask = (1 << calc_bits_of(v.lpg_num-1)) - 1;
  lpgp_num_mask = (1 << calc_bits_of(v.lpgp_num-1)) - 1;
}

/*
 * we have two snap modes:
 *  - pool global snaps
 *    - snap existence/non-existence defined by snaps[] and snap_seq
 *  - user managed snaps
 *    - removal governed by removed_snaps
 *
 * we know which mode we're using based on whether removed_snaps is empty.
 */
bool pg_pool_t::is_pool_snaps_mode() const
{
  return removed_snaps.empty() && get_snap_seq() > 0;
}

bool pg_pool_t::is_removed_snap(snapid_t s) const
{
  if (is_pool_snaps_mode())
    return s <= get_snap_seq() && snaps.count(s) == 0;
  else
    return removed_snaps.contains(s);
}

/*
 * build set of known-removed sets from either pool snaps or
 * explicit removed_snaps set.
 */
void pg_pool_t::build_removed_snaps(interval_set<snapid_t>& rs) const
{
  if (is_pool_snaps_mode()) {
    rs.clear();
    for (snapid_t s = 1; s <= get_snap_seq(); s = s + 1)
      if (snaps.count(s) == 0)
	rs.insert(s);
  } else {
    rs = removed_snaps;
  }
}

snapid_t pg_pool_t::snap_exists(const char *s) const
{
  for (map<snapid_t,pool_snap_info_t>::const_iterator p = snaps.begin();
       p != snaps.end();
       p++)
    if (p->second.name == s)
      return p->second.snapid;
  return 0;
}

void pg_pool_t::add_snap(const char *n, utime_t stamp)
{
  assert(removed_snaps.empty());
  snapid_t s = get_snap_seq() + 1;
  v.snap_seq = s;
  snaps[s].snapid = s;
  snaps[s].name = n;
  snaps[s].stamp = stamp;
}

void pg_pool_t::add_unmanaged_snap(uint64_t& snapid)
{
  if (removed_snaps.empty()) {
    assert(snaps.empty());
    removed_snaps.insert(snapid_t(1));
    v.snap_seq = 1;
  }
  snapid = v.snap_seq = v.snap_seq + 1;
}

void pg_pool_t::remove_snap(snapid_t s)
{
  assert(snaps.count(s));
  snaps.erase(s);
  v.snap_seq = v.snap_seq + 1;
}

void pg_pool_t::remove_unmanaged_snap(snapid_t s)
{
  assert(snaps.empty());
  removed_snaps.insert(s);
  v.snap_seq = v.snap_seq + 1;
  removed_snaps.insert(get_snap_seq());
}

SnapContext pg_pool_t::get_snap_context() const
{
  vector<snapid_t> s(snaps.size());
  unsigned i = 0;
  for (map<snapid_t, pool_snap_info_t>::const_reverse_iterator p = snaps.rbegin();
       p != snaps.rend();
       p++)
    s[i++] = p->first;
  return SnapContext(get_snap_seq(), s);
}

/*
 * map a raw pg (with full precision ps) into an actual pg, for storage
 */
pg_t pg_pool_t::raw_pg_to_pg(pg_t pg) const
{
  if (pg.preferred() >= 0 && v.lpg_num)
    pg.v.ps = ceph_stable_mod(pg.ps(), v.lpg_num, lpg_num_mask);
  else
    pg.v.ps = ceph_stable_mod(pg.ps(), v.pg_num, pg_num_mask);
  return pg;
}
  
/*
 * map raw pg (full precision ps) into a placement seed.  include
 * pool id in that value so that different pools don't use the same
 * seeds.
 */
ps_t pg_pool_t::raw_pg_to_pps(pg_t pg) const
{
  if (pg.preferred() >= 0 && v.lpgp_num)
    return ceph_stable_mod(pg.ps(), v.lpgp_num, lpgp_num_mask) + pg.pool();
  else
    return ceph_stable_mod(pg.ps(), v.pgp_num, pgp_num_mask) + pg.pool();
}

void pg_pool_t::encode(bufferlist& bl) const
{
  __u8 struct_v = CEPH_PG_POOL_VERSION;
  ::encode(struct_v, bl);
  v.num_snaps = snaps.size();
  v.num_removed_snap_intervals = removed_snaps.num_intervals();
  ::encode(v, bl);
  ::encode_nohead(snaps, bl);
  removed_snaps.encode_nohead(bl);
}

void pg_pool_t::decode(bufferlist::iterator& bl)
{
  __u8 struct_v;
  ::decode(struct_v, bl);
  if (struct_v > CEPH_PG_POOL_VERSION)
    throw buffer::error();
  ::decode(v, bl);
  ::decode_nohead(v.num_snaps, snaps, bl);
  removed_snaps.decode_nohead(v.num_removed_snap_intervals, bl);
  calc_pg_masks();
}

ostream& operator<<(ostream& out, const pg_pool_t& p)
{
  out << "pg_pool(";
  switch (p.get_type()) {
  case CEPH_PG_TYPE_REP: out << "rep"; break;
  default: out << "type " << p.get_type();
  }
  out << " pg_size " << p.get_size()
      << " crush_ruleset " << p.get_crush_ruleset()
      << " object_hash " << p.get_object_hash_name()
      << " pg_num " << p.get_pg_num()
      << " pgp_num " << p.get_pgp_num()
      << " lpg_num " << p.get_lpg_num()
      << " lpgp_num " << p.get_lpgp_num()
      << " last_change " << p.get_last_change()
      << " owner " << p.v.auid
      << ")";
  return out;
}



// -- OSDSuperblock --

void OSDSuperblock::encode(bufferlist &bl) const
{
  __u8 v = 3;
  ::encode(v, bl);

  ::encode(fsid, bl);
  ::encode(whoami, bl);
  ::encode(current_epoch, bl);
  ::encode(oldest_map, bl);
  ::encode(newest_map, bl);
  ::encode(weight, bl);
  compat_features.encode(bl);
  ::encode(clean_thru, bl);
  ::encode(mounted, bl);
}

void OSDSuperblock::decode(bufferlist::iterator &bl)
{
  __u8 v;
  ::decode(v, bl);

  if (v < 3) {
    string magic;
    ::decode(magic, bl);
  }
  ::decode(fsid, bl);
  ::decode(whoami, bl);
  ::decode(current_epoch, bl);
  ::decode(oldest_map, bl);
  ::decode(newest_map, bl);
  ::decode(weight, bl);
  if (v >= 2) {
    compat_features.decode(bl);
  } else { //upgrade it!
    compat_features.incompat.insert(CEPH_OSD_FEATURE_INCOMPAT_BASE);
  }
  ::decode(clean_thru, bl);
  ::decode(mounted, bl);
}


// -- SnapSet --

void SnapSet::encode(bufferlist& bl) const
{
  __u8 v = 1;
  ::encode(v, bl);
  ::encode(seq, bl);
  ::encode(head_exists, bl);
  ::encode(snaps, bl);
  ::encode(clones, bl);
  ::encode(clone_overlap, bl);
  ::encode(clone_size, bl);
}

void SnapSet::decode(bufferlist::iterator& bl)
{
  __u8 v;
  ::decode(v, bl);
  ::decode(seq, bl);
  ::decode(head_exists, bl);
  ::decode(snaps, bl);
  ::decode(clones, bl);
  ::decode(clone_overlap, bl);
  ::decode(clone_size, bl);
}

ostream& operator<<(ostream& out, const SnapSet& cs)
{
  return out << cs.seq << "=" << cs.snaps << ":"
	     << cs.clones
	     << (cs.head_exists ? "+head":"");
}


// -- watch_info_t --

void watch_info_t::encode(bufferlist& bl) const
{
  const __u8 v = 2;
  ::encode(v, bl);
  ::encode(cookie, bl);
  ::encode(timeout_seconds, bl);
}

void watch_info_t::decode(bufferlist::iterator& bl)
{
  __u8 v;
  ::decode(v, bl);
  ::decode(cookie, bl);
  if (v < 2) {
    uint64_t ver;
    ::decode(ver, bl);
  }
  ::decode(timeout_seconds, bl);
}


// -- object_info_t --

void object_info_t::copy_user_bits(const object_info_t& other)
{
  // these bits are copied from head->clone.
  size = other.size;
  mtime = other.mtime;
  last_reqid = other.last_reqid;
  truncate_seq = other.truncate_seq;
  truncate_size = other.truncate_size;
  lost = other.lost;
  category = other.category;
}

void object_info_t::encode(bufferlist& bl) const
{
  const __u8 v = 5;
  ::encode(v, bl);
  ::encode(soid, bl);
  ::encode(oloc, bl);
  ::encode(category, bl);
  ::encode(version, bl);
  ::encode(prior_version, bl);
  ::encode(last_reqid, bl);
  ::encode(size, bl);
  ::encode(mtime, bl);
  if (soid.snap == CEPH_NOSNAP)
    ::encode(wrlock_by, bl);
  else
    ::encode(snaps, bl);
  ::encode(truncate_seq, bl);
  ::encode(truncate_size, bl);
  ::encode(lost, bl);
  ::encode(watchers, bl);
  ::encode(user_version, bl);
}

void object_info_t::decode(bufferlist::iterator& bl)
{
  __u8 v;
  ::decode(v, bl);
  ::decode(soid, bl);
  if (v >= 2)
    ::decode(oloc, bl);
  if (v >= 5)
    ::decode(category, bl);
  ::decode(version, bl);
  ::decode(prior_version, bl);
  ::decode(last_reqid, bl);
  ::decode(size, bl);
  ::decode(mtime, bl);
  if (soid.snap == CEPH_NOSNAP)
    ::decode(wrlock_by, bl);
  else
    ::decode(snaps, bl);
  ::decode(truncate_seq, bl);
  ::decode(truncate_size, bl);
  if (v >= 3)
    ::decode(lost, bl);
  else
    lost = false;
  if (v >= 4) {
    ::decode(watchers, bl);
    ::decode(user_version, bl);
  }
}

ostream& operator<<(ostream& out, const object_info_t& oi)
{
  out << oi.soid << "(" << oi.version
      << " " << oi.last_reqid;
  if (oi.soid.snap == CEPH_NOSNAP)
    out << " wrlock_by=" << oi.wrlock_by;
  else
    out << " " << oi.snaps;
  if (oi.lost)
    out << " LOST";
  out << ")";
  return out;
}


// -- ScrubMap --

void ScrubMap::merge_incr(const ScrubMap &l)
{
  assert(valid_through == l.incr_since);
  attrs = l.attrs;
  logbl = l.logbl;
  valid_through = l.valid_through;

  for (map<sobject_t,object>::const_iterator p = l.objects.begin();
       p != l.objects.end();
       p++){
    if (p->second.negative) {
      map<sobject_t,object>::iterator q = objects.find(p->first);
      if (q != objects.end()) {
	objects.erase(q);
      }
    } else {
      objects[p->first] = p->second;
    }
  }
}          

void ScrubMap::encode(bufferlist& bl) const
{
  __u8 struct_v = 1;
  ::encode(struct_v, bl);
  ::encode(objects, bl);
  ::encode(attrs, bl);
  ::encode(logbl, bl);
  ::encode(valid_through, bl);
  ::encode(incr_since, bl);
}

void ScrubMap::decode(bufferlist::iterator& bl)
{
  __u8 struct_v;
  ::decode(struct_v, bl);
  ::decode(objects, bl);
  ::decode(attrs, bl);
  ::decode(logbl, bl);
  ::decode(valid_through, bl);
  ::decode(incr_since, bl);
}


// -- OSDOp --

ostream& operator<<(ostream& out, const OSDOp& op)
{
  out << ceph_osd_op_name(op.op.op);
  if (ceph_osd_op_type_data(op.op.op)) {
    // data extent
    switch (op.op.op) {
    case CEPH_OSD_OP_DELETE:
      break;
    case CEPH_OSD_OP_TRUNCATE:
      out << " " << op.op.extent.offset;
      break;
    case CEPH_OSD_OP_MASKTRUNC:
    case CEPH_OSD_OP_TRIMTRUNC:
      out << " " << op.op.extent.truncate_seq << "@" << (int64_t)op.op.extent.truncate_size;
      break;
    case CEPH_OSD_OP_ROLLBACK:
      out << " " << snapid_t(op.op.snap.snapid);
      break;
    default:
      out << " " << op.op.extent.offset << "~" << op.op.extent.length;
      if (op.op.extent.truncate_seq)
	out << " [" << op.op.extent.truncate_seq << "@" << (int64_t)op.op.extent.truncate_size << "]";
    }
  } else if (ceph_osd_op_type_attr(op.op.op)) {
    // xattr name
    if (op.op.xattr.name_len && op.data.length()) {
      out << " ";
      op.data.write(0, op.op.xattr.name_len, out);
    }
    if (op.op.xattr.value_len)
      out << " (" << op.op.xattr.value_len << ")";
    if (op.op.op == CEPH_OSD_OP_CMPXATTR)
      out << " op " << (int)op.op.xattr.cmp_op << " mode " << (int)op.op.xattr.cmp_mode;
  } else if (ceph_osd_op_type_exec(op.op.op)) {
    // class.method
    if (op.op.cls.class_len && op.data.length()) {
      out << " ";
      op.data.write(0, op.op.cls.class_len, out);
      out << ".";
      op.data.write(op.op.cls.class_len, op.op.cls.method_len, out);
    }
  } else if (ceph_osd_op_type_pg(op.op.op)) {
    switch (op.op.op) {
    case CEPH_OSD_OP_PGLS:
    case CEPH_OSD_OP_PGLS_FILTER:
      out << " cookie " << op.op.pgls.cookie;
      out << " start_epoch " << op.op.pgls.start_epoch;
      break;
    }
  } else if (ceph_osd_op_type_multi(op.op.op)) {
    switch (op.op.op) {
    case CEPH_OSD_OP_CLONERANGE:
      out << " " << op.op.clonerange.offset << "~" << op.op.clonerange.length
	  << " from " << op.soid
	  << " offset " << op.op.clonerange.src_offset;
      break;
    case CEPH_OSD_OP_ASSERT_SRC_VERSION:
      out << " v" << op.op.watch.ver
	  << " of " << op.soid;
      break;
    case CEPH_OSD_OP_SRC_CMPXATTR:
      out << " " << op.soid;
      if (op.op.xattr.name_len && op.data.length()) {
	out << " ";
	op.data.write(0, op.op.xattr.name_len, out);
      }
      if (op.op.xattr.value_len)
	out << " (" << op.op.xattr.value_len << ")";
      if (op.op.op == CEPH_OSD_OP_CMPXATTR)
	out << " op " << (int)op.op.xattr.cmp_op << " mode " << (int)op.op.xattr.cmp_mode;
      break;
    }
  }
  return out;
}
