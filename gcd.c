/*
 * MacRuby API for Grand Central Dispatch.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2009-2011, Apple Inc. All rights reserved.
 */

#define GCD_BLOCKS_COPY_DVARS 1

#include "macruby_internal.h"
#include "gcd.h"
#include <unistd.h>
#include "ruby/intern.h"
#include "ruby/node.h"
#include "ruby/io.h"
#include "objc.h"
#include "id.h"
#include "vm.h"
#include <libkern/OSAtomic.h>
#include <asl.h>

static SEL selClose;

typedef struct {
    struct RBasic basic;
    int suspension_count;
    dispatch_object_t obj;
} rb_dispatch_obj_t;

#define RDispatch(val) ((rb_dispatch_obj_t*)val)

typedef struct {
    struct RBasic basic;
    int suspension_count;
    dispatch_queue_t queue;
    int should_release_queue;
} rb_queue_t;

#define RQueue(val) ((rb_queue_t*)val)

typedef struct {
    struct RBasic basic;
    int suspension_count;
    dispatch_group_t group;
} rb_group_t;

#define RGroup(val) ((rb_group_t*)val)

typedef enum SOURCE_TYPE_ENUM
{
    SOURCE_TYPE_DATA_ADD,
    SOURCE_TYPE_DATA_OR,
    SOURCE_TYPE_MACH_SEND,
    SOURCE_TYPE_MACH_RECV,
    SOURCE_TYPE_PROC,
    SOURCE_TYPE_READ,
    SOURCE_TYPE_SIGNAL,
    SOURCE_TYPE_TIMER,
    SOURCE_TYPE_VNODE,
    SOURCE_TYPE_WRITE
} source_enum_t;

typedef struct {
    struct RBasic basic;
    int suspension_count;
    dispatch_source_t source;
    source_enum_t source_enum;
    rb_vm_block_t *event_handler;
    VALUE handle;
} rb_source_t;

#define RSource(val) ((rb_source_t*)val)

typedef struct {
    struct RBasic basic;
    int reserved;
    dispatch_semaphore_t sem;
    long count;
} rb_semaphore_t;

#define RSemaphore(val) ((rb_semaphore_t*)val)

static OSSpinLock _suspensionLock = 0;

static VALUE mDispatch;
static VALUE cObject;

static void *
dispatch_object_imp(void *rcv, SEL sel)
{
    rb_dispatch_obj_t *obj = RDispatch(rcv);
    return (void *)obj->obj._do;
}

// queue stuff
static VALUE cQueue;
static VALUE qMain;
static VALUE qHighPriority;
static VALUE qDefaultPriority;
static VALUE qLowPriority;
static ID high_priority_id;
static ID low_priority_id;
static ID default_priority_id;

#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1070
static VALUE qBackgroundPriority;
static ID background_priority_id;
#endif

static VALUE cGroup;
static VALUE cSource;
static VALUE cSemaphore;

static VALUE const_time_now;
static VALUE const_time_forever;

static inline void
Check_Queue(VALUE object)
{
    if (CLASS_OF(object) != cQueue && object != qMain) {
	rb_raise(rb_eArgError, "expected Queue object, but got %s",
		rb_class2name(CLASS_OF(object)));
    }
}

dispatch_queue_t
rb_get_dispatch_queue_object(VALUE queue)
{
    Check_Queue(queue);
    return (dispatch_queue_t)dispatch_object_imp((void *)queue, 0);
}

static inline void
Check_Group(VALUE object)
{
    if (CLASS_OF(object) != cGroup) {
	rb_raise(rb_eArgError, "expected Group object, but got %s",
		rb_class2name(CLASS_OF(object)));
    }
}

static VALUE 
rb_raise_init(VALUE self, SEL sel)
{
	rb_raise(rb_eArgError, "initializer called without any arguments");
    return self;
}


#define SEC2NSEC_UINT64(sec) (uint64_t)(sec * NSEC_PER_SEC)
#define SEC2NSEC_INT64(sec) (int64_t)(sec * NSEC_PER_SEC)
#define TIMEOUT_MAX (1.0 * INT64_MAX / NSEC_PER_SEC)

static inline uint64_t
rb_num2nsec(VALUE num)
{
    if (num == const_time_forever) {
	return DISPATCH_TIME_FOREVER;
    }

    const double sec = rb_num2dbl(num);
    if (sec < 0.0) {
        rb_raise(rb_eArgError, "negative delay specified");
    }
    return SEC2NSEC_UINT64(sec);
}

static inline dispatch_time_t
rb_num2timeout(VALUE num)
{
    dispatch_time_t dispatch_timeout = DISPATCH_TIME_FOREVER;
    if (!NIL_P(num)) {
        const double sec = rb_num2dbl(num);
        if (sec < TIMEOUT_MAX) {
            dispatch_timeout = dispatch_walltime(NULL, SEC2NSEC_INT64(sec));
        }
    }
    return dispatch_timeout;
}

static VALUE 
rb_queue_alloc(VALUE klass, SEL sel)
{
    NEWOBJ(queue, rb_queue_t);
    OBJSETUP(queue, klass, RUBY_T_NATIVE);
    queue->suspension_count = 0;
    queue->should_release_queue = 0;
    return (VALUE)queue;
}

static VALUE
rb_queue_from_dispatch(dispatch_queue_t dq, bool should_retain)
{
    VALUE q = rb_queue_alloc(cQueue, 0);
    if (should_retain) { 
        GC_RETAIN(q);
    }
    RQueue(q)->queue = dq;
    return q;
}

/*
 *  call-seq:
 *     Dispatch::Queue.concurrent(priority=:default)    => Dispatch::Queue
 *
 *  Returns one of the global concurrent priority queues.
 * 
 *  A dispatch queue is a FIFO queue that accepts tasks in the form of a block. 
 *  Blocks submitted to dispatch queues are executed on a pool of threads fully 
 *  managed by the system. Dispatched tasks execute one at a time in FIFO order.
 *  GCD takes take of using multiple cores effectively and better accommodate 
 *  the needs of all running applications, matching them to the 
 *  available system resources in a balanced fashion.
 *   
 *  Use concurrent queues to execute large numbers of tasks concurrently.
 *  GCD automatically creates three concurrent dispatch queues that are global 
 *  to your application and are differentiated only by their priority level. 
 *  
 *  The three priority levels are: +:low+, +:default+, 
 *  +:high+, corresponding to the DISPATCH_QUEUE_PRIORITY_HIGH, 
 *  DISPATCH_QUEUE_PRIORITY_DEFAULT, and DISPATCH_QUEUE_PRIORITY_LOW 
 *  (detailed in the dispatch_queue_create(3)[http://developer.apple.com/mac/library/DOCUMENTATION/Darwin/Reference/ManPages/man3/dispatch_queue_create.3.html]
 *  man page). The GCD thread dispatcher
 *  will perform actions submitted to the high priority queue before any actions 
 *  submitted to the default or low queues, and will only perform actions on the 
 *  low queues if there are no actions queued on the high or default queues.
 *  When installed on Mac OS 10.7 or later, the +:background+ priority level is 
 *  available. Actions submitted to this queue will execute on a thread set to 
 *  background state (via setpriority(2)), which throttles disk I/O and sets the 
 *  thread's scheduling priority to the lowest value possible.
 * 
 *  On Mac OS 10.7 and later, passing a string to +concurrent+ creates a new 
 *  concurrent queue with the specified string as its label. Private concurrent queues 
 *  created this way are identical to private FIFO queues created with +new+, except 
 *  for the fact that they execute their blocks in parallel.
 *
 *     gcdq = Dispatch::Queue.concurrent(:high)
 *     5.times { gcdq.async { print 'foo' } }
 *     gcdq_2 = Dispatch::Queue.concurrent(:low)
 *     gcdq_2.sync { print 'bar' }  # will always print 'foofoofoofoofoobar'.
 *
 */
static VALUE
rb_queue_get_concurrent(VALUE klass, SEL sel, int argc, VALUE *argv)
{
    VALUE priority;
    rb_scan_args(argc, argv, "01", &priority);
    if (!NIL_P(priority)) {
	
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1070
	if (TYPE(priority) == T_STRING) {
	    return rb_queue_from_dispatch(
		dispatch_queue_create(RSTRING_PTR(priority), DISPATCH_QUEUE_CONCURRENT), 1);
	} else if (TYPE(priority) != T_SYMBOL) {
		rb_raise(rb_eTypeError, "must pass a symbol or string to `concurrent`");
	}
#endif
	
	ID id = rb_to_id(priority);
	if (id == high_priority_id) {
	    return qHighPriority;
	}
	else if (id == low_priority_id) {
	    return qLowPriority;
	}
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1070
	else if (id == background_priority_id) {
	    return qBackgroundPriority;
	}
#endif
	else if (id != default_priority_id) {
	    rb_raise(rb_eArgError,
		    "invalid priority `%s' (expected either :low, :default or :high)",
		    rb_id2name(id));
        }
    }
    return qDefaultPriority;
}


/*
 *  call-seq:
 *     Dispatch::Queue.current      => Dispatch::Queue
 *
 *  When called from within a block that is being dispatched on a queue,
 *  this returns the queue in question. If executed outside of a block, 
 *  the result depends on whether the run method has been called on the
 *  main queue: if it has, it returns the main queue, otherwise it returns
 *  the default-priority concurrent queue.
 *
 */

static VALUE
rb_queue_get_current(VALUE klass, SEL sel)
{
    // TODO: check this to see if we need to retain it
    return rb_queue_from_dispatch(dispatch_get_current_queue(), false);
}

/*
 *  call-seq:
 *     Dispatch::Queue.main           => Dispatch::Queue
 *
 *  Returns the dispatch queue for the main thread.
 *
 */

static VALUE 
rb_queue_get_main(VALUE klass, SEL sel)
{
    return qMain;
}

/*
 *  call-seq:
 *     Dispatch::Queue.new(label)        => Dispatch::Queue
 *
 *  Returns a new serial dispatch queue.
 * 
 *  A dispatch is a FIFO queue to which you can submit tasks via a block. 
 *  Blocks submitted to dispatch queues are executed on a pool of threads fully 
 *  managed by the system. Dispatched tasks execute one at a time in FIFO order.
 *  GCD takes take of using multiple cores effectively to better accommodate 
 *  the needs of all running applications, matching them to the 
 *  available system resources in a balanced fashion.
 *
 *  Use serial GCD queues to ensure that tasks execute in a predictable order.
 *  It's a good practice to identify a specific purpose for each serial queue, 
 *  such as protecting a resource or synchronizing key processes.
 *  Create as many as you need - serial queues are extremely lightweight 
 *  (with a total memory footprint of less than 300 bytes); however, remember to 
 *  use concurrent queues if you need to perform idempotent tasks in parallel.
 *  Dispatch queues need to be labeled and thereofore you need to pass a name 
 *  to create your queue. By convention, labels are in reverse-DNS style.
 *
 *     gcdq = Dispatch::Queue.new('org.macruby.gcd.example')
 *     gcdq.async { p 'doc' }
 *     gcdq.async { p 'bar' }
 *     gcdq.sync {} 
 *
 */
 
static VALUE 
rb_queue_init(VALUE self, SEL sel, VALUE name)
{
    StringValue(name);

    rb_queue_t *queue = RQueue(self);
    queue->should_release_queue = 1;
    queue->queue = dispatch_queue_create(RSTRING_PTR(name), NULL);
    assert(queue->queue != NULL);
    dispatch_retain(queue->queue);
    return self;
}

static IMP rb_queue_finalize_super;

static void
rb_queue_finalize(void *rcv, SEL sel)
{
    rb_queue_t *queue = RQueue(rcv);
    if (queue->queue != NULL) 
    {
        OSSpinLockLock(&_suspensionLock);
        while (queue->suspension_count > 0) {
            queue->suspension_count--;
            dispatch_resume(queue->queue);
        }
        if (queue->should_release_queue) {
            dispatch_release(queue->queue);
            queue->should_release_queue = 0;
        }        
        OSSpinLockUnlock(&_suspensionLock);
    }
    if (rb_queue_finalize_super != NULL) {
        ((void(*)(void *, SEL))rb_queue_finalize_super)(rcv, sel);
    }
}

static VALUE
rb_block_rescue(VALUE data, VALUE exc)
{
    fprintf(stderr, "*** Dispatch block exited prematurely because of an uncaught exception:\n%s\n", rb_str_cstr(rb_format_exception_message(exc)));
    return Qnil;
}

static VALUE
rb_block_release_eval(VALUE data)
{
    GC_RELEASE(data);
    rb_vm_block_t *b = (rb_vm_block_t *)data;
    return rb_vm_block_eval(b, 0, NULL);
}

static void
rb_block_dispatcher(void *data)
{
    assert(data != NULL);
    rb_rescue(rb_block_release_eval, (VALUE)data, rb_block_rescue, Qnil);
}

static rb_vm_block_t *
get_prepared_block()
{
    rb_vm_block_t *block = rb_vm_current_block();
    if (block == NULL) {
        rb_raise(rb_eArgError, "block not given");
    }
#if GCD_BLOCKS_COPY_DVARS
    block = rb_vm_dup_block(block);
    for (int i = 0; i < block->dvars_size; i++) {
    	VALUE *slot = block->dvars[i];
    	VALUE *new_slot = xmalloc(sizeof(VALUE));
    	GC_WB(new_slot, *slot);
    	GC_WB(&block->dvars[i], new_slot);
    }
#else
    rb_vm_block_make_detachable_proc(block);
#endif
    GC_RETAIN(block);
    return block;
}

/* 
 *  call-seq:
 *    gcdq.async(group=nil) { @i = 42 }
 *
 *  Yields the passed block asynchronously via dispatch_async(3)[http://developer.apple.com/mac/library/DOCUMENTATION/Darwin/Reference/ManPages/man3/dispatch_async.3.html]:
 *  
 *     gcdq = Dispatch::Queue.new('doc')
 *     @i = 0
 *     gcdq.async { @i = 42 }
 *     while @i == 0 do; end
 *     p @i #=> 42
 *
 *  If a group is specified, the dispatch will be associated with that group via
 *  dispatch_group_async(3)[http://developer.apple.com/mac/library/DOCUMENTATION/Darwin/Reference/ManPages/man3/dispatch_group_async.3.html]:
 *
 *     gcdq = Dispatch::Queue.new('doc')
 *     gcdg = Dispatch::Group.new
 *     @i = 3.1415
 *     gcdq.async(gcdg) { @i = 42 }
 *     gcdg.wait
 *     p @i #=> 42
 *
 */

static VALUE
rb_queue_dispatch_async(VALUE self, SEL sel, int argc, VALUE *argv)
{
    rb_vm_block_t *block = get_prepared_block();
    VALUE group;
    rb_scan_args(argc, argv, "01", &group);

    if (group != Qnil) {
	Check_Group(group);
	dispatch_group_async_f(RGroup(group)->group, RQueue(self)->queue,
		(void *)block, rb_block_dispatcher);
    }
    else {
	dispatch_async_f(RQueue(self)->queue, (void *)block,
		rb_block_dispatcher);
    }

    return Qnil;
}

/* 
 *  call-seq:
 *    gcdq.sync { @i = 42 }
 *
 *  Yields the passed block synchronously via dispatch_sync(3)[http://developer.apple.com/mac/library/DOCUMENTATION/Darwin/Reference/ManPages/man3/dispatch_sync.3.html]:
 *  
 *     gcdq = Dispatch::Queue.new('doc')
 *     @i = 42
 *     gcdq.sync { @i = 42 }
 *     p @i #=> 42
 *
 */


static VALUE
rb_queue_dispatch_sync(VALUE self, SEL sel)
{
    rb_vm_block_t *block = get_prepared_block();
    dispatch_sync_f(RQueue(self)->queue, (void *)block,
	    rb_block_dispatcher);

    return Qnil;
}

/* 
 *  call-seq:
 *    gcdq.barrier_async { @i = 42 }
 *
 *  This function is a specialized version of the #async dispatch function. 
 *  When a block enqueued with barrier_async reaches the front of a private 
 *  concurrent queue, it waits until all other enqueued blocks to finish executing,
 *  at which point the block is executed. No blocks submitted after a call to 
 *  barrier_async will be executed until the enqueued block finishes. It returns 
 *  immediately.
 * 
 *  If the provided queue is not a concurrent private queue, this function behaves 
 *  identically to the #async function. 
 * 
 *  This function is only available on OS X 10.7 and later.
 *  
 *     gcdq = Dispatch::Queue.concurrent('org.macruby.documentation')
 *     @i = ""
 *     gcdq.async { @i += 'a' }
 *     gcdq.async { @i += 'b' }
 *     gcdq.barrier_async { @i += 'c' }
 *     p @i #=> either prints out 'abc' or 'bac'
 *
 */

#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1070

static VALUE
rb_queue_dispatch_barrier_async(VALUE self, SEL sel)
{
    rb_vm_block_t *block = get_prepared_block();
    dispatch_barrier_async_f(RQueue(self)->queue, (void *)block, rb_block_dispatcher);
    return Qnil;
}

#endif

/* 
 *  call-seq:
 *    gcdq.barrier_async { @i = 42 }
 *
 *  This function is identical to the #barrier_async function; however, it blocks 
 *  until the provided block is executed.
 * 
 *  If the provided queue is not a concurrent private queue, this function behaves 
 *  identically to the #sync function. 
 * 
 *  This function is only available on OS X 10.7 and later.
 *  
 *     gcdq = Dispatch::Queue.concurrent('org.macruby.documentation')
 *     @i = ""
 *     gcdq.async { @i += 'a' }
 *     gcdq.async { @i += 'b' }
 *     gcdq.barrier_sync { @i += 'c' } # blocks
 *     p @i #=> either prints out 'abc' or 'bac'
 *
 */

#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1070

static VALUE
rb_queue_dispatch_barrier_sync(VALUE self, SEL sel)
{
    rb_vm_block_t *block = get_prepared_block();
    dispatch_barrier_sync_f(RQueue(self)->queue, (void *)block, rb_block_dispatcher);
    return Qnil;
}

#endif

/* 
 *  call-seq:
 *    gcdq.after(delay) { block }
 *
 *  Runs the passed block after the given delay (in seconds) using
 *  dispatch_after(3)[http://developer.apple.com/mac/library/DOCUMENTATION/Darwin/Reference/ManPages/man3/dispatch_after.3.html],
 *  
 *     gcdq.after(0.5) { puts 'wait is over :)' }
 *
 */
static VALUE
rb_queue_dispatch_after(VALUE self, SEL sel, VALUE delay)
{
    dispatch_time_t offset = NIL_P(delay) ? DISPATCH_TIME_NOW : rb_num2timeout(delay);
    rb_vm_block_t *block = get_prepared_block();
    dispatch_after_f(offset, RQueue(self)->queue, (void *)block,
	    rb_block_dispatcher);

    return Qnil;
}

static VALUE
rb_block_arg_eval(VALUE *args)
{
    rb_vm_block_t *b = (rb_vm_block_t *)args[0];
    return rb_vm_block_eval(b, 1, &args[1]);
}

static void
rb_block_arg_dispatcher(rb_vm_block_t *block, VALUE param)
{
    assert(block != NULL);
    VALUE args[2];
    args[0] = (VALUE)block;
    args[1] = param;
    // XXX We are casting a C array to VALUE here!!!
    rb_rescue(rb_block_arg_eval, (VALUE)args, rb_block_rescue, Qnil);
}

static void
rb_block_applier(void *data, size_t ii)
{
    assert(data != NULL);
    rb_vm_block_t *block = rb_vm_uncache_or_dup_block((rb_vm_block_t *)data);
    rb_block_arg_dispatcher(block, SIZET2NUM(ii));
}

/* 
 *  call-seq:
 *    gcdq.apply(count) { |index| block }
 *
 *  Runs a block _count_ number of times asynchronously via
 *  dispatch_apply(3)[http://developer.apple.com/mac/library/DOCUMENTATION/Darwin/Reference/ManPages/man3/dispatch_apply.3.html],
 *  passing in an index and waiting until all of them are done.
 *  You must use a concurrent queue to run the blocks concurrently.
 *  
 *     gcdq = Dispatch::Queue.new('doc')
 *     @result = Array.new(5)
 *     gcdq.apply(5) {|i| @result[i] = i*i }
 *     p @result  #=> [0, 1, 4, 9, 16]
 *
 */

static VALUE
rb_queue_apply(VALUE self, SEL sel, VALUE n)
{
    rb_vm_block_t *block = get_prepared_block();
    dispatch_apply_f(NUM2SIZET(n), RQueue(self)->queue, (void *)block,
	    rb_block_applier);

    GC_RELEASE(block);

    return Qnil;
}

/* 
 *  call-seq:
 *    gcdq.to_s -> str
 *
 *  Returns the label of the dispatch queue
 *  
 *     gcdq = Dispatch::Queue.new('doc')
 *     gcdq.to_s #=> 'doc'
 *     gcdq = Dispatch::Queue.main
 *     gcdq.to_s #=> 'com.apple.main-thread'
 *
 */

static VALUE 
rb_queue_label(VALUE self, SEL sel)
{
    return rb_str_new2(dispatch_queue_get_label(RQueue(self)->queue));
}

static VALUE
rb_main_queue_run(VALUE self, SEL sel)
{
    dispatch_main();
    return Qnil; // never reached
}


/* 
 *  call-seq:
 *    obj.suspend!
 *
 *  Suspends the operation of a
 *  dispatch_object(3)[http://developer.apple.com/mac/library/DOCUMENTATION/Darwin/Reference/ManPages/man3/dispatch_object.3.html#//apple_ref/doc/man/3/dispatch_object]
 *  (queue or source). To resume operation, call +resume!+.
 *  
 *     gcdq = Dispatch::Queue.new('doc')
 *     gcdq.dispatch { sleep 1 }
 *     gcdq.suspend!
 *     gcdq.suspended?  #=> true
 *     gcdq.resume!
 *
 */
 
static VALUE
rb_dispatch_suspend(VALUE self, SEL sel)
{
    rb_dispatch_obj_t *dobj = RDispatch(self);
    OSSpinLockLock(&_suspensionLock);
    dobj->suspension_count++;
    OSSpinLockUnlock(&_suspensionLock);    
    dispatch_suspend(dobj->obj);
    return Qnil;
}

/* 
 *  call-seq:
 *    obj.resume!
 *
 *  Resumes the operation of a
 *  dispatch_object(3)[http://developer.apple.com/mac/library/DOCUMENTATION/Darwin/Reference/ManPages/man3/dispatch_object.3.html#//apple_ref/doc/man/3/dispatch_object]
 *  (queue or source). To suspend operation, call +suspend!+.
 *  
 *     gcdq = Dispatch::Queue.new('doc')
 *     gcdq.dispatch { sleep 1 }
 *     gcdq.suspend!
 *     gcdq.suspended?  #=> true
 *     gcdq.resume!
 *
 */
 
static VALUE
rb_dispatch_resume(VALUE self, SEL sel)
{
    rb_dispatch_obj_t *dobj = RDispatch(self);
    OSSpinLockLock(&_suspensionLock);
    if (dobj->suspension_count > 0) {
        dobj->suspension_count--;
        dispatch_resume(dobj->obj);
    }
    OSSpinLockUnlock(&_suspensionLock);    
    return Qnil;
}

/* 
 *  call-seq:
 *    obj.suspended?   => true or false
 *
 *  Returns +true+ if <i>obj</i> is suspended.
 *  
 *     gcdq = Dispatch::Queue.new('doc')
 *     gcdq.dispatch { sleep 1 }
 *     gcdq.suspend!
 *     gcdq.suspended?  #=> true
 *     gcdq.resume!
 *     gcdq.suspended?  #=> false
 *
 */
 
static VALUE
rb_dispatch_suspended_p(VALUE self, SEL sel)
{
    return (RDispatch(self)->suspension_count == 0) ? Qfalse : Qtrue;
}

static VALUE
rb_group_alloc(VALUE klass, SEL sel)
{
    NEWOBJ(group, rb_group_t);
    OBJSETUP(group, klass, RUBY_T_NATIVE);
    group->suspension_count = 0;
    return (VALUE)group;
}

/* 
 *  call-seq:
 *    Dispatch::Group.new    =>  Dispatch::Group
 *
 *  Returns a Group allowing for aggregate synchronization, as defined in:
 *  dispatch_group_create(3)[http://developer.apple.com/mac/library/DOCUMENTATION/Darwin/Reference/ManPages/man3/dispatch_group_create.3.html]
 
 *  You can dispatch multiple blocks and track when they all complete, 
 *  even though they might run on different queues. 
 *  This behavior can be helpful when progress can not be made until all 
 *  of the specified tasks are complete.
 *  
 *     gcdg = Dispatch::Group.new
 *
 */

static VALUE
rb_group_init(VALUE self, SEL sel)
{
    RGroup(self)->group = dispatch_group_create();
    assert(RGroup(self)->group != NULL);
    
    return self;
}

/* 
 *  call-seq:
 *    grp.notify(queue) { block }
 *
 *  Asynchronously schedules a block to be called when the previously
 *  submitted dispatches for that group have completed.
 *
 *     gcdq = Dispatch::Queue.new('doc')
 *     grp = Dispatch::Group.new
 *     gcdq.async(grp) { print 'foo' }
 *     grp.notify(gcdq) { print 'bar' } #=> foobar
 */

static VALUE
rb_group_notify(VALUE self, SEL sel, VALUE target)
{
    rb_vm_block_t *block = get_prepared_block();
    Check_Queue(target);

    dispatch_group_notify_f(RGroup(self)->group, RQueue(target)->queue,
	    (void *)block, rb_block_dispatcher);

    return Qnil;
}

/* 
 *  call-seq:
 *    grp.wait(timeout=nil)     => true or false
 *
 *  Waits until all the blocks associated with the +grp+ have 
 *  finished executing or until the specified +timeout+ has elapsed.
 *  The function will return +true+ if the group became empty within 
 *  the specified amount of time and will return +false+ otherwise.
 *  If the supplied timeout is nil, the function will wait indefinitely until 
 *  the specified group becomes empty, always returning true.
 *
 *     gcdq = Dispatch::Queue.new('doc')
 *     grp = Dispatch::Group.new
 *     gcdq.async(grp) { sleep 4 }
 *     grp.wait(5) #=> true
 */
 
static VALUE
rb_group_wait(VALUE self, SEL sel, int argc, VALUE *argv)
{
    VALUE num;
    rb_scan_args(argc, argv, "01", &num);
    return dispatch_group_wait(RGroup(self)->group, rb_num2timeout(num))
        == 0 ? Qtrue : Qfalse;
}

static IMP rb_group_finalize_super;

static void
rb_group_finalize(void *rcv, SEL sel)
{
    rb_group_t *grp = RGroup(rcv);
    if (grp->group != NULL) {
        dispatch_release(grp->group);
    }
    if (rb_group_finalize_super != NULL) {
        ((void(*)(void *, SEL))rb_group_finalize_super)(rcv, sel);
    }
}

static inline dispatch_source_type_t
rb_source_enum2type(source_enum_t value)
{
    switch (value)
    {
        case SOURCE_TYPE_DATA_ADD: return DISPATCH_SOURCE_TYPE_DATA_ADD;
        case SOURCE_TYPE_DATA_OR: return DISPATCH_SOURCE_TYPE_DATA_OR;
        case SOURCE_TYPE_MACH_SEND: return DISPATCH_SOURCE_TYPE_MACH_SEND;
        case SOURCE_TYPE_MACH_RECV: return DISPATCH_SOURCE_TYPE_MACH_RECV;
        case SOURCE_TYPE_PROC: return DISPATCH_SOURCE_TYPE_PROC;
        case SOURCE_TYPE_READ: return DISPATCH_SOURCE_TYPE_READ;
        case SOURCE_TYPE_SIGNAL: return DISPATCH_SOURCE_TYPE_SIGNAL;
        case SOURCE_TYPE_TIMER: return DISPATCH_SOURCE_TYPE_TIMER;
        case SOURCE_TYPE_VNODE: return DISPATCH_SOURCE_TYPE_VNODE;
        case SOURCE_TYPE_WRITE: return DISPATCH_SOURCE_TYPE_WRITE;
        default: rb_raise(rb_eArgError, 
                          "Unknown dispatch source type `%d'", value);
    }
    return NULL;
}

static inline BOOL
rb_source_is_file(rb_source_t *src)
{
    source_enum_t value = src->source_enum;
    if (value == SOURCE_TYPE_READ || value == SOURCE_TYPE_VNODE 
        || value == SOURCE_TYPE_WRITE) {
        return true;
    }
    return false;
}

static VALUE
rb_source_alloc(VALUE klass, SEL sel)
{
    NEWOBJ(source, rb_source_t);
    OBJSETUP(source, klass, RUBY_T_NATIVE);
    source->suspension_count = 1;
    return (VALUE)source;
}

static void
rb_source_event_handler(void* sourceptr)
{
    assert(sourceptr != NULL);
    rb_source_t *source = RSource(sourceptr);
    rb_block_arg_dispatcher(source->event_handler, (VALUE) source);
}

static void
rb_source_close_handler(void* sourceptr)
{
    assert(sourceptr != NULL);
    rb_source_t *src = RSource(sourceptr);
    rb_io_close(src->handle);
//    Call rb_io_close directly since rb_vm_call aborts inside block
//    rb_vm_call(io, selClose, 0, NULL, false);
}

/* 
 *  call-seq:
 *    Dispatch::Source.new(type, handle, mask, queue) {|src| block}
 *     => Dispatch::Source
 *
 *  Returns a Source used to monitor a variety of system objects and events, 
 *  using dispatch_source_create(3)[http://developer.apple.com/Mac/library/documentation/Darwin/Reference/ManPages/man3/dispatch_source_create.3.html]
 *
 *  If an IO object (e.g., File) is passed as the handle, it will automatically
 *  create a cancel handler that closes that file (see +cancel!+ for details).
 *  The type must be one of:
 *      - Dispatch::Source::READ (calls +handle.close_read+)
 *      - Dispatch::Source::WRITE (calls +handle.close_write+)
 *      - Dispatch::Source::VNODE (calls +handle.close+)
 *  This is the only way to set the cancel_handler, since in MacRuby
 *  sources start off resumed. This is safer than closing the file
 *  yourself, as the cancel handler is guaranteed to only run once,
 *  and only after all pending events are processed.
 *  If you do *not* want the file closed on cancel, simply use
 *  +file.to_i+ to instead pass a descriptor as the handle.
 */

static VALUE
rb_source_init(VALUE self, SEL sel,
    VALUE type, VALUE handle, VALUE mask, VALUE queue)
{
    Check_Queue(queue);
    rb_source_t *src = RSource(self);
    src->source_enum = (source_enum_t) NUM2LONG(type);
    dispatch_source_type_t c_type = rb_source_enum2type(src->source_enum);
    assert(c_type != NULL);
    uintptr_t c_handle = NUM2UINT(rb_Integer(handle));
    unsigned long c_mask = NUM2LONG(mask);
    dispatch_queue_t c_queue = RQueue(queue)->queue;
    src->source = dispatch_source_create(c_type, c_handle, c_mask, c_queue);
    assert(src->source != NULL);

    rb_vm_block_t *block = get_prepared_block();
    GC_WB(&src->event_handler, block);
    GC_RETAIN(self); // apparently needed to ensure consistent counting
    dispatch_set_context(src->source, (void *)self);
    dispatch_source_set_event_handler_f(src->source, rb_source_event_handler);

    GC_WB(&src->handle, handle);
    if (rb_source_is_file(src) && rb_obj_is_kind_of(handle, rb_cIO)) {
        dispatch_source_set_cancel_handler_f(src->source,
          rb_source_close_handler);
    }
    rb_dispatch_resume(self, 0);
    return self;
}

/* 
 *  call-seq:
 *    Dispatch::Source.timer(delay, interval, leeway, queue) 
 *    =>  Dispatch::Source
 *
 *  Returns a Source that will submit the event handler block to
 *  the target queue after delay, repeated at interval, within leeway, via
 *  a call to dispatch_source_set_timer(3)[http://developer.apple.com/mac/library/DOCUMENTATION/Darwin/Reference/ManPages/man3/dispatch_source_set_timer.3.html].
 *  A best effort attempt is made to submit the event handler block to the
 *  target queue at the specified time; however, actual invocation may occur at
 *  a later time even if the leeway is zero.
 *  
 *     gcdq = Dispatch::Queue.new('doc')
 *     timer = Dispatch::Source.timer(0, 5, 0.1, gcdq) do |s|
 *        puts s.data
 *     end
 *
 */

static VALUE
rb_source_timer(VALUE klass, VALUE sel, VALUE delay, VALUE interval, VALUE leeway, VALUE queue)
{
    Check_Queue(queue);
    dispatch_time_t start_time;
    VALUE argv[4] = {INT2FIX(SOURCE_TYPE_TIMER),
        INT2FIX(0), INT2FIX(0), queue};
    
    VALUE self = rb_class_new_instance(4, argv, cSource);
    rb_source_t *src = RSource(self);

    if (NIL_P(leeway)) {
        leeway = INT2FIX(0);
    }
    if (NIL_P(delay)) {
        start_time = DISPATCH_TIME_NOW;
    }
    else {
        start_time = rb_num2timeout(delay);
    }

    rb_dispatch_suspend(self, 0);
    dispatch_source_set_timer(src->source, start_time,
	    rb_num2nsec(interval), rb_num2nsec(leeway));
    rb_dispatch_resume(self, 0);
    return self;
}

/* 
 *  call-seq:
 *    src.handle => Number
 *
 *  Returns the underlying Ruby handle for the dispatch source (i.e. file,
 *  file descriptor, process identifer, etc.).
 *  
 *     gcdq = Dispatch::Queue.new('doc')
 *     name = "/var/tmp/gcd_spec_source-#{$$}-#{Time.now}"
 *     file = File.open(name, "w")
 *     src = Dispatch::Source.new(Dispatch::Source::WRITE, file, 0, gcdq) { }
 *     puts src.handle #=> file
 */

static VALUE
rb_source_get_handle(VALUE self, SEL sel)
{
    return RSource(self)->handle;
}

/* 
 *  call-seq:
 *    src.mask => Number
 *
 *  Returns a Number representing the mask argument, corresponding to the flags
 *  set when the source was created.
 *  
 *     gcdq = Dispatch::Queue.new('doc')
 *     src = Dispatch::Source.new(Dispatch::Source::DATA_ADD, 0, 0, gcdq) { }
 *     puts src.mask #=> 0
 */

static VALUE
rb_source_get_mask(VALUE self, SEL sel)
{
    return INT2NUM(dispatch_source_get_mask(RSource(self)->source));
}

/* 
 *  call-seq:
 *    src.data => Number
 *
 *  Returns a Number containing currently pending data for the dispatch source.
 *  This function should only be called from within the source's event handler.
 *  The result of calling this function from any other context is undefined.
 *  
 *     gcdq = Dispatch::Queue.new('doc')
 *     src = Dispatch::Source.new(Dispatch::Source::DATA_ADD, 0, 0, gcdq) do |s|
 *       puts s.data
 *     end
 *     src << 1
 *     gcdq.sync { } #=> 1
 */

static VALUE
rb_source_get_data(VALUE self, SEL sel)
{
    return LONG2NUM(dispatch_source_get_data(RSource(self)->source));
}

/* 
 *  call-seq:
 *    src << number
 *
 *  Intended only for use with the Dispatch::Source::DATA_ADD and
 *  Dispatch::Source::DATA_OR source types, calling this function will
 *  atomically ADD or logical OR the count into the source's data, and 
 *  trigger delivery of the source's event handler.
 *  
 *     gcdq = Dispatch::Queue.new('doc')
 *     @sum = 0
 *     src = Dispatch::Source.new(Dispatch::Source::DATA_ADD, 0, 0, gcdq) do |s|
 *       @sum += s.data # safe since always serialized
 *     end
 *     src << 1
 *     src << 3
 *     gcdq.sync { }
 *     puts @sum #=> 4
 */

static VALUE
rb_source_merge(VALUE self, SEL sel, VALUE data)
{
    dispatch_source_merge_data(RSource(self)->source, NUM2INT(data));
    return Qnil;
}

/* 
 *  call-seq:
 *    src.cancel!
 *
 *  Asynchronously cancels the dispatch source, preventing any further
 *  invocation of its event handler block. Cancellation does not interrupt a
 *  currently executing handler block (non-preemptive).
 *
 *  When a dispatch source is canceled its cancellation handler will be 
 *  submitted to its target queue. This is only used by Dispatch::Source;
 *  when initialized a File (or IO) object, it will automatically set a
 *  cancellation handler that closes it.
 */

static VALUE
rb_source_cancel(VALUE self, SEL sel)
{
    dispatch_source_cancel(RSource(self)->source);
    return Qnil;
}

/* 
 *  call-seq:
 *    src.cancelled?
 *
 *  Used to determine whether the specified source has been cancelled.
 *  True will be returned if the source is cancelled.
 */

static VALUE
rb_source_cancelled_p(VALUE self, SEL sel)
{
    return dispatch_source_testcancel(RSource(self)->source) ? Qtrue : Qfalse;
}

static IMP rb_source_finalize_super;

static void
rb_source_finalize(void *rcv, SEL sel)
{
    rb_source_t *src = RSource(rcv);
    if (src->source != NULL) {
        OSSpinLockLock(&_suspensionLock);    
        while (src->suspension_count > 0) {
            src->suspension_count--;
            dispatch_resume(src->source);
        }
        dispatch_release(src->source);
        OSSpinLockUnlock(&_suspensionLock);            
    }
    if (rb_source_finalize_super != NULL) {
        ((void(*)(void *, SEL))rb_source_finalize_super)(rcv, sel);
    }
}

static VALUE
rb_semaphore_alloc(VALUE klass, SEL sel)
{
    NEWOBJ(s, rb_semaphore_t);
    OBJSETUP(s, klass, RUBY_T_NATIVE);
    s->sem = NULL;
    s->count = 0;
    return (VALUE)s;
}

/* 
 *  call-seq:
 *    Dispatch::Semaphore.new(count) =>  Dispatch::Semaphore
 *
 *  Returns a Semaphore used to synchronize threads through a combination of
 *  waiting and signalling, as detailed in the
 *  dispatch_semaphore_create(3)[http://developer.apple.com/mac/library/DOCUMENTATION/Darwin/Reference/ManPages/man3/dispatch_semaphore_create.3.html]
 *  man page.
 *
 *  If the count parameter is equal to zero, the semaphore is useful for
 *  synchronizing completion of work:
 *
 *     gcdq = Dispatch::Queue.new('doc')
 *     sema = Dispatch::Semaphore.new(0)
 *     gcdq.async { puts "Begin."; sema.signal }
 *     puts "Waiting..."
 *     sema.wait
 *     puts "End!"
 *
 *  If the count parameter is greater than zero, then the semaphore is useful
 *  for managing a finite pool of resources.
 *
 */

static VALUE
rb_semaphore_init(VALUE self, SEL sel, VALUE value)
{
    dispatch_semaphore_t s = dispatch_semaphore_create(NUM2LONG(value));
    if (s == NULL) {
    	rb_raise(rb_eArgError, "Can't create semaphore based on value `%ld'",
    		NUM2LONG(value));
    }
    RSemaphore(self)->sem = s;
    RSemaphore(self)->count = NUM2LONG(value);
    return self;
}

/* 
 *  call-seq:
 *    sema.signal => true or false
 *
 *  Signals the semaphore to wake up any waiting threads
 *
 *  Returns true if no thread is waiting, false otherwise
 *
 *     gcdq = Dispatch::Queue.new('doc')
 *     sema = Dispatch::Semaphore.new(0)
 *     gcdq.async { 
 *       sleep 0.1
 *       sema.signal #=> false
 *     }
 *     sema.wait
 *
 */
static VALUE
rb_semaphore_signal(VALUE self, SEL sel)
{
    return dispatch_semaphore_signal(RSemaphore(self)->sem) == 0 
        ? Qtrue : Qfalse;
}

/* 
 *  call-seq:
 *    sema.wait(timeout) => true or false
 *
 *  Waits (blocks the thread) until a signal arrives or the timeout expires.
 *  Timeout defaults to DISPATCH_TIME_FOREVER.
 *
 *  Returns true if signalled, false if timed out.
 *
 *     gcdq = Dispatch::Queue.new('doc')
 *     sema = Dispatch::Semaphore.new(0)
 *     gcdq.async { sleep 0.1; sema.signal }
 *     sema.wait #=> true
 *
 */
static VALUE
rb_semaphore_wait(VALUE self, SEL sel, int argc, VALUE *argv)
{
    VALUE num;
    rb_scan_args(argc, argv, "01", &num);
    return dispatch_semaphore_wait(RSemaphore(self)->sem, rb_num2timeout(num))
        == 0 ? Qtrue : Qfalse;
}


static IMP rb_semaphore_finalize_super;

static void
rb_semaphore_finalize(void *rcv, SEL sel)
{
    if (RSemaphore(rcv)->sem != NULL) {
	while (dispatch_semaphore_signal(RSemaphore(rcv)->sem) != 0) {}
	while (--RSemaphore(rcv)->count >= 0) {
	    dispatch_semaphore_signal(RSemaphore(rcv)->sem);
	}
	dispatch_release(RSemaphore(rcv)->sem);
    }
    if (rb_semaphore_finalize_super != NULL) {
        ((void(*)(void *, SEL))rb_semaphore_finalize_super)(rcv, sel);
    }
}

// GCD callbacks that will let us know when a POSIX thread is started / ended.
// We can appropriately create/delete a RoxorVM object based on that.
static void (*old_dispatch_begin_thread_4GC)(void) = NULL;
static void (*old_dispatch_end_thread_4GC)(void) = NULL;
extern void (*dispatch_begin_thread_4GC)(void);
extern void (*dispatch_end_thread_4GC)(void);

static void
rb_dispatch_begin_thread(void)
{
    if (old_dispatch_begin_thread_4GC != NULL) {
	(*old_dispatch_begin_thread_4GC)();
    }
    rb_vm_register_current_alien_thread();
}

static void
rb_dispatch_end_thread(void)
{
    if (old_dispatch_end_thread_4GC != NULL) {
	(*old_dispatch_end_thread_4GC)();
    }
    rb_vm_unregister_current_alien_thread();
}

void
Init_PreGCD(void)
{
    old_dispatch_begin_thread_4GC = dispatch_begin_thread_4GC;
    old_dispatch_end_thread_4GC = dispatch_end_thread_4GC;
    dispatch_begin_thread_4GC = rb_dispatch_begin_thread;
    dispatch_end_thread_4GC = rb_dispatch_end_thread;
}

void
Init_Dispatch(void)
{
    high_priority_id = rb_intern("high");
    low_priority_id = rb_intern("low");
    default_priority_id = rb_intern("default");
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1070
    background_priority_id = rb_intern("background");
#endif

/*
 *  Grand Central Dispatch (GCD) is a novel approach to multicore computing
 *  first release in Mac OS X version 10.6 Snow Leopard, and available as
 *  open source via the libdispatch project. GCD shifts the
 *  responsibility for managing threads and their execution from
 *  applications onto the operating system. This allows programmers to easily
 *  refactor their programs into small chunks of independent work, which GCD
 *  then schedules onto per-process thread pools.  Because GCD knows the load
 *  across the entire system, it ensures the resulting programs perform
 *  optimally on a wide range of hardware.
 *
 *  The Dispatch module allows Ruby blocks to be scheduled for asynchronous and
 *  concurrent execution, either explicitly or in response to
 *  various kinds of events. It provides a convenient high-level interface
 *  to the underlying C API via objects for the four primary abstractions.
 *
 *  Dispatch::Queue is the basic units of organization of blocks.
 *  Several queues are created by default, and applications may create
 *  additional queues for their own use.
 *
 *  Dispatch::Group allows applications to track the progress of blocks
 *  submitted to queues and take action when the blocks complete.
 * 
 *  Dispatch::Source monitors and coalesces low-level system events so that they
 *  can be responded to asychronously via simple event handlers.
 *
 *  Dispatch::Semaphore synchronizes threads via a combination of
 *  waiting and signalling.
 *
 *  For more information, see the dispatch(3)[http://developer.apple.com/mac/library/DOCUMENTATION/Darwin/Reference/ManPages/man3/dispatch.3.html] man page.  
 */
    mDispatch = rb_define_module("Dispatch");

    cObject = rb_define_class_under(mDispatch, "Object", rb_cObject);
    rb_objc_define_method(cObject, "resume!", rb_dispatch_resume, 0);
    rb_objc_define_method(cObject, "suspend!", rb_dispatch_suspend, 0);
    rb_objc_define_method(cObject, "suspended?", rb_dispatch_suspended_p, 0);

    // This API allows Ruby code to pass the internal dispatch_queue_t object
    // to C/Objective-C APIs.
    class_replaceMethod((Class)cObject, sel_registerName("dispatch_object"),
	    (IMP)dispatch_object_imp, "^v@:");

/*
 * A Dispatch::Queue is the fundamental mechanism for scheduling blocks for
 * execution, either synchronously or asychronously.
 *
 * All blocks submitted to dispatch queues begin executing in the order
 * they were received. The system-defined concurrent queues can execute
 * multiple blocks in parallel, depending on the number of idle threads
 * in the thread pool. Serial queues (the main and user-created queues)
 * wait for the prior block to complete before dequeuing and executing
 * the next block.
 *
 * Queues are not bound to any specific thread of execution and blocks
 * submitted to independent queues may execute concurrently.
 */ 
 
    cQueue = rb_define_class_under(mDispatch, "Queue", cObject);    
    rb_objc_define_method(*(VALUE *)cQueue, "alloc", rb_queue_alloc, 0);
    rb_objc_define_method(*(VALUE *)cQueue, "concurrent",
	    rb_queue_get_concurrent, -1);
    rb_objc_define_method(*(VALUE *)cQueue, "current", rb_queue_get_current, 0);
    rb_objc_define_method(*(VALUE *)cQueue, "main", rb_queue_get_main, 0);
    rb_objc_define_method(cQueue, "initialize", rb_queue_init, 1);
    rb_objc_define_method(cQueue, "initialize", rb_raise_init, 0);
    rb_objc_define_method(cQueue, "apply", rb_queue_apply, 1);
    rb_objc_define_method(cQueue, "async", rb_queue_dispatch_async, -1);
    rb_objc_define_method(cQueue, "sync", rb_queue_dispatch_sync, 0);
    rb_objc_define_method(cQueue, "after", rb_queue_dispatch_after, 1);
    rb_objc_define_method(cQueue, "label", rb_queue_label, 0); // deprecated
    rb_objc_define_method(cQueue, "to_s", rb_queue_label, 0);

#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1070
    rb_objc_define_method(cQueue, "barrier_async", rb_queue_dispatch_barrier_async, 0);
    rb_objc_define_method(cQueue, "barrier_sync", rb_queue_dispatch_barrier_sync, 0);
#endif
    
    
    rb_queue_finalize_super = rb_objc_install_method2((Class)cQueue,
	    "finalize", (IMP)rb_queue_finalize);

    qHighPriority = rb_queue_from_dispatch(dispatch_get_global_queue(
		DISPATCH_QUEUE_PRIORITY_HIGH, 0), true);
    qDefaultPriority = rb_queue_from_dispatch(dispatch_get_global_queue(
		DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), true);
    qLowPriority = rb_queue_from_dispatch(dispatch_get_global_queue(
		DISPATCH_QUEUE_PRIORITY_LOW, 0), true);
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1070
    qBackgroundPriority = rb_queue_from_dispatch(dispatch_get_global_queue(
		DISPATCH_QUEUE_PRIORITY_BACKGROUND, 0), true);
#endif
    
    qMain = rb_queue_from_dispatch(dispatch_get_main_queue(), true);
    rb_objc_define_method(rb_singleton_class(qMain), "run", rb_main_queue_run,
	    0);
    
/*
 * Dispatch::Group is used to aggregate multiple blocks 
 * that have been submitted asynchronously to different queues.
 * This lets you ensure they have all completed before beginning
 * or submitting additional work.
 */ 
    cGroup = rb_define_class_under(mDispatch, "Group", cObject);
    rb_objc_define_method(*(VALUE *)cGroup, "alloc", rb_group_alloc, 0);
    rb_objc_define_method(cGroup, "initialize", rb_group_init, 0);
    rb_objc_define_method(cGroup, "notify", rb_group_notify, 1);
    rb_objc_define_method(cGroup, "on_completion", rb_group_notify, 1); // deprecated
    rb_objc_define_method(cGroup, "wait", rb_group_wait, -1);
    
    rb_group_finalize_super = rb_objc_install_method2((Class)cGroup,
	    "finalize", (IMP)rb_group_finalize);

/*
 *  Dispatch::Source monitors a variety of system objects and events including
 *  file descriptors, processes, virtual filesystem nodes, signals and timers.
 *
 *  When a state change occurs, the dispatch source will submit its event
 *  handler block to its target queue, with the source as a parameter.
 *  
 *     gcdq = Dispatch::Queue.new('doc')
 *     src = Dispatch::Source.new(Dispatch::Source::DATA_ADD, 0, 0, gcdq) do |s|
 *       puts "Fired with #{s.data}"
 *     end
 *
 *  Unlike GCD's C API, Dispatch::Source objects start off resumed
 *  (since the event handler -et al- have already been set).
 *   
 *     src.suspended? #=? false
 *     src << 0
 *     gcdq.sync { } #=> Fired!
 */
    cSource = rb_define_class_under(mDispatch, "Source", cObject);
    rb_define_const(cSource, "DATA_ADD", INT2NUM(SOURCE_TYPE_DATA_ADD));
    rb_define_const(cSource, "DATA_OR", INT2NUM(SOURCE_TYPE_DATA_OR));
    rb_define_const(cSource, "PROC", INT2NUM(SOURCE_TYPE_PROC));
    rb_define_const(cSource, "READ", INT2NUM(SOURCE_TYPE_READ));
    rb_define_const(cSource, "SIGNAL", INT2NUM(SOURCE_TYPE_SIGNAL));
    rb_define_const(cSource, "VNODE", INT2NUM(SOURCE_TYPE_VNODE));
    rb_define_const(cSource, "WRITE", INT2NUM(SOURCE_TYPE_WRITE));
    
    rb_define_const(cSource, "PROC_EXIT", INT2NUM(DISPATCH_PROC_EXIT));
    rb_define_const(cSource, "PROC_FORK", INT2NUM(DISPATCH_PROC_FORK));
    rb_define_const(cSource, "PROC_EXEC", INT2NUM(DISPATCH_PROC_EXEC));
    rb_define_const(cSource, "PROC_SIGNAL", INT2NUM(DISPATCH_PROC_SIGNAL));

    rb_define_const(cSource, "VNODE_DELETE", INT2NUM(DISPATCH_VNODE_DELETE));
    rb_define_const(cSource, "VNODE_WRITE", INT2NUM(DISPATCH_VNODE_WRITE));
    rb_define_const(cSource, "VNODE_EXTEND", INT2NUM(DISPATCH_VNODE_EXTEND));
    rb_define_const(cSource, "VNODE_ATTRIB", INT2NUM(DISPATCH_VNODE_ATTRIB));
    rb_define_const(cSource, "VNODE_LINK", INT2NUM(DISPATCH_VNODE_LINK));
    rb_define_const(cSource, "VNODE_RENAME", INT2NUM(DISPATCH_VNODE_RENAME));
    rb_define_const(cSource, "VNODE_REVOKE", INT2NUM(DISPATCH_VNODE_REVOKE));

    rb_objc_define_method(*(VALUE *)cSource, "alloc", rb_source_alloc, 0);
    rb_objc_define_method(*(VALUE *)cSource, "timer", rb_source_timer, 4);
    rb_objc_define_method(cSource, "initialize", rb_source_init, 4);
    rb_objc_define_method(cSource, "initialize", rb_raise_init, 0);
    rb_objc_define_method(cSource, "cancelled?", rb_source_cancelled_p, 0);
    rb_objc_define_method(cSource, "cancel!", rb_source_cancel, 0);
    rb_objc_define_method(cSource, "handle", rb_source_get_handle, 0);
    rb_objc_define_method(cSource, "mask", rb_source_get_mask, 0);
    rb_objc_define_method(cSource, "data", rb_source_get_data, 0);
    rb_objc_define_method(cSource, "<<", rb_source_merge, 1);

    rb_source_finalize_super = rb_objc_install_method2((Class)cSource,
	    "finalize", (IMP)rb_source_finalize);

/*
 * Dispatch::Semaphore provides an efficient mechanism to synchronizes threads
 * via a combination of waiting and signalling.
 * This is especially useful for controlling access to limited resources.
 */
    cSemaphore = rb_define_class_under(mDispatch, "Semaphore", cObject);
    rb_objc_define_method(*(VALUE *)cSemaphore, "alloc", rb_semaphore_alloc, 0);
    rb_objc_define_method(cSemaphore, "initialize", rb_semaphore_init, 1);
    rb_objc_define_method(cSemaphore, "initialize", rb_raise_init, 0);
    rb_objc_define_method(cSemaphore, "wait", rb_semaphore_wait, -1);
    rb_objc_define_method(cSemaphore, "signal", rb_semaphore_signal, 0);

    rb_semaphore_finalize_super = rb_objc_install_method2((Class)cSemaphore,
	    "finalize", (IMP)rb_semaphore_finalize);

/*
 * Constants for use with
 * dispatch_time(3)[http://developer.apple.com/Mac/library/documentation/Darwin/Reference/ManPages/man3/dispatch_time.3.html]
 */

    const_time_now = ULL2NUM(DISPATCH_TIME_NOW);
    const_time_forever = ULL2NUM(DISPATCH_TIME_FOREVER);
    rb_define_const(mDispatch, "TIME_NOW", const_time_now);
    rb_define_const(mDispatch, "TIME_FOREVER", const_time_forever);
    
/* Constants for future reference */
    selClose = sel_registerName("close");
    assert(selClose != NULL);
}
