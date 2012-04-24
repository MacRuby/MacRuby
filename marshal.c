/*
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2007-2011, Apple Inc. All rights reserved.
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 */

#include "macruby_internal.h"
#include "ruby/io.h"
#include "ruby/st.h"
#include "ruby/util.h"
#include "ruby/encoding.h"
#include "encoding.h"
#include "id.h"
#include "re.h"
#include "class.h"

#include <math.h>
#ifdef HAVE_FLOAT_H
#include <float.h>
#endif
#ifdef HAVE_IEEEFP_H
#include <ieeefp.h>
#endif

#define BITSPERSHORT (2*CHAR_BIT)
#define SHORTMASK ((1<<BITSPERSHORT)-1)
#define SHORTDN(x) RSHIFT(x,BITSPERSHORT)

#if SIZEOF_SHORT == SIZEOF_BDIGITS
#define SHORTLEN(x) (x)
#else
static int
shortlen(long len, BDIGIT *ds)
{
    BDIGIT num;
    int offset = 0;

    num = ds[len-1];
    while (num) {
	num = SHORTDN(num);
	offset++;
    }
    return (len - 1)*sizeof(BDIGIT)/2 + offset;
}
#define SHORTLEN(x) shortlen((x),d)
#endif

#define MARSHAL_MAJOR   4
#define MARSHAL_MINOR   8

#define TYPE_NIL	'0'
#define TYPE_TRUE	'T'
#define TYPE_FALSE	'F'
#define TYPE_FIXNUM	'i'

#define TYPE_EXTENDED_R	'e'
#define TYPE_UCLASS	'C'
#define TYPE_OBJECT	'o'
#define TYPE_DATA       'd'
#define TYPE_USERDEF	'u'
#define TYPE_USRMARSHAL	'U'
#define TYPE_FLOAT	'f'
#define TYPE_BIGNUM	'l'
#define TYPE_STRING	'"'
#define TYPE_REGEXP	'/'
#define TYPE_ARRAY	'['
#define TYPE_HASH	'{'
#define TYPE_HASH_DEF	'}'
#define TYPE_STRUCT	'S'
#define TYPE_MODULE_OLD	'M'
#define TYPE_CLASS	'c'
#define TYPE_MODULE	'm'

#define TYPE_SYMBOL	':'
#define TYPE_SYMLINK	';'

#define TYPE_IVAR	'I'
#define TYPE_LINK	'@'

static ID s_dump, s_load, s_mdump, s_mload;
static ID s_dump_data, s_load_data, s_alloc;
static ID s_getbyte, s_read, s_write, s_binmode;

static ID
rb_id_encoding(void)
{
    static ID id = 0;
    if (id == 0) {
	id = rb_intern("encoding");
    }
    return id;
}

typedef struct {
    VALUE newclass;
    VALUE oldclass;
    VALUE (*dumper)(VALUE);
    VALUE (*loader)(VALUE, VALUE);
} marshal_compat_t;

static st_table *compat_allocator_tbl;
static VALUE compat_allocator_tbl_wrapper;

static rb_alloc_func_t
rb_get_alloc_func(VALUE klass)
{
    // TODO... or is this really needed...
    return NULL;
}

void
rb_marshal_define_compat(VALUE newclass, VALUE oldclass, VALUE (*dumper)(VALUE), VALUE (*loader)(VALUE, VALUE))
{
    marshal_compat_t *compat;
    rb_alloc_func_t allocator = rb_get_alloc_func(newclass);

    if (!allocator) {
        rb_raise(rb_eTypeError, "no allocator");
    }

    compat = ALLOC(marshal_compat_t);
    compat->newclass = Qnil;
    compat->oldclass = Qnil;
    compat->newclass = newclass;
    compat->oldclass = oldclass;
    compat->dumper = dumper;
    compat->loader = loader;

    st_insert(compat_allocator_tbl, (st_data_t)allocator, (st_data_t)compat);
}

struct dump_arg {
    VALUE obj;
    VALUE str, dest;
    st_table *symbols;
    st_table *data;
    int taint;
    st_table *compat_tbl;
    VALUE wrapper;
    st_table *encodings;
};

struct dump_call_arg {
    VALUE obj;
    struct dump_arg *arg;
    int limit;
};

static void
check_dump_arg(struct dump_arg *arg)
{
    if (!DATA_PTR(arg->wrapper)) {
	rb_raise(rb_eRuntimeError, "Marshal.dump reentered");
    }
}

static VALUE
class2path(VALUE klass)
{
    VALUE path;
    if (klass == rb_cNSObject) {
	path = rb_str_new2("Object");
    }
    else if (klass == rb_cNSMutableString) {
	path = rb_str_new2("String");
    }
    else {
	path = rb_class_path(klass);
    }
    const char *n = RSTRING_PTR(path);

    if (n[0] == '#') {
	rb_raise(rb_eTypeError, "can't dump anonymous %s %s",
		 (TYPE(klass) == T_CLASS ? "class" : "module"),
		 n);
    }
    if (rb_path2class(n) != rb_class_real(klass, true)) {
	rb_raise(rb_eTypeError, "%s can't be referred", n);
    }
    return path;
}

static void w_long(long, struct dump_arg*);

static void
w_nbyte(const char *s, int n, struct dump_arg *arg)
{
    VALUE buf = arg->str;
    rb_bstr_concat(buf, (const uint8_t *)s, n);
#if 0 // unused
    if (arg->dest && RSTRING_LEN(buf) >= BUFSIZ) {
	if (arg->taint) {
	    OBJ_TAINT(buf);
	}
	rb_io_write(arg->dest, buf);
	rb_str_resize(buf, 0);
    }
#endif
}

static void
w_byte(char c, struct dump_arg *arg)
{
    w_nbyte(&c, 1, arg);
}

static void
w_bytes(const char *s, int n, struct dump_arg *arg)
{
    w_long(n, arg);
    w_nbyte(s, n, arg);
}

static void
w_short(int x, struct dump_arg *arg)
{
    w_byte((char)((x >> 0) & 0xff), arg);
    w_byte((char)((x >> 8) & 0xff), arg);
}

static void
w_long(long x, struct dump_arg *arg)
{
    char buf[sizeof(long)+1];
    int i, len = 0;

#if SIZEOF_LONG > 4
    if (!(RSHIFT(x, 31) == 0 || RSHIFT(x, 31) == -1)) {
	/* big long does not fit in 4 bytes */
	rb_raise(rb_eTypeError, "long too big to dump");
    }
#endif

    if (x == 0) {
	w_byte(0, arg);
	return;
    }
    if (0 < x && x < 123) {
	w_byte((char)(x + 5), arg);
	return;
    }
    if (-124 < x && x < 0) {
	w_byte((char)((x - 5)&0xff), arg);
	return;
    }
    for (i=1;i<sizeof(long)+1;i++) {
	buf[i] = x & 0xff;
	x = RSHIFT(x,8);
	if (x == 0) {
	    buf[0] = i;
	    break;
	}
	if (x == -1) {
	    buf[0] = -i;
	    break;
	}
    }
    len = i;
    for (i=0;i<=len;i++) {
	w_byte(buf[i], arg);
    }
}

#ifdef DBL_MANT_DIG
#define DECIMAL_MANT (53-16)	/* from IEEE754 double precision */

#if DBL_MANT_DIG > 32
#define MANT_BITS 32
#elif DBL_MANT_DIG > 24
#define MANT_BITS 24
#elif DBL_MANT_DIG > 16
#define MANT_BITS 16
#else
#define MANT_BITS 8
#endif

static int
save_mantissa(double d, char *buf)
{
    int e, i = 0;
    unsigned long m;
    double n;

    d = modf(ldexp(frexp(fabs(d), &e), DECIMAL_MANT), &d);
    if (d > 0) {
	buf[i++] = 0;
	do {
	    d = modf(ldexp(d, MANT_BITS), &n);
	    m = (unsigned long)n;
#if MANT_BITS > 24
	    buf[i++] = m >> 24;
#endif
#if MANT_BITS > 16
	    buf[i++] = m >> 16;
#endif
#if MANT_BITS > 8
	    buf[i++] = m >> 8;
#endif
	    buf[i++] = m;
	} while (d > 0);
	while (!buf[i - 1]) --i;
    }
    return i;
}

static double
load_mantissa(double d, const char *buf, int len)
{
    if (--len > 0 && !*buf++) {	/* binary mantissa mark */
	int e, s = d < 0, dig = 0;
	unsigned long m;

	modf(ldexp(frexp(fabs(d), &e), DECIMAL_MANT), &d);
	do {
	    m = 0;
	    switch (len) {
	      default: m = *buf++ & 0xff;
#if MANT_BITS > 24
	      case 3: m = (m << 8) | (*buf++ & 0xff);
#endif
#if MANT_BITS > 16
	      case 2: m = (m << 8) | (*buf++ & 0xff);
#endif
#if MANT_BITS > 8
	      case 1: m = (m << 8) | (*buf++ & 0xff);
#endif
	    }
	    dig -= len < MANT_BITS / 8 ? 8 * (unsigned)len : MANT_BITS;
	    d += ldexp((double)m, dig);
	} while ((len -= MANT_BITS / 8) > 0);
	d = ldexp(d, e - DECIMAL_MANT);
	if (s) d = -d;
    }
    return d;
}
#else
#define load_mantissa(d, buf, len) (d)
#define save_mantissa(d, buf) 0
#endif

#ifdef DBL_DIG
#define FLOAT_DIG (DBL_DIG+2)
#else
#define FLOAT_DIG 17
#endif

static void
w_float(double d, struct dump_arg *arg)
{
    char buf[FLOAT_DIG + (DECIMAL_MANT + 7) / 8 + 10];

    if (isinf(d)) {
	if (d < 0) strcpy(buf, "-inf");
	else       strcpy(buf, "inf");
    }
    else if (isnan(d)) {
	strcpy(buf, "nan");
    }
    else if (d == 0.0) {
	if (1.0/d < 0) strcpy(buf, "-0");
	else           strcpy(buf, "0");
    }
    else {
	int len;

	/* xxx: should not use system's sprintf(3) */
	snprintf(buf, sizeof(buf), "%.*g", FLOAT_DIG, d);
	len = strlen(buf);
	w_bytes(buf, len + save_mantissa(d, buf + len), arg);
	return;
    }
    w_bytes(buf, strlen(buf), arg);
}

static void
w_symbol(ID id, struct dump_arg *arg)
{
    const char *sym;
    st_data_t num;

    if (st_lookup(arg->symbols, id, &num)) {
	w_byte(TYPE_SYMLINK, arg);
	w_long((long)num, arg);
    }
    else {
	sym = rb_id2name(id);
	if (!sym) {
	    rb_raise(rb_eTypeError, "can't dump anonymous ID %ld", id);
	}
	w_byte(TYPE_SYMBOL, arg);
	w_bytes(sym, strlen(sym), arg);
	st_add_direct(arg->symbols, id, arg->symbols->num_entries);
    }
}

static void
w_unique(const char *s, struct dump_arg *arg)
{
    if (s[0] == '#') {
	rb_raise(rb_eTypeError, "can't dump anonymous class %s", s);
    }
    w_symbol(rb_intern(s), arg);
}

static void w_object(VALUE,struct dump_arg*,int);

static int
hash_each(VALUE key, VALUE value, struct dump_call_arg *arg)
{
    w_object(key, arg->arg, arg->limit);
    w_object(value, arg->arg, arg->limit);
    return ST_CONTINUE;
}

static void
w_extended(VALUE klass, struct dump_arg *arg, int check)
{
#if 0
    const char *path;
    if (check && RCLASS_SINGLETON(klass)) {
#if !WITH_OBJC // TODO
	if (RCLASS_M_TBL(klass)->num_entries ||
	    (RCLASS_IV_TBL(klass) && RCLASS_IV_TBL(klass)->num_entries > 1)) {
	    rb_raise(rb_eTypeError, "singleton can't be dumped");
	}
	klass = RCLASS_SUPER(klass);
#endif
    }
    while (TYPE(klass) == T_ICLASS) {
	path = rb_class2name(RBASIC(klass)->klass);
	w_byte(TYPE_EXTENDED_R, arg);
	w_unique(path, arg);
	klass = RCLASS_SUPER(klass);
    }
#endif
    if (RCLASS_SINGLETON(klass)) {
	VALUE ary = rb_attr_get(klass, idIncludedModules);
	if (ary != Qnil) {
	    for (int i = 0, count = RARRAY_LEN(ary); i < count; i++) {
		VALUE mod = RARRAY_AT(ary, i);
		const char *path = rb_class2name(mod);
		w_byte(TYPE_EXTENDED_R, arg);
		w_unique(path, arg);
	    }
	}
    }
}

static void
w_class(char type, VALUE obj, struct dump_arg *arg, int check)
{
    volatile VALUE p;
    const char *path;
    st_data_t real_obj;
    VALUE klass;

    if (st_lookup(arg->compat_tbl, (st_data_t)obj, &real_obj)) {
        obj = (VALUE)real_obj;
    }
    klass = CLASS_OF(obj);
    w_extended(klass, arg, check);
    w_byte(type, arg);
    p = class2path(rb_class_real(klass, true));
    path = RSTRING_PTR(p);
    w_unique(path, arg);
}

static void
#if WITH_OBJC
w_uclass(VALUE obj, bool is_pure, struct dump_arg *arg)
#else
w_uclass(VALUE obj, VALUE super, struct dump_arg *arg)
#endif
{
    VALUE klass = CLASS_OF(obj);

    w_extended(klass, arg, Qtrue);
    klass = rb_class_real(klass, true);
#if WITH_OBJC
    if (!is_pure) {
#else
    if (klass != super) {
#endif
	w_byte(TYPE_UCLASS, arg);
	w_unique(RSTRING_PTR(class2path(klass)), arg);
    }
}

static int
w_obj_each(ID id, VALUE value, struct dump_call_arg *arg)
{
    if (id == rb_id_encoding()) return ST_CONTINUE;
    w_symbol(id, arg->arg);
    w_object(value, arg->arg, arg->limit);
    return ST_CONTINUE;
}

static void
w_encoding(VALUE obj, long num, struct dump_call_arg *arg)
{
    rb_encoding *enc = 0;
#if WITH_OBJC
    VALUE name;

    enc = rb_enc_get(obj);
    if (enc == NULL) {
	w_long(num, arg->arg);
	return;
    }
    name = rb_enc_name2(enc);
#else
    int encidx = rb_enc_get_index(obj);
    rb_encoding *enc = 0;
    st_data_t name;

    if (encidx <= 0 || !(enc = rb_enc_from_index(encidx))) {
	w_long(num, arg->arg);
	return;
    }
    w_long(num + 1, arg->arg);
    w_symbol(rb_id_encoding(), arg->arg);
    do {
	if (!arg->arg->encodings)
	    arg->arg->encodings = st_init_strcasetable();
	else if (st_lookup(arg->arg->encodings, (st_data_t)rb_enc_name(enc), &name))
	    break;
	name = (st_data_t)rb_str_new2(rb_enc_name(enc));
	st_insert(arg->arg->encodings, (st_data_t)rb_enc_name(enc), name);
    } while (0);
#endif
    w_object(name, arg->arg, arg->limit);
}

static void
w_ivar(VALUE obj, st_table *tbl, struct dump_call_arg *arg)
{
    long num = tbl ? tbl->num_entries : 0;

    w_encoding(obj, num, arg);
    if (tbl) {
	st_foreach_safe(tbl, w_obj_each, (st_data_t)arg);
    }
}

static void
w_objivar(VALUE obj, struct dump_call_arg *arg)
{
#if WITH_OBJC
    VALUE ary = rb_obj_instance_variables(obj);
    int i, len = RARRAY_LEN(ary);

    w_encoding(obj, len, arg);

    for (i = 0; i < len; i++) {
	ID var_id = SYM2ID(RARRAY_AT(ary, i));
	VALUE var_val = rb_ivar_get(obj, var_id);
	w_obj_each(var_id, var_val, arg);
    }
#else
    VALUE *ptr;
    long i, len, num;

    len = ROBJECT_NUMIV(obj);
    ptr = ROBJECT_IVPTR(obj);
    num = 0;
    for (i = 0; i < len; i++)
        if (ptr[i] != Qundef)
            num += 1;

    w_encoding(obj, num, arg);
    if (num != 0) {
        rb_ivar_foreach(obj, w_obj_each, (st_data_t)arg);
    }
#endif
}

static void
w_object(VALUE obj, struct dump_arg *arg, int limit)
{
    struct dump_call_arg c_arg;
    st_table *ivtbl = 0;
    st_data_t num;
    int hasiv = 0;
#if WITH_OBJC
// TODO
#define has_ivars(obj, ivtbl) (false)
#else
#define has_ivars(obj, ivtbl) ((ivtbl = rb_generic_ivar_table(obj)) != 0 || \
			       (!SPECIAL_CONST_P(obj) && !ENCODING_IS_ASCII8BIT(obj)))
#endif

    if (limit == 0) {
	rb_raise(rb_eArgError, "exceed depth limit");
    }

    limit--;
    c_arg.limit = limit;
    c_arg.arg = arg;

    if (st_lookup(arg->data, obj, &num)) {
	w_byte(TYPE_LINK, arg);
	w_long((long)num, arg);
	return;
    }

    if ((hasiv = has_ivars(obj, ivtbl)) != 0) {
	w_byte(TYPE_IVAR, arg);
    }
    if (obj == Qnil) {
	w_byte(TYPE_NIL, arg);
    }
    else if (obj == Qtrue) {
	w_byte(TYPE_TRUE, arg);
    }
    else if (obj == Qfalse) {
	w_byte(TYPE_FALSE, arg);
    }
    else if (FIXNUM_P(obj)) {
#if SIZEOF_LONG <= 4
	w_byte(TYPE_FIXNUM, arg);
	w_long(FIX2INT(obj), arg);
#else
	if (RSHIFT((long)obj, 31) == 0 || RSHIFT((long)obj, 31) == -1) {
	    w_byte(TYPE_FIXNUM, arg);
	    w_long(FIX2LONG(obj), arg);
	}
	else {
	    w_object(rb_int2big(FIX2LONG(obj)), arg, limit);
	}
#endif
    }
    else if (FIXFLOAT_P(obj)) {
	w_byte(TYPE_FLOAT, arg);
	w_float(RFLOAT_VALUE(obj), arg);
    }
    else if (SYMBOL_P(obj)) {
	w_symbol(SYM2ID(obj), arg);
    }
    else {
	if (OBJ_TAINTED(obj)) {
	    arg->taint = Qtrue;
	}

	if (rb_obj_respond_to(obj, s_mdump, Qtrue)) {
	    volatile VALUE v;

            st_add_direct(arg->data, obj, arg->data->num_entries);

	    v = rb_funcall(obj, s_mdump, 0, 0);
	    check_dump_arg(arg);
	    w_class(TYPE_USRMARSHAL, obj, arg, Qfalse);
	    w_object(v, arg, limit);
	    if (hasiv) w_ivar(obj, 0, &c_arg);
	    return;
	}
	if (rb_obj_respond_to(obj, s_dump, Qtrue)) {
	    VALUE v;
            st_table *ivtbl2 = 0;
            int hasiv2;

	    v = rb_funcall(obj, s_dump, 1, INT2NUM(limit));
	    check_dump_arg(arg);
	    if (TYPE(v) != T_STRING) {
		rb_raise(rb_eTypeError, "_dump() must return string");
	    }
	    if ((hasiv2 = has_ivars(v, ivtbl2)) != 0 && !hasiv) {
		w_byte(TYPE_IVAR, arg);
	    }
	    w_class(TYPE_USERDEF, obj, arg, Qfalse);
	    w_bytes(RSTRING_PTR(v), RSTRING_LEN(v), arg);
            if (hasiv2) {
		w_ivar(v, ivtbl2, &c_arg);
            }
            else if (hasiv) {
		w_ivar(obj, ivtbl, &c_arg);
	    }
            st_add_direct(arg->data, obj, arg->data->num_entries);
	    return;
	}

        st_add_direct(arg->data, obj, arg->data->num_entries);

	if (!NATIVE(obj)) {
            st_data_t compat_data;
            rb_alloc_func_t allocator = rb_get_alloc_func(RBASIC(obj)->klass);
            if (st_lookup(compat_allocator_tbl,
                          (st_data_t)allocator,
                          &compat_data)) {
                marshal_compat_t *compat = (marshal_compat_t*)compat_data;
                VALUE real_obj = obj;
                obj = compat->dumper(real_obj);
                st_insert(arg->compat_tbl, (st_data_t)obj, (st_data_t)real_obj);
            }
        }

	switch (TYPE(obj)) {
	  case T_CLASS:
	    if (RCLASS_SINGLETON(obj)) {
		rb_raise(rb_eTypeError, "singleton class can't be dumped");
	    }
	    w_byte(TYPE_CLASS, arg);
	    {
		volatile VALUE path = class2path(obj);
		w_bytes(RSTRING_PTR(path), RSTRING_LEN(path), arg);
	    }
	    break;

	  case T_MODULE:
	    w_byte(TYPE_MODULE, arg);
	    {
		VALUE path = class2path(obj);
		w_bytes(RSTRING_PTR(path), RSTRING_LEN(path), arg);
	    }
	    break;

	  case T_BIGNUM:
	    w_byte(TYPE_BIGNUM, arg);
	    {
		char sign = RBIGNUM_SIGN(obj) ? '+' : '-';
		long len = RBIGNUM_LEN(obj);
		BDIGIT *d = RBIGNUM_DIGITS(obj);

		w_byte(sign, arg);
		w_long(SHORTLEN(len), arg); /* w_short? */
		while (len--) {
#if SIZEOF_BDIGITS > SIZEOF_SHORT
		    BDIGIT num = *d;
		    int i;

		    for (i=0; i<SIZEOF_BDIGITS; i+=SIZEOF_SHORT) {
			w_short(num & SHORTMASK, arg);
			num = SHORTDN(num);
			if (len == 0 && num == 0) break;
		    }
#else
		    w_short(*d, arg);
#endif
		    d++;
		}
	    }
	    break;

	  case T_STRING:
#if WITH_OBJC
	    w_uclass(obj, rb_objc_str_is_pure(obj), arg);
#else
	    w_uclass(obj, rb_cString, arg);
#endif
	    w_byte(TYPE_STRING, arg);
	    w_bytes(RSTRING_PTR(obj), RSTRING_LEN(obj), arg);
	    break;

	  case T_REGEXP:
	    w_uclass(obj, rb_cRegexp, arg);
	    w_byte(TYPE_REGEXP, arg);
	    VALUE re_str = rb_regexp_source(obj);
	    w_bytes(RSTRING_PTR(re_str), RSTRING_LEN(re_str), arg);
	    w_byte(rb_reg_options_to_mri(rb_reg_options(obj)), arg);
	    break;

	  case T_ARRAY:
#if WITH_OBJC
	    w_uclass(obj, rb_objc_ary_is_pure(obj), arg);
#else
	    w_uclass(obj, rb_cArray, arg);
#endif
	    w_byte(TYPE_ARRAY, arg);
	    {
		long len = RARRAY_LEN(obj);
#if WITH_OBJC
		long i;
		w_long(len, arg);
		for (i = 0; i < len; i++)
		    w_object(RARRAY_AT(obj, i), arg, limit);
#else
		VALUE *ptr = RARRAY_PTR(obj);

		w_long(len, arg);
		while (len--) {
		    w_object(*ptr, arg, limit);
		    ptr++;
		}
#endif
	    }
	    break;

	  case T_HASH:
#if WITH_OBJC
	    w_uclass(obj, rb_objc_hash_is_pure(obj), arg);
#else
	    w_uclass(obj, rb_cHash, arg);
#endif
#if WITH_OBJC
	    w_byte(TYPE_HASH, arg);
	    /* TODO: encode ifnone too */
#else
	    if (NIL_P(RHASH(obj)->ifnone)) {
		w_byte(TYPE_HASH, arg);
	    }
	    else if (FL_TEST(obj, FL_USER2)) {
		/* FL_USER2 means HASH_PROC_DEFAULT (see hash.c) */
		rb_raise(rb_eTypeError, "can't dump hash with default proc");
	    }
	    else {
		w_byte(TYPE_HASH_DEF, arg);
	    }
#endif
	    w_long(RHASH_SIZE(obj), arg);
	    rb_hash_foreach(obj, hash_each, (st_data_t)&c_arg);
#if !WITH_OBJC
	    if (!NIL_P(RHASH(obj)->ifnone)) {
		w_object(RHASH(obj)->ifnone, arg, limit);
	    }
#endif
	    break;

	  case T_STRUCT:
	    w_class(TYPE_STRUCT, obj, arg, Qtrue);
	    {
		long len = RSTRUCT_LEN(obj);
		VALUE mem;
		long i;

		w_long(len, arg);
		mem = rb_struct_members(obj);
		for (i=0; i<len; i++) {
		    w_symbol(SYM2ID(RARRAY_AT(mem, i)), arg);
		    w_object(RSTRUCT_PTR(obj)[i], arg, limit);
		}
	    }
	    break;

	  case T_NATIVE:
	  case T_OBJECT:
	    w_class(TYPE_OBJECT, obj, arg, Qtrue);
	    w_objivar(obj, &c_arg);
	    break;

	  case T_DATA:
	    {
		VALUE v;

		if (!rb_obj_respond_to(obj, s_dump_data, Qtrue)) {
		    rb_raise(rb_eTypeError,
			     "no marshal_dump is defined for class %s",
			     rb_obj_classname(obj));
		}
		v = rb_funcall(obj, s_dump_data, 0);
		check_dump_arg(arg);
		w_class(TYPE_DATA, obj, arg, Qtrue);
		w_object(v, arg, limit);
	    }
	    break;

	  default:
	    rb_raise(rb_eTypeError, "can't dump %s (type %d)",
		    rb_obj_classname(obj), TYPE(obj));
	    break;
	}
    }
    if (hasiv) {
	w_ivar(obj, ivtbl, &c_arg);
    }
}

static VALUE
dump(struct dump_call_arg *arg)
{
    w_object(arg->obj, arg->arg, arg->limit);
#if 0 // unused
    if (arg->arg->dest) {
	rb_io_write(arg->arg->dest, arg->arg->str);
	rb_bstr_resize(arg->arg->str, 0);
    }
#endif
    return 0;
}

static VALUE
dump_ensure(struct dump_arg *arg)
{
    if (!DATA_PTR(arg->wrapper)) return 0;
    st_free_table(arg->symbols);
    st_free_table(arg->data);
    st_free_table(arg->compat_tbl);
    DATA_PTR(arg->wrapper) = 0;
    arg->wrapper = 0;
    if (arg->taint) {
	OBJ_TAINT(arg->str);
    }
    return 0;
}

/*
 * call-seq:
 *      dump( obj [, anIO] , limit=--1 ) => anIO
 *
 * Serializes obj and all descendent objects. If anIO is
 * specified, the serialized data will be written to it, otherwise the
 * data will be returned as a String. If limit is specified, the
 * traversal of subobjects will be limited to that depth. If limit is
 * negative, no checking of depth will be performed.
 *
 *     class Klass
 *       def initialize(str)
 *         @str = str
 *       end
 *       def sayHello
 *         @str
 *       end
 *     end
 *
 * (produces no output)
 *
 *     o = Klass.new("hello\n")
 *     data = Marshal.dump(o)
 *     obj = Marshal.load(data)
 *     obj.sayHello   #=> "hello\n"
 */
static VALUE
marshal_dump(VALUE self, SEL sel, int argc, VALUE *argv)
{
    VALUE obj, port, a1, a2;
    int limit = -1;
    struct dump_arg *arg;
    struct dump_call_arg *c_arg;

    port = Qnil;
    rb_scan_args(argc, argv, "12", &obj, &a1, &a2);
    arg = (struct dump_arg *)xmalloc(sizeof(struct dump_arg));
    c_arg = (struct dump_call_arg *)xmalloc(sizeof(struct dump_call_arg));
    if (argc == 3) {
	if (!NIL_P(a2)) {
	    limit = NUM2INT(a2);
	}
	if (NIL_P(a1)) {
	    goto type_error;
	}
	port = a1;
    }
    else if (argc == 2) {
	if (FIXNUM_P(a1)) {
	    limit = FIX2INT(a1);
	}
	else if (NIL_P(a1)) {
	    goto type_error;
	}
	else {
	    port = a1;
	}
    }
    arg->dest = 0;
    bool got_io = false;
    if (!NIL_P(port)) {
	if (!rb_obj_respond_to(port, s_write, Qtrue)) {
type_error:
	    rb_raise(rb_eTypeError, "instance of IO needed");
	}
	GC_WB(&arg->str, rb_bstr_new());
#if 0 // unused
	GC_WB(&arg->dest, port);
#endif
	if (rb_obj_respond_to(port, s_binmode, Qtrue)) {
	    rb_funcall2(port, s_binmode, 0, 0);
	}
	got_io = true;
    }
    else {
	port = rb_bstr_new();
	GC_WB(&arg->str, port);
    }

    GC_WB(&arg->symbols, st_init_numtable());
    GC_WB(&arg->data, st_init_numtable());
    arg->taint = Qfalse;
    GC_WB(&arg->compat_tbl, st_init_numtable());
    GC_WB(&arg->wrapper, Data_Wrap_Struct(rb_cData, NULL, 0, arg));
    arg->encodings = 0;
    GC_WB(&c_arg->obj, obj);
    GC_WB(&c_arg->arg, arg);
    c_arg->limit = limit;

    w_byte(MARSHAL_MAJOR, arg);
    w_byte(MARSHAL_MINOR, arg);

    rb_ensure(dump, (VALUE)c_arg, dump_ensure, (VALUE)arg);

    // If we got an IO object as the port, make sure to write the bytestring
    // to it before leaving!
    if (got_io) {
	rb_io_write(port, arg->str);	
    }
    else {
	rb_str_force_encoding(port, rb_encodings[ENCODING_UTF8]);
    }
    return port;
}

struct load_arg {
    VALUE src;
    long offset;
    st_table *symbols;
    VALUE data;
    VALUE proc;
    int taint;
    st_table *compat_tbl;
    VALUE compat_tbl_wrapper;
};

static void
check_load_arg(struct load_arg *arg)
{
    if (!DATA_PTR(arg->compat_tbl_wrapper)) {
	rb_raise(rb_eRuntimeError, "Marshal.load reentered");
    }
}

static VALUE r_entry(VALUE v, struct load_arg *arg);
static VALUE r_object(struct load_arg *arg);
static VALUE path2class(const char *path);

static int
r_byte(struct load_arg *arg)
{
    int c;

    if (TYPE(arg->src) == T_STRING) {
	if (RSTRING_LEN(arg->src) > arg->offset) {
	    c = (unsigned char)RSTRING_PTR(arg->src)[arg->offset++];
	}
	else {
	    rb_raise(rb_eArgError, "marshal data too short");
	}
    }
    else {
	VALUE src = arg->src;
	VALUE v = rb_funcall2(src, s_getbyte, 0, 0);
	check_load_arg(arg);
	if (NIL_P(v)) {
	    rb_eof_error();
	}
	c = (unsigned char)NUM2CHR(v);
    }

    return c;
}

static void
long_toobig(int size)
{
    rb_raise(rb_eTypeError,
	    "long too big for this architecture (size %zd, given %d)",
	    sizeof(long), size);
}

#undef SIGN_EXTEND_CHAR
#if __STDC__
# define SIGN_EXTEND_CHAR(c) ((signed char)(c))
#else  /* not __STDC__ */
/* As in Harbison and Steele.  */
# define SIGN_EXTEND_CHAR(c) ((((unsigned char)(c)) ^ 128) - 128)
#endif

static long
r_long(struct load_arg *arg)
{
    register long x;
    int c = SIGN_EXTEND_CHAR(r_byte(arg));
    long i;

    if (c == 0) return 0;
    if (c > 0) {
	if (4 < c && c < 128) {
	    return c - 5;
	}
	if (c > sizeof(long)) long_toobig(c);
	x = 0;
	for (i=0;i<c;i++) {
	    x |= (long)r_byte(arg) << (8*i);
	}
    }
    else {
	if (-129 < c && c < -4) {
	    return c + 5;
	}
	c = -c;
	if (c > sizeof(long)) long_toobig(c);
	x = -1;
	for (i=0;i<c;i++) {
	    x &= ~((long)0xff << (8*i));
	    x |= (long)r_byte(arg) << (8*i);
	}
    }
    return x;
}

#define r_bytes(arg) r_bytes0(r_long(arg), (arg))

static VALUE
r_bytes0(long len, struct load_arg *arg)
{
    VALUE str;

    if (len == 0) {
	return rb_str_new(0, 0);
    }
    if (TYPE(arg->src) == T_STRING) {
	if (RSTRING_LEN(arg->src) - arg->offset >= len) {
	    str = rb_bstr_new();
	    rb_bstr_resize(str, len + 1);
	    uint8_t *data = rb_bstr_bytes(str);
	    memcpy(data, (UInt8 *)RSTRING_PTR(arg->src) + arg->offset, len);
	    data[len] = '\0';
	    arg->offset += len;
	}
	else {
too_short:
	    rb_raise(rb_eArgError, "marshal data too short");
	}
    }
    else {
	VALUE src = arg->src;
	VALUE n = LONG2NUM(len);
	str = rb_funcall2(src, s_read, 1, &n);
	check_load_arg(arg);
	if (NIL_P(str)) {
	    goto too_short;
	}
	StringValue(str);
	if (RSTRING_LEN(str) != len) {
	    goto too_short;
	}
	if (OBJ_TAINTED(str)) {
	    arg->taint = Qtrue;
	}
    }
    return str;
}

static ID
r_symlink(struct load_arg *arg)
{
    ID id;
    long num = r_long(arg);

    if (st_lookup(arg->symbols, num, (st_data_t *)&id)) {
	return id;
    }
    rb_raise(rb_eArgError, "bad symbol");
}

static ID
r_symreal(struct load_arg *arg)
{
    volatile VALUE s = r_bytes(arg);
    ID id = rb_intern(RSTRING_PTR(s));

    st_insert(arg->symbols, arg->symbols->num_entries, id);

    return id;
}

static ID
r_symbol(struct load_arg *arg)
{
    int type;

    switch ((type = r_byte(arg))) {
      case TYPE_SYMBOL:
	return r_symreal(arg);
      case TYPE_SYMLINK:
	return r_symlink(arg);
      default:
	rb_raise(rb_eArgError, "dump format error(0x%x)", type);
	break;
    }
}

static const char*
r_unique(struct load_arg *arg)
{
    return rb_id2name(r_symbol(arg));
}

static VALUE
r_string(struct load_arg *arg)
{
    return rb_str_new2(RSTRING_PTR(r_bytes(arg)));
}

static VALUE
r_entry(VALUE v, struct load_arg *arg)
{
    st_data_t real_obj = (VALUE)Qundef;
    if (st_lookup(arg->compat_tbl, v, &real_obj)) {
        rb_hash_aset(arg->data, INT2FIX(RHASH_SIZE(arg->data)), (VALUE)real_obj);
    }
    else {
        rb_hash_aset(arg->data, INT2FIX(RHASH_SIZE(arg->data)), v);
    }
    if (arg->taint) {
        OBJ_TAINT(v);
        if ((VALUE)real_obj != Qundef)
            OBJ_TAINT((VALUE)real_obj);
    }
    return v;
}

static VALUE
r_leave(VALUE v, struct load_arg *arg)
{
    st_data_t data;
    if (st_lookup(arg->compat_tbl, v, &data)) {
        VALUE real_obj = (VALUE)data;
        rb_alloc_func_t allocator = rb_get_alloc_func(CLASS_OF(real_obj));
        st_data_t key = v;
        if (st_lookup(compat_allocator_tbl, (st_data_t)allocator, &data)) {
            marshal_compat_t *compat = (marshal_compat_t*)data;
            compat->loader(real_obj, v);
        }
        st_delete(arg->compat_tbl, &key, 0);
        v = real_obj;
    }
    if (arg->proc) {
	v = rb_funcall(arg->proc, rb_intern("call"), 1, v);
	check_load_arg(arg);
    }
    return v;
}

static void
r_ivar(VALUE obj, struct load_arg *arg)
{
    long len;

    len = r_long(arg);
    if (len > 0) {
	while (len--) {
	    ID id = r_symbol(arg);
	    VALUE val = r_object(arg);
#if !WITH_OBJC
	    if (id == rb_id_encoding()) {
		int idx = rb_enc_find_index(StringValueCStr(val));
		if (idx > 0) rb_enc_associate_index(obj, idx);
	    }
	    else 
#endif
	    {
		rb_ivar_set(obj, id, val);
	    }
	}
    }
}

static VALUE
path2class(const char *path)
{
    VALUE v = rb_path2class(path);

    if (TYPE(v) != T_CLASS) {
	rb_raise(rb_eArgError, "%s does not refer class", path);
    }
    return v;
}

static VALUE
path2module(const char *path)
{
    VALUE v = rb_path2class(path);

    if (TYPE(v) != T_MODULE) {
	rb_raise(rb_eArgError, "%s does not refer module", path);
    }
    return v;
}

static VALUE
obj_alloc_by_path(const char *path, struct load_arg *arg)
{
    VALUE klass;
    st_data_t data;
    rb_alloc_func_t allocator;

    klass = path2class(path);

    allocator = rb_get_alloc_func(klass);
    if (st_lookup(compat_allocator_tbl, (st_data_t)allocator, &data)) {
        marshal_compat_t *compat = (marshal_compat_t*)data;
        VALUE real_obj = rb_obj_alloc(klass);
        VALUE obj = rb_obj_alloc(compat->oldclass);
        st_insert(arg->compat_tbl, (st_data_t)obj, (st_data_t)real_obj);
        return obj;
    }

    return rb_obj_alloc(klass);
}

static VALUE
r_object0(struct load_arg *arg, int *ivp, VALUE extmod)
{
    VALUE v = Qnil;
    int type = r_byte(arg);
    long id;

    switch (type) {
      case TYPE_LINK:
	id = r_long(arg);
	v = rb_hash_aref(arg->data, LONG2FIX(id));
	check_load_arg(arg);
	if (NIL_P(v)) {
	    rb_raise(rb_eArgError, "dump format error (unlinked)");
	}
	if (arg->proc) {
	    v = rb_funcall(arg->proc, rb_intern("call"), 1, v);
	    check_load_arg(arg);
	}
	break;

      case TYPE_IVAR:
        {
	    int ivar = Qtrue;

	    v = r_object0(arg, &ivar, extmod);
	    if (ivar) r_ivar(v, arg);
	}
	break;

      case TYPE_EXTENDED_R:
	{
	    VALUE m = path2module(r_unique(arg));

            if (NIL_P(extmod)) extmod = rb_ary_new2(0);
            rb_ary_push(extmod, m);

	    v = r_object0(arg, 0, extmod);
            while (RARRAY_LEN(extmod) > 0) {
                m = rb_ary_pop(extmod);
                rb_extend_object(v, m);
            }
	}
	break;

      case TYPE_UCLASS:
	{
	    VALUE c = path2class(r_unique(arg));

	    if (RCLASS_SINGLETON(c)) {
		rb_raise(rb_eTypeError, "singleton can't be loaded");
	    }
	    v = r_object0(arg, 0, extmod);
	    if (rb_special_const_p(v) || TYPE(v) == T_OBJECT || TYPE(v) == T_CLASS) {
format_error:
		rb_raise(rb_eArgError, "dump format error (user class)");
	    }
	    if (TYPE(v) == T_MODULE || !RTEST(rb_class_inherited_p(c, RBASIC(v)->klass))) {
		VALUE tmp = rb_obj_alloc(c);

		if (TYPE(v) != TYPE(tmp)) goto format_error;
	    }
	    RBASIC(v)->klass = c;
	}
	break;

      case TYPE_NIL:
	v = Qnil;
	v = r_leave(v, arg);
	break;

      case TYPE_TRUE:
	v = Qtrue;
	v = r_leave(v, arg);
	break;

      case TYPE_FALSE:
	v = Qfalse;
	v = r_leave(v, arg);
	break;

      case TYPE_FIXNUM:
	{
	    long i = r_long(arg);
	    v = LONG2FIX(i);
	}
	v = r_leave(v, arg);
	break;

      case TYPE_FLOAT:
	{
	    double d, t = 0.0;
	    VALUE str = r_bytes(arg);
	    const char *ptr = RSTRING_PTR(str);

	    if (strcmp(ptr, "nan") == 0) {
		d = t / t;
	    }
	    else if (strcmp(ptr, "inf") == 0) {
		d = 1.0 / t;
	    }
	    else if (strcmp(ptr, "-inf") == 0) {
		d = -1.0 / t;
	    }
	    else {
		char *e;
		d = ruby_strtod(ptr, &e);
		d = load_mantissa(d, e, strlen(ptr) - (e - ptr));
	    }
	    v = DOUBLE2NUM(d);
	    v = r_entry(v, arg);
            v = r_leave(v, arg);
	}
	break;

      case TYPE_BIGNUM:
	{
	    long len;
	    BDIGIT *digits;
	    volatile VALUE data;

	    NEWOBJ(big, struct RBignum);
	    OBJSETUP(big, rb_cBignum, T_BIGNUM);
	    RBIGNUM_SET_SIGN(big, (r_byte(arg) == '+'));
	    len = r_long(arg);
	    data = r_bytes0(len * SIZEOF_SHORT, arg);
	    rb_big_resize((VALUE)big, (len + (SIZEOF_BDIGITS/SIZEOF_SHORT - 1)) * SIZEOF_SHORT / SIZEOF_BDIGITS);
	    digits = RBIGNUM_DIGITS(big);
	    MEMCPY(digits, RSTRING_PTR(data), char, len * SIZEOF_SHORT);
#if SIZEOF_BDIGITS > SIZEOF_SHORT
	    MEMZERO((char *)digits + len * SIZEOF_SHORT, char,
		RBIGNUM_LEN(big) * SIZEOF_BDIGITS - len * SIZEOF_SHORT);
#endif
	    len = RBIGNUM_LEN(big);
	    while (len > 0) {
		unsigned char *p = (unsigned char *)digits;
		BDIGIT num = 0;
#if SIZEOF_BDIGITS > SIZEOF_SHORT
		int shift = 0;
		int i;

		for (i=0; i<SIZEOF_BDIGITS; i++) {
		    num |= (BDIGIT)p[i] << shift;
		    shift += 8;
		}
#else
		num = p[0] | (p[1] << 8);
#endif
		*digits++ = num;
		len--;
	    }
	    v = rb_big_norm((VALUE)big);
	    v = r_entry(v, arg);
            v = r_leave(v, arg);
	}
	break;

      case TYPE_STRING:
	v = r_entry(r_string(arg), arg);
        v = r_leave(v, arg);
	break;

      case TYPE_REGEXP:
	{
	    volatile VALUE str = r_bytes(arg);
	    const char *cstr = RSTRING_PTR(str);
	    const int options = rb_reg_options_from_mri(r_byte(arg));
	    v = r_entry(rb_reg_new(cstr, strlen(cstr), options), arg);
            v = r_leave(v, arg);
	}
	break;

      case TYPE_ARRAY:
	{
	    volatile long len = r_long(arg); /* gcc 2.7.2.3 -O2 bug?? */

	    v = rb_ary_new2(len);
	    v = r_entry(v, arg);
	    while (len--) {
		rb_ary_push(v, r_object(arg));
	    }
            v = r_leave(v, arg);
	}
	break;

      case TYPE_HASH:
      case TYPE_HASH_DEF:
	{
	    long len = r_long(arg);

	    v = rb_hash_new();
	    v = r_entry(v, arg);
	    while (len--) {
		VALUE key = r_object(arg);
		VALUE value = r_object(arg);
		rb_hash_aset(v, key, value);
	    }
#if !WITH_OBJC
	    if (type == TYPE_HASH_DEF) {
		RHASH(v)->ifnone = r_object(arg);
	    }
#endif
            v = r_leave(v, arg);
	}
	break;

      case TYPE_STRUCT:
	{
	    VALUE klass, mem;
            VALUE values;
	    volatile long i;	/* gcc 2.7.2.3 -O2 bug?? */
	    long len;
	    ID slot;

	    klass = path2class(r_unique(arg));
	    len = r_long(arg);

            v = rb_obj_alloc(klass);
	    if (TYPE(v) != T_STRUCT) {
		rb_raise(rb_eTypeError, "class %s not a struct", rb_class2name(klass));
	    }
	    mem = rb_struct_s_members(klass);
            if (RARRAY_LEN(mem) != len) {
                rb_raise(rb_eTypeError, "struct %s not compatible (struct size differs)",
                         rb_class2name(klass));
            }

	    v = r_entry(v, arg);
	    values = rb_ary_new2(len);
	    for (i=0; i<len; i++) {
		slot = r_symbol(arg);

		if (RARRAY_AT(mem, i) != ID2SYM(slot)) {
		    rb_raise(rb_eTypeError, "struct %s not compatible (:%s for :%s)",
			     rb_class2name(klass),
			     rb_id2name(slot),
			     rb_id2name(SYM2ID(RARRAY_AT(mem, i))));
		}
                rb_ary_push(values, r_object(arg));
	    }
            rb_struct_initialize(v, 0, values);
            v = r_leave(v, arg);
	}
	break;

      case TYPE_USERDEF:
        {
	    VALUE klass = path2class(r_unique(arg));
	    VALUE data;

	    if (!rb_obj_respond_to(klass, s_load, Qtrue)) {
		rb_raise(rb_eTypeError, "class %s needs to have method `_load'",
			 rb_class2name(klass));
	    }
	    data = r_bytes(arg);
	    if (ivp) {
		r_ivar(data, arg);
		*ivp = Qfalse;
	    }
	    v = rb_funcall(klass, s_load, 1, data);
	    check_load_arg(arg);
	    v = r_entry(v, arg);
            v = r_leave(v, arg);
	}
        break;

      case TYPE_USRMARSHAL:
        {
	    VALUE klass = path2class(r_unique(arg));
	    VALUE data;

	    v = rb_obj_alloc(klass);
            if (!NIL_P(extmod)) {
                while (RARRAY_LEN(extmod) > 0) {
                    VALUE m = rb_ary_pop(extmod);
                    rb_extend_object(v, m);
                }
            }
	    if (!rb_obj_respond_to(v, s_mload, Qtrue)) {
		rb_raise(rb_eTypeError, "instance of %s needs to have method `marshal_load'",
			 rb_class2name(klass));
	    }
	    v = r_entry(v, arg);
	    data = r_object(arg);
	    rb_funcall(v, s_mload, 1, data);
	    check_load_arg(arg);
            v = r_leave(v, arg);
	}
        break;

      case TYPE_OBJECT:
	{
            v = obj_alloc_by_path(r_unique(arg), arg);
	    const int vt = TYPE(v);
	    if (vt != T_OBJECT && vt != T_NATIVE) {
		rb_raise(rb_eArgError, "dump format error");
	    }
	    v = r_entry(v, arg);
	    r_ivar(v, arg);
	    v = r_leave(v, arg);
	}
	break;

      case TYPE_DATA:
       {
           VALUE klass = path2class(r_unique(arg));
           if (rb_obj_respond_to(klass, s_alloc, Qtrue)) {
	       static int warn = Qtrue;
	       if (warn) {
		   rb_warn("define `allocate' instead of `_alloc'");
		   warn = Qfalse;
	       }
	       v = rb_funcall(klass, s_alloc, 0);
	       check_load_arg(arg);
           }
	   else {
	       v = rb_obj_alloc(klass);
	   }
           if (TYPE(v) != T_DATA) {
               rb_raise(rb_eArgError, "dump format error");
           }
           v = r_entry(v, arg);
           if (!rb_obj_respond_to(v, s_load_data, Qtrue)) {
               rb_raise(rb_eTypeError,
                        "class %s needs to have instance method `_load_data'",
                        rb_class2name(klass));
           }
           rb_funcall(v, s_load_data, 1, r_object0(arg, 0, extmod));
	   check_load_arg(arg);
           v = r_leave(v, arg);
       }
       break;

      case TYPE_MODULE_OLD:
        {
	    volatile VALUE str = r_bytes(arg);

	    v = rb_path2class(RSTRING_PTR(str));
	    v = r_entry(v, arg);
            v = r_leave(v, arg);
	}
	break;

      case TYPE_CLASS:
        {
	    volatile VALUE str = r_bytes(arg);

	    v = path2class(RSTRING_PTR(str));
	    v = r_entry(v, arg);
            v = r_leave(v, arg);
	}
	break;

      case TYPE_MODULE:
        {
	    volatile VALUE str = r_bytes(arg);

	    v = path2module(RSTRING_PTR(str));
	    v = r_entry(v, arg);
            v = r_leave(v, arg);
	}
	break;

      case TYPE_SYMBOL:
	v = ID2SYM(r_symreal(arg));
	v = r_leave(v, arg);
	break;

      case TYPE_SYMLINK:
	v = ID2SYM(r_symlink(arg));
	break;

      default:
	rb_raise(rb_eArgError, "dump format error(0x%x)", type);
	break;
    }
    return v;
}

static VALUE
r_object(struct load_arg *arg)
{
    return r_object0(arg, 0, Qnil);
}

static VALUE
load(struct load_arg *arg)
{
    return r_object(arg);
}

static VALUE
load_ensure(struct load_arg *arg)
{
    if (!DATA_PTR(arg->compat_tbl_wrapper)) {
	return 0;
    }
    st_free_table(arg->symbols);
    st_free_table(arg->compat_tbl);
    DATA_PTR(arg->compat_tbl_wrapper) = 0;
    arg->compat_tbl_wrapper = 0;
    return 0;
}

/*
 * call-seq:
 *     load( source [, proc] ) => obj
 *     restore( source [, proc] ) => obj
 * 
 * Returns the result of converting the serialized data in source into a
 * Ruby object (possibly with associated subordinate objects). source
 * may be either an instance of IO or an object that responds to
 * to_str. If proc is specified, it will be passed each object as it
 * is deserialized.
 */
static VALUE
marshal_load(VALUE self, SEL sel, int argc, VALUE *argv)
{
    VALUE port, proc;
    int major, minor;
    VALUE v;
    struct load_arg *arg;

    arg = (struct load_arg *)xmalloc(sizeof(struct load_arg));
    rb_scan_args(argc, argv, "11", &port, &proc);
    v = rb_check_string_type(port);
    if (!NIL_P(v)) {
	arg->taint = OBJ_TAINTED(port); /* original taintedness */
	v = rb_str_bstr(v);
	port = v;
    }
    else if (rb_obj_respond_to(port, s_getbyte, Qtrue)
	    && rb_obj_respond_to(port, s_read, Qtrue)) {
	if (rb_obj_respond_to(port, s_binmode, Qtrue)) {
	    rb_funcall2(port, s_binmode, 0, 0);
	}
	arg->taint = Qtrue;
    }
    else {
	rb_raise(rb_eTypeError, "instance of IO needed");
    }
    GC_WB(&arg->src, port);
    arg->offset = 0;
    GC_WB(&arg->compat_tbl, st_init_numtable());
    GC_WB(&arg->compat_tbl_wrapper, Data_Wrap_Struct(rb_cData, NULL, 0, arg->compat_tbl));

    major = r_byte(arg);
    minor = r_byte(arg);
    if (major != MARSHAL_MAJOR || minor > MARSHAL_MINOR) {
	rb_raise(rb_eTypeError, "incompatible marshal file format (can't be read)\n\
\tformat version %d.%d required; %d.%d given",
		 MARSHAL_MAJOR, MARSHAL_MINOR, major, minor);
    }
    if (RTEST(ruby_verbose) && minor != MARSHAL_MINOR) {
	rb_warn("incompatible marshal file format (can be read)\n\
\tformat version %d.%d required; %d.%d given",
		MARSHAL_MAJOR, MARSHAL_MINOR, major, minor);
    }

    GC_WB(&arg->symbols, st_init_numtable());
    GC_WB(&arg->data, rb_hash_new());
    if (NIL_P(proc)) {
	arg->proc = 0;
    }
    else {
	GC_WB(&arg->proc, proc);
    }
    v = rb_ensure(load, (VALUE)arg, load_ensure, (VALUE)arg);

    return v;
}

/*
 * The marshaling library converts collections of Ruby objects into a
 * byte stream, allowing them to be stored outside the currently
 * active script. This data may subsequently be read and the original
 * objects reconstituted.
 * Marshaled data has major and minor version numbers stored along
 * with the object information. In normal use, marshaling can only
 * load data written with the same major version number and an equal
 * or lower minor version number. If Ruby's ``verbose'' flag is set
 * (normally using -d, -v, -w, or --verbose) the major and minor
 * numbers must match exactly. Marshal versioning is independent of
 * Ruby's version numbers. You can extract the version by reading the
 * first two bytes of marshaled data.
 *
 *     str = Marshal.dump("thing")
 *     RUBY_VERSION   #=> "1.9.0"
 *     str[0].ord     #=> 4
 *     str[1].ord     #=> 8
 *
 * Some objects cannot be dumped: if the objects to be dumped include
 * bindings, procedure or method objects, instances of class IO, or
 * singleton objects, a TypeError will be raised.
 * If your class has special serialization needs (for example, if you
 * want to serialize in some specific format), or if it contains
 * objects that would otherwise not be serializable, you can implement
 * your own serialization strategy by defining two methods, _dump and
 * _load:
 * The instance method _dump should return a String object containing
 * all the information necessary to reconstitute objects of this class
 * and all referenced objects up to a maximum depth given as an integer
 * parameter (a value of -1 implies that you should disable depth checking).
 * The class method _load should take a String and return an object of this class.
 */
void
Init_marshal(void)
{
    VALUE rb_mMarshal = rb_define_module("Marshal");

    s_dump = rb_intern("_dump");
    s_load = rb_intern("_load");
    s_mdump = rb_intern("marshal_dump");
    s_mload = rb_intern("marshal_load");
    s_dump_data = rb_intern("_dump_data");
    s_load_data = rb_intern("_load_data");
    s_alloc = rb_intern("_alloc");
    s_getbyte = rb_intern("getbyte");
    s_read = rb_intern("read");
    s_write = rb_intern("write");
    s_binmode = rb_intern("binmode");

    rb_objc_define_method(*(VALUE *)rb_mMarshal, "dump", marshal_dump, -1);
    rb_objc_define_method(*(VALUE *)rb_mMarshal, "load", marshal_load, -1);
    rb_objc_define_method(*(VALUE *)rb_mMarshal, "restore", marshal_load, -1);

    rb_define_const(rb_mMarshal, "MAJOR_VERSION", INT2FIX(MARSHAL_MAJOR));
    rb_define_const(rb_mMarshal, "MINOR_VERSION", INT2FIX(MARSHAL_MINOR));

    compat_allocator_tbl = st_init_numtable();
    compat_allocator_tbl_wrapper =
	Data_Wrap_Struct(rb_cData, NULL, 0, compat_allocator_tbl);
    GC_RETAIN(compat_allocator_tbl_wrapper);
}

VALUE
rb_marshal_dump(VALUE obj, VALUE port)
{
    int argc = 1;
    VALUE argv[2];

    argv[0] = obj;
    argv[1] = port;
    if (!NIL_P(port)) argc = 2;
    return marshal_dump(0, 0, argc, argv);
}

VALUE
rb_marshal_load(VALUE port)
{
    return marshal_load(0, 0, 1, &port);
}
