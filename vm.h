/*
 * MacRuby VM.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2008-2011, Apple Inc. All rights reserved.
 */

#ifndef __VM_H_
#define __VM_H_

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct {
    short min;		// min number of args that we accept
    short max;		// max number of args that we accept (-1 if rest)
    short left_req;	// number of args required on the left side
    short real;		// number of args of the low level function
} rb_vm_arity_t;

typedef struct rb_vm_local {
    ID name;
    VALUE *value;
    struct rb_vm_local *next;
} rb_vm_local_t;

#define VM_BLOCK_PROC	(1<<0)	// block is a Proc object
#define VM_BLOCK_LAMBDA (1<<1)	// block is a lambda
#define VM_BLOCK_ACTIVE (1<<2)	// block is active (being executed)
#define VM_BLOCK_METHOD (1<<3)	// block is created from Method
#define VM_BLOCK_IFUNC  (1<<4)  // block is created from rb_vm_create_block()
#define VM_BLOCK_EMPTY  (1<<5)	// block has an empty body
#define VM_BLOCK_THREAD (1<<6)	// block is being executed as a Thread
#define VM_BLOCK_AOT	(1<<10) // block is created by the AOT compiler
				// (temporary)

typedef struct rb_vm_block {
    // IMPORTANT: the flags field should always be at the beginning.
    // Look at how rb_vm_take_ownership() is called in compiler.cpp.
    int flags;
    VALUE proc; // a reference to a Proc object, or nil.
    VALUE self;
    VALUE klass;
    VALUE userdata; // if VM_BLOCK_IFUNC, contains the user data, otherwise
		    // contains the key used in the blocks cache.
    rb_vm_arity_t arity;
    IMP imp;
    rb_vm_local_t *locals;
    struct rb_vm_var_uses **parent_var_uses;
    struct rb_vm_block *parent_block;
    int dvars_size;
    // IMPORTANT: do not add fields after dvars, because it would mess with
    // the way the structure is allocated.
    VALUE *dvars[1];
} rb_vm_block_t;

typedef struct rb_vm_outer {
    Class klass;
    bool pushed_by_eval;
    struct rb_vm_outer *outer;
} rb_vm_outer_t;

typedef struct rb_vm_binding {
    VALUE self;
    rb_vm_block_t *block;
    rb_vm_local_t *locals;
    rb_vm_outer_t *outer_stack;
    struct rb_vm_binding *next;
} rb_vm_binding_t;

#define VM_METHOD_EMPTY		1 // method has an empty body (compilation)
#define VM_METHOD_PRIVATE	2 // method is private (runtime)
#define VM_METHOD_PROTECTED	4 // method is protected (runtime)
#define VM_METHOD_FBODY		8 // method has a MRI C prototype (compilation) 

static inline int
rb_vm_noex_flag(const int noex)
{
    switch (noex) {
	case NOEX_PRIVATE:
	    return VM_METHOD_PRIVATE;
	case NOEX_PROTECTED:
	    return VM_METHOD_PROTECTED;
	default:
	case NOEX_PUBLIC:
	    return 0;
    }
}

static inline int
rb_vm_node_flags(NODE *node)
{
    int flags = 0;
    if (nd_type(node) == NODE_FBODY) {
	flags |= VM_METHOD_FBODY;
	if (nd_type(node->nd_body) == NODE_METHOD) {
	    flags |= rb_vm_noex_flag(node->nd_body->nd_noex);
	}
    }
    if (node->nd_body == NULL) {
	flags |= VM_METHOD_EMPTY;
    }
    return flags;
}

static inline SEL
rb_vm_name_to_sel(const char *name, int arity)
{
    SEL sel;
    if (arity > 0 && name[strlen(name) - 1] != ':') {
	char buf[100];
	snprintf(buf, sizeof buf, "%s:", name);
	sel = sel_registerName(buf);
    }
    else {
	sel = sel_registerName(name);
    }
    return sel;
}

static inline SEL
rb_vm_id_to_sel(ID mid, int arity)
{
    return rb_vm_name_to_sel(rb_id2name(mid), arity);
}

typedef struct rb_vm_method_node {
    rb_vm_arity_t arity;
    Class klass;
    SEL sel;
    IMP objc_imp;
    IMP ruby_imp;
    int flags;
} rb_vm_method_node_t;

typedef struct {
    VALUE oclass;
    VALUE rclass;
    VALUE recv;
    SEL sel;
    int arity;
    rb_vm_method_node_t *node;	// NULL in case the method is ObjC
    void *cache;
} rb_vm_method_t;

#define GetThreadPtr(obj) ((rb_vm_thread_t *)DATA_PTR(obj))

typedef enum {
    THREAD_ALIVE,  // this thread was born to be alive
    THREAD_SLEEP,  // this thread is sleeping
    THREAD_KILLED, // this thread is being killed!
    THREAD_DEAD    // this thread is dead, sigh
} rb_vm_thread_status_t;

#include <pthread.h>

#define pthread_assert(cmd) \
    do { \
	const int code = cmd; \
	if (code != 0) { \
	    printf("pthread command `%s' failed: %s (%d)\n", \
		#cmd, strerror(code), code); \
	    abort(); \
	} \
    } \
    while (0)

typedef struct rb_vm_thread {
    pthread_t thread;
    rb_vm_block_t *body;
    int argc;
    const VALUE *argv;
    void *vm;		// a C++ instance of RoxorVM
    VALUE value;
    pthread_mutex_t sleep_mutex;
    pthread_cond_t sleep_cond;
    rb_vm_thread_status_t status;
    bool in_cond_wait;
    bool abort_on_exception;	// per-local state, global one is in RoxorCore
    bool joined_on_exception;
    bool wait_for_mutex_lock;
    VALUE locals;	// a Hash object or Qnil
    VALUE exception;	// killed-by exception or Qnil 
    VALUE group;	// always a ThreadGroup object
    VALUE mutexes;	// an Array object or Qnil
} rb_vm_thread_t;

static inline rb_vm_arity_t
rb_vm_arity(int argc)
{
    rb_vm_arity_t arity;
    arity.left_req = arity.min = arity.max = arity.real = argc;
    return arity;
}

static inline int
rb_vm_arity_n(rb_vm_arity_t arity)
{
    int n = arity.min;
    if (arity.min != arity.max) {
	n = -n - 1;
    }
    return n;
}

static inline rb_vm_arity_t
rb_vm_node_arity(NODE *node)
{
    const int type = nd_type(node);
    rb_vm_arity_t arity;

    if (type == NODE_SCOPE) {
	NODE *n = node->nd_args;
	short opt_args = 0, req_args = 0;
	bool has_rest = false;
	if (n == NULL) {
	    arity.left_req = 0;
	}
	else {
	    req_args = n->nd_frml;
	    arity.left_req = req_args;
	    NODE *n_opt = n->nd_opt;
	    if (n_opt != NULL) {
		NODE *ni = n_opt;
		while (ni != NULL) {
		    opt_args++;
		    ni = ni->nd_next;
		}
	    }
	    if (n->nd_next != NULL) {
		NODE *rest_node = n->nd_next;
		if (rest_node->nd_rest) {
		    has_rest = true;
		}
		if (rest_node->nd_next) {
		    req_args += rest_node->nd_next->nd_frml;
		}
	    }
	}
	arity.min = req_args;
	if (has_rest) {
	    arity.max = -1;
	    arity.real = req_args + opt_args + 1;
	}
	else {
	    arity.max = arity.real = req_args + opt_args;
	}
	return arity;
    }

    if (type == NODE_FBODY) {
	assert(node->nd_body != NULL);
	assert(node->nd_body->nd_body != NULL);
	int argc = node->nd_body->nd_body->nd_argc;
	if (argc >= 0) {
	    arity.left_req = arity.real = arity.min = arity.max = argc;
	}
	else {
	    arity.left_req = arity.min = 0;
	    arity.max = -1;
	    if (argc == -1) {
		arity.real = 2;
	    }
	    else if (argc == -2) {
		arity.real = 1;
	    }
	    else if (argc == -3) {
		arity.real = 3;
	    }
	    else {
		printf("invalid FBODY arity: %d\n", argc);
		abort();
	    }
	}
	return arity; 
    }

    printf("invalid node %p type %d\n", node, type);
    abort();
}

static inline NODE *
rb_vm_cfunc_node_from_imp(Class klass, int arity, IMP imp, int noex)
{
    NODE *node = NEW_CFUNC(imp, arity);
    return NEW_FBODY(NEW_METHOD(node, klass, noex), 0);
}

VALUE rb_vm_eval_string(VALUE self, VALUE klass, VALUE src,
	rb_vm_binding_t *binding, const char *file, const int line,
	bool should_push_outer);
VALUE rb_vm_run(const char *fname, NODE *node, rb_vm_binding_t *binding,
	bool inside_eval);
VALUE rb_vm_run_under(VALUE klass, VALUE self, const char *fname, NODE *node,
	rb_vm_binding_t *binding, bool inside_eval, bool should_push_outer);
void rb_vm_aot_compile(NODE *node);

void rb_vm_init_compiler(void);
void rb_vm_init_jit(void);

bool rb_vm_running(void);
void rb_vm_set_running(bool flag);
VALUE rb_vm_default_random(void);
void rb_vm_set_default_random(VALUE rand);
bool rb_vm_parse_in_eval(void);
void rb_vm_set_parse_in_eval(bool flag);
VALUE rb_vm_load_path(void);
VALUE rb_vm_loaded_features(void);
VALUE rb_vm_trap_cmd_for_signal(int signal);
int rb_vm_trap_level_for_signal(int signal);
void rb_vm_set_trap_for_signal(VALUE trap, int level, int signal);
int rb_vm_safe_level(void);
void rb_vm_set_safe_level(int level);
int rb_vm_thread_safe_level(rb_vm_thread_t *thread);
VALUE rb_vm_top_self(void);
void rb_vm_const_is_defined(ID path);
VALUE rb_vm_resolve_const_value(VALUE val, VALUE klass, ID name);

VALUE rb_vm_const_lookup_level(VALUE outer, ID path, bool lexical,
	bool defined, rb_vm_outer_t *outer_stack);
static inline VALUE
rb_vm_const_lookup(VALUE outer, ID path, bool lexical, bool defined, rb_vm_outer_t *outer_stack)
{
    return rb_vm_const_lookup_level(outer, path, lexical, defined, outer_stack);
}

bool rb_vm_lookup_method(Class klass, SEL sel, IMP *pimp,
	rb_vm_method_node_t **pnode);
bool rb_vm_lookup_method2(Class klass, ID mid, SEL *psel, IMP *pimp,
	rb_vm_method_node_t **pnode);
bool rb_vm_is_ruby_method(Method m);
rb_vm_method_node_t *rb_vm_define_method(Class klass, SEL sel, IMP imp,
	NODE *node, bool direct);
rb_vm_method_node_t *rb_vm_define_method2(Class klass, SEL sel,
	rb_vm_method_node_t *node, long flags, bool direct);
void rb_vm_define_method3(Class klass, ID mid, rb_vm_block_t *node);
bool rb_vm_resolve_method(Class klass, SEL sel);
void *rb_vm_undefined_imp(void *rcv, SEL sel);
void *rb_vm_removed_imp(void *rcv, SEL sel);
#define UNAVAILABLE_IMP(imp) \
    (imp == NULL || imp == (IMP)rb_vm_undefined_imp \
     || imp == (IMP)rb_vm_removed_imp)
void rb_vm_define_attr(Class klass, const char *name, bool read, bool write);
void rb_vm_undef_method(Class klass, ID name, bool must_exist);
void rb_vm_remove_method(Class klass, ID name);
void rb_vm_alias(VALUE klass, ID name, ID def);
bool rb_vm_copy_method(Class klass, Method method);
void rb_vm_copy_methods(Class from_class, Class to_class);
VALUE rb_vm_yield_under(VALUE klass, VALUE self, int argc, const VALUE *argv);
bool rb_vm_respond_to(VALUE obj, SEL sel, bool priv);
bool rb_vm_respond_to2(VALUE obj, VALUE klass, SEL sel, bool priv, bool check_override);
VALUE rb_vm_method_missing(VALUE obj, int argc, const VALUE *argv);
void rb_vm_push_methods(VALUE ary, VALUE mod, bool include_objc_methods,
	int (*filter) (VALUE, ID, VALUE));
VALUE rb_vm_module_nesting(void);
VALUE rb_vm_module_constants(void);
VALUE rb_vm_catch(VALUE tag);
VALUE rb_vm_throw(VALUE tag, VALUE value);

void rb_vm_dispose_class(Class k);

typedef struct {
    ID name;
    VALUE value; 
} rb_object_ivar_slot_t;

#define SLOT_CACHE_VIRGIN	-2
#define SLOT_CACHE_CANNOT	-1

#define RB_OBJECT_DEFAULT_NUM_SLOTS	4

typedef struct {
    struct RBasic basic;
    rb_object_ivar_slot_t *slots;
    unsigned int num_slots;
} rb_object_t;

#define ROBJECT(o) ((rb_object_t *)o)

static inline void
rb_vm_regrow_robject_slots(rb_object_t *obj, unsigned int new_num_slot)
{
    rb_object_ivar_slot_t *new_slots =
	(rb_object_ivar_slot_t *)xrealloc(obj->slots,
		sizeof(rb_object_ivar_slot_t) * (new_num_slot + 1));
    if (new_slots != obj->slots) {
	GC_WB(&obj->slots, new_slots);
    }

    unsigned int i;
    for (i = obj->num_slots; i <= new_num_slot; i++) {
	obj->slots[i].name = 0;
	obj->slots[i].value = Qundef;
    }
    obj->num_slots = new_num_slot + 1;
}

int rb_vm_get_ivar_slot(VALUE obj, ID name, bool create);

static inline VALUE
rb_vm_get_ivar_from_slot(VALUE obj, int slot) 
{
    rb_object_t *robj = ROBJECT(obj);
    return robj->slots[slot].value;
}

static inline void
rb_vm_set_ivar_from_slot(VALUE obj, VALUE val, int slot) 
{
    rb_object_t *robj = ROBJECT(obj);
    GC_WB(&robj->slots[slot].value, val);
}

static inline VALUE
rb_vm_new_rb_object(VALUE klass)
{
    const int num_slots = RB_OBJECT_DEFAULT_NUM_SLOTS;

    rb_object_t *obj = (rb_object_t *)rb_objc_newobj(sizeof(rb_object_t));
    GC_WB(&obj->slots, xmalloc(sizeof(rb_object_ivar_slot_t) * num_slots));

    OBJSETUP(obj, klass, T_OBJECT);
    obj->num_slots = num_slots;

    int i;
    for (i = 0; i < num_slots; i++) {
	obj->slots[i].name = 0;
	obj->slots[i].value = Qundef;
    }
    return (VALUE)obj;
}

// Defined in proc.c
VALUE rb_proc_alloc_with_block(VALUE klass, rb_vm_block_t *proc);

rb_vm_method_t *rb_vm_get_method(VALUE klass, VALUE obj, ID mid, int scope);
rb_vm_block_t *rb_vm_create_block_from_method(rb_vm_method_t *method);
rb_vm_block_t *rb_vm_create_block_calling_mid(ID mid);
VALUE rb_vm_make_curry_proc(VALUE proc, VALUE passed, VALUE arity);

static inline rb_vm_block_t *
rb_proc_get_block(VALUE proc)
{
    return (rb_vm_block_t *)DATA_PTR(proc);
}

void rb_vm_add_block_lvar_use(rb_vm_block_t *block);
rb_vm_block_t *rb_vm_create_block(IMP imp, VALUE self, VALUE userdata);
rb_vm_block_t *rb_vm_current_block(void);
rb_vm_block_t *rb_vm_first_block(void);
bool rb_vm_block_saved(void);
VALUE rb_vm_block_eval(rb_vm_block_t *block, int argc, const VALUE *argv);

rb_vm_block_t *rb_vm_uncache_or_dup_block(rb_vm_block_t *b);
rb_vm_block_t *rb_vm_dup_block(rb_vm_block_t *b);

static inline void
rb_vm_block_make_detachable_proc(rb_vm_block_t *b)
{
    if (!(b->flags & VM_BLOCK_PROC)) {
	b->flags |= VM_BLOCK_PROC;
	if (!(b->flags & VM_BLOCK_METHOD)) {
	    rb_vm_add_block_lvar_use(b);
	}
    }
}

rb_vm_binding_t *rb_vm_create_binding(VALUE self, rb_vm_block_t *current_block,
	rb_vm_binding_t *top_binding, rb_vm_outer_t *outer_stack, 
	int lvars_size, va_list lvars, bool vm_push);
rb_vm_binding_t *rb_vm_current_binding(void);
void rb_vm_add_binding(rb_vm_binding_t *binding);
void rb_vm_pop_binding();
VALUE rb_binding_new_from_binding(rb_vm_binding_t *binding);

void rb_vm_thread_pre_init(rb_vm_thread_t *t, rb_vm_block_t *body, int argc,
	const VALUE *argv, void *vm);
void *rb_vm_create_vm(void);
void rb_vm_register_thread(VALUE thread);
void *rb_vm_thread_run(VALUE thread);
VALUE rb_vm_current_thread(void);
VALUE rb_vm_main_thread(void);
VALUE rb_vm_threads(void);
VALUE rb_vm_thread_locals(VALUE thread, bool create_storage);
void rb_vm_thread_wakeup(rb_vm_thread_t *t);
void rb_vm_thread_cancel(rb_vm_thread_t *t);
void rb_vm_thread_raise(rb_vm_thread_t *t, VALUE exc);

void rb_vm_register_current_alien_thread(void);
void rb_vm_unregister_current_alien_thread(void);

bool rb_vm_abort_on_exception(void);
void rb_vm_set_abort_on_exception(bool flag);

Class rb_vm_set_current_class(Class klass);
Class rb_vm_get_current_class(void);

rb_vm_outer_t *rb_vm_push_outer(Class klass);
void rb_vm_pop_outer(unsigned char need_release);
rb_vm_outer_t *rb_vm_get_outer_stack(void);
rb_vm_outer_t *rb_vm_set_current_outer(rb_vm_outer_t *outer);

bool rb_vm_aot_feature_load(const char *name);
void rb_vm_dln_load(void (*init_fct)(void), IMP __mrep__);
void rb_vm_load(const char *fname_str, int wrap);

bool rb_vm_generate_objc_class_name(const char *name, char *buf,
	size_t buflen);

void rb_vm_raise(VALUE exception);
void rb_vm_raise_current_exception(void);
VALUE rb_vm_current_exception(void);
void rb_vm_set_current_exception(VALUE exception);
VALUE rb_vm_backtrace(int skip);
void rb_vm_print_exception(VALUE exc);
void rb_vm_print_current_exception(void);

#define TEST_THREAD_CANCEL() (pthread_testcancel())

VALUE rb_vm_get_broken_value(void *vm);
VALUE rb_vm_returned_from_block(void *_vm, int id);

#define BROKEN_VALUE() (rb_vm_get_broken_value(rb_vm_current_vm()))

#define ST_STOP_IF_BROKEN() \
    do { \
	VALUE __v = BROKEN_VALUE(); \
	if (__v != Qundef) { \
	    return ST_STOP; \
	} \
    } \
    while (0)

VALUE rb_vm_pop_broken_value(void);
#define RETURN_IF_BROKEN() \
    do { \
	VALUE __v = rb_vm_pop_broken_value(); \
	if (__v != Qundef) { \
	    return __v; \
	} \
    } \
    while (0)

#define ENSURE_AND_RETURN_IF_BROKEN(code) \
    do { \
        VALUE __v = rb_vm_pop_broken_value(); \
        if (__v != Qundef) { \
	    code; \
            return __v; \
        } \
    } \
    while (0)

static inline void
rb_vm_release_ownership(VALUE obj)
{
    if (!SPECIAL_CONST_P(obj)) {
	// This function allows the given object's ownership to be transfered
	// to the current thread. It is used when objects are allocated from a
	// thread but assigned into another thread's stack, which is prohibited
	// by the thread-local collector.
	GC_RETAIN(obj);
	GC_RELEASE(obj);
    }
}

void rb_vm_finalize(void);

void rb_vm_load_bridge_support(const char *path, const char *framework_path,
	int options);

typedef struct {
    VALUE klass;
    VALUE objid;
    VALUE finalizers;
} rb_vm_finalizer_t;

void rb_vm_register_finalizer(rb_vm_finalizer_t *finalizer);
void rb_vm_unregister_finalizer(rb_vm_finalizer_t *finalizer);
void rb_vm_call_finalizer(rb_vm_finalizer_t *finalizer);

struct icache {
    VALUE klass;
    int slot;
};

struct icache *rb_vm_ivar_slot_allocate(void);

struct ccache {
    VALUE outer;
    rb_vm_outer_t *outer_stack;
    VALUE val;
};

typedef VALUE rb_vm_objc_stub_t(IMP imp, id self, SEL sel, int argc,
	const VALUE *argv);
typedef VALUE rb_vm_c_stub_t(IMP imp, int argc, const VALUE *argv);

#include "bridgesupport.h"
#include "compiler.h"

struct mcache {
#define MCACHE_RCALL 0x1 // Ruby call
#define MCACHE_OCALL 0x2 // Objective-C call
#define MCACHE_FCALL 0x4 // C call
#define MCACHE_SUPER 0x8 // Super call (only applied with RCALL or OCALL)
    uint8_t flag;
    SEL sel;
    Class klass;
    union {
	struct {
	    rb_vm_method_node_t *node;
	} rcall;
	struct {
	    IMP imp;
	    int argc;
	    bs_element_method_t *bs_method;	
	    rb_vm_objc_stub_t *stub;
	} ocall;
	struct {
	    IMP imp;
	    bs_element_function_t *bs_function;
	    rb_vm_c_stub_t *stub;
	} fcall;
    } as;
};

#define VM_MCACHE_SIZE	0x1000

VALUE rb_vm_dispatch(void *_vm, struct mcache *cache, VALUE top, VALUE self,
	Class klass, SEL sel, rb_vm_block_t *block, unsigned char opt,
	int argc, const VALUE *argv);

void *rb_vm_current_vm(void) __attribute__((const));
struct mcache *rb_vm_get_mcache(void *vm) __attribute__((const));

static inline int
rb_vm_mcache_hash(Class klass, SEL sel)
{
    return (((unsigned long)klass >> 3) ^ (unsigned long)sel)
	& (VM_MCACHE_SIZE - 1);
}

static inline VALUE
rb_vm_call0(void *vm, VALUE top, VALUE self, Class klass, SEL sel,
	rb_vm_block_t *block, unsigned char opt, int argc, const VALUE *argv)
{
    int hash = rb_vm_mcache_hash(klass, sel);
    if (opt & DISPATCH_SUPER) {
	hash++;
    }
    struct mcache *cache = &rb_vm_get_mcache(vm)[hash];
    return rb_vm_dispatch(vm, cache, top, self, klass, sel, block, opt,
	    argc, argv);
}

static inline VALUE
rb_vm_call(VALUE self, SEL sel, int argc, const VALUE *argv)
{
    return rb_vm_call0(rb_vm_current_vm(), 0, self, (Class)CLASS_OF(self), sel,
	    NULL, DISPATCH_FCALL, argc, argv);
}

static inline VALUE
rb_vm_call_super(VALUE self, SEL sel, int argc, const VALUE *argv)
{
    rb_vm_block_t *b = rb_vm_current_block();
    return rb_vm_call0(rb_vm_current_vm(), 0, self, (Class)CLASS_OF(self), sel,
	    b, DISPATCH_SUPER, argc, argv);
}

static inline VALUE
rb_vm_call2(rb_vm_block_t *block, VALUE self, VALUE klass, SEL sel, int argc,
	const VALUE *argv)
{
    if (klass == 0) {
	klass = CLASS_OF(self);
    }
    return rb_vm_call0(rb_vm_current_vm(), 0, self, (Class)klass, sel, block,
	    DISPATCH_FCALL, argc, argv);
}

static inline VALUE
rb_vm_method_call(rb_vm_method_t *m, rb_vm_block_t *block, int argc,
	const VALUE *argv)
{
    return rb_vm_dispatch(rb_vm_current_vm(), (struct mcache *)m->cache, 0,
	    m->recv, (Class)m->oclass, m->sel, block, DISPATCH_FCALL,
	    argc, argv);
}

static inline VALUE
rb_vm_check_call(VALUE self, SEL sel, int argc, const VALUE *argv)
{
    if (!rb_vm_respond_to(self, sel, true)) {
	return Qundef;
    }
    return rb_vm_call(self, sel, argc, argv);
}

VALUE rb_vm_yield_args(void *vm, int argc, const VALUE *argv);

static inline VALUE
rb_vm_yield(int argc, const VALUE *argv)
{
    return rb_vm_yield_args(rb_vm_current_vm(), argc, argv);
}

#if defined(__cplusplus)
}

#if !defined(MACRUBY_STATIC)
typedef struct {
    Function *func;
    rb_vm_arity_t arity;
    int flags;
} rb_vm_method_source_t;
#endif

#define rb_vm_long_arity_stub_t rb_vm_objc_stub_t
typedef VALUE rb_vm_long_arity_bstub_t(IMP imp, id self, SEL sel,
	VALUE dvars, rb_vm_block_t *b, int argc, const VALUE *argv);

// For rb_vm_define_class()
#define DEFINE_MODULE		0x1
#define DEFINE_OUTER 		0x2
#define DEFINE_SUB_OUTER	0x4

class RoxorCompiler;
class RoxorJITManager;

#define READER(name, type) \
    type get_##name(void) { return name; }

#define WRITER(name, type) \
    void set_##name(type v) { name = v; }

#define ACCESSOR(name, type) \
    READER(name, type) \
    WRITER(name, type)

// The Core class is a singleton, it's only created once and it's used by the
// VMs. All calls to the Core are thread-safe, they acquire a shared lock.
class RoxorCore {
    public:
	static RoxorCore *shared;

    private:
	// LLVM objects.
#if !defined(MACRUBY_STATIC)
	RoxorJITManager *jmm;
	ExecutionEngine *ee;
	FunctionPassManager *fpm;
#endif

	// Running threads.
	VALUE threads;

	// Finalizers. They are automatically called during garbage collection
	// but we still need to keep a list of them, because the list may not
	// be empty when we exit and we need to call the remaining finalizers.
	std::vector<rb_vm_finalizer_t *> finalizers;

	// The global lock.
	pthread_mutex_t gl;

	// State.
#if !defined(MACRUBY_STATIC)
	CodeGenOpt::Level opt_level;
#endif
	bool interpreter_enabled;
	bool running;
	bool abort_on_exception;
	VALUE loaded_features;
	VALUE load_path;
	VALUE default_random;

	// Signals.
	std::map<int, VALUE> trap_cmd;
	std::map<int, int> trap_level;

#if !defined(MACRUBY_STATIC)
	// Cache to avoid compiling the same Function twice.
	std::map<Function *, IMP> JITcache;
#endif

	// Cache to identify pure Ruby implementations / methods.
	std::map<IMP, rb_vm_method_node_t *> ruby_imps;
	std::map<Method, rb_vm_method_node_t *> ruby_methods;

	// Constants cache.
	std::map<ID, struct ccache *> ccache;

	// Outers map (where a class is actually defined).
	std::map<Class, struct rb_vm_outer *> outers;

#if !defined(MACRUBY_STATIC)
	// Optimized selectors redefinition cache.
	std::map<SEL, GlobalVariable *> redefined_ops_gvars;

	// Caches for the lazy JIT.
	std::map<SEL, std::map<Class, rb_vm_method_source_t *> *>
	    method_sources;
	std::multimap<Class, SEL> method_source_sels;
#endif

	// Maps to cache compiled stubs for a given Objective-C runtime type.
	std::map<std::string, void *> c_stubs, objc_stubs,
	    to_rval_convertors, to_ocval_convertors;
#if !defined(MACRUBY_STATIC)
	std::map<IMP, IMP> objc_to_ruby_stubs;
#endif
	std::map<int, void *> rb_large_arity_rstubs; // Large arity Ruby calls
	std::map<int, void *> rb_large_arity_bstubs; // Large arity block calls

	// BridgeSupport caches.
	bs_parser_t *bs_parser;
	std::map<std::string, rb_vm_bs_boxed_t *> bs_boxed;
	std::map<std::string, bs_element_function_t *> bs_funcs;
	std::map<ID, bs_element_constant_t *> bs_consts;
	std::map<std::string, std::map<SEL, bs_element_method_t *> *>
	    bs_classes_class_methods, bs_classes_instance_methods;
	std::map<std::string, bs_element_cftype_t *> bs_cftypes;
	std::map<SEL, std::string *> bs_informal_protocol_imethods,
	    bs_informal_protocol_cmethods;

	// respond_to? cache.
#define RESPOND_TO_NOT_EXIST	0
#define RESPOND_TO_PUBLIC 	1
#define RESPOND_TO_PRIVATE 	2
	std::map<long, int> respond_to_cache;

#if ROXOR_VM_DEBUG
	long functions_compiled;
#endif

    public:
	RoxorCore(void);
	~RoxorCore(void);

	void prepare_jit(void);

	ACCESSOR(running, bool);
	ACCESSOR(abort_on_exception, bool);
	ACCESSOR(default_random, VALUE);
	READER(interpreter_enabled, bool);
	READER(loaded_features, VALUE);
	READER(load_path, VALUE);
	READER(threads, VALUE);

#if ROXOR_VM_DEBUG
	READER(functions_compiled, long);
#endif

	// signals
	void set_trap_for_signal(VALUE trap, int level, int signal);
	VALUE trap_cmd_for_signal(int signal);
	int trap_level_for_signal(int signal);

	void lock(void) { 
	    assert(pthread_mutex_lock(&gl) == 0);
	}
	void unlock(void) {
	    assert(pthread_mutex_unlock(&gl) == 0);
	}

	void register_thread(VALUE thread);
	void unregister_thread(VALUE thread);

#if !defined(MACRUBY_STATIC)
	void optimize(Function *func);
	IMP compile(Function *func, bool optimize=true);
	void delenda(Function *func);

	void load_bridge_support(const char *path, const char *framework_path,
		int options);
#endif

	bs_element_constant_t *find_bs_const(ID name);
	bs_element_method_t *find_bs_method(Class klass, SEL sel);
	rb_vm_bs_boxed_t *find_bs_boxed(std::string type);
	rb_vm_bs_boxed_t *find_bs_struct(std::string type);
	rb_vm_bs_boxed_t *find_bs_opaque(std::string type);
	bs_element_cftype_t *find_bs_cftype(std::string type);
	std::string *find_bs_informal_protocol_method(SEL sel,
		bool class_method);
	bs_element_function_t *find_bs_function(std::string &name);

	// This callback is public for the only reason it's called by C.
	void bs_parse_cb(bs_element_type_t type, void *value, void *ctx);

	void insert_stub(const char *types, void *stub, bool is_objc) {
	    std::map<std::string, void *> &m =
		is_objc ? objc_stubs : c_stubs;
	    m.insert(std::make_pair(types, stub));
	}

	void *gen_large_arity_stub(int argc, bool is_block=false);
	void *gen_stub(std::string types, bool variadic, int min_argc,
		bool is_objc);
	void *gen_to_rval_convertor(std::string type);
	void *gen_to_ocval_convertor(std::string type);

#if !defined(MACRUBY_STATIC)
	std::map<Class, rb_vm_method_source_t *> *
	method_sources_for_sel(SEL sel, bool create) {
	    std::map<SEL, std::map<Class, rb_vm_method_source_t *> *>::iterator
		iter = method_sources.find(sel);
		
	    std::map<Class, rb_vm_method_source_t *> *map = NULL;
	    if (iter == method_sources.end()) {
		if (!create) {
		    return NULL;
		}
		map = new std::map<Class, rb_vm_method_source_t *>();
		method_sources[sel] = map;
	    }
	    else {
		map = iter->second;
	    }
	    return map;
	}
#endif

	bool symbolize_call_address(void *addr, char *path, size_t path_len,
		unsigned long *ln, char *name, size_t name_len,
		unsigned int *interpreter_frame_idx);

	void symbolize_backtrace_entry(int index, char *path, size_t path_len, 
		unsigned long *ln, char *name, size_t name_len);

	void invalidate_method_cache(SEL sel);
	rb_vm_method_node_t *method_node_get(IMP imp, bool create=false);
	rb_vm_method_node_t *method_node_get(Method m, bool create=false);

#if !defined(MACRUBY_STATIC)
	rb_vm_method_source_t *method_source_get(Class klass, SEL sel);

	void prepare_method(Class klass, SEL sel, Function *func,
		const rb_vm_arity_t &arity, int flag);
	bool resolve_methods(std::map<Class, rb_vm_method_source_t *> *map,
		Class klass, SEL sel);
#endif
	rb_vm_method_node_t *resolve_method(Class klass, SEL sel,
		void *func, const rb_vm_arity_t &arity, int flags,
		IMP imp, Method m, void *objc_imp_types);
	rb_vm_method_node_t *add_method(Class klass, SEL sel, IMP imp,
		IMP ruby_imp, const rb_vm_arity_t &arity, int flags,
		const char *types);
	rb_vm_method_node_t *retype_method(Class klass,
		rb_vm_method_node_t *node, const char *old_types,
		const char *new_types);
	void undef_method(Class klass, SEL sel);
	void remove_method(Class klass, SEL sel);
	bool copy_method(Class klass, Method m);
	void copy_methods(Class from_class, Class to_class);
	void get_methods(VALUE ary, Class klass, bool include_objc_methods,
		int (*filter) (VALUE, ID, VALUE));
	void method_added(Class klass, SEL sel);

#if !defined(MACRUBY_STATIC)
	GlobalVariable *redefined_op_gvar(SEL sel, bool create);
#endif
	bool should_invalidate_inline_op(SEL sel, Class klass);

	struct ccache *constant_cache_get(ID path);
	void const_defined(ID path);
	
#if !defined(MACRUBY_STATIC)
	size_t get_sizeof(const Type *type);
	size_t get_sizeof(const char *type);
	bool is_large_struct_type(const Type *type);
#endif

	void register_finalizer(rb_vm_finalizer_t *finalizer);
	void unregister_finalizer(rb_vm_finalizer_t *finalizer);
	void call_all_finalizers(void);

	long respond_to_key(Class klass, SEL sel) {
	    return (long)klass + (long)sel;
	}
	void invalidate_respond_to_cache(void) {
	    respond_to_cache.clear();
	}
	bool respond_to(VALUE obj, VALUE klass, SEL sel, bool priv,
		bool check_override);

	void dispose_class(Class k);

    private:
	bool register_bs_boxed(bs_element_type_t type, void *value);
	void register_bs_class(bs_element_class_t *bs_class);
	rb_vm_bs_boxed_t *register_anonymous_bs_struct(const char *type);
};

#define GET_CORE() (RoxorCore::shared)

typedef struct {
    int nested;
    std::vector<VALUE> current_exceptions;
} rb_vm_catch_t;

typedef enum {
    METHOD_MISSING_DEFAULT = 0,
    METHOD_MISSING_PRIVATE,
    METHOD_MISSING_PROTECTED,
    METHOD_MISSING_VCALL,
    METHOD_MISSING_SUPER
} rb_vm_method_missing_reason_t;

// MacRuby doesn't compile with rtti so we make our own.
#define CATCH_THROW_EXCEPTION		1
#define RETURN_FROM_BLOCK_EXCEPTION	2
#define THREAD_RAISE_EXCEPTION		3

class RoxorSpecialException {
    public:
	int type;

	RoxorSpecialException(int _type) { type = _type; }
};

// Custom C++ exception class used to implement catch/throw.
class RoxorCatchThrowException : public RoxorSpecialException {
    public:
	VALUE throw_symbol;
	VALUE throw_value;

	RoxorCatchThrowException()
	    : RoxorSpecialException(CATCH_THROW_EXCEPTION) {}
};

// Custom C++ exception class used to implement "return-from-block".
class RoxorReturnFromBlockException : public RoxorSpecialException {
    public:
	VALUE val;
	int id;

	RoxorReturnFromBlockException()
	    : RoxorSpecialException(RETURN_FROM_BLOCK_EXCEPTION) {}
};

// Custom C++ exception class used to implement thread cancelation.
class RoxorThreadRaiseException : public RoxorSpecialException {
    public:
	RoxorThreadRaiseException()
	    : RoxorSpecialException(THREAD_RAISE_EXCEPTION) {}
};

// The VM class is instantiated per thread. There is always at least one
// instance. The VM class is purely thread-safe and concurrent, it does not
// acquire any lock, except when it calls the Core.
class RoxorVM {
    public:
	// The main VM object.
	static RoxorVM *main;

	// The pthread specific key to retrieve the current VM thread.
	static pthread_key_t vm_thread_key;

	static force_inline RoxorVM *current(void) {
	    void *vm = pthread_getspecific(vm_thread_key);
	    if (vm == NULL) {
		// The value does not exist yet, which means we are called
		// from a thread that was not created by MacRuby directly
		// (potentially the GC thread or Cocoa). In this case, we
		// create a new VM object just for this thread.
		RoxorVM *new_vm = new RoxorVM();
		new_vm->setup_from_current_thread();
		return new_vm;
	    }
	    return (RoxorVM *)vm;
	}

    private:
	// Cache to avoid allocating the same block twice.
	std::map<void *, rb_vm_block_t *> blocks;

	// Keeps track of the current VM state (blocks, exceptions, bindings).
	std::vector<rb_vm_block_t *> current_blocks;
	std::vector<VALUE> current_exceptions;
	std::vector<rb_vm_binding_t *> bindings;
	std::map<VALUE, rb_vm_catch_t *> catch_nesting;
	std::vector<VALUE> recursive_objects;
        rb_vm_outer_t *outer_stack;
        rb_vm_outer_t *current_outer;

	// Method cache.
	struct mcache *mcache;

	VALUE thread;
	Class current_class;
	VALUE current_top_object;
	VALUE backref;
	VALUE broken_with;
	VALUE last_line;
	VALUE last_status;
	VALUE errinfo;
	int safe_level;
	rb_vm_method_missing_reason_t method_missing_reason;
	bool parse_in_eval;
	bool has_ensure;
	int return_from_block;
	Class current_super_class;
	SEL current_super_sel;
	VALUE current_mri_method_self;
	SEL current_mri_method_sel;

	RoxorSpecialException *special_exc;

	void increase_nesting_for_tag(VALUE tag);
	void decrease_nesting_for_tag(VALUE tag);

    public:
	RoxorVM(void);
	RoxorVM(const RoxorVM &vm);
	~RoxorVM(void);

	ACCESSOR(thread, VALUE);
	ACCESSOR(current_class, Class);
	ACCESSOR(current_top_object, VALUE);
	ACCESSOR(backref, VALUE);
	ACCESSOR(broken_with, VALUE);
	ACCESSOR(last_line, VALUE);
	ACCESSOR(last_status, VALUE);
	ACCESSOR(errinfo, VALUE);
	ACCESSOR(safe_level, int);
	ACCESSOR(method_missing_reason, rb_vm_method_missing_reason_t);
	ACCESSOR(parse_in_eval, bool);
	ACCESSOR(has_ensure, bool);
	ACCESSOR(return_from_block, int);
	ACCESSOR(special_exc, RoxorSpecialException *);
	ACCESSOR(current_super_class, Class);
	ACCESSOR(current_super_sel, SEL);
	READER(mcache, struct mcache *);
	ACCESSOR(current_mri_method_self, VALUE);
	ACCESSOR(current_mri_method_sel, SEL);
	ACCESSOR(outer_stack, rb_vm_outer_t *);
	ACCESSOR(current_outer, rb_vm_outer_t *);

	void debug_blocks(void);

	bool is_block_current(rb_vm_block_t *b) {
	    return b == NULL
		? false
		: current_blocks.empty()
		? false
		: current_blocks.back() == b;
	}

	void add_current_block(rb_vm_block_t *b) {
	    current_blocks.push_back(b);
	}

	void pop_current_block(void) {
	    assert(!current_blocks.empty());
	    current_blocks.pop_back();
	}

	rb_vm_block_t *current_block(void) {
	    return current_blocks.empty()
		? NULL : current_blocks.back();
	}

	rb_vm_block_t *previous_block(void) {
	    if (current_blocks.size() > 1) {
		return current_blocks[current_blocks.size() - 2];
	    }
	    return NULL;
	}

	rb_vm_block_t *first_block(void) {
	    rb_vm_block_t *b = current_block();
	    if (b == NULL) {
		b = previous_block();
	    }
	    return b;
	}

	rb_vm_block_t *uncache_or_create_block(void *key, bool *cached,
		int dvars_size);
	rb_vm_block_t *uncache_or_dup_block(rb_vm_block_t *b);

	rb_vm_binding_t *current_binding(void) {
	    return bindings.empty()
		? NULL : bindings.back();
	}

	rb_vm_binding_t *get_binding(unsigned int index) {
	    if (!bindings.empty()) {
		if (index < bindings.size()) {
		    return bindings[index];
		}
	    }
	    return NULL;
	}

	void push_current_binding(rb_vm_binding_t *binding) {
	    GC_RETAIN(binding);
	    bindings.push_back(binding);
	}

	void pop_current_binding() {
	    if (!bindings.empty()) {
		GC_RELEASE(bindings.back());
		bindings.pop_back();
	    }
	}

	void debug_exceptions(void);

	VALUE current_exception(void) {
	    return current_exceptions.empty()
		? Qnil : current_exceptions.back();
	}

	void push_current_exception(VALUE exc);
	void pop_current_exception(int pos=0);

	VALUE *get_binding_lvar(ID name, bool create);

	VALUE ruby_catch(VALUE tag);
	VALUE ruby_throw(VALUE tag, VALUE value);

	VALUE pop_broken_with(void) {
	    VALUE val = broken_with;
	    if (return_from_block == -1) {
		GC_RELEASE(val);
		broken_with = Qundef;
	    }
	    return val;
	}

	void setup_from_current_thread(void);

	void remove_recursive_object(VALUE obj);
	VALUE exec_recursive(VALUE (*func) (VALUE, VALUE, int), VALUE obj,
		VALUE arg, int outer);

        rb_vm_outer_t *push_outer(Class klass);
        void pop_outer(bool need_release = false);
};

#define GET_VM() (RoxorVM::current())
#define GET_THREAD() (GetThreadPtr(GET_VM()->get_thread()))

#endif /* __cplusplus */

#define not_implemented_in_static(s) \
    rb_raise(rb_eRuntimeError, "%s: not supported in static compilation", sel_getName(s))

#endif /* __VM_H_ */
