/*
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2011, Apple Inc. All rights reserved.
 */

#ifndef __CLASS_H_
#define __CLASS_H_

#if defined(__cplusplus)
extern "C" {
#endif

bool rb_objc_hash_is_pure(VALUE);
bool rb_objc_str_is_pure(VALUE);
bool rb_objc_ary_is_pure(VALUE);

VALUE rb_objc_create_class(const char *name, VALUE super);
void rb_objc_class_sync_version(Class klass, Class super_class);
void rb_define_object_special_methods(VALUE klass);
VALUE rb_class_new_instance_imp(VALUE, SEL, int, VALUE *);
VALUE rb_make_singleton_class(VALUE super);
VALUE rb_singleton_class_attached_object(VALUE klass);
void rb_singleton_class_promote_for_gc(VALUE klass);

#define RCLASS_IS_OBJECT_SUBCLASS    (1<<1)  /* class is a true RubyObject subclass */
#define RCLASS_IS_RUBY_CLASS         (1<<2)  /* class was created from Ruby */
#define RCLASS_IS_MODULE             (1<<3)  /* class represents a Ruby Module */
#define RCLASS_IS_SINGLETON	     (1<<4)  /* class represents a singleton */
#define RCLASS_IS_FROZEN	     (1<<5)  /* class is frozen */
#define RCLASS_IS_TAINTED	     (1<<6)  /* class is tainted */
#define RCLASS_IS_INCLUDED           (1<<10)  /* module is included */
#define RCLASS_HAS_ROBJECT_ALLOC     (1<<11)  /* class uses the default RObject alloc */
#define RCLASS_SCOPE_PRIVATE	     (1<<12)  /* class opened for private methods */
#define RCLASS_SCOPE_PROTECTED	     (1<<13)  /* class opened for protected methods */
#define RCLASS_SCOPE_MOD_FUNC	     (1<<14)  /* class opened for module_function methods */
#define RCLASS_KVO_CHECK_DONE	     (1<<15)  /* class created by KVO and flags merged */

typedef struct rb_class_flags_cache {
    Class klass;
    unsigned long value;
    struct rb_class_flags_cache *next;
} rb_class_flags_cache_t;

#define CLASS_FLAGS_CACHE_SIZE 0x1000

extern rb_class_flags_cache_t *rb_class_flags;

static unsigned int
rb_class_flags_hash(Class k)
{
    return ((unsigned long)k >> 2) & (CLASS_FLAGS_CACHE_SIZE - 1);
}

static inline unsigned long
rb_class_get_mask(Class k)
{
    rb_class_flags_cache_t *e = &rb_class_flags[rb_class_flags_hash(k)];
    while (e != NULL) {
	if (e->klass == k) {
	    return e->value;
	}
	e = e->next;
    }
    return 0;
}

static inline bool
rb_class_erase_mask(Class k)
{
    rb_class_flags_cache_t *e = &rb_class_flags[rb_class_flags_hash(k)];
    while (e != NULL) {
	if (e->klass == k) {
	    e->klass = 0;
	    return true;
	}
	e = e->next;
    }
    return false;
}

static inline void
rb_class_set_mask(Class k, unsigned long mask)
{
    rb_class_flags_cache_t *e = &rb_class_flags[rb_class_flags_hash(k)];
again:
    if (e->klass == k) {
set_value:
	e->value = mask;
	return;
    }
    if (e->klass == 0) {
	e->klass = k;
	goto set_value;
    }
    if (e->next != NULL) {
	e = e->next;
	goto again;
    }
    rb_class_flags_cache_t *ne = (rb_class_flags_cache_t *)malloc(
	    sizeof(rb_class_flags_cache_t));
    ne->klass = k;
    ne->value = mask;
    ne->next = NULL;
    e->next = ne;
}

#define RCLASS_MASK_TYPE_SHIFT 16

static inline unsigned long
rb_class_get_flags(Class k)
{
    return rb_class_get_mask(k) >> RCLASS_MASK_TYPE_SHIFT;
}

static inline void
rb_class_set_flags(Class k, unsigned long flags)
{
    rb_class_set_mask(k, (flags << RCLASS_MASK_TYPE_SHIFT) | 0);
}

#define RCLASS_VERSION(m) (rb_class_get_flags((Class)m))
#define RCLASS_SET_VERSION(m,f) (rb_class_set_flags((Class)m, (unsigned long)f))
#define RCLASS_SET_VERSION_FLAG(m,f) (RCLASS_SET_VERSION((Class)m, (RCLASS_VERSION(m) | f)))

#define RCLASS_RUBY(m) ((RCLASS_VERSION(m) & RCLASS_IS_RUBY_CLASS) == RCLASS_IS_RUBY_CLASS)
#define RCLASS_MODULE(m) ((RCLASS_VERSION(m) & RCLASS_IS_MODULE) == RCLASS_IS_MODULE)
#define RCLASS_SINGLETON(m) ((RCLASS_VERSION(m) & RCLASS_IS_SINGLETON) == RCLASS_IS_SINGLETON)

CFMutableDictionaryRef rb_class_ivar_dict(VALUE);
CFMutableDictionaryRef rb_class_ivar_dict_or_create(VALUE);
void rb_class_ivar_set_dict(VALUE, CFMutableDictionaryRef);
void rb_class_merge_ivar_dicts(VALUE orig_class, VALUE dest_class);

typedef enum {
    SCOPE_DEFAULT = 0,  // public for everything but Object
    SCOPE_PUBLIC,
    SCOPE_PRIVATE,
    SCOPE_PROTECTED,
    SCOPE_MODULE_FUNC,
} rb_vm_scope_t;

static inline void
rb_vm_check_if_module(VALUE mod)
{
    switch (TYPE(mod)) {
	case T_CLASS:
	case T_MODULE:
	    break;

	default:
	    rb_raise(rb_eTypeError, "%s is not a class/module",
		    RSTRING_PTR(rb_inspect(mod)));
    }
}

static inline void
rb_vm_set_current_scope(VALUE mod, rb_vm_scope_t scope)
{
    if (scope == SCOPE_DEFAULT) {
	scope = mod == rb_cObject ? SCOPE_PRIVATE : SCOPE_PUBLIC;
    }
    long v = RCLASS_VERSION(mod);
    switch (scope) {
	case SCOPE_PUBLIC:
	    v &= ~RCLASS_SCOPE_PRIVATE;
	    v &= ~RCLASS_SCOPE_PROTECTED;
	    v &= ~RCLASS_SCOPE_MOD_FUNC;
	    break;

	case SCOPE_PRIVATE:
	    v |= RCLASS_SCOPE_PRIVATE;
	    v &= ~RCLASS_SCOPE_PROTECTED;
	    v &= ~RCLASS_SCOPE_MOD_FUNC;
	    break;

	case SCOPE_PROTECTED:
	    v &= ~RCLASS_SCOPE_PRIVATE;
	    v |= RCLASS_SCOPE_PROTECTED;
	    v &= ~RCLASS_SCOPE_MOD_FUNC;
	    break;

	case SCOPE_MODULE_FUNC:
	    v &= ~RCLASS_SCOPE_PRIVATE;
	    v &= ~RCLASS_SCOPE_PROTECTED;
	    v |= RCLASS_SCOPE_MOD_FUNC;
	    break;

	case SCOPE_DEFAULT:
	    abort(); // handled earlier
    }

    RCLASS_SET_VERSION(mod, v);
}

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // __CLASS_H_
