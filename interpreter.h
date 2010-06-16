/*
 * MacRuby Interpreter.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 * 
 * Copyright (C) 2010, Apple Inc. All rights reserved.
 */

#ifndef __INTERPRETER_H_
#define __INTERPRETER_H_

#if defined(__cplusplus)

#if !defined(MACRUBY_STATIC)

class RoxorInterpreter {
    public:
	static RoxorInterpreter *shared;

	RoxorInterpreter(void);
	~RoxorInterpreter(void); 

	VALUE interpret(Function *func, VALUE self, SEL sel);

    private:
	VALUE self_arg;
	SEL sel_arg;
	VALUE *stack;
	unsigned int stack_p;
#define INTERPRETER_STACK_SIZE	1000
	std::map<Instruction *, VALUE> insns;

	VALUE interpret_basicblock(BasicBlock *bb);
	VALUE interpret_instruction(Instruction *insn);
	VALUE interpret_value(Value *val);
	VALUE interpret_call(CallInst *call);
};

#endif // !MACRUBY_STATIC

#endif // __cplusplus

#endif // __INTERPRETER_H_
