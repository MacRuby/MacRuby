/*
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2007-2011, Apple Inc. All rights reserved.
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 * Copyright (C) 2000  Network Applied Communication Laboratory, Inc.
 * Copyright (C) 2000  Information-technology Promotion Agency, Japan
 */

/*
 * This file is included by vm_eval.c
 */

static ID __send__, object_id;
static ID removed, singleton_removed, undefined, singleton_undefined;
static ID eqq, each, aref, aset, match, missing;
static ID added, singleton_added;

static void
remove_method(VALUE klass, ID mid)
{
    if (klass == rb_cObject) {
	rb_secure(4);
    }
    if (rb_safe_level() >= 4 && !OBJ_UNTRUSTED(klass)) {
	rb_raise(rb_eSecurityError, "Insecure: can't remove method");
    }
    if (OBJ_FROZEN(klass)) {
	rb_error_frozen("class/module");
    }
    if (mid == object_id || mid == __send__ || mid == idInitialize) {
	rb_warn("removing `%s' may cause serious problem", rb_id2name(mid));
    }

    rb_vm_remove_method((Class)klass, mid);
}

void
rb_remove_method(VALUE klass, const char *name)
{
    remove_method(klass, rb_intern(name));
}

/*
 *  call-seq:
 *     remove_method(symbol)   => self
 *
 *  Removes the method identified by _symbol_ from the current
 *  class. For an example, see <code>Module.undef_method</code>.
 */

static VALUE
rb_mod_remove_method(VALUE mod, SEL sel, int argc, VALUE *argv)
{
    for (int i = 0; i < argc; i++) {
	remove_method(mod, rb_to_id(argv[i]));
    }
    return mod;
}

#undef rb_disable_super
#undef rb_enable_super

void
rb_disable_super(VALUE klass, const char *name)
{
    /* obsolete - no use */
}

void
rb_enable_super(VALUE klass, const char *name)
{
    rb_warning("rb_enable_super() is obsolete");
}

void rb_print_undef(VALUE, ID, int);

static void
rb_export_method(VALUE klass, ID name, ID noex)
{
    rb_vm_method_node_t *node;
    SEL sel;

    if (klass == rb_cObject) {
	rb_secure(4);
    }

    if (!rb_vm_lookup_method2((Class)klass, name, &sel, NULL, &node)) {
	if (TYPE(klass) != T_MODULE
		|| !rb_vm_lookup_method2((Class)rb_cObject, name, &sel, NULL,
		    &node)) {
	    rb_print_undef(klass, name, 0);
	}
    }

    if (node == NULL) {
	rb_raise(rb_eRuntimeError,
		"can't change visibility of non Ruby method `%s'",
		sel_getName(sel));
    }

    long flags = (node->flags & ~VM_METHOD_PRIVATE) & ~VM_METHOD_PROTECTED;
    switch (noex) {
	case NOEX_PRIVATE:
	    flags |= VM_METHOD_PRIVATE;
	    break;

	case NOEX_PROTECTED:
	    flags |= VM_METHOD_PROTECTED;
	    break;

	default:
	    break;
    }

    if (node->flags != flags) {
	if (node->klass == (Class)klass) {
	    node->flags = flags;
	}
	else {
	    rb_vm_define_method2((Class)klass, sel, node, flags, false);
	}
    }
}

void
rb_attr(VALUE klass, ID id, int read, int write, int ex)
{
    if (!rb_is_local_id(id) && !rb_is_const_id(id)) {
	rb_name_error(id, "invalid attribute name `%s'", rb_id2name(id));
    }
    const char *name = rb_id2name(id);
    if (name == NULL) {
	rb_raise(rb_eArgError, "argument needs to be symbol or string");
    }
    rb_vm_define_attr((Class)klass, name, read, write);
    if (write) {
	rb_objc_define_kvo_setter(klass, id);
    }
}

void
rb_undef(VALUE klass, ID id)
{
    if (NIL_P(klass)) {
	rb_raise(rb_eTypeError, "no class to undef method");
    }
    if (klass == rb_cObject) {
	rb_secure(4);
    }
    if (rb_safe_level() >= 4 && !OBJ_UNTRUSTED(klass)) {
	rb_raise(rb_eSecurityError, "Insecure: can't undef `%s'",
		rb_id2name(id));
    }
    rb_frozen_class_p(klass);
    if (id == object_id || id == __send__ || id == idInitialize) {
	rb_warn("undefining `%s' may cause serious problem", rb_id2name(id));
    }

    rb_vm_undef_method((Class)klass, id, true);
}

/*
 *  call-seq:
 *     undef_method(symbol)    => self
 *
 *  Prevents the current class from responding to calls to the named
 *  method. Contrast this with <code>remove_method</code>, which deletes
 *  the method from the particular class; Ruby will still search
 *  superclasses and mixed-in modules for a possible receiver.
 *
 *     class Parent
 *       def hello
 *         puts "In parent"
 *       end
 *     end
 *     class Child < Parent
 *       def hello
 *         puts "In child"
 *       end
 *     end
 *
 *
 *     c = Child.new
 *     c.hello
 *
 *
 *     class Child
 *       remove_method :hello  # remove from child, still in parent
 *     end
 *     c.hello
 *
 *
 *     class Child
 *       undef_method :hello   # prevent any calls to 'hello'
 *     end
 *     c.hello
 *
 *  <em>produces:</em>
 *
 *     In child
 *     In parent
 *     prog.rb:23: undefined method `hello' for #<Child:0x401b3bb4> (NoMethodError)
 */

static VALUE
rb_mod_undef_method(VALUE mod, SEL sel, int argc, VALUE *argv)
{
    for (int i = 0; i < argc; i++) {
	rb_undef(mod, rb_to_id(argv[i]));
    }
    return mod;
}

/*
 *  call-seq:
 *     mod.method_defined?(symbol)    => true or false
 *
 *  Returns +true+ if the named method is defined by
 *  _mod_ (or its included modules and, if _mod_ is a class,
 *  its ancestors). Public and protected methods are matched.
 *
 *     module A
 *       def method1()  end
 *     end
 *     class B
 *       def method2()  end
 *     end
 *     class C < B
 *       include A
 *       def method3()  end
 *     end
 *
 *     A.method_defined? :method1    #=> true
 *     C.method_defined? "method1"   #=> true
 *     C.method_defined? "method2"   #=> true
 *     C.method_defined? "method3"   #=> true
 *     C.method_defined? "method4"   #=> false
 */

static SEL selRespondToDefault = 0;

static bool
rb_obj_respond_to2(VALUE obj, VALUE klass, ID id, int priv, int check_override)
{
    const char *id_name = rb_id2name(id);
    SEL sel = sel_registerName(id_name);
    if (!rb_vm_respond_to2(obj, klass, sel, priv, check_override)) {
	char buf[100];
	snprintf(buf, sizeof buf, "%s:", id_name);
	sel = sel_registerName(buf);
	if (!rb_vm_respond_to2(obj, klass, sel, priv, check_override)) {
	    VALUE args[2];
	    args[0] = ID2SYM(id);
	    args[1] = priv ? Qtrue : Qfalse;
	    return RTEST(rb_vm_call(obj, selRespondToDefault, 2, args));
	}
    }
    return true;
}

static VALUE
rb_mod_method_defined(VALUE mod, SEL sel, VALUE mid)
{
    ID id = rb_to_id(mid);
    if (rb_obj_respond_to2(Qnil, mod, id, true, false)) {
	rb_vm_method_node_t *node;
	if (rb_vm_lookup_method2((Class)mod, id, NULL, NULL, &node)) {
	    if (node != NULL) {
		if (node->flags & NOEX_PRIVATE) {
		    return Qfalse;
		}
	    }
	    return Qtrue;
	}
    }
    return Qfalse;
}

#define VISI_CHECK(x,f) (((x)&NOEX_MASK) == (f))

/*
 *  call-seq:
 *     mod.public_method_defined?(symbol)   => true or false
 *
 *  Returns +true+ if the named public method is defined by
 *  _mod_ (or its included modules and, if _mod_ is a class,
 *  its ancestors).
 *
 *     module A
 *       def method1()  end
 *     end
 *     class B
 *       protected
 *       def method2()  end
 *     end
 *     class C < B
 *       include A
 *       def method3()  end
 *     end
 *
 *     A.method_defined? :method1           #=> true
 *     C.public_method_defined? "method1"   #=> true
 *     C.public_method_defined? "method2"   #=> false
 *     C.method_defined? "method2"          #=> true
 */

static VALUE
check_method_visibility(VALUE mod, ID id, int visi)
{
    rb_vm_method_node_t *node;
    if (rb_vm_lookup_method2((Class)mod, id, NULL, NULL, &node)) {
	if (node != NULL) {
	    if ((node->flags & NOEX_MASK) == visi) {
		return Qtrue;
	    }
	}
    }
    return Qfalse;
}

static VALUE
rb_mod_public_method_defined(VALUE mod, SEL sel, VALUE mid)
{
    ID id = rb_to_id(mid);
    return check_method_visibility(mod, id, NOEX_PUBLIC);
}

/*
 *  call-seq:
 *     mod.private_method_defined?(symbol)    => true or false
 *
 *  Returns +true+ if the named private method is defined by
 *  _ mod_ (or its included modules and, if _mod_ is a class,
 *  its ancestors).
 *
 *     module A
 *       def method1()  end
 *     end
 *     class B
 *       private
 *       def method2()  end
 *     end
 *     class C < B
 *       include A
 *       def method3()  end
 *     end
 *
 *     A.method_defined? :method1            #=> true
 *     C.private_method_defined? "method1"   #=> false
 *     C.private_method_defined? "method2"   #=> true
 *     C.method_defined? "method2"           #=> false
 */

static VALUE
rb_mod_private_method_defined(VALUE mod, SEL sel, VALUE mid)
{
    ID id = rb_to_id(mid);
    return check_method_visibility(mod, id, NOEX_PRIVATE);
}

/*
 *  call-seq:
 *     mod.protected_method_defined?(symbol)   => true or false
 *
 *  Returns +true+ if the named protected method is defined
 *  by _mod_ (or its included modules and, if _mod_ is a
 *  class, its ancestors).
 *
 *     module A
 *       def method1()  end
 *     end
 *     class B
 *       protected
 *       def method2()  end
 *     end
 *     class C < B
 *       include A
 *       def method3()  end
 *     end
 *
 *     A.method_defined? :method1              #=> true
 *     C.protected_method_defined? "method1"   #=> false
 *     C.protected_method_defined? "method2"   #=> true
 *     C.method_defined? "method2"             #=> true
 */

static VALUE
rb_mod_protected_method_defined(VALUE mod, SEL sel, VALUE mid)
{
    ID id = rb_to_id(mid);
    return check_method_visibility(mod, id, NOEX_PROTECTED);
}

void
rb_alias(VALUE klass, ID name, ID def)
{
    rb_vm_alias(klass, name, def);
}

/*
 *  call-seq:
 *     alias_method(new_name, old_name)   => self
 *
 *  Makes <i>new_name</i> a new copy of the method <i>old_name</i>. This can
 *  be used to retain access to methods that are overridden.
 *
 *     module Mod
 *       alias_method :orig_exit, :exit
 *       def exit(code=0)
 *         puts "Exiting with code #{code}"
 *         orig_exit(code)
 *       end
 *     end
 *     include Mod
 *     exit(99)
 *
 *  <em>produces:</em>
 *
 *     Exiting with code 99
 */

static VALUE
rb_mod_alias_method(VALUE mod, SEL sel, VALUE newname, VALUE oldname)
{
    rb_alias(mod, rb_to_id(newname), rb_to_id(oldname));
    return mod;
}

static void
secure_visibility(VALUE self)
{
    if (rb_safe_level() >= 4 && !OBJ_UNTRUSTED(self)) {
	rb_raise(rb_eSecurityError,
		 "Insecure: can't change method visibility");
    }
}

static void
set_method_visibility(VALUE self, int argc, VALUE *argv, ID ex)
{
    secure_visibility(self);
    for (int i = 0; i < argc; i++) {
	rb_export_method(self, rb_to_id(argv[i]), ex);
    }
}

/*
 *  call-seq:
 *     public                 => self
 *     public(symbol, ...)    => self
 *
 *  With no arguments, sets the default visibility for subsequently
 *  defined methods to public. With arguments, sets the named methods to
 *  have public visibility.
 */

static VALUE
rb_mod_public(VALUE module, SEL sel, int argc, VALUE *argv)
{
    secure_visibility(module);
    if (argc == 0) {
	rb_vm_set_current_scope(module, SCOPE_PUBLIC);
    }
    else {
	set_method_visibility(module, argc, argv, NOEX_PUBLIC);
    }
    return module;
}

/*
 *  call-seq:
 *     protected                => self
 *     protected(symbol, ...)   => self
 *
 *  With no arguments, sets the default visibility for subsequently
 *  defined methods to protected. With arguments, sets the named methods
 *  to have protected visibility.
 */

static VALUE
rb_mod_protected(VALUE module, SEL sel, int argc, VALUE *argv)
{
    secure_visibility(module);
    if (argc == 0) {
	rb_vm_set_current_scope(module, SCOPE_PROTECTED);
    }
    else {
	set_method_visibility(module, argc, argv, NOEX_PROTECTED);
    }
    return module;
}

/*
 *  call-seq:
 *     private                 => self
 *     private(symbol, ...)    => self
 *
 *  With no arguments, sets the default visibility for subsequently
 *  defined methods to private. With arguments, sets the named methods
 *  to have private visibility.
 *
 *     module Mod
 *       def a()  end
 *       def b()  end
 *       private
 *       def c()  end
 *       private :a
 *     end
 *     Mod.private_instance_methods   #=> [:a, :c]
 */

static VALUE
rb_mod_private(VALUE module, SEL sel, int argc, VALUE *argv)
{
    secure_visibility(module);
    if (argc == 0) {
	rb_vm_set_current_scope(module, SCOPE_PRIVATE);
    }
    else {
	set_method_visibility(module, argc, argv, NOEX_PRIVATE);
    }
    return module;
}

/*
 *  call-seq:
 *     mod.public_class_method(symbol, ...)    => mod
 *
 *  Makes a list of existing class methods public.
 */

static VALUE
rb_mod_public_method(VALUE obj, SEL sel, int argc, VALUE *argv)
{
    set_method_visibility(CLASS_OF(obj), argc, argv, NOEX_PUBLIC);
    return obj;
}

/*
 *  call-seq:
 *     mod.private_class_method(symbol, ...)   => mod
 *
 *  Makes existing class methods private. Often used to hide the default
 *  constructor <code>new</code>.
 *
 *     class SimpleSingleton  # Not thread safe
 *       private_class_method :new
 *       def SimpleSingleton.create(*args, &block)
 *         @me = new(*args, &block) if ! @me
 *         @me
 *       end
 *     end
 */

static VALUE
rb_mod_private_method(VALUE obj, SEL sel, int argc, VALUE *argv)
{
    set_method_visibility(CLASS_OF(obj), argc, argv, NOEX_PRIVATE);
    return obj;
}

/*
 *  call-seq:
 *     public
 *     public(symbol, ...)
 *
 *  With no arguments, sets the default visibility for subsequently
 *  defined methods to public. With arguments, sets the named methods to
 *  have public visibility.
 */

static VALUE
top_public(VALUE recv, SEL sel, int argc, VALUE *argv)
{
    return rb_mod_public(rb_cObject, 0, argc, argv);
}

static VALUE
top_private(VALUE recv, SEL sel, int argc, VALUE *argv)
{
    return rb_mod_private(rb_cObject, 0, argc, argv);
}

/*
 *  call-seq:
 *     module_function(symbol, ...)    => self
 *
 *  Creates module functions for the named methods. These functions may
 *  be called with the module as a receiver, and also become available
 *  as instance methods to classes that mix in the module. Module
 *  functions are copies of the original, and so may be changed
 *  independently. The instance-method versions are made private. If
 *  used with no arguments, subsequently defined methods become module
 *  functions.
 *
 *     module Mod
 *       def one
 *         "This is one"
 *       end
 *       module_function :one
 *     end
 *     class Cls
 *       include Mod
 *       def callOne
 *         one
 *       end
 *     end
 *     Mod.one     #=> "This is one"
 *     c = Cls.new
 *     c.callOne   #=> "This is one"
 *     module Mod
 *       def one
 *         "This is the new one"
 *       end
 *     end
 *     Mod.one     #=> "This is one"
 *     c.callOne   #=> "This is the new one"
 */

static VALUE
rb_mod_modfunc(VALUE module, SEL sel, int argc, VALUE *argv)
{
    if (TYPE(module) != T_MODULE) {
	rb_raise(rb_eTypeError, "module_function must be called for modules");
    }

    secure_visibility(module);
    if (argc == 0) {
	rb_vm_set_current_scope(module, SCOPE_MODULE_FUNC);
	return module;
    }

    set_method_visibility(module, argc, argv, NOEX_PRIVATE);

    for (int i = 0; i < argc; i++) {
	ID id = rb_to_id(argv[i]);
	IMP imp = NULL;
	rb_vm_method_node_t *node = NULL;
	SEL sel = 0;

	if (rb_vm_lookup_method2((Class)module, id, &sel, &imp, &node)
		|| (TYPE(module) == T_MODULE
		    && rb_vm_lookup_method2((Class)rb_cObject, id, &sel,
			&imp, &node))) {
	    rb_vm_define_method2(*(Class *)module, sel, node, -1, false);
	}
	else {
	    rb_bug("undefined method `%s'; can't happen", rb_id2name(id));
	}
    }

    return module;
}

/*
 *  call-seq:
 *     obj.respond_to?(symbol, include_private=false) => true or false
 *
 *  Returns +true+ if _obj_ responds to the given
 *  method. Private methods are included in the search only if the
 *  optional second parameter evaluates to +true+.
 *
 *  If the method is not implemented,
 *  as Process.fork on Windows, File.lchmod on GNU/Linux, etc.,
 *  false is returned.
 *
 *  If the method is not defined, <code>respond_to_missing?</code>
 *  method is called and the result is returned.
 */

int
rb_obj_respond_to(VALUE obj, ID id, int priv)
{
    return rb_obj_respond_to2(obj, Qnil, id, priv, true);
}

int
rb_respond_to(VALUE obj, ID id)
{
    return rb_obj_respond_to(obj, id, false);
}

static VALUE
obj_respond_to(VALUE obj, SEL sel, int argc, VALUE *argv)
{
    VALUE mid, priv;
    ID id;

    rb_scan_args(argc, argv, "11", &mid, &priv);
    id = rb_to_id(mid);
    return rb_obj_respond_to2(obj, Qnil, id, RTEST(priv), false) ? Qtrue : Qfalse;
}

/*
 *  call-seq:
 *     obj.respond_to_missing?(symbol, include_private) => true or false
 *
 *  Hook method to return whether the _obj_ can respond to _id_ method
 *  or not.
 *
 *  See #respond_to?.
 */

static VALUE
obj_respond_to_missing(VALUE obj, SEL sel, VALUE sym, VALUE priv)
{
    return Qfalse;
}

IMP basic_respond_to_imp = NULL;

void
Init_eval_method(void)
{
    rb_objc_define_method(rb_mKernel, "respond_to?", obj_respond_to, -1);
    rb_objc_define_method(rb_mKernel, "respond_to_missing?", obj_respond_to_missing, 2);
    selRespondToDefault = sel_registerName("respond_to_missing?:");
    basic_respond_to_imp = class_getMethodImplementation((Class)rb_mKernel,
	    selRespondTo);

    rb_objc_define_private_method(rb_cModule, "remove_method", rb_mod_remove_method, -1);
    rb_objc_define_private_method(rb_cModule, "undef_method", rb_mod_undef_method, -1);
    rb_objc_define_private_method(rb_cModule, "alias_method", rb_mod_alias_method, 2);
    rb_objc_define_private_method(rb_cModule, "public", rb_mod_public, -1);
    rb_objc_define_private_method(rb_cModule, "protected", rb_mod_protected, -1);
    rb_objc_define_private_method(rb_cModule, "private", rb_mod_private, -1);
    rb_objc_define_private_method(rb_cModule, "module_function", rb_mod_modfunc, -1);

    rb_objc_define_method(rb_cModule, "method_defined?", rb_mod_method_defined, 1);
    rb_objc_define_method(rb_cModule, "public_method_defined?",
	    rb_mod_public_method_defined, 1);
    rb_objc_define_method(rb_cModule, "private_method_defined?",
	    rb_mod_private_method_defined, 1);
    rb_objc_define_method(rb_cModule, "protected_method_defined?",
	    rb_mod_protected_method_defined, 1);
    rb_objc_define_method(rb_cModule, "public_class_method", rb_mod_public_method, -1);
    rb_objc_define_method(rb_cModule, "private_class_method", rb_mod_private_method, -1);

    VALUE cTopLevel = *(VALUE *)rb_vm_top_self();
    rb_objc_define_method(cTopLevel, "public", top_public, -1);
    rb_objc_define_method(cTopLevel, "private", top_private, -1);

    object_id = rb_intern("object_id");
    __send__ = rb_intern("__send__");
    eqq = rb_intern("===");
    each = rb_intern("each");
    aref = rb_intern("[]");
    aset = rb_intern("[]=");
    match = rb_intern("=~");
    missing = rb_intern("method_missing");
    added = rb_intern("method_added");
    singleton_added = rb_intern("singleton_method_added");
    removed = rb_intern("method_removed");
    singleton_removed = rb_intern("singleton_method_removed");
    undefined = rb_intern("method_undefined");
    singleton_undefined = rb_intern("singleton_method_undefined");
}
