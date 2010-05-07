/*
 * MacRuby ObjC helpers.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 * 
 * Copyright (C) 2007-2010, Apple Inc. All rights reserved.
 */

#ifndef __OBJC_H_
#define __OBJC_H_

#if defined(__cplusplus)
extern "C" {
#endif

#include "bs.h"

bool rb_objc_get_types(VALUE recv, Class klass, SEL sel, Method m,
	bs_element_method_t *bs_method, char *buf, size_t buflen);

bool rb_objc_supports_forwarding(VALUE recv, SEL sel);

void rb_objc_define_kvo_setter(VALUE klass, ID mid);
VALUE rb_vm_set_kvo_ivar(VALUE obj, ID name, VALUE val);

static inline IMP
rb_objc_install_method(Class klass, SEL sel, IMP imp)
{
    Method method = class_getInstanceMethod(klass, sel);
    if (method == NULL) {
	printf("method %s not found on class %p - aborting\n",
		sel_getName(sel), klass);
	abort();
    }
    return class_replaceMethod(klass, sel, imp, method_getTypeEncoding(method));
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

bool rb_objc_symbolize_address(void *addr, void **start, char *name,
	size_t name_len);

id rb_rb2oc_exception(VALUE exc);
VALUE rb_oc2rb_exception(id exc);

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
            case 'O':   /* bycopy */
            case 'n':   /* in */
            case 'o':   /* out */
            case 'N':   /* inout */
            case 'r':   /* const */
            case 'V':   /* oneway */
            case '^':   /* pointers */
                break;

                /* arrays */
            case '[':
                return type + SubtypeUntil (type, ']') + 1;

                /* structures */
            case '{':
                return type + SubtypeUntil (type, '}') + 1;

                /* unions */
            case '(':
                return type + SubtypeUntil (type, ')') + 1;

                /* Function pointers */
            case '<':
                return type + SubtypeUntil (type, '>') + 1;

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
	if (FIXNUM_P(obj)) {
	    // TODO: this could be optimized in case we can fit the fixnum
	    // into an immediate NSNumber directly.
	    long val = FIX2LONG(obj);
	    CFNumberRef number = CFNumberCreate(NULL, kCFNumberLongType, &val);
	    CFMakeCollectable(number);
	    return (id)number;
	}
	if (FIXFLOAT_P(obj)) {
	    double val = NUM2DBL(obj);
	    CFNumberRef number = CFNumberCreate(NULL, kCFNumberDoubleType,
		    &val);
	    CFMakeCollectable(number);
	    return (id)number;
	}
    }
    return (id)obj;
}

static inline bool
rb_objc_obj_is_nsnumber(id obj)
{
    Class k = object_getClass(obj); // might be an immediate
    do {
	if (k == (Class)rb_cNSNumber) {
	    return true;
	}
	k = class_getSuperclass(k);
    }
    while (k != NULL);
    return false;
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

    if (rb_objc_obj_is_nsnumber(obj)) {
	// TODO: this could be optimized in case the object is an immediate.
	if (CFNumberIsFloatType((CFNumberRef)obj)) {
	    double v = 0;
	    assert(CFNumberGetValue((CFNumberRef)obj, kCFNumberDoubleType, &v));
	    return DOUBLE2NUM(v);
	}
	else {
	    long v = 0;
	    assert(CFNumberGetValue((CFNumberRef)obj, kCFNumberLongType, &v));
	    return LONG2FIX(v);
	}
    }

    if (((unsigned long)obj & 0x1) == 0x1) {
	rb_bug("unknown Objective-C immediate: %p\n", obj);
    }

    return (VALUE)obj;
}

#define RB2OC(obj) (rb_rval_to_ocid((VALUE)obj))
#define OC2RB(obj) (rb_ocid_to_rval((id)obj))

void rb_objc_exception_raise(const char *name, const char *message);

bool rb_objc_ignore_sel(SEL sel);
void rb_objc_force_class_initialize(Class klass);
void rb_objc_fix_relocatable_load_path(void);

#if defined(__cplusplus)
}
#endif

#endif /* __OBJC_H_ */
