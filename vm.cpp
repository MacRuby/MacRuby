/*
 * MacRuby VM.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 * 
 * Copyright (C) 2008-2009, Apple Inc. All rights reserved.
 */

#define ROXOR_VM_DEBUG		0
#define ROXOR_INTERPRET_EVAL	0
#define ROXOR_COMPILER_DEBUG 	0

#include <llvm/Module.h>
#include <llvm/DerivedTypes.h>
#include <llvm/Constants.h>
#include <llvm/CallingConv.h>
#include <llvm/Instructions.h>
#include <llvm/ModuleProvider.h>
#include <llvm/PassManager.h>
#include <llvm/Analysis/Verifier.h>
#include <llvm/Target/TargetData.h>
#include <llvm/ExecutionEngine/JIT.h>
#include <llvm/ExecutionEngine/JITMemoryManager.h>
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/Target/TargetData.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Intrinsics.h>
#include <llvm/Bitcode/ReaderWriter.h>
using namespace llvm;

#include "ruby/ruby.h"
#include "ruby/node.h"
#include "id.h"
#include "vm.h"
#include "compiler.h"
#include "objc.h"

#include <objc/objc-exception.h>

#if ROXOR_COMPILER_DEBUG
# include <mach/mach.h>
# include <mach/mach_time.h>
#endif

#include <execinfo.h>
#include <dlfcn.h>

#include <iostream>
#include <fstream>

RoxorCore *RoxorCore::shared = NULL;
RoxorVM *RoxorVM::main = NULL;
pthread_key_t RoxorVM::vm_thread_key;

VALUE rb_cTopLevel = 0;

struct RoxorFunction {
    Function *f;
    unsigned char *start;
    unsigned char *end;
    void *imp;

    RoxorFunction (Function *_f, unsigned char *_start, unsigned char *_end) {
	f = _f;
	start = _start;
	end = _end;
	imp = NULL; 	// lazy
    }
};

class RoxorJITManager : public JITMemoryManager {
    private:
        JITMemoryManager *mm;
	std::vector<struct RoxorFunction *> functions;

    public:
	RoxorJITManager() : JITMemoryManager() { 
	    mm = CreateDefaultMemManager(); 
	}

	struct RoxorFunction *find_function(unsigned char *addr) {
	     if (functions.empty()) {
		return NULL;
	     }
	     // TODO optimize me!
	     RoxorFunction *front = functions.front();
	     RoxorFunction *back = functions.back();
	     if (addr < front->start || addr > back->end) {
		return NULL;
	     }
	     std::vector<struct RoxorFunction *>::iterator iter = 
		 functions.begin();
	     while (iter != functions.end()) {
		RoxorFunction *f = *iter;
		if (addr >= f->start && addr <= f->end) {
		    return f;
		}
		++iter;
	     }
	     return NULL;
	}

	void setMemoryWritable(void) { 
	    mm->setMemoryWritable(); 
	}

	void setMemoryExecutable(void) { 
	    mm->setMemoryExecutable(); 
	}

	unsigned char *allocateSpace(intptr_t Size, unsigned Alignment) { 
	    return mm->allocateSpace(Size, Alignment); 
	}

	void AllocateGOT(void) {
	    mm->AllocateGOT();
	}

	unsigned char *getGOTBase() const {
	    return mm->getGOTBase();
	}

	void SetDlsymTable(void *ptr) {
	    mm->SetDlsymTable(ptr);
	}

	void *getDlsymTable() const {
	    return mm->getDlsymTable();
	}

	unsigned char *startFunctionBody(const Function *F, 
		uintptr_t &ActualSize) {
	    return mm->startFunctionBody(F, ActualSize);
	}

	unsigned char *allocateStub(const GlobalValue* F, 
		unsigned StubSize, 
		unsigned Alignment) {
	    return mm->allocateStub(F, StubSize, Alignment);
	}

	void endFunctionBody(const Function *F, unsigned char *FunctionStart, 
		unsigned char *FunctionEnd) {
	    mm->endFunctionBody(F, FunctionStart, FunctionEnd);
	    functions.push_back(new RoxorFunction(const_cast<Function *>(F), 
			FunctionStart, FunctionEnd));
	}

	void deallocateMemForFunction(const Function *F) {
	    mm->deallocateMemForFunction(F);
	}

	unsigned char* startExceptionTable(const Function* F, 
		uintptr_t &ActualSize) {
	    return mm->startExceptionTable(F, ActualSize);
	}

	void endExceptionTable(const Function *F, unsigned char *TableStart, 
		unsigned char *TableEnd, unsigned char* FrameRegister) {
	    mm->endExceptionTable(F, TableStart, TableEnd, FrameRegister);
	}
};

#define FROM_GV(gv,t) ((t)(gv.IntVal.getZExtValue()))
static GenericValue
value2gv(VALUE v)
{
    GenericValue GV;
#if __LP64__
    GV.IntVal = APInt(64, v);
#else
    GV.IntVal = APInt(32, v);
#endif
    return GV;
}
#define VALUE_TO_GV(v) (value2gv((VALUE)v))

extern "C" void *__cxa_allocate_exception(size_t);
extern "C" void __cxa_throw(void *, void *, void (*)(void *));

RoxorCore::RoxorCore(void)
{
    running = false;
    multithreaded = false;
    abort_on_exception = false;

    pthread_assert(pthread_mutex_init(&gl, 0));

    load_path = rb_ary_new();
    rb_objc_retain((void *)load_path);

    loaded_features = rb_ary_new();
    rb_objc_retain((void *)loaded_features);

    threads = rb_ary_new();
    rb_objc_retain((void *)threads);

    bs_parser = NULL;

    emp = new ExistingModuleProvider(RoxorCompiler::module);
    jmm = new RoxorJITManager;
    ee = ExecutionEngine::createJIT(emp, 0, jmm, CodeGenOpt::None);
    assert(ee != NULL);

    fpm = new FunctionPassManager(emp);
    fpm->add(new TargetData(*ee->getTargetData()));

    // Eliminate unnecessary alloca.
    fpm->add(createPromoteMemoryToRegisterPass());
    // Do simple "peephole" optimizations and bit-twiddling optzns.
    fpm->add(createInstructionCombiningPass());
    // Reassociate expressions.
    fpm->add(createReassociatePass());
    // Eliminate Common SubExpressions.
    fpm->add(createGVNPass());
    // Simplify the control flow graph (deleting unreachable blocks, etc).
    fpm->add(createCFGSimplificationPass());
    // Eliminate tail calls.
    fpm->add(createTailCallEliminationPass());

    iee = ExecutionEngine::create(emp, true);
    assert(iee != NULL);

#if ROXOR_VM_DEBUG
    functions_compiled = 0;
#endif
}

RoxorVM::RoxorVM(void)
{
    current_top_object = Qnil;
    current_class = NULL;
    safe_level = 0;
    backref = Qnil;
    broken_with = Qundef;
    last_status = Qnil;
    errinfo = Qnil;
    parse_in_eval = false;
}

static inline void *
block_cache_key(const rb_vm_block_t *b)
{
    if ((b->flags & VM_BLOCK_IFUNC) == VM_BLOCK_IFUNC) {
	return (void *)b->imp;
    }
    return (void *)b->userdata;
}

RoxorVM::RoxorVM(const RoxorVM &vm)
{
    current_top_object = vm.current_top_object;
    current_class = vm.current_class;
    safe_level = vm.safe_level;

    std::vector<rb_vm_block_t *> &vm_blocks =
	const_cast<RoxorVM &>(vm).current_blocks;

    for (std::vector<rb_vm_block_t *>::iterator i = vm_blocks.begin();
	 (i + 1) != vm_blocks.end();
	 ++i) {

	const rb_vm_block_t *orig = *i;
	rb_vm_block_t *b = NULL;
	if (orig != NULL) {
#if 1
	    b = const_cast<rb_vm_block_t *>(orig);
#else
	    // XXX: This code does not work yet, it raises a failed integrity
	    // check when running the specs.
	    const size_t block_size = sizeof(rb_vm_block_t *)
		+ (orig->dvars_size * sizeof(VALUE *));

	    b = (rb_vm_block_t *)xmalloc(block_size);
	    memcpy(b, orig, block_size);

	    b->proc = orig->proc; // weak
	    GC_WB(&b->self, orig->self);
	    GC_WB(&b->locals, orig->locals);
	    GC_WB(&b->parent_block, orig->parent_block);  // XXX not sure
#endif
	    rb_objc_retain(b);
	    blocks[block_cache_key(orig)] = b;
	}
	current_blocks.push_back(b);
    }

    // TODO bindings, exceptions?

    backref = Qnil;
    broken_with = Qundef;
    last_status = Qnil;
    errinfo = Qnil;
    parse_in_eval = false;
}

RoxorVM::~RoxorVM(void)
{
    // TODO
}

static void
append_ptr_address(std::string &s, void *ptr)
{
    char buf[100];
    snprintf(buf, sizeof buf, "%p", ptr);
    s.append(buf);
}

std::string
RoxorVM::debug_blocks(void)
{
    std::string s;
    if (current_blocks.empty()) {
	s.append("empty");
    }
    else {
	for (std::vector<rb_vm_block_t *>::iterator i = current_blocks.begin();
	     i != current_blocks.end();
	     ++i) {
	    append_ptr_address(s, *i);
	    s.append(" ");
	}
    }
    return s;
}

std::string
RoxorVM::debug_exceptions(void)
{
    std::string s;
    if (current_exceptions.empty()) {
	s.append("empty");
    }
    else {
	for (std::vector<VALUE>::iterator i = current_exceptions.begin();
	     i != current_exceptions.end();
	     ++i) {
	    append_ptr_address(s, (void *)*i);
	    s.append(" ");
	}
    }
    return s;
}

inline void
RoxorCore::optimize(Function *func)
{
    fpm->run(*func);
}

IMP
RoxorCore::compile(Function *func)
{
    std::map<Function *, IMP>::iterator iter = JITcache.find(func);
    if (iter != JITcache.end()) {
	return iter->second;
    }

#if ROXOR_COMPILER_DEBUG
    if (verifyModule(*RoxorCompiler::module, PrintMessageAction)) {
	printf("Error during module verification\n");
	exit(1);
    }

    uint64_t start = mach_absolute_time();
#endif

    // Optimize & compile.
    optimize(func);
    IMP imp = (IMP)ee->getPointerToFunction(func);
    JITcache[func] = imp;

#if ROXOR_COMPILER_DEBUG
    uint64_t elapsed = mach_absolute_time() - start;

    static mach_timebase_info_data_t sTimebaseInfo;

    if (sTimebaseInfo.denom == 0) {
	(void) mach_timebase_info(&sTimebaseInfo);
    }

    uint64_t elapsedNano = elapsed * sTimebaseInfo.numer / sTimebaseInfo.denom;

    fprintf(stderr, "compilation of LLVM function %p done, took %lld ns\n",
	func, elapsedNano);
#endif

#if ROXOR_VM_DEBUG
    functions_compiled++;
#endif

    return imp;
}

VALUE
RoxorCore::interpret(Function *func)
{
    std::vector<GenericValue> args;
    args.push_back(PTOGV((void *)GET_VM()->get_current_top_object()));
    args.push_back(PTOGV(NULL));
    return (VALUE)iee->runFunction(func, args).IntVal.getZExtValue();
}

bool
RoxorCore::symbolize_call_address(void *addr, void **startp, char *path,
	size_t path_len, unsigned long *ln, char *name, size_t name_len)
{
    void *start = NULL;

    RoxorFunction *f = jmm->find_function((unsigned char *)addr);
    if (f != NULL) {
	if (f->imp == NULL) {
	    f->imp = ee->getPointerToFunctionOrStub(f->f);
	}
	start = f->imp;
    }
    else {
	if (!rb_objc_symbolize_address(addr, &start, NULL, 0)) {
	    return false;
	}
    }

    assert(start != NULL);
    if (startp != NULL) {
	*startp = start;
    }

    if (name != NULL || path != NULL || ln != NULL) {
	std::map<IMP, rb_vm_method_node_t *>::iterator iter = 
	    ruby_imps.find((IMP)start);
	if (iter == ruby_imps.end()) {
	    // TODO symbolize objc selectors
	    return false;
	}

	rb_vm_method_node_t *node = iter->second;

	RoxorFunctionAnnotation *annotation = f == NULL
	    ? NULL
	    : RoxorFunctionAnnotation::from_function(f->f);
	if (ln != NULL) {
	    if (annotation != NULL) {
#if __LP64__
		// So, we need to determine here which call to the dispatcher
		// we are exactly, so that we can retrieve the appropriate
		// line number from the annotation.
		// Unfortunately, the only way to achieve that seems to scan
		// the current function's machine code.
		// This code has only been tested on x86_64 but could be
		// easily ported to i386.
		const uint32_t sym = *(uint32_t *)((unsigned char *)addr - 8);
		const int sentinel = sym & 0xff;

		unsigned char *p = f->start;
		unsigned int i = 0;
		while ((p = (unsigned char *)memchr(p, sentinel,
				(unsigned char *)addr - p)) != NULL) {
		    if (*(uint32_t *)p == sym) {
			i++;
		    }
		    p++;
		}

		if (i > 0 && i - 1 < annotation->dispatch_lines.size()) {
		    *ln = annotation->dispatch_lines[i - 1];
		}
		else {
		    *ln = 0;
		}
#else
		// TODO 32-bit hack...
		*ln = 0;
#endif
	    }
	    else {
		*ln = 0;
	    }
	}
	if (path != NULL) {
	    if (annotation != NULL) {
		strncpy(path, annotation->path.c_str(), path_len);
	    }
	    else {
		strncpy(path, "core", path_len);
	    }
	}
	if (name != NULL) {
	    strncpy(name, sel_getName(node->sel), name_len);
	}
    }

    return true;
}

struct ccache *
RoxorCore::constant_cache_get(ID path)
{
    std::map<ID, struct ccache *>::iterator iter = ccache.find(path);
    if (iter == ccache.end()) {
	struct ccache *cache = (struct ccache *)malloc(sizeof(struct ccache));
	cache->outer = 0;
	cache->val = Qundef;
	ccache[path] = cache;
	return cache;
    }
    return iter->second;
}

extern "C"
void *
rb_vm_get_constant_cache(const char *name)
{
    return GET_CORE()->constant_cache_get(rb_intern(name));
}

struct mcache *
RoxorCore::method_cache_get(SEL sel, bool super)
{
    if (super) {
	struct mcache *cache = (struct mcache *)malloc(sizeof(struct mcache));
	cache->flag = 0;
	// TODO store the cache somewhere and invalidate it appropriately.
	return cache;
    }
    std::map<SEL, struct mcache *>::iterator iter = mcache.find(sel);
    if (iter == mcache.end()) {
	struct mcache *cache = (struct mcache *)malloc(sizeof(struct mcache));
	cache->flag = 0;
	mcache[sel] = cache;
	return cache;
    }
    return iter->second;
}

extern "C"
void *
rb_vm_get_method_cache(SEL sel)
{
    return GET_CORE()->method_cache_get(sel, false); 
}

inline rb_vm_method_node_t *
RoxorCore::method_node_get(IMP imp)
{
    std::map<IMP, rb_vm_method_node_t *>::iterator iter = ruby_imps.find(imp);
    return iter == ruby_imps.end() ? NULL : iter->second;
}

inline rb_vm_method_node_t *
RoxorCore::method_node_get(Method m, bool create)
{
    rb_vm_method_node_t *n;
    std::map<Method, rb_vm_method_node_t *>::iterator iter =
	ruby_methods.find(m);
    if (iter == ruby_methods.end()) {
	if (create) {
	    n = (rb_vm_method_node_t *)malloc(sizeof(rb_vm_method_node_t));
	    ruby_methods[m] = n;
	}
	else {
	    n = NULL;
	}
    }
    else {
	n = iter->second;
    }
    return n;
}

extern "C"
bool
rb_vm_is_ruby_method(Method m)
{
    return GET_CORE()->method_node_get(m) != NULL;
}

size_t
RoxorCore::get_sizeof(const Type *type)
{
    return ee->getTargetData()->getTypeSizeInBits(type) / 8;
}

size_t
RoxorCore::get_sizeof(const char *type)
{
    return get_sizeof(RoxorCompiler::shared->convert_type(type));
}

bool
RoxorCore::is_large_struct_type(const Type *type)
{
    return type->getTypeID() == Type::StructTyID
	&& ee->getTargetData()->getTypeSizeInBits(type) > 128;
}

inline GlobalVariable *
RoxorCore::redefined_op_gvar(SEL sel, bool create)
{
    std::map <SEL, GlobalVariable *>::iterator iter =
	redefined_ops_gvars.find(sel);
    GlobalVariable *gvar = NULL;
    if (iter == redefined_ops_gvars.end()) {
	if (create) {
	    gvar = new GlobalVariable(
		    Type::Int1Ty,
		    ruby_aot_compile ? true : false,
		    GlobalValue::InternalLinkage,
		    ConstantInt::getFalse(),
		    "redefined",
		    RoxorCompiler::module);
	    assert(gvar != NULL);
	    redefined_ops_gvars[sel] = gvar;
	}
    }
    else {
	gvar = iter->second;
    }
    return gvar;
}

inline bool
RoxorCore::should_invalidate_inline_op(SEL sel, Class klass)
{
    if (sel == selEq || sel == selEqq || sel == selNeq) {
	return klass == (Class)rb_cFixnum
	    || klass == (Class)rb_cFloat
	    || klass == (Class)rb_cBignum
	    || klass == (Class)rb_cSymbol
	    || klass == (Class)rb_cNSString
	    || klass == (Class)rb_cNSMutableString
	    || klass == (Class)rb_cNSArray
	    || klass == (Class)rb_cNSMutableArray
	    || klass == (Class)rb_cNSHash
	    || klass == (Class)rb_cNSMutableHash;
    }
    if (sel == selPLUS || sel == selMINUS || sel == selDIV 
	|| sel == selMULT || sel == selLT || sel == selLE 
	|| sel == selGT || sel == selGE) {
	return klass == (Class)rb_cFixnum
	    || klass == (Class)rb_cFloat
	    || klass == (Class)rb_cBignum;
    }
    if (sel == selLTLT || sel == selAREF || sel == selASET) {
	return klass == (Class)rb_cNSArray
	    || klass == (Class)rb_cNSMutableArray;
    }
    if (sel == selSend || sel == sel__send__ || sel == selEval) {
	// Matches any class, since these are Kernel methods.
	return true;
    }

    printf("invalid inline op `%s' to invalidate!\n", sel_getName(sel));
    abort();
}

rb_vm_method_node_t *
RoxorCore::add_method(Class klass, SEL sel, IMP imp, IMP ruby_imp,
	const rb_vm_arity_t &arity, int flags, const char *types)
{
    // #initialize and #initialize_copy are always private.
    if (sel == selInitialize || sel == selInitialize2
	    || sel == selInitializeCopy) {
	flags |= VM_METHOD_PRIVATE;
    }

#if ROXOR_VM_DEBUG
    printf("defining %c[%s %s] with imp %p/%p types %s flags %d\n",
	    class_isMetaClass(klass) ? '+' : '-',
	    class_getName(klass),
	    sel_getName(sel),
	    imp,
	    ruby_imp,
	    types,
	    flags);
#endif

    // Register the implementation into the runtime.
    class_replaceMethod(klass, sel, imp, types);

    // Cache the method.
    Method m = class_getInstanceMethod(klass, sel);
    assert(m != NULL);
    assert(method_getImplementation(m) == imp);
    rb_vm_method_node_t *real_node = method_node_get(m, true);
    real_node->objc_imp = imp;
    real_node->ruby_imp = ruby_imp;
    real_node->arity = arity;
    real_node->flags = flags;
    real_node->sel = sel;

    // Cache the implementation.
    std::map<IMP, rb_vm_method_node_t *>::iterator iter2 = ruby_imps.find(imp);
    rb_vm_method_node_t *node;
    if (iter2 == ruby_imps.end()) {
	node = (rb_vm_method_node_t *)malloc(sizeof(rb_vm_method_node_t));
	node->objc_imp = imp;
	ruby_imps[imp] = node;
    }
    else {
	node = iter2->second;
	assert(node->objc_imp == imp);
    }
    node->arity = arity;
    node->flags = flags;
    node->sel = sel;
    node->ruby_imp = ruby_imp;
    if (imp != ruby_imp) {
	ruby_imps[ruby_imp] = node;
    }

    // Invalidate dispatch cache.
    std::map<SEL, struct mcache *>::iterator iter3 = mcache.find(sel);
    if (iter3 != mcache.end()) {
	iter3->second->flag = 0;
    }

    // Invalidate inline operations.
    if (running) {
	GlobalVariable *gvar = redefined_op_gvar(sel, false);
	if (gvar != NULL && should_invalidate_inline_op(sel, klass)) {
	    void *val = ee->getOrEmitGlobalVariable(gvar);
#if ROXOR_VM_DEBUG
	    printf("change redefined global for [%s %s] to true\n",
		    class_getName(klass),
		    sel_getName(sel));
#endif
	    assert(val != NULL);
	    *(bool *)val = true;
	}
    }

    // If alloc is redefined, mark the class as such.
    if (sel == selAlloc
	&& (RCLASS_VERSION(klass) & RCLASS_HAS_ROBJECT_ALLOC) 
	== RCLASS_HAS_ROBJECT_ALLOC) {
	RCLASS_SET_VERSION(klass, (RCLASS_VERSION(klass) ^ 
		    RCLASS_HAS_ROBJECT_ALLOC));
    }

    if (get_running()) {
	// Call method_added: or singleton_method_added:.
	VALUE sym = ID2SYM(rb_intern(sel_getName(sel)));
        if (RCLASS_SINGLETON(klass)) {
	    VALUE sk = rb_iv_get((VALUE)klass, "__attached__");
	    rb_vm_call(sk, selSingletonMethodAdded, 1, &sym, false);
        }
        else {
	    rb_vm_call((VALUE)klass, selMethodAdded, 1, &sym, false);
        }
    }

    // Forward method definition to the included classes.
    if (RCLASS_VERSION(klass) & RCLASS_IS_INCLUDED) {
	VALUE included_in_classes = rb_attr_get((VALUE)klass, 
		idIncludedInClasses);
	if (included_in_classes != Qnil) {
	    int i, count = RARRAY_LEN(included_in_classes);
	    for (i = 0; i < count; i++) {
		VALUE mod = RARRAY_AT(included_in_classes, i);
#if ROXOR_VM_DEBUG
		printf("forward %c[%s %s] with imp %p node %p types %s\n",
			class_isMetaClass((Class)mod) ? '+' : '-',
			class_getName((Class)mod),
			sel_getName(sel),
			imp,
			node,
			types);
#endif
		class_replaceMethod((Class)mod, sel, imp, types);

		Method m = class_getInstanceMethod((Class)mod, sel);
		assert(m != NULL);
		assert(method_getImplementation(m) == imp);
		node = method_node_get(m, true);
		node->objc_imp = imp;
		node->ruby_imp = ruby_imp;
		node->arity = arity;
		node->flags = flags;
		node->sel = sel;
	    }
	}
    }

    return real_node;
}

void
RoxorCore::const_defined(ID path)
{
    // Invalidate constant cache.
    std::map<ID, struct ccache *>::iterator iter = ccache.find(path);
    if (iter != ccache.end()) {
	iter->second->val = Qundef;
    }
}

inline int
RoxorCore::find_ivar_slot(VALUE klass, ID name, bool create)
{
    VALUE k = klass;
    int slot = 0;

    while (k != 0) {
	std::map <ID, int> *slots = get_ivar_slots((Class)k);
	std::map <ID, int>::iterator iter = slots->find(name);
	if (iter != slots->end()) {
#if ROXOR_VM_DEBUG
	    printf("prepare ivar %s slot as %d (already prepared in class %s)\n",
		    rb_id2name(name), iter->second, class_getName((Class)k));
#endif
	    return iter->second;
	}
	slot += slots->size();
	k = RCLASS_SUPER(k);
    }

    if (create) {
#if ROXOR_VM_DEBUG
	printf("prepare ivar %s slot as %d (new in class %s)\n",
		rb_id2name(name), slot, class_getName((Class)klass));
#endif
	get_ivar_slots((Class)klass)->insert(std::pair<ID, int>(name, slot));
	return slot;
    }
    else {
	return -1;
    }
}

void
RoxorCore::each_ivar_slot(VALUE obj, int (*func)(ANYARGS),
	void *ctx)
{
    VALUE k = *(VALUE *)obj;

    while (k != 0) {
	std::map <ID, int> *slots = get_ivar_slots((Class)k, false);
	if (slots != NULL) {
	    for (std::map <ID, int>::iterator iter = slots->begin();
		 iter != slots->end();
		 ++iter) {
		ID name = iter->first;
		int slot = iter->second;
		VALUE value = rb_vm_get_ivar_from_slot(obj, slot);
		if (value != Qundef) {
		    func(name, value, ctx);
		}
	    }
	}
	k = RCLASS_SUPER(k);
    }
}

inline bool
RoxorCore::class_can_have_ivar_slots(VALUE klass)
{
    const long klass_version = RCLASS_VERSION(klass);
    if ((klass_version & RCLASS_IS_RUBY_CLASS) != RCLASS_IS_RUBY_CLASS
	|| (klass_version & RCLASS_IS_OBJECT_SUBCLASS)
	    != RCLASS_IS_OBJECT_SUBCLASS
	|| klass == rb_cClass || klass == rb_cModule) {
	return false;
    }
    return true;
}

extern "C"
bool
rb_vm_running(void)
{
    return GET_CORE()->get_running();
}

extern "C"
void
rb_vm_set_running(bool flag)
{
    GET_CORE()->set_running(flag); 
}

extern "C"
bool
rb_vm_abort_on_exception(void)
{
    return GET_CORE()->get_abort_on_exception();
}

extern "C"
void
rb_vm_set_abort_on_exception(bool flag)
{
    GET_CORE()->set_abort_on_exception(flag);
}

extern "C"
void 
rb_vm_set_const(VALUE outer, ID id, VALUE obj, unsigned char dynamic_class)
{
    if (dynamic_class) {
	Class k = GET_VM()->get_current_class();
	if (k != NULL) {
	    outer = (VALUE)k;
	}
    }
#if ROXOR_VM_DEBUG
    printf("define const %s::%s to %p\n", 
	    class_getName((Class)outer), 
	    rb_id2name(id),
	    (void *)obj);
#endif
    rb_const_set(outer, id, obj);
    GET_CORE()->const_defined(id);
}

static inline VALUE
rb_const_get_direct(VALUE klass, ID id)
{
    // Search the given class.
    CFDictionaryRef iv_dict = rb_class_ivar_dict(klass);
    if (iv_dict != NULL) {
	VALUE value;
	if (CFDictionaryGetValueIfPresent(iv_dict, (const void *)id,
		    (const void **)&value)) {
	    return value;
	}
    }
    // Search the included modules.
    VALUE mods = rb_attr_get(klass, idIncludedModules);
    if (mods != Qnil) {
	int i, count = RARRAY_LEN(mods);
	for (i = 0; i < count; i++) {
	    VALUE val = rb_const_get_direct(RARRAY_AT(mods, i), id);
	    if (val != Qundef) {
		return val;
	    }
	}
    }
    return Qundef;
}

static VALUE
rb_vm_const_lookup(VALUE outer, ID path, bool lexical, bool defined)
{
    if (lexical) {
	// Let's do a lexical lookup before a hierarchical one, by looking for
	// the given constant in all modules under the given outer.
	GET_CORE()->lock();
	struct rb_vm_outer *o = GET_CORE()->get_outer((Class)outer);
	while (o != NULL && o->klass != (Class)rb_cNSObject) {
	    VALUE val = rb_const_get_direct((VALUE)o->klass, path);
	    if (val != Qundef) {
		GET_CORE()->unlock();
		return defined ? Qtrue : val;
	    }
	    o = o->outer;
	}
	GET_CORE()->unlock();
    }

    // Nothing was found earlier so here we do a hierarchical lookup.
    return defined ? rb_const_defined(outer, path) : rb_const_get(outer, path);
}

static inline void
check_if_module(VALUE mod)
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

extern "C"
VALUE
rb_vm_get_const(VALUE outer, unsigned char lexical_lookup,
	struct ccache *cache, ID path, unsigned char dynamic_class)
{
    if (dynamic_class) {
	Class k = GET_VM()->get_current_class();
	if (lexical_lookup && k != NULL) {
	    outer = (VALUE)k;
	}
    }

    assert(cache != NULL);

    VALUE val;
    if (cache->outer == outer && cache->val != Qundef) {
	val = cache->val;
    }
    else {
	check_if_module(outer);
	val = rb_vm_const_lookup(outer, path, lexical_lookup, false);
	cache->outer = outer;
	cache->val = val;
    }

    return val;
}

extern "C"
void
rb_vm_const_is_defined(ID path)
{
    GET_CORE()->const_defined(path);
}

extern "C"
void
rb_vm_set_outer(VALUE klass, VALUE under)
{
    GET_CORE()->set_outer((Class)klass, (Class)under);
}

extern "C"
VALUE
rb_vm_get_outer(VALUE klass)
{
    rb_vm_outer_t *o = GET_CORE()->get_outer((Class)klass);
    return o == NULL ? Qundef : (VALUE)o->klass;
}

extern "C"
VALUE
rb_vm_define_class(ID path, VALUE outer, VALUE super, int flags,
	unsigned char dynamic_class)
{
    assert(path > 0);
    check_if_module(outer);

    if (dynamic_class) {
	Class k = GET_VM()->get_current_class();
	if (k != NULL) {
	    outer = (VALUE)k;
	}
    }

    VALUE klass;
    if (rb_const_defined_at(outer, path)) {
	klass = rb_const_get_at(outer, path);
	check_if_module(klass);
	if (!(flags & DEFINE_MODULE) && super != 0) {
	    if (RCLASS_SUPER(klass) != super) {
		rb_raise(rb_eTypeError, "superclass mismatch for class %s",
			rb_class2name(klass));
	    }
	}
    }
    else {
	if (flags & DEFINE_MODULE) {
	    assert(super == 0);
	    klass = rb_define_module_id(path);
	    rb_set_class_path2(klass, outer, rb_id2name(path),
		    flags & DEFINE_OUTER);
	    rb_const_set(outer, path, klass);
	}
	else {
	    if (super == 0) {
		super = rb_cObject;
	    }
	    else {
		check_if_module(super);
	    }
	    klass = rb_define_class_id(path, super);
	    rb_set_class_path2(klass, outer, rb_id2name(path),
		    flags & DEFINE_OUTER);
	    rb_const_set(outer, path, klass);
	    rb_class_inherited(super, klass);
	}
    }

#if ROXOR_VM_DEBUG
    if (flags & DEFINE_MODULE) {
	printf("define module %s::%s\n", 
		class_getName((Class)outer), 
		rb_id2name(path));
    }
    else {
	printf("define class %s::%s < %s\n", 
		class_getName((Class)outer), 
		rb_id2name(path), 
		class_getName((Class)super));
    }
#endif

    return klass;
}

extern "C"
GenericValue
lle_X_rb_vm_define_class(const FunctionType *FT,
			 const std::vector<GenericValue> &Args)
{
    assert(Args.size() == 4);

    return VALUE_TO_GV(
	    rb_vm_define_class(
		FROM_GV(Args[0], ID),
		FROM_GV(Args[1], VALUE),
		FROM_GV(Args[2], VALUE),
		FROM_GV(Args[3], int),
		FROM_GV(Args[3], unsigned char)));
}

extern "C"
VALUE
rb_vm_ivar_get(VALUE obj, ID name, int *slot_cache)
{
#if ROXOR_VM_DEBUG
    printf("get ivar <%s %p>.%s slot %d\n",
	    class_getName((Class)CLASS_OF(obj)), (void *)obj,
	    rb_id2name(name), slot_cache == NULL ? -1 : *slot_cache);
#endif
    if (slot_cache == NULL || *slot_cache == -1) {
	return rb_ivar_get(obj, name);
    }
    else {
	VALUE val = rb_vm_get_ivar_from_slot(obj, *slot_cache);
	return val == Qundef ? Qnil : val;
    }
}

extern "C"
void
rb_vm_ivar_set(VALUE obj, ID name, VALUE val, int *slot_cache)
{
#if ROXOR_VM_DEBUG
    printf("set ivar %p.%s slot %d new_val %p\n", (void *)obj, 
	    rb_id2name(name), 
	    slot_cache == NULL ? -1 : *slot_cache,
	    (void *)val);
#endif
    if (slot_cache == NULL || *slot_cache == -1) {
	rb_ivar_set(obj, name, val);
    }
    else {
	rb_vm_set_ivar_from_slot(obj, val, *slot_cache);
    }
}

extern "C"
VALUE
rb_vm_cvar_get(VALUE klass, ID id, unsigned char check,
	unsigned char dynamic_class)
{
    if (dynamic_class) {
	Class k = GET_VM()->get_current_class();
	if (k != NULL) {
	    klass = (VALUE)k;
	}
    }
    return rb_cvar_get2(klass, id, check);
}

extern "C"
VALUE
rb_vm_cvar_set(VALUE klass, ID id, VALUE val, unsigned char dynamic_class)
{
    if (dynamic_class) {
	Class k = GET_VM()->get_current_class();
	if (k != NULL) {
	    klass = (VALUE)k;
	}
    }
    rb_cvar_set(klass, id, val);
    return val;
}

extern "C"
VALUE
rb_vm_ary_cat(VALUE ary, VALUE obj)
{
    if (TYPE(obj) == T_ARRAY) {
	rb_ary_concat(ary, obj);
    }
    else {
	VALUE ary2 = rb_check_convert_type(obj, T_ARRAY, "Array", "to_a");
	if (!NIL_P(ary2)) {
	    rb_ary_concat(ary, ary2);
	}
	else {
	    rb_ary_push(ary, obj);
	}
    }
    return ary;
}

extern "C"
VALUE
rb_vm_to_a(VALUE obj)
{
    VALUE ary = rb_check_convert_type(obj, T_ARRAY, "Array", "to_a");
    if (NIL_P(ary)) {
	ary = rb_ary_new3(1, obj);
    }
    return ary;
}

extern "C"
VALUE
rb_vm_to_ary(VALUE obj)
{
    VALUE ary = rb_check_convert_type(obj, T_ARRAY, "Array", "to_ary");
    if (NIL_P(ary)) {
	ary = rb_ary_new3(1, obj);
    }
    return ary;
}

extern "C" void rb_print_undef(VALUE, ID, int);

static void
rb_vm_alias_method(Class klass, Method method, ID name, bool noargs)
{
    IMP imp = method_getImplementation(method);
    const char *types = method_getTypeEncoding(method);

    rb_vm_method_node_t *node = GET_CORE()->method_node_get(method);
    if (node == NULL) {
	rb_raise(rb_eArgError,
		"only pure Ruby methods can be aliased (`%s' is not)",
		sel_getName(method_getName(method)));
    }

    const char *name_str = rb_id2name(name);
    SEL sel;
    if (noargs) {
	sel = sel_registerName(name_str);
    }
    else {
	char tmp[100];
	snprintf(tmp, sizeof tmp, "%s:", name_str);
	sel = sel_registerName(tmp);
    }

    GET_CORE()->add_method(klass, sel, imp, node->ruby_imp,
	    node->arity, node->flags, types);
}

extern "C"
void
rb_vm_alias2(VALUE outer, ID name, ID def, unsigned char dynamic_class)
{
    if (dynamic_class) {
	Class k = GET_VM()->get_current_class();
	if (k != NULL) {
	    outer = (VALUE)k;
	}
    }
    rb_frozen_class_p(outer);
    if (outer == rb_cObject) {
        rb_secure(4);
    }
    Class klass = (Class)outer;

    const char *def_str = rb_id2name(def);
    SEL sel = sel_registerName(def_str);
    Method def_method1 = class_getInstanceMethod(klass, sel);
    Method def_method2 = NULL;
    if (def_str[strlen(def_str) - 1] != ':') {
	char tmp[100];
	snprintf(tmp, sizeof tmp, "%s:", def_str);
	sel = sel_registerName(tmp);
 	def_method2 = class_getInstanceMethod(klass, sel);
    }

    if (def_method1 == NULL && def_method2 == NULL) {
	rb_print_undef((VALUE)klass, def, 0);
    }
    if (def_method1 != NULL) {
	rb_vm_alias_method(klass, def_method1, name, true);
    }
    if (def_method2 != NULL) {
	rb_vm_alias_method(klass, def_method2, name, false);
    }
}

extern "C"
void
rb_vm_alias(VALUE outer, ID name, ID def)
{
    rb_vm_alias2(outer, name, def, false);
}

extern "C"
void
rb_vm_undef(VALUE klass, VALUE name, unsigned char dynamic_class)
{
    if (dynamic_class) {
	Class k = GET_VM()->get_current_class();
	if (k != NULL) {
	    klass = (VALUE)k;
	}
    }
    rb_vm_undef_method((Class)klass, rb_to_id(name), true);
}

extern "C"
VALUE
rb_vm_defined(VALUE self, int type, VALUE what, VALUE what2)
{
    const char *str = NULL;

    switch (type) {
	case DEFINED_IVAR:
	    if (rb_ivar_defined(self, (ID)what)) {
		str = "instance-variable";
	    }
	    break;

	case DEFINED_GVAR:
	    if (rb_gvar_defined((struct global_entry *)what)) {
		str = "global-variable";
	    }
	    break;

	case DEFINED_CVAR:
	    if (rb_cvar_defined(CLASS_OF(self), (ID)what)) {
		str = "class variable";
	    }
	    break;

	case DEFINED_CONST:
	case DEFINED_LCONST:
	    {
		if (rb_vm_const_lookup(what2, (ID)what,
			    type == DEFINED_LCONST, true)) {
		    str = "constant";
		}
	    }
	    break;

	case DEFINED_SUPER:
	case DEFINED_METHOD:
	    {
		VALUE klass = CLASS_OF(self);
		if (type == DEFINED_SUPER) {
		    klass = RCLASS_SUPER(klass);
		}
		const char *idname = rb_id2name((ID)what);
		SEL sel = sel_registerName(idname);

		bool ok = class_getInstanceMethod((Class)klass, sel) != NULL;
		if (!ok && idname[strlen(idname) - 1] != ':') {
		    char buf[100];
		    snprintf(buf, sizeof buf, "%s:", idname);
		    sel = sel_registerName(buf);
		    ok = class_getInstanceMethod((Class)klass, sel) != NULL;
		}

		if (ok) {
		    str = type == DEFINED_SUPER ? "super" : "method";
		}
	    }
	    break;

	default:
	    printf("unknown defined? type %d", type);
	    abort();
    }

    return str == NULL ? Qnil : rb_str_new2(str);
}

extern "C"
void
rb_vm_prepare_class_ivar_slot(VALUE klass, ID name, int *slot_cache)
{
    assert(slot_cache != NULL);
    assert(*slot_cache == -1);

    if (GET_CORE()->class_can_have_ivar_slots(klass)) {
	*slot_cache = GET_CORE()->find_ivar_slot(klass, name, true);
    }
}

extern "C"
int
rb_vm_find_class_ivar_slot(VALUE klass, ID name)
{
    if (GET_CORE()->class_can_have_ivar_slots(klass)) {
	return GET_CORE()->find_ivar_slot(klass, name, false);
    }
    return -1;
}

extern "C"
void
rb_vm_each_ivar_slot(VALUE obj, int (*func)(ANYARGS), void *ctx)
{
    if (GET_CORE()->class_can_have_ivar_slots(CLASS_OF(obj))) {
	GET_CORE()->each_ivar_slot(obj, func, ctx);	
    } 
}

static inline void 
resolve_method_type(char *buf, const size_t buflen, Class klass, Method m,
	SEL sel, const unsigned int oc_arity)
{
    bs_element_method_t *bs_method = GET_CORE()->find_bs_method(klass, sel);

    if (m == NULL
	|| !rb_objc_get_types(Qnil, klass, sel, m, bs_method, buf, buflen)) {

	std::string *informal_type =
	    GET_CORE()->find_bs_informal_protocol_method(sel,
		    class_isMetaClass(klass));
	if (informal_type != NULL) {
	    strncpy(buf, informal_type->c_str(), buflen);
	}
	else {
	    assert(oc_arity < buflen);
	    buf[0] = '@';
	    buf[1] = '@';
	    buf[2] = ':';
	    for (unsigned int i = 3; i < oc_arity; i++) {
		buf[i] = '@';
	    }
	    buf[oc_arity] = '\0';
	}
    }
    else {
	const unsigned int m_argc = method_getNumberOfArguments(m);
	if (m_argc < oc_arity) {
	    for (unsigned int i = m_argc; i < oc_arity; i++) {
		strcat(buf, "@");
	    }
	}
    }
}

rb_vm_method_node_t *
RoxorCore::resolve_method(Class klass, SEL sel, Function *func,
	const rb_vm_arity_t &arity, int flags, IMP imp, Method m)
{
    if (imp == NULL) {
	// Compile if necessary.
	assert(func != NULL);
	imp = compile(func);
    }

    // Resolve Objective-C signature.
    const int oc_arity = arity.real + 3;
    char types[100];
    resolve_method_type(types, sizeof types, klass, m, sel, oc_arity);

    // Generate Objective-C stub if needed.
    std::map<IMP, IMP>::iterator iter = objc_to_ruby_stubs.find(imp);
    IMP objc_imp;
    if (iter == objc_to_ruby_stubs.end()) {
	Function *objc_func = RoxorCompiler::shared->compile_objc_stub(func,
		imp, arity, types);
	objc_imp = compile(objc_func);
	objc_to_ruby_stubs[imp] = objc_imp;
    }
    else {
	objc_imp = iter->second;
    }

    // Delete the selector from the not-yet-JIT'ed cache if needed.
    std::multimap<Class, SEL>::iterator iter2, last2;
    iter2 = method_source_sels.find(klass);
    if (iter2 != method_source_sels.end()) {
	last2 = method_source_sels.upper_bound(klass);
	while (iter2 != last2) {
	    if (iter2->second == sel) {
		method_source_sels.erase(iter2);
		break;
	    }
	    ++iter2;
	}
    }

    // Finally, add the method.
    return add_method(klass, sel, objc_imp, imp, arity, flags, types);
}

bool
RoxorCore::resolve_methods(std::map<Class, rb_vm_method_source_t *> *map,
	Class klass, SEL sel)
{
    bool did_something = false;
    std::map<Class, rb_vm_method_source_t *>::iterator iter = map->begin();
    while (iter != map->end()) {
	Class k = iter->first;
	while (k != klass && k != NULL) {
	    k = class_getSuperclass(k);
	}

	if (k != NULL) {
	    rb_vm_method_source_t *m = iter->second;
	    resolve_method(iter->first, sel, m->func, m->arity, m->flags,
		    NULL, NULL);
	    map->erase(iter++);
	    free(m);
	    did_something = true;
	}
	else {
	    ++iter;
	}
    }

    return did_something;
}

static bool
rb_vm_resolve_method(Class klass, SEL sel)
{
    if (!GET_CORE()->get_running()) {
	return false;
    }

    GET_CORE()->lock();

    bool status = false;

#if ROXOR_VM_DEBUG
    printf("resolving %c[%s %s]\n",
	class_isMetaClass(klass) ? '+' : '-',
	class_getName(klass),
	sel_getName(sel));
#endif

    std::map<Class, rb_vm_method_source_t *> *map =
	GET_CORE()->method_sources_for_sel(sel, false);
    if (map == NULL) {
	goto bails;
    }

    // Find the class where the method should be defined.
    while (map->find(klass) == map->end() && klass != NULL) {
	klass = class_getSuperclass(klass);
    }
    if (klass == NULL) {
	goto bails;
    }

    // Now let's resolve all methods of the given name on the given class
    // and superclasses.
    status = GET_CORE()->resolve_methods(map, klass, sel);

bails:
    GET_CORE()->unlock();
    return status;
}

void
RoxorCore::prepare_method(Class klass, SEL sel, Function *func,
	const rb_vm_arity_t &arity, int flags)
{
#if ROXOR_VM_DEBUG
    printf("preparing %c[%s %s] on class %p LLVM func %p flags %d\n",
	    class_isMetaClass(klass) ? '+' : '-',
	    class_getName(klass),
	    sel_getName(sel),
	    klass,
	    func,
	    flags);
#endif

    std::map<Class, rb_vm_method_source_t *> *map =
	method_sources_for_sel(sel, true);

    std::map<Class, rb_vm_method_source_t *>::iterator iter = map->find(klass);

    rb_vm_method_source_t *m = NULL;
    if (iter == map->end()) {
	m = (rb_vm_method_source_t *)malloc(sizeof(rb_vm_method_source_t));
	map->insert(std::make_pair(klass, m));
	method_source_sels.insert(std::make_pair(klass, sel));
    }
    else {
	m = iter->second;
    }

    m->func = func;
    m->arity = arity;
    m->flags = flags;
}

static void
prepare_method(Class klass, bool dynamic_class, SEL sel, void *data,
	const rb_vm_arity_t &arity, int flags, bool precompiled)
{
    if (dynamic_class) {
	Class k = GET_VM()->get_current_class();
	if (k != NULL) {
	    const bool meta = class_isMetaClass(klass);
	    klass = k;
	    if (meta) {
		klass = *(Class *)klass;
	    }
	}
    }

    const long v = RCLASS_VERSION(klass);
    if (v & RCLASS_SCOPE_PRIVATE) {
	flags |= VM_METHOD_PRIVATE;
    }
    else if (v & RCLASS_SCOPE_PROTECTED) {
	flags |= VM_METHOD_PROTECTED;
    }

    if (sel == sel_ignored) {
	// TODO
	return;
    }

    const char *sel_name = sel_getName(sel);
    const bool genuine_selector = sel_name[strlen(sel_name) - 1] == ':';
    bool redefined = false;
    bool added_modfunc = false;
    SEL orig_sel = sel;
    Method m;
    IMP imp = NULL;

prepare_method:

    m = class_getInstanceMethod(klass, sel);
    if (precompiled) {
	imp = (IMP)data;
	GET_CORE()->resolve_method(klass, sel, NULL, arity, flags, imp, m);
    }
    else {
	Function *func = (Function *)data;
	if (m != NULL) {
	    // The method already exists - we need to JIT it.
	    if (imp == NULL) {
		imp = GET_CORE()->compile(func);
	    }
	    GET_CORE()->resolve_method(klass, sel, func, arity, flags, imp, m);
	}
	else {
	    // Let's keep the method and JIT it later on demand.
	    GET_CORE()->prepare_method(klass, sel, func, arity, flags);
	}
    }

    if (!redefined) {
	if (!genuine_selector && arity.max != arity.min) {
	    char buf[100];
	    snprintf(buf, sizeof buf, "%s:", sel_name);
	    sel = sel_registerName(buf);
	    redefined = true;

	    goto prepare_method;
	}
	else if (genuine_selector && arity.min == 0) {
	    char buf[100];
	    strlcpy(buf, sel_name, sizeof buf);
	    buf[strlen(buf) - 1] = 0; // remove the ending ':'
	    sel = sel_registerName(buf);
	    redefined = true;

	    goto prepare_method;
	}
    }

    if (RCLASS_VERSION(klass) & RCLASS_IS_INCLUDED) {
	VALUE included_in_classes = rb_attr_get((VALUE)klass, 
		idIncludedInClasses);
	if (included_in_classes != Qnil) {
	    int i, count = RARRAY_LEN(included_in_classes);
	    for (i = 0; i < count; i++) {
		VALUE mod = RARRAY_AT(included_in_classes, i);
		rb_vm_set_current_scope(mod, SCOPE_PUBLIC);
		prepare_method((Class)mod, false, orig_sel, data, arity,
			flags, precompiled);
		rb_vm_set_current_scope(mod, SCOPE_DEFAULT);
	    }
	}
    }

    if (!added_modfunc && (v & RCLASS_SCOPE_MOD_FUNC)) {
	added_modfunc = true;
	redefined = false;
	klass = *(Class *)klass;
	flags = 0;
	goto prepare_method;
    }
}

extern "C"
void
rb_vm_prepare_method(Class klass, unsigned char dynamic_class, SEL sel,
	Function *func, const rb_vm_arity_t arity, int flags)
{
    prepare_method(klass, dynamic_class, sel, (void *)func, arity,
	    flags, false);
}

extern "C"
void
rb_vm_prepare_method2(Class klass, unsigned char dynamic_class, SEL sel,
	IMP ruby_imp, const rb_vm_arity_t arity, int flags)
{
    prepare_method(klass, dynamic_class, sel, (void *)ruby_imp, arity,
	    flags, true);
}

static rb_vm_method_node_t * __rb_vm_define_method(Class klass, SEL sel,
	IMP objc_imp, IMP ruby_imp, const rb_vm_arity_t &arity, int flags,
	bool direct);

#define VISI(x) ((x)&NOEX_MASK)
#define VISI_CHECK(x,f) (VISI(x) == (f))

static void
push_method(VALUE ary, SEL sel, int flags, int (*filter) (VALUE, ID, VALUE))
{
    if (sel == sel_ignored) {
	return; 
    }

    const char *selname = sel_getName(sel);
    const size_t len = strlen(selname);
    char buf[100];

    const char *p = strchr(selname, ':');
    if (p != NULL && strchr(p + 1, ':') == NULL) {
	// remove trailing ':' for methods with arity 1
	assert(len < sizeof(buf));
	strncpy(buf, selname, len);
	buf[len - 1] = '\0';
	selname = buf;
    }
 
    ID mid = rb_intern(selname);
    VALUE sym = ID2SYM(mid);

    if (rb_ary_includes(ary, sym) == Qfalse) {
	int type = NOEX_PUBLIC;
	if (flags & VM_METHOD_PRIVATE) {
	    type = NOEX_PRIVATE;
	}
	else if (flags & VM_METHOD_PROTECTED) {
	    type = NOEX_PROTECTED;
	}
	(*filter)(sym, type, ary);
    }
} 

rb_vm_method_source_t *
RoxorCore::method_source_get(Class klass, SEL sel)
{
    std::map<SEL, std::map<Class, rb_vm_method_source_t *> *>::iterator iter
	= method_sources.find(sel);
    if (iter != method_sources.end()) {
	std::map<Class, rb_vm_method_source_t *> *m = iter->second;
	std::map<Class, rb_vm_method_source_t *>::iterator iter2
	    = m->find(klass);
	if (iter2 != m->end()) {
	    return iter2->second;
	}
    }
    return NULL;
}

void
RoxorCore::get_methods(VALUE ary, Class klass, bool include_objc_methods,
	int (*filter) (VALUE, ID, VALUE))
{
    // TODO take into account undefined methods

    unsigned int count;
    Method *methods = class_copyMethodList(klass, &count); 
    if (methods != NULL) {
	for (unsigned int i = 0; i < count; i++) {
	    Method m = methods[i];
	    rb_vm_method_node_t *node = method_node_get(m);
	    if (node == NULL && !include_objc_methods) {
		continue;
	    }
	    SEL sel = method_getName(m);
	    push_method(ary, sel, node == NULL ? 0 : node->flags, filter);
	}
	free(methods);
    }

    Class k = klass;
    do {
	std::multimap<Class, SEL>::iterator iter =
	    method_source_sels.find(k);

	if (iter != method_source_sels.end()) {
	    std::multimap<Class, SEL>::iterator last =
		method_source_sels.upper_bound(k);

	    for (; iter != last; ++iter) {
		SEL sel = iter->second;
		rb_vm_method_source_t *src = method_source_get(k, sel);
		assert(src != NULL);
		push_method(ary, sel, src->flags, filter);
	    }
	}

	k = class_getSuperclass(k);
    }
    while (k != NULL);
}

extern "C"
void
rb_vm_push_methods(VALUE ary, VALUE mod, bool include_objc_methods,
		   int (*filter) (VALUE, ID, VALUE))
{
    GET_CORE()->get_methods(ary, (Class)mod, include_objc_methods, filter);
}

#if 0
extern "C"
GenericValue
lle_X_rb_vm_prepare_method(const FunctionType *FT,
			   const std::vector<GenericValue> &Args)
{
    assert(Args.size() == 4);

    rb_vm_prepare_method(
	    FROM_GV(Args[0], Class),
	    (SEL)GVTOP(Args[1]),
	    (Function *)GVTOP(Args[2]),
	    (NODE *)GVTOP(Args[3]));

    GenericValue GV;
    GV.IntVal = 0;
    return GV;
}
#endif

extern "C"
void
rb_vm_copy_methods(Class from_class, Class to_class)
{
    GET_CORE()->copy_methods(from_class, to_class);
}

void
RoxorCore::copy_methods(Class from_class, Class to_class)
{
    Method *methods;
    unsigned int i, methods_count;

    // Copy existing Objective-C methods.
    methods = class_copyMethodList(from_class, &methods_count);
    if (methods != NULL) {
	for (i = 0; i < methods_count; i++) {
	    Method m = methods[i];
	    rb_vm_method_node_t *node = method_node_get(m);
	    if (node == NULL) {
		// Only copy pure-Ruby methods.
		continue;
	    }
	    SEL sel = method_getName(m);

#if ROXOR_VM_DEBUG
	    printf("copy %c[%s %s] to %s\n",
		    class_isMetaClass(from_class) ? '+' : '-',
		    class_getName(from_class),
		    sel_getName(sel),
		    class_getName(to_class));
#endif

	    class_replaceMethod(to_class,
		    sel,
		    method_getImplementation(m),
		    method_getTypeEncoding(m));

	    Method m2 = class_getInstanceMethod(to_class, sel);
	    assert(m2 != NULL);
	    assert(method_getImplementation(m2) == method_getImplementation(m));
	    rb_vm_method_node_t *node2 = method_node_get(m2, true);
	    memcpy(node2, node, sizeof(rb_vm_method_node_t));

	    std::map<Class, rb_vm_method_source_t *> *map =
		method_sources_for_sel(sel, false);
	    if (map != NULL) {
		// There might be some non-JIT'ed yet methods on subclasses.
		resolve_methods(map, to_class, sel);
	    }
	}
	free(methods);
    }

    // Copy methods that have not been JIT'ed yet.
    std::multimap<Class, SEL>::iterator iter =
	method_source_sels.find(from_class);

    if (iter != method_source_sels.end()) {
	std::multimap<Class, SEL>::iterator last =
	    method_source_sels.upper_bound(from_class);
	std::vector<SEL> sels_to_add;

	for (; iter != last; ++iter) {
	    SEL sel = iter->second;

	    std::map<Class, rb_vm_method_source_t *> *dict =
		method_sources_for_sel(sel, false);
	    if (dict == NULL) {
		continue;
	    }

	    std::map<Class, rb_vm_method_source_t *>::iterator
		iter2 = dict->find(from_class);
	    if (iter2 == dict->end()) {
		continue;
	    }

#if ROXOR_VM_DEBUG
	    printf("lazy copy %c[%s %s] to %s\n",
		    class_isMetaClass(from_class) ? '+' : '-',
		    class_getName(from_class),
		    sel_getName(sel),
		    class_getName(to_class));
#endif

	    rb_vm_method_source_t *m = (rb_vm_method_source_t *)
		malloc(sizeof(rb_vm_method_source_t));
	    m->func = iter2->second->func;
	    m->arity = iter2->second->arity;
	    m->flags = iter2->second->flags;
	    dict->insert(std::make_pair(to_class, m));
	    sels_to_add.push_back(sel);
	}

	for (std::vector<SEL>::iterator i = sels_to_add.begin();
	     i != sels_to_add.end();
	     ++i) {
	    method_source_sels.insert(std::make_pair(to_class, *i));
	}
    } 
}

extern "C"
bool
rb_vm_lookup_method2(Class klass, ID mid, SEL *psel, IMP *pimp,
		     rb_vm_method_node_t **pnode)
{
    const char *idstr = rb_id2name(mid);
    SEL sel = sel_registerName(idstr);

    if (!rb_vm_lookup_method(klass, sel, pimp, pnode)) {
	char buf[100];
	snprintf(buf, sizeof buf, "%s:", idstr);
	sel = sel_registerName(buf);
	if (!rb_vm_lookup_method(klass, sel, pimp, pnode)) {
	    return false;
	}
    }

    if (psel != NULL) {
	*psel = sel;
    }
    return true;
}

extern "C"
bool
rb_vm_lookup_method(Class klass, SEL sel, IMP *pimp,
		    rb_vm_method_node_t **pnode)
{
    Method m = class_getInstanceMethod(klass, sel);
    if (m == NULL) {
	return false;
    }
    IMP imp = method_getImplementation(m);
    if (pimp != NULL) {
	*pimp = imp;
    }
    if (pnode != NULL) {
	*pnode = GET_CORE()->method_node_get(m);
    }
    return true;
}

extern "C"
void
rb_vm_define_attr(Class klass, const char *name, bool read, bool write)
{
    assert(klass != NULL);
    assert(read || write);

    char buf[100];
    snprintf(buf, sizeof buf, "@%s", name);
    ID iname = rb_intern(buf);

    if (read) {
	Function *f = RoxorCompiler::shared->compile_read_attr(iname);
	SEL sel = sel_registerName(name);
	rb_vm_prepare_method(klass, false, sel, f, rb_vm_arity(0),
		VM_METHOD_FBODY);
    }

    if (write) {
	Function *f = RoxorCompiler::shared->compile_write_attr(iname);
	snprintf(buf, sizeof buf, "%s=:", name);
	SEL sel = sel_registerName(buf);
	rb_vm_prepare_method(klass, false, sel, f, rb_vm_arity(1),
		VM_METHOD_FBODY);
    }
}

static rb_vm_method_node_t *
__rb_vm_define_method(Class klass, SEL sel, IMP objc_imp, IMP ruby_imp,
		      const rb_vm_arity_t &arity, int flags, bool direct)
{
    assert(klass != NULL);

    if (sel == sel_ignored) {
	// TODO
	return NULL;
    }

    const char *sel_name = sel_getName(sel);
    const bool genuine_selector = sel_name[strlen(sel_name) - 1] == ':';
    int oc_arity = genuine_selector ? arity.real : 0;
    bool redefined = direct;
    rb_vm_method_node_t *node;

define_method:
    Method method = class_getInstanceMethod(klass, sel);

    char types[100];
    resolve_method_type(types, sizeof types, klass, method, sel, oc_arity);

    node = GET_CORE()->add_method(klass, sel, objc_imp, ruby_imp, arity,
	    flags, types);

    if (!redefined) {
	if (!genuine_selector && arity.max != arity.min) {
	    char buf[100];
	    snprintf(buf, sizeof buf, "%s:", sel_name);
	    sel = sel_registerName(buf);
	    oc_arity = arity.real;
	    redefined = true;

	    goto define_method;
	}
	else if (genuine_selector && arity.min == 0) {
	    char buf[100];
	    strlcpy(buf, sel_name, sizeof buf);
	    buf[strlen(buf) - 1] = 0; // remove the ending ':'
	    sel = sel_registerName(buf);
	    oc_arity = 0;
	    redefined = true;

	    goto define_method;
	}
    }

    return node;
}

extern "C"
rb_vm_method_node_t * 
rb_vm_define_method(Class klass, SEL sel, IMP imp, NODE *node, bool direct)
{
    assert(node != NULL);

    // TODO: create objc_imp
    return __rb_vm_define_method(klass, sel, imp, imp, rb_vm_node_arity(node),
	    rb_vm_node_flags(node), direct);
}

extern "C"
rb_vm_method_node_t * 
rb_vm_define_method2(Class klass, SEL sel, rb_vm_method_node_t *node,
	bool direct)
{
    assert(node != NULL);

    long flags = node->flags;
    flags &= ~VM_METHOD_PRIVATE;
    flags &= ~VM_METHOD_PROTECTED;

    return __rb_vm_define_method(klass, sel, node->objc_imp, node->ruby_imp,
	    node->arity, flags, direct);
}

extern "C"
void
rb_vm_define_method3(Class klass, SEL sel, rb_vm_block_t *block)
{
    assert(block != NULL);

    Function *func = RoxorCompiler::shared->compile_block_caller(block);
    IMP imp = GET_CORE()->compile(func);
    NODE *body = rb_vm_cfunc_node_from_imp(klass, -1, imp, 0);
    rb_objc_retain(body);
    rb_objc_retain(block);

    rb_vm_define_method(klass, sel, imp, body, false);
}

static VALUE method_missing(VALUE obj, SEL sel, rb_vm_block_t *block, int argc, const VALUE *argv,
	rb_vm_method_missing_reason_t call_status);

static void *
undefined_imp(void *rcv, SEL sel)
{
    method_missing((VALUE)rcv, sel, NULL, NULL, NULL, METHOD_MISSING_DEFAULT);
    return NULL; // never reached
}

#define UNDEFINED_IMP(imp) (imp == NULL || imp == (IMP)undefined_imp)

void
RoxorCore::undef_method(Class klass, SEL sel)
{
#if ROXOR_VM_DEBUG
    printf("undef %c[%s %s]\n",
	    class_isMetaClass(klass) ? '+' : '-',
	    class_getName(klass),
	    sel_getName(sel));
#endif

    class_replaceMethod((Class)klass, sel, (IMP)undefined_imp, "@@:");

#if 0
    std::map<Method, rb_vm_method_node_t *>::iterator iter
	= ruby_methods.find(m);
    assert(iter != ruby_methods.end());
    free(iter->second);
    ruby_methods.erase(iter);
#endif

#if 0
    // TODO call undefined
    if (RCLASS_SINGLETON(klass)) {
	rb_funcall(rb_iv_get(klass, "__attached__"),
		   singleton_undefined, 1, ID2SYM(id));
    }
    else {
	rb_funcall(klass, undefined, 1, ID2SYM(id));
    }
#endif
}

extern "C"
void
rb_vm_undef_method(Class klass, ID name, bool must_exist)
{
    rb_vm_method_node_t *node = NULL;

    if (!rb_vm_lookup_method2((Class)klass, name, NULL, NULL, &node)) {
	if (must_exist) {
	    rb_raise(rb_eNameError, "undefined method `%s' for %s `%s'",
		    rb_id2name(name),
		    TYPE(klass) == T_MODULE ? "module" : "class",
		    rb_class2name((VALUE)klass));
	}
	const char *namestr = rb_id2name(name);
	SEL sel = sel_registerName(namestr);
	GET_CORE()->undef_method(klass, sel);
    }
    else if (node == NULL) {
	if (must_exist) {
	    rb_raise(rb_eRuntimeError,
		    "cannot undefine method `%s' because it is a native method",
		    rb_id2name(name));
	}
    }
    else {
	GET_CORE()->undef_method(klass, node->sel);
    }
}

extern "C"
VALUE
rb_vm_masgn_get_elem_before_splat(VALUE ary, int offset)
{
    if (offset < RARRAY_LEN(ary)) {
	return RARRAY_AT(ary, offset);
    }
    return Qnil;
}

extern "C"
VALUE
rb_vm_masgn_get_elem_after_splat(VALUE ary, int before_splat_count, int after_splat_count, int offset)
{
    int len = RARRAY_LEN(ary);
    if (len < before_splat_count + after_splat_count) {
	offset += before_splat_count;
	if (offset < len) {
	    return RARRAY_AT(ary, offset);
	}
    }
    else {
	offset += len - after_splat_count;
	return RARRAY_AT(ary, offset);
    }
    return Qnil;
}

extern "C"
VALUE
rb_vm_masgn_get_splat(VALUE ary, int before_splat_count, int after_splat_count) {
    int len = RARRAY_LEN(ary);
    if (len > before_splat_count + after_splat_count) {
	return rb_ary_subseq(ary, before_splat_count, len - before_splat_count - after_splat_count);
    }
    else {
	return rb_ary_new();
    }
}

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

static inline IMP
objc_imp(IMP imp)
{
    rb_vm_method_node_t *node = GET_CORE()->method_node_get(imp);
    if (node != NULL && node->ruby_imp == imp) {
	imp = node->objc_imp;
    }
    return imp;
}

static inline Method
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

extern "C"
VALUE
rb_vm_method_missing(VALUE obj, int argc, const VALUE *argv)
{
    if (argc == 0 || !SYMBOL_P(argv[0])) {
        rb_raise(rb_eArgError, "no id given");
    }

    const rb_vm_method_missing_reason_t last_call_status =
	GET_VM()->get_method_missing_reason();
    const char *format = NULL;
    VALUE exc = rb_eNoMethodError;

    switch (last_call_status) {
	case METHOD_MISSING_PRIVATE:
	    format = "private method `%s' called for %s";
	    break;

	case METHOD_MISSING_PROTECTED:
	    format = "protected method `%s' called for %s";
	    break;

	case METHOD_MISSING_VCALL:
	    format = "undefined local variable or method `%s' for %s";
	    exc = rb_eNameError;
	    break;

	case METHOD_MISSING_SUPER:
	    format = "super: no superclass method `%s' for %s";
	    break;

	case METHOD_MISSING_DEFAULT:
	default:
	    format = "undefined method `%s' for %s";
	    break;
    }

    VALUE meth = rb_sym_to_s(argv[0]);
    if (!rb_vm_respond_to(obj, selToS, true)) {
	// In case #to_s was undefined on the object, let's generate a
	// basic string based on it, because otherwise the following code
	// will raise a #method_missing which will result in an infinite loop.
	obj = rb_any_to_s(obj);
    }

    int n = 0;
    VALUE args[3];
    args[n++] = rb_funcall(rb_cNameErrorMesg, '!', 3, rb_str_new2(format),
	    obj, meth);
    args[n++] = meth;
    if (exc == rb_eNoMethodError) {
	args[n++] = rb_ary_new4(argc - 1, argv + 1);
    }

    exc = rb_class_new_instance(n, args, exc);
    rb_exc_raise(exc);

    abort(); // never reached
}

static VALUE
method_missing(VALUE obj, SEL sel, rb_vm_block_t *block, int argc, const VALUE *argv,
	       rb_vm_method_missing_reason_t call_status)
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

inline void *
RoxorCore::gen_stub(std::string types, int argc, bool is_objc)
{
    lock();

    std::map<std::string, void *> &stubs = is_objc ? objc_stubs : c_stubs;
    std::map<std::string, void *>::iterator iter = stubs.find(types);
    void *stub;
    if (iter == stubs.end()) {
	Function *f = RoxorCompiler::shared->compile_stub(types.c_str(), argc,
		is_objc);
	stub = (void *)compile(f);
	stubs.insert(std::make_pair(types, stub));
    }
    else {
	stub = iter->second;
    }

    unlock();

    return stub;
}

void *
RoxorCore::gen_to_rval_convertor(std::string type)
{
    std::map<std::string, void *>::iterator iter =
	to_rval_convertors.find(type);
    if (iter != to_rval_convertors.end()) {
	return iter->second;
    }

    Function *f = RoxorCompiler::shared->compile_to_rval_convertor(
	    type.c_str());
    void *convertor = (void *)compile(f);
    to_rval_convertors.insert(std::make_pair(type, convertor));
    
    return convertor; 
}

void *
RoxorCore::gen_to_ocval_convertor(std::string type)
{
    std::map<std::string, void *>::iterator iter =
	to_ocval_convertors.find(type);
    if (iter != to_ocval_convertors.end()) {
	return iter->second;
    }

    Function *f = RoxorCompiler::shared->compile_to_ocval_convertor(
	    type.c_str());
    void *convertor = (void *)compile(f);
    to_ocval_convertors.insert(std::make_pair(type, convertor));
    
    return convertor; 
}

static inline void
vm_gen_bs_func_types(bs_element_function_t *bs_func, std::string &types)
{
    types.append(bs_func->retval == NULL ? "v" : bs_func->retval->type);
    for (unsigned i = 0; i < bs_func->args_count; i++) {
	types.append(bs_func->args[i].type);
    }
}

static inline SEL
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

static force_inline void
fill_rcache(struct mcache *cache, Class klass, SEL sel,
	rb_vm_method_node_t *node)
{
    cache->flag = MCACHE_RCALL;
    rcache.klass = klass;
    rcache.node = node;
}

static force_inline bool
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

static force_inline void
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
    if (ocache.bs_method != NULL && ocache.bs_method->variadic
	&& method != NULL) {
	const int real_argc = method_getNumberOfArguments(method) - 2;
	if (real_argc < argc) {
	    const size_t s = strlen(types);
	    assert(s + argc - real_argc < sizeof types);
	    for (int i = real_argc; i < argc; i++) {
		strlcat(types, "@", sizeof types);
	    }
	    argc = real_argc;
	}
    }
    ocache.stub = (rb_vm_objc_stub_t *)GET_CORE()->gen_stub(types, 
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
		vm_gen_bs_func_types(bs_func, types);

		cache->flag = MCACHE_FCALL;
		fcache.bs_function = bs_func;
		fcache.imp = (IMP)dlsym(RTLD_DEFAULT, bs_func->name);
		assert(fcache.imp != NULL);
		fcache.stub = (rb_vm_c_stub_t *)GET_CORE()->gen_stub(types,
			argc, false);
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
	printf("ruby dispatch %c[<%s %p> %s] (imp=%p, block=%p, cached=%s)\n",
		class_isMetaClass(klass) ? '+' : '-',
		class_getName(klass),
		(void *)self,
		sel_getName(sel),
		rcache.node->ruby_imp,
		block,
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
	printf("objc dispatch %c[<%s %p> %s] imp=%p (cached=%s)\n",
		class_isMetaClass(klass) ? '+' : '-',
		class_getName(klass),
		(void *)self,
		sel_getName(sel),
		ocache.imp,
		cached ? "true" : "false");
#endif

	return (*ocache.stub)(ocache.imp, RB2OC(self), sel, argc, argv);
    }
    else if (cache->flag == MCACHE_FCALL) {
#if ROXOR_VM_DEBUG
	printf("C dispatch %s() imp=%p (cached=%s)\n",
		fcache.bs_function->name,
		fcache.imp,
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

#define MAX_DISPATCH_ARGS 500

static force_inline int
__rb_vm_resolve_args(VALUE *argv, int argc, va_list ar)
{
    // TODO we should only determine the real argc here (by taking into
    // account the length splat arguments) and do the real unpacking of
    // splat arguments in __rb_vm_rcall(). This way we can optimize more
    // things (for ex. no need to unpack splats that are passed as a splat
    // argument in the method being called!).
    int i, real_argc = 0;
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
		int j, count = RARRAY_LEN(ary);
		assert(real_argc + count < MAX_DISPATCH_ARGS);
		for (j = 0; j < count; j++) {
		    argv[real_argc++] = RARRAY_AT(ary, j);
		}
		splat_arg_follows = false;
	    }
	    else {
		assert(real_argc < MAX_DISPATCH_ARGS);
		argv[real_argc++] = arg;
	    }
	}
    }

    return real_argc;
}

extern "C"
VALUE
rb_vm_dispatch(struct mcache *cache, VALUE self, SEL sel, rb_vm_block_t *block, 
	       unsigned char opt, int argc, ...)
{
    VALUE argv[MAX_DISPATCH_ARGS];
    if (argc > 0) {
	va_list ar;
	va_start(ar, argc);
	const int new_argc = __rb_vm_resolve_args(argv, argc, ar);
	va_end(ar);
	if (argc > 0 && new_argc == 0) {
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
  	argc = new_argc;
    }

    RoxorVM *vm = GET_VM();

    VALUE retval = __rb_vm_dispatch(vm, cache, self, NULL, sel, block, opt,
	    argc, argv);

    vm->pop_current_binding();

    return retval;
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

extern "C"
void *
rb_vm_get_block(VALUE obj)
{
    if (obj == Qnil) {
	return NULL;
    }

    VALUE proc = rb_check_convert_type(obj, T_DATA, "Proc", "to_proc");
    if (NIL_P(proc)) {
	rb_raise(rb_eTypeError,
		"wrong argument type %s (expected Proc)",
		rb_obj_classname(obj));
    }
    return rb_proc_get_block(proc);
}

extern "C"
rb_vm_block_t *
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

rb_vm_block_t *
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

extern "C"
void*
rb_gc_read_weak_ref(void **referrer);

extern "C"
void
rb_gc_assign_weak_ref(const void *value, void *const*location);

static const int VM_LVAR_USES_SIZE = 8;
enum {
    VM_LVAR_USE_TYPE_BLOCK   = 1,
    VM_LVAR_USE_TYPE_BINDING = 2
};
struct rb_vm_var_uses {
    int uses_count;
    void *uses[VM_LVAR_USES_SIZE];
    unsigned char use_types[VM_LVAR_USES_SIZE];
    struct rb_vm_var_uses *next;
};

static void
rb_vm_add_lvar_use(rb_vm_var_uses **var_uses, void *use, unsigned char use_type)
{
    if (var_uses == NULL) {
	return;
    }

    if ((*var_uses == NULL)
	|| ((*var_uses)->uses_count == VM_LVAR_USES_SIZE)) {

	rb_vm_var_uses *new_uses =
	    (rb_vm_var_uses *)malloc(sizeof(rb_vm_var_uses));
	new_uses->next = *var_uses;
	new_uses->uses_count = 0;
	*var_uses = new_uses;
    }
    int current_index = (*var_uses)->uses_count;
    rb_gc_assign_weak_ref(use, &(*var_uses)->uses[current_index]);
    (*var_uses)->use_types[current_index] = use_type;
    ++(*var_uses)->uses_count;
}

extern "C"
void
rb_vm_add_block_lvar_use(rb_vm_block_t *block)
{
    for (rb_vm_block_t *block_for_uses = block;
	 block_for_uses != NULL;
	 block_for_uses = block_for_uses->parent_block) {

	rb_vm_add_lvar_use(block_for_uses->parent_var_uses, block, VM_LVAR_USE_TYPE_BLOCK);
    }
}

static void
rb_vm_add_binding_lvar_use(rb_vm_binding_t *binding, rb_vm_block_t *block,
	rb_vm_var_uses **parent_var_uses)
{
    for (rb_vm_block_t *block_for_uses = block;
	 block_for_uses != NULL;
	 block_for_uses = block_for_uses->parent_block) {

	rb_vm_add_lvar_use(block_for_uses->parent_var_uses, binding, VM_LVAR_USE_TYPE_BINDING);
    }
    rb_vm_add_lvar_use(parent_var_uses, binding, VM_LVAR_USE_TYPE_BINDING);
}

struct rb_vm_kept_local {
    ID name;
    VALUE *stack_address;
    VALUE *new_address;
};

extern "C"
void
rb_vm_keep_vars(rb_vm_var_uses *uses, int lvars_size, ...)
{
    rb_vm_var_uses *current = uses;
    int use_index;

    while (current != NULL) {
	for (use_index = 0; use_index < current->uses_count; ++use_index) {
	    if (rb_gc_read_weak_ref(&current->uses[use_index]) != NULL) {
		goto use_found;
	    }
	}

	void *old_current = current;
	current = current->next;
	free(old_current);
    }
    // there's no use alive anymore so nothing to do
    return;

use_found:
    rb_vm_kept_local *locals = (rb_vm_kept_local *)alloca(sizeof(rb_vm_kept_local)*lvars_size);

    va_list ar;
    va_start(ar, lvars_size);
    for (int i = 0; i < lvars_size; ++i) {
	locals[i].name = va_arg(ar, ID);
	locals[i].stack_address = va_arg(ar, VALUE *);
	locals[i].new_address = (VALUE *)xmalloc(sizeof(VALUE));
	GC_WB(locals[i].new_address, *locals[i].stack_address);
    }
    va_end(ar);

    while (current != NULL) {
	for (; use_index < current->uses_count; ++use_index) {
	    void *use = rb_gc_read_weak_ref(&current->uses[use_index]);
	    if (use != NULL) {
		unsigned char type = current->use_types[use_index];
		rb_vm_local_t *locals_to_replace;
		if (type == VM_LVAR_USE_TYPE_BLOCK) {
		    rb_vm_block_t *block = (rb_vm_block_t *)use;
		    for (int dvar_index = 0; dvar_index < block->dvars_size; ++dvar_index) {
			for (int lvar_index = 0; lvar_index < lvars_size; ++lvar_index) {
			    if (block->dvars[dvar_index] == locals[lvar_index].stack_address) {
				GC_WB(&block->dvars[dvar_index], locals[lvar_index].new_address);
				break;
			    }
			}
		    }

		    // the parent pointers can't be used anymore
		    block->parent_block = NULL;
		    block->parent_var_uses = NULL;

		    locals_to_replace = block->locals;
		}
		else { // VM_LVAR_USE_TYPE_BINDING
		    rb_vm_binding_t *binding = (rb_vm_binding_t *)use;
		    locals_to_replace = binding->locals;
		}

		for (rb_vm_local_t *l = locals_to_replace; l != NULL; l = l->next) {
		    for (int lvar_index = 0; lvar_index < lvars_size; ++lvar_index) {
			if (l->value == locals[lvar_index].stack_address) {
			    GC_WB(&l->value, locals[lvar_index].new_address);
			    break;
			}
		    }
		}

		// indicate to the GC that we do not have a reference here anymore
		rb_gc_assign_weak_ref(NULL, &current->uses[use_index]);
	    }
	}
	void *old_current = current;
	current = current->next;
	use_index = 0;
	free(old_current);
    }
}

static inline rb_vm_local_t **
push_local(rb_vm_local_t **l, ID name, VALUE *value)
{
    GC_WB(l, xmalloc(sizeof(rb_vm_local_t)));
    (*l)->name = name;
    (*l)->value = value;
    (*l)->next = NULL;
    return &(*l)->next;
}

extern "C"
void
rb_vm_push_binding(VALUE self, rb_vm_block_t *current_block,
		   rb_vm_var_uses **parent_var_uses,
		   int lvars_size, ...)
{
    rb_vm_binding_t *binding =
	(rb_vm_binding_t *)xmalloc(sizeof(rb_vm_binding_t));
    GC_WB(&binding->self, self);

    rb_vm_local_t **l = &binding->locals;

    for (rb_vm_block_t *b = current_block; b != NULL; b = b->parent_block) {
	for (rb_vm_local_t *li = b->locals; li != NULL; li = li->next) {
	    l = push_local(l, li->name, li->value);
	}
    }

    va_list ar;
    va_start(ar, lvars_size);
    for (int i = 0; i < lvars_size; ++i) {
	ID name = va_arg(ar, ID);
	VALUE *value = va_arg(ar, VALUE *);
	l = push_local(l, name, value);
    }
    va_end(ar);

    rb_vm_add_binding_lvar_use(binding, current_block, parent_var_uses);

    GET_VM()->push_current_binding(binding);
}

extern "C"
rb_vm_binding_t *
rb_vm_current_binding(void)
{
    return GET_VM()->current_binding();
}

extern "C"
void
rb_vm_add_binding(rb_vm_binding_t *binding)
{
    GET_VM()->push_current_binding(binding, false);
}

extern "C"
void
rb_vm_pop_binding(void)
{
    GET_VM()->pop_current_binding(false);
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

extern "C"
void *
rb_vm_get_call_cache(SEL sel)
{
    return GET_CORE()->method_cache_get(sel, false);
}

extern "C"
void *
rb_vm_get_call_cache2(SEL sel, unsigned char super)
{
    return GET_CORE()->method_cache_get(sel, super);
}

// Should be used inside a method implementation.
extern "C"
int
rb_block_given_p(void)
{
    return GET_VM()->current_block() != NULL ? Qtrue : Qfalse;
}

// Should only be used by Proc.new.
extern "C"
rb_vm_block_t *
rb_vm_first_block(void)
{
    return GET_VM()->first_block();
}

// Should only be used by #block_given?
extern "C"
bool
rb_vm_block_saved(void)
{
    return GET_VM()->previous_block() != NULL;
}

extern "C"
rb_vm_block_t *
rb_vm_current_block(void)
{
    return GET_VM()->current_block();
}

extern "C"
VALUE
rb_vm_current_block_object(void)
{
    rb_vm_block_t *b = GET_VM()->current_block();
    if (b != NULL) {
	return rb_proc_alloc_with_block(rb_cProc, b);
    }
    return Qnil;
}

void rb_print_undef(VALUE klass, ID id, int scope);

extern "C"
rb_vm_method_t *
rb_vm_get_method(VALUE klass, VALUE obj, ID mid, int scope)
{
    SEL sel;
    IMP imp;
    rb_vm_method_node_t *node;

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

extern "C"
rb_vm_block_t *
rb_vm_create_block_from_method(rb_vm_method_t *method)
{
    rb_vm_block_t *b = (rb_vm_block_t *)xmalloc(sizeof(rb_vm_block_t));

    b->proc = Qnil;
    GC_WB(&b->self, method->recv);
    b->klass = 0;
    b->arity = method->node == NULL
	? rb_vm_arity(method->arity) : method->node->arity;
    b->imp = (IMP)method;
    b->flags = VM_BLOCK_PROC | VM_BLOCK_METHOD;
    b->locals = NULL;
    b->parent_var_uses = NULL;
    b->parent_block = NULL;
    b->dvars_size = 0;

    return b;
}

static VALUE
rb_vm_block_call_sel(VALUE rcv, SEL sel, VALUE **dvars, rb_vm_block_t *b,
	VALUE x)
{
    if (x == Qnil) {
	rb_raise(rb_eArgError, "no receiver given");
    }
    return rb_vm_call(x, (SEL)dvars[0], 0, NULL, false);
}

extern "C"
rb_vm_block_t *
rb_vm_create_block_calling_sel(SEL sel)
{
    rb_vm_block_t *b = (rb_vm_block_t *)xmalloc(sizeof(rb_vm_block_t)
	    + sizeof(VALUE *));

    b->klass = 0;
    b->proc = Qnil;
    b->arity = rb_vm_arity(1);
    b->flags = VM_BLOCK_PROC;
    b->imp = (IMP)rb_vm_block_call_sel;
    b->dvars[0] = (VALUE *)sel;

    return b;
}

static VALUE
rb_vm_block_curry(VALUE rcv, SEL sel, VALUE **dvars, rb_vm_block_t *b,
	VALUE args)
{
    VALUE proc = (VALUE)dvars[0];
    VALUE passed = (VALUE)dvars[1];
    VALUE arity = (VALUE)dvars[2];

    passed = rb_ary_plus(passed, args);
    rb_ary_freeze(passed);
    if (RARRAY_LEN(passed) < FIX2INT(arity)) {
	return rb_vm_make_curry_proc(proc, passed, arity);
    }
    return rb_proc_call(proc, passed);
}

extern "C"
VALUE
rb_vm_make_curry_proc(VALUE proc, VALUE passed, VALUE arity)
{
    // Proc.new { |*args| curry... }
    rb_vm_block_t *b = (rb_vm_block_t *)xmalloc(sizeof(rb_vm_block_t)
	    + (3 * sizeof(VALUE *)));

    b->klass = 0;
    b->proc = Qnil;
    b->arity.min = 0;
    b->arity.max = -1;
    b->arity.left_req = 0;
    b->arity.real = 1;
    b->flags = VM_BLOCK_PROC;
    b->imp = (IMP)rb_vm_block_curry;
    b->dvars[0] = (VALUE *)proc;
    b->dvars[1] = (VALUE *)passed;
    b->dvars[2] = (VALUE *)arity;

    return rb_proc_alloc_with_block(rb_cProc, b);
}

static inline VALUE
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

static inline VALUE
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
    VALUE argv[MAX_DISPATCH_ARGS];
    if (argc > 0) {
	va_list ar;
	va_start(ar, argc);
	argc = __rb_vm_resolve_args(argv, argc, ar);
	va_end(ar);
    }
    return rb_vm_yield0(argc, argv);
}

extern IMP basic_respond_to_imp; // vm_method.c

#if MAC_OS_X_VERSION_MAX_ALLOWED < 1060
// the function is available on Leopard but it's not declared
extern "C" id _objc_msgForward(id receiver, SEL sel, ...);
#endif

#if 0
static inline IMP
class_respond_to(Class klass, SEL sel)
{
    IMP imp = class_getMethodImplementation(klass, sel);
    if (imp == _objc_msgForward) {
	if (rb_vm_resolve_method(klass, sel)) {
	    imp = class_getMethodImplementation(klass, sel);
	}
	else {
	    imp = NULL;
	}
    }
    return imp;
}
#endif

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

extern "C" VALUE rb_reg_match_pre(VALUE match, SEL sel);
extern "C" VALUE rb_reg_match_post(VALUE match, SEL sel);

extern "C"
VALUE
rb_vm_get_special(char code)
{
    VALUE backref = rb_backref_get();
    if (backref == Qnil) {
	return Qnil;
    }

    VALUE val;
    switch (code) {
	case '&':
	    val = rb_reg_last_match(backref);
	    break;
	case '`':
	    val = rb_reg_match_pre(backref, 0);
	    break;
	case '\'':
	    val = rb_reg_match_post(backref, 0);
	    break;
	case '+':
	    val = rb_reg_match_last(backref);
	    break;
	default:
	    {
		int index = (int)code;
		assert(index > 0 && index < 10);
		val = rb_reg_nth_match(index, backref);
	    }
	    break;
    }

    return val;
}

extern "C"
VALUE
rb_iseq_compile(VALUE src, VALUE file, VALUE line)
{
    // TODO
    return NULL;
}

extern "C"
VALUE
rb_iseq_eval(VALUE iseq)
{
    // TODO
    return Qnil;
}

extern "C"
VALUE
rb_iseq_new(NODE *node, VALUE filename)
{
    // TODO
    return Qnil;
}

static inline void
__vm_raise(void)
{
#if __LP64__
    // In 64-bit, an Objective-C exception is a C++ exception.
    id exc = rb_objc_create_exception(GET_VM()->current_exception());
    objc_exception_throw(exc);
#else
    void *exc = __cxa_allocate_exception(0);
    __cxa_throw(exc, NULL, NULL);
#endif
}

extern "C"
void
rb_vm_raise_current_exception(void)
{
    assert(GET_VM()->current_exception() != Qnil);
    __vm_raise(); 
}

extern "C"
void
rb_vm_raise(VALUE exception)
{
    rb_iv_set(exception, "bt", rb_vm_backtrace(100));
    rb_objc_retain((void *)exception);
    GET_VM()->push_current_exception(exception);
    __vm_raise();
}

extern "C"
VALUE
rb_rescue2(VALUE (*b_proc) (ANYARGS), VALUE data1,
           VALUE (*r_proc) (ANYARGS), VALUE data2, ...)
{
    try {
	return (*b_proc)(data1);
    }
    catch (...) {
	VALUE exc = rb_vm_current_exception();
	if (exc != Qnil) {
	    va_list ar;
	    VALUE eclass;
	    bool handled = false;

	    va_start(ar, data2);
	    while ((eclass = va_arg(ar, VALUE)) != 0) {
		if (rb_obj_is_kind_of(exc, eclass)) {
		    handled = true;
		    break;
		}
	    }
	    va_end(ar);

	    if (handled) {
		if (r_proc != NULL) {
		    return (*r_proc)(data2);
		}
		return Qnil;
	    }
	}
	throw;
    }
}

extern "C"
VALUE
rb_ensure(VALUE (*b_proc)(ANYARGS), VALUE data1,
	VALUE (*e_proc)(ANYARGS), VALUE data2)
{
    struct Finally {
	VALUE (*e_proc)(ANYARGS);
	VALUE data2;
	Finally(VALUE (*_e_proc)(ANYARGS), VALUE _data2) {
	    e_proc = _e_proc;
	    data2 = _data2;
	}
	~Finally() { (*e_proc)(data2); }
    } finalizer(e_proc, data2);

    return (*b_proc)(data1);
}

extern "C"
void
rb_vm_break(VALUE val)
{
#if 0
    // XXX this doesn't work yet since break is called inside the block and
    // we do not have a reference to it. This isn't very important though,
    // but since 1.9 doesn't support break without Proc objects we should also
    // raise a similar exception.
    assert(GET_VM()->current_block != NULL);
    if (GET_VM()->current_block->flags & VM_BLOCK_PROC) {
	rb_raise(rb_eLocalJumpError, "break from proc-closure");
    }
#endif
    GET_VM()->set_broken_with(val);
}

extern "C"
VALUE
rb_vm_get_broken_value(void)
{
    return GET_VM()->get_broken_with();
}

extern "C"
VALUE
rb_vm_pop_broken_value(void)
{
    return GET_VM()->pop_broken_with();
}

extern "C"
void
rb_vm_return_from_block(VALUE val, int id)
{
    RoxorReturnFromBlockException *exc = new RoxorReturnFromBlockException();

    exc->val = val;
    exc->id = id;

    throw exc;
}

extern "C" std::type_info *__cxa_current_exception_type(void);

static inline bool
current_exception_is_return_from_block(void)
{
    const std::type_info *exc_type = __cxa_current_exception_type();
    return exc_type != NULL
	&& *exc_type == typeid(RoxorReturnFromBlockException *);
}

extern "C"
VALUE
rb_vm_check_return_from_block_exc(RoxorReturnFromBlockException **pexc, int id)
{
    if (current_exception_is_return_from_block()) {
	RoxorReturnFromBlockException *exc = *pexc;
	if (id == -1 || exc->id == id) {
	    VALUE val = exc->val;
	    delete exc;
	    return val;
	}
    }
    return Qundef;
}

static inline void
rb_vm_rethrow(void)
{
    void *exc = __cxa_allocate_exception(0);
    __cxa_throw(exc, NULL, NULL);
}

extern "C"
VALUE
rb_vm_backtrace(int level)
{
    void *callstack[128];
    int callstack_n = backtrace(callstack, 128);

    // TODO should honor level

    VALUE ary = rb_ary_new();

    for (int i = 0; i < callstack_n; i++) {
	char path[PATH_MAX];
	char name[100];
	unsigned long ln = 0;

	if (GET_CORE()->symbolize_call_address(callstack[i], NULL,
		    path, sizeof path, &ln, name, sizeof name)) {
	    char entry[PATH_MAX];
	    snprintf(entry, sizeof entry, "%s:%ld:in `%s'", path, ln, name);
	    rb_ary_push(ary, rb_str_new2(entry));
	}
    }

    return ary;
}

extern "C"
unsigned char
rb_vm_is_eh_active(int argc, ...)
{
    assert(argc > 0);

    VALUE current_exception = GET_VM()->current_exception();
    if (current_exception == Qnil) {
	// Not a Ruby exception...
	return 0;
    }

    va_list ar;
    unsigned char active = 0;

    va_start(ar, argc);
    for (int i = 0; i < argc && active == 0; ++i) {
	VALUE obj = va_arg(ar, VALUE);
	if (TYPE(obj) == T_ARRAY) {
	    for (int j = 0, count = RARRAY_LEN(obj); j < count; ++j) {
		VALUE obj2 = RARRAY_AT(obj, j);
		if (rb_obj_is_kind_of(current_exception, obj2)) {
		    active = 1;
		}
	    }
	}
	else {
	    if (rb_obj_is_kind_of(current_exception, obj)) {
		active = 1;
	    }
	}
    }
    va_end(ar);

    return active;
}

extern "C"
void
rb_vm_pop_exception(void)
{
    GET_VM()->pop_current_exception();
}

extern "C"
VALUE
rb_vm_current_exception(void)
{
    return GET_VM()->current_exception();
}

extern "C"
void
rb_vm_set_current_exception(VALUE exception)
{
    assert(!NIL_P(exception));

    VALUE current = GET_VM()->current_exception();
    assert(exception != current);
    if (!NIL_P(current)) {
	GET_VM()->pop_current_exception();
    }
    GET_VM()->push_current_exception(exception);
}

extern "C"
void 
rb_vm_debug(void)
{
    printf("rb_vm_debug\n");
}

extern "C"
void
rb_vm_print_current_exception(void)
{
    VALUE exc = GET_VM()->current_exception();
    if (exc == Qnil) {
	printf("uncaught Objective-C/C++ exception...\n");
	std::terminate();
    }

    static SEL sel_message = 0;
    if (sel_message == 0) {
	sel_message = sel_registerName("message");
    }

    VALUE message = rb_vm_call(exc, sel_message, 0, NULL, false);

    printf("%s (%s)\n", RSTRING_PTR(message), rb_class2name(*(VALUE *)exc));
}

extern "C"
bool
rb_vm_parse_in_eval(void)
{
    return GET_VM()->get_parse_in_eval();
}

extern "C"
void
rb_vm_set_parse_in_eval(bool flag)
{
    GET_VM()->set_parse_in_eval(flag);
}

extern "C"
int
rb_parse_in_eval(void)
{
    return rb_vm_parse_in_eval() ? 1 : 0;
}

VALUE *
RoxorVM::get_binding_lvar(ID name, bool create)
{
    if (!bindings.empty()) {
	rb_vm_binding_t *b = bindings.back();
	rb_vm_local_t **l = &b->locals;
	while (*l != NULL) {
	    if ((*l)->name == name) {
		return (*l)->value;
	    }
	    l = &(*l)->next;
	}
	if (create) {
	    GC_WB(l, xmalloc(sizeof(rb_vm_local_t)));
	    (*l)->name = name;
	    GC_WB(&(*l)->value, xmalloc(sizeof(VALUE)));
	    (*l)->next = NULL;
	    return (*l)->value;
	}
    }
    return NULL;
}

extern "C"
int
rb_local_define(ID id)
{
    return GET_VM()->get_binding_lvar(id, true) != NULL ? 1 : 0;
}

extern "C"
int
rb_local_defined(ID id)
{
    return GET_VM()->get_binding_lvar(id, false) != NULL ? 1 : 0;
}

extern "C"
int
rb_dvar_defined(ID id)
{
    // TODO
    return 0;
}

extern "C"
void
rb_vm_init_compiler(void)
{
    RoxorCompiler::shared = ruby_aot_compile
	? new RoxorAOTCompiler()
	: new RoxorCompiler();
}

extern "C"
VALUE
rb_vm_run(const char *fname, NODE *node, rb_vm_binding_t *binding,
	  bool inside_eval)
{
    RoxorVM *vm = GET_VM();

    if (binding != NULL) {
	vm->push_current_binding(binding, false);
    }

    RoxorCompiler *compiler = RoxorCompiler::shared;

    bool old_inside_eval = compiler->is_inside_eval();
    compiler->set_inside_eval(inside_eval);
    compiler->set_fname(fname);
    Function *function = compiler->compile_main_function(node);
    compiler->set_fname(NULL);
    compiler->set_inside_eval(old_inside_eval);

    if (binding != NULL) {
	vm->pop_current_binding(false);
    }

#if ROXOR_INTERPRET_EVAL
    if (inside_eval) {
	return GET_CORE()->interpret(function);
    }
    else {
	IMP imp = GET_CORE()->compile(function);
	return ((VALUE(*)(VALUE, SEL))imp)(vm->get_current_top_object(), 0);
    }
#else
    IMP imp = GET_CORE()->compile(function);
    return ((VALUE(*)(VALUE, SEL))imp)(vm->get_current_top_object(), 0);
#endif
}

extern "C"
VALUE
rb_vm_run_under(VALUE klass, VALUE self, const char *fname, NODE *node,
		rb_vm_binding_t *binding, bool inside_eval)
{
    RoxorVM *vm = GET_VM();
    RoxorCompiler *compiler = RoxorCompiler::shared;

    VALUE old_top_object = vm->get_current_top_object();
    if (binding != NULL) {
	self = binding->self;
    }
    if (self != 0) {
	vm->set_current_top_object(self);
    }
    Class old_class = GET_VM()->get_current_class();
    bool old_dynamic_class = compiler->is_dynamic_class();
    if (klass != 0) {
	vm->set_current_class((Class)klass);
	compiler->set_dynamic_class(true);
    }

    VALUE val = rb_vm_run(fname, node, binding, inside_eval);

    compiler->set_dynamic_class(old_dynamic_class);
    vm->set_current_top_object(old_top_object);
    vm->set_current_class(old_class);

    return val;
}

// in st.c
extern "C" int rb_hash_string(const char *str);

extern "C"
void
rb_vm_aot_compile(NODE *node)
{
    assert(ruby_aot_compile);

    const char *output = RSTRING_PTR(ruby_aot_compile);

    // Generate the name of the init function.
    char init_function_name[PATH_MAX];
    const int hash = rb_hash_string(output);
    snprintf(init_function_name, sizeof init_function_name,
	    "MREP_%d", hash >= 0 ? hash : -hash);

    // Compile the program as IR.
    Function *f = RoxorCompiler::shared->compile_main_function(node);
    f->setName(init_function_name);
    GET_CORE()->optimize(f);

    // Dump the bitcode.
    std::ofstream out(output);
    WriteBitcodeToFile(RoxorCompiler::module, out);
    out.close();
}

extern "C"
VALUE
rb_vm_top_self(void)
{
    return GET_VM()->get_current_top_object();
}

extern "C"
VALUE
rb_vm_loaded_features(void)
{
    return GET_CORE()->get_loaded_features();
}

extern "C"
VALUE
rb_vm_load_path(void)
{
    return GET_CORE()->get_load_path();
}

extern "C"
int
rb_vm_safe_level(void)
{
    return GET_VM()->get_safe_level();
}

extern "C"
int
rb_vm_thread_safe_level(rb_vm_thread_t *thread)
{
    return ((RoxorVM *)thread->vm)->get_safe_level();
}

extern "C"
void 
rb_vm_set_safe_level(int level)
{
    GET_VM()->set_safe_level(level);
}

extern "C"
VALUE
rb_last_status_get(void)
{
    return GET_VM()->get_last_status();
}

extern "C"
void
rb_last_status_set(int status, rb_pid_t pid)
{
    VALUE last_status = GET_VM()->get_last_status();
    if (last_status != Qnil) {
	rb_objc_release((void *)last_status);
    }

    if (pid == -1) {
	last_status = Qnil;
    }
    else {
	last_status = rb_obj_alloc(rb_cProcessStatus);
	rb_iv_set(last_status, "status", INT2FIX(status));
	rb_iv_set(last_status, "pid", PIDT2NUM(pid));
	rb_objc_retain((void *)last_status);
    }
    GET_VM()->set_last_status(last_status);
}

extern "C"
VALUE
rb_errinfo(void)
{
    return GET_VM()->get_errinfo();
}

void
rb_set_errinfo(VALUE err)
{
    if (!NIL_P(err) && !rb_obj_is_kind_of(err, rb_eException)) {
        rb_raise(rb_eTypeError, "assigning non-exception to $!");
    }
    VALUE errinfo = GET_VM()->get_errinfo();
    if (errinfo != Qnil) {
	rb_objc_release((void *)errinfo);
    }
    GET_VM()->set_errinfo(err);
    rb_objc_retain((void *)err);
}

extern "C"
const char *
rb_sourcefile(void)
{
    // TODO
    return "unknown";
}

extern "C"
int
rb_sourceline(void)
{
    // TODO
    return 0;
}

extern "C"
VALUE
rb_lastline_get(void)
{
    // TODO
    return Qnil;
}

extern "C"
void
rb_lastline_set(VALUE val)
{
    // TODO
}

extern "C"
void
rb_iter_break(void)
{
    GET_VM()->set_broken_with(Qnil);
}

extern "C"
VALUE
rb_backref_get(void)
{
    return GET_VM()->get_backref();
}

extern "C"
void
rb_backref_set(VALUE val)
{
    VALUE old = GET_VM()->get_backref();
    if (old != val) {
	rb_objc_release((void *)old);
	GET_VM()->set_backref(val);
	rb_objc_retain((void *)val);
    }
}

VALUE
RoxorVM::ruby_catch(VALUE tag)
{
    std::map<VALUE, rb_vm_catch_t *>::iterator iter =
	catch_jmp_bufs.find(tag);
    rb_vm_catch_t *s = NULL;
    if (iter == catch_jmp_bufs.end()) {
	s = (rb_vm_catch_t *)malloc(sizeof(rb_vm_catch_t));
	s->throw_value = Qnil;
	s->nested = 1;
	catch_jmp_bufs[tag] = s;
	rb_objc_retain((void *)tag);
    }
    else {
	s = iter->second;
	s->nested++;
    }

    VALUE retval;
    if (setjmp(s->buf) == 0) {
	retval = rb_vm_yield(1, &tag);
    }
    else {
	retval = s->throw_value;
	rb_objc_release((void *)retval);
	s->throw_value = Qnil;
    }

    iter = catch_jmp_bufs.find(tag);
    assert(iter != catch_jmp_bufs.end());
    s->nested--;
    if (s->nested == 0) {
	s = iter->second;
	free(s);
	catch_jmp_bufs.erase(iter);
	rb_objc_release((void *)tag);
    }

    return retval;
}

extern "C"
VALUE
rb_vm_catch(VALUE tag)
{
    return GET_VM()->ruby_catch(tag);
}

VALUE
RoxorVM::ruby_throw(VALUE tag, VALUE value)
{
    std::map<VALUE, rb_vm_catch_t *>::iterator iter =
	catch_jmp_bufs.find(tag);
    if (iter == catch_jmp_bufs.end()) {
        VALUE desc = rb_inspect(tag);
        rb_raise(rb_eArgError, "uncaught throw %s", RSTRING_PTR(desc));
    }
    rb_vm_catch_t *s = iter->second;

    rb_objc_retain((void *)value);
    s->throw_value = value;

    longjmp(s->buf, 1);

    return Qnil; // never reached
}

extern "C"
VALUE
rb_vm_throw(VALUE tag, VALUE value)
{
    return GET_VM()->ruby_throw(tag, value);
}

extern "C"
void *
rb_vm_create_vm(void)
{
    GET_CORE()->set_multithreaded(true);

    return (void *)new RoxorVM(*GET_VM());
}

extern "C"
bool
rb_vm_is_multithreaded(void)
{
    return GET_CORE()->get_multithreaded();
}

extern "C"
void
rb_vm_set_multithreaded(bool flag)
{
    GET_CORE()->set_multithreaded(flag);
}

void
RoxorCore::register_thread(VALUE thread)
{
    lock();
    rb_ary_push(threads, thread);
    unlock();

    rb_vm_thread_t *t = GetThreadPtr(thread);
    pthread_assert(pthread_setspecific(RoxorVM::vm_thread_key, t->vm));

    RoxorVM *vm = (RoxorVM *)t->vm;
    vm->set_thread(thread);
}

extern "C" void rb_thread_unlock_all_mutexes(rb_vm_thread_t *thread);

void
RoxorCore::unregister_thread(VALUE thread)
{
    lock();
    if (rb_ary_delete(threads, thread) != thread) {
	printf("trying to unregister a thread (%p) that was never registered!",
		(void *)thread);
	abort();
    }
    unlock();

    rb_vm_thread_t *t = GetThreadPtr(thread);

    const int code = pthread_mutex_destroy(&t->sleep_mutex);
    if (code == EBUSY) {
	// The mutex is already locked, which means we are being called from
	// a cancellation point inside the wait logic. Let's unlock the mutex
	// and try again.
	pthread_assert(pthread_mutex_unlock(&t->sleep_mutex));
	pthread_assert(pthread_mutex_destroy(&t->sleep_mutex));
    }
    else if (code != 0) {
	abort();
    }
    pthread_assert(pthread_cond_destroy(&t->sleep_cond));

    rb_thread_unlock_all_mutexes(t); 

    RoxorVM *vm = (RoxorVM *)t->vm;
    delete vm;
    t->vm = NULL;

    pthread_assert(pthread_setspecific(RoxorVM::vm_thread_key, NULL));

    t->status = THREAD_DEAD;
}

static inline void
rb_vm_thread_throw_kill(void)
{
    // Killing a thread is implemented using a non-catchable (from Ruby)
    // exception, which allows us to call the ensure blocks before dying,
    // which is unfortunately covered in the Ruby specifications.
    rb_vm_rethrow();
}

static void
rb_vm_thread_destructor(void *userdata)
{
    rb_vm_thread_throw_kill();
}

extern "C"
void *
rb_vm_thread_run(VALUE thread)
{
    rb_objc_gc_register_thread();
    GET_CORE()->register_thread(thread);

    // Release the thread now.
    rb_objc_release((void *)thread);

    rb_vm_thread_t *t = GetThreadPtr(thread);

    // Normally the pthread ID is set into the VM structure in the other
    // thread right after pthread_create(), but we might run before the
    // assignment!
    t->thread = pthread_self();

    pthread_cleanup_push(rb_vm_thread_destructor, (void *)thread);

    try {
	VALUE val = rb_vm_block_eval(t->body, t->argc, t->argv);
	GC_WB(&t->value, val);
    }
    catch (...) {
	VALUE exc;
	if (current_exception_is_return_from_block()) {
	    // TODO: the exception is leaking!
	    exc = rb_exc_new2(rb_eLocalJumpError,
		    "unexpected return from Thread");
	}
	else {
	    exc = rb_vm_current_exception();
	}
	if (exc != Qnil) {
	    GC_WB(&t->exception, exc);
	}
	t->value = Qnil;
    }

    pthread_cleanup_pop(0);

    GET_CORE()->unregister_thread(thread);
    rb_objc_gc_unregister_thread();

#if 0
    if (t->exception != Qnil) {
	if (t->abort_on_exception || GET_CORE()->get_abort_on_exception()) {
 	    // TODO: move the exception to the main thread
	    //rb_exc_raise(t->exception);
	}
    }
#endif

    return NULL;
}

extern "C"
VALUE
rb_vm_threads(void)
{
    return GET_CORE()->get_threads();
}

extern "C"
VALUE
rb_vm_current_thread(void)
{
    return GET_VM()->get_thread();
}

extern "C"
VALUE
rb_thread_current(void)
{
    // For compatibility with MRI 1.9.
    return rb_vm_current_thread();
}

extern "C"
VALUE
rb_vm_main_thread(void)
{
    return RoxorVM::main->get_thread();
}

extern "C"
VALUE
rb_vm_thread_locals(VALUE thread, bool create_storage)
{
    rb_vm_thread_t *t = GetThreadPtr(thread);
    if (t->locals == Qnil && create_storage) {
	GC_WB(&t->locals, rb_hash_new());
    }
    return t->locals;
}

extern "C"
void
rb_vm_thread_pre_init(rb_vm_thread_t *t, rb_vm_block_t *body, int argc,
	const VALUE *argv, void *vm)
{
    t->thread = 0; // this will be set later

    if (body != NULL) {
	GC_WB(&t->body, body);
	rb_vm_add_block_lvar_use(body);
    }
    else {
	t->body = NULL;
    }
   
    if (argc > 0) {
	t->argc = argc;
	GC_WB(&t->argv, xmalloc(sizeof(VALUE) * argc));
	int i;
	for (i = 0; i < argc; i++) {
	    GC_WB(&t->argv[i], argv[i]);
	}
    }
    else {
	t->argc = 0;
	t->argv = NULL;
    }

    t->vm  = vm;
    t->value = Qundef;
    t->locals = Qnil;
    t->exception = Qnil;
    t->status = THREAD_ALIVE;
    t->in_cond_wait = false;
    t->abort_on_exception = false;
    t->group = Qnil; // will be set right after
    t->mutexes = Qnil;

    pthread_assert(pthread_mutex_init(&t->sleep_mutex, NULL));
    pthread_assert(pthread_cond_init(&t->sleep_cond, NULL)); 
}

static inline void
pre_wait(rb_vm_thread_t *t)
{
    pthread_assert(pthread_mutex_lock(&t->sleep_mutex));
    t->status = THREAD_SLEEP;
    t->in_cond_wait = true;
}

static inline void
post_wait(rb_vm_thread_t *t)
{
    t->in_cond_wait = false;
    pthread_assert(pthread_mutex_unlock(&t->sleep_mutex));
    if (t->status == THREAD_KILLED) {
	rb_vm_thread_throw_kill();
    }
    t->status = THREAD_ALIVE;
}

extern "C"
void
rb_thread_sleep_forever()
{
    rb_vm_thread_t *t = GET_THREAD();

    pre_wait(t);
    const int code = pthread_cond_wait(&t->sleep_cond, &t->sleep_mutex);
    assert(code == 0 || code == ETIMEDOUT);
    post_wait(t);
}

extern "C"
void
rb_thread_wait_for(struct timeval time)
{
    struct timeval tvn;
    gettimeofday(&tvn, NULL);

    struct timespec ts;
    ts.tv_sec = tvn.tv_sec + time.tv_sec;
    ts.tv_nsec = (tvn.tv_usec + time.tv_usec) * 1000;
    while (ts.tv_nsec >= 1000000000) {
	ts.tv_sec += 1;
	ts.tv_nsec -= 1000000000;
    }

    rb_vm_thread_t *t = GET_THREAD();

    pre_wait(t);
    const int code = pthread_cond_timedwait(&t->sleep_cond, &t->sleep_mutex,
	    &ts);
    assert(code == 0 || code == ETIMEDOUT);
    post_wait(t);
}

extern "C"
void
rb_vm_thread_wakeup(rb_vm_thread_t *t)
{
    if (t->status == THREAD_DEAD) {
	rb_raise(rb_eThreadError, "can't wake up thread from the death");
    }
    if (t->status == THREAD_SLEEP && t->in_cond_wait) {
	pthread_assert(pthread_mutex_lock(&t->sleep_mutex));
	pthread_assert(pthread_cond_signal(&t->sleep_cond));
	pthread_assert(pthread_mutex_unlock(&t->sleep_mutex));
    }
}

extern "C"
void
rb_vm_thread_cancel(rb_vm_thread_t *t)
{
    if (t->status != THREAD_KILLED && t->status != THREAD_DEAD) {
	t->status = THREAD_KILLED;
	if (t->thread == pthread_self()) {
	    rb_vm_thread_throw_kill();
	}
	else {
	    pthread_assert(pthread_mutex_lock(&t->sleep_mutex));
	    if (t->in_cond_wait) {
		// We are trying to kill a thread which is currently waiting
		// for a condition variable (#sleep). Instead of canceling the
		// thread, we are simply signaling the variable, and the thread
		// will autodestroy itself, to work around a stack unwinding
		// bug in the Mac OS X pthread implementation that messes our
		// C++ exception handlers.
		pthread_assert(pthread_cond_signal(&t->sleep_cond));
	    }
	    else {
		pthread_assert(pthread_cancel(t->thread));
	    }
	    pthread_assert(pthread_mutex_unlock(&t->sleep_mutex));
	}
    }
}

extern "C"
void
rb_vm_thread_raise(rb_vm_thread_t *t, VALUE exc)
{
    // XXX we should lock here
    RoxorVM *vm = (RoxorVM *)t->vm;
    vm->push_current_exception(exc);

    rb_vm_thread_cancel(t);
}

extern "C"
void
rb_thread_sleep(int sec)
{
    struct timeval time;
    time.tv_sec = sec;
    time.tv_usec = 0;
    rb_thread_wait_for(time);
}

extern "C"
Class
rb_vm_set_current_class(Class klass)
{
    RoxorVM *vm = GET_VM();
    Class old = vm->get_current_class();
    vm->set_current_class(klass);
    return old;
}

extern "C"
void
rb_vm_set_current_scope(VALUE mod, rb_vm_scope_t scope)
{
    if (scope == SCOPE_DEFAULT) {
	scope = mod == rb_cObject ? SCOPE_PRIVATE : SCOPE_PUBLIC;
    }
    long v = RCLASS_VERSION(mod);
#if ROXOR_VM_DEBUG
    const char *scope_name = NULL;
#endif
    switch (scope) {
	case SCOPE_PUBLIC:
#if ROXOR_VM_DEBUG
	    scope_name = "public";
#endif
	    v &= ~RCLASS_SCOPE_PRIVATE;
	    v &= ~RCLASS_SCOPE_PROTECTED;
	    v &= ~RCLASS_SCOPE_MOD_FUNC;
	    break;

	case SCOPE_PRIVATE:
#if ROXOR_VM_DEBUG
	    scope_name = "private";
#endif
	    v |= RCLASS_SCOPE_PRIVATE;
	    v &= ~RCLASS_SCOPE_PROTECTED;
	    v &= ~RCLASS_SCOPE_MOD_FUNC;
	    break;

	case SCOPE_PROTECTED:
#if ROXOR_VM_DEBUG
	    scope_name = "protected";
#endif
	    v &= ~RCLASS_SCOPE_PRIVATE;
	    v |= RCLASS_SCOPE_PROTECTED;
	    v &= ~RCLASS_SCOPE_MOD_FUNC;
	    break;

	case SCOPE_MODULE_FUNC:
#if ROXOR_VM_DEBUG
	    scope_name = "module_func";
#endif
	    v &= ~RCLASS_SCOPE_PRIVATE;
	    v &= ~RCLASS_SCOPE_PROTECTED;
	    v |= RCLASS_SCOPE_MOD_FUNC;
	    break;

	case SCOPE_DEFAULT:
	    abort(); // handled earlier
    }

#if ROXOR_VM_DEBUG
    printf("changing scope of %s (%p) to %s\n",
	    class_getName((Class)mod), (void *)mod, scope_name);
#endif

    RCLASS_SET_VERSION(mod, v);
}

static VALUE
builtin_ostub1(IMP imp, id self, SEL sel, int argc, VALUE *argv)
{
    return OC2RB(((id (*)(id, SEL))*imp)(self, sel));
}

static void
setup_builtin_stubs(void)
{
    GET_CORE()->insert_stub("@@:", (void *)builtin_ostub1, true);
    GET_CORE()->insert_stub("#@:", (void *)builtin_ostub1, true);
}

static IMP old_resolveClassMethod_imp = NULL;
static IMP old_resolveInstanceMethod_imp = NULL;

static BOOL
resolveClassMethod_imp(void *self, SEL sel, SEL name)
{
    if (rb_vm_resolve_method(*(Class *)self, name)) {
	return YES;
    }
    return NO; // TODO call old IMP
}

static BOOL
resolveInstanceMethod_imp(void *self, SEL sel, SEL name)
{
    if (rb_vm_resolve_method((Class)self, name)) {
	return YES;
    }
    return NO; // TODO call old IMP
}

extern "C"
void 
Init_PreVM(void)
{
    llvm::ExceptionHandling = true; // required!

    RoxorCompiler::module = new llvm::Module("Roxor");
    RoxorCore::shared = new RoxorCore();
    RoxorVM::main = new RoxorVM();

    pthread_assert(pthread_key_create(&RoxorVM::vm_thread_key, NULL));

    setup_builtin_stubs();

    Method m;
    Class ns_object = (Class)objc_getClass("NSObject");
    m = class_getInstanceMethod(*(Class *)ns_object,
	sel_registerName("resolveClassMethod:"));
    assert(m != NULL);
    old_resolveClassMethod_imp = method_getImplementation(m);
    method_setImplementation(m, (IMP)resolveClassMethod_imp);

    m = class_getInstanceMethod(*(Class *)ns_object,
	sel_registerName("resolveInstanceMethod:"));
    assert(m != NULL);
    old_resolveInstanceMethod_imp = method_getImplementation(m);
    method_setImplementation(m, (IMP)resolveInstanceMethod_imp);
}

static VALUE
rb_toplevel_to_s(VALUE rcv, SEL sel)
{
    return rb_str_new2("main");
}

static const char *
resources_path(char *path, size_t len)
{
    CFBundleRef bundle;
    CFURLRef url;

    bundle = CFBundleGetMainBundle();
    assert(bundle != NULL);

    url = CFBundleCopyResourcesDirectoryURL(bundle);
    *path = '-'; 
    *(path+1) = 'I';
    assert(CFURLGetFileSystemRepresentation(
		url, true, (UInt8 *)&path[2], len - 2));
    CFRelease(url);

    return path;
}

extern "C"
int
macruby_main(const char *path, int argc, char **argv)
{
    char **newargv;
    char *p1, *p2;
    int n, i;

    newargv = (char **)malloc(sizeof(char *) * (argc + 2));
    for (i = n = 0; i < argc; i++) {
	if (!strncmp(argv[i], "-psn_", 5) == 0) {
	    newargv[n++] = argv[i];
	}
    }
    
    p1 = (char *)malloc(PATH_MAX);
    newargv[n++] = (char *)resources_path(p1, PATH_MAX);

    p2 = (char *)malloc(PATH_MAX);
    snprintf(p2, PATH_MAX, "%s/%s", (path[0] != '/') ? &p1[2] : "", path);
    newargv[n++] = p2;

    argv = newargv;    
    argc = n;

    try {
	ruby_sysinit(&argc, &argv);
	ruby_init();
	void *tree = ruby_options(argc, argv);
	rb_vm_init_compiler();
	free(newargv);
	free(p1);
	free(p2);
	return ruby_run_node(tree);
    }
    catch (...) {
	rb_vm_print_current_exception();
	exit(1);	
    }
}

extern "C"
void
Init_VM(void)
{
    rb_cTopLevel = rb_define_class("TopLevel", rb_cObject);
    rb_objc_define_method(rb_cTopLevel, "to_s", (void *)rb_toplevel_to_s, 0);

    GET_VM()->set_current_class(NULL);

    VALUE top_self = rb_obj_alloc(rb_cTopLevel);
    rb_objc_retain((void *)top_self);
    GET_VM()->set_current_top_object(top_self);

    rb_vm_set_current_scope(rb_cNSObject, SCOPE_PRIVATE);
}

extern "C"
void
Init_PostVM(void)
{
    // Create and register the main thread.
    RoxorVM *main_vm = GET_VM();
    rb_vm_thread_t *t = (rb_vm_thread_t *)xmalloc(sizeof(rb_vm_thread_t));
    rb_vm_thread_pre_init(t, NULL, 0, NULL, (void *)main_vm);
    t->thread = pthread_self();
    VALUE main = Data_Wrap_Struct(rb_cThread, NULL, NULL, t);
    GET_CORE()->register_thread(main);
    main_vm->set_thread(main);

    // Create main thread group.
    VALUE group = rb_obj_alloc(rb_cThGroup);
    rb_thgroup_add(group, main);
    rb_define_const(rb_cThGroup, "Default", group);
}

extern "C"
void
rb_vm_finalize(void)
{
    if (getenv("VM_DUMP_IR") != NULL) {
	printf("IR dump ----------------------------------------------\n");
	RoxorCompiler::module->dump();
	printf("------------------------------------------------------\n");
    }
#if ROXOR_VM_DEBUG
    printf("functions all=%ld compiled=%ld\n", RoxorCompiler::module->size(),
	    GET_CORE()->get_functions_compiled());
#endif
}
