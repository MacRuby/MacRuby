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

#ifndef __ENCODING_H_
#define __ENCODING_H_

#if defined(__cplusplus)
extern "C" {
#endif

#if defined(__cplusplus)
# include "unicode/unistr.h"
#else
# include "unicode/ustring.h"
#endif

#if __LITTLE_ENDIAN__
#define ENCODING_UTF16_NATIVE ENCODING_UTF16LE
#define ENCODING_UTF32_NATIVE ENCODING_UTF32LE
#define ENCODING_UTF16_NON_NATIVE ENCODING_UTF16BE
#define ENCODING_UTF32_NON_NATIVE ENCODING_UTF32BE
#else
#define ENCODING_UTF16_NATIVE ENCODING_UTF16BE
#define ENCODING_UTF32_NATIVE ENCODING_UTF32BE
#define ENCODING_UTF16_NON_NATIVE ENCODING_UTF16LE
#define ENCODING_UTF32_NON_NATIVE ENCODING_UTF32LE
#endif

#define IS_NATIVE_UTF16_ENC(encoding) \
    ((encoding) == rb_encodings[ENCODING_UTF16_NATIVE])
#define IS_NON_NATIVE_UTF16_ENC(encoding) \
    ((encoding) == rb_encodings[ENCODING_UTF16_NON_NATIVE])
#define IS_UTF16_ENC(encoding) \
    (IS_NATIVE_UTF16_ENC(encoding) || IS_NON_NATIVE_UTF16_ENC(encoding))
#define IS_NATIVE_UTF32_ENC(encoding) \
    ((encoding) == rb_encodings[ENCODING_UTF32_NATIVE])
#define IS_NON_NATIVE_UTF32_ENC(encoding) \
    ((encoding) == rb_encodings[ENCODING_UTF32_NON_NATIVE])
#define IS_UTF32_ENC(encoding) \
    (IS_NATIVE_UTF32_ENC(encoding) || IS_NON_NATIVE_UTF32_ENC(encoding))
#define IS_UTF8_ENC(encoding) ((encoding) == rb_encodings[ENCODING_UTF8])
#define IS_ASCII_ENC(encoding) ((encoding) == rb_encodings[ENCODING_ASCII])
#define IS_BINARY_ENC(encoding) ((encoding) == rb_encodings[ENCODING_BINARY])

typedef uint8_t str_flag_t;

typedef struct RString {
    struct RBasic basic;
    struct rb_encoding *encoding;
    long capacity_in_bytes;
    long length_in_bytes;
    long cached_length;
    char *bytes;
    str_flag_t flags;
} rb_str_t;

#define RSTR(x) ((rb_str_t *)x)

static inline bool
rb_klass_is_rstr(VALUE klass)
{
    do {
	if (klass == rb_cRubyString) {
	    return true;
	}
	if (klass == rb_cNSString) {
	    return false;
	}
	klass = RCLASS_SUPER(klass);
    }
    while (klass != 0);
    return false;
}

#define IS_RSTR(x) (rb_klass_is_rstr(*(VALUE *)x))

static inline void
rstr_modify(VALUE str)
{
    const long mask = RBASIC(str)->flags;
    if ((mask & FL_FREEZE) == FL_FREEZE) {
        rb_raise(rb_eRuntimeError, "can't modify frozen/immutable string");
    }
    if ((mask & FL_UNTRUSTED) != FL_UNTRUSTED) {
	if (rb_safe_level() >= 4) {
	    rb_raise(rb_eSecurityError, "Insecure: can't modify string");
	}
    }
}

static inline void
rstr_frozen_check(VALUE str)
{
    const long mask = RBASIC(str)->flags;
    if ((mask & FL_FREEZE) == FL_FREEZE) {
	rb_raise(rb_eRuntimeError, "string frozen");
    }
}

typedef struct {
    long start_offset_in_bytes;
    long end_offset_in_bytes;
} character_boundaries_t;

typedef struct {
    character_boundaries_t cached_boundaries;
    long cached_boundaries_index;
    long cached_length;
} character_boundaries_cache_t;

typedef struct rb_encoding {
    struct RBasic basic;
    unsigned int index;
    const char *public_name;
    const char **aliases;
    unsigned int aliases_count;
    unsigned char min_char_size;
    bool single_byte_encoding : 1;
    bool ascii_compatible : 1;
    bool little_endian : 1; // only meaningful for UTF-16 or UTF-32
    void *private_data;
} rb_encoding_t;

#define RENC(x) ((rb_encoding_t *)(x))

enum {
    ENCODING_BINARY = 0,
    ENCODING_ASCII,
    ENCODING_UTF8,
    ENCODING_UTF16BE,
    ENCODING_UTF16LE,
    ENCODING_UTF32BE,
    ENCODING_UTF32LE,
    ENCODING_ISO8859_1,
    ENCODING_ISO8859_2,
    ENCODING_ISO8859_3,
    ENCODING_ISO8859_4,
    ENCODING_ISO8859_5,
    ENCODING_ISO8859_6,
    ENCODING_ISO8859_7,
    ENCODING_ISO8859_8,
    ENCODING_ISO8859_9,
    ENCODING_ISO8859_10,
    ENCODING_ISO8859_11,
    ENCODING_ISO8859_13,
    ENCODING_ISO8859_14,
    ENCODING_ISO8859_15,
    ENCODING_ISO8859_16,
    ENCODING_MACROMAN,
    ENCODING_MACCYRILLIC,
    ENCODING_BIG5,
    ENCODING_EUCJP,
    ENCODING_SJIS,
    //ENCODING_CP932,

    ENCODINGS_COUNT
};

extern rb_encoding_t *rb_encodings[ENCODINGS_COUNT];

#define STRING_ASCII_ONLY_SET        0x010
#define STRING_ASCII_ONLY            0x008
#define STRING_VALID_ENCODING_SET    0x004
#define STRING_VALID_ENCODING        0x002

#define BYTES_TO_UCHARS(len) ((len) / sizeof(UChar))
#define UCHARS_TO_BYTES(len) ((len) * sizeof(UChar))

#define ODD_NUMBER(x) ((x) & 0x1)

static inline void
reset_character_boundaries_cache(character_boundaries_cache_t *cache)
{
    assert(cache != NULL);
    cache->cached_boundaries_index = -1;
    cache->cached_length = -1;
}

static inline long
div_round_up(long a, long b)
{
    return ((a) + (b - 1)) / b;
}

void str_update_flags(rb_str_t *self);

static inline void
str_reset_cache(rb_str_t *self)
{
    self->cached_length = 0;
}

static inline void
str_reset_flags(rb_str_t *self)
{
    str_reset_cache(self);
    self->flags = 0;
}

static inline bool
str_check_flag_and_update_if_needed(rb_str_t *self, str_flag_t flag_set,
	str_flag_t flag)
{
    if (!(self->flags & flag_set)) {
	str_update_flags(self);
	assert(self->flags & flag_set);
    }
    return self->flags & flag;
}

static inline bool
str_is_valid_encoding(rb_str_t *self)
{
    return str_check_flag_and_update_if_needed(self, STRING_VALID_ENCODING_SET,
	    STRING_VALID_ENCODING);
}

static inline bool
str_is_ascii_only(rb_str_t *self)
{
    return str_check_flag_and_update_if_needed(self, STRING_ASCII_ONLY_SET,
	    STRING_ASCII_ONLY);
}

static inline bool
str_is_ruby_ascii_only(rb_str_t *self)
{
    // for MRI, a string in a non-ASCII-compatible encoding (like UTF-16)
    // containing only ASCII characters is not "ASCII only" though for us it
    // is internally
    if (!self->encoding->ascii_compatible) {
	return false;
    }
    return str_is_ascii_only(self);
}

static inline void
str_set_flag(rb_str_t *self, bool status, str_flag_t flag_set,
	str_flag_t flag)
{
    if (status) {
	self->flags = self->flags | flag_set | flag;
    }
    else {
	self->flags = (self->flags | flag_set) & ~flag;
    }
}

static inline void
str_set_ascii_only(rb_str_t *self, bool status)
{
    str_set_flag(self, status, STRING_ASCII_ONLY_SET, STRING_ASCII_ONLY);
}

static inline void
str_set_valid_encoding(rb_str_t *self, bool status)
{
    str_set_flag(self, status, STRING_VALID_ENCODING_SET,
	    STRING_VALID_ENCODING);
}

typedef enum {
    TRANSCODE_BEHAVIOR_RAISE_EXCEPTION,
    TRANSCODE_BEHAVIOR_REPLACE_WITH_STRING,
    TRANSCODE_BEHAVIOR_REPLACE_WITH_XML_TEXT,
    TRANSCODE_BEHAVIOR_REPLACE_WITH_XML_ATTR
} transcode_behavior_t;

typedef enum {
    ECONV_INVALID_MASK                = 1,
    ECONV_INVALID_REPLACE             = 1 << 1,
    ECONV_UNDEF_MASK                  = 1 << 2,
    ECONV_UNDEF_REPLACE               = 1 << 3,
    ECONV_UNDEF_HEX_CHARREF           = 1 << 4,
    ECONV_PARTIAL_INPUT               = 1 << 5,
    ECONV_AFTER_OUTPUT                = 1 << 6,
    ECONV_UNIVERSAL_NEWLINE_DECORATOR = 1 << 7,
    ECONV_CRLF_NEWLINE_DECORATOR      = 1 << 8,
    ECONV_CR_NEWLINE_DECORATOR        = 1 << 9,
    ECONV_XML_TEXT_DECORATOR          = 1 << 10,
    ECONV_XML_ATTR_CONTENT_DECORATOR  = 1 << 11,
    ECONV_XML_ATTR_QUOTE_DECORATOR    = 1 << 12
} transcode_flags_t;

rb_str_t *str_transcode(rb_str_t *self, rb_encoding_t *src_encoding, rb_encoding_t *dst_encoding,
	int behavior_for_invalid, int behavior_for_undefined, rb_str_t *replacement_str);

static inline rb_str_t *
str_simple_transcode(rb_str_t *self, rb_encoding_t *dst_encoding)
{
    return str_transcode(self, self->encoding, dst_encoding,
        TRANSCODE_BEHAVIOR_RAISE_EXCEPTION, TRANSCODE_BEHAVIOR_RAISE_EXCEPTION, NULL);
}

VALUE rb_str_substr_with_cache(VALUE str, long beg, long len,
	character_boundaries_cache_t *cache);
VALUE rb_reg_nth_match_with_cache(int nth, VALUE match,
	character_boundaries_cache_t *cache);

int rstr_compare(rb_str_t *str1, rb_str_t *str2);

void rb_str_NSCoder_encode(void *coder, VALUE str, const char *key);
VALUE rb_str_NSCoder_decode(void *coder, const char *key);

VALUE mr_enc_s_is_compatible(VALUE klass, SEL sel, VALUE str1, VALUE str2);
VALUE rb_str_intern_fast(VALUE str);
VALUE rstr_aref(VALUE str, SEL sel, int argc, VALUE *argv);
VALUE rstr_swapcase(VALUE str, SEL sel);
VALUE rstr_capitalize(VALUE str, SEL sel);
VALUE rstr_upcase(VALUE str, SEL sel);
VALUE rstr_downcase(VALUE str, SEL sel);
VALUE rstr_concat(VALUE self, SEL sel, VALUE other);

// The following functions should always been prefered over anything else,
// especially if this "else" is RSTRING_PTR and RSTRING_LEN.
// They also work on CFStrings.

VALUE rb_unicode_str_new(const UniChar *ptr, const size_t len);

#define STR_UCHARS_STATIC_BUFSIZE 64

typedef struct {
    UChar static_buf[STR_UCHARS_STATIC_BUFSIZE];
    UChar *chars;
    long len;
} rb_str_uchars_buf_t;

void rb_str_get_uchars_always(VALUE str, rb_str_uchars_buf_t *buf);

static inline void
rb_str_get_uchars(VALUE str, rb_str_uchars_buf_t *buf)
{
    if (IS_RSTR(str)) {
	rb_str_t *rstr = RSTR(str);
	if (rstr->encoding->ascii_compatible && str_is_ascii_only(rstr)
		&& rstr->length_in_bytes < STR_UCHARS_STATIC_BUFSIZE) {
	    // Fast path.
	    long i;
	    for (i = 0; i < rstr->length_in_bytes; i++) {
		buf->static_buf[i] = rstr->bytes[i];
	    }
	    buf->chars = buf->static_buf;
	    buf->len = rstr->length_in_bytes;
	    return;
	}
    }
    rb_str_get_uchars_always(str, buf);
}

UChar *rb_str_xcopy_uchars(VALUE str, long *len_p);

#define RB_STR_GET_UCHARS(str, _chars, _len) \
    rb_str_uchars_buf_t __buf; \
    rb_str_get_uchars(str, &__buf); \
    UChar *_chars = __buf.chars; \
    long _len = __buf.len

long rb_str_chars_len(VALUE str);
UChar rb_str_get_uchar(VALUE str, long pos);
void rb_str_append_uchar(VALUE str, UChar c);
void rb_str_append_uchars(VALUE str, const UChar *chars, long len);
unsigned long rb_str_hash_uchars(const UChar *chars, long chars_len);
long rb_uchar_strtol(UniChar *chars, long chars_len, long pos,
	long *end_offset);
void rb_str_force_encoding(VALUE str, rb_encoding_t *encoding);
rb_str_t *str_need_string(VALUE str);
rb_str_t *str_new_from_cfstring(CFStringRef source);
rb_str_t *replacement_string_for_encoding(rb_encoding_t* enc);
void str_replace_with_string(rb_str_t *self, rb_str_t *source);

static inline void
str_check_ascii_compatible(VALUE str)
{
    if (IS_RSTR(str) && !RSTR(str)->encoding->ascii_compatible) {
	rb_raise(rb_eEncCompatError, "ASCII incompatible encoding: %s",
		RSTR(str)->encoding->public_name);
    }
}

VALUE rb_f_string(VALUE, SEL, VALUE);
VALUE rstr_new_path(const char *path);

const char *nsstr_cstr(VALUE str);
long nsstr_clen(VALUE str);

static inline id
rstr_only(id rcv, SEL sel)
{
    rb_raise(rb_eArgError, "method `%s' does not work on NSStrings",
            sel_getName(sel));
    return rcv; // never reached
}

#if defined(__cplusplus)
} // extern "C"
#endif

#endif /* __ENCODING_H_ */
