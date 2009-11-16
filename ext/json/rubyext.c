/* 
 * MacRuby JSON API.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 * 
 * Copyright (C) 2009, Satoshi Nakagawa. All rights reserved.
 */

#include "ruby/ruby.h"
#include "ruby/intern.h"
#include "ruby/node.h"
#include "ruby/io.h"
#include "objc.h"
#include "id.h"
#include "vm.h"
#include "api/yajl_parse.h"
#include "api/yajl_gen.h"

static VALUE rb_mJSON;
static VALUE rb_cParser;
static VALUE rb_cEncoder;
static VALUE rb_cParseError;
static VALUE rb_cEncodeError;

typedef struct rb_json_parser_s {
    struct RBasic basic;
    yajl_handle parser;
    int nestedArrayLevel;
    int nestedHashLevel;
    int objectsFound;
    bool symbolizeKeys;
    VALUE builderStack;
} rb_json_parser_t;

#define RJSONParser(val) ((rb_json_parser_t*)val)

typedef struct rb_json_generator_s {
    struct RBasic basic;
    yajl_gen generator;
} rb_json_generator_t;

#define RJSONGenerator(val) ((rb_json_generator_t*)val)

static IMP rb_json_parser_finalize_super = NULL; 
static IMP rb_json_encoder_finalize_super = NULL; 

static void json_parse_chunk(const unsigned char* chunk, unsigned int len, yajl_handle parser);
static void json_encode_part(void* ctx, VALUE obj);

static int yajl_handle_null(void* ctx);
static int yajl_handle_boolean(void* ctx, int value);
static int yajl_handle_number(void* ctx, const char* value, unsigned int len);
static int yajl_handle_string(void* ctx, const unsigned char* value, unsigned int len);
static int yajl_handle_hash_key(void* ctx, const unsigned char* value, unsigned int len);
static int yajl_handle_start_hash(void* ctx);
static int yajl_handle_end_hash(void* ctx);
static int yajl_handle_start_array(void* ctx);
static int yajl_handle_end_array(void* ctx);

static yajl_callbacks callbacks = {
    yajl_handle_null,
    yajl_handle_boolean,
    NULL,
    NULL,
    yajl_handle_number,
    yajl_handle_string,
    yajl_handle_start_hash,
    yajl_handle_hash_key,
    yajl_handle_end_hash,
    yajl_handle_start_array,
    yajl_handle_end_array
};

static ID id_parse;
static ID id_encode;
static ID id_keys;
static ID id_to_s;
static ID id_to_json;
static ID id_allow_comments;
static ID id_check_utf8;
static ID id_pretty;
static ID id_indent;
static ID id_symbolize_keys;

static SEL sel_parse;
static SEL sel_encode;
static SEL sel_keys;
static SEL sel_to_s;
static SEL sel_to_json;

static struct mcache* parse_cache = NULL;
static struct mcache* encode_cache = NULL;
static struct mcache* keys_cache = NULL;
static struct mcache* to_s_cache = NULL;
static struct mcache* to_json_cache = NULL;


static VALUE
rb_json_parser_alloc(VALUE klass, SEL sel)
{
    NEWOBJ(parser, struct rb_json_parser_s);
    OBJSETUP(parser, klass, T_OBJECT);
    return (VALUE)parser;
}

static VALUE
rb_json_parser_initialize(VALUE self, SEL sel, int argc, VALUE* argv)
{
    yajl_parser_config config;
    VALUE opts;
    int allowComments = 1, checkUTF8 = 1, symbolizeKeys = 0;
    
    if (rb_scan_args(argc, argv, "01", &opts) == 1) {
        Check_Type(opts, T_HASH);
        
        if (rb_hash_aref(opts, ID2SYM(id_allow_comments)) == Qfalse) {
            allowComments = 0;
        }
        if (rb_hash_aref(opts, ID2SYM(id_check_utf8)) == Qfalse) {
            checkUTF8 = 0;
        }
        if (rb_hash_aref(opts, ID2SYM(id_symbolize_keys)) == Qtrue) {
            symbolizeKeys = 1;
        }
    }
    
    config = (yajl_parser_config){allowComments, checkUTF8};
    
    rb_json_parser_t* parser = RJSONParser(self);
    parser->parser = yajl_alloc(&callbacks, &config, NULL, (void*)self);
    parser->nestedArrayLevel = 0;
    parser->nestedHashLevel = 0;
    parser->objectsFound = 0;
    parser->symbolizeKeys = symbolizeKeys;
    GC_WB(&parser->builderStack, rb_ary_new());
    return self;
}

static VALUE
rb_json_parser_parse(VALUE self, SEL sel, VALUE input)
{
    yajl_status status;
    rb_json_parser_t* parser = RJSONParser(self);
    
    if (TYPE(input) == T_STRING) {
        const unsigned char* cptr = (const unsigned char*)RSTRING_PTR(input);
        json_parse_chunk(cptr, (unsigned int)strlen((char*)cptr), parser->parser);
    }
    else {
        rb_raise(rb_cParseError, "input must be a string");
    }
    
    status = yajl_parse_complete(parser->parser);

    return rb_ary_pop(parser->builderStack);
}

static void
rb_json_parser_finalize(void* rcv, SEL sel)
{
    // TODO: is this reentrant?
    rb_json_parser_t* parser = RJSONParser(rcv);
    yajl_free(parser->parser);

    if (rb_json_parser_finalize_super) {
        ((void(*)(void*, SEL))rb_json_parser_finalize_super)(rcv, sel);
    }
}


static VALUE
rb_json_encoder_alloc(VALUE klass, SEL sel)
{
    NEWOBJ(generator, struct rb_json_generator_s);
    OBJSETUP(generator, klass, T_OBJECT);
    return (VALUE)generator;
}

static VALUE
rb_json_encoder_initialize(VALUE self, SEL sel, int argc, VALUE* argv)
{
    yajl_gen_config config;
    VALUE opts, indent;
    const char* indentString = "  ";
    int beautify = 0;
    
    if (rb_scan_args(argc, argv, "01", &opts) == 1) {
        Check_Type(opts, T_HASH);
        
        if (rb_hash_aref(opts, ID2SYM(id_pretty)) == Qtrue) {
            beautify = 1;
            indent = rb_hash_aref(opts, ID2SYM(id_indent));
            if (indent != Qnil) {
                Check_Type(indent, T_STRING);
                indentString = RSTRING_PTR(indent);
            }
        }
    }
    
    config = (yajl_gen_config){beautify, indentString};
    
    rb_json_generator_t* gen = RJSONGenerator(self);
    gen->generator = yajl_gen_alloc(&config, NULL);
    return self;
}

static VALUE
rb_json_encoder_encode(VALUE self, SEL sel, VALUE obj)
{
    const unsigned char* buffer;
    unsigned int len;
    rb_json_generator_t* gen = RJSONGenerator(self);
    
    json_encode_part(gen, obj);
    yajl_gen_get_buf(gen->generator, &buffer, &len);
    
    VALUE resultBuf = (VALUE)CFStringCreateWithBytes(NULL, (const UInt8*)buffer, len, kCFStringEncodingUTF8, false);
    CFMakeCollectable((CFTypeRef)resultBuf);
    yajl_gen_clear(gen->generator);
    
    return resultBuf;
}

static void
rb_json_encoder_finalize(void* rcv, SEL sel)
{
    // TODO: is this reentrant?
    rb_json_generator_t* generator = RJSONGenerator(rcv);
    yajl_gen_free(generator->generator);

    if (rb_json_encoder_finalize_super) {
        ((void(*)(void*, SEL))rb_json_encoder_finalize_super)(rcv, sel);
    }
}


static void
json_parse_chunk(const unsigned char* chunk, unsigned int len, yajl_handle parser)
{
    yajl_status status = yajl_parse(parser, chunk, len);
    
    if (status != yajl_status_ok && status != yajl_status_insufficient_data) {
        unsigned char* str = yajl_get_error(parser, 1, chunk, len);
        rb_raise(rb_cParseError, "%s", (const char*) str);
        yajl_free_error(parser, str);
    }
}

static inline void
yajl_set_static_value(void* ctx, VALUE val)
{
    VALUE lastEntry, hash;
    int len;
    rb_json_parser_t* parser = RJSONParser(ctx);
    
    len = RARRAY_LEN(parser->builderStack);
    if (len > 0) {
        lastEntry = rb_ary_entry(parser->builderStack, len-1);
        switch (TYPE(lastEntry)) {
            case T_ARRAY:
                rb_ary_push(lastEntry, val);
                if (TYPE(val) == T_HASH || TYPE(val) == T_ARRAY) {
                    rb_ary_push(parser->builderStack, val);
                }
                break;
            case T_HASH:
                rb_hash_aset(lastEntry, val, Qnil);
                rb_ary_push(parser->builderStack, val);
                break;
            case T_STRING:
            case T_SYMBOL:
                hash = rb_ary_entry(parser->builderStack, len-2);
                if (TYPE(hash) == T_HASH) {
                    rb_hash_aset(hash, lastEntry, val);
                    rb_ary_pop(parser->builderStack);
                    if (TYPE(val) == T_HASH || TYPE(val) == T_ARRAY) {
                        rb_ary_push(parser->builderStack, val);
                    }
                }
                break;
        }
    }
    else {
        rb_ary_push(parser->builderStack, val);
    }
}

static int
yajl_handle_null(void* ctx)
{
    yajl_set_static_value(ctx, Qnil);
    return 1;
}

static int
yajl_handle_boolean(void* ctx, int value)
{
    yajl_set_static_value(ctx, value ? Qtrue : Qfalse);
    return 1;
}

static int
yajl_handle_number(void* ctx, const char* value, unsigned int len)
{
    char buf[len+1];
    memcpy(buf, value, len);
    buf[len] = 0;
    
    if (strchr(buf, '.') || strchr(buf, 'e') || strchr(buf, 'E')) {
        yajl_set_static_value(ctx, rb_float_new(strtod(buf, NULL)));
    }
    else {
        yajl_set_static_value(ctx, rb_cstr2inum(buf, 10));
    }
    return 1;
}

static int
yajl_handle_string(void* ctx, const unsigned char* value, unsigned int len)
{
    VALUE str = (VALUE)CFStringCreateWithBytes(NULL, (const UInt8*)value, len, kCFStringEncodingUTF8, false);
    CFMakeCollectable((CFTypeRef)str);
    yajl_set_static_value(ctx, str);
    return 1;
}

static int
yajl_handle_hash_key(void* ctx, const unsigned char* value, unsigned int len)
{
    rb_json_parser_t* parser = RJSONParser(ctx);
    
    VALUE keyStr = (VALUE)CFStringCreateWithBytes(NULL, (const UInt8*)value, len, kCFStringEncodingUTF8, false);
    CFMakeCollectable((CFTypeRef)keyStr);
    
    if (parser->symbolizeKeys) {
        ID key = rb_intern(RSTRING_PTR(keyStr));
        yajl_set_static_value(ctx, ID2SYM(key));
    }
    else {
        yajl_set_static_value(ctx, keyStr);
    }
    return 1;
}

static int
yajl_handle_start_hash(void* ctx)
{
    rb_json_parser_t* parser = RJSONParser(ctx);
    parser->nestedHashLevel++;
    yajl_set_static_value(ctx, rb_hash_new());
    return 1;
}

static int
yajl_handle_end_hash(void* ctx)
{
    rb_json_parser_t* parser = RJSONParser(ctx);
    parser->nestedHashLevel--;
    if (RARRAY_LEN(parser->builderStack) > 1) {
        rb_ary_pop(parser->builderStack);
    }
    return 1;
}

static int
yajl_handle_start_array(void* ctx)
{
    rb_json_parser_t* parser = RJSONParser(ctx);
    parser->nestedArrayLevel++;
    yajl_set_static_value(ctx, rb_ary_new());
    return 1;
}

static int
yajl_handle_end_array(void* ctx)
{
    rb_json_parser_t* parser = RJSONParser(ctx);
    parser->nestedArrayLevel--;
    if (RARRAY_LEN(parser->builderStack) > 1) {
        rb_ary_pop(parser->builderStack);
    }
    return 1;
}

static void
json_encode_part(void* ctx, VALUE obj)
{
    VALUE str, keys, entry, keyStr;
    yajl_gen_status status;
    const char* cptr;
    int i, len;
    int quote_strings = 1;
    rb_json_generator_t* gen = RJSONGenerator(ctx);
    
    switch (TYPE(obj)) {
        case T_HASH:
            status = yajl_gen_map_open(gen->generator);
            keys = rb_vm_call_with_cache(keys_cache, obj, sel_keys, 0, 0);
            for(i = 0, len = RARRAY_LEN(keys); i < len; i++) {
                entry = rb_ary_entry(keys, i);
                keyStr = rb_vm_call_with_cache(to_s_cache, entry, sel_to_s, 0, 0);
                json_encode_part(gen, keyStr);
                json_encode_part(gen, rb_hash_aref(obj, entry));
            }
            status = yajl_gen_map_close(gen->generator);
            break;
        case T_ARRAY:
            status = yajl_gen_array_open(gen->generator);
            for(i = 0, len = RARRAY_LEN(obj); i < len; i++) {
                entry = rb_ary_entry(obj, i);
                json_encode_part(gen, entry);
            }
            status = yajl_gen_array_close(gen->generator);
            break;
        case T_NIL:
            status = yajl_gen_null(gen->generator);
            break;
        case T_TRUE:
            status = yajl_gen_bool(gen->generator, 1);
            break;
        case T_FALSE:
            status = yajl_gen_bool(gen->generator, 0);
            break;
        case T_FIXNUM:
        case T_FLOAT:
        case T_BIGNUM:
            str = rb_vm_call_with_cache(to_s_cache, obj, sel_to_s, 0, 0);
            cptr = RSTRING_PTR(str);
            if (!strcmp(cptr, "NaN") || !strcmp(cptr, "Infinity") || !strcmp(cptr, "-Infinity")) {
                rb_raise(rb_cEncodeError, "'%s' is an invalid number", cptr);
            }
            status = yajl_gen_number(gen->generator, cptr, (unsigned int)strlen(cptr));
            break;
        case T_STRING:
            cptr = RSTRING_PTR(obj);
            status = yajl_gen_string(gen->generator, (const unsigned char*)cptr, (unsigned int)strlen(cptr), 1);
            break;
        default:
            if (rb_respond_to(obj, id_to_json)) {
                str = rb_vm_call_with_cache(to_json_cache, obj, sel_to_json, 0, 0);
                quote_strings = 0;
            }
            else {
                str = rb_vm_call_with_cache(to_s_cache, obj, sel_to_s, 0, 0);
            }
            cptr = RSTRING_PTR(str);
            status = yajl_gen_string(gen->generator, (const unsigned char*)cptr, (unsigned int)strlen(cptr), quote_strings);
            break;
    }
}


static VALUE
rb_json_parse(VALUE self, SEL sel, int argc, VALUE* argv)
{
    VALUE parser, str, opts;
    
    rb_scan_args(argc, argv, "11", &str, &opts);
    
    parser = rb_json_parser_alloc(rb_cParser, nil);
    if (opts == Qnil) {
        rb_obj_call_init(parser, 0, 0);
    }
    else {
        rb_obj_call_init(parser, 1, &opts);
    }
    
    return rb_vm_call_with_cache(parse_cache, parser, sel_parse, 1, &str);
}

static VALUE
rb_json_generate(VALUE self, SEL sel, int argc, VALUE* argv)
{
    VALUE generator, obj, opts;
    
    rb_scan_args(argc, argv, "11", &obj, &opts);
    
    generator = rb_json_encoder_alloc(rb_cEncoder, nil);
    if (opts == Qnil) {
        rb_obj_call_init(generator, 0, 0);
    }
    else {
        rb_obj_call_init(generator, 1, &opts);
    }
    
    return rb_vm_call_with_cache(encode_cache, generator, sel_encode, 1, &obj);
}

static VALUE
rb_kernel_json(VALUE self, SEL sel, int argc, VALUE* argv)
{
    VALUE obj, opts;
    
    rb_scan_args(argc, argv, "11", &obj, &opts);
    
    if (TYPE(obj) == T_STRING) {
        return rb_json_parse(rb_mJSON, nil, argc, argv);
    }
    else {
        return rb_json_generate(rb_mJSON, nil, argc, argv);
    }
}

static VALUE
rb_to_json(VALUE self, SEL sel, int argc, VALUE* argv)
{
    VALUE generator;
    
    rb_scan_args(argc, argv, "01", &generator);
    
    if (generator == Qnil) {
        generator = rb_json_encoder_alloc(rb_cEncoder, nil);
        rb_obj_call_init(generator, 0, 0);
    }
    
    return rb_vm_call_with_cache(encode_cache, generator, sel_encode, 1, &self);
}

static VALUE
rb_object_to_json(VALUE self, SEL sel, int argc, VALUE* argv)
{
    VALUE buf, str;
    
    str = rb_vm_call_with_cache(to_s_cache, self, sel_to_s, 0, 0);
    
    buf = (VALUE)CFStringCreateMutable(NULL, 0);
    CFMakeCollectable((CFTypeRef)buf);
    CFStringAppendCString((CFMutableStringRef)buf, "\"", kCFStringEncodingUTF8);
    CFStringAppend((CFMutableStringRef)buf, (CFStringRef)str);
    CFStringAppendCString((CFMutableStringRef)buf, "\"", kCFStringEncodingUTF8);
    return buf;
}


void Init_json()
{
    id_parse = rb_intern("parse");
    id_encode = rb_intern("encode");
    id_keys = rb_intern("keys");
    id_to_s = rb_intern("to_s");
    id_to_json = rb_intern("to_json");
    id_allow_comments = rb_intern("allow_comments");
    id_check_utf8 = rb_intern("check_utf8");
    id_pretty = rb_intern("pretty");
    id_indent = rb_intern("indent");
    id_symbolize_keys = rb_intern("symbolize_keys");

    sel_parse = sel_registerName("parse:");
    sel_encode = sel_registerName("encode:");
    sel_keys = sel_registerName("keys");
    sel_to_s = sel_registerName("to_s");
    sel_to_json = sel_registerName("to_json");

    parse_cache = rb_vm_get_call_cache(sel_parse);
    encode_cache = rb_vm_get_call_cache(sel_encode);
    keys_cache = rb_vm_get_call_cache(sel_keys);
    to_s_cache = rb_vm_get_call_cache(sel_to_s);
    to_json_cache = rb_vm_get_call_cache(sel_to_json);
    
    rb_mJSON = rb_define_module("JSON");
    
    rb_cParseError = rb_define_class_under(rb_mJSON, "ParseError", rb_eStandardError);
    rb_cEncodeError = rb_define_class_under(rb_mJSON, "EncodeError", rb_eStandardError);
    
    rb_cParser = rb_define_class_under(rb_mJSON, "Parser", rb_cObject);
    rb_objc_define_method(*(VALUE*)rb_cParser, "alloc", rb_json_parser_alloc, 0);
    rb_objc_define_method(rb_cParser, "initialize", rb_json_parser_initialize, -1);
    rb_objc_define_method(rb_cParser, "parse", rb_json_parser_parse, 1);
    rb_json_parser_finalize_super = rb_objc_install_method2((Class)rb_cParser, "finalize", (IMP)rb_json_parser_finalize);
    
    rb_cEncoder = rb_define_class_under(rb_mJSON, "Encoder", rb_cObject);
    rb_objc_define_method(*(VALUE*)rb_cEncoder, "alloc", rb_json_encoder_alloc, 0);
    rb_objc_define_method(rb_cEncoder, "initialize", rb_json_encoder_initialize, -1);
    rb_objc_define_method(rb_cEncoder, "encode", rb_json_encoder_encode, 1);
    rb_json_encoder_finalize_super = rb_objc_install_method2((Class)rb_cEncoder, "finalize", (IMP)rb_json_encoder_finalize);

    rb_objc_define_module_function(rb_mJSON, "parse", rb_json_parse, -1);
    rb_objc_define_module_function(rb_mJSON, "generate", rb_json_generate, -1);
    rb_objc_define_module_function(rb_mKernel, "JSON", rb_kernel_json, -1);
    
    rb_objc_define_method(rb_cArray, "to_json", rb_to_json, -1);
    rb_objc_define_method(rb_cHash, "to_json", rb_to_json, -1);
    rb_objc_define_method(rb_cNumeric, "to_json", rb_to_json, -1);
    rb_objc_define_method(rb_cString, "to_json", rb_to_json, -1);
    rb_objc_define_method(rb_cTrueClass, "to_json", rb_to_json, -1);
    rb_objc_define_method(rb_cFalseClass, "to_json", rb_to_json, -1);
    rb_objc_define_method(rb_cNilClass, "to_json", rb_to_json, -1);
    rb_objc_define_method(rb_cObject, "to_json", rb_object_to_json, -1);
}
