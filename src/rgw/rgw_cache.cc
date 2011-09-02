#include "rgw_cache.h"

#include <errno.h>

using namespace std;


int ObjectCache::get(string& name, ObjectCacheInfo& info, uint32_t mask)
{
  Mutex::Locker l(lock);

  map<string, ObjectCacheEntry>::iterator iter = cache_map.find(name);
  if (iter == cache_map.end()) {
    RGW_LOG(10) << "cache get: name=" << name << " : miss" << dendl;
    return -ENOENT;
  }

  touch_lru(name, iter->second.lru_iter);

  ObjectCacheInfo& src = iter->second.info;
  if ((src.flags & mask) != mask) {
    RGW_LOG(10) << "cache get: name=" << name << " : type miss (requested=" << mask << ", cached=" << src.flags << dendl;
    return -ENOENT;
  }
  RGW_LOG(10) << "cache get: name=" << name << " : hit" << dendl;

  info = src;

  return 0;
}

void ObjectCache::put(string& name, ObjectCacheInfo& info)
{
  Mutex::Locker l(lock);

  RGW_LOG(10) << "cache put: name=" << name << dendl;
  map<string, ObjectCacheEntry>::iterator iter = cache_map.find(name);
  if (iter == cache_map.end()) {
    ObjectCacheEntry entry;
    entry.lru_iter = lru.end();
    cache_map.insert(pair<string, ObjectCacheEntry>(name, entry));
    iter = cache_map.find(name);
  }
  ObjectCacheEntry& entry = iter->second;
  ObjectCacheInfo& target = entry.info;

  touch_lru(name, entry.lru_iter);

  target.status = info.status;

  if (info.status < 0) {
    target.flags = 0;
    target.xattrs.clear();
    target.data.clear();
    return;
  }

  target.flags |= info.flags;

  if (info.flags & CACHE_FLAG_META)
    target.meta = info.meta;
  else
    target.flags &= ~CACHE_FLAG_META; // any non-meta change should reset meta

  if (info.flags & CACHE_FLAG_XATTRS)
    target.xattrs = info.xattrs;

  if (info.flags & CACHE_FLAG_DATA)
    target.data = info.data;
}

void ObjectCache::remove(string& name)
{
  Mutex::Locker l(lock);

  map<string, ObjectCacheEntry>::iterator iter = cache_map.find(name);
  if (iter == cache_map.end())
    return;

  RGW_LOG(10) << "removing " << name << " from cache" << dendl;

  remove_lru(name, iter->second.lru_iter);
  cache_map.erase(iter);
}

void ObjectCache::touch_lru(string& name, std::list<string>::iterator& lru_iter)
{
  while (lru.size() > (size_t)g_conf->rgw_cache_lru_size) {
    list<string>::iterator iter = lru.begin();
    map<string, ObjectCacheEntry>::iterator map_iter = cache_map.find(*iter);
    RGW_LOG(10) << "removing entry: name=" << *iter << " from cache LRU" << dendl;
    if (map_iter != cache_map.end())
      cache_map.erase(map_iter);
    lru.pop_front();
  }

  if (lru_iter == lru.end()) {
    lru.push_back(name);
    lru_iter--;
    RGW_LOG(10) << "adding " << name << " to cache LRU end" << dendl;
  } else {
    RGW_LOG(10) << "moving " << name << " to cache LRU end" << dendl;
    lru.erase(lru_iter);
    lru.push_back(name);
    lru_iter = lru.end();
    --lru_iter;
  }
}

void ObjectCache::remove_lru(string& name, std::list<string>::iterator& lru_iter)
{
  if (lru_iter == lru.end())
    return;

  lru.erase(lru_iter);
  lru_iter = lru.end();
}


