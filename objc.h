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

bs_element_method_t * rb_bs_find_method(Class klass, SEL sel);

bool rb_objc_get_types(VALUE recv, Class klass, SEL sel,
	bs_element_method_t *bs_method, char *buf, size_t buflen);

VALUE rb_objc_call(VALUE recv, SEL sel, int argc, VALUE *argv);

VALUE rb_objc_call2(VALUE recv, VALUE klass, SEL sel, IMP imp, 
	struct rb_objc_method_sig *sig, bs_element_method_t *bs_method, int argc, 
	VALUE *argv);

void rb_objc_define_kvo_setter(VALUE klass, ID mid);
void rb_objc_change_ruby_method_signature(VALUE mod, ID mid, VALUE sig);

static inline void
rb_objc_install_method(Class klass, SEL sel, IMP imp)
{
    Method method, method2;

    method = class_getInstanceMethod(klass, sel);
    assert(method != NULL);
 
    method2 = class_getInstanceMethod((Class)RCLASS_SUPER(klass), sel);
    if (method == method2)  {
	assert(class_addMethod(klass, sel, imp, method_getTypeEncoding(method)));
    }
    else {
	method_setImplementation(method, imp);
    }
}

static inline void
rb_objc_install_method2(Class klass, const char *selname, IMP imp)
{
    rb_objc_install_method(klass, sel_registerName(selname), imp);
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
    return klass == placeholder_String || klass == placeholder_Dictionary || klass == placeholder_Array;
}

bool rb_objc_symbolize_address(void *addr, void **start, char *name, size_t name_len);

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

#if defined(__cplusplus)
}
#endif

#endif /* __OBJC_H_ */
