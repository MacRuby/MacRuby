/*
 * MacRuby implementation of Thread.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2009-2011, Apple Inc. All rights reserved.
 * Copyright (C) 2004-2007 Koichi Sasada
 */

#include "macruby_internal.h"
#include "ruby/node.h"
#include "vm.h"
#include "objc.h"

VALUE rb_cThread;
VALUE rb_cThGroup;
VALUE rb_cMutex;

typedef struct rb_vm_mutex {
    pthread_mutex_t mutex;
    rb_vm_thread_t *thread;
} rb_vm_mutex_t;

#define GetMutexPtr(obj) ((rb_vm_mutex_t *)DATA_PTR(obj))

typedef struct {
    bool enclosed;
    VALUE threads;
    VALUE mutex;
} rb_thread_group_t;

#define GetThreadGroupPtr(obj) ((rb_thread_group_t *)DATA_PTR(obj))

#if 0
static VALUE
thread_s_new(int argc, VALUE *argv, VALUE klass)
{
    // TODO
    return Qnil;
}
#endif

static VALUE
thread_s_alloc(VALUE self, SEL sel)
{
    rb_vm_thread_t *t = (rb_vm_thread_t *)xmalloc(sizeof(rb_vm_thread_t));
    t->thread = 0;
    t->exception = Qnil;
    return Data_Wrap_Struct(self, NULL, NULL, t);
}

static IMP
thread_finalize_imp_super = NULL;

static void
thread_finalize_imp(void *rcv, SEL sel)
{
    rb_vm_thread_t *t = GetThreadPtr(rcv);
    if (t->exception != Qnil && !t->joined_on_exception) {
	fprintf(stderr, "*** Thread %p exited prematurely because of an uncaught exception:\n%s\n",
		t->thread,
		rb_str_cstr(rb_format_exception_message(t->exception)));
    }
    if (thread_finalize_imp_super != NULL) {
        ((void(*)(void *, SEL))thread_finalize_imp_super)(rcv, sel);
    }
}

static VALUE thgroup_add_m(VALUE group, VALUE thread, bool check_enclose);

static VALUE
thread_initialize(VALUE thread, SEL sel, int argc, const VALUE *argv)
{
    if (!rb_block_given_p()) {
	rb_raise(rb_eThreadError, "must be called with a block");
    }
    rb_vm_block_t *b = rb_vm_current_block();
    assert(b != NULL);

    rb_vm_thread_t *t = GetThreadPtr(thread);
    if (t->thread != 0) {
	rb_raise(rb_eThreadError, "already initialized thread");
    }
    rb_vm_thread_pre_init(t, b, argc, argv, rb_vm_create_vm());

    // The thread's group is always the parent's one.
    // The parent group might be nil (ex. if created from GCD).
    VALUE group = GetThreadPtr(rb_vm_current_thread())->group;
    if (group != Qnil) {
	thgroup_add_m(group, thread, false);
    }

    // Retain the Thread object to avoid a potential GC, the corresponding
    // release is done in rb_vm_thread_run().
    GC_RETAIN(thread);

    // Prepare attributes for the thread.
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setinheritsched(&attr, PTHREAD_INHERIT_SCHED);
    pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    // Register the thread to the core. We are doing this before actually
    // running it because the current thread might perform a method poking at
    // the current registered threads (such as Kernel#sleep) right after that.
    rb_vm_register_thread(thread);

    // Launch it.
    if (pthread_create(&t->thread, &attr, (void *(*)(void *))rb_vm_thread_run,
		(void *)thread) != 0) {
	rb_sys_fail("pthread_create() failed");
    }
    pthread_attr_destroy(&attr);

    return thread;
}

/*
 *  call-seq:
 *     Thread.start([args]*) {|args| block }   => thread
 *     Thread.fork([args]*) {|args| block }    => thread
 *
 *  Basically the same as <code>Thread::new</code>. However, if class
 *  <code>Thread</code> is subclassed, then calling <code>start</code> in that
 *  subclass will not invoke the subclass's <code>initialize</code> method.
 */

static VALUE
thread_start(VALUE klass, SEL sel, int argc, VALUE *argv)
{
    VALUE th = thread_s_alloc(klass, 0);
    return thread_initialize(th, 0, argc, argv);
}

VALUE
rb_thread_create(VALUE (*fn)(ANYARGS), void *arg)
{
    // TODO
    return Qnil;
}

/*
 *  call-seq:
 *     thr.join          => thr
 *     thr.join(limit)   => thr
 *
 *  The calling thread will suspend execution and run <i>thr</i>. Does not
 *  return until <i>thr</i> exits or until <i>limit</i> seconds have passed. If
 *  the time limit expires, <code>nil</code> will be returned, otherwise
 *  <i>thr</i> is returned.
 *
 *  Any threads not joined will be killed when the main program exits.  If
 *  <i>thr</i> had previously raised an exception and the
 *  <code>abort_on_exception</code> and <code>$DEBUG</code> flags are not set
 *  (so the exception has not yet been processed) it will be processed at this
 *  time.
 *
 *     a = Thread.new { print "a"; sleep(10); print "b"; print "c" }
 *     x = Thread.new { print "x"; Thread.pass; print "y"; print "z" }
 *     x.join # Let x thread finish, a will be killed on exit.
 *
 *  <em>produces:</em>
 *
 *     axyz
 *
 *  The following example illustrates the <i>limit</i> parameter.
 *
 *     y = Thread.new { 4.times { sleep 0.1; puts 'tick... ' }}
 *     puts "Waiting" until y.join(0.15)
 *
 *  <em>produces:</em>
 *
 *     tick...
 *     Waiting
 *     tick...
 *     Waitingtick...
 *
 *
 *     tick...
 */

static VALUE
thread_join_m(VALUE self, SEL sel, int argc, VALUE *argv)
{
    VALUE timeout;

    rb_scan_args(argc, argv, "01", &timeout);

    rb_vm_thread_t *t = GetThreadPtr(self);
    if (t->status != THREAD_DEAD) {
	if (timeout == Qnil) {
	    // No timeout given: block until the thread finishes.
	    //pthread_assert(pthread_join(t->thread, NULL));
	    struct timespec ts;
	    ts.tv_sec = 0;
	    ts.tv_nsec = 10000000;
	    while (t->status != THREAD_DEAD) {
		nanosleep(&ts, NULL);
		pthread_yield_np();
		if (t->status == THREAD_KILLED && t->wait_for_mutex_lock) {
		    goto dead;
		}
	    }
	}
	else {
	    // Timeout given: sleep and check if the thread is dead.
	    struct timeval tv = rb_time_interval(timeout);
	    struct timespec ts;
	    ts.tv_sec = tv.tv_sec;
	    ts.tv_nsec = tv.tv_usec * 1000;
	    while (ts.tv_nsec >= 1000000000) {
		ts.tv_sec += 1;
		ts.tv_nsec -= 1000000000;
	    }

	    while (ts.tv_sec > 0 || ts.tv_nsec > 0) {
		struct timespec its;
again:
		if (ts.tv_nsec > 100000000) {
		    ts.tv_nsec -= 100000000;
		    its.tv_sec = 0;
		    its.tv_nsec = 100000000;
		}
		else if (ts.tv_sec > 0) {
		    ts.tv_sec -= 1;
		    ts.tv_nsec += 1000000000;
		    goto again;
		}
		else {
		    its = ts;
		    ts.tv_sec = ts.tv_nsec = 0;
		}
		nanosleep(&its, NULL);
		if (t->status == THREAD_DEAD) {
		    goto dead;
		}
		if (t->status == THREAD_KILLED && t->wait_for_mutex_lock) {
		    goto dead;
		}
	    }
	    return Qnil;
	}
    }

dead:
    // If the thread was terminated because of an exception, we need to
    // propagate it.
    if (t->exception != Qnil) {
	t->joined_on_exception = true;
	rb_exc_raise(t->exception);
    }
    return self;
}

/*
 *  call-seq:
 *     thr.value   => obj
 *
 *  Waits for <i>thr</i> to complete (via <code>Thread#join</code>) and returns
 *  its value.
 *
 *     a = Thread.new { 2 + 2 }
 *     a.value   #=> 4
 */

static VALUE
thread_value(VALUE self, SEL sel)
{
    thread_join_m(self, 0, 0, NULL);
    return GetThreadPtr(self)->value;
}

void
rb_thread_polling(void)
{
    // TODO
}

void
rb_thread_schedule(void)
{
    pthread_yield_np();
}

int rb_thread_critical; /* TODO: dummy variable */

/*
 *  call-seq:
 *     Thread.pass   => nil
 *
 *  Invokes the thread scheduler to pass execution to another thread.
 *
 *     a = Thread.new { print "a"; Thread.pass;
 *                      print "b"; Thread.pass;
 *                      print "c" }
 *     b = Thread.new { print "x"; Thread.pass;
 *                      print "y"; Thread.pass;
 *                      print "z" }
 *     a.join
 *     b.join
 *
 *  <em>produces:</em>
 *
 *     axbycz
 */

static VALUE
thread_s_pass(VALUE klass, SEL sel)
{
    rb_thread_schedule();
    return Qnil;
}

/*
 *  call-seq:
 *     thr.raise(exception)
 *
 *  Raises an exception (see <code>Kernel::raise</code>) from <i>thr</i>. The
 *  caller does not have to be <i>thr</i>.
 *
 *     Thread.abort_on_exception = true
 *     a = Thread.new { sleep(200) }
 *     a.raise("Gotcha")
 *
 *  <em>produces:</em>
 *
 *     prog.rb:3: Gotcha (RuntimeError)
 *     	from prog.rb:2:in `initialize'
 *     	from prog.rb:2:in `new'
 *     	from prog.rb:2
 */

static VALUE
thread_raise_m(VALUE self, SEL sel, int argc, VALUE *argv)
{
    VALUE exc = rb_make_exception(argc, argv);

    rb_vm_thread_t *t = GetThreadPtr(self);

    if (t->thread == pthread_self()) {
	rb_exc_raise(exc);
    }
    else if (t->status != THREAD_DEAD) {
	rb_vm_thread_raise(t, exc);
    }

    return Qnil;
}

/*
 *  call-seq:
 *     thr.exit        => thr
 *     thr.kill        => thr
 *     thr.terminate   => thr
 *
 *  Terminates <i>thr</i> and schedules another thread to be run. If this thread
 *  is already marked to be killed, <code>exit</code> returns the
 *  <code>Thread</code>. If this is the main thread, or the last thread, exits
 *  the process.
 */

static VALUE
rb_thread_kill(VALUE thread, SEL sel)
{
    rb_vm_thread_t *t = GetThreadPtr(thread);
    rb_vm_thread_t *t_main = GetThreadPtr(rb_vm_main_thread()); 
    if (t->thread == t_main->thread) { 
	rb_exit(EXIT_SUCCESS); 
    } 
    if (t->status != THREAD_KILLED) {
	rb_vm_thread_cancel(t);
    }
    return thread;
}

/*
 *  call-seq:
 *     Thread.kill(thread)   => thread
 *
 *  Causes the given <em>thread</em> to exit (see <code>Thread::exit</code>).
 *
 *     count = 0
 *     a = Thread.new { loop { count += 1 } }
 *     sleep(0.1)       #=> 0
 *     Thread.kill(a)   #=> #<Thread:0x401b3d30 dead>
 *     count            #=> 93947
 *     a.alive?         #=> false
 */

static VALUE
rb_thread_s_kill(VALUE obj, SEL sel, VALUE th)
{
    if (!rb_obj_is_kind_of(th, rb_cThread)) {
	rb_raise(rb_eTypeError, "wrong argument type %s (expected Thread)",
		rb_obj_classname(th));
    }
    return rb_thread_kill(th, 0);
}

/*
 *  call-seq:
 *     Thread.exit   => thread
 *
 *  Terminates the currently running thread and schedules another thread to be
 *  run. If this thread is already marked to be killed, <code>exit</code>
 *  returns the <code>Thread</code>. If this is the main thread, or the last
 *  thread, exit the process.
 */

static VALUE
rb_thread_exit(void)
{
    return rb_thread_kill(rb_vm_current_thread(), 0);
}

/*
 *  call-seq:
 *     thr.wakeup   => thr
 *
 *  Marks <i>thr</i> as eligible for scheduling (it may still remain blocked on
 *  I/O, however). Does not invoke the scheduler (see <code>Thread#run</code>).
 *
 *     c = Thread.new { Thread.stop; puts "hey!" }
 *     c.wakeup
 *
 *  <em>produces:</em>
 *
 *     hey!
 */

static VALUE
rb_thread_wakeup(VALUE thread, SEL sel)
{
    rb_vm_thread_wakeup(GetThreadPtr(thread));
    return thread;
}

/*
 *  call-seq:
 *     thr.run   => thr
 *
 *  Wakes up <i>thr</i>, making it eligible for scheduling.
 *
 *     a = Thread.new { puts "a"; Thread.stop; puts "c" }
 *     Thread.pass
 *     puts "Got here"
 *     a.run
 *     a.join
 *
 *  <em>produces:</em>
 *
 *     a
 *     Got here
 *     c
 */

static VALUE
rb_thread_run(VALUE thread, SEL sel)
{
    rb_vm_thread_wakeup(GetThreadPtr(thread));
    pthread_yield_np();
    return thread;
}

/*
 *  call-seq:
 *     Thread.stop   => nil
 *
 *  Stops execution of the current thread, putting it into a ``sleep'' state,
 *  and schedules execution of another thread.
 *
 *     a = Thread.new { print "a"; Thread.stop; print "c" }
 *     Thread.pass
 *     print "b"
 *     a.run
 *     a.join
 *
 *  <em>produces:</em>
 *
 *     abc
 */

static VALUE
rb_thread_stop(VALUE rcv, SEL sel)
{
    rb_thread_sleep_forever();
    return Qnil;
}

/*
 *  call-seq:
 *     Thread.list   => array
 *
 *  Returns an array of <code>Thread</code> objects for all threads that are
 *  either runnable or stopped.
 *
 *     Thread.new { sleep(200) }
 *     Thread.new { 1000000.times {|i| i*i } }
 *     Thread.new { Thread.stop }
 *     Thread.list.each {|t| p t}
 *
 *  <em>produces:</em>
 *
 *     #<Thread:0x401b3e84 sleep>
 *     #<Thread:0x401b3f38 run>
 *     #<Thread:0x401b3fb0 sleep>
 *     #<Thread:0x401bdf4c run>
 */

VALUE
rb_thread_list(VALUE rcv, SEL sel)
{
    return rb_ary_dup(rb_vm_threads());
}

/*
 *  call-seq:
 *     Thread.current   => thread
 *
 *  Returns the currently executing thread.
 *
 *     Thread.current   #=> #<Thread:0x401bdf4c run>
 */

static VALUE
thread_s_current(VALUE klass, SEL sel)
{
    return rb_vm_current_thread();
}

static VALUE
rb_thread_s_main(VALUE klass, SEL sel)
{
    return rb_vm_main_thread();
}

/*
 *  call-seq:
 *     Thread.abort_on_exception   => true or false
 *
 *  Returns the status of the global ``abort on exception'' condition.  The
 *  default is <code>false</code>. When set to <code>true</code>, or if the
 *  global <code>$DEBUG</code> flag is <code>true</code> (perhaps because the
 *  command line option <code>-d</code> was specified) all threads will abort
 *  (the process will <code>exit(0)</code>) if an exception is raised in any
 *  thread. See also <code>Thread::abort_on_exception=</code>.
 */

static VALUE
rb_thread_s_abort_exc(VALUE rcv, SEL sel)
{
    return rb_vm_abort_on_exception() ? Qtrue : Qfalse;
}

/*
 *  call-seq:
 *     Thread.abort_on_exception= boolean   => true or false
 *
 *  When set to <code>true</code>, all threads will abort if an exception is
 *  raised. Returns the new state.
 *
 *     Thread.abort_on_exception = true
 *     t1 = Thread.new do
 *       puts  "In new thread"
 *       raise "Exception from thread"
 *     end
 *     sleep(1)
 *     puts "not reached"
 *
 *  <em>produces:</em>
 *
 *     In new thread
 *     prog.rb:4: Exception from thread (RuntimeError)
 *     	from prog.rb:2:in `initialize'
 *     	from prog.rb:2:in `new'
 *     	from prog.rb:2
 */

static VALUE
rb_thread_s_abort_exc_set(VALUE self, SEL sel, VALUE val)
{
    rb_secure(4);
    rb_vm_set_abort_on_exception(RTEST(val));
    return val;
}

/*
 *  call-seq:
 *     thr.abort_on_exception   => true or false
 *
 *  Returns the status of the thread-local ``abort on exception'' condition for
 *  <i>thr</i>. The default is <code>false</code>. See also
 *  <code>Thread::abort_on_exception=</code>.
 */

static VALUE
rb_thread_abort_exc(VALUE thread, SEL sel)
{
    return GetThreadPtr(thread)->abort_on_exception ? Qtrue : Qfalse;
}

/*
 *  call-seq:
 *     thr.abort_on_exception= boolean   => true or false
 *
 *  When set to <code>true</code>, causes all threads (including the main
 *  program) to abort if an exception is raised in <i>thr</i>. The process will
 *  effectively <code>exit(0)</code>.
 */

static VALUE
rb_thread_abort_exc_set(VALUE thread, SEL sel, VALUE val)
{
    rb_secure(4);
    GetThreadPtr(thread)->abort_on_exception = RTEST(val);
    return val;
}

/*
 *  call-seq:
 *     thr.group   => thgrp or nil
 *
 *  Returns the <code>ThreadGroup</code> which contains <i>thr</i>, or nil if
 *  the thread is not a member of any group.
 *
 *     Thread.main.group   #=> #<ThreadGroup:0x4029d914>
 */

static VALUE
rb_thread_group(VALUE thread, SEL sel)
{
    return GetThreadPtr(thread)->group;
}

/*
 *  call-seq:
 *     thr.status   => string, false or nil
 *
 *  Returns the status of <i>thr</i>: ``<code>sleep</code>'' if <i>thr</i> is
 *  sleeping or waiting on I/O, ``<code>run</code>'' if <i>thr</i> is executing,
 *  ``<code>aborting</code>'' if <i>thr</i> is aborting, <code>false</code> if
 *  <i>thr</i> terminated normally, and <code>nil</code> if <i>thr</i>
 *  terminated with an exception.
 *
 *     a = Thread.new { raise("die now") }
 *     b = Thread.new { Thread.stop }
 *     c = Thread.new { Thread.exit }
 *     d = Thread.new { sleep }
 *     d.kill                  #=> #<Thread:0x401b3678 aborting>
 *     a.status                #=> nil
 *     b.status                #=> "sleep"
 *     c.status                #=> false
 *     d.status                #=> "aborting"
 *     Thread.current.status   #=> "run"
 */

static const char *
rb_thread_status_cstr(VALUE thread)
{
    rb_vm_thread_t *t = GetThreadPtr(thread);
    switch (t->status) {
	case THREAD_ALIVE:
	    return "run";

	case THREAD_SLEEP:
	    return "sleep";

	case THREAD_KILLED:
	    return "aborting";

	case THREAD_DEAD:
	    return "dead";
    }
    return "unknown";
}

static VALUE
rb_thread_status(VALUE thread, SEL sel)
{
    rb_vm_thread_t *t = GetThreadPtr(thread);
    if (t->status == THREAD_DEAD) {
	return t->exception == Qnil ? Qfalse : Qnil;
    }
    return rb_str_new2(rb_thread_status_cstr(thread));
}

/*
 *  call-seq:
 *     thr.alive?   => true or false
 *
 *  Returns <code>true</code> if <i>thr</i> is running or sleeping.
 *
 *     thr = Thread.new { }
 *     thr.join                #=> #<Thread:0x401b3fb0 dead>
 *     Thread.current.alive?   #=> true
 *     thr.alive?              #=> false
 */

static VALUE
rb_thread_alive_p(VALUE thread, SEL sel)
{
    rb_vm_thread_status_t s = GetThreadPtr(thread)->status;
    return s != THREAD_DEAD ? Qtrue : Qfalse;
}

/*
 *  call-seq:
 *     thr.stop?   => true or false
 *
 *  Returns <code>true</code> if <i>thr</i> is dead or sleeping.
 *
 *     a = Thread.new { Thread.stop }
 *     b = Thread.current
 *     a.stop?   #=> true
 *     b.stop?   #=> false
 */

static VALUE
rb_thread_stop_p(VALUE thread)
{
    rb_vm_thread_status_t s = GetThreadPtr(thread)->status;
    return s == THREAD_DEAD || s == THREAD_SLEEP ? Qtrue : Qfalse;
}

/*
 *  call-seq:
 *     thr.safe_level   => integer
 *
 *  Returns the safe level in effect for <i>thr</i>. Setting thread-local safe
 *  levels can help when implementing sandboxes which run insecure code.
 *
 *     thr = Thread.new { $SAFE = 3; sleep }
 *     Thread.current.safe_level   #=> 0
 *     thr.safe_level              #=> 3
 */

static VALUE
rb_thread_safe_level(VALUE thread, SEL sel)
{
    return INT2FIX(rb_vm_thread_safe_level(GetThreadPtr(thread)));
}

/*
 * call-seq:
 *   thr.inspect   => string
 *
 * Dump the name, id, and status of _thr_ to a string.
 */

static VALUE
rb_thread_inspect(VALUE thread, SEL sel)
{
    const char *status = rb_thread_status_cstr(thread);

    char buf[100];
    snprintf(buf, sizeof buf, "#<%s:%p %s>", rb_obj_classname(thread),
	    (void *)thread, status);

    VALUE str = rb_str_new2(buf);
    OBJ_INFECT(str, thread);

    return str;
}

/*
 *  call-seq:
 *      thr[sym]   => obj or nil
 *
 *  Attribute Reference---Returns the value of a thread-local variable, using
 *  either a symbol or a string name. If the specified variable does not exist,
 *  returns <code>nil</code>.
 *
 *     a = Thread.new { Thread.current["name"] = "A"; Thread.stop }
 *     b = Thread.new { Thread.current[:name]  = "B"; Thread.stop }
 *     c = Thread.new { Thread.current["name"] = "C"; Thread.stop }
 *     Thread.list.each {|x| puts "#{x.inspect}: #{x[:name]}" }
 *
 *  <em>produces:</em>
 *
 *     #<Thread:0x401b3b3c sleep>: C
 *     #<Thread:0x401b3bc8 sleep>: B
 *     #<Thread:0x401b3c68 sleep>: A
 *     #<Thread:0x401bdf4c run>:
 */

static VALUE
rb_thread_aref(VALUE thread, SEL sel, VALUE key)
{
    key = ID2SYM(rb_to_id(key));
    VALUE h = rb_vm_thread_locals(thread, false);
    if (h != Qnil) {
	return rb_hash_aref(h, key);
    }
    return Qnil;
}

VALUE
rb_thread_local_aref(VALUE self, ID key)
{
    return rb_thread_aref(self, 0, ID2SYM(key));
}

/*
 *  call-seq:
 *      thr[sym] = obj   => obj
 *
 *  Attribute Assignment---Sets or creates the value of a thread-local variable,
 *  using either a symbol or a string. See also <code>Thread#[]</code>.
 */

static VALUE
rb_thread_aset(VALUE self, SEL sel, VALUE key, VALUE val)
{
    key = ID2SYM(rb_to_id(key));
    return rb_hash_aset(rb_vm_thread_locals(self, true), key, val);
}

VALUE
rb_thread_local_aset(VALUE self, ID key, VALUE val)
{
    return rb_thread_aset(self, 0, ID2SYM(key), val);
}

/*
 *  call-seq:
 *     thr.key?(sym)   => true or false
 *
 *  Returns <code>true</code> if the given string (or symbol) exists as a
 *  thread-local variable.
 *
 *     me = Thread.current
 *     me[:oliver] = "a"
 *     me.key?(:oliver)    #=> true
 *     me.key?(:stanley)   #=> false
 */

static VALUE
rb_thread_key_p(VALUE self, SEL sel, VALUE key)
{
    key = ID2SYM(rb_to_id(key));
    VALUE h = rb_vm_thread_locals(self, false);
    if (h == Qnil) {
	return Qfalse;
    }
    return rb_hash_has_key(h, key);
}

int
rb_thread_alone()
{
    return RARRAY_LEN(rb_vm_threads()) <= 1;
}

/*
 *  call-seq:
 *     thr.keys   => array
 *
 *  Returns an an array of the names of the thread-local variables (as Symbols).
 *
 *     thr = Thread.new do
 *       Thread.current[:cat] = 'meow'
 *       Thread.current["dog"] = 'woof'
 *     end
 *     thr.join   #=> #<Thread:0x401b3f10 dead>
 *     thr.keys   #=> [:dog, :cat]
 */

static VALUE
rb_thread_keys(VALUE self, SEL sel)
{
    VALUE h = rb_vm_thread_locals(self, false);
    return h == Qnil ? rb_ary_new() : rb_hash_keys(h);
}

/*
 *  call-seq:
 *     thr.priority   => integer
 *
 *  Returns the priority of <i>thr</i>. Default is inherited from the
 *  current thread which creating the new thread, or zero for the
 *  initial main thread; higher-priority threads will run before
 *  lower-priority threads.
 *
 *     Thread.current.priority   #=> 0
 */

static VALUE
rb_thread_priority(VALUE thread, SEL sel)
{
    // FIXME this doesn't really minic what 1.9 does, but do we care?
    struct sched_param param;
    pthread_assert(pthread_getschedparam(GetThreadPtr(thread)->thread,
		NULL, &param));
    return INT2FIX(param.sched_priority);
}

/*
 *  call-seq:
 *     thr.priority= integer   => thr
 *
 *  Sets the priority of <i>thr</i> to <i>integer</i>. Higher-priority threads
 *  will run before lower-priority threads.
 *
 *     count1 = count2 = 0
 *     a = Thread.new do
 *           loop { count1 += 1 }
 *         end
 *     a.priority = -1
 *
 *     b = Thread.new do
 *           loop { count2 += 1 }
 *         end
 *     b.priority = -2
 *     sleep 1   #=> 1
 *     count1    #=> 622504
 *     count2    #=> 5832
 */

static VALUE
rb_thread_priority_set(VALUE thread, SEL sel, VALUE prio)
{
    // FIXME this doesn't really minic what 1.9 does, but do we care?
    int policy;
    struct sched_param param;
    rb_secure(4);
    pthread_assert(pthread_getschedparam(GetThreadPtr(thread)->thread,
		&policy, &param));

    const int max = sched_get_priority_max(policy);
    const int min = sched_get_priority_min(policy);

    int priority = FIX2INT(prio);
    if (min > priority) {
	priority = min;
    }
    else if (max > priority) {
	priority = max;
    }

    param.sched_priority = priority;
    pthread_assert(pthread_setschedparam(GetThreadPtr(thread)->thread,
		policy, &param));

    return Qnil;
}

/* for IO */
// TODO these should be moved into io.c

#if defined(NFDBITS) && defined(HAVE_RB_FD_INIT)
void
rb_fd_init(volatile rb_fdset_t *fds)
{
    fds->maxfd = 0;
    GC_WB(&fds->fdset, ALLOC(fd_set));
    FD_ZERO(fds->fdset);
}

void
rb_fd_term(rb_fdset_t *fds)
{
    if (fds->fdset != NULL) {
	xfree(fds->fdset);
	fds->fdset = NULL;
    }
    fds->maxfd = 0;
    fds->fdset = 0;
}

void
rb_fd_zero(rb_fdset_t *fds)
{
    if (fds->fdset) {
	MEMZERO(fds->fdset, fd_mask, howmany(fds->maxfd, NFDBITS));
	FD_ZERO(fds->fdset);
    }
}

static void
rb_fd_resize(int n, rb_fdset_t *fds)
{
    int m = howmany(n + 1, NFDBITS) * sizeof(fd_mask);
    int o = howmany(fds->maxfd, NFDBITS) * sizeof(fd_mask);

    if (m < sizeof(fd_set)) m = sizeof(fd_set);
    if (o < sizeof(fd_set)) o = sizeof(fd_set);

    if (m > o) {
	GC_WB(&fds->fdset, xrealloc(fds->fdset, m));
	memset((char *)fds->fdset + o, 0, m - o);
    }
    if (n >= fds->maxfd) fds->maxfd = n + 1;
}

void
rb_fd_set(int n, rb_fdset_t *fds)
{
    rb_fd_resize(n, fds);
    FD_SET(n, fds->fdset);
}

void
rb_fd_clr(int n, rb_fdset_t *fds)
{
    if (n >= fds->maxfd) return;
    FD_CLR(n, fds->fdset);
}

int
rb_fd_isset(int n, const rb_fdset_t *fds)
{
    if (n >= fds->maxfd) return 0;
    return FD_ISSET(n, fds->fdset) != 0; /* "!= 0" avoids FreeBSD PR 91421 */
}

void
rb_fd_copy(rb_fdset_t *dst, const fd_set *src, int max)
{
    int size = howmany(max, NFDBITS) * sizeof(fd_mask);

    if (size < sizeof(fd_set)) size = sizeof(fd_set);
    dst->maxfd = max;
    GC_WB(&dst->fdset, xrealloc(dst->fdset, size));
    memcpy(dst->fdset, src, size);
}

int
rb_fd_select(int n, rb_fdset_t *readfds, rb_fdset_t *writefds, rb_fdset_t *exceptfds, struct timeval *timeout)
{
    fd_set *r = NULL, *w = NULL, *e = NULL;
    if (readfds) {
        rb_fd_resize(n - 1, readfds);
        r = rb_fd_ptr(readfds);
    }
    if (writefds) {
        rb_fd_resize(n - 1, writefds);
        w = rb_fd_ptr(writefds);
    }
    if (exceptfds) {
        rb_fd_resize(n - 1, exceptfds);
        e = rb_fd_ptr(exceptfds);
    }
    return select(n, r, w, e, timeout);
}

#undef FD_ZERO
#undef FD_SET
#undef FD_CLR
#undef FD_ISSET

#define FD_ZERO(f)	rb_fd_zero(f)
#define FD_SET(i, f)	rb_fd_set(i, f)
#define FD_CLR(i, f)	rb_fd_clr(i, f)
#define FD_ISSET(i, f)	rb_fd_isset(i, f)

#endif

static double
timeofday(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec * 1e-6;
}

static int
do_select(int n, fd_set *read, fd_set *write, fd_set *except,
	  struct timeval *timeout)
{
    int result, lerrno;
    fd_set orig_read, orig_write, orig_except;

    bzero(&orig_read, sizeof(fd_set));
    bzero(&orig_write, sizeof(fd_set));
    bzero(&orig_except, sizeof(fd_set));

#ifndef linux
    double limit = 0;
    struct timeval wait_rest;

    if (timeout) {
	limit = timeofday() +
	  (double)timeout->tv_sec+(double)timeout->tv_usec*1e-6;
	wait_rest = *timeout;
	timeout = &wait_rest;
    }
#endif

    if (read) orig_read = *read;
    if (write) orig_write = *write;
    if (except) orig_except = *except;

  retry:
    lerrno = 0;

    rb_vm_thread_t *thread = GetThreadPtr(rb_vm_current_thread());
    rb_vm_thread_status_t prev_status = thread->status;
    thread->status = THREAD_SLEEP;
    //BLOCKING_REGION({
	result = select(n, read, write, except, timeout);
	if (result < 0) lerrno = errno;
    //}, ubf_select, GET_THREAD());
    thread->status = prev_status;

    errno = lerrno;

    if (result < 0) {
	switch (errno) {
	  case EINTR:
#ifdef ERESTART
	  case ERESTART:
#endif
	    if (read) *read = orig_read;
	    if (write) *write = orig_write;
	    if (except) *except = orig_except;
#ifndef linux
	    if (timeout) {
		double d = limit - timeofday();

		wait_rest.tv_sec = (unsigned int)d;
		wait_rest.tv_usec = (long)((d-(double)wait_rest.tv_sec)*1e6);
		if (wait_rest.tv_sec < 0)  wait_rest.tv_sec = 0;
		if (wait_rest.tv_usec < 0) wait_rest.tv_usec = 0;
	    }
#endif
	    goto retry;
	  default:
	    break;
	}
    }
    return result;
}

static void
rb_thread_wait_fd_rw(int fd, int read)
{
    int result = 0;

    while (result <= 0) {
	rb_fdset_t *set = ALLOC(rb_fdset_t);
	rb_fd_init(set);
	FD_SET(fd, set);

	if (read) {
	    result = do_select(fd + 1, rb_fd_ptr(set), 0, 0, 0);
	}
	else {
	    result = do_select(fd + 1, 0, rb_fd_ptr(set), 0, 0);
	}

	rb_fd_term(set);

	if (result < 0) {
	    rb_sys_fail(0);
	}
    }
}

void
rb_thread_wait_fd(int fd)
{
    rb_thread_wait_fd_rw(fd, 1);
}

int
rb_thread_fd_writable(int fd)
{
    rb_thread_wait_fd_rw(fd, 0);
    return Qtrue;
}

int
rb_thread_select(int max, fd_set * read, fd_set * write, fd_set * except,
		 struct timeval *timeout)
{
    if (!read && !write && !except) {
	if (!timeout) {
	    rb_thread_sleep_forever();
	    return 0;
	}
	rb_thread_wait_for(*timeout);
	return 0;
    }
    else {
	return do_select(max, read, write, except, timeout);
    }
}

/*
 * Document-class: ThreadGroup
 *
 *  <code>ThreadGroup</code> provides a means of keeping track of a number of
 *  threads as a group. A <code>Thread</code> can belong to only one
 *  <code>ThreadGroup</code> at a time; adding a thread to a new group will
 *  remove it from any previous group.
 *
 *  Newly created threads belong to the same group as the thread from which they
 *  were created.
 */

static VALUE mutex_s_alloc(VALUE self, SEL sel);
static VALUE mutex_initialize(VALUE self, SEL sel);

static VALUE
thgroup_s_alloc(VALUE self, SEL sel)
{
    rb_thread_group_t *t = (rb_thread_group_t *)xmalloc(
	    sizeof(rb_thread_group_t));
    t->enclosed = false;
    GC_WB(&t->threads, rb_ary_new());
    OBJ_UNTRUST(t->threads);

    VALUE mutex = mutex_s_alloc(rb_cMutex, 0);
    mutex_initialize(mutex, 0);
    GC_WB(&t->mutex, mutex);

    return Data_Wrap_Struct(self, NULL, NULL, t);
}

/*
 *  call-seq:
 *     thgrp.list   => array
 *
 *  Returns an array of all existing <code>Thread</code> objects that belong to
 *  this group.
 *
 *     ThreadGroup::Default.list   #=> [#<Thread:0x401bdf4c run>]
 */

static VALUE
thgroup_list(VALUE group, SEL sel)
{
    return GetThreadGroupPtr(group)->threads;
}

/*
 *  call-seq:
 *     thgrp.enclose   => thgrp
 *
 *  Prevents threads from being added to or removed from the receiving
 *  <code>ThreadGroup</code>. New threads can still be started in an enclosed
 *  <code>ThreadGroup</code>.
 *
 *     ThreadGroup::Default.enclose        #=> #<ThreadGroup:0x4029d914>
 *     thr = Thread::new { Thread.stop }   #=> #<Thread:0x402a7210 sleep>
 *     tg = ThreadGroup::new               #=> #<ThreadGroup:0x402752d4>
 *     tg.add thr
 *
 *  <em>produces:</em>
 *
 *     ThreadError: can't move from the enclosed thread group
 */

static VALUE
thgroup_enclose(VALUE group, SEL sel)
{
    GetThreadGroupPtr(group)->enclosed = true;
    return group;
}

/*
 *  call-seq:
 *     thgrp.enclosed?   => true or false
 *
 *  Returns <code>true</code> if <em>thgrp</em> is enclosed. See also
 *  ThreadGroup#enclose.
 */

static VALUE
thgroup_enclosed_p(VALUE group)
{
    return GetThreadGroupPtr(group)->enclosed ? Qtrue : Qfalse;
}

/*
 *  call-seq:
 *     thgrp.add(thread)   => thgrp
 *
 *  Adds the given <em>thread</em> to this group, removing it from any other
 *  group to which it may have previously belonged.
 *
 *     puts "Initial group is #{ThreadGroup::Default.list}"
 *     tg = ThreadGroup.new
 *     t1 = Thread.new { sleep }
 *     t2 = Thread.new { sleep }
 *     puts "t1 is #{t1}"
 *     puts "t2 is #{t2}"
 *     tg.add(t1)
 *     puts "Initial group now #{ThreadGroup::Default.list}"
 *     puts "tg group now #{tg.list}"
 *
 *  <em>produces:</em>
 *
 *     Initial group is #<Thread:0x401bdf4c>
 *     t1 is #<Thread:0x401b3c90>
 *     t2 is #<Thread:0x401b3c18>
 *     Initial group now #<Thread:0x401b3c18>#<Thread:0x401bdf4c>
 *     tg group now #<Thread:0x401b3c90>
 */

static inline void
thgroup_lock(rb_thread_group_t *tg)
{
    pthread_assert(pthread_mutex_lock(&GetMutexPtr(tg->mutex)->mutex));
}

static inline void
thgroup_unlock(rb_thread_group_t *tg)
{
    pthread_assert(pthread_mutex_unlock(&GetMutexPtr(tg->mutex)->mutex));
}

static VALUE
thgroup_add_m(VALUE group, VALUE thread, bool check_enclose)
{
    if (OBJ_FROZEN(group)) {
	rb_raise(rb_eThreadError, "can't move to the frozen thread group");
    }

    rb_vm_thread_t *t = GetThreadPtr(thread);

    rb_thread_group_t *new_tg = GetThreadGroupPtr(group);
    if (new_tg->enclosed && check_enclose) {
	rb_raise(rb_eThreadError, "can't move from the enclosed thread group");
    }

    if (t->group != Qnil) {
	if (OBJ_FROZEN(t->group)) {
	    rb_raise(rb_eThreadError,
		    "can't move from the frozen thread group");
	}
	rb_thread_group_t *old_tg = GetThreadGroupPtr(t->group);
	if (old_tg->enclosed && check_enclose) {
	    rb_raise(rb_eThreadError,
		    "can't move from the enclosed thread group");
	}
 	thgroup_lock(old_tg);
	rb_ary_delete(old_tg->threads, thread); 
 	thgroup_unlock(old_tg);
    }

    thgroup_lock(new_tg);
    rb_ary_push(new_tg->threads, thread);
    thgroup_unlock(new_tg);
    GC_WB(&t->group, group);

    return group;
}

static VALUE
thgroup_add(VALUE group, SEL sel, VALUE thread)
{
    return thgroup_add_m(group, thread, true);
}

VALUE
rb_thgroup_add(VALUE group, VALUE thread)
{
    return thgroup_add(group, 0, thread);
}

void
rb_thread_remove_from_group(VALUE thread)
{
    rb_vm_thread_t *t = GetThreadPtr(thread);
    if (t->group != Qnil) {
	rb_thread_group_t *tg = GetThreadGroupPtr(t->group);
	thgroup_lock(tg);
	if (rb_ary_delete(tg->threads, thread) != thread) {
	    printf("trying to remove a thread (%p) from a group that doesn't "\
		    "contain it\n", (void *)thread);
	    abort();
	}
	thgroup_unlock(tg);
	t->group = Qnil;
    }
}

/*
 *  Document-class: Mutex
 *
 *  Mutex implements a simple semaphore that can be used to coordinate access to
 *  shared data from multiple concurrent threads.
 *
 *  Example:
 *
 *    require 'thread'
 *    semaphore = Mutex.new
 *
 *    a = Thread.new {
 *      semaphore.synchronize {
 *        # access shared resource
 *      }
 *    }
 *
 *    b = Thread.new {
 *      semaphore.synchronize {
 *        # access shared resource
 *      }
 *    }
 *
 */

/*
 *  call-seq:
 *     Mutex.new   => mutex
 *
 *  Creates a new Mutex
 */

static VALUE
mutex_s_alloc(VALUE self, SEL sel)
{
    rb_vm_mutex_t *t = (rb_vm_mutex_t *)xmalloc(sizeof(rb_vm_mutex_t));
    return Data_Wrap_Struct(self, NULL, NULL, t);
}

static VALUE
mutex_initialize(VALUE self, SEL sel)
{
    pthread_assert(pthread_mutex_init(&GetMutexPtr(self)->mutex, NULL));
    return self;
}

/*
 * call-seq:
 *    mutex.locked?  => true or false
 *
 * Returns +true+ if this lock is currently held by some thread.
 */

static VALUE
rb_mutex_locked_p(VALUE self, SEL sel)
{
    return GetMutexPtr(self)->thread == 0 ? Qfalse : Qtrue;
}

/*
 * call-seq:
 *    mutex.try_lock  => true or false
 *
 * Attempts to obtain the lock and returns immediately. Returns +true+ if the
 * lock was granted.
 */

static VALUE
rb_mutex_trylock(VALUE self, SEL sel)
{
    rb_vm_mutex_t *m = GetMutexPtr(self);

    if (pthread_mutex_trylock(&m->mutex) == 0) {
	rb_vm_thread_t *current = GetThreadPtr(rb_vm_current_thread());
	m->thread = current;
	if (current->mutexes == Qnil) {
	    GC_WB(&current->mutexes, rb_ary_new());
	    OBJ_UNTRUST(current->mutexes);
	}
	rb_ary_push(current->mutexes, self);
	return Qtrue;
    }

    return Qfalse;
}

/*
 * call-seq:
 *    mutex.lock  => self
 *
 * Attempts to grab the lock and waits if it isn't available.
 * Raises +ThreadError+ if +mutex+ was locked by the current thread.
 */

static VALUE
rb_mutex_lock(VALUE self, SEL sel)
{
    rb_vm_thread_t *current = GetThreadPtr(rb_vm_current_thread());
    rb_vm_mutex_t *m = GetMutexPtr(self);
    rb_vm_thread_status_t prev_status;
    if (m->thread == current) {
	rb_raise(rb_eThreadError, "deadlock; recursive locking");
    }

    prev_status = current->status;
    if (current->status == THREAD_ALIVE) {
	current->status = THREAD_SLEEP;
    }
    current->wait_for_mutex_lock = true;
    pthread_assert(pthread_mutex_lock(&m->mutex));
    current->wait_for_mutex_lock = false;
    current->status = prev_status;
    m->thread = current;
    if (current->mutexes == Qnil) {
	GC_WB(&current->mutexes, rb_ary_new());
	OBJ_UNTRUST(current->mutexes);
    }
    rb_ary_push(current->mutexes, self);

    return self;
}

/*
 * call-seq:
 *    mutex.unlock    => self
 *
 * Releases the lock.
 * Raises +ThreadError+ if +mutex+ wasn't locked by the current thread.
 */

static bool
rb_mutex_can_unlock(rb_vm_mutex_t *m, bool raise)
{
    if (m->thread == NULL) {
	if (!raise) {
	    return false;
	}
	rb_raise(rb_eThreadError,
		"Attempt to unlock a mutex which is not locked");
    }
    else if (m->thread != GetThreadPtr(rb_vm_current_thread())) {
	if (!raise) {
	    return false;
	}
	rb_raise(rb_eThreadError,
		"Attempt to unlock a mutex which is locked by another thread");
    }
    return true;
}

static void
rb_mutex_unlock0(VALUE self, bool assert_unlockable,
	bool delete_from_thread_mutexes)
{
    rb_vm_mutex_t *m = GetMutexPtr(self);
    bool ok = rb_mutex_can_unlock(m, assert_unlockable);
    if (ok) {
	if (delete_from_thread_mutexes) {
	    assert(m->thread->mutexes != Qnil);
	    rb_ary_delete(m->thread->mutexes, self);
	}
	m->thread = NULL;
	pthread_assert(pthread_mutex_unlock(&m->mutex));
    }
}

static VALUE
rb_mutex_unlock(VALUE self, SEL sel)
{
    rb_mutex_unlock0(self, true, true);
    return self;
}

void
rb_thread_unlock_all_mutexes(rb_vm_thread_t *thread)
{
    if (thread->mutexes != Qnil) {
	int i, count = RARRAY_LEN(thread->mutexes);
	for (i = 0; i < count; i++) {
	    rb_mutex_unlock0(RARRAY_AT(thread->mutexes, i), false, false);
	}
	rb_ary_clear(thread->mutexes);
    }
}

static VALUE
thread_sleep_forever(VALUE time)
{
    rb_thread_sleep_forever();
    return Qnil;
}

static VALUE
thread_wait_for(VALUE time)
{
    const struct timeval *t = (struct timeval *)time;
    rb_thread_wait_for(*t);
    return Qnil;
}

static VALUE
mutex_lock(VALUE self)
{
    return rb_mutex_lock(self, 0);
}

/*
 * call-seq:
 *    mutex.sleep(timeout = nil)    => number
 *
 * Releases the lock and sleeps +timeout+ seconds if it is given and
 * non-nil or forever.  Raises +ThreadError+ if +mutex+ wasn't locked by
 * the current thread.
 */

static VALUE
mutex_sleep(VALUE self, SEL sel, int argc, VALUE *argv)
{
    VALUE timeout;
    rb_scan_args(argc, argv, "01", &timeout);

    rb_mutex_unlock(self, 0);

    time_t beg, end;
    beg = time(0);

    if (timeout == Qnil) {
	rb_ensure(thread_sleep_forever, Qnil, mutex_lock, self);
    }
    else {
	struct timeval t = rb_time_interval(timeout);
	rb_ensure(thread_wait_for, (VALUE)&t, mutex_lock, self);
    }

    end = time(0) - beg;
    return INT2FIX(end);
}

/*
 * call-seq:
 *    mutex.synchronize { ... }    => result of the block
 *
 * Obtains a lock, runs the block, and releases the lock when the block
 * completes.  See the example under +Mutex+.
 */

static VALUE
sync_body(VALUE a)
{
    return rb_yield(Qundef);
}

static VALUE
sync_ensure(VALUE mutex)
{
    rb_mutex_unlock0(mutex, false, true);
    return Qnil;
}

static VALUE
mutex_synchronize(VALUE self, SEL sel)
{
    rb_mutex_lock(self, 0);
    return rb_ensure(sync_body, Qundef, sync_ensure, self);
}

VALUE
rb_thread_synchronize(VALUE mutex, VALUE (*func)(VALUE arg), VALUE arg)
{
    // TODO
    return Qnil;
}

/*
 *  +Thread+ encapsulates the behavior of a thread of
 *  execution, including the main thread of the Ruby script.
 *
 *  In the descriptions of the methods in this class, the parameter _sym_
 *  refers to a symbol, which is either a quoted string or a
 *  +Symbol+ (such as <code>:name</code>).
 */

void
Init_Thread(void)
{
    rb_cThread = rb_define_class("Thread", rb_cObject);
    rb_objc_define_method(*(VALUE *)rb_cThread, "alloc", thread_s_alloc, 0);

    thread_finalize_imp_super = rb_objc_install_method2((Class)rb_cThread,
	    "finalize", (IMP)thread_finalize_imp);

    rb_objc_define_method(*(VALUE *)rb_cThread, "start", thread_start, -1);
    rb_objc_define_method(*(VALUE *)rb_cThread, "fork", thread_start, -1);
    rb_objc_define_method(*(VALUE *)rb_cThread, "main", rb_thread_s_main, 0);
    rb_objc_define_method(*(VALUE *)rb_cThread, "current", thread_s_current, 0);
    rb_objc_define_method(*(VALUE *)rb_cThread, "stop", rb_thread_stop, 0);
    rb_objc_define_method(*(VALUE *)rb_cThread, "kill", rb_thread_s_kill, 1);
    rb_objc_define_method(*(VALUE *)rb_cThread, "exit", rb_thread_exit, 0);
    rb_objc_define_method(*(VALUE *)rb_cThread, "pass", thread_s_pass, 0);
    rb_objc_define_method(*(VALUE *)rb_cThread, "list", rb_thread_list, 0);
    rb_objc_define_method(*(VALUE *)rb_cThread, "abort_on_exception", rb_thread_s_abort_exc, 0);
    rb_objc_define_method(*(VALUE *)rb_cThread, "abort_on_exception=", rb_thread_s_abort_exc_set, 1);

    rb_objc_define_method(rb_cThread, "initialize", thread_initialize, -1);
    rb_objc_define_method(rb_cThread, "raise", thread_raise_m, -1);
    rb_objc_define_method(rb_cThread, "join", thread_join_m, -1);
    rb_objc_define_method(rb_cThread, "value", thread_value, 0);
    rb_objc_define_method(rb_cThread, "kill", rb_thread_kill, 0);
    rb_objc_define_method(rb_cThread, "terminate", rb_thread_kill, 0);
    rb_objc_define_method(rb_cThread, "exit", rb_thread_kill, 0);
    rb_objc_define_method(rb_cThread, "run", rb_thread_run, 0);
    rb_objc_define_method(rb_cThread, "wakeup", rb_thread_wakeup, 0);
    rb_objc_define_method(rb_cThread, "[]", rb_thread_aref, 1);
    rb_objc_define_method(rb_cThread, "[]=", rb_thread_aset, 2);
    rb_objc_define_method(rb_cThread, "key?", rb_thread_key_p, 1);
    rb_objc_define_method(rb_cThread, "keys", rb_thread_keys, 0);
    rb_objc_define_method(rb_cThread, "priority", rb_thread_priority, 0);
    rb_objc_define_method(rb_cThread, "priority=", rb_thread_priority_set, 1);
    rb_objc_define_method(rb_cThread, "status", rb_thread_status, 0);
    rb_objc_define_method(rb_cThread, "alive?", rb_thread_alive_p, 0);
    rb_objc_define_method(rb_cThread, "stop?", rb_thread_stop_p, 0);
    rb_objc_define_method(rb_cThread, "abort_on_exception", rb_thread_abort_exc, 0);
    rb_objc_define_method(rb_cThread, "abort_on_exception=", rb_thread_abort_exc_set, 1);
    rb_objc_define_method(rb_cThread, "safe_level", rb_thread_safe_level, 0);
    rb_objc_define_method(rb_cThread, "group", rb_thread_group, 0);

    rb_objc_define_method(rb_cThread, "inspect", rb_thread_inspect, 0);

    rb_cThGroup = rb_define_class("ThreadGroup", rb_cObject);
    rb_objc_define_method(*(VALUE *)rb_cThGroup, "alloc", thgroup_s_alloc, 0);
    rb_objc_define_method(rb_cThGroup, "list", thgroup_list, 0);
    rb_objc_define_method(rb_cThGroup, "enclose", thgroup_enclose, 0);
    rb_objc_define_method(rb_cThGroup, "enclosed?", thgroup_enclosed_p, 0);
    rb_objc_define_method(rb_cThGroup, "add", thgroup_add, 1);

    rb_cMutex = rb_define_class("Mutex", rb_cObject);
    rb_objc_define_method(*(VALUE *)rb_cMutex, "alloc", mutex_s_alloc, 0);
    rb_objc_define_method(rb_cMutex, "initialize", mutex_initialize, 0);
    rb_objc_define_method(rb_cMutex, "locked?", rb_mutex_locked_p, 0);
    rb_objc_define_method(rb_cMutex, "try_lock", rb_mutex_trylock, 0);
    rb_objc_define_method(rb_cMutex, "lock", rb_mutex_lock, 0);
    rb_objc_define_method(rb_cMutex, "unlock", rb_mutex_unlock, 0);
    rb_objc_define_method(rb_cMutex, "sleep", mutex_sleep, -1);
    rb_objc_define_method(rb_cMutex, "synchronize", mutex_synchronize, 0);

    rb_eThreadError = rb_define_class("ThreadError", rb_eStandardError);
}

VALUE
rb_thread_blocking_region(rb_blocking_function_t *func, void *data1,
	rb_unblock_function_t *ubf, void *data2)
{
    // For compatibility with CRuby. We do not have a global lock to release,
    // so we can just call the function directly.
    return func(data1);
}
