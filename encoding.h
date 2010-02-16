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

#define NATIVE_UTF16_ENC(encoding) ((encoding) == encodings[ENCODING_UTF16_NATIVE])
#define NON_NATIVE_UTF16_ENC(encoding) ((encoding) == encodings[ENCODING_UTF16_NON_NATIVE])
#define UTF16_ENC(encoding) (NATIVE_UTF16_ENC(encoding) || NON_NATIVE_UTF16_ENC(encoding))
#define NATIVE_UTF32_ENC(encoding) ((encoding) == encodings[ENCODING_UTF32_NATIVE])
#define NON_NATIVE_UTF32_ENC(encoding) ((encoding) == encodings[ENCODING_UTF32_NON_NATIVE])
#define UTF32_ENC(encoding) (NATIVE_UTF32_ENC(encoding) || NON_NATIVE_UTF32_ENC(encoding))
#define BINARY_ENC(encoding) ((encoding) == encodings[ENCODING_BINARY])

typedef uint8_t str_flag_t;

typedef struct  {
    struct RBasic basic;
    struct encoding_s *encoding;
    long capacity_in_bytes;
    long length_in_bytes;
    union {
	char *bytes;
	UChar *uchars;
    } data;
    str_flag_t flags;
} string_t;

typedef struct {
    long start_offset_in_bytes;
    long end_offset_in_bytes;
} character_boundaries_t;

typedef struct {
    void (*update_flags)(string_t *);
    void (*make_data_binary)(string_t *);
    bool (*try_making_data_uchars)(string_t *);
    long (*length)(string_t *, bool);
    long (*bytesize)(string_t *);
    character_boundaries_t (*get_character_boundaries)(string_t *, long, bool);
    long (*offset_in_bytes_to_index)(string_t *, long, bool);
} encoding_methods_t;

typedef struct encoding_s {
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
} encoding_t;

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

extern encoding_t *encodings[ENCODINGS_COUNT];

extern VALUE rb_cMREncoding;

#define STRING_HAS_SUPPLEMENTARY     0x020
#define STRING_HAS_SUPPLEMENTARY_SET 0x010
#define STRING_ASCII_ONLY            0x008
#define STRING_ASCII_ONLY_SET        0x010
#define STRING_ASCII_ONLY            0x008
#define STRING_VALID_ENCODING_SET    0x004
#define STRING_VALID_ENCODING        0x002
#define STRING_STORED_IN_UCHARS      0x001

#define STRING_REQUIRED_FLAGS STRING_STORED_IN_UCHARS

#define STR(x) ((string_t *)(x))

#define BYTES_TO_UCHARS(len) ((len) / sizeof(UChar))
#define UCHARS_TO_BYTES(len) ((len) * sizeof(UChar))

#define ODD_NUMBER(x) ((x) & 0x1)

static inline long
div_round_up(long a, long b)
{
    return ((a) + (b - 1)) / b;
}

void
str_update_flags(string_t *self);

static inline void
str_unset_facultative_flags(string_t *self)
{
    self->flags &= ~STRING_HAS_SUPPLEMENTARY_SET & ~STRING_ASCII_ONLY_SET & ~STRING_VALID_ENCODING_SET;
}

static inline bool
str_known_to_have_an_invalid_encoding(string_t *self)
{
    return (self->flags & (STRING_VALID_ENCODING_SET | STRING_VALID_ENCODING)) == STRING_VALID_ENCODING_SET;
}

static inline bool
str_known_not_to_have_any_supplementary(string_t *self)
{
    return (self->flags & (STRING_HAS_SUPPLEMENTARY_SET | STRING_HAS_SUPPLEMENTARY)) == STRING_HAS_SUPPLEMENTARY_SET;
}

static inline bool
str_check_flag_and_update_if_needed(string_t *self, str_flag_t flag_set, str_flag_t flag)
{
    if (!(self->flags & flag_set)) {
	str_update_flags(self);
	assert(self->flags & flag_set);
    }
    return self->flags & flag;
}

static inline bool
str_is_valid_encoding(string_t *self)
{
    return str_check_flag_and_update_if_needed(self, STRING_VALID_ENCODING_SET, STRING_VALID_ENCODING);
}

static inline bool
str_is_ascii_only(string_t *self)
{
    return str_check_flag_and_update_if_needed(self, STRING_ASCII_ONLY_SET, STRING_ASCII_ONLY);
}

static inline bool
str_is_ruby_ascii_only(string_t *self)
{
    // for MRI, a string in a non-ASCII-compatible encoding (like UTF-16)
    // containing only ASCII characters is not "ASCII only" though for us it is internally
    if (!self->encoding->ascii_compatible) {
	return false;
    }

    return str_is_ascii_only(self);
}

static inline bool
str_is_stored_in_uchars(string_t *self)
{
    return self->flags & STRING_STORED_IN_UCHARS;
}

static inline void
str_negate_stored_in_uchars(string_t *self)
{
    self->flags ^= STRING_STORED_IN_UCHARS;
}

static inline void
str_set_stored_in_uchars(string_t *self, bool status)
{
    if (status) {
	self->flags |= STRING_STORED_IN_UCHARS;
    }
    else {
	self->flags &= ~STRING_STORED_IN_UCHARS;
    }
}

static inline void
str_set_facultative_flag(string_t *self, bool status, str_flag_t flag_set, str_flag_t flag)
{
    if (status) {
	self->flags = self->flags | flag_set | flag;
    }
    else {
	self->flags = (self->flags | flag_set) & ~flag;
    }
}

static inline void
str_set_has_supplementary(string_t *self, bool status)
{
    str_set_facultative_flag(self, status, STRING_HAS_SUPPLEMENTARY_SET, STRING_HAS_SUPPLEMENTARY);
}

static inline void
str_set_ascii_only(string_t *self, bool status)
{
    str_set_facultative_flag(self, status, STRING_ASCII_ONLY_SET, STRING_ASCII_ONLY);
}

static inline void
str_set_valid_encoding(string_t *self, bool status)
{
    str_set_facultative_flag(self, status, STRING_VALID_ENCODING_SET, STRING_VALID_ENCODING);
}

#if defined(__cplusplus)
}
#endif

#endif /* __ENCODING_H_ */
