#include "ruby/ruby.h"
#include "ruby/node.h"
#include "vm.h"

#include <pthread.h>

VALUE rb_cThread;
VALUE rb_cMutex;

typedef struct rb_vm_thread {
    pthread_t thread;
    rb_vm_block_t *body;
    int argc;
    const VALUE *argv;
} rb_vm_thread_t;

#define GetThreadPtr(obj) ((rb_vm_thread_t *)DATA_PTR(obj))

static void *
rb_vm_thread_run(rb_vm_thread_t *t)
{
    rb_objc_gc_register_thread();
    rb_vm_block_eval(t->body, t->argc, t->argv);
    return NULL;
}

#if 0
static VALUE
thread_s_new(int argc, VALUE *argv, VALUE klass)
{
    // TODO
    return Qnil;
}
#endif

static VALUE
thread_s_alloc(VALUE rcv, SEL sel)
{
    rb_vm_thread_t *t = (rb_vm_thread_t *)xmalloc(sizeof(rb_vm_thread_t));
    return Data_Wrap_Struct(rb_cThread, NULL, NULL, t);
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
thread_start(VALUE klass, VALUE args)
{
    // TODO
    return Qnil;
}

static VALUE
thread_initialize(VALUE thread, SEL sel, int argc, VALUE *argv)
{
    if (!rb_block_given_p()) {
	rb_raise(rb_eThreadError, "must be called with a block");
    }
    rb_vm_block_t *b = rb_vm_current_block();
    assert(b != NULL);

    rb_vm_thread_t *t = GetThreadPtr(thread);
    assert(t->body == NULL);
    GC_WB(&t->body, b);

    if (argc > 0) {
	t->argc = argc;
	GC_WB(&t->argv, xmalloc(sizeof(VALUE) * argc));
	int i;
	for (i = 0; i < argc; i++) {
	    GC_WB(&t->argv[i], argv[i]);
	}
    }

    if (pthread_create(&t->thread, NULL, (void *(*)(void *))rb_vm_thread_run,
		t) != 0) {
	rb_sys_fail("pthread_create() failed");
    }

    return thread;
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
    if (argc != 0) {
	rb_raise(rb_eArgError,
		"calling #join with an argument is not implemented yet");
    }

    rb_vm_thread_t *t = GetThreadPtr(self);

    if (pthread_join(t->thread, NULL) != 0) {
	rb_sys_fail("pthread_join() failed");
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
thread_value(VALUE self)
{
    // TODO
    return Qnil;
}

void
rb_thread_sleep_forever()
{
    // TODO
}

void
rb_thread_wait_for(struct timeval time)
{
    // TODO
}

void
rb_thread_polling(void)
{
    // TODO
}

void
rb_thread_sleep(int sec)
{
    // TODO
}

void
rb_thread_schedule(void)
{
    // TODO
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
thread_s_pass(VALUE klass)
{
    // TODO
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
thread_raise_m(int argc, VALUE *argv, VALUE self)
{
    // TODO
    return Qnil;
}

/*
 *  call-seq:
 *     thr.exit        => thr or nil
 *     thr.kill        => thr or nil
 *     thr.terminate   => thr or nil
 *
 *  Terminates <i>thr</i> and schedules another thread to be run. If this thread
 *  is already marked to be killed, <code>exit</code> returns the
 *  <code>Thread</code>. If this is the main thread, or the last thread, exits
 *  the process.
 */

VALUE
rb_thread_kill(VALUE thread)
{
    // TODO
    return Qnil;
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
rb_thread_s_kill(VALUE obj, VALUE th)
{
    // TODO
    return Qnil;
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
    // TODO
    return Qnil;
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

VALUE
rb_thread_wakeup(VALUE thread)
{
    // TODO
    return Qnil;
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

VALUE
rb_thread_run(VALUE thread)
{
    // TODO
    return Qnil;
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

VALUE
rb_thread_stop(void)
{
    // TODO
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
rb_thread_list(void)
{
    // TODO
    return Qnil;
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
thread_s_current(VALUE klass)
{
    // TODO
    return Qnil;
}

static VALUE
rb_thread_s_main(VALUE klass)
{
    // TODO
    return Qnil;
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
rb_thread_s_abort_exc(void)
{
    // TODO
    return Qnil;
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
rb_thread_s_abort_exc_set(VALUE self, VALUE val)
{
    // TODO
    return Qnil;
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
rb_thread_abort_exc(VALUE thread)
{
    // TODO
    return Qnil;
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
rb_thread_abort_exc_set(VALUE thread, VALUE val)
{
    // TODO
    return Qnil;
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

VALUE
rb_thread_group(VALUE thread)
{
    // TODO
    return Qnil;
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

static VALUE
rb_thread_status(VALUE thread)
{
    // TODO
    return Qnil;
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
rb_thread_alive_p(VALUE thread)
{
    // TODO
    return Qnil;
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
    return Qnil;
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
rb_thread_safe_level(VALUE thread)
{
    // TODO
    return Qnil;
}

/*
 * call-seq:
 *   thr.inspect   => string
 *
 * Dump the name, id, and status of _thr_ to a string.
 */

static VALUE
rb_thread_inspect(VALUE thread)
{
    // TODO
    return Qnil;
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
rb_thread_aref(VALUE thread, VALUE id)
{
    // TODO
    return Qnil;
}

/*
 *  call-seq:
 *      thr[sym] = obj   => obj
 *
 *  Attribute Assignment---Sets or creates the value of a thread-local variable,
 *  using either a symbol or a string. See also <code>Thread#[]</code>.
 */

static VALUE
rb_thread_aset(VALUE self, ID id, VALUE val)
{
    // TODO
    return Qnil;
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
rb_thread_key_p(VALUE self, VALUE key)
{
    // TODO
    return Qnil;
}

int
rb_thread_alone()
{
    // TODO
    return 0;
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
rb_thread_keys(VALUE self)
{
    // TODO
    return Qnil;
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
rb_thread_priority(VALUE thread)
{
    // TODO
    return Qnil;
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
rb_thread_priority_set(VALUE thread, VALUE prio)
{
    // TODO
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
    if (fds->fdset) xfree(fds->fdset);
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

    //BLOCKING_REGION({
	result = select(n, read, write, except, timeout);
	if (result < 0) lerrno = errno;
    //}, ubf_select, GET_THREAD());

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
thgroup_list(VALUE group)
{
    // TODO
    return Qnil;
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

VALUE
thgroup_enclose(VALUE group)
{
    return Qnil;
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
    return Qnil;
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

static VALUE
thgroup_add(VALUE group, VALUE thread)
{
    return Qnil;
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
mutex_initialize(VALUE self)
{
    // TODO
    return Qnil;
}

VALUE
rb_mutex_new(void)
{
    // TODO
    return Qnil;
}

/*
 * call-seq:
 *    mutex.locked?  => true or false
 *
 * Returns +true+ if this lock is currently held by some thread.
 */

VALUE
rb_mutex_locked_p(VALUE self)
{
    return Qnil;
}

/*
 * call-seq:
 *    mutex.try_lock  => true or false
 *
 * Attempts to obtain the lock and returns immediately. Returns +true+ if the
 * lock was granted.
 */

VALUE
rb_mutex_trylock(VALUE self)
{
    return Qnil;
}

/*
 * call-seq:
 *    mutex.lock  => true or false
 *
 * Attempts to grab the lock and waits if it isn't available.
 * Raises +ThreadError+ if +mutex+ was locked by the current thread.
 */

VALUE
rb_mutex_lock(VALUE self)
{
    // TODO
    return Qnil;
}

/*
 * call-seq:
 *    mutex.unlock    => self
 *
 * Releases the lock.
 * Raises +ThreadError+ if +mutex+ wasn't locked by the current thread.
 */

VALUE
rb_mutex_unlock(VALUE self)
{
    // TODO
    return Qnil;
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
mutex_sleep(int argc, VALUE *argv, VALUE self)
{
    // TODO
    return Qnil;
}

/*
 * call-seq:
 *    mutex.synchronize { ... }    => result of the block
 *
 * Obtains a lock, runs the block, and releases the lock when the block
 * completes.  See the example under +Mutex+.
 */

static VALUE
mutex_synchronize(VALUE self, SEL sel)
{
    // TODO
    rb_yield(Qundef);
    return Qnil;
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
    VALUE cThGroup;

    rb_cThread = rb_define_class("Thread", rb_cObject);
    rb_objc_define_method(*(VALUE *)rb_cThread, "alloc", thread_s_alloc, 0);

    //rb_define_singleton_method(rb_cThread, "new", thread_s_new, -1);
    rb_define_singleton_method(rb_cThread, "start", thread_start, -2);
    rb_define_singleton_method(rb_cThread, "fork", thread_start, -2);
    rb_define_singleton_method(rb_cThread, "main", rb_thread_s_main, 0);
    rb_objc_define_method(*(VALUE *)rb_cThread, "current", thread_s_current, 0);
    rb_define_singleton_method(rb_cThread, "stop", rb_thread_stop, 0);
    rb_define_singleton_method(rb_cThread, "kill", rb_thread_s_kill, 1);
    rb_define_singleton_method(rb_cThread, "exit", rb_thread_exit, 0);
    rb_define_singleton_method(rb_cThread, "pass", thread_s_pass, 0);
    rb_define_singleton_method(rb_cThread, "list", rb_thread_list, 0);
    rb_define_singleton_method(rb_cThread, "abort_on_exception", rb_thread_s_abort_exc, 0);
    rb_define_singleton_method(rb_cThread, "abort_on_exception=", rb_thread_s_abort_exc_set, 1);

    rb_objc_define_method(rb_cThread, "initialize", thread_initialize, -1);
    rb_define_method(rb_cThread, "raise", thread_raise_m, -1);
    rb_objc_define_method(rb_cThread, "join", thread_join_m, -1);
    rb_define_method(rb_cThread, "value", thread_value, 0);
    rb_define_method(rb_cThread, "kill", rb_thread_kill, 0);
    rb_define_method(rb_cThread, "terminate", rb_thread_kill, 0);
    rb_define_method(rb_cThread, "exit", rb_thread_kill, 0);
    rb_define_method(rb_cThread, "run", rb_thread_run, 0);
    rb_define_method(rb_cThread, "wakeup", rb_thread_wakeup, 0);
    rb_define_method(rb_cThread, "[]", rb_thread_aref, 1);
    rb_define_method(rb_cThread, "[]=", rb_thread_aset, 2);
    rb_define_method(rb_cThread, "key?", rb_thread_key_p, 1);
    rb_define_method(rb_cThread, "keys", rb_thread_keys, 0);
    rb_define_method(rb_cThread, "priority", rb_thread_priority, 0);
    rb_define_method(rb_cThread, "priority=", rb_thread_priority_set, 1);
    rb_define_method(rb_cThread, "status", rb_thread_status, 0);
    rb_define_method(rb_cThread, "alive?", rb_thread_alive_p, 0);
    rb_define_method(rb_cThread, "stop?", rb_thread_stop_p, 0);
    rb_define_method(rb_cThread, "abort_on_exception", rb_thread_abort_exc, 0);
    rb_define_method(rb_cThread, "abort_on_exception=", rb_thread_abort_exc_set, 1);
    rb_define_method(rb_cThread, "safe_level", rb_thread_safe_level, 0);
    rb_define_method(rb_cThread, "group", rb_thread_group, 0);

    rb_define_method(rb_cThread, "inspect", rb_thread_inspect, 0);

    cThGroup = rb_define_class("ThreadGroup", rb_cObject);
    rb_define_method(cThGroup, "list", thgroup_list, 0);
    rb_define_method(cThGroup, "enclose", thgroup_enclose, 0);
    rb_define_method(cThGroup, "enclosed?", thgroup_enclosed_p, 0);
    rb_define_method(cThGroup, "add", thgroup_add, 1);

    rb_cMutex = rb_define_class("Mutex", rb_cObject);
    rb_define_method(rb_cMutex, "initialize", mutex_initialize, 0);
    rb_define_method(rb_cMutex, "locked?", rb_mutex_locked_p, 0);
    rb_define_method(rb_cMutex, "try_lock", rb_mutex_trylock, 0);
    rb_define_method(rb_cMutex, "lock", rb_mutex_lock, 0);
    rb_define_method(rb_cMutex, "unlock", rb_mutex_unlock, 0);
    rb_define_method(rb_cMutex, "sleep", mutex_sleep, -1);
    rb_objc_define_method(rb_cMutex, "synchronize", mutex_synchronize, 0);

    rb_eThreadError = rb_define_class("ThreadError", rb_eStandardError);
}
