#ifndef CEPH_RGWRADOS_H
#define CEPH_RGWRADOS_H

#include "include/rados/librados.hpp"
#include "rgw_access.h"
#include "rgw_common.h"

class RGWWatcher;

class RGWRados  : public RGWAccess
{
  /** Open the pool used as root for this gateway */
  int open_root_pool_ctx();

  int open_bucket_ctx(std::string& bucket, librados::IoCtx&  io_ctx);

  struct GetObjState {
    librados::IoCtx io_ctx;
    bool sent_data;

    GetObjState() : sent_data(false) {}
  };

  int set_buckets_auid(vector<std::string>& buckets, uint64_t auid);

  RGWWatcher *watcher;
  uint64_t watch_handle;
  librados::IoCtx root_pool_ctx;
  librados::IoCtx control_pool_ctx;

public:
  RGWRados() : watcher(NULL), watch_handle(0) {}

  /** Initialize the RADOS instance and prepare to do other ops */
  virtual int initialize(CephContext *cct);
  /** set up a bucket listing. id is ignored, handle is filled in. */
  virtual int list_buckets_init(std::string& id, RGWAccessHandle *handle);
  /** 
   * get the next bucket in the listing. id is ignored, obj is filled in,
   * handle is updated.
   */
  virtual int list_buckets_next(std::string& id, RGWObjEnt& obj, RGWAccessHandle *handle);

  /** get listing of the objects in a bucket */
  virtual int list_objects(std::string& id, std::string& bucket, int max, std::string& prefix, std::string& delim,
                   std::string& marker, std::vector<RGWObjEnt>& result, map<string, bool>& common_prefixes,
		   bool get_content_type, string& ns, bool *is_truncated, RGWAccessListFilter *filter);

  /**
   * create a bucket with name bucket and the given list of attrs
   * returns 0 on success, -ERR# otherwise.
   */
  virtual int create_bucket(std::string& id, std::string& bucket, map<std::string,bufferlist>& attrs, bool exclusive = true, uint64_t auid = 0);

  /** Write/overwrite an object to the bucket storage. */
  virtual int put_obj_meta(std::string& id, rgw_obj& obj, time_t *mtime,
              map<std::string, bufferlist>& attrs, bool exclusive);
  virtual int put_obj_data(std::string& id, rgw_obj& obj, const char *data,
              off_t ofs, size_t len);
  virtual int aio_put_obj_data(std::string& id, rgw_obj& obj, const char *data,
                               off_t ofs, size_t len, void **handle);
  virtual int aio_wait(void *handle);
  virtual bool aio_completed(void *handle);
  virtual int clone_range(rgw_obj& dst_obj, off_t dst_ofs,
                          rgw_obj& src_obj, off_t src_ofs, uint64_t size);
  virtual int clone_objs(rgw_obj& dst_obj, 
                        vector<RGWCloneRangeInfo>& ranges,
                        map<string, bufferlist> attrs, bool truncate_dest);

  /** Copy an object, with many extra options */
  virtual int copy_obj(std::string& id, rgw_obj& dest_obj,
               rgw_obj& src_obj,
               time_t *mtime,
               const time_t *mod_ptr,
               const time_t *unmod_ptr,
               const char *if_match,
               const char *if_nomatch,
               map<std::string, bufferlist>& attrs,
               struct rgw_err *err);
  /** delete a bucket*/
  virtual int delete_bucket(std::string& id, std::string& bucket);
  virtual int purge_buckets(std::string& id, vector<std::string>& buckets);

  virtual int disable_buckets(std::vector<std::string>& buckets);
  virtual int enable_buckets(std::vector<std::string>& buckets, uint64_t auid);
  virtual int bucket_suspended(std::string& bucket, bool *suspended);

  /** Delete an object.*/
  virtual int delete_obj(std::string& id, rgw_obj& src_obj, bool sync);

  /** Get the attributes for an object.*/
  virtual int get_attr(rgw_obj& obj, const char *name, bufferlist& dest);

  /** Set an attr on an object. */
  virtual int set_attr(rgw_obj& obj, const char *name, bufferlist& bl);

  /** Get data about an object out of RADOS and into memory. */
  virtual int prepare_get_obj(rgw_obj& obj,
            off_t ofs, off_t *end,
            map<string, bufferlist> *attrs,
            const time_t *mod_ptr,
            const time_t *unmod_ptr,
            time_t *lastmod,
            const char *if_match,
            const char *if_nomatch,
            uint64_t *total_size,
            uint64_t *obj_size,
            void **handle,
            struct rgw_err *err);

  virtual int get_obj(void **handle, rgw_obj& obj,
            char **data, off_t ofs, off_t end);

  virtual void finish_get_obj(void **handle);

  virtual int read(rgw_obj& obj, off_t ofs, size_t size, bufferlist& bl);

  virtual int obj_stat(rgw_obj& obj, uint64_t *psize, time_t *pmtime);

  virtual int get_bucket_id(std::string& bucket);

  virtual bool supports_tmap() { return true; }
  virtual int tmap_set(rgw_obj& obj, std::string& key, bufferlist& bl);
  virtual int tmap_create(rgw_obj& obj, std::string& key, bufferlist& bl);
  virtual int tmap_del(rgw_obj& obj, std::string& key);
  virtual int update_containers_stats(map<string, RGWBucketEnt>& m);
  virtual int append_async(rgw_obj& obj, size_t size, bufferlist& bl);

  virtual int init_watch();
  virtual void finalize_watch();
  virtual int distribute(bufferlist& bl);
  virtual int watch_cb(int opcode, uint64_t ver, bufferlist& bl) { return 0; }
};

#endif
