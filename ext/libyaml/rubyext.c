/*
 * MacRuby libYAML API.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2009-2010, Apple Inc. All rights reserved.
 */

#include "macruby_internal.h"
#include "ruby/intern.h"
#include "ruby/node.h"
#include "ruby/io.h"
#include "objc.h"
#include "vm.h"
#include "yaml.h"
#include <unistd.h>

typedef struct rb_yaml_parser_s {
    struct RBasic basic;	// holds the class information
    yaml_parser_t parser;	// the parser object

    VALUE input;		// a reference to the object that's providing
				// input

    VALUE resolver;		// used to determine how to unserialize objects

    yaml_event_t event;		// the event that is currently being parsed.
    bool event_valid;		// is this event valid?
} rb_yaml_parser_t;

#define RYAMLParser(val) ((rb_yaml_parser_t*)val)

typedef struct rb_yaml_emitter_s {
    struct RBasic basic;	// holds the class information
    yaml_emitter_t emitter;	// the emitter object

    VALUE output;		// the object to which we are writing
} rb_yaml_emitter_t;

#define RYAMLEmitter(val) ((rb_yaml_emitter_t*)val)

typedef struct rb_yaml_resolver_s {
    struct RBasic basic;
    CFMutableDictionaryRef tags;
} rb_yaml_resolver_t;

#define RYAMLResolver(val) ((rb_yaml_resolver_t*)val)

static VALUE rb_mYAML;
static VALUE rb_mLibYAML;
static VALUE rb_cParser;
static VALUE rb_cEmitter;
static VALUE rb_cResolver;
static VALUE rb_cYamlNode;

static ID id_plain;
static ID id_quote2;
static ID id_inline;

static SEL sel_to_yaml;
static SEL sel_call;
static SEL sel_yaml_new;

static VALUE rb_oDefaultResolver;

static const int DEFAULT_STACK_SIZE = 8;

static VALUE
rb_yaml_parser_alloc(VALUE klass, SEL sel)
{
    NEWOBJ(parser, struct rb_yaml_parser_s);
    OBJSETUP(parser, klass, T_OBJECT);

    GC_WB(&parser->resolver, rb_oDefaultResolver);
    parser->event_valid = false;

    yaml_parser_initialize(&parser->parser);
    return (VALUE)parser;
}

static int
rb_yaml_io_read_handler(void *io_ptr, unsigned char *buffer, size_t size,
	size_t *size_read)
{
    const long result = rb_io_primitive_read(ExtractIOStruct(io_ptr),
	    (char *)buffer, size);
    *size_read = result;
    return result != -1;
}

static VALUE
rb_yaml_parser_input(VALUE self, SEL sel)
{
    return RYAMLParser(self)->input;
}

static VALUE
rb_yaml_parser_set_input(VALUE self, SEL sel, VALUE input)
{
    rb_yaml_parser_t *rbparser = RYAMLParser(self);
    yaml_parser_t *parser = &rbparser->parser;

    if (!NIL_P(input)) {
	assert(parser != NULL);
	if (TYPE(input) == T_STRING) {
	    const char * instring = RSTRING_PTR(input);
	    yaml_parser_set_input_string(parser,
		    (const unsigned char *)(instring),
		    strlen(instring));			
	}
	else if (TYPE(input) == T_FILE) {
	    yaml_parser_set_input(parser, rb_yaml_io_read_handler,
		    (void *)input);
	}
	else {
	    rb_raise(rb_eArgError, "invalid input for YAML parser: %s",
		    rb_obj_classname(input));
	}
    }

    GC_WB(&rbparser->input, input);
    return input;
}

static VALUE
rb_yaml_parser_initialize(VALUE self, SEL sel, int argc, VALUE *argv)
{
    VALUE input = Qnil;
    rb_scan_args(argc, argv, "01", &input);
    rb_yaml_parser_set_input(self, 0, input);
    return self;
}

static VALUE
rb_yaml_parser_generate_error(yaml_parser_t *parser)
{
    assert(parser != NULL);

    VALUE error = Qnil;
    const char *descriptor;
    switch(parser->error) {
	case YAML_NO_ERROR:
	    return Qnil;

	case YAML_SCANNER_ERROR:
	    descriptor = "scanning";
	    break;

	case YAML_PARSER_ERROR:
	    descriptor = "parsing";
	    break;

	case YAML_MEMORY_ERROR:
	    descriptor = "memory allocation";
	    break;

	case YAML_READER_ERROR:
	    descriptor = "reading";
	    break;

	default:
	    descriptor = "unknown";
	    break;
    }

    char *msg = NULL;
    if (parser->problem != NULL) {
	if (parser->context != NULL) {
	    asprintf(&msg,
		    "%s error encountered during parsing: %s (line %ld, column %ld), context %s (line %ld, column %ld)",
		    descriptor, parser->problem, parser->problem_mark.line,
		    parser->problem_mark.column, parser->context,
		    parser->context_mark.line, parser->context_mark.column);
	}
	else {
	    asprintf(&msg,
		    "%s error encountered during parsing: %s (line %ld, column %ld)",
		    descriptor, parser->problem, parser->problem_mark.line,
		    parser->problem_mark.column);
	}
    }
    else {
	asprintf(&msg, "%s error encountered during parsing", descriptor);
    }

    error = rb_exc_new2(rb_eArgError, msg);
    if (msg != NULL) {
	free(msg);
    }
    return error;
}

static VALUE
rb_yaml_parser_error(VALUE self, SEL sel)
{
    return rb_yaml_parser_generate_error(&(RYAMLParser(self)->parser));
}

static bool
yaml_next_event(rb_yaml_parser_t *parser)
{
    if (NIL_P(parser->input)) {
	rb_raise(rb_eRuntimeError, "input needed");
    }
    if (parser->event_valid) {
	yaml_event_delete(&parser->event);
	parser->event_valid = false;
    }
    if (yaml_parser_parse(&parser->parser, &parser->event) == 0) {
	rb_exc_raise(rb_yaml_parser_generate_error(&parser->parser));
	parser->event_valid = false;
    }
    else {
	parser->event_valid = true;
    }
    return parser->event_valid;
}

#define NEXT_EVENT() yaml_next_event(parser)
static VALUE
get_node(rb_yaml_parser_t *parser);
static VALUE parse_node(rb_yaml_parser_t *parser);

static VALUE
handler_for_tag(rb_yaml_parser_t *parser, yaml_char_t *tag)
{
    if (tag == NULL) {
	return Qnil;
    }

    const void *h =
	CFDictionaryGetValue(RYAMLResolver(parser->resolver)->tags,
		(const void *)rb_intern((const char *)tag));

    if (h != NULL) {
	return (VALUE)h;
    }

    // Dynamic handler, only for pure objects.
    VALUE outer = Qnil;
    if (strncmp((const char *)tag, "!ruby/object:", 13) == 0) {
	outer = rb_cObject;
    }
    else if (strncmp((const char *)tag, "!ruby/struct:", 13) == 0) {
	outer = rb_cStruct;
    }
    if (outer != Qnil) {
	char *path = (char *)tag + 13;
	assert(strlen(path) < 100);

	char *p;
	while ((p = strstr(path, "::")) != NULL) {	    
	    char buf[100];
	    strncpy(buf, path, p - path);
	    buf[p - path] = '\0';
	    ID id = rb_intern(buf);
	    if (rb_const_defined(outer, id)) {
		outer = rb_const_get(outer, id);
	    }
	    else {
		return Qnil;
	    }
	    path = p + 2;
	}

	ID pathid = rb_intern(path);
	if (rb_const_defined(outer, pathid)) {
	    return rb_const_get(outer, pathid);
	}
    }
    return Qnil;
}

static VALUE
interpret_value(rb_yaml_parser_t *parser, VALUE result, VALUE handler)
{
    if (NIL_P(handler)) {
	return result;
    }
    if (rb_vm_respond_to(handler, sel_call, 0)) {
	return rb_vm_call(handler, sel_call, 1, &result);
    }
    else if (rb_vm_respond_to(handler, sel_yaml_new, 0)) {
	return rb_vm_call(handler, sel_yaml_new, 1, &result);
    }
    return result;
}

static bool
is_numeric(const char *str, bool *has_point)
{
    if (*str == '-') {
	str++;
    }
    char c;
    bool point = false;
    bool numeric = false;
    while ((c = *str++) != '\0') {
	if (!isdigit(c)) {
	    if (c == '.') {
		if (point) {
		    return false;
		}
		point = true;
	    }
	    else {
		return false;
	    }
	}
	else if (!point) {
	    numeric = true;
	}
    }
    *has_point = point;
    return numeric;
}

static bool
is_timestamp(const char *str, size_t length)
{
  // TODO: This probably should be coded as a regex and/or in ruby
  bool canonical = true;
  if (length < 10) {
    return false;
  }
  /* 4 digit year - */
  if (!isdigit(str[0]) ||
      !isdigit(str[1]) ||
      !isdigit(str[2]) ||
      !isdigit(str[3]) ||
      str[4] != '-') {
    return false;
  }
  str += 5;
  /* 1/2 digit month - */
  if (!isdigit(*str++)) {
    return false;
  }
  if (isdigit(*str)) {
    ++str;
  }
  else {
    canonical = false;
  } 
  if (*str++ != '-') {
    return false;
  }
  /* 1/2 digit day */
  if (!isdigit(*str++)) {
    return false;
  }
  if (isdigit(*str)) {
    ++str;
  }
  else {
    canonical = false;
  } 
  /* Date alone must be YYYY-MM-DD */
  if (*str == '\0') {
    return canonical;
  }
  else if (*str == 't' || *str == 'T') {
    ++str;
  }
  else if (*str == ' ' || *str == '\t') {
    do {
      ++str;
    } while (*str == ' ' || *str == '\t');
  } 
  else {
    return false;
  }
  /* 1/2 digit hour : */
  if (!isdigit(*str++)) {
    return false;
  }
  if (isdigit(*str)) {
    ++str;
  }
  if (*str++ != ':') {
    return false;
  }
  /* 2 digit minute:second */
  if (!isdigit(str[0]) ||
      !isdigit(str[1]) ||
      (str[2] != ':')  ||
      !isdigit(str[3]) ||
      !isdigit(str[4])) {
    return false;
  }
  str += 5;
  /* Optional fraction */
  if (*str == '.') {
    do {
      ++str;
    } while (isdigit(*str));
  }
  if (*str == '\0') {
    return true; /* Assumed UTC */
  }
  while (*str == ' ' || *str == '\t') {
    ++str;
  }
  if (str[0] == 'Z' && str[1] == '\0') {
    return true; /* UTC */
  }
  else if (str[0] != '+' && str[0] != '-') {
    return false;
  }
  ++str;
  /* 1/2 digit time zone hour */
  if (!isdigit(*str++)) {
    return false;
  }
  if (isdigit(*str)) {
    ++str;
  }
  if (*str == '\0') {
    return true;
  }
  /* Optional minute */
  if ((str[0] != ':')  ||
      !isdigit(str[1]) ||
      !isdigit(str[2]) ||
      (str[3] != '\0')) {
    return false;
  }
  else {
    return true; 
  }
}

static char *
detect_scalar_type(const char * val, size_t length)
{
  bool has_point = false;
  if (length == 0) {
    return "tag:yaml.org,2002:null";
  }
  else if (*val == ':') {
    return "tag:ruby.yaml.org,2002:symbol";
  }
  else if (is_numeric(val, &has_point)) {
    return has_point
      ? "tag:yaml.org,2002:float"
      : "tag:yaml.org,2002:int";
  }
  else if (strcmp(val, "true") == 0) {
    return "tag:yaml.org,2002:true";
  }
  else if (strcmp(val, "false") == 0) {
    return "tag:yaml.org,2002:false";
  }
  else if (is_timestamp(val, length)) {
    return "tag:yaml.org,2002:timestamp";
  }
  else {
    return NULL;
  }
}

static VALUE 
handle_scalar(rb_yaml_parser_t *parser)
{
    char *val = (char*)parser->event.data.scalar.value;
    char *tag = (char*)parser->event.data.scalar.tag;
    if (parser->event.data.scalar.style == YAML_PLAIN_SCALAR_STYLE
	    && tag == NULL) {
	tag = detect_scalar_type(val, parser->event.data.scalar.length);
    }
    if (tag == NULL) {
	tag = "tag:yaml.org,2002:str";
    }
    VALUE handler = handler_for_tag(parser, (yaml_char_t *)tag);
    VALUE scalarval = rb_str_new(val, parser->event.data.scalar.length);
    return interpret_value(parser, scalarval, handler);
}

static VALUE
handle_sequence(rb_yaml_parser_t *parser)
{
    VALUE handler = handler_for_tag(parser,
	    parser->event.data.sequence_start.tag);
    VALUE arr = rb_ary_new();

    VALUE node;
    while ((node = get_node(parser)) != Qundef) {
	rb_ary_push(arr, node);
    }
    return interpret_value(parser, arr, handler);
}

static VALUE
handle_mapping(rb_yaml_parser_t *parser)
{
    VALUE handler = handler_for_tag(parser,
	    parser->event.data.mapping_start.tag);
    VALUE hash = rb_hash_new();

    VALUE key_node;
    while ((key_node = get_node(parser)) != Qundef) {
	VALUE value_node = get_node(parser);
	if (value_node == Qundef) {
	    value_node = Qnil;
	}
	rb_hash_aset(hash, key_node, value_node);
    }
    return interpret_value(parser, hash, handler);
}

static VALUE
get_node(rb_yaml_parser_t *parser)
{
    VALUE node;
    NEXT_EVENT();

    switch (parser->event.type) {
	case YAML_DOCUMENT_END_EVENT:
	case YAML_MAPPING_END_EVENT:
	case YAML_SEQUENCE_END_EVENT:
	case YAML_STREAM_END_EVENT:
	    return Qundef;

	case YAML_MAPPING_START_EVENT:
	    node = handle_mapping(parser);
	    break;

	case YAML_SEQUENCE_START_EVENT:
	    node = handle_sequence(parser);
	    break;

	case YAML_SCALAR_EVENT:
	    node = handle_scalar(parser);
	    break;

	case YAML_ALIAS_EVENT:
	    // ignoring alias
	    node = Qundef;
	    break;

	default:
	    rb_raise(rb_eArgError, "Invalid event %d at top level",
		    (int)parser->event.type);
    }
    return node;
}

static VALUE
rb_yaml_parser_load(VALUE self, SEL sel)
{
    rb_yaml_parser_t *parser = RYAMLParser(self);
    VALUE root = Qnil;

    NEXT_EVENT();
    if (parser->event.type == YAML_STREAM_END_EVENT) {
	return Qnil;
    }
    if (parser->event.type == YAML_STREAM_START_EVENT) {
	NEXT_EVENT();
    }
    if (parser->event.type != YAML_DOCUMENT_START_EVENT) {
	rb_raise(rb_eArgError, "expected DOCUMENT_START event");
    }

    root = get_node(parser);
    if (root == Qundef) {
	root = Qnil;
    }

    NEXT_EVENT();
    if (parser->event.type != YAML_DOCUMENT_END_EVENT) {
	rb_raise(rb_eArgError, "expected DOCUMENT_END event");
    }

    return root;
}

static VALUE 
make_yaml_node(char * tag, VALUE value)
{
    VALUE argv[2];

    argv[0] = rb_str_new2(tag);
    argv[1] = value;

    return rb_class_new_instance(2, argv, rb_cYamlNode);
}

static VALUE 
parse_scalar(rb_yaml_parser_t *parser)
{
    char *val = (char*)parser->event.data.scalar.value;
    char *tag = (char*)parser->event.data.scalar.tag;
    if (parser->event.data.scalar.style == YAML_PLAIN_SCALAR_STYLE
	    && tag == NULL) {
	tag = detect_scalar_type(val, parser->event.data.scalar.length);
    }
    if (tag == NULL) {
	tag = "str";
    }
    VALUE scalarval = rb_str_new(val, parser->event.data.scalar.length);
    return make_yaml_node(tag, scalarval);
}

static VALUE
parse_sequence(rb_yaml_parser_t *parser)
{
    VALUE arr = rb_ary_new();

    VALUE node;
    while ((node = parse_node(parser)) != Qundef) {
	rb_ary_push(arr, node);
    }
    return make_yaml_node("seq", arr);
}

static VALUE
parse_mapping(rb_yaml_parser_t *parser)
{
    VALUE hash = rb_hash_new();

    VALUE key_node;
    while ((key_node = parse_node(parser)) != Qundef) {
	VALUE value_node = parse_node(parser);
	if (value_node == Qundef) {
	    value_node = Qnil;
	}	
	rb_hash_aset(hash, key_node, value_node);
    }
    return make_yaml_node("map", hash);
}

static VALUE
parse_node(rb_yaml_parser_t *parser)
{
    VALUE node;
    NEXT_EVENT();

    switch (parser->event.type) {
	case YAML_DOCUMENT_END_EVENT:
	case YAML_MAPPING_END_EVENT:
	case YAML_SEQUENCE_END_EVENT:
	case YAML_STREAM_END_EVENT:
	    return Qundef;

	case YAML_MAPPING_START_EVENT:
	    node = parse_mapping(parser);
	    break;

	case YAML_SEQUENCE_START_EVENT:
	    node = parse_sequence(parser);
	    break;

	case YAML_SCALAR_EVENT:
	    node = parse_scalar(parser);
	    break;

	case YAML_ALIAS_EVENT:
	    // ignoring alias
	    node = Qundef;
	    break;

	default:
	    rb_raise(rb_eArgError, "Invalid event %d at top level",
		    (int)parser->event.type);
    }
    return node;
}

static VALUE
rb_yaml_parser_parse(VALUE self, SEL sel)
{
    rb_yaml_parser_t *parser = RYAMLParser(self);
    VALUE root = Qnil;

    NEXT_EVENT();
    if (parser->event.type == YAML_STREAM_END_EVENT) {
	return Qnil;
    }
    if (parser->event.type == YAML_STREAM_START_EVENT) {
	NEXT_EVENT();
    }
    if (parser->event.type != YAML_DOCUMENT_START_EVENT) {
	rb_raise(rb_eArgError, "expected DOCUMENT_START event");
    }

    root = parse_node(parser);
    if (root == Qundef) {
	root = Qnil;
    }

    NEXT_EVENT();
    if (parser->event.type != YAML_DOCUMENT_END_EVENT) {
	rb_raise(rb_eArgError, "expected DOCUMENT_END event");
    }

    return root;
}

static IMP rb_yaml_parser_finalize_super = NULL; 

static void
rb_yaml_parser_finalize(void *rcv, SEL sel)
{
    // TODO: is this reentrant?
    rb_yaml_parser_t *rbparser = RYAMLParser(rcv);
    yaml_parser_delete(&rbparser->parser);

    if (rb_yaml_parser_finalize_super != NULL) {
	((void(*)(void *, SEL))rb_yaml_parser_finalize_super)(rcv, sel);
    }
}

static yaml_scalar_style_t
rb_symbol_to_scalar_style(VALUE sym)
{
    yaml_scalar_style_t style = YAML_ANY_SCALAR_STYLE;
    if (NIL_P(sym)) {
	return style;
    }
    else if (rb_to_id(sym) == id_plain) {
	style = YAML_PLAIN_SCALAR_STYLE;
    }
    else if (rb_to_id(sym) == id_quote2) {
	style = YAML_DOUBLE_QUOTED_SCALAR_STYLE;
    }
    return style;
}

static yaml_scalar_style_t
rb_symbol_to_sequence_style(VALUE sym)
{
    yaml_sequence_style_t style = YAML_ANY_SEQUENCE_STYLE;
    if (NIL_P(sym)) {
	return style;
    }
    else if (rb_to_id(sym) == id_inline) {
	style = YAML_FLOW_SEQUENCE_STYLE;
    }
    return style;
}

static yaml_scalar_style_t
rb_symbol_to_mapping_style(VALUE sym)
{
    yaml_mapping_style_t style = YAML_ANY_MAPPING_STYLE;
    if (NIL_P(sym)) {
	return style;
    }
    else if (rb_to_id(sym) == id_inline) {
	style = YAML_FLOW_MAPPING_STYLE;
    }
    return style;
}

static yaml_char_t*
rb_yaml_tag_or_null(VALUE tagstr, int *can_omit_tag, int * string_tag)
{
    // TODO make this part of the resolver chain; this is the wrong place for it
    const char *tag = RSTRING_PTR(tagstr);
    if (strcmp(tag, "tag:yaml.org,2002:str") == 0) {
	*can_omit_tag = 1;
	*string_tag   = 1;
	return NULL;	
    }
    else if ((strcmp(tag, "tag:yaml.org,2002:int") == 0) ||
	    (strcmp(tag, "tag:yaml.org,2002:float") == 0) ||
	    (strcmp(tag, "tag:ruby.yaml.org,2002:symbol") == 0) ||
	    (strcmp(tag, "tag:yaml.org,2002:true") == 0) ||
	    (strcmp(tag, "tag:yaml.org,2002:false") == 0) ||
	    (strcmp(tag, "tag:yaml.org,2002:null") == 0) ||
	    (strcmp(tag, "tag:yaml.org,2002:timestamp") == 0) ||
	    (strcmp(tag, YAML_DEFAULT_SEQUENCE_TAG) == 0) ||
	    (strcmp(tag, YAML_DEFAULT_MAPPING_TAG) == 0)) {
	*can_omit_tag = 1;
	return NULL;	
    }
    return (yaml_char_t*)tag;
}

static VALUE
rb_yaml_emitter_alloc(VALUE klass, SEL sel)
{
    NEWOBJ(emitter, struct rb_yaml_emitter_s);
    OBJSETUP(emitter, klass, T_OBJECT);
    yaml_emitter_initialize(&emitter->emitter);
    emitter->output = Qnil;
    return (VALUE)emitter;
}

static int
rb_yaml_str_output_handler(void *str, unsigned char *buffer, size_t size)
{
    rb_str_cat((VALUE)str, (char *)buffer, size);
    return 1;
}

static int
rb_yaml_io_output_handler(void *data, unsigned char* buffer, size_t size)
{
    rb_io_t *io_struct = ExtractIOStruct(data);
    assert(io_struct->write_fd != -1);
    return write(io_struct->write_fd, (const UInt8*)buffer, (CFIndex)size) > 0;
}

static VALUE
rb_yaml_emitter_set_output(VALUE self, SEL sel, VALUE output)
{
    rb_yaml_emitter_t *remitter = RYAMLEmitter(self);
    GC_WB(&remitter->output, output);
    yaml_emitter_t *emitter = &remitter->emitter;
    if (!NIL_P(output)) {
	switch (TYPE(output)) {
	    case T_FILE:
		yaml_emitter_set_output(emitter, rb_yaml_io_output_handler,
			(void *)output);
		break;

	    case T_STRING:
		yaml_emitter_set_output(emitter, rb_yaml_str_output_handler,
			(void *)output);
		break;

	    default:
		rb_raise(rb_eArgError, "unsupported YAML output type %s",
			rb_obj_classname(output));
	}	
    }
    return output;
}

static VALUE
rb_yaml_emitter_initialize(VALUE self, SEL sel, int argc, VALUE *argv)
{
    VALUE output = Qnil;
    rb_scan_args(argc, argv, "01", &output);
    if (NIL_P(output)) {
	output = rb_str_new(NULL, 0);
    }
    rb_yaml_emitter_set_output(self, 0, output);
    return self;
}

static VALUE
rb_yaml_emitter_stream(VALUE self, SEL sel)
{
    yaml_event_t ev;
    yaml_emitter_t *emitter = &RYAMLEmitter(self)->emitter;

    // RADAR: allow the encoding to be configurable
    yaml_stream_start_event_initialize(&ev, YAML_UTF8_ENCODING); 
    yaml_emitter_emit(emitter, &ev);

    rb_yield(self);

    yaml_stream_end_event_initialize(&ev);
    yaml_emitter_emit(emitter, &ev);
    yaml_emitter_flush(emitter);
    yaml_emitter_delete(emitter);
    // XXX: more cleanup here...
    return RYAMLEmitter(self)->output;
}

static VALUE
rb_yaml_emitter_document(VALUE self, SEL sel, int argc, VALUE *argv)
{
    yaml_emitter_t *emitter = &RYAMLEmitter(self)->emitter;

    VALUE impl_beg = Qnil, impl_end = Qnil;
    rb_scan_args(argc, argv, "02", &impl_beg, &impl_end);
    if (NIL_P(impl_beg)) {
	impl_beg = Qfalse;
    }
    if (NIL_P(impl_end)) {
	impl_end = Qtrue;
    }

    yaml_event_t ev;
    yaml_document_start_event_initialize(&ev, NULL, NULL, NULL, 0);
    yaml_emitter_emit(emitter, &ev);

    rb_yield(self);

    yaml_document_end_event_initialize(&ev, 1);
    yaml_emitter_emit(emitter, &ev);
    yaml_emitter_flush(emitter);
    return self;
}

static VALUE
rb_yaml_emitter_sequence(VALUE self, SEL sel, VALUE taguri, VALUE style)
{
    yaml_event_t ev;
    yaml_emitter_t *emitter = &RYAMLEmitter(self)->emitter;

    int can_omit_tag = 0;
    int string_tag   = 0;
    yaml_char_t *tag = rb_yaml_tag_or_null(taguri, &can_omit_tag, &string_tag);
    yaml_sequence_start_event_initialize(&ev, NULL, tag, can_omit_tag,
	    rb_symbol_to_sequence_style(style));
    yaml_emitter_emit(emitter, &ev);

    rb_yield(self);

    yaml_sequence_end_event_initialize(&ev);
    yaml_emitter_emit(emitter, &ev);
    return self;
}

static VALUE
rb_yaml_emitter_mapping(VALUE self, SEL sel, VALUE taguri, VALUE style)
{
    yaml_event_t ev;
    yaml_emitter_t *emitter = &RYAMLEmitter(self)->emitter;

    int can_omit_tag = 0;
    int string_tag   = 0;
    yaml_char_t *tag = rb_yaml_tag_or_null(taguri, &can_omit_tag, &string_tag);
    yaml_mapping_start_event_initialize(&ev, NULL, tag, can_omit_tag,
	    rb_symbol_to_mapping_style(style));
    yaml_emitter_emit(emitter, &ev);

    rb_yield(self);

    yaml_mapping_end_event_initialize(&ev);
    yaml_emitter_emit(emitter, &ev);
    return self;
}

static VALUE
rb_yaml_emitter_scalar(VALUE self, SEL sel, VALUE taguri, VALUE val,
	VALUE style)
{
    yaml_event_t ev;
    yaml_emitter_t *emitter = &RYAMLEmitter(self)->emitter;
    yaml_char_t *output = (yaml_char_t *)RSTRING_PTR(val);
    const size_t length = strlen((const char *)output);

    int can_omit_tag = 0;
    int string_tag   = 0;
    yaml_char_t *tag = rb_yaml_tag_or_null(taguri, &can_omit_tag, &string_tag);
    yaml_scalar_style_t sstyl = rb_symbol_to_scalar_style(style);
    if (string_tag
	    && (sstyl==YAML_ANY_SCALAR_STYLE || sstyl==YAML_PLAIN_SCALAR_STYLE)
	    && (detect_scalar_type((const char *)output, length) != NULL)) {
	// Quote so this is read back as a string, no matter what type it
	// looks like.
	sstyl = YAML_DOUBLE_QUOTED_SCALAR_STYLE;
    }
    yaml_scalar_event_initialize(&ev, NULL, tag, output, length,
	    can_omit_tag, can_omit_tag, sstyl);
    yaml_emitter_emit(emitter, &ev);

    return self;
}

static VALUE
rb_yaml_emitter_add(VALUE self, SEL sel, int argc, VALUE *argv)
{
    VALUE first = Qnil, second = Qnil;
    rb_scan_args(argc, argv, "11", &first, &second);
    rb_vm_call(first, sel_to_yaml, 1, &self);
    if (argc == 2) {
	rb_vm_call(second, sel_to_yaml, 1, &self);
    }
    return self;
}

static IMP rb_yaml_emitter_finalize_super = NULL; 

static void
rb_yaml_emitter_finalize(void *rcv, SEL sel)
{
    rb_yaml_emitter_t *emitter = RYAMLEmitter(rcv);
    yaml_emitter_delete(&emitter->emitter);

    if (rb_yaml_emitter_finalize_super != NULL) {
	((void(*)(void *, SEL))rb_yaml_emitter_finalize_super)(rcv, sel);
    }
}

static VALUE
rb_yaml_resolver_alloc(VALUE klass, SEL sel)
{
    NEWOBJ(resolver, struct rb_yaml_resolver_s);
    OBJSETUP(resolver, klass, T_OBJECT);
    resolver->tags = NULL;
    return (VALUE)resolver;
}

static VALUE
rb_yaml_resolver_initialize(VALUE self, SEL sel)
{
    rb_yaml_resolver_t *resolver = RYAMLResolver(self);
    CFMutableDictionaryRef d = CFDictionaryCreateMutable(NULL, 0, NULL,
	    &kCFTypeDictionaryValueCallBacks);
    GC_WB(&resolver->tags, d);
    CFMakeCollectable(d);
    return self;
}

static VALUE
rb_yaml_resolver_add_type(VALUE self, SEL sel, VALUE key, VALUE handler)
{
    if (!NIL_P(key)) {
	rb_yaml_resolver_t *r = RYAMLResolver(self);
	CFDictionarySetValue(r->tags, (const void *)rb_intern_str(key),
		(const void *)handler);
    }
    return Qnil;
}

void
Init_libyaml()
{
    id_plain = rb_intern("plain");
    id_quote2 = rb_intern("quote2");
    id_inline = rb_intern("inline");

    sel_to_yaml = sel_registerName("to_yaml:");
    sel_call = sel_registerName("call:");
    sel_yaml_new = sel_registerName("yaml_new:");

    rb_mYAML = rb_define_module("YAML");

    rb_mLibYAML = rb_define_module_under(rb_mYAML, "LibYAML");
    rb_define_const(rb_mLibYAML, "VERSION",
	    rb_str_new2(yaml_get_version_string()));

    rb_cParser = rb_define_class_under(rb_mLibYAML, "Parser", rb_cObject);
    rb_objc_define_method(*(VALUE *)rb_cParser, "alloc", rb_yaml_parser_alloc,
	    0);
    rb_objc_define_method(rb_cParser, "initialize", rb_yaml_parser_initialize,
	    -1);
    rb_objc_define_method(rb_cParser, "input", rb_yaml_parser_input, 0);
    rb_objc_define_method(rb_cParser, "input=", rb_yaml_parser_set_input, 1);
    // commented methods here are just unimplemented; i plan to put them in soon.
    //rb_objc_define_method(rb_cParser, "encoding", rb_yaml_parser_encoding, 0);
    //rb_objc_define_method(rb_cParser, "encoding=", rb_yaml_parser_set_encoding, 1);
    rb_objc_define_method(rb_cParser, "error", rb_yaml_parser_error, 0);
    rb_objc_define_method(rb_cParser, "load", rb_yaml_parser_load, 0);
    rb_objc_define_method(rb_cParser, "parse", rb_yaml_parser_parse, 0);
    rb_yaml_parser_finalize_super = rb_objc_install_method2((Class)rb_cParser,
	    "finalize", (IMP)rb_yaml_parser_finalize);

    rb_cResolver = rb_define_class_under(rb_mLibYAML, "Resolver", rb_cObject);
    rb_objc_define_method(*(VALUE *)rb_cResolver, "alloc",
	    rb_yaml_resolver_alloc, 0);
    rb_objc_define_method(rb_cResolver, "initialize",
	    rb_yaml_resolver_initialize, 0);
    //rb_objc_define_method(rb_cResolver, "transfer", rb_yaml_resolver_transfer, 1);
    rb_objc_define_method(rb_cResolver, "add_type", rb_yaml_resolver_add_type,
	    2);
    //rb_objc_define_method(rb_cResolver, "add_domain_type", rb_yaml_resolver_add_domain_type, 2);
    //rb_objc_define_method(rb_cResolver, "add_ruby_type", rb_yaml_resolver_add_ruby_type, 1);
    //rb_objc_define_method(rb_cResolver, "add_builtin_type", rb_yaml_resolver_add_builtin_type, 1);
    //rb_objc_define_method(rb_cResolver, "add_private_type", rb_yaml_resolver_add_private_type, 1);
    rb_oDefaultResolver = rb_vm_call(rb_cResolver, sel_registerName("new"), 0, NULL);
    rb_define_const(rb_mLibYAML, "DEFAULT_RESOLVER", rb_oDefaultResolver);

    rb_cEmitter = rb_define_class_under(rb_mLibYAML, "Emitter", rb_cObject);
    rb_objc_define_method(*(VALUE *)rb_cEmitter, "alloc", rb_yaml_emitter_alloc, 0);
    rb_define_attr(rb_cEmitter, "output", 1, 1);
    rb_objc_define_method(rb_cEmitter, "initialize", rb_yaml_emitter_initialize, -1);
    rb_objc_define_method(rb_cEmitter, "output=", rb_yaml_emitter_set_output, 1);
    //rb_objc_define_method(rb_cEmitter, "dump", rb_yaml_emitter_dump, -1);
    rb_objc_define_method(rb_cEmitter, "stream", rb_yaml_emitter_stream, 0);
    rb_objc_define_method(rb_cEmitter, "document", rb_yaml_emitter_document, -1);
    rb_objc_define_method(rb_cEmitter, "seq", rb_yaml_emitter_sequence, 2);
    rb_objc_define_method(rb_cEmitter, "map", rb_yaml_emitter_mapping, 2);
    rb_objc_define_method(rb_cEmitter, "scalar", rb_yaml_emitter_scalar, 3);
    rb_objc_define_method(rb_cEmitter, "add", rb_yaml_emitter_add, -1);

    //rb_objc_define_method(rb_cEmitter, "error", rb_yaml_emitter_error, 0);
    //rb_objc_define_method(rb_cEmitter, "encoding", rb_yaml_emitter_encoding, 0);
    //rb_objc_define_method(rb_cEmitter, "encoding=", rb_yaml_emitter_set_encoding, 1);
    //rb_objc_define_method(rb_cEmitter, "indentation", rb_yaml_emitter_indent, 0);
    // TODO: fill in the rest of the accessors
    rb_yaml_emitter_finalize_super = rb_objc_install_method2((Class)rb_cEmitter, "finalize", (IMP)rb_yaml_emitter_finalize);

    rb_cYamlNode = rb_define_class_under(rb_mYAML, "YamlNode", rb_cObject);
}
