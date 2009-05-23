/* 
 * MacRuby implementation of Ruby 1.9's hash.c.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 * 
 * Copyright (C) 2007-2008, Apple Inc. All rights reserved.
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 * Copyright (C) 2000 Network Applied Communication Laboratory, Inc.
 * Copyright (C) 2000 Information-technology Promotion Agency, Japan
 */

#include "ruby/ruby.h"
#include "ruby/st.h"
#include "ruby/util.h"
#include "ruby/signal.h"
#include "ruby/node.h"
#include "id.h"
#include "objc.h"
#include "vm.h"

#include <crt_externs.h>

static VALUE rb_hash_s_try_convert(VALUE, SEL, VALUE);

VALUE
rb_hash_freeze(VALUE hash)
{
    return rb_obj_freeze(hash);
}

VALUE rb_cHash;
VALUE rb_cCFHash;
VALUE rb_cNSHash;
VALUE rb_cNSMutableHash;

static VALUE envtbl;
static ID id_hash, id_yield, id_default;

VALUE
rb_hash(VALUE obj)
{
    VALUE v = rb_funcall(obj, id_hash, 0);
    if (TYPE(v) == T_BIGNUM) {
	unsigned long l = NUM2ULONG(v);
	l /= 2.0;
	v = ULONG2NUM(l);
    }
    return v;
}

typedef int st_foreach_func(st_data_t, st_data_t, st_data_t);

struct foreach_safe_arg {
    st_table *tbl;
    st_foreach_func *func;
    st_data_t arg;
};

static int
foreach_safe_i(st_data_t key, st_data_t value, struct foreach_safe_arg *arg)
{
    int status;

    if (key == Qundef) return ST_CONTINUE;
    status = (*arg->func)(key, value, arg->arg);
    if (status == ST_CONTINUE) {
	return ST_CHECK;
    }
    return status;
}

void
st_foreach_safe(st_table *table, int (*func)(ANYARGS), st_data_t a)
{
    struct foreach_safe_arg arg;

    arg.tbl = table;
    arg.func = (st_foreach_func *)func;
    arg.arg = a;
    if (st_foreach(table, foreach_safe_i, (st_data_t)&arg)) {
	rb_raise(rb_eRuntimeError, "hash modified during iteration");
    }
}

void
rb_hash_foreach(VALUE hash, int (*func)(ANYARGS), VALUE farg)
{
    CFIndex i, count;
    const void **keys;
    const void **values;

    count = CFDictionaryGetCount((CFDictionaryRef)hash);
    if (count == 0)
	return;

    keys = (const void **)alloca(sizeof(void *) * count);
    values = (const void **)alloca(sizeof(void *) * count);

    CFDictionaryGetKeysAndValues((CFDictionaryRef)hash, keys, values);

    for (i = 0; i < count; i++) {
	if ((*func)(OC2RB(keys[i]), OC2RB(values[i]), farg) != ST_CONTINUE)
	    break;
    }
}

# define HASH_KEY_CALLBACKS(h) \
  ((CFDictionaryKeyCallBacks *)((uint8_t *)h + 52))

/* TODO optimize me */
struct rb_objc_hash_struct {
    VALUE ifnone;
    bool has_proc_default; 
};

/* This variable will always stay NULL, we only use its address. */
static void *rb_objc_hash_assoc_key = NULL;

static struct rb_objc_hash_struct *
rb_objc_hash_get_struct(VALUE hash)
{
    return rb_objc_get_associative_ref((void *)hash, &rb_objc_hash_assoc_key);
}

static struct rb_objc_hash_struct *
rb_objc_hash_get_struct2(VALUE hash)
{
    struct rb_objc_hash_struct *s;

    s = rb_objc_hash_get_struct(hash);
    if (s == NULL) {
	s = xmalloc(sizeof(struct rb_objc_hash_struct));
	rb_objc_set_associative_ref((void *)hash, &rb_objc_hash_assoc_key, s);
	s->ifnone = Qnil;
	s->has_proc_default = false;
    }
    return s;
}

static void
rb_objc_hash_set_struct(VALUE hash, VALUE ifnone, bool has_proc_default)
{
    struct rb_objc_hash_struct *s;

    s = rb_objc_hash_get_struct2(hash);

    GC_WB(&s->ifnone, ifnone);
    s->has_proc_default = has_proc_default;
}

static VALUE
hash_alloc(VALUE klass)
{
    CFMutableDictionaryRef hash;

    hash = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    if (klass != 0 && klass != rb_cNSHash && klass != rb_cNSMutableHash)
	*(Class *)hash = (Class)klass;

    CFMakeCollectable(hash);

    return (VALUE)hash;
}

VALUE
rb_hash_dup(VALUE rcv)
{
    VALUE dup = (VALUE)CFDictionaryCreateMutableCopy(NULL, 0, (CFDictionaryRef)rcv);
    if (OBJ_TAINTED(rcv))
	OBJ_TAINT(dup);
    CFMakeCollectable((CFTypeRef)dup);

	struct rb_objc_hash_struct *s = rb_objc_hash_get_struct(rcv);
	if (s != NULL)
	    rb_objc_hash_set_struct(dup, s->ifnone, s->has_proc_default);

    return dup;
}

static VALUE
rb_hash_dup_imp(VALUE rcv, SEL sel)
{
    return rb_hash_dup(rcv);
}

static VALUE
rb_hash_clone(VALUE rcv, SEL sel)
{
    VALUE clone = rb_hash_dup(rcv);
    if (OBJ_FROZEN(rcv))
	OBJ_FREEZE(clone);
    return clone;
}

VALUE
rb_hash_new(void)
{
    return hash_alloc(0);
}

VALUE
rb_hash_new_fast(int argc, ...)
{
    va_list ar;
    VALUE hash;
    int i;

    hash = hash_alloc(0);

    assert(argc % 2 == 0);

    va_start(ar, argc);
    for (i = 0; i < argc; i += 2) {
	VALUE key = va_arg(ar, VALUE);
	VALUE val = va_arg(ar, VALUE);
	CFDictionarySetValue((CFMutableDictionaryRef)hash, (const void *)RB2OC(key),
		(const void *)RB2OC(val));
    }
    va_end(ar);

    return hash;
}

static inline void
rb_hash_modify_check(VALUE hash)
{
    long mask;
#ifdef __LP64__
    mask = RCLASS_RC_FLAGS(hash);
#else
    mask = rb_objc_flag_get_mask((const void *)hash);
#endif
    if (RHASH_IMMUTABLE(hash)) {
	mask |= FL_FREEZE;
    }
    if ((mask & FL_FREEZE) == FL_FREEZE) {
	rb_raise(rb_eRuntimeError, "can't modify frozen/immutable hash");
    }
    if ((mask & FL_TAINT) == FL_TAINT && rb_safe_level() >= 4) {
	rb_raise(rb_eSecurityError, "Insecure: can't modify hash");
    }
}

#define rb_hash_modify rb_hash_modify_check

/*
 *  call-seq:
 *     Hash.new                          => hash
 *     Hash.new(obj)                     => aHash
 *     Hash.new {|hash, key| block }     => aHash
 *
 *  Returns a new, empty hash. If this hash is subsequently accessed by
 *  a key that doesn't correspond to a hash entry, the value returned
 *  depends on the style of <code>new</code> used to create the hash. In
 *  the first form, the access returns <code>nil</code>. If
 *  <i>obj</i> is specified, this single object will be used for
 *  all <em>default values</em>. If a block is specified, it will be
 *  called with the hash object and the key, and should return the
 *  default value. It is the block's responsibility to store the value
 *  in the hash if required.
 *
 *     h = Hash.new("Go Fish")
 *     h["a"] = 100
 *     h["b"] = 200
 *     h["a"]           #=> 100
 *     h["c"]           #=> "Go Fish"
 *     # The following alters the single default object
 *     h["c"].upcase!   #=> "GO FISH"
 *     h["d"]           #=> "GO FISH"
 *     h.keys           #=> ["a", "b"]
 *
 *     # While this creates a new default object each time
 *     h = Hash.new { |hash, key| hash[key] = "Go Fish: #{key}" }
 *     h["c"]           #=> "Go Fish: c"
 *     h["c"].upcase!   #=> "GO FISH: C"
 *     h["d"]           #=> "Go Fish: d"
 *     h.keys           #=> ["c", "d"]
 *
 */

static VALUE
rb_hash_initialize(VALUE hash, SEL sel, int argc, const VALUE *argv)
{
    VALUE ifnone;

    hash = (VALUE)objc_msgSend((id)hash, selInit);

    if (rb_block_given_p()) {
	if (argc > 0) {
	    rb_raise(rb_eArgError, "wrong number of arguments");
	}
	rb_objc_hash_set_struct(hash, rb_block_proc(), true);
    }
    else {
	rb_scan_args(argc, argv, "01", &ifnone);
	if (ifnone != Qnil)
	    rb_objc_hash_set_struct(hash, ifnone, false);
    }

    return hash;
}

VALUE
rb_hash_new2(int argc, const VALUE *argv)
{
    VALUE h = hash_alloc(0);
    rb_hash_initialize(h, 0, argc, argv);
    return h;
}

/*
 *  call-seq:
 *     Hash[ [key =>|, value]* ]   => hash
 *
 *  Creates a new hash populated with the given objects. Equivalent to
 *  the literal <code>{ <i>key</i>, <i>value</i>, ... }</code>. Keys and
 *  values occur in pairs, so there must be an even number of arguments.
 *
 *     Hash["a", 100, "b", 200]       #=> {"a"=>100, "b"=>200}
 *     Hash["a" => 100, "b" => 200]   #=> {"a"=>100, "b"=>200}
 *     { "a" => 100, "b" => 200 }     #=> {"a"=>100, "b"=>200}
 */

static VALUE
rb_hash_s_create(VALUE klass, SEL sel, int argc, VALUE *argv)
{
    VALUE hash, tmp;
    int i;

    if (argc == 1) {
	tmp = rb_hash_s_try_convert(Qnil, 0, argv[0]);
	if (!NIL_P(tmp)) {
	    CFIndex i, count;
	    const void **keys;
	    const void **values;

	    hash = hash_alloc(klass);
	    count = CFDictionaryGetCount((CFDictionaryRef)tmp);
	    if (count == 0)
		return hash;

	    keys = (const void **)alloca(sizeof(void *) * count);
	    values = (const void **)alloca(sizeof(void *) * count);

	    CFDictionaryGetKeysAndValues((CFDictionaryRef)tmp, keys, values);

	    for (i = 0; i < count; i++)
		CFDictionarySetValue((CFMutableDictionaryRef)hash,
			RB2OC(keys[i]), RB2OC(values[i]));

	    return hash;
	}

	tmp = rb_check_array_type(argv[0]);
	if (!NIL_P(tmp)) {
	    long i;

	    hash = hash_alloc(klass);
	    for (i = 0; i < RARRAY_LEN(tmp); ++i) {
		VALUE v = rb_check_array_type(RARRAY_AT(tmp, i));

		if (NIL_P(v)) continue;
		if (RARRAY_LEN(v) < 1 || 2 < RARRAY_LEN(v)) continue;
		rb_hash_aset(hash, RARRAY_AT(v, 0), RARRAY_AT(v, 1));
	    }
	    return hash;
	}
    }
    if (argc % 2 != 0) {
	rb_raise(rb_eArgError, "odd number of arguments for Hash");
    }

    hash = hash_alloc(klass);
    for (i=0; i<argc; i+=2) {
        rb_hash_aset(hash, argv[i], argv[i + 1]);
    }

    return hash;
}

static VALUE
to_hash(VALUE hash)
{
    return rb_convert_type(hash, T_HASH, "Hash", "to_hash");
}

/*
 *  call-seq:
 *     Hash.try_convert(obj) -> hash or nil
 *
 *  Try to convert <i>obj</i> into a hash, using to_hash method.
 *  Returns converted hash or nil if <i>obj</i> cannot be converted
 *  for any reason.
 *
 *     Hash.try_convert({1=>2})   # => {1=>2}
 *     Hash.try_convert("1=>2")   # => nil
 */
static VALUE
rb_hash_s_try_convert(VALUE dummy, SEL sel, VALUE hash)
{
    return rb_check_convert_type(hash, T_HASH, "Hash", "to_hash");
}

/*
 *  call-seq:
 *     hsh.rehash -> hsh
 *
 *  Rebuilds the hash based on the current hash values for each key. If
 *  values of key objects have changed since they were inserted, this
 *  method will reindex <i>hsh</i>. If <code>Hash#rehash</code> is
 *  called while an iterator is traversing the hash, an
 *  <code>RuntimeError</code> will be raised in the iterator.
 *
 *     a = [ "a", "b" ]
 *     c = [ "c", "d" ]
 *     h = { a => 100, c => 300 }
 *     h[a]       #=> 100
 *     a[0] = "z"
 *     h[a]       #=> nil
 *     h.rehash   #=> {["z", "b"]=>100, ["c", "d"]=>300}
 *     h[a]       #=> 100
 */

static VALUE
rb_hash_rehash(VALUE hash, SEL sel)
{
    CFIndex i, count;
    const void **keys;
    const void **values;

    rb_hash_modify_check(hash);

    count = CFDictionaryGetCount((CFDictionaryRef)hash);
    if (count == 0)
	return hash;

    keys = (const void **)alloca(sizeof(void *) * count);
    values = (const void **)alloca(sizeof(void *) * count);

    CFDictionaryGetKeysAndValues((CFDictionaryRef)hash, keys, values);
    CFDictionaryRemoveAllValues((CFMutableDictionaryRef)hash);

    for (i = 0; i < count; i++)
	CFDictionarySetValue((CFMutableDictionaryRef)hash,
	    (const void *)keys[i], (const void *)values[i]);

    return hash;
}

/*
 *  call-seq:
 *     hsh[key]    =>  value
 *
 *  Element Reference---Retrieves the <i>value</i> object corresponding
 *  to the <i>key</i> object. If not found, returns the a default value (see
 *  <code>Hash::new</code> for details).
 *
 *     h = { "a" => 100, "b" => 200 }
 *     h["a"]   #=> 100
 *     h["c"]   #=> nil
 *
 */

VALUE
rb_hash_aref(VALUE hash, VALUE key)
{
    VALUE val;

    if (!CFDictionaryGetValueIfPresent((CFDictionaryRef)hash, (const void *)RB2OC(key),
	(const void **)&val)) {
	return rb_funcall(hash, id_default, 1, key);
    }
    val = OC2RB(val);
    return val;
}

static VALUE
rb_hash_aref_imp(VALUE hash, SEL sel, VALUE key)
{
    return rb_hash_aref(hash, key);
}

VALUE
rb_hash_lookup(VALUE hash, VALUE key)
{
    VALUE val;

    if (!CFDictionaryGetValueIfPresent((CFDictionaryRef)hash, (const void *)RB2OC(key),
	(const void **)&val)) {
	return Qnil;
    }
    val = OC2RB(val);
    return val;
}

/*
 *  call-seq:
 *     hsh.fetch(key [, default] )       => obj
 *     hsh.fetch(key) {| key | block }   => obj
 *
 *  Returns a value from the hash for the given key. If the key can't be
 *  found, there are several options: With no other arguments, it will
 *  raise an <code>KeyError</code> exception; if <i>default</i> is
 *  given, then that will be returned; if the optional code block is
 *  specified, then that will be run and its result returned.
 *
 *     h = { "a" => 100, "b" => 200 }
 *     h.fetch("a")                            #=> 100
 *     h.fetch("z", "go fish")                 #=> "go fish"
 *     h.fetch("z") { |el| "go fish, #{el}"}   #=> "go fish, z"
 *
 *  The following example shows that an exception is raised if the key
 *  is not found and a default value is not supplied.
 *
 *     h = { "a" => 100, "b" => 200 }
 *     h.fetch("z")
 *
 *  <em>produces:</em>
 *
 *     prog.rb:2:in `fetch': key not found (KeyError)
 *      from prog.rb:2
 *
 */

static VALUE
rb_hash_fetch(VALUE hash, SEL sel, int argc, VALUE *argv)
{
    VALUE key, if_none;
    VALUE val;
    long block_given;

    rb_scan_args(argc, argv, "11", &key, &if_none);

    block_given = rb_block_given_p();
    if (block_given && argc == 2) {
	rb_warn("block supersedes default value argument");
    }
    if (!CFDictionaryGetValueIfPresent((CFDictionaryRef)hash, (const void *)RB2OC(key),
	(const void **)&val)) {
	if (block_given) return rb_yield(key);
	if (argc == 1) {
	    rb_raise(rb_eKeyError, "key not found");
	}
	return if_none;
    }
    return OC2RB(val);
}

/*
 *  call-seq:
 *     hsh.default(key=nil)   => obj
 *
 *  Returns the default value, the value that would be returned by
 *  <i>hsh</i>[<i>key</i>] if <i>key</i> did not exist in <i>hsh</i>.
 *  See also <code>Hash::new</code> and <code>Hash#default=</code>.
 *
 *     h = Hash.new                            #=> {}
 *     h.default                               #=> nil
 *     h.default(2)                            #=> nil
 *
 *     h = Hash.new("cat")                     #=> {}
 *     h.default                               #=> "cat"
 *     h.default(2)                            #=> "cat"
 *
 *     h = Hash.new {|h,k| h[k] = k.to_i*10}   #=> {}
 *     h.default                               #=> nil
 *     h.default(2)                            #=> 20
 */

static VALUE
rb_hash_default(VALUE hash, SEL sel, int argc, VALUE *argv)
{
    struct rb_objc_hash_struct *s = rb_objc_hash_get_struct(hash);
    VALUE key;

    if (s == NULL || s->ifnone == Qnil)
	return Qnil;

    rb_scan_args(argc, argv, "01", &key);
    if (s->has_proc_default) {
	if (argc == 0) return Qnil;
	return rb_funcall(s->ifnone, id_yield, 2, hash, key);
    }
    return s->ifnone;
}

/*
 *  call-seq:
 *     hsh.default = obj     => hsh
 *
 *  Sets the default value, the value returned for a key that does not
 *  exist in the hash. It is not possible to set the a default to a
 *  <code>Proc</code> that will be executed on each key lookup.
 *
 *     h = { "a" => 100, "b" => 200 }
 *     h.default = "Go fish"
 *     h["a"]     #=> 100
 *     h["z"]     #=> "Go fish"
 *     # This doesn't do what you might hope...
 *     h.default = proc do |hash, key|
 *       hash[key] = key + key
 *     end
 *     h[2]       #=> #<Proc:0x401b3948@-:6>
 *     h["cat"]   #=> #<Proc:0x401b3948@-:6>
 */

static VALUE
rb_hash_set_default(VALUE hash, SEL sel, VALUE ifnone)
{
    rb_hash_modify(hash);
    rb_objc_hash_set_struct(hash, ifnone, false);
    return ifnone;
}

/*
 *  call-seq:
 *     hsh.default_proc -> anObject
 *
 *  If <code>Hash::new</code> was invoked with a block, return that
 *  block, otherwise return <code>nil</code>.
 *
 *     h = Hash.new {|h,k| h[k] = k*k }   #=> {}
 *     p = h.default_proc                 #=> #<Proc:0x401b3d08@-:1>
 *     a = []                             #=> []
 *     p.call(a, 2)
 *     a                                  #=> [nil, nil, 4]
 */


static VALUE
rb_hash_default_proc(VALUE hash, SEL sel)
{
    struct rb_objc_hash_struct *s = rb_objc_hash_get_struct(hash);
    if (s != NULL && s->has_proc_default)
	return s->ifnone;
    return Qnil;
}

static int
key_i(VALUE key, VALUE value, VALUE *args)
{
    if (rb_equal(value, args[0])) {
	args[1] = key;
	return ST_STOP;
    }
    return ST_CONTINUE;
}

/*
 *  call-seq:
 *     hsh.key(value)    => key
 *
 *  Returns the key for a given value. If not found, returns <code>nil</code>.
 *
 *     h = { "a" => 100, "b" => 200 }
 *     h.key(200)   #=> "b"
 *     h.key(999)   #=> nil
 *
 */

static VALUE
rb_hash_key(VALUE hash, SEL sel, VALUE value)
{
    VALUE args[2];

    args[0] = value;
    args[1] = Qnil;

    rb_hash_foreach(hash, key_i, (st_data_t)args);

    return args[1];
}

/* :nodoc: */
static VALUE
rb_hash_index(VALUE hash, SEL sel, VALUE value)
{
    rb_warn("Hash#index is deprecated; use Hash#key");
    return rb_hash_key(hash, 0, value);
}

static VALUE
rb_hash_delete_key(VALUE hash, VALUE key)
{
    VALUE val;
    id ockey = RB2OC(key);
    if (CFDictionaryGetValueIfPresent((CFDictionaryRef)hash,
	(const void *)ockey, (const void **)&val)) {
	CFDictionaryRemoveValue((CFMutableDictionaryRef)hash, 
	    (const void *)ockey);
	return OC2RB(val);
    }
    return Qundef;
}

/*
 *  call-seq:
 *     hsh.delete(key)                   => value
 *     hsh.delete(key) {| key | block }  => value
 *
 *  Deletes and returns a key-value pair from <i>hsh</i> whose key is
 *  equal to <i>key</i>. If the key is not found, returns the
 *  <em>default value</em>. If the optional code block is given and the
 *  key is not found, pass in the key and return the result of
 *  <i>block</i>.
 *
 *     h = { "a" => 100, "b" => 200 }
 *     h.delete("a")                              #=> 100
 *     h.delete("z")                              #=> nil
 *     h.delete("z") { |el| "#{el} not found" }   #=> "z not found"
 *
 */

VALUE
rb_hash_delete(VALUE hash, VALUE key)
{
    VALUE val;

    rb_hash_modify(hash);
    val = rb_hash_delete_key(hash, key);
    if (val != Qundef) return val;
    if (rb_block_given_p()) {
	return rb_yield(key);
    }
    return Qnil;
}

static VALUE
rb_hash_delete_imp(VALUE hash, SEL sel, VALUE key)
{
    return rb_hash_delete(hash, key);
}

/*
 *  call-seq:
 *     hsh.shift -> anArray or obj
 *
 *  Removes a key-value pair from <i>hsh</i> and returns it as the
 *  two-item array <code>[</code> <i>key, value</i> <code>]</code>, or
 *  the hash's default value if the hash is empty.
 *
 *     h = { 1 => "a", 2 => "b", 3 => "c" }
 *     h.shift   #=> [1, "a"]
 *     h         #=> {2=>"b", 3=>"c"}
 */

static VALUE rb_hash_keys(VALUE, SEL);

static VALUE
rb_hash_shift(VALUE hash, SEL sel)
{
    VALUE keys, key, val;

    keys = rb_hash_keys(hash, 0);
    if (RARRAY_LEN(keys) == 0) {
	struct rb_objc_hash_struct *s = rb_objc_hash_get_struct(hash);

	if (s == NULL || s->ifnone == Qnil)
	    return Qnil;

	if (s->has_proc_default)
	    return rb_funcall(s->ifnone, id_yield, 2, hash, Qnil);
	return s->ifnone;
    }

    key = RARRAY_AT(keys, 0);
    val = rb_hash_aref(hash, key);
    rb_hash_delete(hash, key);

    return rb_assoc_new(key, val);
}

static int
delete_if_i(VALUE key, VALUE value, VALUE hash)
{
    if (key == Qundef) return ST_CONTINUE;
    if (RTEST(rb_yield_values(2, key, value))) {
	rb_hash_delete_key(hash, key);
    }
    return ST_CONTINUE;
}

/*
 *  call-seq:
 *     hsh.delete_if {| key, value | block }  -> hsh
 *
 *  Deletes every key-value pair from <i>hsh</i> for which <i>block</i>
 *  evaluates to <code>true</code>.
 *
 *     h = { "a" => 100, "b" => 200, "c" => 300 }
 *     h.delete_if {|key, value| key >= "b" }   #=> {"a"=>100}
 *
 */

static VALUE
rb_hash_delete_if(VALUE hash, SEL sel)
{
    RETURN_ENUMERATOR(hash, 0, 0);
    rb_hash_modify(hash);
    rb_hash_foreach(hash, delete_if_i, hash);
    return hash;
}

/*
 *  call-seq:
 *     hsh.reject! {| key, value | block }  -> hsh or nil
 *
 *  Equivalent to <code>Hash#delete_if</code>, but returns
 *  <code>nil</code> if no changes were made.
 */

static VALUE
rb_hash_reject_bang(VALUE hash, SEL sel)
{
    CFIndex n;

    RETURN_ENUMERATOR(hash, 0, 0);
    n = CFDictionaryGetCount((CFDictionaryRef)hash);
    rb_hash_delete_if(hash, 0);
    if (n == CFDictionaryGetCount((CFDictionaryRef)hash))
	return Qnil;
    return hash;
}

/*
 *  call-seq:
 *     hsh.reject {| key, value | block }  -> a_hash
 *
 *  Same as <code>Hash#delete_if</code>, but works on (and returns) a
 *  copy of the <i>hsh</i>. Equivalent to
 *  <code><i>hsh</i>.dup.delete_if</code>.
 *
 */

static VALUE
rb_hash_reject(VALUE hash, SEL sel)
{
    return rb_hash_delete_if(rb_hash_dup(hash), 0);
}

/*
 * call-seq:
 *   hsh.values_at(key, ...)   => array
 *
 * Return an array containing the values associated with the given keys.
 * Also see <code>Hash.select</code>.
 *
 *   h = { "cat" => "feline", "dog" => "canine", "cow" => "bovine" }
 *   h.values_at("cow", "cat")  #=> ["bovine", "feline"]
 */

static VALUE
rb_hash_values_at(VALUE hash, SEL sel, int argc, VALUE *argv)
{
    VALUE result = rb_ary_new2(argc);
    long i;

    for (i=0; i<argc; i++) {
	rb_ary_push(result, rb_hash_aref(hash, argv[i]));
    }
    return result;
}

static int
select_i(VALUE key, VALUE value, VALUE result)
{
    if (key == Qundef) return ST_CONTINUE;
    if (RTEST(rb_yield_values(2, key, value)))
	rb_hash_aset(result, key, value);
    return ST_CONTINUE;
}

/*
 *  call-seq:
 *     hsh.select {|key, value| block}   => a_hash
 *
 *  Returns a new hash consisting of entries which the block returns true.
 *
 *     h = { "a" => 100, "b" => 200, "c" => 300 }
 *     h.select {|k,v| k > "a"}  #=> {"b" => 200, "c" => 300}
 *     h.select {|k,v| v < 200}  #=> {"a" => 100}
 */

static VALUE
rb_hash_select(VALUE hash, SEL sel)
{
    VALUE result;

    RETURN_ENUMERATOR(hash, 0, 0);
    result = rb_hash_new();
    rb_hash_foreach(hash, select_i, result);
    return result;
}

/*
 *  call-seq:
 *     hsh.clear -> hsh
 *
 *  Removes all key-value pairs from <i>hsh</i>.
 *
 *     h = { "a" => 100, "b" => 200 }   #=> {"a"=>100, "b"=>200}
 *     h.clear                          #=> {}
 *
 */

static VALUE
rb_hash_clear(VALUE hash, SEL sel)
{
    rb_hash_modify_check(hash);
    CFDictionaryRemoveAllValues((CFMutableDictionaryRef)hash);

    return hash;
}

/*
 *  call-seq:
 *     hsh[key] = value        => value
 *     hsh.store(key, value)   => value
 *
 *  Element Assignment---Associates the value given by
 *  <i>value</i> with the key given by <i>key</i>.
 *  <i>key</i> should not have its value changed while it is in
 *  use as a key (a <code>String</code> passed as a key will be
 *  duplicated and frozen).
 *
 *     h = { "a" => 100, "b" => 200 }
 *     h["a"] = 9
 *     h["c"] = 4
 *     h   #=> {"a"=>9, "b"=>200, "c"=>4}
 *
 */

VALUE
rb_hash_aset(VALUE hash, VALUE key, VALUE val)
{
    rb_hash_modify(hash);
    CFDictionarySetValue((CFMutableDictionaryRef)hash, (const void *)RB2OC(key),
	(const void *)RB2OC(val));
    return val;
}

static VALUE
rb_hash_aset_imp(VALUE hash, SEL sel, VALUE key, VALUE val)
{
    return rb_hash_aset(hash, key, val);
}

static int
replace_i(VALUE key, VALUE val, VALUE hash)
{
    if (key != Qundef) {
	rb_hash_aset(hash, key, val);
    }

    return ST_CONTINUE;
}

/*
 *  call-seq:
 *     hsh.replace(other_hash) -> hsh
 *
 *  Replaces the contents of <i>hsh</i> with the contents of
 *  <i>other_hash</i>.
 *
 *     h = { "a" => 100, "b" => 200 }
 *     h.replace({ "c" => 300, "d" => 400 })   #=> {"c"=>300, "d"=>400}
 *
 */

static VALUE
rb_hash_replace(VALUE hash, SEL sel, VALUE hash2)
{
    hash2 = to_hash(hash2);
    if (hash == hash2) return hash;
    rb_hash_clear(hash, 0);
    rb_hash_foreach(hash2, replace_i, hash);
    {
	struct rb_objc_hash_struct *s = rb_objc_hash_get_struct(hash2);
	if (s != NULL)
	    rb_objc_hash_set_struct(hash, s->ifnone, s->has_proc_default);
    }

    return hash;
}

/*
 *  call-seq:
 *     hsh.length    =>  fixnum
 *     hsh.size      =>  fixnum
 *
 *  Returns the number of key-value pairs in the hash.
 *
 *     h = { "d" => 100, "a" => 200, "v" => 300, "e" => 400 }
 *     h.length        #=> 4
 *     h.delete("a")   #=> 200
 *     h.length        #=> 3
 */

static VALUE
rb_hash_size(VALUE hash, SEL sel)
{
    return INT2FIX(CFDictionaryGetCount((CFDictionaryRef)hash));
}


/*
 *  call-seq:
 *     hsh.empty?    => true or false
 *
 *  Returns <code>true</code> if <i>hsh</i> contains no key-value pairs.
 *
 *     {}.empty?   #=> true
 *
 */

static VALUE
rb_hash_empty_p(VALUE hash, SEL sel)
{
    return RHASH_EMPTY_P(hash) ? Qtrue : Qfalse;
}

static int
each_value_i(VALUE key, VALUE value)
{
    if (key == Qundef) return ST_CONTINUE;
    rb_yield(value);
    return ST_CONTINUE;
}

/*
 *  call-seq:
 *     hsh.each_value {| value | block } -> hsh
 *
 *  Calls <i>block</i> once for each key in <i>hsh</i>, passing the
 *  value as a parameter.
 *
 *     h = { "a" => 100, "b" => 200 }
 *     h.each_value {|value| puts value }
 *
 *  <em>produces:</em>
 *
 *     100
 *     200
 */

static VALUE
rb_hash_each_value(VALUE hash, SEL sel)
{
    RETURN_ENUMERATOR(hash, 0, 0);
    rb_hash_foreach(hash, each_value_i, 0);
    return hash;
}

static int
each_key_i(VALUE key, VALUE value)
{
    if (key == Qundef) return ST_CONTINUE;
    rb_yield(key);
    return ST_CONTINUE;
}

/*
 *  call-seq:
 *     hsh.each_key {| key | block } -> hsh
 *
 *  Calls <i>block</i> once for each key in <i>hsh</i>, passing the key
 *  as a parameter.
 *
 *     h = { "a" => 100, "b" => 200 }
 *     h.each_key {|key| puts key }
 *
 *  <em>produces:</em>
 *
 *     a
 *     b
 */
static VALUE
rb_hash_each_key(VALUE hash, SEL sel)
{
    RETURN_ENUMERATOR(hash, 0, 0);
    rb_hash_foreach(hash, each_key_i, 0);
    return hash;
}

static int
each_pair_i(VALUE key, VALUE value)
{
    if (key == Qundef) return ST_CONTINUE;
    rb_yield(rb_assoc_new(key, value));
    return ST_CONTINUE;
}

/*
 *  call-seq:
 *     hsh.each {| key, value | block } -> hsh
 *     hsh.each_pair {| key, value | block } -> hsh
 *
 *  Calls <i>block</i> once for each key in <i>hsh</i>, passing the key-value
 *  pair as parameters.
 *
 *     h = { "a" => 100, "b" => 200 }
 *     h.each {|key, value| puts "#{key} is #{value}" }
 *
 *  <em>produces:</em>
 *
 *     a is 100
 *     b is 200
 *
 */

static VALUE
rb_hash_each_pair(VALUE hash, SEL sel)
{
    RETURN_ENUMERATOR(hash, 0, 0);
    rb_hash_foreach(hash, each_pair_i, 0);
    return hash;
}

static int
to_a_i(VALUE key, VALUE value, VALUE ary)
{
    if (key == Qundef) return ST_CONTINUE;
    rb_ary_push(ary, rb_assoc_new(key, value));
    return ST_CONTINUE;
}

/*
 *  call-seq:
 *     hsh.to_a -> array
 *
 *  Converts <i>hsh</i> to a nested array of <code>[</code> <i>key,
 *  value</i> <code>]</code> arrays.
 *
 *     h = { "c" => 300, "a" => 100, "d" => 400, "c" => 300  }
 *     h.to_a   #=> [["c", 300], ["a", 100], ["d", 400]]
 */

static VALUE
rb_hash_to_a(VALUE hash, SEL sel)
{
    VALUE ary;

    ary = rb_ary_new();
    rb_hash_foreach(hash, to_a_i, ary);
    if (OBJ_TAINTED(hash)) OBJ_TAINT(ary);

    return ary;
}

static int
inspect_i(VALUE key, VALUE value, VALUE str)
{
    VALUE str2;

    if (key == Qundef) {
	return ST_CONTINUE;
    }
    if (RSTRING_LEN(str) > 1) {
	rb_str_cat2(str, ", ");
    }
    str2 = rb_inspect(key);
    rb_str_buf_append(str, str2);
    rb_str_buf_cat2(str, "=>");
    str2 = rb_inspect(value);
    rb_str_buf_append(str, str2);

    return ST_CONTINUE;
}

static VALUE
inspect_hash(VALUE hash, VALUE dummy, int recur)
{
    VALUE str;

    if (recur) {
	return rb_usascii_str_new2("{...}");
    }
    str = rb_str_buf_new2("{");
    rb_hash_foreach(hash, inspect_i, str);
    rb_str_buf_cat2(str, "}");
    OBJ_INFECT(str, hash);

    return str;
}

/*
 * call-seq:
 *   hsh.to_s   => string
 *   hsh.inspect  => string
 *
 * Return the contents of this hash as a string.
 *
 *     h = { "c" => 300, "a" => 100, "d" => 400, "c" => 300  }
 *     h.to_s   #=> "{\"c\"=>300, \"a\"=>100, \"d\"=>400}"
 */

static VALUE
rb_hash_inspect(VALUE hash, SEL sel)
{
    if (RHASH_EMPTY_P(hash))
	return rb_usascii_str_new2("{}");
    return rb_exec_recursive(inspect_hash, hash, 0);
}

/*
 * call-seq:
 *    hsh.to_hash   => hsh
 *
 * Returns <i>self</i>.
 */

static VALUE
rb_hash_to_hash(VALUE hash, SEL sel)
{
    return hash;
}

static int
keys_i(VALUE key, VALUE value, VALUE ary)
{
    if (key == Qundef) return ST_CONTINUE;
    rb_ary_push(ary, key);
    return ST_CONTINUE;
}

/*
 *  call-seq:
 *     hsh.keys    => array
 *
 *  Returns a new array populated with the keys from this hash. See also
 *  <code>Hash#values</code>.
 *
 *     h = { "a" => 100, "b" => 200, "c" => 300, "d" => 400 }
 *     h.keys   #=> ["a", "b", "c", "d"]
 *
 */

static VALUE
rb_hash_keys(VALUE hash, SEL sel)
{
    VALUE ary;

    ary = rb_ary_new();
    rb_hash_foreach(hash, keys_i, ary);

    return ary;
}

static int
values_i(VALUE key, VALUE value, VALUE ary)
{
    if (key == Qundef) return ST_CONTINUE;
    rb_ary_push(ary, value);
    return ST_CONTINUE;
}

/*
 *  call-seq:
 *     hsh.values    => array
 *
 *  Returns a new array populated with the values from <i>hsh</i>. See
 *  also <code>Hash#keys</code>.
 *
 *     h = { "a" => 100, "b" => 200, "c" => 300 }
 *     h.values   #=> [100, 200, 300]
 *
 */

static VALUE
rb_hash_values(VALUE hash, SEL sel)
{
    VALUE ary;

    ary = rb_ary_new();
    rb_hash_foreach(hash, values_i, ary);

    return ary;
}

/*
 *  call-seq:
 *     hsh.has_key?(key)    => true or false
 *     hsh.include?(key)    => true or false
 *     hsh.key?(key)        => true or false
 *     hsh.member?(key)     => true or false
 *
 *  Returns <code>true</code> if the given key is present in <i>hsh</i>.
 *
 *     h = { "a" => 100, "b" => 200 }
 *     h.has_key?("a")   #=> true
 *     h.has_key?("z")   #=> false
 *
 */

static VALUE
rb_hash_has_key(VALUE hash, SEL sel, VALUE key)
{
    if (CFDictionaryContainsKey((CFDictionaryRef)hash, (const void *)RB2OC(key)))
	return Qtrue;

    return Qfalse;
}

/*
 *  call-seq:
 *     hsh.has_value?(value)    => true or false
 *     hsh.value?(value)        => true or false
 *
 *  Returns <code>true</code> if the given value is present for some key
 *  in <i>hsh</i>.
 *
 *     h = { "a" => 100, "b" => 200 }
 *     h.has_value?(100)   #=> true
 *     h.has_value?(999)   #=> false
 */

static VALUE
rb_hash_has_value(VALUE hash, SEL sel, VALUE val)
{
    return CFDictionaryContainsValue((CFDictionaryRef)hash, (const void *)RB2OC(val))
	? Qtrue : Qfalse;
}

static VALUE
hash_equal(VALUE hash1, VALUE hash2, int eql)
{
    if (hash1 == hash2) return Qtrue;
    if (TYPE(hash2) != T_HASH) {
	if (!rb_respond_to(hash2, rb_intern("to_hash"))) {
	    return Qfalse;
	}
	if (eql)
	    return rb_eql(hash2, hash1);
	else
	    return rb_equal(hash2, hash1);
    }
    if (RHASH_SIZE(hash1) != RHASH_SIZE(hash2))
	return Qfalse;
    return CFEqual((CFTypeRef)hash1, (CFTypeRef)hash2) ? Qtrue : Qfalse;
}

/*
 *  call-seq:
 *     hsh == other_hash    => true or false
 *
 *  Equality---Two hashes are equal if they each contain the same number
 *  of keys and if each key-value pair is equal to (according to
 *  <code>Object#==</code>) the corresponding elements in the other
 *  hash.
 *
 *     h1 = { "a" => 1, "c" => 2 }
 *     h2 = { 7 => 35, "c" => 2, "a" => 1 }
 *     h3 = { "a" => 1, "c" => 2, 7 => 35 }
 *     h4 = { "a" => 1, "d" => 2, "f" => 35 }
 *     h1 == h2   #=> false
 *     h2 == h3   #=> true
 *     h3 == h4   #=> false
 *
 */

static VALUE
rb_hash_equal(VALUE hash1, SEL sel, VALUE hash2)
{
    return hash_equal(hash1, hash2, Qfalse);
}

/*
 *  call-seq:
 *     hash.eql?(other)  -> true or false
 *
 *  Returns <code>true</code> if <i>hash</i> and <i>other</i> are
 *  both hashes with the same content.
 */

static VALUE
rb_hash_eql(VALUE hash1, SEL sel, VALUE hash2)
{
    return hash_equal(hash1, hash2, Qtrue);
}

static int
rb_hash_invert_i(VALUE key, VALUE value, VALUE hash)
{
    if (key == Qundef) return ST_CONTINUE;
    rb_hash_aset(hash, value, key);
    return ST_CONTINUE;
}

/*
 *  call-seq:
 *     hsh.invert -> aHash
 *
 *  Returns a new hash created by using <i>hsh</i>'s values as keys, and
 *  the keys as values.
 *
 *     h = { "n" => 100, "m" => 100, "y" => 300, "d" => 200, "a" => 0 }
 *     h.invert   #=> {0=>"a", 100=>"m", 200=>"d", 300=>"y"}
 *
 */

static VALUE
rb_hash_invert(VALUE hash, SEL sel)
{
    VALUE h = rb_hash_new();

    rb_hash_foreach(hash, rb_hash_invert_i, h);
    return h;
}

static int
rb_hash_update_i(VALUE key, VALUE value, VALUE hash)
{
    if (key == Qundef) return ST_CONTINUE;
    rb_hash_aset(hash, key, value);
    return ST_CONTINUE;
}

static int
rb_hash_update_block_i(VALUE key, VALUE value, VALUE hash)
{
    if (key == Qundef) return ST_CONTINUE;
    if (rb_hash_has_key(hash, 0, key)) {
	value = rb_yield_values(3, key, rb_hash_aref(hash, key), value);
    }
    rb_hash_aset(hash, key, value);
    return ST_CONTINUE;
}

/*
 *  call-seq:
 *     hsh.merge!(other_hash)                                 => hsh
 *     hsh.update(other_hash)                                 => hsh
 *     hsh.merge!(other_hash){|key, oldval, newval| block}    => hsh
 *     hsh.update(other_hash){|key, oldval, newval| block}    => hsh
 *
 *  Adds the contents of <i>other_hash</i> to <i>hsh</i>.  If no
 *  block is specified entries with duplicate keys are overwritten
 *  with the values from <i>other_hash</i>, otherwise the value
 *  of each duplicate key is determined by calling the block with
 *  the key, its value in <i>hsh</i> and its value in <i>other_hash</i>.
 *
 *     h1 = { "a" => 100, "b" => 200 }
 *     h2 = { "b" => 254, "c" => 300 }
 *     h1.merge!(h2)   #=> {"a"=>100, "b"=>254, "c"=>300}
 *
 *     h1 = { "a" => 100, "b" => 200 }
 *     h2 = { "b" => 254, "c" => 300 }
 *     h1.merge!(h2) { |key, v1, v2| v1 }
 *                     #=> {"a"=>100, "b"=>200, "c"=>300}
 */

static VALUE
rb_hash_update(VALUE hash1, SEL sel, VALUE hash2)
{
    hash2 = to_hash(hash2);
    if (rb_block_given_p()) {
	rb_hash_foreach(hash2, rb_hash_update_block_i, hash1);
    }
    else {
	rb_hash_foreach(hash2, rb_hash_update_i, hash1);
    }
    return hash1;
}

/*
 *  call-seq:
 *     hsh.merge(other_hash)                              -> a_hash
 *     hsh.merge(other_hash){|key, oldval, newval| block} -> a_hash
 *
 *  Returns a new hash containing the contents of <i>other_hash</i> and
 *  the contents of <i>hsh</i>, overwriting entries in <i>hsh</i> with
 *  duplicate keys with those from <i>other_hash</i>.
 *
 *     h1 = { "a" => 100, "b" => 200 }
 *     h2 = { "b" => 254, "c" => 300 }
 *     h1.merge(h2)   #=> {"a"=>100, "b"=>254, "c"=>300}
 *     h1             #=> {"a"=>100, "b"=>200}
 *
 */

static VALUE
rb_hash_merge(VALUE hash1, SEL sel, VALUE hash2)
{
    return rb_hash_update(rb_hash_dup(hash1), 0, hash2);
}

static int
assoc_i(VALUE key, VALUE val, VALUE *args)
{
    if (key == Qundef) return ST_CONTINUE;
    if (RTEST(rb_equal(args[0], key))) {
	args[1] = rb_assoc_new(key, val);
	return ST_STOP;
    }
    return ST_CONTINUE;
}

/*
 *  call-seq:
 *     hash.assoc(obj)   ->  an_array  or  nil
 *
 *  Searches through the hash comparing _obj_ with the key using <code>==</code>.
 *  Returns the key-value pair (two elements array) or +nil+
 *  if no match is found.  See <code>Array#assoc</code>.
 *
 *     h = {"colors"  => ["red", "blue", "green"],
 *          "letters" => ["a", "b", "c" ]}
 *     h.assoc("letters")  #=> ["letters", ["a", "b", "c"]]
 *     h.assoc("foo")      #=> nil
 */

VALUE
rb_hash_assoc(VALUE hash, SEL sel, VALUE obj)
{
    VALUE args[2];

    args[0] = obj;
    args[1] = Qnil;
    rb_hash_foreach(hash, assoc_i, (st_data_t)args);
    return args[1];
}

static int
rassoc_i(VALUE key, VALUE val, VALUE *args)
{
    if (key == Qundef) return ST_CONTINUE;
    if (RTEST(rb_equal(args[0], val))) {
	args[1] = rb_assoc_new(key, val);
	return ST_STOP;
    }
    return ST_CONTINUE;
}

/*
 *  call-seq:
 *     hash.rassoc(key) -> an_array or nil
 *
 *  Searches through the hash comparing _obj_ with the value using <code>==</code>.
 *  Returns the first key-value pair (two elements array) that matches. See
 *  also <code>Array#rassoc</code>.
 *
 *     a = {1=> "one", 2 => "two", 3 => "three", "ii" => "two"}
 *     a.rassoc("two")    #=> [2, "two"]
 *     a.rassoc("four")   #=> nil
 */

VALUE
rb_hash_rassoc(VALUE hash, SEL sel, VALUE obj)
{
    VALUE args[2];

    args[0] = obj;
    args[1] = Qnil;
    rb_hash_foreach(hash, rassoc_i, (st_data_t)args);
    return args[1];
}

/*
 *  call-seq:
 *     hash.flatten -> an_array
 *     hash.flatten(level) -> an_array
 *
 *  Returns a new array that is a one-dimensional flattening of this
 *  hash. That is, for every key or value that is an array, extract
 *  its elements into the new array.  Unlike Array#flatten, this
 *  method does not flatten recursively by default.  If the optional
 *  <i>level</i> argument determines the level of recursion to flatten.
 *
 *     a =  {1=> "one", 2 => [2,"two"], 3 => "three"}
 *     a.flatten    # => [1, "one", 2, [2, "two"], 3, "three"]
 *     a.flatten(2) # => [1, "one", 2, 2, "two", 3, "three"]
 */

static VALUE
rb_hash_flatten(VALUE hash, SEL sel, int argc, VALUE *argv)
{
    VALUE ary, tmp;

    ary = rb_hash_to_a(hash, 0);
    if (argc == 0) {
	argc = 1;
	tmp = INT2FIX(1);
	argv = &tmp;
    }
    rb_funcall2(ary, rb_intern("flatten!"), argc, argv);
    return ary;
}

/*
 *  call-seq:
 *     hsh.compare_by_identity => hsh
 *
 *  Makes <i>hsh</i> to compare its keys by their identity, i.e. it
 *  will consider exact same objects as same keys.
 *
 *     h1 = { "a" => 100, "b" => 200, :c => "c" }
 *     h1["a"]        #=> 100
 *     h1.compare_by_identity
 *     h1.compare_by_identity? #=> true
 *     h1["a"]        #=> nil  # different objects.
 *     h1[:c]         #=> "c"  # same symbols are all same.
 *
 */

static VALUE
rb_hash_compare_by_id(VALUE hash, SEL sel)
{
    rb_hash_modify(hash);
//    HASH_KEY_CALLBACKS(hash)->equal = NULL;
    return hash;
}

/*
 *  call-seq:
 *     hsh.compare_by_identity? => true or false
 *
 *  Returns <code>true</code> if <i>hsh</i> will compare its keys by
 *  their identity.  Also see <code>Hash#compare_by_identity</code>.
 *
 */

static VALUE
rb_hash_compare_by_id_p(VALUE hash, SEL sel)
{
    return Qfalse;
//    return HASH_KEY_CALLBACKS(hash) != &kCFTypeDictionaryKeyCallBacks 
//	&& HASH_KEY_CALLBACKS(hash)->equal == NULL
//	    ? Qtrue : Qfalse;
}

static int path_tainted = -1;

static char **origenviron;
#undef environ
#define environ (*_NSGetEnviron())
#define GET_ENVIRON(e) (e)
#define FREE_ENVIRON(e)

static VALUE
env_str_new(const char *ptr, long len)
{
    VALUE str = rb_tainted_str_new(ptr, len);

    rb_obj_freeze(str);
    return str;
}

static VALUE
env_str_new2(const char *ptr)
{
    if (!ptr) return Qnil;
    return env_str_new(ptr, strlen(ptr));
}

static VALUE
env_delete(VALUE obj, VALUE name)
{
    const char *nam, *val;

    rb_secure(4);
    SafeStringValue(name);
    nam = RSTRING_PTR(name);
    if (strlen(nam) != RSTRING_LEN(name)) {
	rb_raise(rb_eArgError, "bad environment variable name");
    }
    val = getenv(nam);
    if (val) {
	VALUE value = env_str_new2(val);

	ruby_setenv(nam, 0);
#ifdef ENV_IGNORECASE
	if (STRCASECMP(nam, PATH_ENV) == 0)
#else
	if (strcmp(nam, PATH_ENV) == 0)
#endif
	{
	    path_tainted = 0;
	}
	return value;
    }
    return Qnil;
}

static VALUE
env_delete_m(VALUE obj, SEL sel, VALUE name)
{
    VALUE val;

    val = env_delete(obj, name);
    if (NIL_P(val) && rb_block_given_p()) rb_yield(name);
    return val;
}

static VALUE
rb_f_getenv(VALUE obj, SEL sel, VALUE name)
{
    const char *nam, *env;

    rb_secure(4);
    SafeStringValue(name);
    nam = RSTRING_PTR(name);
    if (strlen(nam) != RSTRING_LEN(name)) {
	rb_raise(rb_eArgError, "bad environment variable name");
    }
    env = getenv(nam);
    if (env) {
#ifdef ENV_IGNORECASE
	if (STRCASECMP(nam, PATH_ENV) == 0 && !rb_env_path_tainted())
#else
	if (strcmp(nam, PATH_ENV) == 0 && !rb_env_path_tainted())
#endif
	{
	    VALUE str = rb_str_new2(env);

	    rb_obj_freeze(str);
	    return str;
	}
	return env_str_new2(env);
    }
    return Qnil;
}

static VALUE
env_fetch(VALUE rcv, SEL sel, int argc, VALUE *argv)
{
    VALUE key, if_none;
    long block_given;
    const char *nam, *env;

    rb_secure(4);
    rb_scan_args(argc, argv, "11", &key, &if_none);
    block_given = rb_block_given_p();
    if (block_given && argc == 2) {
	rb_warn("block supersedes default value argument");
    }
    SafeStringValue(key);
    nam = RSTRING_PTR(key);
    if (strlen(nam) != RSTRING_LEN(key)) {
	rb_raise(rb_eArgError, "bad environment variable name");
    }
    env = getenv(nam);
    if (!env) {
	if (block_given) return rb_yield(key);
	if (argc == 1) {
	    rb_raise(rb_eKeyError, "key not found");
	}
	return if_none;
    }
#ifdef ENV_IGNORECASE
    if (STRCASECMP(nam, PATH_ENV) == 0 && !rb_env_path_tainted())
#else
    if (strcmp(nam, PATH_ENV) == 0 && !rb_env_path_tainted())
#endif
	return rb_str_new2(env);
    return env_str_new2(env);
}

static void
path_tainted_p(const char *path)
{
    path_tainted = rb_path_check(path)?0:1;
}

int
rb_env_path_tainted(void)
{
    if (path_tainted < 0) {
	path_tainted_p(getenv(PATH_ENV));
    }
    return path_tainted;
}

void
ruby_setenv(const char *name, const char *value)
{
#undef setenv
#undef unsetenv
    if (value)
	setenv(name,value,1);
    else
	unsetenv(name);
}

void
ruby_unsetenv(const char *name)
{
    ruby_setenv(name, 0);
}

static VALUE
env_aset(VALUE obj, SEL sel, VALUE nm, VALUE val)
{
    const char *name, *value;

    if (rb_safe_level() >= 4) {
	rb_raise(rb_eSecurityError, "can't change environment variable");
    }

    if (NIL_P(val)) {
	rb_raise(rb_eTypeError, "cannot assign nil; use Hash#delete instead");
    }
    StringValue(nm);
    StringValue(val);
    name = RSTRING_PTR(nm);
    value = RSTRING_PTR(val);
    if (strlen(name) != RSTRING_LEN(nm))
	rb_raise(rb_eArgError, "bad environment variable name");
    if (strlen(value) != RSTRING_LEN(val))
	rb_raise(rb_eArgError, "bad environment variable value");

    ruby_setenv(name, value);
#ifdef ENV_IGNORECASE
    if (STRCASECMP(name, PATH_ENV) == 0) {
#else
    if (strcmp(name, PATH_ENV) == 0) {
#endif
	if (OBJ_TAINTED(val)) {
	    /* already tainted, no check */
	    path_tainted = 1;
	    return val;
	}
	else {
	    path_tainted_p(value);
	}
    }
    return val;
}

static VALUE
env_keys(VALUE rcv, SEL sel)
{
    char **env;
    VALUE ary;

    rb_secure(4);
    ary = rb_ary_new();
    env = GET_ENVIRON(environ);
    while (*env) {
	char *s = strchr(*env, '=');
	if (s) {
	    rb_ary_push(ary, env_str_new(*env, s-*env));
	}
	env++;
    }
    FREE_ENVIRON(environ);
    return ary;
}

static VALUE
env_each_key(VALUE ehash, SEL sel)
{
    VALUE keys;
    long i;

    RETURN_ENUMERATOR(ehash, 0, 0);
    keys = env_keys(Qnil, 0);	/* rb_secure(4); */
    for (i=0; i<RARRAY_LEN(keys); i++) {
	rb_yield(RARRAY_AT(keys, i));
	RETURN_IF_BROKEN();
    }
    return ehash;
}

static VALUE
env_values(VALUE rcv, SEL sel)
{
    VALUE ary;
    char **env;

    rb_secure(4);
    ary = rb_ary_new();
    env = GET_ENVIRON(environ);
    while (*env) {
	char *s = strchr(*env, '=');
	if (s) {
	    rb_ary_push(ary, env_str_new2(s+1));
	}
	env++;
    }
    FREE_ENVIRON(environ);
    return ary;
}

static VALUE
env_each_value(VALUE ehash, SEL sel)
{
    VALUE values;
    long i;

    RETURN_ENUMERATOR(ehash, 0, 0);
    values = env_values(Qnil, 0);	/* rb_secure(4); */
    for (i=0; i<RARRAY_LEN(values); i++) {
	rb_yield(RARRAY_AT(values, i));
	RETURN_IF_BROKEN();
    }
    return ehash;
}

static VALUE
env_each_pair(VALUE ehash, SEL sel)
{
    char **env;
    VALUE ary;
    long i;

    RETURN_ENUMERATOR(ehash, 0, 0);

    rb_secure(4);
    ary = rb_ary_new();
    env = GET_ENVIRON(environ);
    while (*env) {
	char *s = strchr(*env, '=');
	if (s) {
	    rb_ary_push(ary, env_str_new(*env, s-*env));
	    rb_ary_push(ary, env_str_new2(s+1));
	}
	env++;
    }
    FREE_ENVIRON(environ);

    for (i=0; i<RARRAY_LEN(ary); i+=2) {
	rb_yield(rb_assoc_new(RARRAY_AT(ary, i), RARRAY_AT(ary, i+1)));
	RETURN_IF_BROKEN();
    }
    return ehash;
}

static VALUE
env_reject_bang(VALUE ehash, SEL sel)
{
    volatile VALUE keys;
    long i;
    int del = 0;

    RETURN_ENUMERATOR(ehash, 0, 0);
    keys = env_keys(Qnil, 0);	/* rb_secure(4); */
    for (i=0; i<RARRAY_LEN(keys); i++) {
	VALUE val = rb_f_getenv(Qnil, 0, RARRAY_AT(keys, i));
	if (!NIL_P(val)) {
	    VALUE v = rb_yield_values(2, RARRAY_AT(keys, i), val);
	    RETURN_IF_BROKEN();
	    if (RTEST(v)) {
		rb_obj_untaint(RARRAY_AT(keys, i));
		env_delete(Qnil, RARRAY_AT(keys, i));
		del++;
	    }
	}
    }
    if (del == 0) return Qnil;
    return envtbl;
}

static VALUE
env_delete_if(VALUE ehash, SEL sel)
{
    RETURN_ENUMERATOR(ehash, 0, 0);
    env_reject_bang(ehash, 0);
    return envtbl;
}

static VALUE
env_values_at(VALUE rcv, SEL sel, int argc, VALUE *argv)
{
    VALUE result;
    long i;

    rb_secure(4);
    result = rb_ary_new();
    for (i=0; i<argc; i++) {
	rb_ary_push(result, rb_f_getenv(Qnil, 0, argv[i]));
    }
    return result;
}

static VALUE
env_select(VALUE ehash, SEL sel)
{
    VALUE result;
    char **env;

    RETURN_ENUMERATOR(ehash, 0, 0);
    rb_secure(4);
    result = rb_hash_new();
    env = GET_ENVIRON(environ);
    while (*env) {
	char *s = strchr(*env, '=');
	if (s) {
	    VALUE k = env_str_new(*env, s-*env);
	    VALUE v = env_str_new2(s+1);
	    VALUE v2 = rb_yield_values(2, k, v);
	    RETURN_IF_BROKEN();
	    if (RTEST(v2)) {
		rb_hash_aset(result, k, v);
	    }
	}
	env++;
    }
    FREE_ENVIRON(environ);

    return result;
}

static VALUE
rb_env_clear_imp(VALUE rcv, SEL sel)
{
    VALUE keys;
    long i;

    keys = env_keys(Qnil, 0);	/* rb_secure(4); */
    for (i=0; i<RARRAY_LEN(keys); i++) {
	VALUE val = rb_f_getenv(Qnil, 0, RARRAY_AT(keys, i));
	if (!NIL_P(val)) {
	    env_delete(Qnil, RARRAY_AT(keys, i));
	}
    }
    return envtbl;
}

VALUE
rb_env_clear(void)
{
    return rb_env_clear_imp(Qnil, 0);
}

static VALUE
env_to_s(VALUE rcv, SEL sel)
{
    return rb_usascii_str_new2("ENV");
}

static VALUE
env_inspect(VALUE rcv, SEL sel)
{
    char **env;
    VALUE str, i;

    rb_secure(4);
    str = rb_str_buf_new2("{");
    env = GET_ENVIRON(environ);
    while (*env) {
	char *s = strchr(*env, '=');

	if (env != environ) {
	    rb_str_buf_cat2(str, ", ");
	}
	if (s) {
	    rb_str_buf_cat2(str, "\"");
	    rb_str_buf_cat(str, *env, s-*env);
	    rb_str_buf_cat2(str, "\"=>");
	    i = rb_inspect(rb_str_new2(s+1));
	    rb_str_buf_append(str, i);
	}
	env++;
    }
    FREE_ENVIRON(environ);
    rb_str_buf_cat2(str, "}");
    OBJ_TAINT(str);

    return str;
}

static VALUE
env_to_a(VALUE rcv, SEL sel)
{
    char **env;
    VALUE ary;

    rb_secure(4);
    ary = rb_ary_new();
    env = GET_ENVIRON(environ);
    while (*env) {
	char *s = strchr(*env, '=');
	if (s) {
	    rb_ary_push(ary, rb_assoc_new(env_str_new(*env, s-*env),
					  env_str_new2(s+1)));
	}
	env++;
    }
    FREE_ENVIRON(environ);
    return ary;
}

static VALUE
env_none(VALUE rcv, SEL sel)
{
    return Qnil;
}

static VALUE
env_size(VALUE rcv, SEL sel)
{
    int i;
    char **env;

    rb_secure(4);
    env = GET_ENVIRON(environ);
    for(i=0; env[i]; i++)
	;
    FREE_ENVIRON(environ);
    return INT2FIX(i);
}

static VALUE
env_empty_p(VALUE rcv, SEL sel)
{
    char **env;

    rb_secure(4);
    env = GET_ENVIRON(environ);
    if (env[0] == 0) {
	FREE_ENVIRON(environ);
	return Qtrue;
    }
    FREE_ENVIRON(environ);
    return Qfalse;
}

static VALUE
env_has_key(VALUE env, SEL sel, VALUE key)
{
    char *s;

    rb_secure(4);
    s = StringValuePtr(key);
    if (strlen(s) != RSTRING_LEN(key))
	rb_raise(rb_eArgError, "bad environment variable name");
    if (getenv(s)) return Qtrue;
    return Qfalse;
}

static VALUE
env_assoc(VALUE env, SEL sel, VALUE key)
{
    char *s, *e;

    rb_secure(4);
    s = StringValuePtr(key);
    if (strlen(s) != RSTRING_LEN(key))
	rb_raise(rb_eArgError, "bad environment variable name");
    e = getenv(s);
    if (e) return rb_assoc_new(key, rb_tainted_str_new2(e));
    return Qnil;
}

static VALUE
env_has_value(VALUE dmy, SEL sel, VALUE obj)
{
    char **env;

    rb_secure(4);
    obj = rb_check_string_type(obj);
    if (NIL_P(obj)) return Qnil;
    env = GET_ENVIRON(environ);
    while (*env) {
	char *s = strchr(*env, '=');
	if (s++) {
	    long len = strlen(s);
	    if (RSTRING_LEN(obj) == len && strncmp(s, RSTRING_PTR(obj), len) == 0) {
		FREE_ENVIRON(environ);
		return Qtrue;
	    }
	}
	env++;
    }
    FREE_ENVIRON(environ);
    return Qfalse;
}

static VALUE
env_rassoc(VALUE dmy, VALUE obj)
{
    char **env;

    rb_secure(4);
    obj = rb_check_string_type(obj);
    if (NIL_P(obj)) return Qnil;
    env = GET_ENVIRON(environ);
    while (*env) {
	char *s = strchr(*env, '=');
	if (s++) {
	    long len = strlen(s);
	    if (RSTRING_LEN(obj) == len && strncmp(s, RSTRING_PTR(obj), len) == 0) {
		VALUE result = rb_assoc_new(rb_tainted_str_new(*env, s-*env-1), obj);
		FREE_ENVIRON(environ);
		return result;
	    }
	}
	env++;
    }
    FREE_ENVIRON(environ);
    return Qnil;
}

static VALUE
env_key(VALUE dmy, SEL sel, VALUE value)
{
    char **env;
    VALUE str;

    rb_secure(4);
    StringValue(value);
    env = GET_ENVIRON(environ);
    while (*env) {
	char *s = strchr(*env, '=');
	if (s++) {
	    long len = strlen(s);
	    if (RSTRING_LEN(value) == len && strncmp(s, RSTRING_PTR(value), len) == 0) {
		str = env_str_new(*env, s-*env-1);
		FREE_ENVIRON(environ);
		return str;
	    }
	}
	env++;
    }
    FREE_ENVIRON(environ);
    return Qnil;
}

static VALUE
env_index(VALUE dmy, SEL sel, VALUE value)
{
    rb_warn("ENV.index is deprecated; use ENV.key");
    return env_key(dmy, 0, value);
}

static VALUE
env_to_hash(VALUE rcv, SEL sel)
{
    char **env;
    VALUE hash;

    rb_secure(4);
    hash = rb_hash_new();
    env = GET_ENVIRON(environ);
    while (*env) {
	char *s = strchr(*env, '=');
	if (s) {
	    rb_hash_aset(hash, env_str_new(*env, s-*env),
			       env_str_new2(s+1));
	}
	env++;
    }
    FREE_ENVIRON(environ);
    return hash;
}

static VALUE
env_reject(VALUE rcv, SEL sel)
{
    return rb_hash_delete_if(env_to_hash(Qnil, 0), 0);
}

static VALUE
env_shift(VALUE rcv, SEL sel)
{
    char **env;

    rb_secure(4);
    env = GET_ENVIRON(environ);
    if (*env) {
	char *s = strchr(*env, '=');
	if (s) {
	    VALUE key = env_str_new(*env, s-*env);
	    VALUE val = env_str_new2(getenv(RSTRING_PTR(key)));
	    env_delete(Qnil, key);
	    return rb_assoc_new(key, val);
	}
    }
    FREE_ENVIRON(environ);
    return Qnil;
}

static VALUE
env_invert(VALUE rcv, SEL sel)
{
    return rb_hash_invert(env_to_hash(Qnil, 0), 0);
}

static int
env_replace_i(VALUE key, VALUE val, VALUE keys)
{
    if (key != Qundef) {
	env_aset(Qnil, 0, key, val);
	if (rb_ary_includes(keys, key)) {
	    rb_ary_delete(keys, key);
	}
    }
    return ST_CONTINUE;
}

static VALUE
env_replace(VALUE env, SEL sel, VALUE hash)
{
    volatile VALUE keys;
    long i;

    keys = env_keys(Qnil, 0);	/* rb_secure(4); */
    if (env == hash) return env;
    hash = to_hash(hash);
    rb_hash_foreach(hash, env_replace_i, keys);

    for (i=0; i<RARRAY_LEN(keys); i++) {
	env_delete(env, RARRAY_AT(keys, i));
    }
    return env;
}

static int
env_update_i(VALUE key, VALUE val)
{
    if (key != Qundef) {
	if (rb_block_given_p()) {
	    val = rb_yield_values(3, key, rb_f_getenv(Qnil, 0, key), val);
	    RETURN_IF_BROKEN();
	}
	env_aset(Qnil, 0, key, val);
    }
    return ST_CONTINUE;
}

static VALUE
env_update(VALUE env, SEL sel, VALUE hash)
{
    rb_secure(4);
    if (env == hash) return env;
    hash = to_hash(hash);
    rb_hash_foreach(hash, env_update_i, 0);
    return env;
}

#define PREPARE_RCV(x) \
    Class old = *(Class *)x; \
    *(Class *)x = (Class)rb_cCFHash;

#define RESTORE_RCV(x) \
    *(Class *)x = old;

bool
rb_objc_hash_is_pure(VALUE ary)
{
    return *(Class *)ary == (Class)rb_cCFHash;
}

static CFIndex
imp_rb_hash_count(void *rcv, SEL sel) 
{
    CFIndex count;
    PREPARE_RCV(rcv);
    count = CFDictionaryGetCount((CFDictionaryRef)rcv);
    RESTORE_RCV(rcv);
    return count; 
}

static void *
imp_rb_hash_keyEnumerator(void *rcv, SEL sel)
{
    void *keys;
    static SEL objectEnumerator = 0;
    PREPARE_RCV(rcv);
    keys = (void *)rb_hash_keys((VALUE)rcv, 0);
    RESTORE_RCV(rcv);
    if (objectEnumerator == 0)
	objectEnumerator = sel_registerName("objectEnumerator");
    return objc_msgSend(keys, objectEnumerator);
}

static void *
imp_rb_hash_objectForKey(void *rcv, SEL sel, void *key)
{
    void *obj;
    PREPARE_RCV(rcv);
    if (!CFDictionaryGetValueIfPresent((CFDictionaryRef)rcv, (const void *)key,
	(const void **)&obj)) {
	obj = NULL;
    }
    RESTORE_RCV(rcv);
    return obj;
}

static void 
imp_rb_hash_setObjectForKey(void *rcv, SEL sel, void *obj, void *key) 
{
    PREPARE_RCV(rcv);
    CFDictionarySetValue((CFMutableDictionaryRef)rcv, (const void *)key,
	(const void *)obj);
    RESTORE_RCV(rcv);
} 

static void
imp_rb_hash_getObjectsAndKeys(void *rcv, SEL sel, void **objs, void **keys)
{
    PREPARE_RCV(rcv);
    CFDictionaryGetKeysAndValues((CFDictionaryRef)rcv, (const void **)keys,
	(const void **)objs);
    RESTORE_RCV(rcv);
}

static void
imp_rb_hash_removeObjectForKey(void *rcv, SEL sel, void *key)
{
    PREPARE_RCV(rcv);
    CFDictionaryRemoveValue((CFMutableDictionaryRef)rcv, (const void *)key);
    RESTORE_RCV(rcv);
}

static void
imp_rb_hash_removeAllObjects(void *rcv, SEL sel)
{
    PREPARE_RCV(rcv);
    CFDictionaryRemoveAllValues((CFMutableDictionaryRef)rcv);
    RESTORE_RCV(rcv);
}

static bool
imp_rb_hash_isEqual(void *rcv, SEL sel, void *other)
{
    bool res;
    PREPARE_RCV(rcv);
    res = CFEqual((CFTypeRef)rcv, (CFTypeRef)other);
    RESTORE_RCV(rcv);
    return res;
}

static bool
imp_rb_hash_containsObject(void *rcv, SEL sel, void *obj)
{
    bool res;
    PREPARE_RCV(rcv);
    res = CFDictionaryContainsValue((CFTypeRef)rcv, (const void *)obj);
    RESTORE_RCV(rcv);
    return res;
}

void
rb_objc_install_hash_primitives(Class klass)
{
    rb_objc_install_method2(klass, "count", (IMP)imp_rb_hash_count);
    rb_objc_install_method2(klass, "keyEnumerator", (IMP)imp_rb_hash_keyEnumerator);
    rb_objc_install_method2(klass, "objectForKey:", (IMP)imp_rb_hash_objectForKey);
    rb_objc_install_method2(klass, "getObjects:andKeys:", (IMP)imp_rb_hash_getObjectsAndKeys);
    rb_objc_install_method2(klass, "setObject:forKey:", (IMP)imp_rb_hash_setObjectForKey);
    rb_objc_install_method2(klass, "removeObjectForKey:", (IMP)imp_rb_hash_removeObjectForKey);
    rb_objc_install_method2(klass, "removeAllObjects", (IMP)imp_rb_hash_removeAllObjects);
    rb_objc_install_method2(klass, "isEqual:", (IMP)imp_rb_hash_isEqual);
    rb_objc_install_method2(klass, "containsObject:", (IMP)imp_rb_hash_containsObject);

    rb_objc_define_method(*(VALUE *)klass, "alloc", hash_alloc, 0);
}

/*
 *  A <code>Hash</code> is a collection of key-value pairs. It is
 *  similar to an <code>Array</code>, except that indexing is done via
 *  arbitrary keys of any object type, not an integer index. The order
 *  in which you traverse a hash by either key or value may seem
 *  arbitrary, and will generally not be in the insertion order.
 *
 *  Hashes have a <em>default value</em> that is returned when accessing
 *  keys that do not exist in the hash. By default, that value is
 *  <code>nil</code>.
 *
 */

void
Init_Hash(void)
{
    id_hash = rb_intern("hash");
    id_yield = rb_intern("yield");
    id_default = rb_intern("default");

    rb_cCFHash = (VALUE)objc_getClass("NSCFDictionary");
    rb_const_set(rb_cObject, rb_intern("NSCFDictionary"), rb_cCFHash);
    rb_cHash = rb_cNSHash = (VALUE)objc_getClass("NSDictionary");
    rb_cNSMutableHash = (VALUE)objc_getClass("NSMutableDictionary");
    rb_set_class_path(rb_cNSMutableHash, rb_cObject, "NSMutableDictionary");
    rb_const_set(rb_cObject, rb_intern("Hash"), rb_cNSMutableHash);

    rb_include_module(rb_cHash, rb_mEnumerable);

    /* to return mutable copies */
    rb_objc_define_method(rb_cHash, "dup", rb_hash_dup_imp, 0);
    rb_objc_define_method(rb_cHash, "clone", rb_hash_clone, 0);

    rb_objc_define_method(*(VALUE *)rb_cHash, "[]", rb_hash_s_create, -1);
    rb_objc_define_method(*(VALUE *)rb_cHash, "try_convert", rb_hash_s_try_convert, 1);
    rb_objc_define_method(rb_cHash, "initialize", rb_hash_initialize, -1);
    rb_objc_define_method(rb_cHash, "initialize_copy", rb_hash_replace, 1);
    rb_objc_define_method(rb_cHash, "rehash", rb_hash_rehash, 0);

    rb_objc_define_method(rb_cHash, "to_hash", rb_hash_to_hash, 0);
    rb_objc_define_method(rb_cHash, "to_a", rb_hash_to_a, 0);
    rb_objc_define_method(rb_cHash, "to_s", rb_hash_inspect, 0);
    rb_objc_define_method(rb_cHash, "inspect", rb_hash_inspect, 0);

    rb_objc_define_method(rb_cHash, "==", rb_hash_equal, 1);
    rb_objc_define_method(rb_cHash, "[]", rb_hash_aref_imp, 1);
    rb_objc_define_method(rb_cHash, "eql?", rb_hash_eql, 1);
    rb_objc_define_method(rb_cHash, "fetch", rb_hash_fetch, -1);
    rb_objc_define_method(rb_cHash, "[]=", rb_hash_aset_imp, 2);
    rb_objc_define_method(rb_cHash, "store", rb_hash_aset_imp, 2);
    rb_objc_define_method(rb_cHash, "default", rb_hash_default, -1);
    rb_objc_define_method(rb_cHash, "default=", rb_hash_set_default, 1);
    rb_objc_define_method(rb_cHash, "default_proc", rb_hash_default_proc, 0);
    rb_objc_define_method(rb_cHash, "key", rb_hash_key, 1);
    rb_objc_define_method(rb_cHash, "index", rb_hash_index, 1);
    rb_objc_define_method(rb_cHash, "size", rb_hash_size, 0);
    rb_objc_define_method(rb_cHash, "length", rb_hash_size, 0);
    rb_objc_define_method(rb_cHash, "empty?", rb_hash_empty_p, 0);

    rb_objc_define_method(rb_cHash, "each_value", rb_hash_each_value, 0);
    rb_objc_define_method(rb_cHash, "each_key", rb_hash_each_key, 0);
    rb_objc_define_method(rb_cHash, "each_pair", rb_hash_each_pair, 0);
    rb_objc_define_method(rb_cHash, "each", rb_hash_each_pair, 0);

    rb_objc_define_method(rb_cHash, "keys", rb_hash_keys, 0);
    rb_objc_define_method(rb_cHash, "values", rb_hash_values, 0);
    rb_objc_define_method(rb_cHash, "values_at", rb_hash_values_at, -1);

    rb_objc_define_method(rb_cHash, "shift", rb_hash_shift, 0);
    rb_objc_define_method(rb_cHash, "delete", rb_hash_delete_imp, 1);
    rb_objc_define_method(rb_cHash, "delete_if", rb_hash_delete_if, 0);
    rb_objc_define_method(rb_cHash, "select", rb_hash_select, 0);
    rb_objc_define_method(rb_cHash, "reject", rb_hash_reject, 0);
    rb_objc_define_method(rb_cHash, "reject!", rb_hash_reject_bang, 0);
    rb_objc_define_method(rb_cHash, "clear", rb_hash_clear, 0);
    rb_objc_define_method(rb_cHash, "invert", rb_hash_invert, 0);

    /* to override the private -[NSMutableDictionary invert] method */
    rb_objc_define_method(rb_cNSMutableHash, "invert", rb_hash_invert, 0);

    rb_objc_define_method(rb_cHash, "update", rb_hash_update, 1);
    rb_objc_define_method(rb_cHash, "replace", rb_hash_replace, 1);
    rb_objc_define_method(rb_cHash, "merge!", rb_hash_update, 1);
    rb_objc_define_method(rb_cHash, "merge", rb_hash_merge, 1);
    rb_objc_define_method(rb_cHash, "assoc", rb_hash_assoc, 1);
    rb_objc_define_method(rb_cHash, "rassoc", rb_hash_rassoc, 1);
    rb_objc_define_method(rb_cHash, "flatten", rb_hash_flatten, -1);

    rb_objc_define_method(rb_cHash, "include?", rb_hash_has_key, 1);
    rb_objc_define_method(rb_cHash, "member?", rb_hash_has_key, 1);
    rb_objc_define_method(rb_cHash, "has_key?", rb_hash_has_key, 1);
    rb_objc_define_method(rb_cHash, "has_value?", rb_hash_has_value, 1);
    rb_objc_define_method(rb_cHash, "key?", rb_hash_has_key, 1);
    rb_objc_define_method(rb_cHash, "value?", rb_hash_has_value, 1);

    rb_objc_define_method(rb_cHash, "compare_by_identity", rb_hash_compare_by_id, 0);
    rb_objc_define_method(rb_cHash, "compare_by_identity?", rb_hash_compare_by_id_p, 0);

    origenviron = environ;
    envtbl = rb_obj_alloc(rb_cObject);
    rb_extend_object(envtbl, rb_mEnumerable);

    rb_objc_define_method(*(VALUE *)envtbl, "[]", rb_f_getenv, 1);
    rb_objc_define_method(*(VALUE *)envtbl, "fetch", env_fetch, -1);
    rb_objc_define_method(*(VALUE *)envtbl, "[]=", env_aset, 2);
    rb_objc_define_method(*(VALUE *)envtbl, "store", env_aset, 2);
    rb_objc_define_method(*(VALUE *)envtbl, "each", env_each_pair, 0);
    rb_objc_define_method(*(VALUE *)envtbl, "each_pair", env_each_pair, 0);
    rb_objc_define_method(*(VALUE *)envtbl, "each_key", env_each_key, 0);
    rb_objc_define_method(*(VALUE *)envtbl, "each_value", env_each_value, 0);
    rb_objc_define_method(*(VALUE *)envtbl, "delete", env_delete_m, 1);
    rb_objc_define_method(*(VALUE *)envtbl, "delete_if", env_delete_if, 0);
    rb_objc_define_method(*(VALUE *)envtbl, "clear", rb_env_clear_imp, 0);
    rb_objc_define_method(*(VALUE *)envtbl, "reject", env_reject, 0);
    rb_objc_define_method(*(VALUE *)envtbl, "reject!", env_reject_bang, 0);
    rb_objc_define_method(*(VALUE *)envtbl, "select", env_select, 0);
    rb_objc_define_method(*(VALUE *)envtbl, "shift", env_shift, 0);
    rb_objc_define_method(*(VALUE *)envtbl, "invert", env_invert, 0);
    rb_objc_define_method(*(VALUE *)envtbl, "replace", env_replace, 1);
    rb_objc_define_method(*(VALUE *)envtbl, "update", env_update, 1);
    rb_objc_define_method(*(VALUE *)envtbl, "inspect", env_inspect, 0);
    rb_objc_define_method(*(VALUE *)envtbl, "rehash", env_none, 0);
    rb_objc_define_method(*(VALUE *)envtbl, "to_a", env_to_a, 0);
    rb_objc_define_method(*(VALUE *)envtbl, "to_s", env_to_s, 0);
    rb_objc_define_method(*(VALUE *)envtbl, "key", env_key, 1);
    rb_objc_define_method(*(VALUE *)envtbl, "index", env_index, 1);
    rb_objc_define_method(*(VALUE *)envtbl, "size", env_size, 0);
    rb_objc_define_method(*(VALUE *)envtbl, "length", env_size, 0);
    rb_objc_define_method(*(VALUE *)envtbl, "empty?", env_empty_p, 0);
    rb_objc_define_method(*(VALUE *)envtbl, "keys", env_keys, 0);
    rb_objc_define_method(*(VALUE *)envtbl, "values", env_values, 0);
    rb_objc_define_method(*(VALUE *)envtbl, "values_at", env_values_at, -1);
    rb_objc_define_method(*(VALUE *)envtbl, "include?", env_has_key, 1);
    rb_objc_define_method(*(VALUE *)envtbl, "member?", env_has_key, 1);
    rb_objc_define_method(*(VALUE *)envtbl, "has_key?", env_has_key, 1);
    rb_objc_define_method(*(VALUE *)envtbl, "has_value?", env_has_value, 1);
    rb_objc_define_method(*(VALUE *)envtbl, "key?", env_has_key, 1);
    rb_objc_define_method(*(VALUE *)envtbl, "value?", env_has_value, 1);
    rb_objc_define_method(*(VALUE *)envtbl, "to_hash", env_to_hash, 0);
    rb_objc_define_method(*(VALUE *)envtbl, "assoc", env_assoc, 1);
    rb_objc_define_method(*(VALUE *)envtbl, "rassoc", env_rassoc, 1);

    rb_define_global_const("ENV", envtbl);
}
