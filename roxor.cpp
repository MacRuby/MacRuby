/* ROXOR: the new MacRuby VM that rocks! */

#define ROXOR_COMPILER_DEBUG		0
#define ROXOR_VM_DEBUG			0
#define ROXOR_ULTRA_LAZY_JIT		1
#define ROXOR_INTERPRET_EVAL		0

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
#include <dlfcn.h>

#define force_inline __attribute__((always_inline))

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

    RoxorFunction (Function *_f, unsigned char *_start, unsigned char *_end) {
	f = _f;
	start = _start;
	end = _end;
	imp = NULL; 	// lazy
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

typedef struct {
    bs_element_type_t bs_type;
    bool is_struct(void) { return bs_type == BS_ELEMENT_STRUCT; }
    union {
	bs_element_struct_t *s;
	bs_element_opaque_t *o;
	void *v;
    } as;
    Type *type;
    VALUE klass;
} rb_vm_bs_boxed_t;

#define SPLAT_ARG_FOLLOWS 0xdeadbeef

class RoxorCompiler
{
    public:
	static llvm::Module *module;
	static RoxorCompiler *shared;

	RoxorCompiler(const char *fname);

	Value *compile_node(NODE *node);

	Function *compile_main_function(NODE *node);
	Function *compile_read_attr(ID name);
	Function *compile_write_attr(ID name);
	Function *compile_stub(const char *types, int argc, bool is_objc);
	Function *compile_bs_struct_new(rb_vm_bs_boxed_t *bs_boxed);
	Function *compile_bs_struct_writer(rb_vm_bs_boxed_t *bs_boxed,
		int field);
	Function *compile_ffi_function(void *stub, void *imp, int argc);
	Function *compile_to_rval_convertor(const char *type);
	Function *compile_to_ocval_convertor(const char *type);
	Function *compile_objc_stub(Function *ruby_func, const char *types);

	const Type *convert_type(const char *type);

    private:
	const char *fname;

	std::map<ID, Value *> lvars;
	std::vector<ID> dvars;
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
	Value *current_var_uses;
	Value *running_block;
	BasicBlock *begin_bb;
	BasicBlock *rescue_bb;
	BasicBlock *ensure_bb;
	bool current_rescue;
	NODE *current_block_node;
	Function *current_block_func;
	jmp_buf *return_from_block_jmpbuf;
	GlobalVariable *current_opened_class;
	bool current_module;
	BasicBlock *current_loop_begin_bb;
	BasicBlock *current_loop_body_bb;
	BasicBlock *current_loop_end_bb;
	Value *current_loop_exit_val;

	Function *dispatcherFunc;
	Function *fastEqqFunc;
	Function *whenSplatFunc;
	Function *prepareBlockFunc;
	Function *pushBindingFunc;
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
	Function *toAFunc;
	Function *toAryFunc;
	Function *catArrayFunc;
	Function *dupArrayFunc;
	Function *newArrayFunc;
	Function *newStructFunc;
	Function *newOpaqueFunc;
	Function *getStructFieldsFunc;
	Function *getOpaqueDataFunc;
	Function *getPointerPtrFunc;
	Function *checkArityFunc;
	Function *setStructFunc;
	Function *newRangeFunc;
	Function *newRegexpFunc;
	Function *strInternFunc;
	Function *keepVarsFunc;
	Function *masgnGetElemBeforeSplatFunc;
	Function *masgnGetElemAfterSplatFunc;
	Function *masgnGetSplatFunc;
	Function *newStringFunc;
	Function *yieldFunc;
	Function *gvarSetFunc;
	Function *gvarGetFunc;
	Function *cvarSetFunc;
	Function *cvarGetFunc;
	Function *currentExceptionFunc;
	Function *popExceptionFunc;
	Function *getSpecialFunc;
	Function *breakFunc;
	Function *longjmpFunc;
	Function *setjmpFunc;
	Function *popBrokenValue;

	Constant *zeroVal;
	Constant *oneVal;
	Constant *twoVal;
	Constant *nilVal;
	Constant *trueVal;
	Constant *falseVal;
	Constant *undefVal;
	Constant *splatArgFollowsVal;
	Constant *cObject;
	const Type *RubyObjTy; 
	const Type *RubyObjPtrTy;
	const Type *RubyObjPtrPtrTy;
	const Type *PtrTy;
	const Type *PtrPtrTy;
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

	Instruction *compile_const_pointer_to_pointer(void *ptr, bool insert_to_bb=true) {
	    Value *ptrint = ConstantInt::get(IntTy, (long)ptr);
	    return insert_to_bb
		? new IntToPtrInst(ptrint, PtrPtrTy, "", bb)
		: new IntToPtrInst(ptrint, PtrPtrTy, "");
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
	Value *compile_binding(void);
	Value *compile_optimized_dispatch_call(SEL sel, int argc, std::vector<Value *> &params);
	Value *compile_ivar_read(ID vid);
	Value *compile_ivar_assignment(ID vid, Value *val);
	Value *compile_cvar_assignment(ID vid, Value *val);
	Value *compile_gvar_assignment(struct global_entry *entry, Value *val);
	Value *compile_constant_declaration(NODE *node, Value *val);
	Value *compile_multiple_assignment(NODE *node, Value *val);
	void compile_multiple_assignment_element(NODE *node, Value *val);
	Value *compile_current_class(void);
	Value *compile_class_path(NODE *node);
	Value *compile_const(ID id, Value *outer);
	Value *compile_singleton_class(Value *obj);
	Value *compile_defined_expression(NODE *node);
	Value *compile_dstr(NODE *node);
	Value *compile_dvar_slot(ID name);
	void compile_break_val(Value *val);
	Value *compile_jump(NODE *node);

	void compile_landing_pad_header(void);
	void compile_landing_pad_footer(void);
	void compile_rethrow_exception(void);
	void compile_pop_exception(void);
	Value *compile_lvar_slot(ID name);
	bool compile_lvars(ID *tbl);
	Value *compile_new_struct(Value *klass, std::vector<Value *> &fields);
	Value *compile_new_opaque(Value *klass, Value *val);
	void compile_get_struct_fields(Value *val, Value *buf,
		rb_vm_bs_boxed_t *bs_boxed);
	Value *compile_get_opaque_data(Value *val, rb_vm_bs_boxed_t *bs_boxed,
		Value *slot);
	Value *compile_get_cptr(Value *val, const char *type, Value *slot);
	void compile_check_arity(Value *given, Value *requested);
	void compile_set_struct(Value *rcv, int field, Value *val);

	Value *compile_conversion_to_c(const char *type, Value *val,
				       Value *slot);
	Value *compile_conversion_to_ruby(const char *type,
					  const Type *llvm_type, Value *val);

	int *get_slot_cache(ID id) {
	    if (current_block || !current_instance_method || current_module) {
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
	bool unbox_ruby_constant(Value *val, VALUE *rval);
	SEL mid_to_sel(ID mid, int arity);
};

llvm::Module *RoxorCompiler::module = NULL;
RoxorCompiler *RoxorCompiler::shared = NULL;

struct ccache {
    VALUE outer;
    VALUE val;
};

typedef VALUE rb_vm_objc_stub_t(IMP imp, id self, SEL sel, int argc,
				const VALUE *argv);

typedef VALUE rb_vm_c_stub_t(IMP imp, int argc, const VALUE *argv);

struct mcache {
#define MCACHE_RCALL 0x1 // Ruby call
#define MCACHE_OCALL 0x2 // Objective-C call
#define MCACHE_FCALL 0x4 // C call
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
	    bs_element_method_t *bs_method;	
	    rb_vm_objc_stub_t *stub;
	} ocall;
	struct {
	    IMP imp;
	    bs_element_function_t *bs_function;
	    rb_vm_c_stub_t *stub;
	} fcall;
    } as;
#define rcache cache->as.rcall
#define ocache cache->as.ocall
#define fcache cache->as.fcall
};

extern "C" void *__cxa_allocate_exception(size_t);
extern "C" void __cxa_throw(void *, void *, void *);

struct RoxorFunctionIMP
{
    NODE *node;
    SEL sel;
    IMP objc_imp;
    IMP ruby_imp;

    RoxorFunctionIMP(NODE *_node, SEL _sel, IMP _objc_imp, IMP _ruby_imp) { 
	node = _node;
	sel = _sel;
	objc_imp = _objc_imp;
	ruby_imp = _ruby_imp;
    }
};

struct rb_vm_outer {
    Class klass;
    struct rb_vm_outer *outer;
};

typedef struct {
    jmp_buf buf;
    VALUE throw_value;
    int nested;
} rb_vm_catch_t;

typedef struct {
    Function *func;
    NODE *node;
} rb_vm_method_source_t;

class RoxorVM
{
    private:
	ExistingModuleProvider *emp;
	RoxorJITManager *jmm;
	ExecutionEngine *ee;
	ExecutionEngine *iee;
	FunctionPassManager *fpm;
	bool running;

	std::map<IMP, struct RoxorFunctionIMP *> ruby_imps;
	std::map<SEL, struct mcache *> mcache;
	std::map<ID, struct ccache *> ccache;
	std::map<Class, std::map<ID, int> *> ivar_slots;
	std::map<SEL, GlobalVariable *> redefined_ops_gvars;
	std::map<Class, struct rb_vm_outer *> outers;
	std::map<std::string, void *> c_stubs, objc_stubs,
	    to_rval_convertors, to_ocval_convertors;

	std::vector<rb_vm_block_t *> current_blocks;
	std::vector<VALUE> current_exceptions;

    public:
	static RoxorVM *current;

	Class current_class;
	VALUE current_top_object;
	VALUE loaded_features;
	VALUE load_path;
	VALUE backref;
	VALUE broken_with;
	VALUE last_status;
	VALUE errinfo;
	int safe_level;
	std::vector<rb_vm_binding_t *> bindings;
	std::map<NODE *, rb_vm_block_t *> blocks;
	std::map<double, struct rb_float_cache *> float_cache;
	unsigned char method_missing_reason;
	bool parse_in_eval;

	std::string debug_blocks(void);

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
	    return current_blocks.empty() ? NULL : current_blocks.back();
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

	std::string debug_exceptions(void);

	VALUE current_exception(void) {
	    return current_exceptions.empty()
		? Qnil : current_exceptions.back();
	}

	void push_current_exception(VALUE exc) {
	    assert(!NIL_P(exc));
	    rb_objc_retain((void *)exc);
	    current_exceptions.push_back(exc);
	}

	VALUE pop_current_exception(void) {
	    assert(!current_exceptions.empty());
	    VALUE exc = current_exceptions.back();
	    rb_objc_release((void *)exc);
	    current_exceptions.pop_back();
	    return exc;
	}

	std::map<VALUE, rb_vm_catch_t *> catch_jmp_bufs;
	std::vector<jmp_buf *> return_from_block_jmp_bufs;

	bs_parser_t *bs_parser;
	std::map<std::string, rb_vm_bs_boxed_t *> bs_boxed;
	std::map<std::string, bs_element_function_t *> bs_funcs;
	std::map<ID, bs_element_constant_t *> bs_consts;
	std::map<std::string, std::map<SEL, bs_element_method_t *> *>
	    bs_classes_class_methods, bs_classes_instance_methods;
	std::map<std::string, bs_element_cftype_t *> bs_cftypes;

	bs_element_method_t *find_bs_method(Class klass, SEL sel);
	rb_vm_bs_boxed_t *find_bs_boxed(std::string type);
	rb_vm_bs_boxed_t *find_bs_struct(std::string type);
	rb_vm_bs_boxed_t *find_bs_opaque(std::string type);

	void *gen_stub(std::string types, int argc, bool is_objc);
	void *gen_to_rval_convertor(std::string type);
	void *gen_to_ocval_convertor(std::string type);

	void insert_stub(const char *types, void *stub, bool is_objc) {
	    std::map<std::string, void *> &m =
		is_objc ? objc_stubs : c_stubs;
	    m.insert(std::make_pair(types, stub));
	}

#if ROXOR_ULTRA_LAZY_JIT
	std::map<SEL, std::map<Class, rb_vm_method_source_t *> *>
	    method_sources;
	std::multimap<Class, SEL> method_source_sels;

	std::map<Class, rb_vm_method_source_t *> *
	method_sources_for_sel(SEL sel, bool create)
	{
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

	std::map<Function *, IMP> objc_to_ruby_stubs;

#if ROXOR_VM_DEBUG
	long functions_compiled;
#endif

	RoxorVM(void);

	IMP compile(Function *func);
	VALUE interpret(Function *func);

	bool symbolize_call_address(void *addr, void **startp,
		unsigned long *ln, char *name, size_t name_len);

	bool is_running(void) { return running; }
	void set_running(bool flag) { running = flag; }

	struct mcache *method_cache_get(SEL sel, bool super);
	struct RoxorFunctionIMP *method_func_imp_get(IMP imp);
	NODE *method_node_get(IMP imp);
	void add_method(Class klass, SEL sel, IMP imp, IMP ruby_imp,
		NODE *node, const char *types);

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
	    struct rb_vm_outer *class_outer = get_outer(klass);
	    if (class_outer == NULL || class_outer->outer != mod_outer) {
		if (class_outer != NULL) {
		    free(class_outer);
		}
		class_outer = (struct rb_vm_outer *)
		    malloc(sizeof(struct rb_vm_outer));
		class_outer->klass = klass;
		class_outer->outer = mod_outer;
		outers[klass] = class_outer;
	    }
	}

	VALUE *get_binding_lvar(ID name) {
	    if (!bindings.empty()) {
		rb_vm_binding_t *b = bindings.back();
		for (rb_vm_local_t *l = b->locals; l != NULL; l = l->next) {
		    if (l->name == name) {
			return l->value;
		    }
		}
	    }
	    return NULL;
	}

	size_t get_sizeof(const Type *type) {
	    return ee->getTargetData()->getTypeSizeInBits(type) / 8;
	}

	size_t get_sizeof(const char *type) {
	    return get_sizeof(RoxorCompiler::shared->convert_type(type));
	}

	bool is_large_struct_type(const Type *type) {
	    return type->getTypeID() == Type::StructTyID
		&& ee->getTargetData()->getTypeSizeInBits(type) > 128;
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
    ensure_bb = NULL;
    current_mid = 0;
    current_instance_method = false;
    self_id = rb_intern("self");
    current_self = NULL;
    current_var_uses = NULL;
    running_block = NULL;
    current_block = false;
    current_block_node = NULL;
    current_block_func = NULL;
    current_opened_class = NULL;
    current_module = false;
    current_loop_begin_bb = NULL;
    current_loop_body_bb = NULL;
    current_loop_end_bb = NULL;
    current_loop_exit_val = NULL;
    current_rescue = false;
    return_from_block_jmpbuf = NULL;

    dispatcherFunc = NULL;
    fastEqqFunc = NULL;
    whenSplatFunc = NULL;
    prepareBlockFunc = NULL;
    pushBindingFunc = NULL;
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
    toAFunc = NULL;
    toAryFunc = NULL;
    catArrayFunc = NULL;
    dupArrayFunc = NULL;
    newArrayFunc = NULL;
    newStructFunc = NULL;
    newOpaqueFunc = NULL;
    getStructFieldsFunc = NULL;
    getOpaqueDataFunc = NULL;
    getPointerPtrFunc = NULL;
    checkArityFunc = NULL;
    setStructFunc = NULL;
    newRangeFunc = NULL;
    newRegexpFunc = NULL;
    strInternFunc = NULL;
    keepVarsFunc = NULL;
    masgnGetElemBeforeSplatFunc = NULL;
    masgnGetElemAfterSplatFunc = NULL;
    masgnGetSplatFunc = NULL;
    newStringFunc = NULL;
    yieldFunc = NULL;
    gvarSetFunc = NULL;
    gvarGetFunc = NULL;
    cvarSetFunc = NULL;
    cvarGetFunc = NULL;
    currentExceptionFunc = NULL;
    popExceptionFunc = NULL;
    getSpecialFunc = NULL;
    breakFunc = NULL;
    longjmpFunc = NULL;
    setjmpFunc = NULL;
    popBrokenValue = NULL;

#if __LP64__
    RubyObjTy = IntTy = Type::Int64Ty;
#else
    RubyObjTy = IntTy = Type::Int32Ty;
#endif

    zeroVal = ConstantInt::get(IntTy, 0);
    oneVal = ConstantInt::get(IntTy, 1);
    twoVal = ConstantInt::get(IntTy, 2);

    RubyObjPtrTy = PointerType::getUnqual(RubyObjTy);
    RubyObjPtrPtrTy = PointerType::getUnqual(RubyObjPtrTy);
    nilVal = ConstantInt::get(RubyObjTy, Qnil);
    trueVal = ConstantInt::get(RubyObjTy, Qtrue);
    falseVal = ConstantInt::get(RubyObjTy, Qfalse);
    undefVal = ConstantInt::get(RubyObjTy, Qundef);
    splatArgFollowsVal = ConstantInt::get(RubyObjTy, SPLAT_ARG_FOLLOWS);
    cObject = ConstantInt::get(RubyObjTy, (long)rb_cObject);
    PtrTy = PointerType::getUnqual(Type::Int8Ty);
    PtrPtrTy = PointerType::getUnqual(PtrTy);

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
RoxorCompiler::unbox_ruby_constant(Value *val, VALUE *rval)
{
    if (ConstantInt::classof(val)) {
	long tmp = cast<ConstantInt>(val)->getZExtValue();
	*rval = tmp;
	return true;
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
	// VALUE rb_vm_when_splat(struct mcache *cache,
	//			  unsigned char overriden,
	//			  VALUE comparedTo, VALUE splat)
	whenSplatFunc = cast<Function>
	    (module->getOrInsertFunction("rb_vm_when_splat",
					 RubyObjTy, PtrTy, Type::Int1Ty,
					 RubyObjTy, RubyObjTy, NULL));
    }

    void *eqq_cache = GET_VM()->method_cache_get(selEqq, false);
    std::vector<Value *> params;
    params.push_back(compile_const_pointer(eqq_cache));
    GlobalVariable *is_redefined = GET_VM()->redefined_op_gvar(selEqq, true);
    params.push_back(new LoadInst(is_redefined, "", bb));
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

void
RoxorCompiler::compile_multiple_assignment_element(NODE *node, Value *val)
{
    switch (nd_type(node)) {
	case NODE_LASGN:
	case NODE_DASGN:
	case NODE_DASGN_CURR:
	    {
		Value *slot = compile_lvar_slot(node->nd_vid);
		new StoreInst(val, slot, bb);
	    }
	    break;

	case NODE_IASGN:
	case NODE_IASGN2:
	    compile_ivar_assignment(node->nd_vid, val);
	    break;

	case NODE_CVASGN:
	    compile_cvar_assignment(node->nd_vid, val);
	    break;

	case NODE_GASGN:
	    compile_gvar_assignment(node->nd_entry, val);
	    break;

	case NODE_ATTRASGN:
	    compile_attribute_assign(node, val);
	    break;

	case NODE_MASGN:
	    compile_multiple_assignment(node, val);
	    break;

	case NODE_CDECL:
	    compile_constant_declaration(node, val);
	    break;

	default:
	    compile_node_error("unimplemented MASGN subnode",
			       node);
    }
}

Value *
RoxorCompiler::compile_multiple_assignment(NODE *node, Value *val)
{
    assert(nd_type(node) == NODE_MASGN);
    if (toAryFunc == NULL) {
	// VALUE rb_vm_to_ary(VALUE ary);
	toAryFunc = cast<Function>(module->getOrInsertFunction(
		    "rb_vm_to_ary",
		    RubyObjTy, RubyObjTy, NULL));
    }
    if (masgnGetElemBeforeSplatFunc == NULL) {
	// VALUE rb_vm_masgn_get_elem_before_splat(VALUE ary, int offset);
	masgnGetElemBeforeSplatFunc = cast<Function>(module->getOrInsertFunction(
		    "rb_vm_masgn_get_elem_before_splat",
		    RubyObjTy, RubyObjTy, Type::Int32Ty, NULL));
    }
    if (masgnGetElemAfterSplatFunc == NULL) {
	// VALUE rb_vm_masgn_get_elem_after_splat(VALUE ary, int before_splat_count, int after_splat_count, int offset);
	masgnGetElemAfterSplatFunc = cast<Function>(module->getOrInsertFunction(
		    "rb_vm_masgn_get_elem_after_splat",
		    RubyObjTy, RubyObjTy, Type::Int32Ty, Type::Int32Ty, Type::Int32Ty, NULL));
    }
    if (masgnGetSplatFunc == NULL) {
	// VALUE rb_vm_masgn_get_splat(VALUE ary, int before_splat_count, int after_splat_count);
	masgnGetSplatFunc = cast<Function>(module->getOrInsertFunction(
		    "rb_vm_masgn_get_splat",
		    RubyObjTy, RubyObjTy, Type::Int32Ty, Type::Int32Ty, NULL));
    }

    NODE *before_splat = node->nd_head, *after_splat = NULL, *splat = NULL;

    assert((before_splat == NULL) || (nd_type(before_splat) == NODE_ARRAY));

    // if the splat has no name (a, *, b = 1, 2, 3), its node value is -1
    if ((node->nd_next == (NODE *)-1) || (node->nd_next == NULL) || (nd_type(node->nd_next) != NODE_POSTARG)) {
	splat = node->nd_next;
    }
    else {
	NODE *post_arg = node->nd_next;
	splat = post_arg->nd_1st;
	after_splat = post_arg->nd_2nd;
    }

    assert((after_splat == NULL) || (nd_type(after_splat) == NODE_ARRAY));

    int before_splat_count = 0, after_splat_count = 0;
    for (NODE *l = before_splat; l != NULL; l = l->nd_next) {
	++before_splat_count;
    }
    for (NODE *l = after_splat; l != NULL; l = l->nd_next) {
	++after_splat_count;
    }

    {
	std::vector<Value *> params;
	params.push_back(val);
	val = CallInst::Create(toAryFunc, params.begin(),
	    params.end(), "", bb);
    }

    NODE *l = before_splat;
    for (int i = 0; l != NULL; ++i) {
	std::vector<Value *> params;
	params.push_back(val);
	params.push_back(ConstantInt::get(Type::Int32Ty, i));
	Value *elt = CallInst::Create(masgnGetElemBeforeSplatFunc, params.begin(),
		params.end(), "", bb);

	compile_multiple_assignment_element(l->nd_head, elt);

	l = l->nd_next;
    }

    if (splat != NULL && splat != (NODE *)-1) {
	std::vector<Value *> params;
	params.push_back(val);
	params.push_back(ConstantInt::get(Type::Int32Ty, before_splat_count));
	params.push_back(ConstantInt::get(Type::Int32Ty, after_splat_count));
	Value *elt = CallInst::Create(masgnGetSplatFunc, params.begin(),
		params.end(), "", bb);

	compile_multiple_assignment_element(splat, elt);
    }

    l = after_splat;
    for (int i = 0; l != NULL; ++i) {
	std::vector<Value *> params;
	params.push_back(val);
	params.push_back(ConstantInt::get(Type::Int32Ty, before_splat_count));
	params.push_back(ConstantInt::get(Type::Int32Ty, after_splat_count));
	params.push_back(ConstantInt::get(Type::Int32Ty, i));
	Value *elt = CallInst::Create(masgnGetElemAfterSplatFunc, params.begin(),
		params.end(), "", bb);

	compile_multiple_assignment_element(l->nd_head, elt);

	l = l->nd_next;
    }

    return val;
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
	// void *rb_vm_prepare_block(Function *func, NODE *node, VALUE self,
	//			     rb_vm_var_uses **parent_var_uses,
	//			     rb_vm_block_t *parent_block,
	//			     int dvars_size, ...);
	std::vector<const Type *> types;
	types.push_back(PtrTy);
	types.push_back(PtrTy);
	types.push_back(RubyObjTy);
	types.push_back(PtrPtrTy);
	types.push_back(PtrTy);
	types.push_back(Type::Int32Ty);
	FunctionType *ft = FunctionType::get(PtrTy, types, true);
	prepareBlockFunc = cast<Function>
	    (module->getOrInsertFunction("rb_vm_prepare_block", ft));
    }

    std::vector<Value *> params;
    params.push_back(compile_const_pointer(current_block_func));
    params.push_back(compile_const_pointer(current_block_node));
    params.push_back(current_self);
    if (current_var_uses == NULL) {
	// there is no local variables in this scope
	params.push_back(compile_const_pointer_to_pointer(NULL));
    }
    else {
	params.push_back(current_var_uses);
    }
    if (running_block == NULL) {
	params.push_back(compile_const_pointer(NULL));
    }
    else {
	params.push_back(running_block);
    }

    // Dvars.
    params.push_back(ConstantInt::get(Type::Int32Ty, (int)dvars.size()));
    for (std::vector<ID>::iterator iter = dvars.begin();
	 iter != dvars.end(); ++iter) {
	params.push_back(compile_lvar_slot(*iter));
    }

    // Lvars.
    params.push_back(ConstantInt::get(Type::Int32Ty, (int)lvars.size()));
    for (std::map<ID, Value *>::iterator iter = lvars.begin();
	 iter != lvars.end(); ++iter) {
	ID name = iter->first;
	Value *slot = iter->second;
	if (std::find(dvars.begin(), dvars.end(), name) == dvars.end()) {
	    params.push_back(ConstantInt::get(IntTy, (long)name));
	    params.push_back(slot);
	}
    }

    return CallInst::Create(prepareBlockFunc, params.begin(), params.end(),
	    "", bb);
}

Value *
RoxorCompiler::compile_binding(void)
{
    if (pushBindingFunc == NULL) {
	// void rb_vm_push_binding(VALUE self, int lvars_size, ...);
	std::vector<const Type *> types;
	types.push_back(RubyObjTy);
	types.push_back(Type::Int32Ty);
	FunctionType *ft = FunctionType::get(Type::VoidTy, types, true);
	pushBindingFunc = cast<Function>
	    (module->getOrInsertFunction("rb_vm_push_binding", ft));
    }

    std::vector<Value *> params;
    params.push_back(current_self);

    // Lvars.
    params.push_back(ConstantInt::get(Type::Int32Ty, (int)lvars.size()));
    for (std::map<ID, Value *>::iterator iter = lvars.begin();
	 iter != lvars.end(); ++iter) {
	params.push_back(ConstantInt::get(IntTy, (long)iter->first));
	params.push_back(iter->second);
    }

    return CallInst::Create(pushBindingFunc, params.begin(), params.end(),
	    "", bb);
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
			Type::VoidTy, RubyObjTy, IntTy, RubyObjTy, PtrTy,
			NULL)); 
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
RoxorCompiler::compile_cvar_assignment(ID name, Value *val)
{
    if (cvarSetFunc == NULL) {
	// VALUE rb_vm_cvar_set(VALUE klass, ID id, VALUE val);
	cvarSetFunc = cast<Function>(module->getOrInsertFunction(
		    "rb_vm_cvar_set", 
		    RubyObjTy, RubyObjTy, IntTy, RubyObjTy, NULL));
    }

    std::vector<Value *> params;

    params.push_back(compile_current_class());
    params.push_back(ConstantInt::get(IntTy, (long)name));
    params.push_back(val);

    return CallInst::Create(cvarSetFunc, params.begin(),
	    params.end(), "", bb);
}

Value *
RoxorCompiler::compile_gvar_assignment(struct global_entry *entry, Value *val)
{
    if (gvarSetFunc == NULL) {
	// VALUE rb_gvar_set(struct global_entry *entry, VALUE val);
	gvarSetFunc = cast<Function>(module->getOrInsertFunction(
		    "rb_gvar_set",
		    RubyObjTy, PtrTy, RubyObjTy, NULL));
    }

    std::vector<Value *> params;

    params.push_back(compile_const_pointer(entry));
    params.push_back(val);

    return CallInst::Create(gvarSetFunc, params.begin(),
	    params.end(), "", bb);
}

Value *
RoxorCompiler::compile_constant_declaration(NODE *node, Value *val)
{
    if (setConstFunc == NULL) {
	// VALUE rb_vm_set_const(VALUE mod, ID id, VALUE obj);
	setConstFunc = cast<Function>(module->getOrInsertFunction(
		    "rb_vm_set_const",
		    Type::VoidTy, RubyObjTy, IntTy, RubyObjTy, NULL));
    }

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

    params.push_back(val);

    CallInst::Create(setConstFunc, params.begin(), params.end(), "", bb);

    return val;
}

inline Value *
RoxorCompiler::compile_current_class(void)
{
    if (current_opened_class == NULL) {
	return cObject;
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
	case NODE_COLON3:
	    what2 = nd_type(node) == NODE_COLON2
		? compile_node(node->nd_head)
		: cObject;
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
RoxorCompiler::compile_dvar_slot(ID name)
{
    // TODO we should cache this
    int i = 0, idx = -1;
    for (std::vector<ID>::iterator iter = dvars.begin();
	 iter != dvars.end(); ++iter) {
	if (*iter == name) {
	    idx = i;
	    break;
	}
	i++;
    }
    if (idx == -1) {
	return NULL;
    }

    Function::ArgumentListType::iterator fargs_i =
	bb->getParent()->getArgumentList().begin();
    ++fargs_i; // skip self
    ++fargs_i; // skip sel
    Value *dvars_ary = fargs_i;

    Value *index = ConstantInt::get(Type::Int32Ty, idx);
    Value *slot = GetElementPtrInst::Create(dvars_ary, index, rb_id2name(name), bb);
    return new LoadInst(slot, "", bb);
}

void
RoxorCompiler::compile_break_val(Value *val)
{
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
}

Value *
RoxorCompiler::compile_jump(NODE *node)
{
    const bool within_loop = current_loop_begin_bb != NULL
	&& current_loop_body_bb != NULL
	&& current_loop_end_bb != NULL;

    const bool within_block = current_block && current_mid == 0;

    Value *val = nd_type(node) == NODE_RETRY
	? nilVal
	: node->nd_head != NULL
	    ? compile_node(node->nd_head) : nilVal;

    switch (nd_type(node)) {
	case NODE_RETRY:
	    // Simply jump to the nearest begin label, after poping the
	    // current exception.
	    compile_pop_exception();
	    if (begin_bb == NULL) {
		rb_raise(rb_eSyntaxError, "unexpected retry");
	    }
	    // TODO raise a SyntaxError if called outside of a "rescue"
	    // block.
	    BranchInst::Create(begin_bb, bb);
	    break;

	case NODE_BREAK:
	    if (within_loop) {
		current_loop_exit_val = val;
		BranchInst::Create(current_loop_end_bb, bb);
	    }
	    else if (within_block) {
		compile_break_val(val);
		ReturnInst::Create(val, bb);
	    }
	    else {
		rb_raise(rb_eLocalJumpError, "unexpected break");
	    }
	    break;

	case NODE_NEXT:
	    if (within_loop) {
		BranchInst::Create(current_loop_begin_bb, bb);
	    }
	    else if (within_block) {
		ReturnInst::Create(val, bb);
	    }
	    else {
		rb_raise(rb_eLocalJumpError, "unexpected next");
	    }
	    break;

	case NODE_REDO:
	    if (within_loop) {
		BranchInst::Create(current_loop_body_bb, bb);
	    }
	    else if (within_block) {
		assert(entry_bb != NULL);
		BranchInst::Create(entry_bb, bb);
	    }
	    else {
		rb_raise(rb_eLocalJumpError, "unexpected redo");
	    }
	    break;

	case NODE_RETURN:
	    if (current_rescue) {
		compile_pop_exception();
	    }
	    if (within_block) {
		compile_break_val(val);
		// Return-from-block is implemented using a setjmp() call.
		if (longjmpFunc == NULL) {
		    // void longjmp(jmp_buf, int);
		    longjmpFunc = cast<Function>(
			    module->getOrInsertFunction("longjmp", 
				Type::VoidTy, PtrTy, Type::Int32Ty, NULL));
		}
		std::vector<Value *> params;
		if (return_from_block_jmpbuf == NULL) {
		    return_from_block_jmpbuf = 
			(jmp_buf *)malloc(sizeof(jmp_buf));
		    GET_VM()->return_from_block_jmp_bufs.push_back(
			    return_from_block_jmpbuf);
		}
		params.push_back(compile_const_pointer(
			    return_from_block_jmpbuf));
		params.push_back(ConstantInt::get(Type::Int32Ty, 1));
		CallInst::Create(longjmpFunc, params.begin(), params.end(), "",
			bb);
		ReturnInst::Create(val, bb);
	    }
	    else {
		if (ensure_bb != NULL) {
		    BranchInst::Create(ensure_bb, bb);
		}
		else {
		    ReturnInst::Create(val, bb);
		}
	    }
	    break;
    }

    // To not complicate the compiler even more, let's be very lazy here and
    // continue on a dead branch. Hopefully LLVM is smart enough to eliminate
    // it at compilation time.
    bb = BasicBlock::Create("DEAD", bb->getParent());

    return val;
}

Value *
RoxorCompiler::compile_class_path(NODE *node)
{
    if (nd_type(node) == NODE_COLON3) {
	// ::Foo
	return cObject;
    }
    else if (node->nd_head != NULL) {
	// Bar::Foo
	return compile_node(node->nd_head);
    }

    return compile_current_class();
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
RoxorCompiler::compile_pop_exception(void)
{
    if (popExceptionFunc == NULL) {
	// void rb_vm_pop_exception(void);
	popExceptionFunc = cast<Function>(
		module->getOrInsertFunction("rb_vm_pop_exception", 
		    Type::VoidTy, NULL));
    }
    CallInst::Create(popExceptionFunc, "", bb);
}

void
RoxorCompiler::compile_landing_pad_footer(void)
{
    compile_pop_exception();

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

	VALUE leftRVal = 0, rightRVal = 0;
	const bool leftIsConstant = unbox_ruby_constant(leftVal, &leftRVal);
	const bool rightIsConst = unbox_ruby_constant(rightVal, &rightRVal);

	if (leftIsConstant && rightIsConst
	    && TYPE(leftRVal) == T_SYMBOL && TYPE(rightRVal) == T_SYMBOL) {
	    // Both operands are symbol constants.
	    if (sel == selEq || sel == selEqq || sel == selNeq) {
		Value *is_redefined_val = new LoadInst(is_redefined, "", bb);
		Value *isOpRedefined = new ICmpInst(ICmpInst::ICMP_EQ, 
			is_redefined_val, ConstantInt::getFalse(), "", bb);

		Function *f = bb->getParent();

		BasicBlock *thenBB = BasicBlock::Create("op_not_redefined", f);
		BasicBlock *elseBB  = BasicBlock::Create("op_dispatch", f);
		BasicBlock *mergeBB = BasicBlock::Create("op_merge", f);

		BranchInst::Create(thenBB, elseBB, isOpRedefined, bb);
		Value *thenVal = NULL;
		if (sel == selEq || sel == selEqq) {
		    thenVal = leftRVal == rightRVal ? trueVal : falseVal;
		}
		else if (sel == selNeq) {
		    thenVal = leftRVal != rightRVal ? trueVal : falseVal;
		}
		else {
		    abort();
		}
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
	    else {
		return NULL;
	    }
	}

	const bool leftIsFixnumConstant = FIXNUM_P(leftRVal);
	const bool rightIsFixnumConstant = FIXNUM_P(rightRVal);

	long leftLong = leftIsFixnumConstant ? FIX2LONG(leftRVal) : 0;
	long rightLong = rightIsFixnumConstant ? FIX2LONG(rightRVal) : 0;

	if (leftIsFixnumConstant && rightIsFixnumConstant) {
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
	    if (!leftIsFixnumConstant) {
		leftAndOp = BinaryOperator::CreateAnd(leftVal, oneVal, "", 
			bb);
	    }

	    Value *rightAndOp = NULL;
	    if (!rightIsFixnumConstant) {
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
	    if (leftIsFixnumConstant) {
		unboxedLeft = ConstantInt::get(RubyObjTy, leftLong);
	    }
	    else {
		unboxedLeft = BinaryOperator::CreateAShr(leftVal, oneVal, "", bb);
	    }

	    Value *unboxedRight;
	    if (rightIsFixnumConstant) {
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
rb_vm_arity(int argc)
{
    rb_vm_arity_t arity;
    arity.left_req = arity.min = arity.max = arity.real = argc;
    return arity;
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
    ee = ExecutionEngine::createJIT(emp, 0, jmm, true);
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

    // Optimize.
    fpm->run(*func);

    // Compile.
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

inline struct RoxorFunctionIMP *
RoxorVM::method_func_imp_get(IMP imp)
{
    std::map<IMP, struct RoxorFunctionIMP *>::iterator iter =
	ruby_imps.find(imp);
    if (iter == ruby_imps.end()) {
	return NULL;
    }
    return iter->second;
}

inline NODE *
RoxorVM::method_node_get(IMP imp)
{
    struct RoxorFunctionIMP *func_imp = method_func_imp_get(imp);
    return func_imp == NULL ? NULL : func_imp->node;
}

extern "C"
NODE *
rb_vm_get_method_node(IMP imp)
{
    return GET_VM()->method_node_get(imp);
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
RoxorVM::add_method(Class klass, SEL sel, IMP imp, IMP ruby_imp, NODE *node,
		    const char *types)
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
    std::map<IMP, struct RoxorFunctionIMP *>::iterator iter =
	ruby_imps.find(imp);
    RoxorFunctionIMP *func_imp;
    if (iter == ruby_imps.end()) {
	ruby_imps[imp] = func_imp =
	    new RoxorFunctionIMP(node, sel, imp, ruby_imp);
    }
    else {
	func_imp = iter->second;
	func_imp->node = node;
	func_imp->sel = sel;
	func_imp->ruby_imp = ruby_imp;
    }
#if 1
    if (imp != ruby_imp) {
	ruby_imps[ruby_imp] = func_imp;
    }
#endif

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
		const bool has_dvars = current_block && current_mid == 0;

		// Get dynamic vars.
		if (has_dvars && node->nd_tbl != NULL) {
		    const int args_count = (int)node->nd_tbl[0];
		    const int lvar_count = (int)node->nd_tbl[args_count + 1];
		    for (int i = 0; i < lvar_count; i++) {
			ID id = node->nd_tbl[i + args_count + 2];
			if (lvars.find(id) != lvars.end()) {
			    std::vector<ID>::iterator iter = std::find(dvars.begin(), dvars.end(), id);
			    if (iter == dvars.end()) {
#if ROXOR_COMPILER_DEBUG
				printf("dvar %s\n", rb_id2name(id));
#endif
				dvars.push_back(id);
			    }
			}
		    }
		}

		// Create function type.
		std::vector<const Type *> types;
		types.push_back(RubyObjTy);	// self
		types.push_back(PtrTy);		// sel
		if (has_dvars) {
		    types.push_back(RubyObjPtrPtrTy); // dvars array
		    types.push_back(PtrTy); // rb_vm_block_t of the currently running block
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

		Value *old_running_block = running_block;
		Value *old_current_var_uses = current_var_uses;
		current_var_uses = NULL;

		if (has_dvars) {
		    Value *dvars_arg = arg++;
		    dvars_arg->setName("dvars");
		    running_block = arg++;
		    running_block->setName("running_block");
		}
		else {
		    running_block = NULL;
		}

		if (node->nd_tbl != NULL) {
		    bool has_vars_to_save = false;
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
			has_vars_to_save = true;
		    }

		    // local vars must be created before the optional arguments
		    // because they can be used in them, for instance with def f(a=b=c=1)
		    if (compile_lvars(&node->nd_tbl[args_count + 1])) {
			has_vars_to_save = true;
		    }

		    if (has_vars_to_save) {
			current_var_uses = new AllocaInst(PtrTy, "", bb);
			new StoreInst(compile_const_pointer(NULL), current_var_uses, bb);
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
			    int to_skip = args_node->nd_frml;
			    if (has_dvars) {
				to_skip += 2; // dvars array and currently running block
			    }
			    for (i = 0; i < to_skip; i++) {
				++iter; // skip dvars and args required on the left-side
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

		// current_lvar_uses has 2 uses or more if it is really used
		// (there is always a StoreInst in which we assign it NULL)
		if (current_var_uses != NULL && current_var_uses->hasNUsesOrMore(2)) {
		    if (keepVarsFunc == NULL) {
			// void rb_vm_keep_vars(rb_vm_var_uses *uses, int lvars_size, ...)
			std::vector<const Type *> types;
			types.push_back(PtrTy);
			types.push_back(Type::Int32Ty);
			FunctionType *ft = FunctionType::get(Type::VoidTy, types, true);
			keepVarsFunc = cast<Function>
			    (module->getOrInsertFunction("rb_vm_keep_vars", ft));
		    }

		    std::vector<Value *> params;

		    params.push_back(NULL); // will be filled later

		    params.push_back(ConstantInt::get(Type::Int32Ty, (int)lvars.size()));
		    for (std::map<ID, Value *>::iterator iter = lvars.begin();
			 iter != lvars.end(); ++iter) {
			ID name = iter->first;
			Value *slot = iter->second;
			if (std::find(dvars.begin(), dvars.end(), name) == dvars.end()) {
			    params.push_back(ConstantInt::get(IntTy, (long)name));
			    params.push_back(slot);
			}
		    }

		    // searches all ReturnInst in the function we just created and add before
		    // a call to the function to save the local variables if necessary
		    // (we can't do this before finishing compiling the whole function
		    // because we can't be sure if a block is there or not before)
		    for (Function::iterator block_it = f->begin(); block_it != f->end(); ++block_it) {
			for (BasicBlock::iterator inst_it = block_it->begin(); inst_it != block_it->end(); ++inst_it) {
			    if (dyn_cast<ReturnInst>(inst_it)) {
				// LoadInst needs to be inserted in a BasicBlock
				// so we has to wait before putting it in params
				params[0] = new LoadInst(current_var_uses, "", inst_it);

				// TODO: only call the function if current_use is not NULL
				CallInst::Create(keepVarsFunc, params.begin(), params.end(), "", inst_it);
			    }
			}
		    }
		}

		bb = old_bb;
		entry_bb = old_entry_bb;
		lvars = old_lvars;
		current_self = old_self;
		rescue_bb = old_rescue_bb;
		current_var_uses = old_current_var_uses;
		running_block = old_running_block;

		return cast<Value>(f);
	    }
	    break;

	case NODE_DVAR:
	case NODE_LVAR:
	    {
		assert(node->nd_vid > 0);

		return new LoadInst(compile_lvar_slot(node->nd_vid), "", bb);
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

		return compile_gvar_assignment(
			node->nd_entry,
			compile_node(node->nd_value));
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
	    assert(node->nd_vid > 0);
	    assert(node->nd_value != NULL);
	    return compile_cvar_assignment(node->nd_vid,
		    compile_node(node->nd_value));

	case NODE_MASGN:
	    {
		NODE *rhsn = node->nd_value;
		assert(rhsn != NULL);

		Value *ary = compile_node(rhsn);

		return compile_multiple_assignment(node, ary);
	    }
	    break;

	case NODE_DASGN:
	case NODE_DASGN_CURR:
	case NODE_LASGN:
	    {
		assert(node->nd_vid > 0);
		assert(node->nd_value != NULL);

		Value *new_val = compile_node(node->nd_value);
		new StoreInst(new_val, compile_lvar_slot(node->nd_vid), bb);

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
	case NODE_DREGX_ONCE: // TODO optimize NODE_DREGX_ONCE
	    {
		Value *val  = compile_dstr(node);
		const int flag = node->nd_cflag;

		if (newRegexpFunc == NULL) {
		    newRegexpFunc = cast<Function>(module->getOrInsertFunction(
				"rb_reg_new_str",
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
		    ID *tbl = body->nd_tbl;
		    if (tbl != NULL) {
			const int args_count = (int)tbl[0];
			compile_lvars(&tbl[args_count + 1]);
		    }
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

			bool old_current_module = current_module;

			std::map<ID, int *> old_ivar_slots_cache = ivar_slots_cache;
			ivar_slots_cache.clear();

			new StoreInst(classVal, current_opened_class, bb);

			current_module = nd_type(node) == NODE_MODULE;

			Value *val = compile_node(body->nd_body);

			BasicBlock::InstListType &list = bb->getInstList();
			compile_ivar_slots(classVal, list, list.end());

			current_self = old_self;
			current_opened_class = old_class;
			current_module = old_current_module;

			ivar_slots_cache = old_ivar_slots_cache;

			return val;
		    }
		}

		return nilVal;
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

		const bool block_given = current_block_func != NULL
		    && current_block_node != NULL;
		const bool super_call = nd_type(node) == NODE_SUPER
		    || nd_type(node) == NODE_ZSUPER;

		if (super_call) {
		    mid = current_mid;
		}
		assert(mid > 0);

		Function::ArgumentListType &fargs =
		    bb->getParent()->getArgumentList();
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

		    // TODO check if both functions have the same arity
		    // (paranoid?)

		    Function *f = bb->getParent();
		    std::vector<Value *> params;

		    Function::arg_iterator arg = f->arg_begin();

		    params.push_back(arg++); // self
		    params.push_back(arg++); // sel 

		    for (NODE *n = args; n != NULL; n = n->nd_next) {
			params.push_back(compile_node(n->nd_head));
		    }

		    CallInst *inst = CallInst::Create(f, params.begin(),
			    params.end(), "", bb);
		    inst->setTailCall(true);
		    return cast<Value>(inst);
		}

		// Let's set the block state as NULL temporarily, when we
		// compile the receiver and the arguments. 
		Function *old_current_block_func = current_block_func;
		NODE *old_current_block_node = current_block_node;
		current_block_func = NULL;
		current_block_node = NULL;

		// Prepare the dispatcher parameters.
		std::vector<Value *> params;

		// Method cache.
		const SEL sel = mid_to_sel(mid, positive_arity ? 1 : 0);
		void *cache = GET_VM()->method_cache_get(sel, super_call);
		params.push_back(compile_const_pointer(cache));

		// Self.
		params.push_back(recv == NULL ? current_self
			: compile_node(recv));

		// Selector.
		params.push_back(compile_const_pointer((void *)sel));

		// RubySpec requires that we compile the block *after* the
		// arguments, so we do pass NULL as the block for the moment.
		params.push_back(compile_const_pointer(NULL));
		NODE *real_args = args;
		if (real_args != NULL
		    && nd_type(real_args) == NODE_BLOCK_PASS) {
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
		    params.push_back(ConstantInt::get(Type::Int32Ty,
				fargs_arity));
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

		// Restore the block state.
		current_block_func = old_current_block_func;
		current_block_node = old_current_block_node;

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

		// If we are calling a method that needs a top-level binding
		// object, let's create it.
		// (Note: this won't work if the method is aliased, but we can
		//  live with that for now)
		if (sel == selEval
		    || sel == selInstanceEval
		    || sel == selClassEval
		    || sel == selModuleEval
		    || sel == selLocalVariables
		    || sel == selBinding) {
		    compile_binding();
		}

		// Can we optimize the call?
		if (!super_call && !splat_args) {
		    Value *optimizedCall =
			compile_optimized_dispatch_call(sel, argc, params);
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
	case NODE_RETRY:
	    return compile_jump(node);

	case NODE_CONST:
	    assert(node->nd_vid > 0);
	    return compile_const(node->nd_vid, NULL);

	case NODE_CDECL:
	    {
		assert(node->nd_value != NULL);
		return compile_constant_declaration(node, compile_node(node->nd_value));
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
		    if (toAFunc == NULL) {
			// VALUE rb_vm_to_a(VALUE obj);
			toAFunc = cast<Function>(
				module->getOrInsertFunction("rb_vm_to_a",
				    RubyObjTy, RubyObjTy, NULL));
		    }

		    std::vector<Value *> params;
		    params.push_back(val);
		    val = compile_protected_call(toAFunc, params);
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

	case NODE_MATCH:
	case NODE_MATCH2:
	case NODE_MATCH3:
	    {
		Value *reTarget;
		Value *reSource;

		if (nd_type(node) == NODE_MATCH) {
		    assert(node->nd_lit != 0);
		    reTarget = ConstantInt::get(RubyObjTy, node->nd_lit);
		    reSource = nilVal; // TODO this should get $_
		}
		else {
		    assert(node->nd_recv);
		    assert(node->nd_value);
		    if (nd_type(node) == NODE_MATCH2) {
			reTarget = compile_node(node->nd_recv);
			reSource = compile_node(node->nd_value);
		    }
		    else {
			reTarget = compile_node(node->nd_value);
			reSource = compile_node(node->nd_recv);
		    }
		}

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
		bool old_current_rescue = current_rescue;
		current_rescue = true;
		Value *rescue_val = compile_node(node->nd_resq);
		current_rescue = old_current_rescue;
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
		if (currentExceptionFunc == NULL) {
		    // VALUE rb_vm_current_exception(void);
		    currentExceptionFunc = cast<Function>(
			    module->getOrInsertFunction(
				"rb_vm_current_exception", 
				RubyObjTy, NULL));
		}
		return CallInst::Create(currentExceptionFunc, "", bb);
	    }
	    break;

	case NODE_ENSURE:
	    {
		assert(node->nd_head != NULL);
		assert(node->nd_ensr != NULL);

		Function *f = bb->getParent();
		BasicBlock *old_ensure_bb = ensure_bb;
		ensure_bb = BasicBlock::Create("ensure", f);
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
		}
		else {
		    val = compile_node(node->nd_head);
		    BranchInst::Create(ensure_bb, bb);
		}

		bb = ensure_bb;
		ensure_bb = old_ensure_bb;
		compile_node(node->nd_ensr);

		return val;
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
		std::vector<ID> old_dvars = dvars;

		BasicBlock *old_current_loop_begin_bb = current_loop_begin_bb;
		BasicBlock *old_current_loop_body_bb = current_loop_body_bb;
		BasicBlock *old_current_loop_end_bb = current_loop_end_bb;
		current_loop_begin_bb = current_loop_end_bb = NULL;
		Function *old_current_block_func = current_block_func;
		NODE *old_current_block_node = current_block_node;
		ID old_current_mid = current_mid;
		bool old_current_block = current_block;
		jmp_buf *old_return_from_block_jmpbuf =
		    return_from_block_jmpbuf;

		current_mid = 0;
		current_block = true;
		return_from_block_jmpbuf = NULL;

		assert(node->nd_body != NULL);
		Value *block = compile_node(node->nd_body);	
		assert(Function::classof(block));

		if (return_from_block_jmpbuf != NULL) {
		    // The block we just compiled contains one (or more)
		    // return expressions and provided us a longjmp buffer.
		    // Let's compile a call to setjmp() and make sure to
		    // return if its return value is non-zero.
		    if (setjmpFunc == NULL) {
			// int setjmp(jmp_buf);
			setjmpFunc = cast<Function>(
				module->getOrInsertFunction("setjmp", 
				    Type::Int32Ty, PtrTy, NULL));
		    }
		    std::vector<Value *> params;
		    params.push_back(compile_const_pointer(
				return_from_block_jmpbuf));
		    Value *retval = CallInst::Create(setjmpFunc,
			    params.begin(), params.end(), "", bb);

		    Function *f = bb->getParent();
		    BasicBlock *ret_bb = BasicBlock::Create("ret", f);
		    BasicBlock *no_ret_bb  = BasicBlock::Create("no_ret", f);
		    Value *need_ret = new ICmpInst(ICmpInst::ICMP_NE, 
			    retval, ConstantInt::get(Type::Int32Ty, 0), 
			    "", bb);
		    BranchInst::Create(ret_bb, no_ret_bb, need_ret, bb);
		
		    bb = ret_bb;
		    if (popBrokenValue == NULL) {
			// VALUE rb_vm_pop_broken_value(void);
			popBrokenValue = cast<Function>(
				module->getOrInsertFunction(
				    "rb_vm_pop_broken_value", 
				    RubyObjTy, NULL));
		    }
		    params.clear();
		    Value *val = compile_protected_call(popBrokenValue, params);
		    ReturnInst::Create(val, bb);	

		    bb = no_ret_bb;
		}

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
		return_from_block_jmpbuf = old_return_from_block_jmpbuf;
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
		    FunctionType *ft =
			FunctionType::get(RubyObjTy, types, true);
		    yieldFunc = cast<Function>(module->getOrInsertFunction(
				"rb_vm_yield_args", ft));
		}

		std::vector<Value *> params;
		int argc = 0;
		if (node->nd_head != NULL) {
		    compile_dispatch_arguments(node->nd_head, params, &argc);
		}
		params.insert(params.begin(), ConstantInt::get(Type::Int32Ty, argc));

		return CallInst::Create(yieldFunc, params.begin(),
			params.end(), "", bb);
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
	    return compile_const(node->nd_mid, cObject);

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
    ivar_slots_cache.clear();

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

static inline const char *
GetFirstType(const char *p, char *buf, size_t buflen)
{
    const char *p2 = SkipFirstType(p);
    const size_t len = p2 - p;
    assert(len < buflen);
    strncpy(buf, p, len);
    buf[len] = '\0';
    return p2;
}

static inline void
convert_error(const char type, VALUE val)
{
    rb_raise(rb_eTypeError,
	     "cannot convert object `%s' (%s) to Objective-C type `%c'",
	     RSTRING_PTR(rb_inspect(val)),
	     rb_obj_classname(val),
	     type); 
}

extern "C"
void
rb_vm_rval_to_ocval(VALUE rval, id *ocval)
{
    *ocval = rval == Qnil ? NULL : RB2OC(rval);
}

extern "C"
void
rb_vm_rval_to_bool(VALUE rval, BOOL *ocval)
{
    switch (TYPE(rval)) {
	case T_FALSE:
	case T_NIL:
	    *ocval = NO;
	    break;

	default:
	    // All other types should be converted as true, to follow the Ruby
	    // semantics (where for example any integer is always true, even 0).
	    *ocval = YES;
	    break;
    }
}

extern "C"
void
rb_vm_rval_to_ocsel(VALUE rval, SEL *ocval)
{
    if (NIL_P(rval)) {
	*ocval = NULL;
    }
    else {
	const char *cstr;

	switch (TYPE(rval)) {
	    case T_STRING:
		cstr = StringValuePtr(rval);
		break;

	    case T_SYMBOL:
		cstr = rb_sym2name(rval);
		break;

	    default:
		convert_error(_C_SEL, rval);
	}
	*ocval = sel_registerName(cstr);
    }
}

extern "C"
void
rb_vm_rval_to_charptr(VALUE rval, const char **ocval)
{
    *ocval = NIL_P(rval) ? NULL : StringValueCStr(rval);
}

static inline long
rval_to_long(VALUE rval)
{
   return NUM2LONG(rb_Integer(rval)); 
}

static inline long long
rval_to_long_long(VALUE rval)
{
    return NUM2LL(rb_Integer(rval));
}

static inline double
rval_to_double(VALUE rval)
{
    return RFLOAT_VALUE(rb_Float(rval));
}

extern "C"
void
rb_vm_rval_to_chr(VALUE rval, char *ocval)
{
    if (TYPE(rval) == T_STRING && RSTRING_LEN(rval) == 1) {
	*ocval = (char)RSTRING_PTR(rval)[0];
    }
    else {
	*ocval = (char)rval_to_long(rval);
    }
}

extern "C"
void
rb_vm_rval_to_uchr(VALUE rval, unsigned char *ocval)
{
    if (TYPE(rval) == T_STRING && RSTRING_LEN(rval) == 1) {
	*ocval = (unsigned char)RSTRING_PTR(rval)[0];
    }
    else {
	*ocval = (unsigned char)rval_to_long(rval);
    }
}

extern "C"
void
rb_vm_rval_to_short(VALUE rval, short *ocval)
{
    *ocval = (short)rval_to_long(rval);
}

extern "C"
void
rb_vm_rval_to_ushort(VALUE rval, unsigned short *ocval)
{
    *ocval = (unsigned short)rval_to_long(rval);
}

extern "C"
void
rb_vm_rval_to_int(VALUE rval, int *ocval)
{
    *ocval = (int)rval_to_long(rval);
}

extern "C"
void
rb_vm_rval_to_uint(VALUE rval, unsigned int *ocval)
{
    *ocval = (unsigned int)rval_to_long(rval);
}

extern "C"
void
rb_vm_rval_to_long(VALUE rval, long *ocval)
{
    *ocval = (long)rval_to_long(rval);
}

extern "C"
void
rb_vm_rval_to_ulong(VALUE rval, unsigned long *ocval)
{
    *ocval = (unsigned long)rval_to_long(rval);
}

extern "C"
void
rb_vm_rval_to_long_long(VALUE rval, long long *ocval)
{
    *ocval = (long long)rval_to_long_long(rval);
}

extern "C"
void
rb_vm_rval_to_ulong_long(VALUE rval, unsigned long long *ocval)
{
    *ocval = (unsigned long long)rval_to_long_long(rval);
}

extern "C"
void
rb_vm_rval_to_double(VALUE rval, double *ocval)
{
    *ocval = (double)rval_to_double(rval);
}

extern "C"
void
rb_vm_rval_to_float(VALUE rval, float *ocval)
{
    *ocval = (float)rval_to_double(rval);
}

static void *rb_pointer_get_data(VALUE rcv, const char *type);

extern "C"
void *
rb_vm_rval_to_cptr(VALUE rval, const char *type, void **cptr)
{
    if (NIL_P(rval)) {
	*cptr = NULL;
    }
    else {
	*cptr = rb_pointer_get_data(rval, type);
    }
    return *cptr;
}

static inline long
rebuild_new_struct_ary(const StructType *type, VALUE orig, VALUE new_ary)
{
    long n = 0;

    for (StructType::element_iterator iter = type->element_begin();
	 iter != type->element_end();
	 ++iter) {

	const Type *ftype = *iter;
	
	if (ftype->getTypeID() == Type::StructTyID) {
            long i, n2;
            VALUE tmp;

            n2 = rebuild_new_struct_ary(cast<StructType>(ftype), orig, new_ary);
            tmp = rb_ary_new();
            for (i = 0; i < n2; i++) {
                if (RARRAY_LEN(orig) == 0) {
                    return 0;
		}
                rb_ary_push(tmp, rb_ary_shift(orig));
            }
            rb_ary_push(new_ary, tmp);
        }
        n++;
    }

    return n;
}

extern "C"
void
rb_vm_get_struct_fields(VALUE rval, VALUE *buf, rb_vm_bs_boxed_t *bs_boxed)
{
    if (TYPE(rval) == T_ARRAY) {
	unsigned n = RARRAY_LEN(rval);
	if (n < bs_boxed->as.s->fields_count) {
	    rb_raise(rb_eArgError,
		    "not enough elements in array `%s' to create " \
		    "structure `%s' (%d for %d)",
		    RSTRING_PTR(rb_inspect(rval)), bs_boxed->as.s->name, n,
		    bs_boxed->as.s->fields_count);
	}

	if (n > bs_boxed->as.s->fields_count) {
	    VALUE new_rval = rb_ary_new();
	    VALUE orig = rval;
	    rval = rb_ary_dup(rval);
	    rebuild_new_struct_ary(cast<StructType>(bs_boxed->type), rval,
		    new_rval);
	    n = RARRAY_LEN(new_rval);
	    if (RARRAY_LEN(rval) != 0 || n != bs_boxed->as.s->fields_count) {
		rb_raise(rb_eArgError,
			"too much elements in array `%s' to create " \
			"structure `%s' (%ld for %d)",
			RSTRING_PTR(rb_inspect(orig)),
			bs_boxed->as.s->name, RARRAY_LEN(orig),
			bs_boxed->as.s->fields_count);
	    }
	    rval = new_rval;
	}

	for (unsigned i = 0; i < n; i++) {
	    buf[i] = RARRAY_AT(rval, i);
	}
    }
    else {
	if (!rb_obj_is_kind_of(rval, bs_boxed->klass)) {
	    rb_raise(rb_eTypeError, 
		    "expected instance of `%s', got `%s' (%s)",
		    rb_class2name(bs_boxed->klass),
		    RSTRING_PTR(rb_inspect(rval)),
		    rb_obj_classname(rval));
	}

	VALUE *data;
	Data_Get_Struct(rval, VALUE, data);

	for (unsigned i = 0; i < bs_boxed->as.s->fields_count; i++) {
	    buf[i] = data[i];
	}	
    }
}

void
RoxorCompiler::compile_get_struct_fields(Value *val, Value *buf,
					 rb_vm_bs_boxed_t *bs_boxed)
{
    if (getStructFieldsFunc == NULL) {
	getStructFieldsFunc = cast<Function>(module->getOrInsertFunction(
		    "rb_vm_get_struct_fields",
		    Type::VoidTy, RubyObjTy, RubyObjPtrTy, PtrTy, NULL));
    }

    std::vector<Value *> params;
    params.push_back(val);
    params.push_back(buf);
    params.push_back(compile_const_pointer(bs_boxed));

    CallInst::Create(getStructFieldsFunc, params.begin(), params.end(), "", bb);
}

extern "C"
void *
rb_vm_get_opaque_data(VALUE rval, rb_vm_bs_boxed_t *bs_boxed, void **ocval)
{
    if (rval == Qnil) {
	return *ocval = NULL;
    }
    else {
	if (!rb_obj_is_kind_of(rval, bs_boxed->klass)) {
	    rb_raise(rb_eTypeError,
		    "cannot convert `%s' (%s) to opaque type %s",
		    RSTRING_PTR(rb_inspect(rval)),
		    rb_obj_classname(rval),
		    rb_class2name(bs_boxed->klass));
	}
	VALUE *data;
	Data_Get_Struct(rval, VALUE, data);
	return *ocval = (void *)data;
    }
}

Value *
RoxorCompiler::compile_get_opaque_data(Value *val, rb_vm_bs_boxed_t *bs_boxed,
				       Value *slot)
{
    if (getOpaqueDataFunc == NULL) {
	getOpaqueDataFunc = cast<Function>(module->getOrInsertFunction(
		    "rb_vm_get_opaque_data",
		    PtrTy, RubyObjTy, PtrTy, PtrPtrTy, NULL));
    }

    std::vector<Value *> params;
    params.push_back(val);
    params.push_back(compile_const_pointer(bs_boxed));
    params.push_back(slot);

    return compile_protected_call(getOpaqueDataFunc, params);
}

Value *
RoxorCompiler::compile_get_cptr(Value *val, const char *type, Value *slot)
{
    if (getPointerPtrFunc == NULL) {
	getPointerPtrFunc = cast<Function>(module->getOrInsertFunction(
		    "rb_vm_rval_to_cptr",
		    PtrTy, RubyObjTy, PtrTy, PtrPtrTy, NULL));
    }

    std::vector<Value *> params;
    params.push_back(val);
    params.push_back(compile_const_pointer(sel_registerName(type)));
    params.push_back(new BitCastInst(slot, PtrPtrTy, "", bb));

    return compile_protected_call(getPointerPtrFunc, params);
}

Value *
RoxorCompiler::compile_conversion_to_c(const char *type, Value *val,
				       Value *slot)
{
    const char *func_name = NULL;

    type = SkipTypeModifiers(type);

    if (*type == _C_PTR
	&& GET_VM()->bs_cftypes.find(type) != GET_VM()->bs_cftypes.end()) {
	type = "@";
    }

    switch (*type) {
	case _C_ID:
	case _C_CLASS:
	    func_name = "rb_vm_rval_to_ocval";
	    break;

	case _C_BOOL:
	    func_name = "rb_vm_rval_to_bool";
	    break;

	case _C_CHR:
	    func_name = "rb_vm_rval_to_chr";
	    break;

	case _C_UCHR:
	    func_name = "rb_vm_rval_to_uchr";
	    break;

	case _C_SHT:
	    func_name = "rb_vm_rval_to_short";
	    break;

	case _C_USHT:
	    func_name = "rb_vm_rval_to_ushort";
	    break;

	case _C_INT:
	    func_name = "rb_vm_rval_to_int";
	    break;

	case _C_UINT:
	    func_name = "rb_vm_rval_to_uint";
	    break;

	case _C_LNG:
	    func_name = "rb_vm_rval_to_long";
	    break;

	case _C_ULNG:
	    func_name = "rb_vm_rval_to_ulong";
	    break;

	case _C_LNG_LNG:
	    func_name = "rb_vm_rval_to_long_long";
	    break;

	case _C_ULNG_LNG:
	    func_name = "rb_vm_rval_to_ulong_long";
	    break;

	case _C_FLT:
	    func_name = "rb_vm_rval_to_float";
	    break;

	case _C_DBL:
	    func_name = "rb_vm_rval_to_double";
	    break;

	case _C_SEL:
	    func_name = "rb_vm_rval_to_ocsel";
	    break;

	case _C_CHARPTR:
	    func_name = "rb_vm_rval_to_charptr";
	    break;

	case _C_STRUCT_B:
	    {
		rb_vm_bs_boxed_t *bs_boxed = GET_VM()->find_bs_struct(type);
		if (bs_boxed != NULL) {
		    Value *fields = new AllocaInst(RubyObjTy,
			    ConstantInt::get(Type::Int32Ty,
				bs_boxed->as.s->fields_count), "", bb);

		    compile_get_struct_fields(val, fields, bs_boxed);

		    for (unsigned i = 0; i < bs_boxed->as.s->fields_count;
			    i++) {

			const char *ftype = bs_boxed->as.s->fields[i].type;

			// Load field VALUE.
			Value *fval = GetElementPtrInst::Create(fields,
				ConstantInt::get(Type::Int32Ty, i), "", bb);
			fval = new LoadInst(fval, "", bb);

			// Get a pointer to the struct field. The extra 0 is
			// needed because we are dealing with a pointer to the
			// structure.
			std::vector<Value *> slot_idx;
			slot_idx.push_back(ConstantInt::get(Type::Int32Ty, 0));
			slot_idx.push_back(ConstantInt::get(Type::Int32Ty, i));
			Value *fslot = GetElementPtrInst::Create(slot,
				slot_idx.begin(), slot_idx.end(), "", bb);

			RoxorCompiler::compile_conversion_to_c(ftype, fval,
				fslot);
		    }

		    if (GET_VM()->is_large_struct_type(bs_boxed->type)) {
			// If this structure is too large, we need to pass its
			// address and not its value, to conform to the ABI.
			return slot;
		    }
		    return new LoadInst(slot, "", bb);
		}
	    }
	    break;

	case _C_PTR:
	    {
		rb_vm_bs_boxed_t *bs_boxed = GET_VM()->find_bs_opaque(type);
		if (bs_boxed != NULL) {
		    return compile_get_opaque_data(val, bs_boxed, slot);
		}

		return compile_get_cptr(val, type, slot);
	    }
	    break;
    }

    if (func_name == NULL) {
	rb_raise(rb_eTypeError, "unrecognized compile type `%s' to C", type);
    }
 
    std::vector<Value *> params;
    params.push_back(val);
    params.push_back(slot);

    Function *func = cast<Function>(module->getOrInsertFunction(
		func_name, Type::VoidTy, RubyObjTy,
		PointerType::getUnqual(convert_type(type)), NULL));

    CallInst::Create(func, params.begin(), params.end(), "", bb);
    return new LoadInst(slot, "", bb);
}

extern "C"
VALUE
rb_vm_ocval_to_rval(id ocval)
{
    return OC2RB(ocval);
}

extern "C"
VALUE
rb_vm_long_to_rval(long l)
{
    return INT2NUM(l);
}

extern "C"
VALUE
rb_vm_ulong_to_rval(long l)
{
    return UINT2NUM(l);
}

extern "C"
VALUE
rb_vm_long_long_to_rval(long long l)
{
    return LL2NUM(l);
}

extern "C"
VALUE
rb_vm_ulong_long_to_rval(unsigned long long l)
{
    return ULL2NUM(l);
}

extern "C"
VALUE
rb_vm_sel_to_rval(SEL sel)
{
    return sel == 0 ? Qnil : ID2SYM(rb_intern(sel_getName(sel)));
}

extern "C"
VALUE
rb_vm_charptr_to_rval(const char *ptr)
{
    return ptr == NULL ? Qnil : rb_str_new2(ptr);
}

extern "C"
VALUE
rb_vm_new_struct(VALUE klass, int argc, ...)
{
    assert(argc > 0);

    va_list ar;
    va_start(ar, argc);
    VALUE *data = (VALUE *)xmalloc(argc * sizeof(VALUE));
    for (int i = 0; i < argc; ++i) {
	VALUE field = va_arg(ar, VALUE);
	GC_WB(&data[i], field);
    }
    va_end(ar);

    return Data_Wrap_Struct(klass, NULL, NULL, data);
}

extern "C"
VALUE
rb_vm_new_opaque(VALUE klass, void *val)
{
    return Data_Wrap_Struct(klass, NULL, NULL, val);
}

Value *
RoxorCompiler::compile_new_struct(Value *klass, std::vector<Value *> &fields)
{
    if (newStructFunc == NULL) {
	std::vector<const Type *> types;
	types.push_back(RubyObjTy);
	types.push_back(Type::Int32Ty);
	FunctionType *ft = FunctionType::get(RubyObjTy, types, true);

	newStructFunc = cast<Function>(module->getOrInsertFunction(
		    "rb_vm_new_struct", ft));
    }

    Value *argc = ConstantInt::get(Type::Int32Ty, fields.size());
    fields.insert(fields.begin(), argc);
    fields.insert(fields.begin(), klass);

    return CallInst::Create(newStructFunc, fields.begin(), fields.end(),
	    "", bb); 
}

Value *
RoxorCompiler::compile_new_opaque(Value *klass, Value *val)
{
    if (newOpaqueFunc == NULL) {
	newOpaqueFunc = cast<Function>(module->getOrInsertFunction(
		    "rb_vm_new_opaque", RubyObjTy, RubyObjTy, PtrTy, NULL));
    }

    std::vector<Value *> params;
    params.push_back(klass);
    params.push_back(val);

    return CallInst::Create(newOpaqueFunc, params.begin(), params.end(),
	    "", bb); 
}

Value *
RoxorCompiler::compile_conversion_to_ruby(const char *type,
					  const Type *llvm_type, Value *val)
{
    const char *func_name = NULL;

    type = SkipTypeModifiers(type);

    if (*type == _C_PTR
	&& GET_VM()->bs_cftypes.find(type) != GET_VM()->bs_cftypes.end()) {
	type = "@";
    }

    switch (*type) {
	case _C_VOID:
	    return nilVal;

	case _C_BOOL:
	    {
		Value *is_true = new ICmpInst(ICmpInst::ICMP_EQ, val,
			ConstantInt::get(Type::Int8Ty, 1), "", bb);
		return SelectInst::Create(is_true, trueVal, falseVal, "", bb);
	    }

	case _C_ID:
	case _C_CLASS:
	    func_name = "rb_vm_ocval_to_rval";
	    break;

	case _C_CHR:
	case _C_SHT:
	case _C_INT:
	    val = new SExtInst(val, RubyObjTy, "", bb);
	    val = BinaryOperator::CreateShl(val, oneVal, "", bb);
	    val = BinaryOperator::CreateOr(val, oneVal, "", bb);
	    return val;

	case _C_UCHR:
	case _C_USHT:
	case _C_UINT:
	    val = new ZExtInst(val, RubyObjTy, "", bb);
	    val = BinaryOperator::CreateShl(val, oneVal, "", bb);
	    val = BinaryOperator::CreateOr(val, oneVal, "", bb);
	    return val;

	case _C_LNG:
	    func_name = "rb_vm_long_to_rval";
	    break;

	case _C_ULNG:
	    func_name = "rb_vm_ulong_to_rval";
	    break;

	case _C_LNG_LNG:
	    func_name = "rb_vm_long_long_to_rval";
	    break;

	case _C_ULNG_LNG:
	    func_name = "rb_vm_ulong_long_to_rval";
	    break;

	case _C_FLT:
	    {
		char buf = _C_DBL;
		const Type *dbl_type = convert_type(&buf); 
		val = new FPExtInst(val, dbl_type, "", bb);
		llvm_type = dbl_type;
	    }
	    // fall through	
	case _C_DBL:
	    func_name = "rb_float_new";
	    break;

	case _C_SEL:
	    func_name = "rb_vm_sel_to_rval";
	    break;

	case _C_CHARPTR:
	    func_name = "rb_vm_charptr_to_rval";
	    break;

	case _C_STRUCT_B:
	    {
		rb_vm_bs_boxed_t *bs_boxed = GET_VM()->find_bs_struct(type);
		if (bs_boxed != NULL) {
		    std::vector<Value *> params;

		    for (unsigned i = 0; i < bs_boxed->as.s->fields_count;
			    i++) {

			const char *ftype = bs_boxed->as.s->fields[i].type;
			const Type *llvm_ftype = convert_type(ftype);
			Value *fval = ExtractValueInst::Create(val, i, "", bb);

			params.push_back(compile_conversion_to_ruby(ftype,
				    llvm_ftype, fval));
		    }

		    Value *klass = ConstantInt::get(RubyObjTy, bs_boxed->klass);
		    return compile_new_struct(klass, params);
		}
	    }
	    break;

	case _C_PTR:
	    {
		rb_vm_bs_boxed_t *bs_boxed = GET_VM()->find_bs_opaque(type);
		if (bs_boxed != NULL) {
		    Value *klass = ConstantInt::get(RubyObjTy, bs_boxed->klass);
		    return compile_new_opaque(klass, val);
		}
	    }
	    break; 
    }

    if (func_name == NULL) {
	rb_raise(rb_eTypeError, "unrecognized compile type `%s' to Ruby", type);
	abort();
    }
 
    std::vector<Value *> params;
    params.push_back(val);

    Function *func = cast<Function>(module->getOrInsertFunction(
		func_name, RubyObjTy, llvm_type, NULL));

    return CallInst::Create(func, params.begin(), params.end(), "", bb);
}

inline const Type *
RoxorCompiler::convert_type(const char *type)
{
    type = SkipTypeModifiers(type);

    switch (*type) {
	case _C_VOID:
	    return Type::VoidTy;

	case _C_ID:
	case _C_CLASS:
	    return PtrTy;

	case _C_SEL:
	case _C_CHARPTR:
	case _C_PTR:
	    return PtrTy;

	case _C_BOOL:
	case _C_UCHR:
	case _C_CHR:
	    return Type::Int8Ty;

	case _C_SHT:
	case _C_USHT:
	    return Type::Int16Ty;

	case _C_INT:
	case _C_UINT:
	    return Type::Int32Ty;

	case _C_LNG:
	case _C_ULNG:
#if __LP64__
	    return Type::Int64Ty;
#else
	    return Type::Int32Ty;
#endif

	case _C_FLT:
	    return Type::FloatTy;

	case _C_DBL:
	    return Type::DoubleTy;

	case _C_LNG_LNG:
	case _C_ULNG_LNG:
	    return Type::Int64Ty;

	case _C_STRUCT_B:
	    rb_vm_bs_boxed_t *bs_boxed = GET_VM()->find_bs_struct(type);
	    if (bs_boxed != NULL) {
		if (bs_boxed->type == NULL) {
		    std::vector<const Type *> s_types;
		    for (unsigned i = 0; i < bs_boxed->as.s->fields_count;
			 i++) {

			const char *ftype = bs_boxed->as.s->fields[i].type;
			s_types.push_back(convert_type(ftype));
		    }
		    bs_boxed->type = StructType::get(s_types);
		    assert(bs_boxed->type != NULL);
		}
		return bs_boxed->type;
	    }
	    break;
    }

    rb_raise(rb_eTypeError, "unrecognized runtime type `%s'", type);
}

Function *
RoxorCompiler::compile_stub(const char *types, int argc, bool is_objc)
{
    Function *f;

    if (is_objc) {
	// VALUE stub(IMP imp, VALUE self, SEL sel, int argc, VALUE *argv)
	// {
	//     return (*imp)(self, sel, argv[0], argv[1], ...);
	// }
	f = cast<Function>(module->getOrInsertFunction("",
		    RubyObjTy,
		    PtrTy, RubyObjTy, PtrTy, Type::Int32Ty, RubyObjPtrTy,
		    NULL));
    }
    else {
	// VALUE stub(IMP imp, int argc, VALUE *argv)
	// {
	//     return (*imp)(argv[0], argv[1], ...);
	// }
	f = cast<Function>(module->getOrInsertFunction("",
		    RubyObjTy,
		    PtrTy, Type::Int32Ty, RubyObjPtrTy,
		    NULL));
    }

    bb = BasicBlock::Create("EntryBlock", f);

    Function::arg_iterator arg = f->arg_begin();
    Value *imp_arg = arg++;

    std::vector<const Type *> f_types;
    std::vector<Value *> params;

    // retval
    char buf[100];
    const char *p = GetFirstType(types, buf, sizeof buf);
    const Type *ret_type = convert_type(buf);

    Value *sret = NULL;
    if (GET_VM()->is_large_struct_type(ret_type)) {
	// We are returning a large struct, we need to pass a pointer as the
	// first argument to the structure data and return void to conform to
	// the ABI.
	f_types.push_back(PointerType::getUnqual(ret_type));
	sret = new AllocaInst(ret_type, "", bb);
	params.push_back(sret);
	ret_type = Type::VoidTy;
    }

    if (is_objc) {
	// self
	p = SkipFirstType(p);
	f_types.push_back(RubyObjTy);
	Value *self_arg = arg++;
	params.push_back(self_arg);

	// sel
	p = SkipFirstType(p);
	f_types.push_back(PtrTy);
	Value *sel_arg = arg++;
	params.push_back(sel_arg);
    }

    /*Value *argc_arg =*/ arg++; // XXX do we really need this argument?
    Value *argv_arg = arg++;

    // Arguments.
    std::vector<int> byval_args;
    int given_argc = 0;
    bool variadic = false;
    while ((p = GetFirstType(p, buf, sizeof buf)) != NULL && buf[0] != '\0') {
	if (given_argc == argc) {
	    variadic = true;
	}

	const Type *llvm_type = convert_type(buf);
	const Type *f_type = llvm_type;
	if (GET_VM()->is_large_struct_type(llvm_type)) {
	    // We are passing a large struct, we need to mark this argument
	    // with the byval attribute and configure the internal stub
	    // call to pass a pointer to the structure, to conform to the
	    // ABI.
	    f_type = PointerType::getUnqual(llvm_type);
	    byval_args.push_back(f_types.size() + 1 /* retval */);
	}

	if (!variadic) {
	    // In order to conform to the ABI, we must stop providing types once we
	    // start dealing with variable arguments and instead mark the function as
	    // variadic.
	    f_types.push_back(f_type);
	}

	Value *index = ConstantInt::get(Type::Int32Ty, given_argc);
	Value *slot = GetElementPtrInst::Create(argv_arg, index, "", bb);
	Value *arg_val = new LoadInst(slot, "", bb);
	Value *new_val_slot = new AllocaInst(llvm_type, "", bb);

	params.push_back(compile_conversion_to_c(buf, arg_val, new_val_slot));

	given_argc++;
    }

    // Appropriately cast the IMP argument.
    FunctionType *ft = FunctionType::get(ret_type, f_types, variadic);
    Value *imp = new BitCastInst(imp_arg, PointerType::getUnqual(ft), "", bb);

    // Compile call.
    CallInst *imp_call = CallInst::Create(imp, params.begin(), params.end(),
	    "", bb); 

    for (std::vector<int>::iterator iter = byval_args.begin();
	 iter != byval_args.end(); ++iter) {
	
	imp_call->addAttribute(*iter, Attribute::ByVal);
    }

    // Compile retval.
    Value *retval;
    if (sret != NULL) {
	imp_call->addAttribute(0, Attribute::StructRet);
	retval = new LoadInst(sret, "", bb);
    }
    else {
	retval = imp_call;
    }
    GetFirstType(types, buf, sizeof buf);
    retval = compile_conversion_to_ruby(buf, convert_type(buf), retval);
    ReturnInst::Create(retval, bb);

    return f;
}

bool
RoxorCompiler::compile_lvars(ID *tbl)
{
    int lvar_count = (int)tbl[0];
    int has_real_lvars = false;
    for (int i = 0; i < lvar_count; i++) {
	ID id = tbl[i + 1];
	if (lvars.find(id) != lvars.end()) {
	    continue;
	}
	if (std::find(dvars.begin(), dvars.end(), id) == dvars.end()) {
#if ROXOR_COMPILER_DEBUG
	    printf("lvar %s\n", rb_id2name(id));
#endif
	    Value *store = new AllocaInst(RubyObjTy, "", bb);
	    new StoreInst(nilVal, store, bb);
	    lvars[id] = store;
	    has_real_lvars = true;
	}
    }
    return has_real_lvars;
}

Value *
RoxorCompiler::compile_lvar_slot(ID name)
{
    std::map<ID, Value *>::iterator iter = lvars.find(name);
    if (iter != lvars.end()) {
#if ROXOR_COMPILER_DEBUG
	printf("get_lvar %s\n", rb_id2name(name));
#endif
	return iter->second;
    }
    VALUE *var = GET_VM()->get_binding_lvar(name);
    if (var != NULL) {
#if ROXOR_COMPILER_DEBUG
	printf("get_binding_lvar %s (%p)\n", rb_id2name(name), *(void **)var);
#endif
	Value *int_val = ConstantInt::get(IntTy, (long)var);
	return new IntToPtrInst(int_val, RubyObjPtrTy, "", bb);
    }
    assert(current_block);
    Value *slot = compile_dvar_slot(name);
    assert(slot != NULL);
#if ROXOR_COMPILER_DEBUG
    printf("get_dvar %s\n", rb_id2name(name));
#endif
    return slot;
}

// VM primitives

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
rb_vm_alias_method(Class klass, Method method, ID name, int arity)
{
    IMP imp = method_getImplementation(method);
    const char *types = method_getTypeEncoding(method);

    struct RoxorFunctionIMP *func_imp = GET_VM()->method_func_imp_get(imp);
    if (func_imp == NULL) {
	rb_raise(rb_eArgError, "cannot alias non-Ruby method `%s'",
		sel_getName(method_getName(method)));
    }

    const char *name_str = rb_id2name(name);
    SEL sel;
    if (arity == 0) {
	sel = sel_registerName(name_str);
    }
    else {
	char tmp[100];
	snprintf(tmp, sizeof tmp, "%s:", name_str);
	sel = sel_registerName(tmp);
    }

    GET_VM()->add_method(klass, sel, imp, func_imp->ruby_imp,
	    func_imp->node, types);
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
	rb_vm_alias_method(klass, def_method1, name, 0);
    }
    if (def_method2 != NULL) {
	rb_vm_alias_method(klass, def_method2, name, 1);
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

Function *
RoxorCompiler::compile_objc_stub(Function *ruby_func, const char *types)
{
    char buf[100];
    const char *p = types;
    std::vector<const Type *> f_types;

    // Return value.
    p = GetFirstType(p, buf, sizeof buf);
    std::string ret_type(buf);
    const Type *f_ret_type = convert_type(buf);
    const Type *f_sret_type = NULL;
    if (GET_VM()->is_large_struct_type(f_ret_type)) {
	// We are returning a large struct, we need to pass a pointer as the
	// first argument to the structure data and return void to conform to
	// the ABI.
	f_types.push_back(PointerType::getUnqual(f_ret_type));
	f_sret_type = f_ret_type;
	f_ret_type = Type::VoidTy;
    }

    // self
    f_types.push_back(RubyObjTy);
    p = SkipFirstType(p);
    // sel
    f_types.push_back(PtrTy);
    p = SkipFirstType(p);
    // Arguments.
    std::vector<std::string> arg_types;
    for (unsigned int i = 0; i < ruby_func->arg_size() - 2; i++) {
	p = GetFirstType(p, buf, sizeof buf);
	f_types.push_back(convert_type(buf));
	arg_types.push_back(buf);
    }

    // Create the function.
    FunctionType *ft = FunctionType::get(f_ret_type, f_types, false);
    Function *f = cast<Function>(module->getOrInsertFunction("", ft));
    Function::arg_iterator arg = f->arg_begin();

    bb = BasicBlock::Create("EntryBlock", f);

    Value *sret = NULL;
    if (f_sret_type != NULL) {
	sret = arg++;
	f->addAttribute(0, Attribute::StructRet);
    }

    std::vector<Value *> params;
    params.push_back(arg++); // self
    params.push_back(arg++); // sel

    // Convert every incoming argument into Ruby type.
    for (unsigned int i = 0; i < ruby_func->arg_size() - 2; i++) {
	Value *ruby_arg = compile_conversion_to_ruby(arg_types[i].c_str(),
		f_types[i + 2], arg++);
	params.push_back(ruby_arg);
    }

    // Call the Ruby implementation.
    Value *ret_val = CallInst::Create(ruby_func, params.begin(),
	    params.end(), "", bb);

    // Convert the return value into Objective-C type (if any).
    if (f_ret_type != Type::VoidTy) {
	ret_val = compile_conversion_to_c(ret_type.c_str(), ret_val,
		new AllocaInst(f_ret_type, "", bb));
	ReturnInst::Create(ret_val, bb);
    }
    else if (sret != NULL) {
	compile_conversion_to_c(ret_type.c_str(), ret_val, sret);
	ReturnInst::Create(bb);
    }
    else {
	ReturnInst::Create(bb);
    }

    return f;
}

#if ROXOR_ULTRA_LAZY_JIT
static void
resolve_method(Class klass, SEL sel, Function *func, NODE *node, IMP imp,
	       Method m)
{
    const int oc_arity = rb_vm_node_arity(node).real + 3;

    char types[100];
    bs_element_method_t *bs_method = GET_VM()->find_bs_method(klass, sel);

    if (m == NULL || !rb_objc_get_types(Qnil, klass, sel, bs_method, types,
					sizeof types)) {
	assert((unsigned int)oc_arity < sizeof(types));
	types[0] = '@';
	types[1] = '@';
	types[2] = ':';
	for (int i = 3; i < oc_arity; i++) {
	    types[i] = '@';
	}
	types[oc_arity] = '\0';
    }
    else {
	const int m_argc = method_getNumberOfArguments(m);
	if (m_argc < oc_arity) {
	    for (int i = m_argc; i < oc_arity; i++) {
		strcat(types, "@");
	    }
	}
    }

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

    GET_VM()->add_method(klass, sel, objc_imp, imp, node, types);
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
#endif

extern "C"
void
rb_vm_prepare_method(Class klass, SEL sel, Function *func, NODE *node)
{
    if (GET_VM()->current_class != NULL) {
	klass = GET_VM()->current_class;
    }

#if ROXOR_ULTRA_LAZY_JIT

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

#else // !ROXOR_ULTRA_LAZY_JIT

    IMP imp = GET_VM()->compile(func);
    rb_vm_define_method(klass, sel, imp, node, false);

#endif
}

#define VISI(x) ((x)&NOEX_MASK)
#define VISI_CHECK(x,f) (VISI(x) == (f))

static void
push_method(VALUE ary, VALUE mod, SEL sel, NODE *node,
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
	    const int type = node->nd_body == NULL ? -1 : VISI(node->nd_noex);
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
	    NODE *node = rb_vm_get_method_node(imp);
	    if (node == NULL && !include_objc_methods) {
		continue;
	    }
	    push_method(ary, mod, sel, node, filter);
	}
	free(methods);
    }

#if ROXOR_ULTRA_LAZY_JIT
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
#endif
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

    methods = class_copyMethodList(from_class, &methods_count);
    if (methods != NULL) {
	for (i = 0; i < methods_count; i++) {
	    Method method = methods[i];

	    class_replaceMethod(to_class,
		    method_getName(method),
		    method_getImplementation(method),
		    method_getTypeEncoding(method));
	}
	free(methods);
    }

#if ROXOR_ULTRA_LAZY_JIT
    std::multimap<Class, SEL>::iterator iter =
	GET_VM()->method_source_sels.find(from_class);

    if (iter != GET_VM()->method_source_sels.end()) {
	std::multimap<Class, SEL>::iterator last =
	    GET_VM()->method_source_sels.upper_bound(from_class);
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

	    rb_vm_method_source_t *m = (rb_vm_method_source_t *)
		malloc(sizeof(rb_vm_method_source_t));
	    m->func = iter2->second->func;
	    m->node = iter2->second->node;
	    dict->insert(std::make_pair(to_class, m));
	}
    } 
#endif
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
#if ROXOR_ULTRA_LAZY_JIT
	NODE *node = NEW_CFUNC(NULL, 0);
	NODE *body = NEW_FBODY(NEW_METHOD(node, klass, noex), 0);
	rb_objc_retain(body);

	rb_vm_prepare_method(klass, sel, f, body);
#else
	IMP imp = GET_VM()->compile(f);
	NODE *node = NEW_CFUNC(imp, 0);
	NODE *body = NEW_FBODY(NEW_METHOD(node, klass, noex), 0);
	rb_objc_retain(body);

	rb_vm_define_method(klass, sel, imp, body, false);
#endif
    }

    if (write) {
	Function *f = RoxorCompiler::shared->compile_write_attr(iname);
	snprintf(buf, sizeof buf, "%s=:", name);
	SEL sel = sel_registerName(buf);
#if ROXOR_ULTRA_LAZY_JIT
	NODE *node = NEW_CFUNC(NULL, 1);
	NODE *body = NEW_FBODY(NEW_METHOD(node, klass, noex), 0);
	rb_objc_retain(body);

	rb_vm_prepare_method(klass, sel, f, body);
#else
	IMP imp = GET_VM()->compile(f);
	NODE *node = NEW_CFUNC(imp, 1);
	NODE *body = NEW_FBODY(NEW_METHOD(node, klass, noex), 0);
	rb_objc_retain(body);

	rb_vm_define_method(klass, sel, imp, body, false);
#endif
    }
}

extern "C"
void 
rb_vm_define_method(Class klass, SEL sel, IMP imp, NODE *node, bool direct)
{
    assert(klass != NULL);
    assert(node != NULL);

    const rb_vm_arity_t arity = rb_vm_node_arity(node);
    const char *sel_name = sel_getName(sel);
    const bool genuine_selector = sel_name[strlen(sel_name) - 1] == ':';

    int oc_arity = genuine_selector ? arity.real : 0;
    bool redefined = direct;
    RoxorFunctionIMP *func_imp = GET_VM()->method_func_imp_get(imp);
    IMP ruby_imp = func_imp == NULL ? imp : func_imp->ruby_imp;

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

    GET_VM()->add_method(klass, sel, imp, ruby_imp, node, types);

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
__rb_vm_rcall(VALUE self, SEL sel, NODE *node, IMP pimp,
	      const rb_vm_arity_t &arity, int argc, const VALUE *argv)
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

static /*inline*/ Method
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
	    struct RoxorFunctionIMP *func_imp =
		GET_VM()->method_func_imp_get((IMP)start);
	    if (func_imp != NULL && func_imp->ruby_imp == start) {
		start = (void *)func_imp->objc_imp;
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
    if (argc == 0 && buf[n - 1] == ':') {
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

Function *
RoxorCompiler::compile_to_rval_convertor(const char *type)
{
    // VALUE foo(void *ocval);
    Function *f = cast<Function>(module->getOrInsertFunction("",
		Type::VoidTy, PtrTy, NULL));
    Function::arg_iterator arg = f->arg_begin();
    Value *ocval = arg++;

    bb = BasicBlock::Create("EntryBlock", f);

    const Type *llvm_type = convert_type(type); 
    ocval = new BitCastInst(ocval, PointerType::getUnqual(llvm_type), "", bb);
    ocval = new LoadInst(ocval, "", bb);

    Value *rval = compile_conversion_to_ruby(type, llvm_type, ocval);

    ReturnInst::Create(rval, bb);

    return f;
}

inline void *
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

Function *
RoxorCompiler::compile_to_ocval_convertor(const char *type)
{
    // void foo(VALUE rval, void **ocval);
    Function *f = cast<Function>(module->getOrInsertFunction("",
		Type::VoidTy, RubyObjTy, PtrTy, NULL));
    Function::arg_iterator arg = f->arg_begin();
    Value *rval = arg++;
    Value *ocval = arg++;

    bb = BasicBlock::Create("EntryBlock", f);

    const Type *llvm_type = convert_type(type);
    ocval = new BitCastInst(ocval, PointerType::getUnqual(llvm_type), "", bb);
    compile_conversion_to_c(type, rval, ocval);

    ReturnInst::Create(bb);

    return f;
}

inline void *
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
helper_sel(SEL sel)
{
    const char *p = sel_getName(sel);
    size_t len = strlen(p);
    SEL new_sel = 0;
    char buf[100];

    assert(len < sizeof(buf));

    if (len >= 3 && isalpha(p[len - 3]) && p[len - 2] == '=' && p[len - 1] == ':') {
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
__rb_vm_ruby_dispatch(VALUE self, SEL sel, NODE *node, IMP imp,
		      rb_vm_arity_t &arity, int argc, const VALUE *argv)
{
    if ((argc < arity.min) || ((arity.max != -1) && (argc > arity.max))) {
	rb_raise(rb_eArgError, "wrong number of arguments (%d for %d)",
		argc, arity.min);
    }

    assert(node != NULL);
    const int node_type = nd_type(node);

    if (node_type == NODE_SCOPE && node->nd_body == NULL
	&& arity.max == arity.min) {
	// Calling an empty method, let's just return nil!
	return Qnil;
    }
    if (node_type == NODE_FBODY && arity.max != arity.min) {
	// Calling a function defined with rb_objc_define_method with
	// a negative arity, which means a different calling convention.
	if (arity.real == 2) {
	    return ((VALUE (*)(VALUE, SEL, int, const VALUE *))imp)
		(self, 0, argc, argv);
	}
	else if (arity.real == 1) {
	    return ((VALUE (*)(VALUE, SEL, ...))imp)
		(self, 0, rb_ary_new4(argc, argv));
	}
	else {
	    printf("invalid negative arity for C function %d\n",
		    arity.real);
	    abort();
	}
    }

    return __rb_vm_rcall(self, sel, node, imp, arity, argc, argv);
}

static force_inline void
fill_rcache(struct mcache *cache, Class klass, IMP imp, NODE *node,
	    const rb_vm_arity_t &arity)
{ 
    cache->flag = MCACHE_RCALL;
    rcache.klass = klass;
    rcache.imp = imp;
    rcache.node = node;
    rcache.arity = arity;
}

static force_inline void
fill_ocache(struct mcache *cache, VALUE self, Class klass, IMP imp, SEL sel,
	    int argc)
{
    cache->flag = MCACHE_OCALL;
    ocache.klass = klass;
    ocache.imp = imp;
    ocache.bs_method = GET_VM()->find_bs_method(klass, sel);

    char types[200];
    if (!rb_objc_get_types(self, klass, sel, ocache.bs_method,
		types, sizeof types)) {
	printf("cannot get encoding types for %c[%s %s]\n",
		class_isMetaClass(klass) ? '+' : '-',
		class_getName(klass),
		sel_getName(sel));
	abort();
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
	    IMP imp = method_getImplementation(method);
	    struct RoxorFunctionIMP *func_imp =
		GET_VM()->method_func_imp_get(imp);

	    if (func_imp != NULL) {
		// ruby call
		fill_rcache(cache, klass, func_imp->ruby_imp, func_imp->node,
			rb_vm_node_arity(func_imp->node));
	    }
	    else {
		// objc call
		fill_ocache(cache, self, klass, imp, sel, argc);
	    }
	}
	else {
	    // Method is not found... let's try to see if we are not given a
	    // helper selector.
	    SEL new_sel = helper_sel(sel);
	    if (new_sel != NULL) {
		Method m = class_getInstanceMethod(klass, new_sel);
		if (m != NULL) {
		    IMP imp = method_getImplementation(m);
		    if (GET_VM()->method_node_get(imp) == NULL) {
			sel = new_sel;
			method = m;
			goto recache;	
		    }
		}
	    }

	    // Then let's see if we are not trying to call a not-yet-JITed
	    // BridgeSupport function.
	    const char *selname = (const char *)sel;
	    size_t selnamelen = strlen(selname);
	    if (selname[selnamelen - 1] == ':') {
		selnamelen--;
	    }
	    std::string name(selname, selnamelen);
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
		return method_missing((VALUE)self, sel, argc, argv, opt);
	    }
	}
    }

    if (cache->flag == MCACHE_RCALL) {
	if (rcache.klass != klass) {
	    goto recache;
	}

#if ROXOR_VM_DEBUG
	printf("ruby dispatch %c[<%s %p> %s] (imp=%p, node=%p, block=%p, cached=%s)\n",
		class_isMetaClass(klass) ? '+' : '-',
		class_getName(klass),
		(void *)self,
		sel_getName(sel),
		rcache.imp,
		rcache.node,
		block,
		cached ? "true" : "false");
#endif

	bool block_already_current = GET_VM()->is_block_current(block);
	if (!block_already_current) {
	    GET_VM()->add_current_block(block);
	}

	VALUE ret = Qnil;
	try {
	    ret = __rb_vm_ruby_dispatch(self, sel, rcache.node, rcache.imp,
		    rcache.arity, argc, argv);
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
	    if (klass == (Class)rb_cCFArray) {
		return RARRAY_IMMUTABLE(self)
		    ? rb_cNSArray : rb_cNSMutableArray;
	    }
	    if (klass == (Class)rb_cCFHash) {
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

    printf("BOUH %s\n", (char *)sel);
    abort();
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

    // Let's allocate a static cache here, since a rb_vm_method_t must always
    // point to the method it was created from.
    struct mcache *c = (struct mcache *)xmalloc(sizeof(struct mcache));
    if (node == NULL) {
	fill_ocache(c, obj, oklass, imp, sel, arity);
    }
    else {
	struct RoxorFunctionIMP *func_imp =
	    GET_VM()->method_func_imp_get(imp);
	assert(func_imp != NULL);
	imp = func_imp->ruby_imp;
	fill_rcache(c, oklass, imp, node, rb_vm_node_arity(node));
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
    b->node = method->node;
    b->arity = method->node == NULL
	? rb_vm_arity(method->arity) : rb_vm_node_arity(method->node);
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
	    sel = helper_sel(sel);
	    if (sel != NULL) {
		m = class_getInstanceMethod((Class)klass, sel);
		if (m == NULL) {
		    return false;
		}
		reject_pure_ruby_methods = true;
	    }
	}
	IMP obj_imp = method_getImplementation(m);
	NODE *node = GET_VM()->method_node_get(obj_imp);

	if (node != NULL
		&& (reject_pure_ruby_methods
		    || (priv == 0 && (node->nd_noex & NOEX_PRIVATE)))) {
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
    void *exc = __cxa_allocate_exception(0);
    __cxa_throw(exc, NULL, NULL);
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
	printf("uncatched Objective-C/C++ exception...");
	return;
    }

    static SEL sel_message = 0;
    if (sel_message == 0) {
	sel_message = sel_registerName("message");
    }

    VALUE message = rb_vm_call(exc, sel_message, 0, NULL, false);

    printf("%s (%s)\n", RSTRING_PTR(message), rb_class2name(*(VALUE *)exc));
}

// END OF VM primitives

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

extern "C"
VALUE
rb_vm_run(const char *fname, NODE *node, rb_vm_binding_t *binding,
	  bool try_interpreter)
{
    if (binding != NULL) {
	GET_VM()->bindings.push_back(binding);
    }

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

// BridgeSupport implementation

static inline ID
generate_const_name(char *name)
{
    ID id;
    if (islower(name[0])) {
	name[0] = toupper(name[0]);
	id = rb_intern(name);
	name[0] = tolower(name[0]);
	return id;
    }
    else {
	return rb_intern(name);
    }
}

static VALUE bs_const_magic_cookie = Qnil;

extern "C"
VALUE
rb_vm_resolve_const_value(VALUE v, VALUE klass, ID id)
{
    void *sym;
    bs_element_constant_t *bs_const;

    if (v == bs_const_magic_cookie) {
	std::map<ID, bs_element_constant_t *>::iterator iter =
	    GET_VM()->bs_consts.find(id);
	if (iter == GET_VM()->bs_consts.end()) {
	    rb_bug("unresolved BridgeSupport constant `%s'",
		    rb_id2name(id));
	}
	bs_const = iter->second;

	sym = dlsym(RTLD_DEFAULT, bs_const->name);
	if (sym == NULL) {
	    rb_bug("cannot locate symbol for BridgeSupport constant `%s'",
		    bs_const->name);
	}

	void *convertor = GET_VM()->gen_to_rval_convertor(bs_const->type);
	v = ((VALUE (*)(void *))convertor)(sym);

	CFMutableDictionaryRef iv_dict = rb_class_ivar_dict(rb_cObject);
	assert(iv_dict != NULL);
	CFDictionarySetValue(iv_dict, (const void *)id, (const void *)v);
    }

    return v;
}

VALUE rb_cBoxed;

extern "C"
void
rb_vm_check_arity(int given, int requested)
{
    if (given != requested) {
	rb_raise(rb_eArgError, "wrong number of arguments (%d for %d)",
		given, requested);
    }
}

void
RoxorCompiler::compile_check_arity(Value *given, Value *requested)
{
    if (checkArityFunc == NULL) {
	// void rb_vm_check_arity(int given, int requested);
	checkArityFunc = cast<Function>(module->getOrInsertFunction(
		    "rb_vm_check_arity",
		    Type::VoidTy, Type::Int32Ty, Type::Int32Ty, NULL));
    }

    std::vector<Value *> params;
    params.push_back(given);
    params.push_back(requested);

    compile_protected_call(checkArityFunc, params);
}

extern "C"
void
rb_vm_set_struct(VALUE rcv, int field, VALUE val)
{
    VALUE *data;
    Data_Get_Struct(rcv, VALUE, data);
    GC_WB(&data[field], val);    
}

void
RoxorCompiler::compile_set_struct(Value *rcv, int field, Value *val)
{
    if (setStructFunc == NULL) {
	// void rb_vm_set_struct(VALUE rcv, int field, VALUE val);
	setStructFunc = cast<Function>(module->getOrInsertFunction(
		    "rb_vm_set_struct",
		    Type::VoidTy, RubyObjTy, Type::Int32Ty, RubyObjTy, NULL));
    }

    std::vector<Value *> params;
    params.push_back(rcv);
    params.push_back(ConstantInt::get(Type::Int32Ty, field));
    params.push_back(val);

    CallInst::Create(setStructFunc, params.begin(), params.end(), "", bb);
}

Function *
RoxorCompiler::compile_bs_struct_writer(rb_vm_bs_boxed_t *bs_boxed, int field)
{
    // VALUE foo(VALUE self, SEL sel, VALUE val);
    Function *f = cast<Function>(module->getOrInsertFunction("",
		RubyObjTy, RubyObjTy, PtrTy, RubyObjTy, NULL));
    Function::arg_iterator arg = f->arg_begin();
    Value *self = arg++; 	// self
    arg++;			// sel
    Value *val = arg++; 	// val

    bb = BasicBlock::Create("EntryBlock", f);

    assert((unsigned)field < bs_boxed->as.s->fields_count);
    const char *ftype = bs_boxed->as.s->fields[field].type;
    const Type *llvm_type = convert_type(ftype);

    Value *fval = new AllocaInst(llvm_type, "", bb);
    val = compile_conversion_to_c(ftype, val, fval);
    val = compile_conversion_to_ruby(ftype, llvm_type, val);

    compile_set_struct(self, field, val);

    ReturnInst::Create(val, bb);

    return f;
}

Function *
RoxorCompiler::compile_bs_struct_new(rb_vm_bs_boxed_t *bs_boxed)
{
    // VALUE foo(VALUE self, SEL sel, int argc, VALUE *argv);
    Function *f = cast<Function>(module->getOrInsertFunction("",
		RubyObjTy, RubyObjTy, PtrTy, Type::Int32Ty, RubyObjPtrTy,
		NULL));
    Function::arg_iterator arg = f->arg_begin();
    Value *klass = arg++; 	// self
    arg++;			// sel
    Value *argc = arg++; 	// argc
    Value *argv = arg++; 	// argv

    bb = BasicBlock::Create("EntryBlock", f);

    BasicBlock *no_args_bb = BasicBlock::Create("no_args", f);
    BasicBlock *args_bb  = BasicBlock::Create("args", f);
    Value *has_args = new ICmpInst(ICmpInst::ICMP_EQ, argc,
	    ConstantInt::get(Type::Int32Ty, 0), "", bb);

    BranchInst::Create(no_args_bb, args_bb, has_args, bb);

    // No arguments are given, let's create Ruby field objects based on a
    // zero-filled memory slot.
    bb = no_args_bb;
    std::vector<Value *> fields;

    for (unsigned i = 0; i < bs_boxed->as.s->fields_count; i++) {
	const char *ftype = bs_boxed->as.s->fields[i].type;
	const Type *llvm_type = convert_type(ftype);
	Value *fval = new AllocaInst(llvm_type, "", bb);

	const Type *Tys[] = { IntTy };
	Function *memset_func = Intrinsic::getDeclaration(module,
		Intrinsic::memset, Tys, 1);
	assert(memset_func != NULL);

	std::vector<Value *> params;
	params.push_back(new BitCastInst(fval, PtrTy, "", bb));
	params.push_back(ConstantInt::get(Type::Int8Ty, 0));
	params.push_back(ConstantInt::get(IntTy,
		    GET_VM()->get_sizeof(llvm_type)));
	params.push_back(ConstantInt::get(Type::Int32Ty, 0));
	CallInst::Create(memset_func, params.begin(), params.end(), "", bb);

	fval = new LoadInst(fval, "", bb);
	fval = compile_conversion_to_ruby(ftype, llvm_type, fval);

	fields.push_back(fval);
    }

    ReturnInst::Create(compile_new_struct(klass, fields), bb);

    // Arguments are given. Need to check given arity, then convert the given
    // Ruby values into the requested struct field types.
    bb = args_bb;
    fields.clear();

    compile_check_arity(argc,
	    ConstantInt::get(Type::Int32Ty, bs_boxed->as.s->fields_count));

    for (unsigned i = 0; i < bs_boxed->as.s->fields_count; i++) {
	const char *ftype = bs_boxed->as.s->fields[i].type;
	const Type *llvm_type = convert_type(ftype);
	Value *fval = new AllocaInst(llvm_type, "", bb);

	Value *index = ConstantInt::get(Type::Int32Ty, i);
	Value *arg = GetElementPtrInst::Create(argv, index, "", bb);
	arg = new LoadInst(arg, "", bb);
	arg = compile_conversion_to_c(ftype, arg, fval);
	arg = compile_conversion_to_ruby(ftype, llvm_type, arg);

	fields.push_back(arg);
    }

    ReturnInst::Create(compile_new_struct(klass, fields), bb);

    return f;
}

static ID boxed_ivar_type = 0;

static inline rb_vm_bs_boxed_t *
locate_bs_boxed(VALUE klass, const bool struct_only=false)
{
    VALUE type = rb_ivar_get(klass, boxed_ivar_type);
    assert(type != Qnil);
    rb_vm_bs_boxed_t *bs_boxed = GET_VM()->find_bs_boxed(RSTRING_PTR(type));
    assert(bs_boxed != NULL);
    if (struct_only) {
	assert(bs_boxed->is_struct());
    }
    return bs_boxed;
}

static VALUE
rb_vm_struct_fake_new(VALUE rcv, SEL sel, int argc, VALUE *argv)
{
    // Generate the real #new method.
    rb_vm_bs_boxed_t *bs_boxed = locate_bs_boxed(rcv, true);
    Function *f = RoxorCompiler::shared->compile_bs_struct_new(bs_boxed);
    IMP imp = GET_VM()->compile(f);

    // Replace the fake method with the new one in the runtime.
    rb_objc_define_method(*(VALUE *)rcv, "new", (void *)imp, -1); 

    // Call the new method.
    return ((VALUE (*)(VALUE, SEL, int, VALUE *))imp)(rcv, sel, argc, argv);
}

static VALUE
rb_vm_struct_fake_set(VALUE rcv, SEL sel, VALUE val)
{
    // Locate the given field.
    char buf[100];
    const char *selname = sel_getName(sel);
    size_t s = strlcpy(buf, selname, sizeof buf);
    if (buf[s - 1] == ':') {
	s--;
    }
    assert(buf[s - 1] == '=');
    buf[s - 1] = '\0';
    rb_vm_bs_boxed_t *bs_boxed = locate_bs_boxed(CLASS_OF(rcv), true);
    int field = -1;
    for (unsigned i = 0; i < bs_boxed->as.s->fields_count; i++) {
	const char *fname = bs_boxed->as.s->fields[i].name;
	if (strcmp(fname, buf) == 0) {
	    field = i;
	    break;
	}
    }
    assert(field != -1); 

    // Generate the new setter method.
    Function *f = RoxorCompiler::shared->compile_bs_struct_writer(
	    bs_boxed, field);
    IMP imp = GET_VM()->compile(f);

    // Replace the fake method with the new one in the runtime.
    buf[s - 1] = '=';
    buf[s] = '\0';
    rb_objc_define_method(*(VALUE *)rcv, buf, (void *)imp, 1); 

    // Call the new method.
    return ((VALUE (*)(VALUE, SEL, VALUE))imp)(rcv, sel, val);
}

// Readers are statically generated.
#include "bs_struct_readers.c"

static VALUE
rb_vm_boxed_equal(VALUE rcv, SEL sel, VALUE val)
{
    if (rcv == val) {
	return Qtrue;
    }
    VALUE klass = CLASS_OF(rcv);
    if (!rb_obj_is_kind_of(val, klass)) {
	return Qfalse;
    }

    rb_vm_bs_boxed_t *bs_boxed = locate_bs_boxed(klass);

    VALUE *rcv_data; Data_Get_Struct(rcv, VALUE, rcv_data);
    VALUE *val_data; Data_Get_Struct(val, VALUE, val_data);

    if (bs_boxed->bs_type == BS_ELEMENT_STRUCT) {
	for (unsigned i = 0; i < bs_boxed->as.s->fields_count; i++) {
	    if (!rb_equal(rcv_data[i], val_data[i])) {
		return Qfalse;
	    }
	}
	return Qtrue;
    }

    return rcv_data == val_data ? Qtrue : Qfalse;
}

static VALUE
rb_vm_struct_inspect(VALUE rcv, SEL sel)
{
    VALUE str = rb_str_new2("#<");
    rb_str_cat2(str, rb_obj_classname(rcv));

    VALUE *rcv_data;
    Data_Get_Struct(rcv, VALUE, rcv_data);
    rb_vm_bs_boxed_t *bs_boxed = locate_bs_boxed(CLASS_OF(rcv), true);
    for (unsigned i = 0; i < bs_boxed->as.s->fields_count; i++) {
	rb_str_cat2(str, " ");
	rb_str_cat2(str, bs_boxed->as.s->fields[i].name);
	rb_str_cat2(str, "=");
	rb_str_append(str, rb_inspect(rcv_data[i]));
    }

    rb_str_cat2(str, ">");

    return str;
}

static VALUE
rb_vm_struct_dup(VALUE rcv, SEL sel)
{
    VALUE klass = CLASS_OF(rcv);
    rb_vm_bs_boxed_t *bs_boxed = locate_bs_boxed(klass, true);

    VALUE *rcv_data;
    Data_Get_Struct(rcv, VALUE, rcv_data);
    VALUE *new_data = (VALUE *)xmalloc(
	    bs_boxed->as.s->fields_count * sizeof(VALUE));
    for (unsigned i = 0; i < bs_boxed->as.s->fields_count; i++) {
	VALUE field = rcv_data[i];
	// Numeric values cannot be duplicated.
	if (!rb_obj_is_kind_of(field, rb_cNumeric)) {
	    field = rb_send_dup(field);
	}
	GC_WB(&new_data[i], field);
    }

    return Data_Wrap_Struct(klass, NULL, NULL, new_data);
}

static VALUE
rb_boxed_fields(VALUE rcv, SEL sel)
{
    rb_vm_bs_boxed_t *bs_boxed = locate_bs_boxed(rcv);
    VALUE ary = rb_ary_new();
    if (bs_boxed->bs_type == BS_ELEMENT_STRUCT) {
	for (unsigned i = 0; i < bs_boxed->as.s->fields_count; i++) {
	    VALUE field = ID2SYM(rb_intern(bs_boxed->as.s->fields[i].name));
	    rb_ary_push(ary, field);
	}
    }
    return ary;
}

static VALUE
rb_vm_opaque_new(VALUE rcv, SEL sel)
{
    // XXX instead of doing this, we should perhaps simply delete the new
    // method on the class...
    rb_raise(rb_eRuntimeError, "can't allocate opaque type `%s'",
	    rb_class2name(rcv)); 
}

static bool
register_bs_boxed(bs_element_type_t type, void *value)
{
    std::string octype(((bs_element_opaque_t *)value)->type);

    std::map<std::string, rb_vm_bs_boxed_t *>::iterator iter =
	GET_VM()->bs_boxed.find(octype);

    if (iter != GET_VM()->bs_boxed.end()) {
	// A boxed class of this type already exists, so let's create an
	// alias to it.
	rb_vm_bs_boxed_t *boxed = iter->second;
	const ID name = rb_intern(((bs_element_opaque_t *)value)->name);
	rb_const_set(rb_cObject, name, boxed->klass);
	return false;
    }

    rb_vm_bs_boxed_t *boxed = (rb_vm_bs_boxed_t *)malloc(
	    sizeof(rb_vm_bs_boxed_t));

    boxed->bs_type = type;
    boxed->as.v = value;
    boxed->type = NULL; // lazy
    boxed->klass = rb_define_class(((bs_element_opaque_t *)value)->name,
	    rb_cBoxed);

    rb_ivar_set(boxed->klass, boxed_ivar_type, rb_str_new2(octype.c_str()));

    if (type == BS_ELEMENT_STRUCT) {
	// Define the fake #new method.
	rb_objc_define_method(*(VALUE *)boxed->klass, "new",
		(void *)rb_vm_struct_fake_new, -1);

	// Define accessors.
	assert(boxed->as.s->fields_count <= BS_STRUCT_MAX_FIELDS);
	for (unsigned i = 0; i < boxed->as.s->fields_count; i++) {
	    // Readers.
	    rb_objc_define_method(boxed->klass, boxed->as.s->fields[i].name,
		    (void *)struct_readers[i], 0);
	    // Writers.
	    char buf[100];
	    snprintf(buf, sizeof buf, "%s=", boxed->as.s->fields[i].name);
	    rb_objc_define_method(boxed->klass, buf,
		    (void *)rb_vm_struct_fake_set, 1);
	}

	// Define other utility methods.
	rb_objc_define_method(*(VALUE *)boxed->klass, "fields",
		(void *)rb_boxed_fields, 0);
	rb_objc_define_method(boxed->klass, "dup",
		(void *)rb_vm_struct_dup, 0);
	rb_objc_define_method(boxed->klass, "clone",
		(void *)rb_vm_struct_dup, 0);
	rb_objc_define_method(boxed->klass, "inspect",
		(void *)rb_vm_struct_inspect, 0);
    }
    else {
	// Opaque methods.
	rb_objc_define_method(*(VALUE *)boxed->klass, "new",
		(void *)rb_vm_opaque_new, -1);
    }
    // Common methods.
    rb_objc_define_method(boxed->klass, "==", (void *)rb_vm_boxed_equal, 1);

    GET_VM()->bs_boxed[octype] = boxed;

    return true;
}

static VALUE
rb_boxed_objc_type(VALUE rcv, SEL sel)
{
    return rb_ivar_get(rcv, boxed_ivar_type);
}

static VALUE
rb_boxed_is_opaque(VALUE rcv, SEL sel)
{
    rb_vm_bs_boxed_t *bs_boxed = locate_bs_boxed(rcv);
    return bs_boxed->bs_type == BS_ELEMENT_OPAQUE ? Qtrue : Qfalse;
}

VALUE rb_cPointer;

typedef struct {
    VALUE type;
    size_t type_size;
    VALUE (*convert_to_rval)(void *);
    void (*convert_to_ocval)(VALUE rval, void *);
    void *val;
} rb_vm_pointer_t;

static const char *convert_ffi_type(VALUE type,
	bool raise_exception_if_unknown);

static VALUE
rb_pointer_new(VALUE rcv, SEL sel, int argc, VALUE *argv)
{
    VALUE type, len;
    rb_scan_args(argc, argv, "11", &type, &len);
    const size_t rlen = NIL_P(len) ? 1 : FIX2LONG(len);

    StringValuePtr(type);
    const char *type_str = convert_ffi_type(type, false);

    rb_vm_pointer_t *ptr = (rb_vm_pointer_t *)xmalloc(sizeof(rb_vm_pointer_t));
    GC_WB(&ptr->type, rb_str_new2(type_str));

    ptr->convert_to_rval =
	(VALUE (*)(void *))GET_VM()->gen_to_rval_convertor(type_str);
    ptr->convert_to_ocval =
	(void (*)(VALUE, void *))GET_VM()->gen_to_ocval_convertor(type_str);

    ptr->type_size = GET_VM()->get_sizeof(type_str);
    assert(ptr->type_size > 0);
    GC_WB(&ptr->val, xmalloc(ptr->type_size * rlen));

    return Data_Wrap_Struct(rb_cPointer, NULL, NULL, ptr);
}

static void *
rb_pointer_get_data(VALUE rcv, const char *type)
{
    if (!rb_obj_is_kind_of(rcv, rb_cPointer)) {
	rb_raise(rb_eTypeError,
		"expected instance of Pointer, got `%s' (%s)",
		RSTRING_PTR(rb_inspect(rcv)),
		rb_obj_classname(rcv));
    }

    rb_vm_pointer_t *ptr;
    Data_Get_Struct(rcv, rb_vm_pointer_t, ptr);

    assert(*type == _C_PTR);
    if (strcmp(RSTRING_PTR(ptr->type), type + 1) != 0) {
	rb_raise(rb_eTypeError,
		"expected instance of Pointer of type `%s', got `%s'",
		RSTRING_PTR(ptr->type),
		type + 1);
    }

    return ptr->val;
}

#define POINTER_VAL(ptr, idx) \
    (void *)((char *)ptr->val + (FIX2INT(idx) * ptr->type_size))

static VALUE
rb_pointer_aref(VALUE rcv, SEL sel, VALUE idx)
{
    Check_Type(idx, T_FIXNUM);

    rb_vm_pointer_t *ptr;
    Data_Get_Struct(rcv, rb_vm_pointer_t, ptr);

    return ptr->convert_to_rval(POINTER_VAL(ptr, idx));
}

static VALUE
rb_pointer_aset(VALUE rcv, SEL sel, VALUE idx, VALUE val)
{
    Check_Type(idx, T_FIXNUM);

    rb_vm_pointer_t *ptr;
    Data_Get_Struct(rcv, rb_vm_pointer_t, ptr);

    ptr->convert_to_ocval(val, POINTER_VAL(ptr, idx));

    return val;
}

static VALUE
rb_pointer_assign(VALUE rcv, SEL sel, VALUE val)
{
    return rb_pointer_aset(rcv, 0, FIX2INT(0), val);
}

static VALUE
rb_pointer_type(VALUE rcv, SEL sel)
{
    rb_vm_pointer_t *ptr;
    Data_Get_Struct(rcv, rb_vm_pointer_t, ptr);

    return ptr->type;
}

extern "C"
void
Init_BridgeSupport(void)
{
    // Boxed
    rb_cBoxed = rb_define_class("Boxed", rb_cObject);
    rb_objc_define_method(*(VALUE *)rb_cBoxed, "type",
	    (void *)rb_boxed_objc_type, 0);
    rb_objc_define_method(*(VALUE *)rb_cBoxed, "opaque?",
	    (void *)rb_boxed_is_opaque, 0);
    boxed_ivar_type = rb_intern("__octype__");

    // Pointer
    rb_cPointer = rb_define_class("Pointer", rb_cObject);
    rb_objc_define_method(*(VALUE *)rb_cPointer, "new",
	    (void *)rb_pointer_new, -1);
    rb_objc_define_method(*(VALUE *)rb_cPointer, "new_with_type",
	    (void *)rb_pointer_new, -1);
    rb_objc_define_method(rb_cPointer, "[]",
	    (void *)rb_pointer_aref, 1);
    rb_objc_define_method(rb_cPointer, "[]=",
	    (void *)rb_pointer_aset, 2);
    rb_objc_define_method(rb_cPointer, "assign",
	    (void *)rb_pointer_assign, 1);
    rb_objc_define_method(rb_cPointer, "type",
	    (void *)rb_pointer_type, 0);

    bs_const_magic_cookie = rb_str_new2("bs_const_magic_cookie");
    rb_objc_retain((void *)bs_const_magic_cookie);
}

static inline void
index_bs_class_methods(const char *name,
	std::map<std::string, std::map<SEL, bs_element_method_t *> *> &map,
	bs_element_method_t *methods,
	unsigned method_count)
{
    std::map<std::string, std::map<SEL, bs_element_method_t *> *>::iterator
	iter = map.find(name);

    std::map<SEL, bs_element_method_t *> *methods_map = NULL;	
    if (iter == map.end()) {
	methods_map = new std::map<SEL, bs_element_method_t *>();
	map[name] = methods_map;
    }
    else {
	methods_map = iter->second;
    }

    for (unsigned i = 0; i < method_count; i++) {
	bs_element_method_t *m = &methods[i];
	methods_map->insert(std::make_pair(m->name, m));
    }
} 

inline bs_element_method_t *
RoxorVM::find_bs_method(Class klass, SEL sel)
{
    std::map<std::string, std::map<SEL, bs_element_method_t *> *> &map =
	class_isMetaClass(klass) ? bs_classes_class_methods
	: bs_classes_instance_methods;

    do {
	std::map<std::string,
		 std::map<SEL, bs_element_method_t *> *>::iterator iter =
		     map.find(class_getName(klass));

	if (iter != map.end()) {
	    std::map<SEL, bs_element_method_t *> *map2 = iter->second;
	    std::map<SEL, bs_element_method_t *>::iterator iter2 =
		map2->find(sel);

	    if (iter2 != map2->end()) {
		return iter2->second;
	    }
	}

	klass = class_getSuperclass(klass);
    }
    while (klass != NULL);

    return NULL;
}

inline rb_vm_bs_boxed_t *
RoxorVM::find_bs_boxed(std::string type)
{
    std::map<std::string, rb_vm_bs_boxed_t *>::iterator iter =
	bs_boxed.find(type);

    if (iter == bs_boxed.end()) {
	return NULL;
    }

    return iter->second;
}

inline rb_vm_bs_boxed_t *
RoxorVM::find_bs_struct(std::string type)
{
    rb_vm_bs_boxed_t *boxed = find_bs_boxed(type);
    return boxed == NULL ? NULL : boxed->is_struct() ? boxed : NULL;
}

inline rb_vm_bs_boxed_t *
RoxorVM::find_bs_opaque(std::string type)
{
    rb_vm_bs_boxed_t *boxed = find_bs_boxed(type);
    return boxed == NULL ? NULL : boxed->is_struct() ? NULL : boxed;
}

static inline void
register_bs_class(bs_element_class_t *bs_class)
{
    if (bs_class->class_methods_count > 0) {
	index_bs_class_methods(bs_class->name,
		GET_VM()->bs_classes_class_methods,
		bs_class->class_methods,
		bs_class->class_methods_count);
    }
    if (bs_class->instance_methods_count > 0) {
	index_bs_class_methods(bs_class->name,
		GET_VM()->bs_classes_instance_methods,
		bs_class->instance_methods,
		bs_class->instance_methods_count);
    }
}

static void
bs_parse_cb(bs_parser_t *parser, const char *path, bs_element_type_t type, 
            void *value, void *ctx)
{
    bool do_not_free = false;
    CFMutableDictionaryRef rb_cObject_dict = (CFMutableDictionaryRef)ctx;

    switch (type) {
	case BS_ELEMENT_ENUM:
	{
	    bs_element_enum_t *bs_enum = (bs_element_enum_t *)value;
	    ID name = generate_const_name(bs_enum->name);
	    if (!CFDictionaryGetValueIfPresent(rb_cObject_dict,
			(const void *)name, NULL)) {

		VALUE val = strchr(bs_enum->value, '.') != NULL
		    ? rb_float_new(rb_cstr_to_dbl(bs_enum->value, 1))
		    : rb_cstr_to_inum(bs_enum->value, 10, 1);
		CFDictionarySetValue(rb_cObject_dict, (const void *)name, 
			(const void *)val);
	    }
	    else {
		rb_warning("bs: enum `%s' already defined", rb_id2name(name));
	    }
	    break;
	}

	case BS_ELEMENT_CONSTANT:
	{
	    bs_element_constant_t *bs_const = (bs_element_constant_t *)value;
	    ID name = generate_const_name(bs_const->name);
	    if (!CFDictionaryGetValueIfPresent(rb_cObject_dict,
			(const void *)name, NULL)) {

		GET_VM()->bs_consts[name] = bs_const;
		CFDictionarySetValue(rb_cObject_dict, (const void *)name, 
			(const void *)bs_const_magic_cookie);
		do_not_free = true;
	    }
	    else {
		rb_warning("bs: constant `%s' already defined", 
			   rb_id2name(name));
	    }
	    break;
	}

	case BS_ELEMENT_STRING_CONSTANT:
	{
	    bs_element_string_constant_t *bs_strconst = 
		(bs_element_string_constant_t *)value;
	    ID name = generate_const_name(bs_strconst->name);
	    if (!CFDictionaryGetValueIfPresent(rb_cObject_dict,
			(const void *)name, NULL)) {

		VALUE val;
#if 0 // this is likely not needed anymore
	    	if (bs_strconst->nsstring) {
		    CFStringRef string = CFStringCreateWithCString(NULL,
			    bs_strconst->value, kCFStringEncodingUTF8);
		    val = (VALUE)string;
	    	}
	    	else {
#endif
		    val = rb_str_new2(bs_strconst->value);
//	    	}
		CFDictionarySetValue(rb_cObject_dict, (const void *)name, 
			(const void *)val);
	    }
	    else {
		rb_warning("bs: string constant `%s' already defined", 
			   rb_id2name(name));
	    }
	    break;
	}

	case BS_ELEMENT_FUNCTION:
	{
	    bs_element_function_t *bs_func = (bs_element_function_t *)value;
	    std::string name(bs_func->name);

	    std::map<std::string, bs_element_function_t *>::iterator iter =
		GET_VM()->bs_funcs.find(name);
	    if (iter == GET_VM()->bs_funcs.end()) {
		GET_VM()->bs_funcs[name] = bs_func;
		do_not_free = true;
	    }
	    else {
		rb_warning("bs: function `%s' already defined", bs_func->name);
	    }
	    break;
	}

	case BS_ELEMENT_FUNCTION_ALIAS:
	{
#if 0 // TODO
	    bs_element_function_alias_t *bs_func_alias = 
		(bs_element_function_alias_t *)value;
	    bs_element_function_t *bs_func_original;
	    if (st_lookup(bs_functions, 
			(st_data_t)rb_intern(bs_func_alias->original), 
			(st_data_t *)&bs_func_original)) {
		st_insert(bs_functions, 
			(st_data_t)rb_intern(bs_func_alias->name), 
			(st_data_t)bs_func_original);
	    }
	    else {
		rb_raise(rb_eRuntimeError, 
			"cannot alias '%s' to '%s' because it doesn't exist", 
			bs_func_alias->name, bs_func_alias->original);
	    }
#endif
	    break;
	}

	case BS_ELEMENT_OPAQUE:
	case BS_ELEMENT_STRUCT:
	{
	    if (register_bs_boxed(type, value)) {
		do_not_free = true;
	    }
	    else {
		rb_warning("bs: boxed `%s' already defined",
			((bs_element_opaque_t *)value)->name);
	    }
	    break;
	}

	case BS_ELEMENT_CLASS:
	{
	    bs_element_class_t *bs_class = (bs_element_class_t *)value;
	    register_bs_class(bs_class);
	    free(bs_class);
	    do_not_free = true;
	    break;
	}

	case BS_ELEMENT_INFORMAL_PROTOCOL_METHOD:
	{
#if 0
	    bs_element_informal_protocol_method_t *bs_inf_prot_method = 
		(bs_element_informal_protocol_method_t *)value;
	    struct st_table *t = bs_inf_prot_method->class_method
		? bs_inf_prot_cmethods
		: bs_inf_prot_imethods;

	    st_insert(t, (st_data_t)bs_inf_prot_method->name,
		(st_data_t)bs_inf_prot_method->type);

	    free(bs_inf_prot_method->protocol_name);
	    free(bs_inf_prot_method);
	    do_not_free = true;
#endif
	    break;
	}

	case BS_ELEMENT_CFTYPE:
	{
	    
	    bs_element_cftype_t *bs_cftype = (bs_element_cftype_t *)value;
	    std::map<std::string, bs_element_cftype_t *>::iterator
		iter = GET_VM()->bs_cftypes.find(bs_cftype->type);
	    if (iter == GET_VM()->bs_cftypes.end()) {
		GET_VM()->bs_cftypes[bs_cftype->type] = bs_cftype;
		do_not_free = true;
	    }
	    else {
		rb_warning("bs: CF type `%s' already defined",
			bs_cftype->type);
	    }
	    break;
	}
    }

    if (!do_not_free) {
	bs_element_free(type, value);
    }
}

extern "C"
void
rb_vm_load_bridge_support(const char *path, const char *framework_path,
			  int options)
{
    char *error;
    bool ok;
    CFMutableDictionaryRef rb_cObject_dict;  

    if (GET_VM()->bs_parser == NULL) {
	GET_VM()->bs_parser = bs_parser_new();
    }

    rb_cObject_dict = rb_class_ivar_dict(rb_cObject);
    assert(rb_cObject_dict != NULL);

    ok = bs_parser_parse(GET_VM()->bs_parser, path, framework_path,
			 (bs_parse_options_t)options,
			 bs_parse_cb, rb_cObject_dict, &error);
    if (!ok) {
	rb_raise(rb_eRuntimeError, "%s", error);
    }
#if 0 //TODO //MAC_OS_X_VERSION_MAX_ALLOWED <= 1060
    /* XXX we should introduce the possibility to write prelude scripts per
     * frameworks where this kind of changes could be located.
     */
#if defined(__LP64__)
    static bool R6399046_fixed = false;
    /* XXX work around for <rdar://problem/6399046> NSNotFound 64-bit value is incorrect */
    if (!R6399046_fixed) {
	ID nsnotfound = rb_intern("NSNotFound");
	VALUE val = 
	    (VALUE)CFDictionaryGetValue(rb_cObject_dict, (void *)nsnotfound);
	if ((VALUE)val == INT2FIX(-1)) {
	    CFDictionarySetValue(rb_cObject_dict, 
		    (const void *)nsnotfound,
		    (const void *)ULL2NUM(NSNotFound));
	    R6399046_fixed = true;
	    DLOG("XXX", "applied work-around for rdar://problem/6399046");
	}
    }
#endif
    static bool R6401816_fixed = false;
    /* XXX work around for <rdar://problem/6401816> -[NSObject performSelector:withObject:] has wrong sel_of_type attributes*/
    if (!R6401816_fixed) {
	bs_element_method_t *bs_method = 
	    rb_bs_find_method((Class)rb_cNSObject, 
			      @selector(performSelector:withObject:));
	if (bs_method != NULL) {
	    bs_element_arg_t *arg = bs_method->args;
	    while (arg != NULL) {
		if (arg->index == 0 
		    && arg->sel_of_type != NULL
		    && arg->sel_of_type[0] != '@') {
		    arg->sel_of_type[0] = '@';
		    R6401816_fixed = true;
		    DLOG("XXX", "applied work-around for rdar://problem/6401816");
		    break;
		}
		arg++;
	    }
	}	
    }
#endif
}

// String format

static void
get_types_for_format_str(std::string &octypes, const unsigned int len,
			 VALUE *args, const char *format_str, char **new_fmt)
{
    size_t format_str_len = strlen(format_str);
    unsigned int i = 0, j = 0;

    while (i < format_str_len) {
	bool sharp_modifier = false;
	bool star_modifier = false;
	if (format_str[i++] != '%') {
	    continue;
	}
	if (i < format_str_len && format_str[i] == '%') {
	    i++;
	    continue;
	}
	while (i < format_str_len) {
	    char type = 0;
	    switch (format_str[i]) {
		case '#':
		    sharp_modifier = true;
		    break;

		case '*':
		    star_modifier = true;
		    type = _C_INT;
		    break;

		case 'd':
		case 'i':
		case 'o':
		case 'u':
		case 'x':
		case 'X':
		    type = _C_INT;
		    break;

		case 'c':
		case 'C':
		    type = _C_CHR;
		    break;

		case 'D':
		case 'O':
		case 'U':
		    type = _C_LNG;
		    break;

		case 'f':       
		case 'F':
		case 'e':       
		case 'E':
		case 'g':       
		case 'G':
		case 'a':
		case 'A':
		    type = _C_DBL;
		    break;

		case 's':
		case 'S':
		    {
			if (i - 1 > 0) {
			    unsigned long k = i - 1;
			    while (k > 0 && format_str[k] == '0') {
				k--;
			    }
			    if (k < i && format_str[k] == '.') {
				args[j] = (VALUE)CFSTR("");
			    }
			}
			type = _C_CHARPTR;
		    }
		    break;

		case 'p':
		    type = _C_PTR;
		    break;

		case '@':
		    type = _C_ID;
		    break;

		case 'B':
		case 'b':
		    {
			VALUE arg = args[j];
			switch (TYPE(arg)) {
			    case T_STRING:
				arg = rb_str_to_inum(arg, 0, Qtrue);
				break;
			}
			arg = rb_big2str(arg, 2);
			if (sharp_modifier) {
			    VALUE prefix = format_str[i] == 'B'
				? (VALUE)CFSTR("0B") : (VALUE)CFSTR("0b");
			    rb_str_update(arg, 0, 0, prefix);
			}
			if (*new_fmt == NULL) {
			    *new_fmt = strdup(format_str);
			}
			(*new_fmt)[i] = '@';
			args[j] = arg;
			type = _C_ID;
		    }
		    break;
	    }

	    i++;

	    if (type != 0) {
		if (len == 0 || j >= len) {
		    rb_raise(rb_eArgError, 
			    "Too much tokens in the format string `%s' "\
			    "for the given %d argument(s)", format_str, len);
		}
		octypes.push_back(type);
		j++;
		if (!star_modifier) {
		    break;
		}
	    }
	}
    }
    for (; j < len; j++) {
	octypes.push_back(_C_ID);
    }
}

VALUE
rb_str_format(int argc, const VALUE *argv, VALUE fmt)
{
    if (argc == 0) {
	return fmt;
    }

    char *new_fmt = NULL;
    std::string types("@@@@");
    get_types_for_format_str(types, (unsigned int)argc, (VALUE *)argv, 
	    RSTRING_PTR(fmt), &new_fmt);

    if (new_fmt != NULL) {
	fmt = rb_str_new2(new_fmt);
    }  

    VALUE *stub_args = (VALUE *)alloca(sizeof(VALUE) * argc + 4);
    stub_args[0] = Qnil; // allocator
    stub_args[1] = Qnil; // format options
    stub_args[2] = fmt;  // format string
    for (int i = 0; i < argc; i++) {
	stub_args[3 + i] = argv[i];
    }

    rb_vm_c_stub_t *stub = (rb_vm_c_stub_t *)GET_VM()->gen_stub(types,
	    3, false);

    VALUE str = (*stub)((IMP)&CFStringCreateWithFormat, argc + 3, stub_args);
    CFMakeCollectable((void *)str);
    return str;
}

// FFI

static const char *
convert_ffi_type(VALUE type, bool raise_exception_if_unknown)
{
    const char *typestr = StringValueCStr(type);
    assert(typestr != NULL);

    // Ruby-FFI types.

    if (strcmp(typestr, "char") == 0) {
	return "c";
    }
    if (strcmp(typestr, "uchar") == 0) {
	return "C";
    }
    if (strcmp(typestr, "short") == 0) {
	return "s";
    }
    if (strcmp(typestr, "ushort") == 0) {
	return "S";
    }
    if (strcmp(typestr, "int") == 0) {
	return "i";
    }
    if (strcmp(typestr, "uint") == 0) {
	return "I";
    }
    if (strcmp(typestr, "long") == 0) {
	return "l";
    }
    if (strcmp(typestr, "ulong") == 0) {
	return "L";
    }
    if (strcmp(typestr, "long_long") == 0) {
	return "q";
    }
    if (strcmp(typestr, "ulong_long") == 0) {
	return "Q";
    }
    if (strcmp(typestr, "float") == 0) {
	return "f";
    }
    if (strcmp(typestr, "double") == 0) {
	return "d";
    }
    if (strcmp(typestr, "string") == 0) {
	return "*";
    }
    if (strcmp(typestr, "pointer") == 0) {
	return "^";
    }

    // MacRuby extensions.

    if (strcmp(typestr, "object") == 0) {
	return "@";
    }

    if (raise_exception_if_unknown) {
	rb_raise(rb_eTypeError, "unrecognized string `%s' given as FFI type",
		typestr);
    }
    return typestr;
}

Function *
RoxorCompiler::compile_ffi_function(void *stub, void *imp, int argc)
{
    // VALUE func(VALUE rcv, SEL sel, VALUE arg1, VALUE arg2, ...) {
    //     VALUE *argv = alloca(...);
    //     return stub(imp, argc, argv);
    // }
    std::vector<const Type *> f_types;
    f_types.push_back(RubyObjTy);
    f_types.push_back(PtrTy);
    for (int i = 0; i < argc; i++) {
	f_types.push_back(RubyObjTy);
    }
    FunctionType *ft = FunctionType::get(RubyObjTy, f_types, false);
    Function *f = cast<Function>(module->getOrInsertFunction("", ft));

    bb = BasicBlock::Create("EntryBlock", f);

    Function::arg_iterator arg = f->arg_begin();
    arg++; // skip self
    arg++; // skip sel

    std::vector<Value *> params;
    std::vector<const Type *> stub_types;

    // First argument is the function implementation. 
    params.push_back(compile_const_pointer(imp));
    stub_types.push_back(PtrTy);

    // Second argument is arity;
    params.push_back(ConstantInt::get(Type::Int32Ty, argc));
    stub_types.push_back(Type::Int32Ty);

    // Third is an array of arguments.
    Value *argv;
    if (argc == 0) {
	argv = new BitCastInst(compile_const_pointer(NULL), RubyObjPtrTy,
		"", bb);
    }
    else {
	argv = new AllocaInst(RubyObjTy, ConstantInt::get(Type::Int32Ty, argc),
		"", bb);
	for (int i = 0; i < argc; i++) {
	    Value *index = ConstantInt::get(Type::Int32Ty, i);
	    Value *slot = GetElementPtrInst::Create(argv, index, "", bb);
	    new StoreInst(arg++, slot, "", bb);
	}
    }
    params.push_back(argv);
    stub_types.push_back(RubyObjPtrTy);

    // Cast the given stub using the correct function signature.
    FunctionType *stub_ft = FunctionType::get(RubyObjTy, stub_types, false);
    Value *stub_val = new BitCastInst(compile_const_pointer(stub),
	    PointerType::getUnqual(stub_ft), "", bb);

    // Call the stub and return its return value.
    CallInst *stub_call = CallInst::Create(stub_val, params.begin(),
	    params.end(), "", bb); 
    ReturnInst::Create(stub_call, bb);

    return f;
}

static VALUE
rb_ffi_attach_function(VALUE rcv, SEL sel, VALUE name, VALUE args, VALUE ret)
{
    const char *symname = StringValueCStr(name);
    void *sym = dlsym(RTLD_DEFAULT, symname);
    if (sym == NULL) {
	rb_raise(rb_eArgError, "given function `%s' could not be located",
		symname);
    }

    std::string types;
    types.append(convert_ffi_type(ret, true));

    Check_Type(args, T_ARRAY);
    const int argc = RARRAY_LEN(args);
    for (int i = 0; i < argc; i++) {
	types.append(convert_ffi_type(RARRAY_AT(args, i), true));
    } 

    rb_vm_c_stub_t *stub = (rb_vm_c_stub_t *)GET_VM()->gen_stub(types, argc,
	    false);
    Function *f = RoxorCompiler::shared->compile_ffi_function((void *)stub,
	    sym, argc);
    IMP imp = GET_VM()->compile(f);

    VALUE klass = rb_singleton_class(rcv);
    rb_objc_define_method(klass, symname, (void *)imp, argc);

    return Qnil;
}

extern "C"
void
Init_FFI(void)
{
    VALUE mFFI = rb_define_module("FFI");
    VALUE mFFILib = rb_define_module_under(mFFI, "Library");
    rb_objc_define_method(mFFILib, "attach_function",
	    (void *)rb_ffi_attach_function, 3);
}

// stubs

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

#if ROXOR_ULTRA_LAZY_JIT
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
#endif

extern "C"
void 
Init_PreVM(void)
{
    llvm::ExceptionHandling = true; // required!

    RoxorCompiler::module = new llvm::Module("Roxor");
    RoxorVM::current = new RoxorVM();

    setup_builtin_stubs();

#if ROXOR_ULTRA_LAZY_JIT
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
#endif
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
    RoxorCompiler::shared = new RoxorCompiler("");

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
