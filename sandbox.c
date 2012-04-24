/*
 * MacRuby interface to sandbox/seatbelt.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2011, Apple Inc. All rights reserved.
 */

#include <sandbox.h>
#include "macruby_internal.h"
#include "ruby/util.h"

static VALUE rb_cSandbox;

typedef struct {
    const char *profile;
    uint64_t flags;
} rb_sandbox_t;

static VALUE
rb_sandbox_s_alloc(VALUE klass, SEL sel)
{
    rb_sandbox_t *sb = ALLOC(rb_sandbox_t);
    sb->profile = NULL;
    sb->flags = 0;
    return Data_Wrap_Struct(klass, NULL, NULL, sb);
}

static VALUE
rb_sandbox_init(VALUE obj, SEL sel, VALUE profile)
{
    rb_sandbox_t *box;

    StringValue(profile);
    Data_Get_Struct(obj, rb_sandbox_t, box);
    GC_WB(&box->profile, ruby_strdup(RSTRING_PTR(profile)));
    box->flags = 0;

    return obj;
}


static inline VALUE
predefined_sandbox(const char *name)
{
    VALUE obj = rb_sandbox_s_alloc(rb_cSandbox, 0);
    rb_sandbox_t *box; 
    Data_Get_Struct(obj, rb_sandbox_t, box);
    box->profile = name;
    box->flags = SANDBOX_NAMED;
    return rb_obj_freeze(obj);
}

static VALUE
rb_sandbox_s_no_internet(VALUE klass, SEL sel)
{
    return predefined_sandbox(kSBXProfileNoInternet);
}

static VALUE
rb_sandbox_s_no_network(VALUE klass, SEL sel)
{
    return predefined_sandbox(kSBXProfileNoNetwork); 
}

static VALUE
rb_sandbox_s_no_writes(VALUE klass, SEL sel)
{
    return predefined_sandbox(kSBXProfileNoWrite);
}

static VALUE
rb_sandbox_s_temporary_writes(VALUE klass, SEL sel)
{
    return predefined_sandbox(kSBXProfileNoWriteExceptTemporary);
}

static VALUE
rb_sandbox_s_pure_computation(VALUE klass, SEL sel)
{
    return predefined_sandbox(kSBXProfilePureComputation);
}

static VALUE
rb_sandbox_apply(VALUE self, SEL sel)
{
    rb_sandbox_t *box;
    Data_Get_Struct(self, rb_sandbox_t, box);
    char *error = NULL;
    if (sandbox_init(box->profile, box->flags, &error) == -1) {
        rb_raise(rb_eSecurityError, "Couldn't apply sandbox: `%s`", error);
    }
    return Qnil;
}

void
Init_sandbox(void)
{
    rb_cSandbox = rb_define_class("Sandbox", rb_cData);
    
    rb_objc_define_method(*(VALUE *)rb_cSandbox, "alloc", rb_sandbox_s_alloc, 0);
    rb_objc_define_method(*(VALUE *)rb_cSandbox, "no_internet", rb_sandbox_s_no_internet, 0);
    rb_objc_define_method(*(VALUE *)rb_cSandbox, "no_network", rb_sandbox_s_no_network, 0);
    rb_objc_define_method(*(VALUE *)rb_cSandbox, "no_writes", rb_sandbox_s_no_writes, 0);
    rb_objc_define_method(*(VALUE *)rb_cSandbox, "temporary_writes", rb_sandbox_s_temporary_writes, 0);
    rb_objc_define_method(*(VALUE *)rb_cSandbox, "pure_computation", rb_sandbox_s_pure_computation, 0);
    
    rb_objc_define_method(rb_cSandbox, "initialize", rb_sandbox_init, 1);
    rb_objc_define_method(rb_cSandbox, "apply!", rb_sandbox_apply, 0);
}
