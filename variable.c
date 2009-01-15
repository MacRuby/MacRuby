/**********************************************************************

  variable.c -

  $Author: nobu $
  created at: Tue Apr 19 23:55:15 JST 1994

  Copyright (C) 1993-2007 Yukihiro Matsumoto
  Copyright (C) 2000  Network Applied Communication Laboratory, Inc.
  Copyright (C) 2000  Information-technology Promotion Agency, Japan

**********************************************************************/

#include "ruby/ruby.h"
#include "ruby/node.h"
#include "ruby/st.h"
#include "ruby/util.h"
#include "debug.h"
#include "id.h"

void rb_vm_change_state(void);
st_table *rb_global_tbl;
st_table *rb_class_tbl;
static ID autoload, classpath, tmp_classpath;
#if WITH_OBJC
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
#endif

void
Init_var_tables(void)
{
    rb_global_tbl = st_init_numtable();
    GC_ROOT(&rb_global_tbl);
    rb_class_tbl = st_init_numtable();
    GC_ROOT(&rb_class_tbl);
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
#if WITH_OBJC
	if ((tmp = rb_attr_get(fc->track, classpath)) != Qnil) {
#else
	if (RCLASS_IV_TBL(fc->track) &&
	    st_lookup(RCLASS_IV_TBL(fc->track), classpath, &tmp)) {
#endif
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
#if WITH_OBJC
	CFDictionaryRef iv_dict = rb_class_ivar_dict(value);
	if (iv_dict == NULL) return ST_CONTINUE;
#else
	if (!RCLASS_IV_TBL(value)) return ST_CONTINUE;
#endif
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
#if WITH_OBJC
	    ivar_dict_foreach((VALUE)iv_dict, fc_i, (VALUE)&arg);
#else
	    st_foreach(RCLASS_IV_TBL(value), fc_i, (st_data_t)&arg);
#endif
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

#if WITH_OBJC
    CFMutableDictionaryRef iv_dict = rb_class_ivar_dict(rb_cObject);
    if (iv_dict != NULL) {
	ivar_dict_foreach((VALUE)iv_dict, fc_i, (VALUE)&arg);
    }
#else
    if (RCLASS_IV_TBL(rb_cObject)) {
	st_foreach_safe(RCLASS_IV_TBL(rb_cObject), fc_i, (st_data_t)&arg);
    }
#endif
    if (arg.path == 0) {
	st_foreach_safe(rb_class_tbl, fc_i, (st_data_t)&arg);
    }
    if (arg.path) {
#if WITH_OBJC
	iv_dict = rb_class_ivar_dict_or_create(klass);
	CFDictionarySetValue(iv_dict, (const void *)classpath, (const void *)arg.path);
	CFDictionaryRemoveValue(iv_dict, (const void *)tmp_classpath);
#else
	if (!RCLASS_IV_TBL(klass)) {
	    GC_WB(&RCLASS_IV_TBL(klass), st_init_numtable());
	}
	st_insert(RCLASS_IV_TBL(klass), classpath, arg.path);
	st_delete(RCLASS_IV_TBL(klass), &tmp_classpath, 0);
#endif
	return arg.path;
    }
#if WITH_OBJC
    if (!RCLASS_RUBY(klass)) {
	VALUE name = rb_str_new2(class_getName((Class)klass));
	iv_dict = rb_class_ivar_dict_or_create(klass);
	CFDictionarySetValue(iv_dict, (const void *)classpath, (const void *)name);
	CFDictionaryRemoveValue(iv_dict, (const void *)tmp_classpath);
	return name;
    }
#endif
    return Qnil;
}

static VALUE
classname(VALUE klass)
{
    VALUE path = Qnil;

    if (!klass) klass = rb_cObject;
#if WITH_OBJC
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
#else
    if (RCLASS_IV_TBL(klass)) {
	if (!st_lookup(RCLASS_IV_TBL(klass), classpath, &path)) {
	    ID classid = rb_intern("__classid__");

	    if (!st_lookup(RCLASS_IV_TBL(klass), classid, &path)) {
		return find_class_path(klass);
	    }
	    path = rb_str_dup(rb_id2str(SYM2ID(path)));
	    OBJ_FREEZE(path);
	    st_insert(RCLASS_IV_TBL(klass), classpath, path);
	    st_delete(RCLASS_IV_TBL(klass), (st_data_t*)&classid, 0);
#endif
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
rb_mod_name(VALUE mod)
{
    VALUE path = classname(mod);

    if (!NIL_P(path)) return rb_str_dup(path);
    return path;
}

VALUE
rb_class_path(VALUE klass)
{
    VALUE path = classname(klass);

    if (!NIL_P(path)) return path;
#if WITH_OBJC
    if ((path = rb_attr_get(klass, tmp_classpath)) != Qnil) {
#else
    if (RCLASS_IV_TBL(klass) && st_lookup(RCLASS_IV_TBL(klass),
					   tmp_classpath, &path)) {
#endif
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

#if !WITH_OBJC
/* defined in vm_core.h, imported by debug.h */
struct global_entry {
    struct global_variable *var;
    ID id;
};
#endif

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
    if (data) rb_gc_mark_maybe(data);
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
    if (var) rb_gc_mark_maybe(*var);
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
	if (trace->data) rb_gc_mark_maybe(trace->data);
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

VALUE
rb_f_trace_var(int argc, VALUE *argv)
{
    VALUE var, cmd;
    struct global_entry *entry;
    struct trace_var *trace;

    rb_secure(4);
    if (rb_scan_args(argc, argv, "11", &var, &cmd) == 1) {
	cmd = rb_block_proc();
    }
    if (NIL_P(cmd)) {
	return rb_f_untrace_var(argc, argv);
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
rb_f_untrace_var(int argc, VALUE *argv)
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
rb_f_global_variables(void)
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

#if WITH_OBJC
static CFMutableDictionaryRef generic_iv_dict = NULL;
#else
static int special_generic_ivar = 0;
static st_table *generic_iv_tbl;
#endif

st_table*
rb_generic_ivar_table(VALUE obj)
{
#if WITH_OBJC
    return NULL;
#else
    st_data_t tbl;

    if (!FL_TEST(obj, FL_EXIVAR)) return 0;
    if (!generic_iv_tbl) return 0;
    if (!st_lookup(generic_iv_tbl, obj, &tbl)) return 0;
    return (st_table *)tbl;
#endif
}

static VALUE
generic_ivar_get(VALUE obj, ID id, int warn)
{
#if WITH_OBJC
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
#else
    st_data_t tbl;
    VALUE val;

    if (generic_iv_tbl) {
	if (st_lookup(generic_iv_tbl, obj, &tbl)) {
	    if (st_lookup((st_table *)tbl, id, &val)) {
		return val;
	    }
	}
    }
#endif
    if (warn) {
	rb_warning("instance variable %s not initialized", rb_id2name(id));
    }
    return Qnil;
}

static void
generic_ivar_set(VALUE obj, ID id, VALUE val)
{
#if WITH_OBJC
    CFMutableDictionaryRef obj_dict;

    if (rb_special_const_p(obj)) {
	if (rb_obj_frozen_p(obj)) 
	    rb_error_frozen("object");
    }
    if (generic_iv_dict == NULL) {
	generic_iv_dict = CFDictionaryCreateMutable(NULL, 0, NULL, &rb_cfdictionary_value_cb);
	obj_dict = NULL;
    }
    else {
	obj_dict = (CFMutableDictionaryRef)CFDictionaryGetValue(
	    (CFDictionaryRef)generic_iv_dict, (const void *)obj);
    }
    if (obj_dict == NULL) {
	obj_dict = CFDictionaryCreateMutable(kCFAllocatorMalloc, 0, NULL, &rb_cfdictionary_value_cb);
	CFDictionarySetValue(generic_iv_dict, (const void *)obj, 
	    (const void *)obj_dict);
	CFMakeCollectable(obj_dict);
    }
    CFDictionarySetValue(obj_dict, (const void *)id, (const void *)val);
#else
    st_table *tbl;
    st_data_t data;

    if (rb_special_const_p(obj)) {
	if (rb_obj_frozen_p(obj)) rb_error_frozen("object");
	special_generic_ivar = 1;
    }
    if (!generic_iv_tbl) {
	generic_iv_tbl = st_init_numtable();
	GC_ROOT(&generic_iv_tbl);
    }
    if (!st_lookup(generic_iv_tbl, obj, &data)) {
	FL_SET(obj, FL_EXIVAR);
	tbl = st_init_numtable();
	st_add_direct(generic_iv_tbl, obj, (st_data_t)tbl);
	st_add_direct(tbl, id, val);
	return;
    }
    st_insert((st_table *)data, id, val);
#endif
}

static VALUE
generic_ivar_defined(VALUE obj, ID id)
{
#if WITH_OBJC
    CFMutableDictionaryRef obj_dict;

    if (generic_iv_dict != NULL
	&& CFDictionaryGetValueIfPresent((CFDictionaryRef)generic_iv_dict, 
	   (const void *)obj, (const void **)&obj_dict) && obj_dict != NULL) {
    	if (CFDictionaryGetValueIfPresent(obj_dict, (const void *)id, NULL))
	    return Qtrue;
    }
    return Qfalse;    

#else
    st_table *tbl;
    st_data_t data;
    VALUE val;

    if (!generic_iv_tbl) return Qfalse;
    if (!st_lookup(generic_iv_tbl, obj, &data)) return Qfalse;
    tbl = (st_table *)data;
    if (st_lookup(tbl, id, &val)) {
	return Qtrue;
    }
    return Qfalse;
#endif
}

static int
generic_ivar_remove(VALUE obj, ID id, VALUE *valp)
{
#if WITH_OBJC
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
#else
    st_table *tbl;
    st_data_t data;
    int status;

    if (!generic_iv_tbl) return 0;
    if (!st_lookup(generic_iv_tbl, obj, &data)) return 0;
    tbl = (st_table *)data;
    status = st_delete(tbl, &id, valp);
    if (tbl->num_entries == 0) {
	st_delete(generic_iv_tbl, &obj, &data);
	st_free_table((st_table *)data);
    }
    return status;
#endif
}

#if !WITH_OBJC
void
rb_mark_generic_ivar(VALUE obj)
{
    st_data_t tbl;

    if (!generic_iv_tbl) return;
    if (st_lookup(generic_iv_tbl, obj, &tbl)) {
	rb_mark_tbl((st_table *)tbl);
    }
}

static int
givar_mark_i(ID key, VALUE value)
{
    rb_gc_mark(value);
    return ST_CONTINUE;
}

static int
givar_i(VALUE obj, st_table *tbl)
{
    if (rb_special_const_p(obj)) {
	st_foreach_safe(tbl, givar_mark_i, 0);
    }
    return ST_CONTINUE;
}

void
rb_mark_generic_ivar_tbl(void)
{
    if (!generic_iv_tbl) return;
    if (special_generic_ivar == 0) return;
    st_foreach_safe(generic_iv_tbl, givar_i, 0);
}
#endif

void
rb_free_generic_ivar(VALUE obj)
{
#if WITH_OBJC
    if (generic_iv_dict != NULL)
	CFDictionaryRemoveValue(generic_iv_dict, (const void *)obj);
#else
    st_data_t tbl;

    if (!generic_iv_tbl) return;
    if (st_delete(generic_iv_tbl, &obj, &tbl))
	st_free_table((st_table *)tbl);
#endif
}

void
rb_copy_generic_ivar(VALUE clone, VALUE obj)
{
#if WITH_OBJC
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

    clone_dict = CFDictionaryCreateMutableCopy(kCFAllocatorMalloc, 0, obj_dict);
    CFDictionarySetValue(generic_iv_dict, (const void *)clone, 
	(const void *)clone_dict);
    CFMakeCollectable(clone_dict);
#else
    st_data_t data;

    if (!generic_iv_tbl) return;
    if (!FL_TEST(obj, FL_EXIVAR)) {
clear:
        if (FL_TEST(clone, FL_EXIVAR)) {
            rb_free_generic_ivar(clone);
            FL_UNSET(clone, FL_EXIVAR);
        }
        return;
    }
    if (st_lookup(generic_iv_tbl, obj, &data)) {
	st_table *tbl = (st_table *)data;

        if (tbl->num_entries == 0)
            goto clear;

	if (st_lookup(generic_iv_tbl, clone, &data)) {
	    st_free_table((st_table *)data);
	    st_insert(generic_iv_tbl, clone, (st_data_t)st_copy(tbl));
	}
	else {
	    st_add_direct(generic_iv_tbl, clone, (st_data_t)st_copy(tbl));
	    FL_SET(clone, FL_EXIVAR);
	}
    }
#endif
}

#if WITH_OBJC
# define RCLASS_RUBY_IVAR_DICT(mod) (*(CFMutableDictionaryRef *)((void *)mod + class_getInstanceSize(*(Class *)RCLASS_SUPER(mod))))
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
	    if (old_dict != NULL)
		CFRelease(old_dict);
	    CFRetain(dict);
	    RCLASS_RUBY_IVAR_DICT(mod) = dict;
	}
    }
    else {
	if (generic_iv_dict == NULL) {
	    generic_iv_dict = CFDictionaryCreateMutable(kCFAllocatorMalloc, 0, NULL, &rb_cfdictionary_value_cb);
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
	dict = CFDictionaryCreateMutable(kCFAllocatorMalloc, 0, NULL, &rb_cfdictionary_value_cb);
	rb_class_ivar_set_dict(mod, dict);
	CFMakeCollectable(dict);
    }
    return dict;
}
#endif

static VALUE
ivar_get(VALUE obj, ID id, int warn)
{
    VALUE val;
#if !WITH_OBJC
    VALUE *ptr;
    struct st_table *iv_index_tbl;
    long len;
    st_data_t index;
#endif

    switch (TYPE(obj)) {
      case T_OBJECT:
#if WITH_OBJC
	switch (RB_IVAR_TYPE(ROBJECT(obj)->ivars)) {
	    case RB_IVAR_ARY: 
	    {
		int i;
		val = Qundef;
		for (i = 0; i < RB_IVAR_ARY_LEN(ROBJECT(obj)->ivars); i++) {
		    if (ROBJECT(obj)->ivars.as.ary[i].name == id) {
			val = ROBJECT(obj)->ivars.as.ary[i].value;
			break;		    
		    }
		}
		break;
	    }

	    case RB_IVAR_TBL:
		if (!CFDictionaryGetValueIfPresent(
		    (CFDictionaryRef)ROBJECT(obj)->ivars.as.tbl,
		    (const void *)id,
		    (const void **)&val))
		    val = Qundef;
		break;
	}
#else
        len = ROBJECT_NUMIV(obj);
        ptr = ROBJECT_IVPTR(obj);
        iv_index_tbl = ROBJECT_IV_INDEX_TBL(obj);
        if (!iv_index_tbl) break; 
        if (!st_lookup(iv_index_tbl, id, &index)) break;
        if (len <= index) break;
        val = ptr[index];
#endif

        if (val != Qundef)
            return val;
	break;
      case T_CLASS:
      case T_MODULE:
#if WITH_OBJC
      {
	  CFDictionaryRef iv_dict = rb_class_ivar_dict(obj);
	  if (iv_dict != NULL 
	      && CFDictionaryGetValueIfPresent(
		  iv_dict, (const void *)id, (const void **)&val))
	      return val;
      }
#else
	if (RCLASS_IV_TBL(obj) && st_lookup(RCLASS_IV_TBL(obj), id, &val))
	    return val;
#endif
	break;
#if WITH_OBJC
      case T_NATIVE:
	return generic_ivar_get(obj, id, warn);
#endif
      default:
	if (FL_TEST(obj, FL_EXIVAR) || rb_special_const_p(obj))
	    return generic_ivar_get(obj, id, warn);
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
#if !WITH_OBJC
    struct st_table *iv_index_tbl;
    st_data_t index;
    long i, len;
    int ivar_extended;
#endif

    if (!OBJ_TAINTED(obj) && rb_safe_level() >= 4)
	rb_raise(rb_eSecurityError, "Insecure: can't modify instance variable");
    if (OBJ_FROZEN(obj)) rb_error_frozen("object");
    switch (TYPE(obj)) {
      case T_OBJECT:
#if WITH_OBJC
	switch (RB_IVAR_TYPE(ROBJECT(obj)->ivars)) {
	    case RB_IVAR_ARY: 
	    {
		int i, len;
		bool new_ivar;

		len = RB_IVAR_ARY_LEN(ROBJECT(obj)->ivars);
		new_ivar = true;

		if (len == 0) {
		    assert(ROBJECT(obj)->ivars.as.ary == NULL);
		    GC_WB(&ROBJECT(obj)->ivars.as.ary,
			(struct rb_ivar_ary_entry *)xmalloc(
			    sizeof(struct rb_ivar_ary_entry)));
		}
		else {
		    assert(ROBJECT(obj)->ivars.as.ary != NULL);
		    for (i = 0; i < len; i++) {
			if (ROBJECT(obj)->ivars.as.ary[i].value == Qundef) {
			    ROBJECT(obj)->ivars.as.ary[i].name = id;	
			    GC_WB(&ROBJECT(obj)->ivars.as.ary[i].value, val);
			    new_ivar = false;
			    break;
			}
			else if (ROBJECT(obj)->ivars.as.ary[i].name == id) {
			    GC_WB(&ROBJECT(obj)->ivars.as.ary[i].value, val);
			    new_ivar = false;
			    break;
			}
		    }
		}
		if (new_ivar) {
		    if (len + 1 == RB_IVAR_ARY_MAX) {
			CFMutableDictionaryRef tbl;
			tbl = CFDictionaryCreateMutable(kCFAllocatorMalloc, 0, NULL, 
				&rb_cfdictionary_value_cb);

			for (i = 0; i < len; i++) 
			    CFDictionarySetValue(tbl, 
				(const void *)ROBJECT(obj)->ivars.as.ary[i].name, 
				(const void *)ROBJECT(obj)->ivars.as.ary[i].value);

			CFDictionarySetValue(tbl, (const void *)id, 
				(const void *)val);

			xfree(ROBJECT(obj)->ivars.as.ary);
			GC_WB(&ROBJECT(obj)->ivars.as.tbl, tbl);
			CFMakeCollectable(tbl);
			RB_IVAR_SET_TYPE(ROBJECT(obj)->ivars, RB_IVAR_TBL);
		    }
		    else {
			if (len > 0) {
			    struct rb_ivar_ary_entry *ary;
			    ary = ROBJECT(obj)->ivars.as.ary;
			    REALLOC_N(ary, struct rb_ivar_ary_entry, len + 1);	    
			    GC_WB(&ROBJECT(obj)->ivars.as.ary, ary);
			}
			ROBJECT(obj)->ivars.as.ary[len].name = id;
			GC_WB(&ROBJECT(obj)->ivars.as.ary[len].value, val);
			RB_IVAR_ARY_SET_LEN(ROBJECT(obj)->ivars, len + 1);
		    }
		}
		break;
	    }

	    case RB_IVAR_TBL:
		CFDictionarySetValue(ROBJECT(obj)->ivars.as.tbl, 
			(const void *)id, (const void *)val);
		break;
	}
#else
        iv_index_tbl = ROBJECT_IV_INDEX_TBL(obj);
        if (!iv_index_tbl) {
            VALUE klass = rb_obj_class(obj);
            iv_index_tbl = RCLASS_IV_INDEX_TBL(klass);
            if (!iv_index_tbl) {
		GC_WB(&RCLASS_IV_INDEX_TBL(klass), st_init_numtable());
                iv_index_tbl = RCLASS_IV_INDEX_TBL(klass);
            }
        }
        ivar_extended = 0;
        if (!st_lookup(iv_index_tbl, id, &index)) {
            index = iv_index_tbl->num_entries;
            st_add_direct(iv_index_tbl, id, index);
            ivar_extended = 1;
        }
        len = ROBJECT_NUMIV(obj);
        if (len <= index) {
            VALUE *ptr = ROBJECT_IVPTR(obj);
            if (index < ROBJECT_EMBED_LEN_MAX) {
                RBASIC(obj)->flags |= ROBJECT_EMBED;
                ptr = ROBJECT(obj)->as.ary;
                for (i = 0; i < ROBJECT_EMBED_LEN_MAX; i++) {
                    ptr[i] = Qundef;
                }
            }
            else {
                VALUE *newptr;
                long newsize = (index+1) + (index+1)/4; /* (index+1)*1.25 */
                if (!ivar_extended &&
                    iv_index_tbl->num_entries < newsize) {
                    newsize = iv_index_tbl->num_entries;
                }
                if (RBASIC(obj)->flags & ROBJECT_EMBED) {
                    newptr = ALLOC_N(VALUE, newsize);
                    MEMCPY(newptr, ptr, VALUE, len);
                    RBASIC(obj)->flags &= ~ROBJECT_EMBED;
                    GC_WB(&ROBJECT(obj)->as.heap.ivptr, newptr);
                }
                else {
                    REALLOC_N(ROBJECT(obj)->as.heap.ivptr, VALUE, newsize);
                    newptr = ROBJECT(obj)->as.heap.ivptr;
                }
                for (; len < newsize; len++)
                    newptr[len] = Qundef;
                ROBJECT(obj)->as.heap.numiv = newsize;
                ROBJECT(obj)->as.heap.iv_index_tbl = iv_index_tbl;
            }
        }
        GC_WB(&ROBJECT_IVPTR(obj)[index], val);
#endif
	break;
      case T_CLASS:
      case T_MODULE:
#if WITH_OBJC
      {
	  CFMutableDictionaryRef iv_dict = rb_class_ivar_dict_or_create(obj);
	  CFDictionarySetValue(iv_dict, (const void *)id, (const void *)val);
	  break;
      }
#else
	if (!RCLASS_IV_TBL(obj)) {
	    GC_WB(&RCLASS_IV_TBL(obj), st_init_numtable());
	}
	st_insert(RCLASS_IV_TBL(obj), id, val);
        break;
#endif
#if WITH_OBJC
      case T_NATIVE:
	rb_objc_flag_set((const void *)obj, FL_EXIVAR, true);
	generic_ivar_set(obj, id, val);
	break;
#endif
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
#if !WITH_OBJC
    struct st_table *iv_index_tbl;
    st_data_t index;
#endif

    switch (TYPE(obj)) {
      case T_OBJECT:
#if WITH_OBJC
	val = Qundef;
	switch (RB_IVAR_TYPE(ROBJECT(obj)->ivars)) {
	    case RB_IVAR_ARY:
	    {
		int i;
		for (i = 0; i < RB_IVAR_ARY_LEN(ROBJECT(obj)->ivars); i++) {
		    if (ROBJECT(obj)->ivars.as.ary[i].name == id) {
			val = ROBJECT(obj)->ivars.as.ary[i].value;
			break;
		    }
		}
		break;
	    }

	    case RB_IVAR_TBL:
		if (CFDictionaryGetValueIfPresent(
		    (CFDictionaryRef)ROBJECT(obj)->ivars.as.tbl,
		    (const void *)id, NULL))
		    val = Qtrue;
		break;
	}
#else
        iv_index_tbl = ROBJECT_IV_INDEX_TBL(obj);
        if (!iv_index_tbl) break;
        if (!st_lookup(iv_index_tbl, id, &index)) break;
        if (ROBJECT_NUMIV(obj) <= index) break;
        val = ROBJECT_IVPTR(obj)[index];
#endif
        if (val != Qundef)
            return Qtrue;
	break;
      case T_CLASS:
      case T_MODULE:
#if WITH_OBJC
      {
	  CFDictionaryRef iv_dict = rb_class_ivar_dict(obj);
	  if (iv_dict != NULL && CFDictionaryGetValueIfPresent(iv_dict, (const void *)id, NULL))
	      return Qtrue;
	  break;
      }
#else
	if (RCLASS_IV_TBL(obj) && st_lookup(RCLASS_IV_TBL(obj), id, 0))
	    return Qtrue;
	break;
#endif
#if WITH_OBJC
      case T_NATIVE:
	return generic_ivar_defined(obj, id);
#endif
      default:
	if (FL_TEST(obj, FL_EXIVAR) || rb_special_const_p(obj))
	    return generic_ivar_defined(obj, id);
	break;
    }
    return Qfalse;
}

struct obj_ivar_tag {
    VALUE obj;
    int (*func)(ID key, VALUE val, st_data_t arg);
    st_data_t arg;
};

#if !WITH_OBJC
static int
obj_ivar_i(ID key, VALUE index, struct obj_ivar_tag *data)
{
    if (index < ROBJECT_NUMIV(data->obj)) {
        VALUE val = ROBJECT_IVPTR(data->obj)[index];
        if (val != Qundef) {
            return (data->func)(key, val, data->arg);
        }
    }
    return ST_CONTINUE;
}

static void
obj_ivar_each(VALUE obj, int (*func)(ANYARGS), st_data_t arg)
{
    st_table *tbl;
    struct obj_ivar_tag data;

    tbl = ROBJECT_IV_INDEX_TBL(obj);
    if (!tbl)
        return;

    data.obj = obj;
    data.func = (int (*)(ID key, VALUE val, st_data_t arg))func;
    data.arg = arg;

    st_foreach_safe(tbl, obj_ivar_i, (st_data_t)&data);
}
#endif

void rb_ivar_foreach(VALUE obj, int (*func)(ANYARGS), st_data_t arg)
{
    switch (TYPE(obj)) {
      case T_OBJECT:
#if WITH_OBJC
	switch (RB_IVAR_TYPE(ROBJECT(obj)->ivars)) {
	     case RB_IVAR_ARY:
	     {
		int i;
		for (i = 0; i < RB_IVAR_ARY_LEN(ROBJECT(obj)->ivars); i++) {
		    if ((*func)(ROBJECT(obj)->ivars.as.ary[i].name, 
				ROBJECT(obj)->ivars.as.ary[i].value, arg) 
			    	    != ST_CONTINUE)
			break;
		}
		break;
	     }

	     case RB_IVAR_TBL:
		 CFDictionaryApplyFunction(ROBJECT(obj)->ivars.as.tbl, 
		     (CFDictionaryApplierFunction)func, (void *)arg);
		 break;
	}
#else
        obj_ivar_each(obj, func, arg);
#endif
	return;
      case T_CLASS:
      case T_MODULE:
#if WITH_OBJC
      {
	  CFMutableDictionaryRef iv_dict = rb_class_ivar_dict(obj);
	  if (iv_dict != NULL)
	      ivar_dict_foreach((VALUE)iv_dict, func, arg);
      }
#else
	if (RCLASS_IV_TBL(obj)) {
	    st_foreach_safe(RCLASS_IV_TBL(obj), func, arg);
	}
#endif
	return;
#if WITH_OBJC
      case T_NATIVE:
	goto generic;
#endif
    }
    if (!FL_TEST(obj, FL_EXIVAR) && !rb_special_const_p(obj))
	return;
generic:
#if WITH_OBJC
    if (generic_iv_dict != NULL) {
	CFDictionaryRef obj_dict;

	obj_dict = (CFDictionaryRef)CFDictionaryGetValue(
	    (CFDictionaryRef)generic_iv_dict, (const void *)obj);
	if (obj_dict != NULL)
	    CFDictionaryApplyFunction(obj_dict, 
		(CFDictionaryApplierFunction)func, (void *)arg);
    }
#else
    if (!generic_iv_tbl) break;
    if (FL_TEST(obj, FL_EXIVAR) || rb_special_const_p(obj)) {
	st_data_t tbl;

	if (st_lookup(generic_iv_tbl, obj, &tbl)) {
	    st_foreach_safe((st_table *)tbl, func, arg);
	}
    }
#endif
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
rb_obj_remove_instance_variable(VALUE obj, VALUE name)
{
    VALUE val = Qnil;
    ID id = rb_to_id(name);
#if !WITH_OBJC
    struct st_table *iv_index_tbl;
    st_data_t index;
#endif

    if (!OBJ_TAINTED(obj) && rb_safe_level() >= 4)
	rb_raise(rb_eSecurityError, "Insecure: can't modify instance variable");
    if (OBJ_FROZEN(obj)) rb_error_frozen("object");
    if (!rb_is_instance_id(id)) {
	rb_name_error(id, "`%s' is not allowed as an instance variable name", rb_id2name(id));
    }

    switch (TYPE(obj)) {
      case T_OBJECT:
#if WITH_OBJC
	switch (RB_IVAR_TYPE(ROBJECT(obj)->ivars)) {
	    case RB_IVAR_ARY:
	    {
		int i;
		for (i = 0; i < RB_IVAR_ARY_LEN(ROBJECT(obj)->ivars); i++) {
		    if (ROBJECT(obj)->ivars.as.ary[i].name == id) {
			val = ROBJECT(obj)->ivars.as.ary[i].value;
			ROBJECT(obj)->ivars.as.ary[i].value = Qundef;
			return val;
		    }
		}
		break;
	    }

	    case RB_IVAR_TBL:
		if (CFDictionaryGetValueIfPresent(
		    (CFDictionaryRef)ROBJECT(obj)->ivars.as.tbl,
		    (const void *)id,
		    (const void **)val)) {
		    CFDictionaryRemoveValue(ROBJECT(obj)->ivars.as.tbl, 
		       (const void *)id);
		    return val;
		}
		break;
	}
#else
        iv_index_tbl = ROBJECT_IV_INDEX_TBL(obj);
        if (!iv_index_tbl) break;
        if (!st_lookup(iv_index_tbl, id, &index)) break;
        if (ROBJECT_NUMIV(obj) <= index) break;
        val = ROBJECT_IVPTR(obj)[index];
        if (val != Qundef) {
            ROBJECT_IVPTR(obj)[index] = Qundef;
            return val;
        }
#endif
	break;
      case T_CLASS:
      case T_MODULE:
#if WITH_OBJC
      {
	  CFMutableDictionaryRef iv_dict = rb_class_ivar_dict(obj);
	  if (iv_dict != NULL && CFDictionaryGetValueIfPresent((CFDictionaryRef)iv_dict, (const void *)id, (const void **)&val)) {
	      CFDictionaryRemoveValue(iv_dict, (const void *)id);
	      return val;
	  }	      
	  break;
      }
#else
	if (RCLASS_IV_TBL(obj) && st_delete(RCLASS_IV_TBL(obj), (st_data_t*)&id, &val)) {
	    return val;
	}
	break;
#endif
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
rb_mod_const_missing(VALUE klass, VALUE name)
{
    rb_frame_pop(); /* pop frame for "const_missing" */
    uninitialized_constant(klass, rb_to_id(name));
    return Qnil;		/* not reached */
}

#if WITH_OBJC
static void *rb_mark_tbl = (void *)0x42;
#endif

static struct st_table *
check_autoload_table(VALUE av)
{
    Check_Type(av, T_DATA);
    if (RDATA(av)->dmark != (RUBY_DATA_FUNC)rb_mark_tbl ||
	RDATA(av)->dfree != (RUBY_DATA_FUNC)st_free_table) {
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

#if WITH_OBJC
    if ((av = rb_attr_get(mod, id)) != Qnil && av != Qundef)
#else
    if ((tbl = RCLASS_IV_TBL(mod)) && st_lookup(tbl, id, &av) && av != Qundef)
#endif
	return;

    rb_const_set(mod, id, Qundef);
#if WITH_OBJC
    if ((av = rb_attr_get(mod, autoload)) != Qnil) {
#else
    tbl = RCLASS_IV_TBL(mod);
    if (st_lookup(tbl, autoload, &av)) {
#endif
	tbl = check_autoload_table(av);
    }
    else {
	av = Data_Wrap_Struct(rb_cData, rb_mark_tbl, st_free_table, 0);
#if WITH_OBJC
	rb_ivar_set(mod, autoload, av);
#else
	st_add_direct(tbl, autoload, av);
#endif
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

#if WITH_OBJC
    CFMutableDictionaryRef iv_dict = rb_class_ivar_dict(mod);
    assert(iv_dict != NULL);
    CFDictionaryRemoveValue(iv_dict, (const void *)id);
    if (CFDictionaryGetValueIfPresent((CFDictionaryRef)iv_dict, 
	(const void *)autoload, (const void **)&val)) {
#else
    st_delete(RCLASS_IV_TBL(mod), (st_data_t*)&id, 0);
    if (st_lookup(RCLASS_IV_TBL(mod), autoload, &val)) {
#endif
	struct st_table *tbl = check_autoload_table(val);

	st_delete(tbl, (st_data_t*)&id, &load);

	if (tbl->num_entries == 0) {
	    DATA_PTR(val) = 0;
	    st_free_table(tbl);
	    id = autoload;
#if WITH_OBJC
	    CFDictionaryRemoveValue(iv_dict, (const void *)id);
#else
	    if (st_delete(RCLASS_IV_TBL(mod), (st_data_t*)&id, &val)) {
		rb_gc_force_recycle(val);
	    }
#endif
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

#if WITH_OBJC
    CFMutableDictionaryRef iv_dict = rb_class_ivar_dict(mod);
    assert(iv_dict != NULL);
    if (!CFDictionaryGetValueIfPresent((CFDictionaryRef)iv_dict, 
	   (const void *)autoload, (const void **)&val)
	|| (tbl = check_autoload_table(val)) == NULL
	|| !st_lookup(tbl, id, &load))
	return Qnil;
#else
    if (!st_lookup(RCLASS_IV_TBL(mod), autoload, &val) ||
	!(tbl = check_autoload_table(val)) || !st_lookup(tbl, id, &load)) {
	return Qnil;
    }
#endif
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
#if WITH_OBJC
	CFDictionaryRemoveValue(iv_dict, (const void *)id);
#else
	if (st_delete(RCLASS_IV_TBL(mod), (st_data_t*)&id, &val)) {
	    rb_gc_force_recycle(val);
	}
#endif
    }
    return Qnil;
}

VALUE
rb_autoload_p(VALUE mod, ID id)
{
#if WITH_OBJC
    CFDictionaryRef iv_dict = (CFDictionaryRef)rb_class_ivar_dict(mod);
    VALUE val;

    if (iv_dict == NULL 
	|| !CFDictionaryGetValueIfPresent(iv_dict, (const void *)id, (const void **)&val)
	|| val != Qundef)
	return Qnil;
#else
    struct st_table *tbl = RCLASS_IV_TBL(mod);
    VALUE val;

    if (!tbl || !st_lookup(tbl, id, &val) || val != Qundef) {
	return Qnil;
    }
#endif
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
#if WITH_OBJC
	CFDictionaryRef iv_dict;
	while ((iv_dict = rb_class_ivar_dict(tmp)) != NULL
	       && CFDictionaryGetValueIfPresent(iv_dict, (const void *)id, (const void **)&value)) {
#else
	while (RCLASS_IV_TBL(tmp) && st_lookup(RCLASS_IV_TBL(tmp),id,&value)) {
#endif
	    if (value == Qundef) {
		if (!RTEST(rb_autoload_load(tmp, id))) break;
		continue;
	    }
	    if (exclude && tmp == rb_cObject && klass != rb_cObject) {
		rb_warn("toplevel constant %s referenced by %s::%s",
			rb_id2name(id), rb_class2name(klass), rb_id2name(id));
	    }
#if WITH_OBJC
	    value = rb_objc_resolve_const_value(value, klass, id);
#endif
	    return value;
	}
	if (!recurse && klass != rb_cObject) break;
#if WITH_OBJC
	VALUE inc_mods = rb_attr_get(tmp, idIncludedModules);
	if (inc_mods != Qnil) {
	    int i, count = RARRAY_LEN(inc_mods);
	    for (i = 0; i < count; i++) {
		iv_dict = rb_class_ivar_dict(RARRAY_AT(inc_mods, i));
		if (CFDictionaryGetValueIfPresent(iv_dict, (const void *)id, (const void **)&value))
		    return rb_objc_resolve_const_value(value, klass, id);
	    }
	}
#endif
	tmp = RCLASS_SUPER(tmp);
    }
    if (!exclude && !mod_retry && BUILTIN_TYPE(klass) == T_MODULE) {
	mod_retry = 1;
	tmp = rb_cObject;
	goto retry;
    }

#if WITH_OBJC
    {
      /* Classes are typically pre-loaded by Kernel#framework and imported by
       * rb_objc_resolve_const_value(), but it is still useful to keep the
       * dynamic import facility, because someone in the Objective-C world may
       * dynamically define classes at runtime (like ScriptingBridge.framework).
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
      if (k != NULL && !RCLASS_RUBY(k))
          return (VALUE)k;
    }
#endif

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

    rb_vm_change_state();

    if (!rb_is_const_id(id)) {
	rb_name_error(id, "`%s' is not allowed as a constant name", rb_id2name(id));
    }
    if (!OBJ_TAINTED(mod) && rb_safe_level() >= 4)
	rb_raise(rb_eSecurityError, "Insecure: can't remove constant");
    if (OBJ_FROZEN(mod)) rb_error_frozen("class/module");

#if WITH_OBJC
    CFMutableDictionaryRef iv_dict = rb_class_ivar_dict(mod);
    if (iv_dict != NULL && CFDictionaryGetValueIfPresent((CFDictionaryRef)iv_dict, (const void *)id, (const void **)&val)) {
	CFDictionaryRemoveValue(iv_dict, (const void *)id);
#else	
    if (RCLASS_IV_TBL(mod) && st_delete(RCLASS_IV_TBL(mod), (st_data_t*)&id, &val)) {
#endif
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
#if WITH_OBJC
    CFMutableDictionaryRef iv_dict = rb_class_ivar_dict(mod);
    if (iv_dict != NULL)
	ivar_dict_foreach((VALUE)iv_dict, sv_i, (VALUE)tbl);
#else
    if (RCLASS_IV_TBL(mod)) {
	st_foreach_safe(RCLASS_IV_TBL(mod), sv_i, (st_data_t)tbl);
    }
#endif
    return tbl;
}

void*
rb_mod_const_of(VALUE mod, void *data)
{
    VALUE tmp = mod;
    for (;;) {
	data = rb_mod_const_at(tmp, data);
	tmp = RCLASS_SUPER(tmp);
	if (!tmp) break;
	if (tmp == rb_cObject && mod != rb_cObject) break;
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

    if (!tbl) return rb_ary_new2(0);
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
rb_mod_constants(int argc, VALUE *argv, VALUE mod)
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
#if WITH_OBJC
	CFDictionaryRef iv_dict = rb_class_ivar_dict(tmp);
	if (iv_dict != NULL && CFDictionaryGetValueIfPresent(iv_dict, 
	    (const void *)id, (const void **)&value)) {
#else
	if (RCLASS_IV_TBL(tmp) && st_lookup(RCLASS_IV_TBL(tmp), id, &value)) {
#endif
	    if (value == Qundef && NIL_P(autoload_file(klass, id)))
		return Qfalse;
	    return Qtrue;
	}
	if (!recurse && klass != rb_cObject) break;
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

    if (!OBJ_TAINTED(klass) && rb_safe_level() >= 4)
      rb_raise(rb_eSecurityError, "Insecure: can't set %s", dest);
    if (OBJ_FROZEN(klass)) {
	if (BUILTIN_TYPE(klass) == T_MODULE) {
	    rb_error_frozen("module");
	}
	else {
	    rb_error_frozen("class");
	}
    }
#if WITH_OBJC
    CFMutableDictionaryRef iv_dict = rb_class_ivar_dict_or_create(klass);
#else
    if (!RCLASS_IV_TBL(klass)) {
	GC_WB(&RCLASS_IV_TBL(klass), st_init_numtable());
    }
    else
#endif
    if (isconst) {
	VALUE value = Qfalse;

#if WITH_OBJC
	if (CFDictionaryGetValueIfPresent((CFDictionaryRef)iv_dict, (const void *)id, (const void **)&value)) {
#else
	if (st_lookup(RCLASS_IV_TBL(klass), id, &value)) {
#endif
	    if (value == Qundef)
	      autoload_delete(klass, id);
	    else
	      rb_warn("already initialized %s %s", dest, rb_id2name(id));
	}
    }

    if (isconst) {
	rb_vm_change_state();
    }
#if WITH_OBJC
    DLOG("CONS", "%s::%s <- %p", class_getName((Class)klass), rb_id2name(id), (void *)val);
    CFDictionarySetValue(iv_dict, (const void *)id, (const void *)val);
#else
    st_insert(RCLASS_IV_TBL(klass), id, val);
#endif
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

#if WITH_OBJC
# define IV_LOOKUP(k,i,v) ((iv_dict = rb_class_ivar_dict(k)) != NULL && CFDictionaryGetValueIfPresent((CFDictionaryRef)iv_dict, (const void *)i, (const void **)(v)))
#else
# define IV_LOOKUP(k,i,v) (RCLASS_IV_TBL(k) && st_lookup(RCLASS_IV_TBL(k),(i),(v)))
#endif

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
#if WITH_OBJC
    CFMutableDictionaryRef iv_dict;
#endif

    tmp = klass;
#if WITH_OBJC
    if (!RCLASS_META(klass)) 
	tmp = klass = *(VALUE *)klass;
#endif
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
#if WITH_OBJC
		CFDictionaryRemoveValue(rb_class_ivar_dict(front), (const void *)did);
#else
		st_delete(RCLASS_IV_TBL(front),&did,0);
#endif
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
#if WITH_OBJC
    CFMutableDictionaryRef iv_dict;
#endif

    tmp = klass;
#if WITH_OBJC
    if (!RCLASS_META(klass)) 
	tmp = klass = *(VALUE *)klass;
#endif
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
#if WITH_OBJC
	    CFDictionaryRemoveValue(rb_class_ivar_dict(front), (const void *)did);
#else
	    st_delete(RCLASS_IV_TBL(front),&did,0);
#endif
	}
    }
    return value;
}

VALUE
rb_cvar_defined(VALUE klass, ID id)
{
#if WITH_OBJC
    CFMutableDictionaryRef iv_dict;
#endif
    if (!klass) return Qfalse;
#if WITH_OBJC
    if (!RCLASS_META(klass)) 
	klass = *(VALUE *)klass;
#endif
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
rb_mod_class_variables(VALUE obj)
{
    VALUE ary = rb_ary_new();

#if WITH_OBJC
    CFMutableDictionaryRef iv_dict = rb_class_ivar_dict(obj);
    if (iv_dict != NULL)
	ivar_dict_foreach((VALUE)iv_dict, cv_i, (VALUE)ary);
#else
    if (RCLASS_IV_TBL(obj)) {
	st_foreach_safe(RCLASS_IV_TBL(obj), cv_i, ary);
    }
#endif
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
rb_mod_remove_cvar(VALUE mod, VALUE name)
{
    ID id = rb_to_id(name);
    VALUE val;

    if (!rb_is_class_id(id)) {
	rb_name_error(id, "wrong class variable name %s", rb_id2name(id));
    }
    if (!OBJ_TAINTED(mod) && rb_safe_level() >= 4)
	rb_raise(rb_eSecurityError, "Insecure: can't remove class variable");
    if (OBJ_FROZEN(mod)) rb_error_frozen("class/module");

#if WITH_OBJC
    CFMutableDictionaryRef iv_dict = rb_class_ivar_dict(mod);
    if (iv_dict != NULL && CFDictionaryGetValueIfPresent((CFDictionaryRef)iv_dict, (const void *)id, (const void **)&val)) {
	CFDictionaryRemoveValue(iv_dict, (const void *)id);
	return val;
    }
#else
    if (RCLASS_IV_TBL(mod) && st_delete(RCLASS_IV_TBL(mod), (st_data_t*)&id, &val)) {
	return val;
    }
#endif
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
