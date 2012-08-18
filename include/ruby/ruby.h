/*
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2007-2010, Apple Inc. All rights reserved
 * Copyright (C) 1993-2008 Yukihiro Matsumoto
 * Copyright (C) 2000  Network Applied Communication Laboratory, Inc.
 * Copyright (C) 2000  Information-technology Promotion Agency, Japan
 */

#ifndef RUBY_RUBY_H
#define RUBY_RUBY_H 1

#if defined(__cplusplus)
extern "C" {
#endif

#ifndef RUBY_LIB
# if RUBY_INCLUDED_AS_FRAMEWORK
#  include <MacRuby/ruby/config.h>
# else
#  include "ruby/config.h"
# endif
#endif

#define NORETURN_STYLE_NEW 1
#ifndef NORETURN
# define NORETURN(x) x
#endif
#ifndef DEPRECATED
# define DEPRECATED(x) x
#endif
#ifndef NOINLINE
# define NOINLINE(x) x
#endif

#ifdef __GNUC__
# define PRINTF_ARGS(decl, string_index, first_to_check) \
    decl __attribute__((format(printf, string_index, first_to_check)))
#else
# define PRINTF_ARGS(decl, string_index, first_to_check) decl
#endif

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/time.h>

#if RUBY_INCLUDED_AS_FRAMEWORK
# include <MacRuby/ruby/defines.h>
#else
# include "defines.h"
#endif

#include <alloca.h>

#if SIZEOF_LONG == SIZEOF_VOIDP
typedef unsigned long VALUE;
# define ID unsigned long
# define SIGNED_VALUE long
# define SIZEOF_VALUE SIZEOF_LONG
# define PRIdVALUE "ld"
# define PRIiVALUE "li"
# define PRIoVALUE "lo"
# define PRIuVALUE "lu"
# define PRIxVALUE "lx"
# define PRIXVALUE "lX"
# define PRI_TIMET_PREFIX "l"
#elif SIZEOF_LONG_LONG == SIZEOF_VOIDP
typedef unsigned LONG_LONG VALUE;
typedef unsigned LONG_LONG ID;
# define SIGNED_VALUE LONG_LONG
# define LONG_LONG_VALUE 1
# define SIZEOF_VALUE SIZEOF_LONG_LONG
# define PRIdVALUE "lld"
# define PRIiVALUE "lli"
# define PRIoVALUE "llo"
# define PRIuVALUE "llu"
# define PRIxVALUE "llx"
# define PRIXVALUE "llX"
# define PRI_TIMET_PREFIX "ll"
#else
# error ---->> ruby requires sizeof(void*) == sizeof(long) to be compiled. <<----
#endif

#include <limits.h>

#define FIXNUM_MAX (LONG_MAX>>2)
#define FIXNUM_MIN RSHIFT((long)LONG_MIN,2)

#define INT2FIX(i) ((VALUE)(((SIGNED_VALUE)(i))<<2 | FIXNUM_FLAG))
#define LONG2FIX(i) INT2FIX(i)
#define rb_fix_new(v) INT2FIX(v)
#define INT2NUM(v) rb_int2inum(v)
#define LONG2NUM(v) INT2NUM(v)
#define rb_int_new(v) rb_int2inum(v)
#define UINT2NUM(v) rb_uint2inum(v)
#define ULONG2NUM(v) UINT2NUM(v)
#define rb_uint_new(v) rb_uint2inum(v)

#define TIMET2NUM(t) LONG2NUM(t)

#define LL2NUM(v) rb_ll2inum(v)
#define ULL2NUM(v) rb_ull2inum(v)

#if SIZEOF_OFF_T > SIZEOF_LONG
# define OFFT2NUM(v) LL2NUM(v)
#elif SIZEOF_OFF_T == SIZEOF_LONG
# define OFFT2NUM(v) LONG2NUM(v)
#else
# define OFFT2NUM(v) INT2NUM(v)
#endif

#if SIZEOF_SIZE_T > SIZEOF_LONG
# define SIZET2NUM(v) ULL2NUM(v)
#elif SIZEOF_SIZE_T == SIZEOF_LONG
# define SIZET2NUM(v) ULONG2NUM(v)
#else
# define SIZET2NUM(v) UINT2NUM(v)
#endif

#if SIZEOF_INT < SIZEOF_VALUE
NORETURN(void rb_out_of_int(SIGNED_VALUE num));
#endif

#if SIZEOF_INT < SIZEOF_LONG
#define rb_long2int_internal(n, i) \
    int i = (int)(n); \
    if ((long)i != (n)) rb_out_of_int(n)
#ifdef __GNUC__
#define rb_long2int(n) __extension__ ({long i2l_n = (n); rb_long2int_internal(i2l_n, i2l_i); i2l_i;})
#else
static inline int
rb_long2int(long n) {rb_long2int_internal(n, i); return i;}
#endif
#else
#define rb_long2int(n) ((int)(n))
#endif

#ifndef PIDT2NUM
# define PIDT2NUM(v) LONG2NUM(v)
#endif
#ifndef NUM2PIDT
# define NUM2PIDT(v) NUM2LONG(v)
#endif
#ifndef UIDT2NUM
# define UIDT2NUM(v) LONG2NUM(v)
#endif
#ifndef NUM2UIDT
# define NUM2UIDT(v) NUM2LONG(v)
#endif
#ifndef GIDT2NUM
# define GIDT2NUM(v) LONG2NUM(v)
#endif
#ifndef NUM2GIDT
# define NUM2GIDT(v) NUM2LONG(v)
#endif

#define FIX2LONG(x) RSHIFT((SIGNED_VALUE)x,2)
#define FIX2ULONG(x) ((((VALUE)(x))>>2)&LONG_MAX)
#define FIXNUM_P(f) ((((SIGNED_VALUE)(f)) & IMMEDIATE_MASK) == FIXNUM_FLAG)
#define POSFIXABLE(f) ((f) < FIXNUM_MAX+1)
#define NEGFIXABLE(f) ((f) >= FIXNUM_MIN)
#define FIXABLE(f) (POSFIXABLE(f) && NEGFIXABLE(f))

#define IMMEDIATE_P(x) ((VALUE)(x) & IMMEDIATE_MASK)

#if __LP64__
#define VOODOO_DOUBLE(d) (*(VALUE*)(&d))
#define DBL2FIXFLOAT(d) (VOODOO_DOUBLE(d) | FIXFLOAT_FLAG)
#else
// voodoo_float must be a function
// because the parameter must be converted to float
static inline VALUE
voodoo_float(float f)
{
    return *(VALUE *)(&f);
}
#define DBL2FIXFLOAT(d) (voodoo_float(d) | FIXFLOAT_FLAG)
#endif
#define FIXFLOAT_P(v)  (((VALUE)v & IMMEDIATE_MASK) == FIXFLOAT_FLAG)
#define FIXFLOAT2DBL(v) coerce_ptr_to_double((VALUE)v)

#define SYMBOL_P(x) (TYPE(x) == T_SYMBOL)
#define ID2SYM(x) (rb_id2str((ID)x))
#define SYM2ID(x) (rb_sym2id((VALUE)x))

/* special contants - i.e. non-zero and non-fixnum constants */
enum ruby_special_consts {
    RUBY_Qfalse = 0,
    RUBY_Qtrue  = 2,
    RUBY_Qnil   = 4,
    RUBY_Qundef = 6,

    RUBY_IMMEDIATE_MASK = 0x03,
    RUBY_FIXNUM_FLAG    = 0x01,
    RUBY_FIXFLOAT_FLAG	= 0x03,
    RUBY_SPECIAL_SHIFT  = 8,
};

// We can't directly cast a void* to a double, so we cast it to a union
// and then extract its double member. Hacky, but effective.
static inline double
coerce_ptr_to_double(VALUE v)
{
    union {
	VALUE val;
#if __LP64__
	double d;
#else
	float d;
#endif
    } coerced_value;
    coerced_value.val = v & ~RUBY_FIXFLOAT_FLAG; // unset the last two bits.
    return coerced_value.d;
}

#define Qfalse ((VALUE)RUBY_Qfalse)
#define Qtrue  ((VALUE)RUBY_Qtrue)
#define Qnil   ((VALUE)RUBY_Qnil)
#define Qundef ((VALUE)RUBY_Qundef)	/* undefined value for placeholder */
#define IMMEDIATE_MASK RUBY_IMMEDIATE_MASK
#define FIXNUM_FLAG RUBY_FIXNUM_FLAG
#define FIXFLOAT_FLAG RUBY_FIXFLOAT_FLAG

#define RTEST(v) (((VALUE)(v) & ~Qnil) != 0)
#define NIL_P(v) ((VALUE)(v) == Qnil)

#define CLASS_OF(v) rb_class_of((VALUE)(v))

VALUE rb_uint2big(VALUE);
VALUE rb_int2big(SIGNED_VALUE);
VALUE rb_ull2big(unsigned long long n);
VALUE rb_ll2big(long long n);

static inline VALUE
rb_uint2inum(VALUE n)
{
    if (POSFIXABLE(n)) {
	return LONG2FIX(n);
    }
    return rb_uint2big(n);
}

static inline VALUE
rb_int2inum(SIGNED_VALUE n)
{
    if (FIXABLE(n)) {
	return LONG2FIX(n);
    }
    return rb_int2big(n);
}

static inline VALUE
rb_ull2inum(unsigned long long n)
{
    if (POSFIXABLE(n)) {
	return LONG2FIX(n);
    }
    return rb_ull2big(n);
}

static inline VALUE
rb_ll2inum(long long n)
{
    if (FIXABLE(n)) {
	return LONG2FIX(n);
    }
    return rb_ll2big(n);
}

enum ruby_value_type {
    RUBY_T_NONE   = 0x00,

    RUBY_T_OBJECT = 0x01,
    RUBY_T_CLASS  = 0x02,
    RUBY_T_MODULE = 0x03,
    RUBY_T_FLOAT  = 0x04,
    RUBY_T_STRING = 0x05,
    RUBY_T_REGEXP = 0x06,
    RUBY_T_ARRAY  = 0x07,
    RUBY_T_HASH   = 0x08,
    RUBY_T_STRUCT = 0x09,
    RUBY_T_BIGNUM = 0x0a,
    RUBY_T_FILE   = 0x0b,
    RUBY_T_DATA   = 0x0c,
    RUBY_T_MATCH  = 0x0d,
    RUBY_T_COMPLEX  = 0x0e,
    RUBY_T_RATIONAL = 0x0f,

    RUBY_T_NIL    = 0x11,
    RUBY_T_TRUE   = 0x12,
    RUBY_T_FALSE  = 0x13,
    RUBY_T_SYMBOL = 0x14,
    RUBY_T_FIXNUM = 0x15,
    RUBY_T_NATIVE = 0x16,

    RUBY_T_UNDEF  = 0x1b,
    RUBY_T_NODE   = 0x1c,
    RUBY_T_ICLASS = 0x1d,

    RUBY_T_MASK   = 0x1f,
};

#define T_NONE   RUBY_T_NONE
#define T_NIL    RUBY_T_NIL
#define T_OBJECT RUBY_T_OBJECT
#define T_CLASS  RUBY_T_CLASS
#define T_ICLASS RUBY_T_ICLASS
#define T_MODULE RUBY_T_MODULE
#define T_FLOAT  RUBY_T_FLOAT
#define T_STRING RUBY_T_STRING
#define T_REGEXP RUBY_T_REGEXP
#define T_ARRAY  RUBY_T_ARRAY
#define T_HASH   RUBY_T_HASH
#define T_STRUCT RUBY_T_STRUCT
#define T_BIGNUM RUBY_T_BIGNUM
#define T_FILE   RUBY_T_FILE
#define T_FIXNUM RUBY_T_FIXNUM
#define T_NATIVE RUBY_T_NATIVE
#define T_TRUE   RUBY_T_TRUE
#define T_FALSE  RUBY_T_FALSE
#define T_DATA   RUBY_T_DATA
#define T_MATCH  RUBY_T_MATCH
#define T_SYMBOL RUBY_T_SYMBOL
#define T_RATIONAL RUBY_T_RATIONAL
#define T_COMPLEX RUBY_T_COMPLEX
#define T_UNDEF  RUBY_T_UNDEF
#define T_NODE   RUBY_T_NODE
#define T_MASK   RUBY_T_MASK

#define BUILTIN_TYPE(x) (((struct RBasic*)(x))->flags & T_MASK)

#define TYPE(x) rb_type((VALUE)(x))

#define RB_TYPE_P(obj, type) ( \
        ((type) == T_FIXNUM) ? FIXNUM_P(obj) : \
        ((type) == T_TRUE) ? ((obj) == Qtrue) : \
        ((type) == T_FALSE) ? ((obj) == Qfalse) : \
        ((type) == T_NIL) ? ((obj) == Qnil) : \
        ((type) == T_UNDEF) ? ((obj) == Qundef) : \
        ((type) == T_SYMBOL) ? SYMBOL_P(obj) : \
        (!SPECIAL_CONST_P(obj) && BUILTIN_TYPE(obj) == (type)))

#define RB_GC_GUARD(v) (*(volatile VALUE *)&(v))

void rb_check_type(VALUE,int);
#define Check_Type(v,t) rb_check_type((VALUE)(v),t)

VALUE rb_str_to_str(VALUE);
VALUE rb_string_value(volatile VALUE*);
char *rb_string_value_ptr(volatile VALUE*);
char *rb_string_value_cstr(volatile VALUE*);

#define StringValue(v) rb_string_value(&(v))
#define StringValuePtr(v) rb_string_value_ptr(&(v))
#define StringValueCStr(v) rb_string_value_cstr(&(v))

void rb_check_safe_obj(VALUE);
void rb_check_safe_str(VALUE);
#define SafeStringValue(v) do {\
    StringValue(v);\
    rb_check_safe_obj(v);\
} while (0)
/* obsolete macro - use SafeStringValue(v) */
#define Check_SafeStr(v) rb_check_safe_str((VALUE)(v))

VALUE rb_get_path(VALUE);
#define FilePathValue(v) ((v) = rb_get_path(v))

VALUE rb_get_path_no_checksafe(VALUE);
#define FilePathStringValue(v) ((v) = rb_get_path_no_checksafe(v))

void rb_secure(int);
int rb_safe_level(void);
void rb_set_safe_level(int);
void rb_set_safe_level_force(int);
void rb_secure_update(VALUE);
NORETURN(void rb_insecure_operation(void));

VALUE rb_errinfo(void);
void rb_set_errinfo(VALUE);

SIGNED_VALUE rb_num2long(VALUE);
VALUE rb_num2ulong(VALUE);
#define NUM2LONG(x) (FIXNUM_P(x)?FIX2LONG(x):rb_num2long((VALUE)x))
#define NUM2TIMET(v) NUM2LONG(v)
#define NUM2ULONG(x) rb_num2ulong((VALUE)x)
#if SIZEOF_INT < SIZEOF_LONG
long rb_num2int(VALUE);
# define NUM2INT(x) (FIXNUM_P(x)?FIX2INT(x):rb_num2int((VALUE)x))
long rb_fix2int(VALUE);
# define FIX2INT(x) rb_fix2int((VALUE)x)
unsigned long rb_num2uint(VALUE);
# define NUM2UINT(x) rb_num2uint(x)
unsigned long rb_fix2uint(VALUE);
# define FIX2UINT(x) rb_fix2uint(x)
#else
# define NUM2INT(x) ((int)NUM2LONG(x))
# define NUM2UINT(x) ((unsigned int)NUM2ULONG(x))
# define FIX2INT(x) ((int)FIX2LONG(x))
# define FIX2UINT(x) ((unsigned int)FIX2ULONG(x))
#endif

LONG_LONG rb_num2ll(VALUE);
unsigned LONG_LONG rb_num2ull(VALUE);
static inline long long
__num2ll(VALUE obj)
{
    return FIXNUM_P(obj) ? FIX2LONG(obj) : rb_num2ll(obj);
}
#define NUM2LL(x) __num2ll((VALUE)x)
#define NUM2ULL(x) rb_num2ull((VALUE)x)

#if SIZEOF_OFF_T > SIZEOF_LONG
# define NUM2OFFT(x) ((off_t)NUM2LL(x))
#else
# define NUM2OFFT(x) NUM2LONG(x)
#endif

#if SIZEOF_SIZE_T > SIZEOF_LONG
# define NUM2SIZET(x) ((size_t)NUM2ULL(x))
#else
# define NUM2SIZET(x) NUM2ULONG(x)
#endif

double rb_num2dbl(VALUE);
#define NUM2DBL(x) rb_num2dbl((VALUE)(x))

#define NUM2CHR(x) (((TYPE(x) == T_STRING)&&(RSTRING_LEN(x)>=1))?\
                     RSTRING_PTR(x)[0]:(char)(NUM2INT(x)&0xff))
#define CHR2FIX(x) INT2FIX((long)((x)&0xff))

void *rb_objc_newobj(size_t size);
#define NEWOBJ(obj,type) type *obj = (type*)rb_objc_newobj(sizeof(type))
#define OBJSETUP(obj,c,t) do {\
    RBASIC(obj)->flags = (t);\
    if (c != 0) RBASIC(obj)->klass = (c);\
    if (rb_safe_level() >= 3) FL_SET(obj, FL_TAINT);\
} while (0)
#define CLONESETUP(clone,obj) do {\
    OBJSETUP(clone,rb_singleton_class_clone((VALUE)obj),RBASIC(obj)->flags);\
    rb_singleton_class_attached(RBASIC(clone)->klass, (VALUE)clone);\
    if (FL_TEST(obj, FL_EXIVAR)) rb_copy_generic_ivar((VALUE)clone,(VALUE)obj);\
} while (0)
#define DUPSETUP(dup,obj) do {\
    OBJSETUP(dup,rb_obj_class(obj),(RBASIC(obj)->flags)&(T_MASK|FL_EXIVAR|FL_TAINT));\
    if (FL_TEST(obj, FL_EXIVAR)) rb_copy_generic_ivar((VALUE)dup,(VALUE)obj);\
} while (0)

struct RBasic {
    VALUE klass; /* isa */
    VALUE flags;
};

VALUE rb_class_super(VALUE klass);
void rb_class_set_super(VALUE klass, VALUE super);
int rb_class_ismeta(VALUE klass);
#define RCLASS_SUPER(m) (rb_class_super((VALUE)m))
#define RCLASS_SET_SUPER(m, s) (rb_class_set_super((VALUE)m, (VALUE)s))
#define RCLASS_META(m) (rb_class_ismeta((VALUE)m))

#define RFLOAT_VALUE(v) FIXFLOAT2DBL(v)
#define DOUBLE2NUM(dbl)  rb_float_new(dbl)
#define DBL2NUM DOUBLE2NUM

#define ELTS_SHARED FL_USER2

char *rb_str_cstr(VALUE);
long rb_str_clen(VALUE);
#define RSTRING_PTR(str) (rb_str_cstr((VALUE)str))
#define RSTRING_LEN(str) (rb_str_clen((VALUE)str))
#define RSTRING_END(str) (RSTRING_PTR(str)+RSTRING_LEN(str))
#define RSTRING_LENINT(str) rb_long2int(RSTRING_LEN(str))

long rb_ary_len(VALUE);
VALUE rb_ary_elt(VALUE, long);
void rb_ary_elt_set(VALUE, long, VALUE);

#define RARRAY_LEN(a) (rb_ary_len((VALUE)a))
#define RARRAY_LENINT(a) (rb_long2int(RARRAY_LEN(a)))
#define RARRAY_AT(a,i) (rb_ary_elt((VALUE)a, (long)i))
/* IMPORTANT: try to avoid using RARRAY_PTR if necessary, because it's
 * a _much_ slower operation than RARRAY_AT. RARRAY_PTR is only provided for
 * compatibility but should _not_ be used intensively.
 */
const VALUE *rb_ary_ptr(VALUE);
#define RARRAY_PTR(a) (rb_ary_ptr((VALUE)a)) 

#define RHASH_TBL(h) rb_hash_tbl(h)
#define RHASH_SIZE(h) rb_hash_size(h)
#define RHASH_EMPTY_P(h) (RHASH_SIZE(h) == 0)

struct RFile {
    struct RBasic basic;
    struct rb_io_t *fptr;
};

struct RRational {
    struct RBasic basic;
    VALUE num;
    VALUE den;
};

struct RComplex {
    struct RBasic basic;
    VALUE real;
    VALUE imag;
};

struct RData {
    struct RBasic basic;
    void (*dmark)(void*);
    void (*dfree)(void*);
    void *data;
};

#define ExtractIOStruct(obj) RFILE(obj)->fptr

#define DATA_PTR(dta) (RDATA(dta)->data)

typedef void (*RUBY_DATA_FUNC)(void*);

VALUE rb_data_object_alloc(VALUE,void*,RUBY_DATA_FUNC,RUBY_DATA_FUNC);

#define Data_Wrap_Struct(klass,mark,free,sval)\
    rb_data_object_alloc(klass,sval,(RUBY_DATA_FUNC)mark,(RUBY_DATA_FUNC)free)

#define Data_Make_Struct(klass,type,mark,free,sval) (\
    sval = ALLOC(type),\
    memset(sval, 0, sizeof(type)),\
    Data_Wrap_Struct(klass,mark,free,sval)\
)

#define Data_Get_Struct(obj,type,sval) do {\
    Check_Type(obj, T_DATA); \
    sval = (type*)DATA_PTR(obj);\
} while (0)

#define RSTRUCT_EMBED_LEN_MAX 3
struct RStruct {
    struct RBasic basic;
    union {
	struct {
	    long len;
	    VALUE *ptr;
	} heap;
	VALUE ary[RSTRUCT_EMBED_LEN_MAX];
    } as;
};
#define RSTRUCT_EMBED_LEN_MASK (FL_USER2|FL_USER1)
#define RSTRUCT_EMBED_LEN_SHIFT (FL_USHIFT+1)
#define RSTRUCT_LEN(st) \
    ((RBASIC(st)->flags & RSTRUCT_EMBED_LEN_MASK) ? \
     (long)((RBASIC(st)->flags >> RSTRUCT_EMBED_LEN_SHIFT) & \
            (RSTRUCT_EMBED_LEN_MASK >> RSTRUCT_EMBED_LEN_SHIFT)) : \
     RSTRUCT(st)->as.heap.len)
#define RSTRUCT_PTR(st) \
    ((RBASIC(st)->flags & RSTRUCT_EMBED_LEN_MASK) ? \
     RSTRUCT(st)->as.ary : \
     RSTRUCT(st)->as.heap.ptr)

#define RBIGNUM_EMBED_LEN_MAX ((sizeof(VALUE)*3)/sizeof(BDIGIT))
struct RBignum {
    struct RBasic basic;
    union {
        struct {
            long len;
            BDIGIT *digits;
        } heap;
        BDIGIT ary[RBIGNUM_EMBED_LEN_MAX];
    } as;
};
#define RBIGNUM_SIGN_BIT FL_USER1
/* sign: positive:1, negative:0 */
#define RBIGNUM_SIGN(b) ((RBASIC(b)->flags & RBIGNUM_SIGN_BIT) != 0)
#define RBIGNUM_SET_SIGN(b,sign) \
  ((sign) ? (RBASIC(b)->flags |= RBIGNUM_SIGN_BIT) \
          : (RBASIC(b)->flags &= ~RBIGNUM_SIGN_BIT))
#define RBIGNUM_POSITIVE_P(b) RBIGNUM_SIGN(b)
#define RBIGNUM_NEGATIVE_P(b) (!RBIGNUM_SIGN(b))

#define RBIGNUM_EMBED_FLAG FL_USER2
#define RBIGNUM_EMBED_LEN_MASK (FL_USER5|FL_USER4|FL_USER3)
#define RBIGNUM_EMBED_LEN_SHIFT (FL_USHIFT+3)
#define RBIGNUM_LEN(b) \
    ((RBASIC(b)->flags & RBIGNUM_EMBED_FLAG) ? \
     (long)((RBASIC(b)->flags >> RBIGNUM_EMBED_LEN_SHIFT) & \
            (RBIGNUM_EMBED_LEN_MASK >> RBIGNUM_EMBED_LEN_SHIFT)) : \
     RBIGNUM(b)->as.heap.len)
/* LSB:RBIGNUM_DIGITS(b)[0], MSB:RBIGNUM_DIGITS(b)[RBIGNUM_LEN(b)-1] */
#define RBIGNUM_DIGITS(b) \
    ((RBASIC(b)->flags & RBIGNUM_EMBED_FLAG) ? \
     RBIGNUM(b)->as.ary : \
     RBIGNUM(b)->as.heap.digits)

#define R_CAST(st)   (struct st*)
#define RBASIC(obj)  (R_CAST(RBasic)(obj))
#define RARRAY(obj)  (R_CAST(RArray)(obj))
#define RDATA(obj)   (R_CAST(RData)(obj))
#define RSTRUCT(obj) (R_CAST(RStruct)(obj))
#define RBIGNUM(obj) (R_CAST(RBignum)(obj))
#define RFILE(obj)   (R_CAST(RFile)(obj))
#define RRATIONAL(obj) (R_CAST(RRational)(obj))
#define RCOMPLEX(obj) (R_CAST(RComplex)(obj))
#define RVALUES(obj) (R_CAST(RValues)(obj))

#define FL_SINGLETON FL_USER0
#define FL_MARK      (((VALUE)1)<<5)
#define FL_RESERVED  (((VALUE)1)<<6) /* will be used in the future GC */
#define FL_FINALIZE  (((VALUE)1)<<7)
#define FL_TAINT     (((VALUE)1)<<8)
#define FL_UNTRUSTED (((VALUE)1)<<9)
#define FL_EXIVAR    (((VALUE)1)<<10)
#define FL_FREEZE    (((VALUE)1)<<11)

#define FL_USHIFT    12

#define FL_USER0     (((VALUE)1)<<(FL_USHIFT+0))
#define FL_USER1     (((VALUE)1)<<(FL_USHIFT+1))
#define FL_USER2     (((VALUE)1)<<(FL_USHIFT+2))
#define FL_USER3     (((VALUE)1)<<(FL_USHIFT+3))
#define FL_USER4     (((VALUE)1)<<(FL_USHIFT+4))
#define FL_USER5     (((VALUE)1)<<(FL_USHIFT+5))
#define FL_USER6     (((VALUE)1)<<(FL_USHIFT+6))
#define FL_USER7     (((VALUE)1)<<(FL_USHIFT+7))
#define FL_USER8     (((VALUE)1)<<(FL_USHIFT+8))
#define FL_USER9     (((VALUE)1)<<(FL_USHIFT+9))
#define FL_USER10    (((VALUE)1)<<(FL_USHIFT+10))
#define FL_USER11    (((VALUE)1)<<(FL_USHIFT+11))
#define FL_USER12    (((VALUE)1)<<(FL_USHIFT+12))
#define FL_USER13    (((VALUE)1)<<(FL_USHIFT+13))
#define FL_USER14    (((VALUE)1)<<(FL_USHIFT+14))
#define FL_USER15    (((VALUE)1)<<(FL_USHIFT+15))
#define FL_USER16    (((VALUE)1)<<(FL_USHIFT+16))
#define FL_USER17    (((VALUE)1)<<(FL_USHIFT+17))
#define FL_USER18    (((VALUE)1)<<(FL_USHIFT+18))
#define FL_USER19    (((VALUE)1)<<(FL_USHIFT+19))
#define FL_USER20    (((VALUE)1)<<(FL_USHIFT+20))

#define SPECIAL_CONST_P(x) (IMMEDIATE_P(x) || !RTEST(x))

int rb_obj_is_native(VALUE obj);
#define NATIVE(obj) (rb_obj_is_native((VALUE)obj))

#define FL_ABLE(x) (!SPECIAL_CONST_P(x) && !NATIVE(x) && BUILTIN_TYPE(x) != T_NODE)
#define FL_TEST(x,f) (FL_ABLE(x)?(RBASIC(x)->flags&(f)):0)
#define FL_ANY(x,f) FL_TEST(x,f)
#define FL_ALL(x,f) (FL_TEST(x,f) == (f))
#define FL_SET(x,f) do {if (FL_ABLE(x)) RBASIC(x)->flags |= (f);} while (0)
#define FL_UNSET(x,f) do {if (FL_ABLE(x)) RBASIC(x)->flags &= ~(f);} while (0)
#define FL_REVERSE(x,f) do {if (FL_ABLE(x)) RBASIC(x)->flags ^= (f);} while (0)

#define OBJ_TAINTED(x) (int)(SPECIAL_CONST_P(x) || NATIVE(x) ? rb_obj_tainted((VALUE)x) == Qtrue : FL_TEST((x), FL_TAINT))
#define OBJ_TAINT(x)   (rb_obj_taint((VALUE)x))
#define OBJ_UNTRUSTED(x) (int)(SPECIAL_CONST_P(x) || NATIVE(x) ? rb_obj_untrusted((VALUE)x) == Qtrue : FL_TEST((x), FL_UNTRUSTED))
#define OBJ_UNTRUST(x)	(rb_obj_untrust((VALUE)x))

#define OBJ_INFECT(x,s) \
    do { \
        if (OBJ_TAINTED(s)) { \
	    OBJ_TAINT(x); \
	} \
	if (OBJ_UNTRUSTED(s)) { \
	    OBJ_UNTRUST(x); \
	} \
    } \
    while (0)

#define OBJ_FROZEN(x) (int)(SPECIAL_CONST_P(x) || NATIVE(x) ? rb_obj_frozen_p((VALUE)x) == Qtrue : FL_TEST((x), FL_FREEZE))
#define OBJ_FREEZE(x) (rb_obj_freeze((VALUE)x))

#define ALLOC_N(type,n) (type*)xmalloc2((n),sizeof(type))
#define ALLOC(type) (type*)xmalloc(sizeof(type))
#define REALLOC_N(var,type,n) (var)=(type*)xrealloc2((char*)(var),(n),sizeof(type))

#define ALLOCA_N(type,n) (type*)alloca(sizeof(type)*(n))

#define MEMZERO(p,type,n) memset((p), 0, sizeof(type)*(n))
#define MEMCPY(p1,p2,type,n) memcpy((p1), (p2), sizeof(type)*(n))
#define MEMMOVE(p1,p2,type,n) memmove((p1), (p2), sizeof(type)*(n))
#define MEMCMP(p1,p2,type,n) memcmp((p1), (p2), sizeof(type)*(n))

void rb_obj_infect(VALUE,VALUE);

typedef int ruby_glob_func(const char*,VALUE);
void rb_glob(const char*,void(*)(const char*,VALUE),VALUE);
int ruby_glob(const char*,int,ruby_glob_func*,VALUE);
int ruby_brace_expand(const char*,int,ruby_glob_func*,VALUE);
int ruby_brace_glob(const char*,int,ruby_glob_func*,VALUE);

VALUE rb_define_class(const char*,VALUE);
VALUE rb_define_module(const char*);
VALUE rb_define_class_under(VALUE, const char*, VALUE);
VALUE rb_define_module_under(VALUE, const char*);

void rb_include_module(VALUE,VALUE);
void rb_extend_object(VALUE,VALUE);

void rb_define_variable(const char*,VALUE*);
void rb_define_virtual_variable(const char*,VALUE(*)(ANYARGS),void(*)(ANYARGS));
void rb_define_hooked_variable(const char*,VALUE*,VALUE(*)(ANYARGS),void(*)(ANYARGS));
void rb_define_readonly_variable(const char*,VALUE*);
void rb_define_const(VALUE,const char*,VALUE);
void rb_define_global_const(const char*,VALUE);

#define RUBY_METHOD_FUNC(func) ((VALUE (*)(ANYARGS))func)
void rb_define_method(VALUE,const char*,VALUE(*)(ANYARGS),int);
void rb_define_module_function(VALUE,const char*,VALUE(*)(ANYARGS),int);
void rb_define_global_function(const char*,VALUE(*)(ANYARGS),int);

void rb_undef_method(VALUE,const char*);
void rb_define_alias(VALUE,const char*,const char*);
void rb_define_attr(VALUE,const char*,int,int);

void rb_objc_define_method(VALUE klass, const char *name, void *imp, const int arity);
void rb_objc_define_direct_method(VALUE klass, const char *name, void *imp, const int arity);
void rb_objc_define_private_method(VALUE klass, const char *name, void *imp, const int arity);
void rb_objc_define_module_function(VALUE klass, const char *name, void *imp, const int arity);
void rb_objc_undef_method(VALUE klass, const char *name);

void rb_gvar_readonly_setter(VALUE val, ID id, void *var);

void rb_global_variable(VALUE*);
void rb_register_mark_object(VALUE);
void rb_gc_register_address(VALUE*);
void rb_gc_unregister_address(VALUE*);

ID rb_intern(const char*);
ID rb_intern2(const char*, long);
ID rb_intern_str(VALUE str);
ID rb_to_id(VALUE);
ID rb_sym2id(VALUE sym);
VALUE rb_id2str(ID);
VALUE rb_name2sym(const char *);
const char *rb_sym2name(VALUE sym);

static inline
const char *rb_id2name(ID val)
{
    VALUE s = rb_id2str(val);
    return s == 0 ? NULL : rb_sym2name(s);
}

#define rb_intern_const(str) rb_intern2(str, (long)strlen(str))

const char *rb_class2name(VALUE);
const char *rb_obj_classname(VALUE);

void rb_p(VALUE);

VALUE rb_eval_string(const char*);
VALUE rb_eval_string_protect(const char*, int*);
VALUE rb_eval_string_wrap(const char*, int*);
VALUE rb_funcall(VALUE, ID, int, ...);
VALUE rb_funcall2(VALUE, ID, int, const VALUE*);
VALUE rb_funcall3(VALUE, ID, int, const VALUE*);
int rb_scan_args(int, const VALUE*, const char*, ...);

VALUE rb_gv_set(const char*, VALUE);
VALUE rb_gv_get(const char*);
VALUE rb_iv_get(VALUE, const char*);
VALUE rb_iv_set(VALUE, const char*, VALUE);

VALUE rb_equal(VALUE,VALUE);

// Flags.
RUBY_EXTERN VALUE ruby_verbose, ruby_debug;

// AOT compiler.
RUBY_EXTERN VALUE ruby_aot_compile, ruby_aot_init_func, ruby_aot_bs_files;

// Debugger.
RUBY_EXTERN VALUE ruby_debug_socket_path;

PRINTF_ARGS(NORETURN(void rb_raise(VALUE, const char*, ...)), 2, 3);
PRINTF_ARGS(NORETURN(void rb_fatal(const char*, ...)), 1, 2);
PRINTF_ARGS(NORETURN(void rb_bug(const char*, ...)), 1, 2);
NORETURN(void rb_sys_fail(const char*));
NORETURN(void rb_mod_sys_fail(VALUE, const char*));
void rb_iter_break(void);
NORETURN(void rb_exit(int));
NORETURN(void rb_notimplement(void));

/* reports if `-w' specified */
PRINTF_ARGS(void rb_warning(const char*, ...), 1, 2);
PRINTF_ARGS(void rb_compile_warning(const char *, int, const char*, ...), 3, 4);
PRINTF_ARGS(void rb_sys_warning(const char*, ...), 1, 2);
/* reports always */
PRINTF_ARGS(void rb_warn(const char*, ...), 1, 2);
PRINTF_ARGS(void rb_compile_warn(const char *, int, const char*, ...), 3, 4);

typedef VALUE rb_block_call_func(VALUE, VALUE, int, VALUE*);

VALUE rb_each(VALUE);

VALUE rb_yield(VALUE val);
VALUE rb_yield_values(int n, ...);
VALUE rb_yield_values2(int n, const VALUE *argv);
VALUE rb_yield_splat(VALUE);

int rb_block_given_p(void);
void rb_need_block(void);
VALUE rb_iterate(VALUE(*)(VALUE),VALUE,VALUE(*)(ANYARGS),VALUE);
VALUE rb_block_call(VALUE,ID,int,VALUE*,VALUE(*)(ANYARGS),VALUE);
VALUE rb_rescue(VALUE(*)(ANYARGS),VALUE,VALUE(*)(ANYARGS),VALUE);
VALUE rb_rescue2(VALUE(*)(ANYARGS),VALUE,VALUE(*)(ANYARGS),VALUE,...);
VALUE rb_ensure(VALUE(*)(ANYARGS),VALUE,VALUE(*)(ANYARGS),VALUE);
VALUE rb_catch(const char*,VALUE(*)(ANYARGS),VALUE);
VALUE rb_catch_obj(VALUE,VALUE(*)(ANYARGS),VALUE);
void rb_throw(const char*,VALUE);
void rb_throw_obj(VALUE,VALUE);

VALUE rb_require(const char*);

void ruby_init(void);
void *ruby_options(int, char**);
int ruby_run_node(void *);

int macruby_main(const char *path, int argc, char **argv);

RUBY_EXTERN VALUE rb_mKernel;
RUBY_EXTERN VALUE rb_mComparable;
RUBY_EXTERN VALUE rb_mEnumerable;
RUBY_EXTERN VALUE rb_mPrecision;
RUBY_EXTERN VALUE rb_mErrno;
RUBY_EXTERN VALUE rb_mFileTest;
RUBY_EXTERN VALUE rb_mGC;
RUBY_EXTERN VALUE rb_mMath;
RUBY_EXTERN VALUE rb_mProcess;
RUBY_EXTERN VALUE rb_mWaitReadable;
RUBY_EXTERN VALUE rb_mWaitWritable;

RUBY_EXTERN VALUE rb_cBasicObject;
RUBY_EXTERN VALUE rb_cObject;
RUBY_EXTERN VALUE rb_cArray;
RUBY_EXTERN VALUE rb_cBignum;
RUBY_EXTERN VALUE rb_cBinding;
RUBY_EXTERN VALUE rb_cClass;
RUBY_EXTERN VALUE rb_cCont;
RUBY_EXTERN VALUE rb_cDir;
RUBY_EXTERN VALUE rb_cData;
RUBY_EXTERN VALUE rb_cFalseClass;
RUBY_EXTERN VALUE rb_cEncoding;
RUBY_EXTERN VALUE rb_cEnumerator;
RUBY_EXTERN VALUE rb_cFile;
RUBY_EXTERN VALUE rb_cFixnum;
RUBY_EXTERN VALUE rb_cFloat;
RUBY_EXTERN VALUE rb_cHash;
RUBY_EXTERN VALUE rb_cInteger;
RUBY_EXTERN VALUE rb_cIO;
RUBY_EXTERN VALUE rb_cMatch;
RUBY_EXTERN VALUE rb_cMethod;
RUBY_EXTERN VALUE rb_cModule;
RUBY_EXTERN VALUE rb_cNameErrorMesg;
RUBY_EXTERN VALUE rb_cNilClass;
RUBY_EXTERN VALUE rb_cNumeric;
RUBY_EXTERN VALUE rb_cProc;
RUBY_EXTERN VALUE rb_cProcessStatus;
RUBY_EXTERN VALUE rb_cRange;
RUBY_EXTERN VALUE rb_cRational;
RUBY_EXTERN VALUE rb_cComplex;
RUBY_EXTERN VALUE rb_cRegexp;
RUBY_EXTERN VALUE rb_cSet;
RUBY_EXTERN VALUE rb_cStat;
RUBY_EXTERN VALUE rb_cString;
RUBY_EXTERN VALUE rb_cStruct;
RUBY_EXTERN VALUE rb_cSymbol;
RUBY_EXTERN VALUE rb_cThread;
RUBY_EXTERN VALUE rb_cThGroup;
RUBY_EXTERN VALUE rb_cTime;
RUBY_EXTERN VALUE rb_cTrueClass;
RUBY_EXTERN VALUE rb_cUnboundMethod;
RUBY_EXTERN VALUE rb_cISeq;
RUBY_EXTERN VALUE rb_cVM;
RUBY_EXTERN VALUE rb_cEnv;
RUBY_EXTERN VALUE rb_cRandom;

RUBY_EXTERN VALUE rb_eException;
RUBY_EXTERN VALUE rb_eStandardError;
RUBY_EXTERN VALUE rb_eSystemExit;
RUBY_EXTERN VALUE rb_eInterrupt;
RUBY_EXTERN VALUE rb_eSignal;
RUBY_EXTERN VALUE rb_eFatal;
RUBY_EXTERN VALUE rb_eArgError;
RUBY_EXTERN VALUE rb_eEOFError;
RUBY_EXTERN VALUE rb_eIndexError;
RUBY_EXTERN VALUE rb_eStopIteration;
RUBY_EXTERN VALUE rb_eKeyError;
RUBY_EXTERN VALUE rb_eRangeError;
RUBY_EXTERN VALUE rb_eIOError;
RUBY_EXTERN VALUE rb_eRuntimeError;
RUBY_EXTERN VALUE rb_eSecurityError;
RUBY_EXTERN VALUE rb_eSystemCallError;
RUBY_EXTERN VALUE rb_eThreadError;
RUBY_EXTERN VALUE rb_eTypeError;
RUBY_EXTERN VALUE rb_eZeroDivError;
RUBY_EXTERN VALUE rb_eNotImpError;
RUBY_EXTERN VALUE rb_eNoMemError;
RUBY_EXTERN VALUE rb_eNoMethodError;
RUBY_EXTERN VALUE rb_eFloatDomainError;
RUBY_EXTERN VALUE rb_eLocalJumpError;
RUBY_EXTERN VALUE rb_eSysStackError;
RUBY_EXTERN VALUE rb_eRegexpError;
RUBY_EXTERN VALUE rb_eEncodingError;
RUBY_EXTERN VALUE rb_eEncCompatError;
RUBY_EXTERN VALUE rb_eUndefinedConversionError;
RUBY_EXTERN VALUE rb_eInvalidByteSequenceError;
RUBY_EXTERN VALUE rb_eConverterNotFoundError;

RUBY_EXTERN VALUE rb_eScriptError;
RUBY_EXTERN VALUE rb_eNameError;
RUBY_EXTERN VALUE rb_eSyntaxError;
RUBY_EXTERN VALUE rb_eLoadError;

RUBY_EXTERN VALUE rb_stdin, rb_stdout, rb_stderr;

static inline VALUE
rb_class_of(VALUE obj)
{
    if (IMMEDIATE_P(obj)) {
	if (FIXNUM_P(obj)) {
	    return rb_cFixnum;
	}
	if (FIXFLOAT_P(obj)) {
	    return rb_cFloat;
	}
	if (obj == Qtrue) {
	    return rb_cTrueClass;
	}
    }
    else if (!RTEST(obj)) {
	if (obj == Qnil) {
	    return rb_cNilClass;
	}
	if (obj == Qfalse) {
	    return rb_cFalseClass;
	}
    }
    return RBASIC(obj)->klass;
}

int rb_objc_type(VALUE obj);

static inline int
rb_type(VALUE obj)
{
    if (IMMEDIATE_P(obj)) {
	if (FIXNUM_P(obj)) {
	    return T_FIXNUM;
	}
	if (FIXFLOAT_P(obj)) {
	    return T_FLOAT;
	}
	if (obj == Qtrue) {
	    return T_TRUE;
	}
	if (obj == Qundef) {
	    return T_UNDEF;
	}
    }
    else if (!RTEST(obj)) {
	if (obj == Qnil) {
	    return T_NIL;
	}
	if (obj == Qfalse) {
	    return T_FALSE;
	}
    }
    return rb_objc_type(obj);
}

static inline int
rb_special_const_p(VALUE obj)
{
    return SPECIAL_CONST_P(obj) ? Qtrue : Qfalse;
}

#if RUBY_INCLUDED_AS_FRAMEWORK
# include <MacRuby/ruby/missing.h>
# include <MacRuby/ruby/intern.h>
# include <MacRuby/ruby/objc.h>
#else
# include "ruby/missing.h"
# include "ruby/intern.h"
#endif

void ruby_sysinit(int *, char ***);

#define RUBY_VM 1 /* YARV */
#define HAVE_NATIVETHREAD

/* locale insensitive functions */

#define rb_isascii(c) ((unsigned long)(c) < 128)
#define rb_isalnum(c) (rb_isascii(c) && isalnum(c))
#define rb_isalpha(c) (rb_isascii(c) && isalpha(c))
#define rb_isblank(c) (rb_isascii(c) && isblank(c))
#define rb_iscntrl(c) (rb_isascii(c) && iscntrl(c))
#define rb_isdigit(c) (rb_isascii(c) && isdigit(c))
#define rb_isgraph(c) (rb_isascii(c) && isgraph(c))
#define rb_islower(c) (rb_isascii(c) && islower(c))
#define rb_isprint(c) (rb_isascii(c) && isprint(c))
#define rb_ispunct(c) (rb_isascii(c) && ispunct(c))
#define rb_isspace(c) (rb_isascii(c) && isspace(c))
#define rb_isupper(c) (rb_isascii(c) && isupper(c))
#define rb_isxdigit(c) (rb_isascii(c) && isxdigit(c))
#define rb_tolower(c) (rb_isascii(c) ? tolower(c) : (c))
#define rb_toupper(c) (rb_isascii(c) ? toupper(c) : (c))

#define ISASCII(c) rb_isascii((unsigned char)(c))
#define ISPRINT(c) rb_isprint((unsigned char)(c))
#define ISSPACE(c) rb_isspace((unsigned char)(c))
#define ISUPPER(c) rb_isupper((unsigned char)(c))
#define ISLOWER(c) rb_islower((unsigned char)(c))
#define ISALNUM(c) rb_isalnum((unsigned char)(c))
#define ISALPHA(c) rb_isalpha((unsigned char)(c))
#define ISDIGIT(c) rb_isdigit((unsigned char)(c))
#define ISXDIGIT(c) rb_isxdigit((unsigned char)(c))
#define TOUPPER(c) rb_toupper((unsigned char)(c))
#define TOLOWER(c) rb_tolower((unsigned char)(c))

int st_strcasecmp(const char *s1, const char *s2);
int st_strncasecmp(const char *s1, const char *s2, size_t n);
#define STRCASECMP(s1, s2) (st_strcasecmp(s1, s2))
#define STRNCASECMP(s1, s2, n) (st_strncasecmp(s1, s2, n))

unsigned long ruby_strtoul(const char *str, char **endptr, int base);
#define STRTOUL(str, endptr, base) (ruby_strtoul(str, endptr, base))

#if defined(__cplusplus)
}  // extern "C" {
#endif

#endif /* RUBY_RUBY_H */
