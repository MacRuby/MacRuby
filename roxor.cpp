/* ROXOR: the new MacRuby VM that rocks! */

#define ROXOR_COMPILER_DEBUG	0
#define ROXOR_VM_DEBUG		0
#define ROXOR_DUMP_IR		0

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
#include <llvm/Transforms/Scalar.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/IRBuilder.h>
#include <llvm/Intrinsics.h>
using namespace llvm;

#include <stack>

#if ROXOR_COMPILER_DEBUG
# include <mach/mach.h>
# include <mach/mach_time.h>
#endif

#include "ruby/ruby.h"
#include "ruby/node.h"
#include "id.h"
#include "objc.h"
#include "roxor.h"
#include <execinfo.h>

extern "C" const char *ruby_node_name(int node);

#define DISPATCH_VCALL 1
#define DISPATCH_SUPER 2

static VALUE rb_cTopLevel = 0;

struct RoxorFunction
{
    Function *f;
    unsigned char *start;
    unsigned char *end;
    void *imp;
    ID mid;

    RoxorFunction (Function *_f, unsigned char *_start, unsigned char *_end) {
	f = _f;
	start = _start;
	end = _end;
	imp = NULL; 	// lazy
	mid = 0;	// lazy
    }
};

class RoxorJITManager : public JITMemoryManager
{
    private:
        JITMemoryManager *mm;
	std::vector<struct RoxorFunction *> functions;

    public:
	RoxorJITManager() : JITMemoryManager() { 
	    mm = CreateDefaultMemManager(); 
	}

	struct RoxorFunction *find_function(unsigned char *addr) {
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
			       unsigned char *TableEnd, 
			       unsigned char* FrameRegister) {
	    mm->endExceptionTable(F, TableStart, TableEnd, FrameRegister);
	}
};

#define SPLAT_ARG_FOLLOWS 0xdeadbeef

class RoxorCompiler
{
    public:
	static llvm::Module *module;

	RoxorCompiler(const char *fname);

	Value *compile_node(NODE *node);

	Function *compile_main_function(NODE *node);
	Function *compile_read_attr(ID name);
	Function *compile_write_attr(ID name);

    private:
	const char *fname;

	std::map<ID, Value *> lvars;
	std::map<ID, Value *> dvars;
	std::map<ID, int *> ivar_slots_cache;

#if ROXOR_COMPILER_DEBUG
	int level;
# define DEBUG_LEVEL_INC() (level++)
# define DEBUG_LEVEL_DEC() (level--)
#else
# define DEBUG_LEVEL_INC()
# define DEBUG_LEVEL_DEC()
#endif

	BasicBlock *bb;
	BasicBlock *entry_bb;
	ID current_mid;
	bool current_instance_method;
	ID self_id;
	Value *current_self;
	bool current_block;
	BasicBlock *begin_bb;
	BasicBlock *rescue_bb;
	NODE *current_block_node;
	Function *current_block_func;
        GlobalVariable *current_opened_class;
	BasicBlock *current_loop_begin_bb;
	BasicBlock *current_loop_body_bb;
	BasicBlock *current_loop_end_bb;
	Value *current_loop_exit_val;

	Function *dispatcherFunc;
	Function *fastEqqFunc;
	Function *whenSplatFunc;
	Function *prepareBlockFunc;
	Function *getBlockFunc;
	Function *currentBlockObjectFunc;
	Function *getConstFunc;
	Function *setConstFunc;
	Function *prepareMethodFunc;
	Function *singletonClassFunc;
	Function *defineClassFunc;
	Function *prepareIvarSlotFunc;
	Function *getIvarFunc;
	Function *setIvarFunc;
	Function *definedFunc;
	Function *undefFunc;
	Function *aliasFunc;
	Function *valiasFunc;
	Function *newHashFunc;
	Function *toArrayFunc;
	Function *catArrayFunc;
	Function *dupArrayFunc;
	Function *newArrayFunc;
	Function *newRangeFunc;
	Function *newRegexpFunc;
	Function *strInternFunc;
	Function *rhsnGetFunc;
	Function *newStringFunc;
	Function *yieldFunc;
	Function *gvarSetFunc;
	Function *gvarGetFunc;
	Function *cvarSetFunc;
	Function *cvarGetFunc;
	Function *popExceptionFunc;
	Function *getSpecialFunc;
	Function *breakFunc;

	Constant *zeroVal;
	Constant *oneVal;
	Constant *twoVal;
	Constant *nilVal;
	Constant *trueVal;
	Constant *falseVal;
	Constant *undefVal;
	Constant *splatArgFollowsVal;
	const Type *RubyObjTy; 
	const Type *RubyObjPtrTy; 
	const Type *PtrTy;
	const Type *IntTy;

	void compile_node_error(const char *msg, NODE *node) {
	    int t = nd_type(node);
	    printf("%s: %d (%s)", msg, t, ruby_node_name(t));
	    abort();
	}

	Instruction *compile_const_pointer(void *ptr, bool insert_to_bb=true) {
	    Value *ptrint = ConstantInt::get(IntTy, (long)ptr);
	    return insert_to_bb
		? new IntToPtrInst(ptrint, PtrTy, "", bb)
		: new IntToPtrInst(ptrint, PtrTy, "");
	}

	Value *compile_protected_call(Function *func, std::vector<Value *> &params);
	void compile_dispatch_arguments(NODE *args, std::vector<Value *> &arguments, int *pargc);
	Function::ArgumentListType::iterator
	    compile_optional_arguments(Function::ArgumentListType::iterator iter,
		    NODE *node);
	void compile_boolean_test(Value *condVal, BasicBlock *ifTrueBB, BasicBlock *ifFalseBB);
	void compile_when_arguments(NODE *args, Value *comparedToVal, BasicBlock *thenBB);
	void compile_single_when_argument(NODE *arg, Value *comparedToVal, BasicBlock *thenBB);
	Value *compile_dispatch_call(std::vector<Value *> &params);
	Value *compile_when_splat(Value *comparedToVal, Value *splatVal);
	Value *compile_fast_eqq_call(Value *selfVal, Value *comparedToVal);
	Value *compile_attribute_assign(NODE *node, Value *extra_val);
	Value *compile_block_create(NODE *node=NULL);
	Value *compile_optimized_dispatch_call(SEL sel, int argc, std::vector<Value *> &params);
	Value *compile_ivar_read(ID vid);
	Value *compile_ivar_assignment(ID vid, Value *val);
	Value *compile_current_class(void);
	Value *compile_class_path(NODE *node);
	Value *compile_const(ID id, Value *outer);
	Value *compile_singleton_class(Value *obj);
	Value *compile_defined_expression(NODE *node);
	Value *compile_dstr(NODE *node);
	void compile_dead_branch(void);
	void compile_landing_pad_header(void);
	void compile_landing_pad_footer(void);
	void compile_rethrow_exception(void);

	Value *get_var(ID name, std::map<ID, Value *> &container, 
		       bool do_assert) {
	    std::map<ID, Value *>::iterator iter = container.find(name);
	    if (do_assert) {
		assert(iter != container.end());
		return iter->second;
	    }
	    else {
		return iter != container.end() ? iter->second : NULL;
	    }
	}

	Value *get_lvar(ID name, bool do_assert=true) {
	    return get_var(name, lvars, do_assert);
	}

	int *get_slot_cache(ID id) {
	    if (current_block || !current_instance_method) {
		// TODO should also return NULL if we are inside a module
		return NULL;
	    }
	    std::map<ID, int *>::iterator iter = ivar_slots_cache.find(id);
	    if (iter == ivar_slots_cache.end()) {
#if ROXOR_COMPILER_DEBUG
		printf("allocating a new slot for ivar %s\n", rb_id2name(id));
#endif
		int *slot = (int *)malloc(sizeof(int));
		*slot = -1;
		ivar_slots_cache[id] = slot;
		return slot;
	    }
	    return iter->second;
	}

	ICmpInst *is_value_a_fixnum(Value *val);
	void compile_ivar_slots(Value *klass, BasicBlock::InstListType &list, 
				BasicBlock::InstListType::iterator iter);
	bool unbox_fixnum_constant(Value *val, long *lval);
	SEL mid_to_sel(ID mid, int arity);
};

llvm::Module *RoxorCompiler::module = NULL;

struct ccache {
    VALUE outer;
    VALUE val;
};

struct ocall_helper {
    struct rb_objc_method_sig sig;
    bs_element_method_t *bs_method;
};

struct mcache {
#define MCACHE_RCALL 0x1
#define MCACHE_OCALL 0x2
#define MCACHE_BCALL 0x4 
    uint8_t flag;
    union {
	struct {
	    Class klass;
	    IMP imp;
	    NODE *node;
	    rb_vm_arity_t arity;
	} rcall;
	struct {
	    Class klass;
	    IMP imp;
	    struct ocall_helper *helper;
	} ocall;
    } as;
};

extern "C" void *__cxa_allocate_exception(size_t);
extern "C" void __cxa_throw(void *, void *, void *);

struct RoxorFunctionIMP
{
    NODE *node;
    SEL sel; 

    RoxorFunctionIMP(NODE *_node, SEL _sel) { 
	node = _node;
	sel = _sel;
    }
};

struct rb_vm_outer {
    Class klass;
    struct rb_vm_outer *outer;
};

class RoxorVM
{
    private:
	ExistingModuleProvider *emp;
	RoxorJITManager *jmm;
	ExecutionEngine *ee;
	FunctionPassManager *fpm;
	bool running;

	std::map<IMP, struct RoxorFunctionIMP *> ruby_imps;
	std::map<SEL, struct mcache *> mcache;
	std::map<ID, struct ccache *> ccache;
	std::map<Class, std::map<ID, int> *> ivar_slots;
	std::map<SEL, GlobalVariable *> redefined_ops_gvars;
	std::map<Class, struct rb_vm_outer *> outers;

    public:
	static RoxorVM *current;

	Class current_class;
	VALUE current_top_object;
	VALUE current_exception;
	VALUE loaded_features;
	VALUE load_path;
	VALUE backref;
	VALUE broken_with;
	int safe_level;
	std::map<NODE *, rb_vm_block_t *> blocks;
	std::map<double, struct rb_float_cache *> float_cache;
	unsigned char method_missing_reason;
	rb_vm_block_t *current_block;
	rb_vm_block_t *previous_block; // only used for non-Ruby created blocks
	bool block_saved; // used by block_given?
	bool parse_in_eval;

	RoxorVM(void);

	ExecutionEngine *execution_engine(void) { return ee; }

	IMP compile(Function *func, bool optimize=true) {
	    if (optimize) {
		fpm->run(*func);
	    }
	    return (IMP)ee->getPointerToFunction(func);
	}

	bool symbolize_call_address(void *addr, void **startp, unsigned long *ln,
				    char *name, size_t name_len) {
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
		std::map<IMP, struct RoxorFunctionIMP *>::iterator iter = 
		    ruby_imps.find((IMP)start);
		if (iter == ruby_imps.end()) {
		    // TODO symbolize objc selectors
		    return false;
		}

		struct RoxorFunctionIMP *fi = iter->second;
		if (ln != NULL) {
		    *ln = nd_line(fi->node);
		}
		if (name != NULL) {
		    strncpy(name, sel_getName(fi->sel), name_len);
		}
	    }

	    return true;
	}

	bool is_running(void) { return running; }
	void set_running(bool flag) { running = flag; }

	struct mcache *method_cache_get(SEL sel, bool super);
	NODE *method_node_get(IMP imp);
	void add_method(Class klass, SEL sel, IMP imp, NODE *node, const char *types);

	GlobalVariable *redefined_op_gvar(SEL sel, bool create);
	bool should_invalidate_inline_op(SEL sel, Class klass);

	struct ccache *constant_cache_get(ID path);
	void const_defined(ID path);
	
	std::map<ID, int> *get_ivar_slots(Class klass) {
	    std::map<Class, std::map<ID, int> *>::iterator iter = 
		ivar_slots.find(klass);
	    if (iter == ivar_slots.end()) {
		std::map<ID, int> *map = new std::map<ID, int>;
		ivar_slots[klass] = map;
		return map;
	    }
	    return iter->second;
	}
	int find_ivar_slot(VALUE klass, ID name, bool create);
	bool class_can_have_ivar_slots(VALUE klass);

	struct rb_vm_outer *get_outer(Class klass) {
	    std::map<Class, struct rb_vm_outer *>::iterator iter =
		outers.find(klass);
	    return iter == outers.end() ? NULL : iter->second;
	}

	void set_outer(Class klass, Class mod) {
	    struct rb_vm_outer *mod_outer = get_outer(mod);
	    struct rb_vm_outer *class_outer = (struct rb_vm_outer *)
		malloc(sizeof(struct rb_vm_outer));
	    class_outer->klass = klass;
	    class_outer->outer = mod_outer;
	    outers[klass] = class_outer;
	}
};

RoxorVM *RoxorVM::current = NULL;

#define GET_VM() (RoxorVM::current)

RoxorCompiler::RoxorCompiler(const char *_fname)
{
    fname = _fname;

    bb = NULL;
    entry_bb = NULL;
    begin_bb = NULL;
    rescue_bb = NULL;
    current_mid = 0;
    current_instance_method = false;
    self_id = rb_intern("self");
    current_self = NULL;
    current_block = false;
    current_block_node = NULL;
    current_block_func = NULL;
    current_opened_class = NULL;
    current_loop_begin_bb = NULL;
    current_loop_body_bb = NULL;
    current_loop_end_bb = NULL;
    current_loop_exit_val = NULL;

    dispatcherFunc = NULL;
    fastEqqFunc = NULL;
    whenSplatFunc = NULL;
    prepareBlockFunc = NULL;
    getBlockFunc = NULL;
    currentBlockObjectFunc = NULL;
    getConstFunc = NULL;
    setConstFunc = NULL;
    prepareMethodFunc = NULL;
    singletonClassFunc = NULL;
    defineClassFunc = NULL;
    prepareIvarSlotFunc = NULL;
    getIvarFunc = NULL;
    setIvarFunc = NULL;
    definedFunc = NULL;
    undefFunc = NULL;
    aliasFunc = NULL;
    valiasFunc = NULL;
    newHashFunc = NULL;
    toArrayFunc = NULL;
    catArrayFunc = NULL;
    dupArrayFunc = NULL;
    newArrayFunc = NULL;
    newRangeFunc = NULL;
    newRegexpFunc = NULL;
    strInternFunc = NULL;
    rhsnGetFunc = NULL;
    newStringFunc = NULL;
    yieldFunc = NULL;
    gvarSetFunc = NULL;
    gvarGetFunc = NULL;
    cvarSetFunc = NULL;
    cvarGetFunc = NULL;
    popExceptionFunc = NULL;
    getSpecialFunc = NULL;
    breakFunc = NULL;

#if __LP64__
    RubyObjTy = IntTy = Type::Int64Ty;
#else
    RubyObjTy = IntTy = Type::Int32Ty;
#endif

    zeroVal = ConstantInt::get(IntTy, 0);
    oneVal = ConstantInt::get(IntTy, 1);
    twoVal = ConstantInt::get(IntTy, 2);

    RubyObjPtrTy = PointerType::getUnqual(RubyObjTy);
    nilVal = ConstantInt::get(RubyObjTy, Qnil);
    trueVal = ConstantInt::get(RubyObjTy, Qtrue);
    falseVal = ConstantInt::get(RubyObjTy, Qfalse);
    undefVal = ConstantInt::get(RubyObjTy, Qundef);
    splatArgFollowsVal = ConstantInt::get(RubyObjTy, SPLAT_ARG_FOLLOWS);
    PtrTy = PointerType::getUnqual(Type::Int8Ty);

#if ROXOR_COMPILER_DEBUG
    level = 0;
#endif
}

inline SEL
RoxorCompiler::mid_to_sel(ID mid, int arity)
{
    SEL sel;
    const char *mid_str = rb_id2name(mid);
    if (mid_str[strlen(mid_str) - 1] != ':' && arity > 0) {
	char buf[100];
	snprintf(buf, sizeof buf, "%s:", mid_str);
	sel = sel_registerName(buf);
    }
    else {
	sel = sel_registerName(mid_str);
    }
    return sel;
}

inline bool
RoxorCompiler::unbox_fixnum_constant(Value *val, long *lval)
{
    if (ConstantInt::classof(val)) {
	long tmp = cast<ConstantInt>(val)->getZExtValue();
	if (FIXNUM_P(tmp)) {
	    *lval = FIX2LONG(tmp);
	    return true;
	}
    }
    return false;
}

inline ICmpInst *
RoxorCompiler::is_value_a_fixnum(Value *val)
{
    Value *andOp = BinaryOperator::CreateAnd(val, oneVal, "", bb);
    return new ICmpInst(ICmpInst::ICMP_EQ, andOp, oneVal, "", bb); 
}

Value *
RoxorCompiler::compile_protected_call(Function *func, std::vector<Value *> &params)
{
    if (rescue_bb == NULL) {
	CallInst *dispatch = CallInst::Create(func, 
		params.begin(), 
		params.end(), 
		"", 
		bb);
	return cast<Value>(dispatch);
    }
    else {
	BasicBlock *normal_bb = BasicBlock::Create("normal", bb->getParent());

	InvokeInst *dispatch = InvokeInst::Create(func,
		normal_bb, 
		rescue_bb, 
		params.begin(), 
		params.end(), 
		"", 
		bb);

	bb = normal_bb;

	return cast<Value>(dispatch); 
    }
}

void
RoxorCompiler::compile_single_when_argument(NODE *arg, Value *comparedToVal, BasicBlock *thenBB)
{
    Value *subnodeVal = compile_node(arg);
    Value *condVal;
    if (comparedToVal != NULL) {
	std::vector<Value *> params;
	void *eqq_cache = GET_VM()->method_cache_get(selEqq, false);
	params.push_back(compile_const_pointer(eqq_cache));
	params.push_back(subnodeVal);
	params.push_back(compile_const_pointer((void *)selEqq));
	params.push_back(compile_const_pointer(NULL));
	params.push_back(ConstantInt::get(Type::Int8Ty, 0));
	params.push_back(ConstantInt::get(Type::Int32Ty, 1));
	params.push_back(comparedToVal);

	condVal = compile_optimized_dispatch_call(selEqq, 1, params);
	if (condVal == NULL) {
	    condVal = compile_dispatch_call(params);
	}
    }
    else {
	condVal = subnodeVal;
    }

    Function *f = bb->getParent();
    BasicBlock *nextTestBB = BasicBlock::Create("next_test", f);

    compile_boolean_test(condVal, thenBB, nextTestBB);

    bb = nextTestBB;
}

void RoxorCompiler::compile_boolean_test(Value *condVal, BasicBlock *ifTrueBB, BasicBlock *ifFalseBB)
{
    Function *f = bb->getParent();
    BasicBlock *notFalseBB = BasicBlock::Create("not_false", f);

    Value *notFalseCond = new ICmpInst(ICmpInst::ICMP_NE, condVal, falseVal, "", bb);
    BranchInst::Create(notFalseBB, ifFalseBB, notFalseCond, bb);
    Value *notNilCond = new ICmpInst(ICmpInst::ICMP_NE, condVal, nilVal, "", notFalseBB);
    BranchInst::Create(ifTrueBB, ifFalseBB, notNilCond, notFalseBB);
}

void
RoxorCompiler::compile_when_arguments(NODE *args, Value *comparedToVal, BasicBlock *thenBB)
{
    switch (nd_type(args)) {
	case NODE_ARRAY:
	    while (args != NULL) {
		compile_single_when_argument(args->nd_head, comparedToVal, thenBB);
		args = args->nd_next;
	    }
	    break;

	case NODE_SPLAT:
	    {
		Value *condVal = compile_when_splat(comparedToVal, compile_node(args->nd_head));

		BasicBlock *nextTestBB = BasicBlock::Create("next_test", bb->getParent());
		compile_boolean_test(condVal, thenBB, nextTestBB);

		bb = nextTestBB;
	    }
	    break;

	case NODE_ARGSPUSH:
	case NODE_ARGSCAT:
	    compile_when_arguments(args->nd_head, comparedToVal, thenBB);
	    compile_single_when_argument(args->nd_body, comparedToVal, thenBB);
	    break;

	default:
	    compile_node_error("unrecognized when arg node", args);
    }
}

Function::ArgumentListType::iterator
RoxorCompiler::compile_optional_arguments(Function::ArgumentListType::iterator iter,
					  NODE *node)
{
    assert(nd_type(node) == NODE_OPT_ARG);

    do {
	assert(node->nd_value != NULL);

	Value *isUndefInst = new ICmpInst(ICmpInst::ICMP_EQ, iter, undefVal, "", bb);

	Function *f = bb->getParent();
	BasicBlock *arg_undef = BasicBlock::Create("arg_undef", f);
	BasicBlock *next_bb = BasicBlock::Create("", f);

	BranchInst::Create(arg_undef, next_bb, isUndefInst, bb);

	bb = arg_undef;
	compile_node(node->nd_value);
	BranchInst::Create(next_bb, bb);

	bb = next_bb;
	++iter;
    }
    while ((node = node->nd_next) != NULL);

    return iter;
}

void
RoxorCompiler::compile_dispatch_arguments(NODE *args, std::vector<Value *> &arguments, int *pargc)
{
    int argc = 0;

    switch (nd_type(args)) {
	case NODE_ARRAY:
	    for (NODE *n = args; n != NULL; n = n->nd_next) {
		arguments.push_back(compile_node(n->nd_head));
		argc++;
	    }
	    break;

	case NODE_SPLAT:
	    assert(args->nd_head != NULL);
	    arguments.push_back(splatArgFollowsVal);
	    arguments.push_back(compile_node(args->nd_head));
	    argc++;
	    break;

	case NODE_ARGSCAT:
	    assert(args->nd_head != NULL);
	    compile_dispatch_arguments(args->nd_head, arguments, &argc);
	    assert(args->nd_body != NULL);
	    arguments.push_back(splatArgFollowsVal);
	    arguments.push_back(compile_node(args->nd_body));
	    argc++;
	    break;

	case NODE_ARGSPUSH:
	    assert(args->nd_head != NULL);
	    compile_dispatch_arguments(args->nd_head, arguments, &argc);
	    assert(args->nd_body != NULL);
	    arguments.push_back(compile_node(args->nd_body));
	    argc++;
	    break;

	default:
	    compile_node_error("unrecognized dispatch arg node", args);
    }
    *pargc = argc;
}

Value *
RoxorCompiler::compile_fast_eqq_call(Value *selfVal, Value *comparedToVal)
{
    if (fastEqqFunc == NULL) {
	// VALUE rb_vm_fast_eqq(struct mcache *cache, VALUE left, VALUE right)
	fastEqqFunc = cast<Function>
	    (module->getOrInsertFunction("rb_vm_fast_eqq",
					 RubyObjTy, PtrTy, RubyObjTy, RubyObjTy, NULL));
    }

    void *eqq_cache = GET_VM()->method_cache_get(selEqq, false);

    std::vector<Value *> params;
    params.push_back(compile_const_pointer(eqq_cache));
    params.push_back(selfVal);
    params.push_back(comparedToVal);

    return compile_protected_call(fastEqqFunc, params);
}

Value *
RoxorCompiler::compile_when_splat(Value *comparedToVal, Value *splatVal)
{
    if (whenSplatFunc == NULL) {
	// VALUE rb_vm_when_splat(struct mcache *cache, VALUE comparedTo, VALUE splat)
	whenSplatFunc = cast<Function>
	    (module->getOrInsertFunction("rb_vm_when_splat",
					 RubyObjTy, PtrTy, RubyObjTy, RubyObjTy, NULL));
    }

    void *eqq_cache = GET_VM()->method_cache_get(selEqq, false);
    std::vector<Value *> params;
    params.push_back(compile_const_pointer(eqq_cache));
    params.push_back(comparedToVal);
    params.push_back(splatVal);

    return compile_protected_call(whenSplatFunc, params);
}

Value *
RoxorCompiler::compile_dispatch_call(std::vector<Value *> &params)
{
    if (dispatcherFunc == NULL) {
	// VALUE rb_vm_dispatch(struct mcache *cache, VALUE self, SEL sel,
	//		        void *block, unsigned char opt, int argc, ...);
	std::vector<const Type *> types;
	types.push_back(PtrTy);
	types.push_back(RubyObjTy);
	types.push_back(PtrTy);
	types.push_back(PtrTy);
	types.push_back(Type::Int8Ty);
	types.push_back(Type::Int32Ty);
	FunctionType *ft = FunctionType::get(RubyObjTy, types, true);
	dispatcherFunc = cast<Function>
	    (module->getOrInsertFunction("rb_vm_dispatch", ft));
    }

    return compile_protected_call(dispatcherFunc, params);
}

Value *
RoxorCompiler::compile_attribute_assign(NODE *node, Value *extra_val)
{
    Value *recv = node->nd_recv == (NODE *)1
	? current_self
	: compile_node(node->nd_recv);

    ID mid = node->nd_mid;
    assert(mid > 0);

    std::vector<Value *> args;
    NODE *n = node->nd_args;
    int argc = 0;
    if (n != NULL) {
	compile_dispatch_arguments(n, args, &argc);
    }
    if (extra_val != NULL) {
	args.push_back(extra_val);
	argc++;
    }

    if (mid != idASET) {
	// A regular attribute assignment (obj.foo = 42)
	assert(argc == 1);
    }

    std::vector<Value *> params;
    const SEL sel = mid_to_sel(mid, argc);
    void *cache = GET_VM()->method_cache_get(sel, false);
    params.push_back(compile_const_pointer(cache));
    params.push_back(recv);
    params.push_back(compile_const_pointer((void *)sel));
    params.push_back(compile_const_pointer(NULL));
    params.push_back(ConstantInt::get(Type::Int8Ty, 0));
    params.push_back(ConstantInt::get(Type::Int32Ty, argc));
    for (std::vector<Value *>::iterator i = args.begin();
	 i != args.end();
	 ++i) {
	params.push_back(*i);
    }

    return compile_dispatch_call(params);
}

Value *
RoxorCompiler::compile_block_create(NODE *node)
{
    if (node != NULL) {
	if (getBlockFunc == NULL) {
	    // void *rb_vm_get_block(VALUE obj);
	    getBlockFunc = cast<Function>
		(module->getOrInsertFunction("rb_vm_get_block",
					     PtrTy, RubyObjTy, NULL));
	}

	std::vector<Value *> params;
	params.push_back(compile_node(node->nd_body));
	return compile_protected_call(getBlockFunc, params);
    }

    assert(current_block_func != NULL && current_block_node != NULL);

    if (prepareBlockFunc == NULL) {
	// void *rb_vm_prepare_block(Function *func, NODE *node, VALUE self, int dvars_size, ...);
	std::vector<const Type *> types;
	types.push_back(PtrTy);
	types.push_back(PtrTy);
	types.push_back(RubyObjTy);
	types.push_back(Type::Int32Ty);
	FunctionType *ft = FunctionType::get(PtrTy, types, true);
	prepareBlockFunc = cast<Function>
	    (module->getOrInsertFunction("rb_vm_prepare_block", ft));
    }

    std::vector<Value *> params;
    params.push_back(compile_const_pointer(current_block_func));
    params.push_back(compile_const_pointer(current_block_node));
    params.push_back(current_self);

    params.push_back(ConstantInt::get(Type::Int32Ty, (int)dvars.size()));

    std::map<ID, Value *>::iterator iter = dvars.begin();
    while (iter != dvars.end()) {
	std::map<ID, Value *>::iterator iter2 = lvars.find(iter->first);
	assert(iter2 != lvars.end());
	params.push_back(iter2->second);
	++iter;
    }

    return CallInst::Create(prepareBlockFunc, params.begin(), params.end(), "", bb);
}

Value *
RoxorCompiler::compile_ivar_read(ID vid)
{
    if (getIvarFunc == NULL) {
	// VALUE rb_vm_ivar_get(VALUE obj, ID name, void *slot_cache);
	getIvarFunc = cast<Function>(module->getOrInsertFunction("rb_vm_ivar_get",
		    RubyObjTy, RubyObjTy, IntTy, PtrTy, NULL)); 
    }

    std::vector<Value *> params;
    Value *val;

    params.push_back(current_self);

    val = ConstantInt::get(IntTy, vid);
    params.push_back(val);

    int *slot_cache = get_slot_cache(vid);
    params.push_back(compile_const_pointer(slot_cache));

    return CallInst::Create(getIvarFunc, params.begin(), params.end(), "", bb);
}

Value *
RoxorCompiler::compile_ivar_assignment(ID vid, Value *val)
{
    if (setIvarFunc == NULL) {
	// void rb_vm_ivar_set(VALUE obj, ID name, VALUE val, int *slot_cache);
	setIvarFunc = 
	    cast<Function>(module->getOrInsertFunction("rb_vm_ivar_set",
			Type::VoidTy, RubyObjTy, IntTy, RubyObjTy, PtrTy, NULL)); 
    }

    std::vector<Value *> params;

    params.push_back(current_self);
    params.push_back(ConstantInt::get(IntTy, (long)vid));
    params.push_back(val);

    int *slot_cache = get_slot_cache(vid);
    params.push_back(compile_const_pointer(slot_cache));

    CallInst::Create(setIvarFunc, params.begin(), params.end(), "", bb);

    return val;
}

Value *
RoxorCompiler::compile_current_class(void)
{
    if (current_opened_class == NULL) {
	return ConstantInt::get(RubyObjTy, (long)rb_cObject);
    }
    return new LoadInst(current_opened_class, "", bb);
}

Value *
RoxorCompiler::compile_const(ID id, Value *outer)
{
    bool outer_given = true;
    if (outer == NULL) {
	outer = compile_current_class();
	outer_given = false;
    }

    if (getConstFunc == NULL) {
	// VALUE rb_vm_get_const(VALUE mod, unsigned char lexical_lookup,
	//			 struct ccache *cache, ID id);
	getConstFunc = cast<Function>(module->getOrInsertFunction(
		    "rb_vm_get_const", 
		    RubyObjTy, RubyObjTy, Type::Int8Ty, PtrTy, IntTy, NULL));
    }

    std::vector<Value *> params;

    struct ccache *cache = GET_VM()->constant_cache_get(id);
    params.push_back(outer);
    params.push_back(ConstantInt::get(Type::Int8Ty, outer_given ? 0 : 1));
    params.push_back(compile_const_pointer(cache));
    params.push_back(ConstantInt::get(IntTy, id));

    return compile_protected_call(getConstFunc, params);
}

Value *
RoxorCompiler::compile_singleton_class(Value *obj)
{
    if (singletonClassFunc == NULL) {
	// VALUE rb_singleton_class(VALUE klass);
	singletonClassFunc = cast<Function>(module->getOrInsertFunction(
		    "rb_singleton_class",
		    RubyObjTy, RubyObjTy, NULL));
    }

    std::vector<Value *> params;
    params.push_back(obj);

    return compile_protected_call(singletonClassFunc, params);
}

#define DEFINED_IVAR 	1
#define DEFINED_GVAR 	2
#define DEFINED_CVAR 	3
#define DEFINED_CONST	4
#define DEFINED_LCONST	5
#define DEFINED_SUPER	6
#define DEFINED_METHOD	7

Value *
RoxorCompiler::compile_defined_expression(NODE *node)
{
    // Easy cases first.
    VALUE str = 0;
    switch (nd_type(node)) {
	case NODE_NIL:
	    str = (VALUE)CFSTR("nil");
	    break;

	case NODE_SELF:
	    str = (VALUE)CFSTR("self");
	    break;

	case NODE_TRUE:
	    str = (VALUE)CFSTR("true");
	    break;

	case NODE_FALSE:
	    str = (VALUE)CFSTR("false");
	    break;

	case NODE_ARRAY:
	case NODE_ZARRAY:
	case NODE_STR:
	case NODE_LIT:
	    str = (VALUE)CFSTR("expression");
	    break;

	case NODE_LVAR:
	case NODE_DVAR:
	    str = (VALUE)CFSTR("local-variable");
	    break;

	case NODE_OP_ASGN1:
	case NODE_OP_ASGN2:
	case NODE_OP_ASGN_OR:
	case NODE_OP_ASGN_AND:
	case NODE_MASGN:
	case NODE_LASGN:
	case NODE_DASGN:
	case NODE_DASGN_CURR:
	case NODE_GASGN:
	case NODE_IASGN:
	case NODE_CDECL:
	case NODE_CVDECL:
	case NODE_CVASGN:
	    str = (VALUE)CFSTR("assignment");
	    break;
    }
    if (str != 0) {
	return ConstantInt::get(RubyObjTy, (long)str);
    }

    // Now the less easy ones... let's set up an exception handler first
    // because we might need to evalute things that will result in an
    // exception.
    Function *f = bb->getParent(); 
    BasicBlock *old_rescue_bb = rescue_bb;
    BasicBlock *new_rescue_bb = BasicBlock::Create("rescue", f);
    BasicBlock *merge_bb = BasicBlock::Create("merge", f);
    rescue_bb = new_rescue_bb;

    // Prepare arguments for the runtime.
    Value *self = current_self;
    VALUE what1 = 0;
    Value *what2 = NULL;
    int type = 0;

    switch (nd_type(node)) {
	case NODE_IVAR:
	    type = DEFINED_IVAR;
	    what1 = (VALUE)node->nd_vid;
	    break;

	case NODE_GVAR:
	    type = DEFINED_GVAR;
	    what1 = (VALUE)node->nd_entry;
	    break;

	case NODE_CVAR:
	    type = DEFINED_CVAR;
	    what1 = (VALUE)node->nd_vid;
	    break;

	case NODE_CONST:
	    type = DEFINED_LCONST;
	    what1 = (VALUE)node->nd_vid;
	    what2 = compile_current_class();
	    break;

	case NODE_SUPER:
	case NODE_ZSUPER:
	    type = DEFINED_SUPER;
	    what1 = (VALUE)current_mid;
	    break;

	case NODE_COLON2:
	    what2 = compile_node(node->nd_head);	
	    if (rb_is_const_id(node->nd_mid)) {
		type = DEFINED_CONST;
		what1 = (VALUE)node->nd_mid;
	    }
	    else {
		type = DEFINED_METHOD;
		what1 = (VALUE)node->nd_mid;
	    }
	    break;

      case NODE_CALL:
      case NODE_VCALL:
      case NODE_FCALL:
      case NODE_ATTRASGN:
	    if (nd_type(node) == NODE_CALL
		|| (nd_type(node) == NODE_ATTRASGN
		    && node->nd_recv != (NODE *)1)) {
		self = compile_node(node->nd_recv);
	    }
	    type = DEFINED_METHOD;
	    what1 = (VALUE)node->nd_mid;
	    break;
    }

    if (type == 0) {
	compile_node_error("unrecognized defined? arg", node);
    }

    if (definedFunc == NULL) {
	// VALUE rb_vm_defined(VALUE self, int type, VALUE what, VALUE what2);
	definedFunc = cast<Function>(module->getOrInsertFunction(
		    "rb_vm_defined",
		    RubyObjTy, RubyObjTy, Type::Int32Ty, RubyObjTy, RubyObjTy,
		    NULL));
    }

    std::vector<Value *> params;

    params.push_back(self);
    params.push_back(ConstantInt::get(Type::Int32Ty, type));
    params.push_back(ConstantInt::get(RubyObjTy, what1));
    params.push_back(what2 == NULL ? nilVal : what2);

    // Call the runtime.
    Value *val = compile_protected_call(definedFunc, params);
    BasicBlock *normal_bb = bb;
    BranchInst::Create(merge_bb, bb);

    // The rescue block - here we simply do nothing.
    bb = new_rescue_bb;
    compile_landing_pad_header();
    compile_landing_pad_footer();
    BranchInst::Create(merge_bb, bb);

    // Now merging.
    bb = merge_bb;
    PHINode *pn = PHINode::Create(RubyObjTy, "", bb);
    pn->addIncoming(val, normal_bb);
    pn->addIncoming(nilVal, new_rescue_bb);

    rescue_bb = old_rescue_bb;

    return pn;
}

Value *
RoxorCompiler::compile_dstr(NODE *node)
{
    std::vector<Value *> params;

    if (node->nd_lit != 0) {
	params.push_back(ConstantInt::get(RubyObjTy, node->nd_lit));
    }

    NODE *n = node->nd_next;
    assert(n != NULL);
    while (n != NULL) {
	params.push_back(compile_node(n->nd_head));
	n = n->nd_next;
    }

    const int count = params.size();

    params.insert(params.begin(), ConstantInt::get(Type::Int32Ty, count));

    if (newStringFunc == NULL) {
	// VALUE rb_str_new_fast(int argc, ...)
	std::vector<const Type *> types;
	types.push_back(Type::Int32Ty);
	FunctionType *ft = FunctionType::get(RubyObjTy, types, true);
	newStringFunc = cast<Function>(module->getOrInsertFunction(
		    "rb_str_new_fast", ft));
    }

    return CallInst::Create(newStringFunc, params.begin(), params.end(), "", bb);
}

Value *
RoxorCompiler::compile_class_path(NODE *node)
{
    if (nd_type(node) == NODE_COLON3) {
	// ::Foo
	return ConstantInt::get(RubyObjTy, (long)rb_cObject);
    }
    else if (node->nd_head != NULL) {
	// Bar::Foo
	return compile_node(node->nd_head);
    }

    return compile_current_class();
}

void
RoxorCompiler::compile_dead_branch(void)
{
    // To not complicate the compiler even more, let's be very lazy here and
    // continue on a dead branch. Hopefully LLVM is smart enough to eliminate
    // it at compilation time.
    bb = BasicBlock::Create("DEAD", bb->getParent());
}

void
RoxorCompiler::compile_landing_pad_header(void)
{
    Function *eh_exception_f = Intrinsic::getDeclaration(module,
	    Intrinsic::eh_exception);
    Value *eh_ptr = CallInst::Create(eh_exception_f, "", bb);

#if __LP64__
    Function *eh_selector_f = Intrinsic::getDeclaration(module,
	    Intrinsic::eh_selector_i64);
#else
    Function *eh_selector_f = Intrinsic::getDeclaration(module,
	    Intrinsic::eh_selector_i32);
#endif

    std::vector<Value *> params;
    params.push_back(eh_ptr);
    Function *__gxx_personality_v0_func = NULL;
    if (__gxx_personality_v0_func == NULL) {
	__gxx_personality_v0_func = cast<Function>(
		module->getOrInsertFunction("__gxx_personality_v0",
		    PtrTy, NULL));
    }
    params.push_back(new BitCastInst(__gxx_personality_v0_func,
		PtrTy, "", bb));
    params.push_back(compile_const_pointer(NULL));

    CallInst::Create(eh_selector_f, params.begin(), params.end(),
	    "", bb);

    Function *beginCatchFunc = NULL;
    if (beginCatchFunc == NULL) {
	// void *__cxa_begin_catch(void *);
	beginCatchFunc = cast<Function>(
		module->getOrInsertFunction("__cxa_begin_catch",
		    Type::VoidTy, PtrTy, NULL));
    }
    params.clear();
    params.push_back(eh_ptr);
    CallInst::Create(beginCatchFunc, params.begin(), params.end(),
	    "", bb);
}

void
RoxorCompiler::compile_landing_pad_footer(void)
{
    Function *endCatchFunc = NULL;
    if (endCatchFunc == NULL) {
	// void __cxa_end_catch(void);
	endCatchFunc = cast<Function>(
		module->getOrInsertFunction("__cxa_end_catch",
		    Type::VoidTy, NULL));
    }
    CallInst::Create(endCatchFunc, "", bb);
}

void
RoxorCompiler::compile_rethrow_exception(void)
{
    Function *rethrowFunc = NULL;
    if (rethrowFunc == NULL) {
	// void __cxa_rethrow(void);
	rethrowFunc = cast<Function>(
		module->getOrInsertFunction("__cxa_rethrow",
		    Type::VoidTy, NULL));
    }
    CallInst::Create(rethrowFunc, "", bb);
    new UnreachableInst(bb);
}

Value *
RoxorCompiler::compile_optimized_dispatch_call(SEL sel, int argc, std::vector<Value *> &params)
{
    // The not operator (!).
    if (sel == selNot) {
	
	if (current_block_func != NULL || argc != 0) {
	    return NULL;
	}
	
	Value *val = params[1]; // self

	Function *f = bb->getParent();

	BasicBlock *falseBB = BasicBlock::Create("", f);
	BasicBlock *trueBB = BasicBlock::Create("", f);
	BasicBlock *mergeBB = BasicBlock::Create("", f);

	compile_boolean_test(val, trueBB, falseBB);

	BranchInst::Create(mergeBB, falseBB);
	BranchInst::Create(mergeBB, trueBB);

	bb = mergeBB;	

	PHINode *pn = PHINode::Create(RubyObjTy, "", bb);
	pn->addIncoming(trueVal, falseBB);
	pn->addIncoming(falseVal, trueBB);

	return pn;
    }
    // Pure arithmetic operations.
    else if (sel == selPLUS || sel == selMINUS || sel == selDIV 
	     || sel == selMULT || sel == selLT || sel == selLE 
	     || sel == selGT || sel == selGE || sel == selEq
	     || sel == selNeq || sel == selEqq) {

	if (current_block_func != NULL || argc != 1) {
	    return NULL;
	}

	GlobalVariable *is_redefined = GET_VM()->redefined_op_gvar(sel, true);
	
	Value *leftVal = params[1]; // self
	Value *rightVal = params.back();

	long leftLong = 0, rightLong = 0;
	const bool leftIsConst = unbox_fixnum_constant(leftVal, &leftLong);
	const bool rightIsConst = unbox_fixnum_constant(rightVal, &rightLong);

	if (leftIsConst && rightIsConst) {
	    // Both operands are fixnum constants.
	    bool result_is_fixnum = true;
	    long res;

	    if (sel == selPLUS) {
		res = leftLong + rightLong;
	    }
	    else if (sel == selMINUS) {
		res = leftLong - rightLong;
	    }
	    else if (sel == selDIV) {
		if (rightLong == 0) {
		    return NULL;
		}
		res = leftLong / rightLong;
	    }
	    else if (sel == selMULT) {
		res = leftLong * rightLong;
	    }
	    else {
		result_is_fixnum = false;
		if (sel == selLT) {
		    res = leftLong < rightLong;
		}
		else if (sel == selLE) {
		    res = leftLong <= rightLong;
		}
		else if (sel == selGT) {
		    res = leftLong > rightLong;
		}
		else if (sel == selGE) {
		    res = leftLong >= rightLong;
		}
		else if ((sel == selEq) || (sel == selEqq)) {
		    res = leftLong == rightLong;
		}
		else if (sel == selNeq) {
		    res = leftLong != rightLong;
		}
		else {
		    abort();		
		}
	    }
	    if (!result_is_fixnum || FIXABLE(res)) {
		Value *is_redefined_val = new LoadInst(is_redefined, "", bb);
		Value *isOpRedefined = new ICmpInst(ICmpInst::ICMP_EQ, 
			is_redefined_val, ConstantInt::getFalse(), "", bb);

		Function *f = bb->getParent();

		BasicBlock *thenBB = BasicBlock::Create("op_not_redefined", f);
		BasicBlock *elseBB  = BasicBlock::Create("op_dispatch", f);
		BasicBlock *mergeBB = BasicBlock::Create("op_merge", f);

		BranchInst::Create(thenBB, elseBB, isOpRedefined, bb);
		Value *thenVal = result_is_fixnum
		    ? ConstantInt::get(RubyObjTy, LONG2FIX(res)) 
		    : (res == 1 ? trueVal : falseVal);
		BranchInst::Create(mergeBB, thenBB);

		bb = elseBB;
		Value *elseVal = compile_dispatch_call(params);
		elseBB = bb;
		BranchInst::Create(mergeBB, elseBB);

		PHINode *pn = PHINode::Create(RubyObjTy, "op_tmp", mergeBB);
		pn->addIncoming(thenVal, thenBB);
		pn->addIncoming(elseVal, elseBB);
		bb = mergeBB;

		return pn;
	    }
	    // Non fixable (bignum), call the dispatcher.
	    return NULL;
	}
	else {
	    // Either one or both is not a constant fixnum.
	    Value *is_redefined_val = new LoadInst(is_redefined, "", bb);
	    Value *isOpRedefined = new ICmpInst(ICmpInst::ICMP_EQ, 
		    is_redefined_val, ConstantInt::getFalse(), "", bb);

	    Function *f = bb->getParent();

	    BasicBlock *then1BB = BasicBlock::Create("op_not_redefined", f);
	    BasicBlock *then2BB = BasicBlock::Create("op_optimize", f);
	    BasicBlock *elseBB  = BasicBlock::Create("op_dispatch", f);
	    BasicBlock *mergeBB = BasicBlock::Create("op_merge", f);

	    BranchInst::Create(then1BB, elseBB, isOpRedefined, bb);

 	    bb = then1BB;

	    Value *leftAndOp = NULL;
	    if (!leftIsConst) {
		leftAndOp = BinaryOperator::CreateAnd(leftVal, oneVal, "", 
			bb);
	    }

	    Value *rightAndOp = NULL;
	    if (!rightIsConst) {
		rightAndOp = BinaryOperator::CreateAnd(rightVal, oneVal, "", 
			bb);
	    }

	    Value *areFixnums = NULL;
	    if (leftAndOp != NULL && rightAndOp != NULL) {
		Value *foo = BinaryOperator::CreateAdd(leftAndOp, rightAndOp, 
			"", bb);
		areFixnums = new ICmpInst(ICmpInst::ICMP_EQ, foo, twoVal, "", bb);
	    }
	    else if (leftAndOp != NULL) {
		areFixnums = new ICmpInst(ICmpInst::ICMP_EQ, leftAndOp, oneVal, "", bb);
	    }
	    else {
		areFixnums = new ICmpInst(ICmpInst::ICMP_EQ, rightAndOp, oneVal, "", bb);
	    }
	
	    Value *fastEqqVal = NULL;
	    BasicBlock *fastEqqBB = NULL;
	    if (sel == selEqq) {
		// compile_fast_eqq_call won't be called if #=== has been redefined
		// fixnum optimizations are done separately
		fastEqqBB = BasicBlock::Create("fast_eqq", f);
		BranchInst::Create(then2BB, fastEqqBB, areFixnums, bb);
		bb = fastEqqBB;
		fastEqqVal = compile_fast_eqq_call(leftVal, rightVal);
		fastEqqBB = bb;
		BranchInst::Create(mergeBB, bb);
	    }
	    else {
		BranchInst::Create(then2BB, elseBB, areFixnums, bb);
	    }
	    bb = then2BB;

	    Value *unboxedLeft;
	    if (leftIsConst) {
		unboxedLeft = ConstantInt::get(RubyObjTy, leftLong);
	    }
	    else {
		unboxedLeft = BinaryOperator::CreateAShr(leftVal, oneVal, "", bb);
	    }

	    Value *unboxedRight;
	    if (rightIsConst) {
		unboxedRight = ConstantInt::get(RubyObjTy, rightLong);
	    }
	    else {
		unboxedRight = BinaryOperator::CreateAShr(rightVal, oneVal, "", bb);
	    }

	    Value *opVal;
	    bool result_is_fixnum = true;
	    if (sel == selPLUS) {
		opVal = BinaryOperator::CreateAdd(unboxedLeft, unboxedRight, "", bb);
	    }
	    else if (sel == selMINUS) {
		opVal = BinaryOperator::CreateSub(unboxedLeft, unboxedRight, "", bb);
	    }
	    else if (sel == selDIV) {
		opVal = BinaryOperator::CreateSDiv(unboxedLeft, unboxedRight, "", bb);
	    }
	    else if (sel == selMULT) {
		opVal = BinaryOperator::CreateMul(unboxedLeft, unboxedRight, "", bb);
	    }
	    else {
		result_is_fixnum = false;

		CmpInst::Predicate predicate;

		if (sel == selLT) {
		    predicate = ICmpInst::ICMP_SLT;
		}
		else if (sel == selLE) {
		    predicate = ICmpInst::ICMP_SLE;
		}
		else if (sel == selGT) {
		    predicate = ICmpInst::ICMP_SGT;
		}
		else if (sel == selGE) {
		    predicate = ICmpInst::ICMP_SGE;
		}
		else if ((sel == selEq) || (sel == selEqq)) {
		    predicate = ICmpInst::ICMP_EQ;
		}
		else if (sel == selNeq) {
		    predicate = ICmpInst::ICMP_NE;
		}
		else {
		    abort();
		}

		opVal = new ICmpInst(predicate, unboxedLeft, unboxedRight, "", bb);
		opVal = SelectInst::Create(opVal, trueVal, falseVal, "", bb);
	    }

	    Value *thenVal;
	    BasicBlock *then3BB;

	    if (result_is_fixnum) { 
		Value *shift = BinaryOperator::CreateShl(opVal, oneVal, "", bb);
		thenVal = BinaryOperator::CreateOr(shift, oneVal, "", bb);

		// Is result fixable?
		Value *fixnumMax = ConstantInt::get(IntTy, FIXNUM_MAX + 1);
		Value *isFixnumMaxOk = new ICmpInst(ICmpInst::ICMP_SLT, opVal, fixnumMax, "", bb);

		then3BB = BasicBlock::Create("op_fixable_max", f);

		BranchInst::Create(then3BB, elseBB, isFixnumMaxOk, bb);

		bb = then3BB;
		Value *fixnumMin = ConstantInt::get(IntTy, FIXNUM_MIN);
		Value *isFixnumMinOk = new ICmpInst(ICmpInst::ICMP_SGE, opVal, fixnumMin, "", bb);

		BranchInst::Create(mergeBB, elseBB, isFixnumMinOk, bb);
	    }
	    else {
		thenVal = opVal;
		then3BB = then2BB;
		BranchInst::Create(mergeBB, then3BB);
	    }

	    bb = elseBB;
	    Value *elseVal = compile_dispatch_call(params);
	    elseBB = bb;
	    BranchInst::Create(mergeBB, elseBB);

	    bb = mergeBB;
	    PHINode *pn = PHINode::Create(RubyObjTy, "op_tmp", mergeBB);
	    pn->addIncoming(thenVal, then3BB);
	    pn->addIncoming(elseVal, elseBB);

	    if (sel == selEqq) {
		pn->addIncoming(fastEqqVal, fastEqqBB);
	    }

	    return pn;
	}
    }
    // Other operators (#<< or #[] or #[]=)
    else if (sel == selLTLT || sel == selAREF || sel == selASET) {

	const int expected_argc = sel == selASET ? 2 : 1;
	if (current_block_func != NULL || argc != expected_argc) {
	    return NULL;
	}

	Function *opt_func = NULL;

	if (sel == selLTLT) {
	    opt_func = cast<Function>(module->getOrInsertFunction("rb_vm_fast_shift",
			RubyObjTy, RubyObjTy, RubyObjTy, PtrTy, Type::Int1Ty, NULL));
	}
	else if (sel == selAREF) {
	    opt_func = cast<Function>(module->getOrInsertFunction("rb_vm_fast_aref",
			RubyObjTy, RubyObjTy, RubyObjTy, PtrTy, Type::Int1Ty, NULL));
	}
	else if (sel == selASET) {
	    opt_func = cast<Function>(module->getOrInsertFunction("rb_vm_fast_aset",
			RubyObjTy, RubyObjTy, RubyObjTy, RubyObjTy, PtrTy, Type::Int1Ty, NULL));
	}
	else {
	    abort();
	}

	std::vector<Value *> new_params;
	new_params.push_back(params[1]);		// self
	if (argc == 1) {
	    new_params.push_back(params.back());	// other
	}
	else {
	    // Damn I hate the STL.
	    std::vector<Value *>::iterator iter = params.end();
	    --iter;
	    --iter;
	    new_params.push_back(*iter);		// other1
	    ++iter;
	    new_params.push_back(*iter);		// other2
	}
	new_params.push_back(params[0]);		// cache

	GlobalVariable *is_redefined = GET_VM()->redefined_op_gvar(sel, true);
	new_params.push_back(new LoadInst(is_redefined, "", bb));

	return compile_protected_call(opt_func, new_params);
    }
    // #send or #__send__
    else if (sel == selSend || sel == sel__send__) {

	if (current_block_func != NULL || argc == 0) {
	    return NULL;
	}
	Value *symVal = params[params.size() - argc];
	if (!ConstantInt::classof(symVal)) {
	    return NULL;
	}
	VALUE sym = cast<ConstantInt>(symVal)->getZExtValue();
	if (!SYMBOL_P(sym)) {
	    return NULL;
	}
	SEL new_sel = mid_to_sel(SYM2ID(sym), argc - 1);

	GlobalVariable *is_redefined = GET_VM()->redefined_op_gvar(sel, true);

	Value *is_redefined_val = new LoadInst(is_redefined, "", bb);
	Value *isOpRedefined = new ICmpInst(ICmpInst::ICMP_EQ, 
		is_redefined_val, ConstantInt::getFalse(), "", bb);

	Function *f = bb->getParent();

	BasicBlock *thenBB = BasicBlock::Create("op_not_redefined", f);
	BasicBlock *elseBB = BasicBlock::Create("op_dispatch", f);
	BasicBlock *mergeBB = BasicBlock::Create("op_merge", f);

	BranchInst::Create(thenBB, elseBB, isOpRedefined, bb);

	bb = thenBB;
	std::vector<Value *> new_params;
	void *cache = GET_VM()->method_cache_get(new_sel, false);
	new_params.push_back(compile_const_pointer(cache));
	new_params.push_back(params[1]);
	new_params.push_back(compile_const_pointer((void *)new_sel));
	new_params.push_back(params[3]);
	new_params.push_back(params[4]);
	new_params.push_back(ConstantInt::get(Type::Int32Ty, argc - 1));
	for (int i = 0; i < argc - 1; i++) {
	    new_params.push_back(params[7 + i]);
	}
	Value *thenVal = compile_dispatch_call(new_params);
	BranchInst::Create(mergeBB, thenBB);

	bb = elseBB;
	Value *elseVal = compile_dispatch_call(params);
	BranchInst::Create(mergeBB, elseBB);

	bb = mergeBB;
	PHINode *pn = PHINode::Create(RubyObjTy, "op_tmp", mergeBB);
	pn->addIncoming(thenVal, thenBB);
	pn->addIncoming(elseVal, elseBB);

	return pn;
    }
#if 0
    // XXX this optimization is disabled because it's buggy and not really
    // interesting
    // #eval
    else if (sel == selEval) {

	if (current_block_func != NULL || argc != 1) {
	    return NULL;
	}
	Value *strVal = params.back();
	if (!ConstantInt::classof(strVal)) {
	    return NULL;
	}
	VALUE str = cast<ConstantInt>(strVal)->getZExtValue();
	if (TYPE(str) != T_STRING) {
	    return NULL;
	}
	// FIXME: 
	// - pass the real file/line arguments
	// - catch potential parsing exceptions
	NODE *new_node = rb_compile_string("", str, 0);
	if (new_node == NULL) {
	    return NULL;
	}
	if (nd_type(new_node) != NODE_SCOPE || new_node->nd_body == NULL) {
	    return NULL;
	}

	GlobalVariable *is_redefined = GET_VM()->redefined_op_gvar(sel, true);

	Value *is_redefined_val = new LoadInst(is_redefined, "", bb);
	Value *isOpRedefined = new ICmpInst(ICmpInst::ICMP_EQ, 
		is_redefined_val, ConstantInt::getFalse(), "", bb);

	Function *f = bb->getParent();

	BasicBlock *thenBB = BasicBlock::Create("op_not_redefined", f);
	BasicBlock *elseBB = BasicBlock::Create("op_dispatch", f);
	BasicBlock *mergeBB = BasicBlock::Create("op_merge", f);

	BranchInst::Create(thenBB, elseBB, isOpRedefined, bb);

	bb = thenBB;
	Value *thenVal = compile_node(new_node->nd_body);
	thenBB = bb;
	BranchInst::Create(mergeBB, thenBB);

	bb = elseBB;
	Value *elseVal = compile_dispatch_call(params);
	BranchInst::Create(mergeBB, elseBB);

	bb = mergeBB;
	PHINode *pn = PHINode::Create(RubyObjTy, "op_tmp", mergeBB);
	pn->addIncoming(thenVal, thenBB);
	pn->addIncoming(elseVal, elseBB);

	return pn;

    }
#endif
#if 0
    // TODO: block inlining optimization
    else if (current_block_func != NULL) {
	static SEL selTimes = 0;
	if (selTimes == 0) {
	    selTimes = rb_intern("times");
	}

	if (sel == selTimes && argc == 0) {
	    Value *val = params[1]; // self

	    long valLong;
	    if (unbox_fixnum_constant(val, &valLong)) {
		GlobalVariable *is_redefined = redefined_op_gvar(sel, true);

		Value *is_redefined_val = new LoadInst(is_redefined, "", bb);
		Value *isOpRedefined = new ICmpInst(ICmpInst::ICMP_EQ, is_redefined_val, ConstantInt::getFalse(), "", bb);

		Function *f = bb->getParent();

		BasicBlock *thenBB = BasicBlock::Create("op_not_redefined", f);
		BasicBlock *elseBB  = BasicBlock::Create("op_dispatch", f);
		BasicBlock *mergeBB = BasicBlock::Create("op_merge", f);

		BranchInst::Create(thenBB, elseBB, isOpRedefined, bb);
		bb = thenBB;



//		Val *mem = new AllocaInst(RubyObjTy, "", bb);
//		new StoreInst(zeroVal, mem, "", bb);
//		Val *i = LoadInst(mem, "", bb);
		


		Value *thenVal = val;
		BranchInst::Create(mergeBB, thenBB);

		Value *elseVal = dispatchCall;
		elseBB->getInstList().push_back(dispatchCall);
		BranchInst::Create(mergeBB, elseBB);

		PHINode *pn = PHINode::Create(Type::Int32Ty, "op_tmp", mergeBB);
		pn->addIncoming(thenVal, thenBB);
		pn->addIncoming(elseVal, elseBB);
		bb = mergeBB;

		return pn;
	    }
	}
    }
#endif
    return NULL;
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

RoxorVM::RoxorVM(void)
{
    running = false;
    current_top_object = Qnil;
    current_exception = Qnil;
    safe_level = 0;

    backref = Qnil;
    broken_with = Qundef;

    current_block = NULL;
    previous_block = NULL;
    block_saved = false;

    parse_in_eval = false;

    load_path = rb_ary_new();
    rb_objc_retain((void *)load_path);
    loaded_features = rb_ary_new();
    rb_objc_retain((void *)loaded_features);

    emp = new ExistingModuleProvider(RoxorCompiler::module);
    jmm = new RoxorJITManager;
    ee = ExecutionEngine::createJIT(emp, 0, jmm, true);

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

NODE *
RoxorVM::method_node_get(IMP imp)
{
    std::map<IMP, struct RoxorFunctionIMP *>::iterator iter = ruby_imps.find(imp);
    if (iter == ruby_imps.end()) {
	return NULL;
    }
    return iter->second->node;
}

inline GlobalVariable *
RoxorVM::redefined_op_gvar(SEL sel, bool create)
{
    std::map <SEL, GlobalVariable *>::iterator iter = redefined_ops_gvars.find(sel);
    GlobalVariable *gvar = NULL;
    if (iter == redefined_ops_gvars.end()) {
	if (create) {
	    gvar = new GlobalVariable(
		    Type::Int1Ty,
		    false,
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
    if (sel == selPLUS || sel == selMINUS || sel == selDIV 
	|| sel == selMULT || sel == selLT || sel == selLE 
	|| sel == selGT || sel == selGE || sel == selEq
	|| sel == selNeq || sel == selEqq) {
	return klass == (Class)rb_cFixnum;
    }

    if (sel == selLTLT || sel == selAREF || sel == selASET) {
	return klass == (Class)rb_cNSArray || klass == (Class)rb_cNSMutableArray;
    }

    if (sel == selSend || sel == sel__send__ || sel == selEval) {
	// Matches any class, since these are Kernel methods.
	return true;
    }

    printf("invalid inline op `%s' to invalidate!\n", sel_getName(sel));
    abort();
}

void
RoxorVM::add_method(Class klass, SEL sel, IMP imp, NODE *node, const char *types)
{
#if ROXOR_VM_DEBUG
    printf("defining %c[%s %s] with imp %p node %p types %s\n",
	    class_isMetaClass(klass) ? '+' : '-',
	    class_getName(klass),
	    sel_getName(sel),
	    imp,
	    node,
	    types);
#endif

    // Register the implementation into the runtime.
    class_replaceMethod(klass, sel, imp, types);

    // Cache the method node.
    NODE *old_node = method_node_get(imp);
    if (old_node == NULL) {
	ruby_imps[imp] = new RoxorFunctionIMP(node, sel);
    }
//    else {
//	assert(old_node == node);
//    }

    // Invalidate dispatch cache.
    std::map<SEL, struct mcache *>::iterator iter = mcache.find(sel);
    if (iter != mcache.end()) {
	iter->second->flag = 0;
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
	|| (klass_version & RCLASS_IS_OBJECT_SUBCLASS) != RCLASS_IS_OBJECT_SUBCLASS) {
	return false;
    }
    return true;
}

void
RoxorCompiler::compile_ivar_slots(Value *klass,
				  BasicBlock::InstListType &list, 
				  BasicBlock::InstListType::iterator list_iter)
{
    if (ivar_slots_cache.size() > 0) {
	if (prepareIvarSlotFunc == NULL) {
	    // void rb_vm_prepare_class_ivar_slot(VALUE klass, ID name, int *slot_cache);
	    prepareIvarSlotFunc = cast<Function>(
		    module->getOrInsertFunction("rb_vm_prepare_class_ivar_slot", 
			Type::VoidTy, RubyObjTy, IntTy, PtrTy, NULL));
	}
	for (std::map<ID, int *>::iterator iter = ivar_slots_cache.begin();
		iter != ivar_slots_cache.end();
		++iter) {

	    std::vector<Value *> params;
	    params.push_back(klass);
	    params.push_back(ConstantInt::get(IntTy, iter->first));
	    Instruction *ptr = compile_const_pointer(iter->second, false);
	    list.insert(list_iter, ptr);
	    params.push_back(ptr);

	    CallInst *call = CallInst::Create(prepareIvarSlotFunc, 
		    params.begin(), params.end(), "");

	    list.insert(list_iter, call);
	}
    }
}

Value *
RoxorCompiler::compile_node(NODE *node)
{
#if ROXOR_COMPILER_DEBUG
    printf("%s:%ld ", fname, nd_line(node));
    for (int i = 0; i < level; i++) {
	printf("...");
    }
    printf("... %s\n", ruby_node_name(nd_type(node)));
#endif

    switch (nd_type(node)) {
	case NODE_SCOPE:
	    {
		rb_vm_arity_t arity = rb_vm_node_arity(node);
		const int nargs = bb == NULL ? 0 : arity.real;

		// Get dynamic vars.
		if (current_block && node->nd_tbl != NULL) {
		    const int args_count = (int)node->nd_tbl[0];
		    const int lvar_count = (int)node->nd_tbl[args_count + 1];
		    for (int i = 0; i < lvar_count; i++) {
			ID id = node->nd_tbl[i + args_count + 2];
			if (lvars.find(id) != lvars.end()) {
			    dvars[id] = NULL;
			}
		    }
		}

		// Create function type.
		std::vector<const Type *> types;
		types.push_back(RubyObjTy);	// self
		types.push_back(PtrTy);		// sel
		for (int i = 0, count = dvars.size(); i < count; i++) {
		    types.push_back(RubyObjPtrTy);
		}
		for (int i = 0; i < nargs; ++i) {
		    types.push_back(RubyObjTy);
		}
		FunctionType *ft = FunctionType::get(RubyObjTy, types, false);
		Function *f = cast<Function>(module->getOrInsertFunction("", ft));

		BasicBlock *old_rescue_bb = rescue_bb;
		BasicBlock *old_entry_bb = entry_bb;
		BasicBlock *old_bb = bb;
		rescue_bb = NULL;
		bb = BasicBlock::Create("MainBlock", f);

		std::map<ID, Value *> old_lvars = lvars;
		lvars.clear();
		Value *old_self = current_self;

		Function::arg_iterator arg;

		arg = f->arg_begin();
		Value *self = arg++;
		self->setName("self");
		current_self = self;

		Value *sel = arg++;
		sel->setName("sel");

		for (std::map<ID, Value *>::iterator iter = dvars.begin();
		     iter != dvars.end(); 
		     ++iter) {

		    ID id = iter->first;
		    assert(iter->second == NULL);
	
		    Value *val = arg++;
		    val->setName(std::string("dyna_") + rb_id2name(id));
#if ROXOR_COMPILER_DEBUG
		    printf("dvar %s\n", rb_id2name(id));
#endif
		    lvars[id] = val;
		}

		if (node->nd_tbl != NULL) {
		    int i, args_count = (int)node->nd_tbl[0];
		    assert(args_count == nargs
			    || args_count == nargs + 1 /* optional block */
			    || args_count == nargs - 1 /* unnamed param (|x,|) */);
		    for (i = 0; i < args_count; i++) {
			ID id = node->nd_tbl[i + 1];
#if ROXOR_COMPILER_DEBUG
			printf("arg %s\n", rb_id2name(id));
#endif

			Value *val = NULL;
			if (i < nargs) {
			    val = arg++;
			    val->setName(rb_id2name(id));
			}
			else {
			    // Optional block.
			    if (currentBlockObjectFunc == NULL) {
				// VALUE rb_vm_current_block_object(void);
				currentBlockObjectFunc = cast<Function>(
					module->getOrInsertFunction("rb_vm_current_block_object", 
					    RubyObjTy, NULL));
			    }
			    val = CallInst::Create(currentBlockObjectFunc, "", bb);
			}
			Value *slot = new AllocaInst(RubyObjTy, "", bb);
			new StoreInst(val, slot, bb);
			lvars[id] = slot;
		    }

		    // local vars must be created before the optional arguments
		    // because they can be used in them, for instance with def f(a=b=c=1)
		    int lvar_count = (int)node->nd_tbl[args_count + 1];
		    for (i = 0; i < lvar_count; i++) {
			ID id = node->nd_tbl[i + args_count + 2];
			if (lvars.find(id) != lvars.end()) {
			    continue;
			}
#if ROXOR_COMPILER_DEBUG
			printf("var %s\n", rb_id2name(id));
#endif
			Value *store = new AllocaInst(RubyObjTy, "", bb);
			new StoreInst(nilVal, store, bb);
			lvars[id] = store;
		    }

		    NODE *args_node = node->nd_args;
		    if (args_node != NULL) {
			// compile multiple assignment arguments (def f((a, b, v)))
			// (this must also be done after the creation of local variables)
			NODE *rest_node = args_node->nd_next;
			if (rest_node != NULL) {
			    NODE *right_req_node = rest_node->nd_next;
			    if (right_req_node != NULL) {
				NODE *last_node = right_req_node->nd_next;
				if (last_node != NULL) {
				    assert(nd_type(last_node) == NODE_AND);
				    // multiple assignment for the left-side required arguments
				    if (last_node->nd_1st != NULL) {
					compile_node(last_node->nd_1st);
				    }
				    // multiple assignment for the right-side required arguments
				    if (last_node->nd_2nd != NULL) {
					compile_node(last_node->nd_2nd);
				    }
				}
			    }
			}

			// Compile optional arguments.
			Function::ArgumentListType::iterator iter = f->arg_begin();
			++iter; // skip self
			++iter; // skip sel
			NODE *opt_node = args_node->nd_opt;
			if (opt_node != NULL) {
			    int to_skip = dvars.size() + args_node->nd_frml;
			    for (i = 0; i < to_skip; i++) {
				++iter;  // skip dvars and args required on the left-side
			    }
			    iter = compile_optional_arguments(iter, opt_node);
			}
		    }
		}

		Value *val = NULL;
		if (node->nd_body != NULL) {
		    entry_bb = BasicBlock::Create("entry_point", f); 
		    BranchInst::Create(entry_bb, bb);
		    bb = entry_bb;

		    DEBUG_LEVEL_INC();
		    val = compile_node(node->nd_body);
		    DEBUG_LEVEL_DEC();
		}
		if (val == NULL) {
		    val = nilVal;
		}
		ReturnInst::Create(val, bb);

		bb = old_bb;
		entry_bb = old_entry_bb;
		lvars = old_lvars;
		current_self = old_self;
		rescue_bb = old_rescue_bb;

		return cast<Value>(f);
	    }
	    break;

	case NODE_DVAR:
	case NODE_LVAR:
	    {
		assert(node->nd_vid > 0);
		
		return new LoadInst(get_lvar(node->nd_vid), "", bb);
	    }
	    break;

	case NODE_GVAR:
	    {
		assert(node->nd_vid > 0);
		assert(node->nd_entry != NULL);

		if (gvarGetFunc == NULL) {
		    // VALUE rb_gvar_get(struct global_entry *entry);
		    gvarGetFunc = cast<Function>(module->getOrInsertFunction("rb_gvar_get", RubyObjTy, PtrTy, NULL));
		}

		std::vector<Value *> params;

		params.push_back(compile_const_pointer(node->nd_entry));

		return CallInst::Create(gvarGetFunc, params.begin(), params.end(), "", bb);
	    }
	    break;

	case NODE_GASGN:
	    {
		assert(node->nd_vid > 0);
		assert(node->nd_value != NULL);
		assert(node->nd_entry != NULL);

		if (gvarSetFunc == NULL) {
		    // VALUE rb_gvar_set(struct global_entry *entry, VALUE val);
		    gvarSetFunc = cast<Function>(module->getOrInsertFunction(
				"rb_gvar_set", 
				RubyObjTy, PtrTy, RubyObjTy, NULL));
		}

		std::vector<Value *> params;

		params.push_back(compile_const_pointer(node->nd_entry));
		params.push_back(compile_node(node->nd_value));

		return CallInst::Create(gvarSetFunc, params.begin(),
			params.end(), "", bb);
	    }
	    break;

	case NODE_CVAR:
	    {
		assert(node->nd_vid > 0);

		if (cvarGetFunc == NULL) {
		    // VALUE rb_vm_cvar_get(VALUE klass, ID id);
		    cvarGetFunc = cast<Function>(module->getOrInsertFunction(
				"rb_vm_cvar_get", 
				RubyObjTy, RubyObjTy, IntTy, NULL));
		}

		std::vector<Value *> params;

		params.push_back(compile_current_class());
		params.push_back(ConstantInt::get(IntTy, (long)node->nd_vid));

		return compile_protected_call(cvarGetFunc, params);
	    }
	    break;

	case NODE_CVASGN:
	    {
		assert(node->nd_vid > 0);
		assert(node->nd_value != NULL);

		if (cvarSetFunc == NULL) {
		    // VALUE rb_vm_cvar_set(VALUE klass, ID id, VALUE val);
		    cvarSetFunc = cast<Function>(module->getOrInsertFunction(
				"rb_vm_cvar_set", 
				RubyObjTy, RubyObjTy, IntTy, RubyObjTy, NULL));
		}

		std::vector<Value *> params;

		params.push_back(compile_current_class());
		params.push_back(ConstantInt::get(IntTy, (long)node->nd_vid));
		params.push_back(compile_node(node->nd_value));

		return CallInst::Create(cvarSetFunc, params.begin(),
			params.end(), "", bb);
	    }
	    break;

	case NODE_MASGN:
	    {
		NODE *rhsn = node->nd_value;
		assert(rhsn != NULL);

		Value *ary = compile_node(rhsn);

		NODE *lhsn = node->nd_head;

		if (lhsn == NULL) {
		    // * = 1, 2
		    return ary;
		}

		assert(lhsn != NULL);
		assert(nd_type(lhsn) == NODE_ARRAY);

		if (rhsnGetFunc == NULL) {
		    // VALUE rb_vm_rhsn_get(VALUE ary, int offset);
		    rhsnGetFunc = cast<Function>(module->getOrInsertFunction(
				"rb_vm_rhsn_get", 
				RubyObjTy, RubyObjTy, Type::Int32Ty, NULL));
		}

		int i = 0;
		NODE *l = lhsn;
		while (l != NULL) {
		    NODE *ln = l->nd_head;

		    std::vector<Value *> params;
		    params.push_back(ary);
		    params.push_back(ConstantInt::get(Type::Int32Ty, i++));
		    Value *elt = CallInst::Create(rhsnGetFunc, params.begin(),
			    params.end(), "", bb);

		    switch (nd_type(ln)) {
			case NODE_LASGN:
			case NODE_DASGN:
			case NODE_DASGN_CURR:
			    {			    
				Value *slot = get_lvar(ln->nd_vid);
				new StoreInst(elt, slot, bb);
			    }
			    break;

			case NODE_IASGN:
			case NODE_IASGN2:
			    compile_ivar_assignment(ln->nd_vid, elt);
			    break;

			case NODE_ATTRASGN:
			    compile_attribute_assign(ln, elt);
			    break;

			case NODE_MASGN:
			    // a,(*b),c = 1, 2, 3; b #=> [2]
			    // This is a strange case but covered by the
			    // RubySpecs.
			    // TODO
			    break; 

			default:
			    compile_node_error("unimplemented MASGN subnode",
					       ln);
		    }
		    l = l->nd_next;
		}

		return ary;
	    }
	    break;

	case NODE_DASGN:
	case NODE_DASGN_CURR:
	case NODE_LASGN:
	    {
		assert(node->nd_vid > 0);
		assert(node->nd_value != NULL);

		Value *slot = get_lvar(node->nd_vid);

		Value *new_val = compile_node(node->nd_value);

		new StoreInst(new_val, slot, bb);

		return new_val;
	    }
	    break;

	case NODE_OP_ASGN_OR:
	    {
		assert(node->nd_recv != NULL);
		assert(node->nd_value != NULL);
		
		Value *recvVal = compile_node(node->nd_recv);

		Value *falseCond = new ICmpInst(ICmpInst::ICMP_EQ, recvVal, falseVal, "", bb);

		Function *f = bb->getParent();

		BasicBlock *falseBB = BasicBlock::Create("", f);
		BasicBlock *elseBB  = BasicBlock::Create("", f);
		BasicBlock *trueBB = BasicBlock::Create("", f);
		BasicBlock *mergeBB = BasicBlock::Create("", f);

		BranchInst::Create(falseBB, trueBB, falseCond, bb);

		bb = trueBB;
		Value *nilCond = new ICmpInst(ICmpInst::ICMP_EQ, recvVal, nilVal, "", bb);
		BranchInst::Create(falseBB, elseBB, nilCond, bb);

		bb = falseBB;
		Value *newRecvVal = compile_node(node->nd_value);
		falseBB = bb;
		BranchInst::Create(mergeBB, bb);

		BranchInst::Create(mergeBB, elseBB);

		bb = mergeBB;	
		PHINode *pn = PHINode::Create(RubyObjTy, "", bb);
		pn->addIncoming(newRecvVal, falseBB);
		pn->addIncoming(recvVal, elseBB);

		return pn;
	    }
	    break;

	case NODE_OP_ASGN_AND:
	    {
		assert(node->nd_recv != NULL);
		assert(node->nd_value != NULL);
		
		Value *recvVal = compile_node(node->nd_recv);

		Function *f = bb->getParent();

		BasicBlock *notNilBB = BasicBlock::Create("", f);
		BasicBlock *elseBB  = BasicBlock::Create("", f);
		BasicBlock *mergeBB = BasicBlock::Create("", f);

		compile_boolean_test(recvVal, notNilBB, elseBB);

		bb = notNilBB;
		Value *newRecvVal = compile_node(node->nd_value);
		notNilBB = bb;
		BranchInst::Create(mergeBB, bb);

		BranchInst::Create(mergeBB, elseBB);

		bb = mergeBB;	
		PHINode *pn = PHINode::Create(RubyObjTy, "", bb);
		pn->addIncoming(newRecvVal, notNilBB);
		pn->addIncoming(recvVal, elseBB);

		return pn;
	    }
	    break;

	case NODE_OP_ASGN1:
	case NODE_OP_ASGN2:
	    {
		assert(node->nd_recv != NULL);
		Value *recv = compile_node(node->nd_recv);

		long type = nd_type(node) == NODE_OP_ASGN1
		    ? node->nd_mid : node->nd_next->nd_mid;

		// a=[0] += 42
		//
		// tmp = a.send(:[], 0)
		// tmp = tmp + 42
		// a.send(:[]=, 0, tmp)

		assert(node->nd_args != NULL);
		assert(node->nd_args->nd_head != NULL);

		// tmp = a.send(:[], 0)

		std::vector<Value *> params;
		SEL sel;
		if (nd_type(node) == NODE_OP_ASGN1) {
		    sel = selAREF;
		}
		else {
		    assert(node->nd_next->nd_vid > 0);
		    sel = mid_to_sel(node->nd_next->nd_vid, 0);
		}
		void *cache = GET_VM()->method_cache_get(sel, false);
		params.push_back(compile_const_pointer(cache));
		params.push_back(recv);
		params.push_back(compile_const_pointer((void *)sel));
		params.push_back(compile_const_pointer(NULL));
		params.push_back(ConstantInt::get(Type::Int8Ty, 0));

		int argc = 0;
		std::vector<Value *> arguments;
		if (nd_type(node) == NODE_OP_ASGN1) {
		    assert(node->nd_args->nd_body != NULL);
		    compile_dispatch_arguments(node->nd_args->nd_body,
			    arguments,
			    &argc);
		}
		params.push_back(ConstantInt::get(Type::Int32Ty, argc));
		for (std::vector<Value *>::iterator i = arguments.begin();
			i != arguments.end(); ++i) {
		    params.push_back(*i);
		}

		Value *tmp = compile_optimized_dispatch_call(sel, argc, params);
		if (tmp == NULL) {
		    tmp = compile_dispatch_call(params);
		}

		// tmp = tmp + 42

		BasicBlock *mergeBB = NULL;
		BasicBlock *touchedBB = NULL;
		BasicBlock *untouchedBB = NULL;
		Value *tmp2;
		NODE *value = nd_type(node) == NODE_OP_ASGN1
		    ? node->nd_args->nd_head : node->nd_value;
		assert(value != NULL);
		if (type == 0 || type == 1) {
		    // 0 means OR, 1 means AND
		    Function *f = bb->getParent();

		    touchedBB = BasicBlock::Create("", f);
		    untouchedBB  = BasicBlock::Create("", f);
		    mergeBB = BasicBlock::Create("merge", f);

		    if (type == 0) {
			compile_boolean_test(tmp, untouchedBB, touchedBB);
		    }
		    else {
			compile_boolean_test(tmp, touchedBB, untouchedBB);
		    }

		    BranchInst::Create(mergeBB, untouchedBB);
		    bb = touchedBB;

		    tmp2 = compile_node(value);
		}
		else {
		    ID mid = nd_type(node) == NODE_OP_ASGN1
			? node->nd_mid : node->nd_next->nd_mid;
		    sel = mid_to_sel(mid, 1);
		    cache = GET_VM()->method_cache_get(sel, false);
		    params.clear();
		    params.push_back(compile_const_pointer(cache));
		    params.push_back(tmp);
		    params.push_back(compile_const_pointer((void *)sel));
		    params.push_back(compile_const_pointer(NULL));
		    params.push_back(ConstantInt::get(Type::Int8Ty, 0));
		    params.push_back(ConstantInt::get(Type::Int32Ty, 1));
		    params.push_back(compile_node(value));

		    tmp2 = compile_optimized_dispatch_call(sel, 1, params);
		    if (tmp2 == NULL) {
			tmp2 = compile_dispatch_call(params);
		    }
		}

		// a.send(:[]=, 0, tmp)
 
		if (nd_type(node) == NODE_OP_ASGN1) {
		    sel = selASET;
		}
		else {
		    assert(node->nd_next->nd_aid > 0);
		    sel = mid_to_sel(node->nd_next->nd_aid, 1);
		}
		cache = GET_VM()->method_cache_get(sel, false);
		params.clear();
		params.push_back(compile_const_pointer(cache));
		params.push_back(recv);
		params.push_back(compile_const_pointer((void *)sel));
		params.push_back(compile_const_pointer(NULL));
		params.push_back(ConstantInt::get(Type::Int8Ty, 0));
		argc++;
		params.push_back(ConstantInt::get(Type::Int32Ty, argc));
		for (std::vector<Value *>::iterator i = arguments.begin();
		     i != arguments.end(); ++i) {
		    params.push_back(*i);
		}
		params.push_back(tmp2);

		Value *ret = compile_optimized_dispatch_call(sel, argc, params);
		if (ret == NULL) {
		    ret = compile_dispatch_call(params);
		}

		if (mergeBB == NULL) {
		    return ret;
		}

		BranchInst::Create(mergeBB, touchedBB);

		bb = mergeBB;	

		PHINode *pn = PHINode::Create(RubyObjTy, "", bb);
		pn->addIncoming(tmp, untouchedBB);
		pn->addIncoming(ret, touchedBB);

		return pn;
	    }
	    break;

	case NODE_XSTR:
	case NODE_DXSTR:
	    {
		Value *str;
		if (nd_type(node) == NODE_DXSTR) {
		    str = compile_dstr(node);
		}
		else {
		    assert(node->nd_lit != 0);
		    str = ConstantInt::get(RubyObjTy, node->nd_lit);
		}

		std::vector<Value *> params;
		void *cache = GET_VM()->method_cache_get(selBackquote, false);
		params.push_back(compile_const_pointer(cache));
		params.push_back(nilVal);
		params.push_back(compile_const_pointer((void *)selBackquote));
		params.push_back(compile_const_pointer(NULL));
		params.push_back(ConstantInt::get(Type::Int8Ty, 0));
		params.push_back(ConstantInt::get(Type::Int32Ty, 1));
		params.push_back(str);

		return compile_dispatch_call(params);
	    }
	    break;

	case NODE_DSTR:
	    return compile_dstr(node);

	case NODE_DREGX:
	    {
		Value *val  = compile_dstr(node);
		const int flag = node->nd_cflag;

		if (newRegexpFunc == NULL) {
		    newRegexpFunc = cast<Function>(module->getOrInsertFunction("rb_reg_new_str",
				RubyObjTy, RubyObjTy, Type::Int32Ty, NULL));
		}

		std::vector<Value *> params;
		params.push_back(val);
		params.push_back(ConstantInt::get(Type::Int32Ty, flag));

		return compile_protected_call(newRegexpFunc, params);
	    }
	    break;

	case NODE_DSYM:
	    {
		Value *val = compile_dstr(node);

		if (strInternFunc == NULL) {
		    strInternFunc = cast<Function>(module->getOrInsertFunction("rb_str_intern_fast",
				RubyObjTy, RubyObjTy, NULL));
		}

		std::vector<Value *> params;
		params.push_back(val);

		return compile_protected_call(strInternFunc, params);
	    }
	    break;

	case NODE_EVSTR:
	    {
		assert(node->nd_body != NULL);
		return compile_node(node->nd_body);
	    }
	    break;

	case NODE_OR:
	    {
		NODE *left = node->nd_1st;
		assert(left != NULL);

		NODE *right = node->nd_2nd;
		assert(right != NULL);

		Function *f = bb->getParent();

		BasicBlock *leftNotFalseBB = BasicBlock::Create("left_not_false", f);
		BasicBlock *leftNotTrueBB = BasicBlock::Create("left_not_true", f);
		BasicBlock *leftTrueBB = BasicBlock::Create("left_is_true", f);
		BasicBlock *rightNotFalseBB = BasicBlock::Create("right_not_false", f);
		BasicBlock *rightTrueBB = BasicBlock::Create("right_is_true", f);
		BasicBlock *failBB = BasicBlock::Create("fail", f);
		BasicBlock *mergeBB = BasicBlock::Create("merge", f);

		Value *leftVal = compile_node(left);
		Value *leftNotFalseCond = new ICmpInst(ICmpInst::ICMP_NE, leftVal, falseVal, "", bb);
		BranchInst::Create(leftNotFalseBB, leftNotTrueBB, leftNotFalseCond, bb);

		bb = leftNotFalseBB;
		Value *leftNotNilCond = new ICmpInst(ICmpInst::ICMP_NE, leftVal, nilVal, "", bb);
		BranchInst::Create(leftTrueBB, leftNotTrueBB, leftNotNilCond, bb);

		bb = leftNotTrueBB;
		Value *rightVal = compile_node(right);
		Value *rightNotFalseCond = new ICmpInst(ICmpInst::ICMP_NE, rightVal, falseVal, "", bb);
		BranchInst::Create(rightNotFalseBB, failBB, rightNotFalseCond, bb);

		bb = rightNotFalseBB;
		Value *rightNotNilCond = new ICmpInst(ICmpInst::ICMP_NE, rightVal, nilVal, "", bb);
		BranchInst::Create(rightTrueBB, failBB, rightNotNilCond, bb);

		BranchInst::Create(mergeBB, leftTrueBB);
		BranchInst::Create(mergeBB, rightTrueBB);
		BranchInst::Create(mergeBB, failBB);

		bb = mergeBB;
		PHINode *pn = PHINode::Create(RubyObjTy, "", mergeBB);
		pn->addIncoming(leftVal, leftTrueBB);
		pn->addIncoming(rightVal, rightTrueBB);
		pn->addIncoming(rightVal, failBB);

		return pn;
	    }
	    break;

	case NODE_AND:
	    {
		NODE *left = node->nd_1st;
		assert(left != NULL);

		NODE *right = node->nd_2nd;
		assert(right != NULL);

		Function *f = bb->getParent();

		BasicBlock *leftNotFalseBB = BasicBlock::Create("left_not_false", f);
		BasicBlock *leftTrueBB = BasicBlock::Create("left_is_true", f);
		BasicBlock *rightNotFalseBB = BasicBlock::Create("right_not_false", f);
		BasicBlock *leftFailBB = BasicBlock::Create("left_fail", f);
		BasicBlock *rightFailBB = BasicBlock::Create("right_fail", f);
		BasicBlock *successBB = BasicBlock::Create("success", f);
		BasicBlock *mergeBB = BasicBlock::Create("merge", f);

		Value *leftVal = compile_node(left);
		Value *leftNotFalseCond = new ICmpInst(ICmpInst::ICMP_NE, leftVal, falseVal, "", bb);
		BranchInst::Create(leftNotFalseBB, leftFailBB, leftNotFalseCond, bb);

		bb = leftNotFalseBB;
		Value *leftNotNilCond = new ICmpInst(ICmpInst::ICMP_NE, leftVal, nilVal, "", bb);
		BranchInst::Create(leftTrueBB, leftFailBB, leftNotNilCond, bb);

		bb = leftTrueBB;
		Value *rightVal = compile_node(right);
		Value *rightNotFalseCond = new ICmpInst(ICmpInst::ICMP_NE, rightVal, falseVal, "", bb);

		BranchInst::Create(rightNotFalseBB, rightFailBB, rightNotFalseCond, bb);

		bb = rightNotFalseBB;
		Value *rightNotNilCond = new ICmpInst(ICmpInst::ICMP_NE, rightVal, nilVal, "", bb);
		BranchInst::Create(successBB, rightFailBB, rightNotNilCond, bb);

		BranchInst::Create(mergeBB, successBB);
		BranchInst::Create(mergeBB, leftFailBB);
		BranchInst::Create(mergeBB, rightFailBB);

		bb = mergeBB;
		PHINode *pn = PHINode::Create(RubyObjTy, "", mergeBB);
		pn->addIncoming(leftVal, leftFailBB);
		pn->addIncoming(rightVal, rightFailBB);
		pn->addIncoming(rightVal, successBB);

		return pn;
	    }
	    break;

	case NODE_IF:
	    {
		Value *condVal = compile_node(node->nd_cond);

		Function *f = bb->getParent();

		BasicBlock *thenBB = BasicBlock::Create("then", f);
		BasicBlock *elseBB  = BasicBlock::Create("else", f);
		BasicBlock *mergeBB = BasicBlock::Create("merge", f);

		compile_boolean_test(condVal, thenBB, elseBB);

		bb = thenBB;
		DEBUG_LEVEL_INC();
		Value *thenVal = node->nd_body != NULL ? compile_node(node->nd_body) : nilVal;
		DEBUG_LEVEL_DEC();
		thenBB = bb;
		BranchInst::Create(mergeBB, thenBB);

		bb = elseBB;
		DEBUG_LEVEL_INC();
		Value *elseVal = node->nd_else != NULL ? compile_node(node->nd_else) : nilVal;
		DEBUG_LEVEL_DEC();
		elseBB = bb;
		BranchInst::Create(mergeBB, elseBB);
		
		bb = mergeBB;
		PHINode *pn = PHINode::Create(RubyObjTy, "iftmp", mergeBB);
		pn->addIncoming(thenVal, thenBB);
		pn->addIncoming(elseVal, elseBB);

		return pn;
	    }
	    break;

	case NODE_CLASS:
	case NODE_SCLASS:
	case NODE_MODULE:
	    {
		assert(node->nd_cpath != NULL);

		Value *classVal;
		if (nd_type(node) == NODE_SCLASS) {
		    classVal = compile_singleton_class(compile_node(node->nd_recv));
		}
		else {
		    assert(node->nd_cpath->nd_mid > 0);
		    ID path = node->nd_cpath->nd_mid;

		    NODE *super = node->nd_super;

		    if (defineClassFunc == NULL) {
			// VALUE rb_vm_define_class(ID path, VALUE outer, VALUE super,
			//			    unsigned char is_module);
			defineClassFunc = cast<Function>(module->getOrInsertFunction(
				    "rb_vm_define_class",
				    RubyObjTy, IntTy, RubyObjTy, RubyObjTy,
				    Type::Int8Ty, NULL));
		    }

		    std::vector<Value *> params;

		    params.push_back(ConstantInt::get(IntTy, (long)path));
		    params.push_back(compile_class_path(node->nd_cpath));
		    params.push_back(super == NULL ? zeroVal : compile_node(super));
		    params.push_back(ConstantInt::get(Type::Int8Ty,
				nd_type(node) == NODE_MODULE ? 1 : 0));

		    classVal = compile_protected_call(defineClassFunc, params);
		}

		NODE *body = node->nd_body;
		if (body != NULL) {
		    assert(nd_type(body) == NODE_SCOPE);
		    if (body->nd_body != NULL) {	
			Value *old_self = current_self;
			current_self = classVal;

			GlobalVariable *old_class = current_opened_class;
			current_opened_class = new GlobalVariable(
				RubyObjTy,
				false,
				GlobalValue::InternalLinkage,
				nilVal,
				"current_opened_class",
				RoxorCompiler::module);
			new StoreInst(classVal, current_opened_class, bb);

			std::map<ID, int *> old_ivar_slots_cache = ivar_slots_cache;
			ivar_slots_cache.clear();

			compile_node(body->nd_body);

			BasicBlock::InstListType &list = bb->getInstList();
			compile_ivar_slots(classVal, list, list.end());

			current_self = old_self;
			current_opened_class = old_class;

			ivar_slots_cache = old_ivar_slots_cache;
		    }
		}

		return classVal;
	    }
	    break;

	case NODE_SUPER:
	case NODE_ZSUPER:
	case NODE_CALL:
	case NODE_FCALL:
	case NODE_VCALL:
	    {
		NODE *recv;
		NODE *args;
		ID mid;

		recv = node->nd_recv;
		args = node->nd_args;
		mid = node->nd_mid;

		if (nd_type(node) == NODE_CALL) {
		    assert(recv != NULL);
		}
		else {
		    assert(recv == NULL);
		}

		const bool block_given = current_block_func != NULL && current_block_node != NULL;
		const bool super_call = nd_type(node) == NODE_SUPER || nd_type(node) == NODE_ZSUPER;

		if (super_call) {
		    mid = current_mid;
		}
		assert(mid > 0);

		Function::ArgumentListType &fargs = bb->getParent()->getArgumentList();
		const int fargs_arity = fargs.size() - 2;

		bool splat_args = false;
		bool positive_arity = false;
		if (nd_type(node) == NODE_ZSUPER) {
		    assert(args == NULL);
		    positive_arity = fargs_arity > 0;
		}
		else {
		    NODE *n = args;
rescan_args:
		    if (n != NULL) {
			switch (nd_type(n)) {
			    case NODE_ARRAY:
				positive_arity = n->nd_alen > 0;
				break;

			    case NODE_SPLAT:
			    case NODE_ARGSPUSH:
			    case NODE_ARGSCAT:
				splat_args = true;
				positive_arity = true;
				break;

			    case NODE_BLOCK_PASS:
				n = n->nd_head;
				if (n != NULL) {
				    goto rescan_args;
				}
				positive_arity = false;
				break;

			    default:
				compile_node_error("invalid call args", n);
			}
		    }
		}

		// Recursive method call optimization.
		if (!block_given && !super_call && !splat_args
		    && positive_arity && mid == current_mid && recv == NULL) {

		    // TODO check if both functions have the same arity (paranoid?)

		    Function *f = bb->getParent();
		    std::vector<Value *> params;

		    Function::arg_iterator arg = f->arg_begin();

		    params.push_back(arg++); // self
		    params.push_back(arg++); // sel 

		    for (NODE *n = args; n != NULL; n = n->nd_next) {
			params.push_back(compile_node(n->nd_head));
		    }

		   return cast<Value>(CallInst::Create(f, params.begin(), params.end(), "", bb));
		}

		// Prepare the dispatcher parameters.
		std::vector<Value *> params;

		// Method cache.
		const SEL sel = mid_to_sel(mid, positive_arity ? 1 : 0);
		void *cache = GET_VM()->method_cache_get(sel, super_call);
		params.push_back(compile_const_pointer(cache));

		// Self.
		params.push_back(recv == NULL ? current_self : compile_node(recv));

		// Selector.
		params.push_back(compile_const_pointer((void *)sel));

		// RubySpec requires that we compile the block *after* the arguments, so we
		// do pass NULL as the block for the moment...
		params.push_back(compile_const_pointer(NULL));
		NODE *real_args = args;
		if (real_args != NULL && nd_type(real_args) == NODE_BLOCK_PASS) {
		    real_args = args->nd_head;
		}

		// Call option.
		const unsigned char call_opt = super_call 
		    ? DISPATCH_SUPER
		    : (nd_type(node) == NODE_VCALL)
			? DISPATCH_VCALL
			: 0;
		params.push_back(ConstantInt::get(Type::Int8Ty, call_opt));

		// Arguments.
		int argc = 0;
		if (nd_type(node) == NODE_ZSUPER) {
		    params.push_back(ConstantInt::get(Type::Int32Ty, fargs_arity));
		    Function::ArgumentListType::iterator iter = fargs.begin();
		    iter++; // skip self
		    iter++; // skip sel
		    while (iter != fargs.end()) {
			params.push_back(iter);
			++iter;
		    }
		    argc = fargs_arity;
		}
		else if (real_args != NULL) {
		    std::vector<Value *> arguments;
		    compile_dispatch_arguments(real_args, arguments, &argc);
		    params.push_back(ConstantInt::get(Type::Int32Ty, argc));
		    for (std::vector<Value *>::iterator i = arguments.begin();
			 i != arguments.end(); ++i) {
			params.push_back(*i);
		    }
		}
		else {
		    params.push_back(ConstantInt::get(Type::Int32Ty, 0));
		}

		// Now compile the block and insert it in the params list!
		Value *blockVal;
		if (args != NULL && nd_type(args) == NODE_BLOCK_PASS) {
		    assert(!block_given);
		    blockVal = compile_block_create(args);
		}
		else {
		    blockVal = block_given
			? compile_block_create() : compile_const_pointer(NULL);
		}
		params[3] = blockVal;

		// Can we optimize the call?
		if (!super_call && !splat_args) {
		    Value *optimizedCall = compile_optimized_dispatch_call(sel, argc, params);
		    if (optimizedCall != NULL) {
			return optimizedCall;
		    }
		}

		// Looks like we can't, just do a regular dispatch then.
		return compile_dispatch_call(params);
	    }
	    break;

	case NODE_ATTRASGN:
	    return compile_attribute_assign(node, NULL);

	case NODE_BREAK:
	case NODE_NEXT:
	case NODE_REDO:
	case NODE_RETURN:
	    {
		const bool within_loop = current_loop_begin_bb != NULL
		    && current_loop_body_bb != NULL
		    && current_loop_end_bb != NULL;

		if (!current_block && !within_loop) {
		    if (nd_type(node) == NODE_BREAK) {
			rb_raise(rb_eLocalJumpError, "unexpected break");
		    }
		    if (nd_type(node) == NODE_NEXT) {
			rb_raise(rb_eLocalJumpError, "unexpected next");
		    }
		    if (nd_type(node) == NODE_REDO) {
			rb_raise(rb_eLocalJumpError, "unexpected redo");
		    }
		}

		Value *val = node->nd_head != NULL
		    ? compile_node(node->nd_head) : nilVal;

		if (within_loop) {
		    if (nd_type(node) == NODE_BREAK) {
			current_loop_exit_val = val;
			BranchInst::Create(current_loop_end_bb, bb);
		    }
		    else if (nd_type(node) == NODE_NEXT) {
			BranchInst::Create(current_loop_begin_bb, bb);
		    }
		    else if (nd_type(node) == NODE_REDO) {
			BranchInst::Create(current_loop_body_bb, bb);
		    }
		    else {
			ReturnInst::Create(val, bb);
		    }
		}
		else {
		    if (nd_type(node) == NODE_BREAK) {
			if (breakFunc == NULL) {
			    // void rb_vm_break(VALUE val);
			    breakFunc = cast<Function>(
				    module->getOrInsertFunction("rb_vm_break", 
					Type::VoidTy, RubyObjTy, NULL));
			}
			std::vector<Value *> params;
			params.push_back(val);
			CallInst::Create(breakFunc, params.begin(),
					 params.end(), "", bb);
			ReturnInst::Create(val, bb);
		    }
		    else if (nd_type(node) == NODE_REDO) {
			assert(entry_bb != NULL);
			BranchInst::Create(entry_bb, bb);
		    }
		    else {
			ReturnInst::Create(val, bb);
		    }
		}
		compile_dead_branch();
		return val;
	    }
	    break;

	case NODE_RETRY:
	    {
		if (begin_bb == NULL) {
		    rb_raise(rb_eSyntaxError, "unexpected retry");
		}
		// TODO raise a SyntaxError if called outside of a "rescue"
		// block.
		BranchInst::Create(begin_bb, bb);
		compile_dead_branch();
		return nilVal;
	    }
	    break;

	case NODE_CONST:
	    assert(node->nd_vid > 0);
	    return compile_const(node->nd_vid, NULL);

	case NODE_CDECL:
	    {
		if (setConstFunc == NULL) {
		    // VALUE rb_vm_set_const(VALUE mod, ID id, VALUE obj);
		    setConstFunc = cast<Function>(module->getOrInsertFunction(
				"rb_vm_set_const", 
				Type::VoidTy, RubyObjTy, IntTy, RubyObjTy, NULL));
		}

		assert(node->nd_value != NULL);

		std::vector<Value *> params;
	
		if (node->nd_vid > 0) {
		    params.push_back(compile_current_class());
		    params.push_back(ConstantInt::get(IntTy, (long)node->nd_vid));
		}
		else {
		    assert(node->nd_else != NULL);
		    params.push_back(compile_class_path(node->nd_else));
		    assert(node->nd_else->nd_mid > 0);
		    params.push_back(ConstantInt::get(IntTy, (long)node->nd_else->nd_mid));
		}
		
		Value *val = compile_node(node->nd_value);
		params.push_back(val);

		CallInst::Create(setConstFunc, params.begin(), params.end(), "", bb);

		return val;
	    }
	    break;

	case NODE_IASGN:
	case NODE_IASGN2:
	    {
		assert(node->nd_vid > 0);
		assert(node->nd_value != NULL);
		return compile_ivar_assignment(node->nd_vid,
			compile_node(node->nd_value));
	    }
	    break;

	case NODE_IVAR:
	    {
		assert(node->nd_vid > 0);
		return compile_ivar_read(node->nd_vid);
	    }
	    break;

	case NODE_LIT:
	case NODE_STR:
	    {
		assert(node->nd_lit != 0);
		return ConstantInt::get(RubyObjTy, (long)node->nd_lit);
	    }
	    break;

	case NODE_ARGSCAT:
	case NODE_ARGSPUSH:
	    {
		assert(node->nd_head != NULL);
		Value *ary = compile_node(node->nd_head);

		if (dupArrayFunc == NULL) {
		    dupArrayFunc = cast<Function>(
			    module->getOrInsertFunction("rb_ary_dup",
				RubyObjTy, RubyObjTy, NULL));
		}

		std::vector<Value *> params;
		params.push_back(ary);

		ary = compile_protected_call(dupArrayFunc, params);

		assert(node->nd_body != NULL);
		Value *other = compile_node(node->nd_body);

		if (catArrayFunc == NULL) {
		    // VALUE rb_vm_ary_cat(VALUE obj);
		    catArrayFunc = cast<Function>(
			    module->getOrInsertFunction("rb_vm_ary_cat",
				RubyObjTy, RubyObjTy, RubyObjTy, NULL));
		}

		params.clear();
		params.push_back(ary);
		params.push_back(other);

		return compile_protected_call(catArrayFunc, params);
	    }
	    break;

	case NODE_SPLAT:
	    {
		assert(node->nd_head != NULL);
		Value *val = compile_node(node->nd_head);

		if (nd_type(node->nd_head) != NODE_ARRAY) {
		    if (toArrayFunc == NULL) {
			// VALUE rb_vm_to_a(VALUE obj);
			toArrayFunc = cast<Function>(
				module->getOrInsertFunction("rb_vm_to_a",
				    RubyObjTy, RubyObjTy, NULL));
		    }

		    std::vector<Value *> params;
		    params.push_back(val);
		    val = compile_protected_call(toArrayFunc, params);
		}

		return val;
	    }
	    break;

	case NODE_ARRAY:
	case NODE_ZARRAY:
	case NODE_VALUES:
	    {
		if (newArrayFunc == NULL) {
		    // VALUE rb_ary_new_fast(int argc, ...);
		    std::vector<const Type *> types;
		    types.push_back(Type::Int32Ty);
		    FunctionType *ft = FunctionType::get(RubyObjTy, types, true);
		    newArrayFunc = cast<Function>(module->getOrInsertFunction("rb_ary_new_fast", ft));
		}

		std::vector<Value *> params;

		if (nd_type(node) == NODE_ZARRAY) {
		    params.push_back(ConstantInt::get(Type::Int32Ty, 0));
		}
		else {
		    const int count = node->nd_alen;
		    NODE *n = node;
		    
		    params.push_back(ConstantInt::get(Type::Int32Ty, count));

		    for (int i = 0; i < count; i++) {
			assert(n->nd_head != NULL);
			params.push_back(compile_node(n->nd_head));
			n = n->nd_next;
		    }
		}

		return cast<Value>(CallInst::Create(newArrayFunc, params.begin(), params.end(), "", bb));
	    }
	    break;

	case NODE_HASH:
	    {
		if (newHashFunc == NULL) {
		    // VALUE rb_hash_new_fast(int argc, ...);
		    std::vector<const Type *> types;
		    types.push_back(Type::Int32Ty);
		    FunctionType *ft = FunctionType::get(RubyObjTy, types, true);
		    newHashFunc = cast<Function>(module->getOrInsertFunction("rb_hash_new_fast", ft));
		}

		std::vector<Value *> params;

		if (node->nd_head != NULL) {
		    assert(nd_type(node->nd_head) == NODE_ARRAY);
		    const int count = node->nd_head->nd_alen;
		    assert(count % 2 == 0);
		    NODE *n = node->nd_head;

		    params.push_back(ConstantInt::get(Type::Int32Ty, count));

		    for (int i = 0; i < count; i += 2) {
			Value *key = compile_node(n->nd_head);
			n = n->nd_next;
			Value *val = compile_node(n->nd_head);
			n = n->nd_next;

			params.push_back(key);
			params.push_back(val);
		    }
		}
		else {
		    params.push_back(ConstantInt::get(Type::Int32Ty, 0));
		}

		return cast<Value>(CallInst::Create(newHashFunc, 
			    params.begin(), params.end(), "", bb));
	    }
	    break;

	case NODE_DOT2:
	case NODE_DOT3:
	    {
		if (newRangeFunc == NULL) {
		    // VALUE rb_range_new(VALUE beg, VALUE end, int exclude_end);
		    newRangeFunc = cast<Function>(module->getOrInsertFunction("rb_range_new",
			RubyObjTy, RubyObjTy, RubyObjTy, RubyObjTy, NULL));
		}

		assert(node->nd_beg != NULL);
		assert(node->nd_end != NULL);

		std::vector<Value *> params;
		params.push_back(compile_node(node->nd_beg));
		params.push_back(compile_node(node->nd_end));
		params.push_back(nd_type(node) == NODE_DOT2 ? falseVal : trueVal);

		return cast<Value>(CallInst::Create(newRangeFunc,
			    params.begin(), params.end(), "", bb));
	    }
	    break;

	case NODE_BLOCK:
	    {
		NODE *n = node;
		Value *val = NULL;

		DEBUG_LEVEL_INC();
		while (n != NULL && nd_type(n) == NODE_BLOCK) {
		    val = n->nd_head == NULL ? nilVal : compile_node(n->nd_head);
		    n = n->nd_next;
		}
		DEBUG_LEVEL_DEC();

		return val;
	    }
	    break;

	case NODE_MATCH2:
	case NODE_MATCH3:
	    {
		assert(node->nd_recv);
		assert(node->nd_value);

		Value *reSource = compile_node(node->nd_recv);
		Value *reTarget = compile_node(node->nd_value);

		std::vector<Value *> params;
		void *cache = GET_VM()->method_cache_get(selEqTilde, false);
		params.push_back(compile_const_pointer(cache));
		params.push_back(reTarget);
		params.push_back(compile_const_pointer((void *)selEqTilde));
		params.push_back(compile_const_pointer(NULL));
		params.push_back(ConstantInt::get(Type::Int8Ty, 0));
		params.push_back(ConstantInt::get(Type::Int32Ty, 1));
		params.push_back(reSource);

		return compile_dispatch_call(params);
	    }
	    break;

#if 0 // TODO
	case NODE_CFUNC:
	    {
	    }
#endif

	case NODE_VALIAS:
	    {
		if (valiasFunc == NULL) {
		    // void rb_alias_variable(ID from, ID to);
		    valiasFunc = cast<Function>(module->getOrInsertFunction("rb_alias_variable",
				Type::VoidTy, IntTy, IntTy, NULL));
		}

		std::vector<Value *> params;

		assert(node->u1.id > 0 && node->u2.id > 0);
		params.push_back(ConstantInt::get(IntTy, node->u1.id));
		params.push_back(ConstantInt::get(IntTy, node->u2.id));

		CallInst::Create(valiasFunc, params.begin(), params.end(), "", bb);

		return nilVal;
	    }
	    break;

	case NODE_ALIAS:
	    {
		if (aliasFunc == NULL) {
		    // void rb_vm_alias(VALUE outer, ID from, ID to, unsigned char is_var);
		    aliasFunc = cast<Function>(module->getOrInsertFunction("rb_vm_alias",
				Type::VoidTy, RubyObjTy, IntTy, IntTy, NULL));
		}

		std::vector<Value *> params;

		params.push_back(compile_current_class());
		params.push_back(ConstantInt::get(IntTy, node->u1.node->u1.node->u2.id));
		params.push_back(ConstantInt::get(IntTy, node->u2.node->u1.node->u2.id));

		compile_protected_call(aliasFunc, params);

		return nilVal;
	    }
	    break;

	case NODE_DEFINED:
	    {
		assert(node->nd_head != NULL);

		return compile_defined_expression(node->nd_head);
	    }
	    break;

	case NODE_DEFN:
	case NODE_DEFS:
	    {
		ID mid = node->nd_mid;
		assert(mid > 0);

		NODE *body = node->nd_defn;
		assert(body != NULL);

		const bool singleton_method = nd_type(node) == NODE_DEFS;

		current_mid = mid;
		current_instance_method = !singleton_method;

		DEBUG_LEVEL_INC();
		Value *val = compile_node(body);
		assert(Function::classof(val));
		Function *new_function = cast<Function>(val);
		DEBUG_LEVEL_DEC();

		current_mid = 0;
		current_instance_method = false;

		Value *classVal;
		if (singleton_method) {
		    assert(node->nd_recv != NULL);
		    classVal = compile_singleton_class(compile_node(node->nd_recv));
		}
		else {
		    classVal = compile_current_class();
		}

		if (prepareMethodFunc == NULL) {
		    // void rb_vm_prepare_method(Class klass, SEL sel,
		    //				 Function *f, NODE *node);
		    prepareMethodFunc = 
			cast<Function>(module->getOrInsertFunction(
				    "rb_vm_prepare_method",
				    Type::VoidTy, RubyObjTy, PtrTy, PtrTy,
				    PtrTy, NULL));
		}

		std::vector<Value *> params;

		params.push_back(classVal);

		rb_vm_arity_t arity = rb_vm_node_arity(body);
		const SEL sel = mid_to_sel(mid, arity.real);
		params.push_back(compile_const_pointer((void *)sel));

		params.push_back(compile_const_pointer(new_function));
		rb_objc_retain((void *)body);
		params.push_back(compile_const_pointer(body));

		CallInst::Create(prepareMethodFunc, params.begin(),
			params.end(), "", bb);

		return nilVal;
	    }
	    break;

	case NODE_UNDEF:
	    {
		if (undefFunc == NULL) {
		    // VALUE rb_vm_undef(VALUE klass, ID name);
		    undefFunc =
			cast<Function>(module->getOrInsertFunction(
				"rb_vm_undef",
				Type::VoidTy, RubyObjTy, IntTy, NULL));
		}

		assert(node->u2.node != NULL);
		ID name = node->u2.node->u1.id;

		std::vector<Value *> params;
		params.push_back(compile_current_class());
		params.push_back(ConstantInt::get(IntTy, name));

		compile_protected_call(undefFunc, params);

		return nilVal;
	    }
	    break;

	case NODE_TRUE:
	    return trueVal;

	case NODE_FALSE:
	    return falseVal;

	case NODE_NIL:
	    return nilVal;

	case NODE_SELF:
	    return current_self;

	case NODE_NTH_REF:
	case NODE_BACK_REF:
	    {
		char code = (char)node->nd_nth;

		if (getSpecialFunc == NULL) {
		    // VALUE rb_vm_get_special(char code);
		    getSpecialFunc =
			cast<Function>(module->getOrInsertFunction("rb_vm_get_special",
				RubyObjTy, Type::Int8Ty, NULL));
		}

		std::vector<Value *> params;
		params.push_back(ConstantInt::get(Type::Int8Ty, code));

		return CallInst::Create(getSpecialFunc, params.begin(), params.end(), "", bb);
	    }
	    break;

	case NODE_BEGIN:
	    return node->nd_body == NULL ? nilVal : compile_node(node->nd_body);

	case NODE_RESCUE:
	    {
		assert(node->nd_head != NULL);
		assert(node->nd_resq != NULL);

		Function *f = bb->getParent();

		BasicBlock *old_begin_bb = begin_bb;
		begin_bb = BasicBlock::Create("begin", f);

		BasicBlock *old_rescue_bb = rescue_bb;
		BasicBlock *new_rescue_bb = BasicBlock::Create("rescue", f);
		BasicBlock *merge_bb = BasicBlock::Create("merge", f);

		// Begin code.
		BranchInst::Create(begin_bb, bb);
		bb = begin_bb;
		rescue_bb = new_rescue_bb;
		Value *begin_val = compile_node(node->nd_head);
		BasicBlock *real_begin_bb = bb;
		rescue_bb = old_rescue_bb;
		BranchInst::Create(merge_bb, bb);
		
		// Landing pad header.
		bb = new_rescue_bb;
		compile_landing_pad_header();

		// Landing pad code.
		Value *rescue_val = compile_node(node->nd_resq);
		new_rescue_bb = bb;

		// Landing pad footer.
		compile_landing_pad_footer();

		BranchInst::Create(merge_bb, bb);

		PHINode *pn = PHINode::Create(RubyObjTy, "rescue_result",
			merge_bb);
		pn->addIncoming(begin_val, real_begin_bb);
		pn->addIncoming(rescue_val, new_rescue_bb);
		bb = merge_bb;

		begin_bb = old_begin_bb;

		return pn;
	    }
	    break;

	case NODE_RESBODY:
	    {
		NODE *n = node;

		Function *f = bb->getParent();
		BasicBlock *merge_bb = BasicBlock::Create("merge", f);
		BasicBlock *handler_bb = NULL;

		std::vector<std::pair<Value *, BasicBlock *> > handlers;

		while (n != NULL) {
		    std::vector<Value *> exceptions_to_catch;

		    if (n->nd_args == NULL) {
			// catch StandardError exceptions by default
			exceptions_to_catch.push_back(
				ConstantInt::get(RubyObjTy, 
				    (long)rb_eStandardError));
		    }
		    else {
			NODE *n2 = n->nd_args;
			if (nd_type(n2) == NODE_ARRAY) {
			    while (n2 != NULL) {
				exceptions_to_catch.push_back(compile_node(
					    n2->nd_head));
				n2 = n2->nd_next;
			    }
			}
			else {
			    exceptions_to_catch.push_back(compile_node(n2));
			}
		    }

		    Function *isEHActiveFunc = NULL;
		    if (isEHActiveFunc == NULL) {
			// bool rb_vm_is_eh_active(int argc, ...);
			std::vector<const Type *> types;
			types.push_back(Type::Int32Ty);
			FunctionType *ft = FunctionType::get(Type::Int8Ty,
				types, true);
			isEHActiveFunc = cast<Function>(
				module->getOrInsertFunction(
				    "rb_vm_is_eh_active", ft));
		    }

		    const int size = exceptions_to_catch.size();
		    exceptions_to_catch.insert(exceptions_to_catch.begin(), 
			    ConstantInt::get(Type::Int32Ty, size));

		    Value *handler_active = CallInst::Create(isEHActiveFunc, 
			    exceptions_to_catch.begin(), 
			    exceptions_to_catch.end(), "", bb);

		    Value *is_handler_active = new ICmpInst(ICmpInst::ICMP_EQ,
			    handler_active,
			    ConstantInt::get(Type::Int8Ty, 1), "", bb);
		    
		    handler_bb = BasicBlock::Create("handler", f);
		    BasicBlock *next_handler_bb =
			BasicBlock::Create("handler", f);

		    BranchInst::Create(handler_bb, next_handler_bb,
			    is_handler_active, bb);

		    bb = handler_bb;
		    assert(n->nd_body != NULL);
		    Value *header_val = compile_node(n->nd_body);
		    handler_bb = bb;
		    BranchInst::Create(merge_bb, bb);

		    handlers.push_back(std::pair<Value *, BasicBlock *>
			    (header_val, handler_bb));

		    bb = handler_bb = next_handler_bb;

		    n = n->nd_head;
		}

		bb = handler_bb;
		compile_rethrow_exception();

		bb = merge_bb;
		assert(handlers.size() > 0);
		if (handlers.size() == 1) {
		    return handlers.front().first;
		}
		else {
		    PHINode *pn = PHINode::Create(RubyObjTy, "op_tmp", bb);
		    std::vector<std::pair<Value *, BasicBlock *> >::iterator
			iter = handlers.begin();
		    while (iter != handlers.end()) {
			pn->addIncoming(iter->first, iter->second);
			++iter;
		    }
		    return pn;
		}
	    }
	    break;

	case NODE_ERRINFO:
	    {
		if (popExceptionFunc == NULL) {
		    // VALUE rb_vm_pop_exception(void);
		    popExceptionFunc = cast<Function>(
			    module->getOrInsertFunction("rb_vm_pop_exception", 
				RubyObjTy, NULL));
		}
		return CallInst::Create(popExceptionFunc, "", bb);
	    }
	    break;

	case NODE_ENSURE:
	    {
		assert(node->nd_head != NULL);
		assert(node->nd_ensr != NULL);

		Function *f = bb->getParent();
		BasicBlock *ensure_bb = BasicBlock::Create("ensure", f);
		Value *val;

		if (nd_type(node->nd_head) != NODE_RESCUE) {
		    // An ensure without a rescue (Ex. begin; ...; ensure; end)
		    // we must call the head within an exception handler, then
		    // branch on the ensure block, then re-raise the potential
		    // exception.
		    BasicBlock *new_rescue_bb = BasicBlock::Create("rescue", f);
		    BasicBlock *old_rescue_bb = rescue_bb;

		    rescue_bb = new_rescue_bb;
		    DEBUG_LEVEL_INC();
		    val = compile_node(node->nd_head);
		    DEBUG_LEVEL_DEC();
		    rescue_bb = old_rescue_bb;
		    BranchInst::Create(ensure_bb, bb);

		    bb = new_rescue_bb;
		    compile_landing_pad_header();
		    compile_node(node->nd_ensr);
		    compile_rethrow_exception();
		    //compile_landing_pad_footer();

		    bb = ensure_bb;
		    compile_node(node->nd_ensr);
		}
		else {
		    val = compile_node(node->nd_head);
		    BranchInst::Create(ensure_bb, bb);

		    bb = ensure_bb;
		    compile_node(node->nd_ensr);
		}

		return val;//nilVal;
	    }
	    break;

	case NODE_WHILE:
	case NODE_UNTIL:
	    {
		assert(node->nd_body != NULL);
		assert(node->nd_cond != NULL);

		Function *f = bb->getParent();

		BasicBlock *loopBB = BasicBlock::Create("loop", f);
		BasicBlock *bodyBB = BasicBlock::Create("body", f);
		BasicBlock *afterBB = BasicBlock::Create("after", f);

		const bool first_pass_free = node->nd_state == 0;

		BranchInst::Create(first_pass_free ? bodyBB : loopBB, bb);

		bb = loopBB;
		Value *condVal = compile_node(node->nd_cond);

		if (nd_type(node) == NODE_WHILE) {
		    compile_boolean_test(condVal, bodyBB, afterBB);
		}
		else {
		    compile_boolean_test(condVal, afterBB, bodyBB);
		}

		BasicBlock *old_current_loop_begin_bb = current_loop_begin_bb;
		BasicBlock *old_current_loop_body_bb = current_loop_body_bb;
		BasicBlock *old_current_loop_end_bb = current_loop_end_bb;
		Value *old_current_loop_exit_val = current_loop_exit_val;

		current_loop_begin_bb = loopBB;
		current_loop_body_bb = bodyBB;
		current_loop_end_bb = afterBB;
		current_loop_exit_val = NULL;

		bb = bodyBB;
		compile_node(node->nd_body);	
		bodyBB = bb;

		BranchInst::Create(loopBB, bb);

		bb = afterBB;

		Value *retval = current_loop_exit_val;
		if (retval == NULL) {
		    retval = nilVal;
		}

		current_loop_begin_bb = old_current_loop_begin_bb;
		current_loop_body_bb = old_current_loop_body_bb;
		current_loop_end_bb = old_current_loop_end_bb;
		current_loop_exit_val = old_current_loop_exit_val;

		return retval;
	    }
	    break;

	case NODE_FOR:
	case NODE_ITER:
	    {
		std::map<ID, Value *> old_dvars = dvars;

		BasicBlock *old_current_loop_begin_bb = current_loop_begin_bb;
		BasicBlock *old_current_loop_body_bb = current_loop_body_bb;
		BasicBlock *old_current_loop_end_bb = current_loop_end_bb;
		current_loop_begin_bb = current_loop_end_bb = NULL;
		Function *old_current_block_func = current_block_func;
		NODE *old_current_block_node = current_block_node;
		ID old_current_mid = current_mid;
		bool old_current_block = current_block;
		current_mid = 0;
		current_block = true;

		assert(node->nd_body != NULL);
		Value *block = compile_node(node->nd_body);	
		assert(Function::classof(block));

		current_loop_begin_bb = old_current_loop_begin_bb;
		current_loop_body_bb = old_current_loop_body_bb;
		current_loop_end_bb = old_current_loop_end_bb;
		current_mid = old_current_mid;
		current_block = old_current_block;

		current_block_func = cast<Function>(block);
		current_block_node = node->nd_body;
		rb_objc_retain((void *)current_block_node);

		Value *caller;
		assert(node->nd_iter != NULL);
		if (nd_type(node) == NODE_ITER) {
		    caller = compile_node(node->nd_iter);
		}
		else {
		    // dispatch #each on the receiver
		    std::vector<Value *> params;

		    void *cache = GET_VM()->method_cache_get(selEach, false);
		    params.push_back(compile_const_pointer(cache));
		    params.push_back(compile_node(node->nd_iter));
		    params.push_back(compile_const_pointer((void *)selEach));
		    params.push_back(compile_block_create());
		    params.push_back(ConstantInt::get(Type::Int8Ty, 0));
		    params.push_back(ConstantInt::get(Type::Int32Ty, 0));

		    caller = compile_dispatch_call(params);
		}

		current_block_func = old_current_block_func;
		current_block_node = old_current_block_node;
		dvars = old_dvars;

		return caller;
	    }
	    break;

	case NODE_YIELD:
	    {
		if (yieldFunc == NULL) {
		    // VALUE rb_vm_yield_args(int argc, ...)
		    std::vector<const Type *> types;
		    types.push_back(Type::Int32Ty);
		    FunctionType *ft = FunctionType::get(RubyObjTy, types, true);
		    yieldFunc = cast<Function>(module->getOrInsertFunction("rb_vm_yield_args", ft));
		}

		std::vector<Value *>params;

		if (node->nd_head == NULL) {
		    params.push_back(ConstantInt::get(Type::Int32Ty, 0));
		}
		else {
		    NODE *args = node->nd_head;
		    const int argc = args->nd_alen;
    		    params.push_back(ConstantInt::get(Type::Int32Ty, argc));
		    for (int i = 0; i < argc; i++) {
			params.push_back(compile_node(args->nd_head));
			args = args->nd_next;
		    }
		}

		return CallInst::Create(yieldFunc, params.begin(), params.end(), "", bb);

	    }
	    break;

	case NODE_COLON2:
	    {
		assert(node->nd_mid > 0);
		if (rb_is_const_id(node->nd_mid)) {
		    // Constant
		    assert(node->nd_head != NULL);
		    return compile_const(node->nd_mid, compile_node(node->nd_head));
		}
		else {
		    // Method call
		    abort(); // TODO
		}
	    }
	    break;

	case NODE_COLON3:
	    assert(node->nd_mid > 0);
	    return compile_const(node->nd_mid,
		    ConstantInt::get(RubyObjTy, (long)rb_cObject));

	case NODE_CASE:
	    {
		Function *f = bb->getParent();
		BasicBlock *caseMergeBB = BasicBlock::Create("case_merge", f);

		PHINode *pn = PHINode::Create(RubyObjTy, "case_tmp",
			caseMergeBB);

		Value *comparedToVal = NULL;

		if (node->nd_head != NULL) {
		    comparedToVal = compile_node(node->nd_head);
                }

		NODE *subnode = node->nd_body;

		assert(subnode != NULL);
		assert(nd_type(subnode) == NODE_WHEN);
		while ((subnode != NULL) && (nd_type(subnode) == NODE_WHEN)) {
		    NODE *valueNode = subnode->nd_head;
		    assert(valueNode != NULL);

		    BasicBlock *thenBB = BasicBlock::Create("then", f);

		    compile_when_arguments(valueNode, comparedToVal, thenBB);
		    BasicBlock *nextWhenBB = bb;

		    bb = thenBB;
		    Value *thenVal = subnode->nd_body != NULL
			? compile_node(subnode->nd_body) : nilVal;
		    thenBB = bb;

		    BranchInst::Create(caseMergeBB, thenBB);
		    pn->addIncoming(thenVal, thenBB);

		    bb = nextWhenBB;

		    subnode = subnode->nd_next;
		}

		Value *elseVal = nilVal;
		if (subnode != NULL) { // else
		    elseVal = compile_node(subnode);
		}
		BranchInst::Create(caseMergeBB, bb);
		pn->addIncoming(elseVal, bb);

		bb = caseMergeBB;

		return pn;
	    }
	    break;

	default:
	    compile_node_error("not implemented", node);
    }

    return NULL;
}

Function *
RoxorCompiler::compile_main_function(NODE *node)
{
    rb_objc_retain((void *)node);

    current_instance_method = true;

    Value *val = compile_node(node);
    assert(Function::classof(val));
    Function *function = cast<Function>(val);

    Value *klass = ConstantInt::get(RubyObjTy, (long)rb_cTopLevel);
    BasicBlock::InstListType &list = 
	function->getEntryBlock().getInstList();
    compile_ivar_slots(klass, list, list.begin());

    return function;
}

Function *
RoxorCompiler::compile_read_attr(ID name)
{
    Function *f = cast<Function>(module->getOrInsertFunction("",
		RubyObjTy, RubyObjTy, PtrTy, NULL));

    Function::arg_iterator arg = f->arg_begin();
    current_self = arg++;

    bb = BasicBlock::Create("EntryBlock", f);

    Value *val = compile_ivar_read(name);

    ReturnInst::Create(val, bb);

    return f;
}

Function *
RoxorCompiler::compile_write_attr(ID name)
{
    Function *f = cast<Function>(module->getOrInsertFunction("",
		RubyObjTy, RubyObjTy, PtrTy, RubyObjTy, NULL));

    Function::arg_iterator arg = f->arg_begin();
    current_self = arg++;
    arg++; // sel
    Value *new_val = arg++; // 1st argument

    bb = BasicBlock::Create("EntryBlock", f);

    Value *val = compile_ivar_assignment(name, new_val);

    ReturnInst::Create(val, bb);

    return f;
}

// VM primitives

#define MAX_ARITY 20

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

extern "C"
VALUE
rb_vm_define_class(ID path, VALUE outer, VALUE super, unsigned char is_module)
{
    assert(path > 0);

    if (GET_VM()->current_class != NULL) {
	outer = (VALUE)GET_VM()->current_class;
    }

    VALUE klass;
    if (rb_const_defined_at(outer, path)) {
	klass = rb_const_get_at(outer, path);
	if (!is_module && super != 0) {
	    assert(RCLASS_SUPER(klass) == super);
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
VALUE
rb_vm_ivar_get(VALUE obj, ID name, int *slot_cache)
{
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

extern "C" void rb_print_undef(VALUE, ID, int);

extern "C"
void
rb_vm_alias(VALUE outer, ID name, ID def)
{
    // TODO reassign klass if called within module_eval

    rb_frozen_class_p(outer);
    if (outer == rb_cObject) {
        rb_secure(4);
    }
    Class klass = (Class)outer;

    // Find the implementation of the original method first.
    const char *def_str = rb_id2name(def);
    SEL def_sel = sel_registerName(def_str);
    Method def_method = class_getInstanceMethod(klass, def_sel);
    bool def_qualified = false;
    if (def_method == NULL  && def_str[strlen(def_str) - 1] != ':') {
	char tmp[100];
	snprintf(tmp, sizeof tmp, "%s:", def_str);
	def_sel = sel_registerName(tmp);
 	def_method = class_getInstanceMethod(klass, def_sel);
	def_qualified = true;
    }
    if (def_method == NULL) {
	rb_print_undef((VALUE)klass, def, 0);
    }
    IMP def_imp = method_getImplementation(def_method);
    const char *def_types = method_getTypeEncoding(def_method);

    // Get the NODE*.
    NODE *node = GET_VM()->method_node_get(def_imp);
    if (node == NULL) {
	rb_raise(rb_eArgError, "cannot alias non-Ruby method `%s'", sel_getName(def_sel));
    }

    // Do the method aliasing.
    const char *new_str = rb_id2name(name);
    SEL new_sel;
    if (def_qualified && new_str[strlen(new_str) - 1] != ':') {
	char tmp[100];
	snprintf(tmp, sizeof tmp, "%s:", new_str);
	new_sel = sel_registerName(tmp);
    }
    else {
	new_sel = sel_registerName(new_str);
    }
    GET_VM()->add_method(klass, new_sel, def_imp, node, def_types);
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

extern "C"
void
rb_vm_prepare_method(Class klass, SEL sel, Function *func, NODE *node)
{
    if (GET_VM()->current_class != NULL) {
	klass = GET_VM()->current_class;
    }

    IMP imp = GET_VM()->compile(func);

    rb_vm_define_method(klass, sel, imp, node, false);
}

extern "C"
bool
rb_vm_lookup_method2(Class klass, ID mid, SEL *psel, IMP *pimp, NODE **pnode)
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
rb_vm_lookup_method(Class klass, SEL sel, IMP *pimp, NODE **pnode)
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
rb_vm_define_attr(Class klass, const char *name, bool read, bool write, int noex)
{
    assert(klass != NULL);
    assert(read || write);

    RoxorCompiler *compiler = new RoxorCompiler("");

    char buf[100];
    snprintf(buf, sizeof buf, "@%s", name);
    ID iname = rb_intern(buf);

    if (read) {
	Function *f = compiler->compile_read_attr(iname);
	IMP imp = GET_VM()->compile(f);

	NODE *node = NEW_CFUNC(imp, 0);
	NODE *body = NEW_FBODY(NEW_METHOD(node, klass, noex), 0);
	rb_objc_retain(body);

	rb_vm_define_method(klass, sel_registerName(name), imp, body, false);
    }

    if (write) {
	Function *f = compiler->compile_write_attr(iname);
	IMP imp = GET_VM()->compile(f);

	NODE *node = NEW_CFUNC(imp, 1);
	NODE *body = NEW_FBODY(NEW_METHOD(node, klass, noex), 0);
	rb_objc_retain(body);

	snprintf(buf, sizeof buf, "%s=:", name);
	rb_vm_define_method(klass, sel_registerName(buf), imp, body, false);
    }

    delete compiler;
}

extern "C"
void 
rb_vm_define_method(Class klass, SEL sel, IMP imp, NODE *node, bool direct)
{
    assert(klass != NULL);
    assert(node != NULL);

    const rb_vm_arity_t arity = rb_vm_node_arity(node);
    assert(arity.real < MAX_ARITY);

    const char *sel_name = sel_getName(sel);
    const bool genuine_selector = sel_name[strlen(sel_name) - 1] == ':';

    int oc_arity = genuine_selector ? arity.real : 0;
    bool redefined = direct;

define_method:
    char *types;
    Method method = class_getInstanceMethod(klass, sel);
    if (method != NULL) {
	types = (char *)method_getTypeEncoding(method);
    }
    else {
	// TODO look at informal protocols list
	types = (char *)alloca(oc_arity + 4);
	types[0] = '@';
	types[1] = '@';
	types[2] = ':';
	for (int i = 0; i < oc_arity; i++) {
	    types[3 + i] = '@';
	}
	types[3 + oc_arity] = '\0';
    }

    GET_VM()->add_method(klass, sel, imp, node, types);

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
VALUE
rb_vm_rhsn_get(VALUE obj, int offset)
{
    if (TYPE(obj) == T_ARRAY) {
	if (offset < RARRAY_LEN(obj)) {
	    return OC2RB(CFArrayGetValueAtIndex((CFArrayRef)obj, offset));
	}
    }
    else if (offset == 0) {
	return obj;
    }
    return Qnil;
}

static inline VALUE
__rb_vm_rcall(VALUE self, NODE *node, IMP pimp, const rb_vm_arity_t &arity,
	      int argc, const VALUE *argv)
{
    // TODO investigate why this function is not inlined!
    if ((arity.real != argc) || (arity.max == -1)) {
	VALUE *new_argv = (VALUE *)alloca(sizeof(VALUE) * arity.real);
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
	argv = new_argv;
	argc = arity.real;
    }

    assert(pimp != NULL);

    VALUE (*imp)(VALUE, SEL, ...) = (VALUE (*)(VALUE, SEL, ...))pimp;

    switch (argc) {
	case 0:
	    return (*imp)(self, 0);
	case 1:
	    return (*imp)(self, 0, argv[0]);
	case 2:
	    return (*imp)(self, 0, argv[0], argv[1]);		
	case 3:
	    return (*imp)(self, 0, argv[0], argv[1], argv[2]);
	case 4:
	    return (*imp)(self, 0, argv[0], argv[1], argv[2], argv[3]);
	case 5:
	    return (*imp)(self, 0, argv[0], argv[1], argv[2], argv[3], argv[4]);
	case 6:
	    return (*imp)(self, 0, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5]);
	case 7:
	    return (*imp)(self, 0, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5], argv[6]);
	case 8:
	    return (*imp)(self, 0, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5], argv[6], argv[7]);
	case 9:
	    return (*imp)(self, 0, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5], argv[6], argv[7], argv[8]);
    }	
    printf("invalid argc %d\n", argc);
    abort();
    return Qnil;
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

#if ROXOR_VM_DEBUG
    printf("locating super method %s of class %s in ancestor chain %s\n", 
	    sel_getName(sel), rb_class2name(klass), RSTRING_PTR(rb_inspect(ary)));
    printf("callstack: ");
    for (i = callstack_n - 1; i >= 0; i--) {
	printf("%p ", callstack[i]);
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
		VALUE tmp;
		Method method;

                k = RARRAY_AT(ary, i + 1);

		tmp = RCLASS_SUPER(k);
		RCLASS_SUPER(k) = 0;
		method = class_getInstanceMethod((Class)k, sel);
		RCLASS_SUPER(k) = tmp;

		if (method != NULL) {
		    IMP imp = method_getImplementation(method);

		    bool on_stack = false;
		    for (int j = callstack_n - 1; j >= 0; j--) {
			void *start = NULL;
			if (GET_VM()->symbolize_call_address(callstack[j], &start, NULL, NULL, 0)) {
			    if (start == (void *)imp) {
				on_stack = true;
				break;
			    }
			}
		    }

		    if (!on_stack) {
#if ROXOR_VM_DEBUG
			printf("returning method implementation from class/module %s\n", rb_class2name(k));
#endif
			return method;
		    }
		}
            }
        }
    }

    printf("could not identify the superclass of %s from the ancestors chain %s\n",
                class_getName((Class)klass), RSTRING_PTR(rb_inspect(ary)));
    abort();
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
    VALUE message = rb_const_get(exc, rb_intern("message"));
    args[n++] = rb_funcall(message, '!', 3, rb_str_new2(format), obj, argv[0]);
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
    if (argc == 0 && buf[n - 1] == ':') {
	buf[n - 1] = '\0';
    }
    new_argv[0] = ID2SYM(rb_intern(buf));
    MEMCPY(&new_argv[1], argv, VALUE, argc);

    return rb_vm_call(obj, selMethodMissing, argc + 1, new_argv, false);
}

static inline VALUE
__rb_vm_dispatch(struct mcache *cache, VALUE self, Class klass, SEL sel, 
		 unsigned char opt, int argc, const VALUE *argv)
{
    assert(cache != NULL);

    if (klass == NULL) {
	klass = (Class)CLASS_OF(self);
    }

#if ROXOR_VM_DEBUG
    const bool cached = cache->flag != 0;
#endif

    if (cache->flag == 0) {
recache:
	Method method;
	if (opt == DISPATCH_SUPER) {
	    method = rb_vm_super_lookup((VALUE)klass, sel, NULL);
	}
	else {
	    method = class_getInstanceMethod(klass, sel);
	}

#define rcache cache->as.rcall
#define ocache cache->as.ocall

	if (method != NULL) {
	    IMP imp = method_getImplementation(method);
	    NODE *node = GET_VM()->method_node_get(imp);

	    if (node != NULL) {
		// ruby call
		cache->flag = MCACHE_RCALL;
		rcache.klass = klass;
		rcache.imp = imp;
		rcache.node = node;
		rcache.arity = rb_vm_node_arity(node);
	    }
	    else {
		// objc call
		cache->flag = MCACHE_OCALL;
		ocache.klass = klass;
		ocache.imp = imp;
		ocache.helper = (struct ocall_helper *)malloc(
			sizeof(struct ocall_helper));
		ocache.helper->bs_method = rb_bs_find_method(klass, sel);
		assert(rb_objc_fill_sig(self, klass, sel, 
			    &ocache.helper->sig, ocache.helper->bs_method));
	    }
	}
	else {
	    // TODO bridgesupport C call?
	    return method_missing((VALUE)self, sel, argc, argv, opt);
	}
    }

    if (cache->flag == MCACHE_RCALL) {
	if (rcache.klass != klass) {
	    goto recache;
	}

	if ((argc < rcache.arity.min)
	    || ((rcache.arity.max != -1) && (argc > rcache.arity.max))) {
	    rb_raise(rb_eArgError, "wrong number of arguments (%d for %d)",
		    argc, rcache.arity.min);
	}

#if ROXOR_VM_DEBUG
	printf("ruby dispatch %c[<%s %p> %s] (imp=%p, cached=%s)\n",
		class_isMetaClass(klass) ? '+' : '-',
		class_getName(klass),
		(void *)self,
		sel_getName(sel),
		rcache.imp,
		cached ? "true" : "false");
#endif

	assert(rcache.node != NULL);
	const int node_type = nd_type(rcache.node);

	if (node_type == NODE_SCOPE && rcache.node->nd_body == NULL
	    && rcache.arity.max == rcache.arity.min) {
	    // Calling an empty method, let's just return nil!
	    return Qnil;
	}
	if (node_type == NODE_FBODY
	    && rcache.arity.max != rcache.arity.min) {
	    // Calling a function defined with rb_objc_define_method with
	    // a negative arity, which means a different calling convention.
	    if (rcache.arity.real == 2) {
		return ((VALUE (*)(VALUE, SEL, int, const VALUE *))rcache.imp)
		    (self, 0, argc, argv);
	    }
	    else if (rcache.arity.real == 1) {
		return ((VALUE (*)(VALUE, SEL, ...))rcache.imp)
		    (self, 0, rb_ary_new4(argc, argv));
	    }
	    else {
		printf("invalid negative arity for C function %d\n",
		       rcache.arity.real);
		abort();
	    }
	}

	return __rb_vm_rcall(self, rcache.node, rcache.imp, rcache.arity,
			     argc, argv);
    }
    else if (cache->flag == MCACHE_OCALL) {
	if (ocache.klass != klass) {
	    free(ocache.helper);
	    goto recache;
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

	return rb_objc_call2((VALUE)self, 
		(VALUE)klass, 
		sel, 
		ocache.imp, 
		&ocache.helper->sig, 
		ocache.helper->bs_method, 
		argc, 
		(VALUE *)argv);
    }
#undef rcache
#undef ocache

    printf("BOUH %s\n", (char *)sel);
    abort();
}

extern "C"
VALUE
rb_vm_dispatch(struct mcache *cache, VALUE self, SEL sel, rb_vm_block_t *block, 
	       unsigned char opt, int argc, ...)
{
#define MAX_DISPATCH_ARGS 200
    VALUE argv[MAX_DISPATCH_ARGS];
    if (argc > 0) {
	// TODO we should only determine the real argc here (by taking into
	// account the length splat arguments) and do the real unpacking of
	// splat arguments in __rb_vm_rcall(). This way we can optimize more
	// things (for ex. no need to unpack splats that are passed as a splat
	// argument in the method being called!).
	va_list ar;
	va_start(ar, argc);
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
	va_end(ar);
	argc = real_argc;
    }

    rb_vm_block_t *b = (rb_vm_block_t *)block;
    rb_vm_block_t *old_b = GET_VM()->current_block;
    bool old_block_saved = GET_VM()->block_saved;
    GET_VM()->block_saved = old_b != NULL;
    GET_VM()->current_block = b;

    VALUE retval = __rb_vm_dispatch(cache, self, NULL, sel, opt, argc, argv);

    GET_VM()->current_block = old_b;
    GET_VM()->block_saved = old_block_saved;
    return retval;
}

extern "C"
VALUE
rb_vm_fast_eqq(struct mcache *cache, VALUE self, VALUE comparedTo)
{
    // This function does not check if === has been or not redefined
    // so it should only been called by code generated by compile_optimized_dispatch_call.
    // Fixnums are already tested in compile_optimized_dispatch_call
    switch (TYPE(self)) {
	// TODO: Range
	case T_STRING:
	    if (self == comparedTo)
		return Qtrue;
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
rb_vm_when_splat(struct mcache *cache, VALUE comparedTo, VALUE splat)
{
    VALUE ary = rb_check_convert_type(splat, T_ARRAY, "Array", "to_a");
    if (NIL_P(ary)) {
	ary = rb_ary_new3(1, splat);
    }
    int count = RARRAY_LEN(ary);
    for (int i = 0; i < count; ++i) {
	if (RTEST(rb_vm_fast_eqq(cache, comparedTo, RARRAY_AT(ary, i)))) {
	    return Qtrue;
	}
    }
    return Qfalse;
}

extern "C"
VALUE
rb_vm_fast_shift(VALUE obj, VALUE other, struct mcache *cache, unsigned char overriden)
{
    if (overriden == 0 && TYPE(obj) == T_ARRAY) {
	rb_ary_push(obj, other);
	return other;
    }
    return __rb_vm_dispatch(cache, obj, NULL, selLTLT, 0, 1, &other);
}

extern "C"
VALUE
rb_vm_fast_aref(VALUE obj, VALUE other, struct mcache *cache, unsigned char overriden)
{
    // TODO what about T_HASH?
    if (overriden == 0 && TYPE(obj) == T_ARRAY) {
	if (TYPE(other) == T_FIXNUM) {
	    return rb_ary_elt(obj, FIX2INT(other));
	}
	extern VALUE rb_ary_aref(VALUE ary, SEL sel, int argc, VALUE *argv);
	return rb_ary_aref(obj, 0, 1, &other);
    }
    return __rb_vm_dispatch(cache, obj, NULL, selAREF, 0, 1, &other);
}

extern "C"
VALUE
rb_vm_fast_aset(VALUE obj, VALUE other1, VALUE other2, struct mcache *cache, unsigned char overriden)
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
    return __rb_vm_dispatch(cache, obj, NULL, selASET, 0, 2, args);
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
	GET_VM()->blocks.find(node);

    rb_vm_block_t *b;

    if (iter == GET_VM()->blocks.end()) {
	b = (rb_vm_block_t *)xmalloc(sizeof(rb_vm_block_t)
		+ (sizeof(VALUE) * dvars_size));

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

	rb_objc_retain(b);
	GET_VM()->blocks[cache_key] = b;
    }
    else {
	b = iter->second;
	assert(b->dvars_size == dvars_size);
    }

    b->self = self;
    b->node = node;

    if (dvars_size > 0) {
	va_list ar;
	va_start(ar, dvars_size);
	for (int i = 0; i < dvars_size; ++i) {
	    b->dvars[i] = va_arg(ar, VALUE *);
	}
	va_end(ar);
    }

    return b;
}

extern "C"
VALUE
rb_vm_call(VALUE self, SEL sel, int argc, const VALUE *argv, bool super)
{
    if (super) {
	struct mcache cache; 
	cache.flag = 0;
	VALUE retval = __rb_vm_dispatch(&cache, self, NULL, sel, 
					DISPATCH_SUPER, argc, argv);
	if (cache.flag == MCACHE_OCALL) {
	    free(cache.as.ocall.helper);
	}
	return retval;
    }
    else {
	struct mcache *cache = GET_VM()->method_cache_get(sel, false);
	return __rb_vm_dispatch(cache, self, NULL, sel, 0, argc, argv);
    }
}

extern "C"
VALUE
rb_vm_call_with_cache(void *cache, VALUE self, SEL sel, int argc, 
		      const VALUE *argv)
{
    return __rb_vm_dispatch((struct mcache *)cache, self, NULL, sel, 0,
	    argc, argv);
}

extern "C"
VALUE
rb_vm_call_with_cache2(void *cache, VALUE self, VALUE klass, SEL sel,
		       int argc, const VALUE *argv)
{
    return __rb_vm_dispatch((struct mcache *)cache, self, (Class)klass, sel,
	    0, argc, argv);
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
    return GET_VM()->current_block != NULL ? Qtrue : Qfalse;
}

// Should only be used by #block_given?.
extern "C"
bool
rb_vm_block_saved(void)
{
    return GET_VM()->block_saved;
}

extern "C"
rb_vm_block_t *
rb_vm_current_block(void)
{
    return GET_VM()->current_block;
}

extern "C"
void
rb_vm_change_current_block(rb_vm_block_t *block)
{
    GET_VM()->previous_block = GET_VM()->current_block;
    GET_VM()->current_block = block;
}

extern "C"
void
rb_vm_restore_current_block(void)
{
    GET_VM()->current_block = GET_VM()->previous_block;
    GET_VM()->previous_block = NULL;
}

extern "C" VALUE rb_proc_alloc_with_block(VALUE klass, rb_vm_block_t *proc);

extern "C"
VALUE
rb_vm_current_block_object(void)
{
    if (GET_VM()->current_block != NULL) {
	return rb_proc_alloc_with_block(rb_cProc, GET_VM()->current_block);
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
    NODE *node;

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

    int arity;
    if (node == NULL) {
	Method m = class_getInstanceMethod((Class)klass, sel);
	assert(m != NULL);
	arity = method_getNumberOfArguments(m) - 2;
    }
    else {
	rb_vm_arity_t n_arity = rb_vm_node_arity(node);
	arity = n_arity.min;
	if (n_arity.min != n_arity.max) {
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
    m->cache = rb_vm_get_call_cache(sel);

    return m;
}

static inline VALUE
rb_vm_block_eval0(rb_vm_block_t *b, int argc, const VALUE *argv)
{
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

    int dvars_size = b->dvars_size;
    rb_vm_arity_t arity = b->arity;    

    if (dvars_size > 0 || argc < arity.min || argc > arity.max) {
	VALUE *new_argv;
	if (argc == 1 && TYPE(argv[0]) == T_ARRAY && (arity.min > 1 || (arity.min == 1 && arity.min != arity.max))) {
	    // Expand the array
	    long ary_len = RARRAY_LEN(argv[0]);
	    new_argv = (VALUE *)alloca(sizeof(VALUE) * ary_len);
	    for (int i = 0; i < ary_len; i++) {
		new_argv[i] = RARRAY_AT(argv[0], i);
	    }
	    argv = new_argv;
	    argc = ary_len;
	    if (dvars_size == 0 && argc >= arity.min
		&& (argc <= arity.max || b->arity.max == -1)) {
		return __rb_vm_rcall(b->self, b->node, b->imp, arity, argc,
				     argv);
	    }
	}
	int new_argc;
	if (argc <= arity.min) {
	    new_argc = dvars_size + arity.min;
	}
	else if (argc > arity.max && b->arity.max != -1) {
	    new_argc = dvars_size + arity.max;
	}
	else {
	    new_argc = dvars_size + argc;
	}
	new_argv = (VALUE *)alloca(sizeof(VALUE) * new_argc);
	for (int i = 0; i < dvars_size; i++) {
	    new_argv[i] = (VALUE)b->dvars[i];
	}
	for (int i = 0; i < new_argc - dvars_size; i++) {
	    new_argv[dvars_size + i] = i < argc ? argv[i] : Qnil;
	}
	argc = new_argc;
	argv = new_argv;
	if (dvars_size > 0) {
	    arity.min += dvars_size;
	    if (arity.max != -1) {
		arity.max += dvars_size;
	    }
	    arity.real += dvars_size;
	    arity.left_req += dvars_size;
	}
    }
#if ROXOR_VM_DEBUG
    printf("yield block %p argc %d arity %d dvars %d\n", b, argc,
	    arity.real, b->dvars_size);
#endif

    // We need to preserve dynamic variable slots here because our block may
    // call the parent method which may call the block again, and since dvars
    // are currently implemented using alloca() we will painfully die if the 
    // previous slots are not restored.

    VALUE **old_dvars;
    if (dvars_size > 0) {
	old_dvars = (VALUE **)alloca(sizeof(VALUE *) * dvars_size);
	memcpy(old_dvars, b->dvars, sizeof(VALUE) * dvars_size);
    }
    else {
	old_dvars = NULL;
    }

    VALUE v = __rb_vm_rcall(b->self, b->node, b->imp, arity, argc, argv);

    if (old_dvars != NULL) {
	memcpy(b->dvars, old_dvars, sizeof(VALUE) * dvars_size);
    }

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
    rb_vm_block_t *b = GET_VM()->current_block;

    if (b == NULL) {
	rb_raise(rb_eLocalJumpError, "no block given");
    }

    GET_VM()->current_block = GET_VM()->previous_block;
    VALUE retval = rb_vm_block_eval0(b, argc, argv);
    GET_VM()->current_block = b;

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
    rb_vm_block_t *b = GET_VM()->current_block;

    GET_VM()->current_block = NULL;
    VALUE old_self = b->self;
    b->self = self;
    //Class old_class = GET_VM()->current_class;
    //GET_VM()->current_class = (Class)klass;

    VALUE retval = rb_vm_block_eval0(b, argc, argv);

    b->self = old_self;
    GET_VM()->current_block = b;
    //GET_VM()->current_class = old_class;

    return retval;
}

extern "C"
VALUE 
rb_vm_yield_args(int argc, ...)
{
    VALUE *argv = NULL;
    if (argc > 0) {
	va_list ar;
	va_start(ar, argc);
	argv = (VALUE *)alloca(sizeof(VALUE) * argc);
	for (int i = 0; i < argc; ++i) {
	    argv[i] = va_arg(ar, VALUE);
	}
	va_end(ar);
    }
    return rb_vm_yield0(argc, argv);
}

extern "C"
struct rb_float_cache *
rb_vm_float_cache(double d)
{
    std::map<double, struct rb_float_cache *>::iterator iter = 
	GET_VM()->float_cache.find(d);
    if (iter == GET_VM()->float_cache.end()) {
	struct rb_float_cache *fc = (struct rb_float_cache *)malloc(
		sizeof(struct rb_float_cache));
	assert(fc != NULL);

	fc->count = 0;
	fc->obj = Qnil;
	GET_VM()->float_cache[d] = fc;

	return fc;
    }

    return iter->second;
}

extern IMP basic_respond_to_imp; // vm_method.c

extern "C"
bool
rb_vm_respond_to(VALUE obj, SEL sel, bool priv)
{
    VALUE klass = CLASS_OF(obj);

    IMP respond_to_imp = class_getMethodImplementation((Class)klass, selRespondTo);

    if (respond_to_imp == basic_respond_to_imp) {
	IMP obj_imp = class_getMethodImplementation((Class)klass, sel);
	if (obj_imp == NULL) {
	    return false;
	}
        NODE *node = GET_VM()->method_node_get(obj_imp);
	return node != NULL && (!priv || !(node->nd_noex & NOEX_PRIVATE));
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

extern "C"
void
rb_vm_raise(VALUE exception)
{
    rb_objc_retain((void *)exception);
    GET_VM()->current_exception = exception;
    void *exc = __cxa_allocate_exception(0);
    __cxa_throw(exc, NULL, NULL);
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

	if (GET_VM()->symbolize_call_address(callstack[i], NULL, &ln, name, sizeof name)) {
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
    assert(GET_VM()->current_exception != Qnil);

    va_list ar;
    unsigned char active = 0;

    va_start(ar, argc);
    for (int i = 0; i < argc && active == 0; ++i) {
	VALUE obj = va_arg(ar, VALUE);
	if (TYPE(obj) == T_ARRAY) {
	    for (int j = 0, count = RARRAY_LEN(obj); j < count; ++j) {
		VALUE obj2 = RARRAY_AT(obj, j);
		if (rb_obj_is_kind_of(GET_VM()->current_exception, obj2)) {
		    active = 1;
		}
	    }
	}
	else {
	    if (rb_obj_is_kind_of(GET_VM()->current_exception, obj)) {
		active = 1;
	    }
	}
    }
    va_end(ar);

    return active;
}

extern "C"
VALUE
rb_vm_pop_exception(void)
{
    VALUE exc = GET_VM()->current_exception;
    assert(exc != Qnil);
    rb_objc_release((void *)exc);
    GET_VM()->current_exception = Qnil;
    return exc; 
}

extern "C"
VALUE
rb_vm_current_exception(void)
{
    return GET_VM()->current_exception;
}

extern "C"
void
rb_vm_set_current_exception(VALUE exception)
{
    if (GET_VM()->current_exception != exception) {
	if (GET_VM()->current_exception != Qnil) {
	    rb_objc_release((void *)GET_VM()->current_exception);
	}
	rb_objc_retain((void *)exception);
	GET_VM()->current_exception = exception;
    }
}

extern "C"
void 
rb_vm_debug(void)
{
    printf("rb_vm_debug\n");
}

// END OF VM primitives

#include <llvm/Target/TargetData.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>

static void
rb_vm_print_exception(VALUE exc)
{
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
IMP
rb_vm_compile(const char *fname, NODE *node)
{
    assert(node != NULL);

    RoxorCompiler *compiler = new RoxorCompiler(fname);

    Function *function = compiler->compile_main_function(node);

#if ROXOR_COMPILER_DEBUG
    if (verifyModule(*RoxorCompiler::module)) {
	printf("Error during module verification\n");
	exit(1);
    }

    uint64_t start = mach_absolute_time();
#endif

    IMP imp = GET_VM()->compile(function);

#if ROXOR_COMPILER_DEBUG
    uint64_t elapsed = mach_absolute_time() - start;

    static mach_timebase_info_data_t sTimebaseInfo;

    if (sTimebaseInfo.denom == 0) {
	(void) mach_timebase_info(&sTimebaseInfo);
    }

    uint64_t elapsedNano = elapsed * sTimebaseInfo.numer / sTimebaseInfo.denom;

    printf("compilation/optimization done, took %lld ns\n",  elapsedNano);
#endif

#if ROXOR_DUMP_IR
    printf("IR dump ----------------------------------------------\n");
    RoxorCompiler::module->dump();
    printf("------------------------------------------------------\n");
#endif

    delete compiler;

    return imp;
}

extern "C"
VALUE
rb_vm_run(const char *fname, NODE *node)
{
    IMP imp = rb_vm_compile(fname, node);

    try {
	return ((VALUE(*)(VALUE, SEL))imp)(GET_VM()->current_top_object, 0);
    }
    catch (...) {
	VALUE exc = GET_VM()->current_exception;
	if (exc != Qnil) {
	    rb_vm_print_exception(exc);
	}
	else {
	    printf("uncatched C++/Objective-C exception...\n");
	}
	exit(1);
    }
}

extern "C"
VALUE
rb_vm_run_under(VALUE klass, VALUE self, const char *fname, NODE *node)
{
    assert(klass != 0);

    VALUE old_top_object = GET_VM()->current_top_object;
    if (self != 0) {
	GET_VM()->current_top_object = self;
    }
    Class old_class = GET_VM()->current_class;
    GET_VM()->current_class = (Class)klass;

    VALUE val = rb_vm_run(fname, node);

    GET_VM()->current_top_object = old_top_object;
    GET_VM()->current_class = old_class;

    return val;
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
void 
Init_PreVM(void)
{
    llvm::ExceptionHandling = true; // required!

    RoxorCompiler::module = new llvm::Module("Roxor");
    RoxorVM::current = new RoxorVM();
}

extern "C"
void
Init_VM(void)
{
    rb_cTopLevel = rb_define_class("TopLevel", rb_cObject);

    GET_VM()->current_class = NULL;

    VALUE top_self = rb_obj_alloc(rb_cTopLevel);
    rb_objc_retain((void *)top_self);
    GET_VM()->current_top_object = top_self;
}
