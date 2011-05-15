/* 
 * MacRuby implementation of Ruby 1.9's eval.c.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 * 
 * Copyright (C) 2007-2011, Apple Inc. All rights reserved.
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 * Copyright (C) 2000 Network Applied Communication Laboratory, Inc.
 * Copyright (C) 2000 Information-technology Promotion Agency, Japan
 */

#include "macruby_internal.h"
#include "ruby/node.h"
#include "vm.h"
#include "dtrace.h"
#include "id.h"
#include "class.h"

VALUE proc_invoke(VALUE, VALUE, VALUE, VALUE);

ID rb_frame_callee(void);
VALUE rb_eLocalJumpError;
VALUE rb_eSysStackError;
VALUE sysstack_error;

static VALUE exception_error;

#include "eval_error.c"
#include "eval_safe.c"
#include "eval_jump.c"

/* initialize ruby */

extern char ***_NSGetEnviron(void);
#define environ (*_NSGetEnviron())
char **rb_origenviron;

void rb_call_inits(void);
void Init_ext(void);
void Init_PreGC(void);
void Init_PreVM(void);
void Init_PreClass(void);
void Init_PreGCD(void);
void Init_PreEncoding(void);

bool ruby_initialized = false;

void
ruby_init(void)
{
    if (ruby_initialized) {
	return;
    }
    ruby_initialized = true;

    rb_origenviron = environ;

    Init_PreClass();	// requires nothing
    Init_PreGC(); 	// requires nothing
    Init_PreVM(); 	// requires nothing
    Init_PreGCD(); 	// requires nothing
    Init_PreEncoding(); // requires rb_cEncoding, GC

    rb_call_inits();
    ruby_prog_init();
}

void *
ruby_options(int argc, char **argv)
{
#if MACRUBY_STATIC
    printf("command-line options are not supported in MacRuby static\n");
    abort();
#else
    return ruby_process_options(argc, argv);
#endif
}

static void
ruby_finalize_0(void)
{
    rb_trap_exit();
    rb_exec_end_proc();
}

static void
ruby_finalize_1(void)
{
    rb_vm_finalize();
    ruby_sig_finalize();
    //GET_THREAD()->errinfo = Qnil;
}

void
ruby_finalize(void)
{
    ruby_finalize_0();
    ruby_finalize_1();
}

int
ruby_cleanup(int ex)
{
#if 1
    return 0;
#else
    int state;
    volatile VALUE errs[2];
    rb_thread_t *th = GET_THREAD();
    int nerr;

    errs[1] = th->errinfo;
    th->safe_level = 0;

    ruby_finalize_0();

    errs[0] = th->errinfo;
    PUSH_TAG();
    if ((state = EXEC_TAG()) == 0) {
	//SAVE_ROOT_JMPBUF(th, rb_thread_terminate_all());
    }
    else if (ex == 0) {
	ex = state;
    }
    GC_WB(&th->errinfo, errs[1]);
    ex = error_handle(ex);
    ruby_finalize_1();
    POP_TAG();
    //rb_thread_stop_timer_thread();

    for (nerr = 0; nerr < sizeof(errs) / sizeof(errs[0]); ++nerr) {
	VALUE err = errs[nerr];

	if (!RTEST(err)) continue;

	/* th->errinfo contains a NODE while break'ing */
	if (TYPE(err) == T_NODE) continue;

	if (rb_obj_is_kind_of(err, rb_eSystemExit)) {
	    return sysexit_status(err);
	}
	else if (rb_obj_is_kind_of(err, rb_eSignal)) {
	    VALUE sig = rb_iv_get(err, "signo");
	    ruby_default_signal(NUM2INT(sig));
	}
	else if (ex == 0) {
	    ex = 1;
	}
    }

#if EXIT_SUCCESS != 0 || EXIT_FAILURE != 1
    switch (ex) {
#if EXIT_SUCCESS != 0
      case 0: return EXIT_SUCCESS;
#endif
#if EXIT_FAILURE != 1
      case 1: return EXIT_FAILURE;
#endif
    }
#endif

    return ex;
#endif
}

void
ruby_stop(int ex)
{
    exit(ruby_cleanup(ex));
}

extern VALUE rb_progname;

void rb_require_libraries(void);

int
ruby_executable_node(void *n, int *status)
{
    VALUE v = (VALUE)n;
    int s;

    switch (v) {
      case Qtrue:
        s = EXIT_SUCCESS;
	break;
      case Qfalse:
	s = EXIT_FAILURE;
	break;
      default:
	if (!FIXNUM_P(v)) {
	    return TRUE;
	}
	s = FIX2INT(v);
    }
    if (status) {
	*status = s;
    }
    return FALSE;
}

#if !defined(MACRUBY_STATIC)
int
ruby_run_node(void *n)
{
    int status;
    if (!ruby_executable_node(n, &status)) {
	ruby_cleanup(0);
	return status;
    }
    rb_require_libraries();
    rb_vm_run(RSTRING_PTR(rb_progname), (NODE *)n, NULL, false);
    return ruby_cleanup(0);
}
#endif

/*
 *  call-seq:
 *     Module.nesting    => array
 *
 *  Returns the list of +Modules+ nested at the point of call.
 *
 *     module M1
 *       module M2
 *         $a = Module.nesting
 *       end
 *     end
 *     $a           #=> [M1::M2, M1]
 *     $a[0].name   #=> "M1::M2"
 */

static VALUE
rb_mod_nesting(VALUE self, SEL sel)
{
    VALUE ary = rb_ary_new();
    rb_vm_outer_t *root_outer;
    rb_vm_get_outer(&root_outer);
    rb_vm_outer_t *o = root_outer;
    while (o != NULL && o->outer != NULL) {
        VALUE klass = (VALUE)o->klass;
	if (!o->pushed_by_eval && !NIL_P(klass)) {
	    rb_ary_push(ary, klass);
	}
        o = o->outer;
    }
    rb_vm_release_outer(&root_outer);
    return ary;
}

/*
 *  call-seq:
 *     Module.constants   => array
 *
 *  Returns an array of the names of all constants defined in the
 *  system. This list includes the names of all modules and classes.
 *
 *     p Module.constants.sort[1..5]
 *
 *  <em>produces:</em>
 *
 *     ["ARGV", "ArgumentError", "Array", "Bignum", "Binding"]
 */

VALUE rb_mod_constants(VALUE, SEL, int, VALUE *);

static VALUE
rb_mod_s_constants(VALUE mod, SEL sel, int argc, VALUE *argv)
{
    if (argc > 0) {
	return rb_mod_constants(rb_cModule, 0, argc, argv);
    }

    VALUE cbase = 0;
    void *data = 0;
    rb_vm_outer_t *root_outer;
    rb_vm_get_outer(&root_outer);
    rb_vm_outer_t *o = root_outer;
    while (o != NULL) {
        VALUE klass = (VALUE)o->klass;
	if (!o->pushed_by_eval && !NIL_P(klass)) {
	    data = rb_mod_const_at(klass, data);
	    if (cbase == 0) {
		cbase = klass;
	    }
	}
        o = o->outer;
    }
    rb_vm_release_outer(&root_outer);

    if (cbase != 0) {
	data = rb_mod_const_of(cbase, data);
    }
    return rb_const_list(data);
}

void
rb_frozen_class_p(VALUE klass)
{
    const char *desc = "something(?!)";

    if (OBJ_FROZEN(klass)) {
	if (RCLASS_SINGLETON(klass))
	    desc = "object";
	else {
	    switch (TYPE(klass)) {
	      case T_MODULE:
	      case T_ICLASS:
		desc = "module";
		break;
	      case T_CLASS:
		desc = "class";
		break;
	    }
	}
	rb_error_frozen(desc);
    }
}

VALUE rb_make_backtrace(void);

void
rb_exc_raise(VALUE mesg)
{
    rb_vm_raise(mesg);
    abort();
}

void
rb_exc_fatal(VALUE mesg)
{
    rb_vm_raise(mesg);
    abort();
}

void
rb_interrupt(void)
{
    static const char fmt[1] = {'\0'};
    rb_raise(rb_eInterrupt, "%s", fmt);
}

static VALUE get_errinfo(void);

/*
 *  call-seq:
 *     raise
 *     raise(string)
 *     raise(exception [, string [, array]])
 *     fail
 *     fail(string)
 *     fail(exception [, string [, array]])
 *
 *  With no arguments, raises the exception in <code>$!</code> or raises
 *  a <code>RuntimeError</code> if <code>$!</code> is +nil+.
 *  With a single +String+ argument, raises a
 *  +RuntimeError+ with the string as a message. Otherwise,
 *  the first parameter should be the name of an +Exception+
 *  class (or an object that returns an +Exception+ object when sent
 *  an +exception+ message). The optional second parameter sets the
 *  message associated with the exception, and the third parameter is an
 *  array of callback information. Exceptions are caught by the
 *  +rescue+ clause of <code>begin...end</code> blocks.
 *
 *     raise "Failed to create socket"
 *     raise ArgumentError, "No parameters", caller
 */

static VALUE
rb_f_raise(VALUE klass, SEL sel, int argc, VALUE *argv)
{
    VALUE err;
    if (argc == 0) {
	err = get_errinfo();
	if (!NIL_P(err)) {
	    argc = 1;
	    argv = &err;
	}
    }
    rb_vm_raise(rb_make_exception(argc, argv));
    return Qnil;		/* not reached */
}

VALUE
rb_make_exception(int argc, VALUE *argv)
{
    VALUE mesg;
    ID exception;
    int n;

    mesg = Qnil;
    switch (argc) {
      case 0:
	//mesg = Qnil;
	mesg = rb_exc_new2(rb_eRuntimeError, "");
	break;
      case 1:
	if (NIL_P(argv[0])) {
	    break;
	}
	if (TYPE(argv[0]) == T_STRING) {
	    mesg = rb_exc_new3(rb_eRuntimeError, argv[0]);
	    break;
	}
	n = 0;
	goto exception_call;

      case 2:
      case 3:
	n = 1;
      exception_call:
	exception = rb_intern("exception");
	if (!rb_respond_to(argv[0], exception)) {
	    rb_raise(rb_eTypeError, "exception class/object expected");
	}
	mesg = rb_funcall(argv[0], exception, n, argv[1]);
	break;
      default:
	rb_raise(rb_eArgError, "wrong number of arguments");
	break;
    }
    if (argc > 0) {
	if (!rb_obj_is_kind_of(mesg, rb_eException))
	    rb_raise(rb_eTypeError, "exception object expected");
	if (argc > 2)
	    set_backtrace(mesg, argv[2]);
    }

    return mesg;
}

int
rb_iterator_p()
{
    return rb_block_given_p();
}

/*
 *  call-seq:
 *     block_given?   => true or false
 *     iterator?      => true or false
 *
 *  Returns <code>true</code> if <code>yield</code> would execute a
 *  block in the current context. The <code>iterator?</code> form
 *  is mildly deprecated.
 *
 *     def try
 *       if block_given?
 *         yield
 *       else
 *         "no block"
 *       end
 *     end
 *     try                  #=> "no block"
 *     try { "hello" }      #=> "hello"
 *     try do "hello" end   #=> "hello"
 */


static VALUE
rb_f_block_given_p(VALUE self, SEL sel)
{
    return rb_vm_block_saved() ? Qtrue : Qfalse;
}

VALUE rb_eThreadError;

void
rb_need_block()
{
    if (!rb_block_given_p()) {
	// TODO
	//vm_localjump_error("no block given", Qnil, 0);
	rb_raise(rb_eRuntimeError, "no block given");
    }
}

VALUE
rb_rescue(VALUE (* b_proc)(ANYARGS), VALUE data1,
	  VALUE (* r_proc)(ANYARGS), VALUE data2)
{
    return rb_rescue2(b_proc, data1, r_proc, data2, rb_eStandardError,
		      (VALUE)0);
}

// XXX not thread-safe, but it doesn't matter, since clients are C extensions
// which are not reentrant anyways.
static VALUE protect_exc = Qnil;

static VALUE
protect_rescue(VALUE obj, VALUE exc)
{
   int *state = (int *)obj;
   if (state != NULL) {
	*state = 1;
   }
    GC_RETAIN(exc);
    protect_exc = exc;
    return Qnil;
}

VALUE
rb_protect(VALUE (*proc) (VALUE), VALUE data, int *state)
{
    if (state != NULL) {
	*state = 0;
    }
    return rb_rescue2(proc, data, protect_rescue, (VALUE)state,
	    rb_eStandardError, (VALUE)0);
}

void
rb_jump_tag(int state)
{
    assert(state > 0);
    VALUE exc = protect_exc;
    assert(exc != Qnil);
    protect_exc = Qnil;
    GC_RELEASE(exc);
    rb_exc_raise(exc);
}

ID
rb_frame_this_func(void)
{
    // TODO
    return 0;
}

ID
rb_frame_callee(void)
{
    // TODO
    return 0;
}

/*
 *  call-seq:
 *     append_features(mod)   => mod
 *
 *  When this module is included in another, Ruby calls
 *  <code>append_features</code> in this module, passing it the
 *  receiving module in _mod_. Ruby's default implementation is
 *  to add the constants, methods, and module variables of this module
 *  to _mod_ if this module has not already been added to
 *  _mod_ or one of its ancestors. See also <code>Module#include</code>.
 */

static void
check_cyclic_include(VALUE klass, VALUE mod)
{
    VALUE m = mod;
    do {
	if (m == klass) {
	    rb_raise(rb_eArgError, "cyclic include detected");
	}
	m = RCLASS_SUPER(m);
    }
    while (RCLASS_SINGLETON(m));
}

static VALUE
rb_mod_append_features(VALUE module, SEL sel, VALUE include)
{
    VALUE orig = include;
    switch (TYPE(include)) {
	case T_CLASS:
	case T_MODULE:
	    break;
	default:
	    Check_Type(include, T_CLASS);
	    break;
    }
    check_cyclic_include(include, module);

    if (include != rb_cClass && include != rb_cModule && RCLASS_RUBY(include)) {
	VALUE sinclude = rb_make_singleton_class(RCLASS_SUPER(include));
	RCLASS_SET_SUPER(include, sinclude);
	include = sinclude;
    }	
    rb_include_module2(include, orig, module, true, true);

    VALUE m = module;
    do {
	VALUE ary = rb_attr_get(m, idIncludedModules);
	if (ary != Qnil) {
	    for (int i = 0, count = RARRAY_LEN(ary); i < count; i++) {
		VALUE mod = RARRAY_AT(ary, i);
		rb_mod_append_features(mod, sel, include);
	    }
	}
	m = RCLASS_SUPER(m);
    }
    while (m != 0 && RCLASS_SINGLETON(m));

    return module;
}

/*
 *  call-seq:
 *     include(module, ...)    => self
 *
 *  Invokes <code>Module.append_features</code> on each parameter in turn.
 */

static VALUE
rb_mod_include(VALUE module, SEL sel, int argc, VALUE *argv)
{
    int i;

    for (i = 0; i < argc; i++) {
	Check_Type(argv[i], T_MODULE);
    }
    while (argc--) {
	rb_funcall(argv[argc], rb_intern("append_features"), 1, module);
	rb_funcall(argv[argc], rb_intern("included"), 1, module);
    }
    return module;
}

VALUE
rb_obj_call_init(VALUE obj, int argc, VALUE *argv)
{
    return rb_funcall2(obj, idInitialize, argc, argv);
}

void
rb_extend_object(VALUE obj, VALUE module)
{
    VALUE klass;
    if (TYPE(obj) == T_CLASS && RCLASS_RUBY(obj)) {
	VALUE sklass = rb_make_singleton_class(RCLASS_SUPER(obj));
	RCLASS_SET_SUPER(obj, sklass);
	klass = *(VALUE *)sklass;
    }
    else {
	klass = rb_singleton_class(obj);
    }

    rb_include_module(klass, module);

    VALUE m = module;
    do {
	VALUE ary = rb_attr_get(m, idIncludedModules);
	if (ary != Qnil) {
	    for (int i = 0, count = RARRAY_LEN(ary); i < count; i++) {
		VALUE mod = RARRAY_AT(ary, i);
		rb_extend_object(obj, mod);
	    }
	}
	m = RCLASS_SUPER(m);
    }
    while (m != 0 && RCLASS_SINGLETON(m));
}

/*
 *  call-seq:
 *     extend_object(obj)    => obj
 *
 *  Extends the specified object by adding this module's constants and
 *  methods (which are added as singleton methods). This is the callback
 *  method used by <code>Object#extend</code>.
 *
 *     module Picky
 *       def Picky.extend_object(o)
 *         if String === o
 *           puts "Can't add Picky to a String"
 *         else
 *           puts "Picky added to #{o.class}"
 *           super
 *         end
 *       end
 *     end
 *     (s = Array.new).extend Picky  # Call Object.extend
 *     (s = "quick brown fox").extend Picky
 *
 *  <em>produces:</em>
 *
 *     Picky added to Array
 *     Can't add Picky to a String
 */

static VALUE
rb_mod_extend_object(VALUE mod, SEL sel, VALUE obj)
{
    rb_extend_object(obj, mod);
    return obj;
}

/*
 *  call-seq:
 *     obj.extend(module, ...)    => obj
 *
 *  Adds to _obj_ the instance methods from each module given as a
 *  parameter.
 *
 *     module Mod
 *       def hello
 *         "Hello from Mod.\n"
 *       end
 *     end
 *
 *     class Klass
 *       def hello
 *         "Hello from Klass.\n"
 *       end
 *     end
 *
 *     k = Klass.new
 *     k.hello         #=> "Hello from Klass.\n"
 *     k.extend(Mod)   #=> #<Klass:0x401b3bc8>
 *     k.hello         #=> "Hello from Mod.\n"
 */

static VALUE
rb_obj_extend(VALUE obj, SEL sel, int argc, VALUE *argv)
{
    if (OBJ_FROZEN(obj)) {
	rb_raise(rb_eRuntimeError, "cannot extend a frozen object");
    }
    if (argc == 0) {
	rb_raise(rb_eArgError, "wrong number of arguments (0 for 1)");
    }
    for (int i = 0; i < argc; i++) {
	Check_Type(argv[i], T_MODULE);
    }
    while (argc--) {
	rb_funcall(argv[argc], rb_intern("extend_object"), 1, obj);
	rb_funcall(argv[argc], rb_intern("extended"), 1, obj);
    }
    return obj;
}

/*
 *  call-seq:
 *     include(module, ...)   => self
 *
 *  Invokes <code>Module.append_features</code>
 *  on each parameter in turn. Effectively adds the methods and constants
 *  in each module to the receiver.
 */

static VALUE
top_include(VALUE self, SEL sel, int argc, VALUE *argv)
{
#if 0
    rb_thread_t *th = GET_THREAD();

    rb_secure(4);
    if (th->top_wrapper) {
	rb_warning
	    ("main#include in the wrapped load is effective only in wrapper module");
	return rb_mod_include(argc, argv, th->top_wrapper);
    }
#endif
    return rb_mod_include(rb_cObject, 0, argc, argv);
}

VALUE rb_f_trace_var();
VALUE rb_f_untrace_var();

static VALUE
get_errinfo(void)
{
    VALUE exc = rb_vm_current_exception();
    if (NIL_P(exc)) {
	exc = rb_errinfo();
    }
    return exc;
}

static VALUE
errinfo_getter(ID id)
{
    return get_errinfo();
}

VALUE
rb_rubylevel_errinfo(void)
{
    return get_errinfo();
}

static VALUE
errat_getter(ID id)
{
    VALUE err = get_errinfo();
    if (!NIL_P(err)) {
	return get_backtrace(err);
    }
    else {
	return Qnil;
    }
}

static void
errat_setter(VALUE val, ID id, VALUE *var)
{
    VALUE err = get_errinfo();
    if (NIL_P(err)) {
	rb_raise(rb_eArgError, "$! not set");
    }
    set_backtrace(err, val);
}

/*
 *  call-seq:
 *     local_variables    => array
 *
 *  Returns the names of the current local variables.
 *
 *     fred = 1
 *     for i in 1..10
 *        # ...
 *     end
 *     local_variables   #=> ["fred", "i"]
 */

static VALUE
rb_f_local_variables(VALUE rcv, SEL sel)
{
    rb_vm_binding_t *b = rb_vm_current_binding();
    VALUE ary = rb_ary_new();
    while (b != NULL) {
	rb_vm_local_t *l;
	for (l = b->locals; l != NULL; l = l->next) {
	    rb_ary_push(ary, ID2SYM(l->name));
	}
	b = b->next;
    }
    return ary;
}


/*
 *  call-seq:
 *     __method__         => symbol
 *     __callee__         => symbol
 *
 *  Returns the name of the current method as a Symbol.
 *  If called outside of a method, it returns <code>nil</code>.
 *
 */

static VALUE
rb_f_method_name(VALUE rcv, SEL sel)
{
    return Qnil;
}

void Init_vm_eval(void);
void Init_eval_method(void);

VALUE rb_f_eval(VALUE self, SEL sel, int argc, VALUE *argv);
VALUE rb_f_global_variables(VALUE rcv, SEL sel);
VALUE rb_mod_module_eval(VALUE mod, SEL sel, int argc, VALUE *argv);
VALUE rb_f_trace_var(VALUE rcv, SEL sel, int argc, VALUE *argv);
VALUE rb_f_untrace_var(VALUE rcv, SEL sel, int argc, VALUE *argv);

void
Init_eval(void)
{
    rb_define_virtual_variable("$@", errat_getter, errat_setter);
    rb_define_virtual_variable("$!", errinfo_getter, 0);

    rb_objc_define_module_function(rb_mKernel, "eval", rb_f_eval, -1);
    rb_objc_define_module_function(rb_mKernel, "iterator?", rb_f_block_given_p, 0);
    rb_objc_define_module_function(rb_mKernel, "block_given?", rb_f_block_given_p, 0);

    rb_objc_define_module_function(rb_mKernel, "fail", rb_f_raise, -1);
    rb_objc_define_module_function(rb_mKernel, "raise", rb_f_raise, -1);

    rb_objc_define_module_function(rb_mKernel, "global_variables", rb_f_global_variables, 0);	/* in variable.c */
    rb_objc_define_module_function(rb_mKernel, "local_variables", rb_f_local_variables, 0);

    rb_objc_define_method(rb_mKernel, "__method__", rb_f_method_name, 0);
    rb_objc_define_method(rb_mKernel, "__callee__", rb_f_method_name, 0);

    rb_objc_define_private_method(rb_cModule, "append_features", rb_mod_append_features, 1);
    rb_objc_define_private_method(rb_cModule, "extend_object", rb_mod_extend_object, 1);
    rb_objc_define_private_method(rb_cModule, "include", rb_mod_include, -1);
    rb_objc_define_method(rb_cModule, "module_eval", rb_mod_module_eval, -1);
    rb_objc_define_method(rb_cModule, "class_eval", rb_mod_module_eval, -1);

    rb_undef_method(rb_cClass, "module_function");

    Init_vm_eval();
    Init_eval_method();

    rb_objc_define_method(*(VALUE *)rb_cModule, "nesting", rb_mod_nesting, 0);
    rb_objc_define_method(*(VALUE *)rb_cModule, "constants", rb_mod_s_constants, -1);

    VALUE cTopLevel = *(VALUE *)rb_vm_top_self();    
    rb_objc_define_method(cTopLevel, "include", top_include, -1);

    rb_objc_define_method(rb_mKernel, "extend", rb_obj_extend, -1);

    rb_objc_define_module_function(rb_mKernel, "trace_var", rb_f_trace_var, -1);	/* in variable.c */
    rb_objc_define_module_function(rb_mKernel, "untrace_var", rb_f_untrace_var, -1);	/* in variable.c */

    rb_define_virtual_variable("$SAFE", safe_getter, safe_setter);

    exception_error = rb_exc_new2(rb_eFatal, "exception reentered");
    //rb_ivar_set(exception_error, idThrowState, INT2FIX(TAG_FATAL));
    GC_RETAIN(exception_error);
}
