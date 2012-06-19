/*
 * MacRuby Hash.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2011, Apple Inc. All rights reserved.
 */

#ifndef __HASH_H_
#define __HASH_H_

#if defined(__cplusplus)
extern "C" {
#endif


typedef struct RHash {
    struct RBasic basic;
    st_table *tbl;
    VALUE ifnone;
    bool has_proc_default; 
} rb_hash_t;

#define RHASH(x) ((rb_hash_t *)x)

static inline bool
rb_klass_is_rhash(VALUE klass)
{
    do {
	if (klass == rb_cRubyHash) {
	    return true;
	}
	if (klass == rb_cNSHash) {
	    return false;
	}
	klass = RCLASS_SUPER(klass);
    }
    while (klass != 0);
    return false;
}

#define IS_RHASH(x) (rb_klass_is_rhash(*(VALUE *)x))

static inline void
rhash_modify(VALUE hash)
{
    const long mask = RBASIC(hash)->flags;
    if ((mask & FL_FREEZE) == FL_FREEZE) {
	rb_raise(rb_eRuntimeError, "can't modify frozen/immutable hash");
    } 
    if ((mask & FL_UNTRUSTED) != FL_UNTRUSTED) {
	if (rb_safe_level() >= 4) {
	    rb_raise(rb_eSecurityError, "Insecure: can't modify hash");
	}
    }
}

static inline long
rhash_len(VALUE hash)
{
    return RHASH(hash)->tbl->num_entries;
}

static inline void
rhash_foreach(VALUE hash, int (*func)(ANYARGS), VALUE farg)
{
    st_foreach_safe(RHASH(hash)->tbl, func, (st_data_t)farg);
}

static inline VALUE
rhash_lookup(VALUE hash, VALUE key)
{
    VALUE val;
    if (st_lookup(RHASH(hash)->tbl, key, &val)) {
	return val;
    }
    return Qundef;
}

static inline VALUE
rhash_store(VALUE hash, VALUE key, VALUE val)
{
    rhash_modify(hash);
    if (TYPE(key) == T_STRING) {
        key = rb_str_dup(key);
        OBJ_FREEZE(key);
    }
    st_insert(RHASH(hash)->tbl, key, val);
    return val;
}

static inline VALUE
rhash_delete_key(VALUE hash, VALUE key)
{
    VALUE val;
    if (st_delete(RHASH(hash)->tbl, &key, &val)) {
	return val;
    }
    return Qundef;
}

VALUE rhash_dup(VALUE rcv, SEL sel);
VALUE rhash_aref(VALUE hash, SEL sel, VALUE key);
VALUE rhash_aset(VALUE hash, SEL sel, VALUE key, VALUE val);
VALUE rhash_keys(VALUE hash, SEL sel);
VALUE rhash_has_key(VALUE hash, SEL sel, VALUE key);
VALUE rhash_set_default(VALUE hash, SEL sel, VALUE ifnone);

unsigned long rb_hash_code(VALUE obj);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // __HASH_H_
