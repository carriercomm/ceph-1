// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2008-2011 New Dream Network
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#include "BackTrace.h"
#include "common/ceph_context.h"
#include "common/config.h"
#include "common/debug.h"
#include "include/assert.h"

#include <errno.h>
#include <iostream>
#include <pthread.h>
#include <sstream>
#include <time.h>

namespace ceph {
  static CephContext *g_assert_context = NULL;

  /* If you register an assert context, assert() will try to lock the dout
   * stream of that context before starting an assert. This is nice because the
   * output looks better. Your assert will not be interleaved with other dout
   * statements.
   *
   * However, this is strictly optional and library code currently does not
   * register an assert context. The extra complexity of supporting this
   * wouldn't really be worth it.
   */
  void register_assert_context(CephContext *cct)
  {
    assert(!g_assert_context);
    g_assert_context = cct;
  }

  void __ceph_assert_fail(const char *assertion, const char *file, int line, const char *func)
  {
    DoutLocker dout_locker;
    if (g_assert_context) {
      g_assert_context->dout_trylock(&dout_locker);
    }

    char buf[8096];
    BackTrace *bt = new BackTrace(1);
    snprintf(buf, sizeof(buf),
	     "%s: In function '%s', in thread '%p'\n"
	     "%s: %d: FAILED assert(%s)\n",
	     file, func, (void*)pthread_self(), file, line, assertion);
    dout_emergency(buf);

    // TODO: get rid of this memory allocation.
    ostringstream oss;
    bt->print(oss);
    dout_emergency(oss.str());

    snprintf(buf, sizeof(buf),
	     " NOTE: a copy of the executable, or `objdump -rdS <executable>` "
	     "is needed to interpret this.\n");
    dout_emergency(oss.str());

    throw FailedAssertion(bt);
  }

  void __ceph_assert_warn(const char *assertion, const char *file,
			  int line, const char *func)
  {
    DoutLocker dout_locker;
    if (g_assert_context) {
      g_assert_context->dout_trylock(&dout_locker);
    }

    char buf[8096];
    snprintf(buf, sizeof(buf),
	     "WARNING: assert(%s) at: %s: %d: %s()\n",
	     assertion, file, line, func);
    dout_emergency(buf);
  }
}
