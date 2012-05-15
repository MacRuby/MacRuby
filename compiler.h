/*
 * MacRuby Compiler.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2008-2011, Apple Inc. All rights reserved.
 */

#ifndef __COMPILER_H_
#define __COMPILER_H_

// For the dispatcher.
#define DISPATCH_VCALL		0x1  // no receiver, no argument
#define DISPATCH_FCALL		0x2  // no receiver, one or more arguments
#define DISPATCH_SUPER		0x4  // super call
#define DISPATCH_SPLAT		0x8  // has splat
#define SPLAT_ARG_FOLLOWS	0xdeadbeef

// For const lookup.
#define CONST_LOOKUP_LEXICAL		1
#define CONST_LOOKUP_DYNAMIC_CLASS	2
#define CONST_LOOKUP_INSIDE_EVAL	4

// For defined?
#define DEFINED_IVAR 	1
#define DEFINED_GVAR 	2
#define DEFINED_CVAR 	3
#define DEFINED_CONST	4
#define DEFINED_LCONST	5
#define DEFINED_SUPER	6
#define DEFINED_METHOD	7

#if defined(__cplusplus)

#if !defined(MACRUBY_STATIC)

class RoxorInterpreter;

class RoxorCompiler {
    friend class RoxorInterpreter;

    public:
	static llvm::Module *module;
	static RoxorCompiler *shared;

	RoxorCompiler(bool debug_mode);
	virtual ~RoxorCompiler(void) { }

	void set_fname(const char *_fname);

	Value *compile_node(NODE *node);

	virtual Function *compile_main_function(NODE *node,
		bool *can_be_interpreted);
	Function *compile_read_attr(ID name);
	Function *compile_write_attr(ID name);
	Function *compile_stub(const char *types, bool variadic, int min_argc,
		bool is_objc);
	Function *compile_long_arity_stub(int argc, bool is_block);
	Function *compile_bs_struct_new(rb_vm_bs_boxed_t *bs_boxed);
	Function *compile_bs_struct_writer(rb_vm_bs_boxed_t *bs_boxed,
		int field);
	Function *compile_ffi_function(void *stub, void *imp, int argc);
	Function *compile_to_rval_convertor(const char *type);
	Function *compile_to_ocval_convertor(const char *type);
	Function *compile_objc_stub(Function *ruby_func, IMP ruby_imp,
		const rb_vm_arity_t &arity, const char *types);
	Function *compile_block_caller(rb_vm_block_t *block);
	Function *compile_mri_stub(void *imp, const int arity);

	void inline_function_calls(Function *f);

	const Type *convert_type(const char *type);

	bool is_inside_eval(void) { return inside_eval; }
	void set_inside_eval(bool flag) { inside_eval = flag; }
	bool is_dynamic_class(void) { return dynamic_class; }
	void set_dynamic_class(bool flag) { dynamic_class = flag; }
	bool get_outer_stack_uses(void) { return outer_stack_uses; }
	void set_outer_stack_uses(bool flag) { outer_stack_uses = flag; }

	void generate_location_path(std::string &path, DILocation loc);

    protected:
#if !defined(LLVM_TOT)
	DIBuilder *debug_info;
#else
	DIFactory *debug_info;
#endif
	DICompileUnit debug_compile_unit;
	DISubprogram debug_subprogram;

	bool debug_mode;
	const char *fname;
	bool inside_eval;
	bool should_interpret;
	bool can_interpret;

	std::map<ID, Value *> lvars;
	std::vector<ID> dvars;
	std::map<ID, void *> ivars_slots_cache;
	std::map<std::string, GlobalVariable *> static_strings;
	std::map<CFHashCode, GlobalVariable *> static_ustrings;

	class MAsgnValue {
	    public:
		CallInst *ary;
	};
	std::vector<MAsgnValue> masgn_values;

#if ROXOR_COMPILER_DEBUG
	int level;
# define DEBUG_LEVEL_INC() (level++)
# define DEBUG_LEVEL_DEC() (level--)
#else
# define DEBUG_LEVEL_INC()
# define DEBUG_LEVEL_DEC()
#endif

	unsigned int current_line;
	BasicBlock *bb;
	BasicBlock *entry_bb;
	ID current_mid;
	rb_vm_arity_t current_arity;
	ID self_id;
	Value *current_self;
	bool current_block;
	bool current_block_chain;
	Value *current_var_uses;
	Value *running_block;
	Value *current_block_arg;
	BasicBlock *begin_bb;
	BasicBlock *rescue_invoke_bb;
	BasicBlock *rescue_rethrow_bb;
	BasicBlock *ensure_bb;
	PHINode *ensure_pn;
	NODE *ensure_node;
	bool current_rescue;
	NODE *current_block_node;
	Function *current_block_func;
	Function *current_non_block_func;
	GlobalVariable *current_opened_class;
	bool dynamic_class;
	BasicBlock *current_loop_begin_bb;
	BasicBlock *current_loop_body_bb;
	BasicBlock *current_loop_end_bb;
	PHINode *current_loop_exit_val;
	int return_from_block;
	int return_from_block_ids;
	bool block_declaration;
	AllocaInst *argv_buffer;
	GlobalVariable *outer_stack;
	bool outer_stack_uses;

	Function *writeBarrierFunc;
	Function *dispatchFunc;
	Function *fastPlusFunc;
	Function *fastMinusFunc;
	Function *fastMultFunc;
	Function *fastDivFunc;
	Function *fastModFunc;
	Function *fastLtFunc;
	Function *fastLeFunc;
	Function *fastGtFunc;
	Function *fastGeFunc;
	Function *fastEqFunc;
	Function *fastNeqFunc;
	Function *fastEqqFunc;
	Function *fastArefFunc;
	Function *fastAsetFunc;
	Function *fastShiftFunc;
	Function *whenSplatFunc;
	Function *prepareBlockFunc;
	Function *pushBindingFunc;
	Function *getBlockFunc;
	Function *currentBlockFunc;
	Function *currentBlockObjectFunc;
	Function *getConstFunc;
	Function *setConstFunc;
	Function *prepareMethodFunc;
	Function *singletonClassFunc;
	Function *defineClassFunc;
	Function *prepareIvarSlotFunc;
	Function *getIvarFunc;
	Function *setIvarFunc;
	Function *willChangeValueFunc;
	Function *didChangeValueFunc;
	Function *definedFunc;
	Function *undefFunc;
	Function *aliasFunc;
	Function *valiasFunc;
	Function *newHashFunc;
	Function *storeHashFunc;
	Function *toAFunc;
	Function *toAryFunc;
	Function *catArrayFunc;
	Function *dupArrayFunc;
	Function *newArrayFunc;
	Function *entryArrayFunc;
	Function *checkArrayFunc;
	Function *lengthArrayFunc;
	Function *ptrArrayFunc;
	Function *newStructFunc;
	Function *newOpaqueFunc;
	Function *newPointerFunc;
	Function *getStructFieldsFunc;
	Function *getOpaqueDataFunc;
	Function *getPointerPtrFunc;
	Function *xmallocFunc;
	Function *checkArityFunc;
	Function *setStructFunc;
	Function *newRangeFunc;
	Function *newRegexpFunc;
	Function *strInternFunc;
	Function *selToSymFunc;
	Function *keepVarsFunc;
	Function *masgnGetElemBeforeSplatFunc;
	Function *masgnGetElemAfterSplatFunc;
	Function *masgnGetSplatFunc;
	Function *newStringFunc;
	Function *newString2Func;
	Function *newString3Func;
	Function *yieldFunc;
	Function *getBrokenFunc;
	Function *blockEvalFunc;
	Function *gvarSetFunc;
	Function *gvarGetFunc;
	Function *cvarSetFunc;
	Function *cvarGetFunc;
	Function *currentExceptionFunc;
	Function *popExceptionFunc;
	Function *getBackrefNth;
	Function *getBackrefSpecial;
	Function *breakFunc;
	Function *returnFromBlockFunc;
	Function *returnedFromBlockFunc;
	Function *checkReturnFromBlockFunc;
	Function *setHasEnsureFunc;
	Function *setScopeFunc;
	Function *setCurrentClassFunc;
        Function *pushOuterFunc;
        Function *popOuterFunc;
        Function *setCurrentOuterFunc;
	Function *debugTrapFunc;
	Function *getFFStateFunc;
	Function *setFFStateFunc;
	Function *releaseOwnershipFunc;
	Function *ocvalToRvalFunc;
	Function *charToRvalFunc;
	Function *ucharToRvalFunc;
	Function *shortToRvalFunc;
	Function *ushortToRvalFunc;
	Function *intToRvalFunc;
	Function *uintToRvalFunc;
	Function *longToRvalFunc;
	Function *ulongToRvalFunc;
	Function *longLongToRvalFunc;
	Function *ulongLongToRvalFunc;
	Function *floatToRvalFunc;
	Function *doubleToRvalFunc;
	Function *selToRvalFunc;
	Function *charPtrToRvalFunc;
	Function *rvalToOcvalFunc;
	Function *rvalToBoolFunc;
	Function *rvalToCharFunc;
	Function *rvalToUcharFunc;
	Function *rvalToShortFunc;
	Function *rvalToUshortFunc;
	Function *rvalToIntFunc;
	Function *rvalToUintFunc;
	Function *rvalToLongFunc;
	Function *rvalToUlongFunc;
	Function *rvalToLongLongFunc;
	Function *rvalToUlongLongFunc;
	Function *rvalToFloatFunc;
	Function *rvalToDoubleFunc;
	Function *rvalToSelFunc;
	Function *rvalToCharPtrFunc;
	Function *initBlockFunc;
	Function *blockProcFunc;
	Function *setCurrentMRIMethodContext;

	Constant *zeroVal;
	Constant *oneVal;
	Constant *twoVal;
	Constant *threeVal;
	Constant *nilVal;
	Constant *trueVal;
	Constant *falseVal;
	Constant *undefVal;
	Constant *splatArgFollowsVal;
	Constant *defaultScope;
	Constant *publicScope;

	const Type *VoidTy;
	const Type *Int1Ty;
	const Type *Int8Ty;
	const Type *Int16Ty;
	const Type *Int32Ty;
	const Type *Int64Ty;
	const Type *FloatTy;
	const Type *DoubleTy;
	const Type *RubyObjTy; 
	const PointerType *RubyObjPtrTy;
	const PointerType *RubyObjPtrPtrTy;
	const PointerType *PtrTy;
	const PointerType *PtrPtrTy;
	const Type *IntTy;
	const PointerType *Int32PtrTy;
	const Type *BitTy;
	const Type *BlockLiteralTy;

	unsigned dbg_mdkind;

	void compile_node_error(const char *msg, NODE *node);

	Function *
	get_function(const char *name) {
	    Function *f = module->getFunction(name);
	    if (f == NULL) {
		printf("function %s cannot be found!\n", name);
		abort();
	    }
	    return f;
	}

	virtual Constant *
	compile_const_pointer(void *ptr, const PointerType *type=NULL) {
	    if (type == NULL) {
		type = PtrTy;
	    }
	    if (ptr == NULL) {
		return ConstantPointerNull::get(type);
	    }
	    else {
		Constant *ptrint = ConstantInt::get(IntTy, (long)ptr);
		return ConstantExpr::getIntToPtr(ptrint, type);
	    }
	}

	Constant *
	compile_const_pointer_to_pointer(void *ptr) {
	    return compile_const_pointer(ptr, PtrPtrTy);
	}

	bool should_inline_function(Function *f);

	Value *compile_node0(NODE *node);
	Function *compile_scope(NODE *node);
	Value *compile_call(NODE *node, bool use_tco=true);
	Value *compile_yield(NODE *node);
	Instruction *compile_protected_call(Value *imp, Value **args_begin,
		Value **args_end);
	Instruction *compile_protected_call(Value *imp, std::vector<Value *>
		&params);
	Value *compile_argv_buffer(const long argc);
	Value *recompile_dispatch_argv(std::vector<Value *> &params, int idx);
	void compile_dispatch_arguments(NODE *args,
		std::vector<Value *> &arguments, int *pargc);
	Function::ArgumentListType::iterator compile_optional_arguments(
		Function::ArgumentListType::iterator iter, NODE *node);
	void compile_boolean_test(Value *condVal, BasicBlock *ifTrueBB,
		BasicBlock *ifFalseBB);
	void compile_when_arguments(NODE *args, Value *comparedToVal,
		BasicBlock *thenBB);
	void compile_single_when_argument(NODE *arg, Value *comparedToVal,
		BasicBlock *thenBB);
	void compile_method_definition(NODE *node);
	virtual void compile_prepare_method(Value *classVal, Value *sel,
		bool singleton, Function *new_function, rb_vm_arity_t &arity,
		NODE *body);
	Value *compile_dispatch_call(std::vector<Value *> &params);
	Value *compile_when_splat(Value *comparedToVal, Value *splatVal);
	Value *compile_attribute_assign(NODE *node, Value *extra_val);
	virtual Value *compile_prepare_block_args(Function *func, int *flags);
	Value *compile_prepare_block(void);
	Value *compile_block(NODE *node);
	Value *compile_block_get(Value *block_object);
	Value *compile_binding(void);
	Value *compile_optimized_dispatch_call(SEL sel, int argc,
		std::vector<Value *> &params);
	Value *compile_lvar_assignment(ID vid, Value *val);
	Value *compile_dvar_assignment(ID vid, Value *val);
	Value *compile_lvar_get(ID vid);
	Value *compile_ivar_assignment(ID vid, Value *val);
	Value *compile_ivar_get(ID vid);
	Value *compile_cvar_assignment(ID vid, Value *val);
	Value *compile_cvar_get(ID vid, bool check);
	Value *compile_gvar_assignment(NODE *node, Value *val);
	Value *compile_gvar_get(NODE *node);
	Value *compile_constant_declaration(NODE *node, Value *val);
	Value *compile_multiple_assignment(NODE *node);
	Value *compile_multiple_assignment(NODE *node, Value *val);
	void compile_multiple_assignment_element(NODE *node, Value *val);
	Value *compile_current_class(void);
	virtual Value *compile_nsobject(void);
	virtual Value *compile_standarderror(void);
	Value *compile_class_path(NODE *node, int *flags);
	Value *compile_const(ID id, Value *outer);
	Value *compile_singleton_class(Value *obj);
	Value *compile_defined_expression(NODE *node);
	Value *compile_dstr(NODE *node);
	Value *compile_dvar_slot(ID name);
	void compile_break_val(Value *val);
	void compile_break_within_loop(Value *val);
	void compile_break_within_block(Value *val);
	void compile_simple_return(Value *val);
	Value *compile_set_has_ensure(Value *val);
	void compile_return_from_block(Value *val, int id);
	void compile_return_from_block_handler(int id);
	Value *compile_jump(NODE *node);
	virtual Value *compile_ccache(ID id);
	virtual Value *compile_sel(SEL sel, bool add_to_bb=true) {
	    return compile_const_pointer(sel, PtrTy);
	}
	virtual Value *compile_id(ID id);
	Instruction *compile_const_global_string(const char *str,
		const size_t str_len);
	Instruction *compile_const_global_string(const char *str) {
	    return compile_const_global_string(str, strlen(str));
	}
	Instruction *compile_const_global_ustring(const UniChar *str,
		const size_t str_len);

	Value *compile_arity(rb_vm_arity_t &arity);
	Instruction *compile_range(Value *beg, Value *end, bool exclude_end,
		bool retain=false);
	Value *compile_literal(VALUE val);
	virtual Value *compile_immutable_literal(VALUE val);
	virtual Value *compile_global_entry(NODE *node);

	void compile_set_current_scope(Value *klass, Value *scope);
	Value *compile_set_current_class(Value *klass);
	Value *compile_push_outer(Value *klass);
	void compile_pop_outer(bool need_release = false);
	Value *compile_outer_stack(void);
	Value *compile_set_current_outer(void);

	Value *compile_landing_pad_header(void);
	void compile_landing_pad_footer(bool pop_exception=true);
	Value *compile_current_exception(void);
	void compile_rethrow_exception(void);
	void compile_pop_exception(int pos=0);
	Value *compile_lvar_slot(ID name);
	Value *compile_lvar_slot(ID name, bool *need_wb);
	bool compile_lvars(ID *tbl);
	Value *compile_new_struct(Value *klass, std::vector<Value *> &fields);
	Value *compile_new_opaque(Value *klass, Value *val);
	Value *compile_new_pointer(const char *type, Value *val);
	void compile_get_struct_fields(Value *val, Value *buf,
		rb_vm_bs_boxed_t *bs_boxed);
	Value *compile_get_opaque_data(Value *val, rb_vm_bs_boxed_t *bs_boxed,
		Value *slot);
	Value *compile_get_cptr(Value *val, const char *type, Value *slot);
	void compile_check_arity(Value *given, Value *requested);
	void compile_set_struct(Value *rcv, int field, Value *val);
	Value *compile_xmalloc(size_t len);

	Value *compile_lambda_to_funcptr(const char *type, Value *val,
		Value *slot, bool is_block);
	Value *compile_conversion_to_c(const char *type, Value *val,
		Value *slot);
	Value *compile_conversion_to_ruby(const char *type,
		const Type *llvm_type, Value *val);
	void compile_debug_trap(void);

	virtual Value *compile_slot_cache(ID id);
	void compile_keep_vars(BasicBlock *startBB, BasicBlock *mergeBB);

	SEL mid_to_sel(ID mid, int arity);

	Value *compile_get_ffstate(GlobalVariable *ffstate);
	Value *compile_set_ffstate(Value *val, Value *expected,
		GlobalVariable *ffstate, BasicBlock *mergeBB, Function *f);
	Value *compile_ff2(NODE *current_node);
	Value *compile_ff3(NODE *current_node);

	void attach_current_line_metadata(Instruction *insn);
};

#define context (RoxorCompiler::module->getContext())

class RoxorAOTCompiler : public RoxorCompiler {
    public:
	RoxorAOTCompiler(void);

	Function *compile_main_function(NODE *node, bool *can_be_interpreted);

	// BridgeSupport metadata needed for AOT compilation.
	std::map<SEL, std::vector<std::string> *> bs_c_stubs_types,
	    bs_objc_stubs_types;

	void load_bs_full_file(const char *path);

    private:
	std::map<ID, GlobalVariable *> ccaches;
	std::map<SEL, GlobalVariable *> sels;
	std::map<ID, GlobalVariable *> ids;
	std::map<ID, GlobalVariable *> global_entries;
	std::vector<GlobalVariable *> ivar_slots;
	std::map<VALUE, GlobalVariable *> literals;
	std::vector<std::string> c_stubs, objc_stubs;

	GlobalVariable *cObject_gvar;
	GlobalVariable *cStandardError_gvar;
	std::vector<GlobalVariable *> class_gvars;

	Function *compile_init_function(void);
	Value *compile_ccache(ID id);
	Value *compile_sel(SEL sel, bool add_to_bb=true);
	void compile_prepare_method(Value *classVal, Value *sel,
		bool singleton, Function *new_function, rb_vm_arity_t &arity,
		NODE *body);
	Value *compile_prepare_block_args(Function *func, int *flags);
	Value *compile_nsobject(void);
	Value *compile_standarderror(void);
	Value *compile_id(ID id);
	Value *compile_immutable_literal(VALUE val);
	Value *compile_global_entry(NODE *node);
	Value *compile_slot_cache(ID id);

	Constant *
	compile_const_pointer(void *ptr, const PointerType *type=NULL) {
	    if (ptr == NULL) {
		return RoxorCompiler::compile_const_pointer(ptr, type);
	    }
	    printf("compile_const_pointer() called with a non-NULL pointer " \
		   "on the AOT compiler - leaving the ship!\n");
	    abort();
	}
};

#endif // !MACRUBY_STATIC

#endif /* __cplusplus */

#endif /* __COMPILER_H_ */
