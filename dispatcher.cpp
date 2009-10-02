/*
 * MacRuby VM.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 * 
 * Copyright (C) 2008-2009, Apple Inc. All rights reserved.
 */

#include "llvm.h"
#include "ruby/ruby.h"
#include "ruby/node.h"
#include "id.h"
#include "vm.h"
#include "compiler.h"
#include "objc.h"

#include <execinfo.h>
#include <dlfcn.h>

#define MAX_DISPATCH_ARGS 100

static force_inline void
__rb_vm_fix_args(const VALUE *argv, VALUE *new_argv,
	const rb_vm_arity_t &arity, int argc)
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
	    int opt_arg_index = i - arity.left_req;
	    if (opt_arg_index >= used_opt_args) {
		new_argv[i] = Qundef;
	    }
	    else {
		new_argv[i] = argv[i];
	    }
	}
	else if (i == rest_pos) {
	    // rest
	    int rest_size = argc - arity.real + 1;
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
	      IMP pimp, const rb_vm_arity_t &arity, int argc,
	      const VALUE *argv)
{
    if ((arity.real != argc) || (arity.max == -1)) {
	VALUE *new_argv = (VALUE *)alloca(sizeof(VALUE) * arity.real);
	__rb_vm_fix_args(argv, new_argv, arity, argc);
	argv = new_argv;
	argc = arity.real;
    }

    assert(pimp != NULL);

    VALUE (*imp)(VALUE, SEL, VALUE, rb_vm_block_t *,  ...) = (VALUE (*)(VALUE, SEL, VALUE, rb_vm_block_t *, ...))pimp;

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
	    return (*imp)(self, sel, dvars, b, argv[0], argv[1], argv[2], argv[3]);
	case 5:
	    return (*imp)(self, sel, dvars, b, argv[0], argv[1], argv[2], argv[3], argv[4]);
	case 6:
	    return (*imp)(self, sel, dvars, b, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5]);
	case 7:
	    return (*imp)(self, sel, dvars, b, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5], argv[6]);
	case 8:
	    return (*imp)(self, sel, dvars, b, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5], argv[6], argv[7]);
	case 9:
	    return (*imp)(self, sel, dvars, b, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5], argv[6], argv[7], argv[8]);
    }	
    printf("invalid argc %d\n", argc);
    abort();
}

static force_inline VALUE
__rb_vm_rcall(VALUE self, SEL sel, IMP pimp, const rb_vm_arity_t &arity,
              int argc, const VALUE *argv)
{
    if ((arity.real != argc) || (arity.max == -1)) {
	VALUE *new_argv = (VALUE *)alloca(sizeof(VALUE) * arity.real);
	__rb_vm_fix_args(argv, new_argv, arity, argc);
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
	    return (*imp)(self, sel, argv[0], argv[1], argv[2], argv[3], argv[4]);
	case 6:
	    return (*imp)(self, sel, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5]);
	case 7:
	    return (*imp)(self, sel, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5], argv[6]);
	case 8:
	    return (*imp)(self, sel, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5], argv[6], argv[7]);
	case 9:
	    return (*imp)(self, sel, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5], argv[6], argv[7], argv[8]);
	case 10:
	    return (*imp)(self, sel, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5], argv[6], argv[7], argv[8], argv[9]);
	case 11:
	    return (*imp)(self, sel, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5], argv[6], argv[7], argv[8], argv[9], argv[10]);
    }	
    printf("invalid argc %d\n", argc);
    abort();
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

    assert(len < sizeof(buf));

    if (len >= 3 && isalpha(p[len - 3]) && p[len - 2] == '='
	&& p[len - 1] == ':') {

	/* foo=: -> setFoo: shortcut */
	snprintf(buf, sizeof buf, "set%s", p);
	buf[3] = toupper(buf[3]);
	buf[len + 1] = ':';
	buf[len + 2] = '\0';
	new_sel = sel_registerName(buf);
    }
    else if (len > 1 && p[len - 1] == '?') {
	/* foo?: -> isFoo: shortcut */
	snprintf(buf, sizeof buf, "is%s", p);
	buf[2] = toupper(buf[2]);
	buf[len + 1] = '\0';
	new_sel = sel_registerName(buf);
    }

    return new_sel;
}

static IMP
objc_imp(IMP imp)
{
    rb_vm_method_node_t *node = GET_CORE()->method_node_get(imp);
    if (node != NULL && node->ruby_imp == imp) {
	imp = node->objc_imp;
    }
    return imp;
}

static Method
rb_vm_super_lookup(VALUE klass, SEL sel)
{
    // Locate the current method implementation.
    Method m = class_getInstanceMethod((Class)klass, sel);
    assert(m != NULL);
    IMP self = objc_imp(method_getImplementation(m));

    // Compute the stack call implementations right after our current method.
    void *callstack[128];
    int callstack_n = backtrace(callstack, 128);
    std::vector<void *> callstack_funcs;
    bool skip = true;
    for (int i = callstack_n - 1; i >= 0; i--) {
	void *start = NULL;
	if (GET_CORE()->symbolize_call_address(callstack[i],
		    &start, NULL, 0, NULL, NULL, 0)) {
	    start = (void *)objc_imp((IMP)start);
	    if (start == (void *)self) {
		skip = false;
	    }
	    if (!skip) {
		callstack_funcs.push_back(start);
	    }
	}
    }

    // Iterate over ancestors and return the first method that isn't on
    // the stack.
    VALUE ary = rb_mod_ancestors_nocopy(klass);
    const int count = RARRAY_LEN(ary);
    VALUE k = klass;
    bool klass_located = false;

#if ROXOR_VM_DEBUG
    printf("locating super method %s of class %s in ancestor chain %s\n", 
	    sel_getName(sel), rb_class2name(klass),
	    RSTRING_PTR(rb_inspect(ary)));
    printf("callstack functions: ");
    for (std::vector<void *>::iterator iter = callstack_funcs.begin();
	 iter != callstack_funcs.end();
	 ++iter) {
	printf("%p ", *iter);
    }
    printf("\n");
#endif

    //assert(!callstack_funcs.empty());

    for (int i = 0; i < count; i++) {
        if (!klass_located && RARRAY_AT(ary, i) == klass) {
            klass_located = true;
        }
        if (klass_located) {
            if (i < count - 1) {
                k = RARRAY_AT(ary, i + 1);

		Method method = class_getInstanceMethod((Class)k, sel);
		VALUE super = RCLASS_SUPER(k);

		if (method == NULL || (super != 0
		    && class_getInstanceMethod((Class)super, sel) == method)) {
		    continue;
		}

		IMP imp = method_getImplementation(method);

		if (std::find(callstack_funcs.begin(), callstack_funcs.end(), 
			    (void *)imp) == callstack_funcs.end()) {
		    // Method is not on stack.
#if ROXOR_VM_DEBUG
		    printf("returning method implementation %p " \
		    	   "from class/module %s\n", imp, rb_class2name(k));
#endif
		    return method;
		}
            }
        }
    }

    return NULL;
}

static VALUE
method_missing(VALUE obj, SEL sel, rb_vm_block_t *block, int argc,
	const VALUE *argv, rb_vm_method_missing_reason_t call_status)
{
    GET_VM()->set_method_missing_reason(call_status);

    if (sel == selMethodMissing) {
	rb_vm_method_missing(obj, argc, argv);
    }
    else if (sel == selAlloc) {
        rb_raise(rb_eTypeError, "allocator undefined for %s",
                 rb_class2name(obj));
    }

    VALUE *new_argv = (VALUE *)alloca(sizeof(VALUE) * (argc + 1));

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
        // Not a typical multiple argument selector. So as this is probably a
        // typical ruby method name, chop off the colon.
        buf[n - 1] = '\0';
      }
    }
    new_argv[0] = ID2SYM(rb_intern(buf));
    MEMCPY(&new_argv[1], argv, VALUE, argc);

    struct mcache *cache;
    cache = GET_CORE()->method_cache_get(selMethodMissing, false);
    return rb_vm_call_with_cache2(cache, block, obj, NULL, selMethodMissing,
    	argc + 1, new_argv);
}

extern "C"
void *
rb_vm_undefined_imp(void *rcv, SEL sel)
{
    method_missing((VALUE)rcv, sel, NULL, NULL, NULL, METHOD_MISSING_DEFAULT);
    return NULL; // never reached
}

static force_inline VALUE
__rb_vm_ruby_dispatch(VALUE self, SEL sel, rb_vm_method_node_t *node,
		      unsigned char opt, int argc, const VALUE *argv)
{
    const rb_vm_arity_t &arity = node->arity;
    if ((argc < arity.min) || ((arity.max != -1) && (argc > arity.max))) {
	rb_raise(rb_eArgError, "wrong number of arguments (%d for %d)",
		argc, arity.min);
    }

    if ((node->flags & VM_METHOD_PRIVATE) && opt == 0) {
	// Calling a private method with no explicit receiver OR an attribute
	// assignment to non-self, triggering #method_missing.
	rb_vm_block_t *b = GET_VM()->current_block();
	return method_missing(self, sel, b, argc, argv, METHOD_MISSING_PRIVATE);
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
	else {
	    printf("invalid negative arity for C function %d\n",
		    arity.real);
	    abort();
	}
    }

    return __rb_vm_rcall(self, sel, node->ruby_imp, arity, argc, argv);
}

static void
fill_rcache(struct mcache *cache, Class klass, SEL sel,
	rb_vm_method_node_t *node)
{
    cache->flag = MCACHE_RCALL;
    rcache.klass = klass;
    rcache.node = node;
}

static bool
can_forwardInvocation(VALUE recv, SEL sel)
{
    if (!SPECIAL_CONST_P(recv)) {
	static SEL methodSignatureForSelector = 0;
	if (methodSignatureForSelector == 0) {
	    methodSignatureForSelector =
		sel_registerName("methodSignatureForSelector:");	
	}
	return objc_msgSend((id)recv, methodSignatureForSelector, (id)sel)
	    != nil;
    }
    return false;
}

static void
fill_ocache(struct mcache *cache, VALUE self, Class klass, IMP imp, SEL sel,
	    Method method, int argc)
{
    cache->flag = MCACHE_OCALL;
    ocache.klass = klass;
    ocache.imp = imp;
    ocache.bs_method = GET_CORE()->find_bs_method(klass, sel);

    char types[200];
    if (!rb_objc_get_types(self, klass, sel, method, ocache.bs_method,
		types, sizeof types)) {
	printf("cannot get encoding types for %c[%s %s]\n",
		class_isMetaClass(klass) ? '+' : '-',
		class_getName(klass),
		sel_getName(sel));
	abort();
    }
    bool variadic = false;
    if (ocache.bs_method != NULL && ocache.bs_method->variadic
	&& method != NULL) {
	// TODO honor printf_format
	const int real_argc = method_getNumberOfArguments(method) - 2;
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
    ocache.stub = (rb_vm_objc_stub_t *)GET_CORE()->gen_stub(types, variadic,
	    argc, true);
}

static force_inline VALUE
__rb_vm_dispatch(RoxorVM *vm, struct mcache *cache, VALUE self, Class klass,
	SEL sel, rb_vm_block_t *block, unsigned char opt, int argc,
	const VALUE *argv)
{
    assert(cache != NULL);

    if (klass == NULL) {
	klass = (Class)CLASS_OF(self);
    }

#if ROXOR_VM_DEBUG
    bool cached = true;
#endif
    bool do_rcache = true;

    if (cache->flag == 0) {
recache:
#if ROXOR_VM_DEBUG
	cached = false;
#endif

	Method method;
	if (opt == DISPATCH_SUPER) {
	    method = rb_vm_super_lookup((VALUE)klass, sel);
	}
	else {
	    method = class_getInstanceMethod(klass, sel);
	}

	if (method != NULL) {
recache2:
	    IMP imp = method_getImplementation(method);

	    if (UNDEFINED_IMP(imp)) {
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
	}
	else {
	    // Method is not found...

	    // Force a method resolving, because the objc cache might be
	    // wrong.
	    if (rb_vm_resolve_method(klass, sel)) {
		goto recache;
	    }

	    // Does the receiver implements -forwardInvocation:?
	    if (opt != DISPATCH_SUPER && can_forwardInvocation(self, sel)) {
		fill_ocache(cache, self, klass, (IMP)objc_msgSend, sel, NULL,
			argc);
		goto dispatch;
	    }

	    // Let's see if are not trying to call a Ruby method that accepts
	    // a regular argument then a optional Hash argument, to be
	    // compatible with the Ruby specification.
	    const char *selname = (const char *)sel;
	    size_t selname_len = strlen(selname);
	    if (argc > 1) {
		const char *p = strchr(selname, ':');
		if (p != NULL && p + 1 != '\0') {
		    char *tmp = (char *)alloca(selname_len);
		    strncpy(tmp, selname, p - selname + 1);
		    tmp[p - selname + 1] = '\0';
		    SEL new_sel = sel_registerName(tmp);
		    Method m = class_getInstanceMethod(klass, new_sel);
		    if (m != NULL) {
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
			if (ok) {
			    argc = 2;
			    ((VALUE *)argv)[1] = h; // bad, I know...
			    sel = new_sel;
			    method = m;
			    do_rcache = false;
			    goto recache2;
			}
		    }
		}
	    }

	    // Let's try to see if we are not given a helper selector.
	    SEL new_sel = helper_sel(selname, selname_len);
	    if (new_sel != NULL) {
		Method m = class_getInstanceMethod(klass, new_sel);
		if (m != NULL) {
		    if (GET_CORE()->method_node_get(m) == NULL) {
			sel = new_sel;
			method = m;
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
		std::string types;
		vm_gen_bs_func_types(argc, argv, bs_func, types);

		cache->flag = MCACHE_FCALL;
		fcache.bs_function = bs_func;
		fcache.imp = (IMP)dlsym(RTLD_DEFAULT, bs_func->name);
		assert(fcache.imp != NULL);
		fcache.stub = (rb_vm_c_stub_t *)GET_CORE()->gen_stub(types,
			bs_func->variadic, bs_func->args_count, false);
	    }
	    else {
		// Still nothing, then let's call #method_missing.
		goto call_method_missing;
	    }
	}
    }

dispatch:
    if (cache->flag == MCACHE_RCALL) {
	if (rcache.klass != klass) {
	    goto recache;
	}
	if (!do_rcache) {
	    cache->flag = 0;
	}

#if ROXOR_VM_DEBUG
	printf("ruby dispatch %c[<%s %p> %s] (imp=%p, block=%p, argc=%d, cached=%s)\n",
		class_isMetaClass(klass) ? '+' : '-',
		class_getName(klass),
		(void *)self,
		sel_getName(sel),
		rcache.node->ruby_imp,
		block,
		argc,
		cached ? "true" : "false");
#endif

	bool block_already_current = vm->is_block_current(block);
	Class current_klass = vm->get_current_class();
	if (!block_already_current) {
	    vm->add_current_block(block);
	}
	vm->set_current_class(NULL);

	struct Finally {
	    bool block_already_current;
	    Class current_class;
	    RoxorVM *vm;
	    Finally(bool _block_already_current, Class _current_class,
		    RoxorVM *_vm) {
		block_already_current = _block_already_current;
		current_class = _current_class;
		vm = _vm;
	    }
	    ~Finally() {
		if (!block_already_current) {
		    vm->pop_current_block();
		}
		vm->set_current_class(current_class);
		vm->pop_broken_with();
	    }
	} finalizer(block_already_current, current_klass, vm);

	return __rb_vm_ruby_dispatch(self, sel, rcache.node, opt, argc, argv);
    }
    else if (cache->flag == MCACHE_OCALL) {
	if (ocache.klass != klass) {
	    goto recache;
	}

	if (block != NULL) {
	    if (self == rb_cNSMutableHash && sel == selNew) {
		// Because Hash.new can accept a block.
		vm->add_current_block(block);

		struct Finally {
		    RoxorVM *vm;
		    Finally(RoxorVM *_vm) { vm = _vm; }
		    ~Finally() { vm->pop_current_block(); }
		} finalizer(vm);

		return rb_hash_new2(argc, argv);
	    }
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
	    if (klass == (Class)rb_cCFString) {
		return RSTRING_IMMUTABLE(self)
		    ? rb_cNSString : rb_cNSMutableString;
	    }
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1060
	    if (klass == (Class)rb_cCFArray || klass == (Class)rb_cNSArray0) {
#else
	    if (klass == (Class)rb_cCFArray) {
#endif
		return RARRAY_IMMUTABLE(self)
		    ? rb_cNSArray : rb_cNSMutableArray;
	    }
	    else if (klass == (Class)rb_cRubyArray) {
		return rb_cNSMutableArray;
	    }
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1060
	    if (klass == (Class)rb_cCFHash || klass == (Class)rb_cNSHash0) {
#else
	    if (klass == (Class)rb_cCFHash) {
#endif
		return RHASH_IMMUTABLE(self)
		    ? rb_cNSHash : rb_cNSMutableHash;
	    }
	    if (klass == (Class)rb_cCFSet) {
		return RSET_IMMUTABLE(self)
		    ? rb_cNSSet : rb_cNSMutableSet;
	    }
	}

#if ROXOR_VM_DEBUG
	printf("objc dispatch %c[<%s %p> %s] imp=%p argc=%d (cached=%s)\n",
		class_isMetaClass(klass) ? '+' : '-',
		class_getName(klass),
		(void *)self,
		sel_getName(sel),
		ocache.imp,
		argc,
		cached ? "true" : "false");
#endif

	return (*ocache.stub)(ocache.imp, RB2OC(self), sel, argc, argv);
    }
    else if (cache->flag == MCACHE_FCALL) {
#if ROXOR_VM_DEBUG
	printf("C dispatch %s() imp=%p argc=%d (cached=%s)\n",
		fcache.bs_function->name,
		fcache.imp,
		argc,
		cached ? "true" : "false");
#endif
	return (*fcache.stub)(fcache.imp, argc, argv);
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
    int argc_expected;

    if (argc > 0 && selname[selname_len - 1] == ':') {
	char buf[100];
	assert(sizeof buf > selname_len - 1);
	strlcpy(buf, selname, sizeof buf);
	buf[selname_len - 1] = '\0';
	new_sel = sel_registerName(buf);
	argc_expected = 0;
    }
    else if (argc == 0) {
	char buf[100];
	snprintf(buf, sizeof buf, "%s:", selname);
	new_sel = sel_registerName(buf);
	argc_expected = 1;
    }
    if (new_sel != 0) {
	Method m = class_getInstanceMethod(klass, new_sel);
	if (m != NULL
		&& GET_CORE()->method_node_get(m) != NULL) {
	    rb_raise(rb_eArgError, "wrong number of arguments (%d for %d)",
		    argc, argc_expected);
	}
    }

    rb_vm_method_missing_reason_t status =
	opt == DISPATCH_VCALL
	    ? METHOD_MISSING_VCALL : opt == DISPATCH_SUPER
		? METHOD_MISSING_SUPER : METHOD_MISSING_DEFAULT;
    return method_missing((VALUE)self, sel, block, argc, argv, status);
}

static force_inline void
__rb_vm_resolve_args(VALUE **pargv, size_t argv_size, int *pargc, va_list ar)
{
    // TODO we should only determine the real argc here (by taking into
    // account the length splat arguments) and do the real unpacking of
    // splat arguments in __rb_vm_rcall(). This way we can optimize more
    // things (for ex. no need to unpack splats that are passed as a splat
    // argument in the method being called!).
    unsigned int i, argc = *pargc, real_argc = 0;
    VALUE *argv = *pargv;
    bool splat_arg_follows = false;
    for (i = 0; i < argc; i++) {
	VALUE arg = va_arg(ar, VALUE);
	if (arg == SPLAT_ARG_FOLLOWS) {
	    splat_arg_follows = true;
	    i--;
	}
	else {
	    if (splat_arg_follows) {
		VALUE ary = rb_check_convert_type(arg, T_ARRAY, "Array",
			"to_a");
		if (NIL_P(ary)) {
		    ary = rb_ary_new3(1, arg);
		}
		int count = RARRAY_LEN(ary);
		if (real_argc + count >= argv_size) {
		    const size_t new_argv_size = real_argc + count + 100;
		    VALUE *new_argv = (VALUE *)xmalloc(sizeof(VALUE)
			    * new_argv_size);
		    memcpy(new_argv, argv, sizeof(VALUE) * argv_size);
		    argv = new_argv;
		    argv_size = new_argv_size;
		}
		for (int j = 0; j < count; j++) {
		    argv[real_argc++] = RARRAY_AT(ary, j);
		}
		splat_arg_follows = false;
	    }
	    else {
		if (real_argc >= argv_size) {
		    const size_t new_argv_size = real_argc + 100;
		    VALUE *new_argv = (VALUE *)xmalloc(sizeof(VALUE)
			    * new_argv_size);
		    memcpy(new_argv, argv, sizeof(VALUE) * argv_size);
		    argv = new_argv;
		    argv_size = new_argv_size;
		}
		argv[real_argc++] = arg;
	    }
	}
    }

    *pargv = argv;
    *pargc = real_argc;
}

extern "C"
VALUE
rb_vm_dispatch(struct mcache *cache, VALUE self, SEL sel, rb_vm_block_t *block, 
	       unsigned char opt, int argc, ...)
{
    VALUE base_argv[MAX_DISPATCH_ARGS];
    VALUE *argv = base_argv;
    if (argc > 0) {
	va_list ar;
	va_start(ar, argc);
	__rb_vm_resolve_args(&argv, MAX_DISPATCH_ARGS, &argc, ar);
	va_end(ar);

	if (argc == 0) {
	    const char *selname = sel_getName(sel);
	    const size_t selnamelen = strlen(selname);
	    if (selname[selnamelen - 1] == ':') {
		// Because
		//   def foo; end; foo(*[])
		// creates foo but dispatches foo:.
		char buf[100];
		strncpy(buf, selname, sizeof buf);
		buf[selnamelen - 1] = '\0';
		sel = sel_registerName(buf);
	    }
	}
    }

    RoxorVM *vm = GET_VM();

    VALUE retval = __rb_vm_dispatch(vm, cache, self, NULL, sel, block, opt,
	    argc, argv);

    vm->pop_current_binding();

    return retval;
}

extern "C"
VALUE
rb_vm_call(VALUE self, SEL sel, int argc, const VALUE *argv, bool super)
{
    struct mcache *cache;
    unsigned char opt = DISPATCH_FCALL;
    if (super) {
	cache = (struct mcache *)alloca(sizeof(struct mcache));
	cache->flag = 0;
	opt = DISPATCH_SUPER;
    }
    else {
	cache = GET_CORE()->method_cache_get(sel, false);
    }

    return __rb_vm_dispatch(GET_VM(), cache, self, NULL, sel, NULL, opt, argc,
	    argv);
}

extern "C"
VALUE
rb_vm_call_with_cache(void *cache, VALUE self, SEL sel, int argc, 
	const VALUE *argv)
{
    return __rb_vm_dispatch(GET_VM(), (struct mcache *)cache, self, NULL, sel,
	    NULL, DISPATCH_FCALL, argc, argv);
}

extern "C"
VALUE
rb_vm_call_with_cache2(void *cache, rb_vm_block_t *block, VALUE self,
	VALUE klass, SEL sel, int argc, const VALUE *argv)
{
    return __rb_vm_dispatch(GET_VM(), (struct mcache *)cache, self,
	    (Class)klass, sel, block, DISPATCH_FCALL, argc, argv);
}

// The rb_vm_fast_* functions don't check if the selector has been redefined or
// not, because this is already handled by the compiler.
// Also, fixnums and floats are already handled.

extern "C" {
    VALUE rb_fix_plus(VALUE x, VALUE y);
    VALUE rb_fix_minus(VALUE x, VALUE y);
    VALUE rb_fix_div(VALUE x, VALUE y);
    VALUE rb_fix_mul(VALUE x, VALUE y);
    VALUE rb_flo_plus(VALUE x, VALUE y);
    VALUE rb_flo_minus(VALUE x, VALUE y);
    VALUE rb_flo_div(VALUE x, VALUE y);
    VALUE rb_flo_mul(VALUE x, VALUE y);
    VALUE rb_nu_plus(VALUE x, VALUE y);
    VALUE rb_nu_minus(VALUE x, VALUE y);
    VALUE rb_nu_div(VALUE x, VALUE y);
    VALUE rb_nu_mul(VALUE x, VALUE y);
}

extern "C"
VALUE
rb_vm_fast_plus(struct mcache *cache, VALUE self, VALUE other)
{
    switch (TYPE(self)) {
	// TODO: Array, String
	case T_BIGNUM:
	    return rb_big_plus(self, other);
	case T_FIXNUM:
	    return rb_fix_plus(self, other);
	case T_FLOAT:
	    return rb_flo_plus(self, other);
	case T_COMPLEX:
	    return rb_nu_plus(self, other);
    }
    return rb_vm_dispatch(cache, self, selPLUS, NULL, 0, 1, other);
}

extern "C"
VALUE
rb_vm_fast_minus(struct mcache *cache, VALUE self, VALUE other)
{
    switch (TYPE(self)) {
	// TODO: Array, String
	case T_BIGNUM:
	    return rb_big_minus(self, other);
	case T_FIXNUM:
	    return rb_fix_minus(self, other);
	case T_FLOAT:
	    return rb_flo_minus(self, other);
	case T_COMPLEX:
	    return rb_nu_minus(self, other);
    }
    return rb_vm_dispatch(cache, self, selMINUS, NULL, 0, 1, other);
}

extern "C"
VALUE
rb_vm_fast_div(struct mcache *cache, VALUE self, VALUE other)
{
    switch (TYPE(self)) {
	case T_BIGNUM:
	    return rb_big_div(self, other);
	case T_FIXNUM:
	    return rb_fix_div(self, other);
	case T_FLOAT:
	    return rb_flo_div(self, other);
	case T_COMPLEX:
	    return rb_nu_div(self, other);
    }
    return rb_vm_dispatch(cache, self, selDIV, NULL, 0, 1, other);
}

extern "C"
VALUE
rb_vm_fast_mult(struct mcache *cache, VALUE self, VALUE other)
{
    switch (TYPE(self)) {
	// TODO: Array, String
	case T_BIGNUM:
	    return rb_big_mul(self, other);
	case T_FIXNUM:
	    return rb_fix_mul(self, other);
	case T_FLOAT:
	    return rb_flo_mul(self, other);
	case T_COMPLEX:
	    return rb_nu_mul(self, other);
    }
    return rb_vm_dispatch(cache, self, selMULT, NULL, 0, 1, other);
}

extern "C"
VALUE
rb_vm_fast_lt(struct mcache *cache, VALUE self, VALUE other)
{
    switch (TYPE(self)) {
	case T_BIGNUM:
	    return FIX2INT(rb_big_cmp(self, other)) < 0 ? Qtrue : Qfalse;
    }
    return rb_vm_dispatch(cache, self, selLT, NULL, 0, 1, other);
}

extern "C"
VALUE
rb_vm_fast_le(struct mcache *cache, VALUE self, VALUE other)
{
    switch (TYPE(self)) {
	case T_BIGNUM:
	    return FIX2INT(rb_big_cmp(self, other)) <= 0 ? Qtrue : Qfalse;
    }
    return rb_vm_dispatch(cache, self, selLE, NULL, 0, 1, other);
}

extern "C"
VALUE
rb_vm_fast_gt(struct mcache *cache, VALUE self, VALUE other)
{
    switch (TYPE(self)) {
	case T_BIGNUM:
	    return FIX2INT(rb_big_cmp(self, other)) > 0 ? Qtrue : Qfalse;
    }
    return rb_vm_dispatch(cache, self, selGT, NULL, 0, 1, other);
}

extern "C"
VALUE
rb_vm_fast_ge(struct mcache *cache, VALUE self, VALUE other)
{
    switch (TYPE(self)) {
	case T_BIGNUM:
	    return FIX2INT(rb_big_cmp(self, other)) >= 0 ? Qtrue : Qfalse;
    }
    return rb_vm_dispatch(cache, self, selGE, NULL, 0, 1, other);
}

extern "C"
VALUE
rb_vm_fast_eq(struct mcache *cache, VALUE self, VALUE other)
{
    const int self_type = TYPE(self);
    switch (self_type) {
	case T_SYMBOL:
	    return self == other ? Qtrue : Qfalse;

	case T_STRING:
	case T_ARRAY:
	case T_HASH:
	    if (self == other) {
		return Qtrue;
	    }
	    if (TYPE(other) != self_type) {
		return Qfalse;
	    }
	    if (self_type == T_ARRAY) {
		return rb_ary_equal(self, other);
	    }
	    return CFEqual((CFTypeRef)self, (CFTypeRef)other)
		? Qtrue : Qfalse;

	case T_BIGNUM:
	    return rb_big_eq(self, other);
    }
    return rb_vm_dispatch(cache, self, selEq, NULL, 0, 1, other);
}

extern "C"
VALUE
rb_vm_fast_neq(struct mcache *cache, VALUE self, VALUE other)
{
    // TODO
    return rb_vm_dispatch(cache, self, selNeq, NULL, 0, 1, other);
}

extern "C"
VALUE
rb_vm_fast_eqq(struct mcache *cache, VALUE self, VALUE other)
{
    switch (TYPE(self)) {
	// TODO: Range
	case T_STRING:
	    if (self == other) {
		return Qtrue;
	    }
	    return rb_str_equal(self, other);

	case T_REGEXP:
	    return rb_reg_eqq(self, selEqq, other);

	case T_SYMBOL:
	    return (self == other ? Qtrue : Qfalse);
	
	case T_MODULE:
	case T_CLASS:
	    return rb_obj_is_kind_of(other, self);

	default:
	    return rb_vm_dispatch(cache, self, selEqq, NULL, 0, 1, other);
    }
}

extern "C"
VALUE
rb_vm_when_splat(struct mcache *cache, unsigned char overriden,
		 VALUE comparedTo, VALUE splat)
{
    VALUE ary = rb_check_convert_type(splat, T_ARRAY, "Array", "to_a");
    if (NIL_P(ary)) {
	ary = rb_ary_new3(1, splat);
    }
    int count = RARRAY_LEN(ary);
    if (overriden == 0) {
	for (int i = 0; i < count; ++i) {
	    VALUE o = RARRAY_AT(ary, i);
	    if (RTEST(rb_vm_fast_eqq(cache, o, comparedTo))) {
		return Qtrue;
	    }
	}
    }
    else {
	for (int i = 0; i < count; ++i) {
	    VALUE o = RARRAY_AT(ary, i);
	    if (RTEST(rb_vm_dispatch(cache, o, selEqq, NULL, 0, 1, comparedTo))) {
		return Qtrue;
	    }
	}
    }
    return Qfalse;
}

extern "C"
VALUE
rb_vm_fast_shift(VALUE obj, VALUE other, struct mcache *cache,
		 unsigned char overriden)
{
    if (overriden == 0) {
	switch (TYPE(obj)) {
	    case T_ARRAY:
		rb_ary_push(obj, other);
		return obj;

	    case T_STRING:
		rb_str_concat(obj, other);
		return obj;
	}
    }
    return __rb_vm_dispatch(GET_VM(), cache, obj, NULL, selLTLT, NULL, 0, 1,
	    &other);
}

extern "C"
VALUE
rb_vm_fast_aref(VALUE obj, VALUE other, struct mcache *cache,
		unsigned char overriden)
{
    // TODO what about T_HASH?
    if (overriden == 0 && TYPE(obj) == T_ARRAY) {
	if (TYPE(other) == T_FIXNUM) {
	    return rb_ary_entry(obj, FIX2LONG(other));
	}
	return rb_ary_aref(obj, 0, 1, &other);
    }
    return __rb_vm_dispatch(GET_VM(), cache, obj, NULL, selAREF, NULL, 0, 1,
	    &other);
}

extern "C"
VALUE
rb_vm_fast_aset(VALUE obj, VALUE other1, VALUE other2, struct mcache *cache,
		unsigned char overriden)
{
    // TODO what about T_HASH?
    if (overriden == 0 && TYPE(obj) == T_ARRAY) {
	if (TYPE(other1) == T_FIXNUM) {
	    rb_ary_store(obj, FIX2LONG(other1), other2);
	    return other2;
	}
    }
    VALUE args[2] = { other1, other2 };
    return __rb_vm_dispatch(GET_VM(), cache, obj, NULL, selASET, NULL, 0, 2,
	    args);
}

static rb_vm_block_t *
rb_vm_dup_active_block(rb_vm_block_t *src_b)
{
    assert(src_b->flags & VM_BLOCK_ACTIVE);

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

static force_inline VALUE
rb_vm_block_eval0(rb_vm_block_t *b, SEL sel, VALUE self, int argc,
	const VALUE *argv)
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
	    rb_raise(rb_eArgError, "wrong number of arguments (%d for %d)",
		    argc, arity.min);
	}
	VALUE *new_argv;
	if (argc == 1 && TYPE(argv[0]) == T_ARRAY
	    && (arity.min > 1 || (arity.min == 1 && arity.min != arity.max))) {
	    // Expand the array
	    long ary_len = RARRAY_LEN(argv[0]);
	    new_argv = (VALUE *)alloca(sizeof(VALUE) * ary_len);
	    for (int i = 0; i < ary_len; i++) {
		new_argv[i] = RARRAY_AT(argv[0], i);
	    }
	    argv = new_argv;
	    argc = ary_len;
	    if (argc >= arity.min && (argc <= arity.max || b->arity.max == -1)) {
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
	new_argv = (VALUE *)alloca(sizeof(VALUE) * new_argc);
	for (int i = 0; i < new_argc; i++) {
	    new_argv[i] = i < argc ? argv[i] : Qnil;
	}
	argc = new_argc;
	argv = new_argv;
    }
#if ROXOR_VM_DEBUG
    printf("yield block %p argc %d arity %d\n", b, argc, arity.real);
#endif

block_call:

    if (b->flags & VM_BLOCK_ACTIVE) {
	b = rb_vm_dup_active_block(b);
    }
    b->flags |= VM_BLOCK_ACTIVE;

    RoxorVM *vm = GET_VM();
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
	return rb_vm_call_with_cache2(m->cache, NULL, m->recv, m->oclass,
		m->sel, argc, argv);
    }
    return __rb_vm_bcall(self, sel, (VALUE)b->dvars, b, b->imp, b->arity,
	    argc, argv);
}

extern "C"
VALUE
rb_vm_block_eval(rb_vm_block_t *b, int argc, const VALUE *argv)
{
    return rb_vm_block_eval0(b, NULL, b->self, argc, argv);
}

extern "C"
VALUE
rb_vm_block_eval2(rb_vm_block_t *b, VALUE self, SEL sel, int argc,
	const VALUE *argv)
{
    // TODO check given arity and raise exception
    return rb_vm_block_eval0(b, sel, self, argc, argv);
}

static force_inline VALUE
rb_vm_yield0(int argc, const VALUE *argv)
{
    RoxorVM *vm = GET_VM();
    rb_vm_block_t *b = vm->current_block();
    if (b == NULL) {
	rb_raise(rb_eLocalJumpError, "no block given");
    }

    vm->pop_current_block();

    struct Finally {
	RoxorVM *vm;
	rb_vm_block_t *b;
	Finally(RoxorVM *_vm, rb_vm_block_t *_b) { 
	    vm = _vm;
	    b = _b;
	}
	~Finally() {
	    vm->add_current_block(b);
	}
    } finalizer(vm, b);

    return rb_vm_block_eval0(b, NULL, b->self, argc, argv);
}

extern "C"
VALUE
rb_vm_yield(int argc, const VALUE *argv)
{
    return rb_vm_yield0(argc, argv);
}

extern "C"
VALUE
rb_vm_yield_under(VALUE klass, VALUE self, int argc, const VALUE *argv)
{
    RoxorVM *vm = GET_VM();
    rb_vm_block_t *b = vm->current_block();
    vm->pop_current_block();

    VALUE old_self = b->self;
    b->self = self;
    VALUE old_class = b->klass;
    b->klass = klass;

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
	    b->self = old_self;
	    b->klass = old_class;
	    vm->add_current_block(b);
	}
    } finalizer(vm, b, old_class, old_self);

    return rb_vm_block_eval0(b, NULL, b->self, argc, argv);
}

extern "C"
VALUE 
rb_vm_yield_args(int argc, ...)
{
    VALUE base_argv[MAX_DISPATCH_ARGS];
    VALUE *argv = &base_argv[0];
    if (argc > 0) {
	va_list ar;
	va_start(ar, argc);
	__rb_vm_resolve_args(&argv, MAX_DISPATCH_ARGS, &argc, ar);
	va_end(ar);
    }
    return rb_vm_yield0(argc, argv);
}

force_inline rb_vm_block_t *
RoxorVM::uncache_or_create_block(void *key, bool *cached, int dvars_size)
{
    std::map<void *, rb_vm_block_t *>::iterator iter = blocks.find(key);

    rb_vm_block_t *b;

    if ((iter == blocks.end())
	|| (iter->second->flags & (VM_BLOCK_ACTIVE | VM_BLOCK_PROC))) {

	if (iter != blocks.end()) {
	    rb_objc_release(iter->second);
	}

	b = (rb_vm_block_t *)xmalloc(sizeof(rb_vm_block_t)
		+ (sizeof(VALUE *) * dvars_size));
	rb_objc_retain(b);

	blocks[key] = b;
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

    if (!cached) {
	if ((flags & VM_BLOCK_IFUNC) == VM_BLOCK_IFUNC) {
	    b->imp = (IMP)function;
	}
	else {
	    if (aot_block) {
		b->imp = (IMP)function;
	    }
	    else {
		GET_CORE()->lock();
		b->imp = GET_CORE()->compile((Function *)function);
		GET_CORE()->unlock();
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
    b->self = self;
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
	arity = method_getNumberOfArguments(method) - 2;
	new_node = NULL;
    }
    else {
	arity = node->arity.min;
	if (node->arity.min != node->arity.max) {
	    arity = -arity - 1;
	}
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

extern "C"
bool
rb_vm_respond_to(VALUE obj, SEL sel, bool priv)
{
    VALUE klass = CLASS_OF(obj);

    IMP respond_to_imp = class_getMethodImplementation((Class)klass,
	    selRespondTo);

    if (respond_to_imp == basic_respond_to_imp) {
	// FIXME: too slow!
	bool reject_pure_ruby_methods = false;
	Method m = class_getInstanceMethod((Class)klass, sel);
	if (m == NULL) {
	    const char *selname = sel_getName(sel);
	    sel = helper_sel(selname, strlen(selname));
	    if (sel != NULL) {
		m = class_getInstanceMethod((Class)klass, sel);
		reject_pure_ruby_methods = true;
	    }
	}

	if (m == NULL || UNDEFINED_IMP(method_getImplementation(m))) {
	    return false;
	}

	rb_vm_method_node_t *node = GET_CORE()->method_node_get(m);
	if (node != NULL
	    && (reject_pure_ruby_methods
		|| (!priv && (node->flags & VM_METHOD_PRIVATE)))) {
	    return false;
	}
        return true;
    }
    else {
	VALUE args[2];
	int n = 0;
	args[n++] = ID2SYM(rb_intern(sel_getName(sel)));
	if (priv) {
	    args[n++] = Qtrue;
	}
	return rb_vm_call(obj, selRespondTo, n, args, false) == Qtrue;
    }
}
