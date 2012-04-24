/*
 * MacRuby ObjC helpers.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2007-2011, Apple Inc. All rights reserved.
 */

#ifndef __OBJC_H_
#define __OBJC_H_

#if defined(__cplusplus)
extern "C" {
#endif

#include "bs.h"

static inline const char *
rb_get_bs_method_type(bs_element_method_t *bs_method, int arg)
{
    if (bs_method != NULL) {
	if (arg == -1) {
	    if (bs_method->retval != NULL) {
		return bs_method->retval->type;
	    }
	}
	else {
	    unsigned int i;
	    for (i = 0; i < bs_method->args_count; i++) {
		if (bs_method->args[i].index == arg) {
		    return bs_method->args[i].type;
		}
	    }
	}
    }
    return NULL;
}

bool rb_objc_get_types(VALUE recv, Class klass, SEL sel, Method m,
	bs_element_method_t *bs_method, char *buf, size_t buflen);

bool rb_objc_supports_forwarding(VALUE recv, SEL sel);

void rb_objc_define_kvo_setter(VALUE klass, ID mid);

static inline IMP
rb_objc_install_method(Class klass, SEL sel, IMP imp)
{
    Method method = class_getInstanceMethod(klass, sel);
    if (method == NULL) {
	printf("method %s not found on class %p - aborting\n",
		sel_getName(sel), klass);
	abort();
    }
    IMP old = method_getImplementation(method);
    class_replaceMethod(klass, sel, imp, method_getTypeEncoding(method));
    return old;
}

static inline IMP
rb_objc_install_method2(Class klass, const char *selname, IMP imp)
{
    return rb_objc_install_method(klass, sel_registerName(selname), imp);
}

static inline bool
rb_objc_is_kind_of(id object, Class klass)
{
    Class cls;
    for (cls = *(Class *)object; cls != NULL; cls = class_getSuperclass(cls)) {
	if (cls == klass) {
	    return true;
	}
    }
    return false;
}

id rb_rb2oc_exception(VALUE exc);
VALUE rb_oc2rb_exception(id exc, bool *created);

size_t rb_objc_type_size(const char *type);

static inline int
SubtypeUntil(const char *type, char end)
{
    int level = 0;
    const char *head = type;

    while (*type)
    {
	if (!*type || (!level && (*type == end)))
	    return (int)(type - head);

	switch (*type)
	{
	    case ']': case '}': case ')': case '>': level--; break;
	    case '[': case '{': case '(': case '<': level += 1; break;
	}

	type += 1;
    }

    rb_bug ("Object: SubtypeUntil: end of type encountered prematurely\n");
    return 0;
}

static inline const char *
SkipStackSize(const char *type)
{
    while ((*type >= '0') && (*type <= '9')) {
	type += 1;
    }
    return type;
}

static inline const char *
SkipTypeModifiers(const char *type)
{
    while (true) {
	switch (*type) {
	    case _C_CONST:
	    case 'O': /* bycopy */
	    case 'n': /* in */
	    case 'o': /* out */
	    case 'N': /* inout */
	    case 'V': /* oneway */
		type++;
		break;

	    default:
		return type;
	}
    }
}

static inline const char *
SkipFirstType(const char *type)
{
    while (1) {
        switch (*type++) {
	    case _C_CONST:
	    case _C_PTR:
            case 'O':   /* bycopy */
            case 'n':   /* in */
            case 'o':   /* out */
            case 'N':   /* inout */
            case 'V':   /* oneway */
                break;

	    case _C_ID:
		if (*type == _C_UNDEF) {
		    type++;  /* Blocks */
		}
		return type;

                /* arrays */
            case _C_ARY_B:
                return type + SubtypeUntil (type, _C_ARY_E) + 1;

                /* structures */
            case _C_STRUCT_B:
                return type + SubtypeUntil (type, _C_STRUCT_E) + 1;

                /* unions */
            case _C_UNION_B:
                return type + SubtypeUntil (type, _C_UNION_E) + 1;

                /* lambdas */
            case _MR_C_LAMBDA_B:
                return type + SubtypeUntil (type, _MR_C_LAMBDA_E) + 1;

                /* basic types */
            default:
                return type;
        }
    }
}

static inline const char *
GetFirstType(const char *p, char *buf, size_t buflen)
{
    const char *p2 = SkipFirstType(p);
    const size_t len = p2 - p;
    assert(len < buflen);
    strncpy(buf, p, len);
    buf[len] = '\0';
    return SkipStackSize(p2);
}

static inline unsigned int
TypeArity(const char *type)
{
    unsigned int arity = 0;
    while (*type != '\0') {
	type = SkipFirstType(type);
	type = SkipStackSize(type);
	arity++;
    }
    return arity;
}

// We do not use method_getNumberOfArguments since it's broken on 
// SnowLeopard for signatures containing Block objects.
static inline unsigned int
rb_method_getNumberOfArguments(Method m)
{
    const unsigned int arity = TypeArity(method_getTypeEncoding(m));
    assert(arity >= 2);
    return arity - 1; // Skip return type.
}

id rb_objc_numeric2nsnumber(VALUE obj);
VALUE rb_objc_convert_immediate(id obj);

static inline id
rb_rval_to_ocid(VALUE obj)
{
    if (SPECIAL_CONST_P(obj)) {
        if (obj == Qtrue) {
            return (id)kCFBooleanTrue;
        }
        if (obj == Qfalse) {
            return (id)kCFBooleanFalse;
        }
        if (obj == Qnil) {
            return (id)kCFNull;
        }
	if (IMMEDIATE_P(obj)) {
	    return rb_objc_numeric2nsnumber(obj);
	}
    }
    return (id)obj;
}

static inline VALUE
rb_ocid_to_rval(id obj)
{
    if (obj == (id)kCFBooleanTrue) {
	return Qtrue;
    }
    if (obj == (id)kCFBooleanFalse) {
	return Qfalse;
    }
    if (obj == (id)kCFNull || obj == nil) {
	return Qnil;
    }
    return rb_objc_convert_immediate(obj);
}

#define RB2OC(obj) (rb_rval_to_ocid((VALUE)obj))
#define OC2RB(obj) (rb_ocid_to_rval((id)obj))

void rb_objc_exception_raise(const char *name, const char *message);

bool rb_objc_ignored_sel(SEL sel);
bool rb_objc_isEqual(VALUE x, VALUE y); 
void rb_objc_force_class_initialize(Class klass);
void rb_objc_fix_relocatable_load_path(void);
void rb_objc_load_loaded_frameworks_bridgesupport(void);
void rb_objc_install_NSObject_special_methods(Class k);

extern bool rb_objc_enable_ivar_set_kvo_notifications;

#if !defined(MACRUBY_STATIC)
void rb_vm_parse_bs_full_file(const char *path,
	void (*add_stub_types_cb)(SEL, const char *, bool, void *),
	void *ctx);
#endif

#define SINCE_EPOCH 978307200.0
#define CF_REFERENCE_DATE SINCE_EPOCH

#if defined(__cplusplus)
}
#endif

#endif /* __OBJC_H_ */
