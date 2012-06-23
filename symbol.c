/*
 * MacRuby Symbols.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2010-2011, Apple Inc. All rights reserved.
 */

#include <wctype.h>

#include "macruby_internal.h"
#include "ruby/encoding.h"
#include "encoding.h"
#include "symbol.h"
#include "ruby/node.h"
#include "vm.h"
#include "objc.h"

VALUE rb_cSymbol;

static pthread_mutex_t local_lock = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() (assert(pthread_mutex_lock(&local_lock) == 0))
#define UNLOCK() (assert(pthread_mutex_unlock(&local_lock) == 0))

static CFMutableDictionaryRef sym_id = NULL, id_str = NULL;
static long last_id = 0;

typedef struct rb_sym_t {
    VALUE klass;
    VALUE str;
    ID id;
} rb_sym_t;

#define RSYM(obj) ((rb_sym_t *)(obj))

static rb_sym_t *
sym_alloc(VALUE str, ID id)
{
    rb_sym_t *sym = (rb_sym_t *)xmalloc(sizeof(rb_sym_t));
    assert(rb_cSymbol != 0);
    sym->klass = rb_cSymbol;
    GC_WB(&sym->str, str);
    sym->id = id;
    return sym;
}

static bool
is_identchar(UChar c)
{
    return isalnum(c) || c == '_' || !isascii(c);
}

static ID
rb_intern_uchars(const UChar *chars, const size_t chars_len, VALUE str)
{
    const unsigned long name_hash = rb_str_hash_uchars(chars, chars_len);
    LOCK();
    ID id = (ID)CFDictionaryGetValue(sym_id, (const void *)name_hash); 
    UNLOCK();
    if (id != 0) {
	goto return_id;
    }
 
    if (str == Qnil) {
	str = rb_unicode_str_new(chars, chars_len);
    }

    rb_sym_t *sym = NULL;
    long pos = 0;
    if (chars_len > 0) {
	UChar c = chars[0];
	switch (c) {
	    case '$':
		id = ID_GLOBAL;
		goto new_id;

	    case '@':
		if (chars_len > 1 && chars[1] == '@') {
		    pos++;
		    id = ID_CLASS;
		}
		else {
		    id = ID_INSTANCE;
		}
		pos++;
		break;

	    default:
		if (chars_len > 1 && chars[chars_len - 1] == '=') {
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

    if (pos < chars_len && !isdigit(chars[pos])) {
	for (; pos < chars_len; pos++) {
	    if (!is_identchar(chars[pos])) {
		break;
	    }
	}
    }
    if (pos < chars_len) {
	id = ID_JUNK;
    }

new_id:
    id |= ++last_id << ID_SCOPE_SHIFT;

id_register:
//printf("register %s hash %ld id %ld\n", RSTRING_PTR(str), name_hash, id);
    sym = sym_alloc(str, id);
    LOCK();
    CFDictionarySetValue(sym_id, (const void *)name_hash, (const void *)id);
    CFDictionarySetValue(id_str, (const void *)id, (const void *)sym);
    UNLOCK();

return_id:
    return id;
}

ID
rb_intern_str(VALUE str)
{
    RB_STR_GET_UCHARS(str, chars, chars_len);
    return rb_intern_uchars(chars, chars_len, str);
}

VALUE
rb_id2str(ID id)
{
    LOCK();
    VALUE sym = (VALUE)CFDictionaryGetValue(id_str, (const void *)id);
    UNLOCK();
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
	LOCK();
	sym = (VALUE)CFDictionaryGetValue(id_str, (const void *)id);
	UNLOCK();
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
    UChar buf[64];
    UChar *chars = len < 64
	? buf
	: (UChar *)xmalloc(sizeof(UChar) * len);
    for (long i = 0; i < len; i++) {
	chars[i] = name[i];
    }
    return rb_intern_uchars(chars, len, Qnil);
}

ID
rb_intern(const char *name)
{
    return rb_intern2(name, strlen(name));
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

#if MACRUBY_STATIC
struct rb_op_tbl_entry rb_op_tbl[] = {
    {'+', "+"},
    {'-', "-"},
    {'*', "*"},
    {'/', "/"},
    {'%', "%"},
    {'|', "|"},
    {'^', "^"},
    {'&', "&"},
    {'!', "!"},
    {'>', ">"},
    {'<', "<"},
    {'~', "~"},
    {'!', "!"},
    {'`', "`"},
    {334, ".."},
    {335, "..."},
    {323, "**"},
    {321, "+@"},
    {322, "-@"},
    {324, "<=>"},
    {328, ">="},
    {329, "<="},
    {325, "=="},
    {326, "==="},
    {327, "!="},
    {332, "=~"},
    {333, "!~"},
    {336, "[]"},
    {337, "[]="},
    {338, "<<"},
    {339, ">>"},
    {340, "::"},
    {0,   NULL}
};
#endif

void
Init_PreSymbol(void)
{
    sym_id = CFDictionaryCreateMutable(NULL, 0, NULL, NULL);
    assert(sym_id != NULL);
    id_str = CFDictionaryCreateMutable(NULL, 0, NULL,
	    &kCFTypeDictionaryValueCallBacks);
    assert(id_str != NULL);
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
 *   sym.succ
 *
 * Same as <code>sym.to_s.succ.intern</code>.
 */

static VALUE
rsym_succ(VALUE sym, SEL sel)
{
    return rb_str_intern(rb_str_succ(rb_sym_to_s(sym)));
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
 * call-seq:
 *
 *   sym.casecmp(other)  => -1, 0, +1 or nil
 *
 * Case-insensitive version of <code>Symbol#<=></code>.
 */

static VALUE
rsym_casecmp(VALUE sym, SEL sel, VALUE other)
{
    if (TYPE(other) != T_SYMBOL) {
	return Qnil;
    }
    return INT2FIX(rb_str_casecmp(RSYM(sym)->str, RSYM(other)->str));
}

/*
 *  call-seq:
 *     sym == obj   => true or false
 *
 *  Equality---If <i>sym</i> and <i>obj</i> are exactly the same
 *  symbol, returns <code>true</code>.
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
is_special_global_name(UChar *ptr, long len)
{
    if (len <= 0) {
	return false;
    }

    long pos = 0;
    switch (ptr[pos]) {
	case '~': case '*': case '$': case '?': case '!':
	case '@': case '/': case '\\': case ';': case ',':
	case '.': case '=': case ':': case '<': case '>': 
	case '\"': case '&': case '`': case '\'': case '+': case '0':
	    pos++;
	    break;

	case '-':
	    pos++;
	    if (pos < len && is_identchar(ptr[pos])) {
		pos++;
	    }
	    break;

	default:
	    if (!isdigit(ptr[pos])) {
		return false;
	    }
	    do {
		pos++;
	    }
	    while (pos < len && isdigit(ptr[pos]));
	    break;
    }
    return pos == len;
}

static bool
sym_should_be_escaped(VALUE sym)
{
    RB_STR_GET_UCHARS(RSYM(sym)->str, chars, chars_len);

    if (chars_len == 0) {
	return true;
    }

    bool escape = false;
    for (long i = 0; i < chars_len; i++) {
	if (!isprint(chars[i])) {
	    escape = true;
	    break;
	}
    }

    if (escape) {
	goto bail;
    }

    long pos = 0;
    bool localid = false;

    switch (chars[pos]) {
	case '\0':
	    escape = true;
	    break;

	case '$':
	    pos++;
	    if (pos < chars_len && is_special_global_name(&chars[pos],
			chars_len - pos)) {
		goto bail;
	    }
	    goto id;

	case '@':
	    pos++;
	    if (pos < chars_len && chars[pos] == '@') {
		pos++;
	    }
	    goto id;

	case '<':
	    pos++;
	    if (pos < chars_len) {
		if (chars[pos] == '<') {
		    pos++;
		}
		else if (chars[pos] == '=') {
		    pos++;
		    if (pos < chars_len && chars[pos] == '>') {
			pos++;
		    }
		}
	    }
	    break;

	case '>':
	    pos++;
	    if (pos < chars_len) {
		if (chars[pos] == '>' || chars[pos] == '=') {
		    pos++;
		}
	    }
	    break;

	case '=':
	    pos++;
	    if (pos == chars_len) {
		escape = true;
		goto bail;
	    }
	    else {
		if (chars[pos] == '~') {
		    pos++;
		}
		else if (chars[pos] == '=') {
		    pos++;
		    if (pos < chars_len && chars[pos] == '=') {
			pos++;
		    }
		}
		else {
		    escape = true;
		    goto bail;
		}
	    }
	    break;

	case '*':
	    pos++;
	    if (pos < chars_len && chars[pos] == '*') {
		pos++;
	    }
	    break;

	case '+':
	case '-':
	    pos++;
	    if (pos < chars_len && chars[pos] == '@') {
		pos++;
	    }
	    break;

	case '|': case '^': case '&': case '/':
	case '%': case '~': case '`':
	    pos++;
	    break;

	case '[':
	    pos++;
	    if (pos < chars_len && chars[pos] != ']') {
		escape = true;
		goto bail;
	    }
	    pos++;
	    if (pos < chars_len && chars[pos] == '=') {
		pos++;
	    }
	    break;

	case '!':
	    pos++;
	    if (pos == chars_len) {
		goto bail;
	    }
	    else {
		if (chars[pos] == '=' || chars[pos] == '~') {
		    pos++;
		}
		else {
		    escape = true;
		    goto bail;
		}
	    }
	    break;

	default:
	    localid = !isupper(chars[pos]);
	    // fall through	

	id:
	    if (pos >= chars_len
		    || (chars[pos] != '_' && !isalpha(chars[pos])
			&& isascii(chars[pos]))) {
		escape = true;
		goto bail;
	    }
	    while (pos < chars_len && is_identchar(chars[pos])) {
		pos++;
	    }
	    if (localid) {
		if (pos < chars_len
			&& (chars[pos] == '!' || chars[pos] == '?'
			    || chars[pos] == '=')) {
		    pos++;
		}
	    }
	    break;
    }

    if (pos < chars_len) {
	escape = true;
    }

bail:
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
    ID mid = SYM2ID(sym);
    rb_vm_block_t *b = rb_vm_create_block_calling_mid(mid);
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

/*
 * call-seq:
 *   sym[idx]      => char
 *   sym[b, n]     => char
 *
 * Returns <code>sym.to_s[]</code>.
 */

static VALUE
rsym_aref(VALUE sym, SEL sel, int argc, VALUE *argv)
{
    return rstr_aref(RSYM(sym)->str, sel, argc, argv);
}

/*
 * call-seq:
 *   sym =~ obj   -> fixnum or nil
 *
 * Returns <code>sym.to_s =~ obj</code>.
 */

static VALUE
rsym_match(VALUE sym, SEL sel, VALUE other)
{
    return rb_str_match(rb_sym_to_s(sym), other);
}

/*
 * call-seq:
 *   sym.upcase    => symbol
 *
 * Same as <code>sym.to_s.upcase.intern</code>.
 */

static VALUE
rsym_upcase(VALUE sym, SEL sel)
{
    return ID2SYM(rb_intern_str(rstr_upcase(RSYM(sym)->str, sel)));
}

/*
 * call-seq:
 *   sym.downcase    => symbol
 *
 * Same as <code>sym.to_s.downcase.intern</code>.
 */

static VALUE
rsym_downcase(VALUE sym, SEL sel)
{
    return ID2SYM(rb_intern_str(rstr_downcase(RSYM(sym)->str, sel)));
}

/*
 * call-seq:
 *   sym.capitalize  => symbol
 *
 * Same as <code>sym.to_s.capitalize.intern</code>.
 */

static VALUE
rsym_capitalize(VALUE sym, SEL sel)
{
    return ID2SYM(rb_intern_str(rstr_capitalize(RSYM(sym)->str, sel)));
}

/*
 * call-seq:
 *   sym.swapcase  => symbol
 *
 * Same as <code>sym.to_s.swapcase.intern</code>.
 */

static VALUE
rsym_swapcase(VALUE sym, SEL sel)
{
    return ID2SYM(rb_intern_str(rstr_swapcase(RSYM(sym)->str, sel)));
}

// Cocoa primitives

static void *
rsym_imp_copy(void *rcv, SEL sel)
{
    return rcv;
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

#define RSYM_NSCODER_KEY "MRSymbolStr"

static void
rsym_imp_encodeWithCoder(void *rcv, SEL sel, void *coder)
{
    rb_str_NSCoder_encode(coder, RSYM(rcv)->str, RSYM_NSCODER_KEY);
}

static VALUE
rsym_imp_initWithCoder(void *rcv, SEL sel, void *coder)
{
    return ID2SYM(rb_intern_str(rb_str_NSCoder_decode(coder, RSYM_NSCODER_KEY)));
}

static Class
rsym_imp_classForKeyedArchiver(void *rcv, SEL sel)
{
    return (Class)rb_cSymbol;
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

    // Undefine methods defined on NSString.
    rb_undef_method(rb_cSymbol, "to_i");
    rb_undef_method(rb_cSymbol, "to_f");
    rb_undef_method(rb_cSymbol, "to_r");
    rb_undef_method(rb_cSymbol, "to_str");

    rb_objc_define_method(rb_cSymbol, "==", rsym_equal, 1);
    rb_objc_define_method(rb_cSymbol, "<=>", rsym_cmp, 1);
    rb_objc_define_method(rb_cSymbol, "casecmp", rsym_casecmp, 1);
    rb_objc_define_method(rb_cSymbol, "eql?", rsym_equal, 1);
    rb_objc_define_method(rb_cSymbol, "inspect", rsym_inspect, 0);
    rb_objc_define_method(rb_cSymbol, "to_proc", rsym_to_proc, 0);
    rb_objc_define_method(rb_cSymbol, "to_s", rsym_to_s, 0);
    rb_objc_define_method(rb_cSymbol, "id2name", rsym_to_s, 0);
    rb_objc_define_method(rb_cSymbol, "description", rsym_to_s, 0);
    rb_objc_define_method(rb_cSymbol, "intern", rsym_to_sym, 0);
    rb_objc_define_method(rb_cSymbol, "to_sym", rsym_to_sym, 0);
    rb_objc_define_method(rb_cSymbol, "empty?", rsym_empty, 0);
    rb_objc_define_method(rb_cSymbol, "[]", rsym_aref, -1);
    rb_objc_define_method(rb_cSymbol, "=~", rsym_match, 1);
    rb_objc_define_method(rb_cSymbol, "match", rsym_match, 1);
    rb_objc_define_method(rb_cSymbol, "upcase", rsym_upcase, 0);
    rb_objc_define_method(rb_cSymbol, "downcase", rsym_downcase, 0);
    rb_objc_define_method(rb_cSymbol, "swapcase", rsym_swapcase, 0);
    rb_objc_define_method(rb_cSymbol, "capitalize", rsym_capitalize, 0);
    rb_objc_define_method(rb_cSymbol, "succ", rsym_succ, 0);
    rb_objc_define_method(rb_cSymbol, "next", rsym_succ, 0);

    // Cocoa primitives.
    rb_objc_install_method2((Class)rb_cSymbol, "copy",
	    (IMP)rsym_imp_copy);
    rb_objc_install_method2((Class)rb_cSymbol, "length",
	    (IMP)rsym_imp_length);
    rb_objc_install_method2((Class)rb_cSymbol, "characterAtIndex:",
	    (IMP)rsym_imp_characterAtIndex);
    rb_objc_install_method2((Class)rb_cSymbol, "encodeWithCoder:",
	    (IMP)rsym_imp_encodeWithCoder);
    rb_objc_install_method2((Class)rb_cSymbol, "initWithCoder:",
	    (IMP)rsym_imp_initWithCoder);
    rb_objc_install_method2((Class)rb_cSymbol, "classForKeyedArchiver",
	    (IMP)rsym_imp_classForKeyedArchiver);
}

int
rb_is_const_id(ID id)
{
    return is_const_id(id) ? Qtrue : Qfalse;
}

int
rb_is_class_id(ID id)
{
    return is_class_id(id) ? Qtrue : Qfalse;
}

int
rb_is_instance_id(ID id)
{
    return is_instance_id(id) ? Qtrue : Qfalse;
}

int
rb_is_local_id(ID id)
{
    return is_local_id(id) ? Qtrue : Qfalse;
}

int
rb_is_junk_id(ID id)
{
    return is_junk_id(id) ? Qtrue : Qfalse;
}

ID
rb_id_attrset(ID id)
{
    id &= ~ID_SCOPE_MASK;
    id |= ID_ATTRSET;
    return id;
}

VALUE
rb_sym_str(VALUE sym)
{
    return RSYM(sym)->str;
}

VALUE
rb_sel_to_sym(SEL sel)
{
    if (sel == 0) {
	return Qnil;
    }
    return ID2SYM(rb_intern(sel_getName(sel)));
}
