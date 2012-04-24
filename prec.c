/*
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2007-2011, Apple Inc. All rights reserved.
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 */

#include "macruby_internal.h"

VALUE rb_mPrecision;

static ID prc_pr, prc_if;


/*
 *  call-seq:
 *   num.prec(klass)   => a_class
 *
 *  Converts _self_ into an instance of _klass_. By default,
 *  +prec+ invokes 
 *
 *     klass.induced_from(num)
 *
 *  and returns its value. So, if <code>klass.induced_from</code>
 *  doesn't return an instance of _klass_, it will be necessary
 *  to reimplement +prec+.
 */

static VALUE
prec_prec(VALUE x, SEL sel, VALUE klass)
{
    return rb_funcall(klass, prc_if, 1, x);
}

/*
 *  call-seq:
 *    num.prec_i  =>  Integer
 *
 *  Returns an +Integer+ converted from _num_. It is equivalent 
 *  to <code>prec(Integer)</code>.
 */

static VALUE
prec_prec_i(VALUE x, SEL sel)
{
    VALUE klass = rb_cInteger;

    return rb_funcall(x, prc_pr, 1, klass);
}

/*
 *  call-seq:
 *    num.prec_f  =>  Float
 *
 *  Returns a +Float+ converted from _num_. It is equivalent 
 *  to <code>prec(Float)</code>.
 */

static VALUE
prec_prec_f(VALUE x, SEL sel)
{
    VALUE klass = rb_cFloat;

    return rb_funcall(x, prc_pr, 1, klass);
}

/*
 * call-seq:
 *   Mod.induced_from(number)  =>  a_mod
 * 
 * Creates an instance of mod from. This method is overridden
 * by concrete +Numeric+ classes, so that (for example)
 *
 *   Fixnum.induced_from(9.9)   #=>  9
 *
 * Note that a use of +prec+ in a redefinition may cause
 * an infinite loop.
 */

static VALUE
prec_induced_from(VALUE module, SEL sel, VALUE x)
{
    rb_raise(rb_eTypeError, "undefined conversion from %s into %s",
            rb_obj_classname(x), rb_class2name(module));
    return Qnil;		/* not reached */
}

/*
 * call_seq:
 *   included
 *
 * When the +Precision+ module is mixed-in to a class, this +included+
 * method is used to add our default +induced_from+ implementation
 * to the host class.
 */

static VALUE
prec_included(VALUE module, SEL sel, VALUE include)
{
    switch (TYPE(include)) {
      case T_CLASS:
      case T_MODULE:
       break;
      default:
       Check_Type(include, T_CLASS);
       break;
    }
    rb_objc_define_method(*(VALUE *)include, "induced_from", prec_induced_from, 1);
    return module;
}

/*
 * Precision is a mixin for concrete numeric classes with
 * precision.  Here, `precision' means the fineness of approximation
 * of a real number, so, this module should not be included into
 * anything which is not a subset of Real (so it should not be
 * included in classes such as +Complex+ or +Matrix+).
*/

void
Init_Precision(void)
{
    rb_mPrecision = rb_define_module("Precision");
    rb_objc_define_method(*(VALUE *)rb_mPrecision, "included", prec_included, 1);
    rb_objc_define_method(rb_mPrecision, "prec", prec_prec, 1);
    rb_objc_define_method(rb_mPrecision, "prec_i", prec_prec_i, 0);
    rb_objc_define_method(rb_mPrecision, "prec_f", prec_prec_f, 0);

    prc_pr = rb_intern("prec");
    prc_if = rb_intern("induced_from");
}
