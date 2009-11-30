/*
 * MacRuby ObjC helpers.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 * 
 * Copyright (C) 2007-2009, Apple Inc. All rights reserved.
 */

#ifndef __OBJC_H_
#define __OBJC_H_

#if defined(__cplusplus)
extern "C" {
#endif

#include "bs.h"

struct rb_objc_method_sig {
  const char *types;
  unsigned int argc;
};

bool rb_objc_get_types(VALUE recv, Class klass, SEL sel, Method m,
	bs_element_method_t *bs_method, char *buf, size_t buflen);

VALUE rb_objc_call(VALUE recv, SEL sel, int argc, VALUE *argv);

VALUE rb_objc_call2(VALUE recv, VALUE klass, SEL sel, IMP imp, 
	struct rb_objc_method_sig *sig, bs_element_method_t *bs_method,
	int argc, VALUE *argv);

void rb_objc_define_kvo_setter(VALUE klass, ID mid);
void rb_objc_change_ruby_method_signature(VALUE mod, ID mid, VALUE sig);

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

extern void *placeholder_String;
extern void *placeholder_Dictionary;
extern void *placeholder_Array;

static inline bool
rb_objc_is_placeholder(id obj)
{
    void *klass = *(void **)obj;
    return klass == placeholder_String || klass == placeholder_Dictionary
	|| klass == placeholder_Array;
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
	    case ']': case '}': case ')': level--; break;
	    case '[': case '{': case '(': level += 1; break;
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

                /* basic types */
            default:
                return type;
        }
    }
}

static inline unsigned int
TypeArity(const char *type)
{
    unsigned int arity = 0;
    while (*type != '\0') {
	type = SkipFirstType(type);
	arity++;
    }
    return arity;
}

#if defined(__cplusplus)
}
#endif

#endif /* __OBJC_H_ */
