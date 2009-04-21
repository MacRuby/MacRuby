/*  
 *  Copyright (c) 2008, Apple Inc. All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1.  Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *  2.  Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *  3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *      its contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND
 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR
 *  ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 *  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 *  STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 *  IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

#include <Foundation/Foundation.h>
#include "ruby/ruby.h"
#include "ruby/node.h"
#include "ruby/encoding.h"
#include "ruby/objc.h"
#include "roxor.h"
#include "objc.h"
#include "id.h"

#include <unistd.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <sys/mman.h>
#if HAVE_BRIDGESUPPORT_FRAMEWORK
# include <BridgeSupport/BridgeSupport.h>
#else
# include "bs.h"
#endif

//void native_mutex_lock(pthread_mutex_t *lock);
//void native_mutex_unlock(pthread_mutex_t *lock);
//rb_thread_t *rb_thread_wrap_existing_native_thread(rb_thread_id_t id);

typedef struct {
    bs_element_type_t type;
    void *value;
    VALUE klass;
    ffi_type *ffi_type;
} bs_element_boxed_t;

typedef struct {
    char *name;
    struct st_table *cmethods;
    struct st_table *imethods;
} bs_element_indexed_class_t;

VALUE rb_cBoxed;
static ID rb_ivar_type;

static VALUE bs_const_magic_cookie = Qnil;

static struct st_table *bs_constants;
struct st_table *bs_functions;
static struct st_table *bs_boxeds;
static struct st_table *bs_classes;
static struct st_table *bs_inf_prot_cmethods;
static struct st_table *bs_inf_prot_imethods;
static struct st_table *bs_cftypes;

VALUE rb_cPointer;

struct RPointer
{
  void *ptr;
  const char *type;
};

static inline const char *
rb_objc_skip_octype_modifiers(const char *octype)
{
    while (true) {
	switch (*octype) {
	    case _C_CONST:
	    case 'O': /* bycopy */
	    case 'n': /* in */
	    case 'o': /* out */
	    case 'N': /* inout */
	    case 'V': /* oneway */
		octype++;
		break;

	    default:
		return octype;
	}
    }
}

static inline const char *
__iterate_until(const char *type, char end)
{
    char begin;
    unsigned nested;

    begin = *type;
    nested = 0;

    do {
	type++;
	if (*type == begin) {
	    nested++;
	}
	else if (*type == end) {
	    if (nested == 0) {
		return type;
	    }
	    nested--;
	}
    }
    while (YES);

    return NULL;
}

static const char *
rb_objc_get_first_type(const char *type, char *buf, size_t buf_len)
{
    const char *orig_type;
    const char *p;

    orig_type = type;

    type = rb_objc_skip_octype_modifiers(type);

    switch (*type) {
	case '\0':
	    return NULL;
	case _C_ARY_B:
	    type = __iterate_until(type, _C_ARY_E);
            break;
	case _C_STRUCT_B:
	    type = __iterate_until(type, _C_STRUCT_E);
 	    break;
	case _C_UNION_B:
	    type = __iterate_until(type, _C_UNION_E);
	    break;
	case _C_PTR:
	    type++;
	    buf[0] = _C_PTR;
	    buf_len -= 1;
	    return rb_objc_get_first_type(type, &buf[1], buf_len);
    }

    type++;
    p = type;
    while (*p >= '0' && *p <= '9') { p++; }

    if (buf != NULL) {
	size_t len = (long)(type - orig_type);
	assert(len < buf_len);
	strncpy(buf, orig_type, len);
	buf[len] = '\0';
    }

    return p;
}

static ffi_type *
fake_ary_ffi_type(size_t size, size_t align)
{
    static struct st_table *ary_ffi_types = NULL;
    ffi_type *type;
    unsigned i;

    assert(size > 0);

    if (ary_ffi_types == NULL) {
	ary_ffi_types = st_init_numtable();
	GC_ROOT(&ary_ffi_types);
    }

    if (st_lookup(ary_ffi_types, (st_data_t)size, (st_data_t *)&type)) {
	return type;
    }

    type = (ffi_type *)malloc(sizeof(ffi_type));

    type->size = size;
    type->alignment = align;
    type->type = FFI_TYPE_STRUCT;
    type->elements = malloc(size * sizeof(ffi_type *));
  
    for (i = 0; i < size; i++) {
	type->elements[i] = &ffi_type_uchar;
    }

    st_insert(ary_ffi_types, (st_data_t)size, (st_data_t)type);

    return type;
}

static size_t
get_ffi_struct_size(ffi_type *type)
{
    ffi_type **p;
    size_t s;

    if (type->size > 0)
	return type->size;

    assert(type->type == FFI_TYPE_STRUCT);

    for (s = 0, p = &type->elements[0]; *p != NULL; p++)
	s += get_ffi_struct_size(*p);

    return s;
}

static ffi_type *
rb_objc_octype_to_ffitype(const char *octype)
{
    octype = rb_objc_skip_octype_modifiers(octype);

    if (bs_cftypes != NULL && st_lookup(bs_cftypes, (st_data_t)octype, NULL))
	octype = "@";

    switch (*octype) {
	case _C_ID:
	case _C_CLASS:
	case _C_SEL:
	case _C_CHARPTR:
	case _C_PTR:
	    return &ffi_type_pointer;

	case _C_BOOL:
	case _C_UCHR:
	    return &ffi_type_uchar;

	case _C_CHR:
	    return &ffi_type_schar;

	case _C_SHT:
	    return &ffi_type_sshort;

	case _C_USHT:
	    return &ffi_type_ushort;

	case _C_INT:
	    return &ffi_type_sint;

	case _C_UINT:
	    return &ffi_type_uint;

	case _C_LNG:
	    return sizeof(int) == sizeof(long) 
		? &ffi_type_sint : &ffi_type_slong;

#if defined(_C_LNG_LNG)
	case _C_LNG_LNG:
	    return &ffi_type_sint64;
#endif

	case _C_ULNG:
	    return sizeof(unsigned int) == sizeof(unsigned long) 
		? &ffi_type_uint : &ffi_type_ulong;

#if defined(_C_ULNG_LNG)
	case _C_ULNG_LNG:
	    return &ffi_type_uint64;
#endif

	case _C_FLT:
	    return &ffi_type_float;

	case _C_DBL:
	    return &ffi_type_double;

	case _C_ARY_B:
	{
	    NSUInteger size, align;

	    @try {
		NSGetSizeAndAlignment(octype, &size, &align);
	    }
	    @catch (id exception) {
		rb_raise(rb_eRuntimeError, "can't get size of type `%s': %s",
			 octype, [[exception description] UTF8String]);
            }

	    if (size > 0)
		return fake_ary_ffi_type(size, align);
	    break;
        }
	
	case _C_BFLD:
	{
	    char *type;
	    long lng;
	    size_t size;

	    type = (char *)octype;
	    lng  = strtol(type, &type, 10);

	    /* while next type is a bit field */
	    while (*type == _C_BFLD) {
		long next_lng;

		/* skip over _C_BFLD */
		type++;

		/* get next bit field length */
		next_lng = strtol(type, &type, 10);

		/* if spans next word then align to next word */
		if ((lng & ~31) != ((lng + next_lng) & ~31))
		    lng = (lng + 31) & ~31;

		/* increment running length */
		lng += next_lng;
	    }
	    size = (lng + 7) / 8;
	
	    if (size > 0) {	
		if (size == 1)
		    return &ffi_type_uchar;
		else if (size == 2)
		    return &ffi_type_ushort;
		else if (size <= 4)
		    return &ffi_type_uint;
		return fake_ary_ffi_type(size, 0);
	    }
	    break;
	}

	case _C_STRUCT_B:
	{
	    bs_element_boxed_t *bs_boxed;
	    if (st_lookup(bs_boxeds, (st_data_t)octype, 
			  (st_data_t *)&bs_boxed)) {
		bs_element_struct_t *bs_struct = 
		    (bs_element_struct_t *)bs_boxed->value;
		unsigned i;

		assert(bs_boxed->type == BS_ELEMENT_STRUCT);
		if (bs_boxed->ffi_type != NULL)
		    return bs_boxed->ffi_type;

		bs_boxed->ffi_type = (ffi_type *)malloc(sizeof(ffi_type));
		bs_boxed->ffi_type->size = 0;
		bs_boxed->ffi_type->alignment = 0;
		bs_boxed->ffi_type->type = FFI_TYPE_STRUCT;
		bs_boxed->ffi_type->elements = malloc(
	     	    (bs_struct->fields_count + 1) * sizeof(ffi_type *));

		for (i = 0; i < bs_struct->fields_count; i++) {
		    bs_element_struct_field_t *field = &bs_struct->fields[i];
		    bs_boxed->ffi_type->elements[i] = 
			rb_objc_octype_to_ffitype(field->type);
		}
        
		bs_boxed->ffi_type->elements[bs_struct->fields_count] = NULL;

		{
		    /* Prepare a fake cif, to make sure critical things such 
		     * as the ffi_type size is set. 
		     */
		    ffi_cif cif;
		    ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 0, bs_boxed->ffi_type, 
				 NULL);
		    assert(bs_boxed->ffi_type->size > 0);
		}

		return bs_boxed->ffi_type;
	    }
	    break;
	}

	case _C_VOID:
	    return &ffi_type_void;
    }

    rb_raise(rb_eRuntimeError, "unrecognized octype `%s'", octype);

    return NULL;
}

static size_t
rb_objc_octype_size(const char *octype)
{
    ffi_type *t = rb_objc_octype_to_ffitype(octype);
    if (t == NULL)
	rb_bug("can't determine size of type `%s'", octype);
    return t->size;
}

static bool
rb_objc_rval_to_ocsel(VALUE rval, void **ocval)
{
    const char *cstr;

    if (NIL_P(rval)) {
	*(SEL *)ocval = NULL;
	return true;
    }

    switch (TYPE(rval)) {
	case T_STRING:
	    cstr = StringValuePtr(rval);
	    break;

	case T_SYMBOL:
	    cstr = rb_sym2name(rval);
	    break;

	default:
	    return false;
    }

    *(SEL *)ocval = sel_registerName(cstr);
    return true;
}

static void
rb_bs_boxed_assert_ffitype_ok(bs_element_boxed_t *bs_boxed)
{
    if (bs_boxed->ffi_type == NULL && bs_boxed->type == BS_ELEMENT_STRUCT) {
	/* Make sure the ffi_type is set before use. */
	rb_objc_octype_to_ffitype(
	    ((bs_element_struct_t *)bs_boxed->value)->type);
    }
    assert(bs_boxed->ffi_type != NULL);
}

static void rb_objc_ocval_to_rval(void **ocval, const char *octype, VALUE *rbval);

static VALUE
rb_bs_boxed_new_from_ocdata(bs_element_boxed_t *bs_boxed, void *ocval)
{
    if (ocval == NULL)
	return Qnil;

    if (bs_boxed->type == BS_ELEMENT_OPAQUE && *(void **)ocval == NULL)
	return Qnil;

    rb_bs_boxed_assert_ffitype_ok(bs_boxed);

    if (bs_boxed->type == BS_ELEMENT_STRUCT) {
	bs_element_struct_t *bs_struct = (bs_element_struct_t *)bs_boxed->value;
	VALUE *data;
	int i;
	size_t pos;

	data = (VALUE *)xmalloc(bs_struct->fields_count * sizeof(VALUE));
	for (i = 0, pos = 0; i < bs_struct->fields_count; i++) {
	    bs_element_struct_field_t *bs_field = 
		(bs_element_struct_field_t *)&bs_struct->fields[i];
	    VALUE fval;

	    rb_objc_ocval_to_rval(ocval + pos, bs_field->type, &fval);
	    GC_WB(&data[i], fval);
	    pos += rb_objc_octype_to_ffitype(bs_field->type)->size;
	}
	return Data_Wrap_Struct(bs_boxed->klass, NULL, NULL, data);     
    }
    else {
	void *data;

	data = xmalloc(bs_boxed->ffi_type->size);
	memcpy(data, ocval, bs_boxed->ffi_type->size);

	return Data_Wrap_Struct(bs_boxed->klass, NULL, NULL, data);     
    }
}

static long
rebuild_new_struct_ary(ffi_type **elements, VALUE orig, VALUE new)
{
    long n = 0;
    while ((*elements) != NULL) {
	if ((*elements)->type == FFI_TYPE_STRUCT) {
	    long i, n2;
	    VALUE tmp;

	    n2 = rebuild_new_struct_ary((*elements)->elements, orig, new);
	    tmp = rb_ary_new();
	    for (i = 0; i < n2; i++) {
		if (RARRAY_LEN(orig) == 0)
		    return 0;
		rb_ary_push(tmp, rb_ary_shift(orig));
	    }
	    rb_ary_push(new, tmp);
	}
	elements++;
	n++;
    } 
    return n;
}

static VALUE
rb_pointer_create(void *ptr, const char *type)
{
    struct RPointer *data;

    data = (struct RPointer *)xmalloc(sizeof(struct RPointer ));
    data->ptr = ptr;
    data->type = type;

    return Data_Wrap_Struct(rb_cPointer, NULL, NULL, data);
}

static void rb_objc_rval_to_ocval(VALUE, const char *, void **);

static VALUE
rb_pointer_new_with_type(VALUE recv, SEL sel, VALUE type)
{
    const char *ctype;
    ffi_type *ffitype;
    struct RPointer *data;

    Check_Type(type, T_STRING);
    ctype = RSTRING_PTR(type);
    ffitype = rb_objc_octype_to_ffitype(ctype);

    data = (struct RPointer *)xmalloc(sizeof(struct RPointer ));
    GC_WB(&data->ptr, xmalloc(ffitype->size));
    GC_WB(&data->type, xmalloc(strlen(ctype) + 1));
    strcpy((char *)data->type, ctype);

    return Data_Wrap_Struct(rb_cPointer, NULL, NULL, data);
}

static VALUE
rb_pointer_assign(VALUE recv, SEL sel, VALUE val)
{
    struct RPointer *data;

    Data_Get_Struct(recv, struct RPointer, data);

    assert(data != NULL);
    assert(data->ptr != NULL);
    assert(data->type != NULL);

    rb_objc_rval_to_ocval(val, data->type, data->ptr);

    return val;
}

static VALUE
rb_pointer_aref(VALUE recv, SEL sel, VALUE i)
{
    struct RPointer *data;
    int idx;
    VALUE ret;

    Data_Get_Struct(recv, struct RPointer, data);

    assert(data != NULL);
    assert(data->ptr != NULL);
    assert(data->type != NULL);

    idx = FIX2INT(i);

    rb_objc_ocval_to_rval(data->ptr + (idx * rb_objc_octype_size(data->type)),
			  data->type, &ret);

    return ret;
}

static bool
rb_objc_rval_copy_boxed_data(VALUE rval, bs_element_boxed_t *bs_boxed, void *ocval)
{
    rb_bs_boxed_assert_ffitype_ok(bs_boxed);

    if (bs_boxed->type == BS_ELEMENT_STRUCT) {
	bs_element_struct_t *bs_struct = (bs_element_struct_t *)bs_boxed->value;
	bool is_ary;
	long i, n;
	size_t pos;
	VALUE *data = NULL;

	is_ary = TYPE(rval) == T_ARRAY;
	if (is_ary) {
	    n = RARRAY_LEN(rval);
	    if (n < bs_struct->fields_count)
		rb_raise(rb_eArgError, 
			"not enough elements in array `%s' to create " \
			"structure `%s' (%ld for %d)", 
			RSTRING_PTR(rb_inspect(rval)), bs_struct->name, n, 
			bs_struct->fields_count);

	    if (n > bs_struct->fields_count) {
		VALUE new_rval = rb_ary_new();
		VALUE orig = rval;
		rval = rb_ary_dup(rval);
		rebuild_new_struct_ary(bs_boxed->ffi_type->elements, rval, 
			new_rval);
		n = RARRAY_LEN(new_rval);
		if (RARRAY_LEN(rval) != 0 || n != bs_struct->fields_count) {
		    rb_raise(rb_eArgError, 
			    "too much elements in array `%s' to create " \
			    "structure `%s' (%ld for %d)", 
			    RSTRING_PTR(rb_inspect(orig)), 
			    bs_struct->name, RARRAY_LEN(orig), 
			    bs_struct->fields_count);
		}
		rval = new_rval;
	    }
	}
	else {
	    if (TYPE(rval) != T_DATA)
		return false;
	    Data_Get_Struct(rval, VALUE, data);
	}

	for (i = 0, pos = 0; i < bs_struct->fields_count; i++) {
	    char *field_type;
	    VALUE o;

	    field_type = bs_struct->fields[i].type;
	    o = is_ary ? RARRAY_AT(rval, i) : data[i];
	    rb_objc_rval_to_ocval(o, field_type, ocval + pos);
	    pos += rb_objc_octype_to_ffitype(field_type)->size;
	}
    }
    else {
	void *data;

	if (rval == Qnil) {
	    *(void **)ocval = NULL; 
	}
	else {
	    Data_Get_Struct(rval, void, data);
	    if (data == NULL) {
		*(void **)ocval = NULL; 
	    }
	    else {
		memcpy(ocval, data, bs_boxed->ffi_type->size);
	    }
	}
    }

    return true;
}

static void
rb_objc_rval_to_ocval(VALUE rval, const char *octype, void **ocval)
{
    bs_element_boxed_t *bs_boxed;
    bool ok = true;

    octype = rb_objc_skip_octype_modifiers(octype);

    DLOG("CONV", "ruby obj=%p type=%s dest=%p", (void *)rval, octype, ocval);

    if (*octype == _C_VOID)
	return;

    if (bs_boxeds != NULL
	&& st_lookup(bs_boxeds, (st_data_t)octype, (st_data_t *)&bs_boxed)) {
	ok = rb_objc_rval_copy_boxed_data(rval, bs_boxed, (void *)ocval);
	goto bails; 
    }

    if (bs_cftypes != NULL && st_lookup(bs_cftypes, (st_data_t)octype, NULL))
	octype = "@";

    if (*octype != _C_BOOL && *octype != _C_ID) {
	if (rval == Qtrue)
	    rval = INT2FIX(1);
	else if (rval == Qfalse)
	    rval = INT2FIX(0);
    }

    switch (*octype) {
	case _C_ID:
	case _C_CLASS:
	    *(id *)ocval = rval == Qnil ? NULL : RB2OC(rval);
	    ok = true;
	    break;

	case _C_SEL:
	    ok = rb_objc_rval_to_ocsel(rval, ocval);
	    break;

	case _C_PTR:
	    switch (TYPE(rval)) {
		case T_NIL:
		    *(void **)ocval = NULL;
		    break;
		case T_STRING:
		    *(char **)ocval = StringValuePtr(rval);
		    break;
		case T_ARRAY:
		    {
			int i, count = RARRAY_LEN(rval);
			void *buf = NULL;

			if (count > 0) {
			    size_t subs = rb_objc_octype_size(octype + 1);
			    void *p;
			    p = buf = xmalloc(subs * count);
			    for (i = 0; i < count; i++) {
				rb_objc_rval_to_ocval(RARRAY_AT(rval, i), octype + 1, p);
				p += subs;
			    }
			}
			*(void **)ocval = buf;
		    }
		    break;
		default:
		    if (SPECIAL_CONST_P(rval)) {
			ok = false;
		    }
		    else if (*(VALUE *)rval == rb_cPointer) {
			struct RPointer *data;

			Data_Get_Struct(rval, struct RPointer, data);
			*(void **)ocval = data->ptr;
		    }
		    else if (strcmp(octype, "^v") == 0) {
			*(void **)ocval = (void *)rval;
		    }
		    else if (st_lookup(bs_boxeds, (st_data_t)octype + 1, 
			     (st_data_t *)&bs_boxed)) {
			*(void **)ocval = xmalloc(bs_boxed->ffi_type->size);
			ok = rb_objc_rval_copy_boxed_data(rval, bs_boxed, *(void **)ocval);
		    }
		    else {
			ok = false;
		    }
		    break;
	    }
	    break;

	case _C_UCHR:
 	    *(unsigned char *)ocval = (unsigned char) 
		NUM2UINT(rb_Integer(rval));
	    break;

	case _C_BOOL:
	    {
		unsigned char v;

		switch (TYPE(rval)) {
		    case T_FALSE:
		    case T_NIL:
			v = 0;
			break;
		    case T_TRUE:
			/* All other types should be converted as true, to 
			 * follow the Ruby semantics (where for example any 
			 * integer is always true, even 0)
			 */
		    default:
			v = 1;
			break;
		}
		*(unsigned char *)ocval = v;
	    }
	    break;

	case _C_CHR:
	    if (TYPE(rval) == T_STRING && RSTRING_LEN(rval) == 1) {
		*(char *)ocval = RSTRING_PTR(rval)[0];
	    }
	    else {
		*(char *)ocval = (char) NUM2INT(rb_Integer(rval));
	    }
	    break;

	case _C_SHT:
	    *(short *)ocval = (short) NUM2INT(rb_Integer(rval));
	    break;

	case _C_USHT:
	    *(unsigned short *)ocval = 
		(unsigned short)NUM2UINT(rb_Integer(rval));
	    break;

	case _C_INT:
	    *(int *)ocval = (int) NUM2INT(rb_Integer(rval));
	    break;

	case _C_UINT:
	    *(unsigned int *)ocval = (unsigned int) NUM2UINT(rb_Integer(rval));
	    break;

	case _C_LNG:
	    *(long *)ocval = (long) NUM2LONG(rb_Integer(rval));
	    break;

	case _C_ULNG:
	    *(unsigned long *)ocval = (unsigned long)
		NUM2ULONG(rb_Integer(rval));
	    break;

#if HAVE_LONG_LONG
	case _C_LNG_LNG:
	    *(long long *)ocval = (long long) NUM2LL(rb_Integer(rval));
	    break;

	case _C_ULNG_LNG:
	    *(unsigned long long *)ocval = 
		(unsigned long long) NUM2ULL(rb_Integer(rval));
	    break;
#endif

	case _C_FLT:
	    *(float *)ocval = (float) RFLOAT_VALUE(rb_Float(rval));
	    break;

	case _C_DBL:
	    *(double *)ocval = RFLOAT_VALUE(rb_Float(rval));
	    break;

	case _C_CHARPTR:
	    {
		VALUE str = rb_obj_as_string(rval);
		*(char **)ocval = StringValuePtr(str);
	    }
	    break;

	default:
	    ok = false;
    }

bails:
    if (!ok)
    	rb_raise(rb_eArgError, "can't convert Ruby object `%s' to " \
		 "Objective-C value of type `%s'", 
		 RSTRING_PTR(rb_inspect(rval)), octype);
}

static inline bool 
rb_objc_ocid_to_rval(void **ocval, VALUE *rbval)
{
    id ocid = *(id *)ocval;

    if (ocid == NULL) {
	*rbval = Qnil;
    }
    else {
	*rbval = (VALUE)ocid;
    }

    return true;
}

static void
rb_objc_ocval_to_rval(void **ocval, const char *octype, VALUE *rbval)
{
    bool ok;

    octype = rb_objc_skip_octype_modifiers(octype);
    ok = true;
    
    DLOG("CONV", "objc obj=%p type=%s dest=%p", ocval, octype, rbval);

    {
	bs_element_boxed_t *bs_boxed;

	if (bs_boxeds != NULL
	    && st_lookup(bs_boxeds, (st_data_t)octype, 
		      (st_data_t *)&bs_boxed)) {
	    *rbval = rb_bs_boxed_new_from_ocdata(bs_boxed, ocval);
	    goto bails; 
	}

	if (bs_cftypes != NULL 
	    && st_lookup(bs_cftypes, (st_data_t)octype, NULL))
	    octype = "@";
    }
   
    switch (*octype) {
	case _C_ID:
	{
	    id obj = *(id *)ocval;
	    if (obj == NULL) {
		*rbval = Qnil;
	    }
	    else if (*(Class *)obj == (Class)rb_cFixnum) {
		*rbval = LONG2FIX(RFIXNUM(obj)->value);
	    }
	    else {
		*rbval = *(VALUE *)ocval;
	    }
	    ok = true;
	    break;
	}

	case _C_CLASS:
	    *rbval = *(void **)ocval == NULL ? Qnil : *(VALUE *)ocval;
	    ok = true;
	    break;

	case _C_BOOL:
	    *rbval = *(bool *)ocval ? Qtrue : Qfalse;
	    break;

	case _C_CHR:
	    *rbval = INT2NUM(*(char *)ocval);
	    break;

	case _C_UCHR:
	    *rbval = UINT2NUM(*(unsigned char *)ocval);
	    break;

	case _C_SHT:
	    *rbval = INT2NUM(*(short *)ocval);
	    break;

	case _C_USHT:
	    *rbval = UINT2NUM(*(unsigned short *)ocval);
	    break;
	
	case _C_INT:
	    *rbval = INT2NUM(*(int *)ocval);
	    break;
	
	case _C_UINT:
	    *rbval = UINT2NUM(*(unsigned int *)ocval);
	    break;
	
	case _C_LNG:
	    *rbval = INT2NUM(*(long *)ocval);
	    break;
	
	case _C_ULNG:
	    *rbval = UINT2NUM(*(unsigned long *)ocval);
	    break;

	case _C_LNG_LNG:
	    *rbval = LL2NUM(*(long long *)ocval);
	    break;

	case _C_ULNG_LNG:
	    *rbval = ULL2NUM(*(unsigned long long *)ocval);
	    break;

	case _C_FLT:
	    *rbval = rb_float_new((double)(*(float *)ocval));
	    break;

	case _C_DBL:
	    *rbval = rb_float_new(*(double *)ocval);
	    break;

	case _C_SEL:
	    {
		const char *selname = sel_getName(*(SEL *)ocval);
		*rbval = rb_str_new2(selname);
	    }
	    break;

	case _C_CHARPTR:
	    *rbval =  *(void **)ocval == NULL
		? Qnil
		: rb_str_new2(*(char **)ocval);
	    break;

	case _C_PTR:
	    if (*(void **)ocval == NULL) {
		*rbval = Qnil;
	    }
	    else {
		*rbval = rb_pointer_create(*(void **)ocval, octype + 1);
	    }
	    break;

	default:
	    ok = false;
    }

bails:
    if (!ok)
	rb_raise(rb_eArgError, "can't convert C/Objective-C value `%p' " \
		 "of type `%s' to Ruby object", ocval, octype);
}

static void
rb_objc_exc_raise(id exception)
{
    const char *name;
    const char *desc;

    name = [[exception name] UTF8String];
    desc = [[exception reason] UTF8String];

    rb_raise(rb_eRuntimeError, "%s: %s", name, desc);
}

bs_element_method_t *
rb_bs_find_method(Class klass, SEL sel)
{
    if (bs_classes == NULL)
	return NULL;
    do {
	bs_element_indexed_class_t *bs_class;
	bs_element_method_t *bs_method;

	if (st_lookup(bs_classes, (st_data_t)class_getName(klass),
	              (st_data_t *)&bs_class)) {
 	    struct st_table *t = class_isMetaClass(klass) 
		? bs_class->cmethods : bs_class->imethods;
	    if (t != NULL 
		&& st_lookup(t, (st_data_t)sel, (st_data_t *)&bs_method))
		return bs_method;
	}

	klass = class_getSuperclass(klass);
    }
    while (klass != NULL);

    return NULL;
}

#if 0
static const char *
rb_objc_method_get_type(Method method, unsigned count, 
			bs_element_method_t *bs_method, int n,
			char *type, size_t type_len)
{
    if (bs_method != NULL) {
	if (n == -1) {
	    if (bs_method->retval != NULL) {
		return bs_method->retval->type;
	    }
	}
	else {	    
	    unsigned i;
	    for (i = 0; i < bs_method->args_count; i++) {
		if (bs_method->args[i].index == n
		    && bs_method->args[i].type != NULL) {
		    return bs_method->args[i].type; 
		}
	    }
	}
    }
    if (n == -1) {
	method_getReturnType(method, type, type_len);
    }
    else {
	if (n + 2 < count) {
	    method_getArgumentType(method, n + 2, type, type_len);
	}
	else {
	    assert(bs_method->variadic);
	    return "@"; /* FIXME: should parse the format string if any */
	}
    }
    return type;
}
#endif

static inline void
rb_method_setTypeEncoding(Method method, const char *types)
{
    char **types_p = ((void *)method + sizeof(SEL));
    free(*types_p);
    *types_p = strdup(types);
}

static void *rb_ruby_to_objc_closure(const char *octype, unsigned arity, NODE *node);

static inline void
rb_overwrite_method_signature(Class klass, SEL sel, const char *types, bool raise_if_error)
{
    Method method;
    IMP imp;
    NODE *node;

    method = class_getInstanceMethod(klass, sel);
    if (method == NULL) {
	if (raise_if_error) {
	    rb_raise(rb_eArgError, "%c[%s %s] not found",
		    class_isMetaClass(klass) ? '+' : '-',
		    class_getName(klass),
		    sel_getName(sel));
	}
	return;
    }

    if (strcmp(method_getTypeEncoding(method), types) == 0) {
	return;
    }

    imp = method_getImplementation(method);
    node = rb_objc_method_node3(imp);
    if (node == NULL) {
	if (raise_if_error) {
	    rb_raise(rb_eArgError, "%c[%s %s] is a pure Objective-C method",
		class_isMetaClass(klass) ? '+' : '-',
		class_getName(klass),
		sel_getName(sel));
	}
	else {
	    return;
	}
    }

    DLOG("OCALL", "overwrite %c[%s %s] type encoding to %s",
	class_isMetaClass(klass) ? '+' : '-',
	class_getName(klass),
	sel_getName(sel),
	types);

    /* re-generate the FFI closure with the right types */
    imp = rb_ruby_to_objc_closure(types, method_getNumberOfArguments(method) - 2, node);
    method_setImplementation(method, imp);
    
    /* change the method signature */
    rb_method_setTypeEncoding(method, types);
}

VALUE
rb_objc_call2(VALUE recv, VALUE klass, SEL sel, IMP imp, 
	      struct rb_objc_method_sig *sig, bs_element_method_t *bs_method, 
	      int argc, VALUE *argv)
{
    unsigned i, real_count, count;
    ffi_type *ffi_rettype, **ffi_argtypes;
    void *ffi_ret, **ffi_args;
    ffi_cif *cif;
    const char *type;
    char *rettype, buf[100];
    id ocrcv;

    /* XXX very special exceptions! */
    if (recv == rb_cNSMutableHash && sel == @selector(new)) {
	/* because Hash.new can accept a block */
	return rb_class_new_instance(0, NULL, recv);
    }
    else if (sel == @selector(class)) {
	if (RCLASS_META(klass)) {
	    /* because +[NSObject class] returns self */
	    return RCLASS_MODULE(recv) ? rb_cModule : rb_cClass;
	}
	/* because the CF classes should be hidden */
	else if (klass == rb_cCFString) {
	    bool __CFStringIsMutable(void *);
	    return __CFStringIsMutable((void *)recv) 
		? rb_cNSMutableString : rb_cNSString;
	}
	else if (klass == rb_cCFArray) {
	    bool _CFArrayIsMutable(void *);
	    return _CFArrayIsMutable((void *)recv)
		? rb_cNSMutableArray : rb_cNSArray;
	}
	else if (klass == rb_cCFHash) {
	    bool _CFDictionaryIsMutable(void *);
	    return _CFDictionaryIsMutable((void *)recv)
		? rb_cNSMutableHash : rb_cNSHash;
	}
	else if (klass == rb_cCFSet) {
	    bool _CFSetIsMutable(void *);
	    return _CFSetIsMutable((void *)recv)
		? rb_cNSMutableSet : rb_cNSSet;
	}
    }

    ocrcv = RB2OC(recv);

    DLOG("OCALL", "%c[<%s %p> %s] types=%s bs_method=%p", class_isMetaClass((Class)klass) ? '+' : '-', class_getName((Class)klass), (void *)ocrcv, (char *)sel, sig->types, bs_method);

    count = sig->argc;
    assert(count >= 2);

    real_count = count;
    if (bs_method != NULL && bs_method->variadic) {
	if (argc < count - 2)
	    rb_raise(rb_eArgError, "wrong number of arguments (%d for %d)",
		argc, count - 2);
	count = argc + 2;
    }
    else if (argc != count - 2) {
	rb_raise(rb_eArgError, "wrong number of arguments (%d for %d)",
		 argc, count - 2);
    }

    if (count == 2) {
	if (sig->types[0] == '@' || sig->types[0] == '#' || sig->types[0] == 'v') {
	    /* Easy case! */
	    id exception = nil;
	    //UNLOCK_GIL();
	    @try {
		if (klass == *(VALUE *)ocrcv) {
		    ffi_ret = objc_msgSend(ocrcv, sel);
		}
		else {
		    struct objc_super s;
		    s.receiver = ocrcv;
#if defined(__LP64__)
		    s.super_class = (Class)klass;
#else
		    s.class = (Class)klass;
#endif
		    ffi_ret = objc_msgSendSuper(&s, sel);
		}
	    }
	    @catch (id e) {
		exception = e;
	    }
	    //LOCK_GIL();
	    if (exception != nil) {
		rb_objc_exc_raise(exception);
	    }
	    if (sig->types[0] == '@' || sig->types[0] == '#') {
		VALUE retval;
		buf[0] = sig->types[0];
		buf[1] = '\0';
		rb_objc_ocval_to_rval(&ffi_ret, buf, &retval);
		return retval;
	    }
	    return Qnil;
	}
    } 

    const size_t s = sizeof(ffi_type *) * (count + 1);

    ffi_argtypes = bs_method != NULL && bs_method->variadic
	? (ffi_type **)alloca(s) : (ffi_type **)malloc(s);
    ffi_argtypes[0] = &ffi_type_pointer;
    ffi_argtypes[1] = &ffi_type_pointer;

    ffi_args = (void **)alloca(sizeof(void *) * (count + 1));
    ffi_args[0] = &ocrcv;
    ffi_args[1] = &sel;

    type = SkipFirstType(sig->types);
    rettype = alloca(type - sig->types + 1);
    strncpy(rettype, sig->types, type - sig->types);
    rettype[type - sig->types] = '\0';
    ffi_rettype = rb_objc_octype_to_ffitype(rettype);

    type = SkipStackSize(type);
    type = SkipFirstType(type); /* skip receiver */
    type = SkipStackSize(type);
    type = SkipFirstType(type); /* skip selector */

    bs_element_arg_t *bs_args;

    bs_args = bs_method == NULL ? NULL : bs_method->args;

    for (i = 0; i < argc; i++) {
	bs_element_arg_t *bs_arg;
	ffi_type *ffi_argtype;

	bs_arg = NULL;

	if (*type == '\0') {
	    if (bs_method != NULL && bs_method->variadic) {
		buf[0] = '@';
		buf[1] = '\0';
	    }
	    else {
		rb_bug("incomplete method signature `%s' for argc %d", 
		       sig->types, argc);
	    }
	}
	else {
	    const char *type2;

	    type = SkipStackSize(type);
	    type2 = SkipFirstType(type);
	    strncpy(buf, type, MIN(sizeof buf, type2 - type));
	    buf[MIN(sizeof buf, type2 - type)] = '\0';
	    type = type2;

	    if (bs_args != NULL) {
		while ((bs_arg = bs_args)->index < i) {
		    bs_args++;
		}
	    }
	}

	ffi_argtypes[i + 2] = rb_objc_octype_to_ffitype(buf);
	assert(ffi_argtypes[i + 2]->size > 0);
	ffi_argtype = ffi_argtypes[i + 2];
	ffi_args[i + 2] = (void *)alloca(ffi_argtype->size);
	rb_objc_rval_to_ocval(argv[i], buf, ffi_args[i + 2]);

	if (buf[0] == _C_SEL && bs_arg != NULL && bs_arg->sel_of_type != NULL) {
	    SEL arg_sel;
	    int j;

	    arg_sel = *(SEL *)ffi_args[i + 2];

	    /* XXX BridgeSupport tells us that this argument contains a 
	     * selector of the given type, but we don't have any information
	     * regarding the target. RubyCocoa and the other ObjC bridges do 
	     * not really require it since they use the NSObject message 
	     * forwarding mechanism, but MacRuby registers all methods in the
	     * runtime.
	     *
	     * Therefore, we apply here a naive heuristic by assuming that 
	     * either the receiver or one of the arguments of this call is the
	     * future target.
	     */

	    rb_overwrite_method_signature(*(Class *)ocrcv, arg_sel, 
					  bs_arg->sel_of_type, false);

	    for (j = 0; j < argc; j++) {
		if (j != i && !SPECIAL_CONST_P(argv[j])) {
		    rb_overwrite_method_signature(*(Class *)argv[j], arg_sel,
						  bs_arg->sel_of_type, false);
		}
	    }
	}
    }

    ffi_argtypes[count] = NULL;
    ffi_args[count] = NULL;

    cif = (ffi_cif *)alloca(sizeof(ffi_cif));
    if (ffi_prep_cif(cif, FFI_DEFAULT_ABI, count, ffi_rettype, 
		ffi_argtypes) != FFI_OK) {
	rb_fatal("can't prepare cif for objc method type `%s'",
		sig->types);
    }

    if (ffi_rettype != &ffi_type_void) {
	ffi_ret = (void *)alloca(ffi_rettype->size);
	memset(ffi_ret, 0, ffi_rettype->size);
    }
    else {
	ffi_ret = NULL;
    }

    @try {
	ffi_call(cif, FFI_FN(imp), ffi_ret, ffi_args);
    }
    @catch (id e) {
	rb_objc_exc_raise(e);
    }

    if (ffi_rettype != &ffi_type_void) {
	VALUE resp;
	rb_objc_ocval_to_rval(ffi_ret, rettype, &resp);
	return resp;
    }
    else {
	return Qnil;
    }
}

#if 0
VALUE
rb_objc_call(VALUE recv, SEL sel, int argc, VALUE *argv)
{
    VALUE klass;
    Method method;
    struct rb_objc_method_sig sig;

    klass = CLASS_OF(recv);
    method = class_getInstanceMethod((Class)klass, sel);
    assert(rb_objc_fill_sig(recv, (Class)klass, sel, &sig, NULL));

    return rb_objc_call2(recv, klass, sel, method_getImplementation(method), 
	    &sig, NULL, argc, argv);
}
#endif

static inline const char *
rb_get_bs_method_type(bs_element_method_t *bs_method, int arg)
{
    if (bs_method != NULL) {
	if (arg == -1) {
	    if (bs_method->retval != NULL)
		return bs_method->retval->type;
	}
	else {
	    int i;
	    for (i = 0; i < bs_method->args_count; i++) {
		if (bs_method->args[i].index == arg)
		    return bs_method->args[i].type;
	    }
	}
    }
    return NULL;
}

bool
rb_objc_get_types(VALUE recv, Class klass, SEL sel,
		  bs_element_method_t *bs_method, char *buf, size_t buflen)
{
    Method method;
    const char *type;
    unsigned i;

    method = class_getInstanceMethod(klass, sel);
    if (method != NULL) {
	if (bs_method == NULL) {
	    type = method_getTypeEncoding(method);
	    assert(strlen(type) < buflen);
	    buf[0] = '\0';
	    do {
		const char *type2 = SkipFirstType(type);
		strncat(buf, type, type2 - type);
		type = SkipStackSize(type2);
	    }
	    while (*type != '\0');
	    //strlcpy(buf, method_getTypeEncoding(method), buflen);
	    //sig->argc = method_getNumberOfArguments(method);
	}
	else {
	    char buf2[100];
	    type = rb_get_bs_method_type(bs_method, -1);
	    if (type != NULL) {
		strlcpy(buf, type, buflen);
	    }
	    else {
		method_getReturnType(method, buf2, sizeof buf2);
		strlcpy(buf, buf2, buflen);
	    }

	    //sig->argc = method_getNumberOfArguments(method);
	    int argc = method_getNumberOfArguments(method);
	    for (i = 0; i < argc; i++) {
		if (i >= 2 && (type = rb_get_bs_method_type(bs_method, i - 2))
			!= NULL) {
		    strlcat(buf, type, buflen);
		}
		else {
		    method_getArgumentType(method, i, buf2, sizeof(buf2));
		    strlcat(buf, buf2, buflen);
		}
	    }
	}
	return true;
    }
    else if (!SPECIAL_CONST_P(recv)) {
	NSMethodSignature *msig = [(id)recv methodSignatureForSelector:sel];
	if (msig != NULL) {
	    unsigned i;

	    type = rb_get_bs_method_type(bs_method, -1);
	    if (type == NULL) {
		type = [msig methodReturnType];
	    }
	    strlcpy(buf, type, buflen);

	    //sig->argc = [msig numberOfArguments];
	    int argc = [msig numberOfArguments];
	    for (i = 0; i < argc; i++) {
		if (i < 2 || (type = rb_get_bs_method_type(bs_method, i - 2))
			== NULL) {
		    type = [msig getArgumentTypeAtIndex:i];
		}
		strlcat(buf, type, buflen);
	    }

	    return true;
	}
    }
    return false;
}

static id _symbolicator = nil;
#define SYMBOLICATION_FRAMEWORK @"/System/Library/PrivateFrameworks/Symbolication.framework"

typedef struct _VMURange {
    uint64_t location;
    uint64_t length;
} VMURange;

@interface NSObject (SymbolicatorAPIs) 
- (id)symbolicatorForTask:(mach_port_t)task;
- (id)symbolForAddress:(uint64_t)address;
- (void)forceFullSymbolExtraction;
- (VMURange)addressRange;
@end

static inline id
rb_objc_symbolicator(void) 
{
    if (_symbolicator == nil) {
	NSError *error;

	if (![[NSBundle bundleWithPath:SYMBOLICATION_FRAMEWORK] loadAndReturnError:&error]) {
	    NSLog(@"Cannot load Symbolication.framework: %@", error);
	    abort();    
	}

	Class VMUSymbolicator = NSClassFromString(@"VMUSymbolicator");
	_symbolicator = [VMUSymbolicator symbolicatorForTask:mach_task_self()];
	assert(_symbolicator != nil);
    }

    return _symbolicator;
}

bool
rb_objc_symbolize_address(void *addr, void **start, char *name, size_t name_len) 
{
    Dl_info info;
    if (dladdr(addr, &info) != 0) {
	if (start != NULL) {
	    *start = info.dli_saddr;
	}
	if (name != NULL) {
	    strncpy(name, info.dli_sname, name_len);
	}
	return true;
    }

#if 1
    return false;
#else
    id symbolicator = rb_objc_symbolicator();
    id symbol = [symbolicator symbolForAddress:(NSUInteger)addr];
    if (symbol == nil) {
	return false;
    }
    VMURange range = [symbol addressRange];
    if (start != NULL) {
	*start = (void *)(NSUInteger)range.location;
    }
    if (name != NULL) {
	strncpy(name, [[symbol name] UTF8String], name_len);
    }
    return true;
#endif
}

VALUE
rb_file_expand_path(VALUE fname, VALUE dname)
{
    NSString *res = [(NSString *)fname stringByExpandingTildeInPath];
    if (![res isAbsolutePath]) {
	NSString *dir = dname != Qnil
	    	? (NSString *)dname
		: [[NSFileManager defaultManager] currentDirectoryPath];
	res = [dir stringByAppendingPathComponent:res];
    }
    return (VALUE)[res mutableCopy];
}

void
rb_objc_alias(VALUE klass, ID name, ID def)
{
#if 0
    const char *name_str, *def_str;
    SEL name_sel, def_sel;
    Method method, dest_method;
    bool redo = false;
    VALUE included_in_classes;
    int included_in_classes_count = -1;

    name_str = rb_id2name(name);
    def_str = rb_id2name(def);

    name_sel = sel_registerName(name_str);
    def_sel = sel_registerName(def_str);

    included_in_classes = RCLASS_VERSION(klass) & RCLASS_IS_INCLUDED
	? rb_attr_get(klass, idIncludedInClasses) : Qnil;

    method = class_getInstanceMethod((Class)klass, def_sel);
    if (method == NULL) {
	size_t len = strlen(def_str);
	if (def_str[len - 1] != ':') {
	    char buf[100];

	    snprintf(buf, sizeof buf, "%s:", def_str);
	    def_sel = sel_registerName(buf);
	    method = class_getInstanceMethod((Class)klass, def_sel);
	    if (method == NULL) {
		rb_print_undef(klass, def, 0);
	    }
	    len = strlen(name_str);
	    if (name_str[len - 1] != ':') {
		snprintf(buf, sizeof buf, "%s:", name_str);
		name_sel = sel_registerName(buf);
	    }
	}
    }

alias_method:

#define forward_method_definition(sel,imp,types) \
    do { \
        if (included_in_classes != Qnil) { \
            int i; \
            if (included_in_classes_count == -1) \
                included_in_classes_count = RARRAY_LEN(included_in_classes); \
            for (i = 0; i < included_in_classes_count; i++) { \
                VALUE k = RARRAY_AT(included_in_classes, i); \
                Method m = class_getInstanceMethod((Class)k, sel); \
                DLOG("DEFI", "-[%s %s]", class_getName((Class)k), (char *)sel); \
                if (m != NULL) { \
                    Method m2 = class_getInstanceMethod((Class)RCLASS_SUPER(k), sel); \
                    if (m != m2) { \
                        method_setImplementation(m, imp); \
                        break; \
                    } \
                } \
                assert(class_addMethod((Class)k, sel, imp, types)); \
            } \
        } \
    } \
    while (0)

    dest_method = class_getInstanceMethod((Class)klass, name_sel);

    DLOG("ALIAS", "%c[%s %s -> %s] types=%s direct_override=%d orig_node=%p", 
	    class_isMetaClass((Class)klass) ? '+' : '-', class_getName((Class)klass), (char *)name_sel, (char *)def_sel, method_getTypeEncoding(method), dest_method != NULL, rb_objc_method_node3(method_getImplementation(method)));

    if (dest_method != NULL 
	&& dest_method != class_getInstanceMethod((Class)RCLASS_SUPER(klass), name_sel)) {
	method_setImplementation(dest_method, method_getImplementation(method));
    }
    else {
	assert(class_addMethod((Class)klass, name_sel, 
		    method_getImplementation(method), 
		    method_getTypeEncoding(method)));
    }
    forward_method_definition(name_sel, method_getImplementation(method), method_getTypeEncoding(method));

    if (!redo && name_str[strlen(name_str) - 1] != ':') {
	char buf[100];

	snprintf(buf, sizeof buf, "%s:", def_str);
	def_sel = sel_registerName(buf);
	method = class_getInstanceMethod((Class)klass, def_sel);
	if (method != NULL) {
	    snprintf(buf, sizeof buf, "%s:", name_str);
	    name_sel = sel_registerName(buf);
	    redo = true;
	    goto alias_method;
	}	
    }

#undef forward_method_definition
#endif
}

#if 0
static VALUE
rb_super_objc_send(int argc, VALUE *argv, VALUE rcv)
{
    struct objc_ruby_closure_context fake_ctx;
    id ocrcv;
    ID mid;
    Class klass;

    if (argc < 1)
	rb_raise(rb_eArgError, "expected at least one argument");

    mid = rb_to_id(argv[0]);
    argv++;
    argc--;

    ocrcv = RB2OC(rcv);
    klass = class_getSuperclass(*(Class *)ocrcv);

    fake_ctx.selector = sel_registerName(rb_id2name(mid));
    fake_ctx.method = class_getInstanceMethod(klass, fake_ctx.selector); 
    assert(fake_ctx.method != NULL);
    fake_ctx.bs_method = NULL;
    fake_ctx.cif = NULL;
    fake_ctx.imp = NULL;
    fake_ctx.klass = NULL;

    return rb_objc_call_objc(argc, argv, ocrcv, klass, true, &fake_ctx);
}
#endif

#define IGNORE_PRIVATE_OBJC_METHODS 1

struct rb_ruby_to_objc_closure_handler_main_ctx {
  ffi_cif *cif;
  void *resp;
  void **args;
  void *userdata;
};

static VALUE
rb_ruby_to_objc_closure_handler_main(void *ctx)
{
    // TODO remove me
    return Qnil;
#if 0
    struct rb_ruby_to_objc_closure_handler_main_ctx *_ctx = 
	(struct rb_ruby_to_objc_closure_handler_main_ctx *)ctx;
    ffi_cif *cif = _ctx->cif;
    void *resp = _ctx->resp;
    void **args = _ctx->args;
    void *userdata = _ctx->userdata;
    void *rcv;
    SEL sel;
    ID mid;
    VALUE rrcv, ret;
    Method method;
    const char *type;
    char buf[128];
    long i, argc;
    VALUE *argv, klass;
    NODE *body, *node;
    bs_element_method_t *bs_method;

    rcv = *(id *)args[0];
    sel = *(SEL *)args[1];
    body = (NODE *)userdata;
    node = body->nd_body;

    method = class_getInstanceMethod(*(Class *)rcv, sel);
    assert(method != NULL);
    bs_method = rb_bs_find_method(*(Class *)rcv, sel);

    argc = method_getNumberOfArguments(method) - 2;
    if (argc > 0) {
	argv = (VALUE *)alloca(sizeof(VALUE) * argc);
	for (i = 0; i < argc; i++) {
	    VALUE val;

	    type = rb_objc_method_get_type(method, cif->nargs, bs_method,
		    i, buf, sizeof buf);

	    rb_objc_ocval_to_rval(args[i + 2], type, &val);
	    argv[i] = val;
	}
    }
    else {
	argv = NULL;
    }

    rrcv = rcv == NULL ? Qnil : (VALUE)rcv;

    mid = rb_intern((const char *)sel);
    klass = CLASS_OF(rrcv);

    DLOG("RCALL", "%c[<%s %p> %s] node=%p", class_isMetaClass((Class)klass) ? '+' : '-', class_getName((Class)klass), (void *)rrcv, (char *)sel, body);

    ret = rb_vm_call(GET_THREAD(), klass, rrcv, mid, Qnil,
		     argc, argv, node, 0);

    type = rb_objc_method_get_type(method, cif->nargs, bs_method,
	    -1, buf, sizeof buf);
    rb_objc_rval_to_ocval(ret, type, resp);

    return Qnil;
#endif
}

static void
rb_ruby_to_objc_closure_handler(ffi_cif *cif, void *resp, void **args,
				void *userdata)
{
    struct rb_ruby_to_objc_closure_handler_main_ctx ctx;

    ctx.cif = cif;
    ctx.resp = resp;
    ctx.args = args;
    ctx.userdata = userdata;

    rb_ruby_to_objc_closure_handler_main(&ctx);
}

static void *
rb_ruby_to_objc_closure(const char *octype, unsigned arity, NODE *node)
{
    const char *p;
    char buf[128];
    ffi_type *ret, **args;
    ffi_cif *cif;
    ffi_closure *closure;
    unsigned i;

    p = octype;

    assert((p = rb_objc_get_first_type(p, buf, sizeof buf)) != NULL);
    ret = rb_objc_octype_to_ffitype(buf);

    args = (ffi_type **)malloc(sizeof(ffi_type *) * (arity + 2)); 
    i = 0;
    while ((p = rb_objc_get_first_type(p, buf, sizeof buf)) != NULL) {
	args[i] = rb_objc_octype_to_ffitype(buf);
	assert(++i <= arity + 2);
    }

    cif = (ffi_cif *)malloc(sizeof(ffi_cif));
    if (ffi_prep_cif(cif, FFI_DEFAULT_ABI, arity + 2, ret, args) != FFI_OK) {
	rb_fatal("can't prepare ruby to objc cif");
    }

    /* XXX mmap() and mprotect() are 2 expensive calls, maybe we should try to 
     * mmap() and mprotect() a large memory page and reuse it for closures?
     * XXX currently overwriting a closure leaks the previous one!
     */

    if ((closure = mmap(NULL, sizeof(ffi_closure), PROT_READ | PROT_WRITE,
			MAP_ANON | MAP_PRIVATE, -1, 0)) == (void *)-1) {
	rb_fatal("can't allocate ruby to objc closure");
    }

    if (ffi_prep_closure(closure, cif, rb_ruby_to_objc_closure_handler, node)
	!= FFI_OK) {
	rb_fatal("can't prepare ruby to objc closure");
    }

    if (mprotect(closure, sizeof(closure), PROT_READ | PROT_EXEC) == -1) {
	rb_fatal("can't mprotect the ruby to objc closure");
    }

    rb_objc_retain(node);

    return closure;
}

NODE *
rb_objc_method_node3(IMP imp)
{
    if (imp == NULL || ((ffi_closure *)imp)->fun != rb_ruby_to_objc_closure_handler)
	return NULL;

    return ((ffi_closure *)imp)->user_data;
}

extern id _objc_msgForward(id receiver, SEL sel, ...);
static void *_objc_msgForward_addr = NULL;

NODE *
rb_objc_method_node2(VALUE mod, SEL sel, IMP *pimp)
{
    IMP imp;

    if (pimp != NULL)
	*pimp = NULL;

    imp = class_getMethodImplementation((Class)mod, sel);
    if (imp == (IMP)_objc_msgForward_addr)
	imp = NULL;

    if (pimp != NULL)
	*pimp = imp;

    if (imp == NULL || ((ffi_closure *)imp)->fun != rb_ruby_to_objc_closure_handler)
	return NULL;

    return ((ffi_closure *)imp)->user_data;
}

NODE *
rb_objc_method_node(VALUE mod, ID mid, IMP *pimp, SEL *psel)
{
    SEL sel;
    IMP imp;
    NODE *node;

    sel = mid == ID_ALLOCATOR 
	? @selector(alloc) 
	: sel_registerName(rb_id2name(mid));

    if (psel != NULL) {
	*psel = sel;
    }

    node = rb_objc_method_node2(mod, sel, &imp);

    if (pimp != NULL) {
	*pimp = imp;
    }

    if (imp == NULL) {
    	char buf[100];
	size_t slen;

	slen = strlen((char *)sel);
	if (((char *)sel)[slen - 1] == ':') {
	    return NULL;
	}
	strlcpy(buf, (char *)sel, sizeof buf);
	strlcat(buf, ":", sizeof buf);
	sel = sel_registerName(buf);
	if (psel != NULL) {
	    *psel = sel;
	}
	return rb_objc_method_node2(mod, sel, pimp);
    }

    return node;
}

void
rb_objc_register_ruby_method(VALUE mod, ID mid, NODE *body)
{
    SEL sel;
    Method method;
    char *types;
    int arity, oc_arity;
    IMP imp;
    bool direct_override;
    NODE *node;
    VALUE included_in_classes;
    int included_in_classes_count = - 1;

#define forward_method_definition(sel,imp,types) \
    do { \
	if (included_in_classes != Qnil) { \
	    int i; \
	    if (included_in_classes_count == -1) \
	        included_in_classes_count = RARRAY_LEN(included_in_classes); \
	    for (i = 0; i < included_in_classes_count; i++) { \
		VALUE k = RARRAY_AT(included_in_classes, i); \
		Method m = class_getInstanceMethod((Class)k, sel); \
		DLOG("DEFI", "-[%s %s]", class_getName((Class)k), (char *)sel); \
		if (m != NULL) { \
		    Method m2 = class_getInstanceMethod((Class)RCLASS_SUPER(k), sel); \
		    if (m != m2) { \
		        method_setImplementation(m, imp); \
		        break; \
		    } \
	        } \
	        assert(class_addMethod((Class)k, sel, imp, types)); \
	    } \
	} \
    } \
    while (0)

    if (body != NULL) {
	if (nd_type(body) != NODE_METHOD) 
	    rb_bug("non-method node (%d)", nd_type(body));

	node = body->nd_body;
	arity = oc_arity = rb_node_arity(node);
    }
    else {
	node = NULL;
	arity = oc_arity = 0;
    }

    if (mid == ID_ALLOCATOR) {
	sel = @selector(alloc);
    }
    else {
	char *mid_str;
	size_t mid_str_len;
	char buf[100];

	mid_str = (char *)rb_id2name(mid);
	mid_str_len = strlen(mid_str);

	if ((arity < 0 || arity > 0) && mid_str[mid_str_len - 1] != ':') {
	    assert(sizeof(buf) > mid_str_len + 1);
	    snprintf(buf, sizeof buf, "%s:", mid_str);
	    sel = sel_registerName(buf);
	    oc_arity = 1;
	}
	else {
	    sel = sel_registerName(mid_str);
	    if (sel == sel_ignored || sel == sel_zone) {
		assert(sizeof(buf) > mid_str_len + 7);
		snprintf(buf, sizeof buf, "__rb_%s__", mid_str);
		sel = sel_registerName(buf);
	    }
	}
    }

    included_in_classes = RCLASS_VERSION(mod) & RCLASS_IS_INCLUDED
	? rb_attr_get(mod, idIncludedInClasses) : Qnil;

    direct_override = false;
    method = class_getInstanceMethod((Class)mod, sel);

    if (method != NULL) {
	Class klass;

	if (oc_arity + 2 != method_getNumberOfArguments(method)) {
	    rb_warn("cannot override Objective-C method `%s' in " \
		    "class `%s' because of an arity mismatch (%d for %d)", 
		    (char *)method_getName(method),
		    class_getName((Class)mod), 
		    oc_arity + 2, 
		    method_getNumberOfArguments(method));
	    return;
	}
	types = (char *)method_getTypeEncoding(method);
	klass = (Class)RCLASS_SUPER(mod);
	direct_override = 
	    klass == NULL || class_getInstanceMethod(klass, sel) != method;
    }
    else {
	struct st_table *t = class_isMetaClass((Class)mod)
	    ? bs_inf_prot_cmethods
	    : bs_inf_prot_imethods;

	if (t == NULL || !st_lookup(t, (st_data_t)sel, (st_data_t *)&types)) {
	    if (oc_arity == 0) {
		types = "@@:";
	    }
	    else if (oc_arity == 1) {
		types = "@@:@";
	    }
	    else {
		int i;
		types = alloca(3 + oc_arity + 1);
		types[0] = '@';
		types[1] = '@';
		types[2] = ':';
		for (i = 0; i < oc_arity; i++)
		    types[3 + i] = '@'; 
		types[3 + oc_arity] = '\0';
	    }
	}
    }

    DLOG("DEFM", "%c[%s %s] types=%s arity=%d body=%p override=%d direct_override=%d",
	   class_isMetaClass((Class)mod) ? '+' : '-', class_getName((Class)mod), (char *)sel, types, arity, body, method != NULL, direct_override);

    imp = body == NULL ? NULL : rb_ruby_to_objc_closure(types, oc_arity, body);

    if (method != NULL && direct_override) {
	method_setImplementation(method, imp);
    }
    else {
	assert(class_addMethod((Class)mod, sel, imp, types));
    }
    forward_method_definition(sel, imp, types);

    if (node != NULL) {
	const char *sel_str = (const char *)sel;
	const size_t sel_len = strlen(sel_str);
	SEL new_sel;
	char *new_types;
	bool override;

	if (sel_str[sel_len - 1] == ':') {
	    char buf[100];
	    strlcpy(buf, sel_str, sizeof buf);
	    assert(sizeof buf > sel_len);
	    buf[sel_len - 1] = '\0';
	    new_sel = sel_registerName(buf);
	    new_types = "@@:";
	    override = arity == -1 || arity == -2;
	    arity = 0;
	}
	else {
	    char buf[100];
	    strlcpy(buf, sel_str, sizeof buf);
	    strlcat(buf, ":", sizeof buf);	
	    new_sel = sel_registerName(buf);
	    new_types = "@@:@";
	    override = false;
	    arity = -1;
	}

	method = class_getInstanceMethod((Class)mod, new_sel);
	direct_override = false;
	if (method != NULL || override) {
	    direct_override = method != NULL && class_getInstanceMethod((Class)RCLASS_SUPER(mod), new_sel) != method;
	    DLOG("DEFM", "%c[%s %s] types=%s arity=%d body=%p override=%d direct_override=%d",
		class_isMetaClass((Class)mod) ? '+' : '-', class_getName((Class)mod), (char *)new_sel, new_types, arity, body, method != NULL, direct_override);
	    if (method != NULL && direct_override) {
	 	method_setImplementation(method, imp);	
	    }
	    else { 
		assert(class_addMethod((Class)mod, new_sel, imp, new_types));
	    }
	    forward_method_definition(new_sel, imp, new_types);
	}
    }
#undef forward_method_definition
}

void 
rb_objc_change_ruby_method_signature(VALUE mod, VALUE mid, VALUE sig)
{
    SEL sel = sel_registerName(rb_id2name(rb_to_id(mid)));
    char *types = StringValuePtr(sig);
    rb_overwrite_method_signature((Class)mod, sel, types, true);
}

static inline bool
rb_objc_resourceful(VALUE obj)
{
    /* TODO we should export this function in the runtime 
     * Object#__resourceful__? perhaps? 
     */
    extern CFTypeID __CFGenericTypeID(void *);
    CFTypeID t = __CFGenericTypeID((void *)obj);
    if (t > 0) {
	extern void *_CFRuntimeGetClassWithTypeID(CFTypeID);
	long *d = (long *)_CFRuntimeGetClassWithTypeID(t);
	/* first long is version, 4 means resourceful */
	if (d != NULL && *d & 4)
	    return true;	
    }
    return false;
}

VALUE
rb_bsfunc_call(bs_element_function_t *bs_func, void *sym, int argc, VALUE *argv)
{
    unsigned i;
    ffi_type *ffi_rettype, **ffi_argtypes;
    void *ffi_ret, **ffi_args;
    ffi_cif *cif;
    VALUE resp;

    if (argc != bs_func->args_count)
	rb_raise(rb_eArgError, "wrong number of arguments (%d for %d)",
		 argc, bs_func->args_count);

    DLOG("FCALL", "%s() sym=%p argc=%d", bs_func->name, sym, argc);

    ffi_argtypes = (ffi_type **)alloca(sizeof(ffi_type *) * argc + 1);
    ffi_args = (void **)alloca(sizeof(void *) * argc + 1);

    for (i = 0; i < argc; i++) {
	char *type = bs_func->args[i].type;
	ffi_argtypes[i] = rb_objc_octype_to_ffitype(type);
	ffi_args[i] = (void *)alloca(ffi_argtypes[i]->size);
	rb_objc_rval_to_ocval(argv[i], type, ffi_args[i]);
    }

    ffi_argtypes[argc] = NULL;
    ffi_args[argc] = NULL;

    ffi_rettype = bs_func->retval == NULL
    	? &ffi_type_void
	: rb_objc_octype_to_ffitype(bs_func->retval->type);

    cif = (ffi_cif *)alloca(sizeof(ffi_cif));
    if (ffi_prep_cif(cif, FFI_DEFAULT_ABI, argc, ffi_rettype, ffi_argtypes) 
	!= FFI_OK)
	rb_fatal("can't prepare cif for function `%s'", bs_func->name);

    if (ffi_rettype != &ffi_type_void) {
	ffi_ret = (void *)alloca(ffi_rettype->size);
    }
    else {
	ffi_ret = NULL;
    }

    @try {
	ffi_call(cif, FFI_FN(sym), ffi_ret, ffi_args);
    }
    @catch (id e) {
	rb_objc_exc_raise(e);
    }

    resp = Qnil;
    if (ffi_rettype != &ffi_type_void) {
	rb_objc_ocval_to_rval(ffi_ret, bs_func->retval->type, &resp);
    	if (bs_func->retval->already_retained && !rb_objc_resourceful(resp))
	    CFMakeCollectable((void *)resp);
    }
    return resp;
}

VALUE
rb_objc_resolve_const_value(VALUE v, VALUE klass, ID id)
{
    void *sym;
    bs_element_constant_t *bs_const;

    if (v == bs_const_magic_cookie) { 
	if (!st_lookup(bs_constants, (st_data_t)id, (st_data_t *)&bs_const))
	    rb_bug("unresolved bridgesupport constant `%s' not in cache",
		    rb_id2name(id));

	sym = dlsym(RTLD_DEFAULT, bs_const->name);
	if (sym == NULL)
	    rb_bug("cannot locate symbol for unresolved bridgesupport " \
		    "constant `%s'", bs_const->name);

	rb_objc_ocval_to_rval(sym, bs_const->type, &v);

	CFMutableDictionaryRef iv_dict = rb_class_ivar_dict(rb_cObject);
	assert(iv_dict != NULL);
	CFDictionarySetValue(iv_dict, (const void *)id, (const void *)v);
    }

    return v;
}

static bs_element_boxed_t *
rb_klass_get_bs_boxed(VALUE recv)
{
    bs_element_boxed_t *bs_boxed;
    VALUE type;

    type = rb_ivar_get(recv, rb_ivar_type);
    if (NIL_P(type))
	rb_bug("cannot get boxed objc type of class `%s'", 
	       rb_class2name(recv));
    
    assert(TYPE(type) == T_STRING);

    if (st_lookup(bs_boxeds, (st_data_t)StringValuePtr(type), 
		  (st_data_t *)&bs_boxed)) {
	rb_bs_boxed_assert_ffitype_ok(bs_boxed);
	return bs_boxed;
    }
    return NULL;
}

#if 0
static VALUE
rb_bs_struct_new(int argc, VALUE *argv, VALUE recv)
{
    bs_element_boxed_t *bs_boxed = rb_klass_get_bs_boxed(recv);
    bs_element_struct_t *bs_struct = (bs_element_struct_t *)bs_boxed->value;    
    VALUE *data;
    unsigned i;

    if (argc > 0 && argc != bs_struct->fields_count)
	rb_raise(rb_eArgError, "wrong number of arguments (%d for %d)",
		 argc, bs_struct->fields_count);

    data = (VALUE *)xmalloc(bs_struct->fields_count * sizeof(VALUE));

    for (i = 0; i < bs_struct->fields_count; i++) {
	bs_element_struct_field_t *bs_field = 
	    (bs_element_struct_field_t *)&bs_struct->fields[i];
	size_t fdata_size;
	void *fdata;
	VALUE fval;

	fdata_size = rb_objc_octype_to_ffitype(bs_field->type)->size;
	fdata = alloca(fdata_size);
	if (i < argc) {
	    rb_objc_rval_to_ocval(argv[i], bs_field->type, fdata);
	}
	else {
	    memset(fdata, 0, fdata_size);
	}
	rb_objc_ocval_to_rval(fdata, bs_field->type, &fval);
	GC_WB(&data[i], fval);
    }

    return Data_Wrap_Struct(recv, NULL, NULL, data);
}

static VALUE
rb_bs_struct_to_a(VALUE recv)
{
    bs_element_boxed_t *bs_boxed = rb_klass_get_bs_boxed(CLASS_OF(recv));
    bs_element_struct_t *bs_struct = (bs_element_struct_t *)bs_boxed->value;    
    VALUE *data;
    VALUE ary;
    unsigned i;
    
    Data_Get_Struct(recv, VALUE, data);
    assert(data != NULL);
    
    ary = rb_ary_new();

    for (i = 0; i < bs_struct->fields_count; i++) {
	rb_ary_push(ary, data[i]);
    }

    return ary;
}

static VALUE
rb_bs_boxed_is_equal(VALUE recv, VALUE other)
{
    bs_element_boxed_t *bs_boxed;  

    if (recv == other)
	return Qtrue;

    if (rb_obj_is_kind_of(other, rb_cBoxed) == Qfalse)
	return Qfalse;

    bs_boxed = rb_klass_get_bs_boxed(CLASS_OF(recv));
    if (bs_boxed != rb_klass_get_bs_boxed(CLASS_OF(other)))
	return Qfalse;

    return CFEqual((CFTypeRef)recv, (CFTypeRef)other) ? Qtrue : Qfalse;
}

static VALUE
rb_bs_struct_dup(VALUE recv)
{
    bs_element_boxed_t *bs_boxed = rb_klass_get_bs_boxed(CLASS_OF(recv));
    bs_element_struct_t *bs_struct = (bs_element_struct_t *)bs_boxed->value;    
    VALUE *data, *new_data;
    int i;
    static ID idDup = 0;

    if (idDup == 0) {
	idDup = rb_intern("dup");
    }

    Data_Get_Struct(recv, VALUE, data);
    new_data = (VALUE *)xmalloc(bs_struct->fields_count * sizeof(VALUE));

    for (i = 0; i < bs_struct->fields_count; i++) {
	bs_element_struct_field_t *bs_field =
	    (bs_element_struct_field_t *)&bs_struct->fields[i];
	size_t fdata_size;
	void *fdata;
	VALUE fval;

	fdata_size = rb_objc_octype_to_ffitype(bs_field->type)->size;
	fdata = alloca(fdata_size);

	rb_objc_rval_to_ocval(data[i], bs_field->type, fdata);
	rb_objc_ocval_to_rval(fdata, bs_field->type, &fval);

	GC_WB(&new_data[i], fval);
    }

    return Data_Wrap_Struct(CLASS_OF(recv), NULL, NULL, new_data);
}

static VALUE
rb_bs_struct_inspect(VALUE recv)
{
    bs_element_boxed_t *bs_boxed = rb_klass_get_bs_boxed(CLASS_OF(recv));
    bs_element_struct_t *bs_struct = (bs_element_struct_t *)bs_boxed->value;    
    unsigned i;
    VALUE str;

    str = rb_str_new2("#<");
    rb_str_cat2(str, rb_obj_classname(recv));

    if (!bs_struct->opaque) {
	VALUE *data;

	Data_Get_Struct(recv, VALUE, data);

	for (i = 0; i < bs_struct->fields_count; i++) {
	    rb_str_cat2(str, " ");
	    rb_str_cat2(str, bs_struct->fields[i].name);
	    rb_str_cat2(str, "=");
	    rb_str_append(str, rb_inspect(data[i]));
	}
    }

    rb_str_cat2(str, ">");

    return str;
}
#endif

static VALUE
rb_boxed_objc_type(VALUE recv, SEL sel)
{
    char *type;

    bs_element_boxed_t *bs_boxed;

    bs_boxed = rb_klass_get_bs_boxed(recv);
    type = bs_boxed->type == BS_ELEMENT_OPAQUE
	? ((bs_element_opaque_t *)bs_boxed->value)->type
	: ((bs_element_struct_t *)bs_boxed->value)->type;

    return rb_str_new2(type);
}

static VALUE
rb_boxed_is_opaque(VALUE recv, SEL sel)
{
    bs_element_boxed_t *bs_boxed;

    bs_boxed = rb_klass_get_bs_boxed(recv);
    if (bs_boxed->type == BS_ELEMENT_OPAQUE)
	return Qtrue;

    return ((bs_element_struct_t *)bs_boxed->value)->opaque ? Qtrue : Qfalse;
}

static VALUE
rb_boxed_fields(VALUE recv, SEL sel)
{
    bs_element_boxed_t *bs_boxed;
    VALUE ary;
    unsigned i;

    bs_boxed = rb_klass_get_bs_boxed(recv);

    ary = rb_ary_new();
    if (bs_boxed->type == BS_ELEMENT_STRUCT) {
	bs_element_struct_t *bs_struct;
	bs_struct = (bs_element_struct_t *)bs_boxed->value;
	for (i = 0; i < bs_struct->fields_count; i++) {
	    rb_ary_push(ary, ID2SYM(rb_intern(bs_struct->fields[i].name)));
	}
    }
    return ary;
}

#if 0
static ffi_cif *struct_reader_cif = NULL;
static ffi_cif *struct_writer_cif = NULL;
#endif

struct rb_struct_accessor_context {
    bs_element_struct_field_t *field;
    int num;
};

#if 0
static void
rb_struct_reader_closure_handler(ffi_cif *cif, void *resp, void **args,
				 void *userdata)
{
    struct rb_struct_accessor_context *ctx;
    VALUE recv, *data;
 
    recv = *(VALUE *)args[0];
    Data_Get_Struct(recv, VALUE, data);
    assert(data != NULL);

    ctx = (struct rb_struct_accessor_context *)userdata;
    *(VALUE *)resp = data[ctx->num];
}

static void
rb_struct_writer_closure_handler(ffi_cif *cif, void *resp, void **args,
                                 void *userdata)
{
    struct rb_struct_accessor_context *ctx;
    VALUE recv, value, *data, fval;
    size_t fdata_size;
    void *fdata;

    recv = *(VALUE *)args[0];
    value = *(VALUE *)args[1];
    Data_Get_Struct(recv, VALUE, data);
    assert(data != NULL);

    ctx = (struct rb_struct_accessor_context *)userdata;

    fdata_size = rb_objc_octype_to_ffitype(ctx->field->type)->size;
    fdata = alloca(fdata_size);

    rb_objc_rval_to_ocval(value, ctx->field->type, fdata);
    rb_objc_ocval_to_rval(fdata, ctx->field->type, &fval);

    GC_WB(&data[ctx->num], fval);

    *(VALUE *)resp = fval;
}
#endif

#if 0
static void
rb_struct_gen_accessors(VALUE klass, bs_element_struct_field_t *field, int num)
{
    ffi_closure *closure;
    struct rb_struct_accessor_context *ctx;
    char buf[100];

    ctx = (struct rb_struct_accessor_context *)
	malloc(sizeof(struct rb_struct_accessor_context));
    ctx->field = field;
    ctx->num = num;

    if (struct_reader_cif == NULL) {
	ffi_type **args;

	struct_reader_cif = (ffi_cif *)malloc(sizeof(ffi_cif));
	args = (ffi_type **)malloc(sizeof(ffi_type *) * 1);
	args[0] = &ffi_type_pointer;
	if (ffi_prep_cif(struct_reader_cif, FFI_DEFAULT_ABI, 1, 
			 &ffi_type_pointer, args) != FFI_OK) {
	    rb_fatal("can't prepare struct_reader_cif");
	}
    }
    if ((closure = mmap(NULL, sizeof(ffi_closure), PROT_READ | PROT_WRITE,
			MAP_ANON | MAP_PRIVATE, -1, 0)) == (void *)-1) {
	rb_fatal("can't allocate struct reader closure");
    }
    if (ffi_prep_closure(closure, struct_reader_cif, 
		         rb_struct_reader_closure_handler, ctx) != FFI_OK) {
	rb_fatal("can't prepare struct reader closure");
    }
    if (mprotect(closure, sizeof(closure), PROT_READ | PROT_EXEC) == -1) {
	rb_fatal("can't mprotect struct reader closure");
    }

    rb_define_method(klass, field->name, (VALUE(*)(ANYARGS))closure, 0);

    if (struct_writer_cif == NULL) {
	ffi_type **args;

	struct_writer_cif = (ffi_cif *)malloc(sizeof(ffi_cif));
	args = (ffi_type **)malloc(sizeof(ffi_type *) * 2);
	args[0] = &ffi_type_pointer;
	args[1] = &ffi_type_pointer;
	if (ffi_prep_cif(struct_writer_cif, FFI_DEFAULT_ABI, 2, 
			 &ffi_type_pointer, args) != FFI_OK) {
	    rb_fatal("can't prepare struct_writer_cif");
	}
    }
    if ((closure = mmap(NULL, sizeof(ffi_closure), PROT_READ | PROT_WRITE,
			MAP_ANON | MAP_PRIVATE, -1, 0)) == (void *)-1) {
	rb_fatal("can't allocate struct writer closure");
    }
    if (ffi_prep_closure(closure, struct_writer_cif, 
			 rb_struct_writer_closure_handler, ctx) != FFI_OK) {
	rb_fatal("can't prepare struct writer closure");
    }
    if (mprotect(closure, sizeof(closure), PROT_READ | PROT_EXEC) == -1) {
	rb_fatal("can't mprotect struct writer closure");
    }

    snprintf(buf, sizeof buf, "%s=", field->name);
    rb_define_method(klass, buf, (VALUE(*)(ANYARGS))closure, 1);
}
#endif

#if 0
static void
setup_bs_boxed_type(bs_element_type_t type, void *value)
{
    bs_element_boxed_t *bs_boxed;
    VALUE klass;
    struct __bs_boxed {
	char *name;
	char *type;
    } *p;
    ffi_type *bs_ffi_type;

    p = (struct __bs_boxed *)value;

    klass = rb_define_class(p->name, rb_cBoxed);
    assert(!NIL_P(klass));
    rb_ivar_set(klass, rb_ivar_type, rb_str_new2(p->type));

    if (type == BS_ELEMENT_STRUCT) {
	bs_element_struct_t *bs_struct = (bs_element_struct_t *)value;
	int i;

	/* Needs to be lazily created, because the type of some fields
	 * may not be registered yet.
	 */
        bs_ffi_type = NULL; 

	if (!bs_struct->opaque) {
	    for (i = 0; i < bs_struct->fields_count; i++) {
		bs_element_struct_field_t *field = &bs_struct->fields[i];
		rb_struct_gen_accessors(klass, field, i);
	    }
	    rb_define_method(klass, "to_a", rb_bs_struct_to_a, 0);
	}

	rb_define_singleton_method(klass, "new", rb_bs_struct_new, -1);
	rb_define_method(klass, "dup", rb_bs_struct_dup, 0);
	rb_define_alias(klass, "clone", "dup");	
	rb_define_method(klass, "inspect", rb_bs_struct_inspect, 0);
	rb_define_alias(klass, "to_s", "inspect");
    }
    else {
	rb_undef_alloc_func(klass);
	rb_undef_method(CLASS_OF(klass), "new");
	bs_ffi_type = &ffi_type_pointer;
    }
    rb_define_method(klass, "==", rb_bs_boxed_is_equal, 1);

    bs_boxed = (bs_element_boxed_t *)malloc(sizeof(bs_element_boxed_t));
    bs_boxed->type = type;
    bs_boxed->value = value; 
    bs_boxed->klass = klass;
    bs_boxed->ffi_type = bs_ffi_type;

    st_insert(bs_boxeds, (st_data_t)p->type, (st_data_t)bs_boxed);
}
#endif

static VALUE
rb_objc_load_bs(VALUE recv, SEL sel, VALUE path)
{
    rb_vm_load_bridge_support(StringValuePtr(path), NULL, 0);
    return recv;
}

static void
rb_objc_search_and_load_bridge_support(const char *framework_path)
{
    char path[PATH_MAX];

    if (bs_find_path(framework_path, path, sizeof path)) {
	rb_vm_load_bridge_support(path, framework_path,
                                    BS_PARSE_OPTIONS_LOAD_DYLIBS);
    }
}

static void
reload_protocols(void)
{
    Protocol **prots;
    unsigned int i, prots_count;

    prots = objc_copyProtocolList(&prots_count);
    for (i = 0; i < prots_count; i++) {
	Protocol *p;
	struct objc_method_description *methods;
	unsigned j, methods_count;

	p = prots[i];

#define REGISTER_MDESCS(t) \
    do { \
	for (j = 0; j < methods_count; j++) { \
	    if (methods[j].name == sel_ignored) \
		continue; \
	    st_insert(t, (st_data_t)methods[j].name, \
		      (st_data_t)strdup(methods[j].types)); \
	} \
	free(methods); \
    } \
    while (0)

	methods = protocol_copyMethodDescriptionList(p, true, true, &methods_count);
	REGISTER_MDESCS(bs_inf_prot_imethods);
	methods = protocol_copyMethodDescriptionList(p, false, true, &methods_count);
	REGISTER_MDESCS(bs_inf_prot_imethods);
	methods = protocol_copyMethodDescriptionList(p, true, false, &methods_count);
	REGISTER_MDESCS(bs_inf_prot_cmethods);
	methods = protocol_copyMethodDescriptionList(p, false, false, &methods_count);
	REGISTER_MDESCS(bs_inf_prot_cmethods);

#undef REGISTER_MDESCS
    }
    free(prots);
}

static void
reload_class_constants(void)
{
    static int class_count = 0;
    int i, count;
    Class *buf;

    count = objc_getClassList(NULL, 0);
    if (count == class_count)
	return;

    buf = (Class *)alloca(sizeof(Class) * count);
    objc_getClassList(buf, count);

    for (i = 0; i < count; i++) {
	const char *name = class_getName(buf[i]);
	if (name[0] != '_') {
	    ID id = rb_intern(name);
	    if (!rb_const_defined(rb_cObject, id))
		rb_const_set(rb_cObject, id, (VALUE)buf[i]);
	}
    }

    class_count = count;
}

VALUE
rb_require_framework(VALUE recv, SEL sel, int argc, VALUE *argv)
{
    VALUE framework;
    VALUE search_network;
    const char *cstr;
    NSFileManager *fileManager;
    NSString *path;
    NSBundle *bundle;
    NSError *error;

    rb_scan_args(argc, argv, "11", &framework, &search_network);

    Check_Type(framework, T_STRING);
    cstr = RSTRING_PTR(framework);

    fileManager = [NSFileManager defaultManager];
    path = [fileManager stringWithFileSystemRepresentation:cstr
	length:strlen(cstr)];

    if (![fileManager fileExistsAtPath:path]) {
	/* framework name is given */
	NSSearchPathDomainMask pathDomainMask;
	NSString *frameworkName;
	NSArray *dirs;
	NSUInteger i, count;

	cstr = NULL;

#define FIND_LOAD_PATH_IN_LIBRARY(dir) 					  \
    do { 								  \
	path = [[dir stringByAppendingPathComponent:@"Frameworks"]	  \
	   stringByAppendingPathComponent:frameworkName];		  \
	if ([fileManager fileExistsAtPath:path])  			  \
	    goto success; 						  \
	path = [[dir stringByAppendingPathComponent:@"PrivateFrameworks"] \
	   stringByAppendingPathComponent:frameworkName];		  \
	if ([fileManager fileExistsAtPath:path]) 			  \
	    goto success; 						  \
    } 									  \
    while(0)

	pathDomainMask = RTEST(search_network)
	    ? NSAllDomainsMask
	    : NSUserDomainMask | NSLocalDomainMask | NSSystemDomainMask;

	frameworkName = [path stringByAppendingPathExtension:@"framework"];

	path = [[[[NSBundle mainBundle] bundlePath] 
	    stringByAppendingPathComponent:@"Contents/Frameworks"] 
		stringByAppendingPathComponent:frameworkName];
	if ([fileManager fileExistsAtPath:path])
	    goto success;	

	dirs = NSSearchPathForDirectoriesInDomains(NSLibraryDirectory, 
	    pathDomainMask, YES);
	for (i = 0, count = [dirs count]; i < count; i++) {
	    NSString *dir = [dirs objectAtIndex:i];
	    FIND_LOAD_PATH_IN_LIBRARY(dir);
	}	

	dirs = NSSearchPathForDirectoriesInDomains(NSDeveloperDirectory, 
	    pathDomainMask, YES);
	for (i = 0, count = [dirs count]; i < count; i++) {
	    NSString *dir = [[dirs objectAtIndex:i] 
		stringByAppendingPathComponent:@"Library"];
	    FIND_LOAD_PATH_IN_LIBRARY(dir); 
	}

#undef FIND_LOAD_PATH_IN_LIBRARY

	rb_raise(rb_eRuntimeError, "framework `%s' not found", 
	    RSTRING_PTR(framework));
    }

success:

    if (cstr == NULL)
	cstr = [path fileSystemRepresentation];

    bundle = [NSBundle bundleWithPath:path];
    if (bundle == nil)
	rb_raise(rb_eRuntimeError, 
	         "framework at path `%s' cannot be located",
		 cstr);

    if ([bundle isLoaded])
	return Qfalse;

    if (![bundle loadAndReturnError:&error]) {
	rb_raise(rb_eRuntimeError,
		 "framework at path `%s' cannot be loaded: %s",
		 cstr,
		 [[error description] UTF8String]); 
    }

    rb_objc_search_and_load_bridge_support(cstr);
    reload_class_constants();
    reload_protocols();

    return Qtrue;
}

static const char *
imp_rb_boxed_objCType(void *rcv, SEL sel)
{
    VALUE klass, type;

    klass = CLASS_OF(rcv);
    type = rb_boxed_objc_type(klass, 0);
    
    return StringValuePtr(type);
}

static void
imp_rb_boxed_getValue(void *rcv, SEL sel, void *buffer)
{
    bs_element_boxed_t *bs_boxed;

    bs_boxed = rb_klass_get_bs_boxed(CLASS_OF(rcv));
    assert(rb_objc_rval_copy_boxed_data((VALUE)rcv, bs_boxed, buffer));
}

static void
rb_install_boxed_primitives(void)
{
    Class klass;

    /* Boxed */
    klass = (Class)rb_cBoxed;
    rb_objc_install_method(klass, @selector(objCType), 
	(IMP)imp_rb_boxed_objCType);
    rb_objc_install_method(klass, @selector(getValue:), 
	(IMP)imp_rb_boxed_getValue);
}

static const char *
resources_path(char *path, size_t len)
{
    CFBundleRef bundle;
    CFURLRef url;

    bundle = CFBundleGetMainBundle();
    assert(bundle != NULL);

    url = CFBundleCopyResourcesDirectoryURL(bundle);
    *path = '-'; 
    *(path+1) = 'I';
    assert(CFURLGetFileSystemRepresentation(
	url, true, (UInt8 *)&path[2], len - 2));
    CFRelease(url);

    return path;
}

int
macruby_main(const char *path, int argc, char **argv)
{
    char **newargv;
    char *p1, *p2;
    int n, i;

    newargv = (char **)malloc(sizeof(char *) * (argc + 2));
    for (i = n = 0; i < argc; i++) {
	if (!strncmp(argv[i], "-psn_", 5) == 0)
	    newargv[n++] = argv[i];
    }
    
    p1 = (char *)malloc(PATH_MAX);
    newargv[n++] = (char *)resources_path(p1, PATH_MAX);

    p2 = (char *)malloc(PATH_MAX);
    snprintf(p2, PATH_MAX, "%s/%s", (path[0] != '/') ? &p1[2] : "", path);
    newargv[n++] = p2;

    argv = newargv;    
    argc = n;

    ruby_sysinit(&argc, &argv);
    {
	void *tree;
	ruby_init();
	tree = ruby_options(argc, argv);
	free(newargv);
	free(p1);
	free(p2);
	return ruby_run_node(tree);
    }
}

static void *
rb_objc_kvo_setter_imp(void *recv, SEL sel, void *value)
{
    const char *selname;
    char buf[128];
    size_t s;   

    selname = sel_getName(sel);
    buf[0] = '@';
    buf[1] = tolower(selname[3]);
    s = strlcpy(&buf[2], &selname[4], sizeof buf - 2);
    buf[s + 1] = '\0';

    rb_ivar_set((VALUE)recv, rb_intern(buf), value == NULL ? Qnil : OC2RB(value));

    return NULL; /* we explicitely return NULL because otherwise a special constant may stay on the stack and be returned to Objective-C, and do some very nasty crap, especially if called via -[performSelector:]. */
}

/*
  Defines an attribute writer method which conforms to Key-Value Coding.
  (See http://developer.apple.com/documentation/Cocoa/Conceptual/KeyValueCoding/KeyValueCoding.html)
  
    attr_accessor :foo
  
  Will create the normal accessor methods, plus <tt>setFoo</tt>
  
  TODO: Does not handle the case were the user might override #foo=
*/
void
rb_objc_define_kvo_setter(VALUE klass, ID mid)
{
    char buf[100];
    const char *mid_name;

    buf[0] = 's'; buf[1] = 'e'; buf[2] = 't';
    mid_name = rb_id2name(mid);

    buf[3] = toupper(mid_name[0]);
    buf[4] = '\0';
    strlcat(buf, &mid_name[1], sizeof buf);
    strlcat(buf, ":", sizeof buf);

    if (!class_addMethod((Class)klass, sel_registerName(buf), 
			 (IMP)rb_objc_kvo_setter_imp, "v@:@")) {
	rb_warn("can't register `%s' as an KVO setter (method `%s')",
		mid_name, buf);
    }
}

VALUE
rb_mod_objc_ib_outlet(VALUE recv, SEL sel, int argc, VALUE *argv)
{
    int i;

    rb_warn("ib_outlet has been deprecated, please use attr_writer instead");

    for (i = 0; i < argc; i++) {
	VALUE sym = argv[i];
	
	Check_Type(sym, T_SYMBOL);
	rb_objc_define_kvo_setter(recv, SYM2ID(sym));
    }

    return recv;
}

#define FLAGS_AS_ASSOCIATIVE_REF 1

static CFMutableDictionaryRef __obj_flags;

long
rb_objc_flag_get_mask(const void *obj)
{
#if FLAGS_AS_ASSOCIATIVE_REF
    return (long)rb_objc_get_associative_ref((void *)obj, &__obj_flags);
#else
    if (__obj_flags == NULL)
	return 0;

    return (long)CFDictionaryGetValue(__obj_flags, obj);
#endif
}

bool
rb_objc_flag_check(const void *obj, int flag)
{
    long v;

    v = rb_objc_flag_get_mask(obj);
    if (v == 0)
	return false;

    return (v & flag) == flag;
}

void
rb_objc_flag_set(const void *obj, int flag, bool val)
{
#if FLAGS_AS_ASSOCIATIVE_REF
    long v = (long)rb_objc_get_associative_ref((void *)obj, &__obj_flags);
    if (val) {
	v |= flag;
    }
    else {
	v ^= flag;
    }
    rb_objc_set_associative_ref((void *)obj, &__obj_flags, (void *)v);
#else
    long v;

    if (__obj_flags == NULL) {
	__obj_flags = CFDictionaryCreateMutable(NULL, 0, NULL, NULL);
    }
    v = (long)CFDictionaryGetValue(__obj_flags, obj);
    if (val) {
	v |= flag;
    }
    else {
	v ^= flag;
    }
    CFDictionarySetValue(__obj_flags, obj, (void *)v);
#endif
}

long
rb_objc_remove_flags(const void *obj)
{
#if FLAGS_AS_ASSOCIATIVE_REF
    long flag = (long)rb_objc_get_associative_ref((void *)obj, &__obj_flags);
    //rb_objc_set_associative_ref((void *)obj, &__obj_flags, (void *)0);
    return flag;
#else
    long flag;
    if (CFDictionaryGetValueIfPresent(__obj_flags, obj, 
	(const void **)&flag)) {
	CFDictionaryRemoveValue(__obj_flags, obj);
	return flag;
    }
    return 0;
#endif
}

static void
rb_objc_get_types_for_format_str(char **octypes, const int len, VALUE *args,
				 const char *format_str, char **new_fmt)
{
    unsigned i, j, format_str_len;

    format_str_len = strlen(format_str);
    i = j = 0;

    while (i < format_str_len) {
	bool sharp_modifier = false;
	bool star_modifier = false;
	if (format_str[i++] != '%')
	    continue;
	if (i < format_str_len && format_str[i] == '%') {
	    i++;
	    continue;
	}
	while (i < format_str_len) {
	    char *type = NULL;
	    switch (format_str[i]) {
		case '#':
		    sharp_modifier = true;
		    break;

		case '*':
		    star_modifier = true;
		    type = "i"; // C_INT;
		    break;

		case 'd':
		case 'i':
		case 'o':
		case 'u':
		case 'x':
		case 'X':
		    type = "i"; // _C_INT;
		    break;

		case 'c':
		case 'C':
		    type = "c"; // _C_CHR;
		    break;

		case 'D':
		case 'O':
		case 'U':
		    type = "l"; // _C_LNG;
		    break;

		case 'f':       
		case 'F':
		case 'e':       
		case 'E':
		case 'g':       
		case 'G':
		case 'a':
		case 'A':
		    type = "d"; // _C_DBL;
		    break;

		case 's':
		case 'S':
		    {
			if (i - 1 > 0) {
			    long k = i - 1;
			    while (k > 0 && format_str[k] == '0')
				k--;
			    if (k < i && format_str[k] == '.')
				args[j] = (VALUE)CFSTR("");
			}
			type = "*"; // _C_CHARPTR;
		    }
		    break;

		case 'p':
		    type = "^"; // _C_PTR;
		    break;

		case '@':
		    type = "@"; // _C_ID;
		    break;

		case 'B':
		case 'b':
		    {
			VALUE arg = args[j];
			switch (TYPE(arg)) {
			    case T_STRING:
				arg = rb_str_to_inum(arg, 0, Qtrue);
				break;
			}
			arg = rb_big2str(arg, 2);
			if (sharp_modifier) {
			    VALUE prefix = format_str[i] == 'B'
				? (VALUE)CFSTR("0B") : (VALUE)CFSTR("0b");
			   rb_str_update(arg, 0, 0, prefix);
			}
			if (*new_fmt == NULL)
			    *new_fmt = strdup(format_str);
			(*new_fmt)[i] = '@';
			args[j] = arg;
			type = "@"; 
		    }
		    break;
	    }

	    i++;

	    if (type != NULL) {
		if (len == 0 || j >= len)
		    rb_raise(rb_eArgError, 
			"Too much tokens in the format string `%s' "\
			"for the given %d argument(s)", format_str, len);
		octypes[j++] = type;
		if (!star_modifier)
		    break;
	    }
	}
    }
    for (; j < len; j++)
	octypes[j] = "@"; // _C_ID;
}

VALUE
rb_str_format(int argc, const VALUE *argv, VALUE fmt)
{
    char **types;
    ffi_type *ffi_rettype, **ffi_argtypes;
    void *ffi_ret, **ffi_args;
    ffi_cif *cif;
    int i;
    void *null;
    char *new_fmt;

    if (argc == 0)
	return fmt;

    types = (char **)alloca(sizeof(char *) * argc);
    ffi_argtypes = (ffi_type **)alloca(sizeof(ffi_type *) * (argc + 4));
    ffi_args = (void **)alloca(sizeof(void *) * (argc + 4));

    null = NULL;
    new_fmt = NULL;

    rb_objc_get_types_for_format_str(types, argc, (VALUE *)argv, 
	    RSTRING_PTR(fmt), &new_fmt);
    if (new_fmt != NULL) {
	fmt = (VALUE)CFStringCreateWithCString(NULL, new_fmt, 
		kCFStringEncodingUTF8);
	xfree(new_fmt);
	CFMakeCollectable((void *)fmt);
    }  

    for (i = 0; i < argc; i++) {
	ffi_argtypes[i + 3] = rb_objc_octype_to_ffitype(types[i]);
	ffi_args[i + 3] = (void *)alloca(ffi_argtypes[i + 3]->size);
	rb_objc_rval_to_ocval(argv[i], types[i], ffi_args[i + 3]);
    }

    ffi_argtypes[0] = &ffi_type_pointer;
    ffi_args[0] = &null;
    ffi_argtypes[1] = &ffi_type_pointer;
    ffi_args[1] = &null;
    ffi_argtypes[2] = &ffi_type_pointer;
    ffi_args[2] = &fmt;
   
    ffi_argtypes[argc + 4] = NULL;
    ffi_args[argc + 4] = NULL;

    ffi_rettype = &ffi_type_pointer;
    
    cif = (ffi_cif *)alloca(sizeof(ffi_cif));

    if (ffi_prep_cif(cif, FFI_DEFAULT_ABI, argc + 3, ffi_rettype, ffi_argtypes)
        != FFI_OK)
        rb_fatal("can't prepare cif for CFStringCreateWithFormat");

    ffi_ret = NULL;

    ffi_call(cif, FFI_FN(CFStringCreateWithFormat), &ffi_ret, ffi_args);

    if (ffi_ret != NULL) {
        CFMakeCollectable((CFTypeRef)ffi_ret);
        return (VALUE)ffi_ret;
    }
    return Qnil;
}

extern bool __CFStringIsMutable(void *);
extern bool _CFArrayIsMutable(void *);
extern bool _CFDictionaryIsMutable(void *);

bool
rb_objc_is_immutable(VALUE v)
{
    switch(TYPE(v)) {
	case T_STRING:
	    return !__CFStringIsMutable((void *)v);
	case T_ARRAY:
	    return !_CFArrayIsMutable((void *)v);
	case T_HASH:
	    return !_CFDictionaryIsMutable((void *)v);	    
    }
    return false;
}

#if 0
static void 
timer_cb(CFRunLoopTimerRef timer, void *ctx)
{
    RUBY_VM_CHECK_INTS();
}
#endif

static IMP old_imp_isaForAutonotifying;

static Class
rb_obj_imp_isaForAutonotifying(void *rcv, SEL sel)
{
    Class ret;
    long ret_version;

#define KVO_CHECK_DONE 0x100000

    ret = ((Class (*)(void *, SEL)) old_imp_isaForAutonotifying)(rcv, sel);
    if (ret != NULL && ((ret_version = RCLASS_VERSION(ret)) & KVO_CHECK_DONE) == 0) {
	const char *name = class_getName(ret);
	if (strncmp(name, "NSKVONotifying_", 15) == 0) {
	    Class ret_orig;
	    name += 15;
	    ret_orig = objc_getClass(name);
	    if (ret_orig != NULL && RCLASS_VERSION(ret_orig) & RCLASS_IS_OBJECT_SUBCLASS) {
		DLOG("XXX", "marking KVO generated klass %p (%s) as RObject", ret, class_getName(ret));
		ret_version |= RCLASS_IS_OBJECT_SUBCLASS;
	    }
	}
	ret_version |= KVO_CHECK_DONE;
	RCLASS_SET_VERSION(ret, ret_version);
    }
    return ret;
}

void *placeholder_String = NULL;
void *placeholder_Dictionary = NULL;
void *placeholder_Array = NULL;

void
Init_ObjC(void)
{
    rb_objc_retain(bs_constants = st_init_numtable());
    rb_objc_retain(bs_functions = st_init_numtable());
    rb_objc_retain(bs_boxeds = st_init_strtable());
    rb_objc_retain(bs_classes = st_init_strtable());
    rb_objc_retain(bs_inf_prot_cmethods = st_init_numtable());
    rb_objc_retain(bs_inf_prot_imethods = st_init_numtable());
    rb_objc_retain(bs_cftypes = st_init_strtable());

    rb_objc_retain((const void *)(
	bs_const_magic_cookie = rb_str_new2("bs_const_magic_cookie")));

    rb_cBoxed = rb_define_class("Boxed", (VALUE)objc_getClass("NSValue"));
    RCLASS_SET_VERSION_FLAG(rb_cBoxed, RCLASS_IS_OBJECT_SUBCLASS);
    rb_objc_define_method(*(VALUE *)rb_cBoxed, "type", rb_boxed_objc_type, 0);
    rb_objc_define_method(*(VALUE *)rb_cBoxed, "opaque?", rb_boxed_is_opaque, 0);
    rb_objc_define_method(*(VALUE *)rb_cBoxed, "fields", rb_boxed_fields, 0);
    rb_install_boxed_primitives();

    rb_cPointer = rb_define_class("Pointer", rb_cObject);
    rb_undef_alloc_func(rb_cPointer);
    rb_objc_define_method(*(VALUE *)rb_cPointer, "new_with_type", rb_pointer_new_with_type, 1);
    rb_objc_define_method(rb_cPointer, "assign", rb_pointer_assign, 1);
    rb_objc_define_method(rb_cPointer, "[]", rb_pointer_aref, 1);

    rb_ivar_type = rb_intern("@__objc_type__");

    rb_objc_define_method(rb_mKernel, "load_bridge_support_file", rb_objc_load_bs, 1);

#if 0
    {
	/* XXX timer_cb should acquires the GL or not be triggered when 
	 * MacRuby.framework is loaded in an existing Objective-C app.
	 */
	CFRunLoopTimerRef timer;
	timer = CFRunLoopTimerCreate(NULL,
		CFAbsoluteTimeGetCurrent(), 0.1, 0, 0, timer_cb, NULL);
	CFRunLoopAddTimer(CFRunLoopGetMain(), timer, kCFRunLoopDefaultMode);
    }
#endif

    //rb_define_method(rb_cNSObject, "__super_objc_send__", rb_super_objc_send, -1);

    Method m = class_getInstanceMethod(objc_getClass("NSKeyValueUnnestedProperty"), sel_registerName("isaForAutonotifying"));
    assert(m != NULL);
    old_imp_isaForAutonotifying = method_getImplementation(m);
    method_setImplementation(m, (IMP)rb_obj_imp_isaForAutonotifying);

#if 0
    {
	VALUE klass;
	NODE *node, *body;
	void *closure;

	klass = rb_singleton_class(rb_cNSObject);
	node = NEW_CFUNC(rb_class_new_instance, -1);
	body = NEW_FBODY(NEW_METHOD(node, klass, NOEX_PUBLIC), 0);
	closure = rb_ruby_to_objc_closure("@@:@", 1, body->nd_body);
	assert(class_addMethod((Class)klass, @selector(new:), (IMP)closure, "@@:@"));
    }
#endif
    
    _objc_msgForward_addr = &_objc_msgForward;
        
    placeholder_String = objc_getClass("NSPlaceholderMutableString");
    placeholder_Dictionary = objc_getClass("__NSPlaceholderDictionary");
    placeholder_Array = objc_getClass("__NSPlaceholderArray");
}

// for debug in gdb
int __rb_type(VALUE v) { return TYPE(v); }
int __rb_native(VALUE v) { return NATIVE(v); }

@interface Protocol
@end

@implementation Protocol (MRFindProtocol)
+(id)protocolWithName:(NSString *)name
{
    return (id)objc_getProtocol([name UTF8String]);
} 
@end

extern int ruby_initialized; /* eval.c */

@implementation MacRuby

+ (MacRuby *)sharedRuntime
{
    static MacRuby *runtime = nil;
    if (runtime == nil) {
	runtime = [[MacRuby alloc] init];
	if (ruby_initialized == 0) {
	    int argc = 0;
	    char **argv = NULL;
	    ruby_sysinit(&argc, &argv);
	    ruby_init();
	}
    }
    return runtime;
}

+ (MacRuby *)runtimeAttachedToProcessIdentifier:(pid_t)pid
{
    [NSException raise:NSGenericException format:@"not implemented yet"];
    return nil;
}

static void
rb_raise_ruby_exc_in_objc(VALUE ex)
{
    VALUE ex_name, ex_message, ex_backtrace;
    NSException *ocex;
    static ID name_id = 0;
    static ID message_id = 0;
    static ID backtrace_id = 0;

    if (name_id == 0) {
	name_id = rb_intern("name");
	message_id = rb_intern("message");
	backtrace_id = rb_intern("backtrace");
    }

    ex_name = rb_funcall(CLASS_OF(ex), name_id, 0);
    ex_message = rb_funcall(ex, message_id, 0);
    ex_backtrace = rb_funcall(ex, backtrace_id, 0);

    ocex = [NSException exceptionWithName:(id)ex_name reason:(id)ex_message 
		userInfo:[NSDictionary dictionaryWithObjectsAndKeys:
		    (id)ex, @"object", 
		    (id)ex_backtrace, @"backtrace",
		    NULL]];

    [ocex raise];
}

static VALUE
evaluateString_safe(VALUE expression)
{
    return rb_eval_string([(NSString *)expression UTF8String]);
}

static VALUE
evaluateString_rescue(void)
{
    rb_raise_ruby_exc_in_objc(rb_errinfo());
    return Qnil; /* not reached */
}

- (id)evaluateString:(NSString *)expression
{
    VALUE ret;

    ret = rb_rescue2(evaluateString_safe, (VALUE)expression,
	    	     evaluateString_rescue, Qnil,
		     rb_eException, (VALUE)0);
    return RB2OC(ret);
}

- (id)evaluateFileAtPath:(NSString *)path
{
    return [self evaluateString:[NSString stringWithContentsOfFile:path usedEncoding:nil error:nil]];
}

- (id)evaluateFileAtURL:(NSURL *)URL
{
    if (![URL isFileURL]) {
	[NSException raise:NSInvalidArgumentException format:@"given URL is not a file URL"];
    }
    return [self evaluateFileAtPath:[URL relativePath]];
}

- (void)loadBridgeSupportFileAtPath:(NSString *)path
{
    rb_vm_load_bridge_support([path fileSystemRepresentation], NULL, 0);
}

- (void)loadBridgeSupportFileAtURL:(NSURL *)URL
{
    if (![URL isFileURL]) {
	[NSException raise:NSInvalidArgumentException format:@"given URL is not a file URL"];
    }
    [self loadBridgeSupportFileAtPath:[URL relativePath]];
}

@end

@implementation NSObject (MacRubyAdditions)

- (id)performRubySelector:(SEL)sel
{
    return [self performRubySelector:sel withArguments:NULL];
}

struct performRuby_context
{
    VALUE rcv;
    ID mid;
    int argc;
    VALUE *argv;
    NODE *node;
};

static VALUE
performRuby_safe(VALUE arg)
{
#if 0
    struct performRuby_context *ud = (struct performRuby_context *)arg;
    return rb_vm_call(GET_THREAD(), *(VALUE *)ud->rcv, ud->rcv, ud->mid, Qnil,
		      ud->argc, ud->argv, ud->node, 0);
#endif
    return Qnil;
}

static VALUE
performRuby_rescue(VALUE arg)
{
//    if (arg != 0) {
//	ruby_current_thread = (rb_thread_t *)arg;
//    }
    rb_raise_ruby_exc_in_objc(rb_errinfo());
    return Qnil; /* not reached */
}

- (id)performRubySelector:(SEL)sel withArguments:(id *)argv count:(int)argc
{
//    const bool need_protection = GET_THREAD()->thread_id != pthread_self();
    NODE *node;
    IMP imp;
    VALUE *rargs, ret;
    ID mid;

    imp = NULL;
    node = rb_objc_method_node2(*(VALUE *)self, sel, &imp);
    if (node == NULL) {
	if (imp != NULL) {
	    [NSException raise:NSInvalidArgumentException format:
		@"-[%@ %s] is not a pure Ruby method", self, (char *)sel];
	}
	else {
	    [NSException raise:NSInvalidArgumentException format:
		@"receiver %@ does not respond to %s", (char *)sel];
	}
    }

    if (argc == 0) {
	rargs = NULL;
    }
    else {
	int i;
	rargs = (VALUE *)alloca(sizeof(VALUE) * argc);
	for (i = 0; i < argc; i++) {
	    rargs[i] = OC2RB(argv[i]);
	}
    }
   
#if 0
    rb_thread_t *th, *old = NULL;

    if (need_protection) {
	th = rb_thread_wrap_existing_native_thread(pthread_self());
	native_mutex_lock(&GET_THREAD()->vm->global_interpreter_lock);
	old = ruby_current_thread;
	ruby_current_thread = th;
    }
#endif

    mid = rb_intern((char *)sel);

    struct performRuby_context ud;
    ud.rcv = (VALUE)self;
    ud.mid = mid;
    ud.argc = argc;
    ud.argv = rargs;
    ud.node = node->nd_body;

    ret = rb_rescue2(performRuby_safe, (VALUE)&ud,
                     //performRuby_rescue, (VALUE)old,
                     performRuby_rescue, Qnil,
                     rb_eException, (VALUE)0);
 
#if 0
    if (need_protection) {
	native_mutex_unlock(&GET_THREAD()->vm->global_interpreter_lock);
	ruby_current_thread = old;
    }
#endif

    return RB2OC(ret);
}

- (id)performRubySelector:(SEL)sel withArguments:firstArg, ...
{
    va_list args;
    int argc;
    id *argv;

    if (firstArg != nil) {
	int i;

        argc = 1;
        va_start(args, firstArg);
        while (va_arg(args, id)) {
	    argc++;
	}
        va_end(args);
	argv = alloca(sizeof(id) * argc);
        va_start(args, firstArg);
	argv[0] = firstArg;
        for (i = 1; i < argc; i++) {
            argv[i] = va_arg(args, id);
	}
        va_end(args);
    }
    else {
	argc = 0;
	argv = NULL;
    }

    return [self performRubySelector:sel withArguments:argv count:argc];
}

@end
