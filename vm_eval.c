/**********************************************************************

  vm_eval.c -

  $Author: nobu $
  created at: Sat May 24 16:02:32 JST 2008

  Copyright (C) 1993-2007 Yukihiro Matsumoto
  Copyright (C) 2000  Network Applied Communication Laboratory, Inc.
  Copyright (C) 2000  Information-technology Promotion Agency, Japan

**********************************************************************/

#include "ruby/ruby.h"
#include "ruby/node.h"
#include "ruby/st.h"
#include "roxor.h"
#include "objc.h"
#include "id.h"

#include "vm_method.c"

static inline VALUE
rb_call(VALUE recv, ID mid, int argc, const VALUE *argv, int scope)
{
    SEL sel;
    if (mid == ID_ALLOCATOR) {
	sel = selAlloc;
    }
    else {
	const char *midstr = rb_id2name(mid);
	if (argc > 0 && midstr[strlen(midstr) - 1] != ':') {
	    char buf[100];
	    snprintf(buf, sizeof buf, "%s:", midstr);
	    sel = sel_registerName(buf);
	}
	else {
	    sel = sel_registerName(midstr);
	}
    }
    return rb_vm_call(recv, sel, argc, argv, false);
}

/*
 *  call-seq:
 *     obj.method_missing(symbol [, *args] )   => result
 *
 *  Invoked by Ruby when <i>obj</i> is sent a message it cannot handle.
 *  <i>symbol</i> is the symbol for the method called, and <i>args</i>
 *  are any arguments that were passed to it. By default, the interpreter
 *  raises an error when this method is called. However, it is possible
 *  to override the method to provide more dynamic behavior.
 *  If it is decided that a particular method should not be handled, then
 *  <i>super</i> should be called, so that ancestors can pick up the
 *  missing method.
 *  The example below creates
 *  a class <code>Roman</code>, which responds to methods with names
 *  consisting of roman numerals, returning the corresponding integer
 *  values.
 *
 *     class Roman
 *       def romanToInt(str)
 *         # ...
 *       end
 *       def method_missing(methId)
 *         str = methId.id2name
 *         romanToInt(str)
 *       end
 *     end
 *
 *     r = Roman.new
 *     r.iv      #=> 4
 *     r.xxiii   #=> 23
 *     r.mm      #=> 2000
 */

static VALUE
rb_method_missing(VALUE obj, SEL sel, int argc, const VALUE *argv)
{
    return rb_vm_method_missing(obj, argc, argv);
}

VALUE
rb_apply(VALUE recv, ID mid, VALUE args)
{
    int argc;
    VALUE *argv;

    argc = RARRAY_LEN(args);	/* Assigns LONG, but argc is INT */
    argv = ALLOCA_N(VALUE, argc);
    MEMCPY(argv, RARRAY_PTR(args), VALUE, argc);
    return rb_call(/*CLASS_OF(recv),*/ recv, mid, argc, argv, CALL_FCALL);
}

VALUE
rb_funcall(VALUE recv, ID mid, int n, ...)
{
    VALUE *argv;
    va_list ar;
    va_start(ar, n);

    if (n > 0) {
	long i;

	argv = ALLOCA_N(VALUE, n);

	for (i = 0; i < n; i++) {
	    argv[i] = va_arg(ar, VALUE);
	}
	va_end(ar);
    }
    else {
	argv = 0;
    }
    return rb_call(recv, mid, n, argv, CALL_FCALL);
}

VALUE
rb_funcall2(VALUE recv, ID mid, int argc, const VALUE *argv)
{
    return rb_call(recv, mid, argc, argv, CALL_FCALL);
}

VALUE
rb_funcall3(VALUE recv, ID mid, int argc, const VALUE *argv)
{
    return rb_call(recv, mid, argc, argv, CALL_PUBLIC);
}

static VALUE
send_internal(int argc, VALUE *argv, VALUE recv, int scope)
{
    VALUE vid;

    if (argc == 0) {
	rb_raise(rb_eArgError, "no method name given");
    }

    vid = *argv++; argc--;
    return rb_call(recv, rb_to_id(vid), argc, argv, scope);
}

/*
 *  call-seq:
 *     obj.send(symbol [, args...])        => obj
 *     obj.__send__(symbol [, args...])      => obj
 *
 *  Invokes the method identified by _symbol_, passing it any
 *  arguments specified. You can use <code>__send__</code> if the name
 *  +send+ clashes with an existing method in _obj_.
 *
 *     class Klass
 *       def hello(*args)
 *         "Hello " + args.join(' ')
 *       end
 *     end
 *     k = Klass.new
 *     k.send :hello, "gentle", "readers"   #=> "Hello gentle readers"
 */

static VALUE
rb_f_send(VALUE recv, SEL sel, int argc, VALUE *argv)
{
    return send_internal(argc, argv, recv, NOEX_NOSUPER | NOEX_PRIVATE);
}

/*
 *  call-seq:
 *     obj.public_send(symbol [, args...])  => obj
 *
 *  Invokes the method identified by _symbol_, passing it any
 *  arguments specified. Unlike send, public_send calls public
 *  methods only.
 *
 *     1.public_send(:puts, "hello")  # causes NoMethodError
 */

static VALUE
rb_f_public_send(VALUE recv, SEL sel, int argc, VALUE *argv)
{
    return send_internal(argc, argv, recv, NOEX_PUBLIC);
}

/* yield */

static inline VALUE
rb_yield_0(int argc, const VALUE * argv)
{
    return rb_vm_yield(argc, argv);
}

VALUE
rb_yield(VALUE val)
{
    if (val == Qundef) {
	return rb_yield_0(0, 0);
    }
    else {
	return rb_yield_0(1, &val);
    }
}

VALUE
rb_yield_values(int n, ...)
{
    if (n == 0) {
	return rb_yield_0(0, 0);
    }
    else {
	int i;
	VALUE *argv;
	va_list args;
	argv = ALLOCA_N(VALUE, n);

	va_start(args, n);
	for (i=0; i<n; i++) {
	    argv[i] = va_arg(args, VALUE);
	}
	va_end(args);

	return rb_yield_0(n, argv);
    }
}

VALUE
rb_yield_values2(int argc, const VALUE *argv)
{
    return rb_yield_0(argc, argv);
}

VALUE
rb_yield_splat(VALUE values)
{
    VALUE tmp = rb_check_array_type(values);
    volatile VALUE v;
    if (NIL_P(tmp)) {
        rb_raise(rb_eArgError, "not an array");
    }
    v = rb_yield_0(RARRAY_LEN(tmp), RARRAY_PTR(tmp));
    return v;
}

static VALUE
loop_i(void)
{
    for (;;) {
	rb_yield(Qundef);
	RETURN_IF_BROKEN();
    }
    return Qnil;
}

/*
 *  call-seq:
 *     loop {|| block }
 *
 *  Repeatedly executes the block.
 *
 *     loop do
 *       print "Input: "
 *       line = gets
 *       break if !line or line =~ /^qQ/
 *       # ...
 *     end
 *
 *  StopIteration raised in the block breaks the loop.
 */

static VALUE
rb_f_loop(VALUE klass, SEL sel)
{
    rb_rescue2(loop_i, (VALUE)0, 0, 0, rb_eStopIteration, (VALUE)0);
    return Qnil;		/* dummy */
}

VALUE
rb_objc_block_call(VALUE obj, SEL sel, void *cache, int argc, VALUE *argv, 
		   VALUE (*bl_proc) (ANYARGS), VALUE data2)
{
    NODE *node = NEW_IFUNC(bl_proc, data2);
    rb_vm_block_t *b = rb_vm_prepare_block(NULL, node, obj, 0);
    rb_vm_change_current_block(b);
    if (cache == NULL) {
	cache = rb_vm_get_call_cache(sel);
    }
    VALUE val =  rb_vm_call_with_cache2(cache, obj, 0, sel, argc, argv);
    rb_vm_restore_current_block();
    return val;
}

VALUE
rb_block_call(VALUE obj, ID mid, int argc, VALUE *argv,
	      VALUE (*bl_proc) (ANYARGS), VALUE data2)
{
    SEL sel;
    if (argc == 0) {
	sel = sel_registerName(rb_id2name(mid));
    }
    else {
	char buf[100];
	snprintf(buf, sizeof buf, "%s:", rb_id2name(mid));
	sel = sel_registerName(buf);
    }
    return rb_objc_block_call(obj, sel, NULL, argc, argv, bl_proc, data2);
}

VALUE
rb_each(VALUE obj)
{
    return rb_call(obj, idEach, 0, 0, CALL_FCALL);
}

static VALUE
eval_string_with_cref(VALUE self, VALUE src, VALUE scope, NODE *cref, const char *file, int line)
{
    // TODO honor scope
    NODE *node = rb_compile_string(file, src, line);
    if (node == NULL) {
	rb_raise(rb_eSyntaxError, "compile error");
    }
    return rb_vm_run_node(file, node);
}

static VALUE
eval_string(VALUE self, VALUE src, VALUE scope, const char *file, int line)
{
    return eval_string_with_cref(self, src, scope, 0, file, line);
}

static VALUE
specific_eval(int argc, VALUE *argv, VALUE klass, VALUE self)
{
    if (rb_block_given_p()) {
        if (argc > 0) {
            rb_raise(rb_eArgError, "wrong number of arguments (%d for 0)", argc);
        }
        return rb_vm_yield_under(klass, self, 0, NULL);
    }
    else {
	// TODO
	abort();
    }
}

/*
 *  call-seq:
 *     eval(string [, binding [, filename [,lineno]]])  => obj
 *
 *  Evaluates the Ruby expression(s) in <em>string</em>. If
 *  <em>binding</em> is given, the evaluation is performed in its
 *  context. The binding may be a <code>Binding</code> object or a
 *  <code>Proc</code> object. If the optional <em>filename</em> and
 *  <em>lineno</em> parameters are present, they will be used when
 *  reporting syntax errors.
 *
 *     def getBinding(str)
 *       return binding
 *     end
 *     str = "hello"
 *     eval "str + ' Fred'"                      #=> "hello Fred"
 *     eval "str + ' Fred'", getBinding("bye")   #=> "bye Fred"
 */

VALUE
rb_f_eval(VALUE self, SEL sel, int argc, VALUE *argv)
{
    VALUE src, scope, vfile, vline;
    const char *file = "(eval)";
    int line = 1;

    rb_scan_args(argc, argv, "13", &src, &scope, &vfile, &vline);
    if (rb_safe_level() >= 4) {
	StringValue(src);
	if (!NIL_P(scope) && !OBJ_TAINTED(scope)) {
	    rb_raise(rb_eSecurityError,
		     "Insecure: can't modify trusted binding");
	}
    }
    else {
	SafeStringValue(src);
    }
    if (argc >= 3) {
	StringValue(vfile);
    }
    if (argc >= 4) {
	line = NUM2INT(vline);
    }

    if (!NIL_P(vfile)) {
	file = RSTRING_PTR(vfile);
    }
    return eval_string(self, src, scope, file, line);
}

VALUE
rb_eval_string(const char *str)
{
    return eval_string(rb_vm_top_self(), rb_str_new2(str), Qnil, "(eval)", 1);
}

VALUE
rb_eval_cmd(VALUE cmd, VALUE arg, int level)
{
    VALUE val = Qnil;		/* OK */
    volatile int safe = rb_safe_level();

    if (OBJ_TAINTED(cmd)) {
	level = 4;
    }

    if (TYPE(cmd) != T_STRING) {
	rb_set_safe_level_force(level);
	val = rb_funcall2(cmd, rb_intern("call"), RARRAY_LEN(arg),
		RARRAY_PTR(arg));
	rb_set_safe_level_force(safe);
	return val;
    }

    val = eval_string(rb_vm_top_self(), cmd, Qnil, 0, 0);
    rb_set_safe_level_force(safe);
    return val;
}

/*
 *  call-seq:
 *     obj.instance_eval(string [, filename [, lineno]] )   => obj
 *     obj.instance_eval {| | block }                       => obj
 *
 *  Evaluates a string containing Ruby source code, or the given block,
 *  within the context of the receiver (_obj_). In order to set the
 *  context, the variable +self+ is set to _obj_ while
 *  the code is executing, giving the code access to _obj_'s
 *  instance variables. In the version of <code>instance_eval</code>
 *  that takes a +String+, the optional second and third
 *  parameters supply a filename and starting line number that are used
 *  when reporting compilation errors.
 *
 *     class KlassWithSecret
 *       def initialize
 *         @secret = 99
 *       end
 *     end
 *     k = KlassWithSecret.new
 *     k.instance_eval { @secret }   #=> 99
 */

static VALUE
rb_obj_instance_eval(VALUE self, SEL sel, int argc, VALUE *argv)
{
    VALUE klass;

    if (SPECIAL_CONST_P(self)) {
	klass = Qnil;
    }
    else {
	klass = CLASS_OF(self);
    }
    return specific_eval(argc, argv, klass, self);
}

/*
 *  call-seq:
 *     obj.instance_exec(arg...) {|var...| block }                       => obj
 *
 *  Executes the given block within the context of the receiver
 *  (_obj_). In order to set the context, the variable +self+ is set
 *  to _obj_ while the code is executing, giving the code access to
 *  _obj_'s instance variables.  Arguments are passed as block parameters.
 *
 *     class KlassWithSecret
 *       def initialize
 *         @secret = 99
 *       end
 *     end
 *     k = KlassWithSecret.new
 *     k.instance_exec(5) {|x| @secret+x }   #=> 104
 */

static VALUE
rb_obj_instance_exec(VALUE self, SEL sel, int argc, VALUE *argv)
{
    VALUE klass;

    if (SPECIAL_CONST_P(self)) {
	klass = Qnil;
    }
    else {
	klass = rb_singleton_class(self);
    }
    return rb_vm_yield_under(klass, self, argc, argv);
}

/*
 *  call-seq:
 *     mod.class_eval(string [, filename [, lineno]])  => obj
 *     mod.module_eval {|| block }                     => obj
 *
 *  Evaluates the string or block in the context of _mod_. This can
 *  be used to add methods to a class. <code>module_eval</code> returns
 *  the result of evaluating its argument. The optional _filename_
 *  and _lineno_ parameters set the text for error messages.
 *
 *     class Thing
 *     end
 *     a = %q{def hello() "Hello there!" end}
 *     Thing.module_eval(a)
 *     puts Thing.new.hello()
 *     Thing.module_eval("invalid code", "dummy", 123)
 *
 *  <em>produces:</em>
 *
 *     Hello there!
 *     dummy:123:in `module_eval': undefined local variable
 *         or method `code' for Thing:Class
 */

VALUE
rb_mod_module_eval(VALUE mod, SEL sel, int argc, VALUE *argv)
{
    return specific_eval(argc, argv, mod, mod);
}

/*
 *  call-seq:
 *     mod.module_exec(arg...) {|var...| block }       => obj
 *     mod.class_exec(arg...) {|var...| block }        => obj
 *
 *  Evaluates the given block in the context of the class/module.
 *  The method defined in the block will belong to the receiver.
 *
 *     class Thing
 *     end
 *     Thing.class_exec{
 *       def hello() "Hello there!" end
 *     }
 *     puts Thing.new.hello()
 *
 *  <em>produces:</em>
 *
 *     Hello there!
 */

VALUE
rb_mod_module_exec(VALUE mod, SEL sel, int argc, VALUE *argv)
{
    return rb_vm_yield_under(mod, mod, argc, argv);
}

/*
 *  call-seq:
 *     throw(symbol [, obj])
 *
 *  Transfers control to the end of the active +catch+ block
 *  waiting for _symbol_. Raises +NameError+ if there
 *  is no +catch+ block for the symbol. The optional second
 *  parameter supplies a return value for the +catch+ block,
 *  which otherwise defaults to +nil+. For examples, see
 *  <code>Kernel::catch</code>.
 */

static VALUE
rb_f_throw(VALUE rcv, SEL sel, int argc, VALUE *argv)
{
    // TODO
    return Qnil;
}

void
rb_throw(const char *tag, VALUE val)
{
    VALUE argv[2];

    argv[0] = ID2SYM(rb_intern(tag));
    argv[1] = val;
    rb_f_throw(Qnil, 0, 2, argv);
}

void
rb_throw_obj(VALUE tag, VALUE val)
{
    VALUE argv[2];

    argv[0] = tag;
    argv[1] = val;
    rb_f_throw(Qnil, 0, 2, argv);
}

/*
 *  call-seq:
 *     catch(symbol) {| | block }  > obj
 *
 *  +catch+ executes its block. If a +throw+ is
 *  executed, Ruby searches up its stack for a +catch+ block
 *  with a tag corresponding to the +throw+'s
 *  _symbol_. If found, that block is terminated, and
 *  +catch+ returns the value given to +throw+. If
 *  +throw+ is not called, the block terminates normally, and
 *  the value of +catch+ is the value of the last expression
 *  evaluated. +catch+ expressions may be nested, and the
 *  +throw+ call need not be in lexical scope.
 *
 *     def routine(n)
 *       puts n
 *       throw :done if n <= 0
 *       routine(n-1)
 *     end
 *
 *
 *     catch(:done) { routine(3) }
 *
 *  <em>produces:</em>
 *
 *     3
 *     2
 *     1
 *     0
 */

static VALUE
rb_f_catch(VALUE rcv, SEL sel, int argc, VALUE *argv)
{
    // TODO
    return Qnil;
}

/*
 *  call-seq:
 *     caller(start=1)    => array
 *
 *  Returns the current execution stack---an array containing strings in
 *  the form ``<em>file:line</em>'' or ``<em>file:line: in
 *  `method'</em>''. The optional _start_ parameter
 *  determines the number of initial stack entries to omit from the
 *  result.
 *
 *     def a(skip)
 *       caller(skip)
 *     end
 *     def b(skip)
 *       a(skip)
 *     end
 *     def c(skip)
 *       b(skip)
 *     end
 *     c(0)   #=> ["prog:2:in `a'", "prog:5:in `b'", "prog:8:in `c'", "prog:10"]
 *     c(1)   #=> ["prog:5:in `b'", "prog:8:in `c'", "prog:11"]
 *     c(2)   #=> ["prog:8:in `c'", "prog:12"]
 *     c(3)   #=> ["prog:13"]
 */

static VALUE
rb_f_caller(VALUE klass, SEL sel, int argc, VALUE *argv)
{
    VALUE level;
    int lev;

    rb_scan_args(argc, argv, "01", &level);

    if (NIL_P(level)) {
	lev = 1;
    }
    else {
	lev = NUM2INT(level);
    }

    if (lev < 0) {
	rb_raise(rb_eArgError, "negative level (%d)", lev);
    }

    return rb_vm_backtrace(lev);
}

void
rb_backtrace(void)
{
    long i, count;
    VALUE ary;

    ary = rb_vm_backtrace(-1);
    for (i = 0, count = RARRAY_LEN(ary); i < count; i++) {
	printf("\tfrom %s\n", RSTRING_PTR(RARRAY_AT(ary, i)));
    }
}

VALUE
rb_make_backtrace(void)
{
    return rb_vm_backtrace(-1);
}

void
Init_vm_eval(void)
{
    rb_objc_define_method(rb_mKernel, "catch", rb_f_catch, -1);
    rb_objc_define_method(rb_mKernel, "throw", rb_f_throw, -1);

    rb_objc_define_method(rb_mKernel, "loop", rb_f_loop, 0);

    rb_objc_define_method(rb_cNSObject, "instance_eval", rb_obj_instance_eval, -1);
    rb_objc_define_method(rb_cNSObject, "instance_exec", rb_obj_instance_exec, -1);
    rb_objc_define_private_method(rb_cNSObject, "method_missing", rb_method_missing, -1);
    rb_objc_define_method(rb_cNSObject, "__send__", rb_f_send, -1);

    rb_objc_define_method(rb_cBasicObject, "instance_eval", rb_obj_instance_eval, -1);
    rb_objc_define_method(rb_cBasicObject, "instance_exec", rb_obj_instance_exec, -1);
    rb_objc_define_private_method(rb_cBasicObject, "method_missing", rb_method_missing, -1);
    rb_objc_define_method(rb_cBasicObject, "__send__", rb_f_send, -1);

    rb_objc_define_method(rb_mKernel, "send", rb_f_send, -1);
    rb_objc_define_method(rb_mKernel, "public_send", rb_f_public_send, -1);

    rb_objc_define_method(rb_cModule, "module_exec", rb_mod_module_exec, -1);
    rb_objc_define_method(rb_cModule, "class_exec", rb_mod_module_exec, -1);

    rb_objc_define_method(rb_mKernel, "caller", rb_f_caller, -1);
}

