/*
 * This file is covered by the Ruby license. See COPYING for more details.
 * 
 * Copyright (C) 2007-2011, Apple Inc. All rights reserved.
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 */

#include "macruby_internal.h"
#include "ruby/st.h"
#include "ruby/node.h"
#include "vm.h"
#include "id.h"
#include "class.h"

#include <stdio.h>
#include <stdarg.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifndef EXIT_SUCCESS
#define EXIT_SUCCESS 0
#endif

extern const char ruby_description[];

static int
err_position_0(char *buf, long len, const char *file, int line)
{
    if (!file) {
	return 0;
    }
    else if (line == 0) {
	return snprintf(buf, len, "%s: ", file);
    }
    else {
	return snprintf(buf, len, "%s:%d: ", file, line);
    }
}

static int
err_position(char *buf, long len)
{
    return err_position_0(buf, len, rb_sourcefile(), rb_sourceline());
}

static void
err_snprintf(char *buf, long len, const char *fmt, va_list args)
{
    long n;

    n = err_position(buf, len);
    if (len > n) {
	vsnprintf((char*)buf+n, len-n, fmt, args);
    }
}

static void
compile_snprintf(char *buf, long len, const char *file, int line, const char *fmt, va_list args)
{
    long n;

    n = err_position_0(buf, len, file, line);
    if (len > n) {
	vsnprintf((char*)buf+n, len-n, fmt, args);
    }
}

static void err_append(const char*);

void
rb_compile_error(const char *file, int line, const char *fmt, ...)
{
    va_list args;
    char buf[BUFSIZ];

    va_start(args, fmt);
    compile_snprintf(buf, BUFSIZ, file, line, fmt, args);
    va_end(args);
    err_append(buf);
}

void
rb_compile_error_append(const char *fmt, ...)
{
    va_list args;
    char buf[BUFSIZ];

    va_start(args, fmt);
    vsnprintf(buf, BUFSIZ, fmt, args);
    va_end(args);
    err_append(buf);
}

static void
compile_warn_print(const char *file, int line, const char *fmt, va_list args)
{
    char buf[BUFSIZ];
    int len;

    compile_snprintf(buf, BUFSIZ, file, line, fmt, args);
    len = strlen(buf);
    buf[len++] = '\n';
    rb_write_error2(buf, len);
}

void
rb_compile_warn(const char *file, int line, const char *fmt, ...)
{
    char buf[BUFSIZ];
    va_list args;

    if (NIL_P(ruby_verbose)) return;

    snprintf(buf, BUFSIZ, "warning: %s", fmt);

    va_start(args, fmt);
    compile_warn_print(file, line, buf, args);
    va_end(args);
}

/* rb_compile_warning() reports only in verbose mode */
void
rb_compile_warning(const char *file, int line, const char *fmt, ...)
{
    char buf[BUFSIZ];
    va_list args;

    if (!RTEST(ruby_verbose)) return;

    snprintf(buf, BUFSIZ, "warning: %s", fmt);

    va_start(args, fmt);
    compile_warn_print(file, line, buf, args);
    va_end(args);
}

static void
warn_print(const char *fmt, va_list args)
{
    char buf[BUFSIZ];
    int len;

    err_snprintf(buf, BUFSIZ, fmt, args);
    len = strlen(buf);
    buf[len++] = '\n';
    buf[len] = '\0';
    rb_write_error2(buf, len);
}

void
rb_warn(const char *fmt, ...)
{
    char buf[BUFSIZ];
    va_list args;

    if (NIL_P(ruby_verbose)) return;

    snprintf(buf, BUFSIZ, "warning: %s", fmt);

    va_start(args, fmt);
    warn_print(buf, args);
    va_end(args);
}

/* rb_warning() reports only in verbose mode */
void
rb_warning(const char *fmt, ...)
{
    char buf[BUFSIZ];
    va_list args;

    if (!RTEST(ruby_verbose)) return;

    snprintf(buf, BUFSIZ, "warning: %s", fmt);

    va_start(args, fmt);
    warn_print(buf, args);
    va_end(args);
}

/*
 * call-seq:
 *    warn(msg)   => nil
 *
 * Display the given message (followed by a newline) on STDERR unless
 * warnings are disabled (for example with the <code>-W0</code> flag).
 */

static VALUE
rb_warn_m(VALUE self, SEL sel, VALUE mesg)
{
    if (!NIL_P(ruby_verbose)) {
	rb_io_write(rb_stderr, mesg);
	rb_io_write(rb_stderr, rb_default_rs);
    }
    return Qnil;
}

//void rb_vm_bugreport(void);

static void
report_bug(const char *file, int line, const char *fmt, va_list args)
{
    char buf[BUFSIZ];
    FILE *out = stderr;
    int len = err_position_0(buf, BUFSIZ, file, line);

    if (fwrite(buf, 1, len, out) == len ||
	fwrite(buf, 1, len, (out = stdout)) == len) {
	fputs("[BUG] ", out);
	vfprintf(out, fmt, args);
	fprintf(out, "\n%s\n\n", ruby_description);
	//rb_vm_bugreport();
    }
}

void
rb_bug(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    report_bug(rb_sourcefile(), rb_sourceline(), fmt, args);
    va_end(args);

    abort();
}

void
rb_compile_bug(const char *file, int line, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    report_bug(file, line, fmt, args);
    va_end(args);

    abort();
}

static const struct types {
    int type;
    const char *name;
} builtin_types[] = {
    {T_NIL,	"nil"},
    {T_OBJECT,	"Object"},
    {T_CLASS,	"Class"},
    {T_ICLASS,	"iClass"},	/* internal use: mixed-in module holder */
    {T_MODULE,	"Module"},
    {T_FLOAT,	"Float"},
    {T_STRING,	"String"},
    {T_REGEXP,	"Regexp"},
    {T_ARRAY,	"Array"},
    {T_FIXNUM,	"Fixnum"},
    {T_HASH,	"Hash"},
    {T_STRUCT,	"Struct"},
    {T_BIGNUM,	"Bignum"},
    {T_FILE,	"File"},
    {T_RATIONAL,"Rational"},
    {T_COMPLEX, "Complex"},
    {T_TRUE,	"true"},
    {T_FALSE,	"false"},
    {T_SYMBOL,	"Symbol"},	/* :symbol */
    {T_DATA,	"Data"},	/* internal use: wrapped C pointers */
    {T_MATCH,	"MatchData"},	/* data of $~ */
    {T_NODE,	"Node"},	/* internal use: syntax tree node */
    {T_UNDEF,	"undef"},	/* internal use: #undef; should not happen */
    {T_NATIVE,  "native"}
};

const char *
rb_obj_type(VALUE x)
{
    int i;
    const int t = TYPE(x);
    for (i = 0; i < sizeof(builtin_types) / sizeof(builtin_types[0]); i++) {
	if (t == builtin_types[i].type) {
	    return builtin_types[i].name;
	}
    }
    return "unknown";
}

void
rb_check_type(VALUE x, int t)
{
    const struct types *type = builtin_types;
    const struct types *const typeend = builtin_types +
	sizeof(builtin_types) / sizeof(builtin_types[0]);

    if (x == Qundef) {
	rb_bug("undef leaked to the Ruby space");
    }

    if (TYPE(x) != t) {
	while (type < typeend) {
	    if (type->type == t) {
		const char *etype;

		if (NIL_P(x)) {
		    etype = "nil";
		}
		else if (FIXNUM_P(x)) {
		    etype = "Fixnum";
		}
		else if (SYMBOL_P(x)) {
		    etype = "Symbol";
		}
		else if (rb_special_const_p(x)) {
		    etype = RSTRING_PTR(rb_obj_as_string(x));
		}
		else {
		    etype = rb_obj_classname(x);
		}
		rb_raise(rb_eTypeError, "wrong argument type %s (expected %s)",
			 etype, type->name);
	    }
	    type++;
	}
	rb_bug("unknown type 0x%x (0x%x given)", t, TYPE(x));
    }
}

/* exception classes */
#include <errno.h>

VALUE rb_eException;
VALUE rb_eSystemExit;
VALUE rb_eInterrupt;
VALUE rb_eSignal;
VALUE rb_eFatal;
VALUE rb_eStandardError;
VALUE rb_eRuntimeError;
VALUE rb_eTypeError;
VALUE rb_eArgError;
VALUE rb_eIndexError;
VALUE rb_eKeyError;
VALUE rb_eRangeError;
VALUE rb_eNameError;
VALUE rb_eEncodingError;
VALUE rb_eEncCompatError;
VALUE rb_eUndefinedConversionError;
VALUE rb_eInvalidByteSequenceError;
VALUE rb_eConverterNotFoundError;
VALUE rb_eNoMethodError;
VALUE rb_eSecurityError;
VALUE rb_eNotImpError;
VALUE rb_eNoMemError;
VALUE rb_cNameErrorMesg;

VALUE rb_eScriptError;
VALUE rb_eSyntaxError;
VALUE rb_eLoadError;

VALUE rb_eSystemCallError;
VALUE rb_mErrno;
static VALUE rb_eNOERROR;

VALUE
rb_exc_new(VALUE etype, const char *ptr, long len)
{
    return rb_funcall(etype, rb_intern("new"), 1, rb_str_new(ptr, len));
}

VALUE
rb_exc_new2(VALUE etype, const char *s)
{
    return rb_exc_new(etype, s, strlen(s));
}

VALUE
rb_exc_new3(VALUE etype, VALUE str)
{
    StringValue(str);
    return rb_funcall(etype, rb_intern("new"), 1, str);
}

/*
 * call-seq:
 *    Exception.new(msg = nil)   =>  exception
 *
 *  Construct a new Exception object, optionally passing in 
 *  a message.
 */

static VALUE
exc_initialize(VALUE exc, SEL sel, int argc, VALUE *argv)
{
    VALUE arg;

    rb_scan_args(argc, argv, "01", &arg);
    rb_iv_set(exc, "mesg", arg);
    rb_iv_set(exc, "bt", Qnil);

    return exc;
}

/*
 *  Document-method: exception
 *
 *  call-seq:
 *     exc.exception(string) -> an_exception or exc
 *  
 *  With no argument, or if the argument is the same as the receiver,
 *  return the receiver. Otherwise, create a new
 *  exception object of the same class as the receiver, but with a
 *  message equal to <code>string.to_str</code>.
 *     
 */

static VALUE
exc_exception(VALUE self, SEL sel, int argc, VALUE *argv)
{
    VALUE exc;

    if (argc == 0) return self;
    if (argc == 1 && self == argv[0]) return self;
    exc = rb_obj_clone(self);
    exc_initialize(exc, 0, argc, argv);

    return exc;
}

/*
 * call-seq:
 *   exception.to_s   =>  string
 *
 * Returns exception's message (or the name of the exception if
 * no message is set).
 *
 * Note that on MacRuby this will return the NSException#reason before it will
 * return the +name+.
 *
 */

static VALUE
exc_to_s(VALUE exc, SEL sel)
{
    VALUE mesg = rb_attr_get(exc, rb_intern("mesg"));
    if (NIL_P(mesg)) {
	// first see if it's a NSException with a reason string
	SEL reasonSel = sel_registerName("_reason_before_macruby");
	id reason = objc_msgSend((id)exc, reasonSel);
	if (reason != nil) {
	    mesg = (VALUE)reason;
	} else {
	    // or return the class name, which is what MRI does
	    return rb_class_name(CLASS_OF(exc));
	}
    }
    if (OBJ_TAINTED(exc)) OBJ_TAINT(mesg);
    return mesg;
}

/*
 * call-seq:
 *   exception.message   =>  string
 *
 * Returns the result of invoking <code>exception.to_s</code>.
 * Normally this returns the exception's message or name. By
 * supplying a to_str method, exceptions are agreeing to
 * be used where Strings are expected.
 */

static VALUE
exc_message(VALUE exc, SEL sel)
{
    return rb_funcall(exc, rb_intern("to_s"), 0, 0);
}

/*
 * call-seq:
 *   exception.inspect   => string
 *
 * Return this exception's class name an message
 */

static VALUE
exc_inspect(VALUE exc, SEL sel)
{
    VALUE str, klass;

    klass = CLASS_OF(exc);
    exc = rb_obj_as_string(exc);
    if (RSTRING_LEN(exc) == 0) {
	return rb_str_dup(rb_class_name(klass));
    }

    str = rb_str_buf_new2("#<");
    klass = rb_class_name(klass);
    rb_str_buf_append(str, klass);
    rb_str_buf_cat(str, ": ", 2);
    rb_str_buf_append(str, exc);
    rb_str_buf_cat(str, ">", 1);

    return str;
}

/*
 *  call-seq:
 *     exception.backtrace    => array
 *  
 *  Returns any backtrace associated with the exception. The backtrace
 *  is an array of strings, each containing either ``filename:lineNo: in
 *  `method''' or ``filename:lineNo.''
 *     
 *     def a
 *       raise "boom"
 *     end
 *     
 *     def b
 *       a()
 *     end
 *     
 *     begin
 *       b()
 *     rescue => detail
 *       print detail.backtrace.join("\n")
 *     end
 *     
 *  <em>produces:</em>
 *     
 *     prog.rb:2:in `a'
 *     prog.rb:6:in `b'
 *     prog.rb:10
 *
 *  Note that on MacRuby this will return the NSException#callStackSymbols in
 *  case there's no Ruby backtrace.
*/

static VALUE
exc_backtrace(VALUE exc, SEL sel)
{
    static ID bt;
    if (!bt) bt = rb_intern("bt");

    VALUE res = rb_attr_get(exc, bt);
    if (res == Qnil) {
	SEL symbolsSel = sel_registerName("callStackSymbols");
	id symbols = objc_msgSend((id)exc, symbolsSel);
	if (symbols != nil) {
	    res = (VALUE)symbols;
	}
    }

    return res;
}

VALUE
rb_check_backtrace(VALUE bt)
{
#define BACKTRACE_ERROR "backtrace must be Array of String"
    if (!NIL_P(bt)) {
	const int t = TYPE(bt);
	if (t == T_STRING) {
	    return rb_ary_new4(1, &bt);
	}
	if (t != T_ARRAY) {
	    rb_raise(rb_eTypeError, BACKTRACE_ERROR);
	}
	for (int i = 0, count = RARRAY_LEN(bt); i < count; i++) {
	    if (TYPE(RARRAY_AT(bt, i)) != T_STRING) {
		rb_raise(rb_eTypeError, BACKTRACE_ERROR);
	    }
	}
    }
#undef BACKTRACE_ERROR
    return bt;
}

/*
 *  call-seq:
 *     exc.set_backtrace(array)   =>  array
 *  
 *  Sets the backtrace information associated with <i>exc</i>. The
 *  argument must be an array of <code>String</code> objects in the
 *  format described in <code>Exception#backtrace</code>.
 *     
 */

static VALUE
exc_set_backtrace(VALUE exc, SEL sel, VALUE bt)
{
    return rb_iv_set(exc, "bt", rb_check_backtrace(bt));
}

/*
 *  call-seq:
 *     exc == obj   => true or false
 *  
 *  Equality---If <i>obj</i> is not an <code>Exception</code>, returns
 *  <code>false</code>. Otherwise, returns <code>true</code> if <i>exc</i> and 
 *  <i>obj</i> share same class, messages, and backtrace.
 */

static VALUE
exc_equal(VALUE exc, SEL sel, VALUE obj)
{
    VALUE mesg, backtrace;
    if (exc == obj) {
	return Qtrue;
    }
    ID id_mesg = rb_intern("mesg");
    if (rb_obj_class(exc) != rb_obj_class(obj)) {
	SEL sel_message = sel_registerName("message");
	SEL sel_backtrace = sel_registerName("backtrace");
	if (!rb_vm_respond_to(obj, sel_message, false)
		|| !rb_vm_respond_to(obj, sel_backtrace, false)) {
	    return Qfalse;
	}
	mesg = rb_vm_call(obj, sel_message, 0, NULL);
	backtrace = rb_vm_call(obj, sel_backtrace, 0, NULL);
    }
    else {
        mesg = rb_attr_get(obj, id_mesg);
        backtrace = exc_backtrace(obj, 0);
    }
    if (!rb_equal(rb_attr_get(exc, id_mesg), mesg)) {
        return Qfalse;
    }
    if (!rb_equal(exc_backtrace(exc, 0), backtrace)) {
        return Qfalse;
    }
    return Qtrue;
}

/*
 * call-seq:
 *   SystemExit.new(status=0)   => system_exit
 *
 * Create a new +SystemExit+ exception with the given status.
 */

static VALUE
exit_initialize(VALUE exc, SEL sel, int argc, VALUE *argv)
{
    VALUE status = INT2FIX(EXIT_SUCCESS);
    if (argc > 0 && FIXNUM_P(argv[0])) {
	status = *argv++;
	--argc;
    }
    if (sel == 0) {
	sel = argc == 0 ? selInitialize : selInitialize2;
    }
    rb_vm_call_super(exc, sel, argc, argv);
    rb_iv_set(exc, "status", status);
    return exc;
}


/*
 * call-seq:
 *   system_exit.status   => fixnum
 *
 * Return the status value associated with this system exit.
 */

static VALUE
exit_status(VALUE exc, SEL sel)
{
    return rb_attr_get(exc, rb_intern("status"));
}


/*
 * call-seq:
 *   system_exit.success?  => true or false
 *
 * Returns +true+ if exiting successful, +false+ if not.
 */

static VALUE
exit_success_p(VALUE exc, SEL sel)
{
    VALUE status = rb_attr_get(exc, rb_intern("status"));
    if (NIL_P(status)) return Qtrue;
    if (status == INT2FIX(EXIT_SUCCESS)) return Qtrue;
    return Qfalse;
}

void
rb_name_error(ID id, const char *fmt, ...)
{
    VALUE exc, argv[2];
    va_list args;
    char buf[BUFSIZ];

    va_start(args, fmt);
    vsnprintf(buf, BUFSIZ, fmt, args);
    va_end(args);

    argv[0] = rb_str_new2(buf);
    argv[1] = ID2SYM(id);
    exc = rb_class_new_instance(2, argv, rb_eNameError);
    rb_exc_raise(exc);
}

/*
 * call-seq:
 *   NameError.new(msg [, name])  => name_error
 *
 * Construct a new NameError exception. If given the <i>name</i>
 * parameter may subsequently be examined using the <code>NameError.name</code>
 * method.
 */

static VALUE
name_err_initialize(VALUE self, SEL sel, int argc, VALUE *argv)
{
    VALUE name;

    name = (argc > 1) ? argv[--argc] : Qnil;
    if (sel == 0) {
	sel = argc == 0 ? selInitialize : selInitialize2;
    }
    rb_vm_call_super(self, sel, argc, argv);
    rb_iv_set(self, "name", name);
    return self;
}

/*
 *  call-seq:
 *    name_error.name    =>  string or nil
 *
 *  Return the name associated with this NameError exception.
 */

static VALUE
name_err_name(VALUE self, SEL sel)
{
    VALUE name = rb_attr_get(self, rb_intern("name"));
    // 1. Ensure that we always return a string, because this might be called
    //    by Objective-C code that expects it to return the exception name.
    // 2. When there is a name, prepend it with an explanation that this is in
    //    fact a NameError. Otherwise Objective-C code that expects the
    //    exception name might result in a name being printed that doesn't make
    //    sense.
    return name == Qnil ? rb_str_new2("NameError") :
	rb_sprintf("NameError for name: %s", RSTRING_PTR(name));
}

/*
 * call-seq:
 *  name_error.to_s   => string
 *
 * Produce a nicely-formatted string representing the +NameError+.
 */

static VALUE
name_err_to_s(VALUE exc, SEL sel)
{
    VALUE mesg = rb_attr_get(exc, rb_intern("mesg"));
    VALUE str = mesg;

    if (NIL_P(mesg)) {
	return rb_class_name(CLASS_OF(exc));
    }
    StringValue(str);
    if (str != mesg) {
	rb_iv_set(exc, "mesg", mesg = str);
    }
    if (OBJ_TAINTED(exc)) OBJ_TAINT(mesg);
    return mesg;
}

/*
 * call-seq:
 *   NoMethodError.new(msg, name [, args])  => no_method_error
 *
 * Construct a NoMethodError exception for a method of the given name
 * called with the given arguments. The name may be accessed using
 * the <code>#name</code> method on the resulting object, and the
 * arguments using the <code>#args</code> method.
 */

static VALUE
nometh_err_initialize(VALUE self, SEL sel, int argc, VALUE *argv)
{
    VALUE args = (argc > 2) ? argv[--argc] : Qnil;
    //name_err_initialize(self, sel, argc, argv);
    if (sel == 0) {
	sel = argc == 0 ? selInitialize : selInitialize2;
    }
    rb_vm_call_super(self, sel, argc, argv);
    rb_iv_set(self, "args", args);
    return self;
}

/* :nodoc: */
static VALUE
name_err_mesg_new(VALUE obj, SEL sel, VALUE mesg, VALUE recv, VALUE method)
{
    VALUE *ptr = ALLOC_N(VALUE, 3);

    GC_WB(&ptr[0], mesg);
    GC_WB(&ptr[1], recv);
    GC_WB(&ptr[2], method);

    return Data_Wrap_Struct(rb_cNameErrorMesg, NULL, NULL, ptr);
}

/* :nodoc: */
static VALUE
name_err_mesg_equal(VALUE obj1, SEL sel, VALUE obj2)
{
    VALUE *ptr1, *ptr2;
    int i;

    if (obj1 == obj2) return Qtrue;
    if (rb_obj_class(obj2) != rb_cNameErrorMesg)
	return Qfalse;

    Data_Get_Struct(obj1, VALUE, ptr1);
    Data_Get_Struct(obj2, VALUE, ptr2);
    for (i=0; i<3; i++) {
	if (!rb_equal(ptr1[i], ptr2[i]))
	    return Qfalse;
    }
    return Qtrue;
}

static VALUE
inspect_exec(VALUE obj, VALUE data, int recur)
{
    if (recur) {
	return Qnil;
    }
    return rb_inspect(obj);
}

static VALUE
safe_inspect(VALUE obj)
{
    return rb_exec_recursive(inspect_exec, obj, 0);
}

/* :nodoc: */
static VALUE
name_err_mesg_to_str(VALUE obj, SEL sel)
{
    VALUE *ptr, mesg;
    Data_Get_Struct(obj, VALUE, ptr);

    mesg = ptr[0];
    if (NIL_P(mesg)) {
	return Qnil;
    }
    else {
	const char *desc = 0;
	VALUE d = 0, args[3];

	obj = ptr[1];
	switch (TYPE(obj)) {
	  case T_NIL:
	    desc = "nil";
	    break;
	  case T_TRUE:
	    desc = "true";
	    break;
	  case T_FALSE:
	    desc = "false";
	    break;
	  default:
	    d = rb_protect(safe_inspect, obj, NULL);
	    if (NIL_P(d) || RSTRING_LEN(d) > 65) {
		d = rb_any_to_s(obj);
	    }
	    desc = RSTRING_PTR(d);
	    break;
	}
	if (desc && desc[0] != '#') {
	    d = rb_str_new2(desc);
	    rb_str_cat2(d, ":");
	    rb_str_cat2(d, rb_obj_classname(obj));
	}
	args[0] = mesg;
	args[1] = ptr[2];
	args[2] = d;
	mesg = rb_f_sprintf(3, args);
    }
    if (OBJ_TAINTED(obj)) {
	OBJ_TAINT(mesg);
    }
    return mesg;
}

/* :nodoc: */
static VALUE
name_err_mesg_load(VALUE klass, SEL sel, VALUE str)
{
    return str;
}

/*
 * call-seq:
 *   no_method_error.args  => obj
 *
 * Return the arguments passed in as the third parameter to
 * the constructor.
 */

static VALUE
nometh_err_args(VALUE self, SEL sel)
{
    return rb_attr_get(self, rb_intern("args"));
}

void
rb_invalid_str(const char *str, const char *type)
{
    VALUE s = rb_str_inspect(rb_str_new2(str));

    rb_raise(rb_eArgError, "invalid value for %s: %s", type, RSTRING_PTR(s));
}

/* 
 *  Document-module: Errno
 *
 *  Ruby exception objects are subclasses of <code>Exception</code>.
 *  However, operating systems typically report errors using plain
 *  integers. Module <code>Errno</code> is created dynamically to map
 *  these operating system errors to Ruby classes, with each error
 *  number generating its own subclass of <code>SystemCallError</code>.
 *  As the subclass is created in module <code>Errno</code>, its name
 *  will start <code>Errno::</code>.
 *     
 *  The names of the <code>Errno::</code> classes depend on
 *  the environment in which Ruby runs. On a typical Unix or Windows
 *  platform, there are <code>Errno</code> classes such as
 *  <code>Errno::EACCES</code>, <code>Errno::EAGAIN</code>,
 *  <code>Errno::EINTR</code>, and so on.
 *     
 *  The integer operating system error number corresponding to a
 *  particular error is available as the class constant
 *  <code>Errno::</code><em>error</em><code>::Errno</code>.
 *     
 *     Errno::EACCES::Errno   #=> 13
 *     Errno::EAGAIN::Errno   #=> 11
 *     Errno::EINTR::Errno    #=> 4
 *     
 *  The full list of operating system errors on your particular platform
 *  are available as the constants of <code>Errno</code>.
 *
 *     Errno.constants   #=> :E2BIG, :EACCES, :EADDRINUSE, :EADDRNOTAVAIL, ...
 */

static st_table *syserr_tbl;

static VALUE
set_syserr(int n, const char *name)
{
    VALUE error;

    if (!st_lookup(syserr_tbl, n, &error)) {
	error = rb_define_class_under(rb_mErrno, name, rb_eSystemCallError);
	rb_define_const(error, "Errno", INT2NUM(n));
	st_add_direct(syserr_tbl, n, error);
    }
    else {
	rb_define_const(rb_mErrno, name, error);
    }
    return error;
}

static VALUE
get_syserr(int n)
{
    VALUE error;

    if (!st_lookup(syserr_tbl, n, &error)) {
	char name[8];	/* some Windows' errno have 5 digits. */

	snprintf(name, sizeof(name), "E%03d", n);
	error = set_syserr(n, name);
    }
    return error;
}

/*
 * call-seq:
 *   SystemCallError.new(msg, errno)  => system_call_error_subclass
 *
 * If _errno_ corresponds to a known system error code, constructs
 * the appropriate <code>Errno</code> class for that error, otherwise
 * constructs a generic <code>SystemCallError</code> object. The
 * error number is subsequently available via the <code>errno</code>
 * method.
 */

static VALUE
syserr_initialize(VALUE self, SEL sel, int argc, VALUE *argv)
{
#if !defined(_WIN32) && !defined(__VMS)
    char *strerror();
#endif
    const char *err;
    VALUE mesg, error;
    VALUE klass = rb_obj_class(self);

    if (klass == rb_eSystemCallError) {
	rb_scan_args(argc, argv, "11", &mesg, &error);
	if (argc == 1 && FIXNUM_P(mesg)) {
	    error = mesg; mesg = Qnil;
	}
	if (!NIL_P(error) && st_lookup(syserr_tbl, NUM2LONG(error), &klass)) {
	    RBASIC(self)->klass = klass;
	}
    }
    else {
	rb_scan_args(argc, argv, "01", &mesg);
	error = rb_const_get(klass, rb_intern("Errno"));
    }
    if (!NIL_P(error)) err = strerror(NUM2LONG(error));
    else err = "unknown error";
    if (!NIL_P(mesg)) {
	VALUE str = mesg;

	StringValue(str);
	mesg = rb_sprintf("%s - %.*s", err,
			  (int)RSTRING_LEN(str), RSTRING_PTR(str));
    }
    else {
	mesg = rb_str_new2(err);
    }
    rb_vm_call_super(self, selInitialize2, 1, &mesg);
    rb_iv_set(self, "errno", error);
    return self;
}

/*
 * call-seq:
 *   system_call_error.errno   => fixnum
 *
 * Return this SystemCallError's error number.
 */

static VALUE
syserr_errno(VALUE self, SEL sel)
{
    return rb_attr_get(self, rb_intern("errno"));
}

/*
 * call-seq:
 *   system_call_error === other  => true or false
 *
 * Return +true+ if the receiver is a generic +SystemCallError+, or
 * if the error numbers _self_ and _other_ are the same.
 */

static VALUE
syserr_eqq(VALUE self, SEL sel, VALUE exc)
{
    VALUE num, e;
    ID en = rb_intern("errno");

    if (!rb_obj_is_kind_of(exc, rb_eSystemCallError)) {
	if (!rb_respond_to(exc, en)) return Qfalse;
    }
    else if (self == rb_eSystemCallError) return Qtrue;

    num = rb_attr_get(exc, rb_intern("errno"));
    if (NIL_P(num)) {
	num = rb_funcall(exc, en, 0, 0);
    }
    e = rb_const_get(self, rb_intern("Errno"));
    if (FIXNUM_P(num) ? num == e : rb_equal(num, e))
	return Qtrue;
    return Qfalse;
}

/*
 * call-seq:
 *   Errno.const_missing   => SystemCallError
 *
 * Returns default SystemCallError class.
 */
static VALUE
errno_missing(VALUE self, SEL sel, VALUE id)
{
    return rb_eNOERROR;
}

/*
 * call-seq:
 *   Errno.code   => Fixnum
 *
 * Returns the current errno value.
 */
static VALUE
errno_code(VALUE self, SEL sel)
{
    return INT2FIX(errno);
}

/*
 *  Descendants of class <code>Exception</code> are used to communicate
 *  between <code>raise</code> methods and <code>rescue</code>
 *  statements in <code>begin/end</code> blocks. <code>Exception</code>
 *  objects carry information about the exception---its type (the
 *  exception's class name), an optional descriptive string, and
 *  optional traceback information. Programs may subclass 
 *  <code>Exception</code> to add additional information.
 */

void
Init_Exception(void)
{
    rb_eException = (VALUE)objc_getClass("NSException");
    rb_const_set(rb_cObject, rb_intern("Exception"), rb_eException);

    rb_objc_define_method(*(VALUE *)rb_eException, "new", rb_class_new_instance_imp, -1);
    rb_objc_define_method(*(VALUE *)rb_eException, "exception", rb_class_new_instance_imp, -1);
    rb_objc_define_method(rb_eException, "exception", exc_exception, -1);
    rb_objc_define_method(rb_eException, "initialize", exc_initialize, -1);
    rb_objc_define_method(rb_eException, "==", exc_equal, 1);
    rb_objc_define_method(rb_eException, "to_s", exc_to_s, 0);
    rb_objc_define_method(rb_eException, "message", exc_message, 0);
    rb_objc_define_method(rb_eException, "inspect", exc_inspect, 0);
    rb_objc_define_method(rb_eException, "backtrace", exc_backtrace, 0);
    rb_objc_define_method(rb_eException, "set_backtrace", exc_set_backtrace, 1);

    rb_eSystemExit  = rb_define_class("SystemExit", rb_eException);
    rb_objc_define_method(rb_eSystemExit, "initialize", exit_initialize, -1);
    rb_objc_define_method(rb_eSystemExit, "status", exit_status, 0);
    rb_objc_define_method(rb_eSystemExit, "success?", exit_success_p, 0);

    rb_eFatal  	    = rb_define_class("fatal", rb_eException);
    rb_eSignal      = rb_define_class("SignalException", rb_eException);
    rb_eInterrupt   = rb_define_class("Interrupt", rb_eSignal);

    rb_eStandardError = rb_define_class("StandardError", rb_eException);
    rb_eTypeError     = rb_define_class("TypeError", rb_eStandardError);
    rb_eArgError      = rb_define_class("ArgumentError", rb_eStandardError);
    rb_eIndexError    = rb_define_class("IndexError", rb_eStandardError);
    rb_eKeyError      = rb_define_class("KeyError", rb_eIndexError);
    rb_eRangeError    = rb_define_class("RangeError", rb_eStandardError);

    rb_eScriptError = rb_define_class("ScriptError", rb_eException);
    rb_eSyntaxError = rb_define_class("SyntaxError", rb_eScriptError);
    rb_eLoadError   = rb_define_class("LoadError", rb_eScriptError);
    rb_eNotImpError = rb_define_class("NotImplementedError", rb_eScriptError);

    rb_eNameError     = rb_define_class("NameError", rb_eStandardError);
    rb_objc_define_method(rb_eNameError, "initialize", name_err_initialize, -1);
    rb_objc_define_method(rb_eNameError, "name", name_err_name, 0);
    rb_objc_define_method(rb_eNameError, "to_s", name_err_to_s, 0);
    rb_cNameErrorMesg = rb_define_class_under(rb_eNameError, "message", rb_cData);
    rb_objc_define_method(*(VALUE *)rb_cNameErrorMesg, "!", name_err_mesg_new, 3);
    rb_objc_define_method(rb_cNameErrorMesg, "==", name_err_mesg_equal, 1);
    rb_objc_define_method(rb_cNameErrorMesg, "to_str", name_err_mesg_to_str, 0);
    rb_objc_define_method(rb_cNameErrorMesg, "_dump", name_err_mesg_to_str, 0);
    rb_objc_define_method(*(VALUE *)rb_cNameErrorMesg, "_load", name_err_mesg_load, 1);
    rb_eNoMethodError = rb_define_class("NoMethodError", rb_eNameError);
    rb_objc_define_method(rb_eNoMethodError, "initialize", nometh_err_initialize, -1);
    rb_objc_define_method(rb_eNoMethodError, "args", nometh_err_args, 0);

    rb_eRuntimeError = rb_define_class("RuntimeError", rb_eStandardError);
    rb_eSecurityError = rb_define_class("SecurityError", rb_eException);
    rb_eNoMemError = rb_define_class("NoMemoryError", rb_eException);
    rb_eEncodingError = rb_define_class("EncodingError", rb_eStandardError);
    rb_eEncCompatError = rb_define_class_under(rb_cEncoding, "CompatibilityError", rb_eEncodingError);
    rb_eUndefinedConversionError = rb_define_class_under(rb_cEncoding, "UndefinedConversionError", rb_eEncodingError);
    rb_eInvalidByteSequenceError = rb_define_class_under(rb_cEncoding, "InvalidByteSequenceError", rb_eEncodingError);
    rb_eConverterNotFoundError = rb_define_class_under(rb_cEncoding, "ConverterNotFoundError", rb_eEncodingError);

    syserr_tbl = st_init_numtable();
    GC_RETAIN(syserr_tbl);
    rb_eSystemCallError = rb_define_class("SystemCallError", rb_eStandardError);
    rb_objc_define_method(rb_eSystemCallError, "initialize", syserr_initialize, -1);
    rb_objc_define_method(rb_eSystemCallError, "errno", syserr_errno, 0);
    rb_objc_define_method(*(VALUE *)rb_eSystemCallError, "===", syserr_eqq, 1);

    rb_mErrno = rb_define_module("Errno");
    rb_objc_define_method(*(VALUE *)rb_mErrno, "const_missing", errno_missing, 1);
    rb_objc_define_method(*(VALUE *)rb_mErrno, "code", errno_code, 0);

    rb_objc_define_module_function(rb_mKernel, "warn", rb_warn_m, 1);
}

void
rb_raise(VALUE exc, const char *fmt, ...)
{
    va_list args;
    char buf[BUFSIZ];

    va_start(args,fmt);
    vsnprintf(buf, BUFSIZ, fmt, args);
    va_end(args);
    rb_exc_raise(rb_exc_new2(exc, buf));
}

void
rb_loaderror(const char *fmt, ...)
{
    va_list args;
    char buf[BUFSIZ];

    va_start(args, fmt);
    vsnprintf(buf, BUFSIZ, fmt, args);
    va_end(args);
    rb_exc_raise(rb_exc_new2(rb_eLoadError, buf));
}

void
rb_notimplement(void)
{
    rb_raise(rb_eNotImpError,
	     "%s() function is unimplemented on this machine",
	     "TODO");//rb_id2name(rb_frame_this_func()));
}

VALUE
rb_f_notimplement(VALUE rcv, SEL sel)
{
    rb_raise(rb_eNotImpError,
	    "%s() function is unimplemented on this machine",
	    sel_getName(sel));
    return Qnil; // never reached
}

void
rb_fatal(const char *fmt, ...)
{
    va_list args;
    char buf[BUFSIZ];

    va_start(args, fmt);
    vsnprintf(buf, BUFSIZ, fmt, args);
    va_end(args);

    rb_exc_fatal(rb_exc_new2(rb_eFatal, buf));
}

static VALUE
make_errno_exc(const char *mesg)
{
    int n = errno;
    VALUE arg;

    errno = 0;
    if (n == 0) {
	rb_bug("rb_sys_fail(%s) - errno == 0", mesg ? mesg : "");
    }

    arg = mesg ? rb_str_new2(mesg) : Qnil;
    return rb_class_new_instance(1, &arg, get_syserr(n));
}

void
rb_sys_fail(const char *mesg)
{
    rb_exc_raise(make_errno_exc(mesg));
}

void
rb_mod_sys_fail(VALUE mod, const char *mesg)
{
    VALUE exc = make_errno_exc(mesg);
    rb_extend_object(exc, mod);
    rb_exc_raise(exc);
}

void
rb_sys_warning(const char *fmt, ...)
{
    char buf[BUFSIZ];
    va_list args;
    int errno_save;

    errno_save = errno;

    if (!RTEST(ruby_verbose)) return;

    snprintf(buf, BUFSIZ, "warning: %s", fmt);
    snprintf(buf+strlen(buf), BUFSIZ-strlen(buf), ": %s", strerror(errno_save));

    va_start(args, fmt);
    warn_print(buf, args);
    va_end(args);
    errno = errno_save;
}

void
rb_load_fail(const char *path)
{
    rb_loaderror("%s -- %s", strerror(errno), path);
}

void
rb_error_frozen(const char *what)
{
    rb_raise(rb_eRuntimeError, "can't modify frozen %s", what);
}

void
rb_check_frozen(VALUE obj)
{
    if (OBJ_FROZEN(obj)) rb_error_frozen(rb_obj_classname(obj));
}

static VALUE
format_message(VALUE exc)
{
    CFMutableStringRef result = CFStringCreateMutable(NULL, 0);
    VALUE message = rb_vm_call(exc, sel_registerName("message"), 0, NULL);
    VALUE bt = rb_vm_call(exc, sel_registerName("backtrace"), 0, NULL);

    message = rb_check_string_type(message);
    const char *msg = message == Qnil ? "" : RSTRING_PTR(message);

    const long count = (bt != Qnil ? RARRAY_LEN(bt) : 0);
    if (count > 0) {
	for (long i = 0; i < count; i++) {
	    const char *bte = RSTRING_PTR(RARRAY_AT(bt, i));
	    if (i == 0) {
		CFStringAppendFormat(result, NULL, CFSTR("%s: %s (%s)\n"),
		    bte, msg, rb_class2name(*(VALUE *)exc));
	    }
	    else {
		CFStringAppendFormat(result, NULL, CFSTR("\tfrom %s\n"), bte);
	    }
	}
    }
    else {
	CFStringAppendFormat(result, NULL, CFSTR("%s (%s)\n"),
	    msg, rb_class2name(*(VALUE *)exc));
    }
    CFMakeCollectable(result);
    return (VALUE)result;
}

static VALUE
restore_level(VALUE lvl)
{
    rb_set_safe_level_force((int)lvl);
    return Qnil;
}

VALUE
rb_format_exception_message(VALUE exc)
{
    const int old_level = rb_safe_level();
    rb_set_safe_level_force(0);

    return rb_ensure(format_message, exc, restore_level, (VALUE)old_level);
}

void
Init_syserr(void)
{
#ifdef EPERM
    set_syserr(EPERM, "EPERM");
#endif
#ifdef ENOENT
    set_syserr(ENOENT, "ENOENT");
#endif
#ifdef ESRCH
    set_syserr(ESRCH, "ESRCH");
#endif
#ifdef EINTR
    set_syserr(EINTR, "EINTR");
#endif
#ifdef EIO
    set_syserr(EIO, "EIO");
#endif
#ifdef ENXIO
    set_syserr(ENXIO, "ENXIO");
#endif
#ifdef E2BIG
    set_syserr(E2BIG, "E2BIG");
#endif
#ifdef ENOEXEC
    set_syserr(ENOEXEC, "ENOEXEC");
#endif
#ifdef EBADF
    set_syserr(EBADF, "EBADF");
#endif
#ifdef ECHILD
    set_syserr(ECHILD, "ECHILD");
#endif
#ifdef EAGAIN
    set_syserr(EAGAIN, "EAGAIN");
#endif
#ifdef ENOMEM
    set_syserr(ENOMEM, "ENOMEM");
#endif
#ifdef EACCES
    set_syserr(EACCES, "EACCES");
#endif
#ifdef EFAULT
    set_syserr(EFAULT, "EFAULT");
#endif
#ifdef ENOTBLK
    set_syserr(ENOTBLK, "ENOTBLK");
#endif
#ifdef EBUSY
    set_syserr(EBUSY, "EBUSY");
#endif
#ifdef EEXIST
    set_syserr(EEXIST, "EEXIST");
#endif
#ifdef EXDEV
    set_syserr(EXDEV, "EXDEV");
#endif
#ifdef ENODEV
    set_syserr(ENODEV, "ENODEV");
#endif
#ifdef ENOTDIR
    set_syserr(ENOTDIR, "ENOTDIR");
#endif
#ifdef EISDIR
    set_syserr(EISDIR, "EISDIR");
#endif
#ifdef EINVAL
    set_syserr(EINVAL, "EINVAL");
#endif
#ifdef ENFILE
    set_syserr(ENFILE, "ENFILE");
#endif
#ifdef EMFILE
    set_syserr(EMFILE, "EMFILE");
#endif
#ifdef ENOTTY
    set_syserr(ENOTTY, "ENOTTY");
#endif
#ifdef ETXTBSY
    set_syserr(ETXTBSY, "ETXTBSY");
#endif
#ifdef EFBIG
    set_syserr(EFBIG, "EFBIG");
#endif
#ifdef ENOSPC
    set_syserr(ENOSPC, "ENOSPC");
#endif
#ifdef ESPIPE
    set_syserr(ESPIPE, "ESPIPE");
#endif
#ifdef EROFS
    set_syserr(EROFS, "EROFS");
#endif
#ifdef EMLINK
    set_syserr(EMLINK, "EMLINK");
#endif
#ifdef EPIPE
    set_syserr(EPIPE, "EPIPE");
#endif
#ifdef EDOM
    set_syserr(EDOM, "EDOM");
#endif
#ifdef ERANGE
    set_syserr(ERANGE, "ERANGE");
#endif
#ifdef EDEADLK
    set_syserr(EDEADLK, "EDEADLK");
#endif
#ifdef ENAMETOOLONG
    set_syserr(ENAMETOOLONG, "ENAMETOOLONG");
#endif
#ifdef ENOLCK
    set_syserr(ENOLCK, "ENOLCK");
#endif
#ifdef ENOSYS
    set_syserr(ENOSYS, "ENOSYS");
#endif
#ifdef ENOTEMPTY
    set_syserr(ENOTEMPTY, "ENOTEMPTY");
#endif
#ifdef ELOOP
    set_syserr(ELOOP, "ELOOP");
#endif
#ifdef EWOULDBLOCK
    set_syserr(EWOULDBLOCK, "EWOULDBLOCK");
#endif
#ifdef ENOMSG
    set_syserr(ENOMSG, "ENOMSG");
#endif
#ifdef EIDRM
    set_syserr(EIDRM, "EIDRM");
#endif
#ifdef ECHRNG
    set_syserr(ECHRNG, "ECHRNG");
#endif
#ifdef EL2NSYNC
    set_syserr(EL2NSYNC, "EL2NSYNC");
#endif
#ifdef EL3HLT
    set_syserr(EL3HLT, "EL3HLT");
#endif
#ifdef EL3RST
    set_syserr(EL3RST, "EL3RST");
#endif
#ifdef ELNRNG
    set_syserr(ELNRNG, "ELNRNG");
#endif
#ifdef EUNATCH
    set_syserr(EUNATCH, "EUNATCH");
#endif
#ifdef ENOCSI
    set_syserr(ENOCSI, "ENOCSI");
#endif
#ifdef EL2HLT
    set_syserr(EL2HLT, "EL2HLT");
#endif
#ifdef EBADE
    set_syserr(EBADE, "EBADE");
#endif
#ifdef EBADR
    set_syserr(EBADR, "EBADR");
#endif
#ifdef EXFULL
    set_syserr(EXFULL, "EXFULL");
#endif
#ifdef ENOANO
    set_syserr(ENOANO, "ENOANO");
#endif
#ifdef EBADRQC
    set_syserr(EBADRQC, "EBADRQC");
#endif
#ifdef EBADSLT
    set_syserr(EBADSLT, "EBADSLT");
#endif
#ifdef EDEADLOCK
    set_syserr(EDEADLOCK, "EDEADLOCK");
#endif
#ifdef EBFONT
    set_syserr(EBFONT, "EBFONT");
#endif
#ifdef ENOSTR
    set_syserr(ENOSTR, "ENOSTR");
#endif
#ifdef ENODATA
    set_syserr(ENODATA, "ENODATA");
#endif
#ifdef ETIME
    set_syserr(ETIME, "ETIME");
#endif
#ifdef ENOSR
    set_syserr(ENOSR, "ENOSR");
#endif
#ifdef ENONET
    set_syserr(ENONET, "ENONET");
#endif
#ifdef ENOPKG
    set_syserr(ENOPKG, "ENOPKG");
#endif
#ifdef EREMOTE
    set_syserr(EREMOTE, "EREMOTE");
#endif
#ifdef ENOLINK
    set_syserr(ENOLINK, "ENOLINK");
#endif
#ifdef EADV
    set_syserr(EADV, "EADV");
#endif
#ifdef ESRMNT
    set_syserr(ESRMNT, "ESRMNT");
#endif
#ifdef ECOMM
    set_syserr(ECOMM, "ECOMM");
#endif
#ifdef EPROTO
    set_syserr(EPROTO, "EPROTO");
#endif
#ifdef EMULTIHOP
    set_syserr(EMULTIHOP, "EMULTIHOP");
#endif
#ifdef EDOTDOT
    set_syserr(EDOTDOT, "EDOTDOT");
#endif
#ifdef EBADMSG
    set_syserr(EBADMSG, "EBADMSG");
#endif
#ifdef EOVERFLOW
    set_syserr(EOVERFLOW, "EOVERFLOW");
#endif
#ifdef ENOTUNIQ
    set_syserr(ENOTUNIQ, "ENOTUNIQ");
#endif
#ifdef EBADFD
    set_syserr(EBADFD, "EBADFD");
#endif
#ifdef EREMCHG
    set_syserr(EREMCHG, "EREMCHG");
#endif
#ifdef ELIBACC
    set_syserr(ELIBACC, "ELIBACC");
#endif
#ifdef ELIBBAD
    set_syserr(ELIBBAD, "ELIBBAD");
#endif
#ifdef ELIBSCN
    set_syserr(ELIBSCN, "ELIBSCN");
#endif
#ifdef ELIBMAX
    set_syserr(ELIBMAX, "ELIBMAX");
#endif
#ifdef ELIBEXEC
    set_syserr(ELIBEXEC, "ELIBEXEC");
#endif
#ifdef EILSEQ
    set_syserr(EILSEQ, "EILSEQ");
#endif
#ifdef ERESTART
    set_syserr(ERESTART, "ERESTART");
#endif
#ifdef ESTRPIPE
    set_syserr(ESTRPIPE, "ESTRPIPE");
#endif
#ifdef EUSERS
    set_syserr(EUSERS, "EUSERS");
#endif
#ifdef ENOTSOCK
    set_syserr(ENOTSOCK, "ENOTSOCK");
#endif
#ifdef EDESTADDRREQ
    set_syserr(EDESTADDRREQ, "EDESTADDRREQ");
#endif
#ifdef EMSGSIZE
    set_syserr(EMSGSIZE, "EMSGSIZE");
#endif
#ifdef EPROTOTYPE
    set_syserr(EPROTOTYPE, "EPROTOTYPE");
#endif
#ifdef ENOPROTOOPT
    set_syserr(ENOPROTOOPT, "ENOPROTOOPT");
#endif
#ifdef EPROTONOSUPPORT
    set_syserr(EPROTONOSUPPORT, "EPROTONOSUPPORT");
#endif
#ifdef ESOCKTNOSUPPORT
    set_syserr(ESOCKTNOSUPPORT, "ESOCKTNOSUPPORT");
#endif
#ifdef EOPNOTSUPP
    set_syserr(EOPNOTSUPP, "EOPNOTSUPP");
#endif
#ifdef EPFNOSUPPORT
    set_syserr(EPFNOSUPPORT, "EPFNOSUPPORT");
#endif
#ifdef EAFNOSUPPORT
    set_syserr(EAFNOSUPPORT, "EAFNOSUPPORT");
#endif
#ifdef EADDRINUSE
    set_syserr(EADDRINUSE, "EADDRINUSE");
#endif
#ifdef EADDRNOTAVAIL
    set_syserr(EADDRNOTAVAIL, "EADDRNOTAVAIL");
#endif
#ifdef ENETDOWN
    set_syserr(ENETDOWN, "ENETDOWN");
#endif
#ifdef ENETUNREACH
    set_syserr(ENETUNREACH, "ENETUNREACH");
#endif
#ifdef ENETRESET
    set_syserr(ENETRESET, "ENETRESET");
#endif
#ifdef ECONNABORTED
    set_syserr(ECONNABORTED, "ECONNABORTED");
#endif
#ifdef ECONNRESET
    set_syserr(ECONNRESET, "ECONNRESET");
#endif
#ifdef ENOBUFS
    set_syserr(ENOBUFS, "ENOBUFS");
#endif
#ifdef EISCONN
    set_syserr(EISCONN, "EISCONN");
#endif
#ifdef ENOTCONN
    set_syserr(ENOTCONN, "ENOTCONN");
#endif
#ifdef ESHUTDOWN
    set_syserr(ESHUTDOWN, "ESHUTDOWN");
#endif
#ifdef ETOOMANYREFS
    set_syserr(ETOOMANYREFS, "ETOOMANYREFS");
#endif
#ifdef ETIMEDOUT
    set_syserr(ETIMEDOUT, "ETIMEDOUT");
#endif
#ifdef ECONNREFUSED
    set_syserr(ECONNREFUSED, "ECONNREFUSED");
#endif
#ifdef EHOSTDOWN
    set_syserr(EHOSTDOWN, "EHOSTDOWN");
#endif
#ifdef EHOSTUNREACH
    set_syserr(EHOSTUNREACH, "EHOSTUNREACH");
#endif
#ifdef EALREADY
    set_syserr(EALREADY, "EALREADY");
#endif
#ifdef EINPROGRESS
    set_syserr(EINPROGRESS, "EINPROGRESS");
#endif
#ifdef ESTALE
    set_syserr(ESTALE, "ESTALE");
#endif
#ifdef EUCLEAN
    set_syserr(EUCLEAN, "EUCLEAN");
#endif
#ifdef ENOTNAM
    set_syserr(ENOTNAM, "ENOTNAM");
#endif
#ifdef ENAVAIL
    set_syserr(ENAVAIL, "ENAVAIL");
#endif
#ifdef EISNAM
    set_syserr(EISNAM, "EISNAM");
#endif
#ifdef EREMOTEIO
    set_syserr(EREMOTEIO, "EREMOTEIO");
#endif
#ifdef EDQUOT
    set_syserr(EDQUOT, "EDQUOT");
#endif
    rb_eNOERROR = set_syserr(0, "NOERROR");
}

static void
err_append(const char *s)
{

    if (rb_vm_parse_in_eval()) {
	VALUE err = rb_errinfo();
	if (err == Qnil) {
	    err = rb_exc_new2(rb_eSyntaxError, s);
	    rb_set_errinfo(err);
	}
	else {
	    VALUE str = rb_obj_as_string(err);

	    rb_str_cat2(str, "\n");
	    rb_str_cat2(str, s);
	    rb_set_errinfo(rb_exc_new3(rb_eSyntaxError, str));
	}
    }
    else {
	VALUE err = rb_vm_current_exception();
	if (err == Qnil) {
	    err = rb_exc_new2(rb_eSyntaxError, "compile error");
	    rb_vm_set_current_exception(err);
	}
	rb_write_error(s);
	rb_write_error("\n");
    }
}
