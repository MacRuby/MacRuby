/*
 * MacRuby file loader.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 * 
 * Copyright (C) 2009-2011, Apple Inc. All rights reserved.
 */

#include <sys/stat.h>
#include "macruby_internal.h"
#include "ruby/node.h"
#include "vm.h"
#include "dln.h"

extern bool ruby_is_miniruby;
static bool rbo_enabled = true;

VALUE
rb_get_load_path(void)
{
    VALUE load_path = rb_vm_load_path();
    VALUE ary = rb_ary_new();
    for (long i = 0, count = RARRAY_LEN(load_path); i < count; i++) {
	rb_ary_push(ary, rb_file_expand_path(RARRAY_AT(load_path, i), Qnil));
    }
    return ary;
}

static VALUE
get_loaded_features(void)
{
    return rb_vm_loaded_features();
}

int
rb_provided(const char *feature)
{
    VALUE ary = get_loaded_features();
    for (int i = 0, count = RARRAY_LEN(ary); i < count; i++) {
	VALUE path = RARRAY_AT(ary, i);
	// TODO: this is very naive
	if (strstr(RSTRING_PTR(path), feature) != NULL) {
	    return true;
	}
    }
    return false;
}

static void
rb_provide_feature(VALUE feature)
{
    rb_ary_push(get_loaded_features(), feature);
}

#if !defined(MACRUBY_STATIC)
static void
load_failed(VALUE fname)
{
    rb_raise(rb_eLoadError, "no such file to load -- %s",
	    RSTRING_PTR(fname));
}
#endif

void
rb_provide(const char *feature)
{
    rb_provide_feature(rb_str_new2(feature));
}

void
rb_load(VALUE fname, int wrap)
{
#if MACRUBY_STATIC
    rb_raise(rb_eRuntimeError, "#load is not supported in MacRuby static");
#else
    // TODO honor wrap

    // Locate file.
    FilePathValue(fname);
    fname = rb_str_new4(fname);
    VALUE tmp = rb_find_file(fname);
    if (tmp == 0) {
	load_failed(fname);
    }
    fname = tmp;

    // Load it.
    const char *fname_str = RSTRING_PTR(fname);
//printf("load %s\n", fname_str);

    rb_vm_load(fname_str, wrap);
#endif
}

/*
 *  call-seq:
 *     load(filename, wrap=false)   => true
 *  
 *  Loads and executes the Ruby
 *  program in the file _filename_. If the filename does not
 *  resolve to an absolute path, the file is searched for in the library
 *  directories listed in <code>$:</code>. If the optional _wrap_
 *  parameter is +true+, the loaded script will be executed
 *  under an anonymous module, protecting the calling program's global
 *  namespace. In no circumstance will any local variables in the loaded
 *  file be propagated to the loading environment.
 */

static VALUE
rb_f_load(VALUE rcv, SEL sel, int argc, VALUE *argv)
{
    VALUE fname, wrap;

    rb_scan_args(argc, argv, "11", &fname, &wrap);
    rb_load(fname, RTEST(wrap));
    return Qtrue;
}

/*
 *  call-seq:
 *     require(string)    => true or false
 *  
 *  Ruby tries to load the library named _string_, returning
 *  +true+ if successful. If the filename does not resolve to
 *  an absolute path, it will be searched for in the directories listed
 *  in <code>$:</code>. If the file has the extension ``.rb'', it is
 *  loaded as a source file; if the extension is ``.so'', ``.o'', or
 *  ``.dll'', or whatever the default shared library extension is on
 *  the current platform, Ruby loads the shared library as a Ruby
 *  extension. Otherwise, Ruby tries adding ``.rb'', ``.so'', and so on
 *  to the name. The name of the loaded feature is added to the array in
 *  <code>$"</code>. A feature will not be loaded if it's name already
 *  appears in <code>$"</code>. However, the file name is not converted
 *  to an absolute path, so that ``<code>require 'a';require
 *  './a'</code>'' will load <code>a.rb</code> twice.
 *     
 *     require "my-library.rb"
 *     require "db-driver"
 */

VALUE
rb_f_require(VALUE obj, VALUE fname)
{
    return rb_require_safe(fname, rb_safe_level());
}

static VALUE
rb_f_require_imp(VALUE obj, SEL sel, VALUE fname)
{
    return rb_f_require(obj, fname);
}

#if !defined(MACRUBY_STATIC)
#define TYPE_RB		0x1
#define TYPE_RBO	0x2
#define TYPE_BUNDLE	0x3

static bool
path_ok(const char *path, VALUE *out)
{
    struct stat s;
    if (stat(path, &s) == 0 && S_ISREG(s.st_mode)) {
	VALUE found_path = rb_str_new2(path);
	VALUE features = get_loaded_features();
	*out = rb_ary_includes(features, found_path) == Qtrue
	    ? 0 : found_path;
	return true;
    }
    return false;
}

static bool
check_path(const char *path, VALUE *out, int *type)
{
    char *p = strrchr(path, '.');
    if (p != NULL) {
	// The given path already contains a file extension. Let's check if
	// it's a valid one, then try to validate the path.
	int t = 0;
	if (strcmp(p + 1, "rb") == 0) {
	    t = TYPE_RB;
	}
	else if (rbo_enabled && strcmp(p + 1, "rbo") == 0) {
	    t = TYPE_RBO;
	}
	else if (strcmp(p + 1, "bundle") == 0) {
	    t = TYPE_BUNDLE;
	}
	else if (strcmp(p + 1, "so") == 0) {
	    const char *ext = "bundle";
	    const long ext_len = strlen(ext);
	    const long len = p - path;
	    if (len + ext_len + 1 < PATH_MAX) {
		char buf[PATH_MAX];
		strncpy(buf, path, PATH_MAX);
		strcpy(buf + len + 1, ext);
		buf[PATH_MAX - 1] = '\0';
		if (path_ok(buf, out)) {
		    *type = TYPE_BUNDLE;
		    return true;
		}
	    }
	}
	if (t != 0 && path_ok(path, out)) {
	    *type = t;
	    return true;
	}
    }

    // No valid extension, let's append the valid ones and try to validate
    // the path.
    char buf[PATH_MAX];
    if (rbo_enabled) {
	snprintf(buf, sizeof buf, "%s.rbo", path);
	if (path_ok(buf, out)) {
	    *type = TYPE_RBO;
	    return true;
	}
    }
    snprintf(buf, sizeof buf, "%s.rb", path);
    if (path_ok(buf, out)) {
	*type = TYPE_RB;
	return true;
    }
    snprintf(buf, sizeof buf, "%s.bundle", path);
    if (path_ok(buf, out)) {
	*type = TYPE_BUNDLE;
	return true;
    }

    return false;
}

static bool
search_required(VALUE name, VALUE *out, int *type)
{
    const char *name_cstr = RSTRING_PTR(name);
    if (*name_cstr == '/' || *name_cstr == '.' || *name_cstr == '~') {
	// Given name is an absolute path.
	name = rb_file_expand_path(name, Qnil);
	return check_path(RSTRING_PTR(name), out, type);	
    }

    // Given name is not an absolute path, we need to go through $:.
    VALUE load_path = rb_get_load_path();
    for (long i = 0, count = RARRAY_LEN(load_path); i < count; i++) {
	const char *path = RSTRING_PTR(RARRAY_AT(load_path, i));
	char buf[PATH_MAX];
	snprintf(buf, sizeof buf, "%s/%s", path, name_cstr);
	if (check_path(buf, out, type)) {
	    return true;
	}
    }

    return false;
}

static VALUE
load_try(VALUE path)
{
    rb_load(path, 0);
    return Qnil;
}

static void
rb_remove_feature(VALUE feature)
{
    rb_ary_delete(get_loaded_features(), feature);
}

static VALUE
load_rescue(VALUE path, VALUE exc)
{
    rb_remove_feature(path);
    rb_exc_raise(exc);
    return Qnil;
}
#endif

VALUE
rb_require_safe(VALUE fname, int safe)
{
    FilePathValue(fname);

    // Immediately, check out if we have an AOT feature for this.
    if (rb_vm_aot_feature_load(RSTRING_PTR(fname))) {
	rb_provide_feature(fname);
	return Qtrue;
    }

#if MACRUBY_STATIC
    rb_raise(rb_eRuntimeError, "#require is not supported in MacRuby static");
#else
    VALUE result = Qnil;
    VALUE path;
    int type = 0;

    if (search_required(fname, &path, &type)) {
	if (path == 0) {
	    result = Qfalse;
	}
	else {
	    rb_set_safe_level_force(0);
	    rb_provide_feature(path);
	    switch (type) {
		case TYPE_RB:
		    rb_rescue2(load_try, path, load_rescue, path,
			    rb_eException, 0);
		    break;

		case TYPE_RBO:
		    dln_load(RSTRING_PTR(path), false);
		    break;

		case TYPE_BUNDLE:
		    dln_load(RSTRING_PTR(path), true);
		    break;

		default:
		    abort();
	    }
	    result = Qtrue;
	}
    }

    if (NIL_P(result)) {
	load_failed(fname);
    }

    return result;
#endif
}

VALUE
rb_require(const char *fname)
{
    VALUE fn = rb_str_new2(fname);
    OBJ_FREEZE(fn);
    return rb_require_safe(fn, rb_safe_level());
}

/*
 *  call-seq:
 *     mod.autoload(name, filename)   => nil
 *  
 *  Registers _filename_ to be loaded (using <code>Kernel::require</code>)
 *  the first time that _module_ (which may be a <code>String</code> or
 *  a symbol) is accessed in the namespace of _mod_.
 *     
 *     module A
 *     end
 *     A.autoload(:B, "b")
 *     A::B.doit            # autoloads "b"
 */

static VALUE
rb_mod_autoload(VALUE mod, SEL sel, VALUE sym, VALUE file)
{
    ID id = rb_to_id(sym);

    Check_SafeStr(file);
    rb_autoload(mod, id, RSTRING_PTR(file));
    return Qnil;
}

/*
 * MISSING: documentation
 */

static VALUE
rb_mod_autoload_p(VALUE mod, SEL sel, VALUE sym)
{
    return rb_autoload_p(mod, rb_to_id(sym));
}

/*
 *  call-seq:
 *     autoload(module, filename)   => nil
 *  
 *  Registers _filename_ to be loaded (using <code>Kernel::require</code>)
 *  the first time that _module_ (which may be a <code>String</code> or
 *  a symbol) is accessed.
 *     
 *     autoload(:MyModule, "/usr/local/lib/modules/my_module.rb")
 */

static VALUE
rb_f_autoload(VALUE obj, SEL sel, VALUE sym, VALUE file)
{
    VALUE klass = (VALUE)rb_vm_get_current_class();
    if (klass == 0) {
	klass = rb_cObject;
    }
    return rb_mod_autoload(klass, 0, sym, file);
}

/*
 * MISSING: documentation
 */

static VALUE
rb_f_autoload_p(VALUE obj, SEL sel, VALUE sym)
{
    VALUE klass = (VALUE)rb_vm_get_current_class();
    if (klass == 0) {
	klass = rb_cObject;
    }
    return rb_mod_autoload_p(klass, 0, sym);
}

void
Init_load()
{
    const char *var_load_path = "$:";
    ID id_load_path = rb_intern(var_load_path);

    rbo_enabled = !ruby_is_miniruby && getenv("VM_DISABLE_RBO") == NULL;

    rb_define_virtual_variable("$:", rb_vm_load_path, 0);
    rb_alias_variable((rb_intern)("$-I"), id_load_path);
    rb_alias_variable((rb_intern)("$LOAD_PATH"), id_load_path);

    rb_define_virtual_variable("$\"", rb_vm_loaded_features, 0);
    rb_define_virtual_variable("$LOADED_FEATURES", rb_vm_loaded_features, 0);

    rb_objc_define_module_function(rb_mKernel, "load", rb_f_load, -1);
    rb_objc_define_module_function(rb_mKernel, "require", rb_f_require_imp, 1);
    rb_objc_define_method(rb_cModule, "autoload", rb_mod_autoload, 2);
    rb_objc_define_method(rb_cModule, "autoload?", rb_mod_autoload_p, 1);
    rb_objc_define_module_function(rb_mKernel, "autoload", rb_f_autoload, 2);
    rb_objc_define_module_function(rb_mKernel, "autoload?", rb_f_autoload_p, 1);

    rb_objc_define_module_function(rb_mKernel, "framework",
	    rb_require_framework, -1);
}
