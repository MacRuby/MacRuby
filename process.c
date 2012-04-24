/*
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2007-2011, Apple Inc. All rights reserved.
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 * Copyright (C) 2000  Network Applied Communication Laboratory, Inc.
 * Copyright (C) 2000  Information-technology Promotion Agency, Japan
 */

#include "macruby_internal.h"
#include "ruby/signal.h"
#include "ruby/io.h"
#include "ruby/util.h"
#include "id.h"

#include <stdio.h>
#include <errno.h>
#include <signal.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef __DJGPP__
#include <process.h>
#endif

#include <time.h>
#include <ctype.h>

#ifndef EXIT_SUCCESS
#define EXIT_SUCCESS 0
#endif
#ifndef EXIT_FAILURE
#define EXIT_FAILURE 1
#endif

struct timeval rb_time_interval(VALUE);

#ifdef HAVE_SYS_WAIT_H
# include <sys/wait.h>
#endif
#ifdef HAVE_SYS_RESOURCE_H
# include <sys/resource.h>
#endif
#ifdef HAVE_SYS_PARAM_H
# include <sys/param.h>
#endif
#ifndef MAXPATHLEN
# define MAXPATHLEN 1024
#endif
#include "ruby/st.h"

#ifdef __EMX__
#undef HAVE_GETPGRP
#endif

#include <sys/stat.h>

#ifdef HAVE_SYS_TIMES_H
#include <sys/times.h>
#endif

#ifdef HAVE_GRP_H
#include <grp.h>
#endif

#if defined(HAVE_TIMES) || defined(_WIN32)
static VALUE rb_cProcessTms;
#endif

#ifndef WIFEXITED
#define WIFEXITED(w)    (((w) & 0xff) == 0)
#endif
#ifndef WIFSIGNALED
#define WIFSIGNALED(w)  (((w) & 0x7f) > 0 && (((w) & 0x7f) < 0x7f))
#endif
#ifndef WIFSTOPPED
#define WIFSTOPPED(w)   (((w) & 0xff) == 0x7f)
#endif
#ifndef WEXITSTATUS
#define WEXITSTATUS(w)  (((w) >> 8) & 0xff)
#endif
#ifndef WTERMSIG
#define WTERMSIG(w)     ((w) & 0x7f)
#endif
#ifndef WSTOPSIG
#define WSTOPSIG        WEXITSTATUS
#endif

#if defined(__APPLE__) && ( defined(__MACH__) || defined(__DARWIN__) ) && !defined(__MacOS_X__)
#define __MacOS_X__ 1
#endif

#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__bsdi__)
#define HAVE_44BSD_SETUID 1
#define HAVE_44BSD_SETGID 1
#endif

#ifdef __NetBSD__
#undef HAVE_SETRUID
#undef HAVE_SETRGID
#endif

#ifdef BROKEN_SETREUID
#define setreuid ruby_setreuid
#endif
#ifdef BROKEN_SETREGID
#define setregid ruby_setregid
#endif

#if defined(HAVE_44BSD_SETUID) || defined(__MacOS_X__)
#if !defined(USE_SETREUID) && !defined(BROKEN_SETREUID)
#define OBSOLETE_SETREUID 1
#endif
#if !defined(USE_SETREGID) && !defined(BROKEN_SETREGID)
#define OBSOLETE_SETREGID 1
#endif
#endif

#if SIZEOF_RLIM_T == SIZEOF_INT
# define RLIM2NUM(v) UINT2NUM(v)
# define NUM2RLIM(v) NUM2UINT(v)
#elif SIZEOF_RLIM_T == SIZEOF_LONG
# define RLIM2NUM(v) ULONG2NUM(v)
# define NUM2RLIM(v) NUM2ULONG(v)
#elif SIZEOF_RLIM_T == SIZEOF_LONG_LONG
# define RLIM2NUM(v) ULL2NUM(v)
# define NUM2RLIM(v) NUM2ULL(v)
#endif

#define preserving_errno(stmts) \
	do {int saved_errno = errno; stmts; errno = saved_errno;} while (0)


/*
 *  call-seq:
 *     Process.pid   => fixnum
 *
 *  Returns the process id of this process. Not available on all
 *  platforms.
 *
 *     Process.pid   #=> 27415
 */

static VALUE
get_pid(VALUE rcv, SEL sel)
{
    rb_secure(2);
    return PIDT2NUM(getpid());
}


/*
 *  call-seq:
 *     Process.ppid   => fixnum
 *
 *  Returns the process id of the parent of this process. Always
 *  returns 0 on NT. Not available on all platforms.
 *
 *     puts "I am #{Process.pid}"
 *     Process.fork { puts "Dad is #{Process.ppid}" }
 *
 *  <em>produces:</em>
 *
 *     I am 27417
 *     Dad is 27417
 */

static VALUE
get_ppid(VALUE rcv, SEL sel)
{
    rb_secure(2);
#ifdef _WIN32
    return INT2FIX(0);
#else
    return PIDT2NUM(getppid());
#endif
}


/*********************************************************************
 *
 * Document-class: Process::Status
 *
 *  <code>Process::Status</code> encapsulates the information on the
 *  status of a running or terminated system process. The built-in
 *  variable <code>$?</code> is either +nil+ or a
 *  <code>Process::Status</code> object.
 *
 *     fork { exit 99 }   #=> 26557
 *     Process.wait       #=> 26557
 *     $?.class           #=> Process::Status
 *     $?.to_i            #=> 25344
 *     $? >> 8            #=> 99
 *     $?.stopped?        #=> false
 *     $?.exited?         #=> true
 *     $?.exitstatus      #=> 99
 *
 *  Posix systems record information on processes using a 16-bit
 *  integer.  The lower bits record the process status (stopped,
 *  exited, signaled) and the upper bits possibly contain additional
 *  information (for example the program's return code in the case of
 *  exited processes). Pre Ruby 1.8, these bits were exposed directly
 *  to the Ruby program. Ruby now encapsulates these in a
 *  <code>Process::Status</code> object. To maximize compatibility,
 *  however, these objects retain a bit-oriented interface. In the
 *  descriptions that follow, when we talk about the integer value of
 *  _stat_, we're referring to this 16 bit value.
 */

VALUE rb_cProcessStatus;

static void
rb_last_status_clear(void)
{
    rb_last_status_set(0, -1);
}

/*
 *  call-seq:
 *     stat.to_i     => fixnum
 *     stat.to_int   => fixnum
 *
 *  Returns the bits in _stat_ as a <code>Fixnum</code>. Poking
 *  around in these bits is platform dependent.
 *
 *     fork { exit 0xab }         #=> 26566
 *     Process.wait               #=> 26566
 *     sprintf('%04x', $?.to_i)   #=> "ab00"
 */

static VALUE
pst_to_i(VALUE st, SEL sel)
{
    return rb_iv_get(st, "status");
}


/*
 *  call-seq:
 *     stat.pid   => fixnum
 *
 *  Returns the process ID that this status object represents.
 *
 *     fork { exit }   #=> 26569
 *     Process.wait    #=> 26569
 *     $?.pid          #=> 26569
 */

static VALUE
pst_pid(VALUE st, SEL sel)
{
    return rb_iv_get(st, "pid");
}

static void
pst_message(VALUE str, rb_pid_t pid, int status)
{
    char buf[256];
    snprintf(buf, sizeof(buf), "pid %ld", (long)pid);
    rb_str_cat2(str, buf);
    if (WIFSTOPPED(status)) {
	int stopsig = WSTOPSIG(status);
	const char *signame = ruby_signal_name(stopsig);
	if (signame) {
	    snprintf(buf, sizeof(buf), " stopped SIG%s (signal %d)", signame, stopsig);
	}
	else {
	    snprintf(buf, sizeof(buf), " stopped signal %d", stopsig);
	}
	rb_str_cat2(str, buf);
    }
    if (WIFSIGNALED(status)) {
	int termsig = WTERMSIG(status);
	const char *signame = ruby_signal_name(termsig);
	if (signame) {
	    snprintf(buf, sizeof(buf), " SIG%s (signal %d)", signame, termsig);
	}
	else {
	    snprintf(buf, sizeof(buf), " signal %d", termsig);
	}
	rb_str_cat2(str, buf);
    }
    if (WIFEXITED(status)) {
	snprintf(buf, sizeof(buf), " exit %d", WEXITSTATUS(status));
	rb_str_cat2(str, buf);
    }
#ifdef WCOREDUMP
    if (WCOREDUMP(status)) {
	rb_str_cat2(str, " (core dumped)");
    }
#endif
}


/*
 *  call-seq:
 *     stat.to_s   => string
 *
 *  Show pid and exit status as a string.
 */

static VALUE
pst_to_s(VALUE st, SEL sel)
{
    rb_pid_t pid;
    int status;
    VALUE str;

    pid = NUM2LONG(pst_pid(st, 0));
    status = NUM2INT(pst_to_i(st, 0));

    str = rb_str_buf_new(0);
    pst_message(str, pid, status);
    return str;
}


/*
 *  call-seq:
 *     stat.inspect   => string
 *
 *  Override the inspection method.
 */

static VALUE
pst_inspect(VALUE st, SEL sel)
{
    rb_pid_t pid;
    int status;
    VALUE str;

    pid = NUM2LONG(pst_pid(st, 0));
    status = NUM2INT(pst_to_i(st, 0));

    str = rb_sprintf("#<%s: ", rb_class2name(CLASS_OF(st)));
    pst_message(str, pid, status);
    rb_str_cat2(str, ">");
    return str;
}


/*
 *  call-seq:
 *     stat == other   => true or false
 *
 *  Returns +true+ if the integer value of _stat_
 *  equals <em>other</em>.
 */

static VALUE
pst_equal(VALUE st1, SEL sel, VALUE st2)
{
    if (st1 == st2) {
	return Qtrue;
    }
    return rb_equal(pst_to_i(st1, 0), st2);
}


/*
 *  call-seq:
 *     stat & num   => fixnum
 *
 *  Logical AND of the bits in _stat_ with <em>num</em>.
 *
 *     fork { exit 0x37 }
 *     Process.wait
 *     sprintf('%04x', $?.to_i)       #=> "3700"
 *     sprintf('%04x', $? & 0x1e00)   #=> "1600"
 */

static VALUE
pst_bitand(VALUE st1, SEL sel, VALUE st2)
{
    int status = NUM2INT(st1) & NUM2INT(st2);

    return INT2NUM(status);
}


/*
 *  call-seq:
 *     stat >> num   => fixnum
 *
 *  Shift the bits in _stat_ right <em>num</em> places.
 *
 *     fork { exit 99 }   #=> 26563
 *     Process.wait       #=> 26563
 *     $?.to_i            #=> 25344
 *     $? >> 8            #=> 99
 */

static VALUE
pst_rshift(VALUE st1, SEL sel, VALUE st2)
{
    int status = NUM2INT(st1) >> NUM2INT(st2);

    return INT2NUM(status);
}


/*
 *  call-seq:
 *     stat.stopped?   => true or false
 *
 *  Returns +true+ if this process is stopped. This is only
 *  returned if the corresponding <code>wait</code> call had the
 *  <code>WUNTRACED</code> flag set.
 */

static VALUE
pst_wifstopped(VALUE st, SEL sel)
{
    int status = NUM2INT(st);

    if (WIFSTOPPED(status))
	return Qtrue;
    else
	return Qfalse;
}


/*
 *  call-seq:
 *     stat.stopsig   => fixnum or nil
 *
 *  Returns the number of the signal that caused _stat_ to stop
 *  (or +nil+ if self is not stopped).
 */

static VALUE
pst_wstopsig(VALUE st, SEL sel)
{
    int status = NUM2INT(st);

    if (WIFSTOPPED(status))
	return INT2NUM(WSTOPSIG(status));
    return Qnil;
}


/*
 *  call-seq:
 *     stat.signaled?   => true or false
 *
 *  Returns +true+ if _stat_ terminated because of
 *  an uncaught signal.
 */

static VALUE
pst_wifsignaled(VALUE st, SEL sel)
{
    int status = NUM2INT(st);

    if (WIFSIGNALED(status))
	return Qtrue;
    else
	return Qfalse;
}


/*
 *  call-seq:
 *     stat.termsig   => fixnum or nil
 *
 *  Returns the number of the signal that caused _stat_ to
 *  terminate (or +nil+ if self was not terminated by an
 *  uncaught signal).
 */

static VALUE
pst_wtermsig(VALUE st, SEL sel)
{
    int status = NUM2INT(st);

    if (WIFSIGNALED(status))
	return INT2NUM(WTERMSIG(status));
    return Qnil;
}


/*
 *  call-seq:
 *     stat.exited?   => true or false
 *
 *  Returns +true+ if _stat_ exited normally (for
 *  example using an <code>exit()</code> call or finishing the
 *  program).
 */

static VALUE
pst_wifexited(VALUE st, SEL sel)
{
    int status = NUM2INT(st);

    if (WIFEXITED(status))
	return Qtrue;
    else
	return Qfalse;
}


/*
 *  call-seq:
 *     stat.exitstatus   => fixnum or nil
 *
 *  Returns the least significant eight bits of the return code of
 *  _stat_. Only available if <code>exited?</code> is
 *  +true+.
 *
 *     fork { }           #=> 26572
 *     Process.wait       #=> 26572
 *     $?.exited?         #=> true
 *     $?.exitstatus      #=> 0
 *
 *     fork { exit 99 }   #=> 26573
 *     Process.wait       #=> 26573
 *     $?.exited?         #=> true
 *     $?.exitstatus      #=> 99
 */

static VALUE
pst_wexitstatus(VALUE st, SEL sel)
{
    int status = NUM2INT(st);

    if (WIFEXITED(status))
	return INT2NUM(WEXITSTATUS(status));
    return Qnil;
}


/*
 *  call-seq:
 *     stat.success?   => true, false or nil
 *
 *  Returns +true+ if _stat_ is successful, +false+ if not.
 *  Returns +nil+ if <code>exited?</code> is not +true+.
 */

static VALUE
pst_success_p(VALUE st, SEL sel)
{
    int status = NUM2INT(st);

    if (!WIFEXITED(status))
	return Qnil;
    return WEXITSTATUS(status) == EXIT_SUCCESS ? Qtrue : Qfalse;
}


/*
 *  call-seq:
 *     stat.coredump?   => true or false
 *
 *  Returns +true+ if _stat_ generated a coredump
 *  when it terminated. Not available on all platforms.
 */

static VALUE
pst_wcoredump(VALUE st, SEL sel)
{
#ifdef WCOREDUMP
    int status = NUM2INT(st);

    if (WCOREDUMP(status))
	return Qtrue;
    else
	return Qfalse;
#else
    return Qfalse;
#endif
}

#if !defined(HAVE_WAITPID) && !defined(HAVE_WAIT4)
#define NO_WAITPID
static st_table *pid_tbl;
#else
struct waitpid_arg {
    rb_pid_t pid;
    int *st;
    int flags;
};
#endif

static VALUE
rb_waitpid_blocking(void *data)
{
    rb_pid_t result;
#ifndef NO_WAITPID
    struct waitpid_arg *arg = data;
#endif

#if defined NO_WAITPID
    result = wait(data);
#elif defined HAVE_WAITPID
    result = waitpid(arg->pid, arg->st, arg->flags);
#else  /* HAVE_WAIT4 */
    result = wait4(arg->pid, arg->st, arg->flags, NULL);
#endif
    return (VALUE)result;
}

rb_pid_t
rb_waitpid(rb_pid_t pid, int *st, int flags)
{
    rb_pid_t result;
#ifndef NO_WAITPID
    struct waitpid_arg arg;

    arg.pid = pid;
    arg.st = st;
    arg.flags = flags;
    result = rb_waitpid_blocking(&arg);
//    result = (rb_pid_t)rb_thread_blocking_region(rb_waitpid_blocking, &arg,
//						 RB_UBF_DFL, 0);
    if (result < 0) {
#if 0
	if (errno == EINTR) {
	    rb_thread_polling();
	    goto retry;
	}
#endif
	return -1;
    }
#else  /* NO_WAITPID */
    if (pid_tbl && st_lookup(pid_tbl, pid, (st_data_t *)st)) {
	rb_last_status_set(*st, pid);
	st_delete(pid_tbl, (st_data_t*)&pid, NULL);
	return pid;
    }

    if (flags) {
	rb_raise(rb_eArgError, "can't do waitpid with flags");
    }

    for (;;) {
	result = (rb_pid_t)rb_thread_blocking_region(rb_waitpid_blocking,
						     st, RB_UBF_DFL);
	if (result < 0) {
	    if (errno == EINTR) {
		rb_thread_schedule();
		continue;
	    }
	    return -1;
	}
	if (result == pid) {
	    break;
	}
	if (!pid_tbl)
	  pid_tbl = st_init_numtable();
	st_insert(pid_tbl, pid, (st_data_t)st);
	if (!rb_thread_alone()) rb_thread_schedule();
    }
#endif
    if (result > 0) {
	rb_last_status_set(*st, result);
    }
    return result;
}

#ifdef NO_WAITPID
struct wait_data {
    rb_pid_t pid;
    int status;
};

static int
wait_each(rb_pid_t pid, int status, struct wait_data *data)
{
    if (data->status != -1) return ST_STOP;

    data->pid = pid;
    data->status = status;
    return ST_DELETE;
}

static int
waitall_each(rb_pid_t pid, int status, VALUE ary)
{
    rb_last_status_set(status, pid);
    rb_ary_push(ary, rb_assoc_new(PIDT2NUM(pid), rb_last_status_get());
    return ST_DELETE;
}
#endif


/* [MG]:FIXME: I wasn't sure how this should be done, since ::wait()
   has historically been documented as if it didn't take any arguments
   despite the fact that it's just an alias for ::waitpid(). The way I
   have it below is more truthful, but a little confusing.

   I also took the liberty of putting in the pid values, as they're
   pretty useful, and it looked as if the original 'ri' output was
   supposed to contain them after "[...]depending on the value of
   aPid:".

   The 'ansi' and 'bs' formats of the ri output don't display the
   definition list for some reason, but the plain text one does.
 */

/*
 *  call-seq:
 *     Process.wait()                     => fixnum
 *     Process.wait(pid=-1, flags=0)      => fixnum
 *     Process.waitpid(pid=-1, flags=0)   => fixnum
 *
 *  Waits for a child process to exit, returns its process id, and
 *  sets <code>$?</code> to a <code>Process::Status</code> object
 *  containing information on that process. Which child it waits on
 *  depends on the value of _pid_:
 *
 *  > 0::   Waits for the child whose process ID equals _pid_.
 *
 *  0::     Waits for any child whose process group ID equals that of the
 *          calling process.
 *
 *  -1::    Waits for any child process (the default if no _pid_ is
 *          given).
 *
 *  < -1::  Waits for any child whose process group ID equals the absolute
 *          value of _pid_.
 *
 *  The _flags_ argument may be a logical or of the flag values
 *  <code>Process::WNOHANG</code> (do not block if no child available)
 *  or <code>Process::WUNTRACED</code> (return stopped children that
 *  haven't been reported). Not all flags are available on all
 *  platforms, but a flag value of zero will work on all platforms.
 *
 *  Calling this method raises a <code>SystemError</code> if there are
 *  no child processes. Not available on all platforms.
 *
 *     include Process
 *     fork { exit 99 }                 #=> 27429
 *     wait                             #=> 27429
 *     $?.exitstatus                    #=> 99
 *
 *     pid = fork { sleep 3 }           #=> 27440
 *     Time.now                         #=> 2008-03-08 19:56:16 +0900
 *     waitpid(pid, Process::WNOHANG)   #=> nil
 *     Time.now                         #=> 2008-03-08 19:56:16 +0900
 *     waitpid(pid, 0)                  #=> 27440
 *     Time.now                         #=> 2008-03-08 19:56:19 +0900
 */

static VALUE
proc_wait(VALUE rcv, SEL sel, int argc, VALUE *argv)
{
    VALUE vpid, vflags;
    rb_pid_t pid;
    int flags, status;

    rb_secure(2);
    flags = 0;
    if (argc == 0) {
	pid = -1;
    }
    else {
	rb_scan_args(argc, argv, "02", &vpid, &vflags);
	pid = NUM2PIDT(vpid);
	if (argc == 2 && !NIL_P(vflags)) {
	    flags = NUM2UINT(vflags);
	}
    }
    if ((pid = rb_waitpid(pid, &status, flags)) < 0)
	rb_sys_fail(0);
    if (pid == 0) {
	rb_last_status_clear();
	return Qnil;
    }
    return PIDT2NUM(pid);
}


/*
 *  call-seq:
 *     Process.wait2(pid=-1, flags=0)      => [pid, status]
 *     Process.waitpid2(pid=-1, flags=0)   => [pid, status]
 *
 *  Waits for a child process to exit (see Process::waitpid for exact
 *  semantics) and returns an array containing the process id and the
 *  exit status (a <code>Process::Status</code> object) of that
 *  child. Raises a <code>SystemError</code> if there are no child
 *  processes.
 *
 *     Process.fork { exit 99 }   #=> 27437
 *     pid, status = Process.wait2
 *     pid                        #=> 27437
 *     status.exitstatus          #=> 99
 */

static VALUE
proc_wait2(VALUE rcv, SEL sel, int argc, VALUE *argv)
{
    VALUE pid = proc_wait(rcv, 0, argc, argv);
    if (NIL_P(pid)) return Qnil;
    return rb_assoc_new(pid, rb_last_status_get());
}


/*
 *  call-seq:
 *     Process.waitall   => [ [pid1,status1], ...]
 *
 *  Waits for all children, returning an array of
 *  _pid_/_status_ pairs (where _status_ is a
 *  <code>Process::Status</code> object).
 *
 *     fork { sleep 0.2; exit 2 }   #=> 27432
 *     fork { sleep 0.1; exit 1 }   #=> 27433
 *     fork {            exit 0 }   #=> 27434
 *     p Process.waitall
 *
 *  <em>produces</em>:
 *
 *     [[27434, #<Process::Status: pid=27434,exited(0)>],
 *      [27433, #<Process::Status: pid=27433,exited(1)>],
 *      [27432, #<Process::Status: pid=27432,exited(2)>]]
 */

static VALUE
proc_waitall(VALUE rcv, SEL sel)
{
    VALUE result;
    rb_pid_t pid;
    int status;

    rb_secure(2);
    result = rb_ary_new();
#ifdef NO_WAITPID
    if (pid_tbl) {
	st_foreach(pid_tbl, waitall_each, result);
    }
#else
    rb_last_status_clear();
#endif

    for (pid = -1;;) {
#ifdef NO_WAITPID
	pid = wait(&status);
#else
	pid = rb_waitpid(-1, &status, 0);
#endif
	if (pid == -1) {
	    if (errno == ECHILD)
		break;
#ifdef NO_WAITPID
	    if (errno == EINTR) {
		rb_thread_schedule();
		continue;
	    }
#endif
	    rb_sys_fail(0);
	}
#ifdef NO_WAITPID
	rb_last_status_set(status, pid);
#endif
	rb_ary_push(result, rb_assoc_new(PIDT2NUM(pid), rb_last_status_get()));
    }
    return result;
}

static VALUE
detach_process_watcher(void *arg)
{
    rb_pid_t cpid, pid = (rb_pid_t)(VALUE)arg;
    int status;

    while ((cpid = rb_waitpid(pid, &status, 0)) == 0) {
	/* wait while alive */
    }
    return rb_last_status_get();
}

VALUE
rb_detach_process(rb_pid_t pid)
{
    return rb_thread_create(detach_process_watcher, (void*)(VALUE)pid);
}


/*
 *  call-seq:
 *     Process.detach(pid)   => thread
 *
 *  Some operating systems retain the status of terminated child
 *  processes until the parent collects that status (normally using
 *  some variant of <code>wait()</code>. If the parent never collects
 *  this status, the child stays around as a <em>zombie</em> process.
 *  <code>Process::detach</code> prevents this by setting up a
 *  separate Ruby thread whose sole job is to reap the status of the
 *  process _pid_ when it terminates. Use <code>detach</code>
 *  only when you do not intent to explicitly wait for the child to
 *  terminate.
 *
 *  The waiting thread returns the exit status of the detached process
 *  when it terminates, so you can use <code>Thread#join</code> to
 *  know the result.  If specified _pid_ is not a valid child process
 *  ID, the thread returns +nil+ immediately.
 *
 *  In this first example, we don't reap the first child process, so
 *  it appears as a zombie in the process status display.
 *
 *     p1 = fork { sleep 0.1 }
 *     p2 = fork { sleep 0.2 }
 *     Process.waitpid(p2)
 *     sleep 2
 *     system("ps -ho pid,state -p #{p1}")
 *
 *  <em>produces:</em>
 *
 *     27389 Z
 *
 *  In the next example, <code>Process::detach</code> is used to reap
 *  the child automatically.
 *
 *     p1 = fork { sleep 0.1 }
 *     p2 = fork { sleep 0.2 }
 *     Process.detach(p1)
 *     Process.waitpid(p2)
 *     sleep 2
 *     system("ps -ho pid,state -p #{p1}")
 *
 *  <em>(produces no output)</em>
 */

static VALUE
proc_detach(VALUE obj, SEL sel, VALUE pid)
{
    rb_secure(2);
    return rb_detach_process(NUM2PIDT(pid));
}

#ifndef HAVE_STRING_H
char *strtok();
#endif

#if 1
#define before_exec() 
#define after_exec()
#else
void rb_thread_stop_timer_thread(void);
void rb_thread_start_timer_thread(void);
void rb_thread_reset_timer_thread(void);

#define before_exec() \
  (rb_enable_interrupt(), rb_thread_stop_timer_thread())
#define after_exec() \
  (rb_thread_start_timer_thread(), rb_disable_interrupt())
#endif

#include "dln.h"

static void
security(const char *str)
{
    if (rb_env_path_tainted()) {
	if (rb_safe_level() > 0) {
	    rb_raise(rb_eSecurityError, "Insecure PATH - %s", str);
	}
    }
}

static int
proc_exec_v(char **argv, const char *prog)
{
    char fbuf[MAXPATHLEN];

    if (!prog)
	prog = argv[0];
    prog = dln_find_exe_r(prog, 0, fbuf, sizeof(fbuf));
    if (!prog) {
	errno = ENOENT;
	return -1;
    }

#if (defined(MSDOS) && !defined(DJGPP)) || defined(__human68k__) || defined(__EMX__) || defined(OS2)
    {
#if defined(__human68k__)
#define COMMAND "command.x"
#endif
#if defined(__EMX__) || defined(OS2) /* OS/2 emx */
#define COMMAND "cmd.exe"
#endif
#if (defined(MSDOS) && !defined(DJGPP))
#define COMMAND "command.com"
#endif
	char *extension;

	if ((extension = strrchr(prog, '.')) != NULL && STRCASECMP(extension, ".bat") == 0) {
	    char **new_argv;
	    char *p;
	    int n;

	    for (n = 0; argv[n]; n++)
		/* no-op */;
	    new_argv = ALLOCA_N(char*, n + 2);
	    for (; n > 0; n--)
		new_argv[n + 1] = argv[n];
	    new_argv[1] = strcpy(ALLOCA_N(char, strlen(argv[0]) + 1), argv[0]);
	    for (p = new_argv[1]; *p != '\0'; p++)
		if (*p == '/')
		    *p = '\\';
	    new_argv[0] = COMMAND;
	    argv = new_argv;
	    prog = dln_find_exe_r(argv[0], 0, fbuf, sizeof(fbuf));
	    if (!prog) {
		errno = ENOENT;
		return -1;
	    }
	}
    }
#endif /* MSDOS or __human68k__ or __EMX__ */
    before_exec();
    execv(prog, argv);
    preserving_errno(after_exec());
    return -1;
}

int
rb_proc_exec_n(int argc, VALUE *argv, const char *prog)
{
    char **args;
    int i;

    args = ALLOCA_N(char*, argc+1);
    for (i=0; i<argc; i++) {
	args[i] = (char *)RSTRING_PTR(argv[i]);
    }
    args[i] = 0;
    if (args[0]) {
	return proc_exec_v(args, prog);
    }
    return -1;
}

int
rb_proc_exec(const char *str)
{
#ifndef _WIN32
    const char *s = str;
    char *ss, *t;
    char **argv, **a;
#endif

    while (*str && ISSPACE(*str))
	str++;

#ifdef _WIN32
    before_exec();
    rb_w32_spawn(P_OVERLAY, (char *)str, 0);
    after_exec();
#else
    for (s=str; *s; s++) {
	if (ISSPACE(*s)) {
	    const char *p, *nl = NULL;
	    for (p = s; ISSPACE(*p); p++) {
		if (*p == '\n') nl = p;
	    }
	    if (!*p) break;
	    if (nl) s = nl;
	}
	if (*s != ' ' && !ISALPHA(*s) && strchr("*?{}[]<>()~&|\\$;'`\"\n",*s)) {
#if defined(MSDOS)
	    int status;
	    before_exec();
	    status = system(str);
	    after_exec();
	    if (status != -1)
		exit(status);
#elif defined(__human68k__) || defined(__CYGWIN32__) || defined(__EMX__)
	    char fbuf[MAXPATHLEN];
	    char *shell = dln_find_exe_r("sh", 0, fbuf, sizeof(fbuf));
	    int status = -1;
	    before_exec();
	    if (shell)
		execl(shell, "sh", "-c", str, (char *) NULL);
	    else
		status = system(str);
	    after_exec();
	    if (status != -1)
		exit(status);
#else
	    before_exec();
	    execl("/bin/sh", "sh", "-c", str, (char *)NULL);
	    preserving_errno(after_exec());
#endif
	    return -1;
	}
    }
    a = argv = ALLOCA_N(char*, (s-str)/2+2);
    ss = ALLOCA_N(char, s-str+1);
    memcpy(ss, str, s-str);
    ss[s-str] = '\0';
    if ((*a++ = strtok(ss, " \t")) != 0) {
	while ((t = strtok(NULL, " \t")) != 0) {
	    *a++ = t;
	}
	*a = NULL;
    }
    if (argv[0]) {
	return proc_exec_v(argv, 0);
    }
    errno = ENOENT;
#endif	/* _WIN32 */
    return -1;
}

#if defined(_WIN32)
#define HAVE_SPAWNV 1
#endif

#if !defined(HAVE_FORK) && defined(HAVE_SPAWNV)
#if defined(_WIN32)
#define proc_spawn_v(argv, prog) rb_w32_aspawn(P_NOWAIT, prog, argv)
#else
static rb_pid_t
proc_spawn_v(char **argv, char *prog)
{
    char fbuf[MAXPATHLEN];
    char *extension;
    rb_pid_t status;

    if (!prog)
	prog = argv[0];
    security(prog);
    prog = dln_find_exe_r(prog, 0, fbuf, sizeof(fbuf));
    if (!prog)
	return -1;

#if defined(__human68k__)
    if ((extension = strrchr(prog, '.')) != NULL && STRCASECMP(extension, ".bat") == 0) {
	char **new_argv;
	char *p;
	int n;

	for (n = 0; argv[n]; n++)
	    /* no-op */;
	new_argv = ALLOCA_N(char*, n + 2);
	for (; n > 0; n--)
	    new_argv[n + 1] = argv[n];
	new_argv[1] = strcpy(ALLOCA_N(char, strlen(argv[0]) + 1), argv[0]);
	for (p = new_argv[1]; *p != '\0'; p++)
	    if (*p == '/')
		*p = '\\';
	new_argv[0] = COMMAND;
	argv = new_argv;
	prog = dln_find_exe_r(argv[0], 0, fbuf, sizeof(fbuf));
	if (!prog) {
	    errno = ENOENT;
	    return -1;
	}
    }
#endif
    before_exec();
    status = spawnv(P_WAIT, prog, argv);
    rb_last_status_set(status == -1 ? 127 : status, 0);
    after_exec();
    return status;
}
#endif

static rb_pid_t
proc_spawn_n(int argc, VALUE *argv, VALUE prog)
{
    char **args;
    int i;

    args = ALLOCA_N(char*, argc + 1);
    for (i = 0; i < argc; i++) {
	args[i] = RSTRING_PTR(argv[i]);
    }
    args[i] = (char*) 0;
    if (args[0])
	return proc_spawn_v(args, prog ? RSTRING_PTR(prog) : 0);
    return -1;
}

#if defined(_WIN32)
#define proc_spawn(str) rb_w32_spawn(P_NOWAIT, str, 0)
#else
static rb_pid_t
proc_spawn(char *str)
{
    char fbuf[MAXPATHLEN];
    char *s, *t;
    char **argv, **a;
    rb_pid_t status;

    for (s = str; *s; s++) {
	if (*s != ' ' && !ISALPHA(*s) && strchr("*?{}[]<>()~&|\\$;'`\"\n",*s)) {
	    char *shell = dln_find_exe_r("sh", 0, fbuf, sizeof(fbuf));
	    before_exec();
	    status = shell?spawnl(P_WAIT,shell,"sh","-c",str,(char*)NULL):system(str);
	    rb_last_status_set(status == -1 ? 127 : status, 0);
	    after_exec();
	    return status;
	}
    }
    a = argv = ALLOCA_N(char*, (s - str) / 2 + 2);
    s = ALLOCA_N(char, s - str + 1);
    strcpy(s, str);
    if (*a++ = strtok(s, " \t")) {
	while (t = strtok(NULL, " \t"))
	    *a++ = t;
	*a = NULL;
    }
    return argv[0] ? proc_spawn_v(argv, 0) : -1;
}
#endif
#endif

static VALUE
hide_obj(VALUE obj)
{
#if !WITH_OBJC
    RBASIC(obj)->klass = 0;
#endif
    return obj;
}

enum {
    EXEC_OPTION_PGROUP,
    EXEC_OPTION_RLIMIT,
    EXEC_OPTION_UNSETENV_OTHERS,
    EXEC_OPTION_ENV,
    EXEC_OPTION_CHDIR,
    EXEC_OPTION_UMASK,
    EXEC_OPTION_DUP2,
    EXEC_OPTION_CLOSE,
    EXEC_OPTION_OPEN,
    EXEC_OPTION_CLOSE_OTHERS,
};

static VALUE
check_exec_redirect_fd(VALUE v)
{
    VALUE tmp;
    int fd;
    if (FIXNUM_P(v)) {
        fd = FIX2INT(v);
    }
    else if (SYMBOL_P(v)) {
        ID id = SYM2ID(v);
        if (id == rb_intern("in"))
            fd = 0;
        else if (id == rb_intern("out"))
            fd = 1;
        else if (id == rb_intern("err"))
            fd = 2;
        else
            goto wrong;
    }
    else if (!NIL_P(tmp = rb_check_convert_type(v, T_FILE, "IO", "to_io"))) {
        rb_io_t *fptr = ExtractIOStruct(tmp);
#if 0
        if (fptr->tied_io_for_writing)
            rb_raise(rb_eArgError, "duplex IO redirection");
#endif
        fd = fptr->fd;
    }
    else {
        rb_raise(rb_eArgError, "wrong exec redirect");
    }
    if (fd < 0) {
      wrong:
        rb_raise(rb_eArgError, "negative file descriptor");
    }
    return INT2FIX(fd);
}

static void
check_exec_redirect(VALUE key, VALUE val, VALUE options)
{
    int index;
    VALUE ary, param;
    VALUE path, flags, perm;
    ID id;

    switch (TYPE(val)) {
      case T_SYMBOL:
        id = SYM2ID(val);
        if (id == rb_intern("close")) {
            index = EXEC_OPTION_CLOSE;
            param = Qnil;
        }
        else {
            rb_raise(rb_eArgError, "wrong exec redirect symbol: %s",
                                   rb_id2name(id));
        }
        break;

      case T_FILE:
        val = check_exec_redirect_fd(val);
        /* fall through */
      case T_FIXNUM:
        index = EXEC_OPTION_DUP2;
        param = val;
        break;

      case T_ARRAY:
        index = EXEC_OPTION_OPEN;
        path = rb_ary_entry(val, 0);
        FilePathValue(path);
        flags = rb_ary_entry(val, 1);
        if (NIL_P(flags))
            flags = INT2NUM(O_RDONLY);
        else if (TYPE(flags) == T_STRING)
            flags = INT2FIX(0);//TODO INT2NUM(rb_io_mode_modenum(StringValueCStr(flags)));
        else
            flags = rb_to_int(flags);
        perm = rb_ary_entry(val, 2);
        perm = NIL_P(perm) ? INT2FIX(0644) : rb_to_int(perm);
        param = hide_obj(rb_ary_new3(3, hide_obj(rb_str_dup(path)),
                                        flags, perm));
        break;

      case T_STRING:
        index = EXEC_OPTION_OPEN;
        path = val;
        FilePathValue(path);
        if ((FIXNUM_P(key) && (FIX2INT(key) == 1 || FIX2INT(key) == 2)) ||
            key == rb_stdout || key == rb_stderr)
            flags = INT2NUM(O_WRONLY|O_CREAT|O_TRUNC);
        else
            flags = INT2NUM(O_RDONLY);
        perm = INT2FIX(0644);
        param = hide_obj(rb_ary_new3(3, hide_obj(rb_str_dup(path)),
                                        flags, perm));
        break;

      default:
        rb_raise(rb_eArgError, "wrong exec redirect action");
    }

    ary = rb_ary_entry(options, index);
    if (NIL_P(ary)) {
        ary = hide_obj(rb_ary_new());
        rb_ary_store(options, index, ary);
    }
    if (TYPE(key) != T_ARRAY) {
        VALUE fd = check_exec_redirect_fd(key);
        rb_ary_push(ary, hide_obj(rb_assoc_new(fd, param)));
    }
    else {
        int i, n=0;
        for (i = 0 ; i < RARRAY_LEN(key); i++) {
            VALUE v = RARRAY_AT(key, i);
            VALUE fd = check_exec_redirect_fd(v);
            rb_ary_push(ary, hide_obj(rb_assoc_new(fd, param)));
            n++;
        }
    }
}

#ifdef RLIM2NUM
static int rlimit_type_by_lname(const char *name);
#endif

int
rb_exec_arg_addopt(struct rb_exec_arg *e, VALUE key, VALUE val)
{
    VALUE options = e->options;
    ID id;
#ifdef RLIM2NUM
    int rtype;
#endif

    rb_secure(2);

    switch (TYPE(key)) {
      case T_SYMBOL:
        id = SYM2ID(key);
#ifdef HAVE_SETPGID
        if (id == rb_intern("pgroup")) {
            if (!NIL_P(rb_ary_entry(options, EXEC_OPTION_PGROUP))) {
                rb_raise(rb_eArgError, "pgroup option specified twice");
            }
            if (!RTEST(val))
                val = Qfalse;
            else if (val == Qtrue)
                val = INT2FIX(0);
            else {
                pid_t pgroup = NUM2PIDT(val);
                if (pgroup < 0) {
                    rb_raise(rb_eArgError, "negative process group ID : %ld", (long)pgroup);
                }
                val = PIDT2NUM(pgroup);
            }
            rb_ary_store(options, EXEC_OPTION_PGROUP, val);
        }
        else
#endif
#ifdef RLIM2NUM
        if (strncmp("rlimit_", rb_id2name(id), 7) == 0 &&
            (rtype = rlimit_type_by_lname(rb_id2name(id)+7)) != -1) {
            VALUE ary = rb_ary_entry(options, EXEC_OPTION_RLIMIT);
            VALUE tmp, softlim, hardlim;
            if (NIL_P(ary)) {
                ary = hide_obj(rb_ary_new());
                rb_ary_store(options, EXEC_OPTION_RLIMIT, ary);
            }
            tmp = rb_check_array_type(val);
            if (!NIL_P(tmp)) {
                if (RARRAY_LEN(tmp) == 1)
                    softlim = hardlim = rb_to_int(rb_ary_entry(tmp, 0));
                else if (RARRAY_LEN(tmp) == 2) {
                    softlim = rb_to_int(rb_ary_entry(tmp, 0));
                    hardlim = rb_to_int(rb_ary_entry(tmp, 1));
                }
                else {
                    rb_raise(rb_eArgError, "wrong exec rlimit option");
                }
            }
            else {
                softlim = hardlim = rb_to_int(val);
            }
            tmp = hide_obj(rb_ary_new3(3, INT2NUM(rtype), softlim, hardlim));
            rb_ary_push(ary, tmp);
        }
        else
#endif
        if (id == rb_intern("unsetenv_others")) {
            if (!NIL_P(rb_ary_entry(options, EXEC_OPTION_UNSETENV_OTHERS))) {
                rb_raise(rb_eArgError, "unsetenv_others option specified twice");
            }
            val = RTEST(val) ? Qtrue : Qfalse;
            rb_ary_store(options, EXEC_OPTION_UNSETENV_OTHERS, val);
        }
        else if (id == rb_intern("chdir")) {
            if (!NIL_P(rb_ary_entry(options, EXEC_OPTION_CHDIR))) {
                rb_raise(rb_eArgError, "chdir option specified twice");
            }
            FilePathValue(val);
            rb_ary_store(options, EXEC_OPTION_CHDIR,
                                  hide_obj(rb_str_dup(val)));
        }
        else if (id == rb_intern("umask")) {
            mode_t cmask = NUM2LONG(val);
            if (!NIL_P(rb_ary_entry(options, EXEC_OPTION_UMASK))) {
                rb_raise(rb_eArgError, "umask option specified twice");
            }
            rb_ary_store(options, EXEC_OPTION_UMASK, LONG2NUM(cmask));
        }
        else if (id == rb_intern("close_others")) {
            if (!NIL_P(rb_ary_entry(options, EXEC_OPTION_CLOSE_OTHERS))) {
                rb_raise(rb_eArgError, "close_others option specified twice");
            }
            val = RTEST(val) ? Qtrue : Qfalse;
            rb_ary_store(options, EXEC_OPTION_CLOSE_OTHERS, val);
        }
        else if (id == rb_intern("in")) {
            key = INT2FIX(0);
            goto redirect;
        }
        else if (id == rb_intern("out")) {
            key = INT2FIX(1);
            goto redirect;
        }
        else if (id == rb_intern("err")) {
            key = INT2FIX(2);
            goto redirect;
        }
        else {
            rb_raise(rb_eArgError, "wrong exec option symbol: %s",
                                   rb_id2name(id));
        }
        break;

      case T_FIXNUM:
      case T_FILE:
      case T_ARRAY:
redirect:
        check_exec_redirect(key, val, options);
        break;

      default:
        rb_raise(rb_eArgError, "wrong exec option");
    }

    return ST_CONTINUE;
}

static int
check_exec_options_i(st_data_t st_key, st_data_t st_val, st_data_t arg)
{
    VALUE key = (VALUE)st_key;
    VALUE val = (VALUE)st_val;
    struct rb_exec_arg *e = (struct rb_exec_arg *)arg;
    return rb_exec_arg_addopt(e, key, val);
}

static VALUE
check_exec_fds(VALUE options)
{
    VALUE h = rb_hash_new();
    VALUE ary;
    int index, i;
    int maxhint = -1;

    for (index = EXEC_OPTION_DUP2; index <= EXEC_OPTION_OPEN; index++) {
        ary = rb_ary_entry(options, index);
        if (NIL_P(ary))
            continue;
        for (i = 0; i < RARRAY_LEN(ary); i++) {
            VALUE elt = RARRAY_AT(ary, i);
            int fd = FIX2INT(RARRAY_AT(elt, 0));
            if (RTEST(rb_hash_lookup(h, INT2FIX(fd)))) {
                rb_raise(rb_eArgError, "fd %d specified twice", fd);
            }
            rb_hash_aset(h, INT2FIX(fd), Qtrue);
            if (maxhint < fd)
                maxhint = fd;
            if (index == EXEC_OPTION_DUP2) {
                fd = FIX2INT(RARRAY_AT(elt, 1));
                if (maxhint < fd)
                    maxhint = fd;
            }
        }
    }
    if (rb_ary_entry(options, EXEC_OPTION_CLOSE_OTHERS) != Qfalse) {
        rb_ary_store(options, EXEC_OPTION_CLOSE_OTHERS, INT2FIX(maxhint));
    }
    return h;
}

static void
rb_check_exec_options(VALUE opthash, struct rb_exec_arg *e)
{
    if (RHASH_EMPTY_P(opthash))
        return;
    rb_hash_foreach(opthash, check_exec_options_i, (VALUE)e);
}

static int
check_exec_env_i(st_data_t st_key, st_data_t st_val, st_data_t arg)
{
    VALUE key = (VALUE)st_key;
    VALUE val = (VALUE)st_val;
    VALUE env = (VALUE)arg;
    char *k;

    k = StringValueCStr(key);
    if (strchr(k, '='))
        rb_raise(rb_eArgError, "environment name contains a equal : %s", k);

    if (!NIL_P(val))
        StringValueCStr(val);

    rb_ary_push(env, hide_obj(rb_assoc_new(key, val)));

    return ST_CONTINUE;
}

static VALUE
rb_check_exec_env(VALUE hash)
{
    VALUE env;

    env = hide_obj(rb_ary_new());
    rb_hash_foreach(hash, check_exec_env_i, env);

    return env;
}

static VALUE
rb_check_argv(int argc, VALUE *argv)
{
    VALUE tmp, prog;
    int i;
    const char *name = 0;

    if (argc == 0) {
	rb_raise(rb_eArgError, "wrong number of arguments");
    }

    prog = 0;
    tmp = rb_check_array_type(argv[0]);
    if (!NIL_P(tmp)) {
	if (RARRAY_LEN(tmp) != 2) {
	    rb_raise(rb_eArgError, "wrong first argument");
	}
	prog = RARRAY_AT(tmp, 0);
	argv[0] = RARRAY_AT(tmp, 1);
	SafeStringValue(prog);
	StringValueCStr(prog);
	prog = rb_str_new4(prog);
	name = RSTRING_PTR(prog);
    }
    for (i = 0; i < argc; i++) {
	SafeStringValue(argv[i]);
	argv[i] = rb_str_new4(argv[i]);
	StringValueCStr(argv[i]);
    }
    security(name ? name : RSTRING_PTR(argv[0]));
    return prog;
}

static VALUE
rb_exec_getargs(int *argc_p, VALUE **argv_p, int accept_shell, VALUE *env_ret, VALUE *opthash_ret, struct rb_exec_arg *e)
{
    VALUE hash, prog;

    if (0 < *argc_p) {
        hash = rb_check_convert_type((*argv_p)[*argc_p-1], T_HASH, "Hash", "to_hash");
        if (!NIL_P(hash)) {
            *opthash_ret = hash;
            (*argc_p)--;
        }
    }

    if (0 < *argc_p) {
        hash = rb_check_convert_type((*argv_p)[0], T_HASH, "Hash", "to_hash");
        if (!NIL_P(hash)) {
            *env_ret = hash;
            (*argc_p)--;
            (*argv_p)++;
        }
    }
    prog = rb_check_argv(*argc_p, *argv_p);
    if (!prog) {
        prog = (*argv_p)[0];
        if (accept_shell && *argc_p == 1) {
            *argc_p = 0;
            *argv_p = 0;
        }
    }
    return prog;
}

static void
rb_exec_fillarg(VALUE prog, int argc, VALUE *argv, VALUE env, VALUE opthash, struct rb_exec_arg *e)
{
    VALUE options;
    MEMZERO(e, struct rb_exec_arg, 1);
    options = hide_obj(rb_ary_new());
    e->options = options;

    if (!NIL_P(opthash)) {
        rb_check_exec_options(opthash, e);
    }
    if (!NIL_P(env)) {
        env = rb_check_exec_env(env);
        rb_ary_store(options, EXEC_OPTION_ENV, env);
    }

    e->argc = argc;
    e->argv = argv;
    e->prog = prog ? RSTRING_PTR(prog) : 0;
}

VALUE
rb_exec_arg_init(int argc, VALUE *argv, int accept_shell, struct rb_exec_arg *e)
{
    VALUE prog;
    VALUE env = Qnil, opthash = Qnil;
    prog = rb_exec_getargs(&argc, &argv, accept_shell, &env, &opthash, e);
    rb_exec_fillarg(prog, argc, argv, env, opthash, e);
    return prog;
}

void
rb_exec_arg_fixup(struct rb_exec_arg *e)
{
    e->redirect_fds = check_exec_fds(e->options);
}

/*
 *  call-seq:
 *     exec([env,] command [, arg, ...] [,options])
 *
 *  Replaces the current process by running the given external _command_.
 *  If optional arguments, sequence of +arg+, are not given, that argument is
 *  taken as a line that is subject to shell expansion before being
 *  executed. If one or more +arg+ given, they
 *  are passed as parameters to _command_ with no shell
 *  expansion. If +command+ is a two-element array, the first
 *  element is the command to be executed, and the second argument is
 *  used as the <code>argv[0]</code> value, which may show up in process
 *  listings. In MSDOS environments, the command is executed in a
 *  subshell; otherwise, one of the <code>exec(2)</code> system calls is
 *  used, so the running command may inherit some of the environment of
 *  the original program (including open file descriptors).
 *
 *  The hash arguments, env and options, are same as
 *  <code>system</code> and <code>spawn</code>.
 *  See <code>spawn</code> for details.
 *
 *  Raises SystemCallError if the _command_ couldn't execute (typically
 *  <code>Errno::ENOENT</code> when it was not found).
 *
 *     exec "echo *"       # echoes list of files in current directory
 *     # never get here
 *
 *
 *     exec "echo", "*"    # echoes an asterisk
 *     # never get here
 */

static VALUE
rb_f_exec(VALUE rcv, SEL sel, int argc, VALUE *argv)
{
    struct rb_exec_arg earg;

    rb_exec_arg_init(argc, argv, TRUE, &earg);
    if (NIL_P(rb_ary_entry(earg.options, EXEC_OPTION_CLOSE_OTHERS)))
        rb_exec_arg_addopt(&earg, ID2SYM(rb_intern("close_others")), Qfalse);
    rb_exec_arg_fixup(&earg);

    rb_exec(&earg);
    rb_sys_fail(earg.prog);
    return Qnil;		/* dummy */
}

/*#define DEBUG_REDIRECT*/
#if defined(DEBUG_REDIRECT)

#include <stdarg.h>

static void
ttyprintf(const char *fmt, ...)
{
    va_list ap;
    FILE *tty;
    int save = errno;
    tty = fopen("/dev/tty", "w");
    if (!tty)
        return;

    va_start(ap, fmt);
    vfprintf(tty, fmt, ap);
    va_end(ap);
    fclose(tty);
    errno = save;
}

static int
redirect_dup(int oldfd)
{
    int ret;
    ret = dup(oldfd);
    ttyprintf("dup(%d) => %d\n", oldfd, ret);
    return ret;
}

static int
redirect_dup2(int oldfd, int newfd)
{
    int ret;
    ret = dup2(oldfd, newfd);
    ttyprintf("dup2(%d, %d)\n", oldfd, newfd);
    return ret;
}

static int
redirect_close(int fd)
{
    int ret;
    ret = close(fd);
    ttyprintf("close(%d)\n", fd);
    return ret;
}

static int
redirect_open(const char *pathname, int flags, mode_t perm)
{
    int ret;
    ret = open(pathname, flags, perm);
    ttyprintf("open(\"%s\", 0x%x, 0%o) => %d\n", pathname, flags, perm, ret);
    return ret;
}

#else
#define redirect_dup(oldfd) dup(oldfd)
#define redirect_dup2(oldfd, newfd) dup2(oldfd, newfd)
#define redirect_close(fd) close(fd)
#define redirect_open(pathname, flags, perm) open(pathname, flags, perm)
#endif

static int
save_redirect_fd(int fd, VALUE save)
{
    if (!NIL_P(save)) {
        VALUE newary;
        int save_fd = redirect_dup(fd);
        if (save_fd == -1) return -1;
        newary = rb_ary_entry(save, EXEC_OPTION_DUP2);
        if (NIL_P(newary)) {
            newary = hide_obj(rb_ary_new());
            rb_ary_store(save, EXEC_OPTION_DUP2, newary);
        }
        rb_ary_push(newary,
                    hide_obj(rb_assoc_new(INT2FIX(fd), INT2FIX(save_fd))));

        newary = rb_ary_entry(save, EXEC_OPTION_CLOSE);
        if (NIL_P(newary)) {
            newary = hide_obj(rb_ary_new());
            rb_ary_store(save, EXEC_OPTION_CLOSE, newary);
        }
        rb_ary_push(newary, hide_obj(rb_assoc_new(INT2FIX(save_fd), Qnil)));
    }

    return 0;
}

static VALUE
save_env_i(VALUE i, VALUE ary, int argc, VALUE *argv)
{
    rb_ary_push(ary, hide_obj(rb_ary_dup(argv[0])));
    return Qnil;
}

static void
save_env(VALUE save)
{
    if (!NIL_P(save) && NIL_P(rb_ary_entry(save, EXEC_OPTION_ENV))) {
        VALUE env = rb_const_get(rb_cObject, rb_intern("ENV"));
        if (RTEST(env)) {
            VALUE ary = hide_obj(rb_ary_new());
            rb_objc_block_call(env, selEach, 0, 0, save_env_i, (VALUE)ary);
            rb_ary_store(save, EXEC_OPTION_ENV, ary);
        }
        rb_ary_store(save, EXEC_OPTION_UNSETENV_OTHERS, Qtrue);
    }
}

static int
intcmp(const void *a, const void *b)
{
    return *(int*)a - *(int*)b;
}

static int
run_exec_dup2(VALUE ary, VALUE save)
{
    int n, i;
    int ret;
    int extra_fd = -1;
    struct fd_pair {
        int oldfd;
        int newfd;
        int older_index;
        int num_newer;
    } *pairs = 0;

    n = RARRAY_LEN(ary);
    pairs = ALLOC_N(struct fd_pair, n);

    /* initialize oldfd and newfd: O(n) */
    for (i = 0; i < n; i++) {
        VALUE elt = RARRAY_AT(ary, i);
        pairs[i].oldfd = FIX2INT(RARRAY_AT(elt, 1));
        pairs[i].newfd = FIX2INT(RARRAY_AT(elt, 0)); /* unique */
        pairs[i].older_index = -1;
    }

    /* sort the table by oldfd: O(n log n) */
    qsort(pairs, n, sizeof(struct fd_pair), intcmp);

    /* initialize older_index and num_newer: O(n log n) */
    for (i = 0; i < n; i++) {
        int newfd = pairs[i].newfd;
        struct fd_pair key, *found;
        key.oldfd = newfd;
        found = bsearch(&key, pairs, n, sizeof(struct fd_pair), intcmp);
        pairs[i].num_newer = 0;
        if (found) {
            while (pairs < found && (found-1)->oldfd == newfd)
                found--;
            while (found < pairs+n && found->oldfd == newfd) {
                pairs[i].num_newer++;
                found->older_index = i;
                found++;
            }
        }
    }

    /* non-cyclic redirection: O(n) */
    for (i = 0; i < n; i++) {
        int j = i;
        while (j != -1 && pairs[j].oldfd != -1 && pairs[j].num_newer == 0) {
            if (save_redirect_fd(pairs[j].newfd, save) < 0)
                return -1;
            ret = redirect_dup2(pairs[j].oldfd, pairs[j].newfd);
            if (ret == -1)
                goto fail;
            pairs[j].oldfd = -1;
            j = pairs[j].older_index;
            if (j != -1)
                pairs[j].num_newer--;
        }
    }

    /* cyclic redirection: O(n) */
    for (i = 0; i < n; i++) {
        int j;
        if (pairs[i].oldfd == -1)
            continue;
        if (pairs[i].oldfd == pairs[i].newfd) { /* self cycle */
#ifdef F_GETFD
            int fd = pairs[i].oldfd;
            ret = fcntl(fd, F_GETFD);
            if (ret == -1)
                goto fail;
            if (ret & FD_CLOEXEC) {
                ret &= ~FD_CLOEXEC;
                ret = fcntl(fd, F_SETFD, ret);
                if (ret == -1)
                    goto fail;
            }
#endif
            pairs[i].oldfd = -1;
            continue;
        }
        if (extra_fd == -1) {
            extra_fd = redirect_dup(pairs[i].oldfd);
            if (extra_fd == -1)
                goto fail;
        }
        else {
            ret = redirect_dup2(pairs[i].oldfd, extra_fd);
            if (ret == -1)
                goto fail;
        }
        pairs[i].oldfd = extra_fd;
        j = pairs[i].older_index;
        pairs[i].older_index = -1;
        while (j != -1) {
            ret = redirect_dup2(pairs[j].oldfd, pairs[j].newfd);
            if (ret == -1)
                goto fail;
            pairs[j].oldfd = -1;
            j = pairs[j].older_index;
        }
    }
    if (extra_fd != -1) {
        ret = redirect_close(extra_fd);
        if (ret == -1)
            goto fail;
    }

    return 0;

fail:
    if (pairs)
        xfree(pairs);
    return -1;
}

static int
run_exec_close(VALUE ary)
{
    int i, ret;

    for (i = 0; i < RARRAY_LEN(ary); i++) {
        VALUE elt = RARRAY_AT(ary, i);
        int fd = FIX2INT(RARRAY_AT(elt, 0));
        ret = redirect_close(fd);
        if (ret == -1)
            return -1;
    }
    return 0;
}

static int
run_exec_open(VALUE ary, VALUE save)
{
    int i, ret;

    for (i = 0; i < RARRAY_LEN(ary);) {
        VALUE elt = RARRAY_AT(ary, i);;
        int fd = FIX2INT(RARRAY_AT(elt, 0));
        VALUE param = RARRAY_AT(elt, 1);
        const char *path = RSTRING_PTR(RARRAY_AT(param, 0));
        int flags = NUM2INT(RARRAY_AT(param, 1));
        int perm = NUM2INT(RARRAY_AT(param, 2));
        int need_close = 1;
        int fd2 = redirect_open(path, flags, perm);
        if (fd2 == -1) return -1;
        while (i < RARRAY_LEN(ary) &&
               (elt = RARRAY_AT(ary, i), RARRAY_AT(elt, 1) == param)) {
            fd = FIX2INT(RARRAY_AT(elt, 0));
            if (fd == fd2) {
                need_close = 0;
            }
            else {
                if (save_redirect_fd(fd, save) < 0)
                    return -1;
                ret = redirect_dup2(fd2, fd);
                if (ret == -1) return -1;
            }
            i++;
        }
        if (need_close) {
            ret = redirect_close(fd2);
            if (ret == -1) return -1;
        }
    }
    return 0;
}

#ifdef HAVE_SETPGID
static int
run_exec_pgroup(VALUE obj, VALUE save)
{
    /*
     * If FD_CLOEXEC is available, rb_fork waits the child's execve.
     * So setpgid is done in the child when rb_fork is returned in the parent.
     * No race condition, even without setpgid from the parent.
     * (Is there an environment which has setpgid but FD_CLOEXEC?)
     */
    if (!NIL_P(save)) {
        /* maybe meaningless with no fork environment... */
        rb_ary_store(save, EXEC_OPTION_PGROUP, PIDT2NUM(getpgrp()));
    }
    pid_t pgroup = NUM2PIDT(obj);
    if (pgroup == 0) {
        pgroup = getpid();
    }
    return setpgid(getpid(), pgroup);
}
#endif

#ifdef RLIM2NUM
static int
run_exec_rlimit(VALUE ary, VALUE save)
{
    int i;
    for (i = 0; i < RARRAY_LEN(ary); i++) {
        VALUE elt = RARRAY_AT(ary, i);
        int rtype = NUM2INT(RARRAY_AT(elt, 0));
        struct rlimit rlim;
        if (!NIL_P(save)) {
            if (getrlimit(rtype, &rlim) == -1)
                return -1;
            VALUE tmp = hide_obj(rb_ary_new3(3, RARRAY_AT(elt, 0),
                                             RLIM2NUM(rlim.rlim_cur),
                                             RLIM2NUM(rlim.rlim_max)));
            VALUE newary = rb_ary_entry(save, EXEC_OPTION_RLIMIT);
            if (NIL_P(newary)) {
                newary = hide_obj(rb_ary_new());
                rb_ary_store(save, EXEC_OPTION_RLIMIT, newary);
            }
            rb_ary_push(newary, tmp);
        }
        rlim.rlim_cur = NUM2RLIM(RARRAY_AT(elt, 1));
        rlim.rlim_max = NUM2RLIM(RARRAY_AT(elt, 2));
        if (setrlimit(rtype, &rlim) == -1)
            return -1;
    }
    return 0;
}
#endif

int
rb_run_exec_options(const struct rb_exec_arg *e, struct rb_exec_arg *s)
{
    VALUE options = e->options;
    VALUE soptions = Qnil;
    VALUE obj;

    if (!RTEST(options))
        return 0;

    if (s) {
        s->argc = 0;
        s->argv = NULL;
        s->prog = NULL;
        s->options = soptions = hide_obj(rb_ary_new());
        s->redirect_fds = Qnil;
    }

#ifdef HAVE_SETPGID
    obj = rb_ary_entry(options, EXEC_OPTION_PGROUP);
    if (RTEST(obj)) {
        if (run_exec_pgroup(obj, soptions) == -1)
            return -1;
    }
#endif

#ifdef RLIM2NUM
    obj = rb_ary_entry(options, EXEC_OPTION_RLIMIT);
    if (!NIL_P(obj)) {
        if (run_exec_rlimit(obj, soptions) == -1)
            return -1;
    }
#endif

    obj = rb_ary_entry(options, EXEC_OPTION_UNSETENV_OTHERS);
    if (RTEST(obj)) {
        save_env(soptions);
        rb_env_clear();
    }

    obj = rb_ary_entry(options, EXEC_OPTION_ENV);
    if (!NIL_P(obj)) {
        int i;
        save_env(soptions);
        for (i = 0; i < RARRAY_LEN(obj); i++) {
            VALUE pair = RARRAY_AT(obj, i);
            VALUE key = RARRAY_AT(pair, 0);
            VALUE val = RARRAY_AT(pair, 1);
            if (NIL_P(val))
                ruby_setenv(StringValueCStr(key), 0);
            else
                ruby_setenv(StringValueCStr(key), StringValueCStr(val));
        }
    }

    obj = rb_ary_entry(options, EXEC_OPTION_CHDIR);
    if (!NIL_P(obj)) {
        if (!NIL_P(soptions)) {
            VALUE cwd = ruby_getcwd();
            rb_ary_store(soptions, EXEC_OPTION_CHDIR,
                         hide_obj(cwd));
        }
        if (chdir(RSTRING_PTR(obj)) == -1)
            return -1;
    }

    obj = rb_ary_entry(options, EXEC_OPTION_UMASK);
    if (!NIL_P(obj)) {
        mode_t mask = NUM2LONG(obj);
        mode_t oldmask = umask(mask); /* never fail */
        if (!NIL_P(soptions))
            rb_ary_store(soptions, EXEC_OPTION_UMASK, LONG2NUM(oldmask));
    }

    obj = rb_ary_entry(options, EXEC_OPTION_DUP2);
    if (!NIL_P(obj)) {
        if (run_exec_dup2(obj, soptions) == -1)
            return -1;
    }

    obj = rb_ary_entry(options, EXEC_OPTION_CLOSE);
    if (!NIL_P(obj)) {
        if (!NIL_P(soptions))
            rb_warn("cannot close fd before spawn");
        else {
            if (run_exec_close(obj) == -1)
                return -1;
        }
    }

#ifdef HAVE_FORK
    obj = rb_ary_entry(options, EXEC_OPTION_CLOSE_OTHERS);
    if (obj != Qfalse) {
        // TODO rb_close_before_exec(3, FIX2LONG(obj), e->redirect_fds);
    }
#endif

    obj = rb_ary_entry(options, EXEC_OPTION_OPEN);
    if (!NIL_P(obj)) {
        if (run_exec_open(obj, soptions) == -1)
            return -1;
    }

    return 0;
}

int
rb_exec(const struct rb_exec_arg *e)
{
    int argc = e->argc;
    VALUE *argv = e->argv;
    const char *prog = e->prog;

    if (rb_run_exec_options(e, NULL) < 0) {
        return -1;
    }

    if (argc == 0) {
	rb_proc_exec(prog);
    }
    else {
	rb_proc_exec_n(argc, argv, prog);
    }
#ifndef FD_CLOEXEC
    preserving_errno({
	fprintf(stderr, "%s:%d: command not found: %s\n",
		rb_sourcefile(), rb_sourceline(), prog);
    });
#endif
    return -1;
}

#ifdef HAVE_FORK
static int
rb_exec_atfork(void* arg)
{
    //rb_thread_atfork_before_exec();
    return rb_exec(arg);
}
#endif

#ifdef HAVE_FORK
#ifdef FD_CLOEXEC
#if SIZEOF_INT == SIZEOF_LONG
#define proc_syswait (VALUE (*)(VALUE))rb_syswait
#else
#if 0 // not used
static VALUE
proc_syswait(VALUE pid)
{
    rb_syswait((int)pid);
    return Qnil;
}
#endif
#endif
#endif

#if 0 // not used
static int
move_fds_to_avoid_crash(int *fdp, int n, VALUE fds)
{
    long min = 0;
    int i;
    for (i = 0; i < n; i++) {
        int ret;
        while (RTEST(rb_hash_lookup(fds, INT2FIX(fdp[i])))) {
            if (min <= fdp[i])
                min = fdp[i]+1;
            while (RTEST(rb_hash_lookup(fds, INT2FIX(min))))
                min++;
            ret = fcntl(fdp[i], F_DUPFD, min);
            if (ret == -1)
                return -1;
            close(fdp[i]);
            fdp[i] = ret;
        }
    }
    return 0;
}
#endif

#if 0 // not used
static int
pipe_nocrash(int filedes[2], VALUE fds)
{
    int ret;
    ret = pipe(filedes);
    if (ret == -1)
        return -1;
    if (RTEST(fds)) {
        int save = errno;
        if (move_fds_to_avoid_crash(filedes, 2, fds) == -1) {
            close(filedes[0]);
            close(filedes[1]);
            return -1;
        }
        errno = save;
    }
    return ret;
}
#endif

/*
 * Forks child process, and returns the process ID in the parent
 * process.
 *
 * If +status+ is given, protects from any exceptions and sets the
 * jump status to it.
 *
 * In the child process, just returns 0 if +chfunc+ is +NULL+.
 * Otherwise +chfunc+ will be called with +charg+, and then the child
 * process exits with +EXIT_SUCCESS+ when it returned zero.
 *
 * In the case of the function is called and returns non-zero value,
 * the child process exits with non-+EXIT_SUCCESS+ value (normally
 * 127).  And, on the platforms where +FD_CLOEXEC+ is available,
 * +errno+ is propagated to the parent process, and this function
 * returns -1 in the parent process.  On the other platforms, just
 * returns pid.
 *
 * If fds is not Qnil, internal pipe for the errno propagation is
 * arranged to avoid conflicts of the hash keys in +fds+.
 *
 * +chfunc+ must not raise any exceptions.
 */
rb_pid_t
rb_fork(int *status, int (*chfunc)(void*), void *charg, VALUE fds)
{
    rb_pid_t pid;
    int err, state = 0;
    int ep[2];

#define prefork() (		\
	rb_io_flush(rb_stdout, 0), \
	rb_io_flush(rb_stderr, 0)	\
	)

    prefork();

    if (chfunc) {
		if (pipe(ep)) return -1;
		if (fcntl(ep[1], F_SETFD, FD_CLOEXEC)) {
		    preserving_errno((close(ep[0]), close(ep[1])));
		    return -1;
		}
    }
    for (; (pid = fork()) < 0; prefork()) {
	switch (errno) {
	  case EWOULDBLOCK:
	    if (!status && !chfunc) {
		rb_thread_sleep(1);
		continue;
	    }
	    else {
		rb_protect((VALUE (*)())rb_thread_sleep, 1, &state);
		if (status) *status = state;
		if (!state) continue;
	    }
	  default:
	    if (chfunc) {
		preserving_errno((close(ep[0]), close(ep[1])));
	    }
	    //if (state && !status) rb_jump_tag(state);
	    return -1;
	}
    }
    if (!pid) {
	//rb_thread_reset_timer_thread();
	if (chfunc) {
	    close(ep[0]);
	    if (!(*chfunc)(charg)) _exit(EXIT_SUCCESS);
	    err = errno;
	    write(ep[1], &err, sizeof(err));
#if EXIT_SUCCESS == 127
	    _exit(EXIT_FAILURE);
#else
	    _exit(127);
#endif
	}
	//rb_thread_start_timer_thread();
    }
    else if (chfunc) {
	close(ep[1]);
	if ((state = read(ep[0], &err, sizeof(err))) < 0) {
	    err = errno;
	}
	close(ep[0]);
	if (state) {
	    if (status) {
		rb_protect((VALUE (*)(VALUE))rb_syswait, (VALUE)pid, status);
	    }
	    else {
		rb_syswait(pid);
	    }
	    errno = err;
	    return -1;
	}
    }
#endif
    return pid;
}

/*
 *  call-seq:
 *     Kernel.fork  [{ block }]   => fixnum or nil
 *     Process.fork [{ block }]   => fixnum or nil
 *
 *  Creates a subprocess. If a block is specified, that block is run
 *  in the subprocess, and the subprocess terminates with a status of
 *  zero. Otherwise, the +fork+ call returns twice, once in
 *  the parent, returning the process ID of the child, and once in
 *  the child, returning _nil_. The child process can exit using
 *  <code>Kernel.exit!</code> to avoid running any
 *  <code>at_exit</code> functions. The parent process should
 *  use <code>Process.wait</code> to collect the termination statuses
 *  of its children or use <code>Process.detach</code> to register
 *  disinterest in their status; otherwise, the operating system
 *  may accumulate zombie processes.
 *
 *  The thread calling fork is the only thread in the created child process.
 *  fork doesn't copy other threads.
 */

#if 0
static VALUE
rb_f_fork(VALUE obj, SEL sel)
{
    rb_pid_t pid;

    rb_secure(2);

    switch (pid = rb_fork(0, 0, 0, Qnil)) {
      case 0:
	//rb_thread_atfork();
	if (rb_block_given_p()) {
	    int status;

	    rb_protect(rb_yield, Qundef, &status);
	    ruby_stop(status);
	}
	return Qnil;

      case -1:
	rb_sys_fail("fork(2)");
	return Qnil;

      default:
	return PIDT2NUM(pid);
    }
}
#else
# define rb_f_fork rb_f_notimplement
#endif


/*
 *  call-seq:
 *     Process.exit!(fixnum=-1)
 *
 *  Exits the process immediately. No exit handlers are
 *  run. <em>fixnum</em> is returned to the underlying system as the
 *  exit status.
 *
 *     Process.exit!(0)
 */

static VALUE
rb_f_exit_bang(VALUE obj, SEL sel, int argc, VALUE *argv)
{
    VALUE status;
    int istatus;

    rb_secure(4);
    if (argc > 0 && rb_scan_args(argc, argv, "01", &status) == 1) {
	switch (status) {
	  case Qtrue:
	    istatus = EXIT_SUCCESS;
	    break;
	  case Qfalse:
	    istatus = EXIT_FAILURE;
	    break;
	  default:
	    istatus = NUM2INT(status);
	    break;
	}
    }
    else {
	istatus = EXIT_FAILURE;
    }
    _exit(istatus);

    return Qnil;		/* not reached */
}

void
rb_exit(int status)
{
#if 0 // XXX should we call pthread_exit()
    if (GET_THREAD()->tag) {
	VALUE args[2];

	args[0] = INT2NUM(status);
	args[1] = rb_str_new2("exit");
	rb_exc_raise(rb_class_new_instance(2, args, rb_eSystemExit));
    }
#endif
    ruby_finalize();
    exit(status);
}


/*
 *  call-seq:
 *     exit(integer=0)
 *     Kernel::exit(integer=0)
 *     Process::exit(integer=0)
 *  
 *  Initiates the termination of the Ruby script by raising the
 *  <code>SystemExit</code> exception. This exception may be caught. The
 *  optional parameter is used to return a status code to the invoking
 *  environment.
 *     
 *     begin
 *       exit
 *       puts "never get here"
 *     rescue SystemExit
 *       puts "rescued a SystemExit exception"
 *     end
 *     puts "after begin block"
 *     
 *  <em>produces:</em>
 *     
 *     rescued a SystemExit exception
 *     after begin block
 *     
 *  Just prior to termination, Ruby executes any <code>at_exit</code> functions
 *  (see Kernel::at_exit) and runs any object finalizers (see
 *  ObjectSpace::define_finalizer).
 *     
 *     at_exit { puts "at_exit function" }
 *     ObjectSpace.define_finalizer("string",  proc { puts "in finalizer" })
 *     exit
 *     
 *  <em>produces:</em>
 *     
 *     at_exit function
 *     in finalizer
 */

static VALUE
rb_f_exit(VALUE obj, SEL sel, int argc, VALUE *argv)
{
    VALUE status;
    int istatus;

    rb_secure(4);
    if (argc > 0 && rb_scan_args(argc, argv, "01", &status) == 1) {
	switch (status) {
	  case Qtrue:
	    istatus = EXIT_SUCCESS;
	    break;
	  case Qfalse:
	    istatus = EXIT_FAILURE;
	    break;
	  default:
	    istatus = NUM2INT(status);
#if EXIT_SUCCESS != 0
	    if (istatus == 0)
		istatus = EXIT_SUCCESS;
#endif
	    break;
	}
    }
    else {
	istatus = EXIT_SUCCESS;
    }
    rb_exit(istatus);
    return Qnil;		/* not reached */
}


/*
 *  call-seq:
 *     abort
 *     Kernel::abort
 *     Process::abort
 *  
 *  Terminate execution immediately, effectively by calling
 *  <code>Kernel.exit(1)</code>. If _msg_ is given, it is written
 *  to STDERR prior to terminating.
 */

VALUE rb_io_puts(VALUE out, SEL sel, int argc, VALUE *argv);

static VALUE
rb_f_abort(VALUE obj, SEL sel, int argc, VALUE *argv)
{
    extern void ruby_error_print(void);

    rb_secure(4);
    if (argc == 0) {
#if 0 // XXX
	if (!NIL_P(GET_THREAD()->errinfo)) {
	    ruby_error_print();
	}
#endif
	rb_exit(EXIT_FAILURE);
    }
    else {
	VALUE args[2];

	rb_scan_args(argc, argv, "1", &args[1]);
	StringValue(argv[0]);
	rb_io_puts(rb_stderr, 0, argc, argv);
	args[0] = INT2NUM(EXIT_FAILURE);
	rb_exc_raise(rb_class_new_instance(2, args, rb_eSystemExit));
    }
    return Qnil;		/* not reached */
}


#if defined(sun)
#define signal(a,b) sigset(a,b)
#else
# if defined(POSIX_SIGNAL)
#  define signal(a,b) posix_signal(a,b)
# endif
#endif

void
rb_syswait(rb_pid_t pid)
{
    static int overriding;
#ifdef SIGHUP
    RETSIGTYPE (*hfunc)(int) = 0;
#endif
#ifdef SIGQUIT
    RETSIGTYPE (*qfunc)(int) = 0;
#endif
    RETSIGTYPE (*ifunc)(int) = 0;
    int status;
    int i, hooked = Qfalse;

    if (!overriding) {
#ifdef SIGHUP
	hfunc = signal(SIGHUP, SIG_IGN);
#endif
#ifdef SIGQUIT
	qfunc = signal(SIGQUIT, SIG_IGN);
#endif
	ifunc = signal(SIGINT, SIG_IGN);
	overriding = Qtrue;
	hooked = Qtrue;
    }

    do {
	i = rb_waitpid(pid, &status, 0);
    } while (i == -1 && errno == EINTR);

    if (hooked) {
#ifdef SIGHUP
	signal(SIGHUP, hfunc);
#endif
#ifdef SIGQUIT
	signal(SIGQUIT, qfunc);
#endif
	signal(SIGINT, ifunc);
	overriding = Qfalse;
    }
}

static rb_pid_t
rb_spawn_internal(int argc, VALUE *argv, int default_close_others)
{
    rb_pid_t status;
    VALUE prog;
    struct rb_exec_arg earg;
#if !defined HAVE_FORK
    struct rb_exec_arg sarg;
#endif

    prog = rb_exec_arg_init(argc, argv, TRUE, &earg);
    if (NIL_P(rb_ary_entry(earg.options, EXEC_OPTION_CLOSE_OTHERS))) {
        VALUE v = default_close_others ? Qtrue : Qfalse;
        rb_exec_arg_addopt(&earg, ID2SYM(rb_intern("close_others")), v);
    }
    rb_exec_arg_fixup(&earg);

#if defined HAVE_FORK
    status = rb_fork(&status, rb_exec_atfork, &earg, earg.redirect_fds);
    if (prog && earg.argc) earg.argv[0] = prog;
#else
    if (rb_run_exec_options(&earg, &sarg) < 0) {
        return -1;
    }

    argc = earg.argc;
    argv = earg.argv;
    if (prog && argc) argv[0] = prog;
# if defined HAVE_SPAWNV
    if (!argc) {
	status = proc_spawn(RSTRING_PTR(prog));
    }
    else {
	status = proc_spawn_n(argc, argv, prog);
    }
#  if defined(_WIN32)
    if (status == -1)
	rb_last_status_set(0x7f << 8, 0);
#  endif
# else
    if (argc) prog = rb_ary_join(rb_ary_new4(argc, argv), rb_str_new2(" "));
    status = system(StringValuePtr(prog));
#  if defined(__human68k__) || defined(__DJGPP__)
    rb_last_status_set(status == -1 ? 127 : status, 0);
#  else
    rb_last_status_set((status & 0xff) << 8, 0);
#  endif
# endif

    rb_run_exec_options(&sarg, NULL);
#endif
    return status;
}

rb_pid_t
rb_spawn(int argc, VALUE *argv)
{
    return rb_spawn_internal(argc, argv, Qtrue);
}

/*
 *  call-seq:
 *     system([env,] cmd [, arg, ...] [,options])    => true, false or nil
 *
 *  Executes _cmd_ in a subshell, returning +true+ if the command
 *  gives zero exit status, +false+ for non zero exit status. Returns
 *  +nil+ if command execution fails.  An error status is available in
 *  <code>$?</code>. The arguments are processed in the same way as
 *  for <code>Kernel::exec</code>.
 *
 *  The hash arguments, env and options, are same as
 *  <code>exec</code> and <code>spawn</code>.
 *  See <code>spawn</code> for details.
 *
 *     system("echo *")
 *     system("echo", "*")
 *
 *  <em>produces:</em>
 *
 *     config.h main.rb
 *     *
 */

static VALUE
rb_f_system(VALUE obj, SEL sel, int argc, VALUE *argv)
{
    int status;

#if defined(SIGCLD) && !defined(SIGCHLD)
# define SIGCHLD SIGCLD
#endif

#ifdef SIGCHLD
    RETSIGTYPE (*chfunc)(int);

    chfunc = signal(SIGCHLD, SIG_DFL);
#endif
    status = rb_spawn_internal(argc, argv, Qfalse);
#if defined(HAVE_FORK) || defined(HAVE_SPAWNV)
    if (status > 0) {
	rb_syswait(status);
    }
#endif
#ifdef SIGCHLD
    signal(SIGCHLD, chfunc);
#endif
    if (status < 0) {
	return Qnil;
    }
    status = NUM2INT(rb_last_status_get());
    if (status == EXIT_SUCCESS) return Qtrue;
    return Qfalse;
}

/*
 *  call-seq:
 *     spawn([env,] cmd [, arg, ...] [,options])     => pid
 *
 *  Similar to <code>Kernel::system</code> except for not waiting for
 *  end of _cmd_, but returns its <i>pid</i>.
 *
 *  If a hash is given as +env+, the environment is
 *  updated by +env+ before <code>exec(2)</code> in the child process.
 *
 *  If a hash is given as +options+,
 *  it specifies
 *  process group,
 *  resource limit, 
 *  current directory,
 *  umask and
 *  redirects for the child process.
 *  Also, it can be specified to clear environment variables.
 *
 *  The <code>:unsetenv_others</code> key in +options+ specifies
 *  to clear environment variables, other than specified by +env+.
 *
 *    pid = spawn(command, :unsetenv_others=>true) # no environment variable
 *    pid = spawn({"FOO"=>"BAR"}, command, :unsetenv_others=>true) # FOO only
 *
 *  The <code>:pgroup</code> key in +options+ specifies a process group.
 *  The corresponding value should be true, zero or positive integer.
 *  true and zero means the process should be a process leader.
 *  Other values specifies a process group to be belongs.
 *
 *    pid = spawn(command, :pgroup=>true) # process leader
 *    pid = spawn(command, :pgroup=>10) # belongs to the process group 10
 *
 *  The <code>:rlimit_</code><em>foo</em> key specifies a resource limit.
 *  <em>foo</em> should be one of resource types such as <code>core</code>
 *  The corresponding value should be an integer or an array which have one or
 *  two integers: same as cur_limit and max_limit arguments for
 *  Process.setrlimit.
 *
 *    pid = spawn(command, :rlimit_core=>0) # never dump core.
 *    cur, max = Process.getrlimit(:CORE)
 *    pid = spawn(command, :rlimit_core=>[0,max]) # disable core temporary.
 *    pid = spawn(command, :rlimit_core=>max) # enable core dump
 *
 *  The <code>:chdir</code> key in +options+ specifies the current directory.
 *
 *    pid = spawn(command, :chdir=>"/var/tmp")
 *
 *  The <code>:umask</code> key in +options+ specifies the umask.
 *
 *    pid = spawn(command, :umask=>077)
 *
 *  The :in, :out, :err, a fixnum, an IO and an array key specifies a redirect.
 *  The redirection maps a file descriptor in the child process.
 *
 *  For example, stderr can be merged into stdout:
 *
 *    pid = spawn(command, :err=>:out)
 *    pid = spawn(command, STDERR=>STDOUT)
 *    pid = spawn(command, 2=>1)
 *
 *  The hash keys specifies a file descriptor
 *  in the child process started by <code>spawn</code>.
 *  :err, STDERR and 2 specifies the standard error stream.
 *
 *  The hash values specifies a file descriptor
 *  in the parent process which invokes <code>spawn</code>.
 *  :out, STDOUT and 1 specifies the standard output stream.
 *
 *  The standard output in the child process is not specified.
 *  So it is inherited from the parent process.
 *
 *  The standard input stream can be specifed by :in, STDIN and 0.
 *  
 *  A filename can be specified as a hash value.
 *
 *    pid = spawn(command, STDIN=>"/dev/null") # read mode
 *    pid = spawn(command, STDOUT=>"/dev/null") # write mode
 *    pid = spawn(command, STDERR=>"log") # write mode
 *    pid = spawn(command, 3=>"/dev/null") # read mode
 *
 *  For standard output and standard error,
 *  it is opened in write mode.
 *  Otherwise read mode is used.
 *
 *  For specifying flags and permission of file creation explicitly,
 *  an array is used instead.
 *
 *    pid = spawn(command, STDIN=>["file"]) # read mode is assumed
 *    pid = spawn(command, STDIN=>["file", "r"])
 *    pid = spawn(command, STDOUT=>["log", "w"]) # 0644 assumed
 *    pid = spawn(command, STDOUT=>["log", "w", 0600])
 *    pid = spawn(command, STDOUT=>["log", File::WRONLY|File::EXCL|File::CREAT, 0600])
 *
 *  The array specifies a filename, flags and permission.
 *  The flags can be a string or an integer.
 *  If the flags is ommitted or nil, File::RDONLY is assumed.
 *  The permission should be an integer.
 *  If the permission is ommitted or nil, 0644 is assumed.
 *
 *  If an array of IOs and integers are specified as a hash key,
 *  all the elemetns are redirected.
 *
 *    # standard output and standard error is redirected to log file.
 *    pid = spawn(command, [STDOUT, STDERR]=>["log", "w"])
 *
 *  spawn closes all non-standard unspecified descriptors by default.
 *  The "standard" descriptors are 0, 1 and 2.
 *  This behavior is specified by :close_others option.
 *  :close_others doesn't affect the standard descriptors which are
 *  closed only if :close is specified explicitly.
 *
 *    pid = spawn(command, :close_others=>true)  # close 3,4,5,... (default)
 *    pid = spawn(command, :close_others=>false) # don't close 3,4,5,...
 *
 *  :close_others is true by default for spawn and IO.popen.
 *
 *  So IO.pipe and spawn can be used as IO.popen.
 *
 *    # similar to r = IO.popen(command)
 *    r, w = IO.pipe
 *    pid = spawn(command, STDOUT=>w)   # r, w is closed in the child process.
 *    w.close
 *
 *  :close is specified as a hash value to close a fd individualy.
 *
 *    f = open(foo)
 *    system(command, f=>:close)        # don't inherit f.
 *
 *  It is also possible to exchange file descriptors.
 *
 *    pid = spawn(command, STDOUT=>STDERR, STDERR=>STDOUT)
 *
 *  The hash keys specify file descriptors in the child process.
 *  The hash values specifies file descriptors in the parent process.
 *  So the above specifies exchanging STDOUT and STDERR.
 *  Internally, +spawn+ uses an extra file descriptor to resolve such cyclic
 *  file descriptor mapping.
 *
 */

static VALUE
rb_f_spawn(VALUE obj, SEL sel, int argc, VALUE *argv)
{
    rb_pid_t pid;

    pid = rb_spawn(argc, argv);
    if (pid == -1) rb_sys_fail(RSTRING_PTR(argv[0]));
    return PIDT2NUM(pid);
}

/*
 *  call-seq:
 *     sleep([duration])    => fixnum
 *
 *  Suspends the current thread for _duration_ seconds (which may be any number,
 *  including a +Float+ with fractional seconds). Returns the actual number of
 *  seconds slept (rounded), which may be less than that asked for if another
 *  thread calls <code>Thread#run</code>. Zero arguments causes +sleep+ to sleep
 *  forever.
 *
 *     Time.new    #=> 2008-03-08 19:56:19 +0900
 *     sleep 1.2   #=> 1
 *     Time.new    #=> 2008-03-08 19:56:20 +0900
 *     sleep 1.9   #=> 2
 *     Time.new    #=> 2008-03-08 19:56:22 +0900
 */

static VALUE
rb_f_sleep(VALUE recv, SEL sel, int argc, VALUE *argv)
{
    int beg, end;

    beg = time(0);
    if (argc == 0) {
	rb_thread_sleep_forever();
    }
    else if (argc == 1) {
	rb_thread_wait_for(rb_time_interval(argv[0]));
    }
    else {
	rb_raise(rb_eArgError, "wrong number of arguments");
    }

    end = time(0) - beg;

    return INT2FIX(end);
}


/*
 *  call-seq:
 *     Process.getpgrp   => integer
 *
 *  Returns the process group ID for this process. Not available on
 *  all platforms.
 *
 *     Process.getpgid(0)   #=> 25527
 *     Process.getpgrp      #=> 25527
 */

static VALUE
proc_getpgrp(VALUE rcv, SEL sel)
{
#if defined(HAVE_GETPGRP) && defined(GETPGRP_VOID) || defined(HAVE_GETPGID)
    rb_pid_t pgrp;
#endif

    rb_secure(2);
#if defined(HAVE_GETPGRP) && defined(GETPGRP_VOID)
    pgrp = getpgrp();
    if (pgrp < 0) rb_sys_fail(0);
    return PIDT2NUM(pgrp);
#else
# ifdef HAVE_GETPGID
    pgrp = getpgid(0);
    if (pgrp < 0) rb_sys_fail(0);
    return PIDT2NUM(pgrp);
# else
    rb_notimplement();
# endif
#endif
}


/*
 *  call-seq:
 *     Process.setpgrp   => 0
 *
 *  Equivalent to <code>setpgid(0,0)</code>. Not available on all
 *  platforms.
 */

static VALUE
proc_setpgrp(VALUE rcv, SEL sel)
{
    rb_secure(2);
  /* check for posix setpgid() first; this matches the posix */
  /* getpgrp() above.  It appears that configure will set SETPGRP_VOID */
  /* even though setpgrp(0,0) would be preferred. The posix call avoids */
  /* this confusion. */
#ifdef HAVE_SETPGID
    if (setpgid(0,0) < 0) rb_sys_fail(0);
#elif defined(HAVE_SETPGRP) && defined(SETPGRP_VOID)
    if (setpgrp() < 0) rb_sys_fail(0);
#else
    rb_notimplement();
#endif
    return INT2FIX(0);
}


/*
 *  call-seq:
 *     Process.getpgid(pid)   => integer
 *
 *  Returns the process group ID for the given process id. Not
 *  available on all platforms.
 *
 *     Process.getpgid(Process.ppid())   #=> 25527
 */

static VALUE
proc_getpgid(VALUE obj, SEL sel, VALUE pid)
{
#if defined(HAVE_GETPGID) && !defined(__CHECKER__)
    rb_pid_t i;

    rb_secure(2);
    i = getpgid(NUM2PIDT(pid));
    if (i < 0) rb_sys_fail(0);
    return PIDT2NUM(i);
#else
    rb_notimplement();
#endif
}


/*
 *  call-seq:
 *     Process.setpgid(pid, integer)   => 0
 *
 *  Sets the process group ID of _pid_ (0 indicates this
 *  process) to <em>integer</em>. Not available on all platforms.
 */

static VALUE
proc_setpgid(VALUE obj, SEL sel, VALUE pid, VALUE pgrp)
{
#ifdef HAVE_SETPGID
    rb_pid_t ipid, ipgrp;

    rb_secure(2);
    ipid = NUM2PIDT(pid);
    ipgrp = NUM2PIDT(pgrp);

    if (setpgid(ipid, ipgrp) < 0) rb_sys_fail(0);
    return INT2FIX(0);
#else
    rb_notimplement();
#endif
}


/*
 *  call-seq:
 *     Process.setsid   => fixnum
 *
 *  Establishes this process as a new session and process group
 *  leader, with no controlling tty. Returns the session id. Not
 *  available on all platforms.
 *
 *     Process.setsid   #=> 27422
 */

static VALUE
proc_setsid(VALUE rcv, SEL sel)
{
#if defined(HAVE_SETSID)
    rb_pid_t pid;

    rb_secure(2);
    pid = setsid();
    if (pid < 0) rb_sys_fail(0);
    return PIDT2NUM(pid);
#elif defined(HAVE_SETPGRP) && defined(TIOCNOTTY)
    rb_pid_t pid;
    int ret;

    rb_secure(2);
    pid = getpid();
#if defined(SETPGRP_VOID)
    ret = setpgrp();
    /* If `pid_t setpgrp(void)' is equivalent to setsid(),
       `ret' will be the same value as `pid', and following open() will fail.
       In Linux, `int setpgrp(void)' is equivalent to setpgid(0, 0). */
#else
    ret = setpgrp(0, pid);
#endif
    if (ret == -1) rb_sys_fail(0);

    if ((fd = open("/dev/tty", O_RDWR)) >= 0) {
	ioctl(fd, TIOCNOTTY, NULL);
	close(fd);
    }
    return PIDT2NUM(pid);
#else
    rb_notimplement();
#endif
}


/*
 *  call-seq:
 *     Process.getpriority(kind, integer)   => fixnum
 *
 *  Gets the scheduling priority for specified process, process group,
 *  or user. <em>kind</em> indicates the kind of entity to find: one
 *  of <code>Process::PRIO_PGRP</code>,
 *  <code>Process::PRIO_USER</code>, or
 *  <code>Process::PRIO_PROCESS</code>. _integer_ is an id
 *  indicating the particular process, process group, or user (an id
 *  of 0 means _current_). Lower priorities are more favorable
 *  for scheduling. Not available on all platforms.
 *
 *     Process.getpriority(Process::PRIO_USER, 0)      #=> 19
 *     Process.getpriority(Process::PRIO_PROCESS, 0)   #=> 19
 */

static VALUE
proc_getpriority(VALUE obj, SEL sel, VALUE which, VALUE who)
{
#ifdef HAVE_GETPRIORITY
    int prio, iwhich, iwho;

    rb_secure(2);
    iwhich = NUM2INT(which);
    iwho   = NUM2INT(who);

    errno = 0;
    prio = getpriority(iwhich, iwho);
    if (errno) rb_sys_fail(0);
    return INT2FIX(prio);
#else
    rb_notimplement();
#endif
}


/*
 *  call-seq:
 *     Process.setpriority(kind, integer, priority)   => 0
 *
 *  See <code>Process#getpriority</code>.
 *
 *     Process.setpriority(Process::PRIO_USER, 0, 19)      #=> 0
 *     Process.setpriority(Process::PRIO_PROCESS, 0, 19)   #=> 0
 *     Process.getpriority(Process::PRIO_USER, 0)          #=> 19
 *     Process.getpriority(Process::PRIO_PROCESS, 0)       #=> 19
 */

static VALUE
proc_setpriority(VALUE obj, SEL sel, VALUE which, VALUE who, VALUE prio)
{
#ifdef HAVE_GETPRIORITY
    int iwhich, iwho, iprio;

    rb_secure(2);
    iwhich = NUM2INT(which);
    iwho   = NUM2INT(who);
    iprio  = NUM2INT(prio);

    if (setpriority(iwhich, iwho, iprio) < 0)
	rb_sys_fail(0);
    return INT2FIX(0);
#else
    rb_notimplement();
#endif
}

#if defined(RLIM2NUM)
static int
rlimit_resource_name2int(const char *name, int casetype)
{
    size_t len = strlen(name);
    if (16 < len) return -1;
    if (casetype == 1) {
        int i;
        char *name2 = ALLOCA_N(char, len+1);
        for (i = 0; i < len; i++) {
            if (!ISLOWER(name[i]))
                return -1;
            name2[i] = TOUPPER(name[i]);
        }
        name2[len] = '\0';
        name = name2;
    }

    switch (*name) {
      case 'A':
#ifdef RLIMIT_AS
        if (strcmp(name, "AS") == 0) return RLIMIT_AS;
#endif
        break;

      case 'C':
#ifdef RLIMIT_CORE
        if (strcmp(name, "CORE") == 0) return RLIMIT_CORE;
#endif
#ifdef RLIMIT_CPU
        if (strcmp(name, "CPU") == 0) return RLIMIT_CPU;
#endif
        break;

      case 'D':
#ifdef RLIMIT_DATA
        if (strcmp(name, "DATA") == 0) return RLIMIT_DATA;
#endif
        break;

      case 'F':
#ifdef RLIMIT_FSIZE
        if (strcmp(name, "FSIZE") == 0) return RLIMIT_FSIZE;
#endif
        break;

      case 'M':
#ifdef RLIMIT_MEMLOCK
        if (strcmp(name, "MEMLOCK") == 0) return RLIMIT_MEMLOCK;
#endif
        break;

      case 'N':
#ifdef RLIMIT_NOFILE
        if (strcmp(name, "NOFILE") == 0) return RLIMIT_NOFILE;
#endif
#ifdef RLIMIT_NPROC
        if (strcmp(name, "NPROC") == 0) return RLIMIT_NPROC;
#endif
        break;

      case 'R':
#ifdef RLIMIT_RSS
        if (strcmp(name, "RSS") == 0) return RLIMIT_RSS;
#endif
        break;

      case 'S':
#ifdef RLIMIT_STACK
        if (strcmp(name, "STACK") == 0) return RLIMIT_STACK;
#endif
#ifdef RLIMIT_SBSIZE
        if (strcmp(name, "SBSIZE") == 0) return RLIMIT_SBSIZE;
#endif
        break;
    }
    return -1;
}

static int
rlimit_type_by_hname(const char *name)
{
    return rlimit_resource_name2int(name, 0);
}

static int
rlimit_type_by_lname(const char *name)
{
    return rlimit_resource_name2int(name, 1);
}

static int
rlimit_resource_type(VALUE rtype)
{
    const char *name;
    VALUE v;
    int r;

    switch (TYPE(rtype)) {
      case T_SYMBOL:
        name = rb_sym2name(rtype);
        break;

      default:
        v = rb_check_string_type(rtype);
        if (!NIL_P(v)) {
            rtype = v;
      case T_STRING:
            name = StringValueCStr(rtype);
            break;
        }
        /* fall through */

      case T_FIXNUM:
      case T_BIGNUM:
        return NUM2INT(rtype);
    }

    r = rlimit_type_by_hname(name);
    if (r != -1)
        return r;

    rb_raise(rb_eArgError, "invalid resource name: %s", name);
}

static rlim_t
rlimit_resource_value(VALUE rval)
{
    const char *name;
    VALUE v;

    switch (TYPE(rval)) {
      case T_SYMBOL:
        name = rb_sym2name(rval);
        break;

      default:
        v = rb_check_string_type(rval);
        if (!NIL_P(v)) {
            rval = v;
      case T_STRING:
            name = StringValueCStr(rval);
            break;
        }
        /* fall through */

      case T_FIXNUM:
      case T_BIGNUM:
        return NUM2RLIM(rval);
    }

#ifdef RLIM_INFINITY
    if (strcmp(name, "INFINITY") == 0) return RLIM_INFINITY;
#endif
#ifdef RLIM_SAVED_MAX
    if (strcmp(name, "SAVED_MAX") == 0) return RLIM_SAVED_MAX;
#endif
#ifdef RLIM_SAVED_CUR
    if (strcmp(name, "SAVED_CUR") == 0) return RLIM_SAVED_CUR;
#endif
    rb_raise(rb_eArgError, "invalid resource value: %s", name);
}
#endif

/*
 *  call-seq:
 *     Process.getrlimit(resource)   => [cur_limit, max_limit]
 *
 *  Gets the resource limit of the process.
 *  _cur_limit_ means current (soft) limit and
 *  _max_limit_ means maximum (hard) limit.
 *
 *  _resource_ indicates the kind of resource to limit.
 *  It is specified as a symbol such as <code>:CORE</code>,
 *  a string such as <code>"CORE"</code> or
 *  a constant such as <code>Process::RLIMIT_CORE</code>.
 *  See Process.setrlimit for details.
 *
 *  _cur_limit_ and _max_limit_ may be <code>Process::RLIM_INFINITY</code>,
 *  <code>Process::RLIM_SAVED_MAX</code> or
 *  <code>Process::RLIM_SAVED_CUR</code>.
 *  See Process.setrlimit and the system getrlimit(2) manual for details.
 */

static VALUE
proc_getrlimit(VALUE obj, SEL sel, VALUE resource)
{
#if defined(HAVE_GETRLIMIT) && defined(RLIM2NUM)
    struct rlimit rlim;

    rb_secure(2);

    if (getrlimit(rlimit_resource_type(resource), &rlim) < 0) {
	rb_sys_fail("getrlimit");
    }
    return rb_assoc_new(RLIM2NUM(rlim.rlim_cur), RLIM2NUM(rlim.rlim_max));
#else
    rb_notimplement();
#endif
}

/*
 *  call-seq:
 *     Process.setrlimit(resource, cur_limit, max_limit)        => nil
 *     Process.setrlimit(resource, cur_limit)                   => nil
 *
 *  Sets the resource limit of the process.
 *  _cur_limit_ means current (soft) limit and
 *  _max_limit_ means maximum (hard) limit.
 *
 *  If _max_limit_ is not given, _cur_limit_ is used.
 *
 *  _resource_ indicates the kind of resource to limit.
 *  It should be a symbol such as <code>:CORE</code>,
 *  a string such as <code>"CORE"</code> or
 *  a constant such as <code>Process::RLIMIT_CORE</code>.
 *  The available resources are OS dependent.
 *  Ruby may support following resources.
 *
 *  [CORE] core size (bytes) (SUSv3)
 *  [CPU] CPU time (seconds) (SUSv3)
 *  [DATA] data segment (bytes) (SUSv3)
 *  [FSIZE] file size (bytes) (SUSv3)
 *  [NOFILE] file descriptors (number) (SUSv3)
 *  [STACK] stack size (bytes) (SUSv3)
 *  [AS] total available memory (bytes) (SUSv3, NetBSD, FreeBSD, OpenBSD but 4.4BSD-Lite)
 *  [MEMLOCK] total size for mlock(2) (bytes) (4.4BSD, GNU/Linux)
 *  [NPROC] number of processes for the user (number) (4.4BSD, GNU/Linux)
 *  [RSS] resident memory size (bytes) (4.2BSD, GNU/Linux)
 *  [SBSIZE] all socket buffers (bytes) (NetBSD, FreeBSD)
 *
 *  _cur_limit_ and _max_limit_ may be
 *  <code>:INFINITY</code>, <code>"INFINITY"</code> or
 *  <code>Process::RLIM_INFINITY</code>,
 *  which means that the resource is not limited.
 *  They may be <code>Process::RLIM_SAVED_MAX</code>,
 *  <code>Process::RLIM_SAVED_CUR</code> and
 *  corresponding symbols and strings too.
 *  See system setrlimit(2) manual for details.
 *
 *  The following example raise the soft limit of core size to
 *  the hard limit to try to make core dump possible.
 *
 *    Process.setrlimit(:CORE, Process.getrlimit(:CORE)[1])
 *
 */

static VALUE
proc_setrlimit(VALUE obj, SEL sel, int argc, VALUE *argv)
{
#if defined(HAVE_SETRLIMIT) && defined(NUM2RLIM)
    VALUE resource, rlim_cur, rlim_max;
    struct rlimit rlim;

    rb_secure(2);

    rb_scan_args(argc, argv, "21", &resource, &rlim_cur, &rlim_max);
    if (rlim_max == Qnil)
        rlim_max = rlim_cur;

    rlim.rlim_cur = rlimit_resource_value(rlim_cur);
    rlim.rlim_max = rlimit_resource_value(rlim_max);

    if (setrlimit(rlimit_resource_type(resource), &rlim) < 0) {
	rb_sys_fail("setrlimit");
    }
    return Qnil;
#else
    rb_notimplement();
#endif
}

static int under_uid_switch = 0;
static void
check_uid_switch(void)
{
    rb_secure(2);
    if (under_uid_switch) {
	rb_raise(rb_eRuntimeError, "can't handle UID while evaluating block given to Process::UID.switch method");
    }
}

static int under_gid_switch = 0;
static void
check_gid_switch(void)
{
    rb_secure(2);
    if (under_gid_switch) {
	rb_raise(rb_eRuntimeError, "can't handle GID while evaluating block given to Process::UID.switch method");
    }
}


/*********************************************************************
 * Document-class: Process::Sys
 *
 *  The <code>Process::Sys</code> module contains UID and GID
 *  functions which provide direct bindings to the system calls of the
 *  same names instead of the more-portable versions of the same
 *  functionality found in the <code>Process</code>,
 *  <code>Process::UID</code>, and <code>Process::GID</code> modules.
 */


/*
 *  call-seq:
 *     Process::Sys.setuid(integer)   => nil
 *
 *  Set the user ID of the current process to _integer_. Not
 *  available on all platforms.
 *
 */

static VALUE
p_sys_setuid(VALUE obj, SEL sel, VALUE id)
{
#if defined HAVE_SETUID
    check_uid_switch();
    if (setuid(NUM2UIDT(id)) != 0) rb_sys_fail(0);
#else
    rb_notimplement();
#endif
    return Qnil;
}



/*
 *  call-seq:
 *     Process::Sys.setruid(integer)   => nil
 *
 *  Set the real user ID of the calling process to _integer_.
 *  Not available on all platforms.
 *
 */

static VALUE
p_sys_setruid(VALUE obj, SEL sel, VALUE id)
{
#if defined HAVE_SETRUID
    check_uid_switch();
    if (setruid(NUM2UIDT(id)) != 0) rb_sys_fail(0);
#else
    rb_notimplement();
#endif
    return Qnil;
}


/*
 *  call-seq:
 *     Process::Sys.seteuid(integer)   => nil
 *
 *  Set the effective user ID of the calling process to
 *  _integer_.  Not available on all platforms.
 *
 */

static VALUE
p_sys_seteuid(VALUE obj, SEL sel, VALUE id)
{
#if defined HAVE_SETEUID
    check_uid_switch();
    if (seteuid(NUM2UIDT(id)) != 0) rb_sys_fail(0);
#else
    rb_notimplement();
#endif
    return Qnil;
}


/*
 *  call-seq:
 *     Process::Sys.setreuid(rid, eid)   => nil
 *
 *  Sets the (integer) real and/or effective user IDs of the current
 *  process to _rid_ and _eid_, respectively. A value of
 *  <code>-1</code> for either means to leave that ID unchanged. Not
 *  available on all platforms.
 *
 */

static VALUE
p_sys_setreuid(VALUE obj, SEL sel, VALUE rid, VALUE eid)
{
#if defined HAVE_SETREUID
    check_uid_switch();
    if (setreuid(NUM2UIDT(rid),NUM2UIDT(eid)) != 0) rb_sys_fail(0);
#else
    rb_notimplement();
#endif
    return Qnil;
}


/*
 *  call-seq:
 *     Process::Sys.setresuid(rid, eid, sid)   => nil
 *
 *  Sets the (integer) real, effective, and saved user IDs of the
 *  current process to _rid_, _eid_, and _sid_ respectively. A
 *  value of <code>-1</code> for any value means to
 *  leave that ID unchanged. Not available on all platforms.
 *
 */

static VALUE
p_sys_setresuid(VALUE obj, SEL sel, VALUE rid, VALUE eid, VALUE sid)
{
#if defined HAVE_SETRESUID
    check_uid_switch();
    if (setresuid(NUM2UIDT(rid),NUM2UIDT(eid),NUM2UIDT(sid)) != 0) rb_sys_fail(0);
#else
    rb_notimplement();
#endif
    return Qnil;
}


/*
 *  call-seq:
 *     Process.uid           => fixnum
 *     Process::UID.rid      => fixnum
 *     Process::Sys.getuid   => fixnum
 *
 *  Returns the (real) user ID of this process.
 *
 *     Process.uid   #=> 501
 */

static VALUE
proc_getuid(VALUE obj, SEL sel)
{
    rb_uid_t uid = getuid();
    return UIDT2NUM(uid);
}


/*
 *  call-seq:
 *     Process.uid= integer   => numeric
 *
 *  Sets the (integer) user ID for this process. Not available on all
 *  platforms.
 */

static VALUE
proc_setuid(VALUE obj, SEL sel, VALUE id)
{
    rb_uid_t uid;

    check_uid_switch();

    uid = NUM2UIDT(id);
#if defined(HAVE_SETRESUID) &&  !defined(__CHECKER__)
    if (setresuid(uid, -1, -1) < 0) rb_sys_fail(0);
#elif defined HAVE_SETREUID
    if (setreuid(uid, -1) < 0) rb_sys_fail(0);
#elif defined HAVE_SETRUID
    if (setruid(uid) < 0) rb_sys_fail(0);
#elif defined HAVE_SETUID
    {
	if (geteuid() == uid) {
	    if (setuid(uid) < 0) rb_sys_fail(0);
	}
	else {
	    rb_notimplement();
	}
    }
#else
    rb_notimplement();
#endif
    return id;
}


/********************************************************************
 *
 * Document-class: Process::UID
 *
 *  The <code>Process::UID</code> module contains a collection of
 *  module functions which can be used to portably get, set, and
 *  switch the current process's real, effective, and saved user IDs.
 *
 */

static rb_uid_t SAVED_USER_ID = -1;

#ifdef BROKEN_SETREUID
int
setreuid(rb_uid_t ruid, rb_uid_t euid)
{
    if (ruid != -1 && ruid != getuid()) {
	if (euid == -1) euid = geteuid();
	if (setuid(ruid) < 0) return -1;
    }
    if (euid != -1 && euid != geteuid()) {
	if (seteuid(euid) < 0) return -1;
    }
    return 0;
}
#endif

/*
 *  call-seq:
 *     Process::UID.change_privilege(integer)   => fixnum
 *
 *  Change the current process's real and effective user ID to that
 *  specified by _integer_. Returns the new user ID. Not
 *  available on all platforms.
 *
 *     [Process.uid, Process.euid]          #=> [0, 0]
 *     Process::UID.change_privilege(31)    #=> 31
 *     [Process.uid, Process.euid]          #=> [31, 31]
 */

static VALUE
p_uid_change_privilege(VALUE obj, SEL sel, VALUE id)
{
    rb_uid_t uid;

    check_uid_switch();

    uid = NUM2UIDT(id);

    if (geteuid() == 0) { /* root-user */
#if defined(HAVE_SETRESUID)
	if (setresuid(uid, uid, uid) < 0) rb_sys_fail(0);
	SAVED_USER_ID = uid;
#elif defined(HAVE_SETUID)
	if (setuid(uid) < 0) rb_sys_fail(0);
	SAVED_USER_ID = uid;
#elif defined(HAVE_SETREUID) && !defined(OBSOLETE_SETREUID)
	if (getuid() == uid) {
	    if (SAVED_USER_ID == uid) {
		if (setreuid(-1, uid) < 0) rb_sys_fail(0);
	    } else {
		if (uid == 0) { /* (r,e,s) == (root, root, x) */
		    if (setreuid(-1, SAVED_USER_ID) < 0) rb_sys_fail(0);
		    if (setreuid(SAVED_USER_ID, 0) < 0) rb_sys_fail(0);
		    SAVED_USER_ID = 0; /* (r,e,s) == (x, root, root) */
		    if (setreuid(uid, uid) < 0) rb_sys_fail(0);
		    SAVED_USER_ID = uid;
		} else {
		    if (setreuid(0, -1) < 0) rb_sys_fail(0);
		    SAVED_USER_ID = 0;
		    if (setreuid(uid, uid) < 0) rb_sys_fail(0);
		    SAVED_USER_ID = uid;
		}
	    }
	} else {
	    if (setreuid(uid, uid) < 0) rb_sys_fail(0);
	    SAVED_USER_ID = uid;
	}
#elif defined(HAVE_SETRUID) && defined(HAVE_SETEUID)
	if (getuid() == uid) {
	    if (SAVED_USER_ID == uid) {
		if (seteuid(uid) < 0) rb_sys_fail(0);
	    } else {
		if (uid == 0) {
		    if (setruid(SAVED_USER_ID) < 0) rb_sys_fail(0);
		    SAVED_USER_ID = 0;
		    if (setruid(0) < 0) rb_sys_fail(0);
		} else {
		    if (setruid(0) < 0) rb_sys_fail(0);
		    SAVED_USER_ID = 0;
		    if (seteuid(uid) < 0) rb_sys_fail(0);
		    if (setruid(uid) < 0) rb_sys_fail(0);
		    SAVED_USER_ID = uid;
		}
	    }
	} else {
	    if (seteuid(uid) < 0) rb_sys_fail(0);
	    if (setruid(uid) < 0) rb_sys_fail(0);
	    SAVED_USER_ID = uid;
	}
#else
	rb_notimplement();
#endif
    } else { /* unprivileged user */
#if defined(HAVE_SETRESUID)
	if (setresuid((getuid() == uid)? -1: uid,
		      (geteuid() == uid)? -1: uid,
		      (SAVED_USER_ID == uid)? -1: uid) < 0) rb_sys_fail(0);
	SAVED_USER_ID = uid;
#elif defined(HAVE_SETREUID) && !defined(OBSOLETE_SETREUID)
	if (SAVED_USER_ID == uid) {
	    if (setreuid((getuid() == uid)? -1: uid,
			 (geteuid() == uid)? -1: uid) < 0) rb_sys_fail(0);
	} else if (getuid() != uid) {
	    if (setreuid(uid, (geteuid() == uid)? -1: uid) < 0) rb_sys_fail(0);
	    SAVED_USER_ID = uid;
	} else if (/* getuid() == uid && */ geteuid() != uid) {
	    if (setreuid(geteuid(), uid) < 0) rb_sys_fail(0);
	    SAVED_USER_ID = uid;
	    if (setreuid(uid, -1) < 0) rb_sys_fail(0);
	} else { /* getuid() == uid && geteuid() == uid */
	    if (setreuid(-1, SAVED_USER_ID) < 0) rb_sys_fail(0);
	    if (setreuid(SAVED_USER_ID, uid) < 0) rb_sys_fail(0);
	    SAVED_USER_ID = uid;
	    if (setreuid(uid, -1) < 0) rb_sys_fail(0);
	}
#elif defined(HAVE_SETRUID) && defined(HAVE_SETEUID)
	if (SAVED_USER_ID == uid) {
	    if (geteuid() != uid && seteuid(uid) < 0) rb_sys_fail(0);
	    if (getuid() != uid && setruid(uid) < 0) rb_sys_fail(0);
	} else if (/* SAVED_USER_ID != uid && */ geteuid() == uid) {
	    if (getuid() != uid) {
		if (setruid(uid) < 0) rb_sys_fail(0);
		SAVED_USER_ID = uid;
	    } else {
		if (setruid(SAVED_USER_ID) < 0) rb_sys_fail(0);
		SAVED_USER_ID = uid;
		if (setruid(uid) < 0) rb_sys_fail(0);
	    }
	} else if (/* geteuid() != uid && */ getuid() == uid) {
	    if (seteuid(uid) < 0) rb_sys_fail(0);
	    if (setruid(SAVED_USER_ID) < 0) rb_sys_fail(0);
	    SAVED_USER_ID = uid;
	    if (setruid(uid) < 0) rb_sys_fail(0);
	} else {
	    errno = EPERM;
	    rb_sys_fail(0);
	}
#elif defined HAVE_44BSD_SETUID
	if (getuid() == uid) {
	    /* (r,e,s)==(uid,?,?) ==> (uid,uid,uid) */
	    if (setuid(uid) < 0) rb_sys_fail(0);
	    SAVED_USER_ID = uid;
	} else {
	    errno = EPERM;
	    rb_sys_fail(0);
	}
#elif defined HAVE_SETEUID
	if (getuid() == uid && SAVED_USER_ID == uid) {
	    if (seteuid(uid) < 0) rb_sys_fail(0);
	} else {
	    errno = EPERM;
	    rb_sys_fail(0);
	}
#elif defined HAVE_SETUID
	if (getuid() == uid && SAVED_USER_ID == uid) {
	    if (setuid(uid) < 0) rb_sys_fail(0);
	} else {
	    errno = EPERM;
	    rb_sys_fail(0);
	}
#else
	rb_notimplement();
#endif
    }
    return id;
}



/*
 *  call-seq:
 *     Process::Sys.setgid(integer)   => nil
 *
 *  Set the group ID of the current process to _integer_. Not
 *  available on all platforms.
 *
 */

static VALUE
p_sys_setgid(VALUE obj, SEL sel, VALUE id)
{
#if defined HAVE_SETGID
    check_gid_switch();
    if (setgid(NUM2GIDT(id)) != 0) rb_sys_fail(0);
#else
    rb_notimplement();
#endif
    return Qnil;
}


/*
 *  call-seq:
 *     Process::Sys.setrgid(integer)   => nil
 *
 *  Set the real group ID of the calling process to _integer_.
 *  Not available on all platforms.
 *
 */

static VALUE
p_sys_setrgid(VALUE obj, SEL sel, VALUE id)
{
#if defined HAVE_SETRGID
    check_gid_switch();
    if (setrgid(NUM2GIDT(id)) != 0) rb_sys_fail(0);
#else
    rb_notimplement();
#endif
    return Qnil;
}



/*
 *  call-seq:
 *     Process::Sys.setegid(integer)   => nil
 *
 *  Set the effective group ID of the calling process to
 *  _integer_.  Not available on all platforms.
 *
 */

static VALUE
p_sys_setegid(VALUE obj, SEL sel, VALUE id)
{
#if defined HAVE_SETEGID
    check_gid_switch();
    if (setegid(NUM2GIDT(id)) != 0) rb_sys_fail(0);
#else
    rb_notimplement();
#endif
    return Qnil;
}


/*
 *  call-seq:
 *     Process::Sys.setregid(rid, eid)   => nil
 *
 *  Sets the (integer) real and/or effective group IDs of the current
 *  process to <em>rid</em> and <em>eid</em>, respectively. A value of
 *  <code>-1</code> for either means to leave that ID unchanged. Not
 *  available on all platforms.
 *
 */

static VALUE
p_sys_setregid(VALUE obj, SEL sel, VALUE rid, VALUE eid)
{
#if defined HAVE_SETREGID
    check_gid_switch();
    if (setregid(NUM2GIDT(rid),NUM2GIDT(eid)) != 0) rb_sys_fail(0);
#else
    rb_notimplement();
#endif
    return Qnil;
}

/*
 *  call-seq:
 *     Process::Sys.setresgid(rid, eid, sid)   => nil
 *
 *  Sets the (integer) real, effective, and saved user IDs of the
 *  current process to <em>rid</em>, <em>eid</em>, and <em>sid</em>
 *  respectively. A value of <code>-1</code> for any value means to
 *  leave that ID unchanged. Not available on all platforms.
 *
 */

static VALUE
p_sys_setresgid(VALUE obj, SEL sel, VALUE rid, VALUE eid, VALUE sid)
{
#if defined HAVE_SETRESGID
    check_gid_switch();
    if (setresgid(NUM2GIDT(rid),NUM2GIDT(eid),NUM2GIDT(sid)) != 0) rb_sys_fail(0);
#else
    rb_notimplement();
#endif
    return Qnil;
}


/*
 *  call-seq:
 *     Process::Sys.issetugid   => true or false
 *
 *  Returns +true+ if the process was created as a result
 *  of an execve(2) system call which had either of the setuid or
 *  setgid bits set (and extra privileges were given as a result) or
 *  if it has changed any of its real, effective or saved user or
 *  group IDs since it began execution.
 *
 */

static VALUE
p_sys_issetugid(VALUE obj, SEL sel)
{
#if defined HAVE_ISSETUGID
    rb_secure(2);
    if (issetugid()) {
	return Qtrue;
    } else {
	return Qfalse;
    }
#else
    rb_notimplement();
    return Qnil;		/* not reached */
#endif
}


/*
 *  call-seq:
 *     Process.gid           => fixnum
 *     Process::GID.rid      => fixnum
 *     Process::Sys.getgid   => fixnum
 *
 *  Returns the (real) group ID for this process.
 *
 *     Process.gid   #=> 500
 */

static VALUE
proc_getgid(VALUE obj, SEL sel)
{
    rb_gid_t gid = getgid();
    return GIDT2NUM(gid);
}


/*
 *  call-seq:
 *     Process.gid= fixnum   => fixnum
 *
 *  Sets the group ID for this process.
 */

static VALUE
proc_setgid(VALUE obj, SEL sel, VALUE id)
{
    rb_gid_t gid;

    check_gid_switch();

    gid = NUM2GIDT(id);
#if defined(HAVE_SETRESGID) && !defined(__CHECKER__)
    if (setresgid(gid, -1, -1) < 0) rb_sys_fail(0);
#elif defined HAVE_SETREGID
    if (setregid(gid, -1) < 0) rb_sys_fail(0);
#elif defined HAVE_SETRGID
    if (setrgid(gid) < 0) rb_sys_fail(0);
#elif defined HAVE_SETGID
    {
	if (getegid() == gid) {
	    if (setgid(gid) < 0) rb_sys_fail(0);
	}
	else {
	    rb_notimplement();
	}
    }
#else
    rb_notimplement();
#endif
    return GIDT2NUM(gid);
}


static size_t maxgroups = NGROUPS_MAX;


/*
 *  call-seq:
 *     Process.groups   => array
 *
 *  Get an <code>Array</code> of the gids of groups in the
 *  supplemental group access list for this process.
 *
 *     Process.groups   #=> [27, 6, 10, 11]
 *
 */

static VALUE
proc_getgroups(VALUE obj, SEL sel)
{
#ifdef HAVE_GETGROUPS
    VALUE ary;
    size_t ngroups;
    rb_gid_t *groups;
    int i;

    groups = ALLOCA_N(rb_gid_t, maxgroups);

    ngroups = getgroups(maxgroups, groups);
    if (ngroups == -1)
	rb_sys_fail(0);

    ary = rb_ary_new();
    for (i = 0; i < ngroups; i++)
	rb_ary_push(ary, GIDT2NUM(groups[i]));

    return ary;
#else
    rb_notimplement();
    return Qnil;
#endif
}


/*
 *  call-seq:
 *     Process.groups= array   => array
 *
 *  Set the supplemental group access list to the given
 *  <code>Array</code> of group IDs.
 *
 *     Process.groups   #=> [0, 1, 2, 3, 4, 6, 10, 11, 20, 26, 27]
 *     Process.groups = [27, 6, 10, 11]   #=> [27, 6, 10, 11]
 *     Process.groups   #=> [27, 6, 10, 11]
 *
 */

static VALUE
proc_setgroups(VALUE obj, SEL sel, VALUE ary)
{
#ifdef HAVE_SETGROUPS
    size_t ngroups;
    rb_gid_t *groups;
    int i;
    struct group *gr;

    Check_Type(ary, T_ARRAY);

    ngroups = RARRAY_LEN(ary);
    if (ngroups > maxgroups)
	rb_raise(rb_eArgError, "too many groups, %lu max", (unsigned long)maxgroups);

    groups = ALLOCA_N(rb_gid_t, ngroups);

    for (i = 0; i < ngroups && i < RARRAY_LEN(ary); i++) {
	VALUE g = RARRAY_AT(ary, i);

	if (FIXNUM_P(g)) {
	    groups[i] = NUM2GIDT(g);
	}
	else {
	    VALUE tmp = rb_check_string_type(g);

	    if (NIL_P(tmp)) {
		groups[i] = NUM2GIDT(g);
	    }
	    else {
		gr = getgrnam(RSTRING_PTR(tmp));
		if (gr == NULL)
		    rb_raise(rb_eArgError,
			     "can't find group for %s", RSTRING_PTR(tmp));
		groups[i] = gr->gr_gid;
	    }
	}
    }

    i = setgroups(ngroups, groups);
    if (i == -1)
	rb_sys_fail(0);

    return proc_getgroups(obj, 0);
#else
    rb_notimplement();
    return Qnil;
#endif
}


/*
 *  call-seq:
 *     Process.initgroups(username, gid)   => array
 *
 *  Initializes the supplemental group access list by reading the
 *  system group database and using all groups of which the given user
 *  is a member. The group with the specified <em>gid</em> is also
 *  added to the list. Returns the resulting <code>Array</code> of the
 *  gids of all the groups in the supplementary group access list. Not
 *  available on all platforms.
 *
 *     Process.groups   #=> [0, 1, 2, 3, 4, 6, 10, 11, 20, 26, 27]
 *     Process.initgroups( "mgranger", 30 )   #=> [30, 6, 10, 11]
 *     Process.groups   #=> [30, 6, 10, 11]
 *
 */

static VALUE
proc_initgroups(VALUE obj, SEL sel, VALUE uname, VALUE base_grp)
{
#ifdef HAVE_INITGROUPS
    if (initgroups(StringValuePtr(uname), NUM2GIDT(base_grp)) != 0) {
	rb_sys_fail(0);
    }
    return proc_getgroups(obj, 0);
#else
    rb_notimplement();
    return Qnil;
#endif
}


/*
 *  call-seq:
 *     Process.maxgroups   => fixnum
 *
 *  Returns the maximum number of gids allowed in the supplemental
 *  group access list.
 *
 *     Process.maxgroups   #=> 32
 */

static VALUE
proc_getmaxgroups(VALUE obj, SEL sel)
{
    return INT2FIX(maxgroups);
}


/*
 *  call-seq:
 *     Process.maxgroups= fixnum   => fixnum
 *
 *  Sets the maximum number of gids allowed in the supplemental group
 *  access list.
 */

static VALUE
proc_setmaxgroups(VALUE obj, SEL sel, VALUE val)
{
    size_t  ngroups = FIX2INT(val);

    if (ngroups > 4096)
	ngroups = 4096;

    maxgroups = ngroups;

    return INT2FIX(maxgroups);
}

/*
 *  call-seq:
 *     Process.daemon()                        => fixnum
 *     Process.daemon(nochdir=nil,noclose=nil) => fixnum
 *
 *  Detach the process from controlling terminal and run in
 *  the background as system daemon.  Unless the argument
 *  nochdir is true (i.e. non false), it changes the current
 *  working directory to the root ("/"). Unless the argument
 *  noclose is true, daemon() will redirect standard input,
 *  standard output and standard error to /dev/null.
 */

static VALUE
proc_daemon(VALUE rcv, SEL sel, int argc, VALUE *argv)
{
    VALUE nochdir, noclose;
#if defined(HAVE_DAEMON) || defined(HAVE_FORK)
    int n;
#endif

    rb_secure(2);
    rb_scan_args(argc, argv, "02", &nochdir, &noclose);

#if defined(HAVE_DAEMON)
    n = daemon(RTEST(nochdir), RTEST(noclose));
    if (n < 0) rb_sys_fail("daemon");
    return INT2FIX(n);
#elif defined(HAVE_FORK)
    switch (rb_fork(0, 0, 0, Qnil)) {
      case -1:
	return (-1);
      case 0:
	break;
      default:
	_exit(0);
    }

    proc_setsid(0, 0);

    if (!RTEST(nochdir))
	(void)chdir("/");

    if (!RTEST(noclose) && (n = open("/dev/null", O_RDWR, 0)) != -1) {
	(void)dup2(n, 0);
	(void)dup2(n, 1);
	(void)dup2(n, 2);
	if (n > 2)
	    (void)close (n);
    }
    return INT2FIX(0);
#else
    rb_notimplement();
#endif
}

/********************************************************************
 *
 * Document-class: Process::GID
 *
 *  The <code>Process::GID</code> module contains a collection of
 *  module functions which can be used to portably get, set, and
 *  switch the current process's real, effective, and saved group IDs.
 *
 */

static int SAVED_GROUP_ID = -1;

#ifdef BROKEN_SETREGID
int
setregid(rb_gid_t rgid, rb_gid_t egid)
{
    if (rgid != -1 && rgid != getgid()) {
	if (egid == -1) egid = getegid();
	if (setgid(rgid) < 0) return -1;
    }
    if (egid != -1 && egid != getegid()) {
	if (setegid(egid) < 0) return -1;
    }
    return 0;
}
#endif

/*
 *  call-seq:
 *     Process::GID.change_privilege(integer)   => fixnum
 *
 *  Change the current process's real and effective group ID to that
 *  specified by _integer_. Returns the new group ID. Not
 *  available on all platforms.
 *
 *     [Process.gid, Process.egid]          #=> [0, 0]
 *     Process::GID.change_privilege(33)    #=> 33
 *     [Process.gid, Process.egid]          #=> [33, 33]
 */

static VALUE
p_gid_change_privilege(VALUE obj, SEL sel, VALUE id)
{
    rb_gid_t gid;

    check_gid_switch();

    gid = NUM2GIDT(id);

    if (geteuid() == 0) { /* root-user */
#if defined(HAVE_SETRESGID)
	if (setresgid(gid, gid, gid) < 0) rb_sys_fail(0);
	SAVED_GROUP_ID = gid;
#elif defined HAVE_SETGID
	if (setgid(gid) < 0) rb_sys_fail(0);
	SAVED_GROUP_ID = gid;
#elif defined(HAVE_SETREGID) && !defined(OBSOLETE_SETREGID)
	if (getgid() == gid) {
	    if (SAVED_GROUP_ID == gid) {
		if (setregid(-1, gid) < 0) rb_sys_fail(0);
	    } else {
		if (gid == 0) { /* (r,e,s) == (root, y, x) */
		    if (setregid(-1, SAVED_GROUP_ID) < 0) rb_sys_fail(0);
		    if (setregid(SAVED_GROUP_ID, 0) < 0) rb_sys_fail(0);
		    SAVED_GROUP_ID = 0; /* (r,e,s) == (x, root, root) */
		    if (setregid(gid, gid) < 0) rb_sys_fail(0);
		    SAVED_GROUP_ID = gid;
		} else { /* (r,e,s) == (z, y, x) */
		    if (setregid(0, 0) < 0) rb_sys_fail(0);
		    SAVED_GROUP_ID = 0;
		    if (setregid(gid, gid) < 0) rb_sys_fail(0);
		    SAVED_GROUP_ID = gid;
		}
	    }
	} else {
	    if (setregid(gid, gid) < 0) rb_sys_fail(0);
	    SAVED_GROUP_ID = gid;
	}
#elif defined(HAVE_SETRGID) && defined (HAVE_SETEGID)
	if (getgid() == gid) {
	    if (SAVED_GROUP_ID == gid) {
		if (setegid(gid) < 0) rb_sys_fail(0);
	    } else {
		if (gid == 0) {
		    if (setegid(gid) < 0) rb_sys_fail(0);
		    if (setrgid(SAVED_GROUP_ID) < 0) rb_sys_fail(0);
		    SAVED_GROUP_ID = 0;
		    if (setrgid(0) < 0) rb_sys_fail(0);
		} else {
		    if (setrgid(0) < 0) rb_sys_fail(0);
		    SAVED_GROUP_ID = 0;
		    if (setegid(gid) < 0) rb_sys_fail(0);
		    if (setrgid(gid) < 0) rb_sys_fail(0);
		    SAVED_GROUP_ID = gid;
		}
	    }
	} else {
	    if (setegid(gid) < 0) rb_sys_fail(0);
	    if (setrgid(gid) < 0) rb_sys_fail(0);
	    SAVED_GROUP_ID = gid;
	}
#else
	rb_notimplement();
#endif
    } else { /* unprivileged user */
#if defined(HAVE_SETRESGID)
	if (setresgid((getgid() == gid)? -1: gid,
		      (getegid() == gid)? -1: gid,
		      (SAVED_GROUP_ID == gid)? -1: gid) < 0) rb_sys_fail(0);
	SAVED_GROUP_ID = gid;
#elif defined(HAVE_SETREGID) && !defined(OBSOLETE_SETREGID)
	if (SAVED_GROUP_ID == gid) {
	    if (setregid((getgid() == gid)? -1: gid,
			 (getegid() == gid)? -1: gid) < 0) rb_sys_fail(0);
	} else if (getgid() != gid) {
	    if (setregid(gid, (getegid() == gid)? -1: gid) < 0) rb_sys_fail(0);
	    SAVED_GROUP_ID = gid;
	} else if (/* getgid() == gid && */ getegid() != gid) {
	    if (setregid(getegid(), gid) < 0) rb_sys_fail(0);
	    SAVED_GROUP_ID = gid;
	    if (setregid(gid, -1) < 0) rb_sys_fail(0);
	} else { /* getgid() == gid && getegid() == gid */
	    if (setregid(-1, SAVED_GROUP_ID) < 0) rb_sys_fail(0);
	    if (setregid(SAVED_GROUP_ID, gid) < 0) rb_sys_fail(0);
	    SAVED_GROUP_ID = gid;
	    if (setregid(gid, -1) < 0) rb_sys_fail(0);
	}
#elif defined(HAVE_SETRGID) && defined(HAVE_SETEGID)
	if (SAVED_GROUP_ID == gid) {
	    if (getegid() != gid && setegid(gid) < 0) rb_sys_fail(0);
	    if (getgid() != gid && setrgid(gid) < 0) rb_sys_fail(0);
	} else if (/* SAVED_GROUP_ID != gid && */ getegid() == gid) {
	    if (getgid() != gid) {
		if (setrgid(gid) < 0) rb_sys_fail(0);
		SAVED_GROUP_ID = gid;
	    } else {
		if (setrgid(SAVED_GROUP_ID) < 0) rb_sys_fail(0);
		SAVED_GROUP_ID = gid;
		if (setrgid(gid) < 0) rb_sys_fail(0);
	}
	} else if (/* getegid() != gid && */ getgid() == gid) {
	    if (setegid(gid) < 0) rb_sys_fail(0);
	    if (setrgid(SAVED_GROUP_ID) < 0) rb_sys_fail(0);
	    SAVED_GROUP_ID = gid;
	    if (setrgid(gid) < 0) rb_sys_fail(0);
	} else {
	    errno = EPERM;
	    rb_sys_fail(0);
	}
#elif defined HAVE_44BSD_SETGID
	if (getgid() == gid) {
	    /* (r,e,s)==(gid,?,?) ==> (gid,gid,gid) */
	    if (setgid(gid) < 0) rb_sys_fail(0);
	    SAVED_GROUP_ID = gid;
	} else {
	    errno = EPERM;
	    rb_sys_fail(0);
	}
#elif defined HAVE_SETEGID
	if (getgid() == gid && SAVED_GROUP_ID == gid) {
	    if (setegid(gid) < 0) rb_sys_fail(0);
	} else {
	    errno = EPERM;
	    rb_sys_fail(0);
	}
#elif defined HAVE_SETGID
	if (getgid() == gid && SAVED_GROUP_ID == gid) {
	    if (setgid(gid) < 0) rb_sys_fail(0);
	} else {
	    errno = EPERM;
	    rb_sys_fail(0);
	}
#else
	rb_notimplement();
#endif
    }
    return id;
}


/*
 *  call-seq:
 *     Process.euid           => fixnum
 *     Process::UID.eid       => fixnum
 *     Process::Sys.geteuid   => fixnum
 *
 *  Returns the effective user ID for this process.
 *
 *     Process.euid   #=> 501
 */

static VALUE
proc_geteuid(VALUE obj, SEL sel)
{
    rb_uid_t euid = geteuid();
    return UIDT2NUM(euid);
}


/*
 *  call-seq:
 *     Process.euid= integer
 *
 *  Sets the effective user ID for this process. Not available on all
 *  platforms.
 */

static VALUE
proc_seteuid(VALUE obj, SEL sel, VALUE euid)
{
    rb_uid_t uid;

    check_uid_switch();

    uid = NUM2UIDT(euid);
#if defined(HAVE_SETRESUID) && !defined(__CHECKER__)
    if (setresuid(-1, uid, -1) < 0) rb_sys_fail(0);
#elif defined HAVE_SETREUID
    if (setreuid(-1, uid) < 0) rb_sys_fail(0);
#elif defined HAVE_SETEUID
    if (seteuid(uid) < 0) rb_sys_fail(0);
#elif defined HAVE_SETUID
    if (uid == getuid()) {
	if (setuid(uid) < 0) rb_sys_fail(0);
    }
    else {
	rb_notimplement();
    }
#else
    rb_notimplement();
#endif
    return euid;
}

static rb_uid_t
rb_seteuid_core(rb_uid_t euid)
{
    rb_uid_t uid;

    check_uid_switch();

    uid = getuid();

#if defined(HAVE_SETRESUID) && !defined(__CHECKER__)
    if (uid != euid) {
	if (setresuid(-1,euid,euid) < 0) rb_sys_fail(0);
	SAVED_USER_ID = euid;
    } else {
	if (setresuid(-1,euid,-1) < 0) rb_sys_fail(0);
    }
#elif defined(HAVE_SETREUID) && !defined(OBSOLETE_SETREUID)
    if (setreuid(-1, euid) < 0) rb_sys_fail(0);
    if (uid != euid) {
	if (setreuid(euid,uid) < 0) rb_sys_fail(0);
	if (setreuid(uid,euid) < 0) rb_sys_fail(0);
	SAVED_USER_ID = euid;
    }
#elif defined HAVE_SETEUID
    if (seteuid(euid) < 0) rb_sys_fail(0);
#elif defined HAVE_SETUID
    if (geteuid() == 0) rb_sys_fail(0);
    if (setuid(euid) < 0) rb_sys_fail(0);
#else
    rb_notimplement();
#endif
    return euid;
}


/*
 *  call-seq:
 *     Process::UID.grant_privilege(integer)   => fixnum
 *     Process::UID.eid= integer               => fixnum
 *
 *  Set the effective user ID, and if possible, the saved user ID of
 *  the process to the given _integer_. Returns the new
 *  effective user ID. Not available on all platforms.
 *
 *     [Process.uid, Process.euid]          #=> [0, 0]
 *     Process::UID.grant_privilege(31)     #=> 31
 *     [Process.uid, Process.euid]          #=> [0, 31]
 */

static VALUE
p_uid_grant_privilege(VALUE obj, SEL sel, VALUE id)
{
    rb_seteuid_core(NUM2UIDT(id));
    return id;
}


/*
 *  call-seq:
 *     Process.egid          => fixnum
 *     Process::GID.eid      => fixnum
 *     Process::Sys.geteid   => fixnum
 *
 *  Returns the effective group ID for this process. Not available on
 *  all platforms.
 *
 *     Process.egid   #=> 500
 */

static VALUE
proc_getegid(VALUE obj, SEL sel)
{
    rb_gid_t egid = getegid();

    return GIDT2NUM(egid);
}


/*
 *  call-seq:
 *     Process.egid = fixnum   => fixnum
 *
 *  Sets the effective group ID for this process. Not available on all
 *  platforms.
 */

static VALUE
proc_setegid(VALUE obj, SEL sel, VALUE egid)
{
    rb_gid_t gid;

    check_gid_switch();

    gid = NUM2GIDT(egid);
#if defined(HAVE_SETRESGID) && !defined(__CHECKER__)
    if (setresgid(-1, gid, -1) < 0) rb_sys_fail(0);
#elif defined HAVE_SETREGID
    if (setregid(-1, gid) < 0) rb_sys_fail(0);
#elif defined HAVE_SETEGID
    if (setegid(gid) < 0) rb_sys_fail(0);
#elif defined HAVE_SETGID
    if (gid == getgid()) {
	if (setgid(gid) < 0) rb_sys_fail(0);
    }
    else {
	rb_notimplement();
    }
#else
    rb_notimplement();
#endif
    return egid;
}

static rb_gid_t
rb_setegid_core(rb_gid_t egid)
{
    rb_gid_t gid;

    check_gid_switch();

    gid = getgid();

#if defined(HAVE_SETRESGID) && !defined(__CHECKER__)
    if (gid != egid) {
	if (setresgid(-1,egid,egid) < 0) rb_sys_fail(0);
	SAVED_GROUP_ID = egid;
    } else {
	if (setresgid(-1,egid,-1) < 0) rb_sys_fail(0);
    }
#elif defined(HAVE_SETREGID) && !defined(OBSOLETE_SETREGID)
    if (setregid(-1, egid) < 0) rb_sys_fail(0);
    if (gid != egid) {
	if (setregid(egid,gid) < 0) rb_sys_fail(0);
	if (setregid(gid,egid) < 0) rb_sys_fail(0);
	SAVED_GROUP_ID = egid;
    }
#elif defined HAVE_SETEGID
    if (setegid(egid) < 0) rb_sys_fail(0);
#elif defined HAVE_SETGID
    if (geteuid() == 0 /* root user */) rb_sys_fail(0);
    if (setgid(egid) < 0) rb_sys_fail(0);
#else
    rb_notimplement();
#endif
    return egid;
}


/*
 *  call-seq:
 *     Process::GID.grant_privilege(integer)    => fixnum
 *     Process::GID.eid = integer               => fixnum
 *
 *  Set the effective group ID, and if possible, the saved group ID of
 *  the process to the given _integer_. Returns the new
 *  effective group ID. Not available on all platforms.
 *
 *     [Process.gid, Process.egid]          #=> [0, 0]
 *     Process::GID.grant_privilege(31)     #=> 33
 *     [Process.gid, Process.egid]          #=> [0, 33]
 */

static VALUE
p_gid_grant_privilege(VALUE obj, SEL sel, VALUE id)
{
    rb_setegid_core(NUM2GIDT(id));
    return id;
}


/*
 *  call-seq:
 *     Process::UID.re_exchangeable?   => true or false
 *
 *  Returns +true+ if the real and effective user IDs of a
 *  process may be exchanged on the current platform.
 *
 */

static VALUE
p_uid_exchangeable(VALUE rcv, SEL sel)
{
#if defined(HAVE_SETRESUID) &&  !defined(__CHECKER__)
    return Qtrue;
#elif defined(HAVE_SETREUID) && !defined(OBSOLETE_SETREUID)
    return Qtrue;
#else
    return Qfalse;
#endif
}


/*
 *  call-seq:
 *     Process::UID.re_exchange   => fixnum
 *
 *  Exchange real and effective user IDs and return the new effective
 *  user ID. Not available on all platforms.
 *
 *     [Process.uid, Process.euid]   #=> [0, 31]
 *     Process::UID.re_exchange      #=> 0
 *     [Process.uid, Process.euid]   #=> [31, 0]
 */

static VALUE
p_uid_exchange(VALUE obj, SEL sel)
{
    rb_uid_t uid, euid;

    check_uid_switch();

    uid = getuid();
    euid = geteuid();

#if defined(HAVE_SETRESUID) &&  !defined(__CHECKER__)
    if (setresuid(euid, uid, uid) < 0) rb_sys_fail(0);
    SAVED_USER_ID = uid;
#elif defined(HAVE_SETREUID) && !defined(OBSOLETE_SETREUID)
    if (setreuid(euid,uid) < 0) rb_sys_fail(0);
    SAVED_USER_ID = uid;
#else
    rb_notimplement();
#endif
    return UIDT2NUM(uid);
}


/*
 *  call-seq:
 *     Process::GID.re_exchangeable?   => true or false
 *
 *  Returns +true+ if the real and effective group IDs of a
 *  process may be exchanged on the current platform.
 *
 */

static VALUE
p_gid_exchangeable(VALUE rcv, SEL sel)
{
#if defined(HAVE_SETRESGID) &&  !defined(__CHECKER__)
    return Qtrue;
#elif defined(HAVE_SETREGID) && !defined(OBSOLETE_SETREGID)
    return Qtrue;
#else
    return Qfalse;
#endif
}


/*
 *  call-seq:
 *     Process::GID.re_exchange   => fixnum
 *
 *  Exchange real and effective group IDs and return the new effective
 *  group ID. Not available on all platforms.
 *
 *     [Process.gid, Process.egid]   #=> [0, 33]
 *     Process::GID.re_exchange      #=> 0
 *     [Process.gid, Process.egid]   #=> [33, 0]
 */

static VALUE
p_gid_exchange(VALUE obj, SEL sel)
{
    rb_gid_t gid, egid;

    check_gid_switch();

    gid = getgid();
    egid = getegid();

#if defined(HAVE_SETRESGID) &&  !defined(__CHECKER__)
    if (setresgid(egid, gid, gid) < 0) rb_sys_fail(0);
    SAVED_GROUP_ID = gid;
#elif defined(HAVE_SETREGID) && !defined(OBSOLETE_SETREGID)
    if (setregid(egid,gid) < 0) rb_sys_fail(0);
    SAVED_GROUP_ID = gid;
#else
    rb_notimplement();
#endif
    return GIDT2NUM(gid);
}

/* [MG] :FIXME: Is this correct? I'm not sure how to phrase this. */

/*
 *  call-seq:
 *     Process::UID.sid_available?   => true or false
 *
 *  Returns +true+ if the current platform has saved user
 *  ID functionality.
 *
 */

static VALUE
p_uid_have_saved_id(VALUE rcv, SEL sel)
{
#if defined(HAVE_SETRESUID) || defined(HAVE_SETEUID) || defined(_POSIX_SAVED_IDS)
    return Qtrue;
#else
    return Qfalse;
#endif
}


#if defined(HAVE_SETRESUID) || defined(HAVE_SETEUID) || defined(_POSIX_SAVED_IDS)
static VALUE
p_uid_sw_ensure(rb_uid_t id)
{
    under_uid_switch = 0;
    id = rb_seteuid_core(id);
    return UIDT2NUM(id);
}


/*
 *  call-seq:
 *     Process::UID.switch              => fixnum
 *     Process::UID.switch {|| block}   => object
 *
 *  Switch the effective and real user IDs of the current process. If
 *  a <em>block</em> is given, the user IDs will be switched back
 *  after the block is executed. Returns the new effective user ID if
 *  called without a block, and the return value of the block if one
 *  is given.
 *
 */

static VALUE
p_uid_switch(VALUE obj, SEL sel)
{
    rb_uid_t uid, euid;

    check_uid_switch();

    uid = getuid();
    euid = geteuid();

    if (uid != euid) {
	proc_seteuid(obj, 0, UIDT2NUM(uid));
	if (rb_block_given_p()) {
	    under_uid_switch = 1;
	    return rb_ensure(rb_yield, Qnil, p_uid_sw_ensure, SAVED_USER_ID);
	} else {
	    return UIDT2NUM(euid);
	}
    } else if (euid != SAVED_USER_ID) {
	proc_seteuid(obj, 0, UIDT2NUM(SAVED_USER_ID));
	if (rb_block_given_p()) {
	    under_uid_switch = 1;
	    return rb_ensure(rb_yield, Qnil, p_uid_sw_ensure, euid);
	} else {
	    return UIDT2NUM(uid);
	}
    } else {
	errno = EPERM;
	rb_sys_fail(0);
    }
}
#else
static VALUE
p_uid_sw_ensure(VALUE obj)
{
    under_uid_switch = 0;
    return p_uid_exchange(obj, 0);
}

static VALUE
p_uid_switch(VALUE obj, SEL sel)
{
    rb_uid_t uid, euid;

    check_uid_switch();

    uid = getuid();
    euid = geteuid();

    if (uid == euid) {
	errno = EPERM;
	rb_sys_fail(0);
    }
    p_uid_exchange(obj, 0);
    if (rb_block_given_p()) {
	under_uid_switch = 1;
	return rb_ensure(rb_yield, Qnil, p_uid_sw_ensure, obj);
    } else {
	return UIDT2NUM(euid);
    }
}
#endif


/* [MG] :FIXME: Is this correct? I'm not sure how to phrase this. */

/*
 *  call-seq:
 *     Process::GID.sid_available?   => true or false
 *
 *  Returns +true+ if the current platform has saved group
 *  ID functionality.
 *
 */

static VALUE
p_gid_have_saved_id(VALUE rcv, SEL sel)
{
#if defined(HAVE_SETRESGID) || defined(HAVE_SETEGID) || defined(_POSIX_SAVED_IDS)
    return Qtrue;
#else
    return Qfalse;
#endif
}

#if defined(HAVE_SETRESGID) || defined(HAVE_SETEGID) || defined(_POSIX_SAVED_IDS)
static VALUE
p_gid_sw_ensure(rb_gid_t id)
{
    under_gid_switch = 0;
    id = rb_setegid_core(id);
    return GIDT2NUM(id);
}


/*
 *  call-seq:
 *     Process::GID.switch              => fixnum
 *     Process::GID.switch {|| block}   => object
 *
 *  Switch the effective and real group IDs of the current process. If
 *  a <em>block</em> is given, the group IDs will be switched back
 *  after the block is executed. Returns the new effective group ID if
 *  called without a block, and the return value of the block if one
 *  is given.
 *
 */

static VALUE
p_gid_switch(VALUE obj, SEL sel)
{
    int gid, egid;

    check_gid_switch();

    gid = getgid();
    egid = getegid();

    if (gid != egid) {
	proc_setegid(obj, 0, GIDT2NUM(gid));
	if (rb_block_given_p()) {
	    under_gid_switch = 1;
	    return rb_ensure(rb_yield, Qnil, p_gid_sw_ensure, SAVED_GROUP_ID);
	} else {
	    return GIDT2NUM(egid);
	}
    } else if (egid != SAVED_GROUP_ID) {
	proc_setegid(obj, 0, GIDT2NUM(SAVED_GROUP_ID));
	if (rb_block_given_p()) {
	    under_gid_switch = 1;
	    return rb_ensure(rb_yield, Qnil, p_gid_sw_ensure, egid);
	} else {
	    return GIDT2NUM(gid);
	}
    } else {
	errno = EPERM;
	rb_sys_fail(0);
    }
}
#else
static VALUE
p_gid_sw_ensure(VALUE obj)
{
    under_gid_switch = 0;
    return p_gid_exchange(obj, 0);
}

static VALUE
p_gid_switch(VALUE obj, SEL sel)
{
    rb_gid_t gid, egid;

    check_gid_switch();

    gid = getgid();
    egid = getegid();

    if (gid == egid) {
	errno = EPERM;
	rb_sys_fail(0);
    }
    p_gid_exchange(obj, 0);
    if (rb_block_given_p()) {
	under_gid_switch = 1;
	return rb_ensure(rb_yield, Qnil, p_gid_sw_ensure, obj);
    } else {
	return GIDT2NUM(egid);
    }
}
#endif


/*
 *  call-seq:
 *     Process.times   => aStructTms
 *
 *  Returns a <code>Tms</code> structure (see <code>Struct::Tms</code>
 *  on page 388) that contains user and system CPU times for this
 *  process.
 *
 *     t = Process.times
 *     [ t.utime, t.stime ]   #=> [0.0, 0.02]
 */

static VALUE
rb_proc_times(VALUE obj, SEL sel)
{
#if defined(HAVE_TIMES) && !defined(__CHECKER__)
    const double hertz =
#ifdef HAVE__SC_CLK_TCK
	(double)sysconf(_SC_CLK_TCK);
#else
#ifndef HZ
# ifdef CLK_TCK
#   define HZ CLK_TCK
# else
#   define HZ 60
# endif
#endif /* HZ */
	HZ;
#endif
    struct tms buf;
    volatile VALUE utime, stime, cutime, sctime;

    times(&buf);
    return rb_struct_new(rb_cProcessTms,
			 utime = DOUBLE2NUM(buf.tms_utime / hertz),
			 stime = DOUBLE2NUM(buf.tms_stime / hertz),
			 cutime = DOUBLE2NUM(buf.tms_cutime / hertz),
			 sctime = DOUBLE2NUM(buf.tms_cstime / hertz));
#else
    rb_notimplement();
#endif
}

VALUE rb_mProcess;
VALUE rb_mProcUID;
VALUE rb_mProcGID;
VALUE rb_mProcID_Syscall;


/*
 *  The <code>Process</code> module is a collection of methods used to
 *  manipulate processes.
 */

void
Init_process(void)
{
    rb_define_virtual_variable("$?", rb_last_status_get, 0);
    rb_define_virtual_variable("$$", get_pid, 0);
    rb_objc_define_module_function(rb_mKernel, "exec", rb_f_exec, -1);
    rb_objc_define_module_function(rb_mKernel, "fork", rb_f_fork, 0);
    rb_objc_define_module_function(rb_mKernel, "exit!", rb_f_exit_bang, -1);
    rb_objc_define_module_function(rb_mKernel, "system", rb_f_system, -1);
    rb_objc_define_module_function(rb_mKernel, "spawn", rb_f_spawn, -1);
    rb_objc_define_module_function(rb_mKernel, "sleep", rb_f_sleep, -1);
    rb_objc_define_module_function(rb_mKernel, "exit", rb_f_exit, -1);
    rb_objc_define_module_function(rb_mKernel, "abort", rb_f_abort, -1);

    rb_mProcess = rb_define_module("Process");

#ifdef WNOHANG
    rb_define_const(rb_mProcess, "WNOHANG", INT2FIX(WNOHANG));
#else
    rb_define_const(rb_mProcess, "WNOHANG", INT2FIX(0));
#endif
#ifdef WUNTRACED
    rb_define_const(rb_mProcess, "WUNTRACED", INT2FIX(WUNTRACED));
#else
    rb_define_const(rb_mProcess, "WUNTRACED", INT2FIX(0));
#endif

    rb_objc_define_method(*(VALUE *)rb_mProcess, "exec", rb_f_exec, -1);
    rb_objc_define_method(*(VALUE *)rb_mProcess, "fork", rb_f_fork, 0);
    rb_objc_define_method(*(VALUE *)rb_mProcess, "spawn", rb_f_spawn, -1);
    rb_objc_define_method(*(VALUE *)rb_mProcess, "exit!", rb_f_exit_bang, -1);
    rb_objc_define_method(*(VALUE *)rb_mProcess, "exit", rb_f_exit, -1);
    rb_objc_define_method(*(VALUE *)rb_mProcess, "abort", rb_f_abort, -1);

    rb_objc_define_method(*(VALUE *)rb_mProcess, "kill", rb_f_kill, -1); /* in signal.c */
    rb_objc_define_method(*(VALUE *)rb_mProcess, "wait", proc_wait, -1);
    rb_objc_define_method(*(VALUE *)rb_mProcess, "wait2", proc_wait2, -1);
    rb_objc_define_method(*(VALUE *)rb_mProcess, "waitpid", proc_wait, -1);
    rb_objc_define_method(*(VALUE *)rb_mProcess, "waitpid2", proc_wait2, -1);
    rb_objc_define_method(*(VALUE *)rb_mProcess, "waitall", proc_waitall, 0);
    rb_objc_define_method(*(VALUE *)rb_mProcess, "detach", proc_detach, 1);

    rb_cProcessStatus = rb_define_class_under(rb_mProcess, "Status", rb_cObject);
    rb_undef_method(CLASS_OF(rb_cProcessStatus), "new");

    rb_objc_define_method(rb_cProcessStatus, "==", pst_equal, 1);
    rb_objc_define_method(rb_cProcessStatus, "&", pst_bitand, 1);
    rb_objc_define_method(rb_cProcessStatus, ">>", pst_rshift, 1);
    rb_objc_define_method(rb_cProcessStatus, "to_i", pst_to_i, 0);
    rb_objc_define_method(rb_cProcessStatus, "to_int", pst_to_i, 0);
    rb_objc_define_method(rb_cProcessStatus, "to_s", pst_to_s, 0);
    rb_objc_define_method(rb_cProcessStatus, "inspect", pst_inspect, 0);

    rb_objc_define_method(rb_cProcessStatus, "pid", pst_pid, 0);

    rb_objc_define_method(rb_cProcessStatus, "stopped?", pst_wifstopped, 0);
    rb_objc_define_method(rb_cProcessStatus, "stopsig", pst_wstopsig, 0);
    rb_objc_define_method(rb_cProcessStatus, "signaled?", pst_wifsignaled, 0);
    rb_objc_define_method(rb_cProcessStatus, "termsig", pst_wtermsig, 0);
    rb_objc_define_method(rb_cProcessStatus, "exited?", pst_wifexited, 0);
    rb_objc_define_method(rb_cProcessStatus, "exitstatus", pst_wexitstatus, 0);
    rb_objc_define_method(rb_cProcessStatus, "success?", pst_success_p, 0);
    rb_objc_define_method(rb_cProcessStatus, "coredump?", pst_wcoredump, 0);

    rb_objc_define_method(*(VALUE *)rb_mProcess, "pid", get_pid, 0);
    rb_objc_define_method(*(VALUE *)rb_mProcess, "ppid", get_ppid, 0);

    rb_objc_define_method(*(VALUE *)rb_mProcess, "getpgrp", proc_getpgrp, 0);
    rb_objc_define_method(*(VALUE *)rb_mProcess, "setpgrp", proc_setpgrp, 0);
    rb_objc_define_method(*(VALUE *)rb_mProcess, "getpgid", proc_getpgid, 1);
    rb_objc_define_method(*(VALUE *)rb_mProcess, "setpgid", proc_setpgid, 2);

    rb_objc_define_method(*(VALUE *)rb_mProcess, "setsid", proc_setsid, 0);

    rb_objc_define_method(*(VALUE *)rb_mProcess, "getpriority", proc_getpriority, 2);
    rb_objc_define_method(*(VALUE *)rb_mProcess, "setpriority", proc_setpriority, 3);

#ifdef HAVE_GETPRIORITY
    rb_define_const(rb_mProcess, "PRIO_PROCESS", INT2FIX(PRIO_PROCESS));
    rb_define_const(rb_mProcess, "PRIO_PGRP", INT2FIX(PRIO_PGRP));
    rb_define_const(rb_mProcess, "PRIO_USER", INT2FIX(PRIO_USER));
#endif

    rb_objc_define_method(*(VALUE *)rb_mProcess, "getrlimit", proc_getrlimit, 1);
    rb_objc_define_method(*(VALUE *)rb_mProcess, "setrlimit", proc_setrlimit, -1);
#ifdef RLIM2NUM
    {
        VALUE inf = RLIM2NUM(RLIM_INFINITY), v;
        rb_define_const(rb_mProcess, "RLIM_INFINITY", inf);
#ifdef RLIM_SAVED_MAX
        v = RLIM_INFINITY == RLIM_SAVED_MAX ? inf : RLIM2NUM(RLIM_SAVED_MAX);
        rb_define_const(rb_mProcess, "RLIM_SAVED_MAX", v);
#endif
#ifdef RLIM_SAVED_CUR
        v = RLIM_INFINITY == RLIM_SAVED_CUR ? inf : RLIM2NUM(RLIM_SAVED_CUR);
        rb_define_const(rb_mProcess, "RLIM_SAVED_CUR", v);
#endif
    }
#ifdef RLIMIT_CORE
    rb_define_const(rb_mProcess, "RLIMIT_CORE", INT2FIX(RLIMIT_CORE));
#endif
#ifdef RLIMIT_CPU
    rb_define_const(rb_mProcess, "RLIMIT_CPU", INT2FIX(RLIMIT_CPU));
#endif
#ifdef RLIMIT_DATA
    rb_define_const(rb_mProcess, "RLIMIT_DATA", INT2FIX(RLIMIT_DATA));
#endif
#ifdef RLIMIT_FSIZE
    rb_define_const(rb_mProcess, "RLIMIT_FSIZE", INT2FIX(RLIMIT_FSIZE));
#endif
#ifdef RLIMIT_NOFILE
    rb_define_const(rb_mProcess, "RLIMIT_NOFILE", INT2FIX(RLIMIT_NOFILE));
#endif
#ifdef RLIMIT_STACK
    rb_define_const(rb_mProcess, "RLIMIT_STACK", INT2FIX(RLIMIT_STACK));
#endif
#ifdef RLIMIT_AS
    rb_define_const(rb_mProcess, "RLIMIT_AS", INT2FIX(RLIMIT_AS));
#endif
#ifdef RLIMIT_MEMLOCK
    rb_define_const(rb_mProcess, "RLIMIT_MEMLOCK", INT2FIX(RLIMIT_MEMLOCK));
#endif
#ifdef RLIMIT_NPROC
    rb_define_const(rb_mProcess, "RLIMIT_NPROC", INT2FIX(RLIMIT_NPROC));
#endif
#ifdef RLIMIT_RSS
    rb_define_const(rb_mProcess, "RLIMIT_RSS", INT2FIX(RLIMIT_RSS));
#endif
#ifdef RLIMIT_SBSIZE
    rb_define_const(rb_mProcess, "RLIMIT_SBSIZE", INT2FIX(RLIMIT_SBSIZE));
#endif
#endif

    rb_objc_define_method(*(VALUE *)rb_mProcess, "uid", proc_getuid, 0);
    rb_objc_define_method(*(VALUE *)rb_mProcess, "uid=", proc_setuid, 1);
    rb_objc_define_method(*(VALUE *)rb_mProcess, "gid", proc_getgid, 0);
    rb_objc_define_method(*(VALUE *)rb_mProcess, "gid=", proc_setgid, 1);
    rb_objc_define_method(*(VALUE *)rb_mProcess, "euid", proc_geteuid, 0);
    rb_objc_define_method(*(VALUE *)rb_mProcess, "euid=", proc_seteuid, 1);
    rb_objc_define_method(*(VALUE *)rb_mProcess, "egid", proc_getegid, 0);
    rb_objc_define_method(*(VALUE *)rb_mProcess, "egid=", proc_setegid, 1);
    rb_objc_define_method(*(VALUE *)rb_mProcess, "initgroups", proc_initgroups, 2);
    rb_objc_define_method(*(VALUE *)rb_mProcess, "groups", proc_getgroups, 0);
    rb_objc_define_method(*(VALUE *)rb_mProcess, "groups=", proc_setgroups, 1);
    rb_objc_define_method(*(VALUE *)rb_mProcess, "maxgroups", proc_getmaxgroups, 0);
    rb_objc_define_method(*(VALUE *)rb_mProcess, "maxgroups=", proc_setmaxgroups, 1);

    rb_objc_define_method(*(VALUE *)rb_mProcess, "daemon", proc_daemon, -1);

    rb_objc_define_method(*(VALUE *)rb_mProcess, "times", rb_proc_times, 0);

#if defined(HAVE_TIMES) || defined(_WIN32)
    rb_cProcessTms = rb_struct_define("Tms", "utime", "stime", "cutime", "cstime", NULL);
#endif

    SAVED_USER_ID = geteuid();
    SAVED_GROUP_ID = getegid();

    rb_mProcUID = rb_define_module_under(rb_mProcess, "UID");
    rb_mProcGID = rb_define_module_under(rb_mProcess, "GID");

    rb_objc_define_method(*(VALUE *)rb_mProcUID, "rid", proc_getuid, 0);
    rb_objc_define_method(*(VALUE *)rb_mProcGID, "rid", proc_getgid, 0);
    rb_objc_define_method(*(VALUE *)rb_mProcUID, "eid", proc_geteuid, 0);
    rb_objc_define_method(*(VALUE *)rb_mProcGID, "eid", proc_getegid, 0);
    rb_objc_define_method(*(VALUE *)rb_mProcUID, "change_privilege", p_uid_change_privilege, 1);
    rb_objc_define_method(*(VALUE *)rb_mProcGID, "change_privilege", p_gid_change_privilege, 1);
    rb_objc_define_method(*(VALUE *)rb_mProcUID, "grant_privilege", p_uid_grant_privilege, 1);
    rb_objc_define_method(*(VALUE *)rb_mProcGID, "grant_privilege", p_gid_grant_privilege, 1);
    rb_define_alias(*(VALUE *)rb_mProcUID, "eid=", "grant_privilege");
    rb_define_alias(*(VALUE *)rb_mProcGID, "eid=", "grant_privilege");
    rb_objc_define_method(*(VALUE *)rb_mProcUID, "re_exchange", p_uid_exchange, 0);
    rb_objc_define_method(*(VALUE *)rb_mProcGID, "re_exchange", p_gid_exchange, 0);
    rb_objc_define_method(*(VALUE *)rb_mProcUID, "re_exchangeable?", p_uid_exchangeable, 0);
    rb_objc_define_method(*(VALUE *)rb_mProcGID, "re_exchangeable?", p_gid_exchangeable, 0);
    rb_objc_define_method(*(VALUE *)rb_mProcUID, "sid_available?", p_uid_have_saved_id, 0);
    rb_objc_define_method(*(VALUE *)rb_mProcGID, "sid_available?", p_gid_have_saved_id, 0);
    rb_objc_define_method(*(VALUE *)rb_mProcUID, "switch", p_uid_switch, 0);
    rb_objc_define_method(*(VALUE *)rb_mProcGID, "switch", p_gid_switch, 0);

    rb_mProcID_Syscall = rb_define_module_under(rb_mProcess, "Sys");

    rb_objc_define_method(*(VALUE *)rb_mProcID_Syscall, "getuid", proc_getuid, 0);
    rb_objc_define_method(*(VALUE *)rb_mProcID_Syscall, "geteuid", proc_geteuid, 0);
    rb_objc_define_method(*(VALUE *)rb_mProcID_Syscall, "getgid", proc_getgid, 0);
    rb_objc_define_method(*(VALUE *)rb_mProcID_Syscall, "getegid", proc_getegid, 0);

    rb_objc_define_method(*(VALUE *)rb_mProcID_Syscall, "setuid", p_sys_setuid, 1);
    rb_objc_define_method(*(VALUE *)rb_mProcID_Syscall, "setgid", p_sys_setgid, 1);

    rb_objc_define_method(*(VALUE *)rb_mProcID_Syscall, "setruid", p_sys_setruid, 1);
    rb_objc_define_method(*(VALUE *)rb_mProcID_Syscall, "setrgid", p_sys_setrgid, 1);

    rb_objc_define_method(*(VALUE *)rb_mProcID_Syscall, "seteuid", p_sys_seteuid, 1);
    rb_objc_define_method(*(VALUE *)rb_mProcID_Syscall, "setegid", p_sys_setegid, 1);

    rb_objc_define_method(*(VALUE *)rb_mProcID_Syscall, "setreuid", p_sys_setreuid, 2);
    rb_objc_define_method(*(VALUE *)rb_mProcID_Syscall, "setregid", p_sys_setregid, 2);

    rb_objc_define_method(*(VALUE *)rb_mProcID_Syscall, "setresuid", p_sys_setresuid, 3);
    rb_objc_define_method(*(VALUE *)rb_mProcID_Syscall, "setresgid", p_sys_setresgid, 3);
    rb_objc_define_method(*(VALUE *)rb_mProcID_Syscall, "issetugid", p_sys_issetugid, 0);
}
