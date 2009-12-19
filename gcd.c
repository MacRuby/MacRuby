/*
 * MacRuby API for Grand Central Dispatch.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 * 
 * Copyright (C) 2009, Apple Inc. All rights reserved.
 */

#include "ruby.h"

#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1060

#define GCD_BLOCKS_COPY_DVARS 1

#include <dispatch/dispatch.h>
#include "ruby/intern.h"
#include "ruby/node.h"
#include "ruby/io.h"
#include "objc.h"
#include "id.h"
#include "vm.h"

// TODO: These structures need to be wrapped in a Data struct,
// otherwise there are crashes when one tries to add an instance
// variable to a queue. (Not that that is a good idea.)

/*
 *
 *  Grand Central Dispatch (GCD) is a novel approach to multicore computing
 *  that is built into Mac OS X version 10.6 Snow Leopard. In particular, GCD
 *  shifts responsibility for managing threads and their execution from
 *  applications to the operating system. This allows programmers to easily
 *  refactor their programs into small chunks of independent work, which GCD
 *  then schedules onto per-process thread pools.  Because GCD knows the load
 *  across the entire system, it ensures the resulting programs perform
 *  optimally on a wide range of hardware.
 * 
 *  GCD is built on a highly-efficient multicore engine accessed via a C API
 *  providing four primary abstractions, which are wrapped in this MacRuby
 *  implementation:
 *    ▪ block objects
 *    ▪ dispatch queues
 *    ▪ synchronization services
 *    ▪ event sources
 * 
 *  For more information, see the dispatch(3) man page.  
 *
*/

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

typedef struct {
    struct RBasic basic;
    int suspension_count;
    dispatch_source_t source;
    dispatch_source_type_t type;
    rb_vm_block_t *event_handler;
    rb_vm_block_t *cancel_handler;
} rb_source_t;

#define RSource(val) ((rb_source_t*)val)

typedef struct {
    struct RBasic basic;
    dispatch_semaphore_t sem;
    long count;
} rb_semaphore_t;

#define RSemaphore(val) ((rb_semaphore_t*)val)

static VALUE mDispatch;

// queue stuff
static VALUE cQueue;
static VALUE qMain;
static VALUE qHighPriority;
static VALUE qDefaultPriority;
static VALUE qLowPriority;
static ID high_priority_id;
static ID low_priority_id;
static ID default_priority_id;

static VALUE cGroup;
static VALUE cSource;
static VALUE cSemaphore;

static inline rb_vm_block_t *
given_block(void)
{
    rb_vm_block_t *block = rb_vm_current_block();
    if (block == NULL) {
        rb_raise(rb_eArgError, "block not given");
    }
    return block;
}

static inline void
Check_Queue(VALUE object)
{
    if (CLASS_OF(object) != cQueue) {
	rb_raise(rb_eArgError, "expected Queue object, but got %s",
		rb_class2name(CLASS_OF(object)));
    }
}

static inline void
Check_Group(VALUE object)
{
    if (CLASS_OF(object) != cGroup) {
	rb_raise(rb_eArgError, "expected Group object, but got %s",
		rb_class2name(CLASS_OF(object)));
    }
}

#define SEC2NSEC_UINT64(sec) (uint64_t)(sec * NSEC_PER_SEC)
#define SEC2NSEC_INT64(sec) (int64_t)(sec * NSEC_PER_SEC)
#define TIMEOUT_MAX (1.0 * INT64_MAX / NSEC_PER_SEC)

static inline uint64_t
rb_num2nsec(VALUE num)
{
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
 *  A dispatch queue is a FIFO queue to which you can submit tasks in the form of a block. 
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
 *  The three priority levels are: <code>:low</code>, <code>:default</code>, 
 *  <code>:high</code>, corresponding to the DISPATCH_QUEUE_PRIORITY_HIGH, 
 *  DISPATCH_QUEUE_PRIORITY_DEFAULT, and DISPATCH_QUEUE_PRIORITY_LOW (detailed
 *  in the dispatch_queue_create(3) man page). The Grand Central thread dispatcher
 *  will perform actions submitted to the high priority queue before any actions 
 *  submitted to the default or low queues, and will only perform actions on the 
 *  low queues if there are no actions queued on the high or default queues.
 *
 *     gcdq = Dispatch::Queue.concurrent(:high)
 *     5.times { gcdq.dispatch { print 'foo' } }
 *     gcdq_2 = Dispatch::Queue.concurrent(:low)
 *     gcdq.dispatch(:low) { print 'bar' }  # will always print 'foofoofoofoofoobar'.
 *
 */
static VALUE
rb_queue_get_concurrent(VALUE klass, SEL sel, int argc, VALUE *argv)
{
    VALUE priority;
    rb_scan_args(argc, argv, "01", &priority);
    if (!NIL_P(priority)) {
	ID id = rb_to_id(priority);
	if (id == high_priority_id) {
	    return qHighPriority;
	}
	else if (id == low_priority_id) {
	    return qLowPriority;
	}
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
 *  A dispatch is a FIFO queue to which you can submit tasks in the form of a block. 
 *  Blocks submitted to dispatch queues are executed on a pool of threads fully 
 *  managed by the system. Dispatched tasks execute one at a time in FIFO order.
 *  GCD takes take of using multiple cores effectively and better accommodate 
 *  the needs of all running applications, matching them to the 
 *  available system resources in a balanced fashion.
 *
 *  Use this kind of GCD queue to ensure that tasks execute in a predictable order.
 *  It’s a good practice to identify a specific purpose for each serial queue, 
 *  such as protecting a resource or synchronizing key processes.
 *  Create as many of them as necessary - serial queues are extremely lightweight 
 *  (with a total memory footprint of less than 300 bytes); however, remember to 
 *  use concurrent queues if you need to perform idempotent tasks in parallel.
 *  Dispatch queues need to be labeled and thereofore you need to pass a name 
 *  to create your queue. By convention, labels are in reverse-DNS style.
 *
 *     gcdq = Dispatch::Queue.new('org.macruby.gcd.example')
 *     gcdq.dispatch { p 'foo' }
 *     gcdq.dispatch { p 'bar' }
 *     gcdq.dispatch(true) {} 
 *
 */
 
static VALUE 
rb_queue_initialize(VALUE self, SEL sel, VALUE name)
{
    StringValue(name);

    rb_queue_t *queue = RQueue(self);
    queue->suspension_count = 0;
    queue->should_release_queue = 1;
    queue->queue = dispatch_queue_create(RSTRING_PTR(name), NULL);
    dispatch_retain(queue->queue);
    return self;
}

static IMP rb_queue_finalize_super;

static void
rb_queue_finalize(void *rcv, SEL sel)
{
    rb_queue_t *queue = RQueue(rcv);
    while (queue->suspension_count < 0) {
        dispatch_resume(queue->queue);
        queue->suspension_count--;
    }
    if (queue->should_release_queue) {
        dispatch_release(queue->queue);
        queue->should_release_queue = 0;
    }
    if (rb_queue_finalize_super != NULL) {
        ((void(*)(void *, SEL))rb_queue_finalize_super)(rcv, sel);
    }
}

static VALUE
rb_queue_dispatch_body(VALUE data)
{
    GC_RELEASE(data);
    rb_vm_block_t *b = (rb_vm_block_t *)data;
    return rb_vm_block_eval(b, 0, NULL);
}

static void
rb_queue_dispatcher(void *data)
{
    assert(data != NULL);
    rb_rescue2(rb_queue_dispatch_body, (VALUE)data, NULL, 0,
	    rb_eStandardError);
}

static rb_vm_block_t *
rb_dispatch_prepare_block(rb_vm_block_t *block)
{
    rb_vm_set_multithreaded(true);
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
 *    gcdq.dispatch(synchronicity) { i = 42 }
 *
 *  Yields the passed block synchronously or asynchronously.
 *  By default the block is yielded asynchronously:
 *  
 *     gcdq = Dispatch::Queue.new('doc')
 *     i = 42
 *     gcdq.dispatch { i = 42 }
 *     while i == 0 do; end
 *     i #=> 42
 *
 *   If you want to yield the block synchronously pass <code>true</code> as the
 *   argument.
 *
 *     gcdq = Dispatch::Queue.new('doc')
 *     i = 42
 *     gcdq.dispatch(true) { i = 42 }
 *     i #=> 42
 *
 */

static VALUE
rb_queue_dispatch_async(VALUE self, SEL sel, int argc, VALUE *argv)
{
    rb_vm_block_t *block = given_block();
    block = rb_dispatch_prepare_block(block);

    VALUE group;
    rb_scan_args(argc, argv, "01", &group);

    if (group != Qnil) {
	Check_Group(group);
	dispatch_group_async_f(RGroup(group)->group, RQueue(self)->queue,
		(void *)block, rb_queue_dispatcher);
    }
    else {
	dispatch_async_f(RQueue(self)->queue, (void *)block,
		rb_queue_dispatcher);
    }

    return Qnil;
}

static VALUE
rb_queue_dispatch_sync(VALUE self, SEL sel)
{
    rb_vm_block_t *block = given_block();
    block = rb_dispatch_prepare_block(block);

    dispatch_sync_f(RQueue(self)->queue, (void *)block,
	    rb_queue_dispatcher);

    return Qnil;
}

/* 
 *  call-seq:
 *    gcdq.after(time) { block }
 *
 *  Runs the passed block after the given time (in seconds).
 *  
 *     gcdq.after(0.5) { puts 'wait is over :)' }
 *
 */
static VALUE
rb_queue_dispatch_after(VALUE self, SEL sel, VALUE sec)
{
    dispatch_time_t offset = rb_num2timeout(sec);
    rb_vm_block_t *block = given_block();
    block = rb_dispatch_prepare_block(block);

    dispatch_after_f(offset, RQueue(self)->queue, (void *)block,
	    rb_queue_dispatcher);

    return Qnil;
}

/* 
 *  call-seq:
 *    gcdq.apply(amount_size) { block }
 *
 *  Runs a block and yields it as many times as asked
 *  
 *     gcdq = Dispatch::Queue.new('foo')
 *     i = 0
 *     gcdq.apply(10) { i += 1 }
 *     i #=> 10
 *
 */
 
static void
rb_queue_applier(void *data, size_t ii)
{
    assert(data != NULL);
    rb_vm_block_t *block = rb_vm_uncache_or_dup_block((rb_vm_block_t *)data);
#if !GCD_BLOCKS_COPY_DVARS
    rb_vm_block_make_detachable_proc(block);
#endif
    VALUE num = SIZET2NUM(ii);
    rb_vm_block_eval(block, 1, &num);
}

static VALUE
rb_queue_apply(VALUE self, SEL sel, VALUE n)
{
    rb_vm_block_t *block = given_block();
    block = rb_dispatch_prepare_block(block);

    dispatch_apply_f(NUM2SIZET(n), RQueue(self)->queue, (void *)block,
	    rb_queue_applier);

    GC_RELEASE(block);

    return Qnil;
}

/* 
 *  call-seq:
 *    gcdq.label -> str
 *
 *  Returns the label of the dispatch queue.
 *  
 *     gcdq = Dispatch::Queue.new('doc')
 *     gcdq.label #=> 'doc'
 *     gcdq = Dispatch::Queue.main
 *     gcdq.label #=> 'com.apple.main-thread'
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
 *    obj.resume!
 *
 *  Resumes a suspended dispatch object (group, source or queue).
 *  
 *     obj.suspend!
 *     obj.suspended?  #=> true
 *     obj.resume!
 *
 */
 
static VALUE
rb_dispatch_resume(VALUE self, SEL sel)
{
    rb_dispatch_obj_t *dobj = RDispatch(self);
    if (dobj->suspension_count > 0) {
        dobj->suspension_count--;
        dispatch_resume(dobj->obj);
    }
    return Qnil;
}

/* 
 *  call-seq:
 *    obj.suspend!
 *
 *  Suspends the operation of a dispatch object (group, source or queue).
 *  To resume operation, call <code>resume!</code>.
 *  
 *     gcdq = Dispatch::Queue.new('foo')
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
    dobj->suspension_count++;
    dispatch_suspend(dobj->obj);
    return Qnil;
}

/* 
 *  call-seq:
 *    obj.suspended?   => true or false
 *
 *  Returns <code>true</code> if <i>obj</i> is suspended.
 *  
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
 *  Returns a Queue group allowing for for aggregate synchronization.
 *  You can dispatch multiple blocks and track when they all complete, 
 *  even though they might run on different queues. 
 *  This behavior can be helpful when progress can’t be made until all 
 *  of the specified tasks are complete.
 *
 *  
 *     gcdg = Dispatch::Group.new
 *
 */

static VALUE
rb_group_initialize(VALUE self, SEL sel)
{
    RGroup(self)->group = dispatch_group_create();
    return self;
}

/* 
 *  call-seq:
 *    gcdg.notify { block }
 *
 *  Schedules a block to be called when a group of previously submitted dispatches
 *  have completed.
 *
 *
 *     gcdg = Dispatch::Group.new
 *     gcdg.notify { print 'bar' }
 *     gcdg.dispatch(Dispatch::Queue.concurrent) { print 'foo' }
 *     # prints 'foobar'
 */

static VALUE
rb_group_notify(VALUE self, SEL sel, VALUE target)
{
    rb_vm_block_t *block = given_block();
    block = rb_dispatch_prepare_block(block);

    Check_Queue(target);

    dispatch_group_notify_f(RGroup(self)->group, RQueue(target)->queue,
	    (void *)block, rb_queue_dispatcher);

    return Qnil;
}

/* 
 *  call-seq:
 *    grp.wait(timeout=nil)     => true or false
 *
 *  Waits until all the blocks associated with the <code>grp</code> have 
 *  finished executing or until the specified <code>timeout</code> has elapsed.
 *  The function will return <code>true</code> if the group became empty within 
 *  the specified amount of time and will return <code>false</code> otherwise.
 *  If the supplied timeout is nil, the function will wait indefinitely until 
 *  the specified group becomes empty, always returning true.
 *
 *     q = Dispatch::Queue.new('org.macruby.documentation')
 *     grp = Dispatch::Group.new
 *     grp.dispatch(q) { sleep 4 }
 *     grp.wait(5) # true
 */
static VALUE
rb_group_wait(VALUE self, SEL sel, int argc, VALUE *argv)
{
    VALUE num;
    rb_scan_args(argc, argv, "01", &num);
    return dispatch_group_wait(RGroup(self)->group, rb_num2timeout(num))
     == 0	? Qtrue : Qfalse;
}

static VALUE rb_source_on_event(VALUE self, SEL sel);
static void rb_source_event_handler(void* sourceptr);

static VALUE
rb_source_alloc(VALUE klass, SEL sel)
{
    NEWOBJ(source, rb_source_t);
    OBJSETUP(source, klass, RUBY_T_NATIVE);
    source->suspension_count = 1;
    return (VALUE)source;
}

/* 
 *  call-seq:
 *    Dispatch::Source.for_reading(queue, io, &block)    =>  Dispatch::Source
 *
 *  Returns a Source that monitors the passed IO object for pending data. 
 *	When provided with a valid on_event handler, the source will call the 
 *	handler on the provided queue whenever it sees that data becomes available from the source's
 *	underlying file descriptor. If the on_event handler takes a parameter,
 *	that parameter will be an integer corresponding to an estimated number of
 *	bytes available to be read. See the dispatch_source_create(3) manpage for details.
 *  All Sources start out suspended; in order to activate them, call <code>resume!</code>.
 *	If for_reading is given a block, the block shall be registered as the 
 *  source's event handler.
 *
 *  
 *     file = File.new('testfile')
 *	   queue = Queue.new('org.macruby.documentation')
 *	   reader = Source.for_reading(queue, file) do { |x| puts "#{x} bytes available"}
 *
 */

static VALUE
rb_source_new_for_reading(VALUE klass, SEL sel, VALUE queue, VALUE io)
{
    VALUE src = rb_source_alloc(klass, sel);
    io = rb_check_convert_type(io, T_FILE, "IO", "to_io");
    Check_Queue(queue);
    RSource(src)->source = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, 
	    ExtractIOStruct(io)->fd, 0, RQueue(queue)->queue);
    RSource(src)->type = DISPATCH_SOURCE_TYPE_READ;

    if (rb_block_given_p()) {
	rb_source_on_event(src, 0);
    }

    return src;
}

static VALUE
rb_source_new_for_writing(VALUE klass, SEL sel, VALUE queue, VALUE io)
{
    VALUE src = rb_source_alloc(klass, sel);
    io = rb_check_convert_type(io, T_FILE, "IO", "to_io");
    Check_Queue(queue);
    RSource(src)->source = dispatch_source_create(DISPATCH_SOURCE_TYPE_WRITE, 
	    ExtractIOStruct(io)->fd, 0, RQueue(queue)->queue);
    RSource(src)->type = DISPATCH_SOURCE_TYPE_WRITE;
    
    if (rb_block_given_p()) {
        rb_source_on_event(src, 0);
    }
    
    return src;
}

static VALUE
rb_source_new_timer(VALUE klass, SEL sel, int argc, VALUE* argv)
{
    dispatch_time_t start_time;
    VALUE queue = Qnil, interval = Qnil, delay = Qnil, leeway = Qnil;
    rb_scan_args(argc, argv, "31", &queue, &delay, &interval, &leeway);
    Check_Queue(queue);
    if (NIL_P(leeway)) {
        leeway = INT2FIX(0);
    }
    if (NIL_P(delay)) {
        start_time = DISPATCH_TIME_NOW;
    }
    else {
        start_time = rb_num2timeout(delay);
    }
    const uint64_t dispatch_interval = rb_num2nsec(interval);
    const uint64_t dispatch_leeway = rb_num2nsec(leeway);

    VALUE src = rb_source_alloc(klass, sel);
    RSource(src)->source = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,
	    0, 0, RQueue(queue)->queue);
    RSource(src)->type = DISPATCH_SOURCE_TYPE_TIMER;
    dispatch_source_set_timer(RSource(src)->source, start_time,
	    dispatch_interval, dispatch_leeway);
    
    if (rb_block_given_p()) {
	rb_source_on_event(src, 0);
    }
    
    return src;
}

static inline bool 
source_type_takes_parameters(dispatch_source_type_t t)
{
    return ((t == DISPATCH_SOURCE_TYPE_READ)    || 
            (t == DISPATCH_SOURCE_TYPE_SIGNAL)  || 
            (t == DISPATCH_SOURCE_TYPE_TIMER)   || 
            (t == DISPATCH_SOURCE_TYPE_PROC));
}

static void
rb_source_event_handler(void* sourceptr)
{
    assert(sourceptr != NULL);
    rb_source_t *source = RSource(sourceptr);
    rb_vm_block_t *the_block = source->event_handler;
    if (source_type_takes_parameters(source->type)
	    && the_block->arity.min == 1) {
        VALUE data = UINT2NUM(dispatch_source_get_data(source->source));
        rb_vm_block_eval(the_block, 1, &data);
    }
    else {
        rb_vm_block_eval(the_block, 0, NULL);
    }
}

static VALUE
rb_source_on_event(VALUE self, SEL sel)
{
    rb_source_t *src = RSource(self);
    rb_vm_block_t *block = given_block();
    GC_WB(&src->event_handler, block);
    dispatch_set_context(src->source, (void *)self); // retain this?
    dispatch_source_set_event_handler_f(src->source, rb_source_event_handler);
    return Qnil;
}

static void
rb_source_cancel_handler(void *source)
{
    assert(source != NULL);
    rb_vm_block_t *the_block = RSource(source)->cancel_handler;
    rb_vm_block_eval(the_block, 0, NULL);
}

static VALUE
rb_source_on_cancellation(VALUE self, SEL sel)
{
    rb_source_t *src = RSource(self);
    rb_vm_block_t *block = given_block();
    GC_WB(&src->cancel_handler, block);
    dispatch_set_context(src->source, (void*)self); // retain this?
    dispatch_source_set_cancel_handler_f(src->source, rb_source_cancel_handler);
    return Qnil;
}

static VALUE
rb_source_cancel(VALUE self, SEL sel)
{
    dispatch_source_cancel(RSource(self)->source);
    return Qnil;
}

static VALUE
rb_source_cancelled_p(VALUE self, SEL sel)
{
    return dispatch_source_testcancel(RSource(self)->source) ? Qtrue : Qfalse;
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

static VALUE
rb_semaphore_wait(VALUE self, SEL sel, int argc, VALUE *argv)
{
    VALUE num;
    rb_scan_args(argc, argv, "01", &num);
    return dispatch_semaphore_wait(RSemaphore(self)->sem, rb_num2timeout(num))
     == 0	? Qtrue : Qfalse;
}

static VALUE
rb_semaphore_signal(VALUE self, SEL sel)
{
    return dispatch_semaphore_signal(RSemaphore(self)->sem)
     == 0	? Qtrue : Qfalse;
}

static IMP rb_semaphore_finalize_super;

static void
rb_semaphore_finalize(void *rcv, SEL sel)
{
    if (RSemaphore(rcv)->sem != NULL) {
	// We must re-equilibrate the semaphore count before releasing it,
	// otherwise GCD will violently crash the program by an assertion.
	while (dispatch_semaphore_signal(RSemaphore(rcv)->sem) != 0) { }
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
    mDispatch = rb_define_module("Dispatch");

    cQueue = rb_define_class_under(mDispatch, "Queue", rb_cObject);    
    rb_objc_define_method(*(VALUE *)cQueue, "alloc", rb_queue_alloc, 0);
    rb_objc_define_method(*(VALUE *)cQueue, "concurrent",
	    rb_queue_get_concurrent, -1);
    rb_objc_define_method(*(VALUE *)cQueue, "current", rb_queue_get_current, 0);
    rb_objc_define_method(*(VALUE *)cQueue, "main", rb_queue_get_main, 0);
    rb_objc_define_method(cQueue, "initialize", rb_queue_initialize, 1);
    rb_objc_define_method(cQueue, "apply", rb_queue_apply, 1);
    rb_objc_define_method(cQueue, "async", rb_queue_dispatch_async, -1);
    rb_objc_define_method(cQueue, "sync", rb_queue_dispatch_sync, 0);
    rb_objc_define_method(cQueue, "after", rb_queue_dispatch_after, 1);
    rb_objc_define_method(cQueue, "label", rb_queue_label, 0);
    rb_objc_define_method(cQueue, "to_s", rb_queue_label, 0);
    rb_objc_define_method(cQueue, "resume!", rb_dispatch_resume, 0);
    rb_objc_define_method(cQueue, "suspend!", rb_dispatch_suspend, 0);
    rb_objc_define_method(cQueue, "suspended?", rb_dispatch_suspended_p, 0);
    
    rb_queue_finalize_super = rb_objc_install_method2((Class)cQueue,
	    "finalize", (IMP)rb_queue_finalize);
    
    qHighPriority = rb_queue_from_dispatch(dispatch_get_global_queue(
		DISPATCH_QUEUE_PRIORITY_HIGH, 0), true);
    qDefaultPriority = rb_queue_from_dispatch(dispatch_get_global_queue(
		DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), true);
    qLowPriority = rb_queue_from_dispatch(dispatch_get_global_queue(
		DISPATCH_QUEUE_PRIORITY_LOW, 0), true);
    
    qMain = rb_queue_from_dispatch(dispatch_get_main_queue(), true);
    rb_objc_define_method(rb_singleton_class(qMain), "run", rb_main_queue_run,
	    0);
    
    cGroup = rb_define_class_under(mDispatch, "Group", rb_cObject);
    rb_objc_define_method(*(VALUE *)cGroup, "alloc", rb_group_alloc, 0);
    rb_objc_define_method(cGroup, "initialize", rb_group_initialize, 0);
    rb_objc_define_method(cGroup, "notify", rb_group_notify, 1);
    rb_objc_define_method(cGroup, "on_completion", rb_group_notify, 1);
    rb_objc_define_method(cGroup, "wait", rb_group_wait, -1);
    
    cSource = rb_define_class_under(mDispatch, "Source", rb_cObject);
    rb_objc_define_method(*(VALUE *)cSource, "alloc", rb_source_alloc, 0);
    rb_undef_method(*(VALUE *)cSource, "new");
    rb_objc_define_method(*(VALUE *)cSource, "for_reading", rb_source_new_for_reading, 2);
    rb_objc_define_method(*(VALUE *)cSource, "for_writing", rb_source_new_for_writing, 2);
    #if 0 // TODO: Decide if we want to include these
    //rb_objc_define_method(*(VALUE *)cSource, "for_process", rb_source_new_for_process, 2);
    //rb_objc_define_method(*(VALUE *)cSource, "for_vnode", rb_source_new_for_vnode, 2)
    //rb_objc_define_method(*(VALUE *)cSource, "custom", rb_source_new_custom, 2);
    //rb_objc_define_method(*(VALUE *)cSource, "for_mach", rb_source_new_for_mach, 3);
    //rb_objc_define_method(*(VALUE *)cSource, "for_signal", rb_source_new_for_signal, 2),
    #endif
    rb_objc_define_method(*(VALUE *)cSource, "timer", rb_source_new_timer, -1);
    rb_objc_define_method(cSource, "on_event", rb_source_on_event, 0);
    rb_objc_define_method(cSource, "on_cancel", rb_source_on_cancellation, 0);
    rb_objc_define_method(cSource, "cancelled?", rb_source_cancelled_p, 0);
    rb_objc_define_method(cSource, "cancel!", rb_source_cancel, 0);
    rb_objc_define_method(cSource, "resume!", rb_dispatch_resume, 0);
    rb_objc_define_method(cSource, "suspend!", rb_dispatch_suspend, 0);
    rb_objc_define_method(cSource, "suspended?", rb_dispatch_suspended_p, 0);

    cSemaphore = rb_define_class_under(mDispatch, "Semaphore", rb_cObject);
    rb_objc_define_method(*(VALUE *)cSemaphore, "alloc", rb_semaphore_alloc, 0);
    rb_objc_define_method(cSemaphore, "initialize", rb_semaphore_init, 1);
    rb_objc_define_method(cSemaphore, "wait", rb_semaphore_wait, -1);
    rb_objc_define_method(cSemaphore, "signal", rb_semaphore_signal, 0);

    rb_queue_finalize_super = rb_objc_install_method2((Class)cSemaphore,
	    "finalize", (IMP)rb_semaphore_finalize);

    rb_define_const(mDispatch, "TIME_NOW", ULL2NUM(DISPATCH_TIME_NOW));
    rb_define_const(mDispatch, "TIME_FOREVER", ULL2NUM(DISPATCH_TIME_FOREVER));
}

#else

void
Init_PreGCD(void)
{
    // Do nothing...
}

void
Init_Dispatch(void)
{
    // Do nothing...
}

#endif
