/*
 * MacRuby implementation of Ruby 1.9's class.c.
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
#include "ruby/signal.h"
#include "ruby/node.h"
#include "ruby/st.h"
#include <ctype.h>
#include <stdarg.h>
#include "id.h"
#include "vm.h"
#include "objc.h"
#include "class.h"

extern st_table *rb_class_tbl;
extern VALUE rb_cRubyObject;
extern VALUE rb_cModuleObject;

static bool
rb_class_hidden(VALUE klass)
{
    return klass == rb_cModuleObject || klass == rb_cRubyObject
	|| RCLASS_SINGLETON(klass);
}

VALUE
rb_class_super(VALUE klass)
{
    if (klass == 0) {
	return 0;
    }
    return (VALUE)class_getSuperclass((Class)klass);
}

void
rb_class_set_super(VALUE klass, VALUE super)
{
    class_setSuperclass((Class)klass, (Class)super);
}

int
rb_class_ismeta(VALUE klass)
{
    return class_isMetaClass((Class)klass);
}

void
rb_objc_class_sync_version(Class ocklass, Class ocsuper)
{
    const long super_version = RCLASS_VERSION(ocsuper);
    long klass_version = RCLASS_VERSION(ocklass);

    if (ocsuper == (Class)rb_cRubyObject
	|| (super_version & RCLASS_IS_OBJECT_SUBCLASS)
	    == RCLASS_IS_OBJECT_SUBCLASS) {
	klass_version |= RCLASS_IS_OBJECT_SUBCLASS;
    }

    RCLASS_SET_VERSION(ocklass, klass_version);
}

static void *
rb_obj_imp_allocWithZone(void *rcv, SEL sel, void *zone)
{
    // XXX honor zone?
    return (void *)rb_vm_new_rb_object((VALUE)rcv);
}

static void *
rb_obj_imp_copyWithZone(void *rcv, SEL sel, void *zone)
{
    // XXX honor zone?
    // for now let rb_obj_dup allocate an instance, since we don't honor the
    // zone yet anyways
    return (void *)rb_obj_dup((VALUE)rcv);
}

static BOOL
rb_obj_imp_isEqual(void *rcv, SEL sel, void *obj)
{
    if (obj == NULL) {
	return false;
    }
    VALUE arg = OC2RB(obj);
    return rb_vm_call((VALUE)rcv, selEq, 1, &arg) == Qtrue;
}

static void *
rb_obj_imp_init(void *rcv, SEL sel)
{
    rb_vm_call((VALUE)rcv, selInitialize, 0, NULL);
    return rcv;
}

VALUE rb_any_to_string(VALUE obj, SEL sel);

static void *
rb_obj_imp_description(void *rcv, SEL sel)
{
    // If #description and #to_s are the same method (ex. when aliased)
    Class rcv_class = (Class)CLASS_OF(rcv);
    IMP desc_imp = class_getMethodImplementation(rcv_class, selDescription);
    IMP to_s_imp = class_getMethodImplementation(rcv_class, selToS);
    if (desc_imp == to_s_imp) {
	return (void *)rb_any_to_string((VALUE)rcv, sel);
    }
    return (void *)rb_vm_call(OC2RB(rcv), selToS, 0, NULL);
}

static VALUE
rb_objc_init(VALUE rcv, SEL sel, int argc, VALUE *argv)
{
    IMP imp = NULL;
    rb_vm_method_node_t *node = NULL;
    if (rb_vm_lookup_method((Class)CLASS_OF(rcv), sel, &imp, &node)
	    && imp != NULL && node == NULL && imp != (IMP)rb_obj_imp_init) {
	return (VALUE)((void *(*)(void *, SEL))*imp)((void *)rcv, selInit);
    }
    return rcv;
}

static VALUE
rb_obj_superclass(VALUE klass, SEL sel)
{
    VALUE cl = RCLASS_SUPER(klass);
    while (rb_class_hidden(cl)) {
	cl = RCLASS_SUPER(cl);
    }
    return rb_class_real(cl, true);
}

VALUE rb_obj_init_copy(VALUE, SEL, VALUE);

void
rb_define_object_special_methods(VALUE klass)
{
    RCLASS_SET_VERSION(*(VALUE *)klass,
	    (RCLASS_VERSION(*(VALUE *)klass) | RCLASS_HAS_ROBJECT_ALLOC));

    rb_objc_define_method(*(VALUE *)klass, "new",
	    rb_class_new_instance_imp, -1);
    rb_objc_define_method(klass, "dup", rb_obj_dup, 0);
    rb_objc_define_private_method(klass, "initialize", rb_objc_init, -1);
    rb_objc_define_private_method(klass, "initialize_copy",
	    rb_obj_init_copy, 1);

    // To make sure singleton classes will be filtered.
    rb_objc_define_method(*(VALUE *)klass, "superclass", rb_obj_superclass, 0);
    rb_objc_define_method(klass, "class", rb_obj_class, 0);

    rb_objc_install_method(*(Class *)klass, selAllocWithZone,
	    (IMP)rb_obj_imp_allocWithZone);
    rb_objc_install_method((Class)klass, selIsEqual,
	    (IMP)rb_obj_imp_isEqual);
    rb_objc_install_method((Class)klass, selInit, (IMP)rb_obj_imp_init);
    rb_objc_install_method((Class)klass, selDescription,
	    (IMP)rb_obj_imp_description);

    // Create -copyWithZone:, since the method doesn't exist yet we need to
    // find the type encoding somewhere, here we check Symbol since it's
    // created very early. 
    Method m = class_getInstanceMethod((Class)rb_cSymbol, selCopyWithZone);
    assert(m != NULL);
    class_replaceMethod((Class)klass, selCopyWithZone, 
	    (IMP)rb_obj_imp_copyWithZone, method_getTypeEncoding(m));
}

static VALUE
rb_objc_alloc_class(const char *name, VALUE super, VALUE flags, VALUE klass)
{
    char ocname[512] = { '\0' };
    if (!rb_vm_generate_objc_class_name(name, ocname, sizeof ocname)) {
	goto no_more_classes;
    }

    Class ocklass = objc_allocateClassPair((Class)super, ocname, 0);
    if (ocklass == NULL) {
	goto no_more_classes;
    }

    long version_flag = RCLASS_IS_RUBY_CLASS;
    if (flags == T_MODULE) {
	version_flag |= RCLASS_IS_MODULE;
    }
    RCLASS_SET_VERSION(ocklass, version_flag);

    objc_registerClassPair(ocklass);

    if (klass != 0 && super != 0) {
	rb_objc_class_sync_version(ocklass, (Class)super);
    }

    return (VALUE)ocklass;

no_more_classes:
    rb_raise(rb_eRuntimeError, "can't create new classes");
}

VALUE
rb_objc_create_class(const char *name, VALUE super)
{
    if (super == 0) {
	super = rb_cObject;
    }
    VALUE klass = rb_objc_alloc_class(name, super, T_CLASS, rb_cClass);
   
    if (super != rb_cNSObject && super != 0
	    && ((RCLASS_VERSION(*(VALUE *)super) & RCLASS_HAS_ROBJECT_ALLOC)
		== RCLASS_HAS_ROBJECT_ALLOC)) {
	RCLASS_SET_VERSION(*(VALUE *)klass,
		(RCLASS_VERSION(*(VALUE *)klass) | RCLASS_HAS_ROBJECT_ALLOC));
    }

    if (name != NULL && rb_class_tbl != NULL) {
	st_insert(rb_class_tbl, (st_data_t)rb_intern(name), (st_data_t)klass);
    }

    return klass;
}

static VALUE
rb_class_boot2(VALUE super, const char *name)
{
    if (super == rb_cObject && rb_cRubyObject != 0) {
	super = rb_cRubyObject;
    }	
    return rb_objc_create_class(name, super);
}

VALUE
rb_class_boot(VALUE super)
{
    return rb_class_boot2(super, NULL);
}

void
rb_check_inheritable(VALUE super)
{
    if (TYPE(super) != T_CLASS) {
	rb_raise(rb_eTypeError, "superclass must be a Class (%s given)",
		 rb_obj_classname(super));
    }
    if (RCLASS_SINGLETON(super)) {
	rb_raise(rb_eTypeError, "can't make subclass of singleton class");
    }
    if (super == rb_cClass) {
	rb_raise(rb_eTypeError, "can't make subclass of Class");
    }
}

VALUE
rb_class_new(VALUE super)
{
    Check_Type(super, T_CLASS);
    rb_check_inheritable(super);
    return rb_class_boot(super);
}

/* :nodoc: */

extern ID id_classid, id_classpath;

VALUE
rb_mod_init_copy(VALUE clone, SEL sel, VALUE orig)
{
    rb_obj_init_copy(clone, 0, orig);

    VALUE super;
    if (!RCLASS_RUBY(orig)) {
	super = orig;
	rb_warn("cloning class `%s' is not supported, creating a " \
		"subclass instead", rb_class2name(orig));
    }
    else {
	super = RCLASS_SUPER(orig);
    }
    RCLASS_SET_SUPER(clone, super);

    // Copy flags.
    unsigned long version_flag = RCLASS_IS_RUBY_CLASS;
    if ((RCLASS_VERSION(super) & RCLASS_IS_OBJECT_SUBCLASS)
	    == RCLASS_IS_OBJECT_SUBCLASS) {
	version_flag |= RCLASS_IS_OBJECT_SUBCLASS;
    }
    if (RCLASS_MODULE(orig)) {
	version_flag |= RCLASS_IS_MODULE;
    }
    RCLASS_SET_VERSION(clone, version_flag);
    if (!class_isMetaClass((Class)clone)) {
	// Clear type info.
	RCLASS_SET_VERSION(*(Class *)clone, RCLASS_VERSION(*(Class *)clone));
    }

    // Copy methods.
    rb_vm_copy_methods((Class)orig, (Class)clone);
    if (!class_isMetaClass((Class)orig)) {
	rb_vm_copy_methods(*(Class *)orig, *(Class *)clone);
    }

    // Copy ivars.
    CFMutableDictionaryRef orig_dict = rb_class_ivar_dict(orig);
    CFMutableDictionaryRef clone_dict;
    if (orig_dict != NULL) {
	clone_dict = CFDictionaryCreateMutableCopy(NULL, 0, orig_dict);
	rb_class_ivar_set_dict(clone, clone_dict);
	CFMakeCollectable(clone_dict);
    }
    else {
	clone_dict = rb_class_ivar_dict_or_create(clone);
    }

    // Remove the classpath & classid (name) so that they are not
    // copied over the new module / class.
    CFDictionaryRemoveValue(clone_dict, (const void *)id_classpath);
    CFDictionaryRemoveValue(clone_dict, (const void *)id_classid);
    return clone;
}

/* :nodoc: */
VALUE
rb_class_init_copy(VALUE clone, SEL sel, VALUE orig)
{
    if (orig == rb_cBasicObject || orig == rb_cObject) {
	rb_raise(rb_eTypeError, "can't copy the root class");
    }
    if (/* RCLASS_SUPER(clone) ||  FIXME: comment out because Singleton.clone raises a rb_eTypeError */
	(clone == rb_cBasicObject || clone == rb_cObject)) {
	rb_raise(rb_eTypeError, "already initialized class");
    }
    if (RCLASS_SINGLETON(orig)) {
	rb_raise(rb_eTypeError, "can't copy singleton class");
    }
    clone =  rb_mod_init_copy(clone, 0, orig);
    rb_objc_class_sync_version((Class)clone, (Class)orig);
    return clone;
}

VALUE
rb_singleton_class_clone(VALUE obj)
{
    VALUE klass = RBASIC(obj)->klass;
    if (!RCLASS_SINGLETON(klass)) {
	return klass;
    }

    // Create new singleton class.
    VALUE clone = rb_objc_create_class(NULL, RCLASS_SUPER(klass));

    // Copy ivars.
    CFMutableDictionaryRef ivar_dict = rb_class_ivar_dict(klass);
    if (ivar_dict != NULL) {
	CFMutableDictionaryRef cloned_ivar_dict =
	    CFDictionaryCreateMutableCopy(NULL, 0, (CFDictionaryRef)ivar_dict);
	rb_class_ivar_set_dict(clone, cloned_ivar_dict);
	CFMakeCollectable(cloned_ivar_dict);
    }

    // Copy methods.
    rb_vm_copy_methods((Class)klass, (Class)clone);	

    rb_singleton_class_attached(clone, obj);
    if (RCLASS_SUPER(clone) == rb_cRubyObject) {
	long v = RCLASS_VERSION(clone) ^ RCLASS_IS_OBJECT_SUBCLASS;
	RCLASS_SET_VERSION(clone, v);
    }
    RCLASS_SET_VERSION_FLAG(clone, RCLASS_IS_SINGLETON);
    return clone;
}

static void
robj_sclass_finalize_imp(void *rcv, SEL sel)
{
    bool changed = false;
    while (true) {
	VALUE k = *(VALUE *)rcv;
	if (!RCLASS_SINGLETON(k)
		|| !rb_singleton_class_attached_object(k) == (VALUE)rcv) {
	    break;
	}
	VALUE sk = RCLASS_SUPER(k);
	if (sk == 0) {
	    // This can't happen, but we are never sure...
	    break;
	}
	*(VALUE *)rcv = sk;

	rb_vm_dispose_class((Class)k);

	changed = true;
    }

#if 0
    if (changed) {
	objc_msgSend(rcv, selFinalize);
    }
#endif
}

void
rb_singleton_class_promote_for_gc(VALUE klass)
{
    rb_objc_install_method2((Class)klass, "finalize",
	    (IMP)robj_sclass_finalize_imp);
}

void
rb_singleton_class_attached(VALUE klass, VALUE obj)
{
    if (RCLASS_SINGLETON(klass)) {
	// Weak ref.
	VALUE wobj = LONG2NUM((long)obj);
	rb_ivar_set(klass, idAttached, wobj);
	// FIXME commented for now as it breaks some RubySpecs.
	//rb_singleton_class_promote_for_gc(klass);
    }
}

VALUE
rb_singleton_class_attached_object(VALUE klass)
{
    if (RCLASS_SINGLETON(klass)) {
	// Weak ref.
	VALUE obj = rb_ivar_get(klass, idAttached);
	if (FIXNUM_P(obj)) {
	    return (VALUE)NUM2LONG(obj);
	}
    }
    return Qnil;
}

VALUE
rb_make_singleton_class(VALUE super)
{
    VALUE klass = rb_objc_create_class(NULL, super);
    long v = RCLASS_VERSION(klass);
    if (super == rb_cRubyObject) {
	v ^= RCLASS_IS_OBJECT_SUBCLASS;
    }
    v |= RCLASS_IS_RUBY_CLASS;
    v |= RCLASS_IS_SINGLETON;
    RCLASS_SET_VERSION(klass, v);
    return klass;
}

VALUE
rb_make_metaclass(VALUE obj, VALUE super)
{
    VALUE klass;
    if (TYPE(obj) == T_CLASS && RCLASS_SINGLETON(obj)) {
	RBASIC(obj)->klass = rb_cClass;
	klass = rb_cClass;
    }
    else {
	VALUE objk = RBASIC(obj)->klass;
	if (RCLASS_SINGLETON(objk)
		&& rb_singleton_class_attached_object(objk) == obj) {
	    klass = objk;
	}
	else {
	    klass = rb_make_singleton_class(super);
	    RBASIC(obj)->klass = klass;
	    rb_singleton_class_attached(klass, obj);
	}
    }
    return klass;
}

VALUE
rb_define_class_id(ID id, VALUE super)
{
    if (super == 0) {
	super = rb_cObject;
    }
    return rb_class_boot2(super, rb_id2name(id));
}

VALUE
rb_class_inherited(VALUE super, VALUE klass)
{
    if (rb_vm_running()) {
	if (super == 0) {
	    super = rb_cObject;
	}
	return rb_vm_call(super, selInherited, 1, &klass);
    }
    return Qnil;
}

static void
check_class_super(ID id, VALUE klass, VALUE super)
{
    VALUE k = klass;
    do {
	k = RCLASS_SUPER(k);
    }
    while (k != 0 && rb_class_hidden(k));

    if (rb_class_real(k, true) != super) {
	rb_name_error(id, "%s is already defined", rb_id2name(id));
    }
}

VALUE
rb_define_class(const char *name, VALUE super)
{
    VALUE klass;
    ID id;

    id = rb_intern(name);
    if (rb_const_defined(rb_cObject, id)) {
	klass = rb_const_get(rb_cObject, id);
	if (TYPE(klass) != T_CLASS) {
	    rb_raise(rb_eTypeError, "%s is not a class", name);
	}
	check_class_super(id, klass, super);
	return klass;
    }
    if (!super) {
	rb_warn("no super class for `%s', Object assumed", name);
    }
    klass = rb_define_class_id(id, super);
    st_add_direct(rb_class_tbl, id, klass);
    rb_name_class(klass, id);
    rb_const_set(rb_cObject, id, klass);
    rb_class_inherited(super, klass);

    return klass;
}

VALUE
rb_define_class_under(VALUE outer, const char *name, VALUE super)
{
    VALUE klass;
    ID id;

    id = rb_intern(name);
    if (rb_const_defined_at(outer, id)) {
	klass = rb_const_get_at(outer, id);
	if (TYPE(klass) != T_CLASS) {
	    rb_raise(rb_eTypeError, "%s is not a class", name);
	}
	if (RCLASS_RUBY(klass)) {
	    // Only for pure Ruby classes, as Objective-C classes
	    // might be returned from the dynamic resolver.
	    check_class_super(id, klass, super);
	    return klass;
	}
    }
    if (!super) {
	rb_warn("no super class for `%s::%s', Object assumed",
		rb_class2name(outer), name);
    }
    klass = rb_define_class_id(id, super);
    rb_set_class_path(klass, outer, name);
    rb_const_set(outer, id, klass);
    rb_class_inherited(super, klass);

    return klass;
}

VALUE
rb_module_new(void)
{
    return rb_define_module_id(0);
}

VALUE rb_mod_initialize(VALUE, SEL);

VALUE
rb_define_module_id(ID id)
{
    const char *name = id == 0 ? NULL : rb_id2name(id);
    VALUE mdl = rb_objc_alloc_class(name, rb_cModuleObject, T_MODULE,
	    rb_cModule);
    if (rb_mKernel != 0 && id == 0) {
	// Because Module#initialize can accept a block.
	rb_objc_define_method(*(VALUE *)mdl, "initialize",
		rb_mod_initialize, 0);
    }
    return mdl;
}

VALUE
rb_define_module(const char *name)
{
    VALUE module;
    ID id;

    id = rb_intern(name);
    if (rb_const_defined(rb_cObject, id)) {
	module = rb_const_get(rb_cObject, id);
	if (TYPE(module) == T_MODULE)
	    return module;
	rb_raise(rb_eTypeError, "%s is not a module", rb_obj_classname(module));
    }
    module = rb_define_module_id(id);
    st_add_direct(rb_class_tbl, id, module);
    rb_const_set(rb_cObject, id, module);

    return module;
}

VALUE
rb_define_module_under(VALUE outer, const char *name)
{
    ID id = rb_intern(name);
    if (rb_const_defined_at(outer, id)) {
	VALUE module = rb_const_get_at(outer, id);
	if (TYPE(module) == T_MODULE) {
	    return module;
	}
	rb_raise(rb_eTypeError, "%s::%s:%s is not a module",
		 rb_class2name(outer), name, rb_obj_classname(module));
    }
    VALUE module = rb_define_module_id(id);
    rb_const_set(outer, id, module);
    rb_set_class_path(module, outer, name);
    return module;
}

void
rb_include_module2(VALUE klass, VALUE orig_klass, VALUE module, bool check,
	bool add_methods)
{
    if (check) {
	rb_frozen_class_p(klass);
	if (!OBJ_TAINTED(klass)) {
	    rb_secure(4);
	}
	Check_Type(module, T_MODULE);
    }

    // Register the module as included in the class.
    VALUE ary = rb_attr_get(klass, idIncludedModules);
    if (ary == Qnil) {
	ary = rb_ary_new();
	rb_ivar_set(klass, idIncludedModules, ary);
    }
    else {
	if (rb_ary_includes(ary, module)) {
	    return;
	}
    }
    rb_ary_insert(ary, 0, module);

    // Mark the module as included somewhere.
    const long v = RCLASS_VERSION(module) | RCLASS_IS_INCLUDED;
    RCLASS_SET_VERSION(module, v);

    // Register the class as included in the module.
    ary = rb_attr_get(module, idIncludedInClasses);
    if (ary == Qnil) {
	ary = rb_ary_new();
	rb_ivar_set(module, idIncludedInClasses, ary);
    }
    rb_ary_push(ary, klass);

    // Delete the ancestors array if it exists, since we just changed it.
    CFMutableDictionaryRef iv_dict = rb_class_ivar_dict(klass);
    if (iv_dict != NULL) {
	CFDictionaryRemoveValue(iv_dict, (const void *)idAncestors);
    }

    if (add_methods) {
	// Copy methods. If original class has the basic -initialize and if the
	// module has a customized -initialize, we must copy the customized
	// version to the original class too.
	rb_vm_copy_methods((Class)module, (Class)klass);

	// When including into the class Class, also copy the methods to the
	// singleton class of NSObject.
	if (klass == rb_cClass || klass == rb_cModule) {
            rb_vm_copy_methods((Class)module, *(Class *)rb_cNSObject);
	}

	if (orig_klass != 0 && orig_klass != klass) {
	    Method m = class_getInstanceMethod((Class)orig_klass,
		    selInitialize);
	    Method m2 = class_getInstanceMethod((Class)klass, selInitialize);
	    if (m != NULL && m2 != NULL
		    && method_getImplementation(m) == (IMP)rb_objc_init
		    && method_getImplementation(m2) != (IMP)rb_objc_init) {
		rb_vm_copy_method((Class)orig_klass, m2);
	    }
	}
    }

    // Copy the module's class variables.
    rb_class_merge_ivar_dicts(module, klass);
}

void
rb_include_module(VALUE klass, VALUE module)
{
    rb_include_module2(klass, 0, module, true, true);
}

/*
 *  call-seq:
 *     mod.included_modules -> array
 *  
 *  Returns the list of modules included in <i>mod</i>.
 *     
 *     module Mixin
 *     end
 *     
 *     module Outer
 *       include Mixin
 *     end
 *     
 *     Mixin.included_modules   #=> []
 *     Outer.included_modules   #=> [Mixin]
 */

static void
rb_mod_included_modules_nosuper(VALUE mod, VALUE ary)
{
    VALUE inc_mods = rb_attr_get(mod, idIncludedModules);
    if (inc_mods != Qnil) {
	int i, count = RARRAY_LEN(inc_mods);
	for (i = 0; i < count; i++) {
	    VALUE imod = RARRAY_AT(inc_mods, i);
	    rb_ary_push(ary, imod);
	    rb_ary_concat(ary, rb_mod_included_modules(imod));
	}
    }
}

VALUE
rb_mod_included_modules(VALUE mod)
{
    VALUE ary = rb_ary_new();
    bool mod_detected = false;

    for (VALUE p = mod; p != 0; p = RCLASS_SUPER(p)) {
	if (!mod_detected) {
	    if (RCLASS_MODULE(p)) {
		mod_detected = true;
	    }
	}
	else {
	    if (!RCLASS_SINGLETON(p)) {
		break;
	    }
	}
	rb_mod_included_modules_nosuper(p, ary);
    }
    return ary;
}

/*
 *  call-seq:
 *     mod.include?(module)    => true or false
 *  
 *  Returns <code>true</code> if <i>module</i> is included in
 *  <i>mod</i> or one of <i>mod</i>'s ancestors.
 *     
 *     module A
 *     end
 *     class B
 *       include A
 *     end
 *     class C < B
 *     end
 *     B.include?(A)   #=> true
 *     C.include?(A)   #=> true
 *     A.include?(A)   #=> false
 */

VALUE
rb_mod_include_p(VALUE mod, SEL sel, VALUE mod2)
{
    Check_Type(mod2, T_MODULE);
    return rb_ary_includes(rb_mod_included_modules(mod), mod2);
}

/*
 *  call-seq:
 *     mod.ancestors -> array
 *  
 *  Returns a list of modules included in <i>mod</i> (including
 *  <i>mod</i> itself).
 *     
 *     module Mod
 *       include Math
 *       include Comparable
 *     end
 *     
 *     Mod.ancestors    #=> [Mod, Comparable, Math]
 *     Math.ancestors   #=> [Math]
 */

VALUE
rb_mod_ancestors_nocopy(VALUE mod)
{
    VALUE ary = rb_attr_get(mod, idAncestors);
    if (NIL_P(ary)) {
	ary = rb_ary_new();
	for (VALUE p = mod; p != 0; p = RCLASS_SUPER(p)) {
	    rb_ary_push(ary, p);
	    rb_mod_included_modules_nosuper(p, ary);
	}
	rb_ivar_set(mod, idAncestors, ary);	
    }
    return ary;
}

VALUE
rb_mod_ancestors(VALUE mod)
{
    // This method should return a new array without classes that should be
    // ignored.
    VALUE ary = rb_mod_ancestors_nocopy(mod);
    VALUE filtered = rb_ary_new();
    for (int i = 0, count = RARRAY_LEN(ary); i < count; i++) {
	VALUE p = RARRAY_AT(ary, i);
	if (!rb_class_hidden(p)) {
	    rb_ary_push(filtered, p);
	}
    }
    return filtered;
}

static int
ins_methods_push(VALUE name, long type, VALUE ary, long visi)
{
    if (type != -1) {
	bool visible;
	switch (visi) {
	    case NOEX_PRIVATE:
	    case NOEX_PROTECTED:
	    case NOEX_PUBLIC:
		visible = (type == visi);
		break;
	    default:
		visible = (type != NOEX_PRIVATE);
		break;
	}
	if (visible) {
	    rb_ary_push(ary, name);
	}
    }
    return ST_CONTINUE;
}

static int
ins_methods_i(VALUE name, ID type, VALUE ary)
{
    return ins_methods_push(name, type, ary, -1); /* everything but private */
}

static int
ins_methods_prot_i(VALUE name, ID type, VALUE ary)
{
    return ins_methods_push(name, type, ary, NOEX_PROTECTED);
}

static int
ins_methods_priv_i(VALUE name, ID type, VALUE ary)
{
    return ins_methods_push(name, type, ary, NOEX_PRIVATE);
}

static int
ins_methods_pub_i(VALUE name, ID type, VALUE ary)
{
    return ins_methods_push(name, type, ary, NOEX_PUBLIC);
}

static VALUE
class_instance_method_list(int argc, VALUE *argv, VALUE mod, int (*func) (VALUE, ID, VALUE))
{
    VALUE ary;
    VALUE recur, objc_methods;

    ary = rb_ary_new();

    if (argc == 0) {
	recur = Qtrue;
	objc_methods = Qfalse;
    }
    else {
	rb_scan_args(argc, argv, "02", &recur, &objc_methods);
	if (NIL_P(recur)) {
	    recur = Qtrue;
	}
	if (NIL_P(objc_methods)) {
	    objc_methods = Qfalse;
	}
    }

    while (mod != 0) {
	rb_vm_push_methods(ary, mod, RTEST(objc_methods), func);
	if (recur == Qfalse) {
	   break;	   
	}
	mod = RCLASS_SUPER(mod);
    } 

    return ary;
}

/*
 *  call-seq:
 *     mod.instance_methods(include_super=true)   => array
 *  
 *  Returns an array containing the names of public instance methods in
 *  the receiver. For a module, these are the public methods; for a
 *  class, they are the instance (not singleton) methods. With no
 *  argument, or with an argument that is <code>false</code>, the
 *  instance methods in <i>mod</i> are returned, otherwise the methods
 *  in <i>mod</i> and <i>mod</i>'s superclasses are returned.
 *     
 *     module A
 *       def method1()  end
 *     end
 *     class B
 *       def method2()  end
 *     end
 *     class C < B
 *       def method3()  end
 *     end
 *     
 *     A.instance_methods                #=> [:method1]
 *     B.instance_methods(false)         #=> [:method2]
 *     C.instance_methods(false)         #=> [:method3]
 *     C.instance_methods(true).length   #=> 43
 */

VALUE
rb_class_instance_methods(VALUE mod, SEL sel, int argc, VALUE *argv)
{
    return class_instance_method_list(argc, argv, mod, ins_methods_i);
}

/*
 *  call-seq:
 *     mod.protected_instance_methods(include_super=true)   => array
 *  
 *  Returns a list of the protected instance methods defined in
 *  <i>mod</i>. If the optional parameter is not <code>false</code>, the
 *  methods of any ancestors are included.
 */

VALUE
rb_class_protected_instance_methods(VALUE mod, SEL sel, int argc, VALUE *argv)
{
    return class_instance_method_list(argc, argv, mod, ins_methods_prot_i);
}

/*
 *  call-seq:
 *     mod.private_instance_methods(include_super=true)    => array
 *  
 *  Returns a list of the private instance methods defined in
 *  <i>mod</i>. If the optional parameter is not <code>false</code>, the
 *  methods of any ancestors are included.
 *     
 *     module Mod
 *       def method1()  end
 *       private :method1
 *       def method2()  end
 *     end
 *     Mod.instance_methods           #=> [:method2]
 *     Mod.private_instance_methods   #=> [:method1]
 */

VALUE
rb_class_private_instance_methods(VALUE mod, SEL sel, int argc, VALUE *argv)
{
    return class_instance_method_list(argc, argv, mod, ins_methods_priv_i);
}

/*
 *  call-seq:
 *     mod.public_instance_methods(include_super=true)   => array
 *  
 *  Returns a list of the public instance methods defined in <i>mod</i>.
 *  If the optional parameter is not <code>false</code>, the methods of
 *  any ancestors are included.
 */

VALUE
rb_class_public_instance_methods(VALUE mod, SEL sel, int argc, VALUE *argv)
{
    return class_instance_method_list(argc, argv, mod, ins_methods_pub_i);
}

/*
 *  call-seq:
 *     obj.singleton_methods(all=true)    => array
 *  
 *  Returns an array of the names of singleton methods for <i>obj</i>.
 *  If the optional <i>all</i> parameter is true, the list will include
 *  methods in modules included in <i>obj</i>.
 *     
 *     module Other
 *       def three() end
 *     end
 *     
 *     class Single
 *       def Single.four() end
 *     end
 *     
 *     a = Single.new
 *     
 *     def a.one()
 *     end
 *     
 *     class << a
 *       include Other
 *       def two()
 *       end
 *     end
 *     
 *     Single.singleton_methods    #=> [:four]
 *     a.singleton_methods(false)  #=> [:two, :one]
 *     a.singleton_methods         #=> [:two, :one, :three]
 */

VALUE 
rb_obj_singleton_methods(VALUE obj, SEL sel, int argc, VALUE *argv)
{
    VALUE recur, objc_methods, klass, ary;

    if (argc == 0) {
	recur = Qtrue;
	objc_methods = Qfalse;
    }
    else {
	rb_scan_args(argc, argv, "02", &recur, &objc_methods);
    }

    klass = CLASS_OF(obj);
    ary = rb_ary_new();

    do {
	if (RCLASS_SINGLETON(klass)) {
	    rb_vm_push_methods(ary, klass, RTEST(objc_methods), ins_methods_i);
	}
	klass = RCLASS_SUPER(klass);
    }
    while (recur == Qtrue && klass != 0);

    return ary;
}

static void
rb_objc_add_method(VALUE klass, const char *name, void *imp, const int arity,
		   const int noex, bool direct)
{
    if (!direct) {
	assert(name[strlen(name) - 1] != ':');
    }

    NODE *body = rb_vm_cfunc_node_from_imp((Class)klass, arity, (IMP)imp, noex);
    GC_RETAIN(body);

    rb_vm_define_method((Class)klass, rb_vm_name_to_sel(name, arity), (IMP)imp,
	    body, direct);
}

void *rb_vm_generate_mri_stub(void *imp, const int arity);

static void
rb_add_mri_method(VALUE klass, const char *name, void *imp, const int arity,
	const int noex)
{
#if MACRUBY_STATIC
    rb_raise(rb_eRuntimeError, "MRI methods are not supported in MacRuby static");
#else
    imp = rb_vm_generate_mri_stub(imp, arity);
    rb_objc_add_method(klass, name, imp, arity, noex, false);
#endif
}

void
rb_define_alloc_func(VALUE klass, VALUE (*func)(VALUE))
{
    Check_Type(klass, T_CLASS);
    rb_add_mri_method(rb_singleton_class(klass), "alloc", func, 0,
	    NOEX_PUBLIC);
}

void
rb_objc_define_direct_method(VALUE klass, const char *name, void *imp,
			     const int arity)
{
    rb_objc_add_method(klass, name, imp, arity, NOEX_PUBLIC, true);
}

void
rb_objc_define_method(VALUE klass, const char *name, void *imp, const int arity)
{
    rb_objc_add_method(klass, name, imp, arity, NOEX_PUBLIC, false);
}

void
rb_objc_define_private_method(VALUE klass, const char *name, void *imp,
			      const int arity)
{
    rb_objc_add_method(klass, name, imp, arity, NOEX_PRIVATE, false);
}

void
rb_objc_define_module_function(VALUE module, const char *name, void *imp,
			       const int arity)
{
    rb_objc_define_private_method(module, name, imp, arity);
    rb_objc_define_method(*(VALUE *)module, name, imp, arity);
}

void
rb_undef_alloc_func(VALUE klass)
{
    // TODO
#if 0
    Check_Type(klass, T_CLASS);
    rb_add_mri_method(rb_singleton_class(klass), ID_ALLOCATOR, 0, NOEX_UNDEF);
#endif
}

void
rb_define_method_id(VALUE klass, ID name, VALUE (*func)(ANYARGS), int argc)
{
    rb_add_mri_method(klass, rb_id2name(name), func, argc, NOEX_PUBLIC);
}

void
rb_define_method(VALUE klass, const char *name, VALUE (*func)(ANYARGS),
	int argc)
{
    rb_add_mri_method(klass, name, func, argc, NOEX_PUBLIC);
}

void
rb_define_protected_method(VALUE klass, const char *name,
	VALUE (*func)(ANYARGS), int argc)
{
    rb_add_mri_method(klass, name, func, argc, NOEX_PROTECTED);
}

void
rb_define_private_method(VALUE klass, const char *name,
	VALUE (*func)(ANYARGS), int argc)
{
    rb_add_mri_method(klass, name, func, argc, NOEX_PRIVATE);
}

void
rb_undef_method(VALUE klass, const char *name)
{
    rb_vm_undef_method((Class)klass, rb_intern(name), false);
}

#define SPECIAL_SINGLETON(x,c) do {\
    if (obj == (x)) {\
	return c;\
    }\
} while (0)

VALUE
rb_singleton_class(VALUE obj)
{
    if (FIXNUM_P(obj) || SYMBOL_P(obj) || FIXFLOAT_P(obj)) {
	rb_raise(rb_eTypeError, "can't define singleton");
    }
    if (rb_special_const_p(obj)) {
	SPECIAL_SINGLETON(Qnil, rb_cNilClass);
	SPECIAL_SINGLETON(Qfalse, rb_cFalseClass);
	SPECIAL_SINGLETON(Qtrue, rb_cTrueClass);
	rb_bug("unknown immediate %ld", obj);
    }

    VALUE klass;
    switch (TYPE(obj)) {
	case T_CLASS:
	case T_MODULE:
	    // FIXME we should really create a new metaclass here.
	    klass = *(VALUE *)obj;
	    break;

	default:
	    klass = rb_make_metaclass(obj, RBASIC(obj)->klass);
	    break;
    }

    OBJ_INFECT(klass, obj);
    if (OBJ_FROZEN(obj)) {
	OBJ_FREEZE(klass);
    }

    return klass;
}

void
rb_define_singleton_method(VALUE obj, const char *name,
	VALUE (*func)(ANYARGS), int argc)
{
    rb_define_method(rb_singleton_class(obj), name, func, argc);
}

void
rb_define_module_function(VALUE module, const char *name,
	VALUE (*func)(ANYARGS), int argc)
{
    rb_define_private_method(module, name, func, argc);
    rb_define_singleton_method(module, name, func, argc);
}

void
rb_define_global_function(const char *name, VALUE (*func)(ANYARGS), int argc)
{
    rb_define_module_function(rb_mKernel, name, func, argc);
}

void
rb_define_alias(VALUE klass, const char *name1, const char *name2)
{
    rb_alias(klass, rb_intern(name1), rb_intern(name2));
}

void
rb_define_attr(VALUE klass, const char *name, int read, int write)
{
    rb_attr(klass, rb_intern(name), read, write, Qfalse);
}

int
rb_scan_args(int argc, const VALUE *argv, const char *fmt, ...)
{
    int n, i = 0;
    const char *p = fmt;
    VALUE *var;
    va_list vargs;

    va_start(vargs, fmt);

    if (*p == '*') goto rest_arg;

    if (ISDIGIT(*p)) {
	n = *p - '0';
	if (n > argc) {
	    va_end(vargs);
	    rb_raise(rb_eArgError, "wrong number of arguments (%d for %d)", argc, n);
	}
	for (i=0; i<n; i++) {
	    var = va_arg(vargs, VALUE*);
	    if (var) *var = argv[i];
	}
	p++;
    }
    else {
	va_end(vargs);
	goto error;
    }

    if (ISDIGIT(*p)) {
	n = i + *p - '0';
	for (; i<n; i++) {
	    var = va_arg(vargs, VALUE*);
	    if (argc > i) {
		if (var) *var = argv[i];
	    }
	    else {
		if (var) *var = Qnil;
	    }
	}
	p++;
    }

    if (*p == '*') {
      rest_arg:
	var = va_arg(vargs, VALUE*);
	if (argc > i) {
	    if (var) *var = rb_ary_new4(argc-i, argv+i);
	    i = argc;
	}
	else {
	    if (var) *var = rb_ary_new();
	}
	p++;
    }

    if (*p == '&') {
	var = va_arg(vargs, VALUE*);
	if (rb_block_given_p()) {
	    *var = rb_block_proc();
	}
	else {
	    *var = Qnil;
	}
	p++;
    }
    va_end(vargs);

    if (*p != '\0') {
	goto error;
    }

    if (argc > i) {
	rb_raise(rb_eArgError, "wrong number of arguments (%d for %d)", argc, i);
    }

    return argc;

  error:
    rb_fatal("bad scan arg format: %s", fmt);
    return 0;
}

rb_class_flags_cache_t *rb_class_flags;

void
Init_PreClass(void)
{
    rb_class_flags = (rb_class_flags_cache_t *)calloc(
	    CLASS_FLAGS_CACHE_SIZE, sizeof(rb_class_flags_cache_t));
    assert(rb_class_flags != NULL);
}

void
Init_Class(void)
{
#if 0
    rb_cSClassFinalizer = rb_define_class("__SClassFinalizer", rb_cObject);
    sclass_finalize_imp_super = rb_objc_install_method2(
	    (Class)rb_cSClassFinalizer, "finalize", (IMP)sclass_finalize_imp);
#endif
}

static int
foundation_type(Class k)
{
    Class tmp = k;
    do {
	if (tmp == (Class)rb_cNSString) {
	    return T_STRING;
	}
	if (tmp == (Class)rb_cNSArray) {
	    return T_ARRAY;
	}
	if (tmp == (Class)rb_cNSHash) {
	    return T_HASH;
	}
	tmp = class_getSuperclass(tmp);
    }
    while (tmp != NULL);
    return 0;
}

int
rb_objc_type(VALUE obj)
{
    Class k = *(Class *)obj;

    if (k != NULL) {
	unsigned long mask = rb_class_get_mask(k);
	int type = mask & 0xff;
	if (type == 0) {
	    // Type is not available, let's compute it. 	    
	    if (k == (Class)rb_cSymbol) {
		type = T_SYMBOL;
		goto done;
	    }
	    if ((type = foundation_type(k)) != 0) {
		goto done;
	    }
	    if (RCLASS_META(k)) {
		if (RCLASS_MODULE(obj)) {
		    type = T_MODULE;
		    goto done;
		}
		else {
		    type = T_CLASS;
		    goto done;
		}
	    }
	    const unsigned long flags = mask >> RCLASS_MASK_TYPE_SHIFT;
	    if ((flags & RCLASS_IS_OBJECT_SUBCLASS)
		    != RCLASS_IS_OBJECT_SUBCLASS) {
		type = T_NATIVE;
		goto done;
	    }
	    type = BUILTIN_TYPE(obj);

done:
	    assert(type != 0);
	    mask |= type;
	    rb_class_set_mask(k, mask); 
	}	
	return type;
    }
    return BUILTIN_TYPE(obj);
}

// Note: not returning 'bool' because the function is exported in ruby.h
// and MRI does not include stdbool.
int
rb_obj_is_native(VALUE obj)
{
    Class k = *(Class *)obj;
    return k != NULL && (RCLASS_VERSION(k) & RCLASS_IS_OBJECT_SUBCLASS)
	!= RCLASS_IS_OBJECT_SUBCLASS;
}

VALUE
rb_class_real(VALUE cl, bool hide_builtin_foundation_classes)
{
    if (cl == 0) {
        return 0;
    }
    if (RCLASS_META(cl)) {
	return RCLASS_MODULE(cl) ? rb_cModule : rb_cClass;
    }
    while (RCLASS_SINGLETON(cl)) {
	cl = RCLASS_SUPER(cl);
    }
    if (hide_builtin_foundation_classes && !RCLASS_RUBY(cl)) {
	switch (foundation_type((Class)cl)) {
	    case T_STRING:
		return rb_cRubyString;
	    case T_ARRAY:
		return rb_cRubyArray;
	    case T_HASH:
		return rb_cRubyHash;
	}
    }
    return cl;
}
