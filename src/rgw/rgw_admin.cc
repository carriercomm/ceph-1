#include <errno.h>

#include <iostream>
#include <sstream>
#include <string>

using namespace std;

#include "common/config.h"
#include "common/ceph_argparse.h"
#include "common/Formatter.h"
#include "global/global_init.h"
#include "common/errno.h"

#include "common/armor.h"
#include "rgw_user.h"
#include "rgw_access.h"
#include "rgw_acl.h"
#include "rgw_log.h"
#include "rgw_formats.h"
#include "auth/Crypto.h"


#define SECRET_KEY_LEN 40
#define PUBLIC_ID_LEN 20

static XMLFormatter formatter_xml;
static JSONFormatter formatter_json;

void _usage() 
{
  cerr << "usage: radosgw_admin <cmd> [options...]" << std::endl;
  cerr << "commands:\n";
  cerr << "  user create                create a new user\n" ;
  cerr << "  user modify                modify user\n";
  cerr << "  user info                  get user info\n";
  cerr << "  user rm                    remove user\n";
  cerr << "  user suspend               suspend a user\n";
  cerr << "  user enable                reenable user after suspension\n";
  cerr << "  subuser create             create a new subuser\n" ;
  cerr << "  subuser modify             modify subuser\n";
  cerr << "  subuser rm                 remove subuser\n";
  cerr << "  key create                 create access key\n";
  cerr << "  key rm                     remove access key\n";
  cerr << "  buckets list               list buckets\n";
  cerr << "  bucket link                link bucket to specified user\n";
  cerr << "  bucket unlink              unlink bucket from specified user\n";
  cerr << "  pool info                  show pool information\n";
  cerr << "  pool create                generate pool information (requires bucket)\n";
  cerr << "  policy                     read bucket/object policy\n";
  cerr << "  log show                   dump a log from specific object or (bucket + date\n";
  cerr << "                             + pool-id)\n";
  cerr << "  temp remove                remove temporary objects that were created up to\n";
  cerr << "                             specified date (and optional time)\n";
  cerr << "options:\n";
  cerr << "   --uid=<id>                user id\n";
  cerr << "   --subuser=<name>          subuser name\n";
  cerr << "   --access-key=<key>        S3 access key\n";
  cerr << "   --os-user=<group:name>    OpenStack user\n";
  cerr << "   --email=<email>\n";
  cerr << "   --auth_uid=<auid>         librados uid\n";
  cerr << "   --secret=<key>            S3 key\n";
  cerr << "   --os-secret=<key>         OpenStack key\n";
  cerr << "   --gen-access-key          generate random access key\n";
  cerr << "   --gen-secret              generate random secret key\n";
  cerr << "   --access=<access>         Set access permissions for sub-user, should be one\n";
  cerr << "                             of read, write, readwrite, full\n";
  cerr << "   --display-name=<name>\n";
  cerr << "   --bucket=<bucket>\n";
  cerr << "   --object=<object>\n";
  cerr << "   --date=<yyyy-mm-dd>\n";
  cerr << "   --time=<HH:MM:SS>\n";
  cerr << "   --pool-id=<pool-id>\n";
  cerr << "   --format=<format>         specify output format for certain operations: xml,\n";
  cerr << "                             json\n";
  cerr << "   --purge-data              when specified, user removal will also purge all the\n";
  cerr << "                             user data\n";
  generic_client_usage();
}

int usage()
{
  _usage();
  return 1;
}

void usage_exit()
{
  _usage();
  exit(1);
}

enum {
  OPT_NO_CMD = 0,
  OPT_USER_CREATE,
  OPT_USER_INFO,
  OPT_USER_MODIFY,
  OPT_USER_RM,
  OPT_USER_SUSPEND,
  OPT_USER_ENABLE,
  OPT_SUBUSER_CREATE,
  OPT_SUBUSER_MODIFY,
  OPT_SUBUSER_RM,
  OPT_KEY_CREATE,
  OPT_KEY_RM,
  OPT_BUCKETS_LIST,
  OPT_BUCKET_LINK,
  OPT_BUCKET_UNLINK,
  OPT_POLICY,
  OPT_POOL_INFO,
  OPT_POOL_CREATE,
  OPT_LOG_SHOW,
  OPT_TEMP_REMOVE,
};

static uint32_t str_to_perm(const char *str)
{
  if (strcasecmp(str, "read") == 0)
    return RGW_PERM_READ;
  else if (strcasecmp(str, "write") == 0)
    return RGW_PERM_WRITE;
  else if (strcasecmp(str, "readwrite") == 0)
    return RGW_PERM_READ | RGW_PERM_WRITE;
  else if (strcasecmp(str, "full") == 0)
    return RGW_PERM_FULL_CONTROL;

  usage_exit();
  return 0; // unreachable
}

struct rgw_flags_desc {
  uint32_t mask;
  const char *str;
};

static struct rgw_flags_desc rgw_perms[] = {
 { RGW_PERM_FULL_CONTROL, "full-control" },
 { RGW_PERM_READ | RGW_PERM_WRITE, "read-write" },
 { RGW_PERM_READ, "read" },
 { RGW_PERM_WRITE, "write" },
 { RGW_PERM_READ_ACP, "read-acp" },
 { RGW_PERM_WRITE_ACP, "read-acp" },
 { 0, NULL }
};

static void perm_to_str(uint32_t mask, char *buf, int len)
{
  const char *sep = "";
  int pos = 0;
  if (!mask) {
    snprintf(buf, len, "<none>");
    return;
  }
  while (mask) {
    uint32_t orig_mask = mask;
    for (int i = 0; rgw_perms[i].mask; i++) {
      struct rgw_flags_desc *desc = &rgw_perms[i];
      if ((mask & desc->mask) == desc->mask) {
        pos += snprintf(buf + pos, len - pos, "%s%s", sep, desc->str);
        if (pos == len)
          return;
        sep = ", ";
        mask &= ~desc->mask;
        if (!mask)
          return;
      }
    }
    if (mask == orig_mask) // no change
      break;
  }
}

static int get_cmd(const char *cmd, const char *prev_cmd, bool *need_more)
{
  *need_more = false;
  if (strcmp(cmd, "user") == 0 ||
      strcmp(cmd, "subuser") == 0 ||
      strcmp(cmd, "key") == 0 ||
      strcmp(cmd, "buckets") == 0 ||
      strcmp(cmd, "bucket") == 0 ||
      strcmp(cmd, "pool") == 0 ||
      strcmp(cmd, "log") == 0 ||
      strcmp(cmd, "temp") == 0) {
    *need_more = true;
    return 0;
  }

  if (strcmp(cmd, "policy") == 0)
    return OPT_POLICY;

  if (!prev_cmd)
    return -EINVAL;

  if (strcmp(prev_cmd, "user") == 0) {
    if (strcmp(cmd, "create") == 0)
      return OPT_USER_CREATE;
    if (strcmp(cmd, "info") == 0)
      return OPT_USER_INFO;
    if (strcmp(cmd, "modify") == 0)
      return OPT_USER_MODIFY;
    if (strcmp(cmd, "rm") == 0)
      return OPT_USER_RM;
    if (strcmp(cmd, "suspend") == 0)
      return OPT_USER_SUSPEND;
    if (strcmp(cmd, "enable") == 0)
      return OPT_USER_ENABLE;
  } else if (strcmp(prev_cmd, "subuser") == 0) {
    if (strcmp(cmd, "create") == 0)
      return OPT_SUBUSER_CREATE;
    if (strcmp(cmd, "modify") == 0)
      return OPT_SUBUSER_MODIFY;
    if (strcmp(cmd, "rm") == 0)
      return OPT_SUBUSER_RM;
  } else if (strcmp(prev_cmd, "key") == 0) {
    if (strcmp(cmd, "create") == 0)
      return OPT_KEY_CREATE;
    if (strcmp(cmd, "rm") == 0)
      return OPT_KEY_RM;
  } else if (strcmp(prev_cmd, "buckets") == 0) {
    if (strcmp(cmd, "list") == 0)
      return OPT_BUCKETS_LIST;
  } else if (strcmp(prev_cmd, "bucket") == 0) {
    if (strcmp(cmd, "link") == 0)
      return OPT_BUCKET_LINK;
    if (strcmp(cmd, "unlink") == 0)
      return OPT_BUCKET_UNLINK;
  } else if (strcmp(prev_cmd, "log") == 0) {
    if (strcmp(cmd, "show") == 0)
      return OPT_LOG_SHOW;
  } else if (strcmp(prev_cmd, "temp") == 0) {
    if (strcmp(cmd, "remove") == 0)
      return OPT_TEMP_REMOVE;
  } else if (strcmp(prev_cmd, "pool") == 0) {
    if (strcmp(cmd, "info") == 0)
      return OPT_POOL_INFO;
    if (strcmp(cmd, "create") == 0)
      return OPT_POOL_CREATE;
  }

  return -EINVAL;
}

string escape_str(string& src, char c)
{
  int pos = 0;
  string s = src;
  string dest;

  do {
    int new_pos = src.find(c, pos);
    if (new_pos >= 0) {
      dest += src.substr(pos, new_pos - pos);
      dest += "\\";
      dest += c;
    } else {
      dest += src.substr(pos);
      return dest;
    }
    pos = new_pos + 1;
  } while (pos < (int)src.size());

  return dest;
}

static void show_user_info( RGWUserInfo& info)
{
  map<string, RGWAccessKey>::iterator kiter;
  map<string, RGWSubUser>::iterator uiter;

  cout << "User ID: " << info.user_id << std::endl;
  cout << "RADOS UID: " << info.auid << std::endl;
  cout << "Keys:" << std::endl;
  for (kiter = info.access_keys.begin(); kiter != info.access_keys.end(); ++kiter) {
    RGWAccessKey& k = kiter->second;
    cout << " User: " << info.user_id << (k.subuser.empty() ? "" : ":") << k.subuser << std::endl;
    cout << "  Access Key: " << k.id << std::endl;
    cout << "  Secret Key: " << k.key << std::endl;
  }
  cout << "Users: " << std::endl;
  for (uiter = info.subusers.begin(); uiter != info.subusers.end(); ++uiter) {
    RGWSubUser& u = uiter->second;
    cout << " Name: " << info.user_id << ":" << u.name << std::endl;
    char buf[256];
    perm_to_str(u.perm_mask, buf, sizeof(buf));
    cout << " Permissions: " << buf << std::endl;
  }
  cout << "Display Name: " << info.display_name << std::endl;
  cout << "Email: " << info.user_email << std::endl;
  cout << "OpenStack User: " << (info.openstack_name.size() ? info.openstack_name : "<undefined>")<< std::endl;
  cout << "OpenStack Key: " << (info.openstack_key.size() ? info.openstack_key : "<undefined>")<< std::endl;
}

static int create_bucket(string& bucket, string& user_id, string& display_name, uint64_t auid)
{
  RGWAccessControlPolicy policy, old_policy;
  map<string, bufferlist> attrs;
  bufferlist aclbl;
  string no_oid;
  rgw_obj obj(bucket, no_oid);

  int ret;

  // defaule policy (private)
  policy.create_default(user_id, display_name);
  policy.encode(aclbl);

  ret = rgwstore->create_bucket(user_id, bucket, attrs, false, auid);
  if (ret && ret != -EEXIST)   
    goto done;

  ret = rgwstore->set_attr(NULL, obj, RGW_ATTR_ACL, aclbl);
  if (ret < 0) {
    cerr << "couldn't set acl on bucket" << std::endl;
  }

  ret = rgw_add_bucket(user_id, bucket);

  RGW_LOG(20) << "ret=" << ret << dendl;

  if (ret == -EEXIST)
    ret = 0;
done:
  return ret;
}

static void remove_old_indexes(RGWUserInfo& old_info, RGWUserInfo new_info)
{
  int ret;
  bool success = true;

  if (!old_info.user_id.empty() && old_info.user_id.compare(new_info.user_id) != 0) {
    ret = rgw_remove_uid_index(old_info.user_id);
    if (ret < 0 && ret != -ENOENT) {
      cerr << "ERROR: could not remove index for uid " << old_info.user_id << " return code: " << ret << std::endl;
      success = false;
    }
  }

  if (!old_info.user_email.empty() &&
      old_info.user_email.compare(new_info.user_email) != 0) {
    ret = rgw_remove_email_index(new_info.user_id, old_info.user_email);
    if (ret < 0 && ret != -ENOENT) {
      cerr << "ERROR: could not remove index for email " << old_info.user_email << " return code: " << ret << std::endl;
      success = false;
    }
  }

  if (!old_info.openstack_name.empty() &&
      old_info.openstack_name.compare(new_info.openstack_name) != 0) {
    ret = rgw_remove_openstack_name_index(new_info.user_id, old_info.openstack_name);
    if (ret < 0 && ret != -ENOENT) {
      cerr << "ERROR: could not remove index for openstack_name " << old_info.openstack_name << " return code: " << ret << std::endl;
      success = false;
    }
  }

  /* we're not removing access keys here.. keys are removed explicitly using the key rm command and removing the old key
     index is handled there */

  if (!success)
    cerr << "ERROR: this should be fixed manually!" << std::endl;
}

class IntentLogNameFilter : public RGWAccessListFilter
{
  string prefix;
  bool filter_exact_date;
public:
  IntentLogNameFilter(const char *date, struct tm *tm) {
    prefix = date;
    filter_exact_date = !(tm->tm_hour || tm->tm_min || tm->tm_sec); /* if time was specified and is not 00:00:00
                                                                       we should look at objects from that date */
  }
  bool filter(string& name, string& key) {
    if (filter_exact_date)
      return name.compare(prefix) < 0;
    else
      return name.compare(0, prefix.size(), prefix) <= 0;
  }
};

enum IntentFlags { // bitmask
  I_DEL_OBJ = 1,
};

int process_intent_log(string& bucket, string& oid, time_t epoch, IntentFlags flags, bool purge)
{
  uint64_t size;
  rgw_obj obj(bucket, oid);
  int r = rgwstore->obj_stat(NULL, obj, &size, NULL);
  if (r < 0) {
    cerr << "error while doing stat on " << bucket << ":" << oid
	 << " " << cpp_strerror(-r) << std::endl;
    return -r;
  }
  bufferlist bl;
  r = rgwstore->read(NULL, obj, 0, size, bl);
  if (r < 0) {
    cerr << "error while reading from " <<  bucket << ":" << oid
	 << " " << cpp_strerror(-r) << std::endl;
    return -r;
  }

  bufferlist::iterator iter = bl.begin();
  string id;
  bool complete = true;
  try {
    while (!iter.end()) {
      struct rgw_intent_log_entry entry;
      ::decode(entry, iter);
      if (entry.op_time.sec() > epoch) {
        cerr << "skipping entry for obj=" << obj << " entry.op_time=" << entry.op_time.sec() << " requested epoch=" << epoch << std::endl;
        cerr << "skipping intent log" << std::endl; // no use to continue
        complete = false;
        break;
      }
      switch (entry.intent) {
      case DEL_OBJ:
        if (!flags & I_DEL_OBJ) {
          complete = false;
          break;
        }
        r = rgwstore->delete_obj(NULL, id, entry.obj);
        if (r < 0 && r != -ENOENT) {
          cerr << "failed to remove obj: " << entry.obj << std::endl;
          complete = false;
        }
        break;
      default:
        complete = false;
      }
    }
  } catch (buffer::error& err) {
    cerr << "failed to decode intent log entry in " << bucket << ":" << oid << std::endl;
    complete = false;
  }

  if (complete) {
    rgw_obj obj(bucket, oid);
    cout << "completed intent log: " << obj << (purge ? ", purging it" : "") << std::endl;
    if (purge) {
      r = rgwstore->delete_obj(NULL, id, obj);
      if (r < 0)
        cerr << "failed to remove obj: " << obj << std::endl;
    }
  }

  return 0;
}


int main(int argc, char **argv) 
{
  DEFINE_CONF_VARS(_usage);
  vector<const char*> args;
  argv_to_vec(argc, (const char **)argv, args);
  env_to_vec(args);

  global_init(args, CEPH_ENTITY_TYPE_CLIENT, CODE_ENVIRONMENT_UTILITY, 0);
  common_init_finish(g_ceph_context);

  const char *user_id = 0;
  const char *access_key = 0;
  const char *secret_key = 0;
  const char *user_email = 0;
  const char *display_name = 0;
  const char *bucket = 0;
  const char *object = 0;
  const char *openstack_user = 0;
  const char *openstack_key = 0;
  const char *date = 0;
  const char *time = 0;
  const char *subuser = 0;
  const char *access = 0;
  uint32_t perm_mask = 0;
  uint64_t auid = -1;
  RGWUserInfo info;
  RGWAccess *store;
  const char *prev_cmd = NULL;
  int opt_cmd = OPT_NO_CMD;
  bool need_more;
  bool gen_secret;
  bool gen_key;
  char secret_key_buf[SECRET_KEY_LEN + 1];
  char public_id_buf[PUBLIC_ID_LEN + 1];
  bool user_modify_op;
  int pool_id = -1;
  const char *format = 0;
  Formatter *formatter = &formatter_xml;
  bool purge_data = false;

  FOR_EACH_ARG(args) {
    if (CEPH_ARGPARSE_EQ("help", 'h')) {
      usage();
      return 0;
    } else if (CEPH_ARGPARSE_EQ("uid", 'i')) {
      CEPH_ARGPARSE_SET_ARG_VAL(&user_id, OPT_STR);
    } else if (CEPH_ARGPARSE_EQ("access-key", '\0')) {
      CEPH_ARGPARSE_SET_ARG_VAL(&access_key, OPT_STR);
    } else if (CEPH_ARGPARSE_EQ("subuser", '\0')) {
      CEPH_ARGPARSE_SET_ARG_VAL(&subuser, OPT_STR);
    } else if (CEPH_ARGPARSE_EQ("secret", 's')) {
      CEPH_ARGPARSE_SET_ARG_VAL(&secret_key, OPT_STR);
    } else if (CEPH_ARGPARSE_EQ("email", 'e')) {
      CEPH_ARGPARSE_SET_ARG_VAL(&user_email, OPT_STR);
    } else if (CEPH_ARGPARSE_EQ("display-name", 'n')) {
      CEPH_ARGPARSE_SET_ARG_VAL(&display_name, OPT_STR);
    } else if (CEPH_ARGPARSE_EQ("bucket", 'b')) {
      CEPH_ARGPARSE_SET_ARG_VAL(&bucket, OPT_STR);
    } else if (CEPH_ARGPARSE_EQ("object", 'o')) {
      CEPH_ARGPARSE_SET_ARG_VAL(&object, OPT_STR);
    } else if (CEPH_ARGPARSE_EQ("gen-access-key", '\0')) {
      CEPH_ARGPARSE_SET_ARG_VAL(&gen_key, OPT_BOOL);
    } else if (CEPH_ARGPARSE_EQ("gen-secret", '\0')) {
      CEPH_ARGPARSE_SET_ARG_VAL(&gen_secret, OPT_BOOL);
    } else if (CEPH_ARGPARSE_EQ("auth-uid", 'a')) {
      CEPH_ARGPARSE_SET_ARG_VAL(&auid, OPT_LONGLONG);
    } else if (CEPH_ARGPARSE_EQ("os-user", '\0')) {
      CEPH_ARGPARSE_SET_ARG_VAL(&openstack_user, OPT_STR);
    } else if (CEPH_ARGPARSE_EQ("os-secret", '\0')) {
      CEPH_ARGPARSE_SET_ARG_VAL(&openstack_key, OPT_STR);
    } else if (CEPH_ARGPARSE_EQ("date", '\0')) {
      CEPH_ARGPARSE_SET_ARG_VAL(&date, OPT_STR);
    } else if (CEPH_ARGPARSE_EQ("time", '\0')) {
      CEPH_ARGPARSE_SET_ARG_VAL(&time, OPT_STR);
    } else if (CEPH_ARGPARSE_EQ("access", '\0')) {
      CEPH_ARGPARSE_SET_ARG_VAL(&access, OPT_STR);
      perm_mask = str_to_perm(access);
    } else if (CEPH_ARGPARSE_EQ("pool-id", '\0')) {
      CEPH_ARGPARSE_SET_ARG_VAL(&pool_id, OPT_INT);
      if (pool_id < 0) {
        cerr << "bad pool-id: " << pool_id << std::endl;
        return usage();
      }
    } else if (CEPH_ARGPARSE_EQ("format", '\0')) {
      CEPH_ARGPARSE_SET_ARG_VAL(&format, OPT_STR);
    } else if (CEPH_ARGPARSE_EQ("purge-data", '\0')) {
      CEPH_ARGPARSE_SET_ARG_VAL(&purge_data, OPT_BOOL);
    } else {
      if (!opt_cmd) {
        opt_cmd = get_cmd(CEPH_ARGPARSE_VAL, prev_cmd, &need_more);
        if (opt_cmd < 0) {
          cerr << "unrecognized arg " << args[i] << std::endl;
          return usage();
        }
        if (need_more) {
          prev_cmd = CEPH_ARGPARSE_VAL;
          continue;
        }
      } else {
        cerr << "unrecognized arg " << args[i] << std::endl;
        return usage();
      }
    }
  }

  if (opt_cmd == OPT_NO_CMD)
    return usage();

  if (format) {
    if (strcmp(format, "xml") == 0)
      formatter = &formatter_xml;
    else if (strcmp(format, "json") == 0)
      formatter = &formatter_json;
    else {
      cerr << "unrecognized format: " << format << std::endl;
      return usage();
    }
  }

  if (subuser) {
    char *suser = strdup(subuser);
    char *p = strchr(suser, ':');
    if (!p) {
      free(suser);
    } else {
      *p = '\0';
      if (user_id) {
        if (strcmp(user_id, suser) != 0) {
          cerr << "bad subuser " << subuser << " for uid " << user_id << std::endl;
          return 1;
        }
      } else
        user_id = suser;
      subuser = p + 1;
    }
  }

  if (opt_cmd == OPT_KEY_RM && !access_key) {
    cerr << "error: access key was not specified" << std::endl;
    return usage();
  }

  user_modify_op = (opt_cmd == OPT_USER_MODIFY || opt_cmd == OPT_SUBUSER_MODIFY ||
                    opt_cmd == OPT_SUBUSER_CREATE || opt_cmd == OPT_SUBUSER_RM ||
                    opt_cmd == OPT_KEY_CREATE || opt_cmd == OPT_KEY_RM || opt_cmd == OPT_USER_RM);

  RGWStoreManager store_manager;
  store = store_manager.init("rados", g_ceph_context);
  if (!store) {
    cerr << "couldn't init storage provider" << std::endl;
    return 5; //EIO
  }

  if (opt_cmd != OPT_USER_CREATE && opt_cmd != OPT_LOG_SHOW && !user_id) {
    bool found = false;
    string s;
    if (!found && user_email) {
      s = user_email;
      if (rgw_get_user_info_by_email(s, info) >= 0) {
	found = true;
      } else {
	cerr << "could not find user by specified email" << std::endl;
      }
    }
    if (!found && access_key) {
      s = access_key;
      if (rgw_get_user_info_by_access_key(s, info) >= 0) {
	found = true;
      } else {
	cerr << "could not find user by specified access key" << std::endl;
      }
    }
    if (!found && openstack_user) {
      s = openstack_user;
      if (rgw_get_user_info_by_openstack(s, info) >= 0) {
	found = true;
      } else
        cerr << "could not find user by specified openstack username" << std::endl;
    }
    if (found)
      user_id = info.user_id.c_str();
  }


  if (user_modify_op || opt_cmd == OPT_USER_CREATE ||
      opt_cmd == OPT_USER_INFO || opt_cmd == OPT_BUCKET_UNLINK || opt_cmd == OPT_BUCKET_LINK ||
      opt_cmd == OPT_USER_SUSPEND || opt_cmd == OPT_USER_ENABLE) {
    if (!user_id) {
      cerr << "user_id was not specified, aborting" << std::endl;
      return usage();
    }

    string user_id_str = user_id;

    bool found = (rgw_get_user_info_by_uid(user_id_str, info) >= 0);

    if (opt_cmd == OPT_USER_CREATE) {
      if (found) {
        cerr << "error: user already exists" << std::endl;
        return 1;
      }
    } else if (!found) {
      cerr << "error reading user info, aborting" << std::endl;
      return 1;
    }
  }

  if (opt_cmd == OPT_SUBUSER_CREATE || opt_cmd == OPT_SUBUSER_MODIFY ||
      opt_cmd == OPT_SUBUSER_RM) {
    if (!subuser) {
      cerr << "subuser creation was requires specifying subuser name" << std::endl;
      return 1;
    }
    map<string, RGWSubUser>::iterator iter = info.subusers.find(subuser);
    bool found = (iter != info.subusers.end());
    if (opt_cmd == OPT_SUBUSER_CREATE) {
      if (found) {
        cerr << "error: subuser already exists" << std::endl;
        return 1;
      }
    } else if (!found) {
      cerr << "error: subuser doesn't exist" << std::endl;
      return 1;
    }
  }

  bool keys_not_requested = (!access_key && !secret_key && !gen_secret && !gen_key &&
                             opt_cmd != OPT_KEY_CREATE);

  if (opt_cmd == OPT_USER_CREATE || (user_modify_op && !keys_not_requested)) {
    int ret;

    if (opt_cmd == OPT_USER_CREATE && !display_name) {
      cerr << "display name was not specified, aborting" << std::endl;
      return 0;
    }

    if (!secret_key || gen_secret) {
      ret = gen_rand_base64(secret_key_buf, sizeof(secret_key_buf));
      if (ret < 0) {
        cerr << "aborting" << std::endl;
        return 1;
      }
      secret_key = secret_key_buf;
    }
    if (!access_key || gen_key) {
      RGWUserInfo duplicate_check;
      string duplicate_check_id;
      do {
	ret = gen_rand_alphanumeric_upper(public_id_buf, sizeof(public_id_buf));
	if (ret < 0) {
	  cerr << "aborting" << std::endl;
	  return 1;
	}
	access_key = public_id_buf;
	duplicate_check_id = access_key;
      } while (!rgw_get_user_info_by_access_key(duplicate_check_id, duplicate_check));
    }
  }

  map<string, RGWAccessKey>::iterator kiter;
  map<string, RGWSubUser>::iterator uiter;
  RGWUserInfo old_info = info;

  int err;
  switch (opt_cmd) {
  case OPT_USER_CREATE:
  case OPT_USER_MODIFY:
  case OPT_SUBUSER_CREATE:
  case OPT_SUBUSER_MODIFY:
  case OPT_KEY_CREATE:
    if (user_id)
      info.user_id = user_id;
    if (access_key && secret_key) {
      RGWAccessKey k;
      k.id = access_key;
      k.key = secret_key;
      if (subuser)
        k.subuser = subuser;
      info.access_keys[access_key] = k;
   } else if (access_key || secret_key) {
      cerr << "access key modification requires both access key and secret key" << std::endl;
      return 1;
    }
    if (display_name)
      info.display_name = display_name;
    if (user_email)
      info.user_email = user_email;
    if (auid != (uint64_t)-1)
      info.auid = auid;
    if (openstack_user)
      info.openstack_name = openstack_user;
    if (openstack_key)
      info.openstack_key = openstack_key;
    if (subuser) {
      RGWSubUser u;
      u.name = subuser;
      u.perm_mask = perm_mask;

      info.subusers[subuser] = u;
    }
    if ((err = rgw_store_user_info(info)) < 0) {
      cerr << "error storing user info: " << cpp_strerror(-err) << std::endl;
      break;
    }

    remove_old_indexes(old_info, info);

    show_user_info(info);
    break;

  case OPT_SUBUSER_RM:
    uiter = info.subusers.find(subuser);
    assert (uiter != info.subusers.end());
    info.subusers.erase(uiter);
    if ((err = rgw_store_user_info(info)) < 0) {
      cerr << "error storing user info: " << cpp_strerror(-err) << std::endl;
      break;
    }
    show_user_info(info);
    break;

  case OPT_KEY_RM:
    kiter = info.access_keys.find(access_key);
    if (kiter == info.access_keys.end()) {
      cerr << "key not found" << std::endl;
    } else {
      rgw_remove_key_index(kiter->second);
      info.access_keys.erase(kiter);
      if ((err = rgw_store_user_info(info)) < 0) {
        cerr << "error storing user info: " << cpp_strerror(-err) << std::endl;
        break;
      }
    }
    show_user_info(info);
    break;

  case OPT_USER_INFO:
    show_user_info(info);
    break;
  }

  if (opt_cmd == OPT_POLICY) {
    bufferlist bl;
    if (!bucket)
      bucket = "";
    if (!object)
      object = "";
    string bucket_str(bucket);
    string object_str(object);
    rgw_obj obj(bucket_str, object_str);
    int ret = store->get_attr(NULL, obj, RGW_ATTR_ACL, bl);

    RGWAccessControlPolicy policy;
    if (ret >= 0) {
      bufferlist::iterator iter = bl.begin();
      policy.decode(iter);
      policy.to_xml(cout);
      cout << std::endl;
    }
  }

  if (opt_cmd == OPT_BUCKETS_LIST) {
    string id;
    RGWAccessHandle handle;

    if (user_id) {
      RGWUserBuckets buckets;
      if (rgw_read_user_buckets(user_id, buckets, false) < 0) {
        cout << "could not get buckets for uid " << user_id << std::endl;
      } else {
        map<string, RGWBucketEnt>& m = buckets.get_buckets();
        map<string, RGWBucketEnt>::iterator iter;

        for (iter = m.begin(); iter != m.end(); ++iter) {
          RGWBucketEnt obj = iter->second;
          cout << obj.name << std::endl;
        }
      }
    } else {
      if (store->list_buckets_init(id, &handle) < 0) {
        cout << "list-buckets: no entries found" << std::endl;
      } else {
        RGWObjEnt obj;
        cout << "listing all buckets" << std::endl;
        while (store->list_buckets_next(id, obj, &handle) >= 0) {
          cout << obj.name << std::endl;
        }
      }
    }
  }

  if (opt_cmd == OPT_BUCKET_LINK) {
    if (!bucket) {
      cerr << "bucket name was not specified" << std::endl;
      return usage();
    }
    string bucket_str(bucket);
    string uid_str(user_id);
    
    string no_oid;
    bufferlist aclbl;
    rgw_obj obj(bucket_str, no_oid);

    int r = rgwstore->get_attr(NULL, obj, RGW_ATTR_ACL, aclbl);
    if (r >= 0) {
      RGWAccessControlPolicy policy;
      ACLOwner owner;
      try {
       bufferlist::iterator iter = aclbl.begin();
       ::decode(policy, iter);
       owner = policy.get_owner();
      } catch (buffer::error& err) {
	dout(10) << "couldn't decode policy" << dendl;
	return -EINVAL;
      }
      cout << "bucket is linked to user '" << owner.get_id() << "'.. unlinking" << std::endl;
      r = rgw_remove_bucket(owner.get_id(), bucket_str, false);
      if (r < 0) {
	cerr << "could not unlink policy from user '" << owner.get_id() << "'" << std::endl;
	return r;
      }
    }

    r = create_bucket(bucket_str, uid_str, info.display_name, info.auid);
    if (r < 0)
        cerr << "error linking bucket to user: r=" << r << std::endl;
    return -r;
  }

  if (opt_cmd == OPT_BUCKET_UNLINK) {
    if (!bucket) {
      cerr << "bucket name was not specified" << std::endl;
      return usage();
    }

    string bucket_str(bucket);
    int r = rgw_remove_bucket(user_id, bucket_str, false);
    if (r < 0)
      cerr << "error unlinking bucket " <<  cpp_strerror(-r) << std::endl;
    return -r;
  }

  if (opt_cmd == OPT_TEMP_REMOVE) {
    if (!date) {
      cerr << "date wasn't specified" << std::endl;
      return usage();
    }

    struct tm tm;

    string format = "%Y-%m-%d";
    string datetime = date;
    if (datetime.size() != 10) {
      cerr << "bad date format" << std::endl;
      return -EINVAL;
    }

    if (time) {
      string time_str = time;
      if (time_str.size() != 5 && time_str.size() != 8) {
        cerr << "bad time format" << std::endl;
        return -EINVAL;
      }
      format.append(" %H:%M:%S");
      datetime.append(time);
    }
    const char *s = strptime(datetime.c_str(), format.c_str(), &tm);
    if (s && *s) {
      cerr << "failed to parse date/time" << std::endl;
      return -EINVAL;
    }
    time_t epoch = mktime(&tm);
    string bucket = RGW_INTENT_LOG_BUCKET_NAME;
    string prefix, delim, marker;
    vector<RGWObjEnt> objs;
    map<string, bool> common_prefixes;
    string ns;
    string id;

    int max = 1000;
    bool is_truncated;
    IntentLogNameFilter filter(date, &tm);
    do {
      int r = store->list_objects(id, bucket, max, prefix, delim, marker,
                          objs, common_prefixes, false, ns,
                          &is_truncated, &filter);
      if (r == -ENOENT)
        break;
      if (r < 0) {
        cerr << "failed to list objects" << std::endl;
      }
      vector<RGWObjEnt>::iterator iter;
      for (iter = objs.begin(); iter != objs.end(); ++iter) {
        cout << "processing intent log " << (*iter).name << std::endl;
        process_intent_log(bucket, (*iter).name, epoch, I_DEL_OBJ, true);
      }
    } while (is_truncated);
  }

  if (opt_cmd == OPT_LOG_SHOW) {
    if (!object && (!date || !bucket || pool_id < 0)) {
      cerr << "object or (at least one of date, bucket, pool-id) were not specified" << std::endl;
      return usage();
    }

    string log_bucket = RGW_LOG_BUCKET_NAME;
    string oid;
    if (object) {
      oid = object;
    } else {
      char buf[16];
      snprintf(buf, sizeof(buf), "%d", pool_id);
      oid = date;
      oid += "-";
      oid += buf;
      oid += "-";
      oid += string(bucket);
    }

    uint64_t size;
    rgw_obj obj(log_bucket, oid);
    int r = store->obj_stat(NULL, obj, &size, NULL);
    if (r < 0) {
      cerr << "error while doing stat on " <<  log_bucket << ":" << oid
	   << " " << cpp_strerror(-r) << std::endl;
      return -r;
    }
    bufferlist bl;
    r = store->read(NULL, obj, 0, size, bl);
    if (r < 0) {
      cerr << "error while reading from " <<  log_bucket << ":" << oid
	   << " " << cpp_strerror(-r) << std::endl;
      return -r;
    }

    bufferlist::iterator iter = bl.begin();

    struct rgw_log_entry entry;
    const char *delim = " ";

    if (format) {
      formatter->reset();
      formatter->open_array_section("Log");
    }

    while (!iter.end()) {
      ::decode(entry, iter);

      uint64_t total_time =  entry.total_time.sec() * 1000000LL * entry.total_time.usec();

      if (!format) { // for now, keeping backward compatibility a bit
        cout << (entry.owner.size() ? entry.owner : "-" ) << delim
             << entry.bucket << delim
             << entry.time << delim
             << entry.remote_addr << delim
             << entry.user << delim
             << entry.op << delim
             << "\"" << escape_str(entry.uri, '"') << "\"" << delim
             << entry.http_status << delim
             << "\"" << entry.error_code << "\"" << delim
             << entry.bytes_sent << delim
             << entry.bytes_received << delim
             << entry.obj_size << delim
             << total_time << delim
             << "\"" << escape_str(entry.user_agent, '"') << "\"" << delim
             << "\"" << escape_str(entry.referrer, '"') << "\"" << std::endl;
      } else {
        formatter->open_object_section("LogEntry");
        formatter->dump_format("Bucket", "%s", entry.bucket.c_str());

        stringstream ss;
        ss << entry.time;
        string s = ss.str();

        formatter->dump_format("Time", "%s", s.c_str());
        formatter->dump_format("RemoteAddr", "%s", entry.remote_addr.c_str());
        formatter->dump_format("User", "%s", entry.user.c_str());
        formatter->dump_format("Operation", "%s", entry.op.c_str());
        formatter->dump_format("URI", "%s", entry.uri.c_str());
        formatter->dump_format("HttpStatus", "%s", entry.http_status.c_str());
        formatter->dump_format("ErrorCode", "%s", entry.error_code.c_str());
        formatter->dump_format("BytesSent", "%lld", entry.bytes_sent);
        formatter->dump_format("BytesReceived", "%lld", entry.bytes_received);
        formatter->dump_format("ObjectSize", "%lld", entry.obj_size);
        formatter->dump_format("TotalTime", "%lld", total_time);
        formatter->dump_format("UserAgent", "%s",  entry.user_agent.c_str());
        formatter->dump_format("Referrer", "%s",  entry.referrer.c_str());
        formatter->close_section();
        formatter->flush(cout);
      }
    }

    if (format) {
      formatter->close_section();
      formatter->flush(cout);
    }

  }

  if (opt_cmd == OPT_USER_RM) {
    rgw_delete_user(info, purge_data);
  }

  if (opt_cmd == OPT_POOL_INFO) {
    RGWPoolInfo info;
    int ret = rgw_retrieve_pool_info(pool_id, info);
    if (ret < 0) {
      cerr << "could not retrieve pool info for pool_id=" << pool_id << std::endl;
      return ret;
    }
    formatter->reset();
    formatter->open_object_section("Pool");
    formatter->dump_int("ID", pool_id);
    formatter->dump_format("Bucket", "%s", info.bucket.c_str());
    formatter->dump_format("Owner", "%s", info.owner.c_str());
    formatter->close_section();
    formatter->flush(cout);
  }

  if (opt_cmd == OPT_POOL_CREATE) {
    if (!bucket)
      return usage();
    string bucket_str(bucket);
    string no_object;
    int ret;
    bufferlist bl;
    rgw_obj obj(bucket_str, no_object);

    ret = rgwstore->get_attr(NULL, obj, RGW_ATTR_ACL, bl);
    if (ret < 0) {
      RGW_LOG(0) << "can't read bucket acls: " << ret << dendl;
      return ret;
    }
    RGWAccessControlPolicy policy;
    bufferlist::iterator iter = bl.begin();
    policy.decode(iter);

    RGWPoolInfo info;
    info.bucket = bucket;
    info.owner = policy.get_owner().get_id();

    int pool_id = rgwstore->get_bucket_id(bucket_str);
    ret = rgw_store_pool_info(pool_id, info);
    if (ret < 0) {
      RGW_LOG(0) << "can't store pool info: pool_id=" << pool_id << " ret=" << ret << dendl;
      return ret;
    }
  }

 if (opt_cmd == OPT_USER_SUSPEND || opt_cmd == OPT_USER_ENABLE) {
    string id;
    __u8 disable = (opt_cmd == OPT_USER_SUSPEND ? 1 : 0);

    if (!user_id) {
      cerr << "uid was not specified" << std::endl;
      return usage();
    }
    RGWUserBuckets buckets;
    if (rgw_read_user_buckets(user_id, buckets, false) < 0) {
      cout << "could not get buckets for uid " << user_id << std::endl;
    }
    map<string, RGWBucketEnt>& m = buckets.get_buckets();
    map<string, RGWBucketEnt>::iterator iter;

    int ret;
    info.suspended = disable;
    ret = rgw_store_user_info(info);
    if (ret < 0) {
      cerr << "ERROR: failed to store user info user=" << user_id << " ret=" << ret << std::endl;
      return 1;
    }
     
    if (disable)
      RGW_LOG(0) << "disabling user buckets" << dendl;
    else
      RGW_LOG(0) << "enabling user buckets" << dendl;

    vector<string> bucket_names;
    for (iter = m.begin(); iter != m.end(); ++iter) {
      RGWBucketEnt obj = iter->second;
      bucket_names.push_back(obj.name);
    }
    if (disable)
      ret = rgwstore->disable_buckets(bucket_names);
    else
      ret = rgwstore->enable_buckets(bucket_names, info.auid);
    if (ret < 0) {
      cerr << "ERROR: failed to change pool" << std::endl;
      return 1;
    }
  } 

  return 0;
}
