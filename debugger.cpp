/*
 * MacRuby debugger.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2010-2011, Apple Inc. All rights reserved.
 */

#if !defined(MACRUBY_STATIC)

#include <llvm/Module.h>
#include <llvm/DerivedTypes.h>
#include <llvm/Constants.h>
#include <llvm/CallingConv.h>
#include <llvm/Instructions.h>
#include <llvm/Intrinsics.h>
#include <llvm/Analysis/DebugInfo.h>
#if !defined(LLVM_TOT)
# include <llvm/Analysis/DIBuilder.h>
#endif
#include <llvm/ExecutionEngine/JIT.h>
#include <llvm/PassManager.h>
#include <llvm/Target/TargetData.h>
using namespace llvm;

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "macruby_internal.h"
#include "ruby/node.h"
#include "vm.h"
#include "compiler.h"
#include "debugger.h"

void
RoxorCompiler::compile_debug_trap(void)
{
    if (bb == NULL || inside_eval) {
	return;
    }

    if (debugTrapFunc == NULL) {
	// void rb_vm_debug_trap(const char *file, int line,
	//	VALUE self, rb_vm_block_t *current_block, int lvars_size, ...);
	std::vector<const Type *> types;
	types.push_back(PtrTy);
	types.push_back(Int32Ty);
	types.push_back(RubyObjTy);
	types.push_back(PtrTy);
	types.push_back(Int32Ty);
	FunctionType *ft = FunctionType::get(VoidTy, types, true);
	debugTrapFunc = cast<Function>
	    (module->getOrInsertFunction( "rb_vm_debug_trap", ft));
    }

    std::vector<Value *> params;
    params.push_back(compile_const_pointer((void *)fname));
    params.push_back(ConstantInt::get(Int32Ty, current_line));
    params.push_back(current_self);
    params.push_back(running_block == NULL
	    ? compile_const_pointer(NULL) : running_block);

    // Lvars.
    params.push_back(ConstantInt::get(Int32Ty, (int)lvars.size()));
    for (std::map<ID, Value *>::iterator iter = lvars.begin();
	 iter != lvars.end(); ++iter) {

	ID lvar = iter->first;
	params.push_back(compile_id(lvar));
	params.push_back(iter->second);
    }

    CallInst::Create(debugTrapFunc, params.begin(), params.end(), "", bb);
}

extern "C"
void
rb_vm_debug_trap(const char *file, const int line, VALUE self,
	rb_vm_block_t *current_block, int lvars_size, ...)
{
    if (RoxorDebugger::shared == NULL) {
	RoxorDebugger::shared = RoxorDebugger::unix_server();
    }

    va_list args;
    va_start(args, lvars_size);
    RoxorDebugger::shared->trap(file, line, self, current_block,
	    lvars_size, args);
    va_end(args);
}

unsigned int RoxorBreakPoint::IDs = 0;
RoxorDebugger *RoxorDebugger::shared = NULL;

RoxorDebugger *
RoxorDebugger::unix_server(void)
{
    // Create the socket.
    const int fd = socket(PF_LOCAL, SOCK_STREAM, 0);
    if (fd < 0) {
	perror("socket()");
	exit(1);
    }

    // Bind it to the given path.
    assert(ruby_debug_socket_path != Qfalse);
    const char *path = RSTRING_PTR(ruby_debug_socket_path);
    struct sockaddr_un name;
    name.sun_family = PF_LOCAL;
    strncpy(name.sun_path, path, sizeof(name.sun_path));
    if (bind(fd, (struct sockaddr *)&name, SUN_LEN(&name)) != 0) {
	perror("bind()");
	exit(1);
    }

    // Prepare for listening, backlog of 1 connection.
    if (listen(fd, 1) != 0) {
	perror("listen()");
	exit(1);
    }

    // Accept first connection.
    struct sockaddr_un remote;
    socklen_t remotelen = sizeof(remote);
    const int pipe = accept(fd, (struct sockaddr *)&remote, &remotelen);
    if (socket < 0) {
	perror("accept()");
	exit(1);
    }

//printf("DBG: connected with fd %d\n", pipe);

    return new RoxorDebugger(pipe);
}

RoxorDebugger::RoxorDebugger(int _pipe)
{
    pipe = _pipe;
    binding = NULL;
    breakpoint = NULL;
    frame = 0;

    // We want to break at the very first line.
    break_at_next = true;
}

RoxorDebugger::~RoxorDebugger()
{
    close(pipe);
}

bool
RoxorDebugger::send(std::string &data)
{
//printf("DBG: send %s\n", data.c_str());
    const ssize_t len = ::send(pipe, data.c_str(), data.length(), 0);
    if (len != (ssize_t)data.length()) {
	if (len == -1) {
	    perror("send()");
	}
	else {
	    fprintf(stderr, "expected to send %ld bytes instead of %ld\n",
		    data.length(), len);
	}
	return false;	
    }
    return true;
}

bool
RoxorDebugger::recv(std::string &data)
{
    char buf[512];
    const ssize_t len = ::recv(pipe, buf, sizeof buf, 0);
    if (len == -1) {
	perror("recv()");
	return false;
    }
    data.clear();
    data.append(buf, len);
//printf("DBG: recv %s\n", data.c_str());
    return true;
}

void 
RoxorDebugger::trap(const char *file, const int line, VALUE self,
	rb_vm_block_t *block, int lvars_size, va_list lvars)
{
    // Compute location.
    char buf[100];
    snprintf(buf, sizeof buf, "%s:%d", file, line);
    std::string loc(buf);

    // Should we break here?
    bool should_break = false;
    RoxorBreakPoint *bp = NULL;
    if (break_at_next) {
	should_break = true;
	break_at_next = false;
    }
    else {
	std::map<std::string, RoxorBreakPoint *>::iterator iter =
	    breakpoints.find(loc);
	if (iter != breakpoints.end()) {
	    bp = iter->second;
	    if (bp->enabled) {
		if (bp->condition.length() > 0) {
		    VALUE obj = evaluate_expression(self, block, lvars_size,
			    lvars, bp->condition);
		    if (obj != Qnil && obj != Qfalse) {
			should_break = true;
		    }
		}
		else {
		    should_break = true;
		}
	    }
	}
    }

    if (!should_break) {
	return;
    }

    // Send the current location to the client.
    if (!send(loc)) {
	return;
    }

    // Invalidate latest binding.
    if (binding != NULL) {
	GC_RELEASE(binding);
	binding = NULL;
    }
    frame = 0;

    // Enter the main loop.
    std::string data;
    while (true) {
	// Receive command.
	if (!recv(data)) {
	    return;
	}
	const char *cmd = data.c_str();

	// Handle command.
	if (strcmp(cmd, "exit") == 0) {
	    exit(0);
	}
	if (strcmp(cmd, "continue") == 0) {
	    break;
	}
	if (strcmp(cmd, "next") == 0) {
	    break_at_next = true;
	    break;
	}
	if (strncmp(cmd, "break ", 6) == 0) {
	    std::string location = data.substr(6);
	    if (location.length() >= 3) {
		unsigned int id = add_breakpoint(location);
		char buf[10];
		snprintf(buf, sizeof buf, "%d", id);
		send(buf);
		continue;
	    }
	}
	if (strncmp(cmd, "enable ", 7) == 0) {
	    const unsigned int bpid = (unsigned int)strtol(cmd + 7, NULL, 10);
	    RoxorBreakPoint *bp = find_breakpoint(bpid);
	    if (bp != NULL) {
		bp->enabled = true;
	    }
	    continue;
	}
	if (strncmp(cmd, "disable ", 8) == 0) {
	    const unsigned int bpid = (unsigned int)strtol(cmd + 8, NULL, 10);
	    RoxorBreakPoint *bp = find_breakpoint(bpid);
	    if (bp != NULL) {
		bp->enabled = false;
	    }
	    continue;
	}
	if (strncmp(cmd, "delete ", 7) == 0) {
	    const unsigned int bpid = (unsigned int)strtol(cmd + 7, NULL, 10);
	    delete_breakpoint(bpid);
	    continue;
	}
	if (strncmp(cmd, "condition ", 10) == 0) {
	    const char *p = strchr(cmd + 10, ' ');
	    if (p != NULL) {
		std::string bpstr = data.substr(10, p - cmd - 10);
		const unsigned int bpid = (unsigned int)strtol(bpstr.c_str(),
			NULL, 10);
		std::string condstr = data.substr(10 + bpstr.length());
		RoxorBreakPoint *bp = find_breakpoint(bpid);
		if (bp != NULL) {
		    bp->condition = condstr;
		}
		continue;
	    }
	} 
	if (strcmp(cmd, "info breakpoints") == 0) {
	    std::string resp;
	    for (std::map<std::string, RoxorBreakPoint *>::iterator iter
		    = breakpoints.begin();
		    iter != breakpoints.end();
		    ++iter) {
		char buf[100];
		snprintf(buf, sizeof buf, "id=%d,location=%s,enabled=%d\n",
			iter->second->id, iter->first.c_str(),
			iter->second->enabled);
		resp.append(buf);
	    }
	    if (resp.length() == 0) {
		resp.append("nil");
	    }
	    send(resp);
	    continue;
	}
	if (strcmp(cmd, "backtrace") == 0) {
	    VALUE bt = rb_vm_backtrace(0);
	    VALUE str = rb_ary_join(bt, rb_str_new2("\n"));
	    send(RSTRING_PTR(str));
	    continue;
	}
	if (strncmp(cmd, "frame ", 6) == 0) {
	    unsigned int frid = (unsigned int)strtol(cmd + 6, NULL, 10);
	    if (frame != frid) {
		frame = frid;
		if (binding != NULL) {
		    GC_RELEASE(binding);
		    binding = NULL;
		}
		// TODO update location
	    }
	    continue;
	}
	if (strncmp(cmd, "eval ", 5) == 0) {
	    std::string expr = data.substr(5);
	    if (expr.length() > 0) {
		VALUE obj = evaluate_expression(self, block, lvars_size, lvars,
			expr);
		send(RSTRING_PTR(rb_inspect(obj)));
		continue;
	    }
	} 

	// Unknown command, let's exit the program. This should never happen!
	fprintf(stderr, "unknown command: %s\n", cmd);
	exit(1); 
    }
}

unsigned int
RoxorDebugger::add_breakpoint(std::string &location)
{
    std::map<std::string, RoxorBreakPoint *>::iterator iter
	= breakpoints.find(location);

    RoxorBreakPoint *bp;
    if (iter == breakpoints.end()) {
	bp = new RoxorBreakPoint();
	breakpoints[location] = bp;
    }
    else {
	bp = iter->second;
    }

    return bp->id;
}

VALUE
RoxorDebugger::evaluate_expression(VALUE self, rb_vm_block_t *block,
	int lvars_size, va_list lvars, std::string expr)
{
    if (binding == NULL) {
	if (frame == 0) {
	    binding = rb_vm_create_binding(self, block, NULL, NULL, lvars_size, lvars,
		    false);
	}
	else {
	    binding = GET_VM()->get_binding(frame - 1);
	    assert(binding != NULL);
	}
	GC_RETAIN(binding);
    }

    try {
	return rb_vm_eval_string(self, 0, rb_str_new2(expr.c_str()), binding,
		"(eval)", 1, false);
    }
    catch (...) {
	rb_vm_print_current_exception();
	return Qnil;
    }
}

RoxorBreakPoint *
RoxorDebugger::find_breakpoint(unsigned int bpid)
{
    for (std::map<std::string, RoxorBreakPoint *>::iterator iter
	    = breakpoints.begin();
	    iter != breakpoints.end();
	    ++iter) {
	if (iter->second->id == bpid) {
	    return iter->second;
	}
    }
    return NULL;
}

bool
RoxorDebugger::delete_breakpoint(unsigned int bpid)
{
    for (std::map<std::string, RoxorBreakPoint *>::iterator iter
	    = breakpoints.begin();
	    iter != breakpoints.end();
	    ++iter) {
	if (iter->second->id == bpid) {
	    breakpoints.erase(iter);
	    return true;
	}
    }
    return false;
}

#endif // !MACRUBY_STATIC
