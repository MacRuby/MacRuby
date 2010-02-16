#include "encoding.h"
#include <string.h>

// TODO:
// - use rb_usascii_str_new_cstr instead of rb_str_new2

VALUE rb_cMREncoding = 0;

#define ENC(x) ((encoding_t *)(x))
#define OBJC_CLASS(x) (*(VALUE *)(x))

encoding_t *default_internal = NULL;
encoding_t *default_external = NULL;
encoding_t *encodings[ENCODINGS_COUNT];

static void str_undefined_update_flags(string_t *self) { abort(); }
static void str_undefined_make_data_binary(string_t *self) { abort(); }
static bool str_undefined_try_making_data_uchars(string_t *self) { abort(); }
static long str_undefined_length(string_t *self, bool ucs2_mode) { abort(); }
static long str_undefined_bytesize(string_t *self) { abort(); }
static character_boundaries_t str_undefined_get_character_boundaries(string_t *self, long index, bool ucs2_mode) { abort(); }
static long str_undefined_offset_in_bytes_to_index(string_t *self, long offset_in_bytes, bool ucs2_mode) { abort(); }

static VALUE
mr_enc_s_list(VALUE klass, SEL sel)
{
    VALUE ary = rb_ary_new2(ENCODINGS_COUNT);
    for (unsigned int i = 0; i < ENCODINGS_COUNT; ++i) {
	rb_ary_push(ary, (VALUE)encodings[i]);
    }
    return ary;
}

static VALUE
mr_enc_s_name_list(VALUE klass, SEL sel)
{
    VALUE ary = rb_ary_new();
    for (unsigned int i = 0; i < ENCODINGS_COUNT; ++i) {
	encoding_t *encoding = ENC(encodings[i]);
	// TODO: use US-ASCII strings
	rb_ary_push(ary, rb_str_new2(encoding->public_name));
	for (unsigned int j = 0; j < encoding->aliases_count; ++j) {
	    rb_ary_push(ary, rb_str_new2(encoding->aliases[j]));
	}
    }
    return ary;
}

static VALUE
mr_enc_s_aliases(VALUE klass, SEL sel)
{
    VALUE hash = rb_hash_new();
    for (unsigned int i = 0; i < ENCODINGS_COUNT; ++i) {
	encoding_t *encoding = ENC(encodings[i]);
	for (unsigned int j = 0; j < encoding->aliases_count; ++j) {
	    rb_hash_aset(hash,
		    rb_str_new2(encoding->aliases[j]),
		    rb_str_new2(encoding->public_name));
	}
    }
    return hash;
}

static VALUE
mr_enc_s_default_internal(VALUE klass, SEL sel)
{
    return (VALUE)default_internal;
}

static VALUE
mr_enc_s_default_external(VALUE klass, SEL sel)
{
    return (VALUE)default_external;
}

static VALUE
mr_enc_name(VALUE self, SEL sel)
{
    return rb_str_new2(ENC(self)->public_name);
}

static VALUE
mr_enc_inspect(VALUE self, SEL sel)
{
    return rb_sprintf("#<%s:%s>", rb_obj_classname(self), ENC(self)->public_name);
}

static VALUE
mr_enc_names(VALUE self, SEL sel)
{
    encoding_t *encoding = ENC(self);

    VALUE ary = rb_ary_new2(encoding->aliases_count + 1);
    rb_ary_push(ary, rb_str_new2(encoding->public_name));
    for (unsigned int i = 0; i < encoding->aliases_count; ++i) {
	rb_ary_push(ary, rb_str_new2(encoding->aliases[i]));
    }
    return ary;
}

static VALUE
mr_enc_ascii_compatible_p(VALUE self, SEL sel)
{
    return ENC(self)->ascii_compatible ? Qtrue : Qfalse;
}

static VALUE
mr_enc_dummy_p(VALUE self, SEL sel)
{
    return Qfalse;
}

static void
define_encoding_constant(const char *name, encoding_t *encoding)
{
    char c = name[0];
    if ((c >= '0') && (c <= '9')) {
	// constants can't start with a number
	return;
    }

    char *name_copy = strdup(name);
    if ((c >= 'a') && (c <= 'z')) {
	// the first character must be upper case
	name_copy[0] = c - ('a' - 'A');
    }

    // '.' and '-' must be transformed into '_'
    for (int i = 0; name_copy[i]; ++i) {
	if ((name_copy[i] == '.') || (name_copy[i] == '-')) {
	    name_copy[i] = '_';
	}
    }
    rb_define_const(rb_cMREncoding, name_copy, (VALUE)encoding);
    free(name_copy);
}

extern void enc_init_ucnv_encoding(encoding_t *encoding);

enum {
    ENCODING_TYPE_SPECIAL = 0,
    ENCODING_TYPE_UCNV
};

static void
add_encoding(
	unsigned int encoding_index, // index of the encoding in the encodings array
	unsigned int encoding_type,
	const char *public_name, // public name for the encoding
	unsigned char min_char_size,
	bool single_byte_encoding, // in the encoding a character takes only one byte
	bool ascii_compatible, // is the encoding ASCII compatible or not
	... // aliases for the encoding (should no include the public name) - must end with a NULL
	)
{
    assert(encoding_index < ENCODINGS_COUNT);

    // create an array for the aliases
    unsigned int aliases_count = 0;
    va_list va_aliases;
    va_start(va_aliases, ascii_compatible);
    while (va_arg(va_aliases, const char *) != NULL) {
	++aliases_count;
    }
    va_end(va_aliases);
    const char **aliases = (const char **) malloc(sizeof(const char *) * aliases_count);
    va_start(va_aliases, ascii_compatible);
    for (unsigned int i = 0; i < aliases_count; ++i) {
	aliases[i] = va_arg(va_aliases, const char *);
    }
    va_end(va_aliases);

    // create the MacRuby object
    NEWOBJ(encoding, encoding_t);
    encoding->basic.flags = 0;
    encoding->basic.klass = rb_cMREncoding;
    encodings[encoding_index] = encoding;
    rb_objc_retain(encoding); // it should never be deallocated

    // fill the fields
    encoding->index = encoding_index;
    encoding->public_name = public_name;
    encoding->min_char_size = min_char_size;
    encoding->single_byte_encoding = single_byte_encoding;
    encoding->ascii_compatible = ascii_compatible;
    encoding->aliases_count = aliases_count;
    encoding->aliases = aliases;

    // fill the default implementations with aborts
    encoding->methods.update_flags = str_undefined_update_flags;
    encoding->methods.make_data_binary = str_undefined_make_data_binary;
    encoding->methods.try_making_data_uchars = str_undefined_try_making_data_uchars;
    encoding->methods.length = str_undefined_length;
    encoding->methods.bytesize = str_undefined_bytesize;
    encoding->methods.get_character_boundaries = str_undefined_get_character_boundaries;
    encoding->methods.offset_in_bytes_to_index = str_undefined_offset_in_bytes_to_index;

    switch (encoding_type) {
	case ENCODING_TYPE_SPECIAL:
	    break;
	case ENCODING_TYPE_UCNV:
	    enc_init_ucnv_encoding(encoding);
	    break;
	default:
	    abort();
    }

    // create constants
    define_encoding_constant(public_name, encoding);
    for (unsigned int i = 0; i < aliases_count; ++i) {
	define_encoding_constant(aliases[i], encoding);
    }

    free(aliases);
}

static void
create_encodings(void)
{
    add_encoding(ENCODING_BINARY,    ENCODING_TYPE_SPECIAL, "ASCII-8BIT",  1, true,  true,  "BINARY", NULL);
    add_encoding(ENCODING_ASCII,     ENCODING_TYPE_UCNV,    "US-ASCII",    1, true,  true,  "ASCII", "ANSI_X3.4-1968", "646", NULL);
    add_encoding(ENCODING_UTF8,      ENCODING_TYPE_UCNV,    "UTF-8",       1, false, true,  "CP65001", NULL);
    add_encoding(ENCODING_UTF16BE,   ENCODING_TYPE_UCNV,    "UTF-16BE",    2, false, false, NULL);
    add_encoding(ENCODING_UTF16LE,   ENCODING_TYPE_UCNV,    "UTF-16LE",    2, false, false, NULL);
    add_encoding(ENCODING_UTF32BE,   ENCODING_TYPE_UCNV,    "UTF-32BE",    4, false, false, "UCS-4BE", NULL);
    add_encoding(ENCODING_UTF32LE,   ENCODING_TYPE_UCNV,    "UTF-32LE",    4, false, false, "UCS-4LE", NULL);
    add_encoding(ENCODING_ISO8859_1, ENCODING_TYPE_UCNV,    "ISO-8859-1",  1, true,  true,  "ISO8859-1", NULL);
    add_encoding(ENCODING_MACROMAN,  ENCODING_TYPE_UCNV,    "macRoman",    1, true,  true,  NULL);
    // FIXME: the ICU conversion tables do not seem to match Ruby's Japanese conversion tables
    //add_encoding(ENCODING_EUCJP,     ENCODING_TYPE_RUBY, "EUC-JP",      1, false, true,  "eucJP", NULL);
    //add_encoding(ENCODING_SJIS,      ENCODING_TYPE_RUBY, "Shift_JIS",   1, false, true, "SJIS", NULL);
    //add_encoding(ENCODING_CP932,     ENCODING_TYPE_RUBY, "Windows-31J", 1, false, true, "CP932", "csWindows31J", NULL);

    default_external = encodings[ENCODING_UTF8];
    default_internal = encodings[ENCODING_UTF16_NATIVE];
}

VALUE
mr_enc_s_is_compatible(VALUE klass, SEL sel, VALUE str1, VALUE str2);

void
Init_MREncoding(void)
{
    rb_cMREncoding = rb_define_class("MREncoding", rb_cObject);
    rb_undef_alloc_func(rb_cMREncoding);

    rb_objc_define_method(rb_cMREncoding, "to_s", mr_enc_name, 0);
    rb_objc_define_method(rb_cMREncoding, "inspect", mr_enc_inspect, 0);
    rb_objc_define_method(rb_cMREncoding, "name", mr_enc_name, 0);
    rb_objc_define_method(rb_cMREncoding, "names", mr_enc_names, 0);
    rb_objc_define_method(rb_cMREncoding, "dummy?", mr_enc_dummy_p, 0);
    rb_objc_define_method(rb_cMREncoding, "ascii_compatible?", mr_enc_ascii_compatible_p, 0);
    rb_objc_define_method(OBJC_CLASS(rb_cMREncoding), "list", mr_enc_s_list, 0);
    rb_objc_define_method(OBJC_CLASS(rb_cMREncoding), "name_list", mr_enc_s_name_list, 0);
    rb_objc_define_method(OBJC_CLASS(rb_cMREncoding), "aliases", mr_enc_s_aliases, 0);
    //rb_define_singleton_method(rb_cMREncoding, "find", enc_find, 1);
    // it's defined on Encoding, but it requires String's internals so it's defined with String
    rb_objc_define_method(OBJC_CLASS(rb_cMREncoding), "compatible?", mr_enc_s_is_compatible, 2);

    //rb_define_method(rb_cEncoding, "_dump", enc_dump, -1);
    //rb_define_singleton_method(rb_cEncoding, "_load", enc_load, 1);

    rb_objc_define_method(OBJC_CLASS(rb_cMREncoding), "default_external", mr_enc_s_default_external, 0);
    //rb_define_singleton_method(rb_cMREncoding, "default_external=", set_default_external, 1);
    rb_objc_define_method(OBJC_CLASS(rb_cMREncoding), "default_internal", mr_enc_s_default_internal, 0);
    //rb_define_singleton_method(rb_cMREncoding, "default_internal=", set_default_internal, 1);
    //rb_define_singleton_method(rb_cMREncoding, "locale_charmap", rb_locale_charmap, 0);

    create_encodings();
}
