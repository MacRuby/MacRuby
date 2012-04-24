/*
 * MacRuby BridgeSupport implementation.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2007-2011, Apple Inc. All rights reserved.
 */

#ifndef __BRIDGESUPPORT_H_
#define __BRIDGESUPPORT_H_

#if defined(__cplusplus)
extern "C" {
#endif

#include "bs.h"

void *rb_pointer_get_data(VALUE rcv, const char *type);
VALUE rb_pointer_new(const char *type_str, void *val, size_t len);
VALUE rb_pointer_new2(const char *type_str, VALUE val);
bool rb_boxed_is_type(VALUE klass, const char *type);

#if defined(__cplusplus)
} // extern "C"

typedef struct rb_vm_bs_boxed {
    bs_element_type_t bs_type;
    bool is_struct(void) { return bs_type == BS_ELEMENT_STRUCT; }
    union {
	bs_element_struct_t *s;
	bs_element_opaque_t *o;
	void *v;
    } as;
#if !defined(MACRUBY_STATIC)
    Type *type;
#endif
    VALUE klass;
} rb_vm_bs_boxed_t;

#endif /* __cplusplus */

#endif /* __BRIDGESUPPORT_H_ */
