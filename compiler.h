/*
 * MacRuby compiler.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 * 
 * Copyright (C) 2008-2009, Apple Inc. All rights reserved.
 */

#ifndef __COMPILER_H_
#define __COMPILER_H_

#if defined(__cplusplus)

#define ROXOR_COMPILER_DEBUG 	0

// For the dispatcher.
#define DISPATCH_VCALL 		1
#define DISPATCH_SUPER 		2
#define SPLAT_ARG_FOLLOWS 	0xdeadbeef

// For defined?
#define DEFINED_IVAR 	1
#define DEFINED_GVAR 	2
#define DEFINED_CVAR 	3
#define DEFINED_CONST	4
#define DEFINED_LCONST	5
#define DEFINED_SUPER	6
#define DEFINED_METHOD	7

class RoxorCompiler {
    public:
	static llvm::Module *module;
	static RoxorCompiler *shared;

	RoxorCompiler(const char *fname);
	virtual ~RoxorCompiler(void) { }

	Value *compile_node(NODE *node);

	virtual Function *compile_main_function(NODE *node);
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

    protected:
	const char *fname;

	std::map<ID, Value *> lvars;
	std::vector<ID> dvars;
	std::map<ID, int *> ivar_slots_cache;
	std::map<std::string, GlobalVariable *> static_strings;

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

	void compile_node_error(const char *msg, NODE *node);

	virtual Instruction *
	compile_const_pointer(void *ptr, bool insert_to_bb=true) {
	    Value *ptrint = ConstantInt::get(IntTy, (long)ptr);
	    return insert_to_bb
		? new IntToPtrInst(ptrint, PtrTy, "", bb)
		: new IntToPtrInst(ptrint, PtrTy, "");
	}

	Instruction *
        compile_const_pointer_to_pointer(void *ptr, bool insert_to_bb=true) {
	    Value *ptrint = ConstantInt::get(IntTy, (long)ptr);
	    return insert_to_bb
		? new IntToPtrInst(ptrint, PtrPtrTy, "", bb)
		: new IntToPtrInst(ptrint, PtrPtrTy, "");
	}

	Value *compile_protected_call(Function *func,
		std::vector<Value *> &params);
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
	virtual void compile_prepare_method(Value *classVal, Value *sel,
		Function *new_function, rb_vm_arity_t &arity, NODE *body);
	Value *compile_dispatch_call(std::vector<Value *> &params);
	Value *compile_when_splat(Value *comparedToVal, Value *splatVal);
	Value *compile_fast_eqq_call(Value *selfVal, Value *comparedToVal);
	Value *compile_attribute_assign(NODE *node, Value *extra_val);
	Value *compile_block_create(NODE *node=NULL);
	Value *compile_binding(void);
	Value *compile_optimized_dispatch_call(SEL sel, int argc,
		std::vector<Value *> &params);
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
	virtual Value *compile_mcache(SEL sel, bool super);
	virtual Value *compile_ccache(ID id);
	virtual Instruction *compile_sel(SEL sel, bool add_to_bb=true) {
	    return compile_const_pointer(sel, add_to_bb);
	}
	GlobalVariable *compile_const_global_string(const char *str);

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

class RoxorAOTCompiler : public RoxorCompiler {
    public:
	RoxorAOTCompiler(const char *fname) : RoxorCompiler(fname) { }

	Function *compile_main_function(NODE *node);

    private:
	std::map<SEL, GlobalVariable *> mcaches;
	std::map<ID, GlobalVariable *> ccaches;
	std::map<SEL, GlobalVariable *> sels;

	Value *compile_mcache(SEL sel, bool super);
	Value *compile_ccache(ID id);
	Instruction *compile_sel(SEL sel, bool add_to_bb=true);
	void compile_prepare_method(Value *classVal, Value *sel,
		Function *new_function, rb_vm_arity_t &arity, NODE *body);

	Instruction *
        compile_const_pointer(void *ptr, bool insert_to_bb=true) {
	    if (ptr == NULL) {
		return RoxorCompiler::compile_const_pointer(ptr, insert_to_bb);
	    }
	    printf("compile_const_pointer() called with a non-NULL pointer " \
		   "on the AOT compiler - leaving the ship!\n");
	    abort();
	}
};

#endif /* __cplusplus */

#endif /* __COMPILER_H_ */
