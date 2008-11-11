/*
 * Copyright (c) 2002-2008 Apple Inc. All rights reserved.
 *
 * @APPLE_APACHE_LICENSE_HEADER_START@
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * 
 * @APPLE_APACHE_LICENSE_HEADER_END@
 */

#ifndef __AUTO_ZONE__
#define __AUTO_ZONE__

#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <malloc/malloc.h>

__BEGIN_DECLS

typedef malloc_zone_t auto_zone_t;
    // an auto zone carries a little more state but can be cast into a malloc_zone_t

extern auto_zone_t *auto_zone_create(const char *name);
    // create an garbage collected zone.  Can be (theoretically) done more than once.
    // memory can be allocated by malloc_zone_malloc(result, size)
    // by default, this memory must be malloc_zone_free(result, ptr) as well (or generic free())

extern struct malloc_introspection_t auto_zone_introspection();
    // access the zone introspection functions independent of any particular auto zone instance.
    // this is used by tools to be able to introspect a zone in another process.
    // the introspection functions returned are required to do version checking on the zone.

/*********  External (Global) Use counting  ************/

extern void auto_zone_retain(auto_zone_t *zone, void *ptr);
extern unsigned int auto_zone_release(auto_zone_t *zone, void *ptr);
extern unsigned int auto_zone_retain_count(auto_zone_t *zone, const void *ptr);
    // All pointer in the auto zone have an explicit retain count
    // Objects will not be collected when the retain count is non-zero

/*********  Object information  ************/

extern const void *auto_zone_base_pointer(auto_zone_t *zone, const void *ptr);
    // return base of interior pointer  (or NULL).
extern boolean_t auto_zone_is_valid_pointer(auto_zone_t *zone, const void *ptr);
    // is this a pointer to the base of an allocated block?
extern size_t auto_zone_size(auto_zone_t *zone, const void *ptr);

/*********  Write-barrier   ************/

extern boolean_t auto_zone_set_write_barrier(auto_zone_t *zone, const void *dest, const void *new_value);
    // must be used when an object field/slot in the auto zone is set to another object in the auto zone
    // returns true if the dest was a valid target whose write-barrier was set

boolean_t auto_zone_atomicCompareAndSwap(auto_zone_t *zone, void *existingValue, void *newValue, void *volatile *location, boolean_t isGlobal, boolean_t issueBarrier);
    // Atomically update a location with a new GC value.  These use OSAtomicCompareAndSwapPtr{Barrier} with appropriate write-barrier interlocking logic.

extern void *auto_zone_write_barrier_memmove(auto_zone_t *zone, void *dst, const void *src, size_t size);
    // copy content from an arbitrary source area to an arbitrary destination area
    // marking write barrier if necessary

/*********  Statistics  ************/

typedef uint64_t auto_date_t;

typedef struct {
    auto_date_t     total_duration;
    auto_date_t     scan_duration;
    auto_date_t     enlivening_duration;
    auto_date_t     finalize_duration;
    auto_date_t     reclaim_duration;
} auto_collection_durations_t;

typedef struct {
    /* Memory usage */
    malloc_statistics_t malloc_statistics;
    /* GC stats */
    uint32_t            version;            // reserved - 0 for now
    /* When there is an array, 0 stands for full collection, 1 for generational */
    size_t              num_collections[2];
    boolean_t           last_collection_was_generational;
    size_t              bytes_in_use_after_last_collection[2];
    size_t              bytes_allocated_after_last_collection[2];
    size_t              bytes_freed_during_last_collection[2];
    // durations
    auto_collection_durations_t total[2];   // running total of each field
    auto_collection_durations_t last[2];    // most recent result
    auto_collection_durations_t maximum[2]; // on a per item basis, the max.  Thus, total != scan + finalize ...
} auto_statistics_t;

extern void auto_zone_statistics(auto_zone_t *zone, auto_statistics_t *stats);  // set version to 0

/*********  Garbage Collection   ************/

enum {
    AUTO_COLLECT_RATIO_COLLECTION        = (0 << 0), // run generational or full depending on applying AUTO_COLLECTION_RATIO
    AUTO_COLLECT_GENERATIONAL_COLLECTION = (1 << 0), // collect young objects. Internal only.
    AUTO_COLLECT_FULL_COLLECTION         = (2 << 0), // collect entire heap. Internal only.
    AUTO_COLLECT_EXHAUSTIVE_COLLECTION   = (3 << 0), // run full collections until object count stabilizes.
    AUTO_COLLECT_SYNCHRONOUS             = (1 << 2), // block caller until scanning is finished.
    AUTO_COLLECT_IF_NEEDED               = (1 << 3), // only collect if AUTO_COLLECTION_THRESHOLD exceeded.
};
typedef uint32_t auto_collection_mode_t;

enum {
    AUTO_LOG_COLLECTIONS = (1 << 1),        // log whenever a collection occurs
    AUTO_LOG_REGIONS = (1 << 4),            // log whenever a new region is allocated
    AUTO_LOG_UNUSUAL = (1 << 5),            // log unusual circumstances
    AUTO_LOG_WEAK = (1 << 6),               // log weak reference manipulation
    AUTO_LOG_ALL = (~0u),
    AUTO_LOG_NONE = 0
};
typedef uint32_t auto_log_mask_t;

enum {
    AUTO_HEAP_HOLES_SHRINKING       = 1,        // total size of holes is approaching zero
    AUTO_HEAP_HOLES_EXHAUSTED       = 2,        // all holes exhausted, will use hitherto unused memory in "subzone"
    AUTO_HEAP_SUBZONE_EXHAUSTED     = 3,        // will add subzone
    AUTO_HEAP_REGION_EXHAUSTED      = 4,        // no more subzones available, need to add region
    AUTO_HEAP_ARENA_EXHAUSTED       = 5,        // arena exhausted.  (64-bit only)
};
typedef uint32_t auto_heap_growth_info_t;

typedef struct auto_zone_cursor *auto_zone_cursor_t;
typedef void (*auto_zone_foreach_object_t) (auto_zone_cursor_t cursor, void (*op) (void *ptr, void *data), void* data);

typedef struct {
    uint32_t        version;                // reserved - 0 for now
    void            (*batch_invalidate) (auto_zone_t *zone, auto_zone_foreach_object_t foreach, auto_zone_cursor_t cursor, size_t cursor_size);
        // After unreached objects are found, collector calls this routine with internal context.
        // Typically, one enters a try block to call back into the collector with a function pointer to be used to
        // invalidate each object.  This amortizes the cost of the try block as well as allows the collector to use
        // efficient contexts.
    void            (*resurrect) (auto_zone_t *zone, void *ptr);
        // Objects on the garbage list may be assigned into live objects in an attempted resurrection.  This is not allowed.
        // This function, if supplied, is called for these objects to turn them into zombies.  The zombies may well hold
        // pointers to other objects on the garbage list.  No attempt is made to preserved these objects beyond this collection.
    const unsigned char* (*layout_for_address)(auto_zone_t *zone, void *ptr);
        // The collector assumes that the first word of every "object" is a class pointer.
        // For each class pointer discovered this function is called to return a layout, or NULL
        // if the object should be scanned conservatively.
        // The layout format is nibble pairs {skipcount, scancount}  XXX
    const unsigned char* (*weak_layout_for_address)(auto_zone_t *zone, void *ptr);
        // called once for each allocation encountered for which we don't know the weak layout
        // the callee returns a weak layout for the allocation or NULL if the allocation has no weak references.
    char*           (*name_for_address) (auto_zone_t *zone, vm_address_t base, vm_address_t offset);
        // if supplied, is used during logging for errors such as resurrections
    auto_log_mask_t log;
        // set to auto_log_mask_t bits as desired
    boolean_t       disable_generational;
        // if true, ignores requests to do generational GC.
    boolean_t       malloc_stack_logging;
        // if true, logs allocations for malloc stack logging.  Automatically set if MallocStackLogging{NoCompact} is set
    void            (*scan_external_callout)(void *context, void (*scanner)(void *context, void *start, void *end));
        // an external function that is passed a memory scanner entry point
        // if set, the function will be called during scanning so that the
        // function the collector supplies will be called on all external memory that might
        // have references.  Useful, for example, for green thread systems.
        
    void            (*will_grow)(auto_zone_t *zone, auto_heap_growth_info_t);
        // collector calls this when it is about to grow the heap.  Advise if memory was returned to the collector, or not.
        // if memory was returned, return 0 and the allocation will be attempted again, otherwise the heap will be grown.
    size_t          collection_threshold;
        // if_needed threshold: collector will initiate a collection after this number of bytes is allocated.
    size_t          full_vs_gen_frequency;
        // after full_vs_gen_frequency generational collections, a full collection will occur, if the if_needed threshold exceeded
} auto_collection_control_t;

extern auto_collection_control_t *auto_collection_parameters(auto_zone_t *zone);
    // FIXME: API is to get the control struct and slam it
    // sets a parameter that decides when callback gets called

extern void auto_collector_disable(auto_zone_t *zone);
extern void auto_collector_reenable(auto_zone_t *zone);
    // these two functions turn off/on the collector
    // default is on
    // use with great care.

extern boolean_t auto_zone_is_enabled(auto_zone_t *zone);
extern boolean_t auto_zone_is_collecting(auto_zone_t *zone);

extern void auto_collect(auto_zone_t *zone, auto_collection_mode_t mode, void *collection_context);
    // request a collection.  By default, the collection will occur only on the main thread.

extern void auto_collect_multithreaded(auto_zone_t *zone);
    // start a dedicated thread to do collections. The invalidate callback will subsequently be called from this new thread.

/*********  Object layout for compaction    ************/

// For compaction of the zone, we need to know for sure where are the pointers
// each object is assumed to have a class pointer as word 0 (the "isa")
// This layout information is also used for collection (for "tracing" pointers)

// Exact layout knowledge is also important for ignoring weak references

enum {
    AUTO_TYPE_UNKNOWN = -1,                                 // this is an error value
    AUTO_UNSCANNED = 1,
    AUTO_OBJECT = 2,
    AUTO_MEMORY_SCANNED = 0,                                // holds conservatively scanned pointers
    AUTO_MEMORY_UNSCANNED = AUTO_UNSCANNED,                 // holds unscanned memory (bits)
    AUTO_OBJECT_SCANNED = AUTO_OBJECT,                      // first word is 'isa', may have 'exact' layout info elsewhere
    AUTO_OBJECT_UNSCANNED = AUTO_OBJECT | AUTO_UNSCANNED,   // first word is 'isa', good for bits or auto_zone_retain'ed items
};
typedef int auto_memory_type_t;

extern auto_memory_type_t auto_zone_get_layout_type(auto_zone_t *zone, void *ptr);


extern void* auto_zone_allocate_object(auto_zone_t *zone, size_t size, auto_memory_type_t type, boolean_t initial_refcount_to_one, boolean_t clear);

extern void auto_zone_register_thread(auto_zone_t *zone);
    // threads that are using the auto collector are marked suspendable by storing a non-nil value
    // in their thread local storage, using an internal pthread_key_t.

extern void auto_zone_unregister_thread(auto_zone_t *zone);


// Weak references

// The collector maintains a weak reference system.
// Essentially, locations in which references are stored are registered along with the reference itself.
// The location should not be within scanned GC memory.
// After a collection, before finalization, all registered locations are examined and any containing references to
// newly discovered garbage will be "zeroed" and the registration cancelled.
//
// Reading values from locations must be done through the weak read function because there is a race with such
// reads and the collector having just determined that that value read is in fact otherwise garbage.
//
// The address of a callback block may be supplied optionally.  If supplied, if the location is zeroed, the callback
// block is queued to be called later with the arguments supplied in the callback block.  The same callback block both
// can and should be used as an aggregation point.  A table of weak locations could supply each registration with the
// same pointer to a callback block that will call that table if items are zerod.  The callbacks are made before
// finalization.  Note that only thread-safe operations may be performed by this callback.
//
// It is important to cancel all registrations before deallocating the memory containing locations or callback blocks.
// Cancellation is done by calling the registration function with a NULL "reference" parameter for that location.

typedef struct auto_weak_callback_block {
    struct auto_weak_callback_block *next;              // must be set to zero before first use
    void (*callback_function)(void *arg1, void *arg2);
    void *arg1;
    void *arg2;
} auto_weak_callback_block_t;

extern void auto_assign_weak_reference(auto_zone_t *zone, const void *value, void *const*location, auto_weak_callback_block_t *block);

// Read a weak-reference, informing the collector that it is now strongly referenced.
extern void* auto_read_weak_reference(auto_zone_t *zone, void **referrer);

extern void auto_zone_add_root(auto_zone_t *zone, void *address_of_root_ptr, void *value);

extern void auto_zone_root_write_barrier(auto_zone_t *zone, void *address_of_possible_root_ptr, void *value);


// Associative references.

// This informs the collector that an object A wishes to associate one or more secondary objects with object A's lifetime.
// This can be used to implement GC-safe associations that will neither cause uncollectable cycles, nor suffer the limitations
// of weak references.

extern void auto_zone_set_associative_ref(auto_zone_t *zone, void *object, void *key, void *value);
extern void *auto_zone_get_associative_ref(auto_zone_t *zone, void *object,  void *key);

/***** SPI ******/
    


extern void auto_zone_start_monitor(boolean_t force);
extern void auto_zone_set_class_list(int (*get_class_list)(void **buffer, int count));
extern unsigned int auto_zone_retain_count_no_lock(auto_zone_t *zone, const void *ptr);
extern size_t auto_zone_size_no_lock(auto_zone_t *zone, const void *ptr);
extern boolean_t auto_zone_is_finalized(auto_zone_t *zone, const void *ptr);
extern void auto_zone_set_layout_type(auto_zone_t *zone, void *ptr, auto_memory_type_t type);
extern auto_memory_type_t auto_zone_get_layout_type_no_lock(auto_zone_t *zone, void *ptr);
extern void auto_zone_stats(void); // write stats to stdout
extern void auto_zone_write_stats(FILE *f); // write stats to the given stream
extern char *auto_zone_stats_string(); // return a char * containing the stats string, which should be free()'d

// Reference tracing

// referrer_base[referrer_offset]  ->  referent
typedef struct 
{
    vm_address_t referent;
    vm_address_t referrer_base;
    intptr_t     referrer_offset;
} auto_reference_t;

typedef void (*auto_reference_recorder_t)(auto_zone_t *zone, void *ctx, 
                                          auto_reference_t reference);

extern void auto_enumerate_references(auto_zone_t *zone, void *referent, 
                                      auto_reference_recorder_t callback, 
                                      void *stack_bottom, void *ctx);

extern void auto_enumerate_references_no_lock(auto_zone_t *zone, void *referent, auto_reference_recorder_t callback, void *stack_bottom, void *ctx);

void **auto_weak_find_first_referrer(auto_zone_t *zone, void **location, unsigned long count);


/************ DEPRECATED ***********/
    
extern void auto_zone_write_barrier_range(auto_zone_t *zone, void *address, size_t size);
    // Insufficient.  Will mark values about to be stored into GC memory, but has race if GC starts in the middle.
extern void auto_zone_write_barrier(auto_zone_t *zone, void *recipient, const unsigned long offset_in_bytes, const void *new_value);
extern auto_zone_t *auto_zone(void);
    // returns a pointer to the first garbage collected zone created.
extern const auto_statistics_t *auto_collection_statistics(auto_zone_t *zone);
extern unsigned auto_zone_touched_size(auto_zone_t *zone);
    // conservative (upper bound) on memory touched by the allocator itself.

extern double auto_zone_utilization(auto_zone_t *zone);
    // conservative measure of utilization of allocator touched memory.

__END_DECLS

#endif /* __AUTO_ZONE__ */
