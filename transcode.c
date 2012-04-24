/*
 * MacRuby implementation of transcode.c.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2007-2011, Apple Inc. All rights reserved.
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 * Copyright (C) 2000 Network Applied Communication Laboratory, Inc.
 * Copyright (C) 2000 Information-technology Promotion Agency, Japan
 */
 
// Notes:
// AFAICT, we need to add support for newline decorators.

#include "macruby_internal.h"
#include "ruby/encoding.h"
#include "encoding.h"

static VALUE sym_invalid;
static VALUE sym_undef;
static VALUE sym_replace;
static VALUE sym_xml;
static VALUE sym_text;
static VALUE sym_attr;

typedef struct rb_econv_s {
    rb_encoding_t *source;
    rb_encoding_t *destination;
    transcode_behavior_t invalid_sequence_behavior;
    transcode_behavior_t undefined_conversion_behavior;
    transcode_flags_t special_flags;
    rb_str_t *replacement;
    bool finished;
} rb_econv_t;

VALUE rb_cEncodingConverter;

static rb_econv_t* RConverter(VALUE self) {
    rb_econv_t *conv;
    Data_Get_Struct(self, rb_econv_t, conv);
    return conv;
}

static VALUE
rb_econv_alloc(VALUE klass, SEL sel)
{
    rb_econv_t *conv = ALLOC(rb_econv_t);
    conv->source = NULL;
    conv->destination = NULL;
    conv->replacement = NULL;
    conv->special_flags = 0;
    conv->finished = false;
    return Data_Wrap_Struct(klass, 0, 0, conv);
}

static VALUE 
rb_econv_asciicompat_encoding(VALUE klass, SEL sel, VALUE arg)
{
    rb_encoding_t *enc = NULL;
    if (CLASS_OF(arg) == rb_cEncoding) {
        enc = rb_to_encoding(arg);
    } 
    else {
        StringValue(arg);
        enc = rb_enc_find(RSTRING_PTR(arg));
    }

    if ((enc == NULL) || (enc->ascii_compatible)) {
        return Qnil;
    }
    else if (IS_UTF16_ENC(enc) || IS_UTF32_ENC(enc)) {
        return (VALUE)rb_utf8_encoding();
    }
    // TODO: Port MRI's table that maps ASCII-incompatible encodings to compatible ones.
    rb_raise(rb_eConverterNotFoundError, "could not find ASCII-compatible encoding for %s", enc->public_name);
}

static VALUE rb_econv_convpath(VALUE self, SEL sel);

static VALUE
rb_econv_search_convpath(VALUE klass, SEL sel, int argc, VALUE* argv)
{
    return rb_econv_convpath(rb_class_new_instance(argc, argv, klass), sel);
}

static transcode_behavior_t
symbol_option_with_default(VALUE given_symbol, transcode_behavior_t otherwise, const char* name)
{
    if (given_symbol == sym_replace) {
        return TRANSCODE_BEHAVIOR_REPLACE_WITH_STRING;
    }
    else if (given_symbol == sym_attr) {
        return TRANSCODE_BEHAVIOR_REPLACE_WITH_XML_ATTR;
    }
    else if (given_symbol == sym_text) {
        return TRANSCODE_BEHAVIOR_REPLACE_WITH_XML_TEXT;
    }
    else if (!NIL_P(given_symbol)) {
        rb_raise(rb_eArgError, "unknown value '%s' for option %s", StringValuePtr(given_symbol), name);
    }
    return otherwise;
}

static void parse_conversion_options(VALUE options, transcode_behavior_t* behavior_for_invalid, 
    transcode_behavior_t* behavior_for_undefined, rb_str_t** replacement_str, rb_encoding_t* destination) 
{
    
    *behavior_for_invalid = symbol_option_with_default(rb_hash_aref(options, sym_invalid), 
        TRANSCODE_BEHAVIOR_RAISE_EXCEPTION, "invalid-character");
    
    *behavior_for_undefined = symbol_option_with_default(rb_hash_aref(options, sym_undef),
        TRANSCODE_BEHAVIOR_RAISE_EXCEPTION, "undefined-conversion");
    
    // Because the API conflates the :xml and :undef options, we pass in the previous setting
    *behavior_for_undefined = symbol_option_with_default(rb_hash_aref(options, sym_xml),
        *behavior_for_undefined, "xml-replacement");
    
    *behavior_for_undefined = symbol_option_with_default(rb_hash_aref(options, sym_xml),
        *behavior_for_undefined, "xml-replacement");
    
    VALUE replacement = rb_hash_aref(options, sym_replace);
    if (!NIL_P(replacement)) {
        *replacement_str = str_simple_transcode(str_need_string(replacement), destination);
    }
    
}

static VALUE
rb_econv_initialize(VALUE self, SEL sel, int argc, VALUE* argv)
{
    rb_econv_t *conv = RConverter(self);
    VALUE sourceobj, destobj, options;
    rb_scan_args(argc, argv, "21", &sourceobj, &destobj, &options);
    
    rb_encoding_t* source = rb_to_encoding(sourceobj);
    rb_encoding_t* destination = rb_to_encoding(destobj);
    rb_str_t* replacement_str = NULL;
    
    conv->source = source;
    conv->destination = destination;

    conv->invalid_sequence_behavior = TRANSCODE_BEHAVIOR_RAISE_EXCEPTION;
    conv->undefined_conversion_behavior = TRANSCODE_BEHAVIOR_RAISE_EXCEPTION;
    
    // Extract the options. This is a hateful, hateful API.
    if (!NIL_P(options)) {
        
        if (FIXNUM_P(options)) {
            rb_bug("fixnum arguments are not supported yet.");
        } 
        else if (TYPE(options) == T_HASH) {
            parse_conversion_options(options, &conv->invalid_sequence_behavior, 
                &conv->undefined_conversion_behavior, &replacement_str, destination);
        } 
        else {
            rb_raise(rb_eArgError, "expected either a hash or a fixnum as the last parameter");
        }   
    }
    
    // Get the default replacement string. For UTF-[8, 16, 32] it's /uFFFD, and for others it's '?'
    if (replacement_str == NULL) {
        replacement_str = replacement_string_for_encoding(destination);
    }
    GC_WB(&conv->replacement, replacement_str);
    
    return self;
}

static VALUE
rb_econv_inspect(VALUE self, SEL sel)
{
    // TODO: make this comply with the MRI output when we add newline decorators
    rb_econv_t *conv = RConverter(self);
    return rb_sprintf("#<%s: %s to %s>", rb_obj_classname(self), conv->source->public_name, 
        conv->destination->public_name);
}

static VALUE
rb_econv_convpath(VALUE self, SEL sel)
{
    // in MacRuby, the convpath always looks like this:
    // [[source_encoding, native UTF-16], [native UTF-16, dest_encoding]]
    // The first element is omitted if the source encoding is UTF-16, obviously.
    rb_econv_t *conv = RConverter(self);
    VALUE to_return = rb_ary_new2(2);
    rb_encoding_t* nativeUTF16 = rb_encodings[ENCODING_UTF16_NATIVE];
    
    if (conv->source != nativeUTF16) {
        rb_ary_push(to_return, rb_assoc_new((VALUE)conv->source, (VALUE)nativeUTF16));
    }
    
    rb_ary_push(to_return, rb_assoc_new((VALUE)nativeUTF16, (VALUE)conv->destination));
    
    return to_return;
}

static VALUE
rb_econv_source_encoding(VALUE self, SEL sel)
{
    return (VALUE)(RConverter(self)->source);
}

static VALUE
rb_econv_destination_encoding(VALUE self, SEL sel)
{
    return (VALUE)(RConverter(self)->destination);
}

// Since our converter is basically a black box at this point, we'll leave 
// the lower-level methods unimplemented.
#define rb_econv_primitive_convert rb_f_notimplement

static VALUE 
rb_econv_convert(VALUE self, SEL sel, VALUE str)
{
    rb_econv_t *conv;
    Data_Get_Struct(self, rb_econv_t, conv);
    
    if (conv->finished) {
        rb_raise(rb_eArgError, "convert() called on a finished stream");
    }
    
    assert(conv->replacement->encoding == conv->destination);
    return (VALUE)str_transcode(str_need_string(str), conv->source, conv->destination, conv->invalid_sequence_behavior, conv->undefined_conversion_behavior, conv->replacement);
}

static VALUE
rb_econv_finish(VALUE self, SEL sel)
{
    // TODO: Flesh this out later.
    RConverter(self)->finished = true;
    return rb_str_new2("");
}

#define rb_econv_primitive_errinfo rb_f_notimplement

#define rb_econv_insert_output rb_f_notimplement

#define rb_econv_putback rb_f_notimplement

#define rb_econv_last_error rb_f_notimplement

static VALUE
rb_econv_replacement(VALUE self, SEL sel)
{
    return (VALUE)(RConverter(self)->replacement);
}

static VALUE 
rb_econv_set_replacement(VALUE self, SEL sel, VALUE str)
{
    // TODO: Should we copy this string? Probably.
    rb_econv_t *conv = RConverter(self);
    if (TYPE(str) != T_STRING) {
        rb_raise(rb_eTypeError, "wrong argument type %s (expected String)", rb_obj_classname(str));
    }
    rb_str_force_encoding(str, conv->destination);
    GC_WB(&conv->replacement, str_need_string(str));
    return str;
}

/*
 *  call-seq:
 *     str.encode(encoding [, options] )   => str
 *     str.encode(dst_encoding, src_encoding [, options] )   => str
 *     str.encode([options])   => str
 *
 *  The first form returns a copy of <i>str</i> transcoded
 *  to encoding +encoding+.
 *  The second form returns a copy of <i>str</i> transcoded
 *  from src_encoding to dst_encoding.
 *  The last form returns a copy of <i>str</i> transcoded to
 *  <code>Encoding.default_internal</code>.
 *  By default, the first and second form raise
 *  Encoding::UndefinedConversionError for characters that are
 *  undefined in the destination encoding, and
 *  Encoding::InvalidByteSequenceError for invalid byte sequences
 *  in the source encoding. The last form by default does not raise
 *  exceptions but uses replacement strings.
 *  The <code>options</code> Hash gives details for conversion.
 *
 *  === options
 *  The hash <code>options</code> can have the following keys:
 *  :invalid ::
 *    If the value is <code>:replace</code>, <code>#encode</code> replaces
 *    invalid byte sequences in <code>str</code> with the replacement character.
 *    The default is to raise the exception
 *  :undef ::
 *    If the value is <code>:replace</code>, <code>#encode</code> replaces
 *    characters which are undefined in the destination encoding with
 *    the replacement character.
 *  :replace ::
 *    Sets the replacement string to the value. The default replacement
 *    string is "\uFFFD" for Unicode encoding forms, and "?" otherwise.
 *  :xml ::
 *    The value must be <code>:text</code> or <code>:attr</code>.
 *    If the value is <code>:text</code> <code>#encode</code> replaces
 *    undefined characters with their (upper-case hexadecimal) numeric
 *    character references. '&', '<', and '>' are converted to "&amp;",
 *    "&lt;", and "&gt;", respectively.
 *    If the value is <code>:attr</code>, <code>#encode</code> also quotes
 *    the replacement result (using '"'), and replaces '"' with "&quot;".
 */
extern rb_encoding_t *default_internal;
static VALUE
rstr_encode(VALUE str, SEL sel, int argc, VALUE *argv)
{
    VALUE opt = Qnil;
    if (argc > 0) {
        opt = rb_check_convert_type(argv[argc-1], T_HASH, "Hash", "to_hash");
        if (!NIL_P(opt)) {
            argc--;
        }
    }

    rb_str_t *self = str_need_string(str);
    rb_str_t *replacement_str = NULL;
    rb_encoding_t *src_encoding, *dst_encoding;
    transcode_behavior_t behavior_for_invalid = TRANSCODE_BEHAVIOR_RAISE_EXCEPTION;
    transcode_behavior_t behavior_for_undefined = TRANSCODE_BEHAVIOR_RAISE_EXCEPTION;
    if (argc == 0) {
	src_encoding = self->encoding;
	dst_encoding = default_internal;
	behavior_for_invalid = TRANSCODE_BEHAVIOR_REPLACE_WITH_STRING;
	behavior_for_undefined = TRANSCODE_BEHAVIOR_REPLACE_WITH_STRING;
    }
    else if (argc == 1) {
	src_encoding = self->encoding;
	dst_encoding = rb_to_encoding(argv[0]);
    }
    else if (argc == 2) {
	dst_encoding = rb_to_encoding(argv[0]);
	src_encoding = rb_to_encoding(argv[1]);
    }
    else {
	rb_raise(rb_eArgError, "wrong number of arguments (%d for 0..2)", argc);
    }

    if (!NIL_P(opt)) {
        parse_conversion_options(opt, &behavior_for_invalid, &behavior_for_undefined, &replacement_str, dst_encoding);
	    if ((replacement_str != NULL) 
	        && (behavior_for_invalid != TRANSCODE_BEHAVIOR_REPLACE_WITH_STRING)
		    && (behavior_for_undefined == TRANSCODE_BEHAVIOR_RAISE_EXCEPTION)) {
		behavior_for_undefined = TRANSCODE_BEHAVIOR_REPLACE_WITH_STRING;
	    }
	}

    if ((replacement_str == NULL)
	    && ((behavior_for_invalid == TRANSCODE_BEHAVIOR_REPLACE_WITH_STRING)
		|| (behavior_for_undefined == TRANSCODE_BEHAVIOR_REPLACE_WITH_STRING))) {
	replacement_str = replacement_string_for_encoding(dst_encoding);
    }

    return (VALUE)str_transcode(self, src_encoding, dst_encoding,
	    behavior_for_invalid, behavior_for_undefined, replacement_str);
}

/*
 *  call-seq:
 *     str.encode!(encoding [, options] )   => str
 *     str.encode!(dst_encoding, src_encoding [, options] )   => str
 *
 *  The first form transcodes the contents of <i>str</i> from
 *  str.encoding to +encoding+.
 *  The second form transcodes the contents of <i>str</i> from
 *  src_encoding to dst_encoding.
 *  The options Hash gives details for conversion. See String#encode
 *  for details.
 *  Returns the string even if no changes were made.
 */
static VALUE
rstr_encode_bang(VALUE str, SEL sel, int argc, VALUE *argv)
{
    rstr_modify(str);

    VALUE new_str = rstr_encode(str, sel, argc, argv);
    str_replace_with_string(RSTR(str), RSTR(new_str));
    return str;
}

void
Init_Transcode(void)
{
    // #encode works on both NSStrings and RubyStrings, #encode! only works
    // on RubyStrings.
    rb_objc_define_method(rb_cNSString, "encode", rstr_encode, -1);
    rb_objc_define_method(rb_cRubyString, "encode!", rstr_encode_bang, -1);
    rb_objc_define_method(rb_cNSString, "encode!", rstr_only, -1);

    rb_cEncodingConverter = rb_define_class_under(rb_cEncoding, "Converter", rb_cObject);
    rb_objc_define_method(*(VALUE *)rb_cEncodingConverter, "alloc", rb_econv_alloc, 0);
    rb_objc_define_method(*(VALUE *)rb_cEncodingConverter, "asciicompat_encoding", rb_econv_asciicompat_encoding, 1);
    rb_objc_define_method(*(VALUE *)rb_cEncodingConverter, "search_convpath", rb_econv_search_convpath, -1);
    
    rb_objc_define_method(rb_cEncodingConverter, "initialize", rb_econv_initialize, -1);
    rb_objc_define_method(rb_cEncodingConverter, "inspect", rb_econv_inspect, 0);
    rb_objc_define_method(rb_cEncodingConverter, "convpath", rb_econv_convpath, 0);
    rb_objc_define_method(rb_cEncodingConverter, "source_encoding", rb_econv_source_encoding, 0);
    rb_objc_define_method(rb_cEncodingConverter, "destination_encoding", rb_econv_destination_encoding, 0);
    rb_objc_define_method(rb_cEncodingConverter, "primitive_convert", rb_econv_primitive_convert, -1);
    rb_objc_define_method(rb_cEncodingConverter, "convert", rb_econv_convert, 1);
    rb_objc_define_method(rb_cEncodingConverter, "finish", rb_econv_finish, 0);
    rb_objc_define_method(rb_cEncodingConverter, "primitive_errinfo", rb_econv_primitive_errinfo, 0);
    rb_objc_define_method(rb_cEncodingConverter, "insert_output", rb_econv_insert_output, 1);
    rb_objc_define_method(rb_cEncodingConverter, "putback", rb_econv_putback, -1);
    rb_objc_define_method(rb_cEncodingConverter, "last_error", rb_econv_last_error, 0);
    rb_objc_define_method(rb_cEncodingConverter, "replacement", rb_econv_replacement, 0);
    rb_objc_define_method(rb_cEncodingConverter, "replacement=", rb_econv_set_replacement, 1);
    
    sym_invalid = ID2SYM(rb_intern("invalid"));
    sym_undef = ID2SYM(rb_intern("undef"));
    sym_replace = ID2SYM(rb_intern("replace"));
    sym_attr = ID2SYM(rb_intern("attr"));
    sym_text = ID2SYM(rb_intern("text"));
    sym_xml = ID2SYM(rb_intern("xml"));
    
    // If only these mapped to the internal enums...
    rb_define_const(rb_cEncodingConverter, "INVALID_MASK", INT2FIX(ECONV_INVALID_MASK));
    rb_define_const(rb_cEncodingConverter, "INVALID_REPLACE", INT2FIX(ECONV_INVALID_REPLACE));
    rb_define_const(rb_cEncodingConverter, "UNDEF_MASK", INT2FIX(ECONV_UNDEF_MASK));
    rb_define_const(rb_cEncodingConverter, "UNDEF_REPLACE", INT2FIX(ECONV_UNDEF_REPLACE));
    rb_define_const(rb_cEncodingConverter, "UNDEF_HEX_CHARREF", INT2FIX(ECONV_UNDEF_HEX_CHARREF));
    rb_define_const(rb_cEncodingConverter, "PARTIAL_INPUT", INT2FIX(ECONV_PARTIAL_INPUT));
    rb_define_const(rb_cEncodingConverter, "AFTER_OUTPUT", INT2FIX(ECONV_AFTER_OUTPUT));
    rb_define_const(rb_cEncodingConverter, "UNIVERSAL_NEWLINE_DECORATOR", INT2FIX(ECONV_UNIVERSAL_NEWLINE_DECORATOR));
    rb_define_const(rb_cEncodingConverter, "CRLF_NEWLINE_DECORATOR", INT2FIX(ECONV_CRLF_NEWLINE_DECORATOR));
    rb_define_const(rb_cEncodingConverter, "CR_NEWLINE_DECORATOR", INT2FIX(ECONV_CR_NEWLINE_DECORATOR));
    rb_define_const(rb_cEncodingConverter, "XML_TEXT_DECORATOR", INT2FIX(ECONV_XML_TEXT_DECORATOR));
    rb_define_const(rb_cEncodingConverter, "XML_ATTR_CONTENT_DECORATOR", INT2FIX(ECONV_XML_ATTR_CONTENT_DECORATOR));
    rb_define_const(rb_cEncodingConverter, "XML_ATTR_QUOTE_DECORATOR", INT2FIX(ECONV_XML_ATTR_QUOTE_DECORATOR));

#if 0
    rb_define_method(rb_eUndefinedConversionError, "source_encoding_name", ecerr_source_encoding_name, 0);
    rb_define_method(rb_eUndefinedConversionError, "destination_encoding_name", ecerr_destination_encoding_name, 0);
    rb_define_method(rb_eUndefinedConversionError, "source_encoding", ecerr_source_encoding, 0);
    rb_define_method(rb_eUndefinedConversionError, "destination_encoding", ecerr_destination_encoding, 0);
    rb_define_method(rb_eUndefinedConversionError, "error_char", ecerr_error_char, 0);

    rb_define_method(rb_eInvalidByteSequenceError, "source_encoding_name", ecerr_source_encoding_name, 0);
    rb_define_method(rb_eInvalidByteSequenceError, "destination_encoding_name", ecerr_destination_encoding_name, 0);
    rb_define_method(rb_eInvalidByteSequenceError, "source_encoding", ecerr_source_encoding, 0);
    rb_define_method(rb_eInvalidByteSequenceError, "destination_encoding", ecerr_destination_encoding, 0);
    rb_define_method(rb_eInvalidByteSequenceError, "error_bytes", ecerr_error_bytes, 0);
    rb_define_method(rb_eInvalidByteSequenceError, "readagain_bytes", ecerr_readagain_bytes, 0);
    rb_define_method(rb_eInvalidByteSequenceError, "incomplete_input?", ecerr_incomplete_input, 0);

    Init_newline();
#endif
}
