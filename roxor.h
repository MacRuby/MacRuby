#ifndef __ROXOR_H_
#define __ROXOR_H_

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct {
    short min;		// min number of args that we accept
    short max;		// max number of args that we accept (-1 if rest)
    short left_req;	// number of args required on the left side
    short real;		// number of args of the low level function
} rb_vm_arity_t;

struct rb_vm_local {
    ID name;
    VALUE *value;
    struct rb_vm_local *next;
};
typedef struct rb_vm_local rb_vm_local_t;

#define VM_BLOCK_PROC	0x0001	// block is a Proc object
#define VM_BLOCK_LAMBDA 0x0002	// block is a lambda

typedef struct {
    VALUE self;
    NODE *node;
    rb_vm_arity_t arity;
    IMP imp;
    int flags;
    rb_vm_local_t *locals;
    int dvars_size;
    VALUE *dvars[1];
} rb_vm_block_t;

typedef struct {
    VALUE self;
    rb_vm_local_t *locals;
} rb_vm_binding_t;

typedef struct {
    VALUE oclass;
    VALUE rclass;
    VALUE recv;
    SEL sel;
    int arity;
    NODE *node;			// can be NULL (if pure Objective-C)
    void *cache;
} rb_vm_method_t;

VALUE rb_vm_run(const char *fname, NODE *node, rb_vm_binding_t *binding,
		bool try_interpreter);
VALUE rb_vm_run_under(VALUE klass, VALUE self, const char *fname, NODE *node,
		      rb_vm_binding_t *binding, bool try_interpreter);

bool rb_vm_running(void);
void rb_vm_set_running(bool flag);
bool rb_vm_parse_in_eval(void);
void rb_vm_set_parse_in_eval(bool flag);
VALUE rb_vm_load_path(void);
VALUE rb_vm_loaded_features(void);
int rb_vm_safe_level(void);
void rb_vm_set_safe_level(int level);
VALUE rb_vm_top_self(void);
void rb_vm_const_is_defined(ID path);
VALUE rb_vm_resolve_const_value(VALUE val, VALUE klass, ID name);
bool rb_vm_lookup_method(Class klass, SEL sel, IMP *pimp, NODE **pnode);
bool rb_vm_lookup_method2(Class klass, ID mid, SEL *psel, IMP *pimp, NODE **pnode);
NODE *rb_vm_get_method_node(IMP imp);
void rb_vm_define_method(Class klass, SEL sel, IMP imp, NODE *node, bool direct);
void rb_vm_define_attr(Class klass, const char *name, bool read, bool write, int noex);
void rb_vm_alias(VALUE klass, ID name, ID def);
void rb_vm_copy_methods(Class from_class, Class to_class);
VALUE rb_vm_call(VALUE self, SEL sel, int argc, const VALUE *args, bool super);
VALUE rb_vm_call_with_cache(void *cache, VALUE self, SEL sel, int argc, const VALUE *argv);
VALUE rb_vm_call_with_cache2(void *cache, VALUE self, VALUE klass, SEL sel, int argc, const VALUE *argv);
void *rb_vm_get_call_cache(SEL sel);
VALUE rb_vm_yield(int argc, const VALUE *argv);
VALUE rb_vm_yield_under(VALUE klass, VALUE self, int argc, const VALUE *argv);
bool rb_vm_respond_to(VALUE obj, SEL sel, bool priv);
VALUE rb_vm_method_missing(VALUE obj, int argc, const VALUE *argv);
void rb_vm_push_methods(VALUE ary, VALUE mod, bool include_objc_methods,
	int (*filter) (VALUE, ID, VALUE));
int rb_vm_find_class_ivar_slot(VALUE klass, ID name);
void rb_vm_set_outer(VALUE klass, VALUE under);
VALUE rb_vm_catch(VALUE tag);
VALUE rb_vm_throw(VALUE tag, VALUE value);

static inline void
rb_vm_regrow_robject_slots(struct RObject *obj, unsigned int new_num_slot)
{
    unsigned int i;
#if 0
    VALUE *new_slots = (VALUE *)xrealloc(obj->slots, sizeof(VALUE) * new_num_slot);
    if (obj->slots != new_slots) {
	GC_WB(&obj->slots, new_slots);
    }
#else
    VALUE *new_slots = (VALUE *)xmalloc(sizeof(VALUE) * (new_num_slot + 1));
    for (i = 0; i <= obj->num_slots; i++) {
	GC_WB(&new_slots[i], obj->slots[i]);
    }
    GC_WB(&obj->slots, new_slots);
#endif
    for (i = obj->num_slots + 1; i < new_num_slot; i++) {
	obj->slots[i] = Qundef;
    }
    obj->num_slots = new_num_slot;
}

static inline VALUE
rb_vm_get_ivar_from_slot(VALUE obj, int slot) 
{
    struct RObject *robj = (struct RObject *)obj;
    assert(slot >= 0);
    if (robj->num_slots < (unsigned int)slot) {
	return Qnil;
    }
    return robj->slots[slot];
}

static inline void
rb_vm_set_ivar_from_slot(VALUE obj, VALUE val, int slot) 
{
    struct RObject *robj = (struct RObject *)obj;
    assert(slot >= 0);
    if (robj->num_slots < (unsigned int)slot) {
	rb_vm_regrow_robject_slots(robj, (unsigned int)slot);
    }
    GC_WB(&robj->slots[slot], val);
}

rb_vm_method_t *rb_vm_get_method(VALUE klass, VALUE obj, ID mid, int scope);

static inline rb_vm_block_t *
rb_proc_get_block(VALUE proc)
{
   return (rb_vm_block_t *)DATA_PTR(proc);
}

rb_vm_block_t *rb_vm_prepare_block(void *llvm_function, NODE *node, VALUE self, int dvars_size, ...);
rb_vm_block_t *rb_vm_current_block(void);
bool rb_vm_block_saved(void);
void rb_vm_change_current_block(rb_vm_block_t *block);
void rb_vm_restore_current_block(void);
VALUE rb_vm_block_eval(rb_vm_block_t *block, int argc, const VALUE *argv);

rb_vm_binding_t *rb_vm_current_binding(void);
void rb_vm_add_binding(rb_vm_binding_t *binding);
void rb_vm_pop_binding();

static inline VALUE
rb_robject_allocate_instance(VALUE klass)
{
    struct RObject *obj;
    int num_slots = 10;

    obj = (struct RObject *)xmalloc(sizeof(struct RObject));
    GC_WB(&obj->slots, xmalloc(num_slots * sizeof(VALUE)));

    OBJSETUP(obj, klass, T_OBJECT);

    ROBJECT(obj)->tbl = NULL;
    ROBJECT(obj)->num_slots = num_slots;

    int i;
    for (i = 0; i < num_slots; i++) {
	ROBJECT(obj)->slots[i] = Qundef;
    }

    return (VALUE)obj;
}

void rb_vm_raise(VALUE exception);
VALUE rb_vm_current_exception(void);
void rb_vm_set_current_exception(VALUE exception);
VALUE rb_vm_backtrace(int level);

VALUE rb_vm_pop_broken_value(void);
#define RETURN_IF_BROKEN() \
    do { \
	VALUE __v = rb_vm_pop_broken_value(); \
	if (__v != Qundef) { \
	    return __v; \
	} \
    } \
    while (0)

void rb_vm_finalize(void);

void rb_vm_load_bridge_support(const char *path, const char *framework_path,
	int options);

VALUE rb_iseq_compile(VALUE src, VALUE file, VALUE line);
VALUE rb_iseq_eval(VALUE iseq);
VALUE rb_iseq_new(NODE *node, VALUE filename);

#if 0 // TODO
#if ENABLE_DEBUG_LOGGING 
# include <libgen.h>
extern bool ruby_dlog_enabled;
extern FILE *ruby_dlog_file;
# define DLOG(mod, fmt, args...)                                          \
    if (UNLIKELY(ruby_dlog_enabled)) {                                    \
        fprintf(ruby_dlog_file, "%s:%d %s ",                              \
                basename((char *)rb_sourcefile()), rb_sourceline(), mod); \
        fprintf(ruby_dlog_file, fmt, ##args);                             \
        fprintf(ruby_dlog_file, "\n");                                    \
    }
# endif
#endif
#define DLOG(mod, fmt, args...)

#if defined(__cplusplus)
}
#endif

#endif /* __ROXOR_H_ */
