/*
 * MacRuby API for Grand Central Dispatch.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2009-2011, Apple Inc. All rights reserved.
 */

#ifndef __GCD_H_
#define __GCD_H_

#if defined(__cplusplus)
extern "C" {
#endif

#include <dispatch/dispatch.h>

dispatch_queue_t rb_get_dispatch_queue_object(VALUE queue);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // __GCD_H_
