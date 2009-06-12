/*
 * MacRuby VM.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 * 
 * Copyright (C) 2008-2009, Apple Inc. All rights reserved.
 */

#define ROXOR_VM_DEBUG		0
#define ROXOR_INTERPRET_EVAL	0

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

#define force_inline __attribute__((always_inline))

RoxorVM *RoxorVM::current = NULL;

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
	     // TODO optimize me!
	     if (functions.empty()) {
		return NULL;
	     }
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
			       unsigned char *TableEnd, 
			       unsigned char* FrameRegister) {
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
extern "C" void __cxa_throw(void *, void *, void *);

RoxorVM::RoxorVM(void)
{
    running = false;
    current_top_object = Qnil;
    safe_level = 0;

    backref = Qnil;
    broken_with = Qundef;
    last_status = Qnil;
    errinfo = Qnil;

    parse_in_eval = false;

#if ROXOR_VM_DEBUG
    functions_compiled = 0;
#endif

    load_path = rb_ary_new();
    rb_objc_retain((void *)load_path);
    loaded_features = rb_ary_new();
    rb_objc_retain((void *)loaded_features);

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
	for (std::vector<rb_vm_block_t *>::iterator i =
		current_blocks.begin();
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
RoxorVM::optimize(Function *func)
{
    fpm->run(*func);
}

IMP
RoxorVM::compile(Function *func)
{
#if ROXOR_COMPILER_DEBUG
    if (verifyModule(*RoxorCompiler::module)) {
	printf("Error during module verification\n");
	exit(1);
    }

    uint64_t start = mach_absolute_time();
#endif

    // Optimize & compile.
    optimize(func);
    IMP imp = (IMP)ee->getPointerToFunction(func);

#if ROXOR_COMPILER_DEBUG
    uint64_t elapsed = mach_absolute_time() - start;

    static mach_timebase_info_data_t sTimebaseInfo;

    if (sTimebaseInfo.denom == 0) {
	(void) mach_timebase_info(&sTimebaseInfo);
    }

    uint64_t elapsedNano = elapsed * sTimebaseInfo.numer / sTimebaseInfo.denom;

    printf("compilation of LLVM function %p done, took %lld ns\n",
	func, elapsedNano);
#endif

#if ROXOR_VM_DEBUG
    functions_compiled++;
#endif

    return imp;
}

VALUE
RoxorVM::interpret(Function *func)
{
    std::vector<GenericValue> args;
    args.push_back(PTOGV((void *)GET_VM()->current_top_object));
    args.push_back(PTOGV(NULL));
    return (VALUE)iee->runFunction(func, args).IntVal.getZExtValue();
}

bool
RoxorVM::symbolize_call_address(void *addr, void **startp, unsigned long *ln,
				char *name, size_t name_len)
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

    if (name != NULL || ln != NULL) {
	std::map<IMP, rb_vm_method_node_t *>::iterator iter = 
	    ruby_imps.find((IMP)start);
	if (iter == ruby_imps.end()) {
	    // TODO symbolize objc selectors
	    return false;
	}

	rb_vm_method_node_t *node = iter->second;
#if 0 // TODO
	if (ln != NULL) {
	    *ln = nd_line(node->node);
	}
#endif
	if (name != NULL) {
	    strncpy(name, sel_getName(node->sel), name_len);
	}
    }

    return true;
}

struct ccache *
RoxorVM::constant_cache_get(ID path)
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
    return GET_VM()->constant_cache_get(rb_intern(name));
}


struct mcache *
RoxorVM::method_cache_get(SEL sel, bool super)
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
    return GET_VM()->method_cache_get(sel, false); 
}

inline rb_vm_method_node_t *
RoxorVM::method_node_get(IMP imp)
{
    std::map<IMP, rb_vm_method_node_t *>::iterator iter = ruby_imps.find(imp);
    if (iter == ruby_imps.end()) {
	return NULL;
    }
    return iter->second;
}

extern "C"
rb_vm_method_node_t *
rb_vm_get_method_node(IMP imp)
{
    return GET_VM()->method_node_get(imp);
}

size_t
RoxorVM::get_sizeof(const Type *type)
{
    return ee->getTargetData()->getTypeSizeInBits(type) / 8;
}

size_t
RoxorVM::get_sizeof(const char *type)
{
    return get_sizeof(RoxorCompiler::shared->convert_type(type));
}

bool
RoxorVM::is_large_struct_type(const Type *type)
{
    return type->getTypeID() == Type::StructTyID
	&& ee->getTargetData()->getTypeSizeInBits(type) > 128;
}

inline GlobalVariable *
RoxorVM::redefined_op_gvar(SEL sel, bool create)
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
RoxorVM::should_invalidate_inline_op(SEL sel, Class klass)
{
    if (sel == selEq || sel == selEqq || sel == selNeq) {
	return klass == (Class)rb_cFixnum
	    || klass == (Class)rb_cSymbol;
    }
    if (sel == selPLUS || sel == selMINUS || sel == selDIV 
	|| sel == selMULT || sel == selLT || sel == selLE 
	|| sel == selGT || sel == selGE) {
	return klass == (Class)rb_cFixnum;
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

void
RoxorVM::add_method(Class klass, SEL sel, IMP imp, IMP ruby_imp,
		    const rb_vm_arity_t &arity, int flags, const char *types)
{
#if ROXOR_VM_DEBUG
    printf("defining %c[%s %s] with imp %p types %s\n",
	    class_isMetaClass(klass) ? '+' : '-',
	    class_getName(klass),
	    sel_getName(sel),
	    imp,
	    types);
#endif

    // Register the implementation into the runtime.
    class_replaceMethod(klass, sel, imp, types);

    // Cache the method node.
    std::map<IMP, rb_vm_method_node_t *>::iterator iter = ruby_imps.find(imp);
    rb_vm_method_node_t *node;
    if (iter == ruby_imps.end()) {
	node = (rb_vm_method_node_t *)malloc(sizeof(rb_vm_method_node_t));
	ruby_imps[imp] = node;
	node->objc_imp = imp;
    }
    else {
	node = iter->second;
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
    std::map<SEL, struct mcache *>::iterator iter2 = mcache.find(sel);
    if (iter2 != mcache.end()) {
	iter2->second->flag = 0;
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

    if (is_running()) {
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
		class_replaceMethod((Class)mod, sel, imp, types);
#if ROXOR_VM_DEBUG
		printf("forward %c[%s %s] with imp %p node %p types %s\n",
			class_isMetaClass((Class)mod) ? '+' : '-',
			class_getName((Class)mod),
			sel_getName(sel),
			imp,
			node,
			types);
#endif
	    }
	}
    }
}

void
RoxorVM::const_defined(ID path)
{
    // Invalidate constant cache.
    std::map<ID, struct ccache *>::iterator iter = ccache.find(path);
    if (iter != ccache.end()) {
	iter->second->val = Qundef;
    }
}

inline int
RoxorVM::find_ivar_slot(VALUE klass, ID name, bool create)
{
    VALUE k = klass;
    int slot = 0;

    while (k != 0) {
	std::map <ID, int> *slots = GET_VM()->get_ivar_slots((Class)k);
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

inline bool
RoxorVM::class_can_have_ivar_slots(VALUE klass)
{
    const long klass_version = RCLASS_VERSION(klass);
    if ((klass_version & RCLASS_IS_RUBY_CLASS) != RCLASS_IS_RUBY_CLASS
	|| (klass_version & RCLASS_IS_OBJECT_SUBCLASS)
	    != RCLASS_IS_OBJECT_SUBCLASS) {
	return false;
    }
    return true;
}

extern "C"
bool
rb_vm_running(void)
{
    return GET_VM()->is_running();
}

extern "C"
void
rb_vm_set_running(bool flag)
{
    GET_VM()->set_running(flag); 
}

extern "C"
void 
rb_vm_set_const(VALUE outer, ID id, VALUE obj)
{
    if (GET_VM()->current_class != NULL) {
	outer = (VALUE)GET_VM()->current_class;
    }
    rb_const_set(outer, id, obj);
    GET_VM()->const_defined(id);
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
	struct rb_vm_outer *o = GET_VM()->get_outer((Class)outer);
	while (o != NULL && o->klass != (Class)rb_cNSObject) {
	    VALUE val = rb_const_get_direct((VALUE)o->klass, path);
	    if (val != Qundef) {
		return defined ? Qtrue : val;
	    }
	    o = o->outer;
	}
    }

    // Nothing was found earlier so here we do a hierarchical lookup.
    return defined ? rb_const_defined(outer, path) : rb_const_get(outer, path);
}

extern "C"
VALUE
rb_vm_get_const(VALUE outer, unsigned char lexical_lookup,
		struct ccache *cache, ID path)
{
    if (GET_VM()->current_class != NULL && lexical_lookup) {
	outer = (VALUE)GET_VM()->current_class;
    }

    assert(cache != NULL);
    if (cache->outer == outer && cache->val != Qundef) {
	return cache->val;
    }

    VALUE val = rb_vm_const_lookup(outer, path, lexical_lookup, false);

    cache->outer = outer;
    cache->val = val;

    return val;
}

extern "C"
void
rb_vm_const_is_defined(ID path)
{
    GET_VM()->const_defined(path);
}

extern "C"
void
rb_vm_set_outer(VALUE klass, VALUE under)
{
    GET_VM()->set_outer((Class)klass, (Class)under);
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
rb_vm_define_class(ID path, VALUE outer, VALUE super, unsigned char is_module)
{
    assert(path > 0);
    check_if_module(outer);

    if (GET_VM()->current_class != NULL) {
	outer = (VALUE)GET_VM()->current_class;
    }

    VALUE klass;
    if (rb_const_defined_at(outer, path)) {
	klass = rb_const_get_at(outer, path);
	if (!is_module && super != 0) {
	    check_if_module(klass);
	    if (RCLASS_SUPER(klass) != super) {
		rb_raise(rb_eTypeError, "superclass mismatch for class %s",
			rb_class2name(klass));
	    }
	}
    }
    else {
	if (is_module) {
	    assert(super == 0);
	    klass = rb_define_module_id(path);
	    rb_set_class_path(klass, outer, rb_id2name(path));
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
	    rb_set_class_path(klass, outer, rb_id2name(path));
	    rb_const_set(outer, path, klass);
	    rb_class_inherited(super, klass);
	}
    }

#if ROXOR_VM_DEBUG
    if (is_module) {
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
		FROM_GV(Args[3], unsigned char)));
}

extern "C"
VALUE
rb_vm_ivar_get(VALUE obj, ID name, int *slot_cache)
{
#if ROXOR_VM_DEBUG
    printf("get ivar %p.%s slot %d\n", (void *)obj, rb_id2name(name), 
	    slot_cache == NULL ? -1 : *slot_cache);
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
rb_vm_cvar_get(VALUE klass, ID id)
{
    if (GET_VM()->current_class != NULL) {
	klass = (VALUE)GET_VM()->current_class;
    }
    return rb_cvar_get(klass, id);
}

extern "C"
VALUE
rb_vm_cvar_set(VALUE klass, ID id, VALUE val)
{
    if (GET_VM()->current_class != NULL) {
	klass = (VALUE)GET_VM()->current_class;
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

    rb_vm_method_node_t *node = GET_VM()->method_node_get(imp);
    if (node == NULL) {
	rb_raise(rb_eArgError, "cannot alias non-Ruby method `%s'",
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

    GET_VM()->add_method(klass, sel, imp, node->ruby_imp,
	    node->arity, node->flags, types);
}

extern "C"
void
rb_vm_alias(VALUE outer, ID name, ID def)
{
    if (GET_VM()->current_class != NULL) {
	outer = (VALUE)GET_VM()->current_class;
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
rb_vm_undef(VALUE klass, ID name)
{
    if (GET_VM()->current_class != NULL) {
	klass = (VALUE)GET_VM()->current_class;
    }

    rb_undef(klass, name);
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

    RoxorVM *vm = GET_VM();
    if (vm->class_can_have_ivar_slots(klass)) {
	*slot_cache = vm->find_ivar_slot(klass, name, true);
    }
}

extern "C"
int
rb_vm_find_class_ivar_slot(VALUE klass, ID name)
{
    RoxorVM *vm = GET_VM();
    if (vm->class_can_have_ivar_slots(klass)) {
	return vm->find_ivar_slot(klass, name, false);
    }
    return -1;
}

static inline void 
resolve_method_type(char *buf, const size_t buflen, Class klass, Method m,
		    SEL sel, const unsigned int oc_arity)
{
    bs_element_method_t *bs_method = GET_VM()->find_bs_method(klass, sel);

    if (m == NULL
	|| !rb_objc_get_types(Qnil, klass, sel, m, bs_method, buf, buflen)) {

	std::map<SEL, std::string> &map = class_isMetaClass(klass)
	    ? GET_VM()->bs_informal_protocol_cmethods
	    : GET_VM()->bs_informal_protocol_imethods;

	std::map<SEL, std::string>::iterator iter = map.find(sel);	
	if (iter != map.end()) {
	    strncpy(buf, iter->second.c_str(), buflen);
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

static void
resolve_method(Class klass, SEL sel, Function *func, NODE *node, IMP imp,
	       Method m)
{
    const int oc_arity = rb_vm_node_arity(node).real + 3;

    char types[100];
    resolve_method_type(types, sizeof types, klass, m, sel, oc_arity);

    std::map<Function *, IMP>::iterator iter =
	GET_VM()->objc_to_ruby_stubs.find(func);
    IMP objc_imp;
    if (iter == GET_VM()->objc_to_ruby_stubs.end()) {
	Function *objc_func = RoxorCompiler::shared->compile_objc_stub(func,
		types);
	objc_imp = GET_VM()->compile(objc_func);
	GET_VM()->objc_to_ruby_stubs[func] = objc_imp;
    }
    else {
	objc_imp = iter->second;
    }

    if (imp == NULL) {
	imp = GET_VM()->compile(func);
    }

    const rb_vm_arity_t arity = rb_vm_node_arity(node);
    const int flags = rb_vm_node_flags(node);
    GET_VM()->add_method(klass, sel, objc_imp, imp, arity, flags, types);
}

static bool
__rb_vm_resolve_method(std::map<Class, rb_vm_method_source_t *> *map,
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
	    resolve_method(iter->first, sel, m->func, m->node, NULL, NULL);
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
    if (!GET_VM()->is_running()) {
	return false;
    }

#if ROXOR_VM_DEBUG
    printf("resolving %c[%s %s]\n",
	class_isMetaClass(klass) ? '+' : '-',
	class_getName(klass),
	sel_getName(sel));
#endif

    std::map<Class, rb_vm_method_source_t *> *map =
	GET_VM()->method_sources_for_sel(sel, false);
    if (map == NULL) {
	return false;
    }

    // Find the class where the method should be defined.
    while (map->find(klass) == map->end() && klass != NULL) {
	klass = class_getSuperclass(klass);
    }
    if (klass == NULL) {
	return false;
    }

    // Now let's resolve all methods of the given name on the given class
    // and superclasses.
    return __rb_vm_resolve_method(map, klass, sel);
}

extern "C"
void
rb_vm_prepare_method(Class klass, SEL sel, Function *func, NODE *node)
{
    if (GET_VM()->current_class != NULL) {
	klass = GET_VM()->current_class;
    }

    const rb_vm_arity_t arity = rb_vm_node_arity(node);
    const char *sel_name = sel_getName(sel);
    const bool genuine_selector = sel_name[strlen(sel_name) - 1] == ':';
    bool redefined = false;
    SEL orig_sel = sel;
    Method m;
    IMP imp = NULL;

prepare_method:

    m = class_getInstanceMethod(klass, sel);
    if (m != NULL) {
	// The method already exists - we need to JIT it.
	if (imp == NULL) {
	    imp = GET_VM()->compile(func);
	}
	resolve_method(klass, sel, func, node, imp, m);
    }
    else {
	// Let's keep the method and JIT it later on demand.
#if ROXOR_VM_DEBUG
	printf("preparing %c[%s %s] with LLVM func %p node %p\n",
		class_isMetaClass(klass) ? '+' : '-',
		class_getName(klass),
		sel_getName(sel),
		func,
		node);
#endif

	std::map<Class, rb_vm_method_source_t *> *map =
	    GET_VM()->method_sources_for_sel(sel, true);

	std::map<Class, rb_vm_method_source_t *>::iterator iter =
	    map->find(klass);

	rb_vm_method_source_t *m = NULL;
	if (iter == map->end()) {
	    m = (rb_vm_method_source_t *)malloc(sizeof(rb_vm_method_source_t));
	    map->insert(std::make_pair(klass, m));
	    GET_VM()->method_source_sels.insert(std::make_pair(klass, sel));
	}
	else {
	    m = iter->second;
	}

	m->func = func;
	m->node = node;
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
		rb_vm_prepare_method((Class)mod, orig_sel, func, node);
	    }
	}
    }
}

static void __rb_vm_define_method(Class klass, SEL sel, IMP imp,
	const rb_vm_arity_t &arity, int flags, bool direct);

extern "C"
void
rb_vm_prepare_method2(Class klass, SEL sel, IMP imp, rb_vm_arity_t arity,
		      int flags)
{
    __rb_vm_define_method(klass, sel, imp, arity, flags, false);
}

#define VISI(x) ((x)&NOEX_MASK)
#define VISI_CHECK(x,f) (VISI(x) == (f))

static void
push_method(VALUE ary, VALUE mod, SEL sel, rb_vm_method_node_t *node,
	    int (*filter) (VALUE, ID, VALUE))
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
	if (node != NULL) {
	    const int type = rb_vm_method_node_noex(node);
	    (*filter)(sym, type, ary);
	}
	else {
	    rb_ary_push(ary, sym);
	}
    }
} 

extern "C"
void
rb_vm_push_methods(VALUE ary, VALUE mod, bool include_objc_methods,
		   int (*filter) (VALUE, ID, VALUE))
{
    // TODO take into account undefined methods

    unsigned int count;
    Method *methods = class_copyMethodList((Class)mod, &count); 
    if (methods != NULL) {
	for (unsigned int i = 0; i < count; i++) {
	    Method m = methods[i];
	    SEL sel = method_getName(m);
	    IMP imp = method_getImplementation(m);
	    rb_vm_method_node_t *node = rb_vm_get_method_node(imp);
	    if (node == NULL && !include_objc_methods) {
		continue;
	    }
	    push_method(ary, mod, sel, node, filter);
	}
	free(methods);
    }

    Class k = (Class)mod;
    do {
	std::multimap<Class, SEL>::iterator iter =
	    GET_VM()->method_source_sels.find(k);

	if (iter != GET_VM()->method_source_sels.end()) {
	    std::multimap<Class, SEL>::iterator last =
		GET_VM()->method_source_sels.upper_bound(k);

	    for (; iter != last; ++iter) {
		SEL sel = iter->second;
		// TODO retrieve method NODE*
		push_method(ary, mod, sel, NULL, filter);
	    }
	}

	k = class_getSuperclass(k);
    }
    while (k != NULL);
}

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

extern "C"
void
rb_vm_copy_methods(Class from_class, Class to_class)
{
    Method *methods;
    unsigned int i, methods_count;

    // Copy existing Objective-C methods.
    methods = class_copyMethodList(from_class, &methods_count);
    if (methods != NULL) {
	for (i = 0; i < methods_count; i++) {
	    Method method = methods[i];
	    SEL sel = method_getName(method);

#if ROXOR_VM_DEBUG
	    printf("copy %c[%s %s] to %s\n",
		    class_isMetaClass(from_class) ? '+' : '-',
		    class_getName(from_class),
		    sel_getName(sel),
		    class_getName(to_class));
#endif

	    class_replaceMethod(to_class,
		    sel,
		    method_getImplementation(method),
		    method_getTypeEncoding(method));

	    std::map<Class, rb_vm_method_source_t *> *map =
		GET_VM()->method_sources_for_sel(sel, false);
	    if (map != NULL) {
		// There might be some non-JIT'ed yet methods on subclasses.
		__rb_vm_resolve_method(map, to_class, sel);
	    }
	}
	free(methods);
    }

    // Copy methods that have not been JIT'ed yet.
    std::multimap<Class, SEL>::iterator iter =
	GET_VM()->method_source_sels.find(from_class);

    if (iter != GET_VM()->method_source_sels.end()) {
	std::multimap<Class, SEL>::iterator last =
	    GET_VM()->method_source_sels.upper_bound(from_class);
	std::vector<SEL> sels_to_add;

	for (; iter != last; ++iter) {
	    SEL sel = iter->second;

	    std::map<Class, rb_vm_method_source_t *> *dict =
		GET_VM()->method_sources_for_sel(sel, false);
	    if (dict == NULL) {
		continue;
	    }

	    std::map<Class, rb_vm_method_source_t *>::iterator
		iter2 = dict->find(from_class);
	    if (iter2 == dict->end()) {
		continue;
	    }

#if ROXOR_DEBUG_VM
	    printf("lazy copy %c[%s %s] to %s\n",
		    class_isMetaClass(from_class) ? '+' : '-',
		    class_getName(from_class),
		    sel_getName(method_getName(method)),
		    class_getName(to_class));
#endif

	    rb_vm_method_source_t *m = (rb_vm_method_source_t *)
		malloc(sizeof(rb_vm_method_source_t));
	    m->func = iter2->second->func;
	    m->node = iter2->second->node;
	    dict->insert(std::make_pair(to_class, m));
	    sels_to_add.push_back(sel);
	}

	for (std::vector<SEL>::iterator i = sels_to_add.begin();
	     i != sels_to_add.end();
	     ++i) {
	    GET_VM()->method_source_sels.insert(std::make_pair(to_class, *i));
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
	*pnode = GET_VM()->method_node_get(imp);
    }
    return true;
}

extern "C"
void
rb_vm_define_attr(Class klass, const char *name, bool read, bool write,
		  int noex)
{
    assert(klass != NULL);
    assert(read || write);

    char buf[100];
    snprintf(buf, sizeof buf, "@%s", name);
    ID iname = rb_intern(buf);

    if (read) {
	Function *f = RoxorCompiler::shared->compile_read_attr(iname);
	SEL sel = sel_registerName(name);
	NODE *node = NEW_CFUNC(NULL, 0);
	NODE *body = NEW_FBODY(NEW_METHOD(node, klass, noex), 0);
	rb_objc_retain(body);

	rb_vm_prepare_method(klass, sel, f, body);
    }

    if (write) {
	Function *f = RoxorCompiler::shared->compile_write_attr(iname);
	snprintf(buf, sizeof buf, "%s=:", name);
	SEL sel = sel_registerName(buf);
	NODE *node = NEW_CFUNC(NULL, 1);
	NODE *body = NEW_FBODY(NEW_METHOD(node, klass, noex), 0);
	rb_objc_retain(body);

	rb_vm_prepare_method(klass, sel, f, body);
    }
}

static void
__rb_vm_define_method(Class klass, SEL sel, IMP imp,
		      const rb_vm_arity_t &arity, int flags, bool direct)
{
    assert(klass != NULL);

    const char *sel_name = sel_getName(sel);
    const bool genuine_selector = sel_name[strlen(sel_name) - 1] == ':';

    int oc_arity = genuine_selector ? arity.real : 0;
    bool redefined = direct;
    rb_vm_method_node_t *node = GET_VM()->method_node_get(imp);
    IMP ruby_imp = node == NULL ? imp : node->ruby_imp;

define_method:
    Method method = class_getInstanceMethod(klass, sel);

    char types[100];
    resolve_method_type(types, sizeof types, klass, method, sel, oc_arity);

    GET_VM()->add_method(klass, sel, imp, ruby_imp, arity, flags, types);

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
}

extern "C"
void 
rb_vm_define_method(Class klass, SEL sel, IMP imp, NODE *node, bool direct)
{
    assert(node != NULL);

    __rb_vm_define_method(klass, sel, imp, rb_vm_node_arity(node),
	    rb_vm_node_flags(node), direct);
}

extern "C"
void 
rb_vm_define_method2(Class klass, SEL sel, rb_vm_method_node_t *node,
		     bool direct)
{
    assert(node != NULL);

    __rb_vm_define_method(klass, sel, node->objc_imp, node->arity,
	    node->flags, direct);
}

extern "C"
void
rb_vm_undef_method(Class klass, const char *name, bool must_exist)
{
    rb_vm_method_node_t *node = NULL;
    SEL sel = sel_registerName(name);

    if (!rb_vm_lookup_method((Class)klass, sel, NULL, &node)) {
	if (must_exist) {
	    rb_raise(rb_eNameError, "undefined method `%s' for %s `%s'",
		    name, TYPE(klass) == T_MODULE ? "module" : "class",
		    name);
	}
	assert(name[strlen(name) - 1] != ':');
	class_replaceMethod((Class)klass, sel, NULL, "@@:");
    }

    if (node == NULL) {
	if (must_exist) {
	    rb_raise(rb_eRuntimeError,
		    "cannot undefine method `%s' because it is a native method",
		    name);
	}
	return; // Do nothing.
    }

    Method m = class_getInstanceMethod((Class)klass, node->sel);
    assert(m != NULL);
    class_replaceMethod((Class)klass, node->sel, NULL,
	    method_getTypeEncoding(m));
}

extern "C"
VALUE
rb_vm_masgn_get_elem_before_splat(VALUE ary, int offset)
{
    if (offset < RARRAY_LEN(ary)) {
	return OC2RB(CFArrayGetValueAtIndex((CFArrayRef)ary, offset));
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
	    return OC2RB(CFArrayGetValueAtIndex((CFArrayRef)ary, offset));
	}
    }
    else {
	offset += len - after_splat_count;
	return OC2RB(CFArrayGetValueAtIndex((CFArrayRef)ary, offset));
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
__rb_vm_fix_args(const VALUE *argv, VALUE *new_argv, const rb_vm_arity_t &arity, int argc)
{
    assert(argc >= arity.min);
    assert((arity.max == -1) || (argc <= arity.max));
    int used_opt_args = argc - arity.min;
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
__rb_vm_bcall(VALUE self, VALUE dvars, rb_vm_block_t *b,
	      IMP pimp, const rb_vm_arity_t &arity, int argc, const VALUE *argv)
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
	    return (*imp)(self, 0, dvars, b);
	case 1:
	    return (*imp)(self, 0, dvars, b, argv[0]);
	case 2:
	    return (*imp)(self, 0, dvars, b, argv[0], argv[1]);
	case 3:
	    return (*imp)(self, 0, dvars, b, argv[0], argv[1], argv[2]);
	case 4:
	    return (*imp)(self, 0, dvars, b, argv[0], argv[1], argv[2], argv[3]);
	case 5:
	    return (*imp)(self, 0, dvars, b, argv[0], argv[1], argv[2], argv[3], argv[4]);
	case 6:
	    return (*imp)(self, 0, dvars, b, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5]);
	case 7:
	    return (*imp)(self, 0, dvars, b, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5], argv[6]);
	case 8:
	    return (*imp)(self, 0, dvars, b, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5], argv[6], argv[7]);
	case 9:
	    return (*imp)(self, 0, dvars, b, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5], argv[6], argv[7], argv[8]);
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
    }	
    printf("invalid argc %d\n", argc);
    abort();
}

static inline Method
rb_vm_super_lookup(VALUE klass, SEL sel, VALUE *klassp)
{
    VALUE k, ary;
    int i, count;
    bool klass_located;

    ary = rb_mod_ancestors_nocopy(klass);

    void *callstack[128];
    int callstack_n = backtrace(callstack, 128);

    std::vector<void *> callstack_funcs;
    for (int i = 0; i < callstack_n; i++) {
	void *start = NULL;
	if (GET_VM()->symbolize_call_address(callstack[i],
		    &start, NULL, NULL, 0)) {
	    rb_vm_method_node_t *node = GET_VM()->method_node_get((IMP)start);
	    if (node != NULL && node->ruby_imp == start) {
		start = (void *)node->objc_imp;
	    }
	    callstack_funcs.push_back(start);
	}
    }

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

    count = RARRAY_LEN(ary);
    k = klass;
    klass_located = false;

    for (i = 0; i < count; i++) {
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

    const unsigned char last_call_status = GET_VM()->method_missing_reason;
    const char *format = NULL;
    VALUE exc = rb_eNoMethodError;

    switch (last_call_status) {
#if 0 // TODO
	case NOEX_PRIVATE:
	    format = "private method `%s' called for %s";
	    break;

	case NOEX_PROTECTED:
	    format = "protected method `%s' called for %s";
	    break;
#endif

	case DISPATCH_VCALL:
	    format = "undefined local variable or method `%s' for %s";
	    exc = rb_eNameError;
	    break;

	case DISPATCH_SUPER:
	    format = "super: no superclass method `%s' for %s";
	    break;

	default:
	    format = "undefined method `%s' for %s";
	    break;
    }

    int n = 0;
    VALUE args[3];
    args[n++] = rb_funcall(rb_cNameErrorMesg, '!', 3, rb_str_new2(format), obj,
	    argv[0]);
    args[n++] = argv[0];
    if (exc == rb_eNoMethodError) {
	args[n++] = rb_ary_new4(argc - 1, argv + 1);
    }

    exc = rb_class_new_instance(n, args, exc);
    rb_exc_raise(exc);

    abort(); // never reached
}

static VALUE
method_missing(VALUE obj, SEL sel, int argc, const VALUE *argv,
	       unsigned char call_status)
{
    GET_VM()->method_missing_reason = call_status;

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
    if (argc <= 1 && buf[n - 1] == ':') {
	buf[n - 1] = '\0';
    }
    new_argv[0] = ID2SYM(rb_intern(buf));
    MEMCPY(&new_argv[1], argv, VALUE, argc);

    return rb_vm_call(obj, selMethodMissing, argc + 1, new_argv, false);
}

inline void *
RoxorVM::gen_stub(std::string types, int argc, bool is_objc)
{
    std::map<std::string, void *> &stubs = is_objc ? objc_stubs : c_stubs;
    std::map<std::string, void *>::iterator iter = stubs.find(types);
    if (iter != stubs.end()) {
	return iter->second;
    }

    Function *f = RoxorCompiler::shared->compile_stub(types.c_str(), argc,
	    is_objc);
    void *stub = (void *)compile(f);
    stubs.insert(std::make_pair(types, stub));

    return stub;
}

void *
RoxorVM::gen_to_rval_convertor(std::string type)
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
RoxorVM::gen_to_ocval_convertor(std::string type)
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
		      int argc, const VALUE *argv)
{
    const rb_vm_arity_t &arity = node->arity;
    if ((argc < arity.min) || ((arity.max != -1) && (argc > arity.max))) {
	rb_raise(rb_eArgError, "wrong number of arguments (%d for %d)",
		argc, arity.min);
    }

    if (rb_vm_method_node_empty(node) && arity.max == arity.min) {
	// Calling an empty method, let's just return nil!
	return Qnil;
    }
    if (rb_vm_method_node_type(node) == NODE_FBODY && arity.max != arity.min) {
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
fill_rcache(struct mcache *cache, Class klass, rb_vm_method_node_t *node)
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
    ocache.bs_method = GET_VM()->find_bs_method(klass, sel);

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
    ocache.stub = (rb_vm_objc_stub_t *)GET_VM()->gen_stub(types, 
	    argc, true);
}

static force_inline VALUE
__rb_vm_dispatch(struct mcache *cache, VALUE self, Class klass, SEL sel,
		 rb_vm_block_t *block, unsigned char opt, int argc,
		 const VALUE *argv)
{
    assert(cache != NULL);

    if (klass == NULL) {
	klass = (Class)CLASS_OF(self);
    }

#if ROXOR_VM_DEBUG
    const bool cached = cache->flag != 0;
#endif
    bool do_rcache = true;

    if (cache->flag == 0) {
recache:
	Method method;
	if (opt == DISPATCH_SUPER) {
	    method = rb_vm_super_lookup((VALUE)klass, sel, NULL);
	}
	else {
	    method = class_getInstanceMethod(klass, sel);
	}

	if (method != NULL) {
recache2:
	    IMP imp = method_getImplementation(method);

	    if (imp == NULL) {
		// Method was undefined.
		goto call_method_missing;
	    }

	    rb_vm_method_node_t *node = GET_VM()->method_node_get(imp);

	    if (node != NULL) {
		// ruby call
		fill_rcache(cache, klass, node);
	    }
	    else {
		// objc call
		fill_ocache(cache, self, klass, imp, sel, method, argc);
	    }
	}
	else {
	    // Method is not found...

	    // Does the receiver implements -forwardInvocation:?
	    if (can_forwardInvocation(self, sel)) {
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
		    IMP imp = method_getImplementation(m);
		    if (rb_vm_get_method_node(imp) == NULL) {
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
	    std::map<std::string, bs_element_function_t *>::iterator iter =
		GET_VM()->bs_funcs.find(name);
	    if (iter != GET_VM()->bs_funcs.end()) {
		bs_element_function_t *bs_func = iter->second;
		std::string types;
		vm_gen_bs_func_types(bs_func, types);

		cache->flag = MCACHE_FCALL;
		fcache.bs_function = bs_func;
		fcache.imp = (IMP)dlsym(RTLD_DEFAULT, bs_func->name);
		assert(fcache.imp != NULL);
		fcache.stub = (rb_vm_c_stub_t *)GET_VM()->gen_stub(types,
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

	bool block_already_current = GET_VM()->is_block_current(block);
	if (!block_already_current) {
	    GET_VM()->add_current_block(block);
	}

	VALUE ret = Qnil;
	try {
	    ret = __rb_vm_ruby_dispatch(self, sel, rcache.node, argc, argv);
	}
	catch (...) {
	    if (!block_already_current) {
		GET_VM()->pop_current_block();
	    }
	    throw;
	}
	if (!block_already_current) {
	    GET_VM()->pop_current_block();
	}
	return ret;
    }
    else if (cache->flag == MCACHE_OCALL) {
	if (ocache.klass != klass) {
	    goto recache;
	}

	if (block != NULL) {
	    if (self == rb_cNSMutableHash && sel == selNew) {
		// Because Hash.new can accept a block.
		GET_VM()->add_current_block(block);
		VALUE h = Qnil;
		try {
		    h = rb_hash_new2(argc, argv);
		}
		catch (...) {
		    GET_VM()->pop_current_block();
		    throw;
		}
		GET_VM()->pop_current_block();
		return h;
	    }
	    rb_warn("passing a block to an Objective-C method - " \
		    "will be ignored");
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
	    && GET_VM()->method_node_get(method_getImplementation(m))
	    != NULL) {
	    rb_raise(rb_eArgError, "wrong number of arguments (%d for %d)",
		    argc, argc_expected);
	}
    }

    return method_missing((VALUE)self, sel, argc, argv, opt);
}

#define MAX_DISPATCH_ARGS 200

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
	argc = __rb_vm_resolve_args(argv, argc, ar);
	va_end(ar);
    }

    VALUE retval = __rb_vm_dispatch(cache, self, NULL, sel, block, opt, argc,
	    argv);

    if (!GET_VM()->bindings.empty()) {
	rb_objc_release(GET_VM()->bindings.back());
	GET_VM()->bindings.pop_back();
    }

    return retval;
}

extern "C"
VALUE
rb_vm_fast_eqq(struct mcache *cache, VALUE self, VALUE comparedTo)
{
    // This function does not check if === has been or not redefined
    // so it should only been called by code generated by
    // compile_optimized_dispatch_call().
    // Fixnums are already handled in compile_optimized_dispatch_call
    switch (TYPE(self)) {
	// TODO: Range
	case T_STRING:
	    if (self == comparedTo) {
		return Qtrue;
	    }
	    return rb_str_equal(self, comparedTo);

	case T_REGEXP:
	    return rb_reg_eqq(self, selEqq, comparedTo);

	case T_SYMBOL:
	    return (self == comparedTo ? Qtrue : Qfalse);
	
	case T_MODULE:
	case T_CLASS:
	    return rb_obj_is_kind_of(comparedTo, self);

	default:
	    return rb_vm_dispatch(cache, self, selEqq, NULL, 0, 1, comparedTo);
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
	    if (RTEST(rb_vm_fast_eqq(cache, comparedTo, RARRAY_AT(ary, i)))) {
		return Qtrue;
	    }
	}
    }
    else {
	for (int i = 0; i < count; ++i) {
	    VALUE o = RARRAY_AT(ary, i);
	    if (RTEST(rb_vm_dispatch(cache, comparedTo, selEqq, NULL, 0, 1, &o))) {
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
    return __rb_vm_dispatch(cache, obj, NULL, selLTLT, NULL, 0, 1, &other);
}

extern "C"
VALUE
rb_vm_fast_aref(VALUE obj, VALUE other, struct mcache *cache,
		unsigned char overriden)
{
    // TODO what about T_HASH?
    if (overriden == 0 && TYPE(obj) == T_ARRAY) {
	if (TYPE(other) == T_FIXNUM) {
	    return rb_ary_entry(obj, FIX2INT(other));
	}
	extern VALUE rb_ary_aref(VALUE ary, SEL sel, int argc, VALUE *argv);
	return rb_ary_aref(obj, 0, 1, &other);
    }
    return __rb_vm_dispatch(cache, obj, NULL, selAREF, NULL, 0, 1, &other);
}

extern "C"
VALUE
rb_vm_fast_aset(VALUE obj, VALUE other1, VALUE other2, struct mcache *cache,
		unsigned char overriden)
{
    // TODO what about T_HASH?
    if (overriden == 0 && TYPE(obj) == T_ARRAY) {
	if (TYPE(other1) == T_FIXNUM) {
	    rb_ary_store(obj, FIX2INT(other1), other2);
	    return other2;
	}
    }
    VALUE args[2];
    args[0] = other1;
    args[1] = other2;
    return __rb_vm_dispatch(cache, obj, NULL, selASET, NULL, 0, 2, args);
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
rb_vm_prepare_block(void *llvm_function, NODE *node, VALUE self,
		    rb_vm_var_uses **parent_var_uses,
		    rb_vm_block_t *parent_block,
		    int dvars_size, ...)
{
    NODE *cache_key;
    if (nd_type(node) == NODE_IFUNC) {
	// In this case, node is dynamic but fortunately u1.node is always
	// unique (it contains the IMP)
	cache_key = node->u1.node;
    }
    else {
	cache_key = node;
    }

    std::map<NODE *, rb_vm_block_t *>::iterator iter =
	GET_VM()->blocks.find(cache_key);

    rb_vm_block_t *b;
    bool cached = false;

    if ((iter == GET_VM()->blocks.end())
	|| (iter->second->flags & (VM_BLOCK_ACTIVE | VM_BLOCK_PROC))) {

	if (iter != GET_VM()->blocks.end()) {
	    rb_objc_release(iter->second);
	}

	b = (rb_vm_block_t *)xmalloc(sizeof(rb_vm_block_t)
		+ (sizeof(VALUE *) * dvars_size));

	if (nd_type(node) == NODE_IFUNC) {
	    assert(llvm_function == NULL);
	    b->imp = (IMP)node->u1.node;
	    memset(&b->arity, 0, sizeof(rb_vm_arity_t)); // not used
	}
	else {
	    assert(llvm_function != NULL);
	    b->imp = GET_VM()->compile((Function *)llvm_function);
	    b->arity = rb_vm_node_arity(node);
	}
	b->flags = 0;
	b->dvars_size = dvars_size;
	b->parent_var_uses = NULL;
	b->parent_block = NULL;

	rb_objc_retain(b);
	GET_VM()->blocks[cache_key] = b;
    }
    else {
	b = iter->second;
	assert(b->dvars_size == dvars_size);
	cached = true;
    }

    b->self = self;
    b->node = node;
    b->parent_var_uses = parent_var_uses;
    b->parent_block = parent_block;

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
void*
rb_gc_read_weak_ref(void **referrer);

extern "C"
void
rb_gc_assign_weak_ref(const void *value, void *const*location);

static const int VM_LVAR_USES_SIZE = 8;
struct rb_vm_var_uses {
    int uses_count;
    void *uses[VM_LVAR_USES_SIZE];
    struct rb_vm_var_uses *next;
};

extern "C"
void
rb_vm_add_var_use(rb_vm_block_t *block)
{
    for (rb_vm_block_t *block_for_uses = block;
	 block_for_uses != NULL;
	 block_for_uses = block_for_uses->parent_block) {

	rb_vm_var_uses **var_uses = block_for_uses->parent_var_uses;
	if (var_uses == NULL) {
	    continue;
	}
	if ((*var_uses == NULL)
	    || ((*var_uses)->uses_count == VM_LVAR_USES_SIZE)) {

	    rb_vm_var_uses* new_uses =
		(rb_vm_var_uses*)malloc(sizeof(rb_vm_var_uses));
	    new_uses->next = *var_uses;
	    new_uses->uses_count = 0;
	    *var_uses = new_uses;
	}
	int current_index = (*var_uses)->uses_count;
	rb_gc_assign_weak_ref(block, &(*var_uses)->uses[current_index]);
	++(*var_uses)->uses_count;
    }

    // we should not keep references that won't be used
    block->parent_block = NULL;
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
	    rb_vm_block_t *block = (rb_vm_block_t *)rb_gc_read_weak_ref(&current->uses[use_index]);
	    if (block != NULL) {
		for (int dvar_index=0; dvar_index < block->dvars_size; ++dvar_index) {
		    for (int lvar_index=0; lvar_index < lvars_size; ++lvar_index) {
			if (block->dvars[dvar_index] == locals[lvar_index].stack_address) {
			    GC_WB(&block->dvars[dvar_index], locals[lvar_index].new_address);
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

extern "C"
void
rb_vm_push_binding(VALUE self, int lvars_size, ...)
{
    rb_vm_binding_t *b = (rb_vm_binding_t *)xmalloc(sizeof(rb_vm_binding_t));
    GC_WB(&b->self, self);

    va_list ar;
    va_start(ar, lvars_size);
    rb_vm_local_t **l = &b->locals;
    for (int i = 0; i < lvars_size; ++i) {
	GC_WB(l, xmalloc(sizeof(rb_vm_local_t)));
	(*l)->name = va_arg(ar, ID);
	(*l)->value = va_arg(ar, VALUE *);
	(*l)->next = NULL;
	l = &(*l)->next;
    }
    va_end(ar);

    rb_objc_retain(b);
    GET_VM()->bindings.push_back(b);
}

extern "C"
rb_vm_binding_t *
rb_vm_current_binding(void)
{
   return GET_VM()->bindings.empty() ? NULL : GET_VM()->bindings.back();
}

extern "C"
void
rb_vm_add_binding(rb_vm_binding_t *binding)
{
    GET_VM()->bindings.push_back(binding);
}

extern "C"
void
rb_vm_pop_binding(void)
{
    GET_VM()->bindings.pop_back();
}

extern "C"
VALUE
rb_vm_call(VALUE self, SEL sel, int argc, const VALUE *argv, bool super)
{
    struct mcache *cache;
    unsigned char flg = 0;
    if (super) {
	cache = (struct mcache *)alloca(sizeof(struct mcache));
	cache->flag = 0;
	flg = DISPATCH_SUPER;
    }
    else {
	cache = GET_VM()->method_cache_get(sel, false);
    }

    return __rb_vm_dispatch(cache, self, NULL, sel, NULL, flg, argc, argv);
}

extern "C"
VALUE
rb_vm_call_with_cache(void *cache, VALUE self, SEL sel, int argc, 
		      const VALUE *argv)
{
    return __rb_vm_dispatch((struct mcache *)cache, self, NULL, sel, NULL, 0,
	    argc, argv);
}

extern "C"
VALUE
rb_vm_call_with_cache2(void *cache, rb_vm_block_t *block, VALUE self,
		       VALUE klass, SEL sel, int argc, const VALUE *argv)
{
    return __rb_vm_dispatch((struct mcache *)cache, self, (Class)klass, sel,
	    block, 0, argc, argv);
}

extern "C"
void *
rb_vm_get_call_cache(SEL sel)
{
    return GET_VM()->method_cache_get(sel, false);
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

extern "C" VALUE rb_proc_alloc_with_block(VALUE klass, rb_vm_block_t *proc);

extern "C"
VALUE
rb_vm_current_block_object(void)
{
    rb_vm_block_t *b = GET_VM()->current_block();
    if (b != NULL) {
#if ROXOR_VM_DEBUG
	printf("create Proc object based on block %p\n", b);
#endif
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
    if (node == NULL) {
	arity = method_getNumberOfArguments(method) - 2;
    }
    else {
	arity = node->arity.min;
	if (node->arity.min != node->arity.max) {
	    arity = -arity - 1;
	}
    }

    rb_vm_method_t *m = (rb_vm_method_t *)xmalloc(sizeof(rb_vm_method_t));

    m->oclass = (VALUE)oklass;
    m->rclass = klass;
    GC_WB(&m->recv, obj);
    m->sel = sel;
    m->arity = arity;
    m->node = node;

    // Let's allocate a static cache here, since a rb_vm_method_t must always
    // point to the method it was created from.
    struct mcache *c = (struct mcache *)xmalloc(sizeof(struct mcache));
    if (node == NULL) {
	fill_ocache(c, obj, oklass, imp, sel, method, arity);
    }
    else {
	rb_vm_method_node_t *node = GET_VM()->method_node_get(imp);
	assert(node != NULL);
	fill_rcache(c, oklass, node);
    }
    GC_WB(&m->cache, c);

    return m;
}

extern "C"
rb_vm_block_t *
rb_vm_create_block_from_method(rb_vm_method_t *method)
{
    rb_vm_block_t *b = (rb_vm_block_t *)xmalloc(sizeof(rb_vm_block_t));

    GC_WB(&b->self, method->recv);
    b->node = NULL;
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

static inline VALUE
rb_vm_block_eval0(rb_vm_block_t *b, int argc, const VALUE *argv)
{
    if (b->node != NULL) {
	if (nd_type(b->node) == NODE_IFUNC) {
	    // Special case for blocks passed with rb_objc_block_call(), to
	    // preserve API compatibility.
	    VALUE data = (VALUE)b->node->u2.node;

	    VALUE (*pimp)(VALUE, VALUE, int, const VALUE *) =
		(VALUE (*)(VALUE, VALUE, int, const VALUE *))b->imp;

	    return (*pimp)(argc == 0 ? Qnil : argv[0], data, argc, argv);
	}
	else if (nd_type(b->node) == NODE_SCOPE) {
	    if (b->node->nd_body == NULL) {
		// Trying to call an empty block!
		return Qnil;
	    }
	}
    }

    rb_vm_arity_t arity = b->arity;    

    if (argc < arity.min || argc > arity.max) {
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

    assert(!(b->flags & VM_BLOCK_ACTIVE));
    b->flags |= VM_BLOCK_ACTIVE;
    VALUE v = Qnil;
    try {
	if (b->flags & VM_BLOCK_METHOD) {
	    rb_vm_method_t *m = (rb_vm_method_t *)b->imp;
	    v = rb_vm_call_with_cache2(m->cache, NULL, m->recv, m->oclass,
		    m->sel, argc, argv);
	}
	else {
	    v = __rb_vm_bcall(b->self, (VALUE)b->dvars, b, b->imp, b->arity,
		    argc, argv);
	}
    }
    catch (...) {
	b->flags &= ~VM_BLOCK_ACTIVE;
	throw;
    }
    b->flags &= ~VM_BLOCK_ACTIVE;

    return v;
}

extern "C"
VALUE
rb_vm_block_eval(rb_vm_block_t *b, int argc, const VALUE *argv)
{
    return rb_vm_block_eval0(b, argc, argv);
}

static inline VALUE
rb_vm_yield0(int argc, const VALUE *argv)
{
    rb_vm_block_t *b = GET_VM()->current_block();
    if (b == NULL) {
	rb_raise(rb_eLocalJumpError, "no block given");
    }

    GET_VM()->pop_current_block();

    VALUE retval = Qnil;
    try {
	retval = rb_vm_block_eval0(b, argc, argv);
    }
    catch (...) {
	GET_VM()->add_current_block(b);
	throw;
    }

    GET_VM()->add_current_block(b);

    return retval;
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
    rb_vm_block_t *b = GET_VM()->current_block();
    GET_VM()->pop_current_block();

    VALUE old_self = b->self;
    b->self = self;
    //Class old_class = GET_VM()->current_class;
    //GET_VM()->current_class = (Class)klass;

    VALUE retval = Qnil;
    try {
	retval = rb_vm_block_eval0(b, argc, argv);
    }
    catch (...) {
	b->self = old_self;
	//GET_VM()->current_class = old_class;
	GET_VM()->add_current_block(b);
	throw;
    }

    b->self = old_self;
    //GET_VM()->current_class = old_class;
    GET_VM()->add_current_block(b);

    return retval;
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

extern "C"
bool
rb_vm_respond_to(VALUE obj, SEL sel, bool priv)
{
    VALUE klass = CLASS_OF(obj);

    IMP respond_to_imp = class_getMethodImplementation((Class)klass,
	    selRespondTo);

    if (respond_to_imp == basic_respond_to_imp) {
	Method m = class_getInstanceMethod((Class)klass, sel);
	bool reject_pure_ruby_methods = false;
	if (m == NULL) {
	    const char *selname = sel_getName(sel);
	    sel = helper_sel(selname, strlen(selname));
	    if (sel != NULL) {
		m = class_getInstanceMethod((Class)klass, sel);
		if (m == NULL) {
		    return false;
		}
		reject_pure_ruby_methods = true;
	    }
	}
	IMP obj_imp = method_getImplementation(m);
	rb_vm_method_node_t *node = obj_imp == NULL
	    ? NULL : GET_VM()->method_node_get(obj_imp);

	if (node != NULL
		&& (reject_pure_ruby_methods
		    || (priv == 0
			&& (rb_vm_method_node_noex(node) & NOEX_PRIVATE)))) {
	    return false;
	}
        return obj_imp != NULL;
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
#if 1
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
    GET_VM()->broken_with = val;
}

extern "C"
VALUE
rb_vm_pop_broken_value(void)
{
    VALUE val = GET_VM()->broken_with;
    GET_VM()->broken_with = Qundef;
    return val;
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
	char name[100];
	unsigned long ln = 0;

	if (GET_VM()->symbolize_call_address(callstack[i], NULL, &ln, name,
					     sizeof name)) {
	    char entry[100];
	    snprintf(entry, sizeof entry, "%ld:in `%s'", ln, name);
	    rb_ary_push(ary, rb_str_new2(entry));
	}
    }

    return ary;
}

extern "C"
void
rb_vm_rethrow(void)
{
    void *exc = __cxa_allocate_exception(0);
    __cxa_throw(exc, NULL, NULL);
}

extern "C"
unsigned char
rb_vm_is_eh_active(int argc, ...)
{
    assert(argc > 0);

    VALUE current_exception = GET_VM()->current_exception();
    assert(current_exception != Qnil);

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
	printf("uncatched Objective-C/C++ exception...\n");
	return;
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
    return GET_VM()->parse_in_eval;
}

extern "C"
void
rb_vm_set_parse_in_eval(bool flag)
{
    GET_VM()->parse_in_eval = flag;
}

extern "C"
int
rb_parse_in_eval(void)
{
    return rb_vm_parse_in_eval() ? 1 : 0;
}

extern "C"
int
rb_local_defined(ID id)
{
    return GET_VM()->get_binding_lvar(id) != NULL ? 1 : 0;
}

extern "C"
int
rb_dvar_defined(ID id)
{
    // TODO
    return 0;
}

static inline void
__init_shared_compiler(void)
{
    if (RoxorCompiler::shared == NULL) {
	RoxorCompiler::shared = ruby_aot_compile
	    ? new RoxorAOTCompiler("")
	    : new RoxorCompiler("");
    }
}

extern "C"
VALUE
rb_vm_run(const char *fname, NODE *node, rb_vm_binding_t *binding,
	  bool try_interpreter)
{
    if (binding != NULL) {
	GET_VM()->bindings.push_back(binding);
    }

    __init_shared_compiler();
    Function *function = RoxorCompiler::shared->compile_main_function(node);

    if (binding != NULL) {
	GET_VM()->bindings.pop_back();
    }

#if ROXOR_INTERPRET_EVAL
    if (try_interpreter) {
	return GET_VM()->interpret(function);
    }
    else {
	IMP imp = GET_VM()->compile(function);
	return ((VALUE(*)(VALUE, SEL))imp)(GET_VM()->current_top_object, 0);
    }
#else
    IMP imp = GET_VM()->compile(function);
    return ((VALUE(*)(VALUE, SEL))imp)(GET_VM()->current_top_object, 0);
#endif
}

extern "C"
VALUE
rb_vm_run_under(VALUE klass, VALUE self, const char *fname, NODE *node,
		rb_vm_binding_t *binding, bool try_interpreter)
{
    VALUE old_top_object = GET_VM()->current_top_object;
    if (binding != NULL) {
	self = binding->self;
    }
    if (self != 0) {
	GET_VM()->current_top_object = self;
    }
    Class old_class = GET_VM()->current_class;
    if (klass != 0) {
	GET_VM()->current_class = (Class)klass;
    }

    VALUE val = rb_vm_run(fname, node, binding, try_interpreter);

    GET_VM()->current_top_object = old_top_object;
    GET_VM()->current_class = old_class;

    return val;
}

extern "C"
void
rb_vm_aot_compile(NODE *node)
{
    assert(ruby_aot_compile);

    // Compile the program as IR.
    __init_shared_compiler();
    Function *f = RoxorCompiler::shared->compile_main_function(node);
    f->setName("rb_main");
    GET_VM()->optimize(f);

    // Save the bitcode into a temporary file.
    const char *tmpdir = getenv("TMPDIR");
    assert(tmpdir != NULL);
    std::string bc_path(tmpdir);
    bc_path.append("ruby.bc");
    std::ofstream out(bc_path.c_str());
    WriteBitcodeToFile(RoxorCompiler::module, out);
    out.close();

    // Compile the bitcode as assembly.
    std::string llc_line("llc -f ");
    llc_line.append(bc_path);
    llc_line.append(" -o=");
    std::string as_path(tmpdir);
    as_path.append("ruby.s");
    llc_line.append(as_path);
    if (system(llc_line.c_str()) != 0) {
	printf("error when compiling bitcode as assembly\n" \
	       "line was: %s", llc_line.c_str());
	exit(1);
    }

    // Compile the assembly.
    std::string gcc_line("gcc -c -arch x86_64 ");
    gcc_line.append(as_path);
    std::string o_path(tmpdir);
    o_path.append("ruby.o");
    gcc_line.append(" -o ");
    gcc_line.append(o_path);
    if (system(gcc_line.c_str()) != 0) {
	printf("error when compiling assembly as machine code\n" \
	       "line was: %s", gcc_line.c_str());
	exit(1);
    }

    // Compile main function.
    std::string main_path(tmpdir);
    main_path.append("main.c");
    std::ofstream main_file(main_path.c_str());
    std::string main_content(
"extern void ruby_sysinit(int *, char ***);\n"
"extern void ruby_init(void);\n"
"extern void ruby_set_argv(int, char **);\n"
"extern void *rb_vm_top_self(void);\n"
"extern void *rb_main(void *, void *);\n"
"\n"
"int main(int argc, char **argv)\n"
"{\n"
"    ruby_sysinit(&argc, &argv);\n"
"    if (argc > 0) {\n"
"	argc--; argv++;\n"
"    }\n"
"    ruby_init();\n"
"    ruby_set_argv(argc, argv);\n"
"    rb_main(rb_vm_top_self(), 0);\n"
"    return 1;\n"
"}\n"
);
    main_file.write(main_content.c_str(), main_content.size());
    main_file.close();
    gcc_line.clear();
    gcc_line.append("gcc ");
    gcc_line.append(main_path);
    gcc_line.append(" -c -arch x86_64 -o ");
    std::string main_o_path(tmpdir);
    main_o_path.append("main.o");
    gcc_line.append(main_o_path);
    if (system(gcc_line.c_str()) != 0) {
	printf("error when compiling main function as machine code\n" \
	       "line was: %s", gcc_line.c_str());
	exit(1);
    }

    // Link everything!
    std::string out_path("a.out");
    gcc_line.clear();
    gcc_line.append("g++ ");
    gcc_line.append(o_path);
    gcc_line.append(" -o ");
    gcc_line.append(out_path);
    // XXX -L. should be removed later
    gcc_line.append(" -L. -lmacruby-static -arch x86_64 -framework Foundation -lobjc -lauto -I/usr/include/libxml2 -lxml2 `llvm-config --ldflags --libs core jit nativecodegen interpreter bitwriter` ");
    gcc_line.append(main_o_path);
    if (system(gcc_line.c_str()) != 0) {
	printf("error when compiling main function as machine code\n" \
	       "line was: %s", gcc_line.c_str());
	exit(1);
    }

    //RoxorCompiler::module->dump();
}

extern "C"
VALUE
rb_vm_top_self(void)
{
    return GET_VM()->current_top_object;
}

extern "C"
VALUE
rb_vm_loaded_features(void)
{
    return GET_VM()->loaded_features;
}

extern "C"
VALUE
rb_vm_load_path(void)
{
    return GET_VM()->load_path;
}

extern "C"
int
rb_vm_safe_level(void)
{
    return GET_VM()->safe_level;
}

extern "C"
void 
rb_vm_set_safe_level(int level)
{
    GET_VM()->safe_level = level;
}

extern "C"
VALUE
rb_last_status_get(void)
{
    return GET_VM()->last_status;
}

extern "C"
void
rb_last_status_set(int status, rb_pid_t pid)
{
    if (GET_VM()->last_status != Qnil) {
	rb_objc_release((void *)GET_VM()->last_status);
    }
    VALUE last_status;
    if (pid == -1) {
	last_status = Qnil;
    }
    else {
	last_status = rb_obj_alloc(rb_cProcessStatus);
	rb_iv_set(last_status, "status", INT2FIX(status));
	rb_iv_set(last_status, "pid", PIDT2NUM(pid));
	rb_objc_retain((void *)last_status);
    }
    GET_VM()->last_status = last_status;
}

extern "C"
VALUE
rb_errinfo(void)
{
    return GET_VM()->errinfo;
}

void
rb_set_errinfo(VALUE err)
{
    if (!NIL_P(err) && !rb_obj_is_kind_of(err, rb_eException)) {
        rb_raise(rb_eTypeError, "assigning non-exception to $!");
    }
    if (GET_VM()->errinfo != Qnil) {
	rb_objc_release((void *)GET_VM()->errinfo);
    }
    GET_VM()->errinfo = err;
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
    GET_VM()->broken_with = Qnil;
}

extern "C"
VALUE
rb_backref_get(void)
{
    return GET_VM()->backref;
}

extern "C"
void
rb_backref_set(VALUE val)
{
    VALUE old = GET_VM()->backref;
    if (old != val) {
	rb_objc_release((void *)old);
	GET_VM()->backref = val;
	rb_objc_retain((void *)val);
    }
}

extern "C"
VALUE
rb_vm_catch(VALUE tag)
{
    std::map<VALUE, rb_vm_catch_t *>::iterator iter =
	GET_VM()->catch_jmp_bufs.find(tag);
    rb_vm_catch_t *s = NULL;
    if (iter == GET_VM()->catch_jmp_bufs.end()) {
	s = (rb_vm_catch_t *)malloc(sizeof(rb_vm_catch_t));
	s->throw_value = Qnil;
	s->nested = 1;
	GET_VM()->catch_jmp_bufs[tag] = s;
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

    iter = GET_VM()->catch_jmp_bufs.find(tag);
    assert(iter != GET_VM()->catch_jmp_bufs.end());
    s->nested--;
    if (s->nested == 0) {
	s = iter->second;
	free(s);
	GET_VM()->catch_jmp_bufs.erase(iter);
	rb_objc_release((void *)tag);
    }

    return retval;
}

extern "C"
VALUE
rb_vm_throw(VALUE tag, VALUE value)
{
    std::map<VALUE, rb_vm_catch_t *>::iterator iter =
	GET_VM()->catch_jmp_bufs.find(tag);
    if (iter == GET_VM()->catch_jmp_bufs.end()) {
        VALUE desc = rb_inspect(tag);
        rb_raise(rb_eArgError, "uncaught throw %s", RSTRING_PTR(desc));
    }
    rb_vm_catch_t *s = iter->second;

    rb_objc_retain((void *)value);
    s->throw_value = value;

    longjmp(s->buf, 1);

    return Qnil; // never reached
}

static VALUE
builtin_ostub1(IMP imp, id self, SEL sel, int argc, VALUE *argv)
{
    return OC2RB(((id (*)(id, SEL))*imp)(self, sel));
}

static void
setup_builtin_stubs(void)
{
    GET_VM()->insert_stub("@@:", (void *)builtin_ostub1, true);
    GET_VM()->insert_stub("#@:", (void *)builtin_ostub1, true);
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
    RoxorVM::current = new RoxorVM();

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

extern "C"
void
Init_VM(void)
{
    rb_cTopLevel = rb_define_class("TopLevel", rb_cObject);
    rb_objc_define_method(rb_cTopLevel, "to_s", (void *)rb_toplevel_to_s, 0);

    GET_VM()->current_class = NULL;

    VALUE top_self = rb_obj_alloc(rb_cTopLevel);
    rb_objc_retain((void *)top_self);
    GET_VM()->current_top_object = top_self;
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
	    GET_VM()->functions_compiled);
#endif
}
