#ifndef _PARSER_H_
#define _PARSER_H_

#include "ruby.h"

#if WITH_OBJC
/* We cannot use the GC memory functions here because the underlying libedit
 * function will call free() on the memory, resulting in a leak.
 */
# undef ALLOC
# define ALLOC(type) (type*)malloc(sizeof(type))
# undef ALLOC_N
# define ALLOC_N(type,n) ((type *)malloc(sizeof(type) * (n)))
# undef REALLOC_N
# define REALLOC_N(var,type,n) \
    (var)=(type*)realloc((char*)(var),(n) * sizeof(type))
# define ruby_xfree(x) free(x)
#endif

#if HAVE_RE_H
#include "re.h"
#endif

#ifdef HAVE_RUBY_ENCODING_H
#include "ruby/encoding.h"
#define FORCE_UTF8(obj) rb_enc_associate((obj), rb_utf8_encoding())
#else
#define FORCE_UTF8(obj)
#endif
#ifdef HAVE_RUBY_ST_H
#include "ruby/st.h"
#else
#include "st.h"
#endif

#define option_given_p(opts, key) RTEST(rb_funcall(opts, i_key_p, 1, key))

/* unicode */

typedef unsigned long	UTF32;	/* at least 32 bits */
typedef unsigned short UTF16;	/* at least 16 bits */
typedef unsigned char	UTF8;	  /* typically 8 bits */

#define UNI_REPLACEMENT_CHAR (UTF32)0x0000FFFD
#define UNI_SUR_HIGH_START  (UTF32)0xD800
#define UNI_SUR_HIGH_END    (UTF32)0xDBFF
#define UNI_SUR_LOW_START   (UTF32)0xDC00
#define UNI_SUR_LOW_END     (UTF32)0xDFFF

typedef struct JSON_ParserStruct {
    VALUE Vsource;
    char *source;
    long len;
    char *memo;
    VALUE create_id;
    int max_nesting;
    int current_nesting;
    int allow_nan;
    int parsing_name;
    int symbolize_names;
    VALUE object_class;
    VALUE array_class;
		int create_additions;
		VALUE match_string;
} JSON_Parser;

#define GET_PARSER                          \
    JSON_Parser *json;                      \
    Data_Get_Struct(self, JSON_Parser, json)

#define MinusInfinity "-Infinity"
#define EVIL 0x666

static UTF32 unescape_unicode(const unsigned char *p);
static int convert_UTF32_to_UTF8(char *buf, UTF32 ch);
static char *JSON_parse_object(JSON_Parser *json, char *p, char *pe, VALUE *result);
static char *JSON_parse_value(JSON_Parser *json, char *p, char *pe, VALUE *result);
static char *JSON_parse_integer(JSON_Parser *json, char *p, char *pe, VALUE *result);
static char *JSON_parse_float(JSON_Parser *json, char *p, char *pe, VALUE *result);
static char *JSON_parse_array(JSON_Parser *json, char *p, char *pe, VALUE *result);
static VALUE json_string_unescape(VALUE result, char *string, char *stringEnd);
static char *JSON_parse_string(JSON_Parser *json, char *p, char *pe, VALUE *result);
static VALUE convert_encoding(VALUE source);
static VALUE cParser_initialize(int argc, VALUE *argv, VALUE self);
static VALUE cParser_parse(VALUE self);
static JSON_Parser *JSON_allocate();
static void JSON_mark(JSON_Parser *json);
static void JSON_free(JSON_Parser *json);
static VALUE cJSON_parser_s_allocate(VALUE klass);
static VALUE cParser_source(VALUE self);

#endif
