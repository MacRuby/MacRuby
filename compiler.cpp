/*
 * MacRuby compiler.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 * 
 * Copyright (C) 2008-2009, Apple Inc. All rights reserved.
 */

#define ROXOR_COMPILER_DEBUG 	0

#include <llvm/Module.h>
#include <llvm/DerivedTypes.h>
#include <llvm/Constants.h>
#include <llvm/CallingConv.h>
#include <llvm/Instructions.h>
#include <llvm/ModuleProvider.h>
#include <llvm/Intrinsics.h>
#include <llvm/ExecutionEngine/JIT.h>
#include <llvm/PassManager.h>
#include <llvm/Target/TargetData.h>
using namespace llvm;

#include "ruby/ruby.h"
#include "ruby/node.h"
#include "id.h"
#include "vm.h"
#include "compiler.h"
#include "objc.h"

extern "C" const char *ruby_node_name(int node);

llvm::Module *RoxorCompiler::module = NULL;
RoxorCompiler *RoxorCompiler::shared = NULL;

RoxorCompiler::RoxorCompiler(void)
{
    fname = NULL;
    inside_eval = false;

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
    return_from_block = -1;
    return_from_block_ids = 0;

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
    newPointerFunc = NULL;
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
    newString2Func = NULL;
    newString3Func = NULL;
    yieldFunc = NULL;
    blockEvalFunc = NULL;
    gvarSetFunc = NULL;
    gvarGetFunc = NULL;
    cvarSetFunc = NULL;
    cvarGetFunc = NULL;
    currentExceptionFunc = NULL;
    popExceptionFunc = NULL;
    getSpecialFunc = NULL;
    breakFunc = NULL;
    returnFromBlockFunc = NULL;
    checkReturnFromBlockFunc = NULL;
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
    threeVal = ConstantInt::get(IntTy, 3);

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
    Int32PtrTy = PointerType::getUnqual(Type::Int32Ty);

#if ROXOR_COMPILER_DEBUG
    level = 0;
#endif
}

RoxorAOTCompiler::RoxorAOTCompiler(void)
: RoxorCompiler()
{
    cObject_gvar = NULL;
    name2symFunc = NULL;
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
	params.push_back(compile_mcache(selEqq, false));
	params.push_back(subnodeVal);
	params.push_back(compile_sel(selEqq));
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

    std::vector<Value *> params;
    params.push_back(compile_mcache(selEqq, false));
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

    std::vector<Value *> params;
    params.push_back(compile_mcache(selEqq, false));
    GlobalVariable *is_redefined = GET_CORE()->redefined_op_gvar(selEqq, true);
    params.push_back(new LoadInst(is_redefined, "", bb));
    params.push_back(comparedToVal);
    params.push_back(splatVal);

    return compile_protected_call(whenSplatFunc, params);
}

GlobalVariable *
RoxorCompiler::compile_const_global_string(const char *str,
	const size_t str_len)
{
    assert(str_len > 0);

    std::string s(str, str_len);
    std::map<std::string, GlobalVariable *>::iterator iter =
	static_strings.find(s);

    GlobalVariable *gvar;
    if (iter == static_strings.end()) {
	const ArrayType *str_type = ArrayType::get(Type::Int8Ty, str_len + 1);

	std::vector<Constant *> ary_elements;
	for (unsigned int i = 0; i < str_len; i++) {
	    ary_elements.push_back(ConstantInt::get(Type::Int8Ty, str[i]));
	}
	ary_elements.push_back(ConstantInt::get(Type::Int8Ty, 0));
	
	gvar = new GlobalVariable(
		str_type,
		true,
		GlobalValue::InternalLinkage,
		ConstantArray::get(str_type, ary_elements),
		"",
		RoxorCompiler::module);

	static_strings[s] = gvar;
    }
    else {
	gvar = iter->second;
    }

    return gvar;
}

Value *
RoxorCompiler::compile_mcache(SEL sel, bool super)
{
    struct mcache *cache = GET_CORE()->method_cache_get(sel, super);
    return compile_const_pointer(cache);
}

Value *
RoxorAOTCompiler::compile_mcache(SEL sel, bool super)
{
    if (super) {
	char buf[100];
	snprintf(buf, sizeof buf, "__super__:%s", sel_getName(sel));
        sel = sel_registerName(buf);
    }

    GlobalVariable *gvar;
    std::map<SEL, GlobalVariable *>::iterator iter = mcaches.find(sel);
    if (iter == mcaches.end()) {
	gvar = new GlobalVariable(
		PtrTy,
		false,
		GlobalValue::InternalLinkage,
		Constant::getNullValue(PtrTy),
		"",
		RoxorCompiler::module);
	assert(gvar != NULL);
	mcaches[sel] = gvar;
    }
    else {
	gvar = iter->second;
    }
    return new LoadInst(gvar, "", bb);
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
	gvar = new GlobalVariable(
		PtrTy,
		false,
		GlobalValue::InternalLinkage,
		Constant::getNullValue(PtrTy),
		"",
		RoxorCompiler::module);
	assert(gvar != NULL);
	ccaches[name] = gvar;
    }
    else {
	gvar = iter->second;
    }
    return new LoadInst(gvar, "", bb);
}

Instruction *
RoxorAOTCompiler::compile_sel(SEL sel, bool add_to_bb)
{
    std::map<SEL, GlobalVariable *>::iterator iter = sels.find(sel);
    GlobalVariable *gvar;
    if (iter == sels.end()) {
	gvar = new GlobalVariable(
		PtrTy,
		false,
		GlobalValue::InternalLinkage,
		Constant::getNullValue(PtrTy),
		"",
		RoxorCompiler::module);
	assert(gvar != NULL);
	sels[sel] = gvar;
    }
    else {
	gvar = iter->second;
    }
    return add_to_bb
	? new LoadInst(gvar, "", bb)
	: new LoadInst(gvar, "");
}

inline Value *
RoxorCompiler::compile_arity(rb_vm_arity_t &arity)
{
    uint64_t v;
    assert(sizeof(uint64_t) == sizeof(rb_vm_arity_t));
    memcpy(&v, &arity, sizeof(rb_vm_arity_t));
    return ConstantInt::get(Type::Int64Ty, v);
}

void
RoxorCompiler::compile_prepare_method(Value *classVal, Value *sel,
	Function *new_function, rb_vm_arity_t &arity, NODE *body)
{
    if (prepareMethodFunc == NULL) {
	// void rb_vm_prepare_method(Class klass, SEL sel,
	//	Function *func, rb_vm_arity_t arity, int flags)
	prepareMethodFunc = 
	    cast<Function>(module->getOrInsertFunction(
			"rb_vm_prepare_method",
			Type::VoidTy, RubyObjTy, PtrTy, PtrTy,
			Type::Int64Ty, Type::Int32Ty, NULL));
    }

    std::vector<Value *> params;

    params.push_back(classVal);
    params.push_back(sel);

    params.push_back(compile_const_pointer(new_function));
    rb_objc_retain((void *)body);
    params.push_back(compile_arity(arity));
    params.push_back(ConstantInt::get(Type::Int32Ty, rb_vm_node_flags(body)));

    CallInst::Create(prepareMethodFunc, params.begin(),
	    params.end(), "", bb);
}

void
RoxorAOTCompiler::compile_prepare_method(Value *classVal, Value *sel,
	Function *new_function, rb_vm_arity_t &arity, NODE *body)
{
    if (prepareMethodFunc == NULL) {
	// void rb_vm_prepare_method2(Class klass, SEL sel,
	//	IMP ruby_imp, rb_vm_arity_t arity, int flags)
	prepareMethodFunc = 
	    cast<Function>(module->getOrInsertFunction(
			"rb_vm_prepare_method2",
			Type::VoidTy, RubyObjTy, PtrTy, PtrTy,
			Type::Int64Ty, Type::Int32Ty, NULL));
    }

    std::vector<Value *> params;

    params.push_back(classVal);
    params.push_back(sel);

    // Make sure the function is compiled before use, this way LLVM won't use
    // a stub.
    GET_CORE()->compile(new_function);
    params.push_back(new BitCastInst(new_function, PtrTy, "", bb));

    params.push_back(compile_arity(arity));
    params.push_back(ConstantInt::get(Type::Int32Ty, rb_vm_node_flags(body)));

    CallInst::Create(prepareMethodFunc, params.begin(),
	    params.end(), "", bb);
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
    params.push_back(compile_mcache(sel, false));
    params.push_back(recv);
    params.push_back(compile_sel(sel));
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
RoxorCompiler::compile_prepare_block_args(Function *func, int *flags)
{
    return compile_const_pointer(func);    
}

Value *
RoxorAOTCompiler::compile_prepare_block_args(Function *func, int *flags)
{
    *flags |= VM_BLOCK_AOT;
    // Force compilation (no stub).
    GET_CORE()->compile(func);
    return new BitCastInst(func, PtrTy, "", bb);
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
	// void *rb_vm_prepare_block(Function *func, int flags, VALUE self,
	//	rb_vm_arity_t arity,
	//	rb_vm_var_uses **parent_var_uses,
	//	rb_vm_block_t *parent_block,
	//	int dvars_size, ...);
	std::vector<const Type *> types;
	types.push_back(PtrTy);
	types.push_back(Type::Int32Ty);
	types.push_back(RubyObjTy);
	types.push_back(Type::Int64Ty);
	types.push_back(PtrPtrTy);
	types.push_back(PtrTy);
	types.push_back(Type::Int32Ty);
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
    params.push_back(ConstantInt::get(Type::Int32Ty, flags));
    params.push_back(current_self);
    rb_vm_arity_t arity = rb_vm_node_arity(current_block_node);
    params.push_back(compile_arity(arity));
    params.push_back(current_var_uses == NULL
	    ? compile_const_pointer_to_pointer(NULL) : current_var_uses);
    params.push_back(running_block == NULL
	    ? compile_const_pointer(NULL) : running_block);

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
	params.push_back(compile_id((long)name));
	params.push_back(slot);
    }

    return CallInst::Create(prepareBlockFunc, params.begin(), params.end(),
	    "", bb);
}

Value *
RoxorCompiler::compile_binding(void)
{
    if (pushBindingFunc == NULL) {
	// void rb_vm_push_binding(VALUE self, rb_vm_block_t *current_block,
	//			   rb_vm_var_uses **parent_var_uses,
	//			   int lvars_size, ...);
	std::vector<const Type *> types;
	types.push_back(RubyObjTy);
	types.push_back(PtrTy);
	types.push_back(PtrPtrTy);
	types.push_back(Type::Int32Ty);
	FunctionType *ft = FunctionType::get(Type::VoidTy, types, true);
	pushBindingFunc = cast<Function>
	    (module->getOrInsertFunction("rb_vm_push_binding", ft));
    }

    std::vector<Value *> params;
    params.push_back(current_self);
    params.push_back(running_block == NULL
	    ? compile_const_pointer(NULL) : running_block);
    if (current_var_uses == NULL) {
	// there is no local variables in this scope
	params.push_back(compile_const_pointer_to_pointer(NULL));
    }
    else {
	params.push_back(current_var_uses);
    }

    // Lvars.
    params.push_back(ConstantInt::get(Type::Int32Ty, (int)lvars.size()));
    for (std::map<ID, Value *>::iterator iter = lvars.begin();
	 iter != lvars.end(); ++iter) {

	ID lvar = iter->first;
	params.push_back(compile_id(lvar));
	params.push_back(iter->second);
    }

    return CallInst::Create(pushBindingFunc, params.begin(), params.end(),
	    "", bb);
}

Instruction *
RoxorCompiler::gen_slot_cache(ID id)
{
    int *slot = (int *)malloc(sizeof(int));
    *slot = -1;
    return compile_const_pointer(slot, Int32PtrTy, false);
}

Instruction *
RoxorAOTCompiler::gen_slot_cache(ID id)
{
    GlobalVariable *gvar = new GlobalVariable(
	    Int32PtrTy,
	    false,
	    GlobalValue::InternalLinkage,
	    Constant::getNullValue(Int32PtrTy),
	    "",
	    RoxorCompiler::module);

    ivar_slots.push_back(gvar);

    return new LoadInst(gvar, "");
}

Instruction *
RoxorCompiler::compile_slot_cache(ID id)
{
    if (inside_eval || current_block || !current_instance_method
	|| current_module) {
	return compile_const_pointer(NULL, Int32PtrTy);
    }

    std::map<ID, Instruction *>::iterator iter = ivar_slots_cache.find(id);
    Instruction *slot;
    if (iter == ivar_slots_cache.end()) {
#if ROXOR_COMPILER_DEBUG
	printf("allocating a new slot for ivar %s\n", rb_id2name(id));
#endif
	slot = gen_slot_cache(id);
	ivar_slots_cache[id] = slot;
    }
    else {
	slot = iter->second;
    }

    Instruction *insn = slot->clone();
    BasicBlock::InstListType &list = bb->getInstList();
    list.insert(list.end(), insn);
    return insn;
}

Value *
RoxorCompiler::compile_ivar_read(ID vid)
{
    if (getIvarFunc == NULL) {
	// VALUE rb_vm_ivar_get(VALUE obj, ID name, int *slot_cache);
	getIvarFunc = cast<Function>(module->getOrInsertFunction("rb_vm_ivar_get",
		    RubyObjTy, RubyObjTy, IntTy, Int32PtrTy, NULL)); 
    }

    std::vector<Value *> params;

    params.push_back(current_self);
    params.push_back(compile_id(vid));
    params.push_back(compile_slot_cache(vid));

    return CallInst::Create(getIvarFunc, params.begin(), params.end(), "", bb);
}

Value *
RoxorCompiler::compile_ivar_assignment(ID vid, Value *val)
{
    if (setIvarFunc == NULL) {
	// void rb_vm_ivar_set(VALUE obj, ID name, VALUE val, int *slot_cache);
	setIvarFunc = 
	    cast<Function>(module->getOrInsertFunction("rb_vm_ivar_set",
			Type::VoidTy, RubyObjTy, IntTy, RubyObjTy, Int32PtrTy,
			NULL)); 
    }

    std::vector<Value *> params;

    params.push_back(current_self);
    params.push_back(compile_id(vid));
    params.push_back(val);
    params.push_back(compile_slot_cache(vid));

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
    params.push_back(compile_id(name));
    params.push_back(val);

    return CallInst::Create(cvarSetFunc, params.begin(),
	    params.end(), "", bb);
}

Value *
RoxorCompiler::compile_gvar_assignment(NODE *node, Value *val)
{
    if (gvarSetFunc == NULL) {
	// VALUE rb_gvar_set(struct global_entry *entry, VALUE val);
	gvarSetFunc = cast<Function>(module->getOrInsertFunction(
		    "rb_gvar_set",
		    RubyObjTy, PtrTy, RubyObjTy, NULL));
    }

    std::vector<Value *> params;

    params.push_back(compile_global_entry(node));
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
	params.push_back(compile_id(node->nd_vid));
    }
    else {
	assert(node->nd_else != NULL);
	params.push_back(compile_class_path(node->nd_else));
	assert(node->nd_else->nd_mid > 0);
	params.push_back(compile_id(node->nd_else->nd_mid));
    }

    params.push_back(val);

    CallInst::Create(setConstFunc, params.begin(), params.end(), "", bb);

    return val;
}

inline Value *
RoxorCompiler::compile_current_class(void)
{
    if (current_opened_class == NULL) {
	return compile_nsobject();
    }
    return new LoadInst(current_opened_class, "", bb);
}

inline Value *
RoxorCompiler::compile_nsobject(void)
{
    return cObject;
}

inline Value *
RoxorAOTCompiler::compile_nsobject(void)
{
    if (cObject_gvar == NULL) {
	cObject_gvar = new GlobalVariable(
		RubyObjTy,
		false,
		GlobalValue::InternalLinkage,
		zeroVal,
		"NSObject",
		RoxorCompiler::module);
    }
    return new LoadInst(cObject_gvar, "", bb);
}

inline Value *
RoxorCompiler::compile_id(ID id)
{
    return ConstantInt::get(IntTy, (long)id);
}

inline Value *
RoxorAOTCompiler::compile_id(ID id)
{
    std::map<ID, GlobalVariable *>::iterator iter = ids.find(id);

    GlobalVariable *gvar;
    if (iter == ids.end()) {
	gvar = new GlobalVariable(
		IntTy,
		false,
		GlobalValue::InternalLinkage,
		ConstantInt::get(IntTy, 0),
		"",
		RoxorCompiler::module);
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

    if (getConstFunc == NULL) {
	// VALUE rb_vm_get_const(VALUE mod, unsigned char lexical_lookup,
	//			 struct ccache *cache, ID id);
	getConstFunc = cast<Function>(module->getOrInsertFunction(
		    "rb_vm_get_const", 
		    RubyObjTy, RubyObjTy, Type::Int8Ty, PtrTy, IntTy, NULL));
    }

    std::vector<Value *> params;

    params.push_back(outer);
    params.push_back(ConstantInt::get(Type::Int8Ty, outer_given ? 0 : 1));
    params.push_back(compile_ccache(id));
    params.push_back(compile_id(id));

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
		: compile_nsobject();
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
	params.push_back(compile_literal(node->nd_lit));
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
    CallInst::Create(breakFunc, params.begin(), params.end(), "", bb);
}

void
RoxorCompiler::compile_return_from_block(Value *val, int id)
{
    if (returnFromBlockFunc == NULL) {
	// void rb_vm_return_from_block(VALUE val, int id);
	returnFromBlockFunc = cast<Function>(
		module->getOrInsertFunction("rb_vm_return_from_block", 
		    Type::VoidTy, RubyObjTy, Type::Int32Ty, NULL));
    }
    std::vector<Value *> params;
    params.push_back(val);
    params.push_back(ConstantInt::get(Type::Int32Ty, id));
    CallInst::Create(returnFromBlockFunc, params.begin(), params.end(), "", bb);
}

void
RoxorCompiler::compile_return_from_block_handler(int id)
{
    //const std::type_info &eh_type = typeid(RoxorReturnFromBlockException *);
    //Value *exception = compile_landing_pad_header(eh_type);
    Value *exception = compile_landing_pad_header();

    if (checkReturnFromBlockFunc == NULL) {
	// VALUE rb_vm_check_return_from_block_exc(void *exc, int id);
	checkReturnFromBlockFunc = cast<Function>(
		module->getOrInsertFunction(
		    "rb_vm_check_return_from_block_exc", 
		    RubyObjTy, PtrTy, Type::Int32Ty, NULL));
    }

    std::vector<Value *> params;
    params.push_back(exception);
    params.push_back(ConstantInt::get(Type::Int32Ty, id));
    Value *val = CallInst::Create(checkReturnFromBlockFunc, params.begin(),
	    params.end(), "", bb);

    Function *f = bb->getParent();
    BasicBlock *ret_bb = BasicBlock::Create("ret", f);
    BasicBlock *rethrow_bb  = BasicBlock::Create("rethrow", f);
    Value *need_ret = new ICmpInst(ICmpInst::ICMP_NE, val,
	    ConstantInt::get(RubyObjTy, Qundef), "", bb);
    BranchInst::Create(ret_bb, rethrow_bb, need_ret, bb);

    bb = ret_bb;
    compile_landing_pad_footer(false);
    ReturnInst::Create(val, bb);	

    bb = rethrow_bb;
    compile_rethrow_exception();
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
		if (return_from_block == -1) {
		    return_from_block = return_from_block_ids++;
		}
		compile_return_from_block(val, return_from_block);
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
	return compile_nsobject();
    }
    else if (node->nd_head != NULL) {
	// Bar::Foo
	return compile_node(node->nd_head);
    }

    return compile_current_class();
}

Value *
RoxorCompiler::compile_landing_pad_header(void)
{
    return compile_landing_pad_header(typeid(void));
}

Value *
RoxorCompiler::compile_landing_pad_header(const std::type_info &eh_type)
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

    if (eh_type == typeid(void)) {
	// catch (...)
	params.push_back(compile_const_pointer(NULL));
    }
    else {
	// catch (eh_type &exc)
	params.push_back(compile_const_pointer((void *)&eh_type));
	params.push_back(compile_const_pointer(NULL));
    }

    Value *eh_sel = CallInst::Create(eh_selector_f, params.begin(),
	    params.end(), "", bb);

    if (eh_type != typeid(void)) {
	// TODO: this doesn't work yet, the type id must be a GlobalVariable...
#if __LP64__
	Function *eh_typeid_for_f = Intrinsic::getDeclaration(module,
		Intrinsic::eh_typeid_for_i64);
#else
	Function *eh_typeid_for_f = Intrinsic::getDeclaration(module,
		Intrinsic::eh_typeid_for_i32);
#endif
	std::vector<Value *> params;
	params.push_back(compile_const_pointer((void *)&eh_type));

	Value *eh_typeid = CallInst::Create(eh_typeid_for_f, params.begin(),
		params.end(), "", bb);

	Function *f = bb->getParent();
	BasicBlock *typeok_bb = BasicBlock::Create("typeok", f);
	BasicBlock *nocatch_bb  = BasicBlock::Create("nocatch", f);
	Value *need_ret = new ICmpInst(ICmpInst::ICMP_EQ, eh_sel,
		eh_typeid, "", bb);
	BranchInst::Create(typeok_bb, nocatch_bb, need_ret, bb);

	bb = nocatch_bb;
	compile_rethrow_exception();

	bb = typeok_bb;
    }

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

typedef struct rb_vm_immediate_val {
    int type;
    union {
	long l;
	double d;
    } v;
    rb_vm_immediate_val(void) { type = 0; }
    bool is_fixnum(void) { return type == T_FIXNUM; }
    bool is_float(void) { return type == T_FLOAT; }
    long long_val(void) { return is_fixnum() ? v.l : (long)v.d; }
    double double_val(void) { return is_float() ? v.d : (double)v.l; }
} rb_vm_immediate_val_t;

static bool 
unbox_immediate_val(VALUE rval, rb_vm_immediate_val_t *val)
{
    if (rval != Qundef) {
	if (FIXNUM_P(rval)) {
	    val->type = T_FIXNUM;
	    val->v.l = FIX2LONG(rval);
	    return true;
	}
	else if (FIXFLOAT_P(rval)) {
	    val->type = T_FLOAT;
	    val->v.d = FIXFLOAT2DBL(rval);
	    return true;
	}
    }
    return false;
}

template <class T> static bool
optimized_const_immediate_op(SEL sel, T leftVal, T rightVal,
			     bool *is_predicate, T *res_p)
{
    T res;
    if (sel == selPLUS) {
	res = leftVal + rightVal;
    }
    else if (sel == selMINUS) {
	res = leftVal - rightVal;
    }
    else if (sel == selDIV) {
	if (rightVal == 0) {
	    return false;
	}
	res = leftVal / rightVal;
    }
    else if (sel == selMULT) {
	res = leftVal * rightVal;
    }
    else {
	*is_predicate = true;
	if (sel == selLT) {
	    res = leftVal < rightVal;
	}
	else if (sel == selLE) {
	    res = leftVal <= rightVal;
	}
	else if (sel == selGT) {
	    res = leftVal > rightVal;
	}
	else if (sel == selGE) {
	    res = leftVal >= rightVal;
	}
	else if (sel == selEq || sel == selEqq) {
	    res = leftVal == rightVal;
	}
	else if (sel == selNeq) {
	    res = leftVal != rightVal;
	}
	else {
	    abort();		
	}
    }
    *res_p = res;
    return true;
}

Value *
RoxorCompiler::optimized_immediate_op(SEL sel, Value *leftVal, Value *rightVal,
	bool float_op, bool *is_predicate)
{
    Value *res;
    if (sel == selPLUS) {
	res = BinaryOperator::CreateAdd(leftVal, rightVal, "", bb);
    }
    else if (sel == selMINUS) {
	res = BinaryOperator::CreateSub(leftVal, rightVal, "", bb);
    }
    else if (sel == selDIV) {
	res = float_op
	    ? BinaryOperator::CreateFDiv(leftVal, rightVal, "", bb)
	    : BinaryOperator::CreateSDiv(leftVal, rightVal, "", bb);
		
    }
    else if (sel == selMULT) {
	res = BinaryOperator::CreateMul(leftVal, rightVal, "", bb);
    }
    else {
	*is_predicate = true;

	CmpInst::Predicate predicate;

	if (sel == selLT) {
	    predicate = float_op ? FCmpInst::FCMP_OLT : ICmpInst::ICMP_SLT;
	}
	else if (sel == selLE) {
	    predicate = float_op ? FCmpInst::FCMP_OLE : ICmpInst::ICMP_SLE;
	}
	else if (sel == selGT) {
	    predicate = float_op ? FCmpInst::FCMP_OGT : ICmpInst::ICMP_SGT;
	}
	else if (sel == selGE) {
	    predicate = float_op ? FCmpInst::FCMP_OGE : ICmpInst::ICMP_SGE;
	}
	else if (sel == selEq || sel == selEqq) {
	    predicate = float_op ? FCmpInst::FCMP_OEQ : ICmpInst::ICMP_EQ;
	}
	else if (sel == selNeq) {
	    predicate = float_op ? FCmpInst::FCMP_ONE : ICmpInst::ICMP_NE;
	}
	else {
	    abort();
	}

	if (float_op) {
	    res = new FCmpInst(predicate, leftVal, rightVal, "", bb);
	}
	else {
	    res = new ICmpInst(predicate, leftVal, rightVal, "", bb);
	}
	res = SelectInst::Create(res, trueVal, falseVal, "", bb);
    }
    return res;
}

Value *
RoxorCompiler::compile_double_coercion(Value *val, Value *mask,
	BasicBlock *fallback_bb, Function *f)
{
    Value *is_float = new ICmpInst(ICmpInst::ICMP_EQ,
	    mask, threeVal, "", bb);

    BasicBlock *is_float_bb = BasicBlock::Create("is_float", f);
    BasicBlock *isnt_float_bb = BasicBlock::Create("isnt_float", f);
    BasicBlock *merge_bb = BasicBlock::Create("merge", f);

    BranchInst::Create(is_float_bb, isnt_float_bb, is_float, bb);

    bb = is_float_bb;
    Value *is_float_val = BinaryOperator::CreateXor(val, threeVal, "", bb);
    is_float_val = new BitCastInst(is_float_val, Type::DoubleTy, "", bb);
    BranchInst::Create(merge_bb, bb);

    bb = isnt_float_bb;
    Value *is_fixnum = new ICmpInst(ICmpInst::ICMP_EQ, mask, oneVal, "", bb);
    BasicBlock *is_fixnum_bb = BasicBlock::Create("is_fixnum", f);
    BranchInst::Create(is_fixnum_bb, fallback_bb, is_fixnum, bb);

    bb = is_fixnum_bb;
    Value *is_fixnum_val = BinaryOperator::CreateAShr(val, twoVal, "", bb);
    is_fixnum_val = new SIToFPInst(is_fixnum_val, Type::DoubleTy, "", bb);
    BranchInst::Create(merge_bb, bb);

    bb = merge_bb;
    PHINode *pn = PHINode::Create(Type::DoubleTy, "op_tmp", bb);
    pn->addIncoming(is_float_val, is_float_bb);
    pn->addIncoming(is_fixnum_val, is_fixnum_bb);

    return pn;
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

	GlobalVariable *is_redefined = GET_CORE()->redefined_op_gvar(sel, true);
	
	Value *leftVal = params[1]; // self
	Value *rightVal = params.back();

	VALUE leftRVal = Qundef, rightRVal = Qundef;
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

	rb_vm_immediate_val_t leftImm, rightImm;
	const bool leftIsImmediateConst = unbox_immediate_val(leftRVal,
		&leftImm);
	const bool rightIsImmediateConst = unbox_immediate_val(rightRVal,
		&rightImm);

	if (leftIsImmediateConst && rightIsImmediateConst) {
	    Value *res_val = NULL;

	    if (leftImm.is_fixnum() && rightImm.is_fixnum()) {
		bool result_is_predicate = false;
		long res;
		if (optimized_const_immediate_op<long>(
			    sel,
			    leftImm.long_val(),
			    rightImm.long_val(),
			    &result_is_predicate,
			    &res)) {
		    if (result_is_predicate) {
			res_val = res == 1 ? trueVal : falseVal;
		    }
		    else if (FIXABLE(res)) {
			res_val = ConstantInt::get(RubyObjTy, LONG2FIX(res));
		    }
		}
	    }
	    else {
		bool result_is_predicate = false;
		double res;
		if (optimized_const_immediate_op<double>(
			    sel,
			    leftImm.double_val(),
			    rightImm.double_val(),
			    &result_is_predicate,
			    &res)) {
		    if (result_is_predicate) {
			res_val = res == 1 ? trueVal : falseVal;
		    }
		    else {
			res_val = ConstantInt::get(RubyObjTy,
				DBL2FIXFLOAT(res));
		    }
		}
	    }

	    if (res_val != NULL) {
		Value *is_redefined_val = new LoadInst(is_redefined, "", bb);
		Value *isOpRedefined = new ICmpInst(ICmpInst::ICMP_EQ, 
			is_redefined_val, ConstantInt::getFalse(), "", bb);

		Function *f = bb->getParent();

		BasicBlock *thenBB = BasicBlock::Create("op_not_redefined", f);
		BasicBlock *elseBB  = BasicBlock::Create("op_dispatch", f);
		BasicBlock *mergeBB = BasicBlock::Create("op_merge", f);

		BranchInst::Create(thenBB, elseBB, isOpRedefined, bb);
		Value *thenVal = res_val;
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
	    // Can't optimize, call the dispatcher.
	    return NULL;
	}
	else {
	    // Either one or both is not a constant immediate.
	    Value *is_redefined_val = new LoadInst(is_redefined, "", bb);
	    Value *isOpRedefined = new ICmpInst(ICmpInst::ICMP_EQ, 
		    is_redefined_val, ConstantInt::getFalse(), "", bb);

	    Function *f = bb->getParent();

	    BasicBlock *not_redefined_bb =
		BasicBlock::Create("op_not_redefined", f);
	    BasicBlock *optimize_fixnum_bb =
		BasicBlock::Create("op_optimize_fixnum", f);
	    BasicBlock *optimize_float_bb =
		BasicBlock::Create("op_optimize_float", f);
	    BasicBlock *dispatch_bb  = BasicBlock::Create("op_dispatch", f);
	    BasicBlock *merge_bb = BasicBlock::Create("op_merge", f);

	    BranchInst::Create(not_redefined_bb, dispatch_bb, isOpRedefined,
		    bb);

 	    bb = not_redefined_bb;

	    Value *leftAndOp = NULL;
	    if (!leftIsImmediateConst) {
		leftAndOp = BinaryOperator::CreateAnd(leftVal, threeVal, "", 
			bb);
	    }

	    Value *rightAndOp = NULL;
	    if (!rightIsImmediateConst) {
		rightAndOp = BinaryOperator::CreateAnd(rightVal, threeVal, "", 
			bb);
	    }

	    if (leftAndOp != NULL && rightAndOp != NULL) {
		Value *leftIsFixnum = new ICmpInst(ICmpInst::ICMP_EQ,
			leftAndOp, oneVal, "", bb);
		BasicBlock *left_is_fixnum_bb =
		    BasicBlock::Create("left_fixnum", f);
		BranchInst::Create(left_is_fixnum_bb, optimize_float_bb,
			leftIsFixnum, bb);

		bb = left_is_fixnum_bb;
		Value *rightIsFixnum = new ICmpInst(ICmpInst::ICMP_EQ,
			rightAndOp, oneVal, "", bb);
		BranchInst::Create(optimize_fixnum_bb, optimize_float_bb,
			rightIsFixnum, bb);
	    }
	    else if (leftAndOp != NULL) {
		if (rightImm.is_fixnum()) {
		    Value *leftIsFixnum = new ICmpInst(ICmpInst::ICMP_EQ,
			    leftAndOp, oneVal, "", bb);
		    BranchInst::Create(optimize_fixnum_bb, optimize_float_bb,
			    leftIsFixnum, bb);
		}
		else {
		    BranchInst::Create(optimize_float_bb, bb);
		}
	    }
	    else if (rightAndOp != NULL) {
		if (leftImm.is_fixnum()) {
		    Value *rightIsFixnum = new ICmpInst(ICmpInst::ICMP_EQ,
			    rightAndOp, oneVal, "", bb);
		    BranchInst::Create(optimize_fixnum_bb, optimize_float_bb,
			    rightIsFixnum, bb);
		}
		else {
		    BranchInst::Create(optimize_float_bb, bb);
		}
	    }

	    bb = optimize_fixnum_bb;

	    Value *unboxedLeft;
	    if (leftIsImmediateConst) {
		unboxedLeft = ConstantInt::get(IntTy, leftImm.long_val());
	    }
	    else {
		unboxedLeft = BinaryOperator::CreateAShr(leftVal, twoVal, "",
			bb);
	    }

	    Value *unboxedRight;
	    if (rightIsImmediateConst) {
		unboxedRight = ConstantInt::get(IntTy, rightImm.long_val());
	    }
	    else {
		unboxedRight = BinaryOperator::CreateAShr(rightVal, twoVal, "",
			bb);
	    }

	    bool result_is_predicate = false;
	    Value *fix_op_res = optimized_immediate_op(sel, unboxedLeft,
		    unboxedRight, false, &result_is_predicate);

	    if (!result_is_predicate) {
		// Box the fixnum.
		Value *shift = BinaryOperator::CreateShl(fix_op_res, twoVal, "",
			bb);
		Value *boxed_op_res = BinaryOperator::CreateOr(shift, oneVal,
			"", bb);

		// Is result fixable?
		Value *fixnumMax = ConstantInt::get(IntTy, FIXNUM_MAX + 1);
		Value *isFixnumMaxOk = new ICmpInst(ICmpInst::ICMP_SLT,
			fix_op_res, fixnumMax, "", bb);

		BasicBlock *fixable_max_bb =
		    BasicBlock::Create("op_fixable_max", f);

		BranchInst::Create(fixable_max_bb, dispatch_bb, isFixnumMaxOk,
			bb);

		bb = fixable_max_bb;
		Value *fixnumMin = ConstantInt::get(IntTy, FIXNUM_MIN);
		Value *isFixnumMinOk = new ICmpInst(ICmpInst::ICMP_SGE,
			fix_op_res, fixnumMin, "", bb);

		BranchInst::Create(merge_bb, dispatch_bb, isFixnumMinOk, bb);
		fix_op_res = boxed_op_res;
		optimize_fixnum_bb = fixable_max_bb;
	    }
	    else {
		BranchInst::Create(merge_bb, bb);
	    }

	    bb = optimize_float_bb;

	    if (leftIsImmediateConst) {
		unboxedLeft = ConstantFP::get(Type::DoubleTy,
			leftImm.double_val());
	    }
	    else {
		unboxedLeft = compile_double_coercion(leftVal, leftAndOp,
			dispatch_bb, f);
	    }

	    if (rightIsImmediateConst) {
		unboxedRight = ConstantFP::get(Type::DoubleTy,
			rightImm.double_val());
	    }
	    else {
		unboxedRight = compile_double_coercion(rightVal, rightAndOp,
			dispatch_bb, f);
	    }

	    result_is_predicate = false;
	    Value *flp_op_res = optimized_immediate_op(sel, unboxedLeft,
		    unboxedRight, true, &result_is_predicate);

	    if (!result_is_predicate) {
		// Box the float. 
		flp_op_res = new BitCastInst(flp_op_res, RubyObjTy, "", bb);
		flp_op_res = BinaryOperator::CreateOr(flp_op_res, threeVal,
			"", bb);
	    }
	    optimize_float_bb = bb;
	    BranchInst::Create(merge_bb, bb);

	    bb = dispatch_bb;
	    Value *dispatch_val;
	    if (sel == selEqq) {
		dispatch_val = compile_fast_eqq_call(leftVal, rightVal);
	    }
	    else {
		dispatch_val = compile_dispatch_call(params);
	    }
	    dispatch_bb = bb;
	    BranchInst::Create(merge_bb, bb);

	    bb = merge_bb;
	    PHINode *pn = PHINode::Create(RubyObjTy, "op_tmp", bb);
	    pn->addIncoming(fix_op_res, optimize_fixnum_bb);
	    pn->addIncoming(flp_op_res, optimize_float_bb);
	    pn->addIncoming(dispatch_val, dispatch_bb);

//	    if (sel == selEqq) {
//		pn->addIncoming(fastEqqVal, fastEqqBB);
//	    }

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

	GlobalVariable *is_redefined = GET_CORE()->redefined_op_gvar(sel, true);
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

	GlobalVariable *is_redefined = GET_CORE()->redefined_op_gvar(sel, true);

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
	new_params.push_back(compile_mcache(new_sel, false));
	new_params.push_back(params[1]);
	new_params.push_back(compile_sel(new_sel));
	new_params.push_back(params[3]);
	new_params.push_back(params[4]);
	new_params.push_back(ConstantInt::get(Type::Int32Ty, argc - 1));
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
	const char *str = RSTRING_PTR(val);
	const size_t str_len = RSTRING_LEN(val);
	if (str_len == 0) {
	    if (newString3Func == NULL) {	
		newString3Func = cast<Function>(
			module->getOrInsertFunction(
			    "rb_str_new_empty", RubyObjTy, NULL));
	    }
	    return CallInst::Create(newString3Func, "", bb);
	}
	else {
	    GlobalVariable *str_gvar = compile_const_global_string(str,
		    str_len);

	    std::vector<Value *> idxs;
	    idxs.push_back(ConstantInt::get(Type::Int32Ty, 0));
	    idxs.push_back(ConstantInt::get(Type::Int32Ty, 0));
	    Instruction *load = GetElementPtrInst::Create(str_gvar,
		    idxs.begin(), idxs.end(), "", bb);

	    if (newString2Func == NULL) {	
		newString2Func = cast<Function>(
			module->getOrInsertFunction(
			    "rb_str_new", RubyObjTy, PtrTy, Type::Int32Ty,
			    NULL));
	    }

	    std::vector<Value *> params;
	    params.push_back(load);
	    params.push_back(ConstantInt::get(Type::Int32Ty, str_len));

	    return compile_protected_call(newString2Func, params);
	}
    }

    return compile_immutable_literal(val);
}

Value *
RoxorCompiler::compile_immutable_literal(VALUE val)
{
    return ConstantInt::get(RubyObjTy, (long)val); 
}

Value *
RoxorAOTCompiler::compile_immutable_literal(VALUE val)
{
    if (SPECIAL_CONST_P(val)) {
	return RoxorCompiler::compile_immutable_literal(val);
    }

    std::map<VALUE, GlobalVariable *>::iterator iter = literals.find(val);
    GlobalVariable *gvar = NULL;

    if (iter == literals.end()) {
	gvar = new GlobalVariable(
		RubyObjTy,
		false,
		GlobalValue::InternalLinkage,
		nilVal,
		"",
		RoxorCompiler::module);
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
	gvar = new GlobalVariable(
		PtrTy,
		false,
		GlobalValue::InternalLinkage,
		Constant::getNullValue(PtrTy),
		"",
		RoxorCompiler::module);
	global_entries[name] = gvar;
    }
    else {
	gvar = iter->second;
    }

    return new LoadInst(gvar, "", bb);
}

void
RoxorCompiler::compile_ivar_slots(Value *klass,
	BasicBlock::InstListType &list, 
	BasicBlock::InstListType::iterator list_iter)
{
    if (ivar_slots_cache.size() > 0) {
	if (prepareIvarSlotFunc == NULL) {
	    // void rb_vm_prepare_class_ivar_slot(VALUE klass, ID name,
	    // 		int *slot_cache);
	    prepareIvarSlotFunc = cast<Function>(
		    module->getOrInsertFunction(
			"rb_vm_prepare_class_ivar_slot", 
			Type::VoidTy, RubyObjTy, IntTy, Int32PtrTy, NULL));
	}
	for (std::map<ID, Instruction *>::iterator iter
		= ivar_slots_cache.begin();
	     iter != ivar_slots_cache.end();
	     ++iter) {

	    ID ivar_name = iter->first;
	    Instruction *ivar_slot = iter->second;
	    std::vector<Value *> params;

	    params.push_back(klass);

	    Value *id_val = compile_id(ivar_name);
	    if (Instruction::classof(id_val)) {
		Instruction *insn = cast<Instruction>(id_val);
		insn->removeFromParent();
		list.insert(list_iter, insn);
	    }
	    params.push_back(id_val);

	    Instruction *insn = ivar_slot->clone();
	    list.insert(list_iter, insn);
	    params.push_back(insn);

	    CallInst *call = CallInst::Create(prepareIvarSlotFunc, 
		    params.begin(), params.end(), "");

	    list.insert(list_iter, call);
	}
    }
}

void
RoxorCompiler::compile_node_error(const char *msg, NODE *node)
{
    int t = nd_type(node);
    printf("%s: %d (%s)", msg, t, ruby_node_name(t));
    abort();
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

		    // searches all ReturnInst in the function we just created and add before
		    // a call to the function to save the local variables if necessary
		    // (we can't do this before finishing compiling the whole function
		    // because we can't be sure if a block is there or not before)
		    for (Function::iterator block_it = f->begin();
			 block_it != f->end();
			 ++block_it) {
			for (BasicBlock::iterator inst_it = block_it->begin();
			     inst_it != block_it->end();
			     ++inst_it) {
			    if (dyn_cast<ReturnInst>(inst_it)) {
				if (params.empty()) {
				    params.push_back(NULL);
				    params.push_back(NULL);
				    int vars_count = 0;
				    for (std::map<ID, Value *>::iterator iter = lvars.begin();
					    iter != lvars.end(); ++iter) {
					ID name = iter->first;
					Value *slot = iter->second;
					if (std::find(dvars.begin(), dvars.end(), name) == dvars.end()) {
					    Value *id_val = compile_id(name);
					    if (Instruction::classof(id_val)) {
						cast<Instruction>(id_val)->moveBefore(inst_it);
					    }
					    params.push_back(id_val);
					    params.push_back(slot);
					    vars_count++;
					}
				    }
				    params[1] = ConstantInt::get(Type::Int32Ty, vars_count);
				}

				params[0] = new LoadInst(current_var_uses, "", inst_it);

				// TODO: only call the function if current_use is not NULL
				CallInst::Create(keepVarsFunc, params.begin(), params.end(), "",
					inst_it);
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

		params.push_back(compile_global_entry(node));

		return CallInst::Create(gvarGetFunc, params.begin(), params.end(), "", bb);
	    }
	    break;

	case NODE_GASGN:
	    {
		assert(node->nd_vid > 0);
		assert(node->nd_value != NULL);
		assert(node->nd_entry != NULL);

		return compile_gvar_assignment(node,
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
		params.push_back(compile_id(node->nd_vid));

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
		params.push_back(compile_mcache(sel, false));
		params.push_back(recv);
		params.push_back(compile_sel(sel));
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
		    params.clear();
		    params.push_back(compile_mcache(sel, false));
		    params.push_back(tmp);
		    params.push_back(compile_sel(sel));
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
		params.clear();
		params.push_back(compile_mcache(sel, false));
		params.push_back(recv);
		params.push_back(compile_sel(sel));
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
		params.push_back(compile_mcache(selBackquote, false));
		params.push_back(nilVal);
		params.push_back(compile_sel(selBackquote));
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

		    params.push_back(compile_id(path));
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

			std::map<ID, Instruction *> old_ivar_slots_cache
			    = ivar_slots_cache;
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

		    Function *f = bb->getParent();
		    const unsigned long argc =
			args == NULL ? 0 : args->nd_alen;

		    if (f->arg_size() - 2 == argc) {
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
		params.push_back(compile_mcache(sel, super_call));

		// Self.
		params.push_back(recv == NULL ? current_self
			: compile_node(recv));

		// Selector.
		params.push_back(compile_sel(sel));

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
			? compile_block_create(NULL)
			: compile_const_pointer(NULL);
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
		return compile_literal(node->nd_lit);
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
		params.push_back(compile_mcache(selEqTilde, false));
		params.push_back(reTarget);
		params.push_back(compile_sel(selEqTilde));
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
		params.push_back(compile_id(node->u1.id));
		params.push_back(compile_id(node->u2.id));

		CallInst::Create(valiasFunc, params.begin(), params.end(), "", bb);

		return nilVal;
	    }
	    break;

	case NODE_ALIAS:
	    {
		if (aliasFunc == NULL) {
		    // void rb_vm_alias(VALUE outer, ID from, ID to);
		    aliasFunc = cast<Function>(module->getOrInsertFunction("rb_vm_alias",
				Type::VoidTy, RubyObjTy, IntTy, IntTy, NULL));
		}

		std::vector<Value *> params;

		params.push_back(compile_current_class());
		params.push_back(compile_id(node->u1.node->u1.node->u2.id));
		params.push_back(compile_id(node->u2.node->u1.node->u2.id));

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

		rb_vm_arity_t arity = rb_vm_node_arity(body);
		const SEL sel = mid_to_sel(mid, arity.real);

		compile_prepare_method(classVal, compile_sel(sel),
			new_function, arity, body);

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
		params.push_back(compile_id(name));

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
		int old_return_from_block = return_from_block;
		BasicBlock *old_rescue_bb = rescue_bb;

		current_mid = 0;
		current_block = true;

		assert(node->nd_body != NULL);
		Value *block = compile_node(node->nd_body);	
		assert(Function::classof(block));

		BasicBlock *return_from_block_bb = NULL;
		if (return_from_block != -1) {
		    // The block we just compiled contains one or more
		    // return expressions! We need to enclose the dispatcher
		    // call inside an exception handler, since return-from
		    // -block is implemented using a C++ exception.
		    Function *f = bb->getParent();
		    rescue_bb = return_from_block_bb = BasicBlock::Create(
			    "return-from-block", f);
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

		    params.push_back(compile_mcache(selEach, false));
		    params.push_back(compile_node(node->nd_iter));
		    params.push_back(compile_sel(selEach));
		    params.push_back(compile_block_create(NULL));
		    params.push_back(ConstantInt::get(Type::Int8Ty, 0));
		    params.push_back(ConstantInt::get(Type::Int32Ty, 0));

		    caller = compile_dispatch_call(params);
		}

		if (return_from_block != -1) {
		    BasicBlock *old_bb = bb;
		    bb = return_from_block_bb;
		    compile_return_from_block_handler(return_from_block);	
		    rescue_bb = old_rescue_bb;
		    bb = old_bb;
		}

		return_from_block = old_return_from_block;
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
		params.insert(params.begin(),
			ConstantInt::get(Type::Int32Ty, argc));

		return compile_protected_call(yieldFunc, params);
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
	    return compile_const(node->nd_mid, compile_nsobject());

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
		params.push_back(compile_mcache(sel, false));
		params.push_back(compile_nsobject());
		params.push_back(compile_sel(sel));
		params.push_back(compile_block_create(NULL));
		params.push_back(ConstantInt::get(Type::Int8Ty, 0));
		params.push_back(ConstantInt::get(Type::Int32Ty, 0));

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
RoxorAOTCompiler::compile_main_function(NODE *node)
{
    current_instance_method = true;

    Value *val = compile_node(node);
    assert(Function::classof(val));
    Function *function = cast<Function>(val);

    BasicBlock::InstListType &list = 
	function->getEntryBlock().getInstList();
    bb = &function->getEntryBlock();

    // Compile method caches.

    Function *getMethodCacheFunc = cast<Function>(module->getOrInsertFunction(
		"rb_vm_get_method_cache",
		PtrTy, PtrTy, NULL));

    for (std::map<SEL, GlobalVariable *>::iterator i = mcaches.begin();
	 i != mcaches.end();
	 ++i) {

	SEL sel = i->first;
	GlobalVariable *gvar = i->second;

	std::vector<Value *> params;
	Instruction *load = compile_sel(sel, false);
	params.push_back(load);

	Instruction *call = CallInst::Create(getMethodCacheFunc,
		params.begin(), params.end(), "");

	Instruction *assign = new StoreInst(call, gvar, "");

	list.insert(list.begin(), assign);
	list.insert(list.begin(), call);
	list.insert(list.begin(), load);
    }

    // Compile constant caches.
	
    Function *getConstCacheFunc = cast<Function>(module->getOrInsertFunction(
		"rb_vm_get_constant_cache",
		PtrTy, PtrTy, NULL));

    for (std::map<ID, GlobalVariable *>::iterator i = ccaches.begin();
	 i != ccaches.end();
	 ++i) {

	ID name = i->first;
	GlobalVariable *gvar = i->second;

	GlobalVariable *const_gvar =
	    compile_const_global_string(rb_id2name(name));

	std::vector<Value *> idxs;
	idxs.push_back(ConstantInt::get(Type::Int32Ty, 0));
	idxs.push_back(ConstantInt::get(Type::Int32Ty, 0));
	Instruction *load = GetElementPtrInst::Create(const_gvar,
		idxs.begin(), idxs.end(), "");

	std::vector<Value *> params;
	params.push_back(load);

	Instruction *call = CallInst::Create(getConstCacheFunc,
		params.begin(), params.end(), "");

	Instruction *assign = new StoreInst(call, gvar, "");
 
	list.insert(list.begin(), assign);
	list.insert(list.begin(), call);
	list.insert(list.begin(), load);
    }

    // Compile selectors.

    Function *registerSelFunc = cast<Function>(module->getOrInsertFunction(
		"sel_registerName",
		PtrTy, PtrTy, NULL));

    for (std::map<SEL, GlobalVariable *>::iterator i = sels.begin();
	 i != sels.end();
	 ++i) {

	SEL sel = i->first;
	GlobalVariable *gvar = i->second;

	GlobalVariable *sel_gvar =
	    compile_const_global_string(sel_getName(sel));

	std::vector<Value *> idxs;
	idxs.push_back(ConstantInt::get(Type::Int32Ty, 0));
	idxs.push_back(ConstantInt::get(Type::Int32Ty, 0));
	Instruction *load = GetElementPtrInst::Create(sel_gvar,
		idxs.begin(), idxs.end(), "");

	std::vector<Value *> params;
	params.push_back(load);

	Instruction *call = CallInst::Create(registerSelFunc, params.begin(),
		params.end(), "");

	Instruction *assign = new StoreInst(call, gvar, "");
 
	list.insert(list.begin(), assign);
	list.insert(list.begin(), call);
	list.insert(list.begin(), load);
    }

    // Compile literals.

    for (std::map<VALUE, GlobalVariable *>::iterator i = literals.begin();
	 i != literals.end();
	 ++i) {

	VALUE val = i->first;
	GlobalVariable *gvar = i->second;

	switch (TYPE(val)) {
	    case T_SYMBOL:
		{
		    if (name2symFunc == NULL) {
			name2symFunc =
			    cast<Function>(module->getOrInsertFunction(
					"rb_name2sym",
					RubyObjTy, PtrTy, NULL));
		    }

		    const char *symname = rb_id2name(SYM2ID(val));
		    			 
		    GlobalVariable *symname_gvar =
			compile_const_global_string(symname);

		    std::vector<Value *> idxs;
		    idxs.push_back(ConstantInt::get(Type::Int32Ty, 0));
		    idxs.push_back(ConstantInt::get(Type::Int32Ty, 0));
		    Instruction *load = GetElementPtrInst::Create(symname_gvar,
			    idxs.begin(), idxs.end(), "");

		    std::vector<Value *> params;
		    params.push_back(load);

		    Instruction *call = CallInst::Create(name2symFunc,
			    params.begin(), params.end(), "");

		    Instruction *assign = new StoreInst(call, gvar, "");

		    list.insert(list.begin(), assign);
		    list.insert(list.begin(), call);
		    list.insert(list.begin(), load);
		}
		break;

	    default:
		printf("unrecognized literal `%s' (class `%s' type %d)\n",
			RSTRING_PTR(rb_inspect(val)),
			rb_obj_classname(val),
			TYPE(val));
		abort();
	}
    }

    // Compile global entries.

    Function *globalEntryFunc = cast<Function>(module->getOrInsertFunction(
		"rb_global_entry",
		PtrTy, IntTy, NULL));

    for (std::map<ID, GlobalVariable *>::iterator i = global_entries.begin();
	 i != global_entries.end();
	 ++i) {

	ID name_id = i->first;
	GlobalVariable *gvar = i->second;

	Value *name_val = compile_id(name_id);
	assert(Instruction::classof(name_val));
	Instruction *name = cast<Instruction>(name_val);
	name->removeFromParent();	

	std::vector<Value *> params;
	params.push_back(name);	

	Instruction *call = CallInst::Create(globalEntryFunc, params.begin(),
		params.end(), "");

	Instruction *assign = new StoreInst(call, gvar, "");
	
	list.insert(list.begin(), assign);
	list.insert(list.begin(), call);
	list.insert(list.begin(), name);
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
	
	GlobalVariable *name_gvar =
	    compile_const_global_string(rb_id2name(name));

	std::vector<Value *> idxs;
	idxs.push_back(ConstantInt::get(Type::Int32Ty, 0));
	idxs.push_back(ConstantInt::get(Type::Int32Ty, 0));
	Instruction *load = GetElementPtrInst::Create(name_gvar,
		idxs.begin(), idxs.end(), "");

	std::vector<Value *> params;
	params.push_back(load);

	Instruction *call = CallInst::Create(rbInternFunc, params.begin(),
		params.end(), "");

	Instruction *assign = new StoreInst(call, gvar, "");
 
	list.insert(list.begin(), assign);
	list.insert(list.begin(), call);
	list.insert(list.begin(), load);
    }

    // Compile NSObject reference.

    Function *objcGetClassFunc = cast<Function>(module->getOrInsertFunction(
		"objc_getClass",
		RubyObjTy, PtrTy, NULL));

    if (cObject_gvar != NULL) {
	GlobalVariable *nsobject = compile_const_global_string("NSObject");

	std::vector<Value *> idxs;
	idxs.push_back(ConstantInt::get(Type::Int32Ty, 0));
	idxs.push_back(ConstantInt::get(Type::Int32Ty, 0));
	Instruction *load = GetElementPtrInst::Create(nsobject,
		idxs.begin(), idxs.end(), "");

	std::vector<Value *> params;
	params.push_back(load);

	Instruction *call = CallInst::Create(objcGetClassFunc, params.begin(),
		params.end(), "");

	Instruction *assign = new StoreInst(call, cObject_gvar, "");

	list.insert(list.begin(), assign);
	list.insert(list.begin(), call);
	list.insert(list.begin(), load);
    }

    // Compile ivar slots.

    if (!ivar_slots_cache.empty()) {
	GlobalVariable *toplevel = compile_const_global_string("TopLevel");

	std::vector<Value *> idxs;
	idxs.push_back(ConstantInt::get(Type::Int32Ty, 0));
	idxs.push_back(ConstantInt::get(Type::Int32Ty, 0));
	Instruction *load = GetElementPtrInst::Create(toplevel,
		idxs.begin(), idxs.end(), "");

	std::vector<Value *> params;
	params.push_back(load);

	Instruction *call = CallInst::Create(objcGetClassFunc, params.begin(),
		params.end(), "");

	compile_ivar_slots(call, list, list.begin());
	ivar_slots_cache.clear();

	list.insert(list.begin(), call);
	list.insert(list.begin(), load);
    }

    for (std::vector<GlobalVariable *>::iterator i = ivar_slots.begin();
	 i != ivar_slots.end();
	 ++i) {

	GlobalVariable *gvar = *i;

	Instruction *call = new MallocInst(Type::Int32Ty, "");
	Instruction *assign1 =
	    new StoreInst(ConstantInt::getSigned(Type::Int32Ty, -1), call, "");
	Instruction *assign2 = new StoreInst(call, gvar, "");

	list.insert(list.begin(), assign2);
	list.insert(list.begin(), assign1);
	list.insert(list.begin(), call);
    }

    bb = NULL;

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
    return SkipStackSize(p2);
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

static inline const char *
rval_to_c_str(VALUE rval)
{
    if (NIL_P(rval)) {
	return NULL;
    }
    else {
	switch (TYPE(rval)) {
	    case T_SYMBOL:
		return rb_sym2name(rval);
	}
	return StringValueCStr(rval);
    }
}

extern "C"
void
rb_vm_rval_to_ocsel(VALUE rval, SEL *ocval)
{
    const char *cstr = rval_to_c_str(rval);
    *ocval = cstr == NULL ? NULL : sel_registerName(cstr);
}

extern "C"
void
rb_vm_rval_to_charptr(VALUE rval, const char **ocval)
{
    *ocval = rval_to_c_str(rval);
}

static inline long
bool_to_fix(VALUE rval)
{
    if (rval == Qtrue) {
	return INT2FIX(1);
    }
    if (rval == Qfalse) {
	return INT2FIX(0);
    }
    return rval;
}

static inline long
rval_to_long(VALUE rval)
{
   return NUM2LONG(rb_Integer(bool_to_fix(rval))); 
}

static inline long long
rval_to_long_long(VALUE rval)
{
    return NUM2LL(rb_Integer(bool_to_fix(rval)));
}

static inline double
rval_to_double(VALUE rval)
{
    return RFLOAT_VALUE(rb_Float(bool_to_fix(rval)));
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

extern "C"
void *
rb_vm_rval_to_cptr(VALUE rval, const char *type, void **cptr)
{
    if (NIL_P(rval)) {
	*cptr = NULL;
    }
    else {
	if (TYPE(rval) == T_ARRAY
	    || rb_boxed_is_type(CLASS_OF(rval), type + 1)) {
	    // A convenience helper so that the user can pass a Boxed or an 
	    // Array object instead of a Pointer to the object.
	    rval = rb_pointer_new2(type + 1, rval);
	}
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

    if (*type == _C_PTR && GET_CORE()->find_bs_cftype(type) != NULL) {
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
		rb_vm_bs_boxed_t *bs_boxed = GET_CORE()->find_bs_struct(type);
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

		    if (GET_CORE()->is_large_struct_type(bs_boxed->type)) {
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
		rb_vm_bs_boxed_t *bs_boxed = GET_CORE()->find_bs_opaque(type);
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

extern "C"
VALUE
rb_vm_new_pointer(const char *type, void *val)
{
    return val == NULL ? Qnil : rb_pointer_new(type, val);
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
RoxorCompiler::compile_new_pointer(const char *type, Value *val)
{
    if (newPointerFunc == NULL) {
	newPointerFunc = cast<Function>(module->getOrInsertFunction(
		    "rb_vm_new_pointer", RubyObjTy, PtrTy, PtrTy, NULL));
    }

    std::vector<Value *> params;

    GlobalVariable *gvar = compile_const_global_string(type);
    std::vector<Value *> idxs;
    idxs.push_back(ConstantInt::get(Type::Int32Ty, 0));
    idxs.push_back(ConstantInt::get(Type::Int32Ty, 0));
    Instruction *load = GetElementPtrInst::Create(gvar,
	    idxs.begin(), idxs.end(), "", bb);
    params.push_back(load);

    params.push_back(val);

    return CallInst::Create(newPointerFunc, params.begin(), params.end(),
	    "", bb); 
}

Value *
RoxorCompiler::compile_conversion_to_ruby(const char *type,
					  const Type *llvm_type, Value *val)
{
    const char *func_name = NULL;

    type = SkipTypeModifiers(type);

    if (*type == _C_PTR && GET_CORE()->find_bs_cftype(type) != NULL) {
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
	    val = BinaryOperator::CreateShl(val, twoVal, "", bb);
	    val = BinaryOperator::CreateOr(val, oneVal, "", bb);
	    return val;

	case _C_UCHR:
	case _C_USHT:
	case _C_UINT:
	    val = new ZExtInst(val, RubyObjTy, "", bb);
	    val = BinaryOperator::CreateShl(val, twoVal, "", bb);
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
	    val = new FPExtInst(val, Type::DoubleTy, "", bb);
	    // fall through	
	case _C_DBL:
	    val = new BitCastInst(val, RubyObjTy, "", bb);
	    val = BinaryOperator::CreateOr(val, threeVal, "", bb);
	    return val;
	    break;

	case _C_SEL:
	    func_name = "rb_vm_sel_to_rval";
	    break;

	case _C_CHARPTR:
	    func_name = "rb_vm_charptr_to_rval";
	    break;

	case _C_STRUCT_B:
	    {
		rb_vm_bs_boxed_t *bs_boxed = GET_CORE()->find_bs_struct(type);
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
		rb_vm_bs_boxed_t *bs_boxed = GET_CORE()->find_bs_opaque(type);
		if (bs_boxed != NULL) {
		    Value *klass = ConstantInt::get(RubyObjTy, bs_boxed->klass);
		    return compile_new_opaque(klass, val);
		}

		return compile_new_pointer(type + 1, val);
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
	    rb_vm_bs_boxed_t *bs_boxed = GET_CORE()->find_bs_struct(type);
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
    if (GET_CORE()->is_large_struct_type(ret_type)) {
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
	p = SkipStackSize(p);
	f_types.push_back(RubyObjTy);
	Value *self_arg = arg++;
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
    bool variadic = false;
    while ((p = GetFirstType(p, buf, sizeof buf)) != NULL && buf[0] != '\0') {
	if (given_argc == argc) {
	    variadic = true;
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

	if (!variadic) {
	    // In order to conform to the ABI, we must stop providing types
	    // once we start dealing with variable arguments and instead mark
	    // the function as variadic.
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

    for (std::vector<unsigned int>::iterator iter = byval_args.begin();
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

Function *
RoxorCompiler::compile_objc_stub(Function *ruby_func, IMP ruby_imp,
	const rb_vm_arity_t &arity, const char *types)
{
    assert(ruby_func != NULL || ruby_imp != NULL);

    char buf[100];
    const char *p = types;
    std::vector<const Type *> f_types;

    // Return value.
    p = GetFirstType(p, buf, sizeof buf);
    std::string ret_type(buf);
    const Type *f_ret_type = convert_type(buf);
    const Type *f_sret_type = NULL;
    if (GET_CORE()->is_large_struct_type(f_ret_type)) {
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
    p = SkipStackSize(p);
    // sel
    f_types.push_back(PtrTy);
    p = SkipFirstType(p);
    p = SkipStackSize(p);
    // Arguments.
    std::vector<std::string> arg_types;
    std::vector<unsigned int> byval_args;
    for (int i = 0; i < arity.real; i++) {
	p = GetFirstType(p, buf, sizeof buf);
	const Type *t = convert_type(buf);
	if (GET_CORE()->is_large_struct_type(t)) {
	    // We are passing a large struct, we need to mark this argument
	    // with the byval attribute and configure the internal stub
	    // call to pass a pointer to the structure, to conform to the ABI.
	    t = PointerType::getUnqual(t);
	    byval_args.push_back(f_types.size() + 1 /* retval */);
	}
	f_types.push_back(t);
	arg_types.push_back(buf);
    }

    // Create the function.
    FunctionType *ft = FunctionType::get(f_ret_type, f_types, false);
    Function *f = cast<Function>(module->getOrInsertFunction("", ft));
    Function::arg_iterator arg = f->arg_begin();

    bb = BasicBlock::Create("EntryBlock", f);

    Value *sret = NULL;
    int sret_i = 0;
    if (f_sret_type != NULL) {
	sret = arg++;
	sret_i = 1;
	f->addAttribute(0, Attribute::StructRet);
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
	if (std::find(byval_args.begin(), byval_args.end(),
		    (unsigned int)i + 3 + sret_i) != byval_args.end()) {
	    a = new LoadInst(a, "", bb);
	}
	Value *ruby_arg = compile_conversion_to_ruby(arg_types[i].c_str(),
		f_types[i + 2 + sret_i], a);
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
    Value *ret_val = CallInst::Create(imp, params.begin(), params.end(),
	    "", bb);

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

Function *
RoxorCompiler::compile_block_caller(rb_vm_block_t *block)
{
    // VALUE foo(VALUE rcv, SEL sel, int argc, VALUE *argv)
    // {
    //     return rb_vm_block_eval2(block, rcv, argc, argv);
    // }
    Function *f = cast<Function>(module->getOrInsertFunction("",
		RubyObjTy, RubyObjTy, PtrTy, Type::Int32Ty, RubyObjPtrTy,
		NULL));
    Function::arg_iterator arg = f->arg_begin();
    Value *rcv = arg++;
    arg++; // sel
    Value *argc = arg++;
    Value *argv = arg++;

    bb = BasicBlock::Create("EntryBlock", f);

    if (blockEvalFunc == NULL) {
	blockEvalFunc = cast<Function>(module->getOrInsertFunction(
		    "rb_vm_block_eval2",
		    RubyObjTy, PtrTy, RubyObjTy, Type::Int32Ty, RubyObjPtrTy,
		    NULL));
    }
    std::vector<Value *> params;
    params.push_back(compile_const_pointer(block));
    params.push_back(rcv);
    params.push_back(argc);
    params.push_back(argv);

    Value *retval = compile_protected_call(blockEvalFunc, params);

    ReturnInst::Create(retval, bb);

    return f;
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
