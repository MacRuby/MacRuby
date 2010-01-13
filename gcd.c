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
#include <unistd.h>
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
 *  that is built into Mac OS X version 10.6 Snow Leopard, and available as
 *  open source via the libdispatch project. In particular, GCD
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
    dispatch_source_type_t type; // remove?
    rb_vm_block_t *event_handler;
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
static VALUE cFileSource;
static VALUE cTimer;
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
 *  The three priority levels are: <code>:low</code>, <code>:default</code>, 
 *  <code>:high</code>, corresponding to the DISPATCH_QUEUE_PRIORITY_HIGH, 
 *  DISPATCH_QUEUE_PRIORITY_DEFAULT, and DISPATCH_QUEUE_PRIORITY_LOW (detailed
 *  in the dispatch_queue_create(3) man page). The GCD thread dispatcher
 *  will perform actions submitted to the high priority queue before any actions 
 *  submitted to the default or low queues, and will only perform actions on the 
 *  low queues if there are no actions queued on the high or default queues.
 *
 *     gcdq = Dispatch::Queue.concurrent(:high)
 *     5.times { gcdq.async { print 'doc' } }
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
 *  A dispatch is a FIFO queue to which you can submit tasks via a block. 
 *  Blocks submitted to dispatch queues are executed on a pool of threads fully 
 *  managed by the system. Dispatched tasks execute one at a time in FIFO order.
 *  GCD takes take of using multiple cores effectively to better accommodate 
 *  the needs of all running applications, matching them to the 
 *  available system resources in a balanced fashion.
 *
 *  Use serial GCD queues to ensure that tasks execute in a predictable order.
 *  It’s a good practice to identify a specific purpose for each serial queue, 
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
rb_queue_initialize(VALUE self, SEL sel, VALUE name)
{
    StringValue(name);

    rb_queue_t *queue = RQueue(self);
    queue->suspension_count = 0;
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
 *    gcdq.async(group=nil) { @i = 42 }
 *
 *  Yields the passed block asynchronously via dispatch_async(3):
 *  
 *     gcdq = Dispatch::Queue.new('doc')
 *     @i = 42
 *     gcdq.async { @i = 42 }
 *     while @i == 0 do; end
 *     p @i #=> 42
 *
 *   If a group is specified, the dispatch will be associated with that group
 *   via dispatch_group_async(3)
 *
 *     gcdq = Dispatch::Queue.new('doc')
 *     gcdg = Dispatch::Group.new
 *     @i = 42
 *     gcdq.async(g) { @i = 42 }
 *     g.wait
 *     p @i #=> 42
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

/* 
 *  call-seq:
 *    gcdq.sync { @i = 42 }
 *
 *  Yields the passed block synchronously via dispatch_sync(3):
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
 *    gcdq.apply(count) { |index| block }
 *
 *  Runs a block count number of times in parallel via dispatch_apply(3),
 *  passing in an index and waiting until all of them are done
 *  
 *     gcdq = Dispatch::Queue.new('doc')
 *     @result = []
 *     gcdq.apply(5) {|i| @result[i] = i*i }
 *     p @result  #=> [0, 1, 4, 9, 16, 25]
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
 *  Returns the label of the dispatch queue (aliased to 'to_s')
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
 *    obj.suspend!
 *
 *  Suspends the operation of a dispatch object (queue or source).
 *  To resume operation, call <code>resume!</code>.
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
    dobj->suspension_count++;
    dispatch_suspend(dobj->obj);
    return Qnil;
}

/* 
 *  call-seq:
 *    obj.resume!
 *
 *  Resumes the operation of a dispatch object (queue or source).
 *  To suspend operation, call <code>suspend!</code>.
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
    if (dobj->suspension_count > 0) {
        dobj->suspension_count--;
        dispatch_resume(dobj->obj);
    }
    return Qnil;
}

/* 
 *  call-seq:
 *    obj.suspended?   => true or false
 *
 *  Returns <code>true</code> if <i>obj</i> is suspended.
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
 *  Returns a Group allowing for aggregate synchronization.
 *  You can dispatch multiple blocks and track when they all complete, 
 *  even though they might run on different queues. 
 *  This behavior can be helpful when progress can’t be made until all 
 *  of the specified tasks are complete.
 *  
 *     gcdg = Dispatch::Group.new
 *
 */

static VALUE
rb_group_initialize(VALUE self, SEL sel)
{
    RGroup(self)->group = dispatch_group_create();
    assert(RGroup(self)->group != NULL);
    
    return self;
}

/* 
 *  call-seq:
 *    grp.notify { block }
 *
 *  Asynchronously schedules a block to be called when the previously
 *  submitted dispatches for that group have completed.
 *
 *     gcdq = Dispatch::Queue.new('doc')
 *     grp = Dispatch::Group.new
 *     gcdq.async(grp) { print 'doc' }
 *     grp.notify { print 'bar' } #=> foobar
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
     == 0	? Qtrue : Qfalse;
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

enum SOURCE_TYPE_ENUM
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
};

static inline dispatch_source_type_t
rb_num2source_type(VALUE num)
{
    enum SOURCE_TYPE_ENUM value = NUM2LONG(num);
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
rb_is_file_source_type(VALUE num)
{
    enum SOURCE_TYPE_ENUM value = NUM2LONG(num);
    if (value == SOURCE_TYPE_READ || value == SOURCE_TYPE_VNODE 
        || value == SOURCE_TYPE_WRITE) {
        return YES;
    }
    return NO;
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
    VALUE param = (VALUE) source;
    rb_vm_block_t *the_block = source->event_handler;
    rb_vm_block_eval(the_block, 1, &param);
}

static VALUE
rb_source_on_event(VALUE self, SEL sel)
{
    rb_source_t *src = RSource(self);
    rb_vm_block_t *block = given_block();
    GC_WB(&src->event_handler, block);
    GC_RETAIN(self);
    dispatch_set_context(src->source, (void *)self); // retain this?
    dispatch_source_set_event_handler_f(src->source, rb_source_event_handler);
    return Qnil;
}

static VALUE
rb_source_setup(VALUE self, SEL sel,
    VALUE type, VALUE handle, VALUE mask, VALUE queue)
{
    Check_Queue(queue);
    rb_source_t *src = RSource(self);    
    src->type = rb_num2source_type(type);
    assert(src->type != NULL);
    uintptr_t c_handle = NUM2UINT(handle);
    unsigned long c_mask = NUM2LONG(mask);
    dispatch_queue_t c_queue = RQueue(queue)->queue;
    src->source = dispatch_source_create(src->type, c_handle, c_mask, c_queue);
    assert(src->source != NULL);

    if (rb_block_given_p()) {
        rb_source_on_event(self, 0);
    } else {
        rb_raise(rb_eArgError, "No event handler for Dispatch::Source.");
    }
    return self;
}

/* 
 *  call-seq:
 *    Dispatch::Source.new(type, handle, mask, queue) {|src| block}
 *     => Dispatch::Source
 *
 *  Returns a Source used to monitor a variety of system objects and events
 *  including file descriptors, processes, virtual filesystem nodes, signal
 *  delivery and timers.
 *  When a state change occurs, the dispatch source will submit its event
 *  handler block to its target queue, with the source as a parameter.
 *  
 *     gcdq = Dispatch::Queue.new('doc')
 *     src = Dispatch::Source.new(Dispatch::Source::DATA_ADD, 0, 0, gcdq) do |s|
 *       puts "Fired with #{s.data}"
 *     end
 *
 *   Unlike with the C API, Dispatch::Source objects start off resumed
 *   (since the event handler has already been set).
 *   
 *     src.suspended? #=? false
 *     src.merge(0)
 *     gcdq.sync { } #=> Fired!
 *  
 */

static VALUE
rb_source_init(VALUE self, SEL sel,
    VALUE type, VALUE handle, VALUE mask, VALUE queue)
{
    rb_source_setup(self, sel, type, handle, mask, queue);
    rb_dispatch_resume(self, 0);
    return self;
}

/* 
 *  call-seq:
 *    src.handle => Number
 *
 *  Returns the underlying handle to the dispatch source (i.e. file descriptor,
 *  process identifer, etc.). For Ruby, this must be representable as a Number.
 *  
 *     gcdq = Dispatch::Queue.new('doc')
 *     src = Dispatch::Source.new(Dispatch::Source::DATA_ADD, 0, 0, gcdq) { }
 *     puts src.handle #=> 0
 */

static VALUE
rb_source_get_handle(VALUE self, SEL sel)
{
    return LONG2NUM(dispatch_source_get_handle(RSource(self)->source));
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
 *     src.merge(1)
 *     gcdq.sync { } #=> 1
 */

static VALUE
rb_source_get_data(VALUE self, SEL sel)
{
    return LONG2NUM(dispatch_source_get_data(RSource(self)->source));
}

/* 
 *  call-seq:
 *    src.merge(number)
 *
 *  Intended only for use with the Dispatch::Source::DATA_ADD and
 *  Dispatch::Source::DATA_OR source types, calling this function will
 *  atomically ADD or logical OR the count into the source's data, and 
 * trigger delivery of the source's event handler.
 *  
 *     gcdq = Dispatch::Queue.new('doc')
 *     @sum = 0
 *     src = Dispatch::Source.new(Dispatch::Source::DATA_ADD, 0, 0, gcdq) do |s|
 *       @sum += s.data # safe since always serialized
 *     end
 *     src.merge(1)
 *     src.merge(3)
 *     gcdq.sync { }
 *     puts @sum #=> 4
 */

static VALUE
rb_source_merge(VALUE self, SEL sel, VALUE data)
{
    dispatch_source_merge_data(RSource(self)->source, NUM2INT(data));
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

static IMP rb_source_finalize_super;

static void
rb_source_finalize(void *rcv, SEL sel)
{
    rb_source_t *src = RSource(rcv);
    if (src->source != NULL) {
        while (src->suspension_count < 0) {
            dispatch_resume(src->source);
            src->suspension_count--;
        }
        dispatch_release(src->source);
    }
    if (rb_source_finalize_super != NULL) {
        ((void(*)(void *, SEL))rb_source_finalize_super)(rcv, sel);
    }
}


static void
rb_source_close_handler(void* longptr)
{
    long filedes = (long)longptr;
    close((int)filedes);
}

/* 
 *  call-seq:
 *    Dispatch::FileSource.new(type, handle, mask, queue) 
 *     {|src| block} => Dispatch::Source
 *
 *  Like Dispatch::Source.new, except that it automatically creates
 *  a cancel handler that will close(2) that file descriptor
 *  
 *     gcdq = Dispatch::Queue.new('doc')
 *     file = File.open("/etc/passwd", r)
 *     src = Dispatch::Source.new_close_file(Dispatch::Source::READ,
 *           file.to_i, 0, gcdq) { |s| s.cancel! }
 *
 *   The type must be one of:
 *      - Dispatch::Source::READ
 *      - Dispatch::Source::WRITE
 *      - Dispatch::Source::VNODE
 *
 *   This is the only way to set the cancel_handler, since in MacRuby
 *   sources start off resumed. This is preferred to closing the file
 *   yourself, as the cancel handler is guaranteed to only run once.
 *
 *   NOTE: If you do NOT want the file descriptor closed, use Dispatch::Source.
 *  
 */

static VALUE
rb_source_file_init(VALUE self, SEL sel,
    VALUE type, VALUE handle, VALUE mask, VALUE queue)
{
    if (rb_is_file_source_type(type) == NO) {
        rb_raise(rb_eArgError, "%ld not a file source type", NUM2LONG(type));
    }
    rb_source_setup(self, sel, type, handle, mask, queue);
    rb_source_t *src = RSource(self);            
    long fildes = NUM2INT(type);
    dispatch_set_context(src->source, (void*)fildes);
    dispatch_source_set_cancel_handler_f(src->source, rb_source_close_handler);
    rb_dispatch_resume(self, 0);
    return self;
}

/* 
 *  call-seq:
 *    Dispatch::Timer.new(delay, interval, leeway, queue) =>  Dispatch::Timer
 *
 *  Returns a Source that will submit the event handler block to
 *  the target queue after delay, repeated at interval, within leeway, via
 *  a call to dispatch_source_set_timer(3).
 *  A best effort attempt is made to submit the event handler block to the
 *  target queue at the specified time; however, actual invocation may occur at
 *  a later time even if the leeway is zero.
 *  
 *     gcdq = Dispatch::Queue.new('doc')
 *     timer = Dispatch::Timer.new(Time.now, 5, 0.1, gcdq)
 *
 */

static VALUE
rb_source_timer_init(VALUE self, SEL sel,
 VALUE delay, VALUE interval, VALUE leeway, VALUE queue)
{
    dispatch_time_t start_time;
    rb_source_t *src = RSource(self);
    Check_Queue(queue);
    
    rb_source_setup(self, sel, INT2FIX(SOURCE_TYPE_TIMER),
	    INT2FIX(0), INT2FIX(0), queue);

    if (NIL_P(leeway)) {
        leeway = INT2FIX(0);
    }
    if (NIL_P(delay)) {
        start_time = DISPATCH_TIME_NOW;
    }
    else {
        start_time = rb_num2timeout(delay);
    }

    dispatch_source_set_timer(src->source, start_time,
	    rb_num2nsec(interval), rb_num2nsec(leeway));
    rb_dispatch_resume(self, 0);
    return self;
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
 *  waiting and signalling
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
 *     gcdq.async { sleep 0.1; sema.signal } #=> false
 *     sema.wait
 *
 */
static VALUE
rb_semaphore_signal(VALUE self, SEL sel)
{
    return dispatch_semaphore_signal(RSemaphore(self)->sem)
     == 0	? Qtrue : Qfalse;
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
    
    rb_group_finalize_super = rb_objc_install_method2((Class)cGroup,
	    "finalize", (IMP)rb_group_finalize);
    
        
    cSource = rb_define_class_under(mDispatch, "Source", rb_cObject);
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
    rb_objc_define_method(cSource, "initialize", rb_source_init, 4);
    rb_objc_define_method(cSource, "cancelled?", rb_source_cancelled_p, 0);
    rb_objc_define_method(cSource, "cancel!", rb_source_cancel, 0);
    rb_objc_define_method(cSource, "resume!", rb_dispatch_resume, 0);
    rb_objc_define_method(cSource, "suspend!", rb_dispatch_suspend, 0);
    rb_objc_define_method(cSource, "suspended?", rb_dispatch_suspended_p, 0);
    rb_objc_define_method(cSource, "handle", rb_source_get_handle, 0);
    rb_objc_define_method(cSource, "mask", rb_source_get_mask, 0);
    rb_objc_define_method(cSource, "data", rb_source_get_data, 0);
    rb_objc_define_method(cSource, "<<", rb_source_merge, 1);    
    rb_source_finalize_super = rb_objc_install_method2((Class)cSource,
	    "finalize", (IMP)rb_source_finalize);

    cFileSource = rb_define_class_under(mDispatch, "FileSource", cSource);
    rb_objc_define_method(cFileSource, "initialize", rb_source_file_init, 4);
    
    cTimer = rb_define_class_under(mDispatch, "Timer", cSource);
    rb_objc_define_method(cTimer, "initialize", rb_source_timer_init, 4);

    cSemaphore = rb_define_class_under(mDispatch, "Semaphore", rb_cObject);
    rb_objc_define_method(*(VALUE *)cSemaphore, "alloc", rb_semaphore_alloc, 0);
    rb_objc_define_method(cSemaphore, "initialize", rb_semaphore_init, 1);
    rb_objc_define_method(cSemaphore, "wait", rb_semaphore_wait, -1);
    rb_objc_define_method(cSemaphore, "signal", rb_semaphore_signal, 0);

    rb_semaphore_finalize_super = rb_objc_install_method2((Class)cSemaphore,
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
