/*
 * MacRuby Interpreter.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2008-2011, Apple Inc. All rights reserved.
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

	bool frame_at_index(unsigned int idx, void *addr, std::string *name,
		std::string *path, unsigned int *line);	

    private:
	VALUE self_arg;
	SEL sel_arg;
	VALUE *stack;
	unsigned int stack_p;
#define INTERPRETER_STACK_SIZE	1000
	std::map<Instruction *, VALUE> insns;
	class Frame {
	    public:
		std::string name;
		std::string path;
		unsigned int line;
	};
	std::vector<Frame> frames;

	VALUE interpret_basicblock(BasicBlock *bb);
	VALUE interpret_instruction(Instruction *insn);
	VALUE interpret_value(Value *val);
	VALUE interpret_call(CallInst *call);
};

#endif // !MACRUBY_STATIC

#endif // __cplusplus

#endif // __INTERPRETER_H_
