/* This file is covered by the Ruby license. See COPYING for more details.
 * 
 * Copyright (C) 2007-2008, Apple Inc. All rights reserved.
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 * Copyright (C) 2000  Network Applied Communication Laboratory, Inc.
 * Copyright (C) 2000  Information-technology Promotion Agency, Japan
 */

#include "ruby/ruby.h"
#include "ruby/node.h"
#include "ruby/st.h"
#include "ruby/util.h"
#include "id.h"
#include "vm.h"

st_table *rb_global_tbl;
st_table *rb_class_tbl;
static ID autoload, classpath, tmp_classpath;

static const void *
retain_cb(CFAllocatorRef allocator, const void *v)
{
    rb_objc_retain(v);
    return v;
}

static void
release_cb(CFAllocatorRef allocator, const void *v)
{
    rb_objc_release(v);
}

static void
ivar_dict_foreach(VALUE hash, int (*func)(ANYARGS), VALUE farg)
{
    CFIndex i, count;
    const void **keys;
    const void **values;

    count = CFDictionaryGetCount((CFDictionaryRef)hash);
    if (count == 0)
	return;

    keys = (const void **)alloca(sizeof(void *) * count);
    values = (const void **)alloca(sizeof(void *) * count);

    CFDictionaryGetKeysAndValues((CFDictionaryRef)hash, keys, values);

    for (i = 0; i < count; i++) {
	if ((*func)(keys[i], values[i], farg) != ST_CONTINUE)
	    break;
    }
}

static CFDictionaryValueCallBacks rb_cfdictionary_value_cb = {
    0, retain_cb, release_cb, NULL, NULL
};

void
Init_var_tables(void)
{
    rb_global_tbl = st_init_numtable();
    rb_objc_retain(rb_global_tbl);
    rb_class_tbl = st_init_numtable();
    rb_objc_retain(rb_class_tbl);
    autoload = rb_intern("__autoload__");
    classpath = rb_intern("__classpath__");
    tmp_classpath = rb_intern("__tmp_classpath__");
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
	if ((tmp = rb_attr_get(fc->track, classpath)) != Qnil) {
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
	    ivar_dict_foreach((VALUE)iv_dict, fc_i, (VALUE)&arg);
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
	ivar_dict_foreach((VALUE)iv_dict, fc_i, (VALUE)&arg);
    }
    if (arg.path == 0) {
	st_foreach_safe(rb_class_tbl, fc_i, (st_data_t)&arg);
    }
    if (arg.path) {
	iv_dict = rb_class_ivar_dict_or_create(klass);
	CFDictionarySetValue(iv_dict, (const void *)classpath, (const void *)arg.path);
	CFDictionaryRemoveValue(iv_dict, (const void *)tmp_classpath);
	return arg.path;
    }
    if (!RCLASS_RUBY(klass)) {
	VALUE name = rb_str_new2(class_getName((Class)klass));
	iv_dict = rb_class_ivar_dict_or_create(klass);
	CFDictionarySetValue(iv_dict, (const void *)classpath, (const void *)name);
	CFDictionaryRemoveValue(iv_dict, (const void *)tmp_classpath);
	return name;
    }
    return Qnil;
}

static VALUE
classname(VALUE klass)
{
    VALUE path = Qnil;

    if (!klass) klass = rb_cObject;
    CFMutableDictionaryRef iv_dict = rb_class_ivar_dict(klass);
    if (iv_dict != NULL) {
	if (!CFDictionaryGetValueIfPresent((CFDictionaryRef)iv_dict, 
	    (const void *)classpath, (const void **)&path)) {

	    static ID classid = 0;
	    if (classid == 0)
		classid = rb_intern("__classid__");

	    if (!CFDictionaryGetValueIfPresent((CFDictionaryRef)iv_dict, 
		(const void *)classid, (const void **)&path))
		return find_class_path(klass);

	    path = rb_str_dup(path);
	    OBJ_FREEZE(path);
	    CFDictionarySetValue(iv_dict, (const void *)classpath, (const void *)path);
	    CFDictionaryRemoveValue(iv_dict, (const void *)classid);
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

    if (!NIL_P(path)) return path;
    if ((path = rb_attr_get(klass, tmp_classpath)) != Qnil) {
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
	rb_ivar_set(klass, tmp_classpath, path);

	return path;
    }
}

void
rb_set_class_path(VALUE klass, VALUE under, const char *name)
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
    rb_ivar_set(klass, classpath, str);

    rb_vm_set_outer(klass, under);
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
    rb_iv_set(klass, "__classid__", ID2SYM(id));
}

VALUE
rb_class_name(VALUE klass)
{
    return rb_class_path(rb_class_real(klass));
}

const char *
rb_class2name(VALUE klass)
{
    return RSTRING_PTR(rb_class_name(klass));
}

const char *
rb_obj_classname(VALUE obj)
{
    return rb_class2name(CLASS_OF(obj));
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

    GC_WB(&var->data, (void*)val);
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
    GC_WB(&var->data, (void*)val);
}

static void
val_marker(VALUE data)
{
    if (data) {
	rb_gc_mark_maybe(data);
    }
}

static VALUE
var_getter(ID id, VALUE *var)
{
    if (!var) return Qnil;
    return *var;
}

static void
var_setter(VALUE val, ID id, VALUE *var)
{
    GC_WB(var, val);
}

static void
var_marker(VALUE *var)
{
    if (var) {
	rb_gc_mark_maybe(*var);
    }
}

static void
readonly_setter(VALUE val, ID id, void *var)
{
    rb_name_error(id, "%s is a read-only variable", rb_id2name(id));
}

static int
mark_global_entry(ID key, struct global_entry *entry)
{
    struct trace_var *trace;
    struct global_variable *var = entry->var;

    (*var->marker)(var->data);
    trace = var->trace;
    while (trace) {
	if (trace->data) {
	    rb_gc_mark_maybe(trace->data);
	}
	trace = trace->next;
    }
    return ST_CONTINUE;
}

void
rb_gc_mark_global_tbl(void)
{
    if (rb_global_tbl)
        st_foreach_safe(rb_global_tbl, mark_global_entry, 0);
}

static ID
global_id(const char *name)
{
    ID id;

    if (name[0] == '$') id = rb_intern(name);
    else {
	char *buf = ALLOCA_N(char, strlen(name)+2);
	buf[0] = '$';
	strcpy(buf+1, name);
	id = rb_intern(buf);
    }
    return id;
}

void
rb_define_hooked_variable(
    const char *name,
    VALUE *var,
    VALUE (*getter)(ANYARGS),
    void  (*setter)(ANYARGS))
{
    struct global_variable *gvar;
    ID id;
    VALUE tmp;
    
    if (var)
        tmp = *var;

    id = global_id(name);
    gvar = rb_global_entry(id)->var;
    gvar->data = (void*)var;
    gvar->getter = getter?getter:var_getter;
    gvar->setter = setter?setter:var_setter;
    gvar->marker = var_marker;

    GC_ROOT(var);
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
    char buf[4];
    const char *s = "123456789";

    st_foreach_safe(rb_global_tbl, gvar_i, ary);
    while (*s) {
	sprintf(buf, "$%c", *s++);
	rb_ary_push(ary, ID2SYM(rb_intern(buf)));
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

static VALUE
generic_ivar_get(VALUE obj, ID id, int warn)
{
    if (generic_iv_dict != NULL) {
	CFDictionaryRef obj_dict;

	if (CFDictionaryGetValueIfPresent(generic_iv_dict, 
	    (const void *)obj, (const void **)&obj_dict) 
	    && obj_dict != NULL) {
	    VALUE val;

	    if (CFDictionaryGetValueIfPresent(obj_dict, (const void *)id, 
		(const void **)&val))
		return val;
	}
    }
    if (warn) {
	rb_warning("instance variable %s not initialized", rb_id2name(id));
    }
    return Qnil;
}

static void
generic_ivar_set(VALUE obj, ID id, VALUE val)
{
    CFMutableDictionaryRef obj_dict;

    if (rb_special_const_p(obj)) {
	if (rb_obj_frozen_p(obj)) 
	    rb_error_frozen("object");
    }
    if (generic_iv_dict == NULL) {
	generic_iv_dict = CFDictionaryCreateMutable(NULL, 0, NULL, &rb_cfdictionary_value_cb);
	rb_objc_retain(generic_iv_dict);
	obj_dict = NULL;
    }
    else {
	obj_dict = (CFMutableDictionaryRef)CFDictionaryGetValue(
	    (CFDictionaryRef)generic_iv_dict, (const void *)obj);
    }
    if (obj_dict == NULL) {
	obj_dict = CFDictionaryCreateMutable(NULL, 0, NULL, &rb_cfdictionary_value_cb);
	CFDictionarySetValue(generic_iv_dict, (const void *)obj, 
	    (const void *)obj_dict);
	CFMakeCollectable(obj_dict);
    }
    CFDictionarySetValue(obj_dict, (const void *)id, (const void *)val);
}

static VALUE
generic_ivar_defined(VALUE obj, ID id)
{
    CFMutableDictionaryRef obj_dict;

    if (generic_iv_dict != NULL
	&& CFDictionaryGetValueIfPresent((CFDictionaryRef)generic_iv_dict, 
	   (const void *)obj, (const void **)&obj_dict) && obj_dict != NULL) {
    	if (CFDictionaryGetValueIfPresent(obj_dict, (const void *)id, NULL))
	    return Qtrue;
    }
    return Qfalse;    
}

static int
generic_ivar_remove(VALUE obj, ID id, VALUE *valp)
{
    CFMutableDictionaryRef obj_dict;
    VALUE val;

    if (generic_iv_dict == NULL)
	return 0;

    obj_dict = (CFMutableDictionaryRef)CFDictionaryGetValue(
	(CFDictionaryRef)generic_iv_dict, (const void *)obj);
    if (obj_dict == NULL)
	return 0;

    if (CFDictionaryGetValueIfPresent(obj_dict, (const void *)id, 
	(const void **)&val)) {
	*valp = val;
	CFDictionaryRemoveValue(obj_dict, (const void *)id);
	return 1;
    }

    return 0;
}

void
rb_free_generic_ivar(VALUE obj)
{
    if (generic_iv_dict != NULL) {
	CFDictionaryRemoveValue(generic_iv_dict, (const void *)obj);
    }
}

void
rb_copy_generic_ivar(VALUE clone, VALUE obj)
{
    CFMutableDictionaryRef obj_dict;
    CFMutableDictionaryRef clone_dict;

    if (generic_iv_dict == NULL)
	return;

    obj_dict = (CFMutableDictionaryRef)CFDictionaryGetValue(
	(CFDictionaryRef)generic_iv_dict, (const void *)obj);
    if (obj_dict == NULL)
	return;

    if (CFDictionaryGetValueIfPresent((CFDictionaryRef)generic_iv_dict, 
	(const void *)clone, (const void **)&clone_dict) 
	&& clone_dict != NULL)
	CFDictionaryRemoveValue(generic_iv_dict, (const void *)clone);

    clone_dict = CFDictionaryCreateMutableCopy(NULL, 0, obj_dict);
    CFDictionarySetValue(generic_iv_dict, (const void *)clone, 
	(const void *)clone_dict);
    CFMakeCollectable(clone_dict);
}

#define RCLASS_RUBY_IVAR_DICT(mod) \
    (*(CFMutableDictionaryRef *) \
     	((void *)mod + class_getInstanceSize(*(Class *)RCLASS_SUPER(mod))))

CFMutableDictionaryRef 
rb_class_ivar_dict(VALUE mod)
{
    CFMutableDictionaryRef dict;

    if (RCLASS_RUBY(mod)) {
	dict = RCLASS_RUBY_IVAR_DICT(mod);
    }
    else {
	dict = NULL;
	if (generic_iv_dict != NULL) {
	    CFDictionaryGetValueIfPresent(generic_iv_dict, 
		(const void *)mod, (const void **)&dict);
	} 
    }
    return dict;
}

void
rb_class_ivar_set_dict(VALUE mod, CFMutableDictionaryRef dict)
{
    if (RCLASS_RUBY(mod)) {
	CFMutableDictionaryRef old_dict = RCLASS_RUBY_IVAR_DICT(mod);
	if (old_dict != dict) {
	    if (old_dict != NULL) {
		CFRelease(old_dict);
	    }
	    CFRetain(dict);
	    RCLASS_RUBY_IVAR_DICT(mod) = dict;
	}
    }
    else {
	if (generic_iv_dict == NULL) {
	    generic_iv_dict = CFDictionaryCreateMutable(NULL, 0, NULL, &rb_cfdictionary_value_cb);
	    rb_objc_retain(generic_iv_dict);
	}
	CFDictionarySetValue(generic_iv_dict, (const void *)mod, (const void *)dict);
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
ivar_get(VALUE obj, ID id, int warn)
{
    VALUE val;

    switch (TYPE(obj)) {
	case T_OBJECT:
	    {
		val = Qundef;

		int slot = rb_vm_find_class_ivar_slot(CLASS_OF(obj), id);
		if (slot != -1) {
		    val = rb_vm_get_ivar_from_slot(obj, slot);
		}
		else {
		    if (ROBJECT(obj)->tbl != NULL) {
			if (!CFDictionaryGetValueIfPresent(
				    (CFDictionaryRef)ROBJECT(obj)->tbl,
				    (const void *)id,
				    (const void **)&val)) {
			    val = Qundef;
			}
		    }
		}

		if (val != Qundef) {
		    return val;
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
	    return generic_ivar_get(obj, id, warn);

	default:
	    if (FL_TEST(obj, FL_EXIVAR) || rb_special_const_p(obj)) {
		return generic_ivar_get(obj, id, warn);
	    }
	    break;
    }
    if (warn) {
	rb_warning("instance variable %s not initialized", rb_id2name(id));
    }
    return Qnil;
}

VALUE
rb_ivar_get(VALUE obj, ID id)
{
    return ivar_get(obj, id, Qtrue);
}

VALUE
rb_attr_get(VALUE obj, ID id)
{
    return ivar_get(obj, id, Qfalse);
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
		int slot = rb_vm_find_class_ivar_slot(CLASS_OF(obj), id);
		if (slot != -1) {
		    rb_vm_set_ivar_from_slot(obj, val, slot);
		}
		else {
		    if (ROBJECT(obj)->tbl == NULL) {
			CFMutableDictionaryRef tbl;

			tbl = CFDictionaryCreateMutable(NULL, 0, NULL, 
				&rb_cfdictionary_value_cb);

			GC_WB(&ROBJECT(obj)->tbl, tbl);
			CFMakeCollectable(tbl);
		    }

		    CFDictionarySetValue(ROBJECT(obj)->tbl, 
			    (const void *)id, (const void *)val);
		}
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
	    rb_objc_flag_set((const void *)obj, FL_EXIVAR, true);
	    generic_ivar_set(obj, id, val);
	    break;

	default:
	    generic_ivar_set(obj, id, val);
	    FL_SET(obj, FL_EXIVAR);
	    break;
    }
    return val;
}

VALUE
rb_ivar_defined(VALUE obj, ID id)
{
    VALUE val;

    switch (TYPE(obj)) {
	case T_OBJECT:
	    {
		val = Qundef;

		int slot = rb_vm_find_class_ivar_slot(CLASS_OF(obj), id);
		if (slot != -1) {
		    val = rb_vm_get_ivar_from_slot(obj, slot);
		}
		else {
		    if (ROBJECT(obj)->tbl != NULL) {
			if (CFDictionaryGetValueIfPresent(
				    (CFDictionaryRef)ROBJECT(obj)->tbl,
				    (const void *)id, NULL)) {
			    val = Qtrue;
			}
		    }
		}

		if (val != Qundef) {
		    return Qtrue;
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
	    return generic_ivar_defined(obj, id);

	default:
	    if (FL_TEST(obj, FL_EXIVAR) || rb_special_const_p(obj)) {
		return generic_ivar_defined(obj, id);
	    }
	    break;
    }
    return Qfalse;
}

struct obj_ivar_tag {
    VALUE obj;
    int (*func)(ID key, VALUE val, st_data_t arg);
    st_data_t arg;
};

void rb_ivar_foreach(VALUE obj, int (*func)(ANYARGS), st_data_t arg)
{
    switch (TYPE(obj)) {
      case T_OBJECT:
	  // TODO support slots
	  if (ROBJECT(obj)->tbl != NULL) {
	      CFDictionaryApplyFunction(ROBJECT(obj)->tbl, 
		      (CFDictionaryApplierFunction)func, (void *)arg);
	  }
	  return;

      case T_CLASS:
      case T_MODULE:
	  {
	      CFMutableDictionaryRef iv_dict = rb_class_ivar_dict(obj);
	      if (iv_dict != NULL)
		  ivar_dict_foreach((VALUE)iv_dict, func, arg);
	  }
	  return;

      case T_NATIVE:
	  goto generic;
    }
    if (!FL_TEST(obj, FL_EXIVAR) && !rb_special_const_p(obj)) {
	return;
    }
generic:
    if (generic_iv_dict != NULL) {
	CFDictionaryRef obj_dict;

	obj_dict = (CFDictionaryRef)CFDictionaryGetValue(
		(CFDictionaryRef)generic_iv_dict, (const void *)obj);
	if (obj_dict != NULL) {
	    CFDictionaryApplyFunction(obj_dict, 
		    (CFDictionaryApplierFunction)func, (void *)arg);
	}
    }
}

static int
ivar_i(ID key, VALUE val, VALUE ary)
{
    if (rb_is_instance_id(key)) {
	rb_ary_push(ary, ID2SYM(key));
    }
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
	    // TODO support slots
	    if (ROBJECT(obj)->tbl != NULL) {
		if (CFDictionaryGetValueIfPresent(ROBJECT(obj)->tbl, (const void *)id, (const void **)val)) {
		    CFDictionaryRemoveValue(ROBJECT(obj)->tbl, 
			    (const void *)id);
		    return val;
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
	    if (FL_TEST(obj, FL_EXIVAR) || rb_special_const_p(obj)) {
		if (generic_ivar_remove(obj, id, &val)) {
		    return val;
		}
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
const_missing(VALUE klass, ID id)
{
    return rb_funcall(klass, rb_intern("const_missing"), 1, ID2SYM(id));
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
    VALUE av, fn;
    struct st_table *tbl;

    if (!rb_is_const_id(id)) {
	rb_raise(rb_eNameError, "autoload must be constant name: %s", rb_id2name(id));
    }
    if (!file || !*file) {
	rb_raise(rb_eArgError, "empty file name");
    }

    if ((av = rb_attr_get(mod, id)) != Qnil && av != Qundef) {
	return;
    }

    rb_const_set(mod, id, Qundef);
    if ((av = rb_attr_get(mod, autoload)) != Qnil) {
	tbl = check_autoload_table(av);
    }
    else {
	av = Data_Wrap_Struct(rb_cData, NULL, NULL, 0);
	rb_ivar_set(mod, autoload, av);
	tbl = st_init_numtable();
	GC_WB(&DATA_PTR(av), tbl);
    }
    fn = rb_str_new2(file);
#if __LP64__
    RCLASS_RC_FLAGS(fn) &= ~FL_TAINT;
#else
    FL_UNSET(fn, FL_TAINT);
#endif
    OBJ_FREEZE(fn);
    st_insert(tbl, id, (st_data_t)rb_node_newnode(NODE_MEMO, fn, rb_safe_level(), 0));
}

static NODE*
autoload_delete(VALUE mod, ID id)
{
    VALUE val;
    st_data_t load = 0;

    CFMutableDictionaryRef iv_dict = rb_class_ivar_dict(mod);
    assert(iv_dict != NULL);
    CFDictionaryRemoveValue(iv_dict, (const void *)id);
    if (CFDictionaryGetValueIfPresent((CFDictionaryRef)iv_dict, 
	(const void *)autoload, (const void **)&val)) {
	struct st_table *tbl = check_autoload_table(val);

	st_delete(tbl, (st_data_t*)&id, &load);

	if (tbl->num_entries == 0) {
	    DATA_PTR(val) = 0;
	    st_free_table(tbl);
	    id = autoload;
	    CFDictionaryRemoveValue(iv_dict, (const void *)id);
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
    VALUE val, file;
    struct st_table *tbl;
    st_data_t load;

    CFMutableDictionaryRef iv_dict = rb_class_ivar_dict(mod);
    assert(iv_dict != NULL);
    if (!CFDictionaryGetValueIfPresent((CFDictionaryRef)iv_dict, 
	   (const void *)autoload, (const void **)&val)
	|| (tbl = check_autoload_table(val)) == NULL
	|| !st_lookup(tbl, id, &load))
	return Qnil;
    file = ((NODE *)load)->nd_lit;
    Check_Type(file, T_STRING);
    if (RSTRING_LEN(file) == 0) {
	rb_raise(rb_eArgError, "empty file name");
    }
    if (!rb_provided(RSTRING_PTR(file))) {
	return file;
    }

    /* already loaded but not defined */
    st_delete(tbl, (st_data_t*)&id, 0);
    if (!tbl->num_entries) {
	DATA_PTR(val) = 0;
	st_free_table(tbl);
	id = autoload;
	CFDictionaryRemoveValue(iv_dict, (const void *)id);
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
rb_const_get_0(VALUE klass, ID id, int exclude, int recurse)
{
    VALUE value, tmp;
    int mod_retry = 0;

    tmp = klass;
  retry:
    while (RTEST(tmp)) {
	CFDictionaryRef iv_dict;
	while ((iv_dict = rb_class_ivar_dict(tmp)) != NULL
	       && CFDictionaryGetValueIfPresent(iv_dict, (const void *)id, (const void **)&value)) {
	    if (value == Qundef) {
		if (!RTEST(rb_autoload_load(tmp, id))) break;
		continue;
	    }
	    if (exclude && tmp == rb_cObject && klass != rb_cObject) {
		rb_warn("toplevel constant %s referenced by %s::%s",
			rb_id2name(id), rb_class2name(klass), rb_id2name(id));
	    }
	    value = rb_vm_resolve_const_value(value, klass, id);
	    return value;
	}
	if (!recurse && klass != rb_cObject) break;
	VALUE inc_mods = rb_attr_get(tmp, idIncludedModules);
	if (inc_mods != Qnil) {
	    int i, count = RARRAY_LEN(inc_mods);
	    for (i = 0; i < count; i++) {
		iv_dict = rb_class_ivar_dict(RARRAY_AT(inc_mods, i));
		if (CFDictionaryGetValueIfPresent(iv_dict, (const void *)id, (const void **)&value))
		    return rb_vm_resolve_const_value(value, klass, id);
	    }
	}
	tmp = RCLASS_SUPER(tmp);
    }
    if (!exclude && !mod_retry && BUILTIN_TYPE(klass) == T_MODULE) {
	mod_retry = 1;
	tmp = rb_cObject;
	goto retry;
    }

    /* Classes are typically pre-loaded by Kernel#framework but it is still
     * useful to keep the dynamic import facility, because someone in the
     * Objective-C world may dynamically define classes at runtime (like
     * ScriptingBridge.framework).
     *
     * Note that objc_getClass does _not_ honor namespaces. Consider:
     *
     *  module Namespace
     *    class RubyClass; end
     *  end
     *
     * In this case objc_getClass will happily return the Namespace::RubyClass
     * object, which is ok but _not_ when trying to find a Ruby class. So we
     * test whether or not the found class is a pure Ruby class/module or not.
     */
    Class k = (Class)objc_getClass(rb_id2name(id));
    if (k != NULL && !RCLASS_RUBY(k)) {
	return (VALUE)k;
    }

    return const_missing(klass, id);
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
	ivar_dict_foreach((VALUE)iv_dict, sv_i, (VALUE)tbl);
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
	tmp = RCLASS_SUPER(tmp);
    }
    if (!exclude && !mod_retry && BUILTIN_TYPE(klass) == T_MODULE) {
	mod_retry = 1;
	tmp = rb_cObject;
	goto retry;
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
	if (BUILTIN_TYPE(klass) == T_MODULE) {
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

    DLOG("CONS", "%s::%s <- %p", class_getName((Class)klass), rb_id2name(id), (void *)val);
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

static VALUE
original_module(VALUE c)
{
    if (TYPE(c) == T_ICLASS)
	return RBASIC(c)->klass;
    return c;
}

#define IV_LOOKUP(k,i,v) ((iv_dict = rb_class_ivar_dict(k)) != NULL && CFDictionaryGetValueIfPresent((CFDictionaryRef)iv_dict, (const void *)i, (const void **)(v)))

#define CVAR_LOOKUP(v,r) do {\
    if (IV_LOOKUP(klass, id, v)) {\
	r;\
    }\
    if (RCLASS_SINGLETON(klass)) {\
	VALUE obj = rb_iv_get(klass, "__attached__");\
	switch (TYPE(obj)) {\
	  case T_MODULE:\
	  case T_CLASS:\
	    klass = obj;\
	    break;\
	  default:\
	    klass = RCLASS_SUPER(klass);\
	    break;\
	}\
    }\
    else {\
	klass = RCLASS_SUPER(klass);\
    }\
    while (klass) {\
	if (IV_LOOKUP(klass, id, v)) {\
	    r;\
	}\
	klass = RCLASS_SUPER(klass);\
    }\
} while(0)

void
rb_cvar_set(VALUE klass, ID id, VALUE val)
{
    VALUE tmp, front = 0, target = 0;
    CFMutableDictionaryRef iv_dict;

    tmp = klass;
    if (!RCLASS_META(klass)) { 
	tmp = klass = *(VALUE *)klass;
    }
    CVAR_LOOKUP(0, {if (!front) front = klass; target = klass;});
    if (target) {
	if (front && target != front) {
	    ID did = id;

	    if (RTEST(ruby_verbose)) {
		rb_warning("class variable %s of %s is overtaken by %s",
			   rb_id2name(id), rb_class2name(original_module(front)),
			   rb_class2name(original_module(target)));
	    }
	    if (TYPE(front) == T_CLASS) {
		CFDictionaryRemoveValue(rb_class_ivar_dict(front), (const void *)did);
	    }
	}
    }
    else {
	target = tmp;
    }
    mod_av_set(target, id, val, Qfalse);
}

VALUE
rb_cvar_get(VALUE klass, ID id)
{
    VALUE value, tmp, front = 0, target = 0;
    CFMutableDictionaryRef iv_dict;

    tmp = klass;
    if (!RCLASS_META(klass)) {
	tmp = klass = *(VALUE *)klass;
    }
    CVAR_LOOKUP(&value, {if (!front) front = klass; target = klass;});
    if (!target) {
	rb_name_error(id,"uninitialized class variable %s in %s",
		      rb_id2name(id), rb_class2name(tmp));
    }
    if (front && target != front) {
	ID did = id;

	if (RTEST(ruby_verbose)) {
	    rb_warning("class variable %s of %s is overtaken by %s",
		       rb_id2name(id), rb_class2name(original_module(front)),
		       rb_class2name(original_module(target)));
	}
	if (BUILTIN_TYPE(front) == T_CLASS) {
	    CFDictionaryRemoveValue(rb_class_ivar_dict(front), (const void *)did);
	}
    }
    return value;
}

VALUE
rb_cvar_defined(VALUE klass, ID id)
{
    CFMutableDictionaryRef iv_dict;
    if (!klass) {
	return Qfalse;
    }
    if (!RCLASS_META(klass)) {
	klass = *(VALUE *)klass;
    }
    CVAR_LOOKUP(0,return Qtrue);
    return Qfalse;
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
rb_mod_class_variables(VALUE obj, SEL sel)
{
    if (!RCLASS_META(obj)) {
	obj = *(VALUE *)obj;
    }
    VALUE ary = rb_ary_new();
    CFMutableDictionaryRef iv_dict = rb_class_ivar_dict(obj);
    if (iv_dict != NULL) {
	ivar_dict_foreach((VALUE)iv_dict, cv_i, (VALUE)ary);
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
