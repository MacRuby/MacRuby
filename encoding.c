/* 
 * MacRuby implementation of Ruby 1.9's encoding.c.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 * 
 * Copyright (C) 2007-2008, Apple Inc. All rights reserved.
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 * Copyright (C) 2000 Network Applied Communication Laboratory, Inc.
 * Copyright (C) 2000 Information-technology Promotion Agency, Japan
 */

#include "ruby/ruby.h"
#include "ruby/encoding.h"
#include "regenc.h"
#include <ctype.h>
#ifdef HAVE_LANGINFO_H
#include <langinfo.h>
#endif

static ID id_encoding, id_base_encoding;
VALUE rb_cEncoding;

static CFMutableDictionaryRef __encodings = NULL;

static VALUE
enc_new(const CFStringEncoding *enc)
{
    return Data_Wrap_Struct(rb_cEncoding, NULL, NULL, (void *)enc);
}

static void
enc_init_db(void)
{
    const CFStringEncoding *e;

    __encodings = CFDictionaryCreateMutable(NULL, 0, NULL, &kCFTypeDictionaryValueCallBacks);
    
    /* XXX CFStringGetListOfAvailableEncodings() is a costly call and should
     * be called on demand and not by default when the interpreter starts.
     */
    e = CFStringGetListOfAvailableEncodings();
    while (e != NULL && *e != kCFStringEncodingInvalidId) {
	VALUE iana;
	VALUE encoding;

	encoding = enc_new(e);

	iana = (VALUE)CFStringConvertEncodingToIANACharSetName(*e);
	if (iana != 0) {
	    const char *name;

	    name = RSTRING_PTR(iana);

	    // new_name = name.gsub(/-/, '_').upcase
	    char *new_name = alloca(strlen(name));
	    strcpy(new_name, name);
	    char *p = strchr(name, '-');
	    if (p != NULL) {
		p = new_name + (p - name);
		do {
		    *p = '_';
		    p++;
		    p = strchr(p, '-');	
		}
		while (p != NULL);
	    }
	    p = new_name;
	    while (*p != '\0') {
		if (islower(*p)) {
		    *p = toupper(*p);
		}
		p++;
	    }

	    ID encoding_id = rb_intern(new_name);
	    if (!rb_const_defined(rb_cEncoding, encoding_id)) {
		rb_const_set(rb_cEncoding, encoding_id, encoding);
	    }
	}
	CFDictionarySetValue(__encodings, (const void *)iana, 
	    (const void *)encoding);
	e++;
    }

    assert(CFDictionaryGetCount((CFDictionaryRef)__encodings) > 0);

    // Define shortcuts.
    rb_define_const(rb_cEncoding, "ASCII_8BIT",
	    rb_const_get(rb_cEncoding, rb_intern("US_ASCII")));
}

static VALUE
enc_make(const CFStringEncoding *enc)
{
    VALUE iana, v;

    assert(enc != NULL);
    iana = (VALUE)CFStringConvertEncodingToIANACharSetName(*enc);
    v = (VALUE)CFDictionaryGetValue((CFDictionaryRef)__encodings, 
	(const void *)iana);
    assert(v != 0);
    return v;
}

VALUE
rb_enc_from_encoding(rb_encoding *enc)
{
    return enc_make(enc);
}

static inline CFStringEncoding
rb_enc_to_enc(VALUE v)
{
    return *(CFStringEncoding *)DATA_PTR(v);
}

static inline CFStringEncoding *
rb_enc_to_enc_ptr(VALUE v)
{
    return (CFStringEncoding *)DATA_PTR(v);
}

rb_encoding *
rb_to_encoding(VALUE v)
{
    if (TYPE(v) == T_STRING)
	return rb_enc_find2(v);
    return rb_enc_to_enc_ptr(v);
}

/*
 * call-seq:
 *   enc.dummy? => true or false
 *
 * Returns true for dummy encodings.
 * A dummy encoding is an encoding for which character handling is not properly
 * implemented.
 * It is used for stateful encodings.
 *
 *   Encoding::ISO_2022_JP.dummy?       #=> true
 *   Encoding::UTF_8.dummy?             #=> false
 *
 */
static VALUE
enc_dummy_p(VALUE enc, SEL sel)
{
    return rb_enc_dummy_p(rb_to_encoding(enc)) ? Qtrue : Qfalse;
}

ID
rb_id_encoding(void)
{
    if (!id_encoding) {
	id_encoding = rb_intern("encoding");
    }
    return id_encoding;
}

rb_encoding*
rb_enc_compatible(VALUE str1, VALUE str2)
{
    /* TODO */
    rb_encoding *enc = rb_enc_get(str1);
    if (enc == rb_enc_get(str2))
	return enc;
    return NULL;
}

/*
 *  call-seq:
 *     obj.encoding   => encoding
 *
 *  Returns the Encoding object that represents the encoding of obj.
 */

VALUE
rb_obj_encoding(VALUE obj, SEL sel)
{
    rb_encoding *enc = rb_enc_get(obj);
    if (!enc) {
	rb_raise(rb_eTypeError, "unknown encoding");
    }
    return rb_enc_from_encoding(enc);
}

/*
 * call-seq:
 *   enc.inspect => string
 *
 * Returns a string which represents the encoding for programmers.
 *
 *   Encoding::UTF_8.inspect       #=> "#<Encoding:UTF-8>"
 *   Encoding::ISO_2022_JP.inspect #=> "#<Encoding:ISO-2022-JP (dummy)>"
 */
static VALUE
enc_inspect(VALUE self, SEL sel)
{
    char buffer[512];
    VALUE enc_name;
    long n;

    enc_name = (VALUE)CFStringGetNameOfEncoding(rb_enc_to_enc(self));
    
    n = snprintf(buffer, sizeof buffer, "#<%s:%s>", rb_obj_classname(self),
	RSTRING_PTR(enc_name));

    return rb_str_new(buffer, n);
}

/*
 * call-seq:
 *   enc.name => string
 *
 * Returns the name of the encoding.
 *
 *   Encoding::UTF_8.name       => "UTF-8"
 */
static VALUE
enc_name(VALUE self, SEL sel)
{
    return rb_enc_name2(rb_enc_to_enc_ptr(self));
}

static VALUE
enc_base_encoding(VALUE self, SEL sel)
{
    return rb_attr_get(self, id_base_encoding);
}

/*
 * call-seq:
 *   Encoding.list => [enc1, enc2, ...]
 *
 * Returns the list of loaded encodings.
 *
 *   Encoding.list
 *   => [#<Encoding:ASCII-8BIT>, #<Encoding:UTF-8>,
 *       #<Encoding:ISO-2022-JP (dummy)>]
 *
 *   Encoding.find("US-ASCII")
 *   => #<Encoding:US-ASCII>
 *
 *   Encoding.list
 *   => [#<Encoding:ASCII-8BIT>, #<Encoding:UTF-8>,
 *       #<Encoding:US-ASCII>, #<Encoding:ISO-2022-JP (dummy)>]
 *
 */
static VALUE
enc_list(VALUE klass, SEL sel)
{
    VALUE ary;
    const CFStringEncoding *e;

    ary = rb_ary_new();
    e = CFStringGetListOfAvailableEncodings();
    while (e != NULL && *e != kCFStringEncodingInvalidId) {
	rb_ary_push(ary, enc_make(e));
	e++;
    }
    return ary;
}

/*
 * call-seq:
 *   Encoding.find(string) => enc
 *   Encoding.find(symbol) => enc
 *
 * Search the encoding with specified <i>name</i>.
 * <i>name</i> should be a string or symbol.
 *
 *   Encoding.find("US-ASCII")  => #<Encoding:US-ASCII>
 *   Encoding.find(:Shift_JIS)  => #<Encoding:Shift_JIS>
 *
 */
static VALUE
enc_find2(VALUE enc)
{
    CFStringRef str;
    CFStringEncoding e;

    str = (CFStringRef)StringValue(enc);
    if (CFStringCompare(str, CFSTR("ASCII-8BIT"), 
			kCFCompareCaseInsensitive) == 0) {
	str = CFSTR("ASCII");
    }
    else if (CFStringCompare(str, CFSTR("SJIS"), 
	     kCFCompareCaseInsensitive) == 0) {
	str = CFSTR("Shift-JIS");
    }

    e = CFStringConvertIANACharSetNameToEncoding(str);
    if (e == kCFStringEncodingInvalidId)
	return Qnil;
    return enc_make(&e);
}

static VALUE
enc_find(VALUE klass, SEL sel, VALUE enc)
{
    VALUE e = enc_find2(enc);
    if (e == Qnil) {
	rb_raise(rb_eArgError, "unknown encoding name - %s", RSTRING_PTR(enc));
    }
    return e;
}

/*
 * call-seq:
 *   Encoding.compatible?(str1, str2) => enc or nil
 *
 * Checks the compatibility of two strings.
 * If they are compatible, means concatenatable, 
 * returns an encoding which the concatinated string will be.
 * If they are not compatible, nil is returned.
 *
 *   Encoding.compatible?("\xa1".force_encoding("iso-8859-1"), "b")
 *   => #<Encoding:ISO-8859-1>
 *
 *   Encoding.compatible?(
 *     "\xa1".force_encoding("iso-8859-1"),
 *     "\xa1\xa1".force_encoding("euc-jp"))
 *   => nil
 *
 */
static VALUE
enc_compatible_p(VALUE klass, SEL sel, VALUE str1, VALUE str2)
{
    rb_encoding *enc = rb_enc_compatible(str1, str2);
    VALUE encoding = Qnil;
    if (!enc || !(encoding = rb_enc_from_encoding(enc)))
	encoding = Qnil;
    return encoding;
}

/* :nodoc: */
static VALUE
enc_dump(VALUE self, SEL sel, int argc, VALUE *argv)
{
    rb_scan_args(argc, argv, "01", 0);
    return enc_name(self, 0);
}

/* :nodoc: */
static VALUE
enc_load(VALUE klass, SEL sel, VALUE str)
{
    return enc_find(klass, 0, str);
}

static rb_encoding *default_external;
    
rb_encoding *
rb_default_external_encoding(void)
{
    return default_external;
}

VALUE
rb_enc_default_external(void)
{
    return enc_make(default_external);
}

/*
 * call-seq:
 *   Encoding.default_external => enc
 *
 * Returns default external encoding.
 *
 * It is initialized by the locale or -E option.
 */
static VALUE
get_default_external(VALUE klass, SEL sel)
{
    return rb_enc_default_external();
}

void
rb_enc_set_default_external(VALUE encoding)
{
    default_external = rb_enc_to_enc_ptr(encoding);
}

/*
 * call-seq:
 *   Encoding.locale_charmap => string
 *
 * Returns the locale charmap name.
 *
 *   Debian GNU/Linux
 *     LANG=C
 *       Encoding.locale_charmap  => "ANSI_X3.4-1968"
 *     LANG=ja_JP.EUC-JP
 *       Encoding.locale_charmap  => "EUC-JP"
 *
 *   SunOS 5
 *     LANG=C
 *       Encoding.locale_charmap  => "646"
 *     LANG=ja
 *       Encoding.locale_charmap  => "eucJP"
 *
 */
static VALUE
rb_locale_charmap(VALUE klass, SEL sel)
{
    CFStringEncoding enc = CFStringGetSystemEncoding();
    return (VALUE)CFStringConvertEncodingToIANACharSetName(enc);
}

/*
 * call-seq:
 *   Encoding.name_list => ["enc1", "enc2", ...]
 *
 * Returns the list of available encoding names.
 *
 *   Encoding.name_list
 *   => ["US-ASCII", "ASCII-8BIT", "UTF-8",
 *       "ISO-8859-1", "Shift_JIS", "EUC-JP",
 *       "Windows-31J",
 *       "BINARY", "CP932", "eucJP"]
 *
 * This list doesn't include dummy encodings.
 *
 */

static VALUE
rb_enc_name_list(VALUE klass, SEL sel)
{
    VALUE ary, list;
    long i, count;

    ary = rb_ary_new();
    list = enc_list(klass, 0);
    for (i = 0, count = RARRAY_LEN(list); i < count; i++) {
	rb_ary_push(ary, enc_name(RARRAY_AT(list, i), 0));
    }
    return ary;
}

/*
 * call-seq:
 *   Encoding.aliases => {"alias1" => "orig1", "alias2" => "orig2", ...}
 *
 * Returns the hash of available encoding alias and original encoding name.
 *
 *   Encoding.aliases
 *   => {"BINARY"=>"ASCII-8BIT", "ASCII"=>"US-ASCII", "ANSI_X3.4-1986"=>"US-ASCII",
 *       "SJIS"=>"Shift_JIS", "eucJP"=>"EUC-JP", "CP932"=>"Windows-31J"}
 *
 */

static VALUE
rb_enc_aliases(VALUE klass, SEL sel)
{
    /* TODO: the CFString IANA <-> charset code does support aliases, we should
     * find a way to return them here. 
     */
    return rb_hash_new();
}

VALUE
rb_enc_name2(rb_encoding *enc)
{
    if (enc != NULL) {
	CFStringRef str = CFStringConvertEncodingToIANACharSetName(*enc);
	if (str != NULL) {
	    VALUE name = rb_str_dup((VALUE)str);
	    CFStringUppercase((CFMutableStringRef)name, NULL);
	    return name;
	}
    }
    return Qnil;
}

const char *
rb_enc_name(rb_encoding *enc)
{
    VALUE str = rb_enc_name2(enc);
    return str == Qnil ? NULL : RSTRING_PTR(str);
}

long 
rb_enc_mbminlen(rb_encoding *enc)
{
    return rb_enc_mbmaxlen(enc);
}

long
rb_enc_mbmaxlen(rb_encoding *enc)
{
    return enc == NULL
	? 1 : CFStringGetMaximumSizeForEncoding(1, *enc);
}

rb_encoding *
rb_enc_find(const char *name)
{
    return rb_enc_find2(rb_str_new2(name));
}

rb_encoding *
rb_enc_find2(VALUE name)
{
    VALUE e = enc_find2(name);
    return e == Qnil ? NULL : rb_enc_to_enc_ptr(e);
}

rb_encoding *
rb_enc_get(VALUE obj)
{
    CFStringEncoding enc = kCFStringEncodingInvalidId;

    switch (TYPE(obj)) {
	case T_STRING:
	    enc = *(VALUE *)obj == rb_cByteString
		? kCFStringEncodingASCII
		: CFStringGetFastestEncoding((CFStringRef)obj);
	    break;
    }

    if (enc == kCFStringEncodingInvalidId) {
	return NULL;
    }
    return rb_enc_to_enc_ptr(enc_make(&enc));
}

rb_encoding *
rb_locale_encoding(void)
{
    CFStringEncoding enc = CFStringGetSystemEncoding();
    return rb_enc_to_enc_ptr(enc_make(&enc));
}

void
Init_Encoding(void)
{
    id_base_encoding = rb_intern("#base_encoding");

    rb_cEncoding = rb_define_class("Encoding", rb_cObject);
    rb_undef_alloc_func(rb_cEncoding);
    rb_objc_define_method(rb_cEncoding, "to_s", enc_name, 0);
    rb_objc_define_method(rb_cEncoding, "inspect", enc_inspect, 0);
    rb_objc_define_method(rb_cEncoding, "name", enc_name, 0);
    rb_objc_define_method(rb_cEncoding, "base_encoding", enc_base_encoding, 0);
    rb_objc_define_method(rb_cEncoding, "dummy?", enc_dummy_p, 0);
    rb_objc_define_method(*(VALUE *)rb_cEncoding, "list", enc_list, 0);
    rb_objc_define_method(*(VALUE *)rb_cEncoding, "name_list", rb_enc_name_list, 0);
    rb_objc_define_method(*(VALUE *)rb_cEncoding, "aliases", rb_enc_aliases, 0);
    rb_objc_define_method(*(VALUE *)rb_cEncoding, "find", enc_find, 1);
    rb_objc_define_method(*(VALUE *)rb_cEncoding, "compatible?", enc_compatible_p, 2);

    rb_objc_define_method(rb_cEncoding, "_dump", enc_dump, -1);
    rb_objc_define_method(*(VALUE *)rb_cEncoding, "_load", enc_load, 1);

    rb_objc_define_method(*(VALUE *)rb_cEncoding, "default_external", get_default_external, 0);
    rb_objc_define_method(*(VALUE *)rb_cEncoding, "locale_charmap", rb_locale_charmap, 0);

    enc_init_db();
}

/* locale insensitive functions */

#define ctype_test(c, ctype) \
    (rb_isascii(c) && ONIGENC_IS_ASCII_CODE_CTYPE((c), ctype))

int rb_isalnum(int c) { return ctype_test(c, ONIGENC_CTYPE_ALNUM); }
int rb_isalpha(int c) { return ctype_test(c, ONIGENC_CTYPE_ALPHA); }
int rb_isblank(int c) { return ctype_test(c, ONIGENC_CTYPE_BLANK); }
int rb_iscntrl(int c) { return ctype_test(c, ONIGENC_CTYPE_CNTRL); }
int rb_isdigit(int c) { return ctype_test(c, ONIGENC_CTYPE_DIGIT); }
int rb_isgraph(int c) { return ctype_test(c, ONIGENC_CTYPE_GRAPH); }
int rb_islower(int c) { return ctype_test(c, ONIGENC_CTYPE_LOWER); }
int rb_isprint(int c) { return ctype_test(c, ONIGENC_CTYPE_PRINT); }
int rb_ispunct(int c) { return ctype_test(c, ONIGENC_CTYPE_PUNCT); }
int rb_isspace(int c) { return ctype_test(c, ONIGENC_CTYPE_SPACE); }
int rb_isupper(int c) { return ctype_test(c, ONIGENC_CTYPE_UPPER); }
int rb_isxdigit(int c) { return ctype_test(c, ONIGENC_CTYPE_XDIGIT); }

int
rb_tolower(int c)
{
    return rb_isascii(c) ? ONIGENC_ASCII_CODE_TO_LOWER_CASE(c) : c;
}

int
rb_toupper(int c)
{
    return rb_isascii(c) ? ONIGENC_ASCII_CODE_TO_UPPER_CASE(c) : c;
}

