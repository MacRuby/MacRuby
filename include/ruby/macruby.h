
#ifndef RUBY_MACRUBY_H
#define RUBY_MACRUBY_H 1

#if defined(__cplusplus)
extern "C" {
#if 0
} /* satisfy cc-mode */
#endif
#endif

#include "ruby.h"

#include <objc/objc.h>
#include <objc/runtime.h>
#include <objc/message.h>
#include <objc/objc-auto.h>
#include <assert.h>
#include <CoreFoundation/CoreFoundation.h>

bool rb_obj_is_native(VALUE obj);
#define NATIVE(obj) (rb_obj_is_native((VALUE)obj))

void rb_include_module2(VALUE klass, VALUE orig_klass, VALUE module, bool check,
	bool add_methods);

VALUE rb_objc_block_call(VALUE obj, SEL sel, int argc,
	VALUE *argv, VALUE (*bl_proc) (ANYARGS), VALUE data2);

#if !defined(__AUTO_ZONE__)
boolean_t auto_zone_set_write_barrier(void *zone, const void *dest, const void *new_value);
void auto_zone_add_root(void *zone, void *address_of_root_ptr, void *value);
void auto_zone_retain(void *zone, void *ptr);
unsigned int auto_zone_release(void *zone, void *ptr);
extern void *__auto_zone;
#else
extern auto_zone_t *__auto_zone;
#endif

#define GC_WB(dst, newval) \
    do { \
	void *nv = (void *)newval; \
	if (!SPECIAL_CONST_P(nv)) { \
	    if (!auto_zone_set_write_barrier(__auto_zone, \
			(const void *)dst, (const void *)nv)) { \
		rb_bug("destination %p isn't in the auto zone", dst); \
	    } \
	} \
	*(void **)dst = nv; \
    } \
    while (0)

static inline void *
rb_objc_retain(void *addr)
{
    if (addr != NULL && !SPECIAL_CONST_P(addr)) {
        auto_zone_retain(__auto_zone, addr);
    }
    return addr;
}
#define GC_RETAIN(obj) (rb_objc_retain((void *)obj))

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

#if WITH_OBJC
bool rb_objc_hash_is_pure(VALUE);
bool rb_objc_str_is_pure(VALUE);
bool rb_objc_ary_is_pure(VALUE);
long rb_ary_len(VALUE);
VALUE rb_ary_elt(VALUE, long);
VALUE rb_ary_aref(VALUE ary, SEL sel, int argc, VALUE *argv);

VALUE rb_objc_create_class(const char *name, VALUE super);
void rb_objc_class_sync_version(Class klass, Class super_class);
void rb_define_object_special_methods(VALUE klass);
VALUE rb_class_new_instance_imp(VALUE, SEL, int, VALUE *);
VALUE rb_make_singleton_class(VALUE super);
#endif

/* enumerator.c */
VALUE rb_enumeratorize(VALUE, SEL, int, VALUE *);
#define RETURN_ENUMERATOR(obj, argc, argv) do {				\
	if (!rb_block_given_p())					\
	    return rb_enumeratorize((VALUE)obj, sel, argc, argv);	\
    } while (0)
VALUE rb_f_notimplement(VALUE rcv, SEL sel);
VALUE rb_method_call(VALUE, SEL, int, VALUE*);
VALUE rb_file_directory_p(VALUE,SEL,VALUE);
VALUE rb_obj_id(VALUE obj, SEL sel);

#if WITH_OBJC
void rb_objc_gc_register_thread(void);
void rb_objc_gc_unregister_thread(void);
void rb_objc_set_associative_ref(void *, void *, void *);
void *rb_objc_get_associative_ref(void *, void *);
# define rb_gc_mark_locations(x,y)
# define rb_mark_tbl(x)
# define rb_mark_set(x)
# define rb_mark_hash(x)
# define rb_gc_mark_maybe(x)
# define rb_gc_mark(x)
#else
void rb_gc_mark_locations(VALUE*, VALUE*);
void rb_mark_tbl(struct st_table*);
void rb_mark_set(struct st_table*);
void rb_mark_hash(struct st_table*);
void rb_gc_mark_maybe(VALUE);
void rb_gc_mark(VALUE);
#endif

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
VALUE rb_num_coerce_relop(VALUE, VALUE, SEL);

VALUE rb_f_kill(VALUE, SEL, int, VALUE*);
VALUE rb_struct_initialize(VALUE, SEL, VALUE);
VALUE rb_class_real(VALUE, bool hide_builtin_foundation_classes);
void rb_range_extract(VALUE range, VALUE *begp, VALUE *endp, bool *exclude);
VALUE rb_cvar_get2(VALUE klass, ID id, bool check);

#if WITH_OBJC
void rb_objc_alias(VALUE, ID, ID);
VALUE rb_mod_objc_ancestors(VALUE);
VALUE rb_require_framework(VALUE, SEL, int, VALUE *);
VALUE rb_objc_resolve_const_value(VALUE, VALUE, ID);
ID rb_objc_missing_sel(ID, int);
long rb_objc_flag_get_mask(const void *);
void rb_objc_flag_set(const void *, int, bool);
bool rb_objc_flag_check(const void *, int);
long rb_objc_remove_flags(const void *obj);
void rb_objc_methods(VALUE, Class);
bool rb_objc_is_immutable(VALUE);
#endif

#if defined(__cplusplus)
#if 0
{ /* satisfy cc-mode */
#endif
}  /* extern "C" { */
#endif

#endif /* RUBY_MACRUBY_H */
