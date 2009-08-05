/* -*-c-*- */
/*
 * This file is included by vm_eval.c
 */

static ID __send__, object_id;
static ID removed, singleton_removed, undefined, singleton_undefined;
static ID eqq, each, aref, aset, match, missing;
static ID added, singleton_added;

void
rb_add_method(VALUE klass, ID mid, NODE * node, int noex)
{
    // TODO
    return;
}

void
rb_define_alloc_func(VALUE klass, VALUE (*func)(VALUE))
{
    // TODO
#if 0
    Check_Type(klass, T_CLASS);
    rb_add_method(rb_singleton_class(klass), ID_ALLOCATOR, NEW_CFUNC(func, 0),
		  NOEX_PUBLIC);
#endif
}

void
rb_undef_alloc_func(VALUE klass)
{
    // TODO
#if 0
    Check_Type(klass, T_CLASS);
    rb_add_method(rb_singleton_class(klass), ID_ALLOCATOR, 0, NOEX_UNDEF);
#endif
}

rb_alloc_func_t
rb_get_alloc_func(VALUE klass)
{
    NODE *n;
    Check_Type(klass, T_CLASS);
    n = rb_method_node(CLASS_OF(klass), ID_ALLOCATOR);
    if (!n) return 0;
    if (nd_type(n) != NODE_METHOD) return 0;
    n = n->nd_body;
    if (nd_type(n) != NODE_CFUNC) return 0;
    return (rb_alloc_func_t)n->nd_cfnc;
}

static NODE *
search_method(VALUE klass, ID id, VALUE *klassp)
{
    NODE *node;
    if (klass == 0) {
	return NULL;
    }
    node = rb_method_node(klass, id);
    if (node != NULL) {
	if (klassp != NULL) { /* TODO honour klassp */
	    *klassp = klass;
	}
    }
    return node;
}

/*
 * search method body (NODE_METHOD)
 *   with    : klass and id
 *   without : method cache
 *
 * if you need method node with method cache, use
 * rb_method_node()
 */
NODE *
rb_get_method_body(VALUE klass, ID id, ID *idp)
{
    return search_method(klass, id, NULL);
}

NODE *
rb_method_node(VALUE klass, ID id)
{
    return NULL;
#if 0 // TODO
    NODE *node = rb_objc_method_node(klass, id, NULL, NULL);
    if (node == NULL && id != ID_ALLOCATOR) {
	const char *id_str = rb_id2name(id);
	size_t slen = strlen(id_str);

	if (strcmp(id_str, "retain") == 0
	    || strcmp(id_str, "release") == 0
	    || strcmp(id_str, "zone") == 0) {
	    char buf[100];
	    snprintf(buf, sizeof buf, "__rb_%s__", id_str);
	    return rb_method_node(klass, rb_intern(buf));
	}
	else {
	    if (id_str[slen - 1] == ':') {
		return NULL;
	    }
	    else {
		char buf[100];
		snprintf(buf, sizeof buf, "%s:", id_str);
		return rb_method_node(klass, rb_intern(buf));
	    }
	}
    }
    return node;
#endif
}

static void
remove_method(VALUE klass, ID mid)
{
    if (klass == rb_cObject) {
	rb_secure(4);
    }
    if (rb_safe_level() >= 4 && !OBJ_TAINTED(klass)) {
	rb_raise(rb_eSecurityError, "Insecure: can't remove method");
    }
    if (OBJ_FROZEN(klass))
	rb_error_frozen("class/module");
    if (mid == object_id || mid == __send__ || mid == idInitialize) {
	rb_warn("removing `%s' may cause serious problem", rb_id2name(mid));
    }
    SEL sel;
    Method m;

    sel = sel_registerName(rb_id2name(mid));
    m = class_getInstanceMethod((Class)klass, sel);
    if (m == NULL) {
	char buf[100];
	size_t len = strlen((char *)sel);
	if (((char *)sel)[len - 1] != ':') {
	    snprintf(buf, sizeof buf, "%s:", (char *)sel);
	    sel = sel_registerName(buf);
	    m = class_getInstanceMethod((Class)klass, sel);
	}
    }
    if (m == NULL) {
	rb_name_error(mid, "method `%s' not defined in %s",
		      rb_id2name(mid), rb_class2name(klass));
    }
    if (rb_vm_get_method_node(method_getImplementation(m)) == NULL) {
	rb_warn("removing pure Objective-C method `%s' may cause serious " \
		"problem", rb_id2name(mid));
    }
    method_setImplementation(m, NULL);

    if (RCLASS_SINGLETON(klass)) {
	rb_funcall(rb_iv_get(klass, "__attached__"), singleton_removed, 1,
		   ID2SYM(mid));
    }
    else {
	rb_funcall(klass, removed, 1, ID2SYM(mid));
    }
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
    int i;

    for (i = 0; i < argc; i++) {
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
	    || !rb_vm_lookup_method2((Class)rb_cObject, name, &sel, NULL, &node)) {
	    rb_print_undef(klass, name, 0);
	}
    }

#if 0 // TODO
    if (node->nd_noex != noex) {
	// TODO if the method exists on a super class, we should add a new method
	// with the correct noex that calls super
	assert(!rb_vm_lookup_method((Class)RCLASS_SUPER(klass), sel, NULL, NULL));

	node->nd_noex = noex;
    }
#endif
}

int
rb_method_boundp(VALUE klass, ID id, int ex)
{
    NODE *method;

    if ((method = rb_method_node(klass, id)) != 0) {
	if (ex && (method->nd_noex & NOEX_PRIVATE)) {
	    return Qfalse;
	}
	return Qtrue;
    }
    return Qfalse;
}

void
rb_attr(VALUE klass, ID id, int read, int write, int ex)
{
    const char *name;
    int noex;

    if (!ex) {
	noex = NOEX_PUBLIC;
    }
    else {
	// TODO honor current scope ex
	noex = NOEX_PUBLIC;
    }

    if (!rb_is_local_id(id) && !rb_is_const_id(id)) {
	rb_name_error(id, "invalid attribute name `%s'", rb_id2name(id));
    }
    name = rb_id2name(id);
    if (!name) {
	rb_raise(rb_eArgError, "argument needs to be symbol or string");
    }
    rb_vm_define_attr((Class)klass, name, read, write, noex);
    if (write) {
	rb_objc_define_kvo_setter(klass, id);
    }
}

void
rb_undef(VALUE klass, ID id)
{
    // TODO
#if 0
    VALUE origin;
    NODE *body;

#if 0 // TODO
    if (rb_vm_cbase() == rb_cObject && klass == rb_cObject) {
	rb_secure(4);
    }
#endif
    if (rb_safe_level() >= 4 && !OBJ_TAINTED(klass)) {
	rb_raise(rb_eSecurityError, "Insecure: can't undef `%s'",
		 rb_id2name(id));
    }
    rb_frozen_class_p(klass);
    if (id == object_id || id == __send__ || id == idInitialize) {
	rb_warn("undefining `%s' may cause serious problem", rb_id2name(id));
    }
    /* TODO: warn if a very important method of NSObject is undefined 
     * by default, pure objc methods are not exposed by introspections API 
     */
    body = search_method(klass, id, &origin);
    if (!body || !body->nd_body) {
	const char *s0 = " class";
	VALUE c = klass;

	if (RCLASS_SINGLETON(c)) {
	    VALUE obj = rb_iv_get(klass, "__attached__");

	    switch (TYPE(obj)) {
	      case T_MODULE:
	      case T_CLASS:
		c = obj;
		s0 = "";
	    }
	}
	else if (TYPE(c) == T_MODULE) {
	    s0 = " module";
	}
	rb_name_error(id, "undefined method `%s' for%s `%s'",
		      rb_id2name(id), s0, rb_class2name(c));
    }

    rb_add_method(klass, id, 0, NOEX_PUBLIC);

    if (RCLASS_SINGLETON(klass)) {
	rb_funcall(rb_iv_get(klass, "__attached__"),
		   singleton_undefined, 1, ID2SYM(id));
    }
    else {
	rb_funcall(klass, undefined, 1, ID2SYM(id));
    }
#endif
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
    int i;
    for (i = 0; i < argc; i++) {
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

static VALUE
rb_mod_method_defined(VALUE mod, SEL sel, VALUE mid)
{
    return rb_method_boundp(mod, rb_to_id(mid), 1);
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
rb_mod_public_method_defined(VALUE mod, SEL sel, VALUE mid)
{
    ID id = rb_to_id(mid);
    NODE *method;

    method = rb_method_node(mod, id);
    if (method) {
	if (VISI_CHECK(method->nd_noex, NOEX_PUBLIC))
	    return Qtrue;
    }
    return Qfalse;
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
    NODE *method;

    method = rb_method_node(mod, id);
    if (method) {
	if (VISI_CHECK(method->nd_noex, NOEX_PRIVATE))
	    return Qtrue;
    }
    return Qfalse;
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
    NODE *method;

    method = rb_method_node(mod, id);
    if (method) {
	if (VISI_CHECK(method->nd_noex, NOEX_PROTECTED))
	    return Qtrue;
    }
    return Qfalse;
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
    if (rb_safe_level() >= 4 && !OBJ_TAINTED(self)) {
	rb_raise(rb_eSecurityError,
		 "Insecure: can't change method visibility");
    }
}

static void
set_method_visibility(VALUE self, int argc, VALUE *argv, ID ex)
{
    int i;
    secure_visibility(self);
    for (i = 0; i < argc; i++) {
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
	// TODO change scope!
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
	// TODO change scope!
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
	// TODO change scope!
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
    int i;

    if (TYPE(module) != T_MODULE) {
	rb_raise(rb_eTypeError, "module_function must be called for modules");
    }

    secure_visibility(module);
    if (argc == 0) {
	// TODO change scope!
	return module;
    }

    set_method_visibility(module, argc, argv, NOEX_PRIVATE);

    for (i = 0; i < argc; i++) {
	ID id = rb_to_id(argv[i]);
	IMP imp;
	rb_vm_method_node_t *node;
	SEL sel;

	if (!rb_vm_lookup_method2((Class)module, id, &sel, &imp, &node)) {
	    // Methods are checked in set_method_visibility().
	    rb_bug("undefined method `%s'; can't happen", rb_id2name(id));
	}

	rb_vm_define_method2(*(Class *)module, sel, node, false);
    }

    return module;
}

/*
 *  call-seq:
 *     obj.respond_to?(symbol, include_private=false) => true or false
 *
 *  Returns +true+> if _obj_ responds to the given
 *  method. Private methods are included in the search only if the
 *  optional second parameter evaluates to +true+.
 */

//static NODE *basic_respond_to = 0;

int
rb_obj_respond_to(VALUE obj, ID id, int priv)
{
    const char *id_name = rb_id2name(id);
    SEL sel = sel_registerName(id_name);
    if (!rb_vm_respond_to(obj, sel, priv)) {
	char buf[100];
	snprintf(buf, sizeof buf, "%s:", id_name);
	sel = sel_registerName(buf);
	return rb_vm_respond_to(obj, sel, priv) == true ? 1 : 0;
    }
    return 1;
}

int
rb_respond_to(VALUE obj, ID id)
{
    return rb_obj_respond_to(obj, id, Qfalse);
}

/*
 *  call-seq:
 *     obj.respond_to?(symbol, include_private=false) => true or false
 *
 *  Returns +true+> if _obj_ responds to the given
 *  method. Private methods are included in the search only if the
 *  optional second parameter evaluates to +true+.
 */

static VALUE
obj_respond_to(VALUE obj, SEL sel, int argc, VALUE *argv)
{
    VALUE mid, priv;
    ID id;

    rb_scan_args(argc, argv, "11", &mid, &priv);
    id = rb_to_id(mid);
    return rb_obj_respond_to(obj, id, RTEST(priv)) ? Qtrue : Qfalse;
    return Qfalse;
}

IMP basic_respond_to_imp = NULL;

void
Init_eval_method(void)
{
    rb_objc_define_method(rb_mKernel, "respond_to?", obj_respond_to, -1);
    basic_respond_to_imp = class_getMethodImplementation((Class)rb_mKernel, selRespondTo);
    //basic_respond_to = rb_method_node(rb_cObject, idRespond_to);
    //rb_register_mark_object((VALUE)basic_respond_to);

    rb_objc_define_private_method(rb_cModule, "remove_method", rb_mod_remove_method, -1);
    rb_objc_define_private_method(rb_cModule, "undef_method", rb_mod_undef_method, -1);
    rb_objc_define_private_method(rb_cModule, "alias_method", rb_mod_alias_method, 2);
    rb_objc_define_private_method(rb_cModule, "public", rb_mod_public, -1);
    rb_objc_define_private_method(rb_cModule, "protected", rb_mod_protected, -1);
    rb_objc_define_private_method(rb_cModule, "private", rb_mod_private, -1);
    rb_objc_define_private_method(rb_cModule, "module_function", rb_mod_modfunc, -1);

    rb_objc_define_method(rb_cModule, "method_defined?", rb_mod_method_defined, 1);
    rb_objc_define_method(rb_cModule, "public_method_defined?", rb_mod_public_method_defined, 1);
    rb_objc_define_method(rb_cModule, "private_method_defined?", rb_mod_private_method_defined, 1);
    rb_objc_define_method(rb_cModule, "protected_method_defined?", rb_mod_protected_method_defined, 1);
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

