/*
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2007-2011, Apple Inc. All rights reserved
 */

#ifndef __MACRUBY_INTERNAL_H
#define __MACRUBY_INTERNAL_H 1

#if defined(__cplusplus)
extern "C" {
#endif

#include "ruby.h"
#include "PLBlockIMP.h"

#include <objc/objc.h>
#include <objc/runtime.h>
#include <objc/message.h>
#include <objc/objc-auto.h>
#include <assert.h>
#include <CoreFoundation/CoreFoundation.h>

void rb_include_module2(VALUE klass, VALUE orig_klass, VALUE module, bool check,
	bool add_methods);

VALUE rb_objc_block_call(VALUE obj, SEL sel, int argc,
	VALUE *argv, VALUE (*bl_proc) (ANYARGS), VALUE data2);

#if !defined(__AUTO_ZONE__)
boolean_t auto_zone_set_write_barrier(void *zone, const void *dest, const void *new_value);
void auto_zone_add_root(void *zone, void *address_of_root_ptr, void *value);
void auto_zone_retain(void *zone, void *ptr);
unsigned int auto_zone_release(void *zone, void *ptr);
unsigned int auto_zone_retain_count(void *zone, const void *ptr);
void *auto_zone_write_barrier_memmove(void *zone, void *dst, const void *src, size_t size);
extern void *__auto_zone;
#else
extern auto_zone_t *__auto_zone;
#endif

#define GC_WB_0(dst, newval, check) \
    do { \
	void *nv = (void *)newval; \
	if (!SPECIAL_CONST_P(nv)) { \
	    if (!auto_zone_set_write_barrier(__auto_zone, \
			(const void *)dst, (const void *)nv)) { \
		if (check) { \
		    rb_bug("destination %p isn't in the auto zone", dst); \
		} \
	    } \
	} \
	*(void **)dst = nv; \
    } \
    while (0)

#define GC_WB(dst, newval) GC_WB_0(dst, newval, true)

static inline void *
rb_objc_retain(void *addr)
{
    if (addr != NULL && !SPECIAL_CONST_P(addr)) {
        auto_zone_retain(__auto_zone, addr);
    }
    return addr;
}
#define GC_RETAIN(obj) (rb_objc_retain((void *)obj))

static inline unsigned int
rb_objc_retain_count(const void *addr)
{
    return auto_zone_retain_count(__auto_zone, addr);
}
#define GC_RETAIN_COUNT(obj) (rb_objc_retain_count((const void *)obj))

static inline void *
rb_objc_memmove(void *dst, const void *src, size_t size)
{
    return auto_zone_write_barrier_memmove(__auto_zone, dst, src, size);
}
#define GC_MEMMOVE(dst, src, size) (rb_objc_memmove(dst, src, size))

static inline void *
rb_objc_release(void *addr)
{
    if (addr != NULL && !SPECIAL_CONST_P(addr)) {
        auto_zone_release(__auto_zone, addr);
    }
    return addr;
}
#define GC_RELEASE(obj) (rb_objc_release((void *)obj))

// MacRubyIntern.h

/* object.c */
void rb_obj_invoke_initialize_copy(VALUE dest, VALUE obj);

/* enumerator.c */
VALUE rb_enumeratorize(VALUE, SEL, int, VALUE *);
#define RETURN_ENUMERATOR(obj, argc, argv) \
    do {	\
	if (!rb_block_given_p()) { \
	    return rb_enumeratorize((VALUE)obj, sel, argc, argv); \
	} \
    } while (0)
VALUE rb_f_notimplement(VALUE rcv, SEL sel);
VALUE rb_method_call(VALUE, SEL, int, VALUE*);
VALUE rb_file_directory_p(VALUE,SEL,VALUE);
VALUE rb_obj_id(VALUE obj, SEL sel);

void rb_objc_gc_register_thread(void);
void rb_objc_gc_unregister_thread(void);
void rb_objc_set_associative_ref(void *, void *, void *);
void *rb_objc_get_associative_ref(void *, void *);

VALUE rb_io_gets(VALUE, SEL);
VALUE rb_io_getbyte(VALUE, SEL);
VALUE rb_io_ungetc(VALUE, SEL, VALUE);
VALUE rb_io_flush(VALUE, SEL);
VALUE rb_io_eof(VALUE, SEL);
VALUE rb_io_binmode(VALUE, SEL);
VALUE rb_io_addstr(VALUE, SEL, VALUE);
VALUE rb_io_printf(VALUE, SEL, int, VALUE *);
VALUE rb_io_print(VALUE, SEL, int, VALUE *);

VALUE rb_objc_num_coerce_bin(VALUE x, VALUE Y, SEL sel);
VALUE rb_objc_num_coerce_cmp(VALUE, VALUE, SEL sel);
VALUE rb_objc_num_coerce_relop(VALUE, VALUE, SEL sel);

VALUE rb_f_kill(VALUE, SEL, int, VALUE*);
VALUE rb_struct_initialize(VALUE, SEL, VALUE);
VALUE rb_class_real(VALUE, bool hide_builtin_foundation_classes);
void rb_range_extract(VALUE range, VALUE *begp, VALUE *endp, bool *exclude);
VALUE rb_cvar_get2(VALUE klass, ID id, bool check);

VALUE rb_require_framework(VALUE, SEL, int, VALUE *);

RUBY_EXTERN VALUE rb_cNSObject;
RUBY_EXTERN VALUE rb_cRubyObject;
RUBY_EXTERN VALUE rb_cNSString;
RUBY_EXTERN VALUE rb_cNSMutableString;
RUBY_EXTERN VALUE rb_cRubyString;
RUBY_EXTERN VALUE rb_cNSArray;
RUBY_EXTERN VALUE rb_cNSMutableArray;
RUBY_EXTERN VALUE rb_cRubyArray;
RUBY_EXTERN VALUE rb_cNSHash;
RUBY_EXTERN VALUE rb_cNSMutableHash;
RUBY_EXTERN VALUE rb_cRubyHash;
RUBY_EXTERN VALUE rb_cNSNumber;
RUBY_EXTERN VALUE rb_cNSDate;
RUBY_EXTERN VALUE rb_cBoxed;
RUBY_EXTERN VALUE rb_cPointer;
RUBY_EXTERN VALUE rb_cTopLevel;

long rb_objc_flag_get_mask(const void *);
void rb_objc_flag_set(const void *, int, bool);
bool rb_objc_flag_check(const void *, int);

#if defined(__cplusplus)
}  // extern "C" {
#endif

#endif /* __MACRUBY_INTERNAL_H */
