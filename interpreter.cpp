/*
 * MacRuby Interpreter.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2008-2011, Apple Inc. All rights reserved.
 */

#if !defined(MACRUBY_STATIC)

#include "llvm.h"
#include "macruby_internal.h"
#include "ruby/node.h"
#include "interpreter.h"
#include "vm.h"

#define PRIMITIVE static inline
#include "kernel.c"

// Will be set later, in vm.cpp.
RoxorInterpreter *RoxorInterpreter::shared = NULL;

RoxorInterpreter::RoxorInterpreter(void)
{
    stack = (VALUE *)calloc(INTERPRETER_STACK_SIZE, sizeof(VALUE));
    assert(stack != NULL);
}

RoxorInterpreter::~RoxorInterpreter(void)
{
    free(stack);
}

extern "C" void rb_vm_prepare_method(Class klass, unsigned char dynamic_class,
	SEL sel, Function *func, const rb_vm_arity_t arity, int flags);

#define value_as(val, type) ((type)interpret_value(val))

static inline Value *
call_arg(CallInst *insn, unsigned int i)
{
#if LLVM_TOT
    return insn->getOperand(i + 1);
#else
    return insn->getArgOperand(i);
#endif
}

static inline uint64_t
get_const_int(Value *val)
{
    return cast<ConstantInt>(val)->getZExtValue();
}

// A macro to avoid stupid compiler warnings.
#define oops(msg, val) \
	printf("interpreter: %s (ID: %d)\n", msg, val->getValueID()); \
	val->dump(); \
	abort();

VALUE
RoxorInterpreter::interpret_call(CallInst *call)
{
    Function *called = call->getCalledFunction();

    if (called == RoxorCompiler::shared->prepareMethodFunc) {
	VALUE klass = value_as(call_arg(call, 0), VALUE);
	uint8_t dynamic_class = value_as(call_arg(call, 1), uint8_t);
	SEL sel = value_as(call_arg(call, 2), SEL);
	Function *func = value_as(call_arg(call, 3), Function *);
	uint64_t arity_data = get_const_int(call_arg(call, 4));
	rb_vm_arity_t arity;
	memcpy(&arity, &arity_data, sizeof(rb_vm_arity_t));
	int flags = value_as(call_arg(call, 5), int);

	rb_vm_prepare_method((Class)klass, dynamic_class, sel, func, arity,
		flags);
	return Qnil;
    }
    else if (called == RoxorCompiler::shared->dispatchFunc) {
	VALUE top = value_as(call_arg(call, 0), VALUE);
	VALUE self = value_as(call_arg(call, 1), VALUE);
	void *sel = value_as(call_arg(call, 2), SEL);
	void *block = value_as(call_arg(call, 3), void *);
	uint8_t opt = value_as(call_arg(call, 4), uint8_t);
	int argc = value_as(call_arg(call, 5), int);
	VALUE *argv = value_as(call_arg(call, 6), VALUE *);

	MDNode *node = call->getMetadata(RoxorCompiler::shared->dbg_mdkind);
	if (node != NULL) {
	    std::string path;
	    DILocation loc(node);
	    RoxorCompiler::shared->generate_location_path(path, loc);

	    Frame frame;
	    if (sel != NULL) {
		frame.name = (const char *)sel;
	    }
	    frame.path = path;
	    frame.line = loc.getLineNumber();
	    frames.push_back(frame);
	}

	struct Finally {
	    RoxorInterpreter *ir;
	    bool pop;
	    Finally(RoxorInterpreter *_ir, bool _pop) {
		ir = _ir;
		pop = _pop;	
	    }
	    ~Finally() { 
		if (pop) {
		    ir->frames.pop_back();
		}
	    }
	} finalizer(this, node != NULL);

	return vm_dispatch(top, self, sel, block, opt, argc, argv);
    }
    else if (called == RoxorCompiler::shared->singletonClassFunc) {
	VALUE klass = value_as(call_arg(call, 0), VALUE);

	return rb_singleton_class(klass);
    }
    else if (called == RoxorCompiler::shared->setCurrentOuterFunc) {
	rb_vm_outer_t *outer_stack = value_as(call_arg(call, 0), rb_vm_outer_t *);

	rb_vm_set_current_outer(outer_stack);
	return Qnil;
    }
    else if (called == RoxorCompiler::shared->getBlockFunc) {
	VALUE block = value_as(call_arg(call, 0), VALUE);
	return (VALUE)vm_get_block(block);
    }
    else if (called == RoxorCompiler::shared->currentBlockFunc) {
	return (VALUE)rb_vm_current_block();
    }

    oops("unrecognized call instruction:", call);
}

bool
RoxorInterpreter::frame_at_index(unsigned int idx, void *addr,
	std::string *name, std::string *path, unsigned int *line)
{
    if ((uintptr_t)addr < (uintptr_t)(void *)&vm_dispatch
	    || (uintptr_t)addr > (uintptr_t)(void *)&vm_dispatch + 5000) {
	// Likely not an interpreted dispatch call.
	return false;
    }
    if (idx >= frames.size()) {
	// Not enough frames!
	return false;
    }

    Frame &frame = frames[idx];
    *name = frame.name;
    *path = frame.path;
    *line = frame.line;
    return true;
}

#define return_if_cached(__insn) \
    std::map<Instruction *, VALUE>::iterator __i = insns.find(__insn); \
    if (__i != insns.end()) { \
	return __i->second; \
    } \

VALUE
RoxorInterpreter::interpret_instruction(Instruction *insn)
{
    switch (insn->getOpcode()) {
	case Instruction::Br:
	    {
		BranchInst *br = static_cast<BranchInst *>(insn);
		if (br->isUnconditional()) {
		    BasicBlock *bb = br->getSuccessor(0);
		    assert(bb != NULL);
		    return interpret_basicblock(bb);
		}
		break;
	    }

	case Instruction::Ret:
	    {
		ReturnInst *ret = static_cast<ReturnInst *>(insn);
		Value *retval = ret->getReturnValue();
		assert(retval != NULL);
		return interpret_value(retval);
	    }

	case Instruction::Call:
	    {
		return_if_cached(insn);
		VALUE ret = interpret_call(static_cast<CallInst *>(insn));
		insns[insn] = ret;
		return ret;
	    }

	case Instruction::Alloca:
	    {
		return_if_cached(insn);
		// Assuming the allocated type is VALUE.
		AllocaInst *allocai = static_cast<AllocaInst *>(insn);
		const size_t n = get_const_int(allocai->getArraySize());
		assert(n > 0 && stack_p + n < INTERPRETER_STACK_SIZE);
		VALUE *mem = &stack[stack_p];
		stack_p += n;
		insns[insn] = (VALUE)mem;
		return (VALUE)mem;
	    }

	case Instruction::Store:
	    {
		StoreInst *store = static_cast<StoreInst *>(insn);
		Value *slot = store->getOperand(1);
		GetElementPtrInst *ptr = dyn_cast<GetElementPtrInst>(slot);
		if (ptr == NULL) {
		    break;
		}
		if (ptr->getNumIndices() != 1) {
		    break;
		}
		const size_t off = get_const_int(*ptr->idx_begin());
		VALUE storage = interpret_instruction(cast<AllocaInst>
			(ptr->getPointerOperand()));
		VALUE val = interpret_value(store->getOperand(0));
		((VALUE *)storage)[off] = val;
		return val;
	    }

	// Evaluated later.
	case Instruction::GetElementPtr:
	    return Qnil;
    }

    oops("unrecognized instruction:", insn);
}

VALUE
RoxorInterpreter::interpret_value(Value *val)
{
    unsigned val_id = val->getValueID();
    switch (val_id) {
	case Value::ConstantPointerNullVal:
	    return 0;

	case Value::ConstantExprVal:
	    val = static_cast<ConstantExpr *>(val)->getOperand(0);
	    // fall through
	case Value::ConstantIntVal:
	    {
		ConstantInt *ci = static_cast<ConstantInt *>(val);
		const unsigned w = ci->getBitWidth();
#if __LP64__
		if (w > 64) {
		    break;
		}
#else
		if (w > 32) {
		    break;
		}
#endif
		return ci->getZExtValue();
	    }

	case Value::ArgumentVal:
	    switch (static_cast<Argument *>(val)->getArgNo()) {
		case 0:
		    return self_arg;
		case 1:
		    return (VALUE)sel_arg;
	    }
	    break;

	default:
	    if (val_id >= Value::InstructionVal) {
		return interpret_instruction(static_cast<Instruction *>(val));
	    }
	    break;
    }

    oops("unrecognized value:", val);
}

VALUE
RoxorInterpreter::interpret_basicblock(BasicBlock *bb)
{
    for (BasicBlock::iterator insn = bb->begin(); insn != bb->end(); ++insn) {
	Instruction *i = &*insn;
	VALUE v = interpret_instruction(i);
	if (i->isTerminator()) {	
	    return v;
	}
    }

    oops("malformed block:", bb);
}

VALUE
RoxorInterpreter::interpret(Function *func, VALUE self, SEL sel)
{
    self_arg = self;
    sel_arg = sel;
    stack_p = 0;
    insns.clear();

    BasicBlock &b = func->getEntryBlock();
    return interpret_basicblock(&b);
}

#endif // !MACRUBY_STATIC
