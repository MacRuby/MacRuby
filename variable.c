/* 
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2007-2011, Apple Inc. All rights reserved.
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 * Copyright (C) 2000  Network Applied Communication Laboratory, Inc.
 * Copyright (C) 2000  Information-technology Promotion Agency, Japan
 */

#include "macruby_internal.h"
#include "ruby/node.h"
#include "ruby/st.h"
#include "ruby/util.h"
#include "id.h"
#include "vm.h"
#include "objc.h"
#include "class.h"

st_table *rb_global_tbl;
st_table *rb_class_tbl;
ID id_autoload = 0, id_classpath = 0, id_classid = 0, id_tmp_classpath = 0;

static const void *
retain_cb(CFAllocatorRef allocator, const void *v)
{
    GC_RETAIN(v);
    return v;
}

static void
release_cb(CFAllocatorRef allocator, const void *v)
{
    GC_RELEASE(v);
}

static void
ivar_dict_foreach(CFDictionaryRef dict, int (*func)(ANYARGS), VALUE farg)
{
    const long count = CFDictionaryGetCount(dict);
    if (count == 0) {
	return;
    }

    const void **keys = (const void **)malloc(sizeof(void *) * count);
    assert(keys != NULL);
    const void **values = (const void **)malloc(sizeof(void *) * count);
    assert(values != NULL);

    CFDictionaryGetKeysAndValues(dict, keys, values);

    for (long i = 0; i < count; i++) {
	if ((*func)(keys[i], values[i], farg) != ST_CONTINUE) {
	    break;
	}
    }

    free(keys);
    free(values);
}

static CFDictionaryValueCallBacks rb_cfdictionary_value_cb = {
    0, retain_cb, release_cb, NULL, NULL
};

static SEL selRequire = 0;

void
Init_var_tables(void)
{
    rb_global_tbl = st_init_numtable();
    GC_RETAIN(rb_global_tbl);
    rb_class_tbl = st_init_numtable();
    GC_RETAIN(rb_class_tbl);
    id_autoload = rb_intern("__autoload__");
    id_classpath = rb_intern("__classpath__");
    id_classid = rb_intern("__classid__");
    id_tmp_classpath = rb_intern("__tmp_classpath__");
    selRequire = sel_registerName("require:");
}

struct fc_result {
    ID name;
    VALUE klass;
    VALUE path;
    VALUE track;
    struct fc_result *prev;
};

static VALUE
fc_path(struct fc_result *fc, ID name)
{
    VALUE path, tmp;

    path = rb_str_dup(rb_id2str(name));
    while (fc) {
	if (fc->track == rb_cObject) break;
	if ((tmp = rb_attr_get(fc->track, id_classpath)) != Qnil) {
	    tmp = rb_str_dup(tmp);
	    rb_str_cat2(tmp, "::");
	    rb_str_append(tmp, path);
	    path = tmp;
	    break;
	}
	tmp = rb_str_dup(rb_id2str(fc->name));
	rb_str_cat2(tmp, "::");
	rb_str_append(tmp, path);
	path = tmp;
	fc = fc->prev;
    }
    OBJ_FREEZE(path);
    return path;
}

static int
fc_i(ID key, VALUE value, struct fc_result *res)
{
    if (!rb_is_const_id(key)) return ST_CONTINUE;

    if (value == res->klass) {
	res->path = fc_path(res, key);
	return ST_STOP;
    }
    switch (TYPE(value)) {
      case T_MODULE:
      case T_CLASS:
      {
	CFDictionaryRef iv_dict = rb_class_ivar_dict(value);
	if (iv_dict == NULL) return ST_CONTINUE;
	else {
	    struct fc_result arg;
	    struct fc_result *list;

	    list = res;
	    while (list) {
		if (list->track == value) return ST_CONTINUE;
		list = list->prev;
	    }

	    arg.name = key;
	    arg.path = 0;
	    arg.klass = res->klass;
	    arg.track = value;
	    arg.prev = res;
	    ivar_dict_foreach(iv_dict, fc_i, (VALUE)&arg);
	    if (arg.path) {
		res->path = arg.path;
		return ST_STOP;
	    }
	}
	break;
      }

      default:
	break;
    }
    return ST_CONTINUE;
}

static VALUE
find_class_path(VALUE klass)
{
    struct fc_result arg;

    arg.name = 0;
    arg.path = 0;
    arg.klass = klass;
    arg.track = rb_cObject;
    arg.prev = 0;

    CFMutableDictionaryRef iv_dict = rb_class_ivar_dict(rb_cObject);
    if (iv_dict != NULL) {
	ivar_dict_foreach(iv_dict, fc_i, (VALUE)&arg);
    }
    if (arg.path == 0) {
	st_foreach_safe(rb_class_tbl, fc_i, (st_data_t)&arg);
    }
    if (arg.path) {
	iv_dict = rb_class_ivar_dict_or_create(klass);
	CFDictionarySetValue(iv_dict, (const void *)id_classpath,
		(const void *)arg.path);
	CFDictionaryRemoveValue(iv_dict, (const void *)id_tmp_classpath);
	return arg.path;
    }
    if (!RCLASS_RUBY(klass)) {
	VALUE name = rb_str_new2(class_getName((Class)klass));
	iv_dict = rb_class_ivar_dict_or_create(klass);
	CFDictionarySetValue(iv_dict, (const void *)id_classpath,
		(const void *)name);
	CFDictionaryRemoveValue(iv_dict, (const void *)id_tmp_classpath);
	return name;
    }
    return Qnil;
}

static VALUE
classname(VALUE klass)
{
    VALUE path = Qnil;

    if (klass == 0) {
	klass = rb_cObject;
    }
    CFMutableDictionaryRef iv_dict = rb_class_ivar_dict(klass);
    if (iv_dict != NULL) {
	if (!CFDictionaryGetValueIfPresent((CFDictionaryRef)iv_dict, 
		    (const void *)id_classpath, (const void **)&path)) {
	    if (!CFDictionaryGetValueIfPresent((CFDictionaryRef)iv_dict, 
			(const void *)id_classid, (const void **)&path)) {
		return find_class_path(klass);
	    }
	    path = rb_str_dup(path);
	    OBJ_FREEZE(path);
	    CFDictionarySetValue(iv_dict, (const void *)id_classpath,
		    (const void *)path);
	    CFDictionaryRemoveValue(iv_dict, (const void *)id_classid);
	}
	if (TYPE(path) != T_STRING) {
	    rb_bug("class path is not set properly");
	}
	return path;
    }
    return find_class_path(klass);
}

/*
 *  call-seq:
 *     mod.name    => string
 *  
 *  Returns the name of the module <i>mod</i>.  Returns nil for anonymous modules.
 */

VALUE
rb_mod_name(VALUE mod, SEL sel)
{
    VALUE path = classname(mod);

    if (!NIL_P(path)) {
	return rb_str_dup(path);
    }
    return path;
}

VALUE
rb_class_path(VALUE klass)
{
    VALUE path = classname(klass);

    if (!NIL_P(path)) {
	return path;
    }
    if ((path = rb_attr_get(klass, id_tmp_classpath)) != Qnil) {
	return path;
    }
    else {
	const char *s = "Class";

	if (TYPE(klass) == T_MODULE) {
	    if (rb_obj_class(klass) == rb_cModule) {
		s = "Module";
	    }
	    else {
		s = rb_class2name(RBASIC(klass)->klass);
	    }
	}
	path = rb_sprintf("#<%s:%p>", s, (void*)klass);
	OBJ_FREEZE(path);
	rb_ivar_set(klass, id_tmp_classpath, path);

	return path;
    }
}

void
rb_set_class_path2(VALUE klass, VALUE under, const char *name, VALUE outer)
{
    VALUE str;

    if (under == rb_cObject) {
	str = rb_str_new2(name);
    }
    else {
	str = rb_str_dup(rb_class_path(under));
	rb_str_cat2(str, "::");
	rb_str_cat2(str, name);
    }
    OBJ_FREEZE(str);
    rb_ivar_set(klass, id_classpath, str);
}

void
rb_set_class_path(VALUE klass, VALUE under, const char *name)
{
    return rb_set_class_path2(klass, under, name, under);
}

VALUE
rb_path2class(const char *path)
{
    const char *pbeg, *p;
    ID id;
    VALUE c = rb_cObject;

    if (path[0] == '#') {
	rb_raise(rb_eArgError, "can't retrieve anonymous class %s", path);
    }
    pbeg = p = path;
    while (*p) {
	while (*p && *p != ':') p++;
	id = rb_intern2(pbeg, p-pbeg);
	if (p[0] == ':') {
	    if (p[1] != ':') goto undefined_class;
	    p += 2;
	    pbeg = p;
	}
	if (!rb_const_defined(c, id)) {
	  undefined_class:
	    rb_raise(rb_eArgError, "undefined class/module %.*s", (int)(p-path), path);
	}
	c = rb_const_get_at(c, id);
	switch (TYPE(c)) {
	  case T_MODULE:
	  case T_CLASS:
	    break;
	  default:
	    rb_raise(rb_eTypeError, "%s does not refer class/module", path);
	}
    }

    return c;
}

void
rb_name_class(VALUE klass, ID id)
{
    rb_ivar_set(klass, id_classid, ID2SYM(id));
}

VALUE
rb_class_name(VALUE klass)
{
    return rb_class_path(rb_class_real(klass, false));
}

const char *
rb_class2name(VALUE klass)
{
    return RSTRING_PTR(rb_class_name(klass));
}

const char *
rb_obj_classname(VALUE obj)
{
    return rb_class2name(rb_class_real(CLASS_OF(obj), true));
}

struct trace_var {
    int removed;
    void (*func)();
    VALUE data;
    struct trace_var *next;
};

struct global_variable {
    int   counter;
    void *data;
    VALUE (*getter)();
    void  (*setter)();
    void  (*marker)();
    int block_trace;
    struct trace_var *trace;
};

struct global_entry {
    struct global_variable *var;
    ID id;
};

static VALUE undef_getter(ID id);
static void  undef_setter(VALUE val, ID id, void *data, struct global_variable *var);
static void  undef_marker(void);

static VALUE val_getter(ID id, VALUE val);
static void  val_setter(VALUE val, ID id, void *data, struct global_variable *var);
static void  val_marker(VALUE data);

static VALUE var_getter(ID id, VALUE *var);
static void  var_setter(VALUE val, ID id, VALUE *var);
static void  var_marker(VALUE *var);

#define readonly_setter rb_gvar_readonly_setter

struct global_entry*
rb_global_entry(ID id)
{
    struct global_entry *entry;
    st_data_t data;

    if (!st_lookup(rb_global_tbl, id, &data)) {
	struct global_variable *var;
	entry = ALLOC(struct global_entry);
	var = ALLOC(struct global_variable);
	entry->id = id;
	GC_WB(&entry->var, var);
	var->counter = 1;
	var->data = 0;
	var->getter = undef_getter;
	var->setter = undef_setter;
	var->marker = undef_marker;

	var->block_trace = 0;
	var->trace = 0;
	st_add_direct(rb_global_tbl, id, (st_data_t)entry);
    }
    else {
	entry = (struct global_entry *)data;
    }
    return entry;
}

static VALUE
undef_getter(ID id)
{
    rb_warning("global variable `%s' not initialized", rb_id2name(id));

    return Qnil;
}

static void
undef_setter(VALUE val, ID id, void *data, struct global_variable *var)
{
    var->getter = val_getter;
    var->setter = val_setter;
    var->marker = val_marker;

    GC_WB(&var->data, val);
}

static void
undef_marker(void)
{
}

static VALUE
val_getter(ID id, VALUE val)
{
    return val;
}

static void
val_setter(VALUE val, ID id, void *data, struct global_variable *var)
{
    GC_WB(&var->data, val);
}

static void
val_marker(VALUE data)
{
    // Do nothing.
}

static VALUE
var_getter(ID id, VALUE *var)
{
    if (var == NULL) {
	return Qnil;
    }
    return *var;
}

static void
var_setter(VALUE val, ID id, VALUE *var)
{
    if (*var != val) {
	GC_RELEASE(*var);
	*var = val;
	GC_RETAIN(*var);
    }
}

static void
var_marker(VALUE *var)
{
    // Do nothing.
}

void
readonly_setter(VALUE val, ID id, void *var)
{
    rb_name_error(id, "%s is a read-only variable", rb_id2name(id));
}

static ID
global_id(const char *name)
{
    ID id;

    if (name[0] == '$') id = rb_intern(name);
    else {
	size_t len = strlen(name);
	char *buf = ALLOCA_N(char, len+1);
	buf[0] = '$';
	memcpy(buf+1, name, len);
	id = rb_intern2(buf, len+1);
    }
    return id;
}

void
rb_define_hooked_variable(const char *name, VALUE *var,
	VALUE (*getter)(ANYARGS), void  (*setter)(ANYARGS))
{
    ID id = global_id(name);
    struct global_variable *gvar = rb_global_entry(id)->var;
    gvar->data = (void *)var;
    gvar->getter = getter != NULL ? getter : var_getter;
    gvar->setter = setter != NULL ? setter : var_setter;
    gvar->marker = var_marker;
}

void
rb_define_variable(const char *name, VALUE *var)
{
    rb_define_hooked_variable(name, var, 0, 0);
}

void
rb_define_readonly_variable(const char *name, VALUE *var)
{
    rb_define_hooked_variable(name, var, 0, readonly_setter);
}

void
rb_define_virtual_variable(
    const char *name,
    VALUE (*getter)(ANYARGS),
    void  (*setter)(ANYARGS))
{
    if (!getter) getter = val_getter;
    if (!setter) setter = readonly_setter;
    rb_define_hooked_variable(name, 0, getter, setter);
}

static void
rb_trace_eval(VALUE cmd, VALUE val)
{
    rb_eval_cmd(cmd, rb_ary_new3(1, val), 0);
}

/*
 *  call-seq:
 *     trace_var(symbol, cmd )             => nil
 *     trace_var(symbol) {|val| block }    => nil
 *  
 *  Controls tracing of assignments to global variables. The parameter
 *  +symbol_ identifies the variable (as either a string name or a
 *  symbol identifier). _cmd_ (which may be a string or a
 *  +Proc+ object) or block is executed whenever the variable
 *  is assigned. The block or +Proc+ object receives the
 *  variable's new value as a parameter. Also see
 *  <code>Kernel::untrace_var</code>.
 *     
 *     trace_var :$_, proc {|v| puts "$_ is now '#{v}'" }
 *     $_ = "hello"
 *     $_ = ' there'
 *     
 *  <em>produces:</em>
 *     
 *     $_ is now 'hello'
 *     $_ is now ' there'
 */

VALUE rb_f_untrace_var(VALUE rcv, SEL sel, int argc, VALUE *argv);

VALUE
rb_f_trace_var(VALUE rcv, SEL sel,int argc, VALUE *argv)
{
    VALUE var, cmd;
    struct global_entry *entry;
    struct trace_var *trace;

    rb_secure(4);
    if (rb_scan_args(argc, argv, "11", &var, &cmd) == 1) {
	cmd = rb_block_proc();
    }
    if (NIL_P(cmd)) {
	return rb_f_untrace_var(0, 0, argc, argv);
    }
    entry = rb_global_entry(rb_to_id(var));
    if (OBJ_TAINTED(cmd)) {
	rb_raise(rb_eSecurityError, "Insecure: tainted variable trace");
    }
    trace = ALLOC(struct trace_var);
    trace->next = entry->var->trace;
    trace->func = rb_trace_eval;
    trace->data = cmd;
    trace->removed = 0;
    GC_WB(&entry->var->trace, trace);

    return Qnil;
}

static void
remove_trace(struct global_variable *var)
{
    struct trace_var *trace = var->trace;
    struct trace_var t;
    struct trace_var *next;

    t.next = trace;
    trace = &t;
    while (trace->next) {
	next = trace->next;
	if (next->removed) {
	    trace->next = next->next;
	    xfree(next);
	}
	else {
	    trace = next;
	}
    }
    var->trace = t.next;
}

/*
 *  call-seq:
 *     untrace_var(symbol [, cmd] )   => array or nil
 *  
 *  Removes tracing for the specified command on the given global
 *  variable and returns +nil+. If no command is specified,
 *  removes all tracing for that variable and returns an array
 *  containing the commands actually removed.
 */

VALUE
rb_f_untrace_var(VALUE rcv, SEL sel, int argc, VALUE *argv)
{
    VALUE var, cmd;
    ID id;
    struct global_entry *entry;
    struct trace_var *trace;
    st_data_t data;

    rb_secure(4);
    rb_scan_args(argc, argv, "11", &var, &cmd);
    id = rb_to_id(var);
    if (!st_lookup(rb_global_tbl, id, &data)) {
	rb_name_error(id, "undefined global variable %s", rb_id2name(id));
    }

    trace = (entry = (struct global_entry *)data)->var->trace;
    if (NIL_P(cmd)) {
	VALUE ary = rb_ary_new();

	while (trace) {
	    struct trace_var *next = trace->next;
	    rb_ary_push(ary, (VALUE)trace->data);
	    trace->removed = 1;
	    trace = next;
	}

	if (!entry->var->block_trace) remove_trace(entry->var);
	return ary;
    }
    else {
	while (trace) {
	    if (trace->data == cmd) {
		trace->removed = 1;
		if (!entry->var->block_trace) remove_trace(entry->var);
		return rb_ary_new3(1, cmd);
	    }
	    trace = trace->next;
	}
    }
    return Qnil;
}

VALUE
rb_gvar_get(struct global_entry *entry)
{
    struct global_variable *var = entry->var;
    return (*var->getter)(entry->id, var->data, var);
}

struct trace_data {
    struct trace_var *trace;
    VALUE val;
};

static VALUE
trace_ev(struct trace_data *data)
{
    struct trace_var *trace = data->trace;

    while (trace) {
	(*trace->func)(trace->data, data->val);
	trace = trace->next;
    }
    return Qnil;		/* not reached */
}

static VALUE
trace_en(struct global_variable *var)
{
    var->block_trace = 0;
    remove_trace(var);
    return Qnil;		/* not reached */
}

VALUE
rb_gvar_set(struct global_entry *entry, VALUE val)
{
    struct trace_data trace;
    struct global_variable *var = entry->var;

    if (rb_safe_level() >= 4)
	rb_raise(rb_eSecurityError, "Insecure: can't change global variable value");
    (*var->setter)(val, entry->id, var->data, var);

    if (var->trace && !var->block_trace) {
	var->block_trace = 1;
	trace.trace = var->trace;
	trace.val = val;
	rb_ensure(trace_ev, (VALUE)&trace, trace_en, (VALUE)var);
    }
    return val;
}

VALUE
rb_gv_set(const char *name, VALUE val)
{
    struct global_entry *entry;

    entry = rb_global_entry(global_id(name));
    return rb_gvar_set(entry, val);
}

VALUE
rb_gv_get(const char *name)
{
    struct global_entry *entry;

    entry = rb_global_entry(global_id(name));
    return rb_gvar_get(entry);
}

VALUE
rb_gvar_defined(struct global_entry *entry)
{
    if (entry->var->getter == undef_getter) return Qfalse;
    return Qtrue;
}

static int
gvar_i(ID key, struct global_entry *entry, VALUE ary)
{
    rb_ary_push(ary, ID2SYM(key));
    return ST_CONTINUE;
}

/*
 *  call-seq:
 *     global_variables    => array
 *  
 *  Returns an array of the names of global variables.
 *     
 *     global_variables.grep /std/   #=> [:$stdin, :$stdout, :$stderr]
 */

VALUE
rb_f_global_variables(VALUE rcv, SEL sel)
{
    VALUE ary = rb_ary_new();
    char buf[2];
    int i;

    st_foreach_safe(rb_global_tbl, gvar_i, ary);
    buf[0] = '$';
    for (i = 1; i <= 9; ++i) {
	buf[1] = (char)(i + '0');
	rb_ary_push(ary, ID2SYM(rb_intern2(buf, 2)));
    }
    return ary;
}

void
rb_alias_variable(ID name1, ID name2)
{
    struct global_entry *entry1, *entry2;
    st_data_t data1;

    if (rb_safe_level() >= 4)
	rb_raise(rb_eSecurityError, "Insecure: can't alias global variable");

    entry2 = rb_global_entry(name2);
    if (!st_lookup(rb_global_tbl, name1, &data1)) {
 	entry1 = ALLOC(struct global_entry);
	entry1->id = name1;
	st_add_direct(rb_global_tbl, name1, (st_data_t)entry1);
    }
    else if ((entry1 = (struct global_entry *)data1)->var != entry2->var) {
	struct global_variable *var = entry1->var;
	if (var->block_trace) {
	    rb_raise(rb_eRuntimeError, "can't alias in tracer");
	}
	var->counter--;
	if (var->counter == 0) {
	    struct trace_var *trace = var->trace;
	    while (trace) {
		struct trace_var *next = trace->next;
		xfree(trace);
		trace = next;
	    }
	    xfree(var);
	}
    }
    else {
	return;
    }
    entry2->var->counter++;
    entry1->var = entry2->var;
}

static CFMutableDictionaryRef generic_iv_dict = NULL;

static CFMutableDictionaryRef
generic_ivar_dict(VALUE obj, bool create)
{
    CFMutableDictionaryRef obj_dict = NULL;
    if (SPECIAL_CONST_P(obj)) {
	if (generic_iv_dict == NULL) {	
	    generic_iv_dict = CFDictionaryCreateMutable(NULL, 0, NULL,
		    &kCFTypeDictionaryValueCallBacks);
	}
	if (!CFDictionaryGetValueIfPresent(generic_iv_dict, 
		    (const void *)obj, (const void **)&obj_dict) && create) {
	    obj_dict = CFDictionaryCreateMutable(NULL, 0, NULL,
		    &rb_cfdictionary_value_cb);
	    CFMakeCollectable(obj_dict);
	    CFDictionarySetValue(generic_iv_dict, (const void *)obj,
		    (const void *)obj_dict);	
	}
    }
    else {
	obj_dict = rb_objc_get_associative_ref((void *)obj, &generic_iv_dict);
	if (obj_dict == NULL && create) {
	    obj_dict = CFDictionaryCreateMutable(NULL, 0, NULL,
		    &rb_cfdictionary_value_cb);
	    CFMakeCollectable(obj_dict);
	    rb_objc_set_associative_ref((void *)obj, &generic_iv_dict,
		    (void *)obj_dict);
	}
    }
    return obj_dict;
}

static void
generic_ivar_dict_set(VALUE obj, CFMutableDictionaryRef obj_dict)
{
    if (SPECIAL_CONST_P(obj)) {
	if (generic_iv_dict == NULL) {	
	    generic_iv_dict = CFDictionaryCreateMutable(NULL, 0, NULL,
		    &kCFTypeDictionaryValueCallBacks);
	}
	CFDictionarySetValue(generic_iv_dict, (const void *)obj,
		(const void *)obj_dict);	
    }
    else {
	rb_objc_set_associative_ref((void *)obj, &generic_iv_dict,
		(void *)obj_dict);
    }
}

static VALUE
generic_ivar_get(VALUE obj, ID id, bool warn, bool undef)
{
    CFDictionaryRef obj_dict = generic_ivar_dict(obj, false);
    if (obj_dict != NULL) {
	VALUE val;
	if (CFDictionaryGetValueIfPresent(obj_dict, (const void *)id, 
		    (const void **)&val)) {
	    return val;
	}
    }
    if (warn) {
	rb_warning("instance variable %s not initialized", rb_id2name(id));
    }
    return undef ? Qundef : Qnil;
}

static void
generic_ivar_set(VALUE obj, ID id, VALUE val)
{
    if (rb_special_const_p(obj)) {
	if (rb_obj_frozen_p(obj)) {
	    rb_error_frozen("object");
	}
    }
    CFMutableDictionaryRef obj_dict = generic_ivar_dict(obj, true);
//printf("generic_ivar_set %p %ld %p dict %p\n", (void*)obj,id,(void*)val,obj_dict);
    CFDictionarySetValue(obj_dict, (const void *)id, (const void *)val);
}

static VALUE
generic_ivar_defined(VALUE obj, ID id)
{
    CFDictionaryRef obj_dict = generic_ivar_dict(obj, false);
    if (obj_dict != NULL) {
	if (CFDictionaryGetValueIfPresent(obj_dict, (const void *)id, NULL)) {
	    return Qtrue;
	}
    }
    return Qfalse;    
}

static bool
generic_ivar_remove(VALUE obj, ID id, VALUE *valp)
{
    CFMutableDictionaryRef obj_dict = generic_ivar_dict(obj, false);
    if (obj_dict != NULL) {
	VALUE val;
	if (CFDictionaryGetValueIfPresent(obj_dict, (const void *)id, 
		    (const void **)&val)) {
	    *valp = val;
	    CFDictionaryRemoveValue(obj_dict, (const void *)id);
	    return true;
	}
    }
    return false;
}

void
rb_copy_generic_ivar(VALUE clone, VALUE obj)
{
    CFMutableDictionaryRef obj_dict = generic_ivar_dict(obj, false);
    if (obj_dict != NULL) {
	generic_ivar_dict_set(clone, obj_dict);
    }
}

CFMutableDictionaryRef 
rb_class_ivar_dict(VALUE mod)
{
    return generic_ivar_dict(mod, false);
}
 
void
rb_class_ivar_set_dict(VALUE mod, CFMutableDictionaryRef dict)
{
    generic_ivar_dict_set(mod, dict);
}

void
merge_ivars(const void *key, const void *val, void *ctx)
{
    ID id = (ID)key;
    if (id == id_classpath || id == id_classid
	    || id == idIncludedModules || id == idIncludedInClasses) {
	return;
    }
    CFMutableDictionaryRef dest_dict = (CFMutableDictionaryRef)ctx;
    CFDictionarySetValue(dest_dict, key, val);
}

void
rb_class_merge_ivar_dicts(VALUE orig_class, VALUE dest_class)
{
    CFMutableDictionaryRef orig_dict = rb_class_ivar_dict(orig_class);
    if (orig_dict != NULL) {
	CFMutableDictionaryRef dest_dict =
	    rb_class_ivar_dict_or_create(dest_class);
	CFDictionaryApplyFunction(orig_dict, merge_ivars, dest_dict);
    }
}

CFMutableDictionaryRef
rb_class_ivar_dict_or_create(VALUE mod)
{
    CFMutableDictionaryRef dict;

    dict = rb_class_ivar_dict(mod);
    if (dict == NULL) {
	dict = CFDictionaryCreateMutable(NULL, 0, NULL, &rb_cfdictionary_value_cb);
	rb_class_ivar_set_dict(mod, dict);
	CFMakeCollectable(dict);
    }
    return dict;
}

static VALUE
ivar_get(VALUE obj, ID id, bool warn, bool undef)
{
    VALUE val;

    switch (TYPE(obj)) {
	case T_OBJECT:
	    {
		const int slot = rb_vm_get_ivar_slot(obj, id, false);
		if (slot != -1) {
		    val = rb_vm_get_ivar_from_slot(obj, slot);
		    if (val != Qundef) {
			return val;
		    }
		}
	    }
	    break;

	case T_CLASS:
	case T_MODULE:
	    {
		CFDictionaryRef iv_dict = rb_class_ivar_dict(obj);
		if (iv_dict != NULL 
			&& CFDictionaryGetValueIfPresent(
			    iv_dict, (const void *)id, (const void **)&val)) {
		    return val;
		}
	    }
	    break;

	case T_NATIVE:
	default:
	    return generic_ivar_get(obj, id, warn, undef);
    }
    if (warn) {
	rb_warning("instance variable %s not initialized", rb_id2name(id));
    }
    return undef ? Qundef : Qnil;
}

VALUE
rb_ivar_get(VALUE obj, ID id)
{
    return ivar_get(obj, id, true, false);
}

VALUE
rb_attr_get(VALUE obj, ID id)
{
    return ivar_get(obj, id, false, false);
}

VALUE
rb_ivar_set(VALUE obj, ID id, VALUE val)
{
    if (!OBJ_TAINTED(obj) && rb_safe_level() >= 4) {
	rb_raise(rb_eSecurityError, "Insecure: can't modify instance variable");
    }
    if (OBJ_FROZEN(obj)) {
	rb_error_frozen("object");
    }

    switch (TYPE(obj)) {
	case T_OBJECT:
	    {
		const int slot = rb_vm_get_ivar_slot(obj, id, true);
		assert(slot >= 0);
		rb_vm_set_ivar_from_slot(obj, val, slot);
		return val;
	    }
	    break;

	case T_CLASS:
	case T_MODULE:
	    {
		CFMutableDictionaryRef iv_dict = rb_class_ivar_dict_or_create(obj);
		CFDictionarySetValue(iv_dict, (const void *)id, (const void *)val);
	    }
	    break;

	case T_NATIVE:
	default:
	    generic_ivar_set(obj, id, val);
	    break;
    }
    return val;
}

VALUE
rb_ivar_defined(VALUE obj, ID id)
{
    switch (TYPE(obj)) {
	case T_OBJECT:
	    {
		const int slot = rb_vm_get_ivar_slot(obj, id, false);
		if (slot != -1) {
		    if (rb_vm_get_ivar_from_slot(obj, slot) != Qundef) {
			return Qtrue;
		    }
		}
	    }
	    break;

	case T_CLASS:
	case T_MODULE:
	    {
		CFDictionaryRef iv_dict = rb_class_ivar_dict(obj);
		if (iv_dict != NULL && CFDictionaryGetValueIfPresent(iv_dict, (const void *)id, NULL)) {
		    return Qtrue;
		}
		break;
	    }

	case T_NATIVE:
	default:
	    return generic_ivar_defined(obj, id);
    }
    return Qfalse;
}

void
rb_ivar_foreach(VALUE obj, int (*func)(ANYARGS), st_data_t arg)
{
    switch (TYPE(obj)) {
	case T_OBJECT:
	    for (unsigned int i = 0; i < ROBJECT(obj)->num_slots; i++) {
		ID name = ROBJECT(obj)->slots[i].name;
		VALUE value = ROBJECT(obj)->slots[i].value;
		if (name != 0 && value != Qundef) {
		    func(name, value, arg);
		}
	    }
	    break;

      case T_CLASS:
      case T_MODULE:
	    {
		CFMutableDictionaryRef iv_dict = rb_class_ivar_dict(obj);
		if (iv_dict != NULL) {
		    ivar_dict_foreach(iv_dict, func, arg);
		}
	    }
	    break;

      case T_NATIVE:
      default:
	    {
		CFDictionaryRef obj_dict = generic_ivar_dict(obj, false);
		if (obj_dict != NULL) {
		    CFDictionaryApplyFunction(obj_dict,
			    (CFDictionaryApplierFunction)func, (void *)arg);
		}
	    }
	    break;
    }
}

static int
ivar_i(ID key, VALUE val, VALUE ary)
{
    rb_ary_push(ary, ID2SYM(key));
    return ST_CONTINUE;
}

/*
 *  call-seq:
 *     obj.instance_variables    => array
 *  
 *  Returns an array of instance variable names for the receiver. Note
 *  that simply defining an accessor does not create the corresponding
 *  instance variable.
 *     
 *     class Fred
 *       attr_accessor :a1
 *       def initialize
 *         @iv = 3
 *       end
 *     end
 *     Fred.new.instance_variables   #=> [:@iv]
 */

VALUE
rb_obj_instance_variables(VALUE obj)
{
    VALUE ary;

    ary = rb_ary_new();
    rb_ivar_foreach(obj, ivar_i, ary);
    return ary;
}

/*
 *  call-seq:
 *     obj.remove_instance_variable(symbol)    => obj
 *  
 *  Removes the named instance variable from <i>obj</i>, returning that
 *  variable's value.
 *     
 *     class Dummy
 *       attr_reader :var
 *       def initialize
 *         @var = 99
 *       end
 *       def remove
 *         remove_instance_variable(:@var)
 *       end
 *     end
 *     d = Dummy.new
 *     d.var      #=> 99
 *     d.remove   #=> 99
 *     d.var      #=> nil
 */

VALUE
rb_obj_remove_instance_variable(VALUE obj, SEL sel, VALUE name)
{
    VALUE val = Qnil;
    ID id = rb_to_id(name);

    if (!OBJ_TAINTED(obj) && rb_safe_level() >= 4) {
	rb_raise(rb_eSecurityError, "Insecure: can't modify instance variable");
    }
    if (OBJ_FROZEN(obj)) {
	rb_error_frozen("object");
    }
    if (!rb_is_instance_id(id)) {
	rb_name_error(id, "`%s' is not allowed as an instance variable name", rb_id2name(id));
    }

    switch (TYPE(obj)) {
	case T_OBJECT:
	    {
		const int slot = rb_vm_get_ivar_slot(obj, id, false);
		if (slot != -1) {
		    val = rb_vm_get_ivar_from_slot(obj, slot);
		    if (val != Qundef) {
			rb_vm_set_ivar_from_slot(obj, Qundef, slot);
			return val;
		    }
		}
	    }
	    break;

	case T_CLASS:
	case T_MODULE:
	    {
		CFMutableDictionaryRef iv_dict = rb_class_ivar_dict(obj);
		if (iv_dict != NULL && CFDictionaryGetValueIfPresent(iv_dict, (const void *)id, (const void **)&val)) {
		    CFDictionaryRemoveValue(iv_dict, (const void *)id);
		    return val;
		}	      
	    }
	    break;

	default:
	    if (generic_ivar_remove(obj, id, &val)) {
		return val;
	    }
	    break;
    }
    rb_name_error(id, "instance variable %s not defined", rb_id2name(id));
    return Qnil;		/* not reached */
}

NORETURN(static void uninitialized_constant(VALUE, ID));
static void
uninitialized_constant(VALUE klass, ID id)
{
    if (klass && klass != rb_cObject)
	rb_name_error(id, "uninitialized constant %s::%s",
		      rb_class2name(klass),
		      rb_id2name(id));
    else {
	rb_name_error(id, "uninitialized constant %s", rb_id2name(id));
    }
}

static VALUE
call_const_missing(VALUE klass, ID id)
{
    VALUE arg = ID2SYM(id);
    return rb_vm_call(klass, selConstMissing, 1, &arg);
}

/*
 * call-seq:
 *    mod.const_missing(sym)    => obj
 *
 *  Invoked when a reference is made to an undefined constant in
 *  <i>mod</i>. It is passed a symbol for the undefined constant, and
 *  returns a value to be used for that constant. The
 *  following code is a (very bad) example: if reference is made to
 *  an undefined constant, it attempts to load a file whose name is
 *  the lowercase version of the constant (thus class <code>Fred</code> is
 *  assumed to be in file <code>fred.rb</code>). If found, it returns the
 *  value of the loaded class. It therefore implements a perverse
 *  kind of autoload facility.
 *  
 *    def Object.const_missing(name)
 *      @looked_for ||= {}
 *      str_name = name.to_s
 *      raise "Class not found: #{name}" if @looked_for[str_name]
 *      @looked_for[str_name] = 1
 *      file = str_name.downcase
 *      require file
 *      klass = const_get(name)
 *      return klass if klass
 *      raise "Class not found: #{name}"
 *    end
 *  
 */

VALUE
rb_mod_const_missing(VALUE klass, SEL sel, VALUE name)
{
    //rb_frame_pop(); /* pop frame for "const_missing" */
    uninitialized_constant(klass, rb_to_id(name));
    return Qnil;		/* not reached */
}

static struct st_table *
check_autoload_table(VALUE av)
{
    Check_Type(av, T_DATA);
    if (false) {
	// TODO
	VALUE desc = rb_inspect(av);
	rb_raise(rb_eTypeError, "wrong autoload table: %s", RSTRING_PTR(desc));
    }
    return (struct st_table *)DATA_PTR(av);
}

void
rb_autoload(VALUE mod, ID id, const char *file)
{
    if (!rb_is_const_id(id)) {
	rb_raise(rb_eNameError, "autoload must be constant name: %s",
		rb_id2name(id));
    }
    if (!file || !*file) {
	rb_raise(rb_eArgError, "empty file name");
    }

    VALUE av;
    if ((av = rb_attr_get(mod, id)) != Qnil && av != Qundef) {
	return;
    }

    rb_const_set(mod, id, Qundef);

    struct st_table *tbl;
    if ((av = rb_attr_get(mod, id_autoload)) != Qnil) {
	tbl = check_autoload_table(av);
    }
    else {
	av = Data_Wrap_Struct(rb_cData, NULL, NULL, 0);
	rb_ivar_set(mod, id_autoload, av);
	tbl = st_init_numtable();
	GC_WB(&DATA_PTR(av), tbl);
    }

    VALUE fn = rb_str_new2(file);
    rb_obj_untaint(fn);
    OBJ_FREEZE(fn);
    NODE *n = rb_node_newnode(NODE_MEMO, fn, rb_safe_level(), 0);
    GC_RELEASE(n);
    st_insert(tbl, id, (st_data_t)n);
}

static NODE *
autoload_delete(VALUE mod, ID id)
{
    VALUE val;
    st_data_t load = 0;

    CFMutableDictionaryRef iv_dict = rb_class_ivar_dict(mod);
    assert(iv_dict != NULL);
    CFDictionaryRemoveValue(iv_dict, (const void *)id);
    if (CFDictionaryGetValueIfPresent((CFDictionaryRef)iv_dict, 
		(const void *)id_autoload, (const void **)&val)) {
	struct st_table *tbl = check_autoload_table(val);
	st_delete(tbl, (st_data_t*)&id, &load);
	if (tbl->num_entries == 0) {
	    DATA_PTR(val) = NULL;
	    st_free_table(tbl);
	    CFDictionaryRemoveValue(iv_dict, (const void *)id_autoload);
	}
    }
    return (NODE *)load;
}

VALUE
rb_autoload_load(VALUE klass, ID id)
{
    VALUE file;
    NODE *load = autoload_delete(klass, id);

    if (!load || !(file = load->nd_lit)) {
	return Qfalse;
    }
    return rb_require_safe(file, load->nd_nth);
}

static VALUE
autoload_file(VALUE mod, ID id)
{
    VALUE val;
    struct st_table *tbl;
    st_data_t load;

    CFMutableDictionaryRef iv_dict = rb_class_ivar_dict(mod);
    assert(iv_dict != NULL);
    if (!CFDictionaryGetValueIfPresent((CFDictionaryRef)iv_dict, 
		(const void *)id_autoload, (const void **)&val)
	    || (tbl = check_autoload_table(val)) == NULL
	    || !st_lookup(tbl, id, &load)) {
	return Qnil;
    }

    VALUE file = ((NODE *)load)->nd_lit;
    Check_Type(file, T_STRING);
    if (RSTRING_LEN(file) == 0) {
	rb_raise(rb_eArgError, "empty file name");
    }
    if (!rb_provided(RSTRING_PTR(file))) {
	return file;
    }

    // Already loaded but not defined.
    st_delete(tbl, (st_data_t *)&id, 0);
    if (tbl->num_entries == 0) {
	DATA_PTR(val) = 0;
	st_free_table(tbl);
	CFDictionaryRemoveValue(iv_dict, (const void *)id_autoload);
    }
    return Qnil;
}

VALUE
rb_autoload_p(VALUE mod, ID id)
{
    CFDictionaryRef iv_dict = (CFDictionaryRef)rb_class_ivar_dict(mod);
    VALUE val;

    if (iv_dict == NULL 
	|| !CFDictionaryGetValueIfPresent(iv_dict, (const void *)id, (const void **)&val)
	|| val != Qundef)
	return Qnil;
    return autoload_file(mod, id);
}

static VALUE
retrieve_dynamic_objc_class(VALUE klass, ID name)
{
    // The Objective-C class dynamic resolver. By default, MacRuby doesn't
    // know about native classes. They will be resolved and added into the
    // NSObject dictionary on demand.
    Class k = (Class)objc_getClass(rb_id2name(name));
    if (k != NULL && !RCLASS_RUBY(k)) {
	// Skip classes that aren't pure Objective-C, to avoid namespace
	// conflicts in Ruby land.
	CFMutableDictionaryRef dict = rb_class_ivar_dict_or_create(rb_cObject);
	if (!CFDictionaryContainsKey(dict, (const void *)name)) {
	    CFDictionarySetValue(dict, (const void *)name, (const void *)k);
	    rb_objc_force_class_initialize(k);
	}
	return (VALUE)k;
    }
    return Qnil;
}

static VALUE
rb_const_get_0(VALUE klass, ID id, int exclude, int recurse)
{
    VALUE value, tmp;
    int mod_retry = 0;

    tmp = klass;
retry:
    while (RTEST(tmp)) {
	CFDictionaryRef iv_dict;
	while ((iv_dict = rb_class_ivar_dict(tmp)) != NULL
		&& CFDictionaryGetValueIfPresent(iv_dict, (const void *)id,
		    (const void **)&value)) {
	    if (value == Qundef) {
		if (!RTEST(rb_autoload_load(tmp, id))) {
		    break;
		}
		goto retry;
	    }
	    if (exclude && tmp == rb_cObject && klass != rb_cObject) {
		rb_warn("toplevel constant %s referenced by %s::%s",
			rb_id2name(id), rb_class2name(klass), rb_id2name(id));
	    }
	    return rb_vm_resolve_const_value(value, klass, id);
	}
	if (!recurse && klass != rb_cObject) {
	    break;
	}
	VALUE inc_mods = rb_attr_get(tmp, idIncludedModules);
	if (inc_mods != Qnil) {
	    int i, count = RARRAY_LEN(inc_mods);
	    for (i = 0; i < count; i++) {
		VALUE mod = RARRAY_AT(inc_mods, i);
		iv_dict = rb_class_ivar_dict(mod);
		if (iv_dict != NULL && CFDictionaryGetValueIfPresent(iv_dict, (const void *)id,
			    (const void **)&value)) {
 		    if (value == Qundef) {
			if (!RTEST(rb_autoload_load(mod, id))) {
			    break;
			}
			goto retry;
		    }
		    return rb_vm_resolve_const_value(value, klass, id);
		}
	    }
	}
	tmp = RCLASS_SUPER(tmp);
    }
    if (!exclude && !mod_retry && RCLASS_MODULE(klass)) {
	mod_retry = 1;
	tmp = rb_cObject;
	goto retry;
    }
    VALUE k = retrieve_dynamic_objc_class(klass, id);
    if (k != Qnil) {
	return k;
    }
    return call_const_missing(klass, id);
}

VALUE
rb_const_get_from(VALUE klass, ID id)
{
    return rb_const_get_0(klass, id, Qtrue, Qtrue);
}

VALUE
rb_const_get(VALUE klass, ID id)
{
    return rb_const_get_0(klass, id, Qfalse, Qtrue);
}

VALUE
rb_const_get_at(VALUE klass, ID id)
{
    return rb_const_get_0(klass, id, Qtrue, Qfalse);
}

/*
 *  call-seq:
 *     remove_const(sym)   => obj
 *  
 *  Removes the definition of the given constant, returning that
 *  constant's value. Predefined classes and singleton objects (such as
 *  <i>true</i>) cannot be removed.
 */

VALUE
rb_mod_remove_const(VALUE mod, VALUE name)
{
    ID id = rb_to_id(name);
    VALUE val;

    if (!rb_is_const_id(id)) {
	rb_name_error(id, "`%s' is not allowed as a constant name", rb_id2name(id));
    }
    if (!OBJ_TAINTED(mod) && rb_safe_level() >= 4) {
	rb_raise(rb_eSecurityError, "Insecure: can't remove constant");
    }
    if (OBJ_FROZEN(mod)) {
	rb_error_frozen("class/module");
    }

    CFMutableDictionaryRef iv_dict = rb_class_ivar_dict(mod);
    if (iv_dict != NULL && CFDictionaryGetValueIfPresent((CFDictionaryRef)iv_dict, (const void *)id, (const void **)&val)) {
	CFDictionaryRemoveValue(iv_dict, (const void *)id);
	if (val == Qundef) {
	    autoload_delete(mod, id);
	    val = Qnil;
	}
	return val;
    }
    if (rb_const_defined_at(mod, id)) {
	rb_name_error(id, "cannot remove %s::%s",
		 rb_class2name(mod), rb_id2name(id));
    }
    rb_name_error(id, "constant %s::%s not defined",
		  rb_class2name(mod), rb_id2name(id));
    return Qnil;		/* not reached */
}

static int
sv_i(ID key, VALUE value, st_table *tbl)
{
    if (rb_is_const_id(key)) {
	if (!st_lookup(tbl, key, 0)) {
	    st_insert(tbl, key, key);
	}
    }
    return ST_CONTINUE;
}

void*
rb_mod_const_at(VALUE mod, void *data)
{
    st_table *tbl = data;
    if (!tbl) {
	tbl = st_init_numtable();
    }
    CFMutableDictionaryRef iv_dict = rb_class_ivar_dict(mod);
    if (iv_dict != NULL) {
	ivar_dict_foreach(iv_dict, sv_i, (VALUE)tbl);
    }
    return tbl;
}

void*
rb_mod_const_of(VALUE mod, void *data)
{
    VALUE tmp = mod;
    for (;;) {
	data = rb_mod_const_at(tmp, data);
	tmp = RCLASS_SUPER(tmp);
	if (tmp == 0) {
	    break;
	}
	if (tmp == rb_cObject && mod != rb_cObject) {
	    break;
	}
    }
    return data;
}

static int
list_i(ID key, ID value, VALUE ary)
{
    rb_ary_push(ary, ID2SYM(key));
    return ST_CONTINUE;
}

VALUE
rb_const_list(void *data)
{
    st_table *tbl = data;
    VALUE ary;

    if (tbl == NULL) {
	return rb_ary_new2(0);
    }
    ary = rb_ary_new2(tbl->num_entries);
    st_foreach_safe(tbl, list_i, ary);
    st_free_table(tbl);

    return ary;
}

/*
 *  call-seq:
 *     mod.constants(inherit=true)    => array
 *  
 *  Returns an array of the names of the constants accessible in
 *  <i>mod</i>. This includes the names of constants in any included
 *  modules (example at start of section), unless the <i>all</i>
 *  parameter is set to <code>false</code>.
 *
 *    IO.constants.include?(:SYNC)         => true
 *    IO.constants(false).include?(:SYNC)  => false
 *
 *  Also see <code>Module::const_defined?</code>.
 */

VALUE
rb_mod_constants(VALUE mod, SEL sel, int argc, VALUE *argv)
{
    VALUE inherit;
    st_table *tbl;

    if (argc == 0) {
	inherit = Qtrue;
    }
    else {
	rb_scan_args(argc, argv, "01", &inherit);
    }
    if (RTEST(inherit)) {
	tbl = rb_mod_const_of(mod, 0);
    }
    else {
	tbl = rb_mod_const_at(mod, 0);
    }
    return rb_const_list(tbl);
}

static int
rb_const_defined_0(VALUE klass, ID id, int exclude, int recurse)
{
    VALUE value, tmp;
    int mod_retry = 0;

    tmp = klass;
  retry:
    while (tmp) {
	CFDictionaryRef iv_dict = rb_class_ivar_dict(tmp);
	if (iv_dict != NULL && CFDictionaryGetValueIfPresent(iv_dict, 
	    (const void *)id, (const void **)&value)) {
	    if (value == Qundef && NIL_P(autoload_file(klass, id))) {
		return Qfalse;
	    }
	    return Qtrue;
	}
	if (!recurse && klass != rb_cObject) {
	    break;
	}
	VALUE inc_mods = rb_attr_get(tmp, idIncludedModules);
	if (inc_mods != Qnil) {
	    int i, count = RARRAY_LEN(inc_mods);
	    for (i = 0; i < count; i++) {
		iv_dict = rb_class_ivar_dict(RARRAY_AT(inc_mods, i));
		if (iv_dict != NULL && CFDictionaryGetValueIfPresent(iv_dict, (const void *)id, (const void **)&value))
		    return Qtrue;
	    }
	}
	tmp = RCLASS_SUPER(tmp);
    }
    if (!exclude && !mod_retry && RCLASS_MODULE(klass)) {
	mod_retry = 1;
	tmp = rb_cObject;
	goto retry;
    }
    VALUE k = retrieve_dynamic_objc_class(klass, id);
    if (k != Qnil) {
	return Qtrue;
    }
    return Qfalse;
}

int
rb_const_defined_from(VALUE klass, ID id)
{
    return rb_const_defined_0(klass, id, Qtrue, Qtrue);
}

int
rb_const_defined(VALUE klass, ID id)
{
    return rb_const_defined_0(klass, id, Qfalse, Qtrue);
}

int
rb_const_defined_at(VALUE klass, ID id)
{
    return rb_const_defined_0(klass, id, Qtrue, Qfalse);
}

static void
mod_av_set(VALUE klass, ID id, VALUE val, int isconst)
{
    const char *dest = isconst ? "constant" : "class variable";

    if (!OBJ_TAINTED(klass) && rb_safe_level() >= 4) {
	rb_raise(rb_eSecurityError, "Insecure: can't set %s", dest);
    }
    if (OBJ_FROZEN(klass)) {
	if (TYPE(klass) == T_MODULE) {
	    rb_error_frozen("module");
	}
	else {
	    rb_error_frozen("class");
	}
    }
    CFMutableDictionaryRef iv_dict = rb_class_ivar_dict_or_create(klass);
    if (isconst) {
	VALUE value = Qfalse;

	if (CFDictionaryGetValueIfPresent((CFDictionaryRef)iv_dict, (const void *)id, (const void **)&value)) {
	    if (value == Qundef) {
		autoload_delete(klass, id);
	    }
	    else {
		rb_warn("already initialized %s %s", dest, rb_id2name(id));
	    }
	}
    }

    CFDictionarySetValue(iv_dict, (const void *)id, (const void *)val);
    rb_vm_const_is_defined(id);
}

void
rb_const_set(VALUE klass, ID id, VALUE val)
{
    if (NIL_P(klass)) {
	rb_raise(rb_eTypeError, "no class/module to define constant %s",
		 rb_id2name(id));
    }
    mod_av_set(klass, id, val, Qtrue);
}

void
rb_define_const(VALUE klass, const char *name, VALUE val)
{
    ID id = rb_intern(name);

    if (!rb_is_const_id(id)) {
	rb_warn("rb_define_const: invalid name `%s' for constant", name);
    }
    if (klass == rb_cObject) {
	rb_secure(4);
    }
    rb_const_set(klass, id, val);
}

void
rb_define_global_const(const char *name, VALUE val)
{
    rb_define_const(rb_cObject, name, val);
}

static inline VALUE
unmeta_class(VALUE klass)
{
    if (RCLASS_META(klass)) {
	klass = (VALUE)objc_getClass(class_getName((Class)klass));
    }
    return klass;
}

void
rb_cvar_set(VALUE klass, ID id, VALUE val)
{
    klass = unmeta_class(klass);

    // Locate the class where the cvar should be set by looking through the
    // current class ancestry.
    VALUE k = klass;
    while (k != 0) {
	if (ivar_get(k, id, false, true) != Qundef) {
	    klass = k;
	    break;
	}
	k = RCLASS_SUPER(k);
    }

    rb_ivar_set(klass, id, val);
}

static VALUE
rb_cvar_get3(VALUE klass, ID id, bool check, bool defined)
{
    VALUE orig = klass;
    klass = unmeta_class(klass);

    // Locate the cvar by looking through the class ancestry.
    while (klass != 0) {
	VALUE value = ivar_get(klass, id, false, true);
	if (value != Qundef) {
	    return defined ? Qtrue : value;
	}
	klass = RCLASS_SUPER(klass);
    }

    if (check) {
	rb_name_error(id,"uninitialized class variable %s in %s",
		rb_id2name(id), rb_class2name(orig));
    }
    else {
	return defined ? Qfalse : Qnil;
    }
}

VALUE
rb_cvar_get2(VALUE klass, ID id, bool check)
{
    return rb_cvar_get3(klass, id, check, false);
}

VALUE
rb_cvar_get(VALUE klass, ID id)
{
    return rb_cvar_get2(klass, id, true);
}

VALUE
rb_cvar_defined(VALUE klass, ID id)
{
    return rb_cvar_get3(klass, id, false, true);
}

void
rb_cv_set(VALUE klass, const char *name, VALUE val)
{
    ID id = rb_intern(name);
    if (!rb_is_class_id(id)) {
	rb_name_error(id, "wrong class variable name %s", name);
    }
    rb_cvar_set(klass, id, val);
}

VALUE
rb_cv_get(VALUE klass, const char *name)
{
    ID id = rb_intern(name);
    if (!rb_is_class_id(id)) {
	rb_name_error(id, "wrong class variable name %s", name);
    }
    return rb_cvar_get(klass, id);
}

void
rb_define_class_variable(VALUE klass, const char *name, VALUE val)
{
    ID id = rb_intern(name);

    if (!rb_is_class_id(id)) {
	rb_name_error(id, "wrong class variable name %s", name);
    }
    rb_cvar_set(klass, id, val);
}

static int
cv_i(ID key, VALUE value, VALUE ary)
{
    if (rb_is_class_id(key)) {
	VALUE kval = ID2SYM(key);
	if (!rb_ary_includes(ary, kval)) {
	    rb_ary_push(ary, kval);
	}
    }
    return ST_CONTINUE;
}

/*
 *  call-seq:
 *     mod.class_variables   => array
 *  
 *  Returns an array of the names of class variables in <i>mod</i>.
 *     
 *     class One
 *       @@var1 = 1
 *     end
 *     class Two < One
 *       @@var2 = 2
 *     end
 *     One.class_variables   #=> [:@@var1]
 *     Two.class_variables   #=> [:@@var2]
 */

VALUE
rb_mod_class_variables(VALUE klass, SEL sel)
{
    klass = unmeta_class(klass);
    VALUE ary = rb_ary_new();
    CFMutableDictionaryRef iv_dict = rb_class_ivar_dict(klass);
    if (iv_dict != NULL) {
	ivar_dict_foreach(iv_dict, cv_i, (VALUE)ary);
    }
    return ary;
}

/*
 *  call-seq:
 *     remove_class_variable(sym)    => obj
 *  
 *  Removes the definition of the <i>sym</i>, returning that
 *  constant's value.
 *     
 *     class Dummy
 *       @@var = 99
 *       puts @@var
 *       remove_class_variable(:@@var)
 *       puts(defined? @@var)
 *     end
 *     
 *  <em>produces:</em>
 *     
 *     99
 *     nil
 */

VALUE
rb_mod_remove_cvar(VALUE mod, SEL sel, VALUE name)
{
    ID id = rb_to_id(name);
    VALUE val;

    if (!rb_is_class_id(id)) {
	rb_name_error(id, "wrong class variable name %s", rb_id2name(id));
    }
    if (!OBJ_TAINTED(mod) && rb_safe_level() >= 4) {
	rb_raise(rb_eSecurityError, "Insecure: can't remove class variable");
    }
    if (OBJ_FROZEN(mod)) {
	rb_error_frozen("class/module");
    }

    CFMutableDictionaryRef iv_dict = rb_class_ivar_dict(mod);
    if (iv_dict != NULL && CFDictionaryGetValueIfPresent((CFDictionaryRef)iv_dict, (const void *)id, (const void **)&val)) {
	CFDictionaryRemoveValue(iv_dict, (const void *)id);
	return val;
    }
    if (rb_cvar_defined(mod, id)) {
	rb_name_error(id, "cannot remove %s for %s",
		 rb_id2name(id), rb_class2name(mod));
    }
    rb_name_error(id, "class variable %s not defined for %s",
		  rb_id2name(id), rb_class2name(mod));
    return Qnil;		/* not reached */
}

VALUE
rb_iv_get(VALUE obj, const char *name)
{
    ID id = rb_intern(name);

    return rb_ivar_get(obj, id);
}

VALUE
rb_iv_set(VALUE obj, const char *name, VALUE val)
{
    ID id = rb_intern(name);

    return rb_ivar_set(obj, id, val);
}
