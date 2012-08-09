/*
 * MacRuby Compiler.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2008-2011, Apple Inc. All rights reserved.
 */

#if !defined(MACRUBY_STATIC)

#define ROXOR_COMPILER_DEBUG 	0

#if !defined(DW_LANG_Ruby)
# define DW_LANG_Ruby 0x15 // TODO: Python is 0x14, request a real number
#endif

#include <llvm/LLVMContext.h>
#include <llvm/Transforms/Utils/Cloning.h>

#include "llvm.h"
#include "macruby_internal.h"
#include "ruby/encoding.h"
#include "ruby/node.h"
#include "id.h"
#include "vm.h"
#include "compiler.h"
#include "objc.h"
#include "version.h"
#include "encoding.h"
#include "re.h"
#include "bs.h"
#include "class.h"

extern "C" const char *ruby_node_name(int node);

// Will be set later, in vm.cpp.
llvm::Module *RoxorCompiler::module = NULL;
RoxorCompiler *RoxorCompiler::shared = NULL;

#define __save_state(type, var) \
    type __old__##var = var

#define __restore_state(var) \
    var = __old__##var

#define save_compiler_state() \
    __save_state(unsigned int, current_line);\
    __save_state(BasicBlock *, bb);\
    __save_state(BasicBlock *, entry_bb);\
    __save_state(ID, current_mid);\
    __save_state(rb_vm_arity_t, current_arity);\
    __save_state(ID, self_id);\
    __save_state(Value *, current_self);\
    __save_state(bool, current_block);\
    __save_state(bool, current_block_chain);\
    __save_state(Value *, current_var_uses);\
    __save_state(Value *, running_block);\
    __save_state(Value *, current_block_arg);\
    __save_state(BasicBlock *, begin_bb);\
    __save_state(BasicBlock *, rescue_invoke_bb);\
    __save_state(BasicBlock *, rescue_rethrow_bb);\
    __save_state(BasicBlock *, ensure_bb);\
    __save_state(bool, current_rescue);\
    __save_state(NODE *, current_block_node);\
    __save_state(Function *, current_block_func);\
    __save_state(Function *, current_non_block_func);\
    __save_state(GlobalVariable *, current_opened_class);\
    __save_state(bool, dynamic_class);\
    __save_state(BasicBlock *, current_loop_begin_bb);\
    __save_state(BasicBlock *, current_loop_body_bb);\
    __save_state(BasicBlock *, current_loop_end_bb);\
    __save_state(PHINode *, current_loop_exit_val);\
    __save_state(int, return_from_block);\
    __save_state(int, return_from_block_ids);\
    __save_state(PHINode *, ensure_pn);\
    __save_state(NODE *, ensure_node);\
    __save_state(bool, block_declaration);\
    __save_state(AllocaInst *, argv_buffer);\
    __save_state(GlobalVariable *, outer_stack);

#define restore_compiler_state() \
    __restore_state(current_line);\
    __restore_state(bb);\
    __restore_state(entry_bb);\
    __restore_state(current_mid);\
    __restore_state(current_arity);\
    __restore_state(self_id);\
    __restore_state(current_self);\
    __restore_state(current_block);\
    __restore_state(current_block_chain);\
    __restore_state(current_var_uses);\
    __restore_state(running_block);\
    __restore_state(current_block_arg);\
    __restore_state(begin_bb);\
    __restore_state(rescue_invoke_bb);\
    __restore_state(rescue_rethrow_bb);\
    __restore_state(ensure_bb);\
    __restore_state(current_rescue);\
    __restore_state(current_block_node);\
    __restore_state(current_block_func);\
    __restore_state(current_non_block_func);\
    __restore_state(current_opened_class);\
    __restore_state(dynamic_class);\
    __restore_state(current_loop_begin_bb);\
    __restore_state(current_loop_body_bb);\
    __restore_state(current_loop_end_bb);\
    __restore_state(current_loop_exit_val);\
    __restore_state(return_from_block);\
    __restore_state(return_from_block_ids);\
    __restore_state(ensure_pn);\
    __restore_state(ensure_node);\
    __restore_state(block_declaration);\
    __restore_state(argv_buffer);\
    __restore_state(outer_stack);

#define reset_compiler_state() \
    bb = NULL;\
    entry_bb = NULL;\
    begin_bb = NULL;\
    rescue_invoke_bb = NULL;\
    rescue_rethrow_bb = NULL;\
    ensure_bb = NULL;\
    current_mid = 0;\
    current_arity = rb_vm_arity(-1);\
    self_id = rb_intern("self");\
    current_self = NULL;\
    current_var_uses = NULL;\
    running_block = NULL;\
    current_block_arg = NULL;\
    current_block = false;\
    current_block_chain = false;\
    current_block_node = NULL;\
    current_block_func = NULL;\
    current_non_block_func = NULL;\
    current_opened_class = NULL;\
    dynamic_class = false;\
    current_loop_begin_bb = NULL;\
    current_loop_body_bb = NULL;\
    current_loop_end_bb = NULL;\
    current_loop_exit_val = NULL;\
    current_rescue = false;\
    return_from_block = -1;\
    return_from_block_ids = 0;\
    ensure_pn = NULL;\
    ensure_node = NULL;\
    block_declaration = false;\
    argv_buffer = NULL;\
    outer_stack = NULL;

RoxorCompiler::RoxorCompiler(bool _debug_mode)
{
    assert(RoxorCompiler::module != NULL);
#if !defined(LLVM_TOT)
    debug_info = new DIBuilder(*RoxorCompiler::module);
#else
    debug_info = new DIFactory(*RoxorCompiler::module);
#endif

    can_interpret = false;
    debug_mode = _debug_mode;
    fname = "";
    inside_eval = false;
    current_line = 0;
    outer_stack_uses = false;

    reset_compiler_state();

    writeBarrierFunc = get_function("vm_gc_wb");
    dispatchFunc = get_function("vm_dispatch");
    fastPlusFunc = get_function("vm_fast_plus");
    fastMinusFunc = get_function("vm_fast_minus");
    fastMultFunc = get_function("vm_fast_mult");
    fastDivFunc = get_function("vm_fast_div");
    fastModFunc = get_function("vm_fast_mod");
    fastLtFunc = get_function("vm_fast_lt");
    fastLeFunc = get_function("vm_fast_le");
    fastGtFunc = get_function("vm_fast_gt");
    fastGeFunc = get_function("vm_fast_ge");
    fastEqFunc = get_function("vm_fast_eq");
    fastNeqFunc = get_function("vm_fast_neq");
    fastEqqFunc = get_function("vm_fast_eqq");
    fastArefFunc = get_function("vm_fast_aref");
    fastAsetFunc = get_function("vm_fast_aset");
    fastShiftFunc = get_function("vm_fast_shift");
    whenSplatFunc = get_function("vm_when_splat");
    prepareBlockFunc = NULL;
    pushBindingFunc = NULL;
    getBlockFunc = get_function("vm_get_block");
    currentBlockObjectFunc = NULL;
    currentBlockFunc = NULL;
    getConstFunc = get_function("vm_get_const");
    setConstFunc = get_function("vm_set_const");
    prepareMethodFunc = NULL;
    singletonClassFunc = NULL;
    defineClassFunc = NULL;
    getIvarFunc = get_function("vm_ivar_get");
    setIvarFunc = get_function("vm_ivar_set");
    willChangeValueFunc = NULL;
    didChangeValueFunc = NULL;
    definedFunc = NULL;
    undefFunc = NULL;
    aliasFunc = NULL;
    valiasFunc = NULL;
    newHashFunc = get_function("vm_rhash_new");
    storeHashFunc = get_function("vm_rhash_store");
    toAFunc = get_function("vm_to_a");
    toAryFunc = get_function("vm_to_ary");
    catArrayFunc = get_function("vm_ary_cat");
    dupArrayFunc = get_function("vm_ary_dup");
    newArrayFunc = get_function("vm_rary_new");
    entryArrayFunc = get_function("vm_ary_entry");
    checkArrayFunc = get_function("vm_ary_check");
    lengthArrayFunc = get_function("vm_ary_length");
    ptrArrayFunc = get_function("vm_ary_ptr");
    newStructFunc = NULL;
    newOpaqueFunc = NULL;
    newPointerFunc = NULL;
    getStructFieldsFunc = NULL;
    getOpaqueDataFunc = NULL;
    getPointerPtrFunc = get_function("vm_rval_to_cptr");
    xmallocFunc = NULL;
    checkArityFunc = NULL;
    setStructFunc = NULL;
    newRangeFunc = NULL;
    newRegexpFunc = NULL;
    strInternFunc = NULL;
    selToSymFunc = NULL;
    keepVarsFunc = NULL;
    masgnGetElemBeforeSplatFunc =
	get_function("vm_masgn_get_elem_before_splat");
    masgnGetElemAfterSplatFunc = get_function("vm_masgn_get_elem_after_splat");
    masgnGetSplatFunc = get_function("vm_masgn_get_splat");
    newStringFunc = NULL;
    newString2Func = NULL;
    newString3Func = NULL;
    yieldFunc = get_function("vm_yield_args");
    getBrokenFunc = get_function("vm_get_broken_value");
    blockEvalFunc = NULL;
    gvarSetFunc = NULL;
    gvarGetFunc = NULL;
    cvarSetFunc = get_function("vm_cvar_set");
    cvarGetFunc = get_function("vm_cvar_get");
    currentExceptionFunc = NULL;
    popExceptionFunc = NULL;
    getBackrefNth = NULL;
    getBackrefSpecial = NULL;
    breakFunc = NULL;
    returnFromBlockFunc = NULL;
    returnedFromBlockFunc = get_function("vm_returned_from_block");
    checkReturnFromBlockFunc = NULL;
    setHasEnsureFunc = NULL;
    setScopeFunc = get_function("vm_set_current_scope");
    setCurrentClassFunc = NULL;
    pushOuterFunc = NULL;
    popOuterFunc = NULL;
    setCurrentOuterFunc = NULL;
    debugTrapFunc = NULL;
    getFFStateFunc = NULL;
    setFFStateFunc = NULL;
    releaseOwnershipFunc = get_function("vm_release_ownership");
    ocvalToRvalFunc = get_function("vm_ocval_to_rval");
    charToRvalFunc = get_function("vm_char_to_rval");
    ucharToRvalFunc = get_function("vm_uchar_to_rval");
    shortToRvalFunc = get_function("vm_short_to_rval");
    ushortToRvalFunc = get_function("vm_ushort_to_rval");
    intToRvalFunc = get_function("vm_int_to_rval");
    uintToRvalFunc = get_function("vm_uint_to_rval");
    longToRvalFunc = get_function("vm_long_to_rval");
    ulongToRvalFunc = get_function("vm_ulong_to_rval");
    longLongToRvalFunc = get_function("vm_long_long_to_rval");
    ulongLongToRvalFunc = get_function("vm_ulong_long_to_rval");
    floatToRvalFunc = get_function("vm_float_to_rval");
    doubleToRvalFunc = get_function("vm_double_to_rval");
    selToRvalFunc = get_function("vm_sel_to_rval");
    charPtrToRvalFunc = get_function("vm_charptr_to_rval");
    rvalToOcvalFunc = get_function("vm_rval_to_ocval");
    rvalToBoolFunc = get_function("vm_rval_to_bool");
    rvalToCharFunc = get_function("vm_rval_to_char");
    rvalToUcharFunc = get_function("vm_rval_to_uchar");
    rvalToShortFunc = get_function("vm_rval_to_short");
    rvalToUshortFunc = get_function("vm_rval_to_ushort");
    rvalToIntFunc = get_function("vm_rval_to_int");
    rvalToUintFunc = get_function("vm_rval_to_uint");
    rvalToLongFunc = get_function("vm_rval_to_long");
    rvalToUlongFunc = get_function("vm_rval_to_ulong");
    rvalToLongLongFunc = get_function("vm_rval_to_long_long");
    rvalToUlongLongFunc = get_function("vm_rval_to_ulong_long");
    rvalToFloatFunc = get_function("vm_rval_to_float");
    rvalToDoubleFunc = get_function("vm_rval_to_double");
    rvalToSelFunc = get_function("vm_rval_to_sel");
    rvalToCharPtrFunc = get_function("vm_rval_to_charptr");
    initBlockFunc = get_function("vm_init_c_block");
    blockProcFunc = get_function("vm_ruby_block_literal_proc");
    setCurrentMRIMethodContext = NULL;

    VoidTy = Type::getVoidTy(context);
    Int1Ty = Type::getInt1Ty(context);
    Int8Ty = Type::getInt8Ty(context);
    Int16Ty = Type::getInt16Ty(context);
    Int32Ty = Type::getInt32Ty(context);
    Int64Ty = Type::getInt64Ty(context);
    FloatTy = Type::getFloatTy(context);
    DoubleTy = Type::getDoubleTy(context);

#if __LP64__
    RubyObjTy = IntTy = Int64Ty;
#else
    RubyObjTy = IntTy = Int32Ty;
#endif

    zeroVal = ConstantInt::get(IntTy, 0);
    oneVal = ConstantInt::get(IntTy, 1);
    twoVal = ConstantInt::get(IntTy, 2);
    threeVal = ConstantInt::get(IntTy, 3);

    defaultScope = ConstantInt::get(Int32Ty, SCOPE_DEFAULT);
    publicScope = ConstantInt::get(Int32Ty, SCOPE_PUBLIC);

    RubyObjPtrTy = PointerType::getUnqual(RubyObjTy);
    RubyObjPtrPtrTy = PointerType::getUnqual(RubyObjPtrTy);
    nilVal = ConstantInt::get(RubyObjTy, Qnil);
    trueVal = ConstantInt::get(RubyObjTy, Qtrue);
    falseVal = ConstantInt::get(RubyObjTy, Qfalse);
    undefVal = ConstantInt::get(RubyObjTy, Qundef);
    splatArgFollowsVal = ConstantInt::get(RubyObjTy, SPLAT_ARG_FOLLOWS);
    PtrTy = PointerType::getUnqual(Int8Ty);
    PtrPtrTy = PointerType::getUnqual(PtrTy);
    Int32PtrTy = PointerType::getUnqual(Int32Ty);
    BitTy = Type::getInt1Ty(context);

    BlockLiteralTy = module->getTypeByName("struct.ruby_block_literal");
    assert(BlockLiteralTy != NULL);

#if ROXOR_COMPILER_DEBUG
    level = 0;
#endif

#if LLVM_TOT
    dbg_mdkind = context.getMDKindID("dbg");
    assert(dbg_mdkind != 0);
#else
    dbg_mdkind = LLVMContext::MD_dbg;
#endif
}

RoxorAOTCompiler::RoxorAOTCompiler(void)
: RoxorCompiler(false)
{
    cObject_gvar = NULL;
    cStandardError_gvar = NULL;
}

SEL
RoxorCompiler::mid_to_sel(ID mid, int arity)
{
    SEL sel = rb_vm_id_to_sel(mid, arity);
    if (rb_objc_ignored_sel(sel)) {
	char buf[100];
	snprintf(buf, sizeof buf, "__hidden__%s", rb_id2name(mid));
	sel = sel_registerName(buf);
    }
    return sel;
}

Instruction *
RoxorCompiler::compile_protected_call(Value *imp, Value **args_begin,
	Value **args_end)
{
    if (rescue_invoke_bb == NULL) {
	return CallInst::Create(imp, args_begin, args_end, "", bb);
    }
    else {
	BasicBlock *normal_bb = BasicBlock::Create(context, "normal",
		bb->getParent());
	InvokeInst *dispatch = InvokeInst::Create(imp, normal_bb,
		rescue_invoke_bb, args_begin, args_end, "", bb);
	bb = normal_bb;
	return dispatch;
    }
}

Instruction *
RoxorCompiler::compile_protected_call(Value *imp, std::vector<Value *> &params)
{
    if (rescue_invoke_bb == NULL) {
	return CallInst::Create(imp, params.begin(), params.end(), "", bb);
    }
    else {
	BasicBlock *normal_bb = BasicBlock::Create(context, "normal",
		bb->getParent());
	InvokeInst *dispatch = InvokeInst::Create(imp, normal_bb,
		rescue_invoke_bb, params.begin(), params.end(), "", bb);
	bb = normal_bb;
	return dispatch;
    }
}

void
RoxorCompiler::compile_single_when_argument(NODE *arg, Value *comparedToVal,
	BasicBlock *thenBB)
{
    Value *subnodeVal = compile_node(arg);
    Value *condVal;
    if (comparedToVal != NULL) {
	std::vector<Value *> params;
	params.push_back(current_self);
	params.push_back(subnodeVal);
	params.push_back(compile_sel(selEqq));
	params.push_back(compile_const_pointer(NULL));
	params.push_back(ConstantInt::get(Int8Ty, 0));
	params.push_back(ConstantInt::get(Int32Ty, 1));
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
    BasicBlock *nextTestBB = BasicBlock::Create(context, "next_test", f);

    compile_boolean_test(condVal, thenBB, nextTestBB);

    bb = nextTestBB;
}

void
RoxorCompiler::compile_boolean_test(Value *condVal, BasicBlock *ifTrueBB,
	BasicBlock *ifFalseBB)
{
    Function *f = bb->getParent();
    BasicBlock *notFalseBB = BasicBlock::Create(context, "not_false", f);

    Value *notFalseCond = new ICmpInst(*bb, ICmpInst::ICMP_NE, condVal,
	    falseVal);
    BranchInst::Create(notFalseBB, ifFalseBB, notFalseCond, bb);
    Value *notNilCond = new ICmpInst(*notFalseBB, ICmpInst::ICMP_NE, condVal,
	    nilVal);
    BranchInst::Create(ifTrueBB, ifFalseBB, notNilCond, notFalseBB);
}

void
RoxorCompiler::compile_when_arguments(NODE *args, Value *comparedToVal,
	BasicBlock *thenBB)
{
    switch (nd_type(args)) {
	case NODE_ARRAY:
	    while (args != NULL) {
		compile_single_when_argument(args->nd_head, comparedToVal,
			thenBB);
		args = args->nd_next;
	    }
	    break;

	case NODE_SPLAT:
	    {
		Value *condVal = compile_when_splat(comparedToVal,
			compile_node(args->nd_head));

		BasicBlock *nextTestBB = BasicBlock::Create(context,
			"next_test", bb->getParent());
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
RoxorCompiler::compile_optional_arguments(
	Function::ArgumentListType::iterator iter, NODE *node)
{
    assert(nd_type(node) == NODE_OPT_ARG);

    do {
	assert(node->nd_value != NULL);

	Value *isUndefInst = new ICmpInst(*bb, ICmpInst::ICMP_EQ, iter,
		undefVal);

	Function *f = bb->getParent();
	BasicBlock *arg_undef = BasicBlock::Create(context, "arg_undef", f);
	BasicBlock *next_bb = BasicBlock::Create(context, "", f);

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
RoxorCompiler::compile_dispatch_arguments(NODE *args,
	std::vector<Value *> &arguments, int *pargc)
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
RoxorCompiler::compile_when_splat(Value *comparedToVal, Value *splatVal)
{
    if (comparedToVal == NULL) {
	return splatVal;
    }
    GlobalVariable *is_redefined = GET_CORE()->redefined_op_gvar(selEqq, true);
    Value *args[] = {
	new LoadInst(is_redefined, "", bb),
	comparedToVal,
	splatVal
    };
    return compile_protected_call(whenSplatFunc, args, args + 3);
}

Instruction *
RoxorCompiler::compile_const_global_ustring(const UniChar *str,
	const size_t len)
{
    assert(len > 0);

    const unsigned long hash = rb_str_hash_uchars(str, len);

    std::map<CFHashCode, GlobalVariable *>::iterator iter =
	static_ustrings.find(hash);

    GlobalVariable *gvar;
    if (iter == static_ustrings.end()) {
	const ArrayType *str_type = ArrayType::get(Int16Ty, len);

	std::vector<Constant *> ary_elements;
	for (unsigned int i = 0; i < len; i++) {
	    ary_elements.push_back(ConstantInt::get(Int16Ty, str[i]));
	}

	gvar = new GlobalVariable(*RoxorCompiler::module, str_type, true,
		GlobalValue::InternalLinkage,
		ConstantArray::get(str_type, ary_elements), "");

	static_ustrings[hash] = gvar;
    }
    else {
	gvar = iter->second;
    }

    Value *idxs[] = {
	ConstantInt::get(Int32Ty, 0),
	ConstantInt::get(Int32Ty, 0)
    };
    return GetElementPtrInst::Create(gvar, idxs, idxs + 2, "", bb);
}

Instruction *
RoxorCompiler::compile_const_global_string(const char *str,
	const size_t len)
{
    assert(len > 0);

    std::string s(str, len);
    std::map<std::string, GlobalVariable *>::iterator iter =
	static_strings.find(s);

    GlobalVariable *gvar;
    if (iter == static_strings.end()) {
	const ArrayType *str_type = ArrayType::get(Int8Ty, len + 1);

	std::vector<Constant *> ary_elements;
	for (unsigned int i = 0; i < len; i++) {
	    ary_elements.push_back(ConstantInt::get(Int8Ty, str[i]));
	}
	ary_elements.push_back(ConstantInt::get(Int8Ty, 0));
	
	gvar = new GlobalVariable(*RoxorCompiler::module, str_type, true,
		GlobalValue::InternalLinkage,
		ConstantArray::get(str_type, ary_elements), "");

	static_strings[s] = gvar;
    }
    else {
	gvar = iter->second;
    }

    Value *idxs[] = {
	ConstantInt::get(Int32Ty, 0),
	ConstantInt::get(Int32Ty, 0)
    };
    return GetElementPtrInst::Create(gvar, idxs, idxs + 2, "", bb);
}

Value *
RoxorCompiler::compile_ccache(ID name)
{
    struct ccache *cache = GET_CORE()->constant_cache_get(name);
    return compile_const_pointer(cache);
}

Value *
RoxorAOTCompiler::compile_ccache(ID name)
{
    std::map<ID, GlobalVariable *>::iterator iter =
	ccaches.find(name);
    GlobalVariable *gvar;
    if (iter == ccaches.end()) {
	gvar = new GlobalVariable(*RoxorCompiler::module, PtrTy, false,
		GlobalValue::InternalLinkage, Constant::getNullValue(PtrTy),
		"");
	assert(gvar != NULL);
	ccaches[name] = gvar;
    }
    else {
	gvar = iter->second;
    }
    return new LoadInst(gvar, "", bb);
}

static void
discover_stubs(std::map<SEL, std::vector<std::string> *> &map,
	std::vector<std::string> &dest, SEL sel)
{
    std::map<SEL, std::vector<std::string> *>::iterator iter;
    iter = map.find(sel);
    if (iter != map.end()) {
	std::vector<std::string> *v = iter->second;
	for (std::vector<std::string>::iterator i = v->begin();
		i != v->end(); ++i) {
	    std::string s = *i;
	    if (std::find(dest.begin(), dest.end(), s) == dest.end()) {
		dest.push_back(s);
	    }		
	}
    }
}

Value *
RoxorAOTCompiler::compile_sel(SEL sel, bool add_to_bb)
{
    std::map<SEL, GlobalVariable *>::iterator iter = sels.find(sel);
    GlobalVariable *gvar;
    if (iter == sels.end()) {
	gvar = new GlobalVariable(*RoxorCompiler::module, PtrTy, false,
		GlobalValue::InternalLinkage, Constant::getNullValue(PtrTy),
		"");
	assert(gvar != NULL);
	sels[sel] = gvar;

	discover_stubs(bs_c_stubs_types, c_stubs, sel);
	discover_stubs(bs_objc_stubs_types, objc_stubs, sel);
    }
    else {
	gvar = iter->second;
    }
    return add_to_bb
	? new LoadInst(gvar, "", bb)
	: new LoadInst(gvar, "");
}

Value *
RoxorCompiler::compile_arity(rb_vm_arity_t &arity)
{
    uint64_t v;
    assert(sizeof(uint64_t) == sizeof(rb_vm_arity_t));
    memcpy(&v, &arity, sizeof(rb_vm_arity_t));
    return ConstantInt::get(Int64Ty, v);
}

void
RoxorCompiler::compile_method_definition(NODE *node)
{
    ID mid = node->nd_mid;
    assert(mid > 0);

    NODE *body = node->nd_defn;
    assert(body != NULL);

    const bool singleton_method = nd_type(node) == NODE_DEFS;

    const ID old_current_mid = current_mid;
    current_mid = mid;
    const bool old_current_block_chain = current_block_chain;
    current_block_chain = false;
    const bool old_block_declaration = block_declaration;
    block_declaration = false;
    const bool old_should_interpret = should_interpret;

    DEBUG_LEVEL_INC();
    Value *val = compile_node(body);
    assert(Function::classof(val));
    Function *func = cast<Function>(val);
    DEBUG_LEVEL_DEC();

    should_interpret = old_should_interpret;
    block_declaration = old_block_declaration;
    current_block_chain = old_current_block_chain;
    current_mid = old_current_mid;

    Value *classVal;
    if (singleton_method) {
	assert(node->nd_recv != NULL);
	classVal = compile_singleton_class(compile_node(node->nd_recv));
    }
    else {
	classVal = compile_current_class();
    }

    rb_vm_arity_t arity = rb_vm_node_arity(body);
    const SEL sel = mid_to_sel(mid, arity.real);

    compile_prepare_method(classVal, compile_sel(sel), singleton_method,
	    func, arity, body);

    can_interpret = true;
}

void
RoxorCompiler::compile_prepare_method(Value *classVal, Value *sel,
	bool singleton, Function *new_function, rb_vm_arity_t &arity,
	NODE *body)
{
    if (prepareMethodFunc == NULL) {
	// void rb_vm_prepare_method(Class klass, unsigned char dynamic_class,
	//	SEL sel, Function *func, rb_vm_arity_t arity, int flags)
	prepareMethodFunc = 
	    cast<Function>(module->getOrInsertFunction(
			"rb_vm_prepare_method",
			VoidTy, RubyObjTy, Int8Ty, PtrTy, PtrTy, Int64Ty,
			Int32Ty, NULL));
    }

    Value *args[] = {
	classVal,
	ConstantInt::get(Int8Ty, !singleton && dynamic_class ? 1 : 0),
	sel,
	compile_const_pointer(new_function),
	compile_arity(arity),
	ConstantInt::get(Int32Ty, rb_vm_node_flags(body))
    }; 
    CallInst::Create(prepareMethodFunc, args, args + 6, "", bb);
}

void
RoxorAOTCompiler::compile_prepare_method(Value *classVal, Value *sel,
	bool singleton, Function *func, rb_vm_arity_t &arity, NODE *body)
{
    if (prepareMethodFunc == NULL) {
	// void rb_vm_prepare_method2(Class klass, unsigned char dynamic_class,
	//	SEL sel, IMP ruby_imp, rb_vm_arity_t arity, int flags, ...)
	std::vector<const Type *> types;
	types.push_back(RubyObjTy);
	types.push_back(Int8Ty);
	types.push_back(PtrTy);
	types.push_back(PtrTy);
	types.push_back(Int64Ty);
	types.push_back(Int32Ty);
	FunctionType *ft = FunctionType::get(VoidTy, types, true);
	prepareMethodFunc = cast<Function>(module->getOrInsertFunction(
		    "rb_vm_prepare_method2", ft));
    }

    const unsigned char dyn_class = !singleton && dynamic_class ? 1 : 0;

    std::vector<Value *> params;
    params.push_back(classVal);
    params.push_back(ConstantInt::get(Int8Ty, dyn_class));
    params.push_back(sel);
    params.push_back(new BitCastInst(func, PtrTy, "", bb));
    params.push_back(compile_arity(arity));
    params.push_back(ConstantInt::get(Int32Ty, rb_vm_node_flags(body)));

    // Pre-compile a generic Objective-C stub, where all arguments and return
    // value are Ruby types.
    char types[100];
    types[0] = '@';
    types[1] = '@';
    types[2] = ':';
    assert(arity.real < (int)sizeof(types) - 3);
    for (int i = 0; i < arity.real; i++) {
	types[3 + i] = '@';
    }
    types[arity.real + 3] = '\0';
    params.push_back(compile_const_global_string(types));
    Function *stub = compile_objc_stub(func, NULL, arity, types);
    params.push_back(new BitCastInst(stub, PtrTy, "", bb));
    params.push_back(compile_const_pointer(NULL));

    CallInst::Create(prepareMethodFunc, params.begin(), params.end(), "", bb);
}

void
RoxorCompiler::attach_current_line_metadata(Instruction *insn)
{
    if (fname != NULL) {
#if !defined(LLVM_TOT)
	Value *args[] = {
	    ConstantInt::get(Int32Ty, current_line),
	    ConstantInt::get(Int32Ty, 0),
	    debug_compile_unit,
	    DILocation(NULL) 
	};
	DILocation loc = DILocation(MDNode::get(context, args, 4));
#else
	DILocation loc = debug_info->CreateLocation(current_line, 0,
		debug_compile_unit, DILocation(NULL));
#endif
	insn->setMetadata(dbg_mdkind, loc);
    }
}

void
RoxorCompiler::generate_location_path(std::string &path, DILocation loc)
{
    if (loc.getFilename() == "-e") {
	path.append(loc.getFilename());
    }
    else {
	path.append(loc.getDirectory());
	path.append("/");
	path.append(loc.getFilename());
    }
}

Value *
RoxorCompiler::compile_argv_buffer(const long argc)
{
    if (argc == 0) {
	return compile_const_pointer(NULL, RubyObjPtrTy);
    }
    assert(argc > 0);

    Instruction *first = bb->getParent()->getEntryBlock().getFirstNonPHI();
    if (argv_buffer == NULL) {
	argv_buffer = new AllocaInst(RubyObjTy,
		ConstantInt::get(Int32Ty, argc), "argv", first);
    }
    else {
	Value *size = argv_buffer->getArraySize();
	if (argc > cast<ConstantInt>(size)->getSExtValue()) {
	    AllocaInst *new_argv = new AllocaInst(RubyObjTy,
		    ConstantInt::get(Int32Ty, argc), "argv", first);
	    argv_buffer->replaceAllUsesWith(new_argv);
	    argv_buffer->eraseFromParent();
	    argv_buffer = new_argv;
	}
    }
    return argv_buffer;
}

Value *
RoxorCompiler::recompile_dispatch_argv(std::vector<Value *> &params, int offset)
{
    const long argc = params.size() - offset;
    Value *argv = compile_argv_buffer(argc);

    for (int i = 0; i < argc; i++) {
	Value *idx = ConstantInt::get(Int32Ty, i);
	Value *slot = GetElementPtrInst::Create(argv, idx, "", bb);
	new StoreInst(params[offset + i], slot, bb);
    }
    return argv;
}

Value *
RoxorCompiler::compile_dispatch_call(std::vector<Value *> &params)
{
    Value *argv = recompile_dispatch_argv(params, 6);
    params.erase(params.begin() + 6, params.end());
    params.push_back(argv);

    Instruction *insn = compile_protected_call(dispatchFunc, params);
    attach_current_line_metadata(insn);
    return insn;
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
    params.push_back(current_self);
    params.push_back(recv);
    params.push_back(compile_sel(sel));
    params.push_back(compile_const_pointer(NULL));
    unsigned char opt = 0;
    if (recv == current_self) {
	opt = DISPATCH_FCALL;
    }
    if (argc < (int)args.size()) {
	opt |= DISPATCH_SPLAT;
    }
    params.push_back(ConstantInt::get(Int8Ty, opt));
    params.push_back(ConstantInt::get(Int32Ty, argc));
    for (std::vector<Value *>::iterator i = args.begin();
	 i != args.end();
	 ++i) {
	params.push_back(*i);
    }

    // The return value of these assignments is always the new value.
    Value *retval = params.back();

    if (compile_optimized_dispatch_call(sel, argc, params) == NULL) {
	compile_dispatch_call(params);
    }
    return retval;
}

void
RoxorCompiler::compile_multiple_assignment_element(NODE *node, Value *val)
{
    switch (nd_type(node)) {
	case NODE_LASGN:
	case NODE_DASGN:
	case NODE_DASGN_CURR:
	    new StoreInst(val, compile_lvar_slot(node->nd_vid), bb);
	    break;

	case NODE_IASGN:
	case NODE_IASGN2:
	    compile_ivar_assignment(node->nd_vid, val);
	    break;

	case NODE_CVASGN:
	    compile_cvar_assignment(node->nd_vid, val);
	    break;

	case NODE_GASGN:
	    compile_gvar_assignment(node, val);
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

    NODE *before_splat = node->nd_head, *after_splat = NULL, *splat = NULL;

    assert((before_splat == NULL) || (nd_type(before_splat) == NODE_ARRAY));

    // if the splat has no name (a, *, b = 1, 2, 3), its node value is -1
    if ((node->nd_next == (NODE *)-1) || (node->nd_next == NULL)
	    || (nd_type(node->nd_next) != NODE_POSTARG)) {
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

    val = CallInst::Create(toAryFunc, val, "", bb);

    NODE *l = before_splat;
    for (int i = 0; l != NULL; ++i) {
	Value *args[] = {
	    val,
	    ConstantInt::get(Int32Ty, i)
	};
	Value *elt = CallInst::Create(masgnGetElemBeforeSplatFunc,
		args, args + 2, "", bb);

	compile_multiple_assignment_element(l->nd_head, elt);

	l = l->nd_next;
    }

    if (splat != NULL && splat != (NODE *)-1) {
	Value *args[] = {
	    val,
	    ConstantInt::get(Int32Ty, before_splat_count),
	    ConstantInt::get(Int32Ty, after_splat_count)
	};
	Value *elt = CallInst::Create(masgnGetSplatFunc, args, args + 3,
		"", bb);

	compile_multiple_assignment_element(splat, elt);
    }

    l = after_splat;
    for (int i = 0; l != NULL; ++i) {
	Value *args[] = {
	    val,
	    ConstantInt::get(Int32Ty, before_splat_count),
	    ConstantInt::get(Int32Ty, after_splat_count),
	    ConstantInt::get(Int32Ty, i)
	};
	Value *elt = CallInst::Create(masgnGetElemAfterSplatFunc,
		args, args + 4, "", bb);

	compile_multiple_assignment_element(l->nd_head, elt);

	l = l->nd_next;
    }

    return val;
}

Value *
RoxorCompiler::compile_multiple_assignment(NODE *node)
{
    assert(node->nd_value != NULL);

    if (node->nd_next == NULL
	    && node->nd_head != NULL
	    && nd_type(node->nd_head) == NODE_ARRAY
	    && nd_type(node->nd_value) == NODE_ARRAY
	    && node->nd_head->nd_alen == node->nd_value->nd_alen) {

	// Symetric multiple-assignment optimization.
	// Grab all left operands in separate Allocas.
	Instruction *first = bb->getParent()->getEntryBlock().getFirstNonPHI();
	std::vector<Value *> tmp_rights;
	NODE *right = node->nd_value;
	for (int i = 0; i < node->nd_head->nd_alen; i++) {
	    Value *slot = new AllocaInst(RubyObjTy, "", first);
	    assert(right->nd_head != NULL);
	    new StoreInst(compile_node(right->nd_head), slot, bb);
	    tmp_rights.push_back(new LoadInst(slot, "", bb));
	    right = right->nd_next;
	}

	// Compile assignments.
	NODE *left = node->nd_head;
	for (std::vector<Value *>::iterator i = tmp_rights.begin();
		i != tmp_rights.end(); ++i) {
	    assert(left->nd_head != NULL);
	    compile_multiple_assignment_element(left->nd_head, *i);
	    left = left->nd_next;
	}

	// Compile return value (a new Array) which is eliminated later if
	// never used.
	const int argc = tmp_rights.size();
	Value *argv = compile_argv_buffer(argc);
	for (int i = 0; i < argc; i++) {
	    Value *idx = ConstantInt::get(Int32Ty, i);
	    Value *slot = GetElementPtrInst::Create(argv, idx, "", bb);
	    new StoreInst(tmp_rights[i], slot, bb);
	}
	Value *args[] = {
	    ConstantInt::get(Int32Ty, argc),
	    argv
	};
	CallInst *ary = CallInst::Create(newArrayFunc, args, args + 2, "", bb);
	RoxorCompiler::MAsgnValue val;
	val.ary = ary;
	masgn_values.push_back(val);
	return ary;
    }
    return compile_multiple_assignment(node, compile_node(node->nd_value));
}

Value *
RoxorCompiler::compile_prepare_block_args(Function *func, int *flags)
{
    return compile_const_pointer(func);    
}

Value *
RoxorAOTCompiler::compile_prepare_block_args(Function *func, int *flags)
{
    *flags |= VM_BLOCK_AOT;
    return new BitCastInst(func, PtrTy, "", bb);
}

Value *
RoxorCompiler::compile_block_get(Value *block_object)
{
    Value *args[] = { block_object };
    return compile_protected_call(getBlockFunc, args, args + 1);
}

Value *
RoxorCompiler::compile_prepare_block(void)
{
    assert(current_block_func != NULL && current_block_node != NULL);

    if (prepareBlockFunc == NULL) {
	// void *rb_vm_prepare_block(Function *func, int flags, VALUE self,
	//	rb_vm_arity_t arity,
	//	rb_vm_var_uses **parent_var_uses,
	//	rb_vm_block_t *parent_block,
	//	int dvars_size, ...);
	std::vector<const Type *> types;
	types.push_back(PtrTy);
	types.push_back(Int32Ty);
	types.push_back(RubyObjTy);
	types.push_back(Int64Ty);
	types.push_back(PtrPtrTy);
	types.push_back(PtrTy);
	types.push_back(Int32Ty);
	FunctionType *ft = FunctionType::get(PtrTy, types, true);
	prepareBlockFunc = cast<Function>
	    (module->getOrInsertFunction("rb_vm_prepare_block", ft));
    }

    std::vector<Value *> params;
    int flags = 0;
    params.push_back(compile_prepare_block_args(current_block_func, &flags));
    if (nd_type(current_block_node) == NODE_SCOPE
	    && current_block_node->nd_body == NULL) {
	flags |= VM_BLOCK_EMPTY;
    }
    params.push_back(ConstantInt::get(Int32Ty, flags));
    params.push_back(current_self);
    rb_vm_arity_t arity = rb_vm_node_arity(current_block_node);
    params.push_back(compile_arity(arity));
    params.push_back(current_var_uses == NULL
	    ? compile_const_pointer_to_pointer(NULL) : current_var_uses);
    params.push_back(running_block == NULL
	    ? compile_const_pointer(NULL) : running_block);

    // Dvars.
    params.push_back(ConstantInt::get(Int32Ty, (int)dvars.size()));
    for (std::vector<ID>::iterator iter = dvars.begin();
	 iter != dvars.end(); ++iter) {
	params.push_back(compile_lvar_slot(*iter));
    }

    // Lvars.
    params.push_back(ConstantInt::get(Int32Ty, (int)lvars.size()));
    for (std::map<ID, Value *>::iterator iter = lvars.begin();
	 iter != lvars.end(); ++iter) {
	ID name = iter->first;
	Value *slot = iter->second;
	params.push_back(compile_id((long)name));
	params.push_back(slot);
    }

    return CallInst::Create(prepareBlockFunc, params.begin(), params.end(),
	    "", bb);
}

Value *
RoxorCompiler::compile_block(NODE *node)
{
    std::vector<ID> old_dvars = dvars;

    BasicBlock *old_current_loop_begin_bb = current_loop_begin_bb;
    BasicBlock *old_current_loop_body_bb = current_loop_body_bb;
    BasicBlock *old_current_loop_end_bb = current_loop_end_bb;
    current_loop_begin_bb = current_loop_end_bb = NULL;
    Function *old_current_block_func = current_block_func;
    NODE *old_current_block_node = current_block_node;
    bool old_current_block = current_block;
    bool old_current_block_chain = current_block_chain;
    int old_return_from_block = return_from_block;
    bool old_dynamic_class = dynamic_class;
    bool old_block_declaration = block_declaration;

    current_block = true;
    current_block_chain = true;
    dynamic_class = true;
    block_declaration = true;

    assert(node->nd_body != NULL);
    Value *block = compile_node(node->nd_body);	
    assert(Function::classof(block));

    block_declaration = old_block_declaration;
    dynamic_class = old_dynamic_class;

    BasicBlock *return_from_block_bb = NULL;
    if (!old_current_block_chain && return_from_block != -1) {
	// The block we just compiled contains one or more return expressions!
	// We need to enclose further dispatcher calls inside an exception
	// handler, since return-from-block may use a C++ exception.
	Function *f = bb->getParent();
	rescue_invoke_bb = return_from_block_bb =
	    BasicBlock::Create(context, "return-from-block", f);
    }

    current_loop_begin_bb = old_current_loop_begin_bb;
    current_loop_body_bb = old_current_loop_body_bb;
    current_loop_end_bb = old_current_loop_end_bb;
    current_block = old_current_block;
    current_block_chain = old_current_block_chain;

    current_block_func = cast<Function>(block);
    current_block_node = node->nd_body;

    const int node_type = nd_type(node);
    const bool is_lambda = node_type == NODE_LAMBDA;
    Value *caller;

    if (!is_lambda) {
	assert(node->nd_iter != NULL);
    }

    if (node_type == NODE_ITER) {
	caller = compile_node(node->nd_iter);
    }
    else {
	// Dispatch #each on the receiver.
	std::vector<Value *> params;

	params.push_back(current_self);

	if (!is_lambda) {
	    // The block must not be passed to the code that generates the
	    // values we loop on.
	    current_block_func = NULL;
	    current_block_node = NULL;
	    params.push_back(compile_node(node->nd_iter));
	    current_block_func = cast<Function>(block);
	    current_block_node = node->nd_body;
	}
	else {
	    params.push_back(current_self);
	}

	params.push_back(compile_sel((is_lambda ? selLambda : selEach)));
	params.push_back(compile_prepare_block());
	int opt = 0;
	if (is_lambda) {
	    opt = DISPATCH_FCALL;
	}
	params.push_back(ConstantInt::get(Int8Ty, opt));
	params.push_back(ConstantInt::get(Int32Ty, 0));

	caller = compile_dispatch_call(params);
    }

    const int block_id = return_from_block_bb != NULL
	? return_from_block : -1;

    Value *retval_block = CallInst::Create(returnedFromBlockFunc,
	    ConstantInt::get(Int32Ty, block_id), "", bb);

    Value *is_returned = new ICmpInst(*bb, ICmpInst::ICMP_NE, retval_block,
	    undefVal);

    Function *f = bb->getParent();
    BasicBlock *return_bb = BasicBlock::Create(context, "", f);
    BasicBlock *next_bb = BasicBlock::Create(context, "", f);

    BranchInst::Create(return_bb, next_bb, is_returned, bb);

    bb = return_bb;
    ReturnInst::Create(context, retval_block, bb);

    bb = next_bb;

    if (return_from_block_bb != NULL) {
	BasicBlock *old_bb = bb;
	bb = return_from_block_bb;
	compile_return_from_block_handler(return_from_block);
	bb = old_bb;
	return_from_block = old_return_from_block;
    }

    current_block_func = old_current_block_func;
    current_block_node = old_current_block_node;
    dvars = old_dvars;

    return caller;
}

Value *
RoxorCompiler::compile_binding(void)
{
    if (pushBindingFunc == NULL) {
	// void rb_vm_push_binding(VALUE self, rb_vm_block_t *current_block,
	// 	rb_vm_binding_t *top_binding, unsigned char dynamic_class,
	//      rb_vm_outer_t *outer_stack, rb_vm_var_uses **parent_var_uses,
	// 	int lvars_size, ...);
	std::vector<const Type *> types;
	types.push_back(RubyObjTy);
	types.push_back(PtrTy);
	types.push_back(PtrTy);
	types.push_back(Int8Ty);
	types.push_back(PtrTy);
	types.push_back(PtrPtrTy);
	types.push_back(Int32Ty);
	FunctionType *ft = FunctionType::get(VoidTy, types, true);
	pushBindingFunc = cast<Function>
	    (module->getOrInsertFunction("rb_vm_push_binding", ft));
    }

    std::vector<Value *> params;
    params.push_back(current_self);
    params.push_back(running_block == NULL
	    ? compile_const_pointer(NULL) : running_block);
    params.push_back(compile_const_pointer(rb_vm_current_binding()));
    params.push_back(ConstantInt::get(Int8Ty, dynamic_class ? 1 : 0));
    params.push_back(compile_outer_stack());
    if (current_var_uses == NULL) {
	// there is no local variables in this scope
	params.push_back(compile_const_pointer_to_pointer(NULL));
    }
    else {
	params.push_back(current_var_uses);
    }

    // Lvars.
    params.push_back(ConstantInt::get(Int32Ty, (int)lvars.size()));
    for (std::map<ID, Value *>::iterator iter = lvars.begin();
	 iter != lvars.end(); ++iter) {

	ID lvar = iter->first;
	params.push_back(compile_id(lvar));
	params.push_back(iter->second);
    }

    return CallInst::Create(pushBindingFunc, params.begin(), params.end(),
	    "", bb);
}

Value *
RoxorCompiler::compile_slot_cache(ID id)
{
    std::map<ID, void *>::iterator iter = ivars_slots_cache.find(id);
    void *cache = NULL;
    if (iter == ivars_slots_cache.end()) {
	cache = rb_vm_ivar_slot_allocate();
	ivars_slots_cache[id] = cache;
    }
    else {
	cache = iter->second;
    }
    return compile_const_pointer(cache);
}

Value *
RoxorAOTCompiler::compile_slot_cache(ID id)
{
    std::map<ID, void *>::iterator iter = ivars_slots_cache.find(id);
    GlobalVariable *gvar = NULL;
    if (iter == ivars_slots_cache.end()) {
	gvar = new GlobalVariable(*RoxorCompiler::module,
		PtrTy, false, GlobalValue::InternalLinkage,
		compile_const_pointer(NULL), "");
	ivar_slots.push_back(gvar);
	ivars_slots_cache[id] = gvar;
    }
    else {
	gvar = (GlobalVariable *)iter->second;
    }
    return new LoadInst(gvar, "", bb);
}

Value *
RoxorCompiler::compile_ivar_get(ID vid)
{
    Value *args[] = {
	current_self,
	compile_id(vid),
	compile_slot_cache(vid)
    };
    return CallInst::Create(getIvarFunc, args, args + 3, "", bb);
}

Value *
RoxorCompiler::compile_ivar_assignment(ID vid, Value *val)
{
    Value *args[] = {
	current_self,
	compile_id(vid),
	val,
	compile_slot_cache(vid)
    };
    CallInst::Create(setIvarFunc, args, args + 4, "", bb);

    return val;
}

Value *
RoxorCompiler::compile_cvar_get(ID id, bool check)
{
    Value *args[] = {
	compile_current_class(),
	compile_id(id),
	ConstantInt::get(Int8Ty, check ? 1 : 0),
	ConstantInt::get(Int8Ty, dynamic_class ? 1 : 0)
    };
    return compile_protected_call(cvarGetFunc, args, args + 4);
}

Value *
RoxorCompiler::compile_cvar_assignment(ID name, Value *val)
{
    Value *args[] = {
	compile_current_class(),
	compile_id(name),
	val,
	ConstantInt::get(Int8Ty, dynamic_class ? 1 : 0)
    };
    return CallInst::Create(cvarSetFunc, args, args + 4, "", bb);
}

Value *
RoxorCompiler::compile_gvar_assignment(NODE *node, Value *val)
{
    assert(node->nd_vid > 0);
    assert(node->nd_entry != NULL);

    if (gvarSetFunc == NULL) {
	// VALUE rb_gvar_set(struct global_entry *entry, VALUE val);
	gvarSetFunc = cast<Function>(module->getOrInsertFunction(
		    "rb_gvar_set",
		    RubyObjTy, PtrTy, RubyObjTy, NULL));
    }

    Value *args[] = {
	compile_global_entry(node),
	val
    };
    return compile_protected_call(gvarSetFunc, args, args + 2);
}

Value *
RoxorCompiler::compile_gvar_get(NODE *node)
{
    assert(node->nd_vid > 0);
    assert(node->nd_entry != NULL);

    if (gvarGetFunc == NULL) {
	// VALUE rb_gvar_get(struct global_entry *entry);
	gvarGetFunc = cast<Function>(module->getOrInsertFunction(
		    "rb_gvar_get", RubyObjTy, PtrTy, NULL));
    }

    return CallInst::Create(gvarGetFunc, compile_global_entry(node),
	    "", bb);
}

Value *
RoxorCompiler::compile_constant_declaration(NODE *node, Value *val)
{
    int flags = 0;
    bool lexical_lookup = false;

    Value *args[5];

    if (node->nd_vid > 0) {
	lexical_lookup = true;
	args[0] = nilVal;
	args[1] = compile_id(node->nd_vid);
    }
    else {
	assert(node->nd_else != NULL);
	args[0] = compile_class_path(node->nd_else, &flags);
	assert(node->nd_else->nd_mid > 0);
	args[1] = compile_id(node->nd_else->nd_mid);
	lexical_lookup = flags & DEFINE_OUTER;
    }
    args[2] = val;
    args[3] = ConstantInt::get(Int8Ty, lexical_lookup ? 1 : 0);
    if (lexical_lookup) {
	args[4] = compile_outer_stack();
    }
    else {
	args[4] = compile_const_pointer(NULL);
    }

    CallInst::Create(setConstFunc, args, args + 5, "", bb);

    return val;
}

Value *
RoxorCompiler::compile_current_class(void)
{
    if (current_opened_class == NULL) {
	VALUE current_class = (VALUE)GET_VM()->get_current_class();
	return current_class == 0
	    ? compile_nsobject() : compile_literal(current_class);
    }
    return new LoadInst(current_opened_class, "", bb);
}

Value *
RoxorCompiler::compile_nsobject(void)
{
    return ConstantInt::get(RubyObjTy, rb_cObject);
}

Value *
RoxorAOTCompiler::compile_nsobject(void)
{
    if (cObject_gvar == NULL) {
	cObject_gvar = new GlobalVariable(*RoxorCompiler::module, RubyObjTy,
		false, GlobalValue::InternalLinkage, zeroVal, "NSObject");
	class_gvars.push_back(cObject_gvar);
    }
    return new LoadInst(cObject_gvar, "", bb);
}

Value *
RoxorCompiler::compile_standarderror(void)
{
    return ConstantInt::get(RubyObjTy, rb_eStandardError);
}

Value *
RoxorAOTCompiler::compile_standarderror(void)
{
    if (cStandardError_gvar == NULL) {
	cStandardError_gvar = new GlobalVariable(*RoxorCompiler::module,
		RubyObjTy, false, GlobalValue::InternalLinkage, zeroVal,
		"StandardError");
	class_gvars.push_back(cStandardError_gvar);
    }
    return new LoadInst(cStandardError_gvar, "", bb);
}

Value *
RoxorCompiler::compile_id(ID id)
{
    return ConstantInt::get(IntTy, (long)id);
}

Value *
RoxorAOTCompiler::compile_id(ID id)
{
    std::map<ID, GlobalVariable *>::iterator iter = ids.find(id);

    GlobalVariable *gvar;
    if (iter == ids.end()) {
	gvar = new GlobalVariable(*RoxorCompiler::module, IntTy, false,
		GlobalValue::InternalLinkage, ConstantInt::get(IntTy, 0), "");
	ids[id] = gvar;
    }
    else {
	gvar = iter->second;
    }

    return new LoadInst(gvar, "", bb);
}

Value *
RoxorCompiler::compile_const(ID id, Value *outer)
{
    bool outer_given = true;
    if (outer == NULL) {
	outer = compile_current_class();
	outer_given = false;
    }

    int flags = 0;
    if (!outer_given) {
	flags |= CONST_LOOKUP_LEXICAL;
    }
    if (dynamic_class) {
	flags |= CONST_LOOKUP_DYNAMIC_CLASS;
    }

    Value *args[] = {
	outer,
	compile_ccache(id),
	compile_id(id),
	ConstantInt::get(Int32Ty, flags),
	compile_outer_stack()
    };
    Instruction *insn = compile_protected_call(getConstFunc, args, args + 5);
    attach_current_line_metadata(insn);
    return insn;
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

    Value *args[] = { obj };
    return compile_protected_call(singletonClassFunc, args, args + 1);
}

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
    BasicBlock *old_rescue_invoke_bb = rescue_invoke_bb;
    BasicBlock *new_rescue_invoke_bb = BasicBlock::Create(context, "rescue", f);
    BasicBlock *merge_bb = BasicBlock::Create(context, "merge", f);
    rescue_invoke_bb = new_rescue_invoke_bb;

    // Prepare arguments for the runtime.
    Value *self = current_self;
    Value *what1 = NULL;
    Value *what2 = NULL;
    int type = 0;
    bool expression = false;

    switch (nd_type(node)) {
	case NODE_IVAR:
	    type = DEFINED_IVAR;
	    what1 = compile_id(node->nd_vid);
	    break;

	case NODE_GVAR:
	    type = DEFINED_GVAR;
	    // TODO AOT compiler
	    what1 = ConstantInt::get(RubyObjTy, (VALUE)node->nd_entry);
	    break;

	case NODE_CVAR:
	    type = DEFINED_CVAR;
	    what1 = compile_id(node->nd_vid);
	    break;

	case NODE_CONST:
	    type = DEFINED_LCONST;
	    what1 = compile_id(node->nd_vid);
	    what2 = compile_current_class();
	    break;

	case NODE_SUPER:
	case NODE_ZSUPER:
	    type = DEFINED_SUPER;
	    what1 = compile_id(current_mid);
	    break;

	case NODE_COLON2:
	case NODE_COLON3:
	    what2 = nd_type(node) == NODE_COLON2
		? compile_node(node->nd_head)
		: compile_nsobject();
	    if (rb_is_const_id(node->nd_mid)) {
		type = DEFINED_CONST;
		what1 = compile_id(node->nd_mid);
	    }
	    else {
		type = DEFINED_METHOD;
		what1 = compile_id(node->nd_mid);
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
	    what1 = compile_id(node->nd_mid);
	    break;

	default:
	    // Unhandled node type, probably an expression. Let's compile
	    // it and it case everything goes okay we just return 'expression'.
	    compile_node(node);
	    expression = true;
	    break;
    }

    if (definedFunc == NULL) {
	// VALUE rb_vm_defined(VALUE self, int type, VALUE what, VALUE what2, rb_vm_outer_t *outer_stack);
	definedFunc = cast<Function>(module->getOrInsertFunction(
		    "rb_vm_defined",
		    RubyObjTy, RubyObjTy, Int32Ty, RubyObjTy, RubyObjTy, PtrTy,
		    NULL));
    }

    Value *val = NULL;
    if (!expression) {
	// Call the runtime.
	Value *outer_stack_val;
	if (type == DEFINED_CONST || type == DEFINED_LCONST) {
	    if (current_mid != 0) {
		outer_stack_uses = true;
	    }
	    outer_stack_val = compile_outer_stack();
	}
	else {
	    outer_stack_val = compile_const_pointer(NULL);
	}
	Value *args[] = {
	    self,
	    ConstantInt::get(Int32Ty, type),
	    what1 == NULL ? nilVal : what1,
	    what2 == NULL ? nilVal : what2,
	    outer_stack_val
	};
	val = compile_protected_call(definedFunc, args, args + 5);
    }
    else {
	val = ConstantInt::get(RubyObjTy, (long)CFSTR("expression"));
    }
    BasicBlock *normal_bb = bb;
    BranchInst::Create(merge_bb, bb);

    // The rescue block - here we simply do nothing.
    bb = new_rescue_invoke_bb;
    compile_landing_pad_header();
    compile_landing_pad_footer();
    BranchInst::Create(merge_bb, bb);

    // Now merging.
    bb = merge_bb;
    PHINode *pn = PHINode::Create(RubyObjTy, "", bb);
    pn->addIncoming(val, normal_bb);
    pn->addIncoming(nilVal, new_rescue_invoke_bb);

    rescue_invoke_bb = old_rescue_invoke_bb;

    return pn;
}

Value *
RoxorCompiler::compile_dstr(NODE *node)
{
    std::vector<Value *> params;

    if (node->nd_lit != 0) {
	params.push_back(compile_literal(node->nd_lit));
    }

    NODE *n = node->nd_next;
    assert(n != NULL);
    while (n != NULL) {
	params.push_back(compile_node(n->nd_head));
	n = n->nd_next;
    }

    const int count = params.size();

    params.insert(params.begin(), ConstantInt::get(Int32Ty, count));

    if (newStringFunc == NULL) {
	// VALUE rb_str_new_fast(int argc, ...)
	std::vector<const Type *> types;
	types.push_back(Int32Ty);
	FunctionType *ft = FunctionType::get(RubyObjTy, types, true);
	newStringFunc = cast<Function>(module->getOrInsertFunction(
		    "rb_str_new_fast", ft));
    }

    return CallInst::Create(newStringFunc, params.begin(), params.end(), "",
	    bb);
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

    Value *index = ConstantInt::get(Int32Ty, idx);
    Value *slot = GetElementPtrInst::Create(dvars_ary, index, rb_id2name(name),
	    bb);
    return new LoadInst(slot, "", bb);
}

void
RoxorCompiler::compile_break_val(Value *val)
{
    if (breakFunc == NULL) {
	// void rb_vm_break(VALUE val);
	breakFunc = cast<Function>(
		module->getOrInsertFunction("rb_vm_break", 
		    VoidTy, RubyObjTy, NULL));
    }

    CallInst::Create(breakFunc, val, "", bb);
}

void
RoxorCompiler::compile_break_within_loop(Value *val)
{
    if (ensure_bb == NULL) {
	BranchInst::Create(current_loop_end_bb, bb);
	current_loop_exit_val->addIncoming(val, bb);
    }
    else {
	BranchInst::Create(ensure_bb, bb);
	ensure_pn->addIncoming(val, bb);
    }
}

void
RoxorCompiler::compile_break_within_block(Value *val)
{
    if (ensure_bb == NULL) {
	compile_break_val(val);
	ReturnInst::Create(context, val, bb);
    }
    else {
	BranchInst::Create(ensure_bb, bb);
	ensure_pn->addIncoming(val, bb);
    }
}

void
RoxorCompiler::compile_return_from_block(Value *val, int id)
{
    if (returnFromBlockFunc == NULL) {
	// void rb_vm_return_from_block(VALUE val, int id,
	//	rb_vm_block_t *current_block);
	returnFromBlockFunc = cast<Function>(
		module->getOrInsertFunction("rb_vm_return_from_block", 
		    VoidTy, RubyObjTy, Int32Ty, PtrTy, NULL));
    }

    Value *args[] = {
	val,
	ConstantInt::get(Int32Ty, id),
	running_block
    };
    compile_protected_call(returnFromBlockFunc, args, args + 3);
}

void
RoxorCompiler::compile_return_from_block_handler(int id)
{
    Value *exception = compile_landing_pad_header();

    if (checkReturnFromBlockFunc == NULL) {
	// VALUE rb_vm_check_return_from_block_exc(void *exc, int id);
	checkReturnFromBlockFunc = cast<Function>(
		module->getOrInsertFunction(
		    "rb_vm_check_return_from_block_exc", 
		    RubyObjTy, PtrTy, Int32Ty, NULL));
    }

    Value *args[] = {
	exception,
	ConstantInt::get(Int32Ty, id)
    };
    Value *val = CallInst::Create(checkReturnFromBlockFunc, args, args + 2,
	    "", bb);

    Function *f = bb->getParent();
    BasicBlock *ret_bb = BasicBlock::Create(context, "ret", f);
    BasicBlock *rethrow_bb  = BasicBlock::Create(context, "rethrow", f);
    Value *need_ret = new ICmpInst(*bb, ICmpInst::ICMP_NE, val,
	    ConstantInt::get(RubyObjTy, Qundef));
    BranchInst::Create(ret_bb, rethrow_bb, need_ret, bb);

    bb = ret_bb;
    compile_landing_pad_footer(false);
    ReturnInst::Create(context, val, bb);	

    bb = rethrow_bb;
    compile_rethrow_exception();
}

Value *
RoxorCompiler::compile_jump(NODE *node)
{
    const bool within_loop = current_loop_begin_bb != NULL
	&& current_loop_body_bb != NULL
	&& current_loop_end_bb != NULL;

    const bool within_block = block_declaration;

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
		compile_break_within_loop(val);
	    }
	    else if (within_block) {
		compile_break_within_block(val);
	    }
	    else {
		rb_raise(rb_eLocalJumpError, "unexpected break");
	    }
	    break;

	case NODE_NEXT:
	    if (within_loop) {
		if (ensure_node != NULL) {
		    compile_node(ensure_node);
		}
		BranchInst::Create(current_loop_begin_bb, bb);
	    }
	    else if (within_block) {
		compile_simple_return(val);
	    }
	    else {
		rb_raise(rb_eLocalJumpError, "unexpected next");
	    }
	    break;

	case NODE_REDO:
	    if (current_rescue) {
		compile_landing_pad_footer();
	    }
	    if (within_loop) {
		if (ensure_node != NULL) {
		    compile_node(ensure_node);
		}
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
	    if (within_block) {
		if (return_from_block == -1) {
		    return_from_block = return_from_block_ids++;
		}
		compile_return_from_block(val, return_from_block);
		ReturnInst::Create(context, val, bb);
	    }
	    else {
		if (current_rescue) {
		    compile_pop_exception();
		}
		compile_simple_return(val);
	    }
	    break;
    }

    // To not complicate the compiler even more, let's be very lazy here and
    // continue on a dead branch. Hopefully LLVM is smart enough to eliminate
    // it at compilation time.
    bb = BasicBlock::Create(context, "DEAD", bb->getParent());

    return val;
}

void
RoxorCompiler::compile_simple_return(Value *val)
{
    if (ensure_bb != NULL) {
	BranchInst::Create(ensure_bb, bb);
	ensure_pn->addIncoming(val, bb);
    }
    else {
	ReturnInst::Create(context, val, bb);
    }
}

Value *
RoxorCompiler::compile_set_has_ensure(Value *val)
{
    if (setHasEnsureFunc == NULL) {
	setHasEnsureFunc = cast<Function>(
		module->getOrInsertFunction(
		    "rb_vm_set_has_ensure", Int8Ty, Int8Ty, NULL));
    }

    return CallInst::Create(setHasEnsureFunc, val, "", bb);
}

Value *
RoxorCompiler::compile_class_path(NODE *node, int *flags)
{
    if (nd_type(node) == NODE_COLON3) {
	// ::Foo
	if (flags != NULL) {
	    *flags = 0;
	}
	return compile_nsobject();
    }
    else if (node->nd_head != NULL) {
	// Bar::Foo
	if (flags != NULL) {
	    *flags = DEFINE_SUB_OUTER;
	}
	return compile_node(node->nd_head);
    }
    else {
	if (flags != NULL) {
	    *flags = DEFINE_OUTER;
	}
	return compile_current_class();
    }
}

Value *
RoxorCompiler::compile_landing_pad_header(void)
{
    Function *eh_exception_f = Intrinsic::getDeclaration(module,
	    Intrinsic::eh_exception);
    Value *eh_ptr = CallInst::Create(eh_exception_f, "", bb);

    Function *eh_selector_f = Intrinsic::getDeclaration(module,
	    Intrinsic::eh_selector);

    std::vector<Value *> params;
    params.push_back(eh_ptr);
    Function *__gxx_personality_v0_func = NULL;
    if (__gxx_personality_v0_func == NULL) {
	__gxx_personality_v0_func = cast<Function>(
		module->getOrInsertFunction("__gxx_personality_v0",
		    PtrTy, NULL));
    }
    params.push_back(ConstantExpr::getBitCast(__gxx_personality_v0_func, PtrTy));

    // catch (...)
    params.push_back(compile_const_pointer(NULL));

    CallInst::Create(eh_selector_f, params.begin(), params.end(), "", bb);

    Function *beginCatchFunc = NULL;
    if (beginCatchFunc == NULL) {
	// void *__cxa_begin_catch(void *);
	beginCatchFunc = cast<Function>(
		module->getOrInsertFunction("__cxa_begin_catch",
		    PtrTy, PtrTy, NULL));
    }
    params.clear();
    params.push_back(eh_ptr);
    return CallInst::Create(beginCatchFunc, params.begin(), params.end(),
	    "", bb);
}

void
RoxorCompiler::compile_pop_exception(int pos)
{
    if (popExceptionFunc == NULL) {
	// void rb_vm_pop_exception(int pos);
	popExceptionFunc = cast<Function>(
		module->getOrInsertFunction("rb_vm_pop_exception", 
		    VoidTy, Int32Ty, NULL));
    }

    CallInst::Create(popExceptionFunc, ConstantInt::get(Int32Ty, pos), "", bb);
}

void
RoxorCompiler::compile_landing_pad_footer(bool pop_exception)
{
    if (pop_exception) {
	compile_pop_exception();
    }

    Function *endCatchFunc = NULL;
    if (endCatchFunc == NULL) {
	// void __cxa_end_catch(void);
	endCatchFunc = cast<Function>(
		module->getOrInsertFunction("__cxa_end_catch",
		    VoidTy, NULL));
    }
    CallInst::Create(endCatchFunc, "", bb);
}

void
RoxorCompiler::compile_rethrow_exception(void)
{
    if (rescue_rethrow_bb == NULL) {
	Function *rethrowFunc = NULL;
	if (rethrowFunc == NULL) {
	    // void __cxa_rethrow(void);
	    rethrowFunc = cast<Function>(
		    module->getOrInsertFunction("__cxa_rethrow", VoidTy, NULL));
	}
	CallInst::Create(rethrowFunc, "", bb);
	new UnreachableInst(context, bb);
    }
    else {
	BranchInst::Create(rescue_rethrow_bb, bb);
    }
}

Value *
RoxorCompiler::compile_current_exception(void)
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

Value *
RoxorCompiler::compile_optimized_dispatch_call(SEL sel, int argc,
	std::vector<Value *> &params)
{
    // The not operator (!).
    if (sel == selNot) {
	
	if (current_block_func != NULL || argc != 0) {
	    return NULL;
	}
	
	Value *val = params[1]; // self

	Function *f = bb->getParent();

	BasicBlock *falseBB = BasicBlock::Create(context, "", f);
	BasicBlock *trueBB = BasicBlock::Create(context, "", f);
	BasicBlock *mergeBB = BasicBlock::Create(context, "", f);

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
    else if (sel == selPLUS || sel == selMINUS || sel == selDIV || sel == selMOD
	     || sel == selMULT || sel == selLT || sel == selLE 
	     || sel == selGT || sel == selGE || sel == selEq
	     || sel == selNeq || sel == selEqq) {

	if (current_block_func != NULL || argc != 1) {
	    return NULL;
	}

	Value *leftVal = params[1]; // self
	Value *rightVal = params.back();

	Function *func = NULL;
	if (sel == selPLUS) {
	    func = fastPlusFunc;
	}
	else if (sel == selMINUS) {
	    func = fastMinusFunc;
	}
	else if (sel == selDIV) {
	    func = fastDivFunc;
	}
	else if (sel == selMOD) {
	    func = fastModFunc;
	}
	else if (sel == selMULT) {
	    func = fastMultFunc;
	}
	else if (sel == selLT) {
	    func = fastLtFunc;
	}
	else if (sel == selLE) {
	    func = fastLeFunc;
	}
	else if (sel == selGT) {
	    func = fastGtFunc;
	}
	else if (sel == selGE) {
	    func = fastGeFunc;
	}
	else if (sel == selEq) {
	    func = fastEqFunc;
	}
	else if (sel == selNeq) {
	    func = fastNeqFunc;
	}
	else if (sel == selEqq) {
	    func = fastEqqFunc;
	}
	assert(func != NULL);

	GlobalVariable *is_redefined = GET_CORE()->redefined_op_gvar(sel, true);

	Value *args[] = {
	    leftVal,
	    rightVal,
	    new LoadInst(is_redefined, "", bb)
	};
	return compile_protected_call(func, args, args + 3);
    }
    // Other operators (#<< or #[] or #[]=)
    else if (sel == selLTLT || sel == selAREF || sel == selASET) {

	const int expected_argc = sel == selASET ? 2 : 1;
	if (current_block_func != NULL || argc != expected_argc) {
	    return NULL;
	}

	if (params.size() - argc > 6) {
	    // Looks like there is a splat argument there, we can't handle this
	    // in the primitives.
	    return NULL;
	}

	Function *func = NULL;
	if (sel == selLTLT) {
	    func = fastShiftFunc;
	}
	else if (sel == selAREF) {
	    func = fastArefFunc;
	}
	else if (sel == selASET) {
	    func = fastAsetFunc;
	}
	assert(func != NULL);

	std::vector<Value *> new_params;
	new_params.push_back(params[1]);		// self
	if (argc == 1) {
	    new_params.push_back(params.back());	// other
	}
	else {
	    new_params.insert(new_params.end(), params.end() - 2, params.end());
	}

	GlobalVariable *is_redefined = GET_CORE()->redefined_op_gvar(sel, true);
	new_params.push_back(new LoadInst(is_redefined, "", bb));

	return compile_protected_call(func, new_params);
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

	GlobalVariable *is_redefined = GET_CORE()->redefined_op_gvar(sel, true);

	Value *is_redefined_val = new LoadInst(is_redefined, "", bb);
	Value *isOpRedefined = new ICmpInst(*bb, ICmpInst::ICMP_EQ,
		is_redefined_val, ConstantInt::get(Int8Ty, 0));

	Function *f = bb->getParent();

	BasicBlock *thenBB = BasicBlock::Create(context, "op_not_redefined", f);
	BasicBlock *elseBB = BasicBlock::Create(context, "op_dispatch", f);
	BasicBlock *mergeBB = BasicBlock::Create(context, "op_merge", f);

	BranchInst::Create(thenBB, elseBB, isOpRedefined, bb);

	bb = thenBB;
	std::vector<Value *> new_params;
	// Compile a null top reference, to ignore protected visibility.
	new_params.push_back(ConstantInt::get(RubyObjTy, 0));
	new_params.push_back(params[1]);
	new_params.push_back(compile_sel(new_sel));
	new_params.push_back(params[3]);
	new_params.push_back(ConstantInt::get(Int8Ty, DISPATCH_FCALL));
	new_params.push_back(ConstantInt::get(Int32Ty, argc - 1));
	for (int i = 0; i < argc - 1; i++) {
	    new_params.push_back(params[7 + i]);
	}
	Value *thenVal = compile_dispatch_call(new_params);
	thenBB = bb;
	BranchInst::Create(mergeBB, thenBB);

	bb = elseBB;
	Value *elseVal = compile_dispatch_call(params);
	elseBB = bb;
	BranchInst::Create(mergeBB, elseBB);

	bb = mergeBB;
	PHINode *pn = PHINode::Create(RubyObjTy, "op_tmp", mergeBB);
	pn->addIncoming(thenVal, thenBB);
	pn->addIncoming(elseVal, elseBB);

	return pn;
    }
    // __method__ or __callee__
    else if (sel == sel__method__ || sel == sel__callee__) {

	if (current_block_func != NULL || argc != 0) {
	    return NULL;
	}

	Function *f = bb->getParent();
	Function::arg_iterator arg = f->arg_begin();
	arg++; // skip self
	Value *callee_sel = arg;

	if (selToSymFunc == NULL) {
	    // VALUE rb_sel_to_sym(SEL sel);
	    selToSymFunc = cast<Function>(
		    module->getOrInsertFunction("rb_sel_to_sym",
			RubyObjTy, PtrTy, NULL));
	}
	return CallInst::Create(selToSymFunc, callee_sel, "", bb);
    }
    return NULL;
}

Instruction *
RoxorCompiler::compile_range(Value *beg, Value *end, bool exclude_end,
	bool retain)
{
    if (newRangeFunc == NULL) {
	// VALUE rb_range_new2(VALUE beg, VALUE end, int exclude_end,
	//	int retain);
	newRangeFunc = cast<Function>(module->getOrInsertFunction(
		    "rb_range_new2",
		    RubyObjTy, RubyObjTy, RubyObjTy, Int32Ty, Int32Ty,
		    NULL));
    }

    Value *args[] = {
	beg,
	end,
	ConstantInt::get(Int32Ty, exclude_end ? 1 : 0),
	ConstantInt::get(Int32Ty, retain ? 1 : 0)
    };
    return compile_protected_call(newRangeFunc, args, args + 4);
}

Value *
RoxorCompiler::compile_literal(VALUE val)
{
    if (TYPE(val) == T_STRING) {
	// We must compile a new string creation because strings are
	// mutable, we can't simply compile a reference to a master
	// copy.
	//
	//	10.times { s = 'foo'; s << 'bar' }
	//
	if (rb_str_chars_len(val) == 0) {
	    if (newString3Func == NULL) {	
		newString3Func = cast<Function>(
			module->getOrInsertFunction(
			    "rb_str_new_empty", RubyObjTy, NULL));
	    }
	    return CallInst::Create(newString3Func, "", bb);
	}
	else {
	    const char *cstr = RSTRING_PTR(val);
	    const int cstr_len = RSTRING_LEN(val);

	    assert(cstr_len > 0);

	    if (newString2Func == NULL) {	
		newString2Func = cast<Function>(
			module->getOrInsertFunction(
			    "rb_str_new",
			    RubyObjTy, PtrTy, Int32Ty, NULL));
	    }

	    Value *args[] = {
		compile_const_global_string(cstr, cstr_len),
		ConstantInt::get(Int32Ty, cstr_len)
	    };
	    return CallInst::Create(newString2Func, args, args + 2, "", bb);
	}
    }

    return compile_immutable_literal(val);
}

Value *
RoxorCompiler::compile_immutable_literal(VALUE val)
{
    GC_RETAIN(val);
    return ConstantInt::get(RubyObjTy, (long)val); 
}

Value *
RoxorAOTCompiler::compile_immutable_literal(VALUE val)
{
    if (SPECIAL_CONST_P(val)) {
	return RoxorCompiler::compile_immutable_literal(val);
    }
    if (rb_obj_is_kind_of(val, rb_cEncoding)) {
	// This is the __ENCODING__ keyword.
	// TODO: compile the real encoding...
	return nilVal;
    }

    std::map<VALUE, GlobalVariable *>::iterator iter = literals.find(val);
    GlobalVariable *gvar = NULL;

    if (iter == literals.end()) {
	gvar = new GlobalVariable(*RoxorCompiler::module, RubyObjTy, false,
		GlobalValue::InternalLinkage, nilVal, "");
	literals[val] = gvar;
    }
    else {
	gvar = iter->second;
    }

    return new LoadInst(gvar, "", bb);
}

Value *
RoxorCompiler::compile_global_entry(NODE *node)
{
    return compile_const_pointer(node->nd_entry);
}

Value *
RoxorAOTCompiler::compile_global_entry(NODE *node)
{
    const ID name = node->nd_vid;
    assert(name > 0);
    
    std::map<ID, GlobalVariable *>::iterator iter = global_entries.find(name);
    GlobalVariable *gvar = NULL;
    if (iter == global_entries.end()) {
	gvar = new GlobalVariable(*RoxorCompiler::module, PtrTy, false,
		GlobalValue::InternalLinkage, Constant::getNullValue(PtrTy),
		"");
	global_entries[name] = gvar;
    }
    else {
	gvar = iter->second;
    }

    return new LoadInst(gvar, "", bb);
}

Value *
RoxorCompiler::compile_set_current_class(Value *klass)
{
    if (setCurrentClassFunc == NULL) {
	// Class rb_vm_set_current_class(Class klass)
	setCurrentClassFunc = cast<Function>(
		module->getOrInsertFunction("rb_vm_set_current_class",
		    RubyObjTy, RubyObjTy, NULL));
    }

    return CallInst::Create(setCurrentClassFunc, klass, "", bb);
}

Value *
RoxorCompiler::compile_push_outer(Value *klass)
{
    if (pushOuterFunc == NULL) {
	// rb_vm_outer_t *rb_vm_push_outer(Class klass)
	pushOuterFunc = cast<Function>(
	    module->getOrInsertFunction("rb_vm_push_outer",
					PtrTy, RubyObjTy, NULL));
    }
    
    Value *val = CallInst::Create(pushOuterFunc, klass, "", bb);
    outer_stack = new GlobalVariable(*RoxorCompiler::module, PtrTy, false,
				     GlobalValue::InternalLinkage,
				     Constant::getNullValue(PtrTy), "");
    assert(outer_stack != NULL);
    new StoreInst(val, outer_stack, "", bb);
    return val;
}

void
RoxorCompiler::compile_pop_outer(bool need_release)
{
    if (popOuterFunc == NULL) {
	// void rb_vm_pop_outer(unsigned char need_release)
	popOuterFunc = cast<Function>(
	    module->getOrInsertFunction("rb_vm_pop_outer",
		    VoidTy, Int8Ty, NULL));
    }
    
    Value *val = ConstantInt::get(Int8Ty, need_release ? 1 : 0);
    CallInst::Create(popOuterFunc, val, "", bb);
}

Value *
RoxorCompiler::compile_outer_stack(void)
{
    if (outer_stack == NULL) {
	return compile_const_pointer(rb_vm_get_outer_stack());
    }
    return new LoadInst(outer_stack, "", bb);
}

Value *
RoxorCompiler::compile_set_current_outer(void)
{
    if (setCurrentOuterFunc == NULL) {
	// rb_vm_outer_t *rb_vm_set_current_outer(rb_vm_outer_t *outer)
	setCurrentOuterFunc = cast<Function>(
	    module->getOrInsertFunction("rb_vm_set_current_outer",
					PtrTy, PtrTy, NULL));
    }
    
    return CallInst::Create(setCurrentOuterFunc, compile_outer_stack(), "", bb);
}

void
RoxorCompiler::compile_set_current_scope(Value *klass, Value *scope)
{
    Value *args[] = {
	klass,
	scope
    };
    CallInst::Create(setScopeFunc, args, args + 2, "", bb);
}

void
RoxorCompiler::compile_node_error(const char *msg, NODE *node)
{
    int t = nd_type(node);
    printf("%s: %d (%s)", msg, t, ruby_node_name(t));
    abort();
}

void
RoxorCompiler::compile_keep_vars(BasicBlock *startBB, BasicBlock *mergeBB)
{
    if (keepVarsFunc == NULL) {
	// void rb_vm_keep_vars(rb_vm_var_uses *uses, int lvars_size, ...)
	std::vector<const Type *> types;
	types.push_back(PtrTy);
	types.push_back(Int32Ty);
	FunctionType *ft = FunctionType::get(VoidTy, types, true);
	keepVarsFunc = cast<Function>
	    (module->getOrInsertFunction("rb_vm_keep_vars", ft));
    }

    BasicBlock *notNullBB = BasicBlock::Create(context, "not_null",
	    startBB->getParent());

    bb = startBB;
    Value *usesVal = new LoadInst(current_var_uses, "", bb);
    Value *notNullCond = new ICmpInst(*bb, ICmpInst::ICMP_NE, usesVal,
	    compile_const_pointer(NULL));
    // we only need to call keepVarsFunc if current_var_uses is not NULL
    BranchInst::Create(notNullBB, mergeBB, notNullCond, bb);

    bb = notNullBB;

    // params must be filled each time because in AOT mode it contains
    // instructions
    std::vector<Value *> params;
    params.push_back(new LoadInst(current_var_uses, "", bb));
    params.push_back(NULL);
    int vars_count = 0;
    for (std::map<ID, Value *>::iterator iter = lvars.begin();
	    iter != lvars.end(); ++iter) {
	ID name = iter->first;
	Value *slot = iter->second;
	if (std::find(dvars.begin(), dvars.end(), name) == dvars.end()) {
	    Value *id_val = compile_id(name);
	    params.push_back(id_val);
	    params.push_back(slot);
	    vars_count++;
	}
    }
    params[1] = ConstantInt::get(Int32Ty, vars_count);

    CallInst::Create(keepVarsFunc, params.begin(), params.end(), "", bb);

    BranchInst::Create(mergeBB, bb);
}

bool
RoxorCompiler::should_inline_function(Function *f)
{
    return f->getName().startswith("vm_");
}

void
RoxorCompiler::inline_function_calls(Function *f)
{
    std::vector<CallInst *> insns;

    for (Function::iterator fi = f->begin(); fi != f->end(); ++fi) {
	for (BasicBlock::iterator bi = fi->begin(); bi != fi->end(); ++bi) {
	    CallInst *insn = dyn_cast<CallInst>(bi);
	    if (insn != NULL) {
		Function *called = insn->getCalledFunction();
		if (called != NULL && should_inline_function(called)) {
		    insns.push_back(insn);
		}
	    }
	}
    }

    InlineFunctionInfo IFI;
    for (std::vector<CallInst *>::iterator i = insns.begin();
	    i != insns.end(); ++i) {
	InlineFunction(*i, IFI);
    }
}

Function *
RoxorCompiler::compile_scope(NODE *node)
{
    rb_vm_arity_t arity = rb_vm_node_arity(node);
    const int nargs = bb == NULL ? 0 : arity.real;
    const bool has_dvars = block_declaration;

    // Get dynamic vars.
    if (has_dvars && node->nd_tbl != NULL) {
	const int args_count = (int)node->nd_tbl[0];
	const int lvar_count = (int)node->nd_tbl[args_count + 1];
	for (int i = 0; i < lvar_count; i++) {
	    ID id = node->nd_tbl[i + args_count + 2];
	    if (lvars.find(id) != lvars.end()) {
		std::vector<ID>::iterator iter = std::find(dvars.begin(),
			dvars.end(), id);
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
    types.push_back(PtrTy);	// sel
    if (has_dvars) {
	types.push_back(RubyObjPtrPtrTy); // dvars array
	types.push_back(PtrTy); // rb_vm_block_t of the currently running block
    }
    for (int i = 0; i < nargs; ++i) {
	types.push_back(RubyObjTy);
    }
    FunctionType *ft = FunctionType::get(RubyObjTy, types, false);

    Function *f = Function::Create(ft, GlobalValue::InternalLinkage,
	    "ruby_scope", module);

    NODE *old_ensure_node = ensure_node;
    BasicBlock *old_ensure_bb = ensure_bb;
    ensure_node = NULL;
    ensure_bb = NULL;

    AllocaInst *old_argv_buffer = argv_buffer;
    BasicBlock *old_rescue_invoke_bb = rescue_invoke_bb;
    BasicBlock *old_rescue_rethrow_bb = rescue_rethrow_bb;
    BasicBlock *old_entry_bb = entry_bb;
    BasicBlock *old_bb = bb;
    BasicBlock *new_rescue_invoke_bb = NULL;
    BasicBlock *new_rescue_rethrow_bb = NULL;
    argv_buffer = NULL;
    rescue_invoke_bb = NULL;
    rescue_rethrow_bb = NULL;
    bb = BasicBlock::Create(context, "MainBlock", f);

    DISubprogram old_debug_subprogram = debug_subprogram;
#if 0
    // This is not the right way to emit subprogram DWARF entries,
    // llc emits some assembly that doesn't compile because some
    // symbols are duplicated.
    debug_subprogram = debug_info->CreateSubprogram(
	    debug_compile_unit, f->getName(), f->getName(),
	    f->getName(), debug_compile_unit, nd_line(node),
	    DIType(), f->hasInternalLinkage(), true);
    debug_info->InsertSubprogramStart(debug_subprogram, bb);
#endif

    std::map<ID, Value *> old_lvars = lvars;
    lvars.clear();
    std::vector<MAsgnValue> old_masgn_values = masgn_values;
    masgn_values.clear();

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

    Value *old_current_block_arg = current_block_arg;
    current_block_arg = NULL;
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
			    module->getOrInsertFunction(
				"rb_vm_current_block_object", RubyObjTy, NULL));
		}
		val = CallInst::Create(currentBlockObjectFunc, "", bb);
		current_block_arg = val;
	    }
	    Value *slot = new AllocaInst(RubyObjTy, "", bb);
	    new StoreInst(val, slot, bb);
	    lvars[id] = slot;
	    has_vars_to_save = true;
	}

	// Local vars must be created before the optional arguments
	// because they can be used in them, for instance with def f(a=b=c=1).
	if (compile_lvars(&node->nd_tbl[args_count + 1])) {
	    has_vars_to_save = true;
	}

	if (has_vars_to_save) {
	    current_var_uses = new AllocaInst(PtrTy, "", bb);
	    new StoreInst(compile_const_pointer(NULL),
		    current_var_uses, bb);

	    new_rescue_invoke_bb = BasicBlock::Create(context,
		    "rescue_save_vars", f);
	    new_rescue_rethrow_bb = BasicBlock::Create(context,
		    "rescue_save_vars.rethrow", f);
	    rescue_invoke_bb = new_rescue_invoke_bb;
	    rescue_rethrow_bb = new_rescue_rethrow_bb;
	}

	NODE *args_node = node->nd_args;
	if (args_node != NULL) {
	    // Compile multiple assignment arguments (def f((a, b, v))).
	    // This must also be done after the creation of local variables.
	    NODE *rest_node = args_node->nd_next;
	    if (rest_node != NULL) {
		NODE *right_req_node = rest_node->nd_next;
		if (right_req_node != NULL) {
		    NODE *last_node = right_req_node->nd_next;
		    if (last_node != NULL) {
			assert(nd_type(last_node) == NODE_AND);
			// Multiple assignment for the left-side required
			// arguments.
			if (last_node->nd_1st != NULL) {
			    compile_node(last_node->nd_1st);
			}
			// Multiple assignment for the right-side required
			// arguments.
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
	entry_bb = BasicBlock::Create(context, "entry_point", f); 
	BranchInst::Create(entry_bb, bb);
	bb = entry_bb;

	rb_vm_arity_t old_current_arity = current_arity;
	Function *old_current_non_block_func = current_non_block_func;
	if (!block_declaration) {
	    current_non_block_func = f;
	    current_arity = arity;
	}

	DEBUG_LEVEL_INC();
	val = compile_node(node->nd_body);
	DEBUG_LEVEL_DEC();

	current_non_block_func = old_current_non_block_func;
	current_arity = old_current_arity;
    }
    if (val == NULL) {
	val = nilVal;
    }

    ReturnInst::Create(context, val, bb);

    // The rethrows after the save of variables must be real rethrows.
    rescue_rethrow_bb = NULL;
    rescue_invoke_bb = NULL;

    // Current_lvar_uses has 2 uses or more if it is really used.
    // (there is always a StoreInst in which we assign it NULL)
    if (current_var_uses != NULL && current_var_uses->hasNUsesOrMore(2)) {
	// Searches all ReturnInst in the function we just created and add
	// before a call to the function to save the local variables if
	// necessary (we can't do this before finishing compiling the whole
	// function because we can't be sure if the function contains a block
	// or not before).
	std::vector<ReturnInst *> to_fix;
	for (Function::iterator block_it = f->begin();
		block_it != f->end();
		++block_it) {
	    for (BasicBlock::iterator inst_it = block_it->begin();
		    inst_it != block_it->end();
		    ++inst_it) {
		ReturnInst *inst = dyn_cast<ReturnInst>(inst_it);
		if (inst != NULL) {
		    to_fix.push_back(inst);
		}
	    }
	}
	// We have to process the blocks in a second loop because
	// we can't modify the blocks while iterating on them.
	for (std::vector<ReturnInst *>::iterator inst_it = to_fix.begin();
		inst_it != to_fix.end();
		++inst_it) {

	    ReturnInst *inst = *inst_it;
	    BasicBlock *startBB = inst->getParent();
	    BasicBlock *mergeBB = startBB->splitBasicBlock(inst, "merge");
	    // We do not want the BranchInst added by splitBasicBlock.
	    startBB->getInstList().pop_back();
	    compile_keep_vars(startBB, mergeBB);
	}

	if (new_rescue_invoke_bb->use_empty()
		&& new_rescue_rethrow_bb->use_empty()) {
	    new_rescue_invoke_bb->eraseFromParent();
	    new_rescue_rethrow_bb->eraseFromParent();
	}
	else {
	    if (new_rescue_invoke_bb->use_empty()) {
		new_rescue_invoke_bb->eraseFromParent();
	    }
	    else {
		bb = new_rescue_invoke_bb;
		compile_landing_pad_header();
		BranchInst::Create(new_rescue_rethrow_bb, bb);
	    }

	    bb = new_rescue_rethrow_bb;
	    BasicBlock *mergeBB = BasicBlock::Create(context,
		    "merge", f);
	    compile_keep_vars(bb, mergeBB);

	    bb = mergeBB;
	    compile_rethrow_exception();
	}
    }
    else if (current_var_uses != NULL) {
	for (BasicBlock::use_iterator rescue_use_it =
		new_rescue_invoke_bb->use_begin();
		rescue_use_it != new_rescue_invoke_bb->use_end();
		rescue_use_it = new_rescue_invoke_bb->use_begin()) {
#if LLVM_TOT
	    InvokeInst *invoke = dyn_cast<InvokeInst>(rescue_use_it);
#else
	    InvokeInst *invoke = dyn_cast<InvokeInst>(*rescue_use_it);
#endif
	    assert(invoke != NULL);

	    // Transform the InvokeInst in CallInst.
	    std::vector<Value *> params;
	    for (unsigned i = 0; i < invoke->getNumOperands() - 3; i++) {
		params.push_back(invoke->getOperand(i));
	    }
	    CallInst *call_inst = CallInst::Create(
		    invoke->getCalledValue(),
		    params.begin(), params.end(),
		    "",
		    invoke);

	    // Transfer the debugging metadata if any.
	    MDNode *node = invoke->getMetadata(dbg_mdkind);
	    if (node !=NULL) {
		call_inst->setMetadata(dbg_mdkind, node);
	    }

	    invoke->replaceAllUsesWith(call_inst);
	    BasicBlock *normal_bb = dyn_cast<BasicBlock>(invoke->getNormalDest());
	    assert(normal_bb != NULL);
	    BranchInst::Create(normal_bb, invoke);
	    invoke->eraseFromParent();
	}
	new_rescue_invoke_bb->eraseFromParent();

	if (new_rescue_rethrow_bb->use_empty()) {
	    new_rescue_rethrow_bb->eraseFromParent();
	}
	else {
	    bb = new_rescue_rethrow_bb;
	    compile_rethrow_exception();
	}
    }

    for (std::vector<MAsgnValue>::iterator i = masgn_values.begin();
	    i != masgn_values.end(); ++i) {
	MAsgnValue &v = *i;
	if (v.ary->hasNUses(0)) {
	    v.ary->eraseFromParent();
	}
    }

    current_block_arg = old_current_block_arg;

    rescue_rethrow_bb = old_rescue_rethrow_bb;
    rescue_invoke_bb = old_rescue_invoke_bb;

    ensure_node = old_ensure_node;
    ensure_bb = old_ensure_bb;
    argv_buffer = old_argv_buffer;
    bb = old_bb;
    entry_bb = old_entry_bb;
    lvars = old_lvars;
    masgn_values = old_masgn_values;
    current_self = old_self;
    current_var_uses = old_current_var_uses;
    running_block = old_running_block;
    debug_subprogram = old_debug_subprogram;

    return f;
}

Value *
RoxorCompiler::compile_call(NODE *node, bool use_tco)
{
    NODE *recv = node->nd_recv;
    NODE *args = node->nd_args;
    ID mid = node->nd_mid;

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
    else {
	assert(mid > 0);
    }

    bool splat_args = false;
    bool positive_arity = false;
    if (nd_type(node) == NODE_ZSUPER) {
	assert(args == NULL);
	assert(current_non_block_func != NULL);
	const long s = current_non_block_func->getArgumentList().size();
	positive_arity = s - 2 > 0; /* skip self and sel */
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

    // Recursive method call optimization. Not for everyone.
    if (use_tco && !block_given && !super_call && !splat_args
	    && !block_declaration && positive_arity && mid == current_mid
	    && recv == NULL) {

	Function *f = bb->getParent();
	const unsigned long argc = args == NULL ? 0 : args->nd_alen;

	if (f->arg_size() - 2 == argc) {
	    // We check a global variable that we initialize to 0 in order
	    // to verify that the current method did not overwrite itself.
	    // This can happen. In this case, we do a normal dispatch.
	    SEL sel = mid_to_sel(mid, positive_arity ? 1 : 0);
	    GlobalVariable *gvar = GET_CORE()->redefined_op_gvar(sel, true);
	    new StoreInst(ConstantInt::get(Type::getInt8Ty(context), 0), gvar,
		    f->getEntryBlock().getFirstNonPHI());
	    Value *isNotRedefined = new ICmpInst(*bb, ICmpInst::ICMP_EQ,
		    new LoadInst(gvar, "", bb), ConstantInt::get(Int8Ty, 0));

	    BasicBlock *thenBB = BasicBlock::Create(context, "op_not_redef", f);
	    BasicBlock *elseBB = BasicBlock::Create(context, "op_dispatch", f);
	    BasicBlock *mergeBB = BasicBlock::Create(context, "op_merge", f);

	    BranchInst::Create(thenBB, elseBB, isNotRedefined, bb);

	    // Compile optimized recursive call.
	    bb = thenBB;
	    std::vector<Value *> params;
	    Function::arg_iterator arg = f->arg_begin();
	    params.push_back(arg++); // self
	    params.push_back(arg++); // sel 
	    for (NODE *n = args; n != NULL; n = n->nd_next) {
		params.push_back(compile_node(n->nd_head));
	    }
	    CallInst *inst = CallInst::Create(f, params.begin(), params.end(),
		    "", bb);
	    inst->setTailCall(true); // Promote for tail call elimitation.
	    Value *optz_value = cast<Value>(inst);
	    thenBB = bb;
	    BranchInst::Create(mergeBB, bb);

	    // Compile regular dispatch call.
	    bb = elseBB;
	    Value *unoptz_value = compile_call(node, false);
	    elseBB = bb;
	    BranchInst::Create(mergeBB, bb);

	    bb = mergeBB;
	    PHINode *pn = PHINode::Create(RubyObjTy, "", bb);
	    pn->addIncoming(optz_value, thenBB);
	    pn->addIncoming(unoptz_value, elseBB);
	    return pn;
	}
    }

    // Let's set the block state as NULL temporarily, when we
    // compile the receiver and the arguments. 
    Function *old_current_block_func = current_block_func;
    NODE *old_current_block_node = current_block_node;
    current_block_func = NULL;
    current_block_node = NULL;

    // Prepare the dispatcher parameters.
    std::vector<Value *> params;

    // Prepare the selector.
    Value *sel_val;
    SEL sel;
    if (mid != 0) {
	sel = mid_to_sel(mid, positive_arity ? 1 : 0);
	sel_val = compile_sel(sel);
	if (block_declaration && super_call) {
	    // A super call inside a block. The selector cannot
	    // be determined at compilation time, but at runtime:
	    //
	    //  VALUE my_block(VALUE rcv, SEL sel, ...)
	    //	// ...
	    //	SEL super_sel = sel;
	    //  if (super_sel == 0)
	    //	    super_sel = <hardcoded-mid>;
	    //	rb_vm_dispatch(..., super_sel, ...);
	    Function *f = bb->getParent();
	    Function::arg_iterator arg = f->arg_begin();
	    arg++; // skip self
	    Value *dyn_sel = arg;
	    Value *is_null = new ICmpInst(*bb, ICmpInst::ICMP_EQ, dyn_sel,
		    compile_const_pointer(NULL));
	    sel_val = SelectInst::Create(is_null, sel_val, dyn_sel, "", bb);
	}
    }	
    else {
	assert(super_call);
	// A super call outside a method definition. Compile a
	// null selector, the runtime will raise an exception.
	sel = 0;
	sel_val = compile_const_pointer(NULL);
    }

    // Top.
    params.push_back(current_self);

    // Self.
    params.push_back(recv == NULL ? current_self : compile_node(recv));

    // Selector.
    params.push_back(sel_val);

    // RubySpec requires that we compile the block *after* the arguments, so we
    // do pass NULL as the block for the moment.
    params.push_back(compile_const_pointer(NULL));
    NODE *real_args = args;
    if (real_args != NULL && nd_type(real_args) == NODE_BLOCK_PASS) {
	real_args = args->nd_head;
    }

    // Call option.
    unsigned char call_opt = 0;
    if (super_call) {
	call_opt |= DISPATCH_SUPER; 
    }
    else if (nd_type(node) == NODE_VCALL) {
	call_opt |= DISPATCH_VCALL;
    }
    else if (nd_type(node) == NODE_FCALL) {
	call_opt |= DISPATCH_FCALL;
    }
    params.push_back(ConstantInt::get(Int8Ty, call_opt));

    // Arguments.
    int argc = 0;
    if (nd_type(node) == NODE_ZSUPER) {
	Function::ArgumentListType &fargs =
	    current_non_block_func->getArgumentList();
	const int fargs_arity = fargs.size() - 2;
	params.push_back(ConstantInt::get(Int32Ty, fargs_arity));
	Function::ArgumentListType::iterator iter = fargs.begin();
	iter++; // skip self
	iter++; // skip sel
	const int rest_pos = current_arity.max == -1
	    ? (current_arity.left_req
		    + (current_arity.real - current_arity.min - 1))
	    : -1;
	int i = 0;
	while (iter != fargs.end()) {
	    if (i == rest_pos) {
		params.push_back(splatArgFollowsVal);
		splat_args = true;
	    }

	    // We can't simply push the direct argument address
	    // because if may have a default value.
	    ID argid = rb_intern(iter->getName().data());
	    Value *argslot;
	    if (block_declaration) {
		if (std::find(dvars.begin(), dvars.end(), argid)
			== dvars.end()) {
		    // Dvar does not exist yet, so we create it
		    // on demand!
		    dvars.push_back(argid);
		}
		argslot = compile_dvar_slot(argid);			
	    }
	    else {
		argslot = compile_lvar_slot(argid);
	    }

	    params.push_back(new LoadInst(argslot, "", bb));

	    ++i;
	    ++iter;
	}
	argc = fargs_arity;
    }
    else if (real_args != NULL) {
	std::vector<Value *> arguments;
	compile_dispatch_arguments(real_args, arguments, &argc);
	params.push_back(ConstantInt::get(Int32Ty, argc));
	for (std::vector<Value *>::iterator i = arguments.begin();
		i != arguments.end(); ++i) {
	    params.push_back(*i);
	}
    }
    else {
	params.push_back(ConstantInt::get(Int32Ty, 0));
    }

    // In case we have splat args, modify the call option.
    if (splat_args) {
	call_opt |= DISPATCH_SPLAT;
	params[4] = ConstantInt::get(Int8Ty, call_opt);
    }

    // Restore the block state.
    current_block_func = old_current_block_func;
    current_block_node = old_current_block_node;

    // Now compile the block and insert it in the params list!
    Value *blockVal;
    if (args != NULL && nd_type(args) == NODE_BLOCK_PASS) {
	assert(!block_given);
	assert(args->nd_body != NULL);
	blockVal = compile_block_get(compile_node(args->nd_body));
    }
    else {
	if (block_given) {
	    blockVal = compile_prepare_block();
	}
	else if (nd_type(node) == NODE_SUPER || nd_type(node) == NODE_ZSUPER) {
	    if (current_block_arg != NULL) {
		blockVal = compile_block_get(current_block_arg);
	    }
	    else {
		if (currentBlockFunc == NULL) {
		    currentBlockFunc = cast<Function>(
			    module->getOrInsertFunction(
				"rb_vm_current_block", PtrTy, NULL));
		}
		blockVal = CallInst::Create(currentBlockFunc, "", bb);
	    }
	}
	else {
	    blockVal = compile_const_pointer(NULL);
	}
    }
    params[3] = blockVal;

    // If we are calling a method that needs a reference to the current outer,
    // compile a reference to it.
    if (!super_call
	    && (sel == selEval
		    || sel == selInstanceEval
		    || sel == selClassEval
		    || sel == selModuleEval
		    || sel == selNesting
		    || sel == selConstants
		    || sel == selBinding)) {
	if (current_mid != 0) {
	    outer_stack_uses = true;
	}
	compile_set_current_outer();
    }

    // If we are calling a method that needs a top-level binding object, let's
    // create it. (Note: this won't work if the method is aliased, but we can
    // live with that for now)
    if (debug_mode
	    || (!super_call
		&& (sel == selEval
		    || sel == selInstanceEval
		    || sel == selClassEval
		    || sel == selModuleEval
		    || sel == selLocalVariables
		    || sel == selBinding))) {
	compile_binding();
    }
    else {
	can_interpret = true;
    }

    // Can we optimize the call?
    if (!super_call && !splat_args) {
	Value *opt_call = compile_optimized_dispatch_call(sel, argc, params);
	if (opt_call != NULL) {
	    can_interpret = false;
	    return opt_call;
	}
    }

    // Looks like we can't, just do a regular dispatch then.
    return compile_dispatch_call(params);
}

Value *
RoxorCompiler::compile_yield(NODE *node)
{
    std::vector<Value *> args;
    int argc = 0;
    if (node->nd_head != NULL) {
	compile_dispatch_arguments(node->nd_head, args, &argc);
    }
    Value *argv = recompile_dispatch_argv(args, 0);

    std::vector<Value *> params;
    params.push_back(ConstantInt::get(Int32Ty, argc));
    unsigned char opt = 0;
    if (argc < (int)args.size()) {
	opt |= DISPATCH_SPLAT;
    }
    params.push_back(ConstantInt::get(Int8Ty, opt));
    params.push_back(argv);    

    Instruction *val = compile_protected_call(yieldFunc, params);
    attach_current_line_metadata(val);

    Value *broken = CallInst::Create(getBrokenFunc, "", bb);
    Value *is_broken = new ICmpInst(*bb, ICmpInst::ICMP_NE, broken, undefVal);

    Function *f = bb->getParent();
    BasicBlock *broken_bb = BasicBlock::Create(context, "broken", f);
    BasicBlock *next_bb = BasicBlock::Create(context, "next", f);

    BranchInst::Create(broken_bb, next_bb, is_broken, bb);

    bb = broken_bb;
    ReturnInst::Create(context, broken, bb);

    bb = next_bb;
    return val;
}

inline Value *
RoxorCompiler::compile_node0(NODE *node)
{
    switch (nd_type(node)) {
	case NODE_SCOPE:
	    can_interpret = true;
	    return cast<Value>(compile_scope(node));

	case NODE_DVAR:
	case NODE_LVAR:
	    assert(node->nd_vid > 0);
	    return new LoadInst(compile_lvar_slot(node->nd_vid), "", bb);

	case NODE_GVAR:
	    return compile_gvar_get(node);

	case NODE_GASGN:
	    assert(node->nd_value != NULL);
	    return compile_gvar_assignment(node, compile_node(node->nd_value));

	case NODE_CVAR:
	    assert(node->nd_vid > 0);
	    return compile_cvar_get(node->nd_vid, true);

	case NODE_CVASGN:
	    assert(node->nd_vid > 0);
	    assert(node->nd_value != NULL);
	    return compile_cvar_assignment(node->nd_vid,
		    compile_node(node->nd_value));

	case NODE_MASGN:
	    return compile_multiple_assignment(node);

	case NODE_DASGN:
	case NODE_DASGN_CURR:
	    assert(node->nd_vid > 0);
	    assert(node->nd_value != NULL);
	    return compile_dvar_assignment(node->nd_vid,
		    compile_node(node->nd_value));

	case NODE_LASGN:
	    assert(node->nd_vid > 0);
	    assert(node->nd_value != NULL);
	    return compile_lvar_assignment(node->nd_vid,
		    compile_node(node->nd_value));

	case NODE_OP_ASGN_OR:
	    {
		assert(node->nd_recv != NULL);
		assert(node->nd_value != NULL);

		Value *recvVal;
		if (nd_type(node->nd_recv) == NODE_CVAR) {
		    // @@foo ||= 42
		    // We need to compile the class variable retrieve to not
		    // raise an exception in case the variable has never been
		    // defined yet.
		    assert(node->nd_recv->nd_vid > 0);
		    recvVal = compile_cvar_get(node->nd_recv->nd_vid, false);
		}
		else {
		    recvVal = compile_node(node->nd_recv);
		}


		Value *falseCond = new ICmpInst(*bb, ICmpInst::ICMP_EQ,
			recvVal, falseVal);

		Function *f = bb->getParent();

		BasicBlock *falseBB = BasicBlock::Create(context, "", f);
		BasicBlock *elseBB  = BasicBlock::Create(context, "", f);
		BasicBlock *trueBB = BasicBlock::Create(context, "", f);
		BasicBlock *mergeBB = BasicBlock::Create(context, "", f);

		BranchInst::Create(falseBB, trueBB, falseCond, bb);

		bb = trueBB;
		Value *nilCond = new ICmpInst(*bb, ICmpInst::ICMP_EQ, recvVal,
			nilVal);
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

		BasicBlock *notNilBB = BasicBlock::Create(context, "", f);
		BasicBlock *elseBB  = BasicBlock::Create(context, "", f);
		BasicBlock *mergeBB = BasicBlock::Create(context, "", f);

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
		params.push_back(current_self);
		params.push_back(recv);
		params.push_back(compile_sel(sel));
		params.push_back(compile_const_pointer(NULL));

		int argc = 0;
		std::vector<Value *> arguments;
		if (nd_type(node) == NODE_OP_ASGN1) {
		    assert(node->nd_args->nd_head != NULL);
		    compile_dispatch_arguments(node->nd_args->nd_head,
			    arguments,
			    &argc);
		}

		unsigned char opt = 0;
		if (argc < (int)arguments.size()) {
		    opt |= DISPATCH_SPLAT;
		}
		params.push_back(ConstantInt::get(Int8Ty, opt));

		params.push_back(ConstantInt::get(Int32Ty, argc));
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
		    ? node->nd_args->nd_body : node->nd_value;
		assert(value != NULL);
		if (type == 0 || type == 1) {
		    // 0 means OR, 1 means AND
		    Function *f = bb->getParent();

		    touchedBB = BasicBlock::Create(context, "", f);
		    untouchedBB  = BasicBlock::Create(context, "", f);
		    mergeBB = BasicBlock::Create(context, "merge", f);

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
		    params.clear();
		    params.push_back(current_self);
		    params.push_back(tmp);
		    params.push_back(compile_sel(sel));
		    params.push_back(compile_const_pointer(NULL));
		    params.push_back(ConstantInt::get(Int8Ty, 0));
		    params.push_back(ConstantInt::get(Int32Ty, 1));
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
		params.clear();
		params.push_back(current_self);
		params.push_back(recv);
		params.push_back(compile_sel(sel));
		params.push_back(compile_const_pointer(NULL));
		params.push_back(ConstantInt::get(Int8Ty, 0));
		argc++;
		params.push_back(ConstantInt::get(Int32Ty, argc));
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

		// compile_dispatch_call can create a new BasicBlock
		// so we have to get bb just after
		touchedBB = bb;

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
		    str = compile_literal(node->nd_lit);
		}

		std::vector<Value *> params;
		params.push_back(current_self);
		params.push_back(current_self);
		params.push_back(compile_sel(selBackquote));
		params.push_back(compile_const_pointer(NULL));
		params.push_back(ConstantInt::get(Int8Ty, DISPATCH_FCALL));
		params.push_back(ConstantInt::get(Int32Ty, 1));
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
				RubyObjTy, RubyObjTy, Int32Ty, NULL));
		}

		Value *args[] = {
		    val,
		    ConstantInt::get(Int32Ty, flag)
		};
		return compile_protected_call(newRegexpFunc, args, args + 2);
	    }
	    break;

	case NODE_DSYM:
	    {
		Value *val = compile_dstr(node);

		if (strInternFunc == NULL) {
		    strInternFunc = cast<Function>(module->getOrInsertFunction(
				"rb_str_intern_fast",
				RubyObjTy, RubyObjTy, NULL));
		}

		Value *args[] = { val };
		return compile_protected_call(strInternFunc, args, args + 1);
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

		BasicBlock *leftNotFalseBB = BasicBlock::Create(context,
			"left_not_false", f);
		BasicBlock *leftNotTrueBB = BasicBlock::Create(context,
			"left_not_true", f);
		BasicBlock *leftTrueBB = BasicBlock::Create(context,
			"left_is_true", f);
		BasicBlock *rightNotFalseBB = BasicBlock::Create(context,
			"right_not_false", f);
		BasicBlock *rightTrueBB = BasicBlock::Create(context,
			"right_is_true", f);
		BasicBlock *failBB = BasicBlock::Create(context, "fail", f);
		BasicBlock *mergeBB = BasicBlock::Create(context, "merge", f);

		Value *leftVal = compile_node(left);
		Value *leftNotFalseCond = new ICmpInst(*bb, ICmpInst::ICMP_NE,
			leftVal, falseVal);
		BranchInst::Create(leftNotFalseBB, leftNotTrueBB,
			leftNotFalseCond, bb);

		bb = leftNotFalseBB;
		Value *leftNotNilCond = new ICmpInst(*bb, ICmpInst::ICMP_NE,
			leftVal, nilVal);
		BranchInst::Create(leftTrueBB, leftNotTrueBB, leftNotNilCond,
			bb);

		bb = leftNotTrueBB;
		Value *rightVal = compile_node(right);
		Value *rightNotFalseCond = new ICmpInst(*bb, ICmpInst::ICMP_NE,
			rightVal, falseVal);
		BranchInst::Create(rightNotFalseBB, failBB, rightNotFalseCond,
			bb);

		bb = rightNotFalseBB;
		Value *rightNotNilCond = new ICmpInst(*bb, ICmpInst::ICMP_NE,
			rightVal, nilVal);
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

		BasicBlock *leftNotFalseBB = BasicBlock::Create(context,
			"left_not_false", f);
		BasicBlock *leftTrueBB = BasicBlock::Create(context,
			"left_is_true", f);
		BasicBlock *rightNotFalseBB = BasicBlock::Create(context,
			"right_not_false", f);
		BasicBlock *leftFailBB = BasicBlock::Create(context,
			"left_fail", f);
		BasicBlock *rightFailBB = BasicBlock::Create(context,
			"right_fail", f);
		BasicBlock *successBB = BasicBlock::Create(context, "success",
			f);
		BasicBlock *mergeBB = BasicBlock::Create(context, "merge", f);

		Value *leftVal = compile_node(left);
		Value *leftNotFalseCond = new ICmpInst(*bb, ICmpInst::ICMP_NE,
			leftVal, falseVal);
		BranchInst::Create(leftNotFalseBB, leftFailBB,
			leftNotFalseCond, bb);

		bb = leftNotFalseBB;
		Value *leftNotNilCond = new ICmpInst(*bb, ICmpInst::ICMP_NE,
			leftVal, nilVal);
		BranchInst::Create(leftTrueBB, leftFailBB, leftNotNilCond, bb);

		bb = leftTrueBB;
		Value *rightVal = compile_node(right);
		Value *rightNotFalseCond = new ICmpInst(*bb, ICmpInst::ICMP_NE,
			rightVal, falseVal);

		BranchInst::Create(rightNotFalseBB, rightFailBB, rightNotFalseCond, bb);

		bb = rightNotFalseBB;
		Value *rightNotNilCond = new ICmpInst(*bb, ICmpInst::ICMP_NE,
			rightVal, nilVal);
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

		BasicBlock *thenBB = BasicBlock::Create(context, "then", f);
		BasicBlock *elseBB  = BasicBlock::Create(context, "else", f);
		BasicBlock *mergeBB = BasicBlock::Create(context, "merge", f);

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

		Value *classVal = NULL;
		if (nd_type(node) == NODE_SCLASS) {
		    classVal =
			compile_singleton_class(compile_node(node->nd_recv));
		}
		else {
		    assert(node->nd_cpath->nd_mid > 0);
		    ID path = node->nd_cpath->nd_mid;

		    NODE *super = node->nd_super;

		    if (defineClassFunc == NULL) {
			// VALUE rb_vm_define_class(ID path, VALUE outer,
			//	VALUE super, int flags,
			//	unsigned char dynamic_class, rb_vm_outer_t *outer_stack);
			defineClassFunc = cast<Function>(
				module->getOrInsertFunction(
				    "rb_vm_define_class",
				    RubyObjTy, IntTy, RubyObjTy, RubyObjTy,
				    Int32Ty, Int8Ty, PtrTy, NULL));
		    }

		    int flags = 0;
		    Value *cpath = compile_class_path(node->nd_cpath, &flags);
		    if (nd_type(node) == NODE_MODULE) {
			flags |= DEFINE_MODULE;
		    }

		    Value *args[] = {
			compile_id(path),
			cpath,
			super == NULL ? zeroVal : compile_node(super),
			ConstantInt::get(Int32Ty, flags),
			ConstantInt::get(Int8Ty,
				(flags & DEFINE_OUTER) && dynamic_class
				? 1 : 0),
				compile_outer_stack()
		    };
		    Instruction *insn = compile_protected_call(defineClassFunc,
			args, args + 6);
		    attach_current_line_metadata(insn);
		    classVal = insn;
		}

		NODE *body = node->nd_body;
		if (body != NULL) {
		    assert(nd_type(body) == NODE_SCOPE);
		    if (body->nd_body != NULL) {	
			Value *old_self = current_self;
			current_self = classVal;

			GlobalVariable *old_class = current_opened_class;
			current_opened_class = new GlobalVariable(
				*RoxorCompiler::module, RubyObjTy, false,
				GlobalValue::InternalLinkage, nilVal, "");

			bool old_current_block_chain = current_block_chain;
			bool old_dynamic_class = dynamic_class;

			GlobalVariable *old_outer_stack = outer_stack;
			bool old_outer_stack_uses = outer_stack_uses;
			compile_push_outer(classVal);

			current_block_chain = false;
			dynamic_class = false;

			new StoreInst(classVal, current_opened_class, bb);

			compile_set_current_scope(classVal, publicScope);

			bool old_block_declaration = block_declaration;
			block_declaration = false;

			std::map<ID, void *> old_ivars_slots_cache
			    = ivars_slots_cache;
			old_ivars_slots_cache.clear();

			std::vector<ID> old_dvars = dvars;
			dvars.clear();

			// Compile the scope.
			DEBUG_LEVEL_INC();
			Value *val = compile_node(body);
			assert(Function::classof(val));
			Function *f = cast<Function>(val);
			GET_CORE()->optimize(f);
			DEBUG_LEVEL_DEC();

			dvars = old_dvars;

			ivars_slots_cache = old_ivars_slots_cache;
			block_declaration = old_block_declaration;

			// Create a rescue block for the module scope function,
			// since it might raise an exception and we do want
			// to properly restore the context information.
			Function *main_f = bb->getParent(); 
			BasicBlock *old_rescue_invoke_bb = rescue_invoke_bb;
			BasicBlock *new_rescue_invoke_bb =
			    BasicBlock::Create(context, "rescue", main_f);
			rescue_invoke_bb = new_rescue_invoke_bb;

			// Run the scope.
			std::vector<Value *> params;
			params.push_back(classVal);
			params.push_back(compile_const_pointer(NULL));
			val = compile_protected_call(f, params);
			BasicBlock *normal_bb = bb;

			// The rescue block - restore context before
			// propagating the exception.
			bb = new_rescue_invoke_bb;
			compile_landing_pad_header();
			compile_pop_outer(!outer_stack_uses);
			compile_set_current_scope(classVal, defaultScope);
			compile_rethrow_exception();

			// The normal block - restore context.
			bb = normal_bb;
			compile_pop_outer(!outer_stack_uses);
			compile_set_current_scope(classVal, defaultScope);

			outer_stack_uses = old_outer_stack_uses;
			outer_stack = old_outer_stack;
			dynamic_class = old_dynamic_class;
			current_self = old_self;
			current_opened_class = old_class;
			current_block_chain = old_current_block_chain;
			rescue_invoke_bb = old_rescue_invoke_bb;

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
	    return compile_call(node);

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
	    if (current_mid != 0) {
		outer_stack_uses = true;
	    }
	    return compile_const(node->nd_vid, NULL);

	case NODE_CDECL:
	    assert(node->nd_value != NULL);
	    return compile_constant_declaration(node,
		    compile_node(node->nd_value));

	case NODE_IASGN:
	case NODE_IASGN2:
	    assert(node->nd_vid > 0);
	    assert(node->nd_value != NULL);
	    return compile_ivar_assignment(node->nd_vid,
		    compile_node(node->nd_value));

	case NODE_IVAR:
	    assert(node->nd_vid > 0);
	    return compile_ivar_get(node->nd_vid);

	case NODE_LIT:
	    can_interpret = true;
	case NODE_STR:
	    assert(node->nd_lit != 0);
	    return compile_literal(node->nd_lit);

	case NODE_ARGSCAT:
	case NODE_ARGSPUSH:
	    {
		assert(node->nd_head != NULL);
		Value *ary = compile_node(node->nd_head);
		Value *args1[] = { ary };
		ary = compile_protected_call(dupArrayFunc, args1, args1 + 1);

		assert(node->nd_body != NULL);
		Value *other = compile_node(node->nd_body);
		Value *args2[] = { ary, other };
		return compile_protected_call(catArrayFunc, args2, args2 + 2);
	    }
	    break;

	case NODE_SPLAT:
	    {
		assert(node->nd_head != NULL);
		Value *val = compile_node(node->nd_head);

		if (nd_type(node->nd_head) != NODE_ARRAY) {
		    Value *args[] = { val };
		    val = compile_protected_call(toAFunc, args, args + 1);
		}

		return val;
	    }
	    break;

	case NODE_ARRAY:
	case NODE_ZARRAY:
	case NODE_VALUES:
	    {
		int count = 0;
		std::vector<Value *> elems;
		if (nd_type(node) != NODE_ZARRAY) {
		    count = node->nd_alen;
		    NODE *n = node;
		    for (int i = 0; i < count; i++) {
			assert(n->nd_head != NULL);
			Value *elem = compile_node(n->nd_head);
			elems.push_back(elem);
			n = n->nd_next;
		    }
		}

		Value *argv = compile_argv_buffer(count);
		for (int i = 0; i < count; i++) {
		    Value *idx = ConstantInt::get(Int32Ty, i);
		    Value *slot = GetElementPtrInst::Create(argv, idx, "", bb);
		    new StoreInst(elems[i], slot, bb);
		}

		Value *args[] = {
		    ConstantInt::get(Int32Ty, count),
		    argv
		};
		return CallInst::Create(newArrayFunc, args, args + 2, "", bb);
	    }
	    break;

	case NODE_HASH:
	    {
		Value *hash = CallInst::Create(newHashFunc, "", bb);

		if (node->nd_head != NULL) {
		    assert(nd_type(node->nd_head) == NODE_ARRAY);
		    const int count = node->nd_head->nd_alen;
		    assert(count % 2 == 0);
		    NODE *n = node->nd_head;

		    for (int i = 0; i < count; i += 2) {
			Value *key = compile_node(n->nd_head);
			n = n->nd_next;
			Value *val = compile_node(n->nd_head);
			n = n->nd_next;

			Value *args[] = {
			    hash,
			    key,
			    val
			};
			CallInst::Create(storeHashFunc, args, args + 3, "", bb);
		    }
		}

		return hash;
	    }
	    break;

	case NODE_DOT2:
	case NODE_DOT3:
	    {
		assert(node->nd_beg != NULL);
		assert(node->nd_end != NULL);
		Value *beg = compile_node(node->nd_beg);
		Value *end = compile_node(node->nd_end);
		return compile_range(beg, end, nd_type(node) == NODE_DOT3);
	    }
	    break;

	case NODE_FLIP2:
	case NODE_FLIP3:
	    assert(node->nd_beg != NULL);
	    assert(node->nd_end != NULL);

	    if (nd_type(node) == NODE_FLIP2) {
		return compile_ff2(node);
	    }
	    return compile_ff3(node);

	case NODE_BLOCK:
	    {
		can_interpret = true;
		NODE *n = node;
		Value *val = NULL;

		DEBUG_LEVEL_INC();
		while (n != NULL && nd_type(n) == NODE_BLOCK) {
		    val = n->nd_head == NULL
			? nilVal : compile_node(n->nd_head);
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
		params.push_back(current_self);
		params.push_back(reTarget);
		params.push_back(compile_sel(selEqTilde));
		params.push_back(compile_const_pointer(NULL));
		params.push_back(ConstantInt::get(Int8Ty, 0));
		params.push_back(ConstantInt::get(Int32Ty, 1));
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
		    valiasFunc = cast<Function>(module->getOrInsertFunction(
				"rb_alias_variable",
				VoidTy, IntTy, IntTy, NULL));
		}

		assert(node->u1.id > 0 && node->u2.id > 0);

		Value *args[] = {
		    compile_id(node->u1.id),
		    compile_id(node->u2.id)
		};
		CallInst::Create(valiasFunc, args, args + 2, "", bb);

		return nilVal;
	    }
	    break;

	case NODE_ALIAS:
	    {
		if (aliasFunc == NULL) {
		    // void rb_vm_alias2(VALUE outer, VALUE from_sym,
		    //		VALUE to_sym, unsigned char dynamic_class);
		    aliasFunc = cast<Function>(module->getOrInsertFunction(
				"rb_vm_alias2",
				VoidTy, RubyObjTy, IntTy, IntTy, Int8Ty,
				NULL));
		}

		assert(node->u1.node != NULL);
		assert(node->u2.node != NULL);

		Value *args[] = {
		    compile_current_class(),
		    compile_node(node->u1.node),
		    compile_node(node->u2.node),
		    ConstantInt::get(Int8Ty, dynamic_class ? 1 : 0)
		};
		compile_protected_call(aliasFunc, args, args + 4);

		return nilVal;
	    }
	    break;

	case NODE_DEFINED:
	    assert(node->nd_head != NULL);
	    return compile_defined_expression(node->nd_head);

	case NODE_DEFN:
	case NODE_DEFS:
	    compile_method_definition(node);
	    return nilVal;

	case NODE_UNDEF:
	    {
		if (undefFunc == NULL) {
		    // VALUE rb_vm_undef2(VALUE klass, VALUE sym,
		    //	unsigned char dynamic_class);
		    undefFunc =
			cast<Function>(module->getOrInsertFunction(
				"rb_vm_undef2",
				VoidTy, RubyObjTy, RubyObjTy, Int8Ty, NULL));
		}

		assert(node->u2.node != NULL);
		Value *args[] = {
		    compile_current_class(),
		    compile_node(node->u2.node),
		    ConstantInt::get(Int8Ty, dynamic_class ? 1 : 0)
		};
		compile_protected_call(undefFunc, args, args + 3);

		return nilVal;
	    }
	    break;

	case NODE_TRUE:
	    can_interpret = true;
	    return trueVal;

	case NODE_FALSE:
	    can_interpret = true;
	    return falseVal;

	case NODE_NIL:
	    can_interpret = true;
	    return nilVal;

	case NODE_SELF:
	    return current_self;

	case NODE_NTH_REF:
	    if (getBackrefNth == NULL) {
		getBackrefNth = cast<Function>(
			module->getOrInsertFunction("rb_backref_nth_get",
			    RubyObjTy, Int32Ty, NULL));
	    }
	    return CallInst::Create(getBackrefNth,
		    ConstantInt::get(Int32Ty, node->nd_nth), "", bb);

	case NODE_BACK_REF:
	    if (getBackrefSpecial == NULL) {
		getBackrefSpecial = cast<Function>(
			module->getOrInsertFunction("rb_backref_special_get",
			    RubyObjTy, Int32Ty, NULL));
	    }
	    return CallInst::Create(getBackrefSpecial,
		    ConstantInt::get(Int32Ty, node->nd_nth), "", bb);

	case NODE_BEGIN:
	    can_interpret = true;
	    return node->nd_body == NULL
		? nilVal : compile_node(node->nd_body);

	case NODE_RESCUE:
	    {
		assert(node->nd_head != NULL);
		assert(node->nd_resq != NULL);

		Function *f = bb->getParent();

		BasicBlock *old_begin_bb = begin_bb;
		begin_bb = BasicBlock::Create(context, "begin", f);

		BasicBlock *old_rescue_invoke_bb = rescue_invoke_bb;
		BasicBlock *old_rescue_rethrow_bb = rescue_rethrow_bb;
		BasicBlock *new_rescue_invoke_bb =
		    BasicBlock::Create(context, "rescue", f);
		BasicBlock *new_rescue_rethrow_bb =
		    BasicBlock::Create(context, "rescue.rethrow", f);
		BasicBlock *merge_bb = BasicBlock::Create(context, "merge", f);

		// Begin code.
		BranchInst::Create(begin_bb, bb);
		bb = begin_bb;
		rescue_invoke_bb = new_rescue_invoke_bb;
		rescue_rethrow_bb = new_rescue_rethrow_bb;
		Value *not_rescued_val = compile_node(node->nd_head);
		rescue_rethrow_bb = old_rescue_rethrow_bb;
		rescue_invoke_bb = old_rescue_invoke_bb;

		if (node->nd_else != NULL) {
		    BasicBlock *else_bb = BasicBlock::Create(context, "else",
			    f);
		    BranchInst::Create(else_bb, bb);
		    bb = else_bb;
		    not_rescued_val = compile_node(node->nd_else);
		}

		BasicBlock *not_rescued_bb = bb;
		BranchInst::Create(merge_bb, not_rescued_bb);

		PHINode *pn = PHINode::Create(RubyObjTy, "rescue_result",
			merge_bb);
		pn->addIncoming(not_rescued_val, not_rescued_bb);

		if (new_rescue_invoke_bb->use_empty()
			&& new_rescue_rethrow_bb->use_empty()) {
		    new_rescue_invoke_bb->eraseFromParent();
		    new_rescue_rethrow_bb->eraseFromParent();
		}
		else {
		    if (new_rescue_invoke_bb->use_empty()) {
			new_rescue_invoke_bb->eraseFromParent();
		    }
		    else {
			// Landing pad header.
			bb = new_rescue_invoke_bb;
			compile_landing_pad_header();
			BranchInst::Create(new_rescue_rethrow_bb, bb);
		    }

		    bb = new_rescue_rethrow_bb;

		    // Landing pad code.
		    bool old_current_rescue = current_rescue;
		    current_rescue = true;
		    Value *rescue_val = compile_node(node->nd_resq);
		    current_rescue = old_current_rescue;
		    new_rescue_invoke_bb = bb;

		    // Landing pad footer.
		    compile_landing_pad_footer();

		    BranchInst::Create(merge_bb, bb);
		    pn->addIncoming(rescue_val, new_rescue_invoke_bb);
		}

		bb = merge_bb;
		begin_bb = old_begin_bb;

		return pn;
	    }
	    break;

	case NODE_RESBODY:
	    {
		NODE *n = node;

		Function *f = bb->getParent();
		BasicBlock *merge_bb = BasicBlock::Create(context, "merge", f);
		BasicBlock *handler_bb = NULL;

		std::vector<std::pair<Value *, BasicBlock *> > handlers;

		while (n != NULL) {
		    std::vector<Value *> exceptions_to_catch;

		    if (n->nd_args == NULL) {
			// catch StandardError exceptions by default
			exceptions_to_catch.push_back(compile_standarderror());
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
			types.push_back(Int32Ty);
			FunctionType *ft = FunctionType::get(Int8Ty,
				types, true);
			isEHActiveFunc = cast<Function>(
				module->getOrInsertFunction(
				    "rb_vm_is_eh_active", ft));
		    }

		    const int size = exceptions_to_catch.size();
		    exceptions_to_catch.insert(exceptions_to_catch.begin(), 
			    ConstantInt::get(Int32Ty, size));

		    Value *handler_active = CallInst::Create(isEHActiveFunc, 
			    exceptions_to_catch.begin(), 
			    exceptions_to_catch.end(), "", bb);

		    Value *is_handler_active = new ICmpInst(*bb,
			    ICmpInst::ICMP_EQ, handler_active,
			    ConstantInt::get(Int8Ty, 1));
		    
		    handler_bb = BasicBlock::Create(context, "handler", f);
		    BasicBlock *next_handler_bb =
			BasicBlock::Create(context, "handler", f);

		    BranchInst::Create(handler_bb, next_handler_bb,
			    is_handler_active, bb);

		    bb = handler_bb;
		    assert(n->nd_body != NULL);

		    // Compile the rescue handler within another exception
		    // handler.
		    BasicBlock *old_rescue_invoke_bb = rescue_invoke_bb;
		    BasicBlock *new_rescue_invoke_bb =
			BasicBlock::Create(context, "rescue", f);
		    rescue_invoke_bb = new_rescue_invoke_bb;

		    Value *header_val = compile_node(n->nd_body);
		    handler_bb = bb;
		    BranchInst::Create(merge_bb, bb);
		    handlers.push_back(std::pair<Value *, BasicBlock *>
			    (header_val, handler_bb));

		    // If the handler raised an exception, pop the previous
		    // one from the VM stack and rethrow.
		    bb = new_rescue_invoke_bb;
		    compile_landing_pad_header();
#if ROXOR_COMPILER_DEBUG
		    printf("%s (%s:%d): Calling compile_pop_exception(1)...\n", __FUNCTION__, __FILE__, __LINE__);
#endif
		    compile_pop_exception(1);
		    compile_rethrow_exception();
		    rescue_invoke_bb = old_rescue_invoke_bb;

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
	    return compile_current_exception();

	case NODE_ENSURE:
	    {
		assert(node->nd_ensr != NULL);
		if (node->nd_head == NULL) {
		    compile_node(node->nd_ensr);
		    return nilVal;
		}

		Function *f = bb->getParent();
		BasicBlock *old_ensure_bb = ensure_bb;
		PHINode *old_ensure_pn = ensure_pn;
		// the ensure for when the block is left with a return
		BasicBlock *ensure_return_bb = BasicBlock::Create(context,
			"ensure.for.return", f);
		// the ensure for when the block is left without using return
		BasicBlock *ensure_normal_bb = BasicBlock::Create(context,
			"ensure.no.return", f);
		PHINode *new_ensure_pn = PHINode::Create(RubyObjTy,
			"ensure.phi", ensure_return_bb);
		ensure_pn = new_ensure_pn;

		ensure_bb = ensure_return_bb;

		BasicBlock *new_rescue_invoke_bb = BasicBlock::Create(context,
			"rescue", f);
		BasicBlock *new_rescue_rethrow_bb = BasicBlock::Create(context,
			"rescue.rethrow", f);
		BasicBlock *old_rescue_invoke_bb = rescue_invoke_bb;
		BasicBlock *old_rescue_rethrow_bb = rescue_rethrow_bb;

		Value *old_has_ensure =
		    compile_set_has_ensure(ConstantInt::get(Int8Ty, 1));

		NODE *old_ensure_node = ensure_node;
		ensure_node = node->nd_ensr;

		rescue_invoke_bb = new_rescue_invoke_bb;
		rescue_rethrow_bb = new_rescue_rethrow_bb;
		DEBUG_LEVEL_INC();
		Value *val = compile_node(node->nd_head);
		DEBUG_LEVEL_DEC();
		rescue_rethrow_bb = old_rescue_rethrow_bb;
		rescue_invoke_bb = old_rescue_invoke_bb;
		BranchInst::Create(ensure_normal_bb, bb);

		ensure_node = old_ensure_node;

		if (new_rescue_invoke_bb->use_empty()
			&& new_rescue_rethrow_bb->use_empty()) {
		    new_rescue_invoke_bb->eraseFromParent();
		    new_rescue_rethrow_bb->eraseFromParent();
		}
		else {
		    if (new_rescue_invoke_bb->use_empty()) {
			new_rescue_invoke_bb->eraseFromParent();
		    }
		    else {
			bb = new_rescue_invoke_bb;
			compile_landing_pad_header();
			BranchInst::Create(new_rescue_rethrow_bb, bb);
		    }
		    bb = new_rescue_rethrow_bb;
		    compile_set_has_ensure(old_has_ensure);
		    compile_node(node->nd_ensr);
		    compile_rethrow_exception();
		}

		ensure_bb = old_ensure_bb;
		ensure_pn = old_ensure_pn;

		if (new_ensure_pn->getNumIncomingValues() == 0) {
		    // there was no return in the block so we do not need
		    // to have an ensure block to return the value
		    new_ensure_pn->eraseFromParent();
		    ensure_return_bb->eraseFromParent();
		}
		else {
		    // some value was returned in the block so we have to
		    // make a version of the ensure that returns this value
		    bb = ensure_return_bb;
		    compile_set_has_ensure(old_has_ensure);
		    compile_node(node->nd_ensr);
		    // the return value is the PHINode from all the return
		    const bool within_loop = current_loop_begin_bb != NULL
			&& current_loop_body_bb != NULL
			&& current_loop_end_bb != NULL;
		    const bool within_block = block_declaration;
		    if (within_loop) {
			compile_break_within_loop(new_ensure_pn);
		    }
		    else if (within_block) {
			compile_break_within_block(new_ensure_pn);
		    }
		    else {
			compile_simple_return(new_ensure_pn);
		    }
		}

		// we also have to compile the ensure
		// for when the block was left without return
		bb = ensure_normal_bb;
		compile_set_has_ensure(old_has_ensure);
		compile_node(node->nd_ensr);

		return val;
	    }
	    break;

	case NODE_WHILE:
	case NODE_UNTIL:
	    {
		assert(node->nd_cond != NULL);

		Function *f = bb->getParent();

		BasicBlock *loopBB = BasicBlock::Create(context, "loop", f);
		BasicBlock *bodyBB = BasicBlock::Create(context, "body", f);
		BasicBlock *exitBB = BasicBlock::Create(context, "loop_exit",
			f);
		BasicBlock *afterBB = BasicBlock::Create(context, "after", f);

		const bool first_pass_free = node->nd_state == 0;

		BranchInst::Create(first_pass_free ? bodyBB : loopBB, bb);

		bb = loopBB;
		Value *condVal = compile_node(node->nd_cond);

		if (nd_type(node) == NODE_WHILE) {
		    compile_boolean_test(condVal, bodyBB, exitBB);
		}
		else {
		    compile_boolean_test(condVal, exitBB, bodyBB);
		}
		BranchInst::Create(afterBB, exitBB);

		BasicBlock *old_current_loop_begin_bb = current_loop_begin_bb;
		BasicBlock *old_current_loop_body_bb = current_loop_body_bb;
		BasicBlock *old_current_loop_end_bb = current_loop_end_bb;
		PHINode *old_current_loop_exit_val = current_loop_exit_val;
		NODE *old_ensure_node = ensure_node;
		BasicBlock *old_ensure_bb = ensure_bb;
		ensure_node = NULL;
		ensure_bb = NULL;

		current_loop_begin_bb = loopBB;
		current_loop_body_bb = bodyBB;
		current_loop_end_bb = afterBB;
		current_loop_exit_val = PHINode::Create(RubyObjTy,
			"loop_exit", afterBB);
		current_loop_exit_val->addIncoming(nilVal, exitBB);

		bb = bodyBB;
		if (node->nd_body != NULL) {
		    compile_node(node->nd_body);
		}
		bodyBB = bb;

		BranchInst::Create(loopBB, bb);

		bb = afterBB;

		Value *retval = current_loop_exit_val;

		ensure_bb = old_ensure_bb;
		ensure_node = old_ensure_node;
		current_loop_begin_bb = old_current_loop_begin_bb;
		current_loop_body_bb = old_current_loop_body_bb;
		current_loop_end_bb = old_current_loop_end_bb;
		current_loop_exit_val = old_current_loop_exit_val;

		return retval;
	    }
	    break;

	case NODE_FOR:
	case NODE_ITER:
	case NODE_LAMBDA:
	    return compile_block(node);

	case NODE_YIELD:
	    return compile_yield(node);

	case NODE_COLON2:
	    {
		assert(node->nd_mid > 0);
		if (rb_is_const_id(node->nd_mid)) {
		    // Constant.
		    assert(node->nd_head != NULL);
		    return compile_const(node->nd_mid,
			    compile_node(node->nd_head));
		}
		else {
		    // Method call.
		    abort(); // TODO
		}
	    }
	    break;

	case NODE_COLON3:
	    assert(node->nd_mid > 0);
	    return compile_const(node->nd_mid, compile_nsobject());

	case NODE_CASE:
	    {
		Function *f = bb->getParent();
		BasicBlock *caseMergeBB = BasicBlock::Create(context,
			"case_merge", f);

		PHINode *pn = PHINode::Create(RubyObjTy, "case_tmp",
			caseMergeBB);

		Value *comparedToVal = NULL;

		if (node->nd_head != NULL) {
		    comparedToVal = compile_node(node->nd_head);
                }

		NODE *subnode = node->nd_body;

		assert(subnode != NULL);
		assert(nd_type(subnode) == NODE_WHEN);
		while (subnode != NULL && nd_type(subnode) == NODE_WHEN) {
		    NODE *valueNode = subnode->nd_head;
		    assert(valueNode != NULL);

		    BasicBlock *thenBB = BasicBlock::Create(context, "then", f);

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

	case NODE_PRELUDE:
	    {
		assert(node->nd_head != NULL);
		compile_node(node->nd_head);

		if (node->nd_body != NULL) {
		    compile_node(node->nd_body);
		}

		return nilVal;
	    }

	case NODE_POSTEXE:
	    {
		assert(node->nd_body != NULL);

		Value *body = compile_node(node->nd_body);
		assert(Function::classof(body));

		Function *old_current_block_func = current_block_func;
		NODE *old_current_block_node = current_block_node;
		current_block_func = cast<Function>(body);
		current_block_node = node->nd_body;

		std::vector<Value *> params;
		SEL sel = sel_registerName("at_exit");
		params.push_back(current_self);
		params.push_back(compile_nsobject());
		params.push_back(compile_sel(sel));
		params.push_back(compile_prepare_block());
		params.push_back(ConstantInt::get(Int8Ty, DISPATCH_FCALL));
		params.push_back(ConstantInt::get(Int32Ty, 0));

		current_block_func = old_current_block_func;
		current_block_node = old_current_block_node;

		return compile_dispatch_call(params);
	    }
	    break;

	default:
	    compile_node_error("not implemented", node);
    }

    return NULL;
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
    if (current_line != nd_line(node)) {
	current_line = nd_line(node);
	if (debug_mode) {
	    compile_debug_trap();
	}
    }

    bool old_can_interpret = can_interpret;
    can_interpret = false;

    Value *val = compile_node0(node);

    if (!can_interpret) {
#if ROXOR_COMPILER_DEBUG
	printf("node %s can't be interpreted!\n",
		ruby_node_name(nd_type(node)));
#endif
	should_interpret = false;
    }
    can_interpret = old_can_interpret;

    return val;
}

#include <libgen.h>

void
RoxorCompiler::set_fname(const char *_fname)
{
    if (fname != _fname
	    && (fname == NULL || _fname == NULL
		|| strcmp(fname, _fname) != 0)) {
	fname = _fname;
	if (fname != NULL) {
	    // Compute complete path.
	    char path[PATH_MAX];
	    if (*_fname == '/') {
		strlcpy(path, _fname, sizeof path);
	    }
	    else {
		char cwd[PATH_MAX];
		getcwd(cwd, sizeof cwd);
		snprintf(path, sizeof path, "%s/%s", cwd, _fname);
	    }

	    // Split the path into 2 parts: the directory and the base.
	    char *dir = dirname(path);
	    char *base = basename(path);

	    // LLVM (llc) really doesn't like when you pass empty strings for
	    // these values and might later throw a cryptic C++ exception that
	    // will take hours to investigate. How fun.
	    assert(strlen(dir) > 0);
	    assert(strlen(base) > 0);

#if !defined(LLVM_TOT)
	    debug_info->createCompileUnit(DW_LANG_Ruby, base, dir,
		    RUBY_DESCRIPTION, true, "", 1);
	    debug_compile_unit = DICompileUnit(debug_info->getCU());
#else
	    debug_compile_unit = debug_info->CreateCompileUnit(DW_LANG_Ruby,
		    base, dir, RUBY_DESCRIPTION, false, false, "");
#endif
	}
    }
}

Function *
RoxorCompiler::compile_main_function(NODE *node, bool *can_interpret_p)
{
    save_compiler_state();
    reset_compiler_state();

    should_interpret = true;
    can_interpret = false;

    Value *val = compile_node(node);
    assert(Function::classof(val));
    Function *func =  cast<Function>(val);

    if (can_interpret_p != NULL) {
	*can_interpret_p = should_interpret;
    }
    restore_compiler_state();
    return func;
}

Function *
RoxorAOTCompiler::compile_main_function(NODE *node, bool *can_be_interpreted)
{
    Value *val = compile_node(node);
    assert(Function::classof(val));
    Function *func = cast<Function>(val);
    func->setLinkage(GlobalValue::ExternalLinkage);

    Function *init_func = compile_init_function(); 
    BasicBlock::InstListType &list = func->getEntryBlock().getInstList();
    list.insert(list.begin(), CallInst::Create(init_func, ""));

    return func;
}

Function *
RoxorAOTCompiler::compile_init_function(void)
{
    reset_compiler_state();

    FunctionType *ft = FunctionType::get(VoidTy, false);
    Function *f = Function::Create(ft, GlobalValue::InternalLinkage,
	    "init_func", module);

    bb = BasicBlock::Create(context, "MainBlock", f);

    // Compile constant caches.

    Function *getConstCacheFunc = cast<Function>(module->getOrInsertFunction(
		"rb_vm_get_constant_cache",
		PtrTy, PtrTy, NULL));

    for (std::map<ID, GlobalVariable *>::iterator i = ccaches.begin();
	    i != ccaches.end();
	    ++i) {

	ID name = i->first;
	GlobalVariable *gvar = i->second;

	Value *val = CallInst::Create(getConstCacheFunc,
		compile_const_global_string(rb_id2name(name)), "", bb);
	new StoreInst(val, gvar, bb);
    }

    // Compile selectors.

    Function *registerSelFunc = get_function("sel_registerName");

    for (std::map<SEL, GlobalVariable *>::iterator i = sels.begin();
	    i != sels.end();
	    ++i) {

	SEL sel = i->first;
	GlobalVariable *gvar = i->second;

	Value *val = CallInst::Create(registerSelFunc,
		compile_const_global_string(sel_getName(sel)), "", bb);
	val = new BitCastInst(val, PtrTy, "", bb);
	new StoreInst(val, gvar, bb);
    }

    // Compile literals.

    Function *name2symFunc =
	cast<Function>(module->getOrInsertFunction("rb_name2sym",
		    RubyObjTy, PtrTy, NULL));

    Function *newRegexp2Func =
	cast<Function>(module->getOrInsertFunction(
		    "rb_unicode_regex_new_retained",
		    RubyObjTy, PointerType::getUnqual(Int16Ty), Int32Ty,
		    Int32Ty, NULL));

    Function *newBignumFunc =
	cast<Function>(module->getOrInsertFunction("rb_bignum_new_retained",
		    RubyObjTy, PtrTy, NULL));

    Function *getClassFunc =
	cast<Function>(module->getOrInsertFunction("objc_getClass",
		    RubyObjTy, PtrTy, NULL));

    for (std::map<VALUE, GlobalVariable *>::iterator i = literals.begin();
	 i != literals.end();
	 ++i) {

	VALUE val = i->first;
	GlobalVariable *gvar = i->second;
	Value *lit_val = NULL;

	switch (TYPE(val)) {
	    case T_CLASS:
		{
		    // This strange literal seems to be only emitted for 
		    // `for' loops.
		    const char *cname = class_getName((Class)val);
		    lit_val = CallInst::Create(getClassFunc,
			    compile_const_global_string(cname), "", bb);
		}
		break;

	    case T_REGEXP:
		{
		    const UChar *chars = NULL;
		    int32_t chars_len = 0;

		    regexp_get_uchars(val, &chars, &chars_len);

		    Value *re_str;
		    if (chars_len == 0) {
			re_str = ConstantPointerNull::get(
				PointerType::getUnqual(Int16Ty));
		    }
		    else {
			re_str = compile_const_global_ustring(chars,
				chars_len);
		    }

		    Value *args[] = {
			re_str,
			ConstantInt::get(Int32Ty, chars_len),
			ConstantInt::get(Int32Ty, rb_reg_options(val))
		    };
		    lit_val = CallInst::Create(newRegexp2Func, args, args + 3,
			    "", bb);
		}
		break;

	    case T_SYMBOL:
		{
		    const char *symname = rb_id2name(SYM2ID(val));
		    lit_val = CallInst::Create(name2symFunc,
			    compile_const_global_string(symname), "", bb);
		}
		break;

	    case T_BIGNUM:
		{
		    const char *bigstr = RSTRING_PTR(rb_big2str(val, 10));
		    lit_val = CallInst::Create(newBignumFunc,
			    compile_const_global_string(bigstr), "", bb);
		}
		break;

	    default:
		if (rb_obj_is_kind_of(val, rb_cRange)) {
		    VALUE beg = 0, end = 0;
		    bool exclude_end = false;
		    rb_range_extract(val, &beg, &end, &exclude_end);

		    lit_val = compile_range(ConstantInt::get(RubyObjTy, beg),
			    ConstantInt::get(RubyObjTy, end),
			    exclude_end, true);	
		}
		else {
		    printf("unrecognized literal `%s' (class `%s' type %d)\n",
			    RSTRING_PTR(rb_inspect(val)),
			    rb_obj_classname(val),
			    TYPE(val));
		    abort();
		}
		break;
	}

	assert(lit_val != NULL);
	new StoreInst(lit_val, gvar, bb);
    }

    // Compile IDs.

    Function *rbInternFunc = cast<Function>(module->getOrInsertFunction(
		"rb_intern",
		IntTy, PtrTy, NULL));

    for (std::map<ID, GlobalVariable *>::iterator i = ids.begin();
	    i != ids.end();
	    ++i) {

	ID name = i->first;
	GlobalVariable *gvar = i->second;

	Value *val = CallInst::Create(rbInternFunc,
		compile_const_global_string(rb_id2name(name)), "", bb);
	new StoreInst(val, gvar, bb);
    }

    // Compile global entries.

    Function *globalEntryFunc = cast<Function>(module->getOrInsertFunction(
		"rb_global_entry",
		PtrTy, IntTy, NULL));

    for (std::map<ID, GlobalVariable *>::iterator i = global_entries.begin();
	    i != global_entries.end();
	    ++i) {

	ID name = i->first;
	GlobalVariable *gvar = i->second;

	Value *val = CallInst::Create(rbInternFunc,
		compile_const_global_string(rb_id2name(name)), "", bb);
	val = CallInst::Create(globalEntryFunc, val, "", bb);
	new StoreInst(val, gvar, bb);
    }

    // Compile constant class references.

    for (std::vector<GlobalVariable *>::iterator i = class_gvars.begin();
	    i != class_gvars.end();
	    ++i) {

	GlobalVariable *gvar = *i;
	Value *val = CallInst::Create(getClassFunc,
		compile_const_global_string(gvar->getName().str().c_str()),
		"",bb);
	new StoreInst(val, gvar, bb);
    }

    // Instance variable slots.

    Function *ivarSlotAlloc = cast<Function>(module->getOrInsertFunction(
		"rb_vm_ivar_slot_allocate",
		PtrTy, NULL));

    for (std::vector<GlobalVariable *>::iterator i = ivar_slots.begin();
	    i != ivar_slots.end();
	    ++i) {

	GlobalVariable *gvar = *i;
	Value *val = CallInst::Create(ivarSlotAlloc, "", bb);
	new StoreInst(val, gvar, bb);
    }

    // Stubs.

    Function *addStub = cast<Function>(module->getOrInsertFunction(
		"rb_vm_add_stub", VoidTy, PtrTy, PtrTy, Int8Ty, NULL));

    for (std::vector<std::string>::iterator i = c_stubs.begin();
	    i != c_stubs.end();
	    ++i) {

	const char *types = i->c_str();
	try {
	    Value *args[] = {
		compile_const_global_string(types),
		new BitCastInst(compile_stub(types, false, TypeArity(types),
			    false), PtrTy, "", bb),
		ConstantInt::get(Int8Ty, 0)
	    };
	    CallInst::Create(addStub, args, args + 3, "", bb);
	}
	catch (...) {}
    }

    for (std::vector<std::string>::iterator i = objc_stubs.begin();
	    i != objc_stubs.end();
	    ++i) {

	const char *types = i->c_str();
	try {
	    Value *args[] = {
		compile_const_global_string(types),
		new BitCastInst(compile_stub(types, false,
			    TypeArity(types) - 2, true), PtrTy, "", bb),
		ConstantInt::get(Int8Ty, 1)
	    };
	    CallInst::Create(addStub, args, args + 3, "", bb);
	}
	catch (...) {}
    }

    ReturnInst::Create(context, bb);

    return f;
}

Function *
RoxorCompiler::compile_read_attr(ID name)
{
    Function *f = cast<Function>(module->getOrInsertFunction("",
		RubyObjTy, RubyObjTy, PtrTy, NULL));

    Function::arg_iterator arg = f->arg_begin();
    current_self = arg++;

    bb = BasicBlock::Create(context, "EntryBlock", f);

    ReturnInst::Create(context, compile_ivar_get(name), bb);

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

    bb = BasicBlock::Create(context, "EntryBlock", f);

    Value *val;
    if (rb_objc_enable_ivar_set_kvo_notifications) {
	VALUE tmp = rb_id2str(name);
 	VALUE str = rb_str_substr(tmp, 1, rb_str_chars_len(tmp) - 1);

	Value *args[] = {
	    current_self,
	    compile_immutable_literal(str)
	};

	if (willChangeValueFunc == NULL) {
	    willChangeValueFunc =
		cast<Function>(module->getOrInsertFunction(
			    "rb_objc_willChangeValueForKey",
			    VoidTy, RubyObjTy, RubyObjTy, NULL));
	}
	CallInst::Create(willChangeValueFunc, args, args + 2, "", bb);

	val = compile_ivar_assignment(name, new_val);

	if (didChangeValueFunc == NULL) {
	    didChangeValueFunc =
		cast<Function>(module->getOrInsertFunction(
			    "rb_objc_didChangeValueForKey",
			    VoidTy, RubyObjTy, RubyObjTy, NULL));
	}
	CallInst::Create(didChangeValueFunc, args, args + 2, "", bb);
    }
    else {
	val = compile_ivar_assignment(name, new_val);
    }

    ReturnInst::Create(context, val, bb);

    return f;
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

void
RoxorCompiler::compile_get_struct_fields(Value *val, Value *buf,
					 rb_vm_bs_boxed_t *bs_boxed)
{
    if (getStructFieldsFunc == NULL) {
	getStructFieldsFunc = cast<Function>(module->getOrInsertFunction(
		    "rb_vm_get_struct_fields",
		    VoidTy, RubyObjTy, RubyObjPtrTy, PtrTy, NULL));
    }

    Value *args[] = {
	val,
	buf,
	compile_const_pointer(bs_boxed)
    };
    CallInst::Create(getStructFieldsFunc, args, args + 3, "", bb);
}

extern "C"
void *
rb_vm_get_opaque_data(VALUE rval, rb_vm_bs_boxed_t *bs_boxed, void **ocval)
{
    void *data = NULL;
    if (rval != Qnil) {
	if (!rb_obj_is_kind_of(rval, bs_boxed->klass)) {
	    rb_raise(rb_eTypeError,
		    "cannot convert `%s' (%s) to opaque type %s",
		    RSTRING_PTR(rb_inspect(rval)),
		    rb_obj_classname(rval),
		    rb_class2name(bs_boxed->klass));
	}
	Data_Get_Struct(rval, VALUE, data);
    }
    if (ocval != NULL) {
	*ocval = data;
    }
    return data;
}

Value *
RoxorCompiler::compile_get_opaque_data(Value *val, rb_vm_bs_boxed_t *bs_boxed,
	Value *slot)
{
    if (getOpaqueDataFunc == NULL) {
	// void *rb_vm_get_opaque_data(VALUE rval, rb_vm_bs_boxed_t *bs_boxed,
	//	void **ocval)
	getOpaqueDataFunc = cast<Function>(module->getOrInsertFunction(
		    "rb_vm_get_opaque_data",
		    PtrTy, RubyObjTy, PtrTy, PtrPtrTy, NULL));
    }

    Value *args[] = {
	val,
	compile_const_pointer(bs_boxed),
	slot
    };
    return compile_protected_call(getOpaqueDataFunc, args, args + 3);
}

Value *
RoxorCompiler::compile_get_cptr(Value *val, const char *type, Value *slot)
{
    Value *args[] = {
	val,
	compile_const_global_string(type),
	new BitCastInst(slot, PtrPtrTy, "", bb)
    };
    return compile_protected_call(getPointerPtrFunc, args, args + 3);
}

Value *
RoxorCompiler::compile_lambda_to_funcptr(const char *type,
	Value *val, Value *slot, bool is_block)
{
    GlobalVariable *proc_gvar = NULL;
    if (!is_block) {
	// When compiling a function pointer closure, the Proc object we
	// want to call must be preserved as a global variable.
	// This isn't needed for C blocks because we can retrieve the Proc
	// object from the block literal argument.
	proc_gvar = new GlobalVariable(*RoxorCompiler::module,
		RubyObjTy, false, GlobalValue::InternalLinkage,
		nilVal, "");
	new StoreInst(val, proc_gvar, bb);
    }

    const size_t buf_len = strlen(type + 1) + 1;
    assert(buf_len > 1);
    char *buf = (char *)malloc(buf_len);
    assert(buf != NULL);

    const char *p = GetFirstType(type + 1, buf, buf_len);
    const Type *ret_type = convert_type(buf);
    int argc = 0;

    std::vector<std::string> arg_ctypes;
    std::vector<const Type *> arg_types;

    if (is_block) {
	// The block literal argument.
	arg_types.push_back(PtrTy);	
    }

    while (*p != _MR_C_LAMBDA_E) {
	p = GetFirstType(p, buf, buf_len);
	arg_ctypes.push_back(std::string(buf));
	arg_types.push_back(convert_type(buf));
	argc++;
    }
    FunctionType *ft = FunctionType::get(ret_type, arg_types,
	    false);

    // ret_type stub(arg1, arg2, ...)
    // {
    //     VALUE *argv = alloc(argc);
    //     argv[0] = arg1;
    //     argv[1] = arg2;
    //     return rb_proc_check_and_call(procval, argc, argv);
    // }
    Function *f = cast<Function>(module->getOrInsertFunction("",
		ft));

    BasicBlock *oldbb = bb;
    bb = BasicBlock::Create(context, "EntryBlock", f);

    Function::arg_iterator arg = f->arg_begin();
    Value *block_lit = NULL;
    if (is_block) {
	block_lit = arg++;
    }

    Value *argv;
    if (argc == 0) {
	argv = new BitCastInst(compile_const_pointer(NULL),
		RubyObjPtrTy, "", bb);
    }
    else {
	argv = new AllocaInst(RubyObjTy, ConstantInt::get(Int32Ty, argc),
		"", bb);
	const int off = is_block ? 1 : 0;
	for (int i = 0; i < argc; i++) {
	    Value *index = ConstantInt::get(Int32Ty, i);
	    Value *aslot = GetElementPtrInst::Create(argv, index, "", bb);
	    Value *rval = compile_conversion_to_ruby(arg_ctypes[i].c_str(),
		    arg_types[i + off], arg++);
	    new StoreInst(rval, aslot, bb);
	}
    }

    Value *proc;
    if (is_block) {
	block_lit = new BitCastInst(block_lit,
		PointerType::getUnqual(BlockLiteralTy), "", bb);
	proc = CallInst::Create(blockProcFunc, block_lit, "", bb);
    }
    else {
	proc = new LoadInst(proc_gvar, "", bb);
    }

    // VALUE rb_proc_check_and_call(
    //	VALUE self, int argc, VALUE *argv
    // )
    Function *proc_call_f =
	cast<Function>(module->getOrInsertFunction(
		    "rb_proc_check_and_call",
		    RubyObjTy,
		    RubyObjTy, Int32Ty, RubyObjPtrTy, NULL));

    Value *args[] = {
	proc,
	ConstantInt::get(Int32Ty, argc),
	argv
    };
    Value *ret_val = compile_protected_call(proc_call_f, args,
	    args + 3);

    if (ret_type != VoidTy) {
	GetFirstType(type + 1, buf, buf_len);
	ret_val = compile_conversion_to_c(buf, ret_val,
		new AllocaInst(ret_type, "", bb));
	ReturnInst::Create(context, ret_val, bb);
    }
    else {
	ReturnInst::Create(context, bb);
    }

    bb = oldbb;
    free(buf);
    return new BitCastInst(f, PtrTy, "", bb);
}

static void
decompose_ary_type(const char *type, long *size_p, char *elem_type_p, 
	const size_t elem_type_len)
{
    // Syntax is [8S] for `short foo[8]'.
    // First, let's grab the size.
    char buf[100];
    unsigned int n = 0;
    const char *p = type + 1;
    while (isdigit(*p)) {
	assert(n < (sizeof buf) - 1);
	buf[n++] = *p;
	p++;
    }
    assert(n > 0);
    buf[n] = '\0';
    const long size = atol(buf);
    assert(size > 0);
    if (size_p != NULL) {
	*size_p = size;
    }

    // Second, the element type.
    n = 0;
    while (*p != _C_ARY_E) {
	assert(n < (sizeof buf) - 1);
	buf[n++] = *p;
	p++;
    }
    assert(n > 0);
    buf[n] = '\0';
    if (elem_type_p != NULL) {
	strlcpy(elem_type_p, buf, elem_type_len);
    }
}

Value *
RoxorCompiler::compile_conversion_to_c(const char *type, Value *val,
	Value *slot)
{
    type = SkipTypeModifiers(type);

    if (type[0] == _C_PTR && type[1] != _C_VOID
	    && GET_CORE()->find_bs_cftype(type) != NULL) {
	type = "@";
    }

    Function *func = NULL;

    switch (*type) {
	case _C_ID:
	case _C_CLASS:
	    func = rvalToOcvalFunc;
	    break;

	case _C_BOOL:
	    func = rvalToBoolFunc;
	    break;

	case _C_CHR:
	    func = rvalToCharFunc;
	    break;

	case _C_UCHR:
	    func = rvalToUcharFunc;
	    break;

	case _C_SHT:
	    func = rvalToShortFunc;
	    break;

	case _C_USHT:
	    func = rvalToUshortFunc;
	    break;

	case _C_INT:
	    func = rvalToIntFunc;
	    break;

	case _C_UINT:
	    func = rvalToUintFunc;
	    break;

	case _C_LNG:
	    func = rvalToLongFunc;
	    break;

	case _C_ULNG:
	    func = rvalToUlongFunc;
	    break;

	case _C_LNG_LNG:
	    func = rvalToLongLongFunc;
	    break;

	case _C_ULNG_LNG:
	    func = rvalToUlongLongFunc;
	    break;

	case _C_FLT:
	    func = rvalToFloatFunc;
	    break;

	case _C_DBL:
	    func = rvalToDoubleFunc;
	    break;

	case _C_SEL:
	    func = rvalToSelFunc;
	    break;

	case _C_CHARPTR:
	    func = rvalToCharPtrFunc;
	    break;

	case _C_STRUCT_B:
	    {
		rb_vm_bs_boxed_t *bs_boxed = GET_CORE()->find_bs_struct(type);
		if (bs_boxed != NULL) {
		    if (bs_boxed->as.s->opaque) {
			// Structure is opaque, we just copy the data from
			// the opaque object into the slot.
			Value *data = compile_get_opaque_data(val, bs_boxed,
				ConstantPointerNull::get(PtrPtrTy));
			data = new BitCastInst(data,
				PointerType::getUnqual(convert_type(type)), 
				"", bb);
			new StoreInst(new LoadInst(data, "", bb), slot, bb);
		    }
		    else {
			// Retrieve all fields (as Ruby objects).
			Value *fields = new AllocaInst(RubyObjTy,
				ConstantInt::get(Int32Ty,
				    bs_boxed->as.s->fields_count), "", bb);
			compile_get_struct_fields(val, fields, bs_boxed);

			// Convert each field to C inside the slot memory.
			for (unsigned i = 0; i < bs_boxed->as.s->fields_count;
				i++) {

			    const char *ftype = bs_boxed->as.s->fields[i].type;

			    // Load field VALUE.
			    Value *fval = GetElementPtrInst::Create(fields,
				    ConstantInt::get(Int32Ty, i), "", bb);
			    fval = new LoadInst(fval, "", bb);

			    // Get a pointer to the struct field. The extra 0
			    // is needed because we are dealing with a pointer
			    // to the structure.
			    Value *slot_idx[] = {
				ConstantInt::get(Int32Ty, 0),
				ConstantInt::get(Int32Ty, i)
			    };
			    Value *fslot = GetElementPtrInst::Create(slot,
				    slot_idx, slot_idx + 2, "", bb);

			    RoxorCompiler::compile_conversion_to_c(ftype, fval,
				    fslot);
			}
		    }

		    if (GET_CORE()->is_large_struct_type(bs_boxed->type)) {
			// If this structure is too large, we need to pass its
			// address and not its value, to conform to the ABI.
			return slot;
		    }
		    return new LoadInst(slot, "", bb);
		}
	    }
	    break;

        case _MR_C_LAMBDA_B:
	    {
		type++;
		const bool is_block = *type == _MR_C_LAMBDA_BLOCK;

		Value *funcptr = compile_lambda_to_funcptr(type, val, slot,
			is_block);
		if (!is_block) {
		    // A pure function pointer, let's pass it.
		    return funcptr;
		}

		// A C-level block. We allocate on the auto heap the literal
		// structure following the ABI, initialize it then pass
		// a pointer to it.
		Value *block_lit =
		    compile_xmalloc(GET_CORE()->get_sizeof(BlockLiteralTy));
		Value *args[] = {
		    new BitCastInst(block_lit,
			    PointerType::getUnqual(BlockLiteralTy), "", bb),
		    funcptr,
		    val
		};
		CallInst::Create(initBlockFunc, args, args + 3, "", bb);
		return new BitCastInst(block_lit, PtrTy, "", bb);
	    }
	    break;

	case _C_PTR:
	    {
		rb_vm_bs_boxed_t *bs_boxed = GET_CORE()->find_bs_opaque(type);
		if (bs_boxed != NULL) {
		    return compile_get_opaque_data(val, bs_boxed, slot);
		}
		return compile_get_cptr(val, type, slot);
	    }
	    break;

	case _C_ARY_B:
	    {
		const ArrayType *ary_type = cast<ArrayType>(convert_type(type));
		const Type *elem_type = ary_type->getElementType();
		const unsigned elem_count = ary_type->getNumElements();

		char elem_c_type[100];
		decompose_ary_type(type, NULL, elem_c_type,
			sizeof elem_c_type);

		Value *elem_count_val = ConstantInt::get(Int32Ty, elem_count);
		Value *args[] = {
		    val,
		    elem_count_val	
		};
		val = CallInst::Create(checkArrayFunc, args, args + 2, "", bb);

		slot = new BitCastInst(slot, PointerType::getUnqual(elem_type),
			"", bb);

		for (unsigned i = 0; i < elem_count; i++) {
		    Value *idx = ConstantInt::get(Int32Ty, i);
		    Value *args[] = {
			val,
			idx	
		    };
		    Value *elem = CallInst::Create(entryArrayFunc,
			    args, args + 2, "", bb);
		    Value *elem_slot = GetElementPtrInst::Create(slot, idx,
			    "", bb);
		    compile_conversion_to_c(elem_c_type, elem, elem_slot);
		}

		return new BitCastInst(slot, PointerType::getUnqual(ary_type),
			"", bb);
	    }
	    break;
    }

    if (func == NULL) {
	rb_raise(rb_eTypeError, "unrecognized compile type `%s' to C", type);
    }

    Value *args[] = {
	val,
	slot
    };
    CallInst::Create(func, args, args + 2, "", bb);
    return new LoadInst(slot, "", bb);
}

extern "C"
VALUE
rb_vm_new_struct(VALUE klass, int argc, ...)
{
    assert(argc > 0);

    va_list ar;
    va_start(ar, argc);
    VALUE *data = (VALUE *)xmalloc_ptrs(argc * sizeof(VALUE));
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
    if (val == NULL) {
	return Qnil;
    }
    return Data_Wrap_Struct(klass, NULL, NULL, val);
}

extern "C"
VALUE
rb_vm_new_pointer(const char *type, void *val)
{
    return val == NULL ? Qnil : rb_pointer_new(type, val, 0);
}

Value *
RoxorCompiler::compile_new_struct(Value *klass, std::vector<Value *> &fields)
{
    if (newStructFunc == NULL) {
	// VALUE rb_vm_new_struct(VALUE klass, int argc, ...)
	std::vector<const Type *> types;
	types.push_back(RubyObjTy);
	types.push_back(Int32Ty);
	FunctionType *ft = FunctionType::get(RubyObjTy, types, true);

	newStructFunc = cast<Function>(module->getOrInsertFunction(
		    "rb_vm_new_struct", ft));
    }

    Value *argc = ConstantInt::get(Int32Ty, fields.size());
    fields.insert(fields.begin(), argc);
    fields.insert(fields.begin(), klass);

    return CallInst::Create(newStructFunc, fields.begin(), fields.end(),
	    "", bb); 
}

Value *
RoxorCompiler::compile_new_opaque(Value *klass, Value *val)
{
    if (newOpaqueFunc == NULL) {
	// VALUE rb_vm_new_opaque(VALUE klass, void *val)
	newOpaqueFunc = cast<Function>(module->getOrInsertFunction(
		    "rb_vm_new_opaque", RubyObjTy, RubyObjTy, PtrTy, NULL));
    }

    Value *args[] = {
	klass,
	val
    };
    return CallInst::Create(newOpaqueFunc, args, args + 2, "", bb); 
}

Value *
RoxorCompiler::compile_new_pointer(const char *type, Value *val)
{
    if (newPointerFunc == NULL) {
	newPointerFunc = cast<Function>(module->getOrInsertFunction(
		    "rb_vm_new_pointer", RubyObjTy, PtrTy, PtrTy, NULL));
    }

    Value *args[] = {
	compile_const_global_string(type),
	val
    };
    return CallInst::Create(newPointerFunc, args, args + 2, "", bb);
}

Value *
RoxorCompiler::compile_xmalloc(size_t len)
{
    if (xmallocFunc == NULL) {
	// void *ruby_xmalloc(size_t len);
	xmallocFunc = cast<Function>(module->getOrInsertFunction(
		    "ruby_xmalloc", PtrTy, Int64Ty, NULL));
    }

    return CallInst::Create(xmallocFunc, ConstantInt::get(Int64Ty, len),
	    "", bb);
}

Value *
RoxorCompiler::compile_conversion_to_ruby(const char *type,
	const Type *llvm_type, Value *val)
{
    type = SkipTypeModifiers(type);

    if (type[0] == _C_PTR && type[1] != _C_VOID
	    && GET_CORE()->find_bs_cftype(type) != NULL) {
	type = "@";
    }

    Function *func = NULL;

    switch (*type) {
	case _C_VOID:
	    return nilVal;

	case _C_BOOL:
	    val = new ICmpInst(*bb, ICmpInst::ICMP_EQ, val,
		    ConstantInt::get(Int8Ty, 1));
	    return SelectInst::Create(val, trueVal, falseVal, "", bb);

	case _C_ID:
	case _C_CLASS:
	    func = ocvalToRvalFunc;
	    break;

	case _C_CHR:
	    func = charToRvalFunc;
	    break;

	case _C_UCHR:
	    func = ucharToRvalFunc;
	    break;

	case _C_SHT:
	    func = shortToRvalFunc;
	    break;

	case _C_USHT:
	    func = ushortToRvalFunc;
	    break;

	case _C_INT:
	    func = intToRvalFunc;
	    break;

	case _C_UINT:
	    func = uintToRvalFunc;
	    break;

	case _C_LNG:
	    func = longToRvalFunc;
	    break;

	case _C_ULNG:
	    func = ulongToRvalFunc;
	    break;

	case _C_LNG_LNG:
	    func = longLongToRvalFunc;
	    break;

	case _C_ULNG_LNG:
	    func = ulongLongToRvalFunc;
	    break;

	case _C_FLT:
	    func = floatToRvalFunc;
	    break;

	case _C_DBL:
	    func = doubleToRvalFunc;
	    break;

	case _C_SEL:
	    func = selToRvalFunc;
	    break;

	case _C_CHARPTR:
	    func = charPtrToRvalFunc;
	    break;

	case _C_STRUCT_B:
	    {
		rb_vm_bs_boxed_t *bs_boxed = GET_CORE()->find_bs_struct(type);
		if (bs_boxed != NULL) {
		    Value *klass = ConstantInt::get(RubyObjTy, bs_boxed->klass);
		    if (bs_boxed->as.s->opaque) {
			// Structure is opaque, we make a copy.
			const size_t s = GET_CORE()->get_sizeof(llvm_type);
			Value *slot = compile_xmalloc(s);
			slot = new BitCastInst(slot,
				PointerType::getUnqual(llvm_type), "", bb);
			new StoreInst(val, slot, bb);
			slot = new BitCastInst(slot, PtrTy, "", bb);
			return compile_new_opaque(klass, slot);
		    }
		    else {
			// Convert every field into a Ruby type, then box them.
			std::vector<Value *> params;
			for (unsigned i = 0; i < bs_boxed->as.s->fields_count;
				i++) {

			    const char *ftype = bs_boxed->as.s->fields[i].type;
			    const Type *llvm_ftype = convert_type(ftype);
			    Value *fval = ExtractValueInst::Create(val, i, "",
				    bb);
			    params.push_back(compile_conversion_to_ruby(ftype,
					llvm_ftype, fval));
			}
			return compile_new_struct(klass, params);
		    }
		}
	    }
	    break;

	case _C_PTR:
	    {
		rb_vm_bs_boxed_t *bs_boxed = GET_CORE()->find_bs_opaque(type);
		if (bs_boxed != NULL) {
		    Value *klass = ConstantInt::get(RubyObjTy, bs_boxed->klass);
		    return compile_new_opaque(klass, val);
		}

		if (type[1] != '\0') {
		    return compile_new_pointer(&type[1], val);
		}
	    }
	    break;

	case _C_ARY_B:
	    {
		long elem_count = 0;
		char elem_c_type[100];
		decompose_ary_type(type, &elem_count, elem_c_type,
			sizeof elem_c_type);
		const Type *elem_type = convert_type(elem_c_type);

		const bool is_ary = ArrayType::classof(val->getType());
		if (!is_ary) {
		    val = new BitCastInst(val,
			    PointerType::getUnqual(elem_type), "", bb);
		}

		Value *elems = compile_argv_buffer(elem_count);
		for (long i = 0; i < elem_count; i++) {
		    Value *idx = ConstantInt::get(Int32Ty, i);
		    Value *elem = NULL;
		    if (is_ary) {
			elem = ExtractValueInst::Create(val, i, "", bb);
		    }
		    else {
			Value *slot = GetElementPtrInst::Create(val, idx, "",
				bb);
			elem = new LoadInst(slot, "", bb);
		    }
		    elem = compile_conversion_to_ruby(elem_c_type, elem_type,
			    elem);
		    Value *slot = GetElementPtrInst::Create(elems, idx, "", bb);
		    new StoreInst(elem, slot, bb);
		}

		Value *args[] = {
		    ConstantInt::get(Int32Ty, elem_count),
		    elems
		};
		return CallInst::Create(newArrayFunc, args, args + 2, "", bb);
	    }
	    break;
    }

    if (func == NULL) {
	rb_raise(rb_eTypeError, "unrecognized compile type `%s' to Ruby", type);
	abort();
    }
 
    return CallInst::Create(func, val, "", bb);
}

const Type *
RoxorCompiler::convert_type(const char *type)
{
    type = SkipTypeModifiers(type);

    switch (*type) {
	case _C_VOID:
	    return VoidTy;

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
	    return Int8Ty;

	case _C_SHT:
	case _C_USHT:
	    return Int16Ty;

	case _C_INT:
	case _C_UINT:
	    return Int32Ty;

	case _C_LNG:
	case _C_ULNG:
#if __LP64__
	    return Int64Ty;
#else
	    return Int32Ty;
#endif

	case _C_FLT:
	    return FloatTy;

	case _C_DBL:
	    return DoubleTy;

	case _C_LNG_LNG:
	case _C_ULNG_LNG:
	    return Int64Ty;

	case _C_BFLD:
	    {
		// Syntax is `b3' for `unsigned foo:3'. 
		const long size = atol(type + 1);
		if (size <= 0) {
		    rb_raise(rb_eTypeError, "invalid bitfield type: %s",
			    type);	
		}
		return ArrayType::get(BitTy, size);
	    }
	    break;

	case _C_ARY_B:
	    {
		char buf[100];
		long size = 0;
		decompose_ary_type(type, &size, buf, sizeof buf);
		const Type *type = convert_type(buf);
		// Now, we can return the array type.
		return ArrayType::get(type, size);
	    }
	    break;

	case _MR_C_LAMBDA_B:
	    return PtrTy;

	case _C_STRUCT_B:
	    rb_vm_bs_boxed_t *bs_boxed = GET_CORE()->find_bs_struct(type);
	    if (bs_boxed != NULL) {
		if (bs_boxed->type == NULL) {
		    std::vector<const Type *> s_types;
		    for (unsigned i = 0; i < bs_boxed->as.s->fields_count;
			 i++) {

			const char *ftype = bs_boxed->as.s->fields[i].type;
			s_types.push_back(convert_type(ftype));
		    }
		    bs_boxed->type = StructType::get(context, s_types);
		    assert(bs_boxed->type != NULL);
		}
		return bs_boxed->type;
	    }
	    break;
    }

    rb_raise(rb_eTypeError, "unrecognized runtime type `%s'", type);
}

Function *
RoxorCompiler::compile_stub(const char *types, bool variadic, int min_argc,
	bool is_objc)
{
    save_compiler_state();
    reset_compiler_state();

    Function *f = NULL;
    try {

    if (is_objc) {
	// VALUE stub(IMP imp, VALUE self, SEL sel, int argc, VALUE *argv)
	// {
	//     return (*imp)(self, sel, argv[0], argv[1], ...);
	// }
	f = cast<Function>(module->getOrInsertFunction("",
		    RubyObjTy,
		    PtrTy, RubyObjTy, PtrTy, Int32Ty, RubyObjPtrTy, NULL));
    }
    else {
	// VALUE stub(IMP imp, int argc, VALUE *argv)
	// {
	//     return (*imp)(argv[0], argv[1], ...);
	// }
	f = cast<Function>(module->getOrInsertFunction("",
		    RubyObjTy,
		    PtrTy, Int32Ty, RubyObjPtrTy,
		    NULL));
    }

    bb = BasicBlock::Create(context, "EntryBlock", f);

    Function::arg_iterator arg = f->arg_begin();
    Value *imp_arg = arg++;

    std::vector<const Type *> f_types;
    std::vector<Value *> params;

    const size_t buf_len = strlen(types) + 1;
    assert(buf_len > 1);
    char *buf = (char *)malloc(buf_len);
    assert(buf != NULL);

    // retval
    const char *p = GetFirstType(types, buf, buf_len);
    const Type *ret_type = convert_type(buf);

    Value *sret = NULL;
    if (GET_CORE()->is_large_struct_type(ret_type)) {
	// We are returning a large struct, we need to pass a pointer as the
	// first argument to the structure data and return void to conform to
	// the ABI.
	f_types.push_back(PointerType::getUnqual(ret_type));
	sret = new AllocaInst(ret_type, "", bb);
	params.push_back(sret);
	ret_type = VoidTy;
    }

#if !defined(__LP64__)
    const Type *small_struct_type = NULL;
    if (ret_type->getTypeID() == Type::StructTyID
	    && GET_CORE()->get_sizeof(ret_type) == 8) {
	// We are returning a small struct that can fit inside a 64-bit
	// integer (such as NSPoint).
	// TODO: we should probably make this more generic.
	small_struct_type = ret_type;
	ret_type = Int64Ty;
    }
#endif

    Value *self_arg = NULL;
    if (is_objc) {
	// self
	p = SkipFirstType(p);
	p = SkipStackSize(p);
	f_types.push_back(RubyObjTy);
	self_arg = arg++;
	params.push_back(self_arg);

	// sel
	p = SkipFirstType(p);
	p = SkipStackSize(p);
	f_types.push_back(PtrTy);
	Value *sel_arg = arg++;
	params.push_back(sel_arg);
    }

    /*Value *argc_arg =*/ arg++; // XXX do we really need this argument?
    Value *argv_arg = arg++;

    // Arguments.
    std::vector<unsigned int> byval_args;
    int given_argc = 0;
    bool stop_arg_type = false;
    while ((p = GetFirstType(p, buf, buf_len)) != NULL && buf[0] != '\0') {
	if (variadic && given_argc == min_argc) {
	    stop_arg_type = true;
	}

	const Type *llvm_type = convert_type(buf);
	const Type *f_type = llvm_type;
	if (GET_CORE()->is_large_struct_type(llvm_type)) {
	    // We are passing a large struct, we need to mark this argument
	    // with the byval attribute and configure the internal stub
	    // call to pass a pointer to the structure, to conform to the
	    // ABI.
	    f_type = PointerType::getUnqual(llvm_type);
	    byval_args.push_back(f_types.size() + 1 /* retval */);
	}
	else if (ArrayType::classof(llvm_type)) {
	    // Vectors are passed by reference.
	    f_type = PointerType::getUnqual(llvm_type);
	}

	if (!stop_arg_type) {
	    // In order to conform to the ABI, we must stop providing types
	    // once we start dealing with variable arguments and instead mark
	    // the function as variadic.
	    f_types.push_back(f_type);
	}

	Value *index = ConstantInt::get(Int32Ty, given_argc);
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

    for (std::vector<unsigned int>::iterator iter = byval_args.begin();
	 iter != byval_args.end(); ++iter) {
	imp_call->addAttribute(*iter, Attribute::ByVal);
    }

    // Compile retval.
    Value *retval;
    if (sret != NULL) {
	imp_call->addAttribute(1, Attribute::StructRet);
	retval = new LoadInst(sret, "", bb);
    }
    else {
	retval = imp_call;
    }

    GetFirstType(types, buf, buf_len);
    ret_type = convert_type(buf);
    if (self_arg != NULL && ret_type == VoidTy) {
	// If we are calling an Objective-C method that returns void, let's
	// return the receiver instead of nil, for convenience purposes.
	retval = self_arg;
    }
    else {
#if !defined(__LP64__)
	if (small_struct_type != NULL) {
	    Value *slot = new AllocaInst(small_struct_type, "", bb);
	    new StoreInst(retval,
		    new BitCastInst(slot, PointerType::getUnqual(Int64Ty),
			"", bb),
		    bb);
	    retval = new LoadInst(slot, "", bb);
	    ret_type = small_struct_type;
	}
#endif
	retval = compile_conversion_to_ruby(buf, ret_type, retval);
    }
    ReturnInst::Create(context, retval, bb);

    free(buf);

    } // try
    catch (...) {
	if (f != NULL) {
	    f->eraseFromParent();
	}
	restore_compiler_state();
	throw;
    }

    restore_compiler_state();
    return f;
}

Function *
RoxorCompiler::compile_long_arity_stub(int argc, bool is_block)
{
    Function *f;

    if (is_block) {
	// VALUE stubX(IMP imp, VALUE self, SEL sel,
	//	       VALUE dvars, rb_vm_block_t *b, int argc, VALUE *argv)
	// {
	//	return (*imp)(self, sel, dvars, b, argv[0], ..., argv[X - 1]);
	// }
	f = cast<Function>(module->getOrInsertFunction("",
		RubyObjTy,
		PtrTy, RubyObjTy, PtrTy,
		RubyObjTy, PtrTy, Int32Ty, RubyObjPtrTy, NULL));
    }
    else {
	// VALUE stubX(IMP imp, VALUE self, SEL sel, int argc, VALUE *argv)
	// {
	//     return (*imp)(self, sel, argv[0], argv[1], ..., argv[X - 1]);
	// }
	f = cast<Function>(module->getOrInsertFunction("",
		RubyObjTy,
		PtrTy, RubyObjTy, PtrTy, Int32Ty, RubyObjPtrTy, NULL));
    }

    bb = BasicBlock::Create(context, "EntryBlock", f);

    Function::arg_iterator arg = f->arg_begin();

    Value *imp_arg = arg++;
    Value *self_arg = arg++;
    Value *sel_arg = arg++;
    Value *dvars_arg;
    Value *block_arg;
    if (is_block) {
	dvars_arg = arg++;
	block_arg = arg++;
    }
    /*Value *argc_arg = */arg++;
    Value *argv_arg = arg++;

    std::vector<const Type *> f_types;
    std::vector<Value *> params;

    // Return type
    const Type *ret_type = RubyObjTy;

    // self
    f_types.push_back(RubyObjTy);
    params.push_back(self_arg);

    // sel
    f_types.push_back(PtrTy);
    params.push_back(sel_arg);

    if (is_block) {
	// dvars
	f_types.push_back(RubyObjTy);
	params.push_back(dvars_arg);

	// block
	f_types.push_back(PtrTy);
	params.push_back(block_arg);
    }

    for (int i = 0; i < argc; i++) {
	f_types.push_back(RubyObjTy);

	// Get an int
	Value *index = ConstantInt::get(Int32Ty, i);
	// Get the slot (aka argv[index])
	Value *slot = GetElementPtrInst::Create(argv_arg, index, "", bb);
	// Load the slot into memory and add it as an argument
	Value *arg_val = new LoadInst(slot, "", bb);
	params.push_back(arg_val);
    }

    // Get the function type, aka:    VALUE (*)(VALUE, SEL, ...)
    FunctionType *ft = FunctionType::get(ret_type, f_types, false);
    // Cast imp as the function type
    Value *imp = new BitCastInst(imp_arg, PointerType::getUnqual(ft), "", bb);

    // Compile call to the function
    CallInst *imp_call = CallInst::Create(imp, params.begin(), params.end(),
	"", bb);

    // Compile return value
    Value *retval = imp_call;
    ReturnInst::Create(context, retval, bb);

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
RoxorCompiler::compile_lvar_slot(ID name, bool *need_wb)
{
    std::map<ID, Value *>::iterator iter = lvars.find(name);
    if (iter != lvars.end()) {
#if ROXOR_COMPILER_DEBUG
	printf("get_lvar %s\n", rb_id2name(name));
#endif
	return iter->second;
    }
    if (current_block) {
	Value *slot = compile_dvar_slot(name);
	if (slot != NULL) {	
#if ROXOR_COMPILER_DEBUG
	    printf("get_dvar %s\n", rb_id2name(name));
#endif
	    return slot;
	}
    }
    VALUE *var = GET_VM()->get_binding_lvar(name, false);
    if (var != NULL) {
#if ROXOR_COMPILER_DEBUG
	printf("get_binding_lvar %s (%p)\n", rb_id2name(name), *(void **)var);
#endif
	Value *int_val = ConstantInt::get(IntTy, (long)var);
	if (need_wb != NULL) {
	    *need_wb = true;
	}
	return new IntToPtrInst(int_val, RubyObjPtrTy, "", bb);
    }
    fprintf(stderr, "can't find slot for variable %s\n", rb_id2name(name));
    abort();
}

Value *
RoxorCompiler::compile_lvar_slot(ID name)
{
    return compile_lvar_slot(name, NULL);
}

Value *
RoxorCompiler::compile_dvar_assignment(ID vid, Value *val)
{
    if (running_block != NULL) {
	// Dynamic variables assignments inside a block are a little bit
	// complicated: if we are creating new objects we do need to defeat
	// the thread-local collector by releasing ownership of the objects,
	// otherwise the TLC might prematurely collect them. This is because
	// the assignment is done into another thread's stack, which is not
	// honored by the TLC.
	Value *flag = new BitCastInst(running_block, Int32PtrTy, "", bb);
	flag = new LoadInst(flag, "", bb);
	Value *flagv = ConstantInt::get(Int32Ty, VM_BLOCK_THREAD);
	flag = BinaryOperator::CreateAnd(flag, flagv, "", bb);
	Value *is_thread = new ICmpInst(*bb, ICmpInst::ICMP_EQ, flag, flagv);

	Function *f = bb->getParent();
	BasicBlock *is_thread_bb = BasicBlock::Create(context, "", f);
	BasicBlock *merge_bb = BasicBlock::Create(context, "", f);

	BranchInst::Create(is_thread_bb, merge_bb, is_thread, bb);

	bb = is_thread_bb;
	CallInst::Create(releaseOwnershipFunc, val, "", bb);
	BranchInst::Create(merge_bb, bb);

	bb = merge_bb;
    }
    return compile_lvar_assignment(vid, val);
}

Value *
RoxorCompiler::compile_lvar_assignment(ID vid, Value *val)
{
    bool need_wb = false;
    Value *slot = compile_lvar_slot(vid, &need_wb);
    if (need_wb) {
	Value *args[] = { 
	    new BitCastInst(slot, RubyObjPtrTy, "", bb),
	    new BitCastInst(val, RubyObjTy, "", bb)
	};
	return CallInst::Create(writeBarrierFunc, args, args + 2, "", bb);
    }
    else {
	new StoreInst(val, slot, bb);
    }
    return val;
}

#if __LP64__
# define MAX_GPR_REGS 6
# define MAX_SSE_REGS 8
// The x86-64 ABI requires us to pass by reference arguments when there isn't
// enough registers anymore. We need to count both GPR and SEE registers usage.
// For more info: http://www.x86-64.org/documentation/abi.pdf
static void
examine(const Type *t, int *ngpr, int *nsse)
{
    switch (t->getTypeID()) {
	case Type::IntegerTyID:
	case Type::PointerTyID:
	    (*ngpr)++;
	    break;

	case Type::FloatTyID:
	case Type::DoubleTyID:
	    (*nsse)++;
	    break;

	case Type::StructTyID:
	    {
		const StructType *st = cast<StructType>(t);
		for (unsigned i = 0; i < st->getNumElements(); i++) {
		    examine(st->getElementType(i), ngpr, nsse);
		}
	    }
	    break;

	default:
	    // oops
	    break;
    }
}
#endif

Function *
RoxorCompiler::compile_objc_stub(Function *ruby_func, IMP ruby_imp,
	const rb_vm_arity_t &arity, const char *types)
{
    assert(ruby_func != NULL || ruby_imp != NULL);

    save_compiler_state();
    reset_compiler_state();

    const size_t buf_len = strlen(types) + 1;
    assert(buf_len > 1);
    char *buf = (char *)malloc(buf_len);
    assert(buf != NULL);

    const char *p = types;
    std::vector<const Type *> f_types;

#if __LP64__
    int gprcount = 0, ssecount = 0;
#endif

    // Return value.
    p = GetFirstType(p, buf, buf_len);
    std::string ret_type(buf);
    const Type *f_ret_type = convert_type(buf);
    const Type *f_sret_type = NULL;
    if (GET_CORE()->is_large_struct_type(f_ret_type)) {
	// We are returning a large struct, we need to pass a pointer as the
	// first argument to the structure data and return void to conform to
	// the ABI.
	f_types.push_back(PointerType::getUnqual(f_ret_type));
	f_sret_type = f_ret_type;
	f_ret_type = VoidTy;
#if __LP64__
	gprcount++;
#endif
    }

    // self
    f_types.push_back(RubyObjTy);
    p = SkipFirstType(p);
    p = SkipStackSize(p);
    // sel
    f_types.push_back(PtrTy);
    p = SkipFirstType(p);
    p = SkipStackSize(p);
#if __LP64__
    gprcount += 2;
#endif
    // Arguments.
    std::vector<std::string> arg_types;
    std::vector<unsigned int> byval_args;
    for (int i = 0; i < arity.real; i++) {
	p = GetFirstType(p, buf, buf_len);
	const Type *t = convert_type(buf);
	bool enough_registers = true;
#if __LP64__
	int ngpr = 0, nsse = 0;
	examine(t, &ngpr, &nsse);
	if (gprcount + ngpr > MAX_GPR_REGS || ssecount + nsse > MAX_SSE_REGS) {
	    enough_registers = false;
	}
	else {
	    gprcount += ngpr;
	    ssecount += nsse;
	}
	//printf("arg[%d] is using %d gpr %d sse\n", i, ngpr, nsse);
#endif
	if (!enough_registers || GET_CORE()->is_large_struct_type(t)) {
	    // We are passing a large struct, we need to mark this argument
	    // with the byval attribute and configure the internal stub
	    // call to pass a pointer to the structure, to conform to the ABI.
	    t = PointerType::getUnqual(t);
	    byval_args.push_back(f_types.size() + 1 /* retval */);
	}
	else if (ArrayType::classof(t)) {
	    // Vectors are passed by reference.
	    t = PointerType::getUnqual(t);
	}
	f_types.push_back(t);
	arg_types.push_back(buf);
    }

    free(buf);
    buf = NULL;

    // Create the function.
    FunctionType *ft = FunctionType::get(f_ret_type, f_types, false);
    Function *f = cast<Function>(module->getOrInsertFunction("", ft));
    Function::arg_iterator arg = f->arg_begin();

    bb = BasicBlock::Create(context, "EntryBlock", f);
#if !__LP64__
    // Prepare exception handler (see below).
    BasicBlock *old_rescue_invoke_bb = rescue_invoke_bb;
    rescue_invoke_bb = BasicBlock::Create(context, "rescue", f);
#endif

    Value *sret = NULL;
    int sret_i = 0;
    if (f_sret_type != NULL) {
	sret = arg++;
	sret_i = 1;
	f->addAttribute(1, Attribute::StructRet);
    }
    for (std::vector<unsigned int>::iterator iter = byval_args.begin();
	 iter != byval_args.end(); ++iter) {
	f->addAttribute(*iter, Attribute::ByVal);
    }

    std::vector<Value *> params;
    params.push_back(arg++); // self
    params.push_back(arg++); // sel

    // Convert every incoming argument into Ruby type.
    for (int i = 0; i < arity.real; i++) {
	Value *a = arg++;
	const Type *t = f_types[i + 2 + sret_i];
	if (std::find(byval_args.begin(), byval_args.end(),
		    (unsigned int)i + 3 + sret_i) != byval_args.end()) {
	    a = new LoadInst(a, "", bb);
	    t = cast<PointerType>(t)->getElementType();
	}
	Value *ruby_arg = compile_conversion_to_ruby(arg_types[i].c_str(),
		t, a);
	params.push_back(ruby_arg);
    }

    // Create the Ruby implementation type (unless it's already provided).
    Value *imp;
    if (ruby_func == NULL) {
	std::vector<const Type *> ruby_func_types;
	ruby_func_types.push_back(RubyObjTy);
	ruby_func_types.push_back(PtrTy);
	for (int i = 0; i < arity.real; i++) {
	    ruby_func_types.push_back(RubyObjTy);
	}
	FunctionType *ft = FunctionType::get(RubyObjTy, ruby_func_types, false);
	imp = new BitCastInst(compile_const_pointer((void *)ruby_imp),
		PointerType::getUnqual(ft), "", bb);
    }
    else {
	imp = ruby_func;
    }

    // Call the Ruby implementation.
    Instruction *ruby_call_insn = compile_protected_call(imp, params);

    // Convert the return value into Objective-C type (if any).
    Value *ret_val = ruby_call_insn;
    if (f_ret_type != VoidTy) {
	ret_val = compile_conversion_to_c(ret_type.c_str(), ret_val,
		new AllocaInst(f_ret_type, "", bb));
	ReturnInst::Create(context, ret_val, bb);
    }
    else if (sret != NULL) {
	compile_conversion_to_c(ret_type.c_str(), ret_val, sret);
	ReturnInst::Create(context, bb);
    }
    else {
	ReturnInst::Create(context, bb);
    }

#if !__LP64__
    // The 32-bit Objective-C runtime doesn't use C++ exceptions, therefore
    // we must convert Ruby exceptions to Objective-C ones.
    bb = rescue_invoke_bb;
    compile_landing_pad_header();

    Function *ruby2ocExcHandlerFunc = NULL;
    if (ruby2ocExcHandlerFunc == NULL) {
	// void rb_rb2oc_exc_handler(void);
	ruby2ocExcHandlerFunc = cast<Function>(
		module->getOrInsertFunction("rb_rb2oc_exc_handler", VoidTy,
		    NULL));
    }
    CallInst::Create(ruby2ocExcHandlerFunc, "", bb);

    compile_landing_pad_footer();
    new UnreachableInst(context, bb);

    rescue_invoke_bb = old_rescue_invoke_bb;
#endif

    restore_compiler_state();

    return f;
}

Function *
RoxorCompiler::compile_block_caller(rb_vm_block_t *block)
{
    if (blockEvalFunc == NULL) {
	// VALUE rb_vm_block_eval2(rb_vm_block_t *b, VALUE self, SEL sel,
	//	int argc, const VALUE *argv)
	blockEvalFunc = cast<Function>(module->getOrInsertFunction(
		    "rb_vm_block_eval2",
		    RubyObjTy, PtrTy, RubyObjTy, PtrTy, Int32Ty, RubyObjPtrTy,
		    NULL));
    }

    Function *f;
    Value *rcv;
    Value *sel;
    Value *argc;
    Value *argv;

    const int arity = rb_vm_arity_n(block->arity);
    if (arity == -1) {
	// VALUE foo(VALUE rcv, SEL sel, int argc, VALUE *argv)
	// {
	//     return rb_vm_block_eval2(block, rcv, sel, argc, argv);
	// }
	f = cast<Function>(module->getOrInsertFunction("",
		    RubyObjTy, RubyObjTy, PtrTy, Int32Ty, RubyObjPtrTy, NULL));
	Function::arg_iterator arg = f->arg_begin();
	rcv = arg++;
	sel = arg++;
	argc = arg++;
	argv = arg++;

	bb = BasicBlock::Create(context, "EntryBlock", f);
    }
    else if (arity < -1) {
	// VALUE foo(VALUE rcv, SEL sel, VALUE argv)
	// {
	//	return rb_block_eval2(block, rcv, sel, RARRAY_LEN(argv),
	//		RARRAY_PTR(argv));
	// }
	f = cast<Function>(module->getOrInsertFunction("",
		    RubyObjTy, RubyObjTy, PtrTy, RubyObjTy, NULL));
	Function::arg_iterator arg = f->arg_begin();
	rcv = arg++;
	sel = arg++;
	Value *argv_ary = arg++;

	bb = BasicBlock::Create(context, "EntryBlock", f);

	argc = CallInst::Create(lengthArrayFunc, argv_ary, "", bb);
	argc = new TruncInst(argc, Int32Ty, "", bb);
	argv = CallInst::Create(ptrArrayFunc, argv_ary, "", bb);
    }
    else {
	assert(arity >= 0);
	// VALUE foo(VALUE rcv, SEL sel, VALUE arg1, ...)
	// {
	//     VALUE argv[n] = {arg1, ...};
	//     return rb_vm_block_eval2(block, rcv, sel, n, argv);
	// }
	std::vector<const Type *> stub_types;
	stub_types.push_back(RubyObjTy);
	stub_types.push_back(PtrTy);
	for (int i = 0; i < arity; i++) {
	    stub_types.push_back(RubyObjTy);
	}
	FunctionType *ft = FunctionType::get(RubyObjTy, stub_types, false);
	f = cast<Function>(module->getOrInsertFunction("", ft));

	Function::arg_iterator arg = f->arg_begin();
	rcv = arg++;
	sel = arg++;
	argc = ConstantInt::get(Int32Ty, arity);

	bb = BasicBlock::Create(context, "EntryBlock", f);

	argv = new AllocaInst(RubyObjTy, argc, "", bb);
	for (int i = 0; i < arity; i++) {
	    Value *index = ConstantInt::get(Int32Ty, i);
	    Value *slot = GetElementPtrInst::Create(argv, index, "", bb);
	    new StoreInst(arg++, slot, bb);
	}
    }

    Value *args[] = {
	compile_const_pointer(block),
	rcv,
	sel,
	argc,
	argv
    };
    Value *retval = compile_protected_call(blockEvalFunc, args, args + 5);
    ReturnInst::Create(context, retval, bb);	
    return f;
}

Function *
RoxorCompiler::compile_mri_stub(void *imp, const int arity)
{
    if (arity == 0) {
	// ABI matches if arity is 0.
	// MacRuby:	VALUE foo(VALUE rcv, SEL sel);
	// MRI:		VALUE foo(VALUE rcv);
	return NULL;
    }

    // Prepare function type for the stub.
    std::vector<const Type *> stub_types;
    stub_types.push_back(RubyObjTy); 		// self
    stub_types.push_back(PtrTy);		// SEL
    if (arity == -2) {
	stub_types.push_back(RubyObjTy); 	// ary
    }
    else if (arity == -1) {
	stub_types.push_back(Int32Ty); 		// argc
	stub_types.push_back(RubyObjPtrTy); 	// argv
    }
    else {
	assert(arity > 0);
	for (int i = 0; i < arity; i++) {
	    stub_types.push_back(RubyObjTy); 	// arg...
	}
    }

    // Create the stub.
    FunctionType *ft = FunctionType::get(RubyObjTy, stub_types, false);
    Function *f = cast<Function>(module->getOrInsertFunction("", ft));
    bb = BasicBlock::Create(context, "EntryBlock", f);
    Function::arg_iterator arg = f->arg_begin();
    Value *rcv = arg++;
    Value *sel = arg++;

    // Register the receiver and selector to the VM (for rb_call_super()).
    if (setCurrentMRIMethodContext == NULL) {
	// void rb_vm_set_current_mri_method_context(VALUE self, SEL sel)
	setCurrentMRIMethodContext = 
	    cast<Function>(module->getOrInsertFunction(
			"rb_vm_set_current_mri_method_context",
			VoidTy, RubyObjTy, PtrTy, NULL));
    }
    Value *args[2] = { rcv, sel };
    CallInst::Create(setCurrentMRIMethodContext, args, args + 2, "", bb);

    // Prepare function types for the MRI implementation and arguments.
    std::vector<const Type *> imp_types;
    std::vector<Value *> params;
    if (arity == -2) {
	imp_types.push_back(RubyObjTy); 	// self
	imp_types.push_back(RubyObjTy); 	// ary
	params.push_back(rcv);
	params.push_back(arg++);
    }
    else if (arity == -1) {
	imp_types.push_back(Int32Ty);		// argc
	imp_types.push_back(RubyObjPtrTy);	// argv
	imp_types.push_back(RubyObjTy); 	// self
	params.push_back(arg++);
	params.push_back(arg++);
	params.push_back(rcv);
    }
    else {
	assert(arity > 0);
	imp_types.push_back(RubyObjTy); 	// self
	params.push_back(rcv);
	for (int i = 0; i < arity; i++) {
	    imp_types.push_back(RubyObjTy); 	// arg...
	    params.push_back(arg++);
	}
    }

    // Cast the given MRI implementation.
    FunctionType *imp_ft = FunctionType::get(RubyObjTy, imp_types, false);
    Value *imp_val = new BitCastInst(compile_const_pointer(imp),
	    PointerType::getUnqual(imp_ft), "", bb);

    // Call the MRI implementation and return its value.
    CallInst *imp_call = CallInst::Create(imp_val, params.begin(),
	    params.end(), "", bb); 
    ReturnInst::Create(context, imp_call, bb);

    return f;
}

Function *
RoxorCompiler::compile_to_rval_convertor(const char *type)
{
    // VALUE foo(void *ocval);
    Function *f = cast<Function>(module->getOrInsertFunction("",
		RubyObjTy, PtrTy, NULL));
    Function::arg_iterator arg = f->arg_begin();
    Value *ocval = arg++;

    bb = BasicBlock::Create(context, "EntryBlock", f);

    const Type *llvm_type = convert_type(type); 
    ocval = new BitCastInst(ocval, PointerType::getUnqual(llvm_type), "", bb);
    ocval = new LoadInst(ocval, "", bb);

    Value *rval = compile_conversion_to_ruby(type, llvm_type, ocval);

    ReturnInst::Create(context, rval, bb);

    return f;
}

Function *
RoxorCompiler::compile_to_ocval_convertor(const char *type)
{
    // void foo(VALUE rval, void **ocval);
    Function *f = cast<Function>(module->getOrInsertFunction("",
		VoidTy, RubyObjTy, PtrTy, NULL));
    Function::arg_iterator arg = f->arg_begin();
    Value *rval = arg++;
    Value *ocval = arg++;

    bb = BasicBlock::Create(context, "EntryBlock", f);

    const Type *llvm_type = convert_type(type);
    ocval = new BitCastInst(ocval, PointerType::getUnqual(llvm_type), "", bb);
    compile_conversion_to_c(type, rval, ocval);

    ReturnInst::Create(context, bb);

    return f;
}

Value *
RoxorCompiler::compile_get_ffstate(GlobalVariable *ffstate)
{
    return new LoadInst(ffstate, "", bb);
}

Value *
RoxorCompiler::compile_set_ffstate(Value *val, Value *expected,
				   GlobalVariable *ffstate, BasicBlock *mergeBB, Function *f)
{
    BasicBlock *valEqExpectedBB = BasicBlock::Create(context,
	"value_equal_expected", f);
    BasicBlock *valNotEqExpectedBB = BasicBlock::Create(context,
	"value_not_equal_expected", f);

    Value *valueEqExpectedCond = new ICmpInst(*bb, ICmpInst::ICMP_EQ, val,
	expected);
    BranchInst::Create(valEqExpectedBB, valNotEqExpectedBB,
	valueEqExpectedCond, bb);

    new StoreInst(trueVal,  ffstate, valEqExpectedBB);
    new StoreInst(falseVal, ffstate, valNotEqExpectedBB);

    BranchInst::Create(mergeBB, valEqExpectedBB);
    BranchInst::Create(mergeBB, valNotEqExpectedBB);

    PHINode *pn = PHINode::Create(RubyObjTy, "", mergeBB);
    pn->addIncoming(trueVal, valEqExpectedBB);
    pn->addIncoming(falseVal, valNotEqExpectedBB);

    return pn;
}

Value *
RoxorCompiler::compile_ff2(NODE *node)
{
    /*
     * if ($state == true || nd_beg == true)
     *   $state = (nd_end == false)
     *   return true
     * else
     *   return false
     * end
     */

    GlobalVariable *ffstate = new GlobalVariable(*RoxorCompiler::module,
	RubyObjTy, false, GlobalValue::InternalLinkage, falseVal, "");

    Function *f = bb->getParent();

    BasicBlock *stateNotTrueBB = BasicBlock::Create(context,
	"state_not_true", f);
    BasicBlock *stateOrBegIsTrueBB = BasicBlock::Create(context,
	"state_or_beg_is_true", f);
    BasicBlock *returnTrueBB = BasicBlock::Create(context, "return_true", f);
    BasicBlock *returnFalseBB = BasicBlock::Create(context, "return_false", f);
    BasicBlock *mergeBB = BasicBlock::Create(context, "merge", f);

    // `if $state == true`
    Value *stateVal = compile_get_ffstate(ffstate);
    Value *stateIsTrueCond = new ICmpInst(*bb, ICmpInst::ICMP_EQ, stateVal,
	trueVal);
    BranchInst::Create(stateOrBegIsTrueBB, stateNotTrueBB, stateIsTrueCond,
	bb);

    // `or if nd_beg == true`
    bb = stateNotTrueBB;
    Value *beginValue = compile_node(node->nd_beg);
    stateNotTrueBB = bb;
    Value *begIsTrueCond = new ICmpInst(*bb, ICmpInst::ICMP_EQ,
	beginValue, trueVal);
    BranchInst::Create(stateOrBegIsTrueBB, returnFalseBB, begIsTrueCond, bb);

    // `$state = (nd_end == false)`
    bb = stateOrBegIsTrueBB;
    Value *endValue = compile_node(node->nd_end);
    stateOrBegIsTrueBB = bb;
    compile_set_ffstate(endValue, falseVal, ffstate, returnTrueBB, f);

    BranchInst::Create(mergeBB, returnTrueBB);
    BranchInst::Create(mergeBB, returnFalseBB);

    bb = mergeBB;
    PHINode *pn = PHINode::Create(RubyObjTy, "", mergeBB);
    pn->addIncoming(trueVal, returnTrueBB);
    pn->addIncoming(falseVal, returnFalseBB);

    return pn;
}

Value *
RoxorCompiler::compile_ff3(NODE *node)
{
    /*
     * if ($state == true)
     *   $state = (nd_end == false)
     *   return true
     * else
     *   $state = (nd_beg == true)
     *   return $state
     * end
     */

    GlobalVariable *ffstate = new GlobalVariable(*RoxorCompiler::module,
	RubyObjTy, false, GlobalValue::InternalLinkage, falseVal, "");

    Function *f = bb->getParent();

    BasicBlock *stateIsTrueBB = BasicBlock::Create(context, "state_is_true",
	f);
    BasicBlock *stateIsFalseBB = BasicBlock::Create(context, "state_is_false",
	f);
    BasicBlock *returnTrueBB = BasicBlock::Create(context, "return_true", f);
    BasicBlock *returnStateBB = BasicBlock::Create(context, "return_state", f);
    BasicBlock *mergeBB = BasicBlock::Create(context, "merge", f);

    
    // `if $state == true`
    Value *stateVal = compile_get_ffstate(ffstate);
    Value *stateIsTrueCond = new ICmpInst(*bb, ICmpInst::ICMP_EQ, stateVal,
	trueVal);
    BranchInst::Create(stateIsTrueBB, stateIsFalseBB, stateIsTrueCond, bb);

    // `$state = (nd_end == false)`
    bb = stateIsTrueBB;
    Value *endValue = compile_node(node->nd_end);
    stateIsTrueBB = bb;
    compile_set_ffstate(endValue, falseVal, ffstate, returnTrueBB, f);

    // `$state = (nd_beg == true)`
    bb = stateIsFalseBB;
    Value *beginValue = compile_node(node->nd_beg);
    stateIsFalseBB = bb;
    stateVal = compile_set_ffstate(beginValue, trueVal, ffstate, returnStateBB, f);

    BranchInst::Create(mergeBB, returnTrueBB);
    BranchInst::Create(mergeBB, returnStateBB);

    bb = mergeBB;
    PHINode *pn = PHINode::Create(RubyObjTy, "", mergeBB);
    pn->addIncoming(trueVal, returnTrueBB);
    pn->addIncoming(stateVal, returnStateBB);

    return pn;
}

static void
add_stub_types_cb(SEL sel, const char *types, bool is_objc, void *ctx)
{
    RoxorAOTCompiler *compiler = (RoxorAOTCompiler *)ctx;

    std::map<SEL, std::vector<std::string> *> &map =
	is_objc ? compiler->bs_objc_stubs_types : compiler->bs_c_stubs_types;

    std::map<SEL, std::vector<std::string> *>::iterator iter = map.find(sel);
    std::vector<std::string> *v;
    if (iter == map.end()) {
	v = new std::vector<std::string>();
	map[sel] = v;
    }
    else {
	v = iter->second;
    }
    v->push_back(types);
}

extern "C"
void
rb_vm_parse_bs_full_file(const char *path,
	void (*add_stub_types_cb)(SEL, const char *, bool, void *),
	void *ctx);

void
RoxorAOTCompiler::load_bs_full_file(const char *path)
{
    rb_vm_parse_bs_full_file(path, add_stub_types_cb, (void *)this);
}

#endif // !MACRUBY_STATIC
