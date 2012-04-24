/*
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2007-2011, Apple Inc. All rights reserved.
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 * Copyright (C) 2000  Network Applied Communication Laboratory, Inc.
 * Copyright (C) 2000  Information-technology Promotion Agency, Japan
 */

// TODO: rewrite me!

#include "macruby_internal.h"
#include "ruby/signal.h"
#include "ruby/node.h"
#include "id.h"
#include "vm.h"
#include <signal.h>
#include <stdio.h>

#define USE_DEFAULT_HANDLER (void (*)(int))-1

static RETSIGTYPE sighandler(int sig);

static const struct signals {
    const char *signm;
    int  signo;
} siglist [] = {
    {"EXIT",   0},
    {"HUP",    SIGHUP},
    {"INT",    SIGINT},
    {"QUIT",   SIGQUIT},
    {"ILL",    SIGILL},
    {"TRAP",   SIGTRAP},
    {"IOT",    SIGIOT},
    {"ABRT",   SIGABRT},
    {"EMT",    SIGEMT},
    {"FPE",    SIGFPE},
    {"KILL",   SIGKILL},
    {"BUS",    SIGBUS},
    {"SEGV",   SIGSEGV},
    {"SYS",    SIGSYS},
    {"PIPE",   SIGPIPE},
    {"ALRM",   SIGALRM},
    {"TERM",   SIGTERM},
    {"URG",    SIGURG},
    {"STOP",   SIGSTOP},
    {"TSTP",   SIGTSTP},
    {"CONT",   SIGCONT},
    {"CHLD",   SIGCHLD},
    {"CLD",    SIGCHLD},
    {"TTIN",   SIGTTIN},
    {"TTOU",   SIGTTOU},
    {"IO",     SIGIO},
    {"XCPU",   SIGXCPU},
    {"XFSZ",   SIGXFSZ},
    {"VTALRM", SIGVTALRM},
    {"PROF",   SIGPROF},
    {"WINCH",  SIGWINCH},
    {"INFO",   SIGINFO},
    {"USR1",   SIGUSR1},
    {"USR2",   SIGUSR2},
    {NULL, 0}
};

static const struct trap_handlers {
    const char   *command;
    VALUE        new_cmd_value;
    sighandler_t handler;
} gl_trap_handlers[] = {
    {"SYSTEM_DEFAULT", 0,      SIG_DFL},
    {"SIG_IGN",        0,      SIG_IGN},
    {"IGNORE",         0,      SIG_IGN},
    {"",               0,      SIG_IGN},
    {"SIG_DFL",        0,      USE_DEFAULT_HANDLER},
    {"DEFAULT",        0,      USE_DEFAULT_HANDLER},
    {"EXIT",           Qundef, sighandler},
    {NULL, 0}
 };

static int
signm2signo(const char *nm)
{
    const struct signals *sigs;

    for (sigs = siglist; sigs->signm; sigs++)
	if (strcmp(sigs->signm, nm) == 0)
	    return sigs->signo;
    return 0;
}

static const char*
signo2signm(int no)
{
    const struct signals *sigs;

    for (sigs = siglist; sigs->signm; sigs++)
	if (sigs->signo == no)
	    return sigs->signm;
    return 0;
}

const char *
ruby_signal_name(int no)
{
    return signo2signm(no);
}

/*
 * call-seq:
 *    SignalException.new(sig)   =>  signal_exception
 *
 *  Construct a new SignalException object.  +sig+ should be a known
 *  signal name, or a signal number.
 */

static VALUE
esignal_init(VALUE self, SEL sel, int argc, VALUE *argv)
{
    int argnum = 1;
    VALUE sig = Qnil;
    int signo;
    const char *signm;

    if (argc > 0) {
	sig = rb_check_to_integer(argv[0], "to_int");
	if (!NIL_P(sig)) argnum = 2;
	else sig = argv[0];
    }
    if (argc < 1 || argnum < argc) {
	rb_raise(rb_eArgError, "wrong number of arguments (%d for %d)",
		 argc, argnum);
    }
    if (argnum == 2) {
	signo = NUM2INT(sig);
	if (signo < 0 || signo > NSIG) {
	    rb_raise(rb_eArgError, "invalid signal number (%d)", signo);
	}
	if (argc > 1) {
	    sig = argv[1];
	}
	else {
	    signm = signo2signm(signo);
	    if (signm) {
		sig = rb_sprintf("SIG%s", signm);
	    }
	    else {
		sig = rb_sprintf("SIG%u", signo);
	    }
	}
    }
    else {
	signm = SYMBOL_P(sig) ? rb_sym2name(sig) : StringValuePtr(sig);
	if (strncmp(signm, "SIG", 3) == 0) signm += 3;
	signo = signm2signo(signm);
	if (!signo) {
	    rb_raise(rb_eArgError, "unsupported name `SIG%s'", signm);
	}
	sig = rb_sprintf("SIG%s", signm);
    }
    rb_vm_call_super(self, selInitialize2, 1, &sig);
    rb_iv_set(self, "signo", INT2NUM(signo));

    return self;
}

/*
 * call-seq:
 *    signal_exception.signo   =>  num
 *
 *  Returns a signal number.
 */

static VALUE
esignal_signo(VALUE self, SEL sel)
{
    return rb_iv_get(self, "signo");
}

static VALUE
interrupt_init(VALUE self, SEL sel, int argc, VALUE *argv)
{
    VALUE args[2];

    args[0] = INT2FIX(SIGINT);
    rb_scan_args(argc, argv, "01", &args[1]);
    return rb_vm_call_super(self, selInitialize2, 2, args);
}

void
ruby_default_signal(int sig)
{
#ifndef MACOS_UNUSE_SIGNAL
    signal(sig, SIG_DFL);
    raise(sig);
#endif
}

/*
 *  call-seq:
 *     Process.kill(signal, pid, ...)    => fixnum
 *  
 *  Sends the given signal to the specified process id(s), or to the
 *  current process if _pid_ is zero. _signal_ may be an
 *  integer signal number or a POSIX signal name (either with or without
 *  a +SIG+ prefix). If _signal_ is negative (or starts
 *  with a minus sign), kills process groups instead of
 *  processes. Not all signals are available on all platforms.
 *     
 *     pid = fork do
 *        Signal.trap("HUP") { puts "Ouch!"; exit }
 *        # ... do some work ...
 *     end
 *     # ...
 *     Process.kill("HUP", pid)
 *     Process.wait
 *     
 *  <em>produces:</em>
 *     
 *     Ouch!
 */

VALUE
rb_f_kill(VALUE self, SEL sel, int argc, VALUE *argv)
{
    int negative = 0;
    int sig;
    int i;
    int type;
    const char *s = NULL;

    rb_secure(2);
    if (argc < 2)
	rb_raise(rb_eArgError, "wrong number of arguments -- kill(sig, pid...)");

    type = TYPE(argv[0]);
    if (type == T_FIXNUM) {
	sig = FIX2INT(argv[0]);
    }
    else {
	if (type == T_SYMBOL) {
	    s = rb_sym2name(argv[0]);
	    if (!s)
		rb_raise(rb_eArgError, "bad signal");
	}
	else if (type == T_STRING) {
	    s = RSTRING_PTR(argv[0]);
	    if (s[0] == '-') {
		negative++;
		s++;
	    }
	}
	else {
	    VALUE str;
	    str = rb_check_string_type(argv[0]);
	    if (!NIL_P(str)) {
		s = RSTRING_PTR(str);
	    }
	}
	if (s == NULL)
	    rb_raise(rb_eArgError, "bad signal type %s", rb_obj_classname(argv[0]));

	if (strncmp("SIG", s, 3) == 0)
	    s += 3;
	if ((sig = signm2signo(s)) == 0)
	    rb_raise(rb_eArgError, "unsupported name `SIG%s'", s);
	if (negative)
	    sig = -sig;
    }

    if (sig < 0) {
	sig = -sig;
	for (i = 1; i < argc; i++) {
	    if (killpg(NUM2PIDT(argv[i]), sig) < 0)
		rb_sys_fail(0);
	}
    }
    else {
	for (i = 1; i < argc; i++) {
	    if (kill(NUM2PIDT(argv[i]), sig) < 0)
		rb_sys_fail(0);
	}
    }
    rb_thread_polling();
    return INT2FIX(i - 1);
}

VALUE
rb_get_trap_cmd(int sig)
{
    return rb_vm_trap_cmd_for_signal(sig);
}

sighandler_t
ruby_signal(int signum, sighandler_t handler)
{
    struct sigaction sigact;
    struct sigaction old;

    sigemptyset(&sigact.sa_mask);
    sigact.sa_handler = handler;
    sigact.sa_flags = 0;

    if (signum == SIGCHLD && handler == SIG_IGN)
	sigact.sa_flags |= SA_NOCLDWAIT;
    if (signum == SIGSEGV)
	sigact.sa_flags |= SA_ONSTACK;
    if (sigaction(signum, &sigact, &old) < 0) {
	if (errno != 0 && errno != EINVAL) {
	    rb_bug("sigaction error.\n");
	}
    }
    return old.sa_handler;
}

sighandler_t
posix_signal(int signum, sighandler_t handler)
{
    return ruby_signal(signum, handler);
}

static void signal_exec(VALUE cmd, int level, int sig);
static void rb_signal_exec(int sig);

static RETSIGTYPE
sighandler(int sig)
{
    int olderrno = errno;
    rb_signal_exec(sig);
    ruby_signal(sig, sighandler);
    errno = olderrno;
}

#include <pthread.h>

void
rb_disable_interrupt(void)
{
    sigset_t mask;
    sigfillset(&mask);
    sigdelset(&mask, SIGVTALRM);
    sigdelset(&mask, SIGSEGV);
    pthread_sigmask(SIG_SETMASK, &mask, NULL);
}

void
rb_enable_interrupt(void)
{
    sigset_t mask;
    sigemptyset(&mask);
    pthread_sigmask(SIG_SETMASK, &mask, NULL);
}

static RETSIGTYPE
sigpipe(int sig)
{
    /* do nothing */
}

static void
signal_exec(VALUE cmd, int level, int sig)
{
    VALUE signum = INT2NUM(sig);
    rb_eval_cmd(cmd, rb_ary_new3(1, signum), level);
}

void
rb_trap_exit(void)
{
#if 0//ndef MACOS_UNUSE_SIGNAL
    VALUE trap_exit;
    int safe;

    trap_exit = rb_vm_trap_cmd_for_signal(0);
    if (trap_exit != (VALUE)NULL) {
	safe = rb_vm_trap_level_for_signal(0);
	rb_vm_set_trap_for_signal((VALUE)0, safe, 0);
	signal_exec(trap_exit, safe, 0);
    }
#endif
}

static void
rb_signal_exec(int sig)
{
/*     rb_vm_thread_t *t; */
/*     VALUE exc; */
    VALUE cmd = rb_get_trap_cmd(sig);

    if (cmd == 0) {
	switch (sig) {
	  case SIGINT:
	    rb_interrupt();
	    break;
	  case SIGHUP:
	  case SIGQUIT:
	  case SIGTERM:
	  case SIGALRM:
	  case SIGUSR1:
	  case SIGUSR2:
/* 	    t = GetThreadPtr(rb_vm_main_thread()); */
/* 	    exc = rb_exc_new2(rb_eSignal, ruby_signal_name(sig)); */
/* 	    rb_vm_thread_raise(t, exc); */
/* 	    rb_raise(rb_eSignal, "%s", signo2signm(sig)); */
	    break;
	}
    }
    else if (cmd == Qundef) {
	//rb_thread_signal_exit(th);
    }
    else {
	signal_exec(cmd, rb_vm_trap_level_for_signal(sig), sig);
    }
}

struct trap_arg {
    int sig;
    sighandler_t func;
    VALUE cmd;
};

static sighandler_t
default_handler(int sig)
{
    sighandler_t func;
    switch (sig) {
      case SIGINT:
      case SIGHUP:
      case SIGQUIT:
      case SIGTERM:
      case SIGALRM:
      case SIGUSR1:
      case SIGUSR2:
        func = sighandler;
        break;
      case SIGPIPE:
        func = sigpipe;
        break;
      default:
        func = SIG_DFL;
        break;
    }

    return func;
}

static sighandler_t
trap_handler(VALUE *cmd, int sig)
{
    sighandler_t func = sighandler;
    VALUE command;

    if (NIL_P(*cmd)) {
	func = SIG_IGN;
    }
    else {
	command = rb_check_string_type(*cmd);
	if (NIL_P(command) && SYMBOL_P(*cmd)) {
	    command = rb_id2str(SYM2ID(*cmd));
	    if (!command) rb_raise(rb_eArgError, "bad handler");
	}
	if (!NIL_P(command)) {
	    SafeStringValue(command);	/* taint check */
	    *cmd = command;
	    for (int i = 0; gl_trap_handlers[i].command != NULL; i++) {
		if (strcmp(gl_trap_handlers[i].command, RSTRING_PTR(command)) == 0) {
		    func = gl_trap_handlers[i].handler;
		    if (func == USE_DEFAULT_HANDLER) {
			func = default_handler(sig);
		    }
		    *cmd = gl_trap_handlers[i].new_cmd_value;
		    break;
		}
	    }
	}
	else {
/* 	    func = sighandler; */
	}
    }

    return func;
}

static int
trap_signm(VALUE vsig)
{
    int sig = -1;
    const char *s;

    if (TYPE(vsig) == T_FIXNUM) {
	sig = FIX2INT(vsig);
	if (sig < 0 || sig >= NSIG) {
	    rb_raise(rb_eArgError, "invalid signal number (%d)", sig);
	}
    }
    else {
	if (TYPE(vsig) == T_SYMBOL) {
	    s = rb_sym2name(vsig);
	    if (s == NULL)
		rb_raise(rb_eArgError, "bad signal");
	}
	else
	    s = StringValuePtr(vsig);

	if (strncmp("SIG", s, 3) == 0)
	    s += 3;
	sig = signm2signo(s);
	if (sig == 0 && strcmp(s, "EXIT") != 0)
	    rb_raise(rb_eArgError, "unsupported signal SIG%s", s);
    }
    return sig;
}

static VALUE
trap(struct trap_arg *arg)
{
    sighandler_t oldfunc;
    sighandler_t func = arg->func;
    VALUE oldcmd;
    VALUE command = arg->cmd;
    int sig = arg->sig;

    oldfunc = ruby_signal(sig, func);
    oldcmd = rb_vm_trap_cmd_for_signal(sig);
    if (oldcmd == 0) {
	if (oldfunc == SIG_IGN)
	    oldcmd = rb_str_new2("IGNORE");
	else if (oldfunc == sighandler)
	    oldcmd = rb_str_new2("DEFAULT");
	else
	    oldcmd = Qnil;
    }
    else if (oldcmd == Qundef)
	oldcmd = rb_str_new2("EXIT");

    // Assign trap to signal
    rb_vm_set_trap_for_signal(command, rb_safe_level(), sig);

    return oldcmd;
}

void
rb_trap_restore_mask(void)
{
}

/*
 * call-seq:
 *   Signal.trap( signal, command ) => obj
 *   Signal.trap( signal ) {| | block } => obj
 *
 * Specifies the handling of signals. The first parameter is a signal
 * name (a string such as ``SIGALRM'', ``SIGUSR1'', and so on) or a
 * signal number. The characters ``SIG'' may be omitted from the
 * signal name. The command or block specifies code to be run when the
 * signal is raised.
 * If the command is the string ``IGNORE'' or ``SIG_IGN'', the signal
 * will be ignored.
 * If the command is ``DEFAULT'' or ``SIG_DFL'', the Ruby's default handler
 * will be invoked.
 * If the command is ``EXIT'', the script will be terminated by the signal.
 * If the command is ``SYSTEM_DEFAULT'', the operating system's default
 * handler will be invoked.
 * Otherwise, the given command or block will be run.
 * The special signal name ``EXIT'' or signal number zero will be
 * invoked just prior to program termination.
 * trap returns the previous handler for the given signal.
 *
 *     Signal.trap(0, proc { puts "Terminating: #{$$}" })
 *     Signal.trap("CLD")  { puts "Child died" }
 *     fork && Process.wait
 *
 * produces:
 *     Terminating: 27461
 *     Child died
 *     Terminating: 27460
 */

static VALUE
sig_trap(VALUE rcv, SEL sel, int argc, VALUE *argv)
{
    struct trap_arg arg;

    rb_secure(2);
    if (argc == 0 || argc > 2) {
	rb_raise(rb_eArgError, "wrong number of arguments -- trap(sig, cmd)/trap(sig){...}");
    }

    arg.sig = trap_signm(argv[0]);
    if (argc == 1) {
	arg.cmd = rb_block_proc();
	arg.func = sighandler;
    }
    else {
	arg.cmd = argv[1];
	arg.func = trap_handler(&arg.cmd, arg.sig);
    }

    if (OBJ_TAINTED(arg.cmd)) {
	rb_raise(rb_eSecurityError, "Insecure: tainted signal trap");
    }

    return trap(&arg);
}

/*
 * call-seq:
 *   Signal.list => a_hash
 *
 * Returns a list of signal names mapped to the corresponding
 * underlying signal numbers.
 *
 * Signal.list   #=> {"ABRT"=>6, "ALRM"=>14, "BUS"=>7, "CHLD"=>17, "CLD"=>17, "CONT"=>18, "FPE"=>8, "HUP"=>1, "ILL"=>4, "INT"=>2, "IO"=>29, "IOT"=>6, "KILL"=>9, "PIPE"=>13, "POLL"=>29, "PROF"=>27, "PWR"=>30, "QUIT"=>3, "SEGV"=>11, "STOP"=>19, "SYS"=>31, "TERM"=>15, "TRAP"=>5, "TSTP"=>20, "TTIN"=>21, "TTOU"=>22, "URG"=>23, "USR1"=>10, "USR2"=>12, "VTALRM"=>26, "WINCH"=>28, "XCPU"=>24, "XFSZ"=>25}
 */
static VALUE
sig_list(VALUE rcv, SEL sel)
{
    VALUE h = rb_hash_new();
    const struct signals *sigs;

    for (sigs = siglist; sigs->signm; sigs++) {
	rb_hash_aset(h, rb_str_new2(sigs->signm), INT2FIX(sigs->signo));
    }
    return h;
}

static void
install_sighandler(int signum, sighandler_t handler)
{
    sighandler_t old;

    old = ruby_signal(signum, handler);
    if (old != SIG_DFL) {
	ruby_signal(signum, old);
    }
}

#if defined(SIGCLD) || defined(SIGCHLD)
static void
init_sigchld(int sig)
{
    sighandler_t oldfunc;

    oldfunc = ruby_signal(sig, SIG_DFL);
    if (oldfunc != SIG_DFL && oldfunc != SIG_IGN) {
	ruby_signal(sig, oldfunc);
    } else {
	rb_vm_set_trap_for_signal((VALUE)0, rb_safe_level(), sig);
    }
}
#endif

void
ruby_sig_finalize()
{
    sighandler_t oldfunc;

    oldfunc = ruby_signal(SIGINT, SIG_IGN);
    if (oldfunc == sighandler) {
	ruby_signal(SIGINT, SIG_DFL);
    }
}

#ifdef RUBY_DEBUG_ENV
int ruby_enable_coredump = 0;
#endif

/*
 * Many operating systems allow signals to be sent to running
 * processes. Some signals have a defined effect on the process, while
 * others may be trapped at the code level and acted upon. For
 * example, your process may trap the USR1 signal and use it to toggle
 * debugging, and may use TERM to initiate a controlled shutdown.
 *
 *     pid = fork do
 *       Signal.trap("USR1") do
 *         $debug = !$debug
 *         puts "Debug now: #$debug"
 *       end
 *       Signal.trap("TERM") do
 *         puts "Terminating..."
 *         shutdown()
 *       end
 *       # . . . do some work . . .
 *     end
 *
 *     Process.detach(pid)
 *
 *     # Controlling program:
 *     Process.kill("USR1", pid)
 *     # ...
 *     Process.kill("USR1", pid)
 *     # ...
 *     Process.kill("TERM", pid)
 *
 * produces:
 *     Debug now: true
 *     Debug now: false
 *    Terminating...
 *
 * The list of available signal names and their interpretation is
 * system dependent. Signal delivery semantics may also vary between
 * systems; in particular signal delivery may not always be reliable.
 */
void
Init_signal(void)
{
#ifndef MACOS_UNUSE_SIGNAL
    VALUE mSignal = rb_define_module("Signal");

    rb_objc_define_module_function(rb_mKernel, "trap", sig_trap, -1);
    rb_objc_define_method(*(VALUE *)mSignal, "trap", sig_trap, -1);
    rb_objc_define_method(*(VALUE *)mSignal, "list", sig_list, 0);

    rb_objc_define_method(rb_eSignal, "initialize", esignal_init, -1);
    rb_objc_define_method(rb_eSignal, "signo", esignal_signo, 0);
    rb_alias(rb_eSignal, rb_intern("signm"), rb_intern("message"));
    rb_objc_define_method(rb_eInterrupt, "initialize", interrupt_init, -1);

/*     install_sighandler(SIGINT, sighandler); */
/*     install_sighandler(SIGHUP, sighandler); */
/*     install_sighandler(SIGQUIT, sighandler); */
/*     install_sighandler(SIGTERM, sighandler); */
/*     install_sighandler(SIGALRM, sighandler); */
/*     install_sighandler(SIGUSR1, sighandler); */
/*     install_sighandler(SIGUSR2, sighandler); */

#ifdef RUBY_DEBUG_ENV
    if (!ruby_enable_coredump)
#endif
    install_sighandler(SIGPIPE, sigpipe);

    init_sigchld(SIGCHLD);
#endif /* !MACOS_UNUSE_SIGNAL */
}
