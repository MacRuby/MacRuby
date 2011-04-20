#include <Foundation/Foundation.h>
#include "ruby/ruby.h"

static VALUE
ocid_str(VALUE obj)
{
    char buf[64];
    snprintf(buf, sizeof buf, "%ld", obj);
    return rb_str_new2(buf);
}

static VALUE
test_arity0(VALUE rcv)
{
    return ocid_str(rcv);
}

static VALUE
test_arity1(VALUE rcv, VALUE arg1)
{
    VALUE str = ocid_str(rcv);
    rb_str_concat(str, ocid_str(arg1));
    return str;
}

static VALUE
test_arity2(VALUE rcv, VALUE arg1, VALUE arg2)
{
    VALUE str = ocid_str(rcv);
    rb_str_concat(str, ocid_str(arg1));
    rb_str_concat(str, ocid_str(arg2));
    return str;
}

static VALUE
test_arity3(VALUE rcv, VALUE arg1, VALUE arg2, VALUE arg3)
{
    VALUE str = ocid_str(rcv);
    rb_str_concat(str, ocid_str(arg1));
    rb_str_concat(str, ocid_str(arg2));
    rb_str_concat(str, ocid_str(arg3));
    return str;
}

static VALUE
test_arity4(VALUE rcv, VALUE arg1, VALUE arg2, VALUE arg3, VALUE arg4)
{
    VALUE str = ocid_str(rcv);
    rb_str_concat(str, ocid_str(arg1));
    rb_str_concat(str, ocid_str(arg2));
    rb_str_concat(str, ocid_str(arg3));
    rb_str_concat(str, ocid_str(arg4));
    return str;
}

static VALUE
test_arity5(VALUE rcv, VALUE arg1, VALUE arg2, VALUE arg3, VALUE arg4, VALUE arg5)
{
    VALUE str = ocid_str(rcv);
    rb_str_concat(str, ocid_str(arg1));
    rb_str_concat(str, ocid_str(arg2));
    rb_str_concat(str, ocid_str(arg3));
    rb_str_concat(str, ocid_str(arg4));
    rb_str_concat(str, ocid_str(arg5));
    return str;
}

static VALUE
test_arity6(VALUE rcv, VALUE arg1, VALUE arg2, VALUE arg3, VALUE arg4, VALUE arg5, VALUE arg6)
{
    VALUE str = ocid_str(rcv);
    rb_str_concat(str, ocid_str(arg1));
    rb_str_concat(str, ocid_str(arg2));
    rb_str_concat(str, ocid_str(arg3));
    rb_str_concat(str, ocid_str(arg4));
    rb_str_concat(str, ocid_str(arg5));
    rb_str_concat(str, ocid_str(arg6));
    return str;
}

static VALUE
test_arity_m1(int argc, VALUE *argv, VALUE rcv)
{
    VALUE str = ocid_str(rcv);
    int i;
    for (i = 0; i < argc; i++) {
	rb_str_concat(str, ocid_str(argv[i]));
    }
    return str;
}

static VALUE
test_arity_m2(VALUE rcv, VALUE argv)
{
    VALUE str = ocid_str(rcv);
    int i;
    for (i = 0; i < RARRAY_LEN(argv); i++) {
	rb_str_concat(str, ocid_str(rb_ary_entry(argv, i)));
    }
    return str;
}

void
Init_mri_abi(void)
{
    VALUE klass = rb_define_class("MRI_ABI_TEST", rb_cObject);
    rb_define_method(klass, "test_arity0", test_arity0, 0);
    rb_define_method(klass, "test_arity1", test_arity1, 1);
    rb_define_method(klass, "test_arity2", test_arity2, 2);
    rb_define_method(klass, "test_arity3", test_arity3, 3);
    rb_define_method(klass, "test_arity4", test_arity4, 4);
    rb_define_method(klass, "test_arity5", test_arity5, 5);
    rb_define_method(klass, "test_arity6", test_arity6, 6);
    rb_define_method(klass, "test_arity_m1", test_arity_m1, -1);
    rb_define_method(klass, "test_arity_m2", test_arity_m2, -2);
}
