/*
 * MacRuby debugger.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2010-2011, Apple Inc. All rights reserved.
 */

#ifndef __DEBUGGER_H_
#define __DEBUGGER_H_

#if defined(__cplusplus)

#if !defined(MACRUBY_STATIC)

class RoxorBreakPoint {
    public:
	static unsigned int IDs;
	unsigned int id;
	bool enabled;
	std::string condition;

	RoxorBreakPoint(void) {
	    id = ++IDs;
	    enabled = true;
	}
};

class RoxorDebugger {
    private:
	std::map<std::string, RoxorBreakPoint *> breakpoints;
	bool break_at_next;
	std::string location;
	RoxorBreakPoint *breakpoint;
	rb_vm_binding_t *binding;
	unsigned int frame;
	int pipe;

	bool send(std::string &data);
	bool send(const char *str) {
	    std::string s(str);
	    return send(s);
	}
	bool recv(std::string &data);

	unsigned int add_breakpoint(std::string &location);
	RoxorBreakPoint *find_breakpoint(unsigned int bpid);
	bool delete_breakpoint(unsigned int bpid);

	VALUE evaluate_expression(VALUE self, rb_vm_block_t *block,
		int lvars_size, va_list lvars, std::string expr);
	
    public:
	static RoxorDebugger *shared;

	static RoxorDebugger *unix_server(void);

	RoxorDebugger(int pipe);
	~RoxorDebugger();

	void trap(const char *file, const int line, VALUE self,
		rb_vm_block_t *block, int lvars_size, va_list lvars);
};

#endif // !MACRUBY_STATIC

#endif /* __cplusplus */

#endif /* __DEBUGGER_H_ */
