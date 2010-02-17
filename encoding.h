/* 
 * MacRuby implementation of Ruby 1.9 String.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 * 
 * Copyright (C) 2007-2010, Apple Inc. All rights reserved.
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 * Copyright (C) 2000 Network Applied Communication Laboratory, Inc.
 * Copyright (C) 2000 Information-technology Promotion Agency, Japan
 */

#ifndef __ENCODING_H_
#define __ENCODING_H_

#if defined(__cplusplus)
extern "C" {
#endif

#include "ruby.h"
#include <stdbool.h>
#include "unicode/ustring.h"

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

#define NATIVE_UTF16_ENC(encoding) \
    ((encoding) == rb_encodings[ENCODING_UTF16_NATIVE])
#define NON_NATIVE_UTF16_ENC(encoding) \
    ((encoding) == rb_encodings[ENCODING_UTF16_NON_NATIVE])
#define UTF16_ENC(encoding) \
    (NATIVE_UTF16_ENC(encoding) || NON_NATIVE_UTF16_ENC(encoding))
#define NATIVE_UTF32_ENC(encoding) \
    ((encoding) == rb_encodings[ENCODING_UTF32_NATIVE])
#define NON_NATIVE_UTF32_ENC(encoding) \
    ((encoding) == rb_encodings[ENCODING_UTF32_NON_NATIVE])
#define UTF32_ENC(encoding) \
    (NATIVE_UTF32_ENC(encoding) || NON_NATIVE_UTF32_ENC(encoding))
#define BINARY_ENC(encoding) ((encoding) == rb_encodings[ENCODING_BINARY])

typedef uint8_t str_flag_t;

typedef struct {
    struct RBasic basic;
    struct rb_encoding *encoding;
    long capacity_in_bytes;
    long length_in_bytes;
    union {
	char *bytes;
	UChar *uchars;
    } data;
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

typedef struct {
    long start_offset_in_bytes;
    long end_offset_in_bytes;
} character_boundaries_t;

typedef struct {
    void (*update_flags)(rb_str_t *);
    void (*make_data_binary)(rb_str_t *);
    bool (*try_making_data_uchars)(rb_str_t *);
    long (*length)(rb_str_t *, bool);
    long (*bytesize)(rb_str_t *);
    character_boundaries_t (*get_character_boundaries)(rb_str_t *, long, bool);
    long (*offset_in_bytes_to_index)(rb_str_t *, long, bool);
} encoding_methods_t;

typedef struct rb_encoding {
    struct RBasic basic;
    unsigned int index;
    const char *public_name;
    const char **aliases;
    unsigned int aliases_count;
    unsigned char min_char_size;
    bool single_byte_encoding : 1;
    bool ascii_compatible : 1;
    encoding_methods_t methods;
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
    ENCODING_MACROMAN,
    //ENCODING_EUCJP,
    //ENCODING_SJIS,
    //ENCODING_CP932,

    ENCODINGS_COUNT
};

extern rb_encoding_t *rb_encodings[ENCODINGS_COUNT];

#define STRING_HAS_SUPPLEMENTARY     0x020
#define STRING_HAS_SUPPLEMENTARY_SET 0x010
#define STRING_ASCII_ONLY            0x008
#define STRING_ASCII_ONLY_SET        0x010
#define STRING_ASCII_ONLY            0x008
#define STRING_VALID_ENCODING_SET    0x004
#define STRING_VALID_ENCODING        0x002
#define STRING_STORED_IN_UCHARS      0x001

#define STRING_REQUIRED_FLAGS STRING_STORED_IN_UCHARS

#define BYTES_TO_UCHARS(len) ((len) / sizeof(UChar))
#define UCHARS_TO_BYTES(len) ((len) * sizeof(UChar))

#define ODD_NUMBER(x) ((x) & 0x1)

static inline long
div_round_up(long a, long b)
{
    return ((a) + (b - 1)) / b;
}

void str_update_flags(rb_str_t *self);

static inline void
str_unset_facultative_flags(rb_str_t *self)
{
    self->flags &= ~STRING_HAS_SUPPLEMENTARY_SET & ~STRING_ASCII_ONLY_SET
	& ~STRING_VALID_ENCODING_SET;
}

static inline bool
str_known_to_have_an_invalid_encoding(rb_str_t *self)
{
    return (self->flags & (STRING_VALID_ENCODING_SET
		| STRING_VALID_ENCODING)) == STRING_VALID_ENCODING_SET;
}

static inline bool
str_known_not_to_have_any_supplementary(rb_str_t *self)
{
    return (self->flags & (STRING_HAS_SUPPLEMENTARY_SET
		| STRING_HAS_SUPPLEMENTARY)) == STRING_HAS_SUPPLEMENTARY_SET;
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

static inline bool
str_is_stored_in_uchars(rb_str_t *self)
{
    return self->flags & STRING_STORED_IN_UCHARS;
}

static inline void
str_negate_stored_in_uchars(rb_str_t *self)
{
    self->flags ^= STRING_STORED_IN_UCHARS;
}

static inline void
str_set_stored_in_uchars(rb_str_t *self, bool status)
{
    if (status) {
	self->flags |= STRING_STORED_IN_UCHARS;
    }
    else {
	self->flags &= ~STRING_STORED_IN_UCHARS;
    }
}

static inline void
str_set_facultative_flag(rb_str_t *self, bool status, str_flag_t flag_set,
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
str_set_has_supplementary(rb_str_t *self, bool status)
{
    str_set_facultative_flag(self, status, STRING_HAS_SUPPLEMENTARY_SET,
	    STRING_HAS_SUPPLEMENTARY);
}

static inline void
str_set_ascii_only(rb_str_t *self, bool status)
{
    str_set_facultative_flag(self, status, STRING_ASCII_ONLY_SET,
	    STRING_ASCII_ONLY);
}

static inline void
str_set_valid_encoding(rb_str_t *self, bool status)
{
    str_set_facultative_flag(self, status, STRING_VALID_ENCODING_SET,
	    STRING_VALID_ENCODING);
}

// Return a string object appropriate for bstr_ calls. This does nothing for
// data/binary RubyStrings.
VALUE rb_str_bstr(VALUE str);

// Byte strings APIs. Use this only when dealing with raw data.
VALUE bstr_new(void);
VALUE bstr_new_with_data(const uint8_t *bytes, long len);
uint8_t *bstr_bytes(VALUE str);
long bstr_length(VALUE str);
void bstr_set_length(VALUE str, long len);
void bstr_resize(VALUE str, long capa);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif /* __ENCODING_H_ */
