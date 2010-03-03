/* 
 * MacRuby Symbols.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 * 
 * Copyright (C) 2010, Apple Inc. All rights reserved.
 */

#include <wctype.h>

#include "ruby.h"
#include "ruby/encoding.h"
#include "encoding.h"
#include "symbol.h"
#include "ruby/node.h"
#include "vm.h"
#include "objc.h"

VALUE rb_cSymbol;

static CFMutableDictionaryRef sym_id = NULL, id_str = NULL;
static long last_id = 0;

typedef struct {
    VALUE klass;
    VALUE str;
    ID id;
} rb_sym_t;

#define RSYM(obj) ((rb_sym_t *)(obj))

static rb_sym_t *
sym_alloc(VALUE str, ID id)
{
    rb_sym_t *sym = (rb_sym_t *)malloc(sizeof(rb_sym_t));
    assert(rb_cSymbol != 0);
    sym->klass = rb_cSymbol;
    GC_RETAIN(str); // never released
    sym->str = str;
    sym->id = id;
    return sym;
}

ID
rb_intern_str(VALUE str)
{
    const unsigned long name_hash = rb_str_hash(str);
    ID id = (ID)CFDictionaryGetValue(sym_id, (const void *)name_hash); 
    if (id != 0) {
	return id;
    } 

    rb_sym_t *sym = NULL;

    const long chars_len = rb_str_chars_len(str);
    if (chars_len > 0) {
	UChar c = rb_str_get_uchar(str, 0);
	switch (c) {
	    case '$':
		id = ID_GLOBAL;
		break;

	    case '@':
		if (chars_len > 1 && rb_str_get_uchar(str, 1) == '@') {
		    id = ID_CLASS;
		}
		else {
		    id = ID_INSTANCE;
		}
		break;

	    default:
		if (chars_len > 1
			&& rb_str_get_uchar(str, chars_len - 1) == '=') {
		    // Attribute assignment.
		    id = rb_intern_str(rb_str_substr(str, 0, chars_len - 1));
		    if (!is_attrset_id(id)) {
			id = rb_id_attrset(id);
			goto id_register;
		    }
		    id = ID_ATTRSET;
		}
		else if (iswupper(c)) {
		    id = ID_CONST;
		}
		else {
		    id = ID_LOCAL;
		}
		break;
	}
    }

    id |= ++last_id << ID_SCOPE_SHIFT;

id_register:
//printf("register %s hash %ld id %ld\n", RSTRING_PTR(str), name_hash, id);
    sym = sym_alloc(str, id);
    CFDictionarySetValue(sym_id, (const void *)name_hash, (const void *)id);
    CFDictionarySetValue(id_str, (const void *)id, (const void *)sym);

    return id;
}

VALUE
rb_id2str(ID id)
{
    VALUE sym = (VALUE)CFDictionaryGetValue(id_str, (const void *)id);
    if (sym != 0) {
//printf("lookup %ld -> %s\n", id, rb_sym2name(sym));
	return sym;
    }

    if (is_attrset_id(id)) {
	// Attribute assignment.
	ID id2 = (id & ~ID_SCOPE_MASK) | ID_LOCAL;

	while ((sym = rb_id2str(id2)) == 0) {
	    if (!is_local_id(id2)) {
//printf("lookup %ld -> FAIL\n", id);
		return 0;
	    }
	    id2 = (id & ~ID_SCOPE_MASK) | ID_CONST;
	}

	VALUE str = rb_str_dup(RSYM(sym)->str);
	rb_str_cat(str, "=", 1);
	rb_intern_str(str);

	// Retry one more time.
	sym = (VALUE)CFDictionaryGetValue(id_str, (const void *)id);
	if (sym != 0) {
//printf("lookup %ld -> %s\n", id, rb_sym2name(sym));
	    return sym;
	}
    }
//printf("lookup %ld -> FAIL\n", id);
    return 0;
}

ID
rb_intern3(const char *name, long len, rb_encoding *enc)
{
    VALUE str = rb_enc_str_new(name, len, enc);
    return rb_intern_str(str);
}

ID
rb_intern2(const char *name, long len)
{
    return rb_intern_str(rb_str_new(name, len));
}

ID
rb_intern(const char *name)
{
    return rb_intern_str(rb_str_new2(name));
}

ID
rb_sym2id(VALUE sym)
{
    return RSYM(sym)->id;
}

VALUE
rb_name2sym(const char *name)
{
    return rb_id2str(rb_intern(name));
}

VALUE
rb_sym_to_s(VALUE sym)
{
    return rb_str_dup(RSYM(sym)->str);
}

const char *
rb_sym2name(VALUE sym)
{
    return RSTRING_PTR(RSYM(sym)->str);
}

/*
 *  call-seq:
 *     Symbol.all_symbols    => array
 *
 *  Returns an array of all the symbols currently in Ruby's symbol
 *  table.
 *
 *     Symbol.all_symbols.size    #=> 903
 *     Symbol.all_symbols[1,20]   #=> [:floor, :ARGV, :Binding, :symlink,
 *                                     :chown, :EOFError, :$;, :String,
 *                                     :LOCK_SH, :"setuid?", :$<,
 *                                     :default_proc, :compact, :extend,
 *                                     :Tms, :getwd, :$=, :ThreadGroup,
 *                                     :wait2, :$>]
 */

static VALUE
rsym_all_symbols(VALUE klass, SEL sel)
{
    VALUE ary = rb_ary_new();
    const long count = CFDictionaryGetCount(id_str);
    if (count >= 0) {
	const void **values = (const void **)malloc(sizeof(void *) * count);
	CFDictionaryGetKeysAndValues(id_str, NULL, values);
	for (long i = 0; i < count; i++) {
	    rb_ary_push(ary, (VALUE)values[i]);
	}
	free(values);
    }
    return ary;
}

void
Init_PreSymbol(void)
{
    sym_id = CFDictionaryCreateMutable(NULL, 0, NULL, NULL);
    id_str = CFDictionaryCreateMutable(NULL, 0, NULL, NULL);
    last_id = 1000;

    // Pre-register parser symbols.
    for (int i = 0; rb_op_tbl[i].token != 0; i++) {
	ID id = rb_op_tbl[i].token;
	VALUE str = rb_str_new2(rb_op_tbl[i].name);
	rb_sym_t *sym = sym_alloc(str, id);
	unsigned long name_hash = rb_str_hash(str);

//printf("pre-register %s hash %ld id %ld\n", RSTRING_PTR(str), name_hash, id);

	CFDictionarySetValue(sym_id, (const void *)name_hash, (const void *)id);
	CFDictionarySetValue(id_str, (const void *)id, (const void *)sym);
    }
}

/*
 * call-seq:
 *
 *   str <=> other       => -1, 0, +1 or nil
 *
 * Compares _sym_ with _other_ in string form.
 */

static VALUE
rsym_cmp(VALUE sym, SEL sel, VALUE other)
{
    if (TYPE(other) != T_SYMBOL) {
	return Qnil;
    }
    return INT2FIX(rb_str_cmp(RSYM(sym)->str, RSYM(other)->str));
}

/*
 *  call-seq:
 *     sym == obj   => true or false
 *  
 *  Equality---If <i>sym</i> and <i>obj</i> are exactly the same
 *  symbol, returns <code>true</code>. Otherwise, compares them
 *  as strings.
 */

static VALUE
rsym_equal(VALUE sym, SEL sel, VALUE other)
{
    return sym == other ? Qtrue : Qfalse;
}

/*
 *  call-seq:
 *     sym.inspect    => string
 *  
 *  Returns the representation of <i>sym</i> as a symbol literal.
 *     
 *     :fred.inspect   #=> ":fred"
 */

static bool
sym_should_be_escaped(VALUE sym)
{
    UChar *chars = NULL;
    long chars_len = 0;
    bool need_free = false;
    rb_str_get_uchars(RSYM(sym)->str, &chars, &chars_len, &need_free);

    // TODO: this is really not enough, we should mimic 1.9's
    // rb_enc_symname2_p() function.
    bool escape = false;
    for (long i = 0; i < chars_len; i++) {
	if (!iswprint(chars[i])
		|| iswspace(chars[i])) {
	    escape = true;
	    break;
	}
    }

    if (need_free) {
	free(chars);
    }

    return escape;
}

static VALUE
rsym_inspect(VALUE sym, SEL sel)
{
    VALUE str = rb_str_new2(":");
    if (sym_should_be_escaped(sym)) {
	rb_str_concat(str, rb_str_inspect(RSYM(sym)->str));
    }
    else {
	rb_str_concat(str, RSYM(sym)->str);
    }
    return str;
}

/*
 * call-seq:
 *   sym.to_proc
 *
 * Returns a _Proc_ object which respond to the given method by _sym_.
 *
 *   (1..3).collect(&:to_s)  #=> ["1", "2", "3"]
 */

static VALUE
rsym_to_proc(VALUE sym, SEL sel)
{
    SEL msel = sel_registerName(rb_id2name(SYM2ID(sym)));
    rb_vm_block_t *b = rb_vm_create_block_calling_sel(msel);
    return rb_proc_alloc_with_block(rb_cProc, b);
}

/*
 *  call-seq:
 *     sym.id2name   => string
 *     sym.to_s      => string
 *  
 *  Returns the name or string corresponding to <i>sym</i>.
 *     
 *     :fred.id2name   #=> "fred"
 */

static VALUE
rsym_to_s(VALUE sym, SEL sel)
{
    return rb_sym_to_s(sym);
}

/*
 * call-seq:
 *   sym.to_sym   => sym
 *   sym.intern   => sym
 *
 * In general, <code>to_sym</code> returns the <code>Symbol</code>
 * corresponding to an object. As <i>sym</i> is already a symbol,
 * <code>self</code> is returned in this case.
 */

static VALUE
rsym_to_sym(VALUE sym, SEL sel)
{
    return sym;
}

/*
 * call-seq:
 *   sym.empty?   => true or false
 *
 * Returns that _sym_ is :"" or not.
 */

static VALUE
rsym_empty(VALUE sym, SEL sel)
{
    return rb_str_chars_len(RSYM(sym)->str) == 0 ? Qtrue : Qfalse;
}

static CFIndex
rsym_imp_length(void *rcv, SEL sel)
{
    return CFStringGetLength((CFStringRef)RSYM(rcv)->str);
}

static UniChar
rsym_imp_characterAtIndex(void *rcv, SEL sel, CFIndex idx)
{
    return CFStringGetCharacterAtIndex((CFStringRef)RSYM(rcv)->str, idx);
}

void
Init_Symbol(void)
{
    // rb_cSymbol is defined earlier in Init_PreVM().
    rb_set_class_path(rb_cSymbol, rb_cObject, "Symbol");
    rb_const_set(rb_cObject, rb_intern("Symbol"), rb_cSymbol);

    rb_undef_alloc_func(rb_cSymbol);
    rb_undef_method(*(VALUE *)rb_cSymbol, "new");
    rb_objc_define_method(*(VALUE *)rb_cSymbol, "all_symbols",
	    rsym_all_symbols, 0);

    rb_objc_define_method(rb_cSymbol, "==", rsym_equal, 1);
    rb_objc_define_method(rb_cSymbol, "<=>", rsym_cmp, 1);
    rb_objc_define_method(rb_cSymbol, "eql?", rsym_equal, 1);
    rb_objc_define_method(rb_cSymbol, "inspect", rsym_inspect, 0);
    rb_objc_define_method(rb_cSymbol, "to_proc", rsym_to_proc, 0);
    rb_objc_define_method(rb_cSymbol, "to_s", rsym_to_s, 0);
    rb_objc_define_method(rb_cSymbol, "id2name", rsym_to_s, 0);
    rb_objc_define_method(rb_cSymbol, "description", rsym_to_s, 0);
    rb_objc_define_method(rb_cSymbol, "intern", rsym_to_sym, 0);
    rb_objc_define_method(rb_cSymbol, "to_sym", rsym_to_sym, 0);
    rb_objc_define_method(rb_cSymbol, "empty?", rsym_empty, 0);

    // Cocoa primitives.
    rb_objc_install_method2((Class)rb_cSymbol, "length",
	    (IMP)rsym_imp_length);
    rb_objc_install_method2((Class)rb_cSymbol, "characterAtIndex:",
	    (IMP)rsym_imp_characterAtIndex);
}
