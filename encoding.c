/*
 * MacRuby implementation of Ruby 1.9 String.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2007-2011, Apple Inc. All rights reserved.
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 * Copyright (C) 2000 Network Applied Communication Laboratory, Inc.
 * Copyright (C) 2000 Information-technology Promotion Agency, Japan
 */

#include <string.h>

#include "macruby_internal.h"
#include "ruby/encoding.h"
#include "encoding.h"
#include "symbol.h"

VALUE rb_cEncoding;

rb_encoding_t *default_internal = NULL;
static rb_encoding_t *default_external = NULL;
rb_encoding_t *rb_encodings[ENCODINGS_COUNT];

static VALUE
mr_enc_s_list(VALUE klass, SEL sel)
{
    VALUE ary = rb_ary_new2(ENCODINGS_COUNT);
    for (unsigned int i = 0; i < ENCODINGS_COUNT; ++i) {
	rb_ary_push(ary, (VALUE)rb_encodings[i]);
    }
    return ary;
}

static VALUE
mr_enc_s_name_list(VALUE klass, SEL sel)
{
    VALUE ary = rb_ary_new();
    for (unsigned int i = 0; i < ENCODINGS_COUNT; ++i) {
	rb_encoding_t *encoding = RENC(rb_encodings[i]);
	// TODO: use US-ASCII strings
	rb_ary_push(ary, rb_usascii_str_new2(encoding->public_name));
	for (unsigned int j = 0; j < encoding->aliases_count; ++j) {
	    rb_ary_push(ary, rb_usascii_str_new2(encoding->aliases[j]));
	}
    }
    return ary;
}

static VALUE
mr_enc_s_aliases(VALUE klass, SEL sel)
{
    VALUE hash = rb_hash_new();
    for (unsigned int i = 0; i < ENCODINGS_COUNT; ++i) {
	rb_encoding_t *encoding = RENC(rb_encodings[i]);
	for (unsigned int j = 0; j < encoding->aliases_count; ++j) {
	    rb_hash_aset(hash, rb_usascii_str_new2(encoding->aliases[j]),
		    rb_usascii_str_new2(encoding->public_name));
	}
    }
    return hash;
}

static VALUE
mr_enc_s_find(VALUE klass, SEL sel, VALUE name)
{
    StringValue(name);
    rb_encoding_t *enc = rb_enc_find(RSTRING_PTR(name));
    if (enc == NULL) {
	rb_raise(rb_eArgError, "unknown encoding name - %s",
		RSTRING_PTR(name));
    }
    return (VALUE)enc;
}

static VALUE
mr_enc_s_default_internal(VALUE klass, SEL sel)
{
    return (VALUE)default_internal;
}

static VALUE
mr_enc_set_default_internal(VALUE klass, SEL sel, VALUE enc)
{
    default_internal = rb_to_encoding(enc);
    return (VALUE)default_internal;
}

static VALUE
mr_enc_s_default_external(VALUE klass, SEL sel)
{
    return (VALUE)default_external;
}

static VALUE
mr_enc_set_default_external(VALUE klass, SEL sel, VALUE enc)
{
    default_external = rb_to_encoding(enc);
    return (VALUE)default_external;
}

static VALUE
mr_enc_name(VALUE self, SEL sel)
{
    return rb_usascii_str_new2(RENC(self)->public_name);
}

static VALUE
mr_enc_inspect(VALUE self, SEL sel)
{
    return rb_sprintf("#<%s:%s>", rb_obj_classname(self),
	    RENC(self)->public_name);
}

static VALUE
mr_enc_names(VALUE self, SEL sel)
{
    rb_encoding_t *encoding = RENC(self);

    VALUE ary = rb_ary_new2(encoding->aliases_count + 1);
    rb_ary_push(ary, rb_usascii_str_new2(encoding->public_name));
    for (unsigned int i = 0; i < encoding->aliases_count; ++i) {
	rb_ary_push(ary, rb_usascii_str_new2(encoding->aliases[i]));
    }
    return ary;
}

static VALUE
mr_enc_ascii_compatible_p(VALUE self, SEL sel)
{
    return RENC(self)->ascii_compatible ? Qtrue : Qfalse;
}

static VALUE
mr_enc_dummy_p(VALUE self, SEL sel)
{
    return Qfalse;
}

// For UTF-[8, 16, 32] it's /uFFFD, and for others it's '?'
rb_str_t *replacement_string_for_encoding(rb_encoding_t* destination)
{
    rb_str_t *replacement_str = NULL;
    if (destination == rb_encodings[ENCODING_UTF16BE]) {
        replacement_str = RSTR(rb_enc_str_new("\xFF\xFD", 2, destination));
    }
    else if (destination == rb_encodings[ENCODING_UTF32BE]) {
        replacement_str = RSTR(rb_enc_str_new("\0\0\xFF\xFD", 4, destination));
    }
    else if (destination == rb_encodings[ENCODING_UTF16LE]) {
        replacement_str = RSTR(rb_enc_str_new("\xFD\xFF", 2, destination));
    }
    else if (destination == rb_encodings[ENCODING_UTF32LE]) {
        replacement_str = RSTR(rb_enc_str_new("\xFD\xFF\0\0", 4, destination));
    }
    else if (destination == rb_encodings[ENCODING_UTF8]) {
        replacement_str = RSTR(rb_enc_str_new("\xEF\xBF\xBD", 3, destination));
    }
    else {
        replacement_str = RSTR(rb_enc_str_new("?", 1, rb_encodings[ENCODING_ASCII]));
        replacement_str = str_simple_transcode(replacement_str, destination);
    }
    return replacement_str;
}

static void
define_encoding_constant(const char *name, rb_encoding_t *encoding)
{
    char c = name[0];
    if ((c >= '0') && (c <= '9')) {
	// constants can't start with a number
	return;
    }

    if (strcmp(name, "locale") == 0) {
	// there is no constant for locale
	return;
    }

    char *name_copy = strdup(name);
    if ((c >= 'a') && (c <= 'z')) {
	// the first character must be upper case
	name_copy[0] = c - ('a' - 'A');
    }

    bool has_lower_case = false;
    // '.' and '-' must be transformed into '_'
    for (int i = 0; name_copy[i]; ++i) {
	if ((name_copy[i] == '.') || (name_copy[i] == '-')) {
	    name_copy[i] = '_';
	}
	else if ((name_copy[i] >= 'a') && (name_copy[i] <= 'z')) {
	    has_lower_case = true;
	}
    }
    rb_define_const(rb_cEncoding, name_copy, (VALUE)encoding);
    // if the encoding name has lower case characters,
    // also define it in upper case
    if (has_lower_case) {
	for (int i = 0; name_copy[i]; ++i) {
	    if ((name_copy[i] >= 'a') && (name_copy[i] <= 'z')) {
		name_copy[i] = name_copy[i] - 'a' + 'A';
	    }
	}
	rb_define_const(rb_cEncoding, name_copy, (VALUE)encoding);
    }

    free(name_copy);
}

extern void enc_init_ucnv_encoding(rb_encoding_t *encoding);

enum {
    ENCODING_TYPE_SPECIAL = 0,
    ENCODING_TYPE_UCNV
};

static void
add_encoding(
	unsigned int encoding_index, // index of the encoding in the encodings
				     // array
	unsigned int rb_encoding_type,
	const char *public_name, // public name for the encoding
	unsigned char min_char_size,
	bool single_byte_encoding, // in the encoding a character takes only
				   // one byte
	bool ascii_compatible, // is the encoding ASCII compatible or not
	bool little_endian, // for UTF-16/32, if the encoding is little endian
	... // aliases for the encoding (should no include the public name)
	    // - must end with a NULL
	)
{
    assert(encoding_index < ENCODINGS_COUNT);

    // create an array for the aliases
    unsigned int aliases_count = 0;
    va_list va_aliases;
    va_start(va_aliases, little_endian);
    while (va_arg(va_aliases, const char *) != NULL) {
	++aliases_count;
    }
    va_end(va_aliases);
    const char **aliases = (const char **)
	malloc(sizeof(const char *) * aliases_count);
    assert(aliases != NULL);
    va_start(va_aliases, little_endian);
    for (unsigned int i = 0; i < aliases_count; ++i) {
	aliases[i] = va_arg(va_aliases, const char *);
    }
    va_end(va_aliases);

    // create the MacRuby object
    NEWOBJ(encoding, rb_encoding_t);
    encoding->basic.flags = 0;
    encoding->basic.klass = rb_cEncoding;
    rb_encodings[encoding_index] = encoding;
    GC_RETAIN(encoding); // it should never be deallocated

    // fill the fields
    encoding->index = encoding_index;
    encoding->public_name = public_name;
    encoding->min_char_size = min_char_size;
    encoding->single_byte_encoding = single_byte_encoding;
    encoding->ascii_compatible = ascii_compatible;
    encoding->little_endian = little_endian;
    encoding->aliases_count = aliases_count;
    encoding->aliases = aliases;

    switch (rb_encoding_type) {
	case ENCODING_TYPE_SPECIAL:
	    break;
	case ENCODING_TYPE_UCNV:
	    enc_init_ucnv_encoding(encoding);
	    break;
	default:
	    abort();
    }
}

// This Init function is called very early. Do not use any runtime method
// because things may not be initialized properly yet.
void
Init_PreEncoding(void)
{
    add_encoding(ENCODING_BINARY,      ENCODING_TYPE_SPECIAL, "ASCII-8BIT",  1, true,  true,  false, "BINARY", NULL);
    add_encoding(ENCODING_ASCII,       ENCODING_TYPE_UCNV,    "US-ASCII",    1, true,  true,  false, "ASCII", "ANSI_X3.4-1968", "646", NULL);
    add_encoding(ENCODING_UTF8,        ENCODING_TYPE_UCNV,    "UTF-8",       1, false, true,  false, "CP65001", "locale", NULL);
    add_encoding(ENCODING_UTF16BE,     ENCODING_TYPE_UCNV,    "UTF-16BE",    2, false, false, false, NULL);
    add_encoding(ENCODING_UTF16LE,     ENCODING_TYPE_UCNV,    "UTF-16LE",    2, false, false, true,  NULL);
    add_encoding(ENCODING_UTF32BE,     ENCODING_TYPE_UCNV,    "UTF-32BE",    4, false, false, false, "UCS-4BE", NULL);
    add_encoding(ENCODING_UTF32LE,     ENCODING_TYPE_UCNV,    "UTF-32LE",    4, false, false, true,  "UCS-4LE", NULL);
    add_encoding(ENCODING_ISO8859_1,   ENCODING_TYPE_UCNV,    "ISO-8859-1",  1, true,  true,  false, "ISO8859-1", NULL);
    add_encoding(ENCODING_ISO8859_2,   ENCODING_TYPE_UCNV,    "ISO-8859-2",  1, true,  true,  false, "ISO8859-2", NULL);
    add_encoding(ENCODING_ISO8859_3,   ENCODING_TYPE_UCNV,    "ISO-8859-3",  1, true,  true,  false, "ISO8859-3", NULL);
    add_encoding(ENCODING_ISO8859_4,   ENCODING_TYPE_UCNV,    "ISO-8859-4",  1, true,  true,  false, "ISO8859-4", NULL);
    add_encoding(ENCODING_ISO8859_5,   ENCODING_TYPE_UCNV,    "ISO-8859-5",  1, true,  true,  false, "ISO8859-5", NULL);
    add_encoding(ENCODING_ISO8859_6,   ENCODING_TYPE_UCNV,    "ISO-8859-6",  1, true,  true,  false, "ISO8859-6", NULL);
    add_encoding(ENCODING_ISO8859_7,   ENCODING_TYPE_UCNV,    "ISO-8859-7",  1, true,  true,  false, "ISO8859-7", NULL);
    add_encoding(ENCODING_ISO8859_8,   ENCODING_TYPE_UCNV,    "ISO-8859-8",  1, true,  true,  false, "ISO8859-8", NULL);
    add_encoding(ENCODING_ISO8859_9,   ENCODING_TYPE_UCNV,    "ISO-8859-9",  1, true,  true,  false, "ISO8859-9", NULL);
    add_encoding(ENCODING_ISO8859_10,  ENCODING_TYPE_UCNV,    "ISO-8859-10", 1, true,  true,  false, "ISO8859-10", NULL);
    add_encoding(ENCODING_ISO8859_11,  ENCODING_TYPE_UCNV,    "ISO-8859-11", 1, true,  true,  false, "ISO8859-11", NULL);
    add_encoding(ENCODING_ISO8859_13,  ENCODING_TYPE_UCNV,    "ISO-8859-13", 1, true,  true,  false, "ISO8859-13", NULL);
    add_encoding(ENCODING_ISO8859_14,  ENCODING_TYPE_UCNV,    "ISO-8859-14", 1, true,  true,  false, "ISO8859-14", NULL);
    add_encoding(ENCODING_ISO8859_15,  ENCODING_TYPE_UCNV,    "ISO-8859-15", 1, true,  true,  false, "ISO8859-15", NULL);
    add_encoding(ENCODING_ISO8859_16,  ENCODING_TYPE_UCNV,    "ISO-8859-16", 1, true,  true,  false, "ISO8859-16", NULL);
    add_encoding(ENCODING_MACROMAN,    ENCODING_TYPE_UCNV,    "macRoman",    1, true,  true,  false, NULL);
    add_encoding(ENCODING_MACCYRILLIC, ENCODING_TYPE_UCNV,    "macCyrillic", 1, true,  true,  false, NULL);
    add_encoding(ENCODING_BIG5,        ENCODING_TYPE_UCNV,    "Big5",        1, false, true,  false, "CP950", NULL);
    // FIXME: the ICU conversion tables do not seem to match Ruby's Japanese conversion tables
    add_encoding(ENCODING_EUCJP,       ENCODING_TYPE_UCNV,    "EUC-JP",      1, false, true,  false, "eucJP", NULL);
    add_encoding(ENCODING_SJIS,        ENCODING_TYPE_UCNV,    "Shift_JIS",   1, false, true,  false, "SJIS", NULL);
    //add_encoding(ENCODING_EUCJP,     ENCODING_TYPE_RUBY, "EUC-JP",      1, false, true,  "eucJP", NULL);
    //add_encoding(ENCODING_SJIS,      ENCODING_TYPE_RUBY, "Shift_JIS",   1, false, true, "SJIS", NULL);
    //add_encoding(ENCODING_CP932,     ENCODING_TYPE_RUBY, "Windows-31J", 1, false, true, "CP932", "csWindows31J", NULL);

    default_external = rb_encodings[ENCODING_UTF8];
    default_internal = rb_encodings[ENCODING_UTF8];
}

void
Init_Encoding(void)
{
    // rb_cEncoding is defined earlier in Init_PreVM().
    rb_set_class_path(rb_cEncoding, rb_cObject, "Encoding");
    rb_const_set(rb_cObject, rb_intern("Encoding"), rb_cEncoding);

    rb_undef_alloc_func(rb_cEncoding);

    rb_objc_define_method(rb_cEncoding, "to_s", mr_enc_name, 0);
    rb_objc_define_method(rb_cEncoding, "inspect", mr_enc_inspect, 0);
    rb_objc_define_method(rb_cEncoding, "name", mr_enc_name, 0);
    rb_objc_define_method(rb_cEncoding, "names", mr_enc_names, 0);
    rb_objc_define_method(rb_cEncoding, "dummy?", mr_enc_dummy_p, 0);
    rb_objc_define_method(rb_cEncoding, "ascii_compatible?",
	    mr_enc_ascii_compatible_p, 0);
    rb_objc_define_method(*(VALUE *)rb_cEncoding, "list", mr_enc_s_list, 0);
    rb_objc_define_method(*(VALUE *)rb_cEncoding, "name_list",
	    mr_enc_s_name_list, 0);
    rb_objc_define_method(*(VALUE *)rb_cEncoding, "aliases",
	    mr_enc_s_aliases, 0);
    rb_objc_define_method(*(VALUE *)rb_cEncoding, "find", mr_enc_s_find, 1);
    rb_objc_define_method(*(VALUE *)rb_cEncoding, "compatible?",
	    mr_enc_s_is_compatible, 2); // in string.c

    //rb_define_method(rb_cEncoding, "_dump", enc_dump, -1);
    //rb_define_singleton_method(rb_cEncoding, "_load", enc_load, 1);

    rb_objc_define_method(*(VALUE *)rb_cEncoding, "default_external",
	    mr_enc_s_default_external, 0);
    rb_objc_define_method(*(VALUE *)rb_cEncoding, "default_external=",
	    mr_enc_set_default_external, 1);
    rb_objc_define_method(*(VALUE *)rb_cEncoding, "default_internal",
	    mr_enc_s_default_internal, 0);
    rb_objc_define_method(*(VALUE *)rb_cEncoding, "default_internal=",
	    mr_enc_set_default_internal, 1);
    //rb_define_singleton_method(rb_cEncoding, "locale_charmap", rb_locale_charmap, 0);

    // Create constants.
    for (unsigned int i = 0; i < ENCODINGS_COUNT; i++) {
	rb_encoding_t *enc = rb_encodings[i];
	define_encoding_constant(enc->public_name, enc);
	for (unsigned int j = 0; j < enc->aliases_count; j++) {
	    define_encoding_constant(enc->aliases[j], enc);
	}
    }
}

// MRI C-API compatibility.

rb_encoding_t *
rb_enc_find(const char *name)
{
    for (unsigned int i = 0; i < ENCODINGS_COUNT; i++) {
	rb_encoding_t *enc = rb_encodings[i];
	if (strcasecmp(enc->public_name, name) == 0) {
	    return enc;
	}
	for (unsigned int j = 0; j < enc->aliases_count; j++) {
	    const char *alias = enc->aliases[j];
	    if (strcasecmp(alias, name) == 0) {
		return enc;
	    }
	}
    }
    return NULL;
}

VALUE
rb_enc_from_encoding(rb_encoding_t *enc)
{
    return (VALUE)enc;
}

rb_encoding_t *
rb_enc_get(VALUE obj)
{
    switch (TYPE(obj)) {
	case T_STRING:
	    if (IS_RSTR(obj)) {
		return RSTR(obj)->encoding;
	    }
	    return rb_encodings[ENCODING_UTF8];

	case T_SYMBOL:
	    return rb_enc_get(rb_sym_str(obj));
    }
    return NULL;
}

rb_encoding_t *
rb_to_encoding(VALUE obj)
{
    rb_encoding_t *enc;
    if (CLASS_OF(obj) == rb_cEncoding) {
	enc = RENC(obj);
    }
    else {
	StringValue(obj);
	enc = rb_enc_find(RSTRING_PTR(obj));
	if (enc == NULL) {
	    rb_raise(rb_eArgError, "unknown encoding name - %s",
		    RSTRING_PTR(obj));
	}
    }
    return enc;
}

const char *
rb_enc_name(rb_encoding_t *enc)
{
    return RENC(enc)->public_name;
}

VALUE
rb_enc_name2(rb_encoding_t *enc)
{
    return rb_usascii_str_new2(rb_enc_name(enc));
}

long
rb_enc_mbminlen(rb_encoding_t *enc)
{
    return enc->min_char_size;    
}

long
rb_enc_mbmaxlen(rb_encoding_t *enc)
{
    return enc->single_byte_encoding ? 1 : 10; // XXX 10?
}

rb_encoding *
rb_ascii8bit_encoding(void)
{
    return rb_encodings[ENCODING_BINARY];
}

rb_encoding *
rb_utf8_encoding(void)
{
    return rb_encodings[ENCODING_UTF8];
}

rb_encoding *
rb_usascii_encoding(void)
{
    return rb_encodings[ENCODING_ASCII];
}

rb_encoding_t *
rb_locale_encoding(void)
{
    // XXX
    return rb_encodings[ENCODING_UTF8];
}

void
rb_enc_set_default_external(VALUE encoding)
{
    assert(CLASS_OF(encoding) == rb_cEncoding);
    default_external = RENC(encoding); 
}

rb_encoding *
rb_default_internal_encoding(void)
{
    return (rb_encoding *)default_internal;
}

rb_encoding *
rb_default_external_encoding(void)
{
    return (rb_encoding *)default_external;
}

static int
index_of_encoding(rb_encoding_t *enc)
{
    if (enc != NULL) {
	for (int i = 0; i <ENCODINGS_COUNT; i++) {
	    if (rb_encodings[i] == enc) {
		return i;
	    }
	}
    }
    return -1;
}

int
rb_enc_get_index(VALUE obj)
{
    return index_of_encoding(rb_enc_get(obj));
}

int
rb_enc_to_index(VALUE enc)
{
    if (CLASS_OF(enc) == rb_cEncoding) {
	return index_of_encoding(RENC(enc));
    }
    return -1;
}

void
rb_enc_set_index(VALUE obj, int encindex)
{
    assert(encindex >= 0 && encindex < ENCODINGS_COUNT);
    if (TYPE(obj) == T_STRING) {
	rb_str_force_encoding(obj, rb_encodings[encindex]);
    }
}

int
rb_to_encoding_index(VALUE enc)
{
    if (CLASS_OF(enc) != rb_cEncoding && TYPE(enc) != T_STRING) {
        return -1;
    }
    else {
        int idx = index_of_encoding((rb_encoding_t *)enc);
        if (idx >= 0) {
            return idx;
        }
        else if (NIL_P(enc = rb_check_string_type(enc))) {
            return -1;
        }
        if (!rb_enc_asciicompat(rb_enc_get(enc))) {
            return -1;
        }
        return rb_enc_find_index(StringValueCStr(enc));
    }
}

int
rb_enc_find_index(const char *name)
{
    return index_of_encoding(rb_enc_find(name));
}

int
rb_ascii8bit_encindex(void)
{
    return index_of_encoding(rb_encodings[ENCODING_BINARY]);
}

int
rb_utf8_encindex(void)
{
    return index_of_encoding(rb_encodings[ENCODING_UTF8]);
}

int
rb_usascii_encindex(void)
{
    return index_of_encoding(rb_encodings[ENCODING_ASCII]);
}

rb_encoding *
rb_enc_from_index(int idx)
{
    assert(idx >= 0 && idx < ENCODINGS_COUNT);
    return rb_encodings[idx];
}

VALUE
rb_enc_associate_index(VALUE obj, int idx)
{
    if (TYPE(obj) == T_STRING) {
	assert(idx >= 0 && idx < ENCODINGS_COUNT);
	rb_str_force_encoding(obj, rb_encodings[idx]);
	return obj;
    }
    rb_raise(rb_eArgError, "cannot set encoding on non-string object");
}

void
rb_enc_copy(VALUE obj1, VALUE obj2)
{
    rb_enc_associate_index(obj1, rb_enc_get_index(obj2));
}
