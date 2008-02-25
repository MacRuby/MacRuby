#include "ruby/ruby.h"
#include "ruby/node.h"
#include "ruby/encoding.h"
#include <unistd.h>
#include <dlfcn.h>
#include <Foundation/Foundation.h>
#include <BridgeSupport/BridgeSupport.h>

typedef struct {
    bs_element_type_t type;
    void *value;
    VALUE klass;
    ffi_type *ffi_type;
} bs_element_boxed_t;

static VALUE rb_cBoxed;
static ID rb_ivar_type;

static VALUE bs_const_magic_cookie = Qnil;

static struct st_table *bs_constants;
static struct st_table *bs_functions;
static struct st_table *bs_function_syms;
static struct st_table *bs_boxeds;

static char *
rb_objc_sel_to_mid(SEL selector, char *buffer, unsigned buffer_len)
{
    size_t s;
    char *p;

    s = strlcpy(buffer, (const char *)selector, buffer_len);

    p = buffer + s - 1;
    if (*p == ':')
	*p = '\0';

    p = buffer;
    while ((p = strchr(p, ':')) != NULL) {
	*p = '_';
	p++;
    }

    return buffer;
}

static inline const char *
rb_objc_skip_octype_modifiers(const char *octype)
{
    if (*octype == _C_CONST)
	octype++;
    return octype;
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
	    if (nested == 0)
		return type;
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

    if (st_lookup(ary_ffi_types, (st_data_t)size, (st_data_t *)&type))
	return type;

    type = (ffi_type *)malloc(sizeof(ffi_type));

    type->size = size;
    type->alignment = align;
    type->type = FFI_TYPE_STRUCT;
    type->elements = malloc(size * sizeof(ffi_type *));
  
    for (i = 0; i < size; i++)
	type->elements[i] = &ffi_type_uchar;

    st_insert(ary_ffi_types, (st_data_t)size, (st_data_t)type);

    return type;
}

static size_t
get_ffi_struct_size(ffi_type *type)
{
    unsigned i;
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
#if __LP64__
	    unsigned long size, align;
#else
	    unsigned int size, align;
#endif

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
	     	    (bs_struct->fields_count) * sizeof(ffi_type *));

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

    rb_bug("Unrecognized octype `%s'", octype);

    return NULL;
}

#if 0
static bool
rb_primitive_obj_to_ocid(VALUE rval, id *ocval)
{
    if (TYPE(rval) == T_STRING) {
	CFStringRef string;
	CFStringEncoding cf_encoding;	
	rb_encoding *enc;

	enc = rb_enc_get(rval);
	if (enc == NULL) {
	    cf_encoding = kCFStringEncodingASCII;
	}
	else {
	    /* TODO: support more encodings! */
	    cf_encoding = kCFStringEncodingASCII;
	}

	string = CFStringCreateWithCStringNoCopy(
	    NULL, RSTRING_PTR(rval), cf_encoding, kCFAllocatorNull);

	*ocval = NSMakeCollectable(string);
	return true;
    }

    rb_bug("cannot convert primitive obj `%s' to Objective-C",
	   RSTRING_PTR(rb_inspect(rval)));
}
#endif

static bool
rb_objc_rval_to_ocid(VALUE rval, void **ocval)
{
    if (!rb_special_const_p(rval) && rb_objc_is_non_native(rval)) {
	*(id *)ocval = (id)rval;
	return true;
    }

    switch (TYPE(rval)) {
	case T_STRING:
	case T_ARRAY:
	case T_HASH:
	case T_OBJECT:
	    *(id *)ocval = (id)rval;
	    return true;

	case T_CLASS:
	case T_MODULE:
	    *(id *)ocval = (id)RCLASS_OCID(rval);
	    return true;

	case T_NIL:
	    *(id *)ocval = NULL;
	    return true;

	case T_TRUE:
	case T_FALSE:
	case T_FLOAT:
	case T_FIXNUM:
	case T_BIGNUM:
	    /* TODO */
	    break;
    }

    return false;
}

static bool
rb_objc_rval_to_ocsel(VALUE rval, void **ocval)
{
    const char *cstr;

    switch (TYPE(rval)) {
	case T_STRING:
	    cstr = StringValuePtr(rval);
	    break;

	case T_SYMBOL:
	    cstr = rb_id2name(SYM2ID(rval));
	    break;

	default:
	    return false;
    }

    *(SEL *)ocval = sel_registerName(cstr);
    return true;
}

static void *
bs_element_boxed_get_data(bs_element_boxed_t *bs_boxed, VALUE rval,
			  bool *success)
{
    void *data;

    assert(bs_boxed->ffi_type != NULL);

    if (NIL_P(rval) && bs_boxed->ffi_type == &ffi_type_pointer) {
	*success = true;
	return NULL;
    }

    if (rb_obj_is_kind_of(rval, rb_cBoxed) == Qfalse) {
	*success = false;
	return NULL;
    } 

    if (bs_boxed->type == BS_ELEMENT_STRUCT) {
	bs_element_struct_t *bs_struct;
	unsigned i;

	bs_struct = (bs_element_struct_t *)bs_boxed->value;

	/* Resync the ivars if necessary.
	 * This is required as a field may nest another structure, which
	 * could have been modified as a copy in the Ruby world.
	 */
	for (i = 0; i < bs_struct->fields_count; i++) {
	    char buf[128];
	    ID ivar_id;

	    snprintf(buf, sizeof buf, "@%s", bs_struct->fields[i].name);
	    ivar_id = rb_intern(buf);
	    if (rb_ivar_defined(rval, ivar_id) == Qtrue) {
		VALUE val;

		val = rb_ivar_get(rval, ivar_id);
		snprintf(buf, sizeof buf, "%s=", bs_struct->fields[i].name);
		rb_funcall(rval, rb_intern(buf), 1, val);

		//if (clean_ivars)
		  //  rb_rval_remove_instance_variable(rval, ID2SYM(ivar_id));
	    }
	}
    }

    Data_Get_Struct(rval, void, data);
    *success = true;		

    return data;
}

static VALUE
rb_bs_boxed_new_from_ocdata(bs_element_boxed_t *bs_boxed, void *ocval)
{
    void *data;

    if (ocval == NULL)
	return Qnil;

    if (bs_boxed->type == BS_ELEMENT_OPAQUE && *(void **)ocval == NULL)
	return Qnil;

    assert(bs_boxed->ffi_type != NULL);

    data = xmalloc(bs_boxed->ffi_type->size);
    memcpy(data, ocval, bs_boxed->ffi_type->size);

    return Data_Wrap_Struct(bs_boxed->klass, NULL, NULL, data);     
}

static void
rb_objc_rval_to_ocval(VALUE rval, const char *octype, void **ocval)
{
    bool ok;

    octype = rb_objc_skip_octype_modifiers(octype);

    {
	bs_element_boxed_t *bs_boxed;
	if (st_lookup(bs_boxeds, (st_data_t)octype, 
		      (st_data_t *)&bs_boxed)) {
	    void *data = bs_element_boxed_get_data(bs_boxed, rval, &ok);
	    if (ok) {
		if (data == NULL)
		    *(void **)ocval = NULL;
		else
		    memcpy(ocval, data, bs_boxed->ffi_type->size);
	    }
	    goto bails; 
	}
    }

    if (*octype != _C_BOOL) {
	if (TYPE(rval) == T_TRUE)
	    rval = INT2FIX(1);
	else if (TYPE(rval) == T_FALSE)
	    rval = INT2FIX(0);
    }

    ok = true;
    switch (*octype) {
	case _C_ID:
	case _C_CLASS:
	    ok = rb_objc_rval_to_ocid(rval, ocval);
	    break;

	case _C_SEL:
	    ok = rb_objc_rval_to_ocsel(rval, ocval);
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
	    *(char *)ocval = (char) NUM2INT(rb_Integer(rval));
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

VALUE
rb_objc_boot_ocid(id ocid)
{
    if (rb_objc_is_non_native((VALUE)ocid)) {
        /* Make sure the ObjC class is imported in Ruby. */ 
        rb_objc_import_class(object_getClass(ocid)); 
    }
    else if (RBASIC(ocid)->klass == 0) {
	/* This pure-Ruby object was created from Objective-C, we need to 
	 * initialize the Ruby bits. 
	 */
	VALUE klass;
        
	klass = rb_objc_import_class(object_getClass(ocid)); 

	RBASIC(ocid)->klass = klass;
	RBASIC(ocid)->flags = 
	    klass == rb_cString
	    ? T_STRING
	    : klass == rb_cArray
	    ? T_ARRAY
	    : klass == rb_cHash
	    ? T_HASH
	    : T_OBJECT;
    }

    return (VALUE)ocid;
}

static bool 
rb_objc_ocid_to_rval(void **ocval, VALUE *rbval)
{
    id ocid = *(id *)ocval;

    if (ocid == NULL) {
	*rbval = Qnil;
    }
    else {
	*rbval = rb_objc_boot_ocid(ocid);
    }

    return true;
}

static void
rb_objc_ocval_to_rbval(void **ocval, const char *octype, VALUE *rbval)
{
    bool ok;

    octype = rb_objc_skip_octype_modifiers(octype);
    ok = true;
    
    {
	bs_element_boxed_t *bs_boxed;
	if (st_lookup(bs_boxeds, (st_data_t)octype, 
		      (st_data_t *)&bs_boxed)) {
	    *rbval = rb_bs_boxed_new_from_ocdata(bs_boxed, ocval);
	    goto bails; 
	}
    }
    
    switch (*octype) {
	case _C_ID:
	    ok = rb_objc_ocid_to_rval(ocval, rbval);
	    break;
	
	case _C_CLASS:
	    *rbval = rb_objc_import_class(*(Class *)ocval);
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

	case _C_FLT:
	    *rbval = rb_float_new((double)(*(float *)ocval));
	    break;

	case _C_DBL:
	    *rbval = rb_float_new(*(double *)ocval);
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

static void
rb_objc_to_ruby_closure_handler(ffi_cif *cif, void *resp, void **args,
				void *userdata)
{
    Method method = (Method)userdata;
    VALUE rcv, argv;
    unsigned i, count;
    ffi_type *ffi_rettype, **ffi_argtypes;
    void *ffi_ret, **ffi_args;
    SEL selector;
    char type[128];
    id ocrcv;
    void *imp;

    rcv = (*(VALUE **)args)[0];
    argv = (*(VALUE **)args)[1];

    if (TYPE(argv) != T_ARRAY)
	rb_bug("argv isn't an array");
	
    count = method_getNumberOfArguments(method);
    assert(count >= 2);

    if (RARRAY_LEN(argv) != count - 2)
	rb_raise(rb_eArgError, "wrong number of arguments (%d for %d)",
		 RARRAY_LEN(argv), count - 2);

    selector = method_getName(method);

    imp = method_getImplementation(method);

    ffi_argtypes = (ffi_type **)alloca(sizeof(ffi_type *) * count + 1);
    ffi_argtypes[0] = &ffi_type_pointer;
    ffi_argtypes[1] = &ffi_type_pointer;
    ffi_args = (void **)alloca(sizeof(void *) * count + 1);
    rb_objc_rval_to_ocid(rcv, (void **)&ocrcv);
    ffi_args[0] = &ocrcv;
    ffi_args[1] = &selector;

    for (i = 0; i < RARRAY_LEN(argv); i++) {
	method_getArgumentType(method, i + 2, type, sizeof type);
	ffi_argtypes[i + 2] = rb_objc_octype_to_ffitype(type);
	assert(ffi_argtypes[i + 2]->size > 0);
	ffi_args[i + 2] = (void *)alloca(ffi_argtypes[i + 2]->size);
	rb_objc_rval_to_ocval(RARRAY_PTR(argv)[i], type, ffi_args[i + 2]);
    }

    ffi_argtypes[count] = NULL;
    ffi_args[count] = NULL;

    method_getReturnType(method, type, sizeof type);
    ffi_rettype = rb_objc_octype_to_ffitype(type);

    cif = (ffi_cif *)alloca(sizeof(ffi_cif));
    if (ffi_prep_cif(cif, FFI_DEFAULT_ABI, count, ffi_rettype, ffi_argtypes) 
	!= FFI_OK)
	rb_fatal("can't prepare cif for objc method type `%s'",
		 method_getTypeEncoding(method));

    if (ffi_rettype != &ffi_type_void) {
	ffi_ret = (void *)alloca(ffi_rettype->size);
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
	rb_objc_ocval_to_rbval(ffi_ret, type, (VALUE *)resp);
    }
    else {
	*(VALUE *)resp = Qnil;
    }
}

static void *
rb_objc_to_ruby_closure(Method method)
{
    static ffi_cif *cif = NULL;
    ffi_closure *closure;

    if (cif == NULL) {
	static ffi_type *args[3];
    
	cif = (ffi_cif *)malloc(sizeof(ffi_cif)); /*ALLOC(ffi_cif);*/
    
	args[0] = &ffi_type_pointer;
	args[1] = &ffi_type_pointer;
	args[2] = NULL;
    
	if (ffi_prep_cif(cif, FFI_DEFAULT_ABI, 2, &ffi_type_pointer, args)
	    != FFI_OK)
	    rb_fatal("can't prepare objc to ruby cif");
    }

    closure = (ffi_closure *)malloc(sizeof(ffi_closure)); /*ALLOC(ffi_closure);*/

    if (ffi_prep_closure(closure, cif, rb_objc_to_ruby_closure_handler, method)
	!= FFI_OK)
	rb_fatal("can't prepare ruby to objc closure");

    return closure;
}

#define IGNORE_PRIVATE_OBJC_METHODS 1

static void
rb_ruby_to_objc_closure_handler(ffi_cif *cif, void *resp, void **args,
				void *userdata)
{
    void *rcv;
    SEL sel;
    ID mid;
    VALUE rrcv, rargs, ret;
    unsigned i;

    rcv = (*(id **)args)[0];
    sel = (*(SEL **)args)[1];

    mid = rb_intern((const char *)sel);

    rargs = rb_ary_new();
    for (i = 2; i < cif->nargs; i++) {
	/* TODO: get the right type */
	VALUE val;
	rb_objc_ocid_to_rval(args[i], &val);
	rb_ary_push(rargs, val);
    }

    rb_objc_ocid_to_rval(&rcv, &rrcv);

    ret = rb_apply(rrcv, mid, rargs);

    rb_objc_rval_to_ocid(ret, resp);
}

static void *
rb_ruby_to_objc_closure(const char *octype, const unsigned arity)
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

    args = (ffi_type **)malloc(sizeof(ffi_type *) * (arity + 2)); /*ALLOC_N(ffi_type*, arity + 2);*/
    i = 0;
    while ((p = rb_objc_get_first_type(p, buf, sizeof buf)) != NULL) {
	args[i] = rb_objc_octype_to_ffitype(buf);
	assert(++i <= arity + 2);
    }

    cif = (ffi_cif *)malloc(sizeof(ffi_cif)); /*ALLOC(ffi_cif);*/
    if (ffi_prep_cif(cif, FFI_DEFAULT_ABI, arity + 2, ret, args) != FFI_OK)
	rb_fatal("can't prepare ruby to objc cif");
    
    closure = (ffi_closure *)malloc(sizeof(ffi_closure)); /*ALLOC(ffi_closure);*/
    if (ffi_prep_closure(closure, cif, rb_ruby_to_objc_closure_handler, NULL)
	!= FFI_OK)
	rb_fatal("can't prepare ruby to objc closure");

    return closure;
}

void
rb_objc_sync_ruby_method(VALUE mod, ID mid, NODE *node, unsigned override)
{
    SEL sel;
    Class ocklass;
    Method method;
    char *types;
    int arity;
    char *mid_str;
    IMP imp;
    bool direct_override;

    /* Do not expose C functions. */
    if (bs_functions != NULL
	&& mod == CLASS_OF(rb_mKernel)
	&& st_lookup(bs_functions, (st_data_t)mid, NULL))
	return;

    arity = rb_node_arity(node);
    if (arity < 0)
	arity = 0; /* TODO: support negative arity */
 
    mid_str = (char *)rb_id2name(mid);

    if (arity == 1 && mid_str[strlen(mid_str) - 1] != ':') {
	char buf[100];
	snprintf(buf, sizeof buf, "%s:", mid_str);
	sel = sel_registerName(buf);
    }
    else {
	sel = sel_registerName(mid_str);
    }

    ocklass = RCLASS_OCID(mod);
    direct_override = false;
    method = class_getInstanceMethod(ocklass, sel);

    if (method != NULL) {
	void *klass = RCLASS_OCID(rb_cBasicObject);
	if (!override || class_getInstanceMethod(klass, sel) == method) 
	    return;
	if (arity + 2 != method_getNumberOfArguments(method)) {
	    rb_warning("cannot override Objective-C method `%s' in " \
		       "class `%s' because of an arity mismatch (%d for %d)", 
		       (char *)sel, 
		       class_getName(ocklass), 
		       arity + 2, 
		       method_getNumberOfArguments(method));
	    return;
	}
	types = (char *)method_getTypeEncoding(method);
	klass = class_getSuperclass(ocklass);
	direct_override = 
	    klass == NULL || class_getInstanceMethod(klass, sel) != method;
    }
    else {
	types = (char *)alloca((arity + 4) * sizeof(char));
	types[0] = '@';
	types[1] = '@';
	types[2] = ':';
	memset(&types[3], '@', arity);
	types[arity + 3] = '\0';
    }

//    printf("registering sel %s of types %s arity %d to class %s\n",
//	   (char *)sel, types, arity, class_getName(ocklass));

    imp = rb_ruby_to_objc_closure(types, arity);

    if (method != NULL && direct_override) {
	method_setImplementation(method, imp);
    }
    else {
	assert(class_addMethod(ocklass, sel, imp, types));	
    }
}

#if 0
static int
__rb_objc_add_ruby_method(ID mid, NODE *body, VALUE mod)
{
    if (mid == ID_ALLOCATOR)
	return ST_CONTINUE;
    
    if (body == NULL || body->nd_body->nd_body == NULL)
	return ST_CONTINUE;

    if (VISI(body->nd_body->nd_noex) != NOEX_PUBLIC)
	return ST_CONTINUE;

    rb_objc_sync_ruby_method(mod, mid, body->nd_body->nd_body, 0);

    return ST_CONTINUE;
}
#endif

static inline unsigned
is_ignored_selector(SEL sel)
{
#if defined(__ppc__)
    return sel == (SEL)0xfffef000;
#elif defined(__i386__)
    return sel == (SEL)0xfffeb010;
#else
# error Unsupported arch
#endif
}

#if 0
static void
__rb_objc_sync_methods(VALUE mod, Class ocklass)
{
    Method *methods;
    unsigned int i, count;
    char buffer[128];
    VALUE imod;

    methods = class_copyMethodList(ocklass, &count);

    imod = mod;
#if 0
    for (;;) {
	st_foreach(RCLASS_M_TBL(imod), __rb_objc_add_ruby_method, 
		   (st_data_t)mod);
	imod = RCLASS_SUPER(imod);
	if (imod == 0 || BUILTIN_TYPE(imod) != T_ICLASS)
	    break;
    }
#endif

    for (i = 0; i < count; i++) {
	SEL sel;
	ID mid;
	st_data_t data;
	NODE *node;

	sel = method_getName(methods[i]);
	if (is_ignored_selector(sel))
	    continue;
#if IGNORE_PRIVATE_OBJC_METHODS
	if (*(char *)sel == '_')
	    continue;
#endif

	rb_objc_sel_to_mid(sel, buffer, sizeof buffer);
	mid = rb_intern(buffer);

	if (rb_method_boundp(mod, mid, 1) == Qtrue)
	    continue;

	node = NEW_CFUNC(rb_objc_to_ruby_closure(methods[i]), -2); 
	data = (st_data_t)NEW_FBODY(NEW_METHOD(node, mod, 
		    			       NOEX_WITH_SAFE(NOEX_PUBLIC)), 0);

	st_insert(RCLASS_M_TBL(mod), mid, data);
    }

    free(methods);
}
#endif

NODE *
rb_objc_define_objc_mid_closure(VALUE recv, ID mid)
{
    SEL sel;
    Class ocklass;
    Method method;
    VALUE mod;
    NODE *node;
    NODE *data;
    Method (*getMethod)(Class, SEL);

    assert(mid > 1);

    sel = sel_registerName(rb_id2name(mid));

    if (TYPE(recv) == T_CLASS) {
	mod = recv;
	getMethod = class_getClassMethod;
    }
    else {
	mod = CLASS_OF(recv);
	getMethod = class_getInstanceMethod;
    }

    ocklass = RCLASS_OCID(mod);
    method = (*getMethod)(ocklass, sel);
    if (method == NULL || method_getImplementation(method) == NULL)
	return NULL;	/* recv doesn't respond to this selector */

    do {
	Class ocsuper = class_getSuperclass(ocklass);
	if ((*getMethod)(ocsuper, sel) == NULL)
	    break;
	ocklass = ocsuper;
    }
    while (1);

    if (RCLASS(mod)->ocklass != ocklass) {
	mod = rb_objc_import_class(ocklass);
	if (TYPE(recv) == T_CLASS)
	    mod = CLASS_OF(mod);
    }

    /* Already defined. */
    node = rb_method_node(mod, mid);
    if (node != NULL)
	return node;

//printf("defining %s#%s\n", rb_class2name(mod), (char *)sel);

    node = NEW_CFUNC(rb_objc_to_ruby_closure(method), -2); 
    data = NEW_FBODY(NEW_METHOD(node, mod, 
				NOEX_WITH_SAFE(NOEX_PUBLIC)), 0);

    rb_add_method_direct(mod, mid, data);

    return data->nd_body;
}

#if 0
rb_objc_sync_objc_methods_into(VALUE mod, Class ocklass)
{
    /* Load instance methods */
    __rb_objc_sync_methods(mod, ocklass);

    /* Load class methods */
    __rb_objc_sync_methods(rb_singleton_class(mod), 
			   object_getClass((id)ocklass));
}

void
rb_objc_sync_objc_methods(VALUE mod)
{
    rb_objc_sync_objc_methods_into(mod, RCLASS_OCID(mod));
}
#endif

VALUE
rb_mod_objc_ancestors(VALUE recv)
{
    void *klass;
    VALUE ary;

    ary = rb_ary_new();

    for (klass = RCLASS(recv)->ocklass; klass != NULL; 
	 klass = class_getSuperclass(klass)) {
	rb_ary_push(ary, rb_str_new2(class_getName(klass)));		
    }

    return ary;
}

void 
rb_objc_methods(VALUE ary, Class ocklass)
{
    while (ocklass != NULL) {
	unsigned i, count;
	Method *methods;

 	methods = class_copyMethodList(ocklass, &count);
 	if (methods != NULL) { 
	    for (i = 0; i < count; i++) {
		SEL sel = method_getName(methods[i]);
		if (is_ignored_selector(sel))
		    continue;
		rb_ary_push(ary, ID2SYM(rb_intern(sel_getName(sel))));
	    }
	    free(methods);
    	}
	ocklass = class_getSuperclass(ocklass);
    }

    rb_funcall(ary, rb_intern("uniq!"), 0);
}

static VALUE
bs_function_dispatch(int argc, VALUE *argv, VALUE recv)
{
    ID callee;
    bs_element_function_t *bs_func;
    void *sym;
    unsigned i;
    ffi_type *ffi_rettype, **ffi_argtypes;
    void *ffi_ret, **ffi_args;
    ffi_cif *cif;
    VALUE resp;

    callee = rb_frame_this_func();
    assert(callee > 1);
    if (!st_lookup(bs_functions, (st_data_t)callee, (st_data_t *)&bs_func))
	rb_bug("bridgesupport function `%s' not in cache", rb_id2name(callee));

    if (!st_lookup(bs_function_syms, (st_data_t)callee, (st_data_t *)&sym)
	|| sym == NULL) {
	sym = dlsym(RTLD_DEFAULT, bs_func->name);
	if (sym == NULL)
	    rb_bug("cannot locate symbol for bridgesupport function `%s'",
		   bs_func->name);
	st_insert(bs_function_syms, (st_data_t)callee, (st_data_t)sym);
    }

    if (argc != bs_func->args_count)
	rb_raise(rb_eArgError, "wrong number of arguments (%d for %d)",
		 argc, bs_func->args_count);

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
    if (ffi_rettype != &ffi_type_void)
	rb_objc_ocval_to_rbval(ffi_ret, bs_func->retval->type, &resp);

    return resp;
}

VALUE
rb_objc_resolve_const_value(VALUE v, VALUE klass, ID id)
{
    void *sym;
    bs_element_constant_t *bs_const;

    if (v != bs_const_magic_cookie)
	return v;

    if (!st_lookup(bs_constants, (st_data_t)id, (st_data_t *)&bs_const))
	rb_bug("unresolved bridgesupport constant `%s' not in cache",
	       rb_id2name(id));

    sym = dlsym(RTLD_DEFAULT, bs_const->name);
    if (sym == NULL)
	rb_bug("cannot locate symbol for unresolved bridgesupport " \
	       "constant `%s'", bs_const->name);

    rb_objc_ocval_to_rbval(sym, bs_const->type, &v);

// FIXME
//    assert(RCLASS_IV_TBL(klass) != NULL);
//    assert(st_delete(RCLASS_IV_TBL(klass), (st_data_t*)&id, NULL));

    rb_const_set(klass, id, v); 

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
	/* Make sure the ffi_type is ready for use. */
	if (bs_boxed->type == BS_ELEMENT_STRUCT 
	    && bs_boxed->ffi_type == NULL) {
	    rb_objc_octype_to_ffitype(
		((bs_element_struct_t *)bs_boxed->value)->type);
	    assert(bs_boxed->ffi_type != NULL);
	}
	return bs_boxed;
    }
    return NULL;
}

static VALUE
rb_bs_struct_new(int argc, VALUE *argv, VALUE recv)
{
    bs_element_boxed_t *bs_boxed = rb_klass_get_bs_boxed(recv);
    bs_element_struct_t *bs_struct = (bs_element_struct_t *)bs_boxed->value;    
    void *data;
    unsigned i;
    size_t pos;

    if (argc > 0 && argc != bs_struct->fields_count)
	rb_raise(rb_eArgError, "wrong number of arguments (%d for %d)",
		 argc, bs_struct->fields_count);

    data = (void *)xmalloc(bs_boxed->ffi_type->size);
    memset(data, 0, bs_boxed->ffi_type->size);

    for (i = 0, pos = 0; i < argc; i++) {
	bs_element_struct_field_t *bs_field = 
	    (bs_element_struct_field_t *)&bs_struct->fields[i];

	rb_objc_rval_to_ocval(argv[i], bs_field->type, data + pos);	
    
        pos += rb_objc_octype_to_ffitype(bs_field->type)->size;
    }

    return Data_Wrap_Struct(recv, NULL, NULL, data);
}

static ID
rb_bs_struct_field_ivar_id(void)
{
    char ivar_name[128];
    int len;

    len = snprintf(ivar_name, sizeof ivar_name, "@%s", 
		   rb_id2name(rb_frame_this_func()));
    if (ivar_name[len - 1] == '=')
	ivar_name[len - 1] = '\0';

    return rb_intern(ivar_name);
}

static void *
rb_bs_struct_get_field_data(bs_element_struct_t *bs_struct, VALUE recv,
			    ID ivar_id, char **octype)
{
    unsigned i;
    const char *ivar_id_str;
    void *data;
    size_t pos;

    /* FIXME we should cache the ivar IDs somewhere in the 
     * bs_element_struct_fields 
     */

    ivar_id_str = rb_id2name(ivar_id);
    ivar_id_str++; /* skip first '@' */

    Data_Get_Struct(recv, void, data);
    assert(data != NULL);

    for (i = 0, pos = 0; i < bs_struct->fields_count; i++) {
	bs_element_struct_field_t *bs_field =
	    (bs_element_struct_field_t *)&bs_struct->fields[i];
	if (strcmp(ivar_id_str, bs_field->name) == 0) {
	    *octype = bs_field->type;
	    return data + pos;   
	}
        pos += rb_objc_octype_to_ffitype(bs_field->type)->size;
    }

    rb_bug("can't find field `%s' in recv `%s'", ivar_id_str,
	   RSTRING_PTR(rb_inspect(recv)));
}

static VALUE
rb_bs_struct_get(VALUE recv)
{
    bs_element_boxed_t *bs_boxed = rb_klass_get_bs_boxed(CLASS_OF(recv));
    bs_element_struct_t *bs_struct = (bs_element_struct_t *)bs_boxed->value;    
    ID ivar_id;
    VALUE result;

    ivar_id = rb_bs_struct_field_ivar_id();

    if (rb_ivar_defined(recv, ivar_id) == Qfalse) {
	void *data;
	char *octype;
	BOOL ok;

	data = rb_bs_struct_get_field_data(bs_struct, recv, ivar_id, &octype);
	rb_objc_ocval_to_rbval(data, octype, &result);
	rb_ivar_set(recv, ivar_id, result);
    }
    else {
	result = rb_ivar_get(recv, ivar_id);
    }
   
     return result;
}

static VALUE
rb_bs_struct_set(VALUE recv, VALUE value)
{
    bs_element_boxed_t *bs_boxed = rb_klass_get_bs_boxed(CLASS_OF(recv));
    bs_element_struct_t *bs_struct = (bs_element_struct_t *)bs_boxed->value;    
    ID ivar_id;
    void *data;
    char *octype;

    ivar_id = rb_bs_struct_field_ivar_id();
    data = rb_bs_struct_get_field_data(bs_struct, recv, ivar_id, &octype);
    rb_objc_rval_to_ocval(value, octype, data);
    rb_ivar_set(recv, ivar_id, value);

    return value;    
}

static VALUE
rb_bs_struct_to_a(VALUE recv)
{
    bs_element_boxed_t *bs_boxed = rb_klass_get_bs_boxed(CLASS_OF(recv));
    bs_element_struct_t *bs_struct = (bs_element_struct_t *)bs_boxed->value;    
    VALUE ary;
    unsigned i;

    ary = rb_ary_new();

    for (i = 0; i < bs_struct->fields_count; i++) {
	VALUE obj;

	obj = rb_funcall(recv, rb_intern(bs_struct->fields[i].name), 0, NULL);
	rb_ary_push(ary, obj);
    }

    return ary;
}

static VALUE
rb_bs_struct_is_equal(VALUE recv, VALUE other)
{
    unsigned i;
    bs_element_boxed_t *bs_boxed;  
    bool ok;
    void *d1, *d2; 

    if (recv == other)
	return Qtrue;

    if (rb_obj_is_kind_of(other, rb_cBoxed) == Qfalse)
	return Qfalse;

    bs_boxed = rb_klass_get_bs_boxed(CLASS_OF(recv));
    if (bs_boxed != rb_klass_get_bs_boxed(CLASS_OF(other)))
	return Qfalse;

    d1 = bs_element_boxed_get_data(bs_boxed, recv, &ok);
    if (!ok)
	rb_raise(rb_eRuntimeError, "can't retrieve data for boxed `%s'",
		 RSTRING_PTR(rb_inspect(recv)));

    d2 = bs_element_boxed_get_data(bs_boxed, other, &ok);
    if (!ok)
	rb_raise(rb_eRuntimeError, "can't retrieve data for boxed `%s'",
		 RSTRING_PTR(rb_inspect(recv)));

    if (d1 == d2)
	return Qtrue;
    else if (d1 == NULL || d2 == NULL)
	return Qfalse;

    return memcmp(d1, d2, bs_boxed->ffi_type->size) == 0 ? Qtrue : Qfalse;
}

static VALUE
rb_bs_struct_dup(VALUE recv)
{
    bs_element_boxed_t *bs_boxed = rb_klass_get_bs_boxed(CLASS_OF(recv));
    void *data, *newdata;
    bool ok;

    data = bs_element_boxed_get_data(bs_boxed, recv, &ok);
    if (!ok)
	rb_raise(rb_eRuntimeError, "can't retrieve data for boxed `%s'",
		 RSTRING_PTR(rb_inspect(recv)));

    if (data == NULL)
	return Qnil;

    newdata = xmalloc(sizeof(bs_boxed->ffi_type->size));
    memcpy(newdata, data, bs_boxed->ffi_type->size);

    return rb_bs_boxed_new_from_ocdata(bs_boxed, newdata);
}

static VALUE
rb_bs_struct_inspect(VALUE recv)
{
    bs_element_boxed_t *bs_boxed = rb_klass_get_bs_boxed(CLASS_OF(recv));
    bs_element_struct_t *bs_struct = (bs_element_struct_t *)bs_boxed->value;    
    VALUE ary;
    unsigned i;
    VALUE str;

    str = rb_str_new2("#<");
    rb_str_cat2(str, rb_obj_classname(recv));

    for (i = 0; i < bs_struct->fields_count; i++) {
	VALUE obj;

	obj = rb_funcall(recv, rb_intern(bs_struct->fields[i].name), 0, NULL);
	rb_str_cat2(str, " ");
	rb_str_cat2(str, bs_struct->fields[i].name);
	rb_str_cat2(str, "=");
	rb_str_append(str, rb_inspect(obj));
    }

    rb_str_cat2(str, ">");

    return str;
}

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
	char buf[128];
	int i;

	/* Needs to be lazily created, because the type of some fields
	 * may not be registered yet.
	 */
        bs_ffi_type = NULL; 

	if (!bs_struct->opaque) {
	    for (i = 0; i < bs_struct->fields_count; i++) {
		bs_element_struct_field_t *field = &bs_struct->fields[i];
		rb_define_method(klass, field->name, rb_bs_struct_get, 0);
		strlcpy(buf, field->name, sizeof buf);
		strlcat(buf, "=", sizeof buf);
		rb_define_method(klass, buf, rb_bs_struct_set, 1);
	    }
	    rb_define_method(klass, "to_a", rb_bs_struct_to_a, 0);
	}

	rb_define_singleton_method(klass, "new", rb_bs_struct_new, -1);
	rb_define_method(klass, "==", rb_bs_struct_is_equal, 1);
	rb_define_method(klass, "dup", rb_bs_struct_dup, 0);
	rb_define_alias(klass, "clone", "dup");	
	rb_define_method(klass, "inspect", rb_bs_struct_inspect, 0);
    }
    else {
	bs_ffi_type = &ffi_type_pointer;
    }

    bs_boxed = (bs_element_boxed_t *)malloc(sizeof(bs_element_boxed_t));
    bs_boxed->type = type;
    bs_boxed->value = value; 
    bs_boxed->klass = klass;
    bs_boxed->ffi_type = bs_ffi_type;

    st_insert(bs_boxeds, (st_data_t)p->type, (st_data_t)bs_boxed);
}

static inline ID
generate_const_name(char *name)
{
    ID id;
    if (islower(name[0])) {
	name[0] = toupper(name[0]);
	id = rb_intern(name);
	name[0] = tolower(name[0]);
	return id;
    }
    else {
	return rb_intern(name);
    }
}

static void
bs_parse_cb(const char *path, bs_element_type_t type, void *value, void *ctx)
{
    bool do_not_free = false;
    switch (type) {
	case BS_ELEMENT_ENUM:
	{
	    bs_element_enum_t *bs_enum = (bs_element_enum_t *)value;
	    ID name = generate_const_name(bs_enum->name);
	    if (!rb_const_defined(rb_cObject, name)) {
		VALUE val = strchr(bs_enum->value, '.') != NULL
		    ? rb_float_new(rb_cstr_to_dbl(bs_enum->value, 1))
		    : rb_cstr_to_inum(bs_enum->value, 10, 1);
		rb_const_set(rb_cObject, name, val); 
	    }
	    else {
		rb_warning("bs: enum `%s' already defined", rb_id2name(name));
	    }
	    break;
	}

	case BS_ELEMENT_CONSTANT:
	{
	    bs_element_constant_t *bs_const = (bs_element_constant_t *)value;
	    ID name = generate_const_name(bs_const->name);
	    if (!rb_const_defined(rb_cObject, name)) {	
		st_insert(bs_constants, (st_data_t)name, (st_data_t)bs_const);
		rb_const_set(rb_cObject, name, bs_const_magic_cookie); 
		do_not_free = true;
	    }
	    else {
		rb_warning("bs: constant `%s' already defined", 
			   rb_id2name(name));
	    }
	    break;
	}

	case BS_ELEMENT_STRING_CONSTANT:
	{
	    bs_element_string_constant_t *bs_strconst = 
		(bs_element_string_constant_t *)value;
	    ID name = generate_const_name(bs_strconst->name);
	    if (!rb_const_defined(rb_cObject, name)) {	
		VALUE val;
	    	if (bs_strconst->nsstring) {
		    CFStringRef string;
		    string = CFStringCreateWithCString(
			NULL, bs_strconst->value, kCFStringEncodingUTF8);
		    val = (VALUE)string;
	    	}
	    	else {
		    val = rb_str_new2(bs_strconst->value);
	    	}
		rb_const_set(rb_cObject, name, val);
	    }
	    else {
		rb_warning("bs: string constant `%s' already defined", 
			   rb_id2name(name));
	    }
	    break;
	}

	case BS_ELEMENT_FUNCTION:
	{
	    bs_element_function_t *bs_func = (bs_element_function_t *)value;
	    ID name = rb_intern(bs_func->name);
	    if (1) {
		st_insert(bs_functions, (st_data_t)name, (st_data_t)bs_func);
		/* FIXME we should reuse the same node for all functions */
		rb_define_global_function(
		    bs_func->name, bs_function_dispatch, -1);
		do_not_free = true;
	    }
	    else {
		rb_warning("bs: function `%s' already defined", bs_func->name);
	    }
	    break;
	}

	case BS_ELEMENT_FUNCTION_ALIAS:
	{
	    bs_element_function_alias_t *bs_func_alias = 
		(bs_element_function_alias_t *)value;
	    rb_define_alias(CLASS_OF(rb_mKernel), bs_func_alias->name,
			    bs_func_alias->original);
	    break;
	}

	case BS_ELEMENT_OPAQUE:
	case BS_ELEMENT_STRUCT:
	{
	    setup_bs_boxed_type(type, value);
	    break;
	}
    }

    if (!do_not_free)
	bs_element_free(type, value);
}

static void
load_bridge_support(const char *framework_path)
{
    char path[PATH_MAX];
    char *error;
    char *p;

    if (bs_find_path(framework_path, path, sizeof path)) {
	if (!bs_parse(path, 0, bs_parse_cb, NULL, &error))
	    rb_raise(rb_eRuntimeError, error);
#if 0
	/* FIXME 'GC capability mismatch' with .dylib files */
	p = strrchr(path, '.');
	assert(p != NULL);
	strlcpy(p, ".dylib", p - path - 1);
	if (access(path, R_OK) == 0) {
	    if (dlopen(path, RTLD_LAZY) == NULL)
		rb_raise(rb_eRuntimeError, dlerror());
	}
#endif
    }
}

static void
load_framework(const char *path)
{
    CFStringRef string;
    CFURLRef url;
    CFBundleRef bundle;
    CFErrorRef error;

    string = CFStringCreateWithCStringNoCopy(
	NULL, path, kCFStringEncodingUTF8, kCFAllocatorNull);
    assert(string != NULL);

    url = CFURLCreateWithFileSystemPath(
	NULL, string, kCFURLPOSIXPathStyle, true);
    assert(url != NULL);

    CFRelease(string);

    bundle = CFBundleCreate(NULL, url);

    if (bundle == NULL) {
	CFRelease(url);
	rb_raise(rb_eRuntimeError, "framework at path `%s' not found", path);
    }

    CFRelease(url);

    if (!CFBundleLoadExecutableAndReturnError(bundle, &error)) {
	char error_buf[1024];

	string = CFErrorCopyDescription(error);
	CFStringGetCString(string, &error_buf[0], sizeof error_buf,
			   kCFStringEncodingUTF8);
	CFRelease(bundle);
	CFRelease(string);
	rb_raise(rb_eRuntimeError, error_buf); 
    }

    CFRelease(bundle);

    load_bridge_support(path);
}

VALUE
rb_require_framework(VALUE recv, VALUE framework)
{
    const char *cstr;

    Check_Type(framework, T_STRING);
    cstr = RSTRING_PTR(framework);
    if (*cstr == '\0')
	rb_raise(rb_eArgError, "empty string given");

    if (*cstr == '.' || *cstr == '/') {
	/* framework path is given */
	load_framework(cstr);
    }
    else {
	/* framework name is given */
	char path[PATH_MAX];
	static char *home = NULL;

#define TRY_LOAD_PATH() 			\
    do { 					\
	if (access(path, R_OK) == 0) { 		\
	    load_framework(path);		\
	    return Qtrue;			\
	} 					\
    } 						\
    while(0)

	if (home == NULL)
	    home = getenv("HOME");
	if (home != NULL) {
	    snprintf(path, sizeof path, "%s/%s.framework", home, cstr);
	    TRY_LOAD_PATH(); 
	}

	snprintf(path, sizeof path, 
		 "/System/Library/Frameworks/%s.framework", cstr);
	TRY_LOAD_PATH();

	snprintf(path, sizeof path, "/Library/Frameworks/%s.framework", cstr);
	TRY_LOAD_PATH();

#undef TRY_LOAD_PATH

	rb_raise(rb_eRuntimeError, "framework `%s' not found", cstr);
    }

    return Qtrue;
}

static NSUInteger
imp_rb_ary_count(void *rcv, SEL sel)
{
    return RARRAY_LEN(rcv);
}

static void *
imp_rb_ary_objectAtIndex(void *rcv, SEL sel, NSUInteger idx)
{
    VALUE element;
    void *ptr;

    if (idx >= RARRAY_LEN(rcv))
	[NSException raise:@"NSRangeException" 
	    format:@"index (%d) beyond bounds (%d)", idx, RARRAY_LEN(rcv)];

    element = RARRAY_PTR(rcv)[idx];

    if (!rb_objc_rval_to_ocid(element, &ptr))
	[NSException raise:@"NSException" 
	    format:@"element (%s) at index (%d) cannott be passed to " \
	    "Objective-C", RSTRING_PTR(rb_inspect(element)), idx];

    if (ptr == NULL)
	ptr = [NSNull null];

    return ptr;
}

static NSUInteger
imp_rb_hash_count(void *rcv, SEL sel)
{
    return RHASH_SIZE(rcv);
}

static void *
imp_rb_hash_objectForKey(void *rcv, SEL sel, void *key)
{
    VALUE rkey;
    VALUE val;
    void *ptr;

    rb_objc_ocid_to_rval(&key, &rkey); 

    val = rb_hash_aref((VALUE)rcv, rkey);

    if (!rb_objc_rval_to_ocid(val, &ptr))
	[NSException raise:@"NSException" 
	    format:@"element (%s) at key (%s) cannot be passed to Objective-C", 
	    RSTRING_PTR(rb_inspect(val)), RSTRING_PTR(rb_inspect(rkey))];

    if (ptr == NULL
	&& RHASH(rcv)->ntbl != NULL 
	&& st_lookup(RHASH(rcv)->ntbl, (st_data_t)rkey, 0))
	ptr = [NSNull null];

    return ptr;
}

static void *
imp_rb_hash_keyEnumerator(void *rcv, SEL sel)
{
    VALUE keys;

    keys = rb_funcall((VALUE)rcv, rb_intern("keys"), 0, NULL);
    return [(NSArray *)keys objectEnumerator];
}

static NSUInteger
imp_rb_string_length(void *rcv, SEL sel)
{
    return RSTRING_LEN(rcv);
}

static unichar
imp_rb_string_characterAtIndex(void *rcv, SEL sel, NSUInteger idx)
{
    if (idx >= RARRAY_LEN(rcv))
	[NSException raise:@"NSRangeException" 
	    format:@"index (%d) beyond bounds (%d)", idx, RARRAY_LEN(rcv)];
    /* FIXME this is not quite true for multibyte strings */
    return RSTRING_PTR(rcv)[idx];
}

static inline void
rb_objc_install_method(Class klass, SEL sel, IMP imp)
{
    Method method = class_getInstanceMethod(klass, sel);
    assert(method != NULL);
    assert(class_addMethod(klass, sel, imp, method_getTypeEncoding(method)));
}

static inline void
rb_objc_override_method(Class klass, SEL sel, IMP imp)
{
    Method method = class_getInstanceMethod(klass, sel);
    assert(method != NULL);
    method_setImplementation(method, imp);
}

static void
rb_install_objc_primitives(void)
{
    Class klass;

    /* Array */
    klass = RCLASS_OCID(rb_cArray);
    rb_objc_install_method(klass, @selector(count), (IMP)imp_rb_ary_count);
    rb_objc_install_method(klass, @selector(objectAtIndex:), 
	(IMP)imp_rb_ary_objectAtIndex);

    /* Hash */
    klass = RCLASS_OCID(rb_cHash);
    rb_objc_install_method(klass, @selector(count), (IMP)imp_rb_hash_count);
    rb_objc_install_method(klass, @selector(objectForKey:), 
	(IMP)imp_rb_hash_objectForKey);
    rb_objc_install_method(klass, @selector(keyEnumerator), 
	(IMP)imp_rb_hash_keyEnumerator);

    /* String */
    klass = RCLASS_OCID(rb_cString);
    rb_objc_override_method(klass, @selector(length), 
	(IMP)imp_rb_string_length);
    rb_objc_install_method(klass, @selector(characterAtIndex:), 
	(IMP)imp_rb_string_characterAtIndex);
}

void
Init_ObjC(void)
{
    bs_constants = st_init_numtable();
    GC_ROOT(&bs_constants);
    bs_functions = st_init_numtable();
    GC_ROOT(&bs_functions);
    bs_function_syms = st_init_numtable();
    GC_ROOT(&bs_function_syms);
    bs_boxeds = st_init_strtable();
    GC_ROOT(&bs_boxeds);

    bs_const_magic_cookie = rb_str_new2("bs_const_magic_cookie");
    rb_cBoxed = rb_define_class("Boxed", rb_cObject);
    rb_ivar_type = rb_intern("@__objc_type__");

    rb_install_objc_primitives();
}
