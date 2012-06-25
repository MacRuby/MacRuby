/*
 * MacRuby VM.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2008-2011, Apple Inc. All rights reserved.
 */

#define ROXOR_VM_DEBUG		0
#define ROXOR_COMPILER_DEBUG 	0
#define ROXOR_VM_DEBUG_CONST	0

#if MACRUBY_STATIC
# include <vector>
# include <map>
# include <string>
#else
# include <llvm/Module.h>
# include <llvm/DerivedTypes.h>
# include <llvm/Constants.h>
# include <llvm/CallingConv.h>
# include <llvm/Instructions.h>
# include <llvm/PassManager.h>
# include <llvm/Analysis/DebugInfo.h>
# if !defined(LLVM_TOT)
#  include <llvm/Analysis/DIBuilder.h>
# endif
# include <llvm/Analysis/Verifier.h>
# include <llvm/Target/TargetData.h>
# include <llvm/CodeGen/MachineFunction.h>
# include <llvm/ExecutionEngine/JIT.h>
# include <llvm/ExecutionEngine/JITMemoryManager.h>
# include <llvm/ExecutionEngine/JITEventListener.h>
# include <llvm/ExecutionEngine/GenericValue.h>
# include <llvm/Target/TargetData.h>
# include <llvm/Target/TargetMachine.h>
# include <llvm/Target/TargetOptions.h>
# include <llvm/Target/TargetSelect.h>
# include <llvm/Transforms/Scalar.h>
# include <llvm/Transforms/IPO.h>
# include <llvm/Support/raw_ostream.h>
# if !defined(LLVM_TOT)
#  include <llvm/Support/system_error.h>
# endif
# include <llvm/Support/PrettyStackTrace.h>
# include <llvm/Support/MemoryBuffer.h>
# include <llvm/Support/StandardPasses.h>
# include <llvm/Intrinsics.h>
# include <llvm/Bitcode/ReaderWriter.h>
# include <llvm/LLVMContext.h>
# include "llvm/ADT/Statistic.h"
using namespace llvm;
#endif // MACRUBY_STATIC

#if ROXOR_COMPILER_DEBUG
# include <mach/mach.h>
# include <mach/mach_time.h>
#endif

#include "macruby_internal.h"
#include "ruby/node.h"
#include "id.h"
#include "vm.h"
#include "compiler.h"
#include "debugger.h"
#include "interpreter.h"
#include "objc.h"
#include "dtrace.h"
#include "class.h"

#include <objc/objc-exception.h>

#include <execinfo.h>
#include <dlfcn.h>

#include <iostream>
#include <fstream>

RoxorCore *RoxorCore::shared = NULL;
RoxorVM *RoxorVM::main = NULL;
pthread_key_t RoxorVM::vm_thread_key;

VALUE rb_cTopLevel = 0;

// A simple class that acquires the core global lock in its constructor and
// releases it in its destructor. It is used to make sure the lock will be
// released in case an exception happens inside the scope (since C++ exceptions
// call object destructors).
class RoxorCoreLock {
    private:
	bool locked;

    public:
	RoxorCoreLock() {
	    GET_CORE()->lock();
	    locked = true;
	}

	~RoxorCoreLock() {
	    if (locked) {
		GET_CORE()->unlock();
		locked = false;
	    }
	}

	void unlock(void) {
	    assert(locked);
	    GET_CORE()->unlock();
	    locked = false;
	}
};

#if !defined(MACRUBY_STATIC)
class RoxorFunction {
    public: 
	// Information retrieved from JITManager.
	Function *f;
	unsigned char *start;
	unsigned char *end;
	std::vector<unsigned char *> ehs;

	// Information retrieved from JITListener.
	std::string path;
	class Line {
	    public:
		uintptr_t address;
		unsigned line;
		Line(uintptr_t _address, unsigned _line) {
		    address = _address;
		    line = _line;
		}
	};
	std::vector<Line> lines;

	// Information retrieved later (lazily).
	void *imp;

	RoxorFunction(Function *_f, unsigned char *_start,
		unsigned char *_end) {
	    f = _f;
	    start = _start;
	    end = _end;
	    imp = NULL;
	}
};

class RoxorJITManager : public JITMemoryManager, public JITEventListener {
    private:
        JITMemoryManager *mm;
	std::vector<RoxorFunction *> functions;

	RoxorFunction *current_function(void) {
	    assert(!functions.empty());
	    return functions.back();
	}

    public:
	RoxorJITManager() : JITMemoryManager() { 
	    mm = CreateDefaultMemManager(); 
	}

	RoxorFunction *find_function(uint8_t *addr) {
	     if (functions.empty()) {
		return NULL;
	     }
	     // TODO optimize me!
	     RoxorFunction *front = functions.front();
	     RoxorFunction *back = functions.back();
	     if (addr < front->start || addr > back->end) {
		return NULL;
	     }
	     std::vector<RoxorFunction *>::iterator iter = 
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

	RoxorFunction *delete_function(Function *func) {
	    std::vector<RoxorFunction *>::iterator iter = 
		functions.begin();
	    while (iter != functions.end()) {
		RoxorFunction *f = *iter;
		if (f->f == func) {
		    functions.erase(iter);
		    return f;
		}
		++iter;
	    }
	    return NULL;
	}

	// JITMemoryManager callbacks.

	void setMemoryWritable(void) { 
	    mm->setMemoryWritable(); 
	}

	void setMemoryExecutable(void) { 
	    mm->setMemoryExecutable(); 
	}

	uint8_t *allocateSpace(intptr_t Size, unsigned Alignment) { 
	    return mm->allocateSpace(Size, Alignment); 
	}

	uint8_t *allocateGlobal(uintptr_t Size, unsigned Alignment) {
	    return mm->allocateGlobal(Size, Alignment);
	}

	void AllocateGOT(void) {
	    mm->AllocateGOT();
	}

	uint8_t *getGOTBase() const {
	    return mm->getGOTBase();
	}

	uint8_t *startFunctionBody(const Function *F, 
		uintptr_t &ActualSize) {
	    return mm->startFunctionBody(F, ActualSize);
	}

	uint8_t *allocateStub(const GlobalValue* F, 
		unsigned StubSize, 
		unsigned Alignment) {
	    return mm->allocateStub(F, StubSize, Alignment);
	}

	void endFunctionBody(const Function *F, uint8_t *FunctionStart, 
		uint8_t *FunctionEnd) {
	    mm->endFunctionBody(F, FunctionStart, FunctionEnd);
	    Function *f = const_cast<Function *>(F);
	    functions.push_back(new RoxorFunction(f, FunctionStart,
			FunctionEnd));
	}

	void deallocateFunctionBody(void *data) {
	    mm->deallocateFunctionBody(data);
	}

	void deallocateExceptionTable(void *data) {
	    mm->deallocateExceptionTable(data);
	}

	uint8_t* startExceptionTable(const Function* F, 
		uintptr_t &ActualSize) {
	    return mm->startExceptionTable(F, ActualSize);
	}

	void endExceptionTable(const Function *F, uint8_t *TableStart, 
		uint8_t *TableEnd, uint8_t* FrameRegister) {
	    current_function()->ehs.push_back(FrameRegister);
	    mm->endExceptionTable(F, TableStart, TableEnd, FrameRegister);
	}

	void setPoisonMemory(bool poison) {
	    mm->setPoisonMemory(poison);
	}

	// JITEventListener callbacks.

	void NotifyFunctionEmitted(const Function &F,
		void *Code, size_t Size,
		const EmittedFunctionDetails &Details) {
	    RoxorFunction *function = current_function();

	    std::string path;
	    for (std::vector<EmittedFunctionDetails::LineStart>::const_iterator iter = Details.LineStarts.begin(); iter != Details.LineStarts.end(); ++iter) {
		MDNode *scope = iter->Loc.getAsMDNode(F.getContext());
		DILocation loc = DILocation(scope);
		if (path.size() == 0) {
		    RoxorCompiler::shared->generate_location_path(path, loc);
		}
//printf("%p -> %d\n", (void*)iter->Address, loc.getLineNumber());
		RoxorFunction::Line line(iter->Address, loc.getLineNumber());
		function->lines.push_back(line);
	    }

	    function->path = path;
	}
};
#endif

extern "C" void *__cxa_allocate_exception(size_t);
extern "C" void __cxa_throw(void *, void *, void (*)(void *));
extern "C" void __cxa_rethrow(void);
extern "C" std::type_info *__cxa_current_exception_type(void);

RoxorCore::RoxorCore(void)
{
    running = false;
    abort_on_exception = false;

    pthread_assert(pthread_mutex_init(&gl, 0));

    // Will be set later.
    default_random = Qnil;

    load_path = rb_ary_new();
    GC_RETAIN(load_path);

    loaded_features = rb_ary_new();
    GC_RETAIN(loaded_features);

    threads = rb_ary_new();
    GC_RETAIN(threads);

#if !MACRUBY_STATIC
    bs_parser = NULL;
    llvm_start_multithreaded();
    interpreter_enabled = getenv("VM_DISABLE_INTERPRETER") == NULL;

    // The JIT is created later, if necessary.
    InitializeNativeTarget();
    jmm = NULL; 
    ee = NULL;
    fpm = NULL;

# if ROXOR_VM_DEBUG
    functions_compiled = 0;
# endif
#endif // !MACRUBY_STATIC
}

void
RoxorCore::prepare_jit(void)
{
#if !defined(MACRUBY_STATIC)
    assert(ee == NULL);
    jmm = new RoxorJITManager;

    opt_level = CodeGenOpt::Default;
    const char *env_str = getenv("VM_OPT_LEVEL");
    if (env_str != NULL) {
	const int tmp = atoi(env_str);
	if (tmp >= 0 && tmp <= 3) {
	    switch (tmp) {
		case 0:
		    opt_level = CodeGenOpt::None;
		    break;
		case 1:
		    opt_level = CodeGenOpt::Less;
		    break;
		case 2:
		    opt_level = CodeGenOpt::Default;
		    break;
		case 3:
		    opt_level = CodeGenOpt::Aggressive;
		    break;
	    }
	}
    }

    std::string err;
    ee = ExecutionEngine::createJIT(RoxorCompiler::module, &err, jmm,
	    opt_level, false);
    if (ee == NULL) {
	fprintf(stderr, "error while creating JIT: %s\n", err.c_str());
	abort();
    }
    ee->DisableLazyCompilation();
    ee->RegisterJITEventListener(jmm);

    fpm = new FunctionPassManager(RoxorCompiler::module);
    fpm->add(new TargetData(*ee->getTargetData()));

    // Do simple "peephole" optimizations and bit-twiddling optzns.
    fpm->add(createInstructionCombiningPass());
    // Eliminate unnecessary alloca.
    fpm->add(createPromoteMemoryToRegisterPass());
    // Reassociate expressions.
    fpm->add(createReassociatePass());
    // Eliminate Common SubExpressions.
    fpm->add(createGVNPass());
    // Simplify the control flow graph (deleting unreachable blocks, etc).
    fpm->add(createCFGSimplificationPass());
    // Eliminate tail calls.
    fpm->add(createTailCallEliminationPass());
#endif
}

RoxorCore::~RoxorCore(void)
{
    // TODO
}

RoxorVM::RoxorVM(void)
{
    current_top_object = Qnil;
    current_class = NULL;
    outer_stack = NULL;
    current_outer = NULL;
    safe_level = 0;
    backref = Qnil;
    broken_with = Qundef;
    last_line = Qnil;
    last_status = Qnil;
    errinfo = Qnil;
    parse_in_eval = false;
    has_ensure = false;
    return_from_block = -1;
    special_exc = NULL;
    current_super_class = NULL;
    current_super_sel = 0;
    current_mri_method_self = Qnil;
    current_mri_method_sel = 0;

    mcache = (struct mcache *)calloc(VM_MCACHE_SIZE, sizeof(struct mcache));
    assert(mcache != NULL);
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
    outer_stack = vm.outer_stack;
    GC_RETAIN(outer_stack);
    current_outer = vm.current_outer;
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
	    GC_RETAIN(b);
	    blocks[block_cache_key(orig)] = b;
	}
	current_blocks.push_back(b);
    }

    // TODO bindings, exceptions?

    backref = Qnil;
    broken_with = Qundef;
    last_line = Qnil;
    last_status = Qnil;
    errinfo = Qnil;
    parse_in_eval = false;
    has_ensure = false;
    return_from_block = -1;
    special_exc = NULL;
    current_super_class = NULL;
    current_super_sel = 0;

    mcache = (struct mcache *)calloc(VM_MCACHE_SIZE, sizeof(struct mcache));
    assert(mcache != NULL);
}

RoxorVM::~RoxorVM(void)
{
    for (std::map<void *, rb_vm_block_t *>::iterator i = blocks.begin();
	i != blocks.end();
	++i) {
	GC_RELEASE(i->second);
    }
    blocks.clear();

    GC_RELEASE(outer_stack);
    GC_RELEASE(backref);
    GC_RELEASE(broken_with);
    GC_RELEASE(last_status);
    GC_RELEASE(errinfo);

    free(mcache);
    mcache = NULL;
}

void
RoxorVM::debug_blocks(void)
{
    for (std::vector<rb_vm_block_t *>::iterator i = current_blocks.begin();
	    i != current_blocks.end();
	    ++i) {
	printf("%p ", *i);
    }
    printf("\n");
}

void
RoxorVM::debug_exceptions(void)
{
    for (std::vector<VALUE>::iterator i = current_exceptions.begin();
	    i != current_exceptions.end();
	    ++i) {
	    printf("current_exceptions[%d] = (%p) \"%s\"",
				(int)(i - current_exceptions.begin()),
				(void *)*i,
				RSTRING_PTR(rb_inspect(*i)));
    }
    printf("\n");
}

#if !defined(MACRUBY_STATIC)
static bool
should_optimize(Function *func)
{
    // Don't optimize big functions.
    size_t insns = 0;
    for (Function::iterator i = func->begin(); i != func->end(); ++i) {
	insns += i->size();
    }
    return insns < 2000;
}

void
RoxorCore::optimize(Function *func)
{
    switch (opt_level) {
	case CodeGenOpt::None:
	    break;

	case CodeGenOpt::Aggressive:
	    RoxorCompiler::shared->inline_function_calls(func);
	    goto optimize;

	default:
	    if (ruby_aot_compile || should_optimize(func)) {
optimize:
		if (fpm != NULL) {	
		    fpm->run(*func);
		}
	    }
	    break;    
    }
}

extern "C"
void
rb_verify_module(void)
{
    if (verifyModule(*RoxorCompiler::module, PrintMessageAction)) {
	printf("Error during module verification\n");
	abort();
    }
}

IMP
RoxorCore::compile(Function *func, bool run_optimize)
{
    std::map<Function *, IMP>::iterator iter = JITcache.find(func);
    if (iter != JITcache.end()) {
	return iter->second;
    }

#if ROXOR_COMPILER_DEBUG
    // in AOT mode, the verifier is already called
    // (and calling it here would check functions not fully compiled yet)
    if (!ruby_aot_compile) {
	rb_verify_module();
    }

    uint64_t start = mach_absolute_time();
#endif

    // Optimize if needed.
    if (run_optimize) {
	optimize(func);
    }

    // Compile & cache.
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

// in libgcc
extern "C" void __deregister_frame(const void *);

void
RoxorCore::delenda(Function *func)
{
    assert(func->use_empty());

    RoxorCoreLock lock;

    // Remove from cache.
    std::map<Function *, IMP>::iterator iter = JITcache.find(func);
    if (iter != JITcache.end()) {
	JITcache.erase(iter);
    }

    // Delete for JIT memory manager list.
    RoxorFunction *f = jmm->delete_function(func);
    assert(f != NULL);

    // Unregister each dwarf exception handler.
    // XXX this should really be done by LLVM...
    for (std::vector<unsigned char *>::iterator i = f->ehs.begin();
	    i != f->ehs.end(); ++i) {
	__deregister_frame((const void *)*i);
    }

    // Remove the compiler scope.
    delete f;

    // Delete machine code.
    ee->freeMachineCodeForFunction(func);

    // Delete IR.
    func->eraseFromParent();
}
#endif

// Dummy function to be used for debugging (in gdb).
extern "C"
void
rb_symbolicate(void *addr)
{
    char path[1000];
    char name[100];
    unsigned long ln = 0;
    if (GET_CORE()->symbolize_call_address(addr, path, sizeof path,
		&ln, name, sizeof name, NULL)) {
	printf("addr %p selector %s location %s:%ld\n",
		addr, name, path, ln);
    }
    else {
	printf("addr %p unknown\n", addr);
    }
}

bool
RoxorCore::symbolize_call_address(void *addr, char *path, size_t path_len,
	unsigned long *ln, char *name, size_t name_len,
	unsigned int *interpreter_frame_idx)
{
#if MACRUBY_STATIC
    return false;
#else
    if (jmm == NULL) {
	return false;
    }

    RoxorFunction *f = jmm->find_function((unsigned char *)addr);
    if (f != NULL) {
	if (f->imp == NULL) {
	    f->imp = ee->getPointerToFunctionOrStub(f->f);
	}
    }
    else {
	std::string fr_name;
	std::string fr_path;
	unsigned int fr_line = 0;
	if (interpreter_frame_idx == NULL
		|| !RoxorInterpreter::shared->frame_at_index(*interpreter_frame_idx,
		    addr, &fr_name, &fr_path, &fr_line)) {
	    return false;
	}
	(*interpreter_frame_idx)++;
	if (name != NULL) {
	    strlcpy(name, fr_name.c_str(), name_len);
	}
	if (path != NULL) {
	    strlcpy(path, fr_path.c_str(), path_len);
	}
	if (ln != NULL) {
	    *ln = fr_line;
	}
	return true;
    }

    if (f != NULL) {
	if (ln != NULL) {
	    *ln = 0;
	    for (std::vector<RoxorFunction::Line>::iterator iter =
		    f->lines.begin(); iter != f->lines.end(); ++iter) {
		if ((uintptr_t)addr <= (*iter).address) {
		    break;
		}
		*ln = (*iter).line;
	    }
	}
	if (path != NULL) {
	    strlcpy(path, f->path.c_str(), path_len);
	}
	if (name != NULL) {
	    std::map<IMP, rb_vm_method_node_t *>::iterator iter = 
		ruby_imps.find((IMP)f->imp);
	    if (iter == ruby_imps.end()) {
		strlcpy(name, "block", name_len);
	    }
	    else {
		strlcpy(name, sel_getName(iter->second->sel), name_len);
	    }
	}
    }
    else {
	if (ln != NULL) {
	    *ln = 0;
	}
	if (path != NULL) {
	    strlcpy(path, "core", path_len);
	}
	if (name != NULL) {
	    name[0] = '\0';
	}
    }

    return true;
#endif
}

void
RoxorCore::symbolize_backtrace_entry(int index, char *path, size_t path_len,
	unsigned long *ln, char *name, size_t name_len)
{
    void *callstack[10];
    const int callstack_n = backtrace(callstack, 10);

    index++; // count us!

    if (callstack_n < index
	    || !GET_CORE()->symbolize_call_address(callstack[index], path,
		path_len, ln, name, name_len, NULL)) {
	if (path != NULL) {
	    strlcpy(path, "core", path_len);
	}
	if (ln != NULL) {
	    *ln = 0;
	}
    }
}

struct ccache *
RoxorCore::constant_cache_get(ID path)
{
    std::map<ID, struct ccache *>::iterator iter = ccache.find(path);
    if (iter == ccache.end()) {
	struct ccache *cache = (struct ccache *)malloc(sizeof(struct ccache));
	assert(cache != NULL);
	cache->outer = 0;
	cache->outer_stack = NULL;
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

rb_vm_method_node_t *
RoxorCore::method_node_get(IMP imp, bool create)
{
    rb_vm_method_node_t *n;
    std::map<IMP, rb_vm_method_node_t *>::iterator iter = ruby_imps.find(imp);
    if (iter == ruby_imps.end()) {
	if (create) {
	    n = (rb_vm_method_node_t *)malloc(sizeof(rb_vm_method_node_t));
	    assert(n != NULL);
	    ruby_imps[imp] = n;
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

rb_vm_method_node_t *
RoxorCore::method_node_get(Method m, bool create)
{
    rb_vm_method_node_t *n;
    std::map<Method, rb_vm_method_node_t *>::iterator iter =
	ruby_methods.find(m);
    if (iter == ruby_methods.end()) {
	if (create) {
	    n = (rb_vm_method_node_t *)malloc(sizeof(rb_vm_method_node_t));
	    assert(n != NULL);
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

#if !defined(MACRUBY_STATIC)
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

#ifdef __LP64__
# define LARGE_STRUCT_SIZE 128
#else
# define LARGE_STRUCT_SIZE 64
#endif /* !__LP64__ */

bool
RoxorCore::is_large_struct_type(const Type *type)
{
    return type->getTypeID() == Type::StructTyID
	&& ee->getTargetData()->getTypeSizeInBits(type) > LARGE_STRUCT_SIZE;
}

GlobalVariable *
RoxorCore::redefined_op_gvar(SEL sel, bool create)
{
    std::map <SEL, GlobalVariable *>::iterator iter =
	redefined_ops_gvars.find(sel);
    GlobalVariable *gvar = NULL;
    if (iter == redefined_ops_gvars.end()) {
	if (create) {
	    // TODO: if OPTZ_LEVEL is 3, force global variables to always be
	    // true and read-only.
	    gvar = new GlobalVariable(*RoxorCompiler::module,
		    Type::getInt8Ty(context),
		    false,
		    GlobalValue::InternalLinkage,
		    ConstantInt::get(Type::getInt8Ty(context), 0),
		    "");
	    assert(gvar != NULL);
	    redefined_ops_gvars[sel] = gvar;
	}
    }
    else {
	gvar = iter->second;
    }
    return gvar;
}
#endif

bool
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

    // Assume yes by default.
    return true;
}

static ID
sanitize_mid(SEL sel)
{
    const char *selname = sel_getName(sel);
    const size_t sellen = strlen(selname);
    if (selname[sellen - 1] == ':') {
	if (memchr(selname, ':', sellen - 1) != NULL) {
	    return 0;
	}
	char buf[100];
	strncpy(buf, selname, sizeof buf);
	buf[sellen - 1] = '\0';
	return rb_intern(buf);
    }
    return rb_intern(selname);
}

void
RoxorCore::method_added(Class klass, SEL sel)
{
    if (get_running()) {
	// Call method_added: or singleton_method_added:.
	ID mid = sanitize_mid(sel);
	if (mid != 0) {
	    VALUE sym = ID2SYM(mid);
	    if (RCLASS_SINGLETON(klass)) {
		VALUE sk = rb_singleton_class_attached_object((VALUE)klass);
		rb_vm_call(sk, selSingletonMethodAdded, 1, &sym);
	    }
	    else {
		rb_vm_call((VALUE)klass, selMethodAdded, 1, &sym);
	    }
	}
    }
}

void
RoxorCore::invalidate_method_cache(SEL sel)
{
    struct mcache *cache = GET_VM()->get_mcache();
    for (int i = 0; i < VM_MCACHE_SIZE; i++) {
	struct mcache *e = &cache[i];
	if (e->sel == sel) {
	    e->flag = 0;
	}
    }
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
    printf("defining %c[%s %s] with imp %p/%p types %s flags %d arity %d\n",
	    class_isMetaClass(klass) ? '+' : '-',
	    class_getName(klass),
	    sel_getName(sel),
	    imp,
	    ruby_imp,
	    types,
	    flags,
	    arity.real);
#endif

    // Register the implementation into the runtime.
    class_replaceMethod(klass, sel, imp, types);

    // Cache the method.
    Method m = class_getInstanceMethod(klass, sel);
    assert(m != NULL);
    assert(method_getImplementation(m) == imp);
    rb_vm_method_node_t *real_node = method_node_get(m, true);
    real_node->klass = klass;
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
    node->klass = klass;
    node->arity = arity;
    node->flags = flags;
    node->sel = sel;
    node->ruby_imp = ruby_imp;
    if (imp != ruby_imp) {
	ruby_imps[ruby_imp] = node;
    }

    if (running) {
	// Invalidate respond_to cache.
	invalidate_respond_to_cache();

	// Invalidate dispatch cache.
	invalidate_method_cache(sel);

	// Invalidate inline operations.
#if !defined(MACRUBY_STATIC)
	GlobalVariable *gvar = redefined_op_gvar(sel, false);
	if (gvar != NULL && should_invalidate_inline_op(sel, klass)) {
	    void *val = ee->getOrEmitGlobalVariable(gvar);
#if ROXOR_VM_DEBUG
	    printf("change redefined global for [%s %s] to true\n",
		    class_getName(klass),
		    sel_getName(sel));
#endif
	    assert(val != NULL);
	    *(unsigned char *)val = 1;
	}
#endif
    }

    // If alloc is redefined, mark the class as such.
    if (sel == selAlloc
	&& (RCLASS_VERSION(klass) & RCLASS_HAS_ROBJECT_ALLOC) 
	== RCLASS_HAS_ROBJECT_ALLOC) {
	RCLASS_SET_VERSION(klass, (RCLASS_VERSION(klass) ^ 
		    RCLASS_HAS_ROBJECT_ALLOC));
    }

    // Forward method definition to the included classes.
    if (RCLASS_VERSION(klass) & RCLASS_IS_INCLUDED) {
	VALUE included_in_classes = rb_attr_get((VALUE)klass, 
		idIncludedInClasses);
	if (included_in_classes != Qnil) {
	    for (int i = 0, count = RARRAY_LEN(included_in_classes);
		    i < count; i++) {
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
		node->klass = (Class)mod;
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
VALUE
rb_vm_default_random(void)
{
    return GET_CORE()->get_default_random();
}

extern "C"
void
rb_vm_set_default_random(VALUE random)
{
    RoxorCore *core = GET_CORE();
    RoxorCoreLock lock;

    if (core->get_default_random() != random) {
	GC_RELEASE(core->get_default_random());
	GC_RETAIN(random);
	core->set_default_random(random);
    }
}

VALUE
RoxorCore::trap_cmd_for_signal(int signal)
{
    RoxorCoreLock lock;

    return trap_cmd[signal];
}

extern "C"
VALUE
rb_vm_trap_cmd_for_signal(int signal)
{
    return GET_CORE()->trap_cmd_for_signal(signal);
}

int
RoxorCore::trap_level_for_signal(int signal)
{
    RoxorCoreLock lock;

    return trap_level[signal];
}

extern "C"
int
rb_vm_trap_level_for_signal(int signal)
{
    return GET_CORE()->trap_level_for_signal(signal);
}

void
RoxorCore::set_trap_for_signal(VALUE trap, int level, int signal)
{
    RoxorCoreLock lock;

    VALUE oldtrap = trap_cmd[signal];
    if (oldtrap != trap) {
	GC_RELEASE(oldtrap);
	GC_RETAIN(trap);
	trap_cmd[signal] = trap;
	trap_level[signal] = level;
    }
}

extern "C"
void
rb_vm_set_trap_for_signal(VALUE trap, int level, int signal)
{
    GET_CORE()->set_trap_for_signal(trap, level, signal);
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

static inline VALUE
rb_const_get_direct(VALUE klass, ID id)
{
    // Search the given class.
    CFDictionaryRef iv_dict = rb_class_ivar_dict(klass);
    if (iv_dict != NULL) {
retry:
	VALUE value;
	if (CFDictionaryGetValueIfPresent(iv_dict, (const void *)id,
		    (const void **)&value)) {
	    if (value == Qundef) {
		// Constant is a candidate for autoload. We must release the
		// GIL before requiring the file and acquire it again.
		GET_CORE()->unlock();
		const bool autoloaded = RTEST(rb_autoload_load(klass, id));
		GET_CORE()->lock();
		if (autoloaded) {
		    goto retry;
		}
	    }
	    return value;
	}
    }
    // Search the included modules.
    VALUE mods = rb_attr_get(klass, idIncludedModules);
    if (mods != Qnil) {
	int i, count = RARRAY_LEN(mods);
	for (i = 0; i < count; i++) {
	    VALUE mod = RARRAY_AT(mods, i);
	    VALUE val = rb_const_get_direct(mod, id);
	    if (val != Qundef) {
		return val;
	    }
	}
    }
    return Qundef;
}

#if ROXOR_VM_DEBUG_CONST
extern "C" const char *ruby_node_name(int node);

static void
rb_vm_print_outer_stack(const char *fname, NODE *node, const char *function, int line,
			rb_vm_outer_t *outer_stack, const char *prefix)
{
    if (fname != NULL) {
	printf("%s:", fname);
    }
    if (node != NULL) {
	printf("%ld:%s:", nd_line(node), ruby_node_name(nd_type(node)));
    }
    printf("%s:%d:", function, line);
    if (prefix != NULL && prefix[0] != '\0') {
	printf("%s ", prefix);
    }
    printf("outer_stack(");
    
    bool first = true;
    for (rb_vm_outer_t *o = outer_stack; o != NULL; o = o->outer) {
	if (first) {
	    first = false;
	}
	else {
	    printf(" > ");
	}
	printf("%s", class_getName(o->klass));
	if (o->pushed_by_eval) {
	    printf("[skip]");
	}
    }
    printf(")\n");
}
#endif

extern "C"
VALUE
rb_vm_const_lookup_level(VALUE outer, ID path, bool lexical, bool defined,
	rb_vm_outer_t *outer_stack)
{
    rb_vm_check_if_module(outer);
#if ROXOR_VM_DEBUG_CONST
    printf("%s:%d:%s:outer(%s) path(%s) lexical(%s) defined(%s) outer_stack(%p)\n", __FILE__, __LINE__, __FUNCTION__,
	   class_getName((Class)outer), rb_id2name(path), lexical ? "true" : "false", defined ? "true" : "false", outer_stack);
    if (lexical) {
        GET_CORE()->lock();
	rb_vm_print_outer_stack(NULL, NULL, __FUNCTION__, __LINE__,
				GET_VM()->get_outer_stack(), "vm->get_outer_stack");

	rb_vm_print_outer_stack(NULL, NULL, __FUNCTION__, __LINE__,
				GET_VM()->get_current_outer(), "vm->get_current_outer");

	GET_CORE()->unlock();
    }
#endif

    if (lexical && outer_stack != NULL) {
	// Let's do a lexical lookup before a hierarchical one, by looking for
	// the given constant in all modules under the given outer.
	GET_CORE()->lock();
#if ROXOR_VM_DEBUG_CONST
	rb_vm_print_outer_stack(NULL, NULL, __FUNCTION__, __LINE__,
				outer_stack, "compile time");
#endif
	rb_vm_outer_t *root_outer = outer_stack;
	while (root_outer != NULL && root_outer->pushed_by_eval) {
	    root_outer = root_outer->outer;
	}
	for (rb_vm_outer_t *o = root_outer; o != NULL; o = o->outer) {
	    if (o->pushed_by_eval) {
		continue;
	    }
	    VALUE val = rb_const_get_direct((VALUE)o->klass, path);
	    if (val != Qundef) {
		GET_CORE()->unlock();
		return defined ? Qtrue : val;
	    }
	}
	if (root_outer && !NIL_P(root_outer->klass)) {
	    outer = (VALUE)root_outer->klass;
	}
	GET_CORE()->unlock();
    }

    // Nothing was found earlier so here we do a hierarchical lookup.
    return defined ? rb_const_defined(outer, path) : rb_const_get(outer, path);
}

extern "C"
void
rb_vm_const_is_defined(ID path)
{
    GET_CORE()->const_defined(path);
}

extern "C"
VALUE
rb_vm_module_nesting(void)
{
    VALUE ary = rb_ary_new();
    for (rb_vm_outer_t *o = GET_VM()->get_current_outer(); o != NULL; o = o->outer) {
	if (!o->pushed_by_eval) {
	    rb_ary_push(ary, (VALUE)o->klass);
	}
    }
    return ary;
}

extern "C"
VALUE
rb_vm_module_constants(void)
{
    VALUE cbase = 0;
    void *data = 0;
    for (rb_vm_outer_t *o = GET_VM()->get_current_outer(); o != NULL; o = o->outer) {
	if (!o->pushed_by_eval) {
	    data = rb_mod_const_at((VALUE)o->klass, data);
	    if (cbase == 0) {
		cbase = (VALUE)o->klass;
	    }
	}
    }
    data = rb_mod_const_at(rb_cObject, data);
    if (cbase == 0) {
	cbase = rb_cObject;
    }
    if (cbase != 0) {
	data = rb_mod_const_of(cbase, data);
    }
    return rb_const_list(data);
}

static VALUE
get_klass_const(VALUE outer, ID path, bool lexical, rb_vm_outer_t *outer_stack)
{
    VALUE klass = Qundef;
    if (lexical) {
	if (rb_vm_const_lookup(outer, path, true, true, outer_stack) == Qtrue) {
	    klass = rb_vm_const_lookup(outer, path, true, false, outer_stack);
	}
    }
    else {
	if (rb_const_defined_at(outer, path)) {
	    klass = rb_const_get_at(outer, path);
	}
    }
    if (klass != Qundef) {
	rb_vm_check_if_module(klass);
	if (outer != rb_cObject && !RCLASS_RUBY(klass)) {
	    // Ignore classes retrieved by the dynamic resolver.
	    klass = Qundef;
	}
    }
    return klass;
}

extern "C"
VALUE
rb_vm_define_class(ID path, VALUE outer, VALUE super, int flags,
	unsigned char dynamic_class, rb_vm_outer_t *outer_stack)
{
    assert(path > 0);
    if (flags & DEFINE_OUTER) {
	if (outer_stack == NULL) {
	    outer = rb_cNSObject;
	}
	else {
	    outer = outer_stack->klass ? (VALUE)outer_stack->klass : Qnil;
	}
    }
    rb_vm_check_if_module(outer);

    VALUE klass = get_klass_const(outer, path, dynamic_class, outer_stack);
    if (klass != Qundef) {
	// Constant is already defined.
	if (!(flags & DEFINE_MODULE) && super != 0) {
	    if (rb_class_real(RCLASS_SUPER(klass), true) != super) {
		rb_raise(rb_eTypeError, "superclass mismatch for class %s",
			rb_class2name(klass));
	    }
	}
    }
    else {
	// Prepare the constant outer.
	VALUE const_outer;
	if ((flags & DEFINE_OUTER) || (flags & DEFINE_SUB_OUTER)) {
	    const_outer = outer;
	}
	else {
	    const_outer = rb_cObject;
	}

	// Define the constant.
	if (flags & DEFINE_MODULE) {
	    assert(super == 0);
	    klass = rb_define_module_id(path);
	    rb_set_class_path2(klass, outer, rb_id2name(path), const_outer);
	    rb_const_set(outer, path, klass);
	}
	else {
	    if (super == 0) {
		super = rb_cObject;
	    }
	    else {
		if (TYPE(super) != T_CLASS) {
		    rb_raise(rb_eTypeError,
			"wrong argument type (expected Class)");
		}
	    }
	    klass = rb_define_class_id(path, super);
	    rb_set_class_path2(klass, outer, rb_id2name(path), const_outer);
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
struct icache *
rb_vm_ivar_slot_allocate(void)
{
    struct icache *icache = (struct icache *)malloc(sizeof(struct icache));
    assert(icache != NULL);
    icache->klass = 0;
    icache->slot = SLOT_CACHE_VIRGIN;
    return icache;
}

extern "C"
int
rb_vm_get_ivar_slot(VALUE obj, ID name, bool create)
{
    if (TYPE(obj) == T_OBJECT) {
        unsigned int i;
        for (i = 0; i < ROBJECT(obj)->num_slots; i++) {
            if (ROBJECT(obj)->slots[i].name == name) {
                return i;
            }
        }
	if (create) {
	    for (i = 0; i < ROBJECT(obj)->num_slots; i++) {
		if (ROBJECT(obj)->slots[i].value == Qundef) {
		    ROBJECT(obj)->slots[i].name = name;
		    return i;
		}
	    }
	    const int new_slot = ROBJECT(obj)->num_slots;
	    rb_vm_regrow_robject_slots(ROBJECT(obj), new_slot + 1);
	    ROBJECT(obj)->slots[new_slot].name = name;
	    return new_slot;
	}
    }
    return -1;
}

extern "C" void rb_print_undef(VALUE, ID, int);

static void
vm_alias_method(Class klass, Method method, ID name, bool noargs)
{
    IMP imp = method_getImplementation(method);
    if (UNAVAILABLE_IMP(imp)) {
	return;
    }

    const char *types = method_getTypeEncoding(method);
    SEL sel = rb_vm_id_to_sel(name, noargs ? 0 : 1);
    rb_vm_method_node_t *node = GET_CORE()->method_node_get(method);
    if (node != NULL) {
	GET_CORE()->add_method(klass, sel, imp, node->ruby_imp,
		node->arity, node->flags, types);
    }
    else {
	class_replaceMethod(klass, sel, imp, types);
    }
}

static void
vm_alias(VALUE outer, ID name, ID def)
{
    if (NIL_P(outer)) {
	rb_raise(rb_eTypeError, "no class to make alias");
    }

    rb_frozen_class_p(outer);
    if (outer == rb_cObject) {
        rb_secure(4);
    }

    VALUE dest = outer;
    Class klass = (Class)outer;
    Class dest_klass = (Class)dest;
    const bool klass_is_mod = TYPE(klass) == T_MODULE;

    const char *def_str = rb_id2name(def);
    SEL sel = sel_registerName(def_str);
    Method def_method1 = class_getInstanceMethod(klass, sel);
    if (def_method1 == NULL && klass_is_mod) {
	def_method1 = class_getInstanceMethod((Class)rb_cObject, sel);
    }

    Method def_method2 = NULL;
    if (def_str[strlen(def_str) - 1] != ':') {
	char tmp[100];
	snprintf(tmp, sizeof tmp, "%s:", def_str);
	sel = sel_registerName(tmp);
 	def_method2 = class_getInstanceMethod(klass, sel);
	if (def_method2 == NULL && klass_is_mod) {
	    def_method2 = class_getInstanceMethod((Class)rb_cObject, sel);
	}
    }

    if (def_method1 == NULL && def_method2 == NULL) {
	rb_print_undef((VALUE)klass, def, 0);
    }
    if (def_method1 != NULL) {
	vm_alias_method(dest_klass, def_method1, name, true);
    }
    if (def_method2 != NULL) {
	vm_alias_method(dest_klass, def_method2, name, false);
    }
}

extern "C"
void
rb_vm_alias2(VALUE outer, VALUE name, VALUE def, unsigned char dynamic_class)
{
    if (dynamic_class) {
	Class k = GET_VM()->get_current_class();
	if (NIL_P(outer)) {
	    rb_raise(rb_eTypeError, "no class to make alias");
	}
	else if (k != NULL) {
	    outer = (VALUE)k;
	}
    }

    // Given arguments should always be symbols (compiled as such).
    assert(TYPE(name) == T_SYMBOL);
    assert(TYPE(def) == T_SYMBOL);

    vm_alias(outer, SYM2ID(name), SYM2ID(def));
}

extern "C"
void
rb_vm_alias(VALUE outer, ID name, ID def)
{
    vm_alias(outer, name, def);
}

extern "C"
void
rb_vm_undef(VALUE klass, ID name, unsigned char dynamic_class)
{
    if (dynamic_class) {
	Class k = GET_VM()->get_current_class();
	if (k != NULL) {
	    klass = (VALUE)k;
	}
    }
    rb_vm_undef_method((Class)klass, name, true);
}

extern "C"
void
rb_vm_undef2(VALUE klass, VALUE sym, unsigned char dynamic_class)
{
    assert(TYPE(sym) == T_SYMBOL);
    return rb_vm_undef(klass, SYM2ID(sym), dynamic_class);
}

extern "C"
VALUE
rb_vm_defined(VALUE self, int type, VALUE what, VALUE what2, rb_vm_outer_t *outer_stack)
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
		if (rb_vm_const_lookup(what2, (ID)what, type == DEFINED_LCONST, true, outer_stack)) {
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
		if (what == 0) {
		    rb_raise(rb_eRuntimeError,
			    "defined?(super) out of a method block isn't supported");
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

static bool
kvo_sel(Class klass, const char *selname, const size_t selsize,
	const char *begin, const char *end)
{
    // ^#{begin}(.+)#{end}$ -> token
    const size_t begin_len = strlen(begin);
    const size_t end_len = strlen(end);
    unsigned int token_beg = 0, token_end = selsize;
    if (begin_len > 0) {
	if (strncmp(selname, begin, begin_len) != 0 || selsize <= begin_len) {
	    return false;
	}
	token_beg = begin_len;
    }
    if (end_len > 0) {
	const char *p = strstr(selname, end);
	if (p == NULL || p + end_len != selname + selsize) {
	    return false;
	}
	token_end = p - selname;
    }
    const size_t token_len = token_end - token_beg;
    char token[100];
    if (token_len > sizeof(token)) {
	return false;
    }
    memcpy(token, &selname[token_beg], token_len);
    token[token_len] = '\0';

    if (strchr(token, ':') != NULL) {
	return false;
    }

#if 1
    // token must start with a capital character.
    return isupper(token[0]);
#else
    // token must start with a capital character.
    if (!isupper(token[0])) {
	return false;
    }

    // Decapitalize the token and look if it's a valid KVO attribute.
    token[0] = tolower(token[0]);
    SEL sel = sel_registerName(token);
    return class_getInstanceMethod(klass, sel) != NULL;
#endif
}

static const char *
get_bs_method_type(bs_element_method_t *bs_method, int idx)
{
    const char *type = rb_get_bs_method_type(bs_method, idx);
    if (type == NULL) {
	type = "@";
    }
    return type;
}

static void 
resolve_method_type(char *buf, const size_t buflen, Class klass, Method m,
	SEL sel, const unsigned int types_count)
{
    bs_element_method_t *bs_method = GET_CORE()->find_bs_method(klass, sel);

    if (m == NULL
	|| !rb_objc_get_types(Qnil, klass, sel, m, bs_method, buf, buflen)) {

	std::string *informal_type =
	    GET_CORE()->find_bs_informal_protocol_method(sel,
		    class_isMetaClass(klass));
	if (informal_type != NULL) {
	    // Get the signature from the BridgeSupport database as an
	    // informal protocol method.
	    const char *informal_type_str = informal_type->c_str();
	    strlcpy(buf, informal_type_str, buflen);
	    for (unsigned int i = TypeArity(informal_type_str);
		    i < types_count; i++) {
		strlcat(buf, "@", buflen);
	    }
	}
	else {
	    // Generate an automatic signature. We do check for KVO selectors
	    // which require a customized signature, otherwise we do generate
	    // one that assumes that the return value and all arguments are
	    // objects ('@').
	    const char *selname = sel_getName(sel);
	    const size_t selsize = strlen(selname);

	    if (kvo_sel(klass, selname, selsize, "countOf", "")) {
		strlcpy(buf, "i@:", buflen);
	    }
	    else if (kvo_sel(klass, selname, selsize, "objectIn", "AtIndex:")) {
		strlcpy(buf, "@@:i", buflen);
	    }
	    else if (kvo_sel(klass, selname, selsize, "insertObject:in",
			"AtIndex:")) {
		strlcpy(buf, "v@:@i", buflen);
	    }
	    else if (kvo_sel(klass, selname, selsize, "removeObjectFrom",
			"AtIndex:")) {
		strlcpy(buf, "v@:i", buflen);
	    }
	    else if (kvo_sel(klass, selname, selsize, "replaceObjectIn",
			"AtIndex:withObject:")) {
		strlcpy(buf, "v@:i@", buflen);
	    }
#if 0 // TODO
	    else if (kvo_sel(klass, selname, selsize, "get", ":range:")) {
	    }
#endif
	    else if (bs_method != NULL) {
		buf[0] = '\0';

		// retval, self and sel.
		strlcat(buf, get_bs_method_type(bs_method, -1), buflen);
		strlcat(buf, "@", buflen);
		strlcat(buf, ":", buflen);

		// Arguments.
		for (unsigned int i = 3; i < types_count; i++) {
		    strlcat(buf, get_bs_method_type(bs_method, i - 3), buflen);
		}
	    }
	    else {
		assert(types_count < buflen);

		// retval, self and sel.
		buf[0] = strncmp(selname, "set", 3) == 0 ? 'v' : '@';
		buf[1] = '@';
		buf[2] = ':';

		// Arguments.
		for (unsigned int i = 3; i < types_count; i++) {
		    buf[i] = '@';
		}
		buf[types_count] = '\0';
	    }
	}
    }
    else {
	assert(strlen(buf) >= 3);
	for (unsigned int i = rb_method_getNumberOfArguments(m) + 1;
		i < types_count; i++) {
	    strlcat(buf, "@", buflen);
	}
    }
}

rb_vm_method_node_t *
RoxorCore::retype_method(Class klass, rb_vm_method_node_t *node,
	const char *old_types, const char *new_types)
{
#if MACRUBY_STATIC
    rb_raise(rb_eRuntimeError, "methods cannot be retyped in MacRuby static");
#else
    if (strcmp(old_types, new_types) == 0) {
	// No need to retype.
	// XXX might be better to compare every type after filtering stack
	// size and other crappy modifiers.
	return node;
    }

    const int new_types_arity = TypeArity(new_types);
    char buf[100];
    if (node->arity.real + 3 >= new_types_arity) {
	// The method arity is bigger than the number of types of the new
	// signature, so we need to pad.
	strlcpy(buf, new_types, sizeof buf);
	for (int i = 0; i < node->arity.real + 3 - new_types_arity; i++) {
	    strlcat(buf, "@", sizeof buf);
	}
	new_types = &buf[0];
    }

    RoxorCoreLock lock;

    // Re-generate ObjC stub. 
    Function *objc_func = RoxorCompiler::shared->compile_objc_stub(NULL,
	    node->ruby_imp, node->arity, new_types);
    node->objc_imp = compile(objc_func, false);
    // TODO: free LLVM machine code from old objc IMP
    objc_to_ruby_stubs[node->ruby_imp] = node->objc_imp;

    // Re-add the method.
    return add_method(klass, node->sel, node->objc_imp, node->ruby_imp,
	    node->arity, node->flags, new_types);
#endif
}

struct vm_objc_imp_type {
    const char *types;
    IMP imp;

    vm_objc_imp_type(const char *_types, IMP _imp) {
	types = _types;
	imp = _imp;
    }
};

rb_vm_method_node_t *
RoxorCore::resolve_method(Class klass, SEL sel, void *func,
	const rb_vm_arity_t &arity, int flags, IMP imp, Method m,
	void *objc_imp_types)
{
#if MACRUBY_STATIC
    assert(imp != NULL);
#else
    if (imp == NULL) {
	// Compile if necessary.
	assert(func != NULL);
	imp = compile((Function *)func);
    }
#endif

    // Resolve Objective-C signature.
    const int types_count = arity.real + 3; // retval, self and sel
    char types[100];
    resolve_method_type(types, sizeof types, klass, m, sel, types_count);

    // Retrieve previous-generated Objective-C stub if possible.
    IMP objc_imp = NULL;
    if (objc_imp_types != NULL) {
	std::vector<vm_objc_imp_type> *v =
	    (std::vector<vm_objc_imp_type> *)objc_imp_types;

	for (std::vector<vm_objc_imp_type>::iterator i = v->begin();
		i != v->end(); ++i) {
	    if (strcmp(types, i->types) == 0) {
		objc_imp = i->imp;
		break;
	    }
	}
    }

#if MACRUBY_STATIC
    if (objc_imp == NULL) {
	printf("can't define method `%s' because no Objective-C stub was pre-compiled for types `%s'\n", sel_getName(sel), types);
	abort();
    }
#else
    // Generate Objective-C stub if needed.
    if (objc_imp == NULL) {
	std::map<IMP, IMP>::iterator iter = objc_to_ruby_stubs.find(imp);
	if (iter == objc_to_ruby_stubs.end()) {
	    Function *objc_func = RoxorCompiler::shared->compile_objc_stub(
		    (Function *)func, imp, arity, types);
	    objc_imp = compile(objc_func);
	    objc_to_ruby_stubs[imp] = objc_imp;
	}
	else {
	    objc_imp = iter->second;
	}
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
#endif

    // Finally, add the method.
    return add_method(klass, sel, objc_imp, imp, arity, flags, types);
}

#if !defined(MACRUBY_STATIC)
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
		    NULL, NULL, NULL);
	    map->erase(iter++);
	    free(m);
	    did_something = true;
	}
	else {
	    ++iter;
	}
    }

    // If the map is empty, there is no point in keeping it.
    if (map->size() == 0) {
	std::map<SEL, std::map<Class, rb_vm_method_source_t *> *>::iterator
	    iter = method_sources.find(sel);
	assert(iter != method_sources.end());
	method_sources.erase(iter);
	delete map;	
    }

    return did_something;
}

extern "C"
bool
rb_vm_resolve_method(Class klass, SEL sel)
{
    if (!GET_CORE()->get_running()) {
	return false;
    }

    // Make sure the VM is created & registered to avoid a deadlock.
    GET_VM();

    RoxorCoreLock lock;

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
	assert(m != NULL);
	map->insert(std::make_pair(klass, m));
	method_source_sels.insert(std::make_pair(klass, sel));
    }
    else {
	m = iter->second;
    }

    m->func = func;
    m->arity = arity;
    m->flags = flags;

    invalidate_respond_to_cache();
}
#endif

#if !defined(MACRUBY_STATIC)
static bool class_has_custom_resolver(Class klass);
#endif

static void
prepare_method(Class klass, bool dynamic_class, SEL sel, void *data,
	const rb_vm_arity_t &arity, int flags, bool precompiled,
	void *objc_imp_types)
{
    if (OBJ_FROZEN(klass)) {
	rb_error_frozen("class/module");
    }
    if (dynamic_class) {
	Class k;
	rb_vm_outer_t *o = GET_VM()->get_outer_stack();
	if (o == NULL) {
	    k = (Class)rb_cNSObject;
	}
	else {
	    k = o->klass;
	}
	if (NIL_P(k)) {
	    rb_raise(rb_eTypeError, "no class/module to add method");
	}
	else if (klass != NULL) {
	    const bool meta = class_isMetaClass(klass);
	    klass = k;
	    if (meta && !class_isMetaClass(klass)) {
		klass = *(Class *)klass;
	    }
	}
    }
    else {
	rb_vm_outer_t *o = GET_VM()->get_outer_stack();
	if ((o != NULL && o->klass == NULL) || NIL_P(o)) {
	    rb_raise(rb_eTypeError, "no class/module to add method");
	}
    }

    const long v = RCLASS_VERSION(klass);
    if (v & RCLASS_SCOPE_PRIVATE) {
	flags |= VM_METHOD_PRIVATE;
    }
    else if (v & RCLASS_SCOPE_PROTECTED) {
	flags |= VM_METHOD_PROTECTED;
    }

    if (rb_objc_ignored_sel(sel)) {
	return;
    }

    const char *sel_name = sel_getName(sel);
    const bool genuine_selector = sel_name[strlen(sel_name) - 1] == ':';
#if !defined(MACRUBY_STATIC)
    const bool custom_resolver = class_has_custom_resolver(klass);
#endif
    bool redefined = false;
    bool added_modfunc = false;
    SEL orig_sel = sel;
    Method m;
    IMP imp = NULL;

prepare_method:

    m = class_getInstanceMethod(klass, sel);
#if !defined(MACRUBY_STATIC)
    if (m == NULL && rb_vm_resolve_method(klass, sel)) {
	m = class_getInstanceMethod(klass, sel);
	assert(m != NULL);
    }
#endif

    if (precompiled) {
	if (imp == NULL) {
	    imp = (IMP)data;
	}
	assert(objc_imp_types != NULL);
	GET_CORE()->resolve_method(klass, sel, NULL, arity, flags, imp, m,
		objc_imp_types);
    }
    else {
#if MACRUBY_STATIC
	abort();
#else
	Function *func = (Function *)data;
	GET_CORE()->lock();
	if (m != NULL || custom_resolver) {
	    // The method already exists _or_ the class implemented a custom
	    // Objective-C method resolver - we need to JIT it.
	    if (imp == NULL) {
		imp = GET_CORE()->compile(func);
	    }
	    GET_CORE()->resolve_method(klass, sel, func, arity, flags, imp, m,
		    objc_imp_types);
	}
	else {
	    // Let's keep the method and JIT it later on demand.
	    GET_CORE()->prepare_method(klass, sel, func, arity, flags);
	}
	GET_CORE()->unlock();
#endif
    }

    if (!redefined) {
	char buf[100];
	SEL new_sel = 0;
	if (!genuine_selector) {
	    snprintf(buf, sizeof buf, "%s:", sel_name);
	    new_sel = sel_registerName(buf);
	    if (arity.max != arity.min) {
		sel = new_sel;
		redefined = true;
		goto prepare_method;
	    }
	}
	else {
	    strlcpy(buf, sel_name, sizeof buf);
	    buf[strlen(buf) - 1] = 0; // remove the ending ':'
	    new_sel = sel_registerName(buf);
	    if (arity.min == 0) {
		sel = new_sel;
		redefined = true;
		goto prepare_method;
	    }
	}
	Method tmp_m = class_getInstanceMethod(klass, new_sel);
	if (tmp_m != NULL) {
	    // If we add -[foo:] and the class responds to -[foo], we need
	    // to disable it (and vice-versa).
	    class_replaceMethod(klass, new_sel,
		    (IMP)rb_vm_undefined_imp, method_getTypeEncoding(tmp_m));
	    // Invalidate the cache so that the previously defined
	    // implementation is not called anymore if the call was cached
	    GET_CORE()->invalidate_method_cache(new_sel);
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
			flags, precompiled, objc_imp_types);
		rb_vm_set_current_scope(mod, SCOPE_DEFAULT);
	    }
	}
    }

    GET_CORE()->method_added(klass, sel);

    if (!added_modfunc && (v & RCLASS_SCOPE_MOD_FUNC)) {
	added_modfunc = true;
	redefined = false;
	klass = *(Class *)klass;
	sel = orig_sel;
	goto prepare_method;
    }
}

#if !defined(MACRUBY_STATIC)
extern "C"
void
rb_vm_prepare_method(Class klass, unsigned char dynamic_class, SEL sel,
	Function *func, const rb_vm_arity_t arity, int flags)
{
    prepare_method(klass, dynamic_class, sel, (void *)func, arity,
	    flags, false, NULL);
}
#endif

extern "C"
void
rb_vm_prepare_method2(Class klass, unsigned char dynamic_class, SEL sel,
	IMP ruby_imp, const rb_vm_arity_t arity, int flags, ...)
{
    std::vector<vm_objc_imp_type> v;
    va_list ar;
    va_start(ar, flags);
    do {
	const char *types = va_arg(ar, const char *);
	if (types == NULL) {
	    break;
	}
	IMP imp = va_arg(ar, IMP);
	assert(imp != NULL);
	v.push_back(vm_objc_imp_type(types, imp));
    }
    while (true);
    va_end(ar);

    prepare_method(klass, dynamic_class, sel, (void *)ruby_imp, arity,
	    flags, true, (void *)&v);
}

#define VISI(x) ((x)&NOEX_MASK)
#define VISI_CHECK(x,f) (VISI(x) == (f))

static void
push_method(VALUE ary, SEL sel, int flags, int (*filter) (VALUE, ID, VALUE))
{
    if (rb_objc_ignored_sel(sel)) {
	return; 
    }

    const char *selname = sel_getName(sel);
    const size_t len = strlen(selname);
    assert(len > 0);
    char *buf = NULL;

    const char *p = strchr(selname, ':');
    if (p != NULL && strchr(p + 1, ':') == NULL) {
	// remove trailing ':' for methods with arity 1
	buf = (char *)malloc(len);
	assert(buf != NULL);
	strncpy(buf, selname, len);
	buf[len - 1] = '\0';
	selname = buf;
    }
 
    ID mid = rb_intern(selname);
    VALUE sym = ID2SYM(mid);

    if (buf != NULL) {
	free(buf);
	buf = NULL;
    }

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

#if !defined(MACRUBY_STATIC)
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
#endif

void
RoxorCore::get_methods(VALUE ary, Class klass, bool include_objc_methods,
	int (*filter) (VALUE, ID, VALUE))
{
    RoxorCoreLock lock;
    // TODO take into account undefined methods

    unsigned int count;
    Method *methods = class_copyMethodList(klass, &count); 
    if (methods != NULL) {
	for (unsigned int i = 0; i < count; i++) {
	    Method m = methods[i];
	    if (UNAVAILABLE_IMP(method_getImplementation(m))) {
		continue;
	    }
	    rb_vm_method_node_t *node = method_node_get(m);
	    if (node == NULL && !include_objc_methods) {
		continue;
	    }
	    SEL sel = method_getName(m);
	    push_method(ary, sel, node == NULL ? 0 : node->flags, filter);
	}
	free(methods);
    }

#if !defined(MACRUBY_STATIC)
    Class k = klass;
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
#endif
}

extern "C"
void
rb_vm_push_methods(VALUE ary, VALUE mod, bool include_objc_methods,
	int (*filter) (VALUE, ID, VALUE))
{
    GET_CORE()->get_methods(ary, (Class)mod, include_objc_methods, filter);
}

extern "C"
void
rb_vm_copy_methods(Class from_class, Class to_class)
{
    GET_CORE()->copy_methods(from_class, to_class);
}

extern "C"
bool
rb_vm_copy_method(Class klass, Method m)
{
    return GET_CORE()->copy_method(klass, m);
}

bool
RoxorCore::copy_method(Class klass, Method m)
{
    SEL sel = method_getName(m);

#if ROXOR_VM_DEBUG
    printf("copy %c[%s %s] from method %p imp %p\n",
	    class_isMetaClass(klass) ? '+' : '-',
	    class_getName(klass),
	    sel_getName(sel),
	    m,
	    method_getImplementation(m));
#endif

    class_replaceMethod(klass, sel, method_getImplementation(m),
	    method_getTypeEncoding(m));

    rb_vm_method_node_t *node = method_node_get(m);
    if (node != NULL) {
	Method m2 = class_getInstanceMethod(klass, sel);
	assert(m2 != NULL);
	assert(method_getImplementation(m2) == method_getImplementation(m));
	rb_vm_method_node_t *node2 = method_node_get(m2, true);
	memcpy(node2, node, sizeof(rb_vm_method_node_t));
    }
    return true;
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
	    if (!copy_method(to_class, m)) {
		continue;
	    }

#if !defined(MACRUBY_STATIC)
	    SEL sel = method_getName(m);
	    std::map<Class, rb_vm_method_source_t *> *map =
		method_sources_for_sel(sel, false);
	    if (map != NULL) {
		// There might be some non-JIT'ed yet methods on subclasses.
		resolve_methods(map, to_class, sel);
	    }
#endif
	}
	free(methods);
    }

#if !defined(MACRUBY_STATIC)
    // Copy methods that have not been JIT'ed yet.

    // First, make a list of selectors.
    std::vector<SEL> sels_to_copy;
    std::multimap<Class, SEL>::iterator iter =
	method_source_sels.find(from_class);

    if (iter != method_source_sels.end()) {
	std::multimap<Class, SEL>::iterator last =
	    method_source_sels.upper_bound(from_class);

	for (; iter != last; ++iter) {
	    sels_to_copy.push_back(iter->second);
	}
    }

    // Force a resolving of these selectors on the target class. This must be
    // done outside the next loop since the resolver messes up the Core
    // structures.
    for (std::vector<SEL>::iterator iter = sels_to_copy.begin();
	    iter != sels_to_copy.end();
	    ++iter) {
	class_getInstanceMethod(to_class, *iter);
    }

    // Now, let's really copy the lazy methods.
    std::vector<SEL> sels_to_add;
    for (std::vector<SEL>::iterator iter = sels_to_copy.begin();
	    iter != sels_to_copy.end();
	    ++iter) {
	SEL sel = *iter;

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

	rb_vm_method_source_t *m_src = iter2->second;

	Method m = class_getInstanceMethod(to_class, sel);
	if (m != NULL) {
	    // The method already exists on the target class, we need to
	    // JIT it.
	    IMP imp = GET_CORE()->compile(m_src->func);
	    resolve_method(to_class, sel, m_src->func, m_src->arity,
		    m_src->flags, imp, m, NULL);
	}
	else {
#if ROXOR_VM_DEBUG
	    printf("lazy copy %c[%s %s] to %c%s\n",
		    class_isMetaClass(from_class) ? '+' : '-',
		    class_getName(from_class),
		    sel_getName(sel),
		    class_isMetaClass(to_class) ? '+' : '-',
		    class_getName(to_class));
#endif

	    rb_vm_method_source_t *m = (rb_vm_method_source_t *)
		malloc(sizeof(rb_vm_method_source_t));
	    assert(m != NULL);
	    m->func = m_src->func;
	    m->arity = m_src->arity;
	    m->flags = m_src->flags;
	    dict->insert(std::make_pair(to_class, m));
	    sels_to_add.push_back(sel);
	}
    }

    for (std::vector<SEL>::iterator i = sels_to_add.begin();
	    i != sels_to_add.end();
	    ++i) {
	method_source_sels.insert(std::make_pair(to_class, *i));
    }
#endif
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
    if (UNAVAILABLE_IMP(imp)) {
	return false;
    }
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

#if MACRUBY_STATIC
    rb_raise(rb_eRuntimeError, "attr_* is not supported in MacRuby static");
#else
    char buf[100];
    snprintf(buf, sizeof buf, "@%s", name);
    ID iname = rb_intern(buf);

    if (read) {
	GET_CORE()->lock();
	Function *f = RoxorCompiler::shared->compile_read_attr(iname);
	GET_CORE()->unlock();
	SEL sel = sel_registerName(name);
	rb_vm_prepare_method(klass, false, sel, f, rb_vm_arity(0),
		VM_METHOD_FBODY);
    }

    if (write) {
	GET_CORE()->lock();
	Function *f = RoxorCompiler::shared->compile_write_attr(iname);
	GET_CORE()->unlock();
	snprintf(buf, sizeof buf, "%s=:", name);
	SEL sel = sel_registerName(buf);
	rb_vm_prepare_method(klass, false, sel, f, rb_vm_arity(1),
		VM_METHOD_FBODY);
    }
#endif
}

static rb_vm_method_node_t *
__rb_vm_define_method(Class klass, SEL sel, IMP objc_imp, IMP ruby_imp,
	const rb_vm_arity_t &arity, int flags, bool direct)
{
    assert(klass != NULL);

    if (rb_objc_ignored_sel(sel)) {
	return NULL;
    }

    if (OBJ_FROZEN(klass)) {
	rb_error_frozen("class/module");
    }

    const char *sel_name = sel_getName(sel);
    const bool genuine_selector = sel_name[strlen(sel_name) - 1] == ':';
    int types_count = genuine_selector ? arity.real + 3 : 3;
    bool redefined = direct;
    rb_vm_method_node_t *node;

define_method:
    Method method = class_getInstanceMethod(klass, sel);

    char types[100];
    resolve_method_type(types, sizeof types, klass, method, sel, types_count);

    GET_CORE()->lock();
    node = GET_CORE()->add_method(klass, sel, objc_imp, ruby_imp, arity,
	    flags, types);
    GET_CORE()->unlock();

    if (!redefined) {
	if (!genuine_selector && arity.max != arity.min) {
	    char buf[100];
	    snprintf(buf, sizeof buf, "%s:", sel_name);
	    sel = sel_registerName(buf);
	    types_count = arity.real + 3;
	    redefined = true;

	    goto define_method;
	}
	else if (genuine_selector && arity.min == 0) {
	    char buf[100];
	    strlcpy(buf, sel_name, sizeof buf);
	    buf[strlen(buf) - 1] = 0; // remove the ending ':'
	    sel = sel_registerName(buf);
	    types_count = 3;
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
	long flags, bool direct)
{
    assert(node != NULL);

    if (flags == -1) {
	flags = node->flags;
	flags &= ~VM_METHOD_PRIVATE;
	flags &= ~VM_METHOD_PROTECTED;
    }

    return __rb_vm_define_method(klass, sel, node->objc_imp, node->ruby_imp,
	    node->arity, flags, direct);
}

#if !defined(MACRUBY_STATIC)
extern "C"
void
rb_vm_define_method3(Class klass, ID mid, rb_vm_block_t *block)
{
    assert(block != NULL);

    const int arity = rb_vm_arity_n(block->arity);
    SEL sel = rb_vm_id_to_sel(mid, arity);

    GET_CORE()->lock();
    Function *func = RoxorCompiler::shared->compile_block_caller(block);
    IMP imp = GET_CORE()->compile(func);
    GET_CORE()->unlock();
    NODE *body = rb_vm_cfunc_node_from_imp(klass, arity < -1 ? -2 : arity, imp, 0);
    GC_RETAIN(body);
    GC_RETAIN(block);

    rb_vm_define_method(klass, sel, imp, body, false);
}

extern "C"
void *
rb_vm_generate_mri_stub(void *imp, const int arity)
{
    Function *func = RoxorCompiler::shared->compile_mri_stub(imp, arity);
    if (func == NULL) {
	// Not needed!
	return imp;
    }
    return (void *)GET_CORE()->compile(func, false); 
}
#endif

void
RoxorCore::undef_method(Class klass, SEL sel)
{
#if ROXOR_VM_DEBUG
    printf("undef %c[%s %s]\n",
	    class_isMetaClass(klass) ? '+' : '-',
	    class_getName(klass),
	    sel_getName(sel));
#endif

    class_replaceMethod((Class)klass, sel, (IMP)rb_vm_undefined_imp, "@@:");
    invalidate_respond_to_cache();

#if 0
    std::map<Method, rb_vm_method_node_t *>::iterator iter
	= ruby_methods.find(m);
    assert(iter != ruby_methods.end());
    free(iter->second);
    ruby_methods.erase(iter);
#endif

    if (get_running()) {
	ID mid = sanitize_mid(sel);
	if (mid != 0) {
	    VALUE sym = ID2SYM(mid);
	    if (RCLASS_SINGLETON(klass)) {
		VALUE sk = rb_singleton_class_attached_object((VALUE)klass);
		rb_vm_call(sk, selSingletonMethodUndefined, 1, &sym);
	    }
	    else {
		rb_vm_call((VALUE)klass, selMethodUndefined, 1, &sym);
	    }
	}
    }
}

extern "C"
void
rb_vm_undef_method(Class klass, ID name, bool check)
{
    if (NIL_P(klass)) {
	rb_raise(rb_eTypeError, "no class to undef method");
    }

    rb_frozen_class_p((VALUE)klass);

    const char *name_str = rb_id2name(name);
    SEL sel0 = rb_vm_name_to_sel(name_str, 0);
    SEL sel1 = rb_vm_name_to_sel(name_str, 1);

    rb_vm_method_node_t *node0 = NULL;
    rb_vm_method_node_t *node1 = NULL;
    bool exist0 = rb_vm_lookup_method((Class)klass, sel0, NULL, &node0);
    bool exist1 = rb_vm_lookup_method((Class)klass, sel1, NULL, &node1);

    if (!exist0 && !exist1 && check) {
	rb_raise(rb_eNameError, "undefined method `%s' for %s `%s'",
		name_str,
		TYPE(klass) == T_MODULE ? "module" : "class",
		rb_class2name((VALUE)klass));
    }

    if ((exist0 && node0 == NULL) || (exist1 && node1 == NULL)) {
	if (check) {
	    rb_raise(rb_eRuntimeError,
		    "cannot undefine method `%s' because it is a native method",
		    name_str);
	}
    }
    else { 
	GET_CORE()->undef_method(klass, sel0);
	if (sel0 != sel1) {
	    GET_CORE()->undef_method(klass, sel1);
	}
    }
}

void
RoxorCore::remove_method(Class klass, SEL sel)
{
#if ROXOR_VM_DEBUG
    printf("remove %c[%s %s]\n",
	    class_isMetaClass(klass) ? '+' : '-',
	    class_getName(klass),
	    sel_getName(sel));
#endif

    Method m = class_getInstanceMethod(klass, sel);
    assert(m != NULL);
    method_setImplementation(m, (IMP)rb_vm_removed_imp);
    invalidate_respond_to_cache();

    ID mid = sanitize_mid(sel);
    if (mid != 0) {
	VALUE sym = ID2SYM(mid);
	if (RCLASS_SINGLETON(klass)) {
	    VALUE sk = rb_singleton_class_attached_object((VALUE)klass);
	    rb_vm_call(sk, selSingletonMethodRemoved, 1, &sym);
	}
	else {
	    rb_vm_call((VALUE)klass, selMethodRemoved, 1, &sym);
	}
    }
}

extern "C"
void
rb_vm_remove_method(Class klass, ID name)
{
    rb_vm_method_node_t *node = NULL;

    if (!rb_vm_lookup_method2((Class)klass, name, NULL, NULL, &node)) {
	rb_raise(rb_eNameError, "undefined method `%s' for %s `%s'",
		rb_id2name(name),
		TYPE(klass) == T_MODULE ? "module" : "class",
		rb_class2name((VALUE)klass));
    }
    if (node == NULL) {
	rb_raise(rb_eRuntimeError,
		"cannot remove method `%s' because it is a native method",
		rb_id2name(name));
    }
    if (node->klass != klass) {
	rb_raise(rb_eNameError, "method `%s' not defined in %s",
		rb_id2name(name), rb_class2name((VALUE)klass));
    }

    GET_CORE()->remove_method(klass, node->sel);
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
    VALUE not_args[3] = {rb_str_new2(format), obj, meth};
    args[n++] = rb_vm_call(rb_cNameErrorMesg, selNot2, 3, not_args);
    args[n++] = meth;
    if (exc == rb_eNoMethodError) {
	args[n++] = rb_ary_new4(argc - 1, argv + 1);
    }

    exc = rb_class_new_instance(n, args, exc);
    rb_exc_raise(exc);

    abort(); // never reached
}

void *
RoxorCore::gen_large_arity_stub(int argc, bool is_block)
{
    RoxorCoreLock lock;

    std::map<int, void *> &stubs =
	is_block ? rb_large_arity_bstubs : rb_large_arity_rstubs;
    std::map<int, void *>::iterator iter = stubs.find(argc);
    void *stub;
    if (iter == stubs.end()) {
#if MACRUBY_STATIC
	printf("uncached large arity stub (%d)\n", argc);
	abort();
#else
	Function *f = RoxorCompiler::shared->compile_long_arity_stub(argc,
		is_block);
	stub = (void *)compile(f, false);
	stubs.insert(std::make_pair(argc, stub));
#endif
    }
    else {
	stub = iter->second;
    }

    return stub;
}

void *
RoxorCore::gen_stub(std::string types, bool variadic, int min_argc,
	bool is_objc)
{
    RoxorCoreLock lock;

#if ROXOR_VM_DEBUG
    printf("gen Ruby -> %s stub with types %s\n", is_objc ? "ObjC" : "C",
	    types.c_str());
#endif

    std::map<std::string, void *> &stubs = is_objc ? objc_stubs : c_stubs;
    std::map<std::string, void *>::iterator iter = stubs.find(types);
    void *stub;
    if (iter == stubs.end()) {
#if MACRUBY_STATIC
	printf("uncached %s stub `%s'\n", is_objc ? "ObjC" : "C",
		types.c_str());
	abort();
#else
	Function *f = RoxorCompiler::shared->compile_stub(types.c_str(),
		variadic, min_argc, is_objc);
	stub = (void *)compile(f);
	stubs.insert(std::make_pair(types, stub));
#endif
    }
    else {
	stub = iter->second;
    }

    return stub;
}

void *
RoxorCore::gen_to_rval_convertor(std::string type)
{
    RoxorCoreLock lock;

    std::map<std::string, void *>::iterator iter =
	to_rval_convertors.find(type);
    if (iter != to_rval_convertors.end()) {
	return iter->second;
    }

#if MACRUBY_STATIC
    printf("uncached to_rval convertor %s\n", type.c_str());
    abort();
#else
    Function *f = RoxorCompiler::shared->compile_to_rval_convertor(
	    type.c_str());
    void *convertor = (void *)compile(f);
    to_rval_convertors.insert(std::make_pair(type, convertor));
    
    return convertor; 
#endif
}

void *
RoxorCore::gen_to_ocval_convertor(std::string type)
{
    RoxorCoreLock lock;

    std::map<std::string, void *>::iterator iter =
	to_ocval_convertors.find(type);
    if (iter != to_ocval_convertors.end()) {
	return iter->second;
    }

#if MACRUBY_STATIC
    printf("uncached to_ocval convertor %s\n", type.c_str());
    abort();
#else
    Function *f = RoxorCompiler::shared->compile_to_ocval_convertor(
	    type.c_str());
    void *convertor = (void *)compile(f);
    to_ocval_convertors.insert(std::make_pair(type, convertor));
    
    return convertor; 
#endif
}

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
rb_vm_add_lvar_use(rb_vm_var_uses **var_uses, void *use,
	unsigned char use_type)
{
    if (var_uses == NULL) {
	return;
    }

    if ((*var_uses == NULL)
	|| ((*var_uses)->uses_count == VM_LVAR_USES_SIZE)) {

	rb_vm_var_uses *new_uses =
	    (rb_vm_var_uses *)xmalloc(sizeof(rb_vm_var_uses));
	new_uses->uses_count = 0;
	GC_WB(&new_uses->next, *var_uses);
	// var_uses should be on the stack so no need for GC_WB
	*var_uses = new_uses;
    }

    const int current_index = (*var_uses)->uses_count;
    GC_WB(&(*var_uses)->uses[current_index], use);
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

	rb_vm_add_lvar_use(block_for_uses->parent_var_uses, block,
		VM_LVAR_USE_TYPE_BLOCK);
    }
}

static void
rb_vm_add_binding_lvar_use(rb_vm_binding_t *binding, rb_vm_block_t *block,
	rb_vm_var_uses **parent_var_uses)
{
    for (rb_vm_block_t *block_for_uses = block;
	 block_for_uses != NULL;
	 block_for_uses = block_for_uses->parent_block) {

	rb_vm_add_lvar_use(block_for_uses->parent_var_uses, binding,
		VM_LVAR_USE_TYPE_BINDING);
    }
    rb_vm_add_lvar_use(parent_var_uses, binding, VM_LVAR_USE_TYPE_BINDING);
}

rb_vm_outer_t *
RoxorVM::push_outer(Class klass)
{
    rb_vm_outer_t *o = (rb_vm_outer_t *)xmalloc(sizeof(rb_vm_outer_t));
    o->klass = klass;
    GC_WB(&o->outer, outer_stack);
    o->pushed_by_eval = false;
    outer_stack = o;
    GC_RETAIN(outer_stack);

#if ROXOR_VM_DEBUG_CONST
    rb_vm_print_outer_stack(NULL, NULL, __FUNCTION__, __LINE__,
			    outer_stack, "push_outer");
#endif
    
    return o;
}

void
RoxorVM::pop_outer(bool need_release)
{
    assert(outer_stack != NULL);
    rb_vm_outer_t *old = outer_stack;
    outer_stack = outer_stack->outer;
    if (need_release) {
	GC_RELEASE(old);
    }

#if ROXOR_VM_DEBUG_CONST
    rb_vm_print_outer_stack(NULL, NULL, __FUNCTION__, __LINE__,
			    outer_stack, "pop_outer");
#endif
}

rb_vm_outer_t *
rb_vm_push_outer(Class klass)
{
    return GET_VM()->push_outer(klass);
}

void
rb_vm_pop_outer(unsigned char need_release)
{
    GET_VM()->pop_outer(need_release);
}

rb_vm_outer_t *
rb_vm_get_outer_stack(void)
{
    return GET_VM()->get_outer_stack();
}

rb_vm_outer_t *
rb_vm_set_current_outer(rb_vm_outer_t *outer)
{
    RoxorVM *vm = GET_VM();
    rb_vm_outer_t *old = vm->get_current_outer();
    vm->set_current_outer(outer);
    return old;
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

    if (current == NULL || current->uses_count == 0) {
	// there's no use alive so nothing to do
	return;
    }

    rb_vm_kept_local *locals = NULL;
    if (lvars_size > 0) {
	locals = (rb_vm_kept_local *)xmalloc(sizeof(rb_vm_kept_local)
		* lvars_size);

	va_list ar;
	va_start(ar, lvars_size);
	for (int i = 0; i < lvars_size; ++i) {
	    locals[i].name = va_arg(ar, ID);
	    locals[i].stack_address = va_arg(ar, VALUE *);
	    GC_WB(&locals[i].new_address, (VALUE *)xmalloc(sizeof(VALUE)));
	    GC_WB(locals[i].new_address, *locals[i].stack_address);
	}
	va_end(ar);
    }

    while (current != NULL) {
	for (int use_index = 0; use_index < current->uses_count; ++use_index) {
	    void *use = current->uses[use_index];
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
	    GC_WB(&current->uses[use_index], NULL);
	}
	current = current->next;
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
rb_vm_binding_t *
rb_vm_create_binding(VALUE self, rb_vm_block_t *current_block,
	rb_vm_binding_t *top_binding, rb_vm_outer_t *outer_stack, 
	int lvars_size, va_list lvars, bool vm_push)
{
    rb_vm_binding_t *binding =
	(rb_vm_binding_t *)xmalloc(sizeof(rb_vm_binding_t));
    GC_WB(&binding->self, self);
    GC_WB(&binding->next, top_binding);
    GC_WB(&binding->outer_stack, outer_stack);

    rb_vm_local_t **l = &binding->locals;

    for (rb_vm_block_t *b = current_block; b != NULL; b = b->parent_block) {
	for (rb_vm_local_t *li = b->locals; li != NULL; li = li->next) {
	    l = push_local(l, li->name, li->value);
	}
    }

    for (int i = 0; i < lvars_size; ++i) {
	ID name = va_arg(lvars, ID);
	VALUE *value = va_arg(lvars, VALUE *);
	l = push_local(l, name, value);
    }

    RoxorVM *vm = GET_VM();
    GC_WB(&binding->block, vm->current_block());
    if (vm_push) {
	vm->push_current_binding(binding);
    }

    return binding;
}

extern "C"
void
rb_vm_push_binding(VALUE self, rb_vm_block_t *current_block,
	rb_vm_binding_t *top_binding, unsigned char dynamic_class,
	rb_vm_outer_t *outer_stack, rb_vm_var_uses **parent_var_uses,
	int lvars_size, ...)
{
    if (dynamic_class) {
	outer_stack = GET_VM()->get_outer_stack();
    }
    va_list lvars;
    va_start(lvars, lvars_size);
    rb_vm_binding_t *binding = rb_vm_create_binding(self, current_block,
	    top_binding, outer_stack, lvars_size, lvars, true);
    va_end(lvars);

    rb_vm_add_binding_lvar_use(binding, current_block, parent_var_uses);
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
    GET_VM()->push_current_binding(binding);
}

extern "C"
void
rb_vm_pop_binding(void)
{
    GET_VM()->pop_current_binding();
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
	VALUE args)
{
    const VALUE *argv = RARRAY_PTR(args);
    const long argc = RARRAY_LEN(args);
    if (argc == 0) {
	rb_raise(rb_eArgError, "no receiver given");
    }
    SEL msel = argc - 1 == 0 ? (SEL)dvars[0] : (SEL)dvars[1];
    return rb_vm_call(argv[0], msel, argc - 1, &argv[1]);
}

extern "C"
rb_vm_block_t *
rb_vm_create_block_calling_mid(ID mid)
{
    rb_vm_block_t *b = (rb_vm_block_t *)xmalloc(sizeof(rb_vm_block_t)
	    + (2 * sizeof(VALUE *)));

    b->klass = 0;
    b->proc = Qnil;
    b->flags = VM_BLOCK_PROC;
    b->imp = (IMP)rb_vm_block_call_sel;

    // Arity is -1.
    b->arity.min = 0;
    b->arity.max = -1;
    b->arity.left_req = 0;
    b->arity.real = 1;

    // Prepare 2 selectors for the dispatcher later. One for 0 arity, one for
    // 1 or more arity.
    const char *midstr = rb_id2name(mid);
    if (midstr[strlen(midstr) - 1] == ':') {
	rb_raise(rb_eArgError, "invalid method name `%s'", midstr);
    }
    char buf[100];
    snprintf(buf, sizeof buf, "%s:", midstr);
    *(b->dvars + 0) = (VALUE *)sel_registerName(midstr);
    *(b->dvars + 1) = (VALUE *)sel_registerName(buf);

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
    GC_WB((b->dvars + 0), (VALUE *)proc);
    GC_WB((b->dvars + 1), (VALUE *)passed);
    *(b->dvars + 2) = (VALUE *)arity;

    return rb_proc_alloc_with_block(rb_cProc, b);
}

static VALUE
rb_vm_iterate_block(VALUE rcv, SEL sel, VALUE **dvars, rb_vm_block_t *b,
	VALUE args)
{
    VALUE (*bl_proc) (ANYARGS) = (VALUE (*) (ANYARGS))dvars[0];
    VALUE data2 = (VALUE)dvars[1];

    return (*bl_proc)(args, data2, rcv);
}

extern "C"
VALUE
rb_iterate(VALUE (*it_proc) (VALUE), VALUE data1, VALUE (*bl_proc) (ANYARGS),
	VALUE data2)
{
    // Proc.new { |*args| curry... }
    rb_vm_block_t *b = (rb_vm_block_t *)xmalloc(sizeof(rb_vm_block_t)
	    + (2 * sizeof(VALUE *)));

    b->klass = 0;
    b->proc = Qnil;
    b->arity.min = 0;
    b->arity.max = -1;
    b->arity.left_req = 0;
    b->arity.real = 1;
    b->flags = VM_BLOCK_PROC;
    b->imp = (IMP)rb_vm_iterate_block;
    b->dvars[0] =(VALUE *)bl_proc;
    GC_WB(&b->dvars[1], (VALUE *)data2);

    RoxorVM *vm = GET_VM();
    vm->add_current_block(b);

    struct Finally {
	RoxorVM *vm;
	Finally(RoxorVM *_vm) {
	    vm = _vm;
	}
	~Finally() {
	    vm->pop_current_block();
	}
    } finalizer(vm);

    return (*it_proc)(data1);
}

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

static inline void
__vm_raise(void)
{
    VALUE rb_exc = GET_VM()->current_exception();
#if ROXOR_VM_DEBUG
	printf("%s (%s:%d): rb_exc = \"%s\"\n",
			__FUNCTION__, __FILE__, __LINE__, RSTRING_PTR(rb_inspect(rb_exc)));
#endif

    // DTrace probe: raise
    if (MACRUBY_RAISE_ENABLED()) {
	char *classname = (char *)rb_class2name(CLASS_OF(rb_exc));
	char file[PATH_MAX];
	unsigned long line = 0;
	GET_CORE()->symbolize_backtrace_entry(2, file, sizeof file, &line,
		NULL, 0);
	MACRUBY_RAISE(classname, file, line);
    } 
#if __LP64__
    // In 64-bit, an Objective-C exception is a C++ exception.
    id exc = rb_rb2oc_exception(rb_exc);
    objc_exception_throw(exc);
#else
    void *exc = __cxa_allocate_exception(0);
    __cxa_throw(exc, NULL, NULL);
#endif
}

void
RoxorVM::push_current_exception(VALUE exc)
{
    assert(!NIL_P(exc));
    GC_RETAIN(exc);
    current_exceptions.push_back(exc);
#if ROXOR_VM_DEBUG
	printf("%s (%s:%d): exc = \"%s\"\n",
			__FUNCTION__, __FILE__, __LINE__, RSTRING_PTR(rb_inspect(exc)));
#endif
//printf("PUSH %p %s\n", (void *)exc, RSTRING_PTR(rb_inspect(exc)));
}

void
RoxorVM::pop_current_exception(int pos)
{
    RoxorSpecialException *sexc = get_special_exc();
    if (sexc != NULL) {
	return;
    }

#if ROXOR_VM_DEBUG
    if (!((size_t)pos < current_exceptions.size()))
    {
        printf("RoxorVM::%s (%s:%d) - "
               "Warning: Assertion about to fail: "
               "((size_t)pos < current_exceptions.size()); pos = %d; "
               "current_exceptions.size() = %d\n",
               __FUNCTION__, __FILE__, __LINE__, pos, (int)current_exceptions.size());
		debug_exceptions();
    }
#endif
    assert((size_t)pos < current_exceptions.size());

    std::vector<VALUE>::iterator iter = current_exceptions.end() - (pos + 1);
    VALUE exc = *iter;
    current_exceptions.erase(iter);

    GC_RELEASE(exc);
//printf("POP (%d) %p %s\n", pos, (void *)exc, RSTRING_PTR(rb_inspect(exc)));
}

#if !__LP64__
extern "C"
void
rb_rb2oc_exc_handler(void)
{
    VALUE exc = GET_VM()->current_exception();
#if ROXOR_VM_DEBUG
	printf("%s (%s:%d): exc = \"%s\"\n",
			__FUNCTION__, __FILE__, __LINE__, RSTRING_PTR(rb_inspect(exc)));
#endif
    if (exc != Qnil) {
	id ocexc = rb_rb2oc_exception(exc);
	objc_exception_throw(ocexc);
    }
    else {
	__cxa_rethrow();
    }
}
#endif

static void
prepare_exception_bt(VALUE exc)
{
    // An exception's backtrace is prepared on demand, and only once.
    ID bt_id = rb_intern("bt");
    VALUE bt = rb_attr_get(exc, bt_id);
    if (bt == Qnil) {
	rb_ivar_set(exc, bt_id, rb_vm_backtrace(0));
    }
}

extern "C"
void
rb_vm_raise_current_exception(void)
{
    VALUE exception = GET_VM()->current_exception();
#if ROXOR_VM_DEBUG
	printf("%s (%s:%d): exception = \"%s\"\n",
			__FUNCTION__, __FILE__, __LINE__, RSTRING_PTR(rb_inspect(exception)));
#endif
    assert(exception != Qnil);
    prepare_exception_bt(exception);
    __vm_raise(); 
}

extern "C"
void
rb_vm_raise(VALUE exception)
{
    prepare_exception_bt(exception);
#if ROXOR_VM_DEBUG
	printf("%s (%s:%d): exception = \"%s\"\n",
			__FUNCTION__, __FILE__, __LINE__, RSTRING_PTR(rb_inspect(exception)));
#endif
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
	RoxorVM *vm = GET_VM();
	VALUE exc = vm->current_exception();
#if ROXOR_VM_DEBUG
	printf("%s (%s:%d): exc = \"%s\"\n",
			__FUNCTION__, __FILE__, __LINE__, RSTRING_PTR(rb_inspect(exc)));
#endif
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
#if ROXOR_VM_DEBUG
		printf("%s (%s:%d): Calling pop_current_exception...\n",
				__FUNCTION__, __FILE__, __LINE__);
#endif
		vm->pop_current_exception();
		if (r_proc != NULL) {
		    return (*r_proc)(data2, exc);
		}
		return Qnil;
	    }
	}
	throw;
    }
    return Qnil; // never reached
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
rb_ensure_b(void (^b_block)(void), void (^e_block)(void))
{
    struct Finally {
	void (^e_block)(void);
	Finally(void (^_e_block)(void)) {
	    e_block = _e_block;
	}
	~Finally() { e_block(); }
    } finalizer(e_block);

    b_block();
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
    RoxorVM *vm = GET_VM();
    if (vm->get_broken_with() != val) {
	GC_RELEASE(vm->get_broken_with());
	vm->set_broken_with(val);
	GC_RETAIN(val);
    }
}

extern "C"
VALUE
rb_vm_get_broken_value(void *vm)
{
    return ((RoxorVM *)vm)->get_broken_with();
}

extern "C"
VALUE
rb_vm_pop_broken_value(void)
{
    return GET_VM()->pop_broken_with();
}

extern "C"
unsigned char
rb_vm_set_has_ensure(unsigned char state)
{
    RoxorVM *vm = GET_VM();
    const bool old_state = vm->get_has_ensure();
    vm->set_has_ensure(state);
    return old_state ? 1 : 0;
}

extern "C"
void
rb_vm_return_from_block(VALUE val, int id, rb_vm_block_t *running_block)
{
    RoxorVM *vm = GET_VM();

    // Do not trigger a return from the calling scope if the running block
    // is a lambda, to conform to the ruby 1.9 specifications.
    if (running_block->flags & VM_BLOCK_LAMBDA) {
	return;
    }

    // If we are inside an ensure block or if the running block is a Proc,
    // let's implement return-from-block using a C++ exception (slow).
    if (vm->get_has_ensure() || (running_block->flags & VM_BLOCK_PROC)) {
	RoxorReturnFromBlockException *exc =
	    new RoxorReturnFromBlockException();
	vm->set_special_exc(exc);
	exc->val = val;
	exc->id = id;
	throw exc;
    }

    // Otherwise, let's mark the VM (fast).
    vm->set_return_from_block(id);
    if (vm->get_broken_with() != val) {
	GC_RELEASE(vm->get_broken_with());
	vm->set_broken_with(val);
	GC_RETAIN(val);
    }
}

extern "C"
VALUE
rb_vm_returned_from_block(void *_vm, int id)
{
    RoxorVM *vm = (RoxorVM *)_vm;
    if (id != -1 && vm->get_return_from_block() == id) {
	vm->set_return_from_block(-1);
    }
    return vm->pop_broken_with();
}

extern "C"
VALUE
rb_vm_check_return_from_block_exc(RoxorReturnFromBlockException **pexc, int id)
{
    RoxorVM *vm = GET_VM();
    RoxorSpecialException *sexc = vm->get_special_exc();
    if (sexc != NULL && sexc->type == RETURN_FROM_BLOCK_EXCEPTION) {
	RoxorReturnFromBlockException *exc = *pexc;
	if (id == -1 || exc->id == id) {
	    VALUE val = exc->val;
	    delete exc;
	    vm->set_special_exc(NULL);
	    return val;
	}
    }
    return Qundef;
}

extern "C"
VALUE
rb_vm_backtrace(int skip)
{
    assert(skip >= 0);

    void *callstack[128];
    int callstack_n = backtrace(callstack, 128);

    // TODO should honor level

    VALUE ary = rb_ary_new();

    unsigned int interpreter_frame_idx = 0;

    for (int i = 0; i < callstack_n; i++) {
	char path[PATH_MAX];
	char name[100];
	unsigned long ln = 0;

	path[0] = name[0] = '\0';

	if (GET_CORE()->symbolize_call_address(callstack[i], path, sizeof path,
		    &ln, name, sizeof name, &interpreter_frame_idx)
		&& name[0] != '\0' && path[0] != '\0') {

	    // Sanitize the method name to not contain trailing ':' like CRuby.
	    char *p = &name[strlen(name) - 1];
	    if (strchr(name, ':') == p) {
		*p = '\0';
	    }

	    char entry[PATH_MAX];
	    if (ln == 0) {
		snprintf(entry, sizeof entry, "%s:in `%s'",
			path, name);
	    }
	    else {
		snprintf(entry, sizeof entry, "%s:%ld:in `%s'",
			path, ln, name);
	    }
	    rb_ary_push(ary, rb_str_new2(entry));
	}
    }

    while (skip-- > 0) {
	rb_ary_shift(ary);
    }

    return ary;
}

extern "C"
unsigned char
rb_vm_is_eh_active(int argc, ...)
{
    assert(argc > 0);

    VALUE current_exception = GET_VM()->current_exception();
#if ROXOR_VM_DEBUG
	printf("%s (%s:%d): current_exception = \"%s\"\n",
			__FUNCTION__, __FILE__, __LINE__, RSTRING_PTR(rb_inspect(current_exception)));
#endif
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
		    break;
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
rb_vm_pop_exception(int pos)
{
#if ROXOR_VM_DEBUG
	printf("%s (%s:%d): Calling pop_current_exception(%d)...\n",
			__FUNCTION__, __FILE__, __LINE__, pos);
#endif
    GET_VM()->pop_current_exception(pos);
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

#if ROXOR_VM_DEBUG
	printf("%s (%s:%d): exception = \"%s\"\n",
			__FUNCTION__, __FILE__, __LINE__, RSTRING_PTR(rb_inspect(exception)));
#endif
    VALUE current = GET_VM()->current_exception();
#if ROXOR_VM_DEBUG
	printf("%s: (%s:%d) current = \"%s\"\n",
			__FUNCTION__, __FILE__, __LINE__, RSTRING_PTR(rb_inspect(current)));
#endif
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
rb_vm_print_exception(VALUE exc)
{
    printf("%s", rb_str_cstr(rb_format_exception_message(exc)));
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
    rb_vm_print_exception(exc);
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
    rb_vm_binding_t *current_b = current_binding();
    rb_vm_binding_t *b = current_b;
    while (b != NULL) {
	rb_vm_local_t **l = &b->locals;
	while (*l != NULL) {
	    if ((*l)->name == name) {
		return (*l)->value;
	    }
	    l = &(*l)->next;
	}
	b = b->next;
    }
    if (create && current_b != NULL) {
	rb_vm_binding_t *b = current_b;
	rb_vm_local_t **l = &b->locals;
	while (*l != NULL) {
	    l = &(*l)->next;
	}
	GC_WB(l, xmalloc(sizeof(rb_vm_local_t)));
	(*l)->name = name;
	GC_WB(&(*l)->value, xmalloc(sizeof(VALUE *)));
	(*l)->next = NULL;
	return (*l)->value;
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
rb_vm_init_jit(void)
{
    GET_CORE()->prepare_jit();
}

extern "C"
void
rb_vm_init_compiler(void)
{
#if !defined(MACRUBY_STATIC)
    if (ruby_aot_compile && ruby_debug_socket_path) {
	fprintf(stderr, "cannot run in both AOT and debug mode\n");
	exit(1);
    }
    RoxorCompiler::shared = ruby_aot_compile
	? new RoxorAOTCompiler()
	: new RoxorCompiler(ruby_debug_socket_path);
#endif
}

extern "C" void rb_node_release(NODE *node);

extern "C"
VALUE
rb_vm_run(const char *fname, NODE *node, rb_vm_binding_t *binding,
	bool inside_eval)
{
#if MACRUBY_STATIC
    rb_raise(rb_eRuntimeError, "codegen is not supported in MacRuby static");
#else
    RoxorVM *vm = GET_VM();
    RoxorCompiler *compiler = RoxorCompiler::shared;
    RoxorCoreLock lock;

    // Compile IR.
    if (binding != NULL) {
	vm->push_current_binding(binding);
    }
    bool old_inside_eval = compiler->is_inside_eval();
    compiler->set_inside_eval(inside_eval);
    compiler->set_fname(fname);
    bool can_interpret = false;
    Function *func = compiler->compile_main_function(node, &can_interpret);
    //compiler->set_fname(NULL);
    compiler->set_inside_eval(old_inside_eval);
    if (binding != NULL) {
	vm->pop_current_binding();
    }

    VALUE ret;

    if (can_interpret && GET_CORE()->get_interpreter_enabled()) {
//printf("interpret:\n");
//func->dump();
	// If the function can be interpreted, do it, then delete the IR.
	lock.unlock();
	ret = RoxorInterpreter::shared->interpret(func,
		vm->get_current_top_object(), 0);
 	func->eraseFromParent();
    }
    else {
//printf("jit:\n");
//func->dump();
	// Optimize & compile the function.
	IMP imp = GET_CORE()->compile(func);

	// Register it for symbolication.
	rb_vm_method_node_t *mnode = GET_CORE()->method_node_get(imp, true);
	mnode->klass = 0;
	mnode->arity = rb_vm_arity(2);
	mnode->sel = sel_registerName("<main>");
	mnode->objc_imp = mnode->ruby_imp = imp;
	mnode->flags = 0;

	// Execute the function.
	lock.unlock();
	ret = ((VALUE(*)(VALUE, SEL))imp)(vm->get_current_top_object(), 0);

#if 0
	// FIXME deleting functions causes crashes in the C++ EH personality
	// function. To investigate!
	if (inside_eval) {
	    // XXX We only delete functions created by #eval. In theory it
	    // should also work for other functions, but it makes spec:ci crash.
	    GET_CORE()->delenda(func);
	}
#endif
    }

    rb_node_release(node);

    return ret;
#endif
}

extern "C"
VALUE
rb_vm_run_under(VALUE klass, VALUE self, const char *fname, NODE *node,
	rb_vm_binding_t *binding, bool inside_eval, bool should_push_outer)
{
#if MACRUBY_STATIC
    rb_raise(rb_eRuntimeError, "codegen is not supported in MacRuby static");
#else
    RoxorVM *vm = GET_VM();

    VALUE old_top_object = vm->get_current_top_object();
    if (binding != NULL) {
	self = binding->self;
	rb_vm_outer_t *o = binding->outer_stack;
	if (o == NULL) {
	    klass = rb_cNSObject;
	}
	else {
	    klass = (VALUE)o->klass;
	}
    }
    if (NIL_P(klass)) {
	klass = 0;
    }
    if (self != 0) {
	vm->set_current_top_object(self);
    }
    Class old_class = GET_VM()->get_current_class();
    bool old_dynamic_class = RoxorCompiler::shared->is_dynamic_class();

    vm->set_current_class((Class)klass);

    rb_vm_outer_t *old_outer_stack = NULL;
    bool should_pop_outer = false;
    bool old_outer_stack_uses = false;
    if (binding == NULL) {
	if (vm->get_outer_stack() != vm->get_current_outer()) {
	    old_outer_stack = vm->get_outer_stack();
	    vm->set_outer_stack(vm->get_current_outer());
	}
	if (should_push_outer) {
	    vm->push_outer((Class)klass);
	    should_pop_outer = true;
	    old_outer_stack_uses =
		RoxorCompiler::shared->get_outer_stack_uses();
	    RoxorCompiler::shared->set_outer_stack_uses(false);
	}
    }
    else {
	if (vm->get_outer_stack() != binding->outer_stack) {
	    old_outer_stack = vm->get_outer_stack();
	    vm->set_outer_stack(binding->outer_stack);
	}
    }

    RoxorCompiler::shared->set_dynamic_class(true);

    vm->add_current_block(binding != NULL ? binding->block : NULL);

    struct Finally {
	RoxorVM *vm;
	bool old_dynamic_class;
	Class old_class;
	VALUE old_top_object;
	rb_vm_outer_t *old_outer_stack;
	bool should_pop_outer;
	bool outer_stack_uses;
	Finally(RoxorVM *_vm, bool _dynamic_class, Class _class, VALUE _obj, rb_vm_outer_t *_outer_stack, bool _should_pop_outer, bool _outer_stack_uses) {
	    vm = _vm;
	    old_dynamic_class = _dynamic_class;
	    old_class = _class;
	    old_top_object = _obj;
	    old_outer_stack = _outer_stack;
	    should_pop_outer = _should_pop_outer;
	    outer_stack_uses = _outer_stack_uses;
	}
	~Finally() { 
	    RoxorCompiler::shared->set_dynamic_class(old_dynamic_class);
	    vm->set_current_top_object(old_top_object);
	    if (should_pop_outer) {
		vm->pop_outer(!RoxorCompiler::shared->get_outer_stack_uses());
		RoxorCompiler::shared->set_outer_stack_uses(outer_stack_uses);
	    }
	    if (old_outer_stack != NULL) {
		vm->set_outer_stack(old_outer_stack);
	    }
	    vm->set_current_class(old_class);
	    vm->pop_current_block();
	}
    } finalizer(vm, old_dynamic_class, old_class, old_top_object, old_outer_stack, should_pop_outer, old_outer_stack_uses);

    return rb_vm_run(fname, node, binding, inside_eval);
#endif
}

extern "C"
VALUE
rb_vm_eval_string(VALUE self, VALUE klass, VALUE src, rb_vm_binding_t *binding,
	const char *file, const int line, bool should_push_outer)
{
#if MACRUBY_STATIC
    rb_raise(rb_eRuntimeError,
	    "evaluating strings is not supported in MacRuby static");
#else
    RoxorVM *vm = GET_VM();
    bool old_parse_in_eval = vm->get_parse_in_eval();
    vm->set_parse_in_eval(true);
    if (binding != NULL) {
	// Binding must be added because the parser needs it.
        vm->push_current_binding(binding);
    }
    VALUE old_errinfo = vm->get_errinfo();
    vm->set_errinfo(Qnil);

    NODE *node = rb_compile_string(file, src, line);

    VALUE errinfo = vm->get_errinfo();
    vm->set_errinfo(old_errinfo);
    if (binding != NULL) {
	// We remove the binding now but we still pass it to the VM, which
	// will use it for compilation.
        vm->pop_current_binding();
    }
    vm->set_parse_in_eval(old_parse_in_eval);

    if (node == NULL) {
	if (errinfo != Qnil) {
            rb_vm_raise(errinfo);
	}
	else {
	    rb_raise(rb_eSyntaxError, "compile error");
	}
    }

    return rb_vm_run_under(klass, self, file, node, binding, true,
	    should_push_outer);
#endif
}

extern VALUE rb_progname;
extern "C" void rb_vm_aot_load_bs_files(VALUE);

extern "C"
void
rb_vm_aot_compile(NODE *node)
{
#if MACRUBY_STATIC
    abort();
#else
    assert(ruby_aot_compile);
    assert(ruby_aot_init_func);

    // Load the BridgeSupport files.
    if (ruby_aot_bs_files != Qnil) {
	for (int i = 0, count = RARRAY_LEN(ruby_aot_bs_files); i < count;
		i++) {
	    ((RoxorAOTCompiler *)RoxorCompiler::shared)->load_bs_full_file(
		RSTRING_PTR(RARRAY_AT(ruby_aot_bs_files, i)));
	}
    }

    // Mark all VM primitives as static, to avoid symbol collision when
    // linking with other AOT compiled files.
    llvm::Module::FunctionListType &funcs =
	RoxorCompiler::module->getFunctionList();
    for (llvm::Module::FunctionListType::iterator i = funcs.begin();
	    i != funcs.end(); ++i) {
	if (i->getName().startswith("vm_")) {
	    i->setLinkage(GlobalValue::InternalLinkage);
	}
    }

    // Compile the program as IR.
    RoxorCompiler::shared->set_fname(RSTRING_PTR(rb_progname));
    Function *f = RoxorCompiler::shared->compile_main_function(node, NULL);
    f->setName(RSTRING_PTR(ruby_aot_init_func));

    // Force a module verification.
    rb_verify_module();

    // Run standard optimization passes on the module.
    PassManager pm;
    createStandardModulePasses(&pm, 3, false, true, true, true, true,
	    createFunctionInliningPass());
    pm.run(*RoxorCompiler::module);

    // Dump the bitcode.
    std::string err;
    const char *output = RSTRING_PTR(ruby_aot_compile);
    raw_fd_ostream out(output, err, raw_fd_ostream::F_Binary);
    if (!err.empty()) {
	fprintf(stderr, "error when opening the output bitcode file: %s\n",
		err.c_str());
	abort();
    }
    WriteBitcodeToFile(RoxorCompiler::module, out);
    out.close();
#endif
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
	GC_RELEASE(last_status);
    }

    if (pid == -1) {
	last_status = Qnil;
    }
    else {
	last_status = rb_obj_alloc(rb_cProcessStatus);
	rb_iv_set(last_status, "status", INT2FIX(status));
	rb_iv_set(last_status, "pid", PIDT2NUM(pid));
	GC_RETAIN(last_status);
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
	GC_RELEASE(errinfo);
    }
    GET_VM()->set_errinfo(err);
    GC_RETAIN(err);
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
    return GET_VM()->get_last_line();
}

extern "C"
void
rb_lastline_set(VALUE val)
{
    VALUE old = GET_VM()->get_last_line();
    if (old != val) {
	GC_RELEASE(old);
	GET_VM()->set_last_line(val);
	GC_RETAIN(val);
    }
}

extern "C"
void
rb_iter_break(void)
{
    RoxorVM *vm = GET_VM();
    GC_RELEASE(vm->get_broken_with());
    vm->set_broken_with(Qnil);
}

extern "C"
VALUE
rb_backref_get(void)
{
    return GET_VM()->get_backref();
}

extern "C"
VALUE
rb_backref_nth_get(int nth)
{
    VALUE backref = rb_backref_get();
    if (backref == Qnil) {
	return Qnil;
    }
    return rb_reg_nth_match(nth, backref);
}

extern "C"
VALUE
rb_backref_special_get(int code)
{
    VALUE backref = rb_backref_get();
    if (backref == Qnil) {
	return Qnil;
    }
    switch (code) {
	case '&':
	    return rb_reg_last_match(backref);
	case '`':
	    return rb_reg_match_pre(backref);
	case '\'':
	    return rb_reg_match_post(backref);
	case '+':
	    return rb_reg_match_last(backref);
    }
    // This can't happen.
    printf("invalid backref special code: %d (%c)\n", code, code);
    abort(); 
}

extern "C"
void
rb_backref_set(VALUE val)
{
    VALUE old = GET_VM()->get_backref();
    if (old != val) {
	GC_RELEASE(old);
	GET_VM()->set_backref(val);
	GC_RETAIN(val);
    }
}

void
RoxorVM::increase_nesting_for_tag(VALUE tag)
{
    std::map<VALUE, rb_vm_catch_t *>::iterator iter = this->catch_nesting.find(tag);
    VALUE exc = current_exception();
#if ROXOR_VM_DEBUG
	printf("%s: (%s:%d) exc = \"%s\"\n",
			__FUNCTION__, __FILE__, __LINE__, RSTRING_PTR(rb_inspect(exc)));
#endif
    if (iter == catch_nesting.end()) {
	rb_vm_catch_t *catch_ptr = new rb_vm_catch_t;
	catch_ptr->nested = 1;
	catch_ptr->current_exceptions.push_back(exc);
	catch_nesting[tag] = catch_ptr;
	GC_RETAIN(tag);
    }
    else {
	rb_vm_catch_t *catch_ptr = iter->second;
	catch_ptr->nested++;
	catch_ptr->current_exceptions.push_back(exc);
    }
}

void
RoxorVM::decrease_nesting_for_tag(VALUE tag)
{
    std::map<VALUE, rb_vm_catch_t *>::iterator iter = this->catch_nesting.find(tag);
    assert(iter != catch_nesting.end());
    rb_vm_catch_t *catch_ptr = iter->second;
    assert(catch_ptr->nested > 0);
    catch_ptr->nested--;
    catch_ptr->current_exceptions.pop_back();
    if (catch_ptr->nested == 0) {
	delete catch_ptr;
	catch_nesting.erase(iter);
	GC_RELEASE(tag);
    }
}

VALUE
RoxorVM::ruby_catch(VALUE tag)
{
    VALUE retval = Qundef;

    increase_nesting_for_tag(tag);
    try {
	retval = rb_vm_yield(1, &tag);
    }
    catch (...) {
	RoxorSpecialException *sexc = get_special_exc();
	if (sexc != NULL && sexc->type == CATCH_THROW_EXCEPTION) {
	    RoxorCatchThrowException *exc = (RoxorCatchThrowException *)sexc;
	    if (exc->throw_symbol == tag) {
		retval = exc->throw_value;
		GC_RELEASE(retval);
		delete exc;
		set_special_exc(NULL);
	    }
	}
	if (retval == Qundef) {
	    decrease_nesting_for_tag(tag);
	    throw;
	}
    }
    decrease_nesting_for_tag(tag); 

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
    std::map<VALUE, rb_vm_catch_t *>::iterator iter = catch_nesting.find(tag);
    if (iter == catch_nesting.end()) {
        VALUE desc = rb_inspect(tag);
        rb_raise(rb_eArgError, "uncaught throw %s", RSTRING_PTR(desc));
    }

    GC_RETAIN(value);

    // We must pop the current VM exception in case we are in a rescue handler,
    // since we are going to unwind the stack.
    rb_vm_catch_t *catch_ptr = iter->second;
    while (catch_ptr->current_exceptions.back() != current_exception()) {
#if ROXOR_VM_DEBUG
		printf("RoxorVM::%s (%s:%d): Calling pop_current_exception...\n",
				__FUNCTION__, __FILE__, __LINE__);
#endif
	pop_current_exception();
    }

    RoxorCatchThrowException *exc = new RoxorCatchThrowException;
    set_special_exc(exc);
    exc->throw_symbol = tag;
    exc->throw_value = value;
    throw exc;

    return Qnil; // Never reached;
}

extern "C"
VALUE
rb_vm_throw(VALUE tag, VALUE value)
{
    return GET_VM()->ruby_throw(tag, value);
}

extern "C"
VALUE
rb_exec_recursive(VALUE (*func) (VALUE, VALUE, int), VALUE obj, VALUE arg)
{
    return GET_VM()->exec_recursive(func, obj, arg, 0);
}

extern "C"
VALUE
rb_exec_recursive_outer(VALUE (*func) (VALUE, VALUE, int), VALUE obj, VALUE arg)
{
    return GET_VM()->exec_recursive(func, obj, arg, 1);
}

void
RoxorVM::remove_recursive_object(VALUE obj)
{
    std::vector<VALUE>::iterator iter =
	std::find(recursive_objects.begin(), recursive_objects.end(), obj);
    assert(iter != recursive_objects.end());
    recursive_objects.erase(iter);
}

VALUE
RoxorVM::exec_recursive(VALUE (*func) (VALUE, VALUE, int), VALUE obj,
	VALUE arg, int outer)
{
    std::vector<VALUE>::iterator iter =
	std::find(recursive_objects.begin(), recursive_objects.end(), obj);
    try {
	VALUE ret = Qnil;
	if (iter != recursive_objects.end()) {
	    // Object is already being iterated!
	    ret = (*func) (obj, arg, Qtrue);
	    if (outer) {
		// throw the result value of outer loop
		throw ret;
	    }
	    return ret;
	}

	recursive_objects.push_back(obj);
	try {
	    ret = (*func) (obj, arg, Qfalse);
	}
	catch (VALUE ret) {
	    // catch and rethrow the value of outer loop
	    throw;
	}
	catch (...) {
	    remove_recursive_object(obj);
	    throw;
	}
	remove_recursive_object(obj);
	return ret;
    }
    catch (VALUE ret) {
	// catch the value of outer loop
	if (!recursive_objects.empty()) {
	    recursive_objects.pop_back();
	    throw;
	}
	return ret;
    }

    return Qnil; /* not reached */
}

extern "C"
void
rb_vm_register_finalizer(rb_vm_finalizer_t *finalizer)
{
    GET_CORE()->register_finalizer(finalizer);
}

extern "C"
void
rb_vm_unregister_finalizer(rb_vm_finalizer_t *finalizer)
{
    GET_CORE()->unregister_finalizer(finalizer);
}

void
RoxorCore::register_finalizer(rb_vm_finalizer_t *finalizer)
{
    RoxorCoreLock lock;

    finalizers.push_back(finalizer);
}

void
RoxorCore::unregister_finalizer(rb_vm_finalizer_t *finalizer)
{
    RoxorCoreLock lock;

    std::vector<rb_vm_finalizer_t *>::iterator i = std::find(finalizers.begin(),
	    finalizers.end(), finalizer);
    if (i != finalizers.end()) {
	finalizers.erase(i);
    }
}

static void
call_finalizer(rb_vm_finalizer_t *finalizer)
{
    for (int i = 0, count = RARRAY_LEN(finalizer->finalizers); i < count; i++) {
	VALUE b = RARRAY_AT(finalizer->finalizers, i);
	try {
	    rb_vm_call(b, selCall, 1, &finalizer->objid);
	}
	catch (...) {
	    // Do nothing.
	}
    }
    rb_ary_clear(finalizer->finalizers);
}

extern "C"
void
rb_vm_call_finalizer(rb_vm_finalizer_t *finalizer)
{
    call_finalizer(finalizer);
}

void
RoxorCore::call_all_finalizers(void)
{
    for (std::vector<rb_vm_finalizer_t *>::iterator i = finalizers.begin();
	    i != finalizers.end();
	    ++i) {
	call_finalizer(*i);
    }
    finalizers.clear();
}

extern "C"
void *
rb_vm_create_vm(void)
{
    return (void *)new RoxorVM(*GET_VM());
}

extern "C"
void *
rb_vm_current_vm(void)
{
    return (void *)GET_VM();
}

extern "C"
struct mcache *
rb_vm_get_mcache(void *vm)
{
    return ((RoxorVM *)vm)->get_mcache();
} 

void
RoxorCore::register_thread(VALUE thread)
{
    RoxorCoreLock lock;
    rb_ary_push(threads, thread);
}

static void
rb_vm_post_register_thread(VALUE thread)
{
    rb_vm_thread_t *t = GetThreadPtr(thread);
    pthread_assert(pthread_setspecific(RoxorVM::vm_thread_key, t->vm));

    RoxorVM *vm = (RoxorVM *)t->vm;
    vm->set_thread(thread);
}

extern "C"
void
rb_vm_register_thread(VALUE thread)
{
    GET_CORE()->register_thread(thread);
}

extern "C" void rb_thread_unlock_all_mutexes(rb_vm_thread_t *thread);

void
RoxorCore::unregister_thread(VALUE thread)
{
    RoxorCoreLock lock;

    rb_vm_thread_t *t = GetThreadPtr(thread);
    t->status = THREAD_DEAD;

    // We do not call #delete because it might trigger #== in case it has been
    // overriden on the thread object, and therefore cause a deadlock if the
    // new method tries to acquire the RoxorCore GIL.
    bool deleted = false;
    for (long i = 0, n = RARRAY_LEN(threads); i < n; i++) {
	VALUE ti = RARRAY_AT(threads, i);
	if (ti == thread) {
	    rb_ary_delete_at(threads, i);
	    deleted = true;
	    break;
	}
    }
    if (!deleted) {
	printf("trying to unregister a thread (%p) that was never registered!",
		(void *)thread);
	abort();
    }

    lock.unlock();

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
}

static inline void
rb_vm_thread_throw_kill(void)
{
    // Killing a thread is implemented using a non-catchable (from Ruby)
    // exception, which allows us to call the ensure blocks before dying,
    // which is unfortunately covered in the Ruby specifications.
    throw new RoxorThreadRaiseException();
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
    // The thread object should have been registered to the core already,
    // via rb_vm_register_thread().
    rb_objc_gc_register_thread();
    rb_vm_post_register_thread(thread);

    // Release the thread now.
    GC_RELEASE(thread);

    rb_vm_thread_t *t = GetThreadPtr(thread);

    // Normally the pthread ID is set into the VM structure in the other
    // thread right after pthread_create(), but we might run before the
    // assignment!
    t->thread = pthread_self();

    pthread_cleanup_push(rb_vm_thread_destructor, (void *)thread);

    RoxorVM *vm = GET_VM();
    try {
	VALUE val = rb_vm_block_eval(t->body, t->argc, t->argv);
	GC_WB(&t->value, val);
    }
    catch (...) {
	VALUE exc;
	RoxorSpecialException *sexc = vm->get_special_exc();
	if (sexc != NULL && sexc->type == RETURN_FROM_BLOCK_EXCEPTION) {
	    delete sexc;
	    vm->set_special_exc(NULL);
	    exc = rb_exc_new2(rb_eLocalJumpError,
		    "unexpected return from Thread");
	}
	else {
	    exc = rb_vm_current_exception();
	}
#if ROXOR_VM_DEBUG
	printf("%s (%s:%d): exc = \"%s\"\n",
			__FUNCTION__, __FILE__, __LINE__, RSTRING_PTR(rb_inspect(exc)));
#endif
	if (exc != Qnil) {
	    GC_WB(&t->exception, exc);
	}
	t->value = Qnil;
    }

    pthread_cleanup_pop(0);

    rb_thread_remove_from_group(thread); 
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
	rb_vm_block_make_detachable_proc(body);

	// Release ownership of all dynamic variables, mark the block as
	// being run from a thread.
	for (int i = 0; i < body->dvars_size; i++) {
	    VALUE *dvar = body->dvars[i];
	    rb_vm_release_ownership(*dvar);
	}
	body->flags |= VM_BLOCK_THREAD;
    }
    else {
	t->body = NULL;
    }
   
    if (argc > 0) {
	t->argc = argc;
	GC_WB(&t->argv, xmalloc_ptrs(sizeof(VALUE) * argc));
	for (int i = 0; i < argc; i++) {
	    GC_WB(&t->argv[i], argv[i]);
	}
    }
    else {
	t->argc = 0;
	t->argv = NULL;
    }

    t->vm  = vm;
    t->value = Qnil;
    t->locals = Qnil;
    t->exception = Qnil;
    t->status = THREAD_ALIVE;
    t->in_cond_wait = false;
    t->abort_on_exception = false;
    t->joined_on_exception = false;
    t->wait_for_mutex_lock = false;
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

    if (rb_thread_alone()) {
	rb_raise(rb_eThreadError,
		"stopping only thread\n\tnote: use sleep to stop forever");
    }

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
    RoxorCoreLock lock;

    if (t->status != THREAD_KILLED && t->status != THREAD_DEAD) {
	t->status = THREAD_KILLED;
	if (t->thread == pthread_self()) {
	    lock.unlock();
	    rb_vm_thread_throw_kill();
	    return;
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
    lock.unlock();
}

extern "C"
void
rb_vm_thread_raise(rb_vm_thread_t *t, VALUE exc)
{
    // XXX we should lock here
    RoxorVM *vm = (RoxorVM *)t->vm;
#if ROXOR_VM_DEBUG
	printf("%s (%s:%d): exc = \"%s\"\n",
			__FUNCTION__, __FILE__, __LINE__, RSTRING_PTR(rb_inspect(exc)));
#endif
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
Class
rb_vm_get_current_class(void)
{
    return GET_VM()->get_current_class();
}

extern "C"
bool
rb_vm_generate_objc_class_name(const char *name, char *buf, size_t buflen)
{
    RoxorCoreLock lock;

    if (name == NULL) {
	static unsigned long anon_count = 1;
	if (anon_count == ULONG_MAX) {
	    return false;
	}
	snprintf(buf, buflen, "RBAnonymous%ld", ++anon_count);
    }
    else {
	if (objc_getClass(name) != NULL) {
	    unsigned long count = 1;
	    snprintf(buf, buflen, "RB%s", name);
	    while (objc_getClass(buf) != NULL) {
		if (count == ULONG_MAX) {
		    return false;
		}
		snprintf(buf, buflen, "RB%s%ld", name, ++count);
	    }
	}
	else {
	    strlcpy(buf, name, buflen);
	}
    }
    return true;
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

extern "C"
void
rb_vm_add_stub(const char *types, void *func, unsigned char is_objc)
{
    GET_CORE()->insert_stub(types, func, is_objc);
}

#if !defined(MACRUBY_STATIC)
static IMP old_resolveClassMethod_imp = NULL;
static IMP old_resolveInstanceMethod_imp = NULL;
static SEL sel_resolveClassMethod = 0;
static SEL sel_resolveInstanceMethod = 0;

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

static bool
class_has_custom_resolver(Class klass)
{
    if (!class_isMetaClass(klass)) {
	klass = *(Class *)klass;
    }
    if (class_getMethodImplementation(klass, sel_resolveClassMethod)
	    != (IMP)resolveClassMethod_imp) {
	return true;
    }
    if (class_getMethodImplementation(klass, sel_resolveInstanceMethod)
	    != (IMP)resolveInstanceMethod_imp) {
	return true;
    }
    return false;
}
#endif

#if !defined(MACRUBY_STATIC)
# include ".objs/kernel_data.c"
#endif

static bool vm_enable_stats = false;

extern "C"
void 
Init_PreVM(void)
{
#if !defined(MACRUBY_STATIC)
    // To emit DWARF exception tables. 
    llvm::JITExceptionHandling = true;
    // To emit DWARF debug metadata. 
    llvm::JITEmitDebugInfo = true; 
    // To not interfere with our signal handling mechanism.
    llvm::DisablePrettyStackTrace = true;
    // To not corrupt stack pointer (essential for backtracing).
    llvm::NoFramePointerElim = true;

    if (getenv("VM_STATS") != NULL) {
	vm_enable_stats = true;
	llvm::EnableStatistics();
    }

    MemoryBuffer *mbuf = NULL;
    const char *kernel_file = getenv("VM_KERNEL_PATH");
    if (kernel_file != NULL) {
	std::string err;
#if !defined(LLVM_TOT)
	OwningPtr<MemoryBuffer> MB;
	error_code errcode = MemoryBuffer::getFile(kernel_file, MB);
	if (errcode) {
	    err = errcode.message();
	}
	else {
	    mbuf = MB.take();
	    assert(mbuf != NULL);
	}
#else
	mbuf = MemoryBuffer::getFile(kernel_file, &err);
#endif
	if (mbuf == NULL) {
	    printf("can't open given kernel file `%s': %s\n", kernel_file,
		    err.c_str());
	    abort();
	}
    }
    else {
	// Retrieve the kernel bitcode for the right architecture. We substract
	// 1 to the length because it's NULL terminated.
	const char *kernel_beg;
	const char *kernel_end;
#if __LP64__
	kernel_beg = (const char *)_objs_kernel_x86_64_bc;
	kernel_end = kernel_beg + _objs_kernel_x86_64_bc_len - 1;
#else
	kernel_beg = (const char *)_objs_kernel_i386_bc;
	kernel_end = kernel_beg + _objs_kernel_i386_bc_len - 1;
#endif

	mbuf = MemoryBuffer::getMemBuffer(StringRef(kernel_beg, kernel_end - kernel_beg));
    }
    std::string err;
    RoxorCompiler::module = ParseBitcodeFile(mbuf, getGlobalContext(), &err);
    delete mbuf;
    if (RoxorCompiler::module == NULL) {
	printf("kernel bitcode couldn't be read: %s\n", err.c_str());
    }
    assert(RoxorCompiler::module != NULL);

    RoxorInterpreter::shared = new RoxorInterpreter();
#endif // !MACRUBY_STATIC

    RoxorCore::shared = new RoxorCore();
    RoxorVM::main = new RoxorVM();

    pthread_assert(pthread_key_create(&RoxorVM::vm_thread_key, NULL));
    pthread_assert(pthread_setspecific(RoxorVM::vm_thread_key,
		(void *)RoxorVM::main));

    setup_builtin_stubs();

#if !defined(MACRUBY_STATIC)
    Class ns_object = (Class)objc_getClass("NSObject");
    Method m;
    sel_resolveClassMethod = sel_registerName("resolveClassMethod:");
    m = class_getInstanceMethod(*(Class *)ns_object, sel_resolveClassMethod);
    assert(m != NULL);
    old_resolveClassMethod_imp = method_getImplementation(m);
    method_setImplementation(m, (IMP)resolveClassMethod_imp);

    sel_resolveInstanceMethod = sel_registerName("resolveInstanceMethod:");
    m = class_getInstanceMethod(*(Class *)ns_object, sel_resolveInstanceMethod);
    assert(m != NULL);
    old_resolveInstanceMethod_imp = method_getImplementation(m);
    method_setImplementation(m, (IMP)resolveInstanceMethod_imp);
#endif // !MACRUBY_STATIC

    // Early define some classes.
    rb_cNSString = (VALUE)objc_getClass("NSString");
    assert(rb_cNSString != 0);
    rb_cNSArray = (VALUE)objc_getClass("NSArray");
    assert(rb_cNSArray != 0);
    rb_cNSHash = (VALUE)objc_getClass("NSDictionary");
    assert(rb_cNSHash != 0);
    rb_cSymbol = rb_objc_create_class("Symbol", rb_cNSString);
    rb_cEncoding = rb_objc_create_class("Encoding",
	    (VALUE)objc_getClass("NSObject"));
    rb_cRubyString = rb_objc_create_class("String",
	    (VALUE)objc_getClass("NSMutableString"));
}

static VALUE
rb_toplevel_to_s(VALUE rcv, SEL sel)
{
    return rb_str_new2("main");
}

#if !defined(MACRUBY_STATIC)
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
    assert(CFURLGetFileSystemRepresentation(url, true, (UInt8 *)&path[2],
		len - 2));
    CFRelease(url);

    return path;
}

extern "C"
int
macruby_main(const char *path, int argc, char **argv)
{
    // Transform the original argv into something like this:
    // argv[0] = original value
    // argv[1] = -I/path-to-app-dir/(...)/Resources
    // argv[2] = main .rb file
    // argv[3 .. N] = rest of original argv

    char **newargv = (char **)malloc(sizeof(char *) * (argc + 2));
    assert(newargv != NULL);
    newargv[0] = argv[0];
    
    char *p1 = (char *)malloc(PATH_MAX);
    assert(p1 != NULL);
    newargv[1] = (char *)resources_path(p1, PATH_MAX);
    
    char *p2 = (char *)malloc(PATH_MAX);
    assert(p2 != NULL);
    snprintf(p2, PATH_MAX, "%s/%s", (path[0] != '/') ? &p1[2] : "", path);
    newargv[2] = p2;
   
    int n = 3; 
    for (int i = 1; i < argc; i++) {
	if (strncmp(argv[i], "-psn_", 5) != 0) {
	    newargv[n++] = argv[i];
	}
    }
 
    argv = newargv;    
    argc = n;

    unsetenv("RUBYOPT");

    try {
	ruby_sysinit(&argc, &argv);
	ruby_init();
	void *tree = ruby_options(argc, argv);
	rb_vm_init_compiler();
	free(newargv);
	free(p1);
	free(p2);
	rb_objc_fix_relocatable_load_path();
	rb_objc_load_loaded_frameworks_bridgesupport();
	return ruby_run_node(tree);
    }
    catch (...) {
	rb_vm_print_current_exception();
	exit(1);	
    }
}
#endif

extern "C"
void
Init_VM(void)
{
    rb_cTopLevel = rb_define_class("TopLevel", rb_cObject);
    rb_objc_define_method(rb_cTopLevel, "to_s", (void *)rb_toplevel_to_s, 0);
    rb_objc_define_method(rb_cTopLevel, "inspect", (void *)rb_toplevel_to_s, 0);

    GET_VM()->set_current_class(NULL);

    VALUE top_self = rb_obj_alloc(rb_cTopLevel);
    GC_RETAIN(top_self);
    GET_VM()->set_current_top_object(top_self);

    rb_vm_set_current_scope(rb_cNSObject, SCOPE_PRIVATE);
}

void
RoxorVM::setup_from_current_thread(void)
{
    pthread_assert(pthread_setspecific(RoxorVM::vm_thread_key, (void *)this));

    rb_vm_thread_t *t = (rb_vm_thread_t *)xmalloc(sizeof(rb_vm_thread_t));
    rb_vm_thread_pre_init(t, NULL, 0, NULL, (void *)this);
    t->thread = pthread_self();

    VALUE thread = Data_Wrap_Struct(rb_cThread, NULL, NULL, t);
    GET_CORE()->register_thread(thread);
    rb_vm_post_register_thread(thread);
    this->set_thread(thread);
}

extern "C"
void
rb_vm_register_current_alien_thread(void)
{
    // The creation of RoxorVM objects is done lazily (in RoxorVM::current())
    // for performance reason, because the callback is called *a lot* and most
    // of the time from various parts of the system which will never ask us to
    // execute Ruby code.
#if 0
    if (GET_CORE()->get_running()) {
	printf("registered alien thread %p\n", pthread_self());
	RoxorVM *vm = new RoxorVM();
	vm->setup_from_current_thread();
    }
#endif
}

extern "C"
void
rb_vm_unregister_current_alien_thread(void)
{
    if (!GET_CORE()->get_running()) {
	return;
    }

    pthread_t self = pthread_self();
    if (GetThreadPtr(RoxorVM::main->get_thread())->thread == self) {
	// Do not unregister the main thread.
	return;
    }

    // Check if the current pthread has been registered.
    RoxorCoreLock lock;
    VALUE ary = GET_CORE()->get_threads();
    bool need_to_unregister = false;
    for (int i = 0; i < RARRAY_LEN(ary); i++) {
	VALUE t = RARRAY_AT(ary, i);
	if (GetThreadPtr(t)->thread == self) {
	    need_to_unregister = true;
	}
    }
    lock.unlock();

    // If yes, appropriately unregister it.
    if (need_to_unregister) {
	//printf("unregistered alien thread %p\n", pthread_self());
	GET_CORE()->unregister_thread(GET_VM()->get_thread());
    }
}

// AOT features. These are registered at runtime once an AOT object file
// is loaded, either directly from an executable's main() function or from
// a gcc constructor (in case of a dylib).
//
// XXX this shared map is not part of RoxorCore because gcc constructors can
// potentially be called *before* RoxorCore has been initialized. This is
// definitely not thread-safe, but it shouldn't be a big deal at this point.
static std::map<std::string, void *> aot_features;

extern "C"
bool
rb_vm_aot_feature_load(const char *name)
{
    std::string key(name);
    std::map<std::string, void *>::iterator iter = aot_features.find(name);
    if (iter == aot_features.end()) {
	return false;
    }
    void *init_func = iter->second;
    if (init_func != NULL) {
	RoxorVM *vm = GET_VM();
	struct Finally {
	    RoxorVM *vm;
	    Class old_class;
	    rb_vm_outer_t *old_outer_stack;
	    rb_vm_outer_t *old_current_outer;
	    Finally(RoxorVM *_vm) {
		vm = _vm;
		old_class = vm->get_current_class();
		old_outer_stack = vm->get_outer_stack();
		old_current_outer = vm->get_current_outer();
	    }
	    ~Finally() { 
		vm->set_current_outer(old_current_outer);
		vm->set_outer_stack(old_outer_stack);
		vm->set_current_class(old_class);
	    }
	} finalizer(vm);
	
	vm->set_current_class(NULL);
	vm->set_outer_stack(NULL);
	vm->set_current_outer(NULL);
	
        ((void *(*)(void *, void *))init_func)((void *)rb_vm_top_self(), NULL);
        iter->second = NULL;
    }
    return true;
}

extern "C"
void
rb_vm_aot_feature_provide(const char *name, void *init_func)
{
    std::string key(name);
    std::map<std::string, void *>::iterator iter = aot_features.find(key);
    if (iter != aot_features.end()) {
	printf("WARNING: AOT feature '%s' already registered, new one will be ignored. This could happen if you link your executable against dylibs that contain the same Ruby file.\n", name);
    }
    aot_features[key] = init_func;
}

void
rb_vm_dln_load(void (*init_fct)(void), IMP __mrep__)
{
    RoxorVM *vm = GET_VM();

    struct Finally {
	RoxorVM *vm;
	Class old_class;
	rb_vm_outer_t *old_outer_stack;
	rb_vm_outer_t *old_current_outer;
	Finally(RoxorVM *_vm) {
	    vm = _vm;
	    old_class = vm->get_current_class();
	    old_outer_stack = vm->get_outer_stack();
	    old_current_outer = vm->get_current_outer();
	}
	~Finally() { 
	    vm->set_current_outer(old_current_outer);
	    vm->set_outer_stack(old_outer_stack);
	    vm->set_current_class(old_class);
	}
    } finalizer(vm);

    vm->set_current_class(NULL);
    vm->set_outer_stack(NULL);
    vm->set_current_outer(NULL);

    if (__mrep__ == NULL) {
	(*init_fct)();
    }
    else {
	(__mrep__)((id)vm->get_current_top_object(), 0);
    }
}

void
rb_vm_load(const char *fname_str, int wrap)
{
    RoxorVM *vm = GET_VM();
    rb_vm_binding_t *b = vm->current_binding();
    if (b != NULL) {
	vm->pop_current_binding();
    }
    NODE *node = (NODE *)rb_load_file(fname_str);
    if (b != NULL) {
	vm->push_current_binding(b);
    }
    if (node == NULL) {
	rb_raise(rb_eSyntaxError, "compile error");
    }

    struct Finally {
	RoxorVM *vm;
	Class old_class;
	rb_vm_outer_t *old_outer_stack;
	rb_vm_outer_t *old_current_outer;
	Finally(RoxorVM *_vm) {
	    vm = _vm;
	    old_class = vm->get_current_class();
	    old_outer_stack = vm->get_outer_stack();
	    old_current_outer = vm->get_current_outer();
	}
	~Finally() { 
	    vm->set_current_outer(old_current_outer);
	    vm->set_outer_stack(old_outer_stack);
	    vm->set_current_class(old_class);
	}
    } finalizer(vm);

    vm->set_current_class(NULL);
    vm->set_outer_stack(NULL);
    vm->set_current_outer(NULL);

    rb_vm_run(fname_str, node, NULL, false);
}

void
RoxorCore::dispose_class(Class k)
{
//printf("%p %d\n", k, auto_zone_retain_count(__auto_zone, k));
//    if (auto_zone_retain_count(__auto_zone, k) > 1) {
//	return;
//    }
//return;

    RoxorCoreLock lock;

    // Free ivars dict.
    rb_class_ivar_set_dict((VALUE)k, NULL);

    // Free class flags.
    rb_class_erase_mask(k);

#if !defined(MACRUBY_STATIC)
    // Free lazy-JIT caches.
    std::multimap<Class, SEL>::iterator iter =
	method_source_sels.find(k);

    if (iter != method_source_sels.end()) {
	std::multimap<Class, SEL>::iterator first = iter;
	std::multimap<Class, SEL>::iterator last =
	    method_source_sels.upper_bound(k);

	for (; iter != last; iter++) {
	    SEL sel = iter->second;
			
	    std::map<SEL, std::map<Class, rb_vm_method_source_t *> *>::iterator
		iter2 = method_sources.find(sel);
	    if (iter2 != method_sources.end()) {
		delete iter2->second;
		method_sources.erase(iter2);
	    }
	}
	method_source_sels.erase(first, last);
    }
#endif

    // Free the runtime bits.
    objc_disposeClassPair(k);
}

extern "C"
void
rb_vm_dispose_class(Class k)
{
    GET_CORE()->dispose_class(k);
}

extern "C"
void
Init_PostVM(void)
{
    // Create and register the main thread.
    RoxorVM *main_vm = GET_VM();
    main_vm->setup_from_current_thread();

    // Create main thread group.
    VALUE group = rb_obj_alloc(rb_cThGroup);
    rb_thgroup_add(group, main_vm->get_thread());
    rb_define_const(rb_cThGroup, "Default", group);
}

extern "C"
void
rb_vm_finalize(void)
{
#if !defined(MACRUBY_STATIC)
    if (getenv("VM_DUMP_IR") != NULL) {
	printf("IR dump ----------------------------------------------\n");
	RoxorCompiler::module->dump();
	printf("------------------------------------------------------\n");
    }
#if ROXOR_VM_DEBUG
    printf("functions all=%ld compiled=%ld\n", RoxorCompiler::module->size(),
	    GET_CORE()->get_functions_compiled());
#endif

    if (getenv("VM_VERIFY_IR") != NULL) {
	rb_verify_module();
	printf("IR verified!\n");
    }

    if (vm_enable_stats) {
	llvm::PrintStatistics();
    }
#endif

    // XXX: deleting the core is not safe at this point because there might be
    // threads still running and trying to unregister.
//    delete RoxorCore::shared;
//    RoxorCore::shared = NULL;
    GET_CORE()->call_all_finalizers();
}
