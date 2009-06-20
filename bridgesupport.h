/*
 * MacRuby BridgeSupport implementation.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 * 
 * Copyright (C) 2007-2009, Apple Inc. All rights reserved.
 */

#ifndef __BRIDGESUPPORT_H_
#define __BRIDGESUPPORT_H_

#if defined(__cplusplus)

#include "bs.h"

typedef struct rb_vm_bs_boxed {
    bs_element_type_t bs_type;
    bool is_struct(void) { return bs_type == BS_ELEMENT_STRUCT; }
    union {
	bs_element_struct_t *s;
	bs_element_opaque_t *o;
	void *v;
    } as;
    Type *type;
    VALUE klass;
} rb_vm_bs_boxed_t;

VALUE rb_pointer_new(const char *type_str, void *val);
VALUE rb_pointer_new2(const char *type_str, VALUE val);
void *rb_pointer_get_data(VALUE rcv, const char *type);

bool rb_boxed_is_type(VALUE klass, const char *type);

#endif /* __cplusplus */

#endif /* __BRIDGESUPPORT_H_ */
