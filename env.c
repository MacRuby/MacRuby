/*
 * MacRuby implementation of ENV.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2007-2011, Apple Inc. All rights reserved.
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 * Copyright (C) 2000 Network Applied Communication Laboratory, Inc.
 * Copyright (C) 2000 Information-technology Promotion Agency, Japan
 */

#include "macruby_internal.h"
#include "ruby/st.h"
#include "ruby/util.h"
#include "ruby/node.h"
#include "vm.h"

char ***_NSGetEnviron();

static VALUE envtbl;

static int path_tainted = -1;

static char **origenviron;
#define GET_ENVIRON() (*_NSGetEnviron())

static VALUE
to_hash(VALUE hash)
{
    return rb_convert_type(hash, T_HASH, "Hash", "to_hash");
}

static VALUE
env_str_new(const char *ptr, long len)
{
    VALUE str = rb_tainted_str_new(ptr, len);
    rb_obj_freeze(str);
    return str;
}

static VALUE
env_str_new2(const char *ptr)
{
    if (ptr == NULL) {
	return Qnil;
    }
    return env_str_new(ptr, strlen(ptr));
}

static VALUE
env_delete(VALUE obj, VALUE name)
{
    rb_secure(4);
    SafeStringValue(name);
    const char *nam = RSTRING_PTR(name);
    if (strlen(nam) != RSTRING_LEN(name)) {
	rb_raise(rb_eArgError, "bad environment variable name");
    }
    const char *val = getenv(nam);
    if (val != NULL) {
	VALUE value = env_str_new2(val);
	ruby_setenv(nam, 0);
	if (strcmp(nam, PATH_ENV) == 0) {
	    path_tainted = 0;
	}
	return value;
    }
    return Qnil;
}

static VALUE
env_delete_m(VALUE obj, SEL sel, VALUE name)
{
    VALUE val = env_delete(obj, name);
    if (NIL_P(val) && rb_block_given_p()) {
	rb_yield(name);
    }
    return val;
}

static VALUE
rb_f_getenv(VALUE obj, SEL sel, VALUE name)
{
    rb_secure(4);
    SafeStringValue(name);
    const char *nam = RSTRING_PTR(name);
    if (strlen(nam) != RSTRING_LEN(name)) {
	rb_raise(rb_eArgError, "bad environment variable name");
    }
    const char *env = getenv(nam);
    if (env != NULL) {
	if (strcmp(nam, PATH_ENV) == 0 && !rb_env_path_tainted()) {
	    VALUE str = rb_str_new2(env);
	    rb_obj_freeze(str);
	    return str;
	}
	return env_str_new2(env);
    }
    return Qnil;
}

static VALUE
env_fetch(VALUE rcv, SEL sel, int argc, VALUE *argv)
{
    rb_secure(4);

    VALUE key, if_none;
    rb_scan_args(argc, argv, "11", &key, &if_none);

    const bool block_given = rb_block_given_p();
    if (block_given && argc == 2) {
	rb_warn("block supersedes default value argument");
    }
    SafeStringValue(key);
    const char *nam = RSTRING_PTR(key);
    if (strlen(nam) != RSTRING_LEN(key)) {
	rb_raise(rb_eArgError, "bad environment variable name");
    }
    const char *env = getenv(nam);
    if (env == NULL) {
	if (block_given) {
	    return rb_yield(key);
	}
	if (argc == 1) {
	    rb_raise(rb_eKeyError, "key not found");
	}
	return if_none;
    }
    if (strcmp(nam, PATH_ENV) == 0 && !rb_env_path_tainted()) {
	return rb_str_new2(env);
    }
    return env_str_new2(env);
}

static void
path_tainted_p(const char *path)
{
    path_tainted = rb_path_check(path) ? 0 : 1;
}

int
rb_env_path_tainted(void)
{
    if (path_tainted < 0) {
	path_tainted_p(getenv(PATH_ENV));
    }
    return path_tainted;
}

void
ruby_setenv(const char *name, const char *value)
{
    if (value != NULL) {
	if (setenv(name, value, 1)) {
	    rb_sys_fail("setenv");
	}
    }
    else {
	if (unsetenv(name)) {
	    rb_sys_fail("unsetenv");
	}
    }
}

void
ruby_unsetenv(const char *name)
{
    ruby_setenv(name, 0);
}

static VALUE
env_aset(VALUE obj, SEL sel, VALUE nm, VALUE val)
{
    if (rb_safe_level() >= 4) {
	rb_raise(rb_eSecurityError, "can't change environment variable");
    }

    if (NIL_P(val)) {
	env_delete(obj, nm);
	return Qnil;
    }
    StringValue(nm);
    StringValue(val);
    const char *name = RSTRING_PTR(nm);
    const char *value = RSTRING_PTR(val);
    if (strlen(name) != RSTRING_LEN(nm)) {
	rb_raise(rb_eArgError, "bad environment variable name");
    }
    if (strlen(value) != RSTRING_LEN(val)) {
	rb_raise(rb_eArgError, "bad environment variable value");
    }

    ruby_setenv(name, value);
    if (strcmp(name, PATH_ENV) == 0) {
	if (OBJ_TAINTED(val)) {
	    /* already tainted, no check */
	    path_tainted = 1;
	    return val;
	}
	else {
	    path_tainted_p(value);
	}
    }
    return val;
}

static VALUE
env_keys(VALUE rcv, SEL sel)
{
    rb_secure(4);

    VALUE ary = rb_ary_new();
    char **env = GET_ENVIRON();
    while (*env != NULL) {
	const char *s = strchr(*env, '=');
	if (s != NULL) {
	    rb_ary_push(ary, env_str_new(*env, s - *env));
	}
	env++;
    }
    return ary;
}

static VALUE
env_each_key(VALUE ehash, SEL sel)
{
    RETURN_ENUMERATOR(ehash, 0, 0);
    VALUE keys = env_keys(Qnil, 0);	/* rb_secure(4); */
    for (long i = 0, count = RARRAY_LEN(keys); i < count; i++) {
	rb_yield(RARRAY_AT(keys, i));
	RETURN_IF_BROKEN();
    }
    return ehash;
}

static VALUE
env_values(VALUE rcv, SEL sel)
{
    rb_secure(4);

    VALUE ary = rb_ary_new();
    char **env = GET_ENVIRON();
    while (*env != NULL) {
	const char *s = strchr(*env, '=');
	if (s != NULL) {
	    rb_ary_push(ary, env_str_new2(s + 1));
	}
	env++;
    }
    return ary;
}

static VALUE
env_each_value(VALUE ehash, SEL sel)
{
    RETURN_ENUMERATOR(ehash, 0, 0);
    VALUE values = env_values(Qnil, 0);	/* rb_secure(4); */
    for (long i = 0, count = RARRAY_LEN(values); i < count; i++) {
	rb_yield(RARRAY_AT(values, i));
	RETURN_IF_BROKEN();
    }
    return ehash;
}

static VALUE
env_each_pair(VALUE ehash, SEL sel)
{
    RETURN_ENUMERATOR(ehash, 0, 0);

    rb_secure(4);
    VALUE ary = rb_ary_new();
    char **env = GET_ENVIRON();
    while (*env != NULL) {
	const char *s = strchr(*env, '=');
	if (s != NULL) {
	    rb_ary_push(ary, env_str_new(*env, s - *env));
	    rb_ary_push(ary, env_str_new2(s + 1));
	}
	env++;
    }

    for (long i = 0, count = RARRAY_LEN(ary); i < count; i += 2) {
	rb_yield(rb_assoc_new(RARRAY_AT(ary, i), RARRAY_AT(ary, i + 1)));
	RETURN_IF_BROKEN();
    }
    return ehash;
}

static VALUE
env_reject_bang(VALUE ehash, SEL sel)
{
    RETURN_ENUMERATOR(ehash, 0, 0);
    VALUE keys = env_keys(Qnil, 0);	/* rb_secure(4); */
    bool deleted = false;
    for (long i = 0, count = RARRAY_LEN(keys); i < count; i++) {
	VALUE key = RARRAY_AT(keys, i);
	VALUE val = rb_f_getenv(Qnil, 0, key);
	if (!NIL_P(val)) {
	    VALUE v = rb_yield_values(2, key, val);
	    RETURN_IF_BROKEN();
	    if (RTEST(v)) {
		rb_obj_untaint(key);
		env_delete(Qnil, key);
		deleted = true;
	    }
	}
    }
    return deleted ? envtbl : Qnil;
}

static VALUE
env_delete_if(VALUE ehash, SEL sel)
{
    RETURN_ENUMERATOR(ehash, 0, 0);
    env_reject_bang(ehash, 0);
    return envtbl;
}

static VALUE
env_values_at(VALUE rcv, SEL sel, int argc, VALUE *argv)
{
    rb_secure(4);
    VALUE result = rb_ary_new();
    for (long i = 0; i < argc; i++) {
	rb_ary_push(result, rb_f_getenv(Qnil, 0, argv[i]));
    }
    return result;
}

static VALUE
env_select(VALUE ehash, SEL sel)
{
    RETURN_ENUMERATOR(ehash, 0, 0);
    rb_secure(4);
    VALUE result = rb_hash_new();
    char **env = GET_ENVIRON();
    while (*env != NULL) {
	const char *s = strchr(*env, '=');
	if (s != NULL) {
	    VALUE k = env_str_new(*env, s - *env);
	    VALUE v = env_str_new2(s + 1);
	    VALUE v2 = rb_yield_values(2, k, v);
	    RETURN_IF_BROKEN();
	    if (RTEST(v2)) {
		rb_hash_aset(result, k, v);
	    }
	}
	env++;
    }
    return result;
}

static VALUE
env_select_bang(VALUE ehash, SEL sel)
{
    volatile VALUE keys;
    long i;
    int del = 0;

    RETURN_ENUMERATOR(ehash, 0, 0);
    keys = env_keys(Qnil, 0);	/* rb_secure(4); */
    for (i=0; i<RARRAY_LEN(keys); i++) {
	VALUE val = rb_f_getenv(Qnil, 0, RARRAY_AT(keys, i));
	if (!NIL_P(val)) {
	    if (!RTEST(rb_yield_values(2, RARRAY_AT(keys, i), val))) {
		rb_obj_untaint(RARRAY_AT(keys, i));
		env_delete(Qnil, RARRAY_PTR(keys)[i]);
		del++;
	    }
	}
    }
    if (del == 0) {
	return Qnil;
    }
    return envtbl;
}

static VALUE
env_keep_if(VALUE ehash, SEL sel)
{
    RETURN_ENUMERATOR(ehash, 0, 0);
    env_select_bang(ehash, 0);
    return envtbl;
}

static VALUE
rb_env_clear_imp(VALUE rcv, SEL sel)
{
    VALUE keys = env_keys(Qnil, 0);	/* rb_secure(4); */
    for (long i = 0, count = RARRAY_LEN(keys); i < count; i++) {
	VALUE val = rb_f_getenv(Qnil, 0, RARRAY_AT(keys, i));
	if (!NIL_P(val)) {
	    env_delete(Qnil, RARRAY_AT(keys, i));
	}
    }
    return envtbl;
}

VALUE
rb_env_clear(void)
{
    return rb_env_clear_imp(Qnil, 0);
}

static VALUE
env_to_s(VALUE rcv, SEL sel)
{
    return rb_usascii_str_new2("ENV");
}

static VALUE
env_inspect(VALUE rcv, SEL sel)
{
    rb_secure(4);

    VALUE str = rb_str_buf_new2("{");
    char **env = GET_ENVIRON();
    while (*env != NULL) {
	const char *s = strchr(*env, '=');

	if (env != GET_ENVIRON()) {
	    rb_str_buf_cat2(str, ", ");
	}
	if (s != NULL) {
	    rb_str_buf_cat2(str, "\"");
	    rb_str_buf_cat(str, *env, s - *env);
	    rb_str_buf_cat2(str, "\"=>");
	    VALUE i = rb_inspect(rb_str_new2(s + 1));
	    rb_str_buf_append(str, i);
	}
	env++;
    }
    rb_str_buf_cat2(str, "}");
    OBJ_TAINT(str);

    return str;
}

static VALUE
env_to_a(VALUE rcv, SEL sel)
{
    rb_secure(4);

    VALUE ary = rb_ary_new();
    char **env = GET_ENVIRON();
    while (*env != NULL) {
	const char *s = strchr(*env, '=');
	if (s != NULL) {
	    rb_ary_push(ary, rb_assoc_new(env_str_new(*env, s - *env),
			env_str_new2(s + 1)));
	}
	env++;
    }
    return ary;
}

static VALUE
env_none(VALUE rcv, SEL sel)
{
    return Qnil;
}

static VALUE
env_size(VALUE rcv, SEL sel)
{
    rb_secure(4);

    char **env = GET_ENVIRON();
    int i = 0;
    while (env[i] != NULL) {
	i++;
    }
    return INT2FIX(i);
}

static VALUE
env_empty_p(VALUE rcv, SEL sel)
{
    rb_secure(4);

    char **env = GET_ENVIRON();
    if (env[0] == NULL) {
	return Qtrue;
    }
    return Qfalse;
}

static VALUE
env_has_key(VALUE env, SEL sel, VALUE key)
{
    rb_secure(4);

    const char *s = StringValuePtr(key);
    if (strlen(s) != RSTRING_LEN(key)) {
	rb_raise(rb_eArgError, "bad environment variable name");
    }
    if (getenv(s) != NULL) {
	return Qtrue;
    }
    return Qfalse;
}

static VALUE
env_assoc(VALUE env, SEL sel, VALUE key)
{
    rb_secure(4);

    const char *s = StringValuePtr(key);
    if (strlen(s) != RSTRING_LEN(key)) {
	rb_raise(rb_eArgError, "bad environment variable name");
    }
    const char *e = getenv(s);
    if (e != NULL) {
	return rb_assoc_new(key, rb_tainted_str_new2(e));
    }
    return Qnil;
}

static VALUE
env_has_value(VALUE dmy, SEL sel, VALUE obj)
{
    rb_secure(4);

    obj = rb_check_string_type(obj);
    if (NIL_P(obj)) {
	return Qnil;
    }
    char **env = GET_ENVIRON();
    while (*env != NULL) {
	const char *s = strchr(*env, '=');
	if (s++ != NULL) {
	    const long len = strlen(s);
	    if (RSTRING_LEN(obj) == len
		    && strncmp(s, RSTRING_PTR(obj), len) == 0) {
		return Qtrue;
	    }
	}
	env++;
    }
    return Qfalse;
}

static VALUE
env_rassoc(VALUE dmy, SEL sel, VALUE obj)
{
    rb_secure(4);

    obj = rb_check_string_type(obj);
    if (NIL_P(obj)) {
	return Qnil;
    }
    char **env = GET_ENVIRON();
    while (*env != NULL) {
	const char *s = strchr(*env, '=');
	if (s++ != NULL) {
	    const long len = strlen(s);
	    if (RSTRING_LEN(obj) == len
		    && strncmp(s, RSTRING_PTR(obj), len) == 0) {
		return rb_assoc_new(rb_tainted_str_new(*env, s - *env - 1),
			obj);
	    }
	}
	env++;
    }
    return Qnil;
}

static VALUE
env_key(VALUE dmy, SEL sel, VALUE value)
{
    rb_secure(4);

    StringValue(value);
    char **env = GET_ENVIRON();
    while (*env != NULL) {
	const char *s = strchr(*env, '=');
	if (s++ != NULL) {
	    const long len = strlen(s);
	    if (RSTRING_LEN(value) == len
		    && strncmp(s, RSTRING_PTR(value), len) == 0) {
		return env_str_new(*env, s - *env - 1);
	    }
	}
	env++;
    }
    return Qnil;
}

static VALUE
env_index(VALUE dmy, SEL sel, VALUE value)
{
    rb_warn("ENV.index is deprecated; use ENV.key");
    return env_key(dmy, 0, value);
}

static VALUE
env_to_hash(VALUE rcv, SEL sel)
{
    rb_secure(4);

    VALUE hash = rb_hash_new();
    char **env = GET_ENVIRON();
    while (*env != NULL) {
	const char *s = strchr(*env, '=');
	if (s != NULL) {
	    rb_hash_aset(hash, env_str_new(*env, s - *env),
		    env_str_new2(s + 1));
	}
	env++;
    }
    return hash;
}

static VALUE
env_reject(VALUE rcv, SEL sel)
{
    rb_secure(4);

    RETURN_ENUMERATOR(rcv, 0, 0);

    VALUE hash = rb_hash_new();
    char **env = GET_ENVIRON();
    while (*env != NULL) {
	const char *s = strchr(*env, '=');
	if (s != NULL) {
	    VALUE key = env_str_new(*env, s - *env);
	    VALUE val = env_str_new2(s + 1);
	    if (!RTEST(rb_yield_values(2, key, val))) {
		rb_hash_aset(hash, key, val);
	    }
	}
	env++;
    }
    return hash;
}

static VALUE
env_shift(VALUE rcv, SEL sel)
{
    rb_secure(4);

    char **env = GET_ENVIRON();
    if (*env != NULL) {
	const char *s = strchr(*env, '=');
	if (s != NULL) {
	    VALUE key = env_str_new(*env, s - *env);
	    VALUE val = env_str_new2(getenv(RSTRING_PTR(key)));
	    env_delete(Qnil, key);
	    return rb_assoc_new(key, val);
	}
    }
    return Qnil;
}

static VALUE
env_invert(VALUE rcv, SEL sel)
{
    rb_secure(4);

    VALUE hash = rb_hash_new();
    char **env = GET_ENVIRON();
    while (*env != NULL) {
	const char *s = strchr(*env, '=');
	if (s != NULL) {
	    rb_hash_aset(hash, env_str_new2(s + 1),
		    env_str_new(*env, s - *env));
	}
	env++;
    }
    return hash;
}

static int
env_replace_i(VALUE key, VALUE val, VALUE keys)
{
    if (key != Qundef) {
	env_aset(Qnil, 0, key, val);
	if (rb_ary_includes(keys, key)) {
	    rb_ary_delete(keys, key);
	}
    }
    return ST_CONTINUE;
}

static VALUE
env_replace(VALUE env, SEL sel, VALUE hash)
{
    VALUE keys = env_keys(Qnil, 0);	/* rb_secure(4); */
    if (env == hash) {
	return env;
    }
    hash = to_hash(hash);
    rb_hash_foreach(hash, env_replace_i, keys);

    for (long i = 0, count = RARRAY_LEN(keys); i < count; i++) {
	env_delete(env, RARRAY_AT(keys, i));
    }
    return env;
}

static int
env_update_i(VALUE key, VALUE val, VALUE ctx)
{
    if (key != Qundef) {
	if (rb_block_given_p()) {
	    val = rb_yield_values(3, key, rb_f_getenv(Qnil, 0, key), val);
	    RETURN_IF_BROKEN();
	}
	env_aset(Qnil, 0, key, val);
    }
    return ST_CONTINUE;
}

static VALUE
env_update(VALUE env, SEL sel, VALUE hash)
{
    rb_secure(4);
    if (env == hash) {
	return env;
    }
    hash = to_hash(hash);
    rb_hash_foreach(hash, env_update_i, 0);
    return env;
}

void
Init_ENV(void)
{
    origenviron = GET_ENVIRON();
    envtbl = rb_obj_alloc(rb_cObject);
    rb_extend_object(envtbl, rb_mEnumerable);

    VALUE klass = rb_singleton_class(envtbl);

    rb_objc_define_method(klass, "[]", rb_f_getenv, 1);
    rb_objc_define_method(klass, "fetch", env_fetch, -1);
    rb_objc_define_method(klass, "[]=", env_aset, 2);
    rb_objc_define_method(klass, "store", env_aset, 2);
    rb_objc_define_method(klass, "each", env_each_pair, 0);
    rb_objc_define_method(klass, "each_pair", env_each_pair, 0);
    rb_objc_define_method(klass, "each_key", env_each_key, 0);
    rb_objc_define_method(klass, "each_value", env_each_value, 0);
    rb_objc_define_method(klass, "delete", env_delete_m, 1);
    rb_objc_define_method(klass, "delete_if", env_delete_if, 0);
    rb_objc_define_method(klass, "keep_if", env_keep_if, 0);
    rb_objc_define_method(klass, "clear", rb_env_clear_imp, 0);
    rb_objc_define_method(klass, "reject", env_reject, 0);
    rb_objc_define_method(klass, "reject!", env_reject_bang, 0);
    rb_objc_define_method(klass, "select", env_select, 0);
    rb_objc_define_method(klass, "select!", env_select_bang, 0);
    rb_objc_define_method(klass, "shift", env_shift, 0);
    rb_objc_define_method(klass, "invert", env_invert, 0);
    rb_objc_define_method(klass, "replace", env_replace, 1);
    rb_objc_define_method(klass, "update", env_update, 1);
    rb_objc_define_method(klass, "inspect", env_inspect, 0);
    rb_objc_define_method(klass, "rehash", env_none, 0);
    rb_objc_define_method(klass, "to_a", env_to_a, 0);
    rb_objc_define_method(klass, "to_s", env_to_s, 0);
    rb_objc_define_method(klass, "key", env_key, 1);
    rb_objc_define_method(klass, "index", env_index, 1);
    rb_objc_define_method(klass, "size", env_size, 0);
    rb_objc_define_method(klass, "length", env_size, 0);
    rb_objc_define_method(klass, "empty?", env_empty_p, 0);
    rb_objc_define_method(klass, "keys", env_keys, 0);
    rb_objc_define_method(klass, "values", env_values, 0);
    rb_objc_define_method(klass, "values_at", env_values_at, -1);
    rb_objc_define_method(klass, "include?", env_has_key, 1);
    rb_objc_define_method(klass, "member?", env_has_key, 1);
    rb_objc_define_method(klass, "has_key?", env_has_key, 1);
    rb_objc_define_method(klass, "has_value?", env_has_value, 1);
    rb_objc_define_method(klass, "key?", env_has_key, 1);
    rb_objc_define_method(klass, "value?", env_has_value, 1);
    rb_objc_define_method(klass, "to_hash", env_to_hash, 0);
    rb_objc_define_method(klass, "assoc", env_assoc, 1);
    rb_objc_define_method(klass, "rassoc", env_rassoc, 1);

    rb_define_global_const("ENV", envtbl);
}
