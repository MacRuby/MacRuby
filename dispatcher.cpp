/*
 * MacRuby Dispatcher.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2008-2011, Apple Inc. All rights reserved.
 */

#include "llvm.h"
#include "macruby_internal.h"
#include "ruby/node.h"
#include "id.h"
#include "vm.h"
#include "compiler.h"
#include "objc.h"
#include "dtrace.h"
#include "class.h"

#include <execinfo.h>
#include <dlfcn.h>

#define ROXOR_VM_DEBUG		0
#define MAX_DISPATCH_ARGS 	100

static force_inline void
vm_fix_args(const VALUE *argv, VALUE *new_argv, const rb_vm_arity_t &arity,
	int argc)
{
    assert(argc >= arity.min);
    assert((arity.max == -1) || (argc <= arity.max));
    const int used_opt_args = argc - arity.min;
    int opt_args, rest_pos;
    if (arity.max == -1) {
	opt_args = arity.real - arity.min - 1;
	rest_pos = arity.left_req + opt_args;
    }
    else {
	opt_args = arity.real - arity.min;
	rest_pos = -1;
    }
    for (int i = 0; i < arity.real; ++i) {
	if (i < arity.left_req) {
	    // required args before optional args
	    new_argv[i] = argv[i];
	}
	else if (i < arity.left_req + opt_args) {
	    // optional args
	    const int opt_arg_index = i - arity.left_req;
	    if (opt_arg_index >= used_opt_args) {
		new_argv[i] = Qundef;
	    }
	    else {
		new_argv[i] = argv[i];
	    }
	}
	else if (i == rest_pos) {
	    // rest
	    const int rest_size = argc - arity.real + 1;
	    if (rest_size <= 0) {
		new_argv[i] = rb_ary_new();
	    }
	    else {
		new_argv[i] = rb_ary_new4(rest_size, &argv[i]);
	    }
	}
	else {
	    // required args after optional args
	    new_argv[i] = argv[argc-(arity.real - i)];
	}
    }
}

static force_inline VALUE
__rb_vm_bcall(VALUE self, SEL sel, VALUE dvars, rb_vm_block_t *b,
	IMP pimp, const rb_vm_arity_t &arity, int argc, const VALUE *argv)
{
    VALUE buf[100];
    if (arity.real != argc || arity.max == -1) {
	VALUE *new_argv;
	if (arity.real < 100) {
	    new_argv = buf;
	}
	else {
	    new_argv = (VALUE *)xmalloc_ptrs(sizeof(VALUE) * arity.real);
	}
	vm_fix_args(argv, new_argv, arity, argc);
	argv = new_argv;
	argc = arity.real;
    }

    assert(pimp != NULL);

    VALUE (*imp)(VALUE, SEL, VALUE, rb_vm_block_t *,  ...) =
	(VALUE (*)(VALUE, SEL, VALUE, rb_vm_block_t *, ...))pimp;

    switch (argc) {
	case 0:
	    return (*imp)(self, sel, dvars, b);
	case 1:
	    return (*imp)(self, sel, dvars, b, argv[0]);
	case 2:
	    return (*imp)(self, sel, dvars, b, argv[0], argv[1]);
	case 3:
	    return (*imp)(self, sel, dvars, b, argv[0], argv[1], argv[2]);
	case 4:
	    return (*imp)(self, sel, dvars, b, argv[0], argv[1], argv[2],
		    argv[3]);
	case 5:
	    return (*imp)(self, sel, dvars, b, argv[0], argv[1], argv[2],
		    argv[3], argv[4]);
	case 6:
	    return (*imp)(self, sel, dvars, b, argv[0], argv[1], argv[2],
		    argv[3], argv[4], argv[5]);
	case 7:
	    return (*imp)(self, sel, dvars, b, argv[0], argv[1], argv[2],
		    argv[3], argv[4], argv[5], argv[6]);
	case 8:
	    return (*imp)(self, sel, dvars, b, argv[0], argv[1], argv[2],
		    argv[3], argv[4], argv[5], argv[6], argv[7]);
	case 9:
	    return (*imp)(self, sel, dvars, b, argv[0], argv[1], argv[2],
		    argv[3], argv[4], argv[5], argv[6], argv[7], argv[8]);
    }

#if MACRUBY_STATIC
    rb_raise(rb_eRuntimeError,
	    "MacRuby static doesn't support passing more than 9 arguments");
#else
    rb_vm_long_arity_bstub_t *stub = (rb_vm_long_arity_bstub_t *)
	GET_CORE()->gen_large_arity_stub(argc, true);
    return (*stub)(pimp, (id)self, sel, dvars, b, argc, argv);
#endif
}

static force_inline VALUE
__rb_vm_rcall(VALUE self, SEL sel, IMP pimp, const rb_vm_arity_t &arity,
	int argc, const VALUE *argv)
{
    VALUE buf[100];
    if (arity.real != argc || arity.max == -1) {
	VALUE *new_argv;
	if (arity.real < 100) {
	    new_argv = buf;
	}
	else {
	    new_argv = (VALUE *)xmalloc_ptrs(sizeof(VALUE) * arity.real);
	}
	vm_fix_args(argv, new_argv, arity, argc);
	argv = new_argv;
	argc = arity.real;
    }

    assert(pimp != NULL);

    VALUE (*imp)(VALUE, SEL, ...) = (VALUE (*)(VALUE, SEL, ...))pimp;

    switch (argc) {
	case 0:
	    return (*imp)(self, sel);
	case 1:
	    return (*imp)(self, sel, argv[0]);
	case 2:
	    return (*imp)(self, sel, argv[0], argv[1]);		
	case 3:
	    return (*imp)(self, sel, argv[0], argv[1], argv[2]);
	case 4:
	    return (*imp)(self, sel, argv[0], argv[1], argv[2], argv[3]);
	case 5:
	    return (*imp)(self, sel, argv[0], argv[1], argv[2], argv[3],
		    argv[4]);
	case 6:
	    return (*imp)(self, sel, argv[0], argv[1], argv[2], argv[3],
		    argv[4], argv[5]);
	case 7:
	    return (*imp)(self, sel, argv[0], argv[1], argv[2], argv[3],
		    argv[4], argv[5], argv[6]);
	case 8:
	    return (*imp)(self, sel, argv[0], argv[1], argv[2], argv[3],
		    argv[4], argv[5], argv[6], argv[7]);
	case 9:
	    return (*imp)(self, sel, argv[0], argv[1], argv[2], argv[3],
		    argv[4], argv[5], argv[6], argv[7], argv[8]);
	case 10:
	    return (*imp)(self, sel, argv[0], argv[1], argv[2], argv[3],
		    argv[4], argv[5], argv[6], argv[7], argv[8], argv[9]);
	case 11:
	    return (*imp)(self, sel, argv[0], argv[1], argv[2], argv[3],
		    argv[4], argv[5], argv[6], argv[7], argv[8], argv[9],
		    argv[10]);
    }

#if MACRUBY_STATIC
    rb_raise(rb_eRuntimeError,
	    "MacRuby static doesn't support passing more than 9 arguments");
#else
    rb_vm_long_arity_stub_t *stub = (rb_vm_long_arity_stub_t *)
	GET_CORE()->gen_large_arity_stub(argc);
    return (*stub)(pimp, (id)self, sel, argc, argv);
#endif
}

static void
vm_gen_bs_func_types(int argc, const VALUE *argv,
	bs_element_function_t *bs_func, std::string &types)
{
    types.append(bs_func->retval == NULL ? "v" : bs_func->retval->type);
    int printf_arg = -1;
    for (int i = 0; i < (int)bs_func->args_count; i++) {
	types.append(bs_func->args[i].type);
	if (bs_func->args[i].printf_format) {
	    printf_arg = i;
	}
    }
    if (bs_func->variadic) {
	// TODO honor printf_format
//	if (printf_arg != -1) {	    
//	}
	for (int i = bs_func->args_count; i < argc; i++) {
	    types.append("@");
	}
    }
}

static SEL
helper_sel(const char *p, size_t len)
{
    SEL new_sel = 0;
    char buf[100];

    // Avoid buffer overflow
    // len + "sel" + ':' + '\0'
    if ((len + 5) > sizeof(buf)) {
	return (SEL)0;
    }

    if (len >= 3 && isalpha(p[len - 3]) && p[len - 2] == '='
	&& p[len - 1] == ':') {

	// foo=: -> setFoo: shortcut
	snprintf(buf, sizeof buf, "set%s", p);
	buf[3] = toupper(buf[3]);
	buf[len + 1] = ':';
	buf[len + 2] = '\0';
	new_sel = sel_registerName(buf);
    }
    else if (len > 1 && p[len - 1] == '?') {
	// foo?: -> isFoo: shortcut
	snprintf(buf, sizeof buf, "is%s", p);
	buf[2] = toupper(buf[2]);
	buf[len + 1] = '\0';
	new_sel = sel_registerName(buf);
    }
    else if (strcmp(p, "[]:") == 0) {
	// []: -> objectForKey: shortcut
	new_sel = selObjectForKey;
    }
    else if (strcmp(p, "[]=:") == 0) {
	// []=: -> setObjectForKey: shortcut
	new_sel = selSetObjectForKey;
    }

    return new_sel;
}

static Method
rb_vm_super_lookup(Class klass, SEL sel, Class *super_class_p)
{
    // Locate the current method implementation.
    Class self_class = klass;
    Method method = class_getInstanceMethod(self_class, sel);
    if (method == NULL) {
	// The given selector does not exist, let's go through
	// #method_missing...
	*super_class_p = NULL;
	return NULL; 
    }
    IMP self_imp = method_getImplementation(method);

    // Iterate over ancestors, locate the current class and return the
    // super method, if it exists.
    VALUE ary = rb_mod_ancestors_nocopy((VALUE)klass);
    const int count = RARRAY_LEN(ary);
    bool klass_located = false;
#if ROXOR_VM_DEBUG
    printf("locating super method %s of class %s (%p) in ancestor chain: ", 
	    sel_getName(sel), rb_class2name((VALUE)klass), klass);
    for (int i = 0; i < count; i++) {
	VALUE sk = RARRAY_AT(ary, i);
	printf("%s (%p) ", rb_class2name(sk), (void *)sk);
    }
    printf("\n");
#endif
try_again:
    for (int i = 0; i < count; i++) {
        if (!klass_located && RARRAY_AT(ary, i) == (VALUE)self_class) {
            klass_located = true;
        }
        if (klass_located) {
            if (i < count - 1) {
                VALUE k = RARRAY_AT(ary, i + 1);
#if ROXOR_VM_DEBUG
		printf("looking in %s\n", rb_class2name((VALUE)k));
#endif

		Method method = class_getInstanceMethod((Class)k, sel);
		if (method == NULL) {
		    continue;
		}

		IMP imp = method_getImplementation(method);
		if (imp == self_imp || UNAVAILABLE_IMP(imp)) {
		    continue;
		}

		VALUE super = RCLASS_SUPER(k);
		if (super != 0 && class_getInstanceMethod((Class)super,
			    sel) == method) {
		    continue;
		}

#if ROXOR_VM_DEBUG
		printf("returning method %p of class %s (#%d)\n",
			method, rb_class2name(k), i + 1);
#endif

		*super_class_p = (Class)k;
		return method;
            }
        }
    }
    if (!klass_located) {
	// Could not locate the receiver's class in the ancestors list.
	// It probably means that the receiver has been extended somehow.
	// We therefore assume that the super method will be in the direct
	// superclass.
	klass_located = true;
	goto try_again;
    }

    *super_class_p = NULL;
    return NULL;
}

static VALUE
method_missing(VALUE obj, SEL sel, rb_vm_block_t *block, int argc,
	const VALUE *argv, rb_vm_method_missing_reason_t call_status)
{
    if (sel == selAlloc) {
        rb_raise(rb_eTypeError, "allocator undefined for %s",
		RSTRING_PTR(rb_inspect(obj)));
    }

    GET_VM()->set_method_missing_reason(call_status);

    VALUE *new_argv = (VALUE *)xmalloc_ptrs(sizeof(VALUE) * (argc + 1));

    char buf[100];
    int n = snprintf(buf, sizeof buf, "%s", sel_getName(sel));
    if (buf[n - 1] == ':') {
	// Let's see if there are more colons making this a real selector.
	bool multiple_colons = false;
	for (int i = 0; i < (n - 1); i++) {
	    if (buf[i] == ':') {
		multiple_colons = true;
		break;
	    }
	}
	if (!multiple_colons) {
	    // Not a typical multiple argument selector. So as this is
	    // probably a typical ruby method name, chop off the colon.
	    buf[n - 1] = '\0';
	}
    }
    new_argv[0] = ID2SYM(rb_intern(buf));
    MEMCPY(&new_argv[1], argv, VALUE, argc);

    // In case the missing selector _is_ method_missing: OR the object does
    // not respond to method_missing: (this can happen for NSProxy-based
    // objects), directly trigger the exception.
    Class k = (Class)CLASS_OF(obj);
    if (sel == selMethodMissing
	    || class_getInstanceMethod(k, selMethodMissing) == NULL) {
	rb_vm_method_missing(obj, argc + 1, new_argv);
	return Qnil; // never reached
    }
    else {
	return rb_vm_call2(block, obj, (VALUE)k, selMethodMissing, argc + 1,
		new_argv);
    }
}

extern "C"
void *
rb_vm_undefined_imp(void *rcv, SEL sel)
{
    method_missing((VALUE)rcv, sel, NULL, 0, NULL, METHOD_MISSING_DEFAULT);
    return NULL; // never reached
}

extern "C"
void *
rb_vm_removed_imp(void *rcv, SEL sel)
{
    method_missing((VALUE)rcv, sel, NULL, 0, NULL, METHOD_MISSING_DEFAULT);
    return NULL; // never reached
}

static force_inline VALUE
ruby_dispatch(VALUE top, VALUE self, SEL sel, rb_vm_method_node_t *node,
	unsigned char opt, int argc, const VALUE *argv)
{
    const rb_vm_arity_t &arity = node->arity;
    if ((argc < arity.min) || ((arity.max != -1) && (argc > arity.max))) {
	short limit = (argc < arity.min) ? arity.min : arity.max;
	rb_raise(rb_eArgError, "wrong number of arguments (%d for %d)",
		argc, limit);
    }

    if ((node->flags & VM_METHOD_PRIVATE) && opt == 0) {
	// Calling a private method with no explicit receiver OR an attribute
	// assignment to non-self, triggering #method_missing.
	rb_vm_block_t *b = GET_VM()->current_block();
	return method_missing(self, sel, b, argc, argv,
		METHOD_MISSING_PRIVATE);
    }

    if ((node->flags & VM_METHOD_PROTECTED)
	    && top != 0 && node->klass != NULL
	    && !rb_obj_is_kind_of(top, (VALUE)node->klass)) {
	// Calling a protected method inside a method where 'self' is not
	// an instance of the class where the method was originally defined,
	// triggering #method_missing.
	rb_vm_block_t *b = GET_VM()->current_block();
	return method_missing(self, sel, b, argc, argv,
		METHOD_MISSING_PROTECTED);
    }

    if ((node->flags & VM_METHOD_EMPTY) && arity.max == arity.min) {
	// Calling an empty method, let's just return nil!
	return Qnil;
    }

    if ((node->flags & VM_METHOD_FBODY) && arity.max != arity.min) {
	// Calling a function defined with rb_objc_define_method with
	// a negative arity, which means a different calling convention.
	if (arity.real == 2) {
	    return ((VALUE (*)(VALUE, SEL, int, const VALUE *))node->ruby_imp)
		(self, sel, argc, argv);
	}
	else if (arity.real == 1) {
	    return ((VALUE (*)(VALUE, SEL, ...))node->ruby_imp)
		(self, sel, rb_ary_new4(argc, argv));
	}
	else if (arity.real == 3) {
	    return ((VALUE (*)(VALUE, SEL, VALUE, int,
			    const VALUE *))node->ruby_imp)
		(self, sel, top, argc, argv);
	}
	else {
	    printf("invalid negative arity for C function %d\n",
		    arity.real);
	    abort();
	}
    }

    return __rb_vm_rcall(self, sel, node->ruby_imp, arity, argc, argv);
}

static
#if __LP64__
// This method can't be inlined in 32-bit because @try compiles as a call
// to setjmp().
force_inline
#endif
VALUE
__rb_vm_objc_dispatch(rb_vm_objc_stub_t *stub, IMP imp, id rcv, SEL sel,
	int argc, const VALUE *argv)
{
    @try {
	return (*stub)(imp, rcv, sel, argc, argv);
    }
    @catch (id exc) {
	bool created = false;
	VALUE rbexc = rb_oc2rb_exception(exc, &created);
#if __LP64__
	if (rb_vm_current_exception() == Qnil) {
	    rb_vm_set_current_exception(rbexc);
	    throw;
	}
#endif
	if (created) {
	    rb_exc_raise(rbexc);
	}
	throw;
    }
    abort(); // never reached
}

static void
fill_rcache(struct mcache *cache, Class klass, SEL sel,
	rb_vm_method_node_t *node)
{
    cache->flag = MCACHE_RCALL;
    cache->sel = sel;
    cache->klass = klass;
    cache->as.rcall.node = node;
}

static void
fill_ocache(struct mcache *cache, VALUE self, Class klass, IMP imp, SEL sel,
	    Method method, int argc)
{
    cache->flag = MCACHE_OCALL;
    cache->sel = sel;
    cache->klass = klass;
    cache->as.ocall.imp = imp;
    cache->as.ocall.argc = argc;
    cache->as.ocall.bs_method = GET_CORE()->find_bs_method(klass, sel);

    char types[200];
    if (!rb_objc_get_types(self, klass, sel, method, cache->as.ocall.bs_method,
		types, sizeof types)) {
	printf("cannot get encoding types for %c[%s %s]\n",
		class_isMetaClass(klass) ? '+' : '-',
		class_getName(klass),
		sel_getName(sel));
	abort();
    }
    bool variadic = false;
    if (cache->as.ocall.bs_method != NULL
	    && cache->as.ocall.bs_method->variadic && method != NULL) {
	// TODO honor printf_format
	const int real_argc = rb_method_getNumberOfArguments(method) - 2;
	if (real_argc < argc) {
	    const size_t s = strlen(types);
	    assert(s + argc - real_argc < sizeof types);
	    for (int i = real_argc; i < argc; i++) {
		strlcat(types, "@", sizeof types);
	    }
	    argc = real_argc;
	}
	variadic = true;
    }
    cache->as.ocall.stub = (rb_vm_objc_stub_t *)GET_CORE()->gen_stub(types,
	    variadic, argc, true);
}

static bool
reinstall_method_maybe(Class klass, SEL sel, const char *types)
{
    Method m = class_getInstanceMethod(klass, sel);
    if (m == NULL) {
	return false;
    }

    rb_vm_method_node_t *node = GET_CORE()->method_node_get(m);
    if (node == NULL) {
	// We only do that for pure Ruby methods.
	return false;
    }

    GET_CORE()->retype_method(klass, node, method_getTypeEncoding(m), types);
    return true;
}

static inline bool
sel_equal(Class klass, SEL x, SEL y)
{
    if (x == y) {
	return true;
    }

    IMP x_imp = class_getMethodImplementation(klass, x);
    IMP y_imp = class_getMethodImplementation(klass, y);
    return x_imp == y_imp;
}

extern "C"
VALUE
rb_vm_dispatch(void *_vm, struct mcache *cache, VALUE top, VALUE self,
	Class klass, SEL sel, rb_vm_block_t *block, unsigned char opt,
	int argc, const VALUE *argv)
{
    RoxorVM *vm = (RoxorVM *)_vm;

#if ROXOR_VM_DEBUG
    bool cached = true;
#endif
    bool cache_method = true;

    Class current_super_class = vm->get_current_super_class();
    SEL current_super_sel = vm->get_current_super_sel();

    if (opt & DISPATCH_SUPER) {
	// TODO
	goto recache;
    }

    if (cache->sel != sel || cache->klass != klass || cache->flag == 0) {
recache:
#if ROXOR_VM_DEBUG
	cached = false;
#endif

	Method method;
	if (opt & DISPATCH_SUPER) {
	    if (!sel_equal(klass, current_super_sel, sel)) {
		const char *selname = sel_getName(sel);
		const size_t selname_len = strlen(selname);
		char buf[100];
		if (argc == 0 && selname[selname_len - 1] == ':') {
		    strlcpy(buf, selname, sizeof buf);
		    buf[selname_len - 1] = '\0';
		    sel = sel_registerName(buf);
		}
		else if (argc > 0 && selname[selname_len - 1] != ':') {
		    snprintf(buf, sizeof buf, "%s:", selname);
		    sel = sel_registerName(buf);
		}
		current_super_sel = sel;
		current_super_class = klass;
	    }
	    else {
		// Let's make sure the current_super_class is valid before
		// using it; we check this by verifying that it's a real
		// super class of the current class, as we may be calling
		// a super method of the same name but on a totally different
		// class hierarchy.
		Class k = klass;
		bool current_super_class_ok = false;
		while (k != NULL) {
		    if (k == current_super_class) {
			current_super_class_ok = true;
			break;
		    }
		    k = class_getSuperclass(k);
		}
		if (!current_super_class_ok) {
		    current_super_class = klass;
		}
	    }
	    method = rb_vm_super_lookup(current_super_class, sel,
		    &current_super_class);
	}
	else {
	    current_super_sel = 0;
	    method = class_getInstanceMethod(klass, sel);
	}

	if (method != NULL) {
recache2:
	    IMP imp = method_getImplementation(method);

	    if (UNAVAILABLE_IMP(imp)) {
		// Method was undefined.
		goto call_method_missing;
	    }

	    rb_vm_method_node_t *node = GET_CORE()->method_node_get(method);

	    if (node != NULL) {
		// ruby call
		fill_rcache(cache, klass, sel, node);
	    }
	    else {
		// objc call
		fill_ocache(cache, self, klass, imp, sel, method, argc);
	    }

	    if (opt & DISPATCH_SUPER) {
		cache->flag |= MCACHE_SUPER;
	    }
	}
	else {
	    // Method is not found...

#if !defined(MACRUBY_STATIC)
	    // Force a method resolving, because the objc cache might be
	    // wrong.
	    if (rb_vm_resolve_method(klass, sel)) {
		goto recache;
	    }
#endif

	    // Does the receiver implements -forwardInvocation:?
	    if ((opt & DISPATCH_SUPER) == 0
		    && rb_objc_supports_forwarding(self, sel)) {

//#if MAC_OS_X_VERSION_MAX_ALLOWED < 1070
		// In earlier versions of the Objective-C runtime, there seems
		// to be a bug where class_getInstanceMethod isn't atomic,
		// and might return NULL while at the exact same time another
		// thread registers the related method.
		// As a work-around, we double-check if the method still does
		// not exist here. If he does, we can dispatch it properly.

		// note: OS X 10.7 also, this workaround is required. see #1476
		method = class_getInstanceMethod(klass, sel);
		if (method != NULL) {
		    goto recache2;
		}
//#endif
		fill_ocache(cache, self, klass, (IMP)objc_msgSend, sel, NULL,
			argc);
		goto dispatch;
	    }

	    // Let's see if are not trying to call a Ruby method that accepts
	    // a regular argument then an optional Hash argument, to be
	    // compatible with the Ruby specification.
	    const char *selname = sel_getName(sel);
	    size_t selname_len = strlen(selname);
	    if (argc > 1) {
		const char *p = strchr(selname, ':');
		if (p != NULL && p + 1 != '\0') {
		    char *tmp = (char *)malloc(selname_len + 1);
		    assert(tmp != NULL);
		    strncpy(tmp, selname, p - selname + 1);
		    tmp[p - selname + 1] = '\0';
		    sel = sel_registerName(tmp);
		    VALUE h = rb_hash_new();
		    bool ok = true;
		    p += 1;
		    for (int i = 1; i < argc; i++) {
			const char *p2 = strchr(p, ':');
			if (p2 == NULL) {
			    ok = false;
			    break;
			}
			strlcpy(tmp, p, selname_len);
			tmp[p2 - p] = '\0';
			p = p2 + 1; 
			rb_hash_aset(h, ID2SYM(rb_intern(tmp)), argv[i]);
		    }
		    free(tmp);
		    tmp = NULL;
		    if (ok) {
			argc = 2;
			((VALUE *)argv)[1] = h; // bad, I know...
			Method m = class_getInstanceMethod(klass, sel);
			if (m != NULL) {	
			    method = m;
			    cache_method = false;
			    goto recache2;
			}
		    }
		}
	    }

	    // Enable helpers for classes which are not RubyObject based.
	    if ((RCLASS_VERSION(klass) & RCLASS_IS_OBJECT_SUBCLASS)
		    != RCLASS_IS_OBJECT_SUBCLASS) {
		// Let's try to see if we are not given a helper selector.
		SEL new_sel = helper_sel(selname, selname_len);
		if (new_sel != NULL) {
		    Method m = class_getInstanceMethod(klass, new_sel);
		    if (m != NULL) {
		    	sel = new_sel;
		    	method = m;
		    	// We need to invert arguments because
		    	// #[]= and setObject:forKey: take arguments
		    	// in a reverse order
		    	if (new_sel == selSetObjectForKey && argc == 2) {
		    	    VALUE swap = argv[0];
		    	    ((VALUE *)argv)[0] = argv[1];
		    	    ((VALUE *)argv)[1] = swap;
		    	    cache_method = false;
		    	}
		    	goto recache2;
		    }
		}
	    }

	    // Let's see if we are not trying to call a BridgeSupport function.
	    if (selname[selname_len - 1] == ':') {
		selname_len--;
	    }
	    std::string name(selname, selname_len);
	    bs_element_function_t *bs_func = GET_CORE()->find_bs_function(name);
	    if (bs_func != NULL) {
		if ((unsigned)argc < bs_func->args_count
			|| ((unsigned)argc > bs_func->args_count
				&& bs_func->variadic == false)) {
		    rb_raise(rb_eArgError, "wrong number of arguments (%d for %d)",
			argc, bs_func->args_count);
		}
		std::string types;
		vm_gen_bs_func_types(argc, argv, bs_func, types);

		cache->flag = MCACHE_FCALL;
		cache->sel = sel;
		cache->klass = klass;
		cache->as.fcall.bs_function = bs_func;
		cache->as.fcall.imp = (IMP)dlsym(RTLD_DEFAULT, bs_func->name);
		assert(cache->as.fcall.imp != NULL);
		cache->as.fcall.stub = (rb_vm_c_stub_t *)GET_CORE()->gen_stub(
			types, bs_func->variadic, bs_func->args_count, false);
	    }
	    else {
		// Still nothing, then let's call #method_missing.
		goto call_method_missing;
	    }
	}
    }

dispatch:
    if (cache->flag & MCACHE_RCALL) {
	if (!cache_method) {
	    cache->flag = 0;
	}

#if ROXOR_VM_DEBUG
	printf("ruby dispatch %c[<%s %p> %s] (imp %p block %p argc %d opt %d cache %p cached %s)\n",
		class_isMetaClass(klass) ? '+' : '-',
		class_getName(klass),
		(void *)self,
		sel_getName(sel),
		cache->as.rcall.node->ruby_imp,
		block,
		argc,
		opt,
		cache,
		cached ? "true" : "false");
#endif

	bool block_already_current = vm->is_block_current(block);
	Class current_klass = vm->get_current_class();
	if (!block_already_current) {
	    vm->add_current_block(block);
	}
	vm->set_current_class(NULL);

	Class old_current_super_class = vm->get_current_super_class();
	vm->set_current_super_class(current_super_class);
	SEL old_current_super_sel = vm->get_current_super_sel();
	vm->set_current_super_sel(current_super_sel);

	const bool should_pop_broken_with =
	    sel != selInitialize && sel != selInitialize2;

	struct Finally {
	    bool block_already_current;
	    Class current_class;
	    Class current_super_class;
	    SEL current_super_sel;
	    bool should_pop_broken_with;
	    RoxorVM *vm;
	    Finally(bool _block_already_current, Class _current_class,
		    Class _current_super_class, SEL _current_super_sel,
		    bool _should_pop_broken_with, RoxorVM *_vm) {
		block_already_current = _block_already_current;
		current_class = _current_class;
		current_super_class = _current_super_class;
		current_super_sel = _current_super_sel;
		should_pop_broken_with = _should_pop_broken_with;
		vm = _vm;
	    }
	    ~Finally() {
		if (!block_already_current) {
		    vm->pop_current_block();
		}
		vm->set_current_class(current_class);
		if (should_pop_broken_with) {
		    vm->pop_broken_with();
		}
		vm->set_current_super_class(current_super_class);
		vm->set_current_super_sel(current_super_sel);
		vm->pop_current_binding();
	    }
	} finalizer(block_already_current, current_klass,
		old_current_super_class, old_current_super_sel,
		should_pop_broken_with, vm);

	// DTrace probe: method__entry
	if (MACRUBY_METHOD_ENTRY_ENABLED()) {
	    char *class_name = (char *)rb_class2name((VALUE)klass);
	    char *method_name = (char *)sel_getName(sel);
	    char file[PATH_MAX];
	    unsigned long line = 0;
	    GET_CORE()->symbolize_backtrace_entry(1, file, sizeof file, &line,
		    NULL, 0);
	    MACRUBY_METHOD_ENTRY(class_name, method_name, file, line);
	}

	VALUE v = ruby_dispatch(top, self, sel, cache->as.rcall.node,
		opt, argc, argv);

	// DTrace probe: method__return
	if (MACRUBY_METHOD_RETURN_ENABLED()) {
	    char *class_name = (char *)rb_class2name((VALUE)klass);
	    char *method_name = (char *)sel_getName(sel);
	    char file[PATH_MAX];
	    unsigned long line = 0;
	    GET_CORE()->symbolize_backtrace_entry(1, file, sizeof file, &line,
		    NULL, 0);
	    MACRUBY_METHOD_RETURN(class_name, method_name, file, line);
	}

	return v;
    }
    else if (cache->flag & MCACHE_OCALL) {
	if (cache->as.ocall.argc != argc) {
	    goto recache;
	}
	if (!cache_method) {
	    cache->flag = 0;
	}

	if (block != NULL) {
	    rb_warn("passing a block to an Objective-C method - " \
		    "will be ignored");
	}
	else if (sel == selNew) {
	    if (self == rb_cNSMutableArray) {
		self = rb_cRubyArray;
	    }
	}
	else if (sel == selClass) {
	    // Because +[NSObject class] returns self.
	    if (RCLASS_META(klass)) {
		return RCLASS_MODULE(self) ? rb_cModule : rb_cClass;
	    }
	    // Because the CF classes should be hidden, for Ruby compat.
	    if (self == Qnil) {
		return rb_cNilClass;
	    }
	    if (self == Qtrue) {
		return rb_cTrueClass;
	    }
	    if (self == Qfalse) {
		return rb_cFalseClass;
	    }
	    return rb_class_real((VALUE)klass, true);
	}

#if ROXOR_VM_DEBUG
	printf("objc dispatch %c[<%s %p> %s] imp=%p cache=%p argc=%d (cached=%s)\n",
		class_isMetaClass(klass) ? '+' : '-',
		class_getName(klass),
		(void *)self,
		sel_getName(sel),
		cache->as.ocall.imp,
		cache,
		argc,
		cached ? "true" : "false");
#endif

	id ocrcv = RB2OC(self);

 	if (cache->as.ocall.bs_method != NULL) {
	    Class ocklass = object_getClass(ocrcv);
	    for (int i = 0; i < (int)cache->as.ocall.bs_method->args_count;
		    i++) {
		bs_element_arg_t *arg = &cache->as.ocall.bs_method->args[i];
		if (arg->sel_of_type != NULL) {
		    // BridgeSupport tells us that this argument contains a
		    // selector of the given type, but we don't have any
		    // information regarding the target. RubyCocoa and the
		    // other ObjC bridges do not really require it since they
		    // use the NSObject message forwarding mechanism, but
		    // MacRuby registers all methods in the runtime.
		    //
		    // Therefore, we apply here a naive heuristic by assuming
		    // that either the receiver or one of the arguments of this
		    // call is the future target.
		    const int arg_i = arg->index;
		    assert(arg_i >= 0 && arg_i < argc);
		    if (argv[arg_i] != Qnil) {
			ID arg_selid = rb_to_id(argv[arg_i]);
			SEL arg_sel = sel_registerName(rb_id2name(arg_selid));

			if (reinstall_method_maybe(ocklass, arg_sel,
				    arg->sel_of_type)) {
			    goto sel_target_found;
			}
			for (int j = 0; j < argc; j++) {
			    if (j != arg_i && !SPECIAL_CONST_P(argv[j])) {
				if (reinstall_method_maybe(*(Class *)argv[j],
					    arg_sel, arg->sel_of_type)) {
				    goto sel_target_found;
				}
			    }
			}
		    }

sel_target_found:
		    // There can only be one sel_of_type argument.
		    break; 
		}
	    }
	}

	return __rb_vm_objc_dispatch(cache->as.ocall.stub, cache->as.ocall.imp,
		ocrcv, sel, argc, argv);
    }
    else if (cache->flag & MCACHE_FCALL) {
#if ROXOR_VM_DEBUG
	printf("C dispatch %s() imp=%p argc=%d (cached=%s)\n",
		cache->as.fcall.bs_function->name,
		cache->as.fcall.imp,
		argc,
		cached ? "true" : "false");
#endif
	return (*cache->as.fcall.stub)(cache->as.fcall.imp, argc, argv);
    }

    printf("method dispatch is b0rked\n");
    abort();

call_method_missing:
    // Before calling method_missing, let's check if we are not in the following
    // cases:
    //
    //    def foo; end; foo(42)
    //    def foo(x); end; foo
    //
    // If yes, we need to raise an ArgumentError exception instead.
    const char *selname = sel_getName(sel);
    const size_t selname_len = strlen(selname);
    SEL new_sel = 0;

    if (argc > 0 && selname[selname_len - 1] == ':') {
	char buf[100];
	assert(sizeof buf > selname_len - 1);
	strlcpy(buf, selname, sizeof buf);
	buf[selname_len - 1] = '\0';
	new_sel = sel_registerName(buf);
    }
    else if (argc == 0) {
	char buf[100];
	snprintf(buf, sizeof buf, "%s:", selname);
	new_sel = sel_registerName(buf);
    }
    if (new_sel != 0) {
	Method m = class_getInstanceMethod(klass, new_sel);
	if (m != NULL) {
	    IMP mimp = method_getImplementation(m);
	    if (!UNAVAILABLE_IMP(mimp)) {
		unsigned expected_argc;
		rb_vm_method_node_t *node = GET_CORE()->method_node_get(m);
		if (node != NULL) {
		    expected_argc = node->arity.min;
		}
		else {
		    expected_argc = rb_method_getNumberOfArguments(m);
		    expected_argc -= 2; // removing receiver and selector
		}
		rb_raise(rb_eArgError, "wrong number of arguments (%d for %d)",
			argc, expected_argc);
	    }
	}
    }

    rb_vm_method_missing_reason_t status;
    if (opt & DISPATCH_VCALL) {
	status = METHOD_MISSING_VCALL;
    }
    else if (opt & DISPATCH_SUPER) {
	status = METHOD_MISSING_SUPER;
    }
    else {
	status = METHOD_MISSING_DEFAULT;
    }
    return method_missing((VALUE)self, sel, block, argc, argv, status);
}

static rb_vm_block_t *
dup_block(rb_vm_block_t *src_b)
{
    const size_t block_size = sizeof(rb_vm_block_t)
	    + (sizeof(VALUE *) * src_b->dvars_size);

    rb_vm_block_t *new_b = (rb_vm_block_t *)xmalloc(block_size);

    memcpy(new_b, src_b, block_size);
    new_b->proc = src_b->proc; // weak
    GC_WB(&new_b->parent_block, src_b->parent_block);
    GC_WB(&new_b->self, src_b->self);
    new_b->flags = src_b->flags & ~VM_BLOCK_ACTIVE;

    rb_vm_local_t *src_l = src_b->locals;
    rb_vm_local_t **new_l = &new_b->locals;
    while (src_l != NULL) {
	GC_WB(new_l, xmalloc(sizeof(rb_vm_local_t)));
	(*new_l)->name = src_l->name;
	(*new_l)->value = src_l->value;

	new_l = &(*new_l)->next;
	src_l = src_l->next;
    }
    *new_l = NULL;

    return new_b;
}

extern "C"
rb_vm_block_t *
rb_vm_uncache_or_dup_block(rb_vm_block_t *b)
{
    return GET_VM()->uncache_or_dup_block(b);
}

extern "C"
rb_vm_block_t *
rb_vm_dup_block(rb_vm_block_t *b)
{
    return dup_block(b);
}

rb_vm_block_t *
RoxorVM::uncache_or_dup_block(rb_vm_block_t *b)
{
    void *key = (void *)b->imp;
    std::map<void *, rb_vm_block_t *>::iterator iter = blocks.find(key);
    if (iter == blocks.end() || iter->second->self != b->self) {
	if (iter != blocks.end()) {
	    GC_RELEASE(iter->second);
	}
	b = dup_block(b);
	GC_RETAIN(b);
	blocks[key] = b;
    }
    else {
	b = iter->second;
    }
    return b;
}

static force_inline VALUE
vm_block_eval(RoxorVM *vm, rb_vm_block_t *b, SEL sel, VALUE self,
	int argc, const VALUE *argv)
{
    if ((b->flags & VM_BLOCK_IFUNC) == VM_BLOCK_IFUNC) {
	// Special case for blocks passed with rb_objc_block_call(), to
	// preserve API compatibility.
	VALUE (*pimp)(VALUE, VALUE, int, const VALUE *) =
	    (VALUE (*)(VALUE, VALUE, int, const VALUE *))b->imp;

	return (*pimp)(argc == 0 ? Qnil : argv[0], b->userdata, argc, argv);
    }
    else if ((b->flags & VM_BLOCK_EMPTY) == VM_BLOCK_EMPTY) {
	// Trying to call an empty block!
	return Qnil;
    }

    rb_vm_arity_t arity = b->arity;    

    if (argc < arity.min || argc > arity.max) {
	if (arity.max != -1
		&& (b->flags & VM_BLOCK_LAMBDA) == VM_BLOCK_LAMBDA) {
	    short limit = (argc < arity.min) ? arity.min : arity.max;
	    rb_raise(rb_eArgError, "wrong number of arguments (%d for %d)",
		    argc, limit);
	}

	VALUE *new_argv;
	if (argc == 1 && TYPE(argv[0]) == T_ARRAY
		&& (arity.min > 1
		    || (arity.min == 1 && arity.min != arity.max))) {
	    // Expand the array.
	    const int ary_len = RARRAY_LENINT(argv[0]);
	    if (ary_len > 0) {
		new_argv = (VALUE *)RARRAY_PTR(argv[0]);
	    }
	    else {
		new_argv = NULL;
	    }
	    argv = new_argv;
	    argc = ary_len;
	    if (argc >= arity.min
		    && (argc <= arity.max || b->arity.max == -1)) {
		goto block_call;
	    }
	}

	int new_argc;
	if (argc <= arity.min) {
	    new_argc = arity.min;
	}
	else if (argc > arity.max && b->arity.max != -1) {
	    new_argc = arity.max;
	}
	else {
	    new_argc = argc;
	}

	if (new_argc > 0) {
	    new_argv = (VALUE *)xmalloc_ptrs(sizeof(VALUE) * new_argc);
	    for (int i = 0; i < new_argc; i++) {
		new_argv[i] = i < argc ? argv[i] : Qnil;
	    }
	}
	else {
	    new_argv = NULL;
	}

	argc = new_argc;
	argv = new_argv;
    }
#if ROXOR_VM_DEBUG
    printf("yield block %p argc %d arity %d\n", b, argc, arity.real);
#endif

block_call:

    if (b->flags & VM_BLOCK_ACTIVE) {
	b = dup_block(b);
    }
    b->flags |= VM_BLOCK_ACTIVE;

    Class old_current_class = vm->get_current_class();
    vm->set_current_class((Class)b->klass);

    struct Finally {
	RoxorVM *vm;
	rb_vm_block_t *b;
	Class c;
	Finally(RoxorVM *_vm, rb_vm_block_t *_b, Class _c) {
	    vm = _vm;
	    b = _b;
	    c = _c;
	}
	~Finally() {
	    b->flags &= ~VM_BLOCK_ACTIVE;
	    vm->set_current_class(c);
	}
    } finalizer(vm, b, old_current_class);

    if (b->flags & VM_BLOCK_METHOD) {
	rb_vm_method_t *m = (rb_vm_method_t *)b->imp;
	return rb_vm_dispatch(vm, (struct mcache *)m->cache, 0, m->recv,
		(Class)m->oclass, m->sel, NULL, DISPATCH_FCALL, argc, argv);
    }
    return __rb_vm_bcall(self, sel, (VALUE)b->dvars, b, b->imp, b->arity,
	    argc, argv);
}

extern "C"
VALUE
rb_vm_block_eval(rb_vm_block_t *b, int argc, const VALUE *argv)
{
    return vm_block_eval(GET_VM(), b, NULL, b->self, argc, argv);
}

extern "C"
VALUE
rb_vm_block_eval2(rb_vm_block_t *b, VALUE self, SEL sel, int argc,
	const VALUE *argv)
{
    // TODO check given arity and raise exception
    return vm_block_eval(GET_VM(), b, sel, self, argc, argv);
}

extern "C"
VALUE
rb_vm_yield_args(void *_vm, int argc, const VALUE *argv)
{
    RoxorVM *vm = (RoxorVM *)_vm;

    rb_vm_block_t *b = vm->current_block();
    if (b == NULL) {
	rb_raise(rb_eLocalJumpError, "no block given");
    }

    vm->pop_current_block();

    rb_vm_block_t *top_b = vm->current_block();
    if (top_b == NULL) {
	if (vm != RoxorVM::main) {
	    top_b = GetThreadPtr(vm->get_thread())->body;
	}
    }
    if (top_b != NULL && (top_b->flags & VM_BLOCK_THREAD)) {
	b->flags |= VM_BLOCK_THREAD;
    }

    struct Finally {
	RoxorVM *vm;
	rb_vm_block_t *b;
	Finally(RoxorVM *_vm, rb_vm_block_t *_b) { 
	    vm = _vm;
	    b = _b;
	}
	~Finally() {
	    vm->add_current_block(b);
	    if (vm == RoxorVM::main) {
		b->flags &= ~VM_BLOCK_THREAD;
	    }
	}
    } finalizer(vm, b);

    return vm_block_eval(vm, b, NULL, b->self, argc, argv);
}

extern "C"
VALUE
rb_vm_yield_under(VALUE klass, VALUE self, int argc, const VALUE *argv)
{
    RoxorVM *vm = GET_VM();
    rb_vm_block_t *b = vm->current_block();
    if (b == NULL) {
	rb_raise(rb_eLocalJumpError, "no block given");
    }

    vm->pop_current_block();

    VALUE old_self = b->self;
    b->self = self;
    VALUE old_class = b->klass;
    b->klass = klass;

    rb_vm_outer_t *o = vm->push_outer((Class)klass);
    o->pushed_by_eval = true;

    struct Finally {
	RoxorVM *vm;
	rb_vm_block_t *b;
	VALUE old_class;
	VALUE old_self;
	Finally(RoxorVM *_vm, rb_vm_block_t *_b, VALUE _old_class,
		VALUE _old_self) {
	    vm = _vm;
	    b = _b;
	    old_class = _old_class;
	    old_self = _old_self;
	}
	~Finally() {
	    vm->pop_outer(true);
	    b->self = old_self;
	    b->klass = old_class;
	    vm->add_current_block(b);
	}
    } finalizer(vm, b, old_class, old_self);

    return vm_block_eval(vm, b, NULL, b->self, argc, argv);
}

force_inline rb_vm_block_t *
RoxorVM::uncache_or_create_block(void *key, bool *cached, int dvars_size)
{
    std::map<void *, rb_vm_block_t *>::iterator iter = blocks.find(key);

    rb_vm_block_t *b;
    const int create_block_mask =
	VM_BLOCK_ACTIVE | VM_BLOCK_PROC | VM_BLOCK_IFUNC;

    if ((iter == blocks.end()) || (iter->second->flags & create_block_mask)) {
	const bool is_ifunc = (iter != blocks.end())
	    && ((iter->second->flags & VM_BLOCK_IFUNC) == VM_BLOCK_IFUNC);

	b = (rb_vm_block_t *)xmalloc(sizeof(rb_vm_block_t)
		+ (sizeof(VALUE *) * dvars_size));
	if (!is_ifunc) {
	    if (iter != blocks.end()) {
		GC_RELEASE(iter->second);
	    }
	    GC_RETAIN(b);
	    blocks[key] = b;
	}
	*cached = false;
    }
    else {
	b = iter->second;
	*cached = true;
    }

    return b;
}

extern "C"
rb_vm_block_t *
rb_vm_prepare_block(void *function, int flags, VALUE self, rb_vm_arity_t arity,
	rb_vm_var_uses **parent_var_uses, rb_vm_block_t *parent_block,
	int dvars_size, ...)
{
    assert(function != NULL);
    RoxorVM *vm = GET_VM();

    bool cached = false;
    rb_vm_block_t *b = vm->uncache_or_create_block(function, &cached,
	dvars_size);

    bool aot_block = false;
    if ((flags & VM_BLOCK_AOT) == VM_BLOCK_AOT) {
	flags ^= VM_BLOCK_AOT;
	aot_block = true;
    }

    if (parent_block != NULL && (parent_block->flags & VM_BLOCK_THREAD)) {
	flags |= VM_BLOCK_THREAD;
    }

    if (!cached) {
	if ((flags & VM_BLOCK_IFUNC) == VM_BLOCK_IFUNC) {
	    b->imp = (IMP)function;
	}
	else {
	    if (aot_block) {
		b->imp = (IMP)function;
	    }
	    else {
#if MACRUBY_STATIC
		abort();
#else
		GET_CORE()->lock();
		b->imp = GET_CORE()->compile((Function *)function);
		GET_CORE()->unlock();
#endif
	    }
	    b->userdata = (VALUE)function;
	}
	b->arity = arity;
	b->flags = flags;
	b->dvars_size = dvars_size;
	b->parent_var_uses = NULL;
	b->parent_block = NULL;
    }
    else {
	assert(b->dvars_size == dvars_size);
	assert((b->flags & flags) == flags);
    }

    b->proc = Qnil;
    GC_WB(&b->self, self);
    b->klass = (VALUE)vm->get_current_class();
    b->parent_var_uses = parent_var_uses;
    GC_WB(&b->parent_block, parent_block);

    va_list ar;
    va_start(ar, dvars_size);
    for (int i = 0; i < dvars_size; ++i) {
	b->dvars[i] = va_arg(ar, VALUE *);
    }
    int lvars_size = va_arg(ar, int);
    if (lvars_size > 0) {
	if (!cached) {
	    rb_vm_local_t **l = &b->locals;
	    for (int i = 0; i < lvars_size; i++) {
		GC_WB(l, xmalloc(sizeof(rb_vm_local_t)));
		l = &(*l)->next;
	    }
	}
	rb_vm_local_t *l = b->locals;
	for (int i = 0; i < lvars_size; ++i) {
	    assert(l != NULL);
	    l->name = va_arg(ar, ID);
	    l->value = va_arg(ar, VALUE *);
	    l = l->next;
	}
    }
    va_end(ar);

    return b;
}

extern "C"
rb_vm_block_t *
rb_vm_create_block(IMP imp, VALUE self, VALUE userdata)
{
    rb_vm_block_t *b = rb_vm_prepare_block((void *)imp, VM_BLOCK_IFUNC, self,
	    rb_vm_arity(0), // not used
	    NULL, NULL, 0, 0);
    GC_WB(&b->userdata, userdata);
    return b;
}

extern "C" void rb_print_undef(VALUE klass, ID id, int scope);

extern "C"
rb_vm_method_t *
rb_vm_get_method(VALUE klass, VALUE obj, ID mid, int scope)
{
    SEL sel = 0;
    IMP imp = NULL;
    rb_vm_method_node_t *node = NULL;

    // TODO honor scope

    if (!rb_vm_lookup_method2((Class)klass, mid, &sel, &imp, &node)) {
	rb_print_undef(klass, mid, 0);
    }

    Class k, oklass = (Class)klass;
    while ((k = class_getSuperclass(oklass)) != NULL) {
	if (!rb_vm_lookup_method(k, sel, NULL, NULL)) {
	    break;
	}
	oklass = k;
    }

    Method method = class_getInstanceMethod((Class)klass, sel);
    assert(method != NULL);

    int arity;
    rb_vm_method_node_t *new_node;
    if (node == NULL) {
	arity = rb_method_getNumberOfArguments(method) - 2;
	new_node = NULL;
    }
    else {
	arity = rb_vm_arity_n(node->arity);
	new_node = (rb_vm_method_node_t *)xmalloc(sizeof(rb_vm_method_node_t));
	memcpy(new_node, node, sizeof(rb_vm_method_node_t));
    }

    rb_vm_method_t *m = (rb_vm_method_t *)xmalloc(sizeof(rb_vm_method_t));

    m->oclass = (VALUE)oklass;
    m->rclass = klass;
    GC_WB(&m->recv, obj);
    m->sel = sel;
    m->arity = arity;
    GC_WB(&m->node, new_node);

    // Let's allocate a static cache here, since a rb_vm_method_t must always
    // point to the method it was created from.
    struct mcache *c = (struct mcache *)xmalloc(sizeof(struct mcache));
    if (new_node == NULL) {
	fill_ocache(c, obj, oklass, imp, sel, method, arity);
    }
    else {
	fill_rcache(c, oklass, sel, new_node);
    }
    GC_WB(&m->cache, c);

    return m;
}

extern IMP basic_respond_to_imp; // vm_method.c

bool
RoxorCore::respond_to(VALUE obj, VALUE klass, SEL sel, bool priv,
	bool check_override)
{
    if (klass == Qnil) {
	klass = CLASS_OF(obj);
    }
    else {
	assert(!check_override);
    }

    IMP imp = NULL;
    const bool overriden = check_override
	? ((imp = class_getMethodImplementation((Class)klass, selRespondTo))
		!= basic_respond_to_imp)
	: false;

    if (!overriden) {
	lock();
	const long key = respond_to_key((Class)klass, sel);
	std::map<long, int>::iterator iter = respond_to_cache.find(key);
	int iter_cached = (iter != respond_to_cache.end());
	unlock();
	int status;
	if (iter_cached) {
	    status = iter->second;
	}
	else {
	    Method m = class_getInstanceMethod((Class)klass, sel);
	    if (m == NULL) {
		const char *selname = sel_getName(sel);
		sel = helper_sel(selname, strlen(selname));
		if (sel != NULL) {
		    m = class_getInstanceMethod((Class)klass, sel);
		}
	    }

	    IMP imp = method_getImplementation(m);
	    if (UNAVAILABLE_IMP(imp) || imp == (IMP)rb_f_notimplement) {
		status = RESPOND_TO_NOT_EXIST;
	    }
	    else {
		rb_vm_method_node_t *node = method_node_get(m);
		if (node != NULL && (node->flags & VM_METHOD_PRIVATE)) {
		    status = RESPOND_TO_PRIVATE;
		}
		else {
		    status = RESPOND_TO_PUBLIC;
		}
	    }
	    lock();
	    respond_to_cache[key] = status;
	    unlock();
	}
	return status == RESPOND_TO_PUBLIC
	    || (priv && status == RESPOND_TO_PRIVATE);
    }
    else {
	if (imp == NULL || imp == _objc_msgForward) {
	    // The class does not respond to respond_to?:, it's probably
	    // NSProxy-based.
	    return false;
	}
	VALUE args[2];
	int n = 0;
	args[n++] = ID2SYM(rb_intern(sel_getName(sel)));
	if (priv) {
	    rb_vm_method_node_t *node = method_node_get(imp);
	    if (node != NULL
		    && (2 < node->arity.min
			|| (node->arity.max != -1 && 2 > node->arity.max))) {
		// Do nothing, custom respond_to? method incompatible arity.
	    }
	    else {
		args[n++] = Qtrue;
	    }
	}
	return rb_vm_call(obj, selRespondTo, n, args) == Qtrue;
    }
}

static bool
respond_to(VALUE obj, VALUE klass, SEL sel, bool priv, bool check_override)
{
    return GET_CORE()->respond_to(obj, klass, sel, priv, check_override);
}

extern "C"
bool
rb_vm_respond_to(VALUE obj, SEL sel, bool priv)
{
    return respond_to(obj, Qnil, sel, priv, true);
}

extern "C"
bool
rb_vm_respond_to2(VALUE obj, VALUE klass, SEL sel, bool priv,
	bool check_override)
{
    return respond_to(obj, klass, sel, priv, check_override);
}

// Note: rb_call_super() MUST always be called from methods registered using
// the MRI API (such as rb_define_method() & friends). It must NEVER be used
// internally inside MacRuby core.
extern "C"
VALUE
rb_call_super(int argc, const VALUE *argv)
{
    RoxorVM *vm = GET_VM();
    VALUE self = vm->get_current_mri_method_self();
    SEL sel = vm->get_current_mri_method_sel();
    assert(self != 0 && sel != 0);

    return rb_vm_call_super(self, sel, argc, argv);
}

extern "C"
void
rb_vm_set_current_mri_method_context(VALUE self, SEL sel)
{
    RoxorVM *vm = GET_VM();
    vm->set_current_mri_method_self(self);
    vm->set_current_mri_method_sel(sel);
} 
