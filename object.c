/*
 * MacRuby implementation of Ruby 1.9's object.c.
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
#include "ruby/st.h"
#include "ruby/util.h"
#include "ruby/node.h"
#include "id.h"
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <math.h>
#include <float.h>
#include "objc.h"
#include "vm.h"
#include "encoding.h"
#include "array.h"
#include "hash.h"
#include "class.h"

VALUE rb_cBasicObject;
VALUE rb_mKernel;
VALUE rb_cNSObject;
VALUE rb_cObject;
VALUE rb_cRubyObject;
VALUE rb_cModule;
VALUE rb_cModuleObject;
VALUE rb_cClass;
VALUE rb_cData;

VALUE rb_cNilClass;
VALUE rb_cTrueClass;
VALUE rb_cFalseClass;

static ID id_eq, id_match, id_inspect, id_init_copy;

static SEL eqlSel = 0;

VALUE
rb_send_dup(VALUE obj)
{
    return rb_vm_call(obj, selDup, 0, NULL);
}

/*
 *  call-seq:
 *     obj === other   => true or false
 *  
 *  Case Equality---For class <code>Object</code>, effectively the same
 *  as calling  <code>#==</code>, but typically overridden by descendents
 *  to provide meaningful semantics in <code>case</code> statements.
 */

VALUE
rb_equal(VALUE obj1, VALUE obj2)
{
    if (obj1 == obj2) {
	return Qtrue;
    }
    VALUE result = rb_vm_call(obj1, selEq, 1, &obj2);
    if (RTEST(result)) {
	return Qtrue;
    }
    return Qfalse;
}

static VALUE
rb_equal_imp(VALUE obj1, SEL sel, VALUE obj2)
{
    return rb_equal(obj1, obj2);
}

int
rb_eql(VALUE obj1, VALUE obj2)
{
    VALUE obj1_class = CLASS_OF(obj1);

    if (obj1_class == rb_cFixnum || obj1_class == rb_cFloat
	    || obj1_class == rb_cSymbol) {
	return obj1 == obj2;
    }

    VALUE obj2_class = CLASS_OF(obj2);
    if (obj1_class == obj2_class) {
	if (obj1_class == rb_cRubyString) {
	    return rstr_compare(RSTR(obj1), RSTR(obj2)) == 0;
	}
	if (obj1_class == rb_cRubyArray) {
	    return rary_eql_fast(RARY(obj1), RARY(obj2));
	}
    }

    return RTEST(rb_vm_call(obj1, eqlSel, 1, &obj2));
}

/*
 *  call-seq:
 *     obj == other        => true or false
 *     obj.equal?(other)   => true or false
 *     obj.eql?(other)     => true or false
 *  
 *  Equality---At the <code>Object</code> level, <code>==</code> returns
 *  <code>true</code> only if <i>obj</i> and <i>other</i> are the
 *  same object. Typically, this method is overridden in descendent
 *  classes to provide class-specific meaning.
 *
 *  Unlike <code>==</code>, the <code>equal?</code> method should never be
 *  overridden by subclasses: it is used to determine object identity
 *  (that is, <code>a.equal?(b)</code> iff <code>a</code> is the same
 *  object as <code>b</code>).
 *
 *  The <code>eql?</code> method returns <code>true</code> if
 *  <i>obj</i> and <i>anObject</i> have the same value. Used by
 *  <code>Hash</code> to test members for equality.  For objects of
 *  class <code>Object</code>, <code>eql?</code> is synonymous with
 *  <code>==</code>. Subclasses normally continue this tradition, but
 *  there are exceptions. <code>Numeric</code> types, for example,
 *  perform type conversion across <code>==</code>, but not across
 *  <code>eql?</code>, so:
 *     
 *     1 == 1.0     #=> true
 *     1.eql? 1.0   #=> false
 */

static VALUE
rb_obj_equal(VALUE obj1, SEL sel, VALUE obj2)
{
    return obj1 == obj2 ? Qtrue : Qfalse;
}

static VALUE
rb_nsobj_equal(VALUE obj1, SEL sel, VALUE obj2)
{
    return rb_objc_isEqual(obj1, obj2) ? Qtrue : Qfalse;
}

static VALUE
rb_obj_hash(VALUE obj, SEL sel)
{
    VALUE oid = rb_obj_id(obj, sel);
    st_index_t h = rb_hash_end(rb_hash_start(NUM2LONG(oid)));
    return LONG2FIX(h);
}

/*
 *  call-seq:
 *     !obj    => true or false
 *
 *  Boolean negate.
 */

static VALUE
rb_obj_not(VALUE obj, SEL sel)
{
    return RTEST(obj) ? Qfalse : Qtrue;
}

/*
 *  call-seq:
 *     obj != other        => true or false
 *
 *  Returns true if two objects are not-equal, otherwise false.
 */

static VALUE
rb_obj_not_equal(VALUE obj1, SEL sel, VALUE obj2)
{
    VALUE result = rb_vm_call(obj1, selEq, 1, &obj2);
    return RTEST(result) ? Qfalse : Qtrue;
}

/*
 *  call-seq:
 *     obj.class    => class
 *  
 *  Returns the class of <i>obj</i>, now preferred over
 *  <code>Object#type</code>, as an object's type in Ruby is only
 *  loosely tied to that object's class. This method must always be
 *  called with an explicit receiver, as <code>class</code> is also a
 *  reserved word in Ruby.
 *     
 *     1.class      #=> Fixnum
 *     self.class   #=> Object
 */

VALUE
rb_obj_class(VALUE obj)
{
    return rb_class_real(CLASS_OF(obj), true);
}

/*
 *  call-seq:
 *     obj.singleton_class    -> class
 *
 *  Returns the singleton class of <i>obj</i>.  This method creates
 *  a new singleton class if <i>obj</i> does not have it.
 *
 *  If <i>obj</i> is <code>nil</code>, <code>true</code>, or
 *  <code>false</code>, it returns NilClass, TrueClass, or FalseClass,
 *  respectively.
 *  If <i>obj</i> is a Fixnum or a Symbol, it raises a TypeError.
 *
 *     Object.new.singleton_class  #=> #<Class:#<Object:0xb7ce1e24>>
 *     String.singleton_class      #=> #<Class:String>
 *     nil.singleton_class         #=> NilClass
 */

static VALUE
rb_obj_singleton_class(VALUE obj, SEL sel)
{
    return rb_singleton_class(obj);
}


static void
init_copy(VALUE dest, VALUE obj)
{
    if (OBJ_FROZEN(dest)) {
        rb_raise(rb_eTypeError, "[bug] frozen object (%s) allocated",
		rb_obj_classname(dest));
    }

    rb_copy_generic_ivar(dest, obj);
    rb_gc_copy_finalizer(dest, obj);

    switch (TYPE(obj)) {
	case T_OBJECT:
	    if (ROBJECT(obj)->num_slots > 0) {
		if (ROBJECT(dest)->num_slots < ROBJECT(obj)->num_slots) {
		    rb_vm_regrow_robject_slots(ROBJECT(dest),
			    ROBJECT(obj)->num_slots);
		}
		for (int i = 0; i < ROBJECT(obj)->num_slots; i++) {
		    rb_object_ivar_slot_t *dest_sl = &ROBJECT(dest)->slots[i];
		    rb_object_ivar_slot_t *orig_sl = &ROBJECT(obj)->slots[i];
		    dest_sl->name = orig_sl->name;
		    GC_WB(&dest_sl->value, orig_sl->value);
		}
	    }
	    ROBJECT(dest)->num_slots = ROBJECT(obj)->num_slots;
	    break;
    }
}

void
rb_obj_invoke_initialize_copy(VALUE dest, VALUE obj)
{
    init_copy(dest, obj);
    rb_vm_call(dest, selInitializeCopy, 1, &obj);
}

/*
 *  call-seq:
 *     obj.clone -> an_object
 *  
 *  Produces a shallow copy of <i>obj</i>---the instance variables of
 *  <i>obj</i> are copied, but not the objects they reference. Copies
 *  the frozen and tainted state of <i>obj</i>. See also the discussion
 *  under <code>Object#dup</code>.
 *     
 *     class Klass
 *        attr_accessor :str
 *     end
 *     s1 = Klass.new      #=> #<Klass:0x401b3a38>
 *     s1.str = "Hello"    #=> "Hello"
 *     s2 = s1.clone       #=> #<Klass:0x401b3998 @str="Hello">
 *     s2.str[1,4] = "i"   #=> "i"
 *     s1.inspect          #=> "#<Klass:0x401b3a38 @str=\"Hi\">"
 *     s2.inspect          #=> "#<Klass:0x401b3998 @str=\"Hi\">"
 *
 *  This method may have class-specific behavior.  If so, that
 *  behavior will be documented under the #+initialize_copy+ method of
 *  the class.
 */

static VALUE rb_class_s_alloc(VALUE, SEL);

static VALUE
rb_obj_clone_imp(VALUE obj, SEL sel)
{
    if (rb_special_const_p(obj) || TYPE(obj) == T_SYMBOL) {
        rb_raise(rb_eTypeError, "can't clone %s", rb_obj_classname(obj));
    }

    // #alloc
    VALUE clone;
    switch (TYPE(obj)) {
	case T_CLASS:
	case T_MODULE:
	    clone = rb_class_s_alloc(Qnil, 0);
	    break;

	default:
	    clone = rb_obj_alloc(rb_obj_class(obj));
	    do {
		VALUE klass = rb_singleton_class_clone(obj);
		if (klass != RBASIC(obj)->klass) {
		    RBASIC(clone)->klass = klass;
		}
	    }
	    while (0);
	    break;
    }

    // #initialize_copy
    init_copy(clone, obj);
    rb_vm_call(clone, selInitializeClone, 1, &obj);

    // Copy flags.
    OBJ_INFECT(clone, obj);
    if (OBJ_FROZEN(obj)) {
	OBJ_FREEZE(clone);
    }
    return clone;
}

VALUE
rb_obj_clone(VALUE obj)
{
    return rb_obj_clone_imp(obj, 0);
}

/*
 *  call-seq:
 *     obj.dup -> an_object
 *  
 *  Produces a shallow copy of <i>obj</i>---the instance variables of
 *  <i>obj</i> are copied, but not the objects they reference.
 *  <code>dup</code> copies the tainted state of <i>obj</i>. See also
 *  the discussion under <code>Object#clone</code>. In general,
 *  <code>clone</code> and <code>dup</code> may have different semantics
 *  in descendent classes. While <code>clone</code> is used to duplicate
 *  an object, including its internal state, <code>dup</code> typically
 *  uses the class of the descendent object to create the new instance.
 *
 *  This method may have class-specific behavior.  If so, that
 *  behavior will be documented under the #+initialize_copy+ method of
 *  the class.
 */

VALUE
rb_obj_dup(VALUE obj)
{
    VALUE dup;

    if (rb_special_const_p(obj) || TYPE(obj) == T_SYMBOL) {
        rb_raise(rb_eTypeError, "can't dup %s", rb_obj_classname(obj));
    }
    dup = rb_obj_alloc(rb_obj_class(obj));
    init_copy(dup, obj);
    rb_vm_call(dup, selInitializeDup, 1, &obj);
    OBJ_INFECT(dup, obj);

    return dup;
}

static VALUE
rb_obj_type(VALUE obj)
{
    return LONG2FIX(TYPE(obj));
}

/* :nodoc: */
VALUE
rb_obj_init_copy(VALUE obj, SEL sel, VALUE orig)
{
    if (obj == orig) {
	return obj;
    }
    rb_check_frozen(obj);
    if (rb_obj_class(obj) != rb_obj_class(orig)) {
	rb_raise(rb_eTypeError,
		"initialize_copy should take same class object");
    }
    return obj;
}

/* :nodoc: */
VALUE
rb_obj_init_dup_clone(VALUE obj, SEL sel, VALUE orig)
{
    rb_vm_call(obj, selInitializeCopy, 1, &orig);
    return obj;
}

static VALUE
rb_nsobj_dup(VALUE obj, VALUE sel)
{
    return rb_obj_dup(obj);
}

/*
 *  call-seq:
 *     obj.to_s    => string
 *  
 *  Returns a string representing <i>obj</i>. The default
 *  <code>to_s</code> prints the object's class and an encoding of the
 *  object id. As a special case, the top-level object that is the
 *  initial execution context of Ruby programs returns ``main.''
 */

VALUE
rb_any_to_string(VALUE obj, SEL sel)
{
    const char *cname = rb_obj_classname(obj);
    VALUE str = rb_sprintf("#<%s:%p>", cname, (void*)obj);
    OBJ_INFECT(str, obj);
    return str;
}

VALUE
rb_any_to_s(VALUE obj)
{
    return rb_any_to_string(obj, 0);
}

VALUE
rb_inspect(VALUE obj)
{
    return rb_obj_as_string(rb_funcall(obj, id_inspect, 0, 0));
}

static int
inspect_i(ID id, VALUE value, VALUE str)
{
    if (!rb_is_instance_id(id)) {
	return ST_CONTINUE;
    }

    const char *cstr = RSTRING_PTR(str);
    if (cstr[0] == '-') { /* first element */
	rb_str_update(str, 0, 0, rb_str_new2("#"));
	rb_str_cat2(str, " ");
    }
    else {
	rb_str_cat2(str, " ");
    }

    const char *ivname = rb_id2name(id);
    rb_str_cat2(str, ivname);
    rb_str_cat2(str, "=");
    VALUE str2 = rb_inspect(value);
    rb_str_append(str, str2);
    OBJ_INFECT(str, str2);

    return ST_CONTINUE;
}

static VALUE
inspect_obj(VALUE obj, VALUE str, int recur)
{
    if (recur) {
	rb_str_cat2(str, " ...");
    }
    else {
	rb_ivar_foreach(obj, inspect_i, str);
    }
    rb_str_cat2(str, ">");
    OBJ_INFECT(str, obj);

    return str;
}

/*
 *  call-seq:
 *     obj.inspect   => string
 *  
 *  Returns a string containing a human-readable representation of
 *  <i>obj</i>. If not overridden, uses the <code>to_s</code> method to
 *  generate the string.
 *     
 *     [ 1, 2, 3..4, 'five' ].inspect   #=> "[1, 2, 3..4, \"five\"]"
 *     Time.new.inspect                 #=> "2008-03-08 19:43:39 +0900"
 */


static VALUE
rb_obj_inspect(VALUE obj, SEL sel)
{
    if (TYPE(obj) == T_OBJECT) {
	for (int i = 0; i < ROBJECT(obj)->num_slots; i++) {
	    if (ROBJECT(obj)->slots[i].value != Qundef) {
		// There is at least an ivar.
		const char *c = rb_obj_classname(obj);
		VALUE str = rb_sprintf("#<%s:%p", c, (void*)obj);
		return rb_exec_recursive(inspect_obj, obj, str);
	    }
	}
    }
    else if (!SPECIAL_CONST_P(obj) &&
	     !(RCLASS_RUBY(obj) || RCLASS_RUBY(RBASIC(obj)->klass))) {
	return rb_str_new3(rb_vm_call(obj, selDescription, 0, 0));
    }
    return rb_funcall(obj, rb_intern("to_s"), 0, 0);
}

/*
 *  call-seq:
 *     obj.instance_of?(class)    => true or false
 *  
 *  Returns <code>true</code> if <i>obj</i> is an instance of the given
 *  class. See also <code>Object#kind_of?</code>.
 */

VALUE
rb_obj_is_instance_of(VALUE obj, VALUE c)
{
    switch (TYPE(c)) {
      case T_MODULE:
      case T_CLASS:
      case T_ICLASS:
	break;
      default:
	rb_raise(rb_eTypeError, "class or module required");
    }

    if (rb_obj_class(obj) == c) {
	return Qtrue;
    }
    return Qfalse;
}

static VALUE
rb_obj_is_instance_of_imp(VALUE obj, SEL sel, VALUE c)
{
    return rb_obj_is_instance_of(obj, c);
}

/*
 *  call-seq:
 *     obj.is_a?(class)       => true or false
 *     obj.kind_of?(class)    => true or false
 *  
 *  Returns <code>true</code> if <i>class</i> is the class of
 *  <i>obj</i>, or if <i>class</i> is one of the superclasses of
 *  <i>obj</i> or modules included in <i>obj</i>.
 *     
 *     module M;    end
 *     class A
 *       include M
 *     end
 *     class B < A; end
 *     class C < B; end
 *     b = B.new
 *     b.instance_of? A   #=> false
 *     b.instance_of? B   #=> true
 *     b.instance_of? C   #=> false
 *     b.instance_of? M   #=> false
 *     b.kind_of? A       #=> true
 *     b.kind_of? B       #=> true
 *     b.kind_of? C       #=> false
 *     b.kind_of? M       #=> true
 */

VALUE
rb_obj_is_kind_of(VALUE obj, VALUE c)
{
    VALUE cl = CLASS_OF(obj);
    bool is_module = false;

    switch (TYPE(c)) {
      case T_MODULE:
	  is_module = true;
	  // fall through
      case T_CLASS:
      case T_ICLASS:
	  break;

      default:
	  rb_raise(rb_eTypeError, "class or module required");
    }

    const int t = TYPE(obj);
    if (c == rb_cRubyString && t == T_STRING) {
	return Qtrue;
    }
    if (c == rb_cRubyArray && t == T_ARRAY) {
	return Qtrue;
    }
    if (c == rb_cRubyHash && t == T_HASH) {
	return Qtrue;
    }

    if (RCLASS_META(cl)) {
	is_module = true;
    }

    while (cl != 0) {
	if (cl == c) {
	    return Qtrue;
	}
	if (is_module) {
	    VALUE ary = rb_attr_get(cl, idIncludedModules);
	    if (ary != Qnil) {
		for (int i = 0, count = RARRAY_LEN(ary); i < count; i++) {
		    VALUE imod = RARRAY_AT(ary, i);
		    if (imod == c) {
			return Qtrue;
		    }
		}
	    }
	}
	cl = RCLASS_SUPER(cl);
    }
    return Qfalse;
}

static VALUE
rb_obj_is_kind_of_imp(VALUE obj, SEL sel, VALUE c)
{
    return rb_obj_is_kind_of(obj, c);
}

/*
 *  call-seq:
 *     obj.tap{|x|...}    => obj
 *  
 *  Yields <code>x</code> to the block, and then returns <code>x</code>.
 *  The primary purpose of this method is to "tap into" a method chain,
 *  in order to perform operations on intermediate results within the chain.
 *
 *	(1..10)                .tap {|x| puts "original: #{x.inspect}"}
 *	  .to_a                .tap {|x| puts "array: #{x.inspect}"}
 *	  .select {|x| x%2==0} .tap {|x| puts "evens: #{x.inspect}"}
 *	  .map { |x| x*x }     .tap {|x| puts "squares: #{x.inspect}"}
 *
 */

static VALUE
rb_obj_tap(VALUE obj, SEL sel)
{
    rb_yield(obj);
    return obj;
}


/*
 * Document-method: inherited
 *
 * call-seq:
 *    inherited(subclass)
 *
 * Callback invoked whenever a subclass of the current class is created.
 *
 * Example:
 *
 *    class Foo
 *       def self.inherited(subclass)
 *          puts "New subclass: #{subclass}"
 *       end
 *    end
 *
 *    class Bar < Foo
 *    end
 *
 *    class Baz < Bar
 *    end
 *
 * produces:
 *
 *    New subclass: Bar
 *    New subclass: Baz
 */

/*
 * Document-method: singleton_method_added
 *
 *  call-seq:
 *     singleton_method_added(symbol)
 *  
 *  Invoked as a callback whenever a singleton method is added to the
 *  receiver.
 *     
 *     module Chatty
 *       def Chatty.singleton_method_added(id)
 *         puts "Adding #{id.id2name}"
 *       end
 *       def self.one()     end
 *       def two()          end
 *       def Chatty.three() end
 *     end
 *     
 *  <em>produces:</em>
 *     
 *     Adding singleton_method_added
 *     Adding one
 *     Adding three
 *     
 */

/*
 * Document-method: singleton_method_removed
 *
 *  call-seq:
 *     singleton_method_removed(symbol)
 *  
 *  Invoked as a callback whenever a singleton method is removed from
 *  the receiver.
 *     
 *     module Chatty
 *       def Chatty.singleton_method_removed(id)
 *         puts "Removing #{id.id2name}"
 *       end
 *       def self.one()     end
 *       def two()          end
 *       def Chatty.three() end
 *       class <<self
 *         remove_method :three
 *         remove_method :one
 *       end
 *     end
 *     
 *  <em>produces:</em>
 *     
 *     Removing three
 *     Removing one
 */

/*
 * Document-method: singleton_method_undefined
 *
 *  call-seq:
 *     singleton_method_undefined(symbol)
 *  
 *  Invoked as a callback whenever a singleton method is undefined in
 *  the receiver.
 *     
 *     module Chatty
 *       def Chatty.singleton_method_undefined(id)
 *         puts "Undefining #{id.id2name}"
 *       end
 *       def Chatty.one()   end
 *       class << self
 *          undef_method(:one)
 *       end
 *     end
 *     
 *  <em>produces:</em>
 *     
 *     Undefining one
 */


/*
 * Document-method: included
 *
 * call-seq:
 *    included( othermod )
 *
 * Callback invoked whenever the receiver is included in another
 * module or class. This should be used in preference to
 * <tt>Module.append_features</tt> if your code wants to perform some
 * action when a module is included in another.
 *
 *        module A
 *          def A.included(mod)
 *            puts "#{self} included in #{mod}"
 *          end
 *        end
 *        module Enumerable
 *          include A
 *        end
 */


/*
 * Not documented
 */

static VALUE
rb_obj_dummy(VALUE self, SEL sel)
{
    return Qnil;
}

static VALUE
rb_obj_dummy2(VALUE self, SEL sel, VALUE other)
{
    return Qnil;
}

// In order to mimic 1.9, we allow Floats to be tainted and untrusted, even
// though they are technically immediates.
static st_table *immediate_flags_tbl = 0;


/*
 *  call-seq:
 *     obj.tainted?    => true or false
 *  
 *  Returns <code>true</code> if the object is tainted.
 */

static VALUE
rb_obj_tainted_p(VALUE obj, SEL sel)
{
    if (SPECIAL_CONST_P(obj)) {
	if (immediate_flags_tbl && FIXFLOAT_P(obj)) {
	    VALUE flags = 0;
	    if (st_lookup(immediate_flags_tbl, obj, &flags) && (flags & FL_TAINT)) {
		return Qtrue;
	    }
	}
	return Qfalse;
    }
    else if (NATIVE(obj)) {
	switch (TYPE(obj)) {
	    case T_SYMBOL:
		return Qfalse;
	    case T_ARRAY:
		if (rb_klass_is_rary(*(VALUE *)obj)) {
		    return RBASIC(obj)->flags & FL_TAINT ? Qtrue : Qfalse;
		}
		// fall through
	    case T_STRING:
		if (rb_klass_is_rstr(*(VALUE *)obj)) {
		    return RBASIC(obj)->flags & FL_TAINT ? Qtrue : Qfalse;
		}
		// fall through
	    case T_HASH:
		if (rb_klass_is_rhash(*(VALUE *)obj)) {
		    return RBASIC(obj)->flags & FL_TAINT ? Qtrue : Qfalse;
		}
	    default:
		return rb_objc_flag_check((const void *)obj, FL_TAINT)
		    ? Qtrue : Qfalse;
	}
    }
    if (FL_TEST(obj, FL_TAINT)) {
	return Qtrue;
    }
    return Qfalse;
}

VALUE
rb_obj_tainted(VALUE obj)
{
    return rb_obj_tainted_p(obj, 0);
}

/*
 *  call-seq:
 *     obj.taint -> obj
 *  
 *  Marks <i>obj</i> as tainted---if the <code>$SAFE</code> level is
 *  set appropriately, many method calls which might alter the running
 *  programs environment will refuse to accept tainted strings.
 */

static VALUE
rb_obj_taint_m(VALUE obj, SEL sel)
{
    rb_secure(4);
    if (SPECIAL_CONST_P(obj)) {
	if (!FIXFLOAT_P(obj)) {
	    return obj;
	}
	else if (!immediate_flags_tbl) {
	    immediate_flags_tbl = st_init_numtable();
	    GC_RETAIN(immediate_flags_tbl);
	    st_insert(immediate_flags_tbl, obj, (st_data_t)FL_TAINT);
	}
	else {
	    VALUE flags = 0;
	    st_lookup(immediate_flags_tbl, obj, &flags);
	    flags |= FL_TAINT;
	    st_insert(immediate_flags_tbl, obj, (st_data_t)flags);
	}
	return obj;
    }
    else if (NATIVE(obj)) {
	switch (TYPE(obj)) {
	    case T_SYMBOL:
		return obj;
	    case T_ARRAY:
		if (rb_klass_is_rary(*(VALUE *)obj)) {
		    RBASIC(obj)->flags |= FL_TAINT;
		    break;
		}
		// fall through
	    case T_STRING:
		if (rb_klass_is_rstr(*(VALUE *)obj)) {
		    RBASIC(obj)->flags |= FL_TAINT;
		    break;
		}
		// fall through
	    case T_HASH:
		if (rb_klass_is_rhash(*(VALUE *)obj)) {
		    RBASIC(obj)->flags |= FL_TAINT;
		    break;
		}
	    default:
		rb_objc_flag_set((const void *)obj, FL_TAINT, true);
	}
	return obj;
    }
    if (!OBJ_TAINTED(obj)) {
	if (OBJ_FROZEN(obj)) {
	    rb_error_frozen("object");
	}
	FL_SET(obj, FL_TAINT);
    }
    return obj;
}

VALUE 
rb_obj_taint(VALUE obj)
{
    return rb_obj_taint_m(obj, 0);
}


/*
 *  call-seq:
 *     obj.untaint    => obj
 *  
 *  Removes the taint from <i>obj</i>.
 */

static VALUE
rb_obj_untaint_m(VALUE obj, SEL sel)
{
    rb_secure(3);
    if (SPECIAL_CONST_P(obj)) {
	if (immediate_flags_tbl && FIXFLOAT_P(obj)) {
	    VALUE flags = 0;
	    st_lookup(immediate_flags_tbl, obj, &flags);
	    flags &= ~FL_TAINT;
	    st_insert(immediate_flags_tbl, obj, (st_data_t)flags);
	}
	return obj;
    }
    else if (NATIVE(obj)) {
	switch (TYPE(obj)) {
	    case T_SYMBOL:
		return obj;
	    case T_ARRAY:
		if (rb_klass_is_rary(*(VALUE *)obj)) {
		    RBASIC(obj)->flags &= ~FL_TAINT;
		    break;
		}	
		// fall through
	    case T_STRING:
		if (rb_klass_is_rstr(*(VALUE *)obj)) {
		    RBASIC(obj)->flags &= ~FL_TAINT;
		    break;
		}
		// fall through
	    case T_HASH:
		if (rb_klass_is_rhash(*(VALUE *)obj)) {
		    RBASIC(obj)->flags &= ~FL_TAINT;
		    break;
		}
	    default:
		rb_objc_flag_set((const void *)obj, FL_TAINT, false);
	}
	return obj;
    }
    if (OBJ_TAINTED(obj)) {
	if (OBJ_FROZEN(obj)) {
	    rb_error_frozen("object");
	}
	FL_UNSET(obj, FL_TAINT);
    }
    return obj;
}

VALUE
rb_obj_untaint(VALUE obj)
{
    return rb_obj_untaint_m(obj, 0);
}

static VALUE
rb_obj_untrusted_imp(VALUE obj, SEL sel)
{
    if (SPECIAL_CONST_P(obj)) {
	if (immediate_flags_tbl && FIXFLOAT_P(obj)) {
	    VALUE flags = 0;
	    if (st_lookup(immediate_flags_tbl, obj, &flags) && (flags & FL_UNTRUSTED)) {
		return Qtrue;
	    }
	}
	return Qfalse;
    }
    else if (NATIVE(obj)) {
	switch (TYPE(obj)) {
	    case T_SYMBOL:
		return Qfalse;	
	    case T_ARRAY:
		if (rb_klass_is_rary(*(VALUE *)obj)) {
		    return RBASIC(obj)->flags & FL_UNTRUSTED ? Qtrue : Qfalse;
		}
		// fall through
	    case T_STRING:
		if (rb_klass_is_rstr(*(VALUE *)obj)) {
		    return RBASIC(obj)->flags & FL_UNTRUSTED ? Qtrue : Qfalse;
		}
		// fall through
	    case T_HASH:
		if (rb_klass_is_rhash(*(VALUE *)obj)) {
		    return RBASIC(obj)->flags & FL_UNTRUSTED ? Qtrue : Qfalse;
		}
	    default:
		return rb_objc_flag_check((const void *)obj, FL_UNTRUSTED) ? Qtrue : Qfalse;
	}
    }
    if (FL_TEST(obj, FL_UNTRUSTED)) {
	return Qtrue;
    }
    return Qfalse;
}

VALUE
rb_obj_untrusted(VALUE obj)
{
    return rb_obj_untrusted_imp(obj, 0);
}

static VALUE
rb_obj_trust_imp(VALUE obj, SEL sel)
{
    rb_secure(3);
    if (OBJ_UNTRUSTED(obj)) {
	if (OBJ_FROZEN(obj)) {
	    rb_error_frozen("object");
	}
	else if (SPECIAL_CONST_P(obj)) {
	    if(!FIXFLOAT_P(obj)) {
		return obj;
	    }
	    else if(!immediate_flags_tbl) {
		immediate_flags_tbl = st_init_numtable();
		GC_RETAIN(immediate_flags_tbl);
		st_insert(immediate_flags_tbl, obj, FL_UNTRUSTED);
	    }
	    else {
		VALUE flags = 0;
		st_lookup(immediate_flags_tbl, obj, &flags);
		flags &= ~FL_UNTRUSTED;
		st_insert(immediate_flags_tbl, obj, (st_data_t)flags);
	    }
	    return obj;
	}
	else if (NATIVE(obj)) {
	    switch (TYPE(obj)) {
		case T_SYMBOL:
		    return obj;
		case T_ARRAY:
		    if (rb_klass_is_rary(*(VALUE *)obj)) {
			RBASIC(obj)->flags &= ~FL_UNTRUSTED;
			break;
		    }
		    // fall through
		case T_STRING:
		    if (rb_klass_is_rstr(*(VALUE *)obj)) {
			RBASIC(obj)->flags &= ~FL_UNTRUSTED;
			break;
		    }
		    // fall through
		case T_HASH:
		    if (rb_klass_is_rhash(*(VALUE *)obj)) {
			RBASIC(obj)->flags &= ~FL_UNTRUSTED;
			break;
		    }
		default:
		    rb_objc_flag_set((const void *)obj, FL_UNTRUSTED, false);
	    }
	    return obj;
	}
	FL_UNSET(obj, FL_UNTRUSTED);
    }
    return obj;
}

VALUE
rb_obj_trust(VALUE obj)
{
    return rb_obj_trust_imp(obj, 0);
}

static VALUE
rb_obj_untrust_imp(VALUE obj, SEL sel)
{
    rb_secure(4);
    if (!OBJ_UNTRUSTED(obj)) {
	if (OBJ_FROZEN(obj)) {
	    rb_error_frozen("object");
	}
	else if (SPECIAL_CONST_P(obj)) {
	    if (!FIXFLOAT_P(obj)) {
		return obj;
	    }
	    else if (!immediate_flags_tbl) {
		immediate_flags_tbl = st_init_numtable();
		GC_RETAIN(immediate_flags_tbl);
		st_insert(immediate_flags_tbl, obj, (st_data_t)FL_UNTRUSTED);
	    }
	    else {
		VALUE flags = 0;
		st_lookup(immediate_flags_tbl, obj, &flags);
		flags |= FL_UNTRUSTED;
		st_insert(immediate_flags_tbl, obj, (st_data_t)flags);
	    }
	    return obj;
	}
	else if (NATIVE(obj)) {
	    switch (TYPE(obj)) {
		case T_SYMBOL:
		    return Qfalse;
		case T_ARRAY:
		    if (rb_klass_is_rary(*(VALUE *)obj)) {
			RBASIC(obj)->flags |= FL_UNTRUSTED;
			break;
		    }
		    // fall through
		case T_STRING:
		    if (rb_klass_is_rstr(*(VALUE *)obj)) {
			RBASIC(obj)->flags |= FL_UNTRUSTED;
			break;
		    }
		    // fall through
		case T_HASH:
		    if (rb_klass_is_rhash(*(VALUE *)obj)) {
			RBASIC(obj)->flags |= FL_UNTRUSTED;
			break;
		    }
		default:
		    rb_objc_flag_set((const void *)obj, FL_UNTRUSTED, true);
	    }
	    return obj;
	}
	FL_SET(obj, FL_UNTRUSTED);
    }
    return obj;
}

VALUE
rb_obj_untrust(VALUE obj)
{
	return rb_obj_untrust_imp(obj, 0);
}

void
rb_obj_infect(VALUE obj1, VALUE obj2)
{
    OBJ_INFECT(obj1, obj2);
}

/*
 *  call-seq:
 *     obj.freeze    => obj
 *  
 *  Prevents further modifications to <i>obj</i>. A
 *  <code>TypeError</code> will be raised if modification is attempted.
 *  There is no way to unfreeze a frozen object. See also
 *  <code>Object#frozen?</code>.
 *     
 *     a = [ "a", "b", "c" ]
 *     a.freeze
 *     a << "z"
 *     
 *  <em>produces:</em>
 *     
 *     prog.rb:3:in `<<': can't modify frozen array (TypeError)
 *     	from prog.rb:3
 */

static VALUE
rb_obj_freeze_m(VALUE obj, SEL sel)
{
    if (!OBJ_FROZEN(obj)) {
	int type;
	if (rb_safe_level() >= 4 && OBJ_UNTRUSTED(obj)) {
	    rb_raise(rb_eSecurityError, "Insecure: can't freeze object");
	}
	else if (SPECIAL_CONST_P(obj)) {
immediate:
	    if (!immediate_flags_tbl) {
		immediate_flags_tbl = st_init_numtable();
	 	GC_RETAIN(immediate_flags_tbl);
		st_insert(immediate_flags_tbl, obj, (st_data_t)FL_FREEZE);
	    } 
	    else {
		VALUE flags = 0;
		st_lookup(immediate_flags_tbl, obj, &flags);
		flags |= FL_FREEZE;
		st_insert(immediate_flags_tbl, obj, (st_data_t)flags);
	    }
	    return obj;
	}
	else if ((type = TYPE(obj)) == T_CLASS || type == T_MODULE) {
	    RCLASS_SET_VERSION_FLAG(obj, RCLASS_IS_FROZEN);
	}
	else if (NATIVE(obj)) {
	    switch (TYPE(obj)) {
		case T_SYMBOL:
		    goto immediate;

		case T_ARRAY:
		    if (rb_klass_is_rary(*(VALUE *)obj)) {
			RBASIC(obj)->flags |= FL_FREEZE;
			break;
		    }
		    // fall through
		case T_STRING:
		    if (rb_klass_is_rstr(*(VALUE *)obj)) {
			RBASIC(obj)->flags |= FL_FREEZE;
			break;
		    }
		    // fall through
		case T_HASH:
		    if (rb_klass_is_rhash(*(VALUE *)obj)) {
			RBASIC(obj)->flags |= FL_FREEZE;
			break;
		    }
		default:
		    rb_objc_flag_set((const void *)obj, FL_FREEZE, true);
	    }
	}
	else {
	    FL_SET(obj, FL_FREEZE);
	}
    }
    return obj;
}

VALUE
rb_obj_freeze(VALUE obj)
{
    return rb_obj_freeze_m(obj, 0);
}

/*
 *  call-seq:
 *     obj.frozen?    => true or false
 *  
 *  Returns the freeze status of <i>obj</i>.
 *     
 *     a = [ "a", "b", "c" ]
 *     a.freeze    #=> ["a", "b", "c"]
 *     a.frozen?   #=> true
 */

static VALUE
rb_obj_frozen(VALUE obj, SEL sel)
{
    if (SPECIAL_CONST_P(obj)) {
immediate:
	if (immediate_flags_tbl) {
	    VALUE flags = 0;
	    if (st_lookup(immediate_flags_tbl, obj, &flags) && (flags & FL_FREEZE)) {
	    	return Qtrue;
	    }
	}
	return Qfalse;
    }
    switch (TYPE(obj)) {
	case T_SYMBOL:
	    goto immediate;

	case T_ARRAY:
	    if (rb_klass_is_rary(*(VALUE *)obj)) {
		return RBASIC(obj)->flags & FL_FREEZE ? Qtrue : Qfalse;
	    }
	    // fall through
	case T_STRING:
	    if (rb_klass_is_rstr(*(VALUE *)obj)) {
		return RBASIC(obj)->flags & FL_FREEZE ? Qtrue : Qfalse;
	    }
	    // fall through
	case T_HASH:
	    if (rb_klass_is_rhash(*(VALUE *)obj)) {
		return RBASIC(obj)->flags & FL_FREEZE ? Qtrue : Qfalse;
	    }
	case T_NATIVE:
	    return rb_objc_flag_check((const void *)obj, FL_FREEZE)
		? Qtrue : Qfalse;
	case T_CLASS:
	case T_ICLASS:
	case T_MODULE:
	    return (RCLASS_VERSION(obj) & RCLASS_IS_FROZEN) == RCLASS_IS_FROZEN ? Qtrue : Qfalse;
	default:
	    return FL_TEST(obj, FL_FREEZE) ? Qtrue : Qfalse;
    }
}

VALUE
rb_obj_frozen_p(VALUE obj)
{
    return rb_obj_frozen(obj, 0);
}


/*
 * Document-class: NilClass
 *
 *  The class of the singleton object <code>nil</code>.
 */

/*
 *  call-seq:
 *     nil.to_i => 0
 *  
 *  Always returns zero.
 *     
 *     nil.to_i   #=> 0
 */


static VALUE
nil_to_i(VALUE obj, SEL sel)
{
    return INT2FIX(0);
}

/*
 *  call-seq:
 *     nil.to_f    => 0.0
 *  
 *  Always returns zero.
 *     
 *     nil.to_f   #=> 0.0
 */

static VALUE
nil_to_f(VALUE obj, SEL sel)
{
    return DOUBLE2NUM(0.0);
}

/*
 *  call-seq:
 *     nil.to_s    => ""
 *  
 *  Always returns the empty string.
 */

static VALUE
nil_to_s(VALUE obj, SEL sel)
{
    return rb_usascii_str_new(0, 0);
}

/*
 * Document-method: to_a
 *
 *  call-seq:
 *     nil.to_a    => []
 *  
 *  Always returns an empty array.
 *     
 *     nil.to_a   #=> []
 */

static VALUE
nil_to_a(VALUE obj, SEL sel)
{
    return rb_ary_new2(0);
}

/*
 *  call-seq:
 *    nil.inspect  => "nil"
 *
 *  Always returns the string "nil".
 */

static VALUE
nil_inspect(VALUE obj, SEL sel)
{
    return rb_usascii_str_new2("nil");
}

/***********************************************************************
 *  Document-class: TrueClass
 *
 *  The global value <code>true</code> is the only instance of class
 *  <code>TrueClass</code> and represents a logically true value in
 *  boolean expressions. The class provides operators allowing
 *  <code>true</code> to be used in logical expressions.
 */


/*
 * call-seq:
 *   true.to_s   =>  "true"
 *
 * The string representation of <code>true</code> is "true".
 */

static VALUE
true_to_s(VALUE obj, SEL sel)
{
    return rb_usascii_str_new2("true");
}


/*
 *  call-seq:
 *     true & obj    => true or false
 *  
 *  And---Returns <code>false</code> if <i>obj</i> is
 *  <code>nil</code> or <code>false</code>, <code>true</code> otherwise.
 */

static VALUE
true_and(VALUE obj, SEL sel, VALUE obj2)
{
    return RTEST(obj2)?Qtrue:Qfalse;
}

/*
 *  call-seq:
 *     true | obj   => true
 *  
 *  Or---Returns <code>true</code>. As <i>anObject</i> is an argument to
 *  a method call, it is always evaluated; there is no short-circuit
 *  evaluation in this case.
 *     
 *     true |  puts("or")
 *     true || puts("logical or")
 *     
 *  <em>produces:</em>
 *     
 *     or
 */

static VALUE
true_or(VALUE obj, SEL sel, VALUE obj2)
{
    return Qtrue;
}


/*
 *  call-seq:
 *     true ^ obj   => !obj
 *  
 *  Exclusive Or---Returns <code>true</code> if <i>obj</i> is
 *  <code>nil</code> or <code>false</code>, <code>false</code>
 *  otherwise.
 */

static VALUE
true_xor(VALUE obj, SEL sel, VALUE obj2)
{
    return RTEST(obj2)?Qfalse:Qtrue;
}


/*
 *  Document-class: FalseClass
 *
 *  The global value <code>false</code> is the only instance of class
 *  <code>FalseClass</code> and represents a logically false value in
 *  boolean expressions. The class provides operators allowing
 *  <code>false</code> to participate correctly in logical expressions.
 *     
 */

/*
 * call-seq:
 *   false.to_s   =>  "false"
 *
 * 'nuf said...
 */

static VALUE
false_to_s(VALUE obj, SEL sel)
{
    return rb_usascii_str_new2("false");
}

/*
 *  call-seq:
 *     false & obj   => false
 *     nil & obj     => false
 *  
 *  And---Returns <code>false</code>. <i>obj</i> is always
 *  evaluated as it is the argument to a method call---there is no
 *  short-circuit evaluation in this case.
 */

static VALUE
false_and(VALUE obj, SEL sel, VALUE obj2)
{
    return Qfalse;
}


/*
 *  call-seq:
 *     false | obj   =>   true or false
 *     nil   | obj   =>   true or false
 *  
 *  Or---Returns <code>false</code> if <i>obj</i> is
 *  <code>nil</code> or <code>false</code>; <code>true</code> otherwise.
 */

static VALUE
false_or(VALUE obj, SEL sel, VALUE obj2)
{
    return RTEST(obj2)?Qtrue:Qfalse;
}

/*
 *  call-seq:
 *     false ^ obj    => true or false
 *     nil   ^ obj    => true or false
 *  
 *  Exclusive Or---If <i>obj</i> is <code>nil</code> or
 *  <code>false</code>, returns <code>false</code>; otherwise, returns
 *  <code>true</code>.
 *     
 */

static VALUE
false_xor(VALUE obj, SEL sel, VALUE obj2)
{
    return RTEST(obj2)?Qtrue:Qfalse;
}

/*
 * call_seq:
 *   nil.nil?               => true
 *
 * Only the object <i>nil</i> responds <code>true</code> to <code>nil?</code>.
 */

static VALUE
rb_true(VALUE obj, SEL sel)
{
    return Qtrue;
}

/*
 * call_seq:
 *   nil.nil?               => true
 *   <anything_else>.nil?   => false
 *
 * Only the object <i>nil</i> responds <code>true</code> to <code>nil?</code>.
 */


static VALUE
rb_false(VALUE obj, SEL sel)
{
    return Qfalse;
}


/*
 *  call-seq:
 *     obj =~ other  => nil
 *  
 *  Pattern Match---Overridden by descendents (notably
 *  <code>Regexp</code> and <code>String</code>) to provide meaningful
 *  pattern-match semantics.
 */

static VALUE
rb_obj_match(VALUE obj1, SEL sel, VALUE obj2)
{
    return Qnil;
}

/*
 *  call-seq:
 *     obj !~ other  => nil
 *  
 *  Returns true if two objects does not match, using <i>=~</i> method.
 */

static VALUE
rb_obj_not_match(VALUE obj1, SEL sel, VALUE obj2)
{
    VALUE result = rb_funcall(obj1, id_match, 1, obj2);
    return RTEST(result) ? Qfalse : Qtrue;
}

static VALUE
rb_obj_cmp(VALUE obj1, SEL sel, VALUE obj2)
{
    if (rb_equal(obj1, obj2) == Qtrue) {
	return INT2FIX(0);
    }
    return Qnil;
}


/***********************************************************************
 *
 * Document-class: Module
 *
 *  A <code>Module</code> is a collection of methods and constants. The
 *  methods in a module may be instance methods or module methods.
 *  Instance methods appear as methods in a class when the module is
 *  included, module methods do not. Conversely, module methods may be
 *  called without creating an encapsulating object, while instance
 *  methods may not. (See <code>Module#module_function</code>)
 *     
 *  In the descriptions that follow, the parameter <i>syml</i> refers
 *  to a symbol, which is either a quoted string or a
 *  <code>Symbol</code> (such as <code>:name</code>).
 *     
 *     module Mod
 *       include Math
 *       CONST = 1
 *       def meth
 *         #  ...
 *       end
 *     end
 *     Mod.class              #=> Module
 *     Mod.constants          #=> [:CONST, :PI, :E]
 *     Mod.instance_methods   #=> [:meth]
 *     
 */

/*
 * call-seq:
 *   mod.to_s   => string
 *
 * Return a string representing this module or class. For basic
 * classes and modules, this is the name. For singletons, we
 * show information on the thing we're attached to as well.
 */

static VALUE
rb_mod_to_s(VALUE klass, SEL sel)
{
    if (RCLASS_SINGLETON(klass)) {
	VALUE s = rb_usascii_str_new2("#<");
	VALUE v = rb_singleton_class_attached_object(klass);

	rb_str_cat2(s, "Class:");
	switch (TYPE(v)) {
	  case T_CLASS: case T_MODULE:
	    rb_str_append(s, rb_inspect(v));
	    break;
	  default:
	    rb_str_append(s, rb_any_to_s(v));
	    break;
	}
	rb_str_cat2(s, ">");

	return s;
    }
    return rb_str_dup(rb_class_name(klass));
}

static VALUE 
rb_mod_included_modules_imp(VALUE recv, SEL sel)
{
    return rb_mod_included_modules(recv);
}

static VALUE
rb_mod_ancestors_imp(VALUE self, SEL sel)
{
    return rb_mod_ancestors(self);
}

static VALUE
rb_mod_ancestors_all_imp(VALUE self, SEL sel)
{
    return rb_mod_ancestors_nocopy(self);
}

static VALUE
rb_mod_properties_imp(VALUE self, SEL sel)
{
    VALUE ary = rb_ary_new();
    unsigned int count = 0;
    objc_property_t *list = class_copyPropertyList((Class)self, &count);
    if (list != NULL) {
	for (unsigned int i = 0; i < count; i++) {
	    rb_ary_push(ary, ID2SYM(rb_intern(property_getName(list[i]))));
	}
	free(list);
    }
    return ary;
}

/*
 *  call-seq:
 *     mod.freeze
 *  
 *  Prevents further modifications to <i>mod</i>.
 */

static VALUE
rb_mod_freeze(VALUE mod, SEL sel)
{
    rb_class_name(mod);
    return rb_obj_freeze(mod);
}

/*
 *  call-seq:
 *     mod === obj    => true or false
 *  
 *  Case Equality---Returns <code>true</code> if <i>anObject</i> is an
 *  instance of <i>mod</i> or one of <i>mod</i>'s descendents. Of
 *  limited use for modules, but can be used in <code>case</code>
 *  statements to classify objects by class.
 */

static VALUE
rb_mod_eqq(VALUE mod, SEL sel, VALUE arg)
{
    return rb_obj_is_kind_of(arg, mod);
}

/*
 * call-seq:
 *   mod <= other   =>  true, false, or nil
 *
 * Returns true if <i>mod</i> is a subclass of <i>other</i> or
 * is the same as <i>other</i>. Returns 
 * <code>nil</code> if there's no relationship between the two. 
 * (Think of the relationship in terms of the class definition: 
 * "class A<B" implies "A<B").
 *
 */

static inline bool
rb_class_mixin_inherited(VALUE mod, VALUE arg)
{
    VALUE ary = rb_attr_get(mod, idIncludedModules);
    if (ary != Qnil) {
	for (int i = 0; i < RARRAY_LEN(ary); i++) {
	    if (RARRAY_AT(ary, i) == arg) {
		return true;
	    }
	} 
    }
    return false;
}

VALUE
rb_class_inherited_p(VALUE mod, VALUE arg)
{
    VALUE start = mod;

    if (mod == arg) {
	return Qtrue;
    }
    switch (TYPE(arg)) {
	case T_MODULE:
	case T_CLASS:
	    break;
	default:
	    rb_raise(rb_eTypeError, "compared with non class/module");
    }
    while (mod) {
	if (mod == arg || rb_class_mixin_inherited(mod, arg)) {
	    return Qtrue;
	}
	mod = RCLASS_SUPER(mod);
    }
    /* not mod < arg; check if mod > arg */
    while (arg) {
	if (arg == start || rb_class_mixin_inherited(arg, start)) {
	    return Qfalse;
	}
	arg = RCLASS_SUPER(arg);
    }
    return Qnil;
}

static VALUE
rb_class_inherited_imp(VALUE mod, SEL sel, VALUE arg)
{
    return rb_class_inherited_p(mod, arg);
}

/*
 * call-seq:
 *   mod < other   =>  true, false, or nil
 *
 * Returns true if <i>mod</i> is a subclass of <i>other</i>. Returns 
 * <code>nil</code> if there's no relationship between the two. 
 * (Think of the relationship in terms of the class definition: 
 * "class A<B" implies "A<B").
 *
 */

static VALUE
rb_mod_lt(VALUE mod, SEL sel, VALUE arg)
{
    if (mod == arg) return Qfalse;
    return rb_class_inherited_p(mod, arg);
}


/*
 * call-seq:
 *   mod >= other   =>  true, false, or nil
 *
 * Returns true if <i>mod</i> is an ancestor of <i>other</i>, or the
 * two modules are the same. Returns 
 * <code>nil</code> if there's no relationship between the two. 
 * (Think of the relationship in terms of the class definition: 
 * "class A<B" implies "B>A").
 *
 */

static VALUE
rb_mod_ge(VALUE mod, SEL sel, VALUE arg)
{
    switch (TYPE(arg)) {
      case T_MODULE:
      case T_CLASS:
	break;
      default:
	rb_raise(rb_eTypeError, "compared with non class/module");
    }

    return rb_class_inherited_p(arg, mod);
}

/*
 * call-seq:
 *   mod > other   =>  true, false, or nil
 *
 * Returns true if <i>mod</i> is an ancestor of <i>other</i>. Returns 
 * <code>nil</code> if there's no relationship between the two. 
 * (Think of the relationship in terms of the class definition: 
 * "class A<B" implies "B>A").
 *
 */

static VALUE
rb_mod_gt(VALUE mod, SEL sel, VALUE arg)
{
    if (mod == arg) return Qfalse;
    return rb_mod_ge(mod, 0, arg);
}

/*
 *  call-seq:
 *     mod <=> other_mod   => -1, 0, +1, or nil
 *  
 *  Comparison---Returns -1 if <i>mod</i> includes <i>other_mod</i>, 0 if
 *  <i>mod</i> is the same as <i>other_mod</i>, and +1 if <i>mod</i> is
 *  included by <i>other_mod</i> or if <i>mod</i> has no relationship with
 *  <i>other_mod</i>. Returns <code>nil</code> if <i>other_mod</i> is
 *  not a module.
 */

static VALUE
rb_mod_cmp(VALUE mod, SEL sel, VALUE arg)
{
    VALUE cmp;

    if (mod == arg) return INT2FIX(0);
    switch (TYPE(arg)) {
      case T_MODULE:
      case T_CLASS:
	break;
      default:
	return Qnil;
    }

    cmp = rb_class_inherited_p(mod, arg);
    if (NIL_P(cmp)) return Qnil;
    if (cmp) {
	return INT2FIX(-1);
    }
    return INT2FIX(1);
}

static VALUE
rb_module_s_alloc(VALUE klass, SEL sel)
{
    return rb_module_new();
}

static VALUE
rb_class_s_alloc(VALUE klass, SEL sel)
{
    return rb_class_boot(0);
}

/*
 *  call-seq:
 *    Module.new                  => mod
 *    Module.new {|mod| block }   => mod
 *  
 *  Creates a new anonymous module. If a block is given, it is passed
 *  the module object, and the block is evaluated in the context of this
 *  module using <code>module_eval</code>.
 *     
 *     Fred = Module.new do
 *       def meth1
 *         "hello"
 *       end
 *       def meth2
 *         "bye"
 *       end
 *     end
 *     a = "my string"
 *     a.extend(Fred)   #=> "my string"
 *     a.meth1          #=> "hello"
 *     a.meth2          #=> "bye"
 */

VALUE rb_mod_module_exec(VALUE mod, SEL sel, int argc, VALUE *argv);

VALUE
rb_mod_initialize(VALUE module, SEL sel)
{
    static bool initialized = false;
    if (!initialized) {
	initialized = true;
	rb_objc_force_class_initialize((Class)module);
	initialized = false;

	if (rb_block_given_p()) {
	    rb_mod_module_exec(module, 0, 1, &module);
	}
    }
    return Qnil;
}

/*
 *  call-seq:
 *    class Something
 *      def setThing(thing, forIndex: i); @things[i] = thing; end
 *      method_signature :'setThing:forIndex:', 'v@:@i'
 *    end
 *
 *  Changes the type signature stored in the Objective-C runtime for the
 *  given method.
 *
 */
static VALUE
rb_mod_method_signature(VALUE module, SEL sel, VALUE mid, VALUE sim)
{
    // TODO
    abort();
    return Qnil;
}

/*
 *  call-seq:
 *     Class.new(super_class=Object)   =>    a_class
 *  
 *  Creates a new anonymous (unnamed) class with the given superclass
 *  (or <code>Object</code> if no parameter is given). You can give a
 *  class a name by assigning the class object to a constant.
 *     
 */

static VALUE
rb_class_initialize(int argc, VALUE *argv, VALUE klass)
{
    VALUE super;

    if (argc == 0) {
	super = rb_cObject;
    }
    else {
	rb_scan_args(argc, argv, "01", &super);
	rb_check_inheritable(super);
    }
    if (super == rb_cObject) {
	super = rb_cRubyObject;
    }
    RCLASS_SET_SUPER(klass, super);
    rb_objc_class_sync_version((Class)klass, (Class)super);

    rb_class_inherited(super, klass);
    rb_mod_initialize(klass, 0);

    return klass;
}

/*
 *  call-seq:
 *     class.allocate()   =>   obj
 *  
 *  Allocates space for a new object of <i>class</i>'s class. The
 *  returned object must be an instance of <i>class</i>.
 *     
 */

static inline VALUE
rb_obj_alloc0(VALUE klass)
{
    if (RCLASS_SINGLETON(klass)) {
	rb_raise(rb_eTypeError, "can't create instance of singleton class");
    }

    if ((RCLASS_VERSION(*(void **)klass) & RCLASS_HAS_ROBJECT_ALLOC)
	    == RCLASS_HAS_ROBJECT_ALLOC) {
	// Fast path!
	return rb_vm_new_rb_object(klass);
    }
    return rb_vm_call(klass, selAlloc, 0, NULL);
}

VALUE
rb_obj_alloc(VALUE klass)
{
    return rb_obj_alloc0(klass);
}

static VALUE
rb_obj_alloc_imp(VALUE klass, SEL sel)
{
    return rb_obj_alloc0(klass);
}

/*
 *  call-seq:
 *     class.new(args, ...)    =>  obj
 *  
 *  Calls <code>allocate</code> to create a new object of
 *  <i>class</i>'s class, then invokes that object's
 *  <code>initialize</code> method, passing it <i>args</i>.
 *  This is the method that ends up getting called whenever
 *  an object is constructed using .new.
 *     
 */

static inline VALUE
rb_class_new_instance0(int argc, VALUE *argv, VALUE klass)
{
    VALUE obj = rb_obj_alloc0(klass);

    /* Because we cannot override +[NSObject initialize] */
    if (klass == rb_cClass) {
	return rb_class_initialize(argc, argv, obj);
    }

    //init_obj = rb_obj_call_init(obj, argc, argv);

    rb_vm_block_t *block = rb_vm_current_block();
    if (argc == 0) {
	rb_vm_call2(block, obj, CLASS_OF(obj), selInitialize, argc, argv);
    }
    else {
	rb_vm_call2(block, obj, CLASS_OF(obj), selInitialize2, argc, argv);
    }
    RETURN_IF_BROKEN();
    return obj;
}

VALUE
rb_class_new_instance_imp(VALUE klass, SEL sel, int argc, VALUE *argv)
{
    return rb_class_new_instance0(argc, argv, klass);
}

VALUE
rb_class_new_instance(int argc, VALUE *argv, VALUE klass)
{
    return rb_class_new_instance0(argc, argv, klass);
}

/*
 *  call-seq:
 *     attr_reader(symbol, ...)    => nil
 *     attr(symbol, ...)             => nil
 *  
 *  Creates instance variables and corresponding methods that return the
 *  value of each instance variable. Equivalent to calling
 *  ``<code>attr</code><i>:name</i>'' on each name in turn.
 */

static VALUE
rb_mod_attr_reader(VALUE klass, SEL sel, int argc, VALUE *argv)
{
    for (int i = 0; i < argc; i++) {
	rb_attr(klass, rb_to_id(argv[i]), Qtrue, Qfalse, Qtrue);
    }
    return Qnil;
}

static VALUE
rb_mod_attr(VALUE klass, SEL sel, int argc, VALUE *argv)
{
    if (argc == 2 && (argv[1] == Qtrue || argv[1] == Qfalse)) {
	rb_warning("optional boolean argument is obsoleted");
	rb_attr(klass, rb_to_id(argv[0]), 1, RTEST(argv[1]), Qtrue);
	return Qnil;
    }
    return rb_mod_attr_reader(klass, 0, argc, argv);
}

/*
 *  call-seq:
 *      attr_writer(symbol, ...)    => nil
 *  
 *  Creates an accessor method to allow assignment to the attribute
 *  <i>aSymbol</i><code>.id2name</code>.
 */

static VALUE
rb_mod_attr_writer(VALUE klass, SEL sel, int argc, VALUE *argv)
{
    for (int i = 0; i < argc; i++) {
	rb_attr(klass, rb_to_id(argv[i]), Qfalse, Qtrue, Qtrue);
    }
    return Qnil;
}

/*
 *  call-seq:
 *     attr_accessor(symbol, ...)    => nil
 *  
 *  Defines a named attribute for this module, where the name is
 *  <i>symbol.</i><code>id2name</code>, creating an instance variable
 *  (<code>@name</code>) and a corresponding access method to read it.
 *  Also creates a method called <code>name=</code> to set the attribute.
 *     
 *     module Mod
 *       attr_accessor(:one, :two)
 *     end
 *     Mod.instance_methods.sort   #=> [:one, :one=, :two, :two=]
 */

static VALUE
rb_mod_attr_accessor(VALUE klass, SEL sel, int argc, VALUE *argv)
{
    for (int i = 0; i < argc; i++) {
	rb_attr(klass, rb_to_id(argv[i]), Qtrue, Qtrue, Qtrue);
    }
    return Qnil;
}

/*
 *  call-seq:
 *     mod.const_get(sym, inherit=true)    => obj
 *  
 *  Returns the value of the named constant in <i>mod</i>.
 *     
 *     Math.const_get(:PI)   #=> 3.14159265358979
 *
 *  If the constant is not defined or is defined by the ancestors and
 *  +inherit+ is false, +NameError+ will be raised.
 */

static VALUE
rb_mod_const_get(VALUE mod, SEL sel, int argc, VALUE *argv)
{
    VALUE name, recur;
    ID id;

    if (argc == 1) {
	name = argv[0];
	recur = Qtrue;
    }
    else {
	rb_scan_args(argc, argv, "11", &name, &recur);
    }
    id = rb_to_id(name);
    if (!rb_is_const_id(id)) {
	rb_name_error(id, "wrong constant name %s", rb_id2name(id));
    }
    return RTEST(recur) ? rb_const_get(mod, id) : rb_const_get_at(mod, id);
}

/*
 *  call-seq:
 *     mod.const_set(sym, obj)    => obj
 *  
 *  Sets the named constant to the given object, returning that object.
 *  Creates a new constant if no constant with the given name previously
 *  existed.
 *     
 *     Math.const_set("HIGH_SCHOOL_PI", 22.0/7.0)   #=> 3.14285714285714
 *     Math::HIGH_SCHOOL_PI - Math::PI              #=> 0.00126448926734968
 */

static VALUE
rb_mod_const_set(VALUE mod, SEL sel, VALUE name, VALUE value)
{
    ID id = rb_to_id(name);

    if (!rb_is_const_id(id)) {
	rb_name_error(id, "wrong constant name %s", rb_id2name(id));
    }
    rb_const_set(mod, id, value);
    return value;
}

/*
 *  call-seq:
 *     mod.const_defined?(sym, inherit=true)   => true or false
 *  
 *  Returns <code>true</code> if a constant with the given name is
 *  defined by <i>mod</i>, or its ancestors if +inherit+ is not false.
 *     
 *     Math.const_defined? "PI"   #=> true
 *     IO.const_defined? "SYNC"   #=> true
 *     IO.const_defined? "SYNC", false   #=> false
 */

static VALUE
rb_mod_const_defined(VALUE mod, SEL sel, int argc, VALUE *argv)
{
    VALUE name, recur;
    ID id;

    if (argc == 1) {
	name = argv[0];
	recur = Qtrue;
    }
    else {
	rb_scan_args(argc, argv, "11", &name, &recur);
    }
    id = rb_to_id(name);
    if (!rb_is_const_id(id)) {
	rb_name_error(id, "wrong constant name %s", rb_id2name(id));
    }
    return RTEST(recur) ? rb_const_defined(mod, id) : rb_const_defined_at(mod, id);
}

static VALUE
rb_mod_remove_const_imp(VALUE mod, SEL sel, VALUE name)
{
    return rb_mod_remove_const(mod, name);
}

/*
 *  call-seq:
 *     obj.methods    => array
 *  
 *  Returns a list of the names of methods publicly accessible in
 *  <i>obj</i>. This will include all the methods accessible in
 *  <i>obj</i>'s ancestors.
 *     
 *     class Klass
 *       def kMethod()
 *       end
 *     end
 *     k = Klass.new
 *     k.methods[0..9]    #=> ["kMethod", "freeze", "nil?", "is_a?", 
 *                        #    "class", "instance_variable_set",
 *                        #    "methods", "extend", "__send__", "instance_eval"]
 *     k.methods.length   #=> 42
 */

VALUE rb_class_instance_methods(VALUE, SEL, int, VALUE *);

static VALUE
rb_obj_methods(VALUE obj, SEL sel, int argc, VALUE *argv)
{
    VALUE recur, objc_methods;
    VALUE args[2];

    if (argc == 0) {
	recur = Qtrue;
	objc_methods = Qfalse;
    }
    else {
	rb_scan_args(argc, argv, "02", &recur, &objc_methods);
    }

    args[0] = recur;
    args[1] = objc_methods;

    return rb_class_instance_methods(CLASS_OF(obj), 0, 2, args);
}

/*
 *  call-seq:
 *     obj.protected_methods(all=true)   => array
 *  
 *  Returns the list of protected methods accessible to <i>obj</i>. If
 *  the <i>all</i> parameter is set to <code>false</code>, only those methods
 *  in the receiver will be listed.
 */

VALUE rb_class_protected_instance_methods(VALUE, SEL, int, VALUE *);

static VALUE
rb_obj_protected_methods(VALUE obj, SEL sel, int argc, VALUE *argv)
{
    if (argc == 0) {		/* hack to stop warning */
	VALUE args[1];

	args[0] = Qtrue;
	return rb_class_protected_instance_methods(CLASS_OF(obj), 0, 1, args);
    }
    return rb_class_protected_instance_methods(CLASS_OF(obj), 0, argc, argv);
}

/*
 *  call-seq:
 *     obj.private_methods(all=true)   => array
 *  
 *  Returns the list of private methods accessible to <i>obj</i>. If
 *  the <i>all</i> parameter is set to <code>false</code>, only those methods
 *  in the receiver will be listed.
 */

VALUE rb_class_private_instance_methods(VALUE, SEL, int, VALUE *);

static VALUE
rb_obj_private_methods(VALUE obj, SEL sel, int argc, VALUE *argv)
{
    if (argc == 0) {		/* hack to stop warning */
	VALUE args[1];

	args[0] = Qtrue;
	return rb_class_private_instance_methods(CLASS_OF(obj), 0, 1, args);
    }
    return rb_class_private_instance_methods(CLASS_OF(obj), 0, argc, argv);
}

/*
 *  call-seq:
 *     obj.public_methods(all=true)   => array
 *  
 *  Returns the list of public methods accessible to <i>obj</i>. If
 *  the <i>all</i> parameter is set to <code>false</code>, only those methods
 *  in the receiver will be listed.
 */

VALUE rb_class_public_instance_methods(VALUE, SEL, int, VALUE *);

static VALUE
rb_obj_public_methods(VALUE obj, SEL sel, int argc, VALUE *argv)
{
    if (argc == 0) {		/* hack to stop warning */
	VALUE args[1];

	args[0] = Qtrue;
	return rb_class_public_instance_methods(CLASS_OF(obj), 0, 1, args);
    }
    return rb_class_public_instance_methods(CLASS_OF(obj), 0, argc, argv);
}

/*
 *  call-seq:
 *     obj.instance_variable_get(symbol)    => obj
 *
 *  Returns the value of the given instance variable, or nil if the
 *  instance variable is not set. The <code>@</code> part of the
 *  variable name should be included for regular instance
 *  variables. Throws a <code>NameError</code> exception if the
 *  supplied symbol is not valid as an instance variable name.
 *     
 *     class Fred
 *       def initialize(p1, p2)
 *         @a, @b = p1, p2
 *       end
 *     end
 *     fred = Fred.new('cat', 99)
 *     fred.instance_variable_get(:@a)    #=> "cat"
 *     fred.instance_variable_get("@b")   #=> 99
 */

static VALUE
rb_obj_ivar_get(VALUE obj, SEL sel, VALUE iv)
{
    ID id = rb_to_id(iv);

    if (!rb_is_instance_id(id)) {
	rb_name_error(id, "`%s' is not allowed as an instance variable name", rb_id2name(id));
    }
    return rb_ivar_get(obj, id);
}

/*
 *  call-seq:
 *     obj.instance_variable_set(symbol, obj)    => obj
 *  
 *  Sets the instance variable names by <i>symbol</i> to
 *  <i>object</i>, thereby frustrating the efforts of the class's
 *  author to attempt to provide proper encapsulation. The variable
 *  did not have to exist prior to this call.
 *     
 *     class Fred
 *       def initialize(p1, p2)
 *         @a, @b = p1, p2
 *       end
 *     end
 *     fred = Fred.new('cat', 99)
 *     fred.instance_variable_set(:@a, 'dog')   #=> "dog"
 *     fred.instance_variable_set(:@c, 'cat')   #=> "cat"
 *     fred.inspect                             #=> "#<Fred:0x401b3da8 @a=\"dog\", @b=99, @c=\"cat\">"
 */

static VALUE
rb_obj_ivar_set(VALUE obj, SEL sel, VALUE iv, VALUE val)
{
    ID id = rb_to_id(iv);

    if (!rb_is_instance_id(id)) {
	rb_name_error(id, "`%s' is not allowed as an instance variable name", rb_id2name(id));
    }
    return rb_ivar_set(obj, id, val);
}

/*
 *  call-seq:
 *     obj.instance_variable_defined?(symbol)    => true or false
 *
 *  Returns <code>true</code> if the given instance variable is
 *  defined in <i>obj</i>.
 *
 *     class Fred
 *       def initialize(p1, p2)
 *         @a, @b = p1, p2
 *       end
 *     end
 *     fred = Fred.new('cat', 99)
 *     fred.instance_variable_defined?(:@a)    #=> true
 *     fred.instance_variable_defined?("@b")   #=> true
 *     fred.instance_variable_defined?("@c")   #=> false
 */

static VALUE
rb_obj_ivar_defined(VALUE obj, SEL sel, VALUE iv)
{
    ID id = rb_to_id(iv);

    if (!rb_is_instance_id(id)) {
	rb_name_error(id, "`%s' is not allowed as an instance variable name", rb_id2name(id));
    }
    return rb_ivar_defined(obj, id);
}

/*
 *  call-seq:
 *     mod.class_variable_get(symbol)    => obj
 *  
 *  Returns the value of the given class variable (or throws a
 *  <code>NameError</code> exception). The <code>@@</code> part of the
 *  variable name should be included for regular class variables
 *     
 *     class Fred
 *       @@foo = 99
 *     end
 *     Fred.class_variable_get(:@@foo)     #=> 99
 */

static VALUE
rb_mod_cvar_get(VALUE obj, SEL sel, VALUE iv)
{
    ID id = rb_to_id(iv);

    if (!rb_is_class_id(id)) {
	rb_name_error(id, "`%s' is not allowed as a class variable name", rb_id2name(id));
    }
    return rb_cvar_get(obj, id);
}

/*
 *  call-seq:
 *     obj.class_variable_set(symbol, obj)    => obj
 *  
 *  Sets the class variable names by <i>symbol</i> to
 *  <i>object</i>.
 *     
 *     class Fred
 *       @@foo = 99
 *       def foo
 *         @@foo
 *       end
 *     end
 *     Fred.class_variable_set(:@@foo, 101)     #=> 101
 *     Fred.new.foo                             #=> 101
 */

static VALUE
rb_mod_cvar_set(VALUE obj, SEL sel, VALUE iv, VALUE val)
{
    ID id = rb_to_id(iv);

    if (!rb_is_class_id(id)) {
	rb_name_error(id, "`%s' is not allowed as a class variable name", rb_id2name(id));
    }
    rb_cvar_set(obj, id, val);
    return val;
}

/*
 *  call-seq:
 *     obj.class_variable_defined?(symbol)    => true or false
 *
 *  Returns <code>true</code> if the given class variable is defined
 *  in <i>obj</i>.
 *
 *     class Fred
 *       @@foo = 99
 *     end
 *     Fred.class_variable_defined?(:@@foo)    #=> true
 *     Fred.class_variable_defined?(:@@bar)    #=> false
 */

static VALUE
rb_mod_cvar_defined(VALUE obj, SEL sel, VALUE iv)
{
    ID id = rb_to_id(iv);

    if (!rb_is_class_id(id)) {
	rb_name_error(id, "`%s' is not allowed as a class variable name", rb_id2name(id));
    }
    return rb_cvar_defined(obj, id);
}

static VALUE
convert_type(VALUE val, const char *tname, const char *method, int raise)
{
//    ID m;

//    m = rb_intern(method);
//    if (!rb_obj_respond_to(val, m, Qtrue)) {

    SEL sel = sel_registerName(method);
    VALUE result = rb_vm_check_call(val, sel, 0, NULL);
    if (result ==  Qundef) {
	if (raise) {
	    rb_raise(rb_eTypeError, "can't convert %s into %s",
		    NIL_P(val) ? "nil" :
		    val == Qtrue ? "true" :
		    val == Qfalse ? "false" :
		    rb_obj_classname(val), 
		    tname);
	}
	return Qnil;
    }
    return result;
}

VALUE
rb_convert_type(VALUE val, int type, const char *tname, const char *method)
{
    if (TYPE(val) == type) {
	return val;
    }
    VALUE v = convert_type(val, tname, method, TRUE);
    if (TYPE(v) != type) {
	const char *cname = rb_obj_classname(val);
	rb_raise(rb_eTypeError, "can't convert %s to %s (%s#%s gives %s)",
		 cname, tname, cname, method, rb_obj_classname(v));
    }
    return v;
}

VALUE
rb_check_convert_type(VALUE val, int type, const char *tname, const char *method)
{
    VALUE v;

    /* always convert T_DATA */
    if (TYPE(val) == type && type != T_DATA) {
	return val;
    }
    v = convert_type(val, tname, method, FALSE);
    if (NIL_P(v)) {
	return Qnil;
    }
    if (TYPE(v) != type) {
	const char *cname = rb_obj_classname(val);
	rb_raise(rb_eTypeError, "can't convert %s to %s (%s#%s gives %s)",
		 cname, tname, cname, method, rb_obj_classname(v));
    }
    return v;
}


static VALUE
rb_to_integer(VALUE val, const char *method)
{
    VALUE v;

    if (FIXNUM_P(val)) return val;
    if (TYPE(val) == T_BIGNUM) return val;
    v = convert_type(val, "Integer", method, TRUE);
    if (!rb_obj_is_kind_of(v, rb_cInteger)) {
	const char *cname = rb_obj_classname(val);
	rb_raise(rb_eTypeError, "can't convert %s to Integer (%s#%s gives %s)",
		 cname, cname, method, rb_obj_classname(v));
    }
    return v;
}

VALUE
rb_check_to_integer(VALUE val, const char *method)
{
    VALUE v;

    if (FIXNUM_P(val)) return val;
    if (TYPE(val) == T_BIGNUM) return val;
    v = convert_type(val, "Integer", method, FALSE);
    if (!rb_obj_is_kind_of(v, rb_cInteger)) {
	return Qnil;
    }
    return v;
}

VALUE
rb_to_int(VALUE val)
{
    return rb_to_integer(val, "to_int");
}

static VALUE
rb_convert_to_integer(VALUE val, int base)
{
    VALUE tmp;

    switch (TYPE(val)) {
	case T_FLOAT:
	    if (base != 0) {
		goto arg_error;
	    }
	    if (RFLOAT_VALUE(val) <= (double)FIXNUM_MAX
		    && RFLOAT_VALUE(val) >= (double)FIXNUM_MIN) {
		break;
	    }
	    return rb_dbl2big(RFLOAT_VALUE(val));

	case T_FIXNUM:
	case T_BIGNUM:
	    if (base != 0) {
		goto arg_error;
	    }
	    return val;

	case T_STRING:
string_conv:
	    return rb_str_to_inum(val, base, TRUE);

	case T_NIL:
	    if (base != 0) {
		goto arg_error;
	    }
	    rb_raise(rb_eTypeError, "can't convert nil into Integer");
	    break;

	default:
	    break;
    }
    if (base != 0) {
	tmp = rb_check_string_type(val);
	if (!NIL_P(tmp)) {
	    goto string_conv;
	}
arg_error:
	rb_raise(rb_eArgError, "base specified for non string value");
    }
    tmp = convert_type(val, "Integer", "to_int", FALSE);
    if (NIL_P(tmp)) {
	return rb_to_integer(val, "to_i");
    }
    return tmp;

}

VALUE
rb_Integer(VALUE val)
{
    return rb_convert_to_integer(val, 0);
}

/*
 *  call-seq:
 *     Integer(arg)    => integer
 *  
 *  Converts <i>arg</i> to a <code>Fixnum</code> or <code>Bignum</code>.
 *  Numeric types are converted directly (with floating point numbers
 *  being truncated). If <i>arg</i> is a <code>String</code>, leading
 *  radix indicators (<code>0</code>, <code>0b</code>, and
 *  <code>0x</code>) are honored. Others are converted using
 *  <code>to_int</code> and <code>to_i</code>. This behavior is
 *  different from that of <code>String#to_i</code>.
 *     
 *     Integer(123.999)    #=> 123
 *     Integer("0x1a")     #=> 26
 *     Integer(Time.new)   #=> 1204973019
 */

static VALUE
rb_f_integer(VALUE obj, SEL sel, int argc, VALUE *argv)
{
    VALUE arg = Qnil;
    int base = 0;

    switch (argc) {
	case 2:
	    base = NUM2INT(argv[1]);
	case 1:
	    arg = argv[0];
	    break;
	default:
	    // Should cause ArgumentError.
	    rb_scan_args(argc, argv, "11", NULL, NULL);
    }
    return rb_convert_to_integer(arg, base);
}

double
rb_cstr_to_dbl(const char *p, int badcheck)
{
    const char *q;
    char *end;
    double d;
    const char *ellipsis = "";
    int w;
    enum {max_width = 20};
#define OutOfRange() ((end - p > max_width) ? \
		      (w = max_width, ellipsis = "...") : \
		      (w = (int)(end - p), ellipsis = ""))

    if (!p) return 0.0;
    q = p;
    while (ISSPACE(*p)) p++;

    if (!badcheck && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
	return 0.0;
    }

    d = ruby_strtod(p, &end);
    if (errno == ERANGE) {
	OutOfRange();
	rb_warning("Float %.*s%s out of range", w, p, ellipsis);
	errno = 0;
    }
    if (p == end) {
	if (badcheck) {
	  bad:
	    rb_invalid_str(q, "Float()");
	}
	return d;
    }
    if (*end) {
	char buf[DBL_DIG * 4 + 10];
	char *n = buf;
	char *e = buf + sizeof(buf) - 1;
	char prev = 0;

	while (p < end && n < e) prev = *n++ = *p++;
	while (*p) {
	    if (*p == '_') {
		/* remove underscores between digits */
		if (badcheck) {
		    if (n == buf || !ISDIGIT(prev)) goto bad;
		    ++p;
		    if (!ISDIGIT(*p)) goto bad;
		}
		else {
		    while (*++p == '_');
		    continue;
		}
	    }
	    prev = *p++;
	    if (n < e) *n++ = prev;
	}
	*n = '\0';
	p = buf;

	if (!badcheck && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
	    return 0.0;
	}

	d = ruby_strtod(p, &end);
	if (errno == ERANGE) {
	    OutOfRange();
	    rb_warning("Float %.*s%s out of range", w, p, ellipsis);
	    errno = 0;
	}
	if (badcheck) {
	    if (!end || p == end) goto bad;
	    while (*end && ISSPACE(*end)) end++;
	    if (*end) goto bad;
	}
    }
    if (errno == ERANGE) {
	errno = 0;
	OutOfRange();
	rb_raise(rb_eArgError, "Float %.*s%s out of range", w, q, ellipsis);
    }
    return d;
}
double
rb_str_to_dbl(VALUE str, int badcheck)
{
    const char *s;
    long len;

    StringValue(str);
    s = RSTRING_PTR(str);
    len = RSTRING_LEN(str);
    if (s) {
	if (badcheck && memchr(s, '\0', len)) {
	    rb_raise(rb_eArgError, "string for Float contains null byte");
	}
	if (s[len]) {		/* no sentinel somehow */
	    char *p = ALLOCA_N(char, len+1);

	    MEMCPY(p, s, char, len);
	    p[len] = '\0';
	    s = p;
	}
    }
    return rb_cstr_to_dbl(s, badcheck);
}

VALUE
rb_Float(VALUE val)
{
    switch (TYPE(val)) {
      case T_FIXNUM:
	return DOUBLE2NUM((double)FIX2LONG(val));

      case T_FLOAT:
	return val;

      case T_BIGNUM:
	return DOUBLE2NUM(rb_big2dbl(val));

      case T_STRING:
	return DOUBLE2NUM(rb_str_to_dbl(val, 1));

      case T_NIL:
	rb_raise(rb_eTypeError, "can't convert nil into Float");
	break;

      default:
	return rb_convert_type(val, T_FLOAT, "Float", "to_f");
    }
}

/*
 *  call-seq:
 *     Float(arg)    => float
 *  
 *  Returns <i>arg</i> converted to a float. Numeric types are converted
 *  directly, the rest are converted using <i>arg</i>.to_f. As of Ruby
 *  1.8, converting <code>nil</code> generates a <code>TypeError</code>.
 *     
 *     Float(1)           #=> 1.0
 *     Float("123.456")   #=> 123.456
 */

static VALUE
rb_f_float(VALUE obj, SEL sel, VALUE arg)
{
    return rb_Float(arg);
}

VALUE
rb_to_float(VALUE val)
{
    if (TYPE(val) ==  T_FLOAT) return val;
    if (!rb_obj_is_kind_of(val, rb_cNumeric)) {
	rb_raise(rb_eTypeError, "can't convert %s into Float",
		 NIL_P(val) ? "nil" :
		 val ==  Qtrue ? "true" :
		 val ==  Qfalse ? "false" :
		 rb_obj_classname(val));
    }
    return rb_convert_type(val, T_FLOAT, "Float", "to_f");
}

VALUE
rb_check_to_float(VALUE val)
{
    if (TYPE(val) == T_FLOAT) {
	return val;
    }
    if (!rb_obj_is_kind_of(val, rb_cNumeric)) {
        return Qnil;
    }
    return rb_check_convert_type(val, T_FLOAT, "Float", "to_f");
}

double
rb_num2dbl(VALUE val)
{
    switch (TYPE(val)) {
      case T_FLOAT:
	return RFLOAT_VALUE(val);

      case T_STRING:
	rb_raise(rb_eTypeError, "no implicit conversion to float from string");
	break;

      case T_NIL:
	rb_raise(rb_eTypeError, "no implicit conversion to float from nil");
	break;

      default:
	break;
    }

    return RFLOAT_VALUE(rb_Float(val));
}

VALUE
rb_Array(VALUE val)
{
    VALUE tmp = rb_check_array_type(val);

    if (NIL_P(tmp)) {
	tmp = rb_check_convert_type(val, T_ARRAY, "Array", "to_a");
	if (NIL_P(tmp)) {
	    return rb_ary_new4(1, &val);
	}
    }
    return tmp;
}

VALUE
rb_String(VALUE val)
{
    return rb_convert_type(val, T_STRING, "String", "to_s");
}

static VALUE
boot_defclass(const char *name, VALUE super)
{
    ID id = rb_intern(name);
    extern st_table *rb_class_tbl;
    VALUE obj = rb_objc_create_class(name, super);
    st_add_direct(rb_class_tbl, id, obj);
    rb_const_set((rb_cObject ? rb_cObject : obj), id, obj);
    return obj;
}

static VALUE
rb_class_is_meta(VALUE klass, SEL sel)
{
    return RCLASS_META(klass) ? Qtrue : Qfalse;
}

/*
 *  Document-class: Class
 *
 *  Classes in Ruby are first-class objects---each is an instance of
 *  class <code>Class</code>.
 *     
 *  When a new class is created (typically using <code>class Name ...
 *  end</code>), an object of type <code>Class</code> is created and
 *  assigned to a global constant (<code>Name</code> in this case). When
 *  <code>Name.new</code> is called to create a new object, the
 *  <code>new</code> method in <code>Class</code> is run by default.
 *  This can be demonstrated by overriding <code>new</code> in
 *  <code>Class</code>:
 *     
 *     class Class
 *        alias oldNew  new
 *        def new(*args)
 *          print "Creating a new ", self.name, "\n"
 *          oldNew(*args)
 *        end
 *      end
 *     
 *     
 *      class Name
 *      end
 *     
 *     
 *      n = Name.new
 *     
 *  <em>produces:</em>
 *     
 *     Creating a new Name
 *     
 *  Classes, modules, and objects are interrelated. In the diagram
 *  that follows, the vertical arrows represent inheritance, and the
 *  parentheses meta-classes. All metaclasses are instances 
 *  of the class `Class'.
 *
 *                             +-----------------+
 *                            |                  |
 *           BasicObject-->(BasicObject)         |
 *                ^           ^                  |
 *                |           |                  |
 *              Object---->(Object)              |
 *               ^  ^        ^  ^                |
 *               |  |        |  |                |
 *               |  |  +-----+  +---------+      |
 *               |  |  |                  |      |
 *               |  +-----------+         |      |
 *               |     |        |         |      |
 *        +------+     |     Module--->(Module)  |
 *        |            |        ^         ^      |
 *   OtherClass-->(OtherClass)  |         |      |
 *                              |         |      |
 *                            Class---->(Class)  |
 *                              ^                |
 *                              |                |
 *                              +----------------+
 */


/*
 *  <code>BasicObject</code> is the parent class of all classes in Ruby.
 *  It's an explicit blank class.  <code>Object</code>, the root of Ruby's
 *  class hierarchy is a direct subclass of <code>BasicObject</code>.  Its
 *  methods are therefore available to all objects unless explicitly
 *  overridden.
 *     
 *  <code>Object</code> mixes in the <code>Kernel</code> module, making
 *  the built-in kernel functions globally accessible. Although the
 *  instance methods of <code>Object</code> are defined by the
 *  <code>Kernel</code> module, we have chosen to document them here for
 *  clarity.
 *     
 *  In the descriptions of Object's methods, the parameter <i>symbol</i> refers
 *  to a symbol, which is either a quoted string or a
 *  <code>Symbol</code> (such as <code>:name</code>).
 */

VALUE rb_f_sprintf_imp(VALUE recv, SEL sel, int argc, VALUE *argv);

void
Init_Object(void)
{
    rb_cObject = rb_cNSObject = (VALUE)objc_getClass("NSObject");
    rb_const_set(rb_cObject, rb_intern("Object"), rb_cNSObject);
    rb_set_class_path(rb_cObject, rb_cObject, "NSObject");
    rb_cBasicObject = (VALUE)objc_duplicateClass((Class)rb_cObject,
	    "BasicObject", 0);
    rb_const_set(rb_cObject, rb_intern("BasicObject"), rb_cBasicObject);
    rb_cModule = boot_defclass("Module", rb_cNSObject);
    rb_define_object_special_methods(rb_cModule);
    rb_cClass =  boot_defclass("Class",  rb_cModule);
    rb_cRubyObject = boot_defclass("RubyObject", rb_cObject);
    RCLASS_SET_VERSION_FLAG(rb_cRubyObject, RCLASS_IS_OBJECT_SUBCLASS);
    rb_define_object_special_methods(rb_cRubyObject);
    rb_objc_install_NSObject_special_methods((Class)rb_cRubyObject);

    // Every instance of the Module class inherits from this class, which is
    // a duplicate of NSObject. This is because the CRuby standard does not
    // make Module instances inherit from Object (and Kernel).
    rb_cModuleObject = (VALUE)objc_duplicateClass((Class)rb_cObject,
	    "__ModuleObject", 0);
    assert(rb_cModuleObject != 0);

    eqlSel = sel_registerName("eql?:");

    rb_objc_define_method(*(VALUE *)rb_cModule, "alloc", rb_module_s_alloc, 0);
    rb_objc_define_method(*(VALUE *)rb_cClass, "alloc", rb_class_s_alloc, 0);
    rb_objc_define_method(rb_cClass, "new", rb_class_new_instance_imp, -1);

    // At this point, methods defined on Class or Module will be automatically
    // added to NSObject's metaclass.
    rb_include_module2(*(VALUE *)rb_cNSObject, 0, rb_cClass, false, false);
    rb_include_module2(*(VALUE *)rb_cNSObject, 0, rb_cModule, false, false);

    rb_objc_define_direct_method(*(VALUE *)rb_cNSObject, "new:", rb_class_new_instance_imp, -1);

    rb_objc_define_private_method(rb_cNSObject, "initialize", rb_obj_dummy, 0);
    rb_objc_define_method(rb_cRubyObject, "==", rb_obj_equal, 1);
    rb_objc_define_method(rb_cNSObject, "equal?", rb_obj_equal, 1);
    rb_objc_define_method(rb_cNSObject, "==", rb_nsobj_equal, 1);
    rb_objc_define_method(rb_cNSObject, "!", rb_obj_not, 0);
    rb_objc_define_method(rb_cNSObject, "!=", rb_obj_not_equal, 1);

    rb_objc_define_private_method(rb_cBasicObject, "initialize", rb_obj_dummy, 0);
    rb_objc_define_method(rb_cBasicObject, "==", rb_obj_equal, 1);
    rb_objc_define_method(rb_cBasicObject, "equal?", rb_obj_equal, 1);
    rb_objc_define_method(rb_cBasicObject, "!", rb_obj_not, 0);
    rb_objc_define_method(rb_cBasicObject, "!=", rb_obj_not_equal, 1);

    rb_objc_define_private_method(rb_cNSObject, "singleton_method_added", rb_obj_dummy2, 1);
    rb_objc_define_private_method(rb_cNSObject, "singleton_method_removed", rb_obj_dummy2, 1);
    rb_objc_define_private_method(rb_cNSObject, "singleton_method_undefined", rb_obj_dummy2, 1);

    rb_objc_define_private_method(rb_cBasicObject, "singleton_method_added", rb_obj_dummy2, 1);
    rb_objc_define_private_method(rb_cBasicObject, "singleton_method_removed", rb_obj_dummy2, 1);
    rb_objc_define_private_method(rb_cBasicObject, "singleton_method_undefined", rb_obj_dummy2, 1);

    rb_mKernel = rb_define_module("Kernel");
    rb_include_module(rb_cNSObject, rb_mKernel);
    rb_objc_define_private_method(rb_cClass, "inherited", rb_obj_dummy2, 1);
    rb_objc_define_private_method(rb_cModule, "included", rb_obj_dummy2, 1);
    rb_objc_define_private_method(rb_cModule, "extended", rb_obj_dummy2, 1);
    rb_objc_define_private_method(rb_cModule, "method_added", rb_obj_dummy2, 1);
    rb_objc_define_private_method(rb_cModule, "method_removed", rb_obj_dummy2, 1);
    rb_objc_define_private_method(rb_cModule, "method_undefined", rb_obj_dummy2, 1);
    rb_objc_define_private_method(rb_cModule, "method_signature", rb_mod_method_signature, 2);

    rb_objc_define_method(rb_mKernel, "nil?", rb_false, 0);
    rb_objc_define_method(rb_mKernel, "===", rb_equal_imp, 1); 
    rb_objc_define_method(rb_mKernel, "=~", rb_obj_match, 1);
    rb_objc_define_method(rb_mKernel, "!~", rb_obj_not_match, 1);
    rb_objc_define_method(rb_mKernel, "eql?", rb_obj_equal, 1);
    rb_objc_define_method(rb_cNSObject, "eql?", rb_nsobj_equal, 1);
    rb_objc_define_method(rb_mKernel, "hash", rb_obj_hash, 0);
    rb_objc_define_method(rb_mKernel, "<=>", rb_obj_cmp, 1);

    rb_objc_define_method(rb_mKernel, "singleton_class", rb_obj_singleton_class, 0);
    rb_objc_define_method(rb_cNSObject, "clone", rb_obj_clone_imp, 0);
    rb_objc_define_method(rb_cNSObject, "dup", rb_nsobj_dup, 0);
    rb_objc_define_method(rb_cNSObject, "__type__", rb_obj_type, 0);

    rb_objc_define_method(rb_mKernel, "initialize_copy", rb_obj_init_copy, 1);
    rb_objc_define_method(rb_mKernel, "initialize_dup", rb_obj_init_dup_clone, 1);
    rb_objc_define_method(rb_mKernel, "initialize_clone", rb_obj_init_dup_clone, 1);

    rb_objc_define_method(rb_mKernel, "taint", rb_obj_taint_m, 0);
    rb_objc_define_method(rb_mKernel, "tainted?", rb_obj_tainted_p, 0);
    rb_objc_define_method(rb_mKernel, "untaint", rb_obj_untaint_m, 0);
    rb_objc_define_method(rb_mKernel, "freeze", rb_obj_freeze_m, 0);
    rb_objc_define_method(rb_mKernel, "frozen?", rb_obj_frozen, 0);
    rb_objc_define_method(rb_mKernel, "trust", rb_obj_trust_imp, 0);
    rb_objc_define_method(rb_mKernel, "untrust", rb_obj_untrust_imp, 0);
    rb_objc_define_method(rb_mKernel, "untrusted?", rb_obj_untrusted_imp, 0);

    rb_objc_define_method(rb_mKernel, "to_s", rb_any_to_string, 0);
    rb_objc_define_method(rb_mKernel, "inspect", rb_obj_inspect, 0);
    rb_objc_define_method(rb_mKernel, "methods", rb_obj_methods, -1);

    VALUE rb_obj_singleton_methods(VALUE obj, SEL sel, int argc, VALUE *argv);
    rb_objc_define_method(rb_mKernel, "singleton_methods", rb_obj_singleton_methods, -1); /* in class.c */
    rb_objc_define_method(rb_mKernel, "protected_methods", rb_obj_protected_methods, -1);
    rb_objc_define_method(rb_mKernel, "private_methods", rb_obj_private_methods, -1);
    rb_objc_define_method(rb_mKernel, "public_methods", rb_obj_public_methods, -1);
    rb_objc_define_method(rb_mKernel, "instance_variables", rb_obj_instance_variables, 0); /* in variable.c */
    rb_objc_define_method(rb_mKernel, "instance_variable_get", rb_obj_ivar_get, 1);
    rb_objc_define_method(rb_mKernel, "instance_variable_set", rb_obj_ivar_set, 2);
    rb_objc_define_method(rb_mKernel, "instance_variable_defined?", rb_obj_ivar_defined, 1);
    VALUE rb_obj_remove_instance_variable(VALUE obj, SEL sel, VALUE other);
    rb_objc_define_private_method(rb_mKernel, "remove_instance_variable",
	    rb_obj_remove_instance_variable, 1); /* in variable.c */

    rb_objc_define_method(rb_mKernel, "instance_of?", rb_obj_is_instance_of_imp, 1);
    rb_objc_define_method(rb_mKernel, "kind_of?", rb_obj_is_kind_of_imp, 1);
    rb_objc_define_method(rb_mKernel, "is_a?", rb_obj_is_kind_of_imp, 1);
    rb_objc_define_method(rb_mKernel, "tap", rb_obj_tap, 0);

    rb_objc_define_module_function(rb_mKernel, "sprintf", rb_f_sprintf_imp, -1); /* in sprintf.c */
    rb_objc_define_module_function(rb_mKernel, "format", rb_f_sprintf_imp, -1);  /* in sprintf.c */

    rb_objc_define_module_function(rb_mKernel, "Integer", rb_f_integer, -1);
    rb_objc_define_module_function(rb_mKernel, "Float", rb_f_float, 1);
    rb_objc_define_module_function(rb_mKernel, "String", rb_f_string, 1); /* in string.c */
    rb_objc_define_module_function(rb_mKernel, "Array", rb_f_array, 1); /* in array.c */

    rb_const_set(rb_cObject, rb_intern("NSNull"), (VALUE)objc_getClass("NSNull"));

    rb_cNilClass = rb_define_class("NilClass", rb_cObject);
    rb_objc_define_method(rb_cNilClass, "to_i", nil_to_i, 0);
    rb_objc_define_method(rb_cNilClass, "to_f", nil_to_f, 0);
    rb_objc_define_method(rb_cNilClass, "to_s", nil_to_s, 0);
    rb_objc_define_method(rb_cNilClass, "to_a", nil_to_a, 0);
    rb_objc_define_method(rb_cNilClass, "inspect", nil_inspect, 0);
    rb_objc_define_method(rb_cNilClass, "&", false_and, 1);
    rb_objc_define_method(rb_cNilClass, "|", false_or, 1);
    rb_objc_define_method(rb_cNilClass, "^", false_xor, 1);

    rb_objc_define_method(rb_cNilClass, "nil?", rb_true, 0);
    rb_undef_method(*(VALUE *)rb_cNilClass, "alloc");
    rb_undef_method(*(VALUE *)rb_cNilClass, "new");
    rb_define_global_const("NIL", Qnil);

    rb_objc_define_method(rb_cModule, "freeze", rb_mod_freeze, 0);
    rb_objc_define_method(rb_cModule, "===", rb_mod_eqq, 1);
    rb_objc_define_method(rb_cModule, "==", rb_obj_equal, 1);
    rb_objc_define_method(rb_cModule, "<=>",  rb_mod_cmp, 1);
    rb_objc_define_method(rb_cModule, "<",  rb_mod_lt, 1);
    rb_objc_define_method(rb_cModule, "<=", rb_class_inherited_imp, 1);
    rb_objc_define_method(rb_cModule, ">",  rb_mod_gt, 1);
    rb_objc_define_method(rb_cModule, ">=", rb_mod_ge, 1);
    VALUE rb_mod_init_copy(VALUE recv, SEL sel, VALUE copy);
    rb_objc_define_method(rb_cModule, "initialize_copy", rb_mod_init_copy, 1); /* in class.c */
    rb_objc_define_method(rb_cModule, "to_s", rb_mod_to_s, 0);
    rb_objc_define_method(rb_cModule, "included_modules", rb_mod_included_modules_imp, 0);
    VALUE rb_mod_include_p(VALUE, SEL, VALUE);
    rb_objc_define_method(rb_cModule, "include?", rb_mod_include_p, 1); /* in class.c */
    VALUE rb_mod_name(VALUE, SEL);
    rb_objc_define_method(rb_cModule, "name", rb_mod_name, 0);  /* in variable.c */
    rb_objc_define_method(rb_cModule, "ancestors", rb_mod_ancestors_imp, 0);
    rb_objc_define_method(rb_cModule, "__ancestors__", rb_mod_ancestors_all_imp, 0);
    rb_objc_define_method(rb_cModule, "__properties__", rb_mod_properties_imp, 0);
    VALUE rb_mod_objc_ib_outlet(VALUE, SEL, int, VALUE *);
    rb_objc_define_private_method(rb_cModule, "ib_outlet", rb_mod_objc_ib_outlet, -1); /* in objc.m */
    rb_objc_define_method(rb_cClass, "__meta__?", rb_class_is_meta, 0);

    rb_objc_define_private_method(rb_cModule, "attr", rb_mod_attr, -1);
    rb_objc_define_private_method(rb_cModule, "attr_reader", rb_mod_attr_reader, -1);
    rb_objc_define_private_method(rb_cModule, "attr_writer", rb_mod_attr_writer, -1);
    rb_objc_define_private_method(rb_cModule, "attr_accessor", rb_mod_attr_accessor, -1);

    rb_objc_define_method(rb_cModule, "instance_methods", rb_class_instance_methods, -1); /* in class.c */
    rb_objc_define_method(rb_cModule, "public_instance_methods", 
		     rb_class_public_instance_methods, -1);    /* in class.c */
    rb_objc_define_method(rb_cModule, "protected_instance_methods", 
		     rb_class_protected_instance_methods, -1); /* in class.c */
    rb_objc_define_method(rb_cModule, "private_instance_methods", 
		     rb_class_private_instance_methods, -1);   /* in class.c */

    VALUE rb_mod_constants(VALUE, SEL, int, VALUE *);
    rb_objc_define_method(rb_cModule, "constants", rb_mod_constants, -1); /* in variable.c */
    rb_objc_define_method(rb_cModule, "const_get", rb_mod_const_get, -1);
    rb_objc_define_method(rb_cModule, "const_set", rb_mod_const_set, 2);
    rb_objc_define_method(rb_cModule, "const_defined?", rb_mod_const_defined, -1);
    rb_objc_define_private_method(rb_cModule, "remove_const", 
			     rb_mod_remove_const_imp, 1); /* in variable.c */
    VALUE rb_mod_const_missing(VALUE, SEL, VALUE);
    rb_objc_define_method(rb_cModule, "const_missing", 
		     rb_mod_const_missing, 1); /* in variable.c */
    VALUE rb_mod_class_variables(VALUE, SEL);
    rb_objc_define_method(rb_cModule, "class_variables", 
		     rb_mod_class_variables, 0); /* in variable.c */
    VALUE rb_mod_remove_cvar(VALUE, SEL, VALUE);
    rb_objc_define_method(rb_cModule, "remove_class_variable", 
		     rb_mod_remove_cvar, 1); /* in variable.c */
    rb_objc_define_method(rb_cModule, "class_variable_get", rb_mod_cvar_get, 1);
    rb_objc_define_method(rb_cModule, "class_variable_set", rb_mod_cvar_set, 2);
    rb_objc_define_method(rb_cModule, "class_variable_defined?", rb_mod_cvar_defined, 1);

    rb_objc_define_method(rb_cClass, "allocate", rb_obj_alloc_imp, 0);
    VALUE rb_class_init_copy(VALUE, SEL, VALUE);
    rb_objc_define_method(rb_cClass, "initialize_copy", rb_class_init_copy, 1); /* in class.c */
    rb_undef_method(rb_cClass, "extend_object");
    rb_undef_method(rb_cClass, "append_features");

    rb_cData = rb_define_class("Data", rb_cObject);
    rb_undef_method(*(VALUE *)rb_cData, "alloc");

    rb_cTrueClass = rb_define_class("TrueClass", rb_cObject);
    rb_objc_define_method(rb_cTrueClass, "to_s", true_to_s, 0);
    rb_objc_define_method(rb_cTrueClass, "&", true_and, 1);
    rb_objc_define_method(rb_cTrueClass, "|", true_or, 1);
    rb_objc_define_method(rb_cTrueClass, "^", true_xor, 1);
    rb_undef_method(*(VALUE *)rb_cTrueClass, "alloc");
    rb_undef_method(*(VALUE *)rb_cTrueClass, "new");
    rb_define_global_const("TRUE", Qtrue);

    rb_cFalseClass = rb_define_class("FalseClass", rb_cObject);
    rb_objc_define_method(rb_cFalseClass, "to_s", false_to_s, 0);
    rb_objc_define_method(rb_cFalseClass, "&", false_and, 1);
    rb_objc_define_method(rb_cFalseClass, "|", false_or, 1);
    rb_objc_define_method(rb_cFalseClass, "^", false_xor, 1);
    rb_undef_method(*(VALUE *)rb_cFalseClass, "alloc");
    rb_undef_method(*(VALUE *)rb_cFalseClass, "new");
    rb_define_global_const("FALSE", Qfalse);

    id_eq = rb_intern("==");
    id_match = rb_intern("=~");
    id_inspect = rb_intern("inspect");
    id_init_copy = rb_intern("initialize_copy");
}
