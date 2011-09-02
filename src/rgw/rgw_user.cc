#include <errno.h>

#include <string>
#include <map>

#include "common/errno.h"
#include "rgw_access.h"
#include "rgw_acl.h"

#include "include/types.h"
#include "rgw_user.h"

using namespace std;

static string ui_key_bucket = USER_INFO_BUCKET_NAME;
static string ui_email_bucket = USER_INFO_EMAIL_BUCKET_NAME;
static string ui_openstack_bucket = USER_INFO_OPENSTACK_BUCKET_NAME;
static string ui_uid_bucket = USER_INFO_UID_BUCKET_NAME;

static string pi_pool_bucket = POOL_INFO_BUCKET_NAME;

string rgw_root_bucket = RGW_ROOT_BUCKET;

#define READ_CHUNK_LEN (16 * 1024)

/**
 * Get the anonymous (ie, unauthenticated) user info.
 */
void rgw_get_anon_user(RGWUserInfo& info)
{
  info.user_id = RGW_USER_ANON_ID;
  info.display_name.clear();
  info.access_keys.clear();
}

bool rgw_user_is_authenticated(RGWUserInfo& info)
{
  return (info.user_id != RGW_USER_ANON_ID);
}

static int put_obj(string& uid, string& bucket, string& oid, const char *data, size_t size)
{
  map<string,bufferlist> attrs;

  rgw_obj obj(bucket, oid);

  int ret = rgwstore->put_obj(NULL, uid, obj, data, size, NULL, attrs);

  if (ret == -ENOENT) {
    ret = rgwstore->create_bucket(uid, bucket, attrs);
    if (ret >= 0)
      ret = rgwstore->put_obj(NULL, uid, obj, data, size, NULL, attrs);
  }

  return ret;
}

static int get_obj(string& bucket, string& key, bufferlist& bl)
{
  int ret;
  char *data = NULL;
  struct rgw_err err;
  RGWUID uid;
  void *handle = NULL;
  bufferlist::iterator iter;
  int request_len = READ_CHUNK_LEN;
  rgw_obj obj(bucket, key);
  ret = rgwstore->prepare_get_obj(NULL, obj, 0, NULL, NULL, NULL,
                                  NULL, NULL, NULL, NULL, NULL, NULL, &handle, &err);
  if (ret < 0)
    return ret;

  do {
    ret = rgwstore->get_obj(NULL, &handle, obj, &data, 0, request_len - 1);
    if (ret < 0)
      goto done;
    if (ret < request_len)
      break;
    free(data);
    request_len *= 2;
  } while (true);

  bl.append(data, ret);
  free(data);

  ret = 0;
done:
  rgwstore->finish_get_obj(&handle);
  return ret;
}

/**
 * Save the given user information to storage.
 * Returns: 0 on success, -ERR# on failure.
 */
int rgw_store_user_info(RGWUserInfo& info)
{
  bufferlist bl;
  info.encode(bl);
  string md5;
  int ret;
  map<string,bufferlist> attrs;

  if (info.openstack_name.size()) {
    /* check if openstack mapping exists */
    RGWUserInfo inf;
    int r = rgw_get_user_info_by_openstack(info.openstack_name, inf);
    if (r >= 0 && inf.user_id.compare(info.user_id) != 0) {
      RGW_LOG(0) << "can't store user info, openstack id already mapped to another user" << dendl;
      return -EEXIST;
    }
  }

  if (info.access_keys.size()) {
    /* check if access keys already exist */
    RGWUserInfo inf;
    map<string, RGWAccessKey>::iterator iter = info.access_keys.begin();
    for (; iter != info.access_keys.end(); ++iter) {
      RGWAccessKey& k = iter->second;
      int r = rgw_get_user_info_by_access_key(k.id, inf);
      if (r >= 0 && inf.user_id.compare(info.user_id) != 0) {
        RGW_LOG(0) << "can't store user info, access key already mapped to another user" << dendl;
        return -EEXIST;
      }
    }
  }

  bufferlist uid_bl;
  RGWUID ui;
  ui.user_id = info.user_id;
  ::encode(ui, uid_bl);
  ::encode(info, uid_bl);

  ret = put_obj(info.user_id, ui_uid_bucket, info.user_id, uid_bl.c_str(), uid_bl.length());
  if (ret < 0)
    return ret;

  if (info.user_email.size()) {
    ret = put_obj(info.user_id, ui_email_bucket, info.user_email, uid_bl.c_str(), uid_bl.length());
    if (ret < 0)
      return ret;
  }

  if (info.access_keys.size()) {
    map<string, RGWAccessKey>::iterator iter = info.access_keys.begin();
    for (; iter != info.access_keys.end(); ++iter) {
      RGWAccessKey& k = iter->second;
      ret = put_obj(k.id, ui_key_bucket, k.id, uid_bl.c_str(), uid_bl.length());
      if (ret < 0)
        return ret;
    }
  }

  if (info.openstack_name.size())
    ret = put_obj(info.user_id, ui_openstack_bucket, info.openstack_name, uid_bl.c_str(), uid_bl.length());

  return ret;
}

int rgw_get_user_info_from_index(string& key, string& bucket, RGWUserInfo& info)
{
  bufferlist bl;
  RGWUID uid;

  int ret = get_obj(bucket, key, bl);
  if (ret < 0)
    return ret;

  bufferlist::iterator iter = bl.begin();
  ::decode(uid, iter);
  if (!iter.end())
    info.decode(iter);

  return 0;
}

/**
 * Given an email, finds the user info associated with it.
 * returns: 0 on success, -ERR# on failure (including nonexistence)
 */
int rgw_get_user_info_by_uid(string& uid, RGWUserInfo& info)
{
  return rgw_get_user_info_from_index(uid, ui_uid_bucket, info);
}

/**
 * Given an email, finds the user info associated with it.
 * returns: 0 on success, -ERR# on failure (including nonexistence)
 */
int rgw_get_user_info_by_email(string& email, RGWUserInfo& info)
{
  return rgw_get_user_info_from_index(email, ui_email_bucket, info);
}

/**
 * Given an openstack username, finds the user_info associated with it.
 * returns: 0 on success, -ERR# on failure (including nonexistence)
 */
extern int rgw_get_user_info_by_openstack(string& openstack_name, RGWUserInfo& info)
{
  return rgw_get_user_info_from_index(openstack_name, ui_openstack_bucket, info);
}

/**
 * Given an access key, finds the user info associated with it.
 * returns: 0 on success, -ERR# on failure (including nonexistence)
 */
extern int rgw_get_user_info_by_access_key(string& access_key, RGWUserInfo& info)
{
  return rgw_get_user_info_from_index(access_key, ui_key_bucket, info);
}

static void get_buckets_obj(string& user_id, string& buckets_obj_id)
{
    buckets_obj_id = user_id;
    buckets_obj_id += RGW_BUCKETS_OBJ_PREFIX;
}

static int rgw_read_buckets_from_attr(string& user_id, RGWUserBuckets& buckets)
{
  bufferlist bl;
  rgw_obj obj(ui_uid_bucket, user_id);
  int ret = rgwstore->get_attr(NULL, obj, RGW_ATTR_BUCKETS, bl);
  if (ret)
    return ret;

  bufferlist::iterator iter = bl.begin();
  buckets.decode(iter);
  return 0;
}

static void store_buckets(string& user_id, RGWUserBuckets& buckets)
{
  map<string, RGWBucketEnt>& m = buckets.get_buckets();
  map<string, RGWBucketEnt>::iterator iter;
  for (iter = m.begin(); iter != m.end(); ++iter) {
    string bucket_name = iter->first;
    int r = rgw_add_bucket(user_id, bucket_name);
    if (r < 0)
      RGW_LOG(0) << "failed to store bucket information for user " << user_id << " bucket=" << bucket_name << dendl;
  }
}

/**
 * Get all the buckets owned by a user and fill up an RGWUserBuckets with them.
 * Returns: 0 on success, -ERR# on failure.
 */
int rgw_read_user_buckets(string user_id, RGWUserBuckets& buckets, bool need_stats)
{
  int ret;
  buckets.clear();
  if (rgwstore->supports_tmap()) {
    string buckets_obj_id;
    get_buckets_obj(user_id, buckets_obj_id);
    bufferlist bl;
#define LARGE_ENOUGH_LEN (4096 * 1024)
    size_t len = LARGE_ENOUGH_LEN;
    rgw_obj obj(ui_uid_bucket, buckets_obj_id);

    do {
      ret = rgwstore->read(NULL, obj, 0, len, bl);
      if (ret == -ENOENT) {
        /* try to read the old format */
        ret = rgw_read_buckets_from_attr(user_id, buckets);
        if (!ret) {
          store_buckets(user_id, buckets);
          goto done;
        }

        ret = 0;
        return 0;
      }
      if (ret < 0)
        return ret;

      if ((size_t)ret != len)
        break;

      len *= 2;
    } while (1);

    bufferlist::iterator p = bl.begin();
    bufferlist header;
    map<string,bufferlist> m;
    ::decode(header, p);
    ::decode(m, p);
    for (map<string,bufferlist>::iterator q = m.begin(); q != m.end(); q++) {
      bufferlist::iterator iter = q->second.begin();
      RGWBucketEnt bucket;
      ::decode(bucket, iter);
      buckets.add(bucket);
    }
  } else {
    ret = rgw_read_buckets_from_attr(user_id, buckets);
    switch (ret) {
    case 0:
      break;
    case -ENODATA:
      ret = 0;
      return 0;
    default:
      return ret;
    }
  }

done:
  list<string> buckets_list;

  if (need_stats) {
   map<string, RGWBucketEnt>& m = buckets.get_buckets();
   int r = rgwstore->update_containers_stats(m);
   if (r < 0)
     RGW_LOG(0) << "could not get stats for buckets" << dendl;

  }
  return 0;
}

/**
 * Store the set of buckets associated with a user on a n xattr
 * not used with all backends
 * This completely overwrites any previously-stored list, so be careful!
 * Returns 0 on success, -ERR# otherwise.
 */
int rgw_write_buckets_attr(string user_id, RGWUserBuckets& buckets)
{
  bufferlist bl;
  buckets.encode(bl);

  rgw_obj obj(ui_uid_bucket, user_id);

  int ret = rgwstore->set_attr(NULL, obj, RGW_ATTR_BUCKETS, bl);

  return ret;
}

int rgw_add_bucket(string user_id, string bucket_name)
{
  int ret;

  if (rgwstore->supports_tmap()) {
    bufferlist bl;

    RGWBucketEnt new_bucket;
    new_bucket.name = bucket_name;
    new_bucket.size = 0;
    time(&new_bucket.mtime);
    ::encode(new_bucket, bl);

    string buckets_obj_id;
    get_buckets_obj(user_id, buckets_obj_id);

    rgw_obj obj(ui_uid_bucket, buckets_obj_id);
    ret = rgwstore->tmap_create(obj, bucket_name, bl);
    if (ret < 0) {
      RGW_LOG(0) << "error adding bucket to directory: "
		 << cpp_strerror(-ret)<< dendl;
    }
  } else {
    RGWUserBuckets buckets;

    ret = rgw_read_user_buckets(user_id, buckets, false);
    RGWBucketEnt new_bucket;

    switch (ret) {
    case 0:
    case -ENOENT:
    case -ENODATA:
      new_bucket.name = bucket_name;
      new_bucket.size = 0;
      time(&new_bucket.mtime);
      buckets.add(new_bucket);
      ret = rgw_write_buckets_attr(user_id, buckets);
      break;
    default:
      RGW_LOG(10) << "rgw_write_buckets_attr returned " << ret << dendl;
      break;
    }
  }

  return ret;
}

int rgw_remove_bucket(string user_id, string bucket_name, bool purge_data)
{
  int ret;

  if (rgwstore->supports_tmap()) {
    bufferlist bl;

    string buckets_obj_id;
    get_buckets_obj(user_id, buckets_obj_id);

    rgw_obj obj(ui_uid_bucket, buckets_obj_id);
    ret = rgwstore->tmap_del(obj, bucket_name);
    if (ret < 0) {
      RGW_LOG(0) << "error removing bucket from directory: "
		 << cpp_strerror(-ret)<< dendl;
    }
  } else {
    RGWUserBuckets buckets;

    ret = rgw_read_user_buckets(user_id, buckets, false);

    if (ret == 0 || ret == -ENOENT) {
      buckets.remove(bucket_name);
      ret = rgw_write_buckets_attr(user_id, buckets);
    }
  }

  if (ret == 0 && purge_data) {
    vector<string> buckets;
    buckets.push_back(bucket_name);
    ret = rgwstore->purge_buckets(user_id, buckets);
  }

  return ret;
}

int rgw_remove_key_index(RGWAccessKey& access_key)
{
  rgw_obj obj(ui_key_bucket, access_key.id);
  int ret = rgwstore->delete_obj(NULL, access_key.id, obj);
  return ret;
}

int rgw_remove_uid_index(string& uid)
{
  rgw_obj obj(ui_uid_bucket, uid);
  int ret = rgwstore->delete_obj(NULL, uid, obj);
  return ret;
}

int rgw_remove_email_index(string& uid, string& email)
{
  rgw_obj obj(ui_email_bucket, email);
  int ret = rgwstore->delete_obj(NULL, uid, obj);
  return ret;
}

int rgw_remove_openstack_name_index(string& uid, string& openstack_name)
{
  rgw_obj obj(ui_openstack_bucket, openstack_name);
  int ret = rgwstore->delete_obj(NULL, uid, obj);
  return ret;
}

/**
 * delete a user's presence from the RGW system.
 * First remove their bucket ACLs, then delete them
 * from the user and user email pools. This leaves the pools
 * themselves alone, as well as any ACLs embedded in object xattrs.
 */
int rgw_delete_user(RGWUserInfo& info, bool purge_data) {
  RGWUserBuckets user_buckets;
  int ret = rgw_read_user_buckets(info.user_id, user_buckets, false);
  if (ret < 0)
    return ret;

  map<string, RGWBucketEnt>& buckets = user_buckets.get_buckets();
  vector<string> buckets_vec;
  for (map<string, RGWBucketEnt>::iterator i = buckets.begin();
       i != buckets.end();
       ++i) {
    string bucket_name = i->first;
    buckets_vec.push_back(bucket_name);
  }
  map<string, RGWAccessKey>::iterator kiter = info.access_keys.begin();
  for (; kiter != info.access_keys.end(); ++kiter) {
    ret = rgw_remove_key_index(kiter->second);
    if (ret < 0 && ret != -ENOENT)
      RGW_LOG(0) << "ERROR: could not remove " << kiter->first << " (access key object), should be fixed manually (err=" << ret << ")" << dendl;
  }

  rgw_obj uid_obj(ui_uid_bucket, info.user_id);
  ret = rgwstore->delete_obj(NULL, info.user_id, uid_obj);
  if (ret < 0 && ret != -ENOENT)
    RGW_LOG(0) << "ERROR: could not remove " << info.user_id << ":" << uid_obj << ", should be fixed manually (err=" << ret << ")" << dendl;

  string buckets_obj_id;
  get_buckets_obj(info.user_id, buckets_obj_id);
  rgw_obj uid_bucks(ui_uid_bucket, buckets_obj_id);
  ret = rgwstore->delete_obj(NULL, info.user_id, uid_bucks);
  if (ret < 0 && ret != -ENOENT)
    RGW_LOG(0) << "ERROR: could not remove " << info.user_id << ":" << uid_bucks << ", should be fixed manually (err=" << ret << ")" << dendl;
  

  rgw_obj email_obj(ui_email_bucket, info.user_email);
  ret = rgwstore->delete_obj(NULL, info.user_id, email_obj);
  if (ret < 0 && ret != -ENOENT)
    RGW_LOG(0) << "ERROR: could not remove " << info.user_id << ":" << email_obj << ", should be fixed manually (err=" << ret << ")" << dendl;

  if (purge_data) {
    ret = rgwstore->purge_buckets(info.user_id, buckets_vec);
    if (ret < 0)
      RGW_LOG(0) << "ERROR: delete_buckets returned " << ret << dendl;
  }
  return 0;
}


int rgw_store_pool_info(int pool_id, RGWPoolInfo& pool_info)
{
  bufferlist bl;

  ::encode(pool_info, bl);

  string uid;
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", pool_id);
  string pool_id_str(buf);

  int ret = put_obj(uid, pi_pool_bucket, pool_id_str, bl.c_str(), bl.length());
  if (ret < 0) {
    RGW_LOG(0) << "ERROR: could not write to pool=" << pi_pool_bucket << " obj=" << pool_id_str << " ret=" << ret << dendl;
  }
  return ret;
}

int rgw_retrieve_pool_info(int pool_id, RGWPoolInfo& pool_info)
{
  bufferlist bl;

  string uid;
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", pool_id);
  string pool_id_str(buf);

  int ret = get_obj(pi_pool_bucket, pool_id_str, bl);
  if (ret < 0) {
    RGW_LOG(0) << "ERROR: could not read from pool=" << pi_pool_bucket << " obj=" << pool_id_str << " ret=" << ret << dendl;
    return ret;
  }
  bufferlist::iterator iter = bl.begin();
  ::decode(pool_info, iter);

  return 0;
}
