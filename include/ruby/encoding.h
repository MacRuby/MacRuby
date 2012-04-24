/*
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2007-2010, Apple Inc. All rights reserved
 * Copyright (C) 2007 Yukihiro Matsumoto
 */

#ifndef RUBY_ENCODING_H
#define RUBY_ENCODING_H 1

#if defined(__cplusplus)
extern "C" {
#endif

#include <stdarg.h>

typedef struct rb_encoding rb_encoding;

#define ENCODING_INLINE_MAX 1023

#define ENCODING_GET(obj) (rb_enc_get_index((VALUE)obj))
#define ENCODING_GET_INLINED(obj) (ENCODING_GET(obj))
#define ENCODING_SET(obj, idx) (rb_enc_set_index((VALUE)obj, idx))

int rb_enc_get_index(VALUE obj);
void rb_enc_set_index(VALUE obj, int encindex);
int rb_enc_find_index(const char *name);
int rb_enc_to_index(VALUE enc);
int rb_to_encoding_index(VALUE);
int rb_ascii8bit_encindex(void);
int rb_utf8_encindex(void);
int rb_usascii_encindex(void);
rb_encoding* rb_to_encoding(VALUE);
rb_encoding* rb_enc_get(VALUE);
VALUE rb_enc_associate_index(VALUE, int);
VALUE rb_enc_associate(VALUE, rb_encoding*);
void rb_enc_copy(VALUE dst, VALUE src);
VALUE rb_str_export_to_enc(VALUE str, rb_encoding *enc);

VALUE rb_enc_str_new(const char*, long, rb_encoding*);
PRINTF_ARGS(VALUE rb_enc_sprintf(rb_encoding *, const char*, ...), 2, 3);
VALUE rb_enc_vsprintf(rb_encoding *, const char*, va_list);
VALUE rb_enc_str_buf_cat(VALUE str, const char *ptr, long len, rb_encoding *enc);

/* index -> rb_encoding */
rb_encoding* rb_enc_from_index(int idx);

/* name -> rb_encoding */
rb_encoding * rb_enc_find(const char *name);

/* encoding -> name */
const char *rb_enc_name(rb_encoding *);
VALUE rb_enc_name2(rb_encoding *);

/* encoding -> minlen/maxlen */
long rb_enc_mbminlen(rb_encoding *);
long rb_enc_mbmaxlen(rb_encoding *);

#include <wctype.h>

#define rb_enc_isctype(c,t,enc)	(iswctype(c,t))
#define rb_enc_isascii(c,enc)	(iswascii(c))
#define rb_enc_isalpha(c,enc)	(iswalpha(c))
#define rb_enc_islower(c,enc)	(iswlower(c))
#define rb_enc_isupper(c,enc)	(iswupper(c))
#define rb_enc_isalnum(c,enc)	(iswalnum(c))
#define rb_enc_isprint(c,enc)	(iswprint(c))
#define rb_enc_isspace(c,enc)	(iswspace(c))
#define rb_enc_isdigit(c,enc)	(iswdigit(c))

#define rb_enc_asciicompat(enc) (rb_enc_mbminlen(enc)==1 && !rb_enc_dummy_p(enc))
#define rb_enc_str_asciicompat_p(str) rb_enc_asciicompat(rb_enc_get(str))

ID rb_intern3(const char*, long, rb_encoding*);
VALUE rb_enc_from_encoding(rb_encoding *enc);
rb_encoding *rb_ascii8bit_encoding(void);
rb_encoding *rb_utf8_encoding(void);
rb_encoding *rb_usascii_encoding(void);
rb_encoding *rb_locale_encoding(void);
rb_encoding *rb_default_external_encoding(void);
rb_encoding *rb_default_internal_encoding(void);
int rb_usascii_encindex(void);
int rb_ascii8bit_encindex(void);
void rb_enc_set_default_external(VALUE encoding);

RUBY_EXTERN VALUE rb_cEncoding;

static inline int
rb_enc_dummy_p(rb_encoding *enc)
{
    return Qfalse;
}

VALUE rb_str_transcode(VALUE str, VALUE to);

// CRuby compat.
#define rb_external_str_new_with_enc rb_enc_str_new

#if defined(__cplusplus)
} // extern "C" {
#endif

#endif /* RUBY_ENCODING_H */
