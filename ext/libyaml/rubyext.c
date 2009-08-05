/*
 *
 * rubyext.c - ruby extensions to libYAML
 * author: Patrick Thomson
 * date: July 27, 2009
 *
 */ 

#include "ruby/ruby.h"
#include "ruby/intern.h"
#include "ruby/node.h"
#include "ruby/io.h"
#include "objc.h"
#include "id.h"
#include "vm.h"
#include "yaml.h"

// Ideas to speed this up:
// none as of yet.

typedef struct rb_yaml_parser_s {
	struct RBasic basic;		// holds the class information
	yaml_parser_t *parser;		// the parser object.
	
	VALUE input;				// a reference to the object that's providing input
	
	VALUE *stack;				// the current object hierarchy that is being parsed.
								// the root element is at stack[0].
	uint32_t stack_index;		// what element are we currently adding to?
	uint32_t stack_size;		// how big can the stack become?
	
	VALUE resolver;				// used to determine how to unserialize objects.
	
	yaml_event_t event;			// the event that is currently being parsed.
	bool event_valid;			// is this event valid?
} rb_yaml_parser_t;

#define RYAMLParser(val) ((rb_yaml_parser_t*)val)

typedef struct rb_yaml_emitter_s {
	struct RBasic basic;		// holds the class information
	yaml_emitter_t *emitter;	// the emitter object
	
	VALUE output;				// the object to which we are writing
} rb_yaml_emitter_t;

#define RYAMLEmitter(val) ((rb_yaml_emitter_t*)val)

static VALUE rb_mYAML;
static VALUE rb_mLibYAML;
static VALUE rb_cParser;
static VALUE rb_cEmitter;
static VALUE rb_cResolver;

static ID id_tags_ivar;
static ID id_plain;
static ID id_quote2;

static SEL sel_to_yaml;

static VALUE rb_oDefaultResolver;

static struct mcache *to_yaml_cache = NULL;

static VALUE
rb_yaml_parser_alloc(VALUE klass, SEL sel)
{
	NEWOBJ(parser, struct rb_yaml_parser_s);
	OBJSETUP(parser, klass, T_OBJECT);
	GC_WB(&parser->parser, ALLOC(yaml_parser_t));
	parser->input = Qnil;
	yaml_parser_initialize(parser->parser);
	return (VALUE)parser;
}

static int
rb_yaml_io_read_handler(void *io_ptr, unsigned char *buffer, size_t size, size_t* size_read)
{
	VALUE io = (VALUE)io_ptr;
	long result = rb_io_primitive_read(ExtractIOStruct(io), (UInt8*)buffer, size);
	if (result == -1)
	{
		return 0;
	}
	*size_read = result;
	return 1;
}

#if 0
static void
rb_yaml_guess_type_of_plain_node(yaml_node_t *node)
{
	const char* v = (char*) node->data.scalar.value;
	if (node->data.scalar.length == 0)
	{
		node->tag = (yaml_char_t*)"tag:yaml.org,2002:null";
	}
	// holy cow, this is not a good solution at all.
	// i should incorporate rb_cstr_to_inum here, or something.
	else if (strtol(v, NULL, 10) != 0)
	{
		node->tag = (yaml_char_t*)"tag:yaml.org,2002:int";
	}
	else if (*v == ':')
	{
		node->tag = (yaml_char_t*)"tag:ruby.yaml.org,2002:symbol";
	}
	else if ((strcmp(v, "true") == 0) || (strcmp(v, "false") == 0))
	{
		node->tag = (yaml_char_t*)"tag:yaml.org,2002:bool";
	} 
}

#endif


static VALUE
rb_yaml_parser_set_input(VALUE self, SEL sel, VALUE input)
{
	rb_yaml_parser_t *rbparser = RYAMLParser(self);
	rbparser->input = input; // do we need to retain this?
	yaml_parser_t *parser = rbparser->parser;
	if (!NIL_P(input))
	{
		assert(parser != NULL);
		if (CLASS_OF(input) == rb_cByteString)
		{
			yaml_parser_set_input_string(parser, (const unsigned char*)rb_bytestring_byte_pointer(input), rb_bytestring_length(input));
		}
		else if (TYPE(input) == T_STRING)
		{
			// TODO: Make sure that this is Unicode-aware.
			yaml_parser_set_input_string(parser, (const unsigned char *)(RSTRING_PTR(input)), RSTRING_LEN(input));			
		}
		else if (TYPE(input) == T_FILE)
		{
			yaml_parser_set_input(parser, rb_yaml_io_read_handler, (void*)input);
		}
		else
		{
			rb_raise(rb_eArgError, "invalid input for YAML parser: %s", rb_obj_classname(input));
		}
	}
	return input;
}

static VALUE
rb_yaml_parser_input(VALUE self, SEL sel)
{
	return RYAMLParser(self)->input;
}

static VALUE
rb_yaml_parser_error(VALUE self, SEL sel)
{
	VALUE error = Qnil;
	char *msg = NULL;
	yaml_parser_t *parser = RYAMLParser(self)->parser;
	assert(parser != NULL);
	switch(parser->error)
	{
		case YAML_SCANNER_ERROR:
		case YAML_PARSER_ERROR:
		{
			asprintf(&msg, "syntax error on line %d, col %d: %s", parser->problem_mark.line,
				parser->problem_mark.column, parser->problem);
			error = rb_exc_new2(rb_eArgError, msg);
		}
		
		case YAML_NO_ERROR:
		break;
		
		default:
		error = rb_exc_new2(rb_eRuntimeError, parser->problem);
	}
	if(msg != NULL)
	{
		free(msg);
	}
	return error;
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
rb_yaml_parser_load(VALUE self, SEL sel)
{
	rb_notimplement();
}

static IMP rb_yaml_parser_finalize_super = NULL; 

// TODO: check with lrz to see if this is correctly reentrant
static void
rb_yaml_parser_finalize(void *rcv, SEL sel)
{
	rb_yaml_parser_t *rbparser = RYAMLParser(rcv);
	if((rbparser != NULL) && (rbparser->parser != NULL)) 
	{
		yaml_parser_delete(rbparser->parser);
		rbparser->parser = NULL;
	}
	if (rb_yaml_parser_finalize_super != NULL)
	{
		((void(*)(void *, SEL))rb_yaml_parser_finalize_super)(rcv, sel);
	}
}

static yaml_scalar_style_t
rb_symbol_to_scalar_style(VALUE sym)
{
	yaml_scalar_style_t style = YAML_ANY_SCALAR_STYLE;
	if (NIL_P(sym))
	{
		return style;
	}
	else if (rb_to_id(sym) == id_plain)
	{
		style = YAML_PLAIN_SCALAR_STYLE;
	}
	else if (rb_to_id(sym) == id_quote2)
	{
		style = YAML_DOUBLE_QUOTED_SCALAR_STYLE;
	}
	return style;
}


static yaml_char_t*
rb_yaml_tag_or_null(VALUE tagstr, int *can_omit_tag)
{
	// todo: make this part of the resolver chain; this is the wrong place for it
	const char *tag = RSTRING_PTR(tagstr);
	if ((strcmp(tag, "tag:yaml.org,2002:int") == 0) ||
		(strcmp(tag, "tag:yaml.org,2002:float") == 0) ||
		(strcmp(tag, "tag:ruby.yaml.org,2002:symbol") == 0) ||
		(strcmp(tag, "tag:yaml.org,2002:bool") == 0) ||
		(strcmp(tag, "tag:yaml.org,2002:null") == 0) ||
		(strcmp(tag, "tag:yaml.org,2002:str") == 0))
	{
		*can_omit_tag = 1;
		return NULL;	
	}
	return (yaml_char_t*)tag;
}

static VALUE
rb_yaml_resolver_initialize(VALUE self, SEL sel)
{
	rb_ivar_set(self, id_tags_ivar, rb_hash_new());
	return self;
}

static VALUE
rb_yaml_emitter_alloc(VALUE klass, SEL sel)
{
	NEWOBJ(emitter, struct rb_yaml_emitter_s);
	OBJSETUP(emitter, klass, T_OBJECT);
	GC_WB(&emitter->emitter, ALLOC(yaml_emitter_t));
	yaml_emitter_initialize(emitter->emitter);
	emitter->output = Qnil;
	return (VALUE)emitter;
}

static int
rb_yaml_bytestring_output_handler(void *bs, unsigned char *buffer, size_t size)
{
	CFMutableDataRef data = rb_bytestring_wrapped_data((VALUE)bs);
	CFDataAppendBytes(data, (const UInt8*)buffer, (CFIndex)size);
	return 1;
}

static int
rb_yaml_io_output_handler(void *data, unsigned char* buffer, size_t size)
{
	rb_io_t *io_struct = ExtractIOStruct(data);
	return (CFWriteStreamWrite(io_struct->writeStream, (const UInt8*)buffer, (CFIndex)size) > 0);
}

static VALUE
rb_yaml_emitter_set_output(VALUE self, SEL sel, VALUE output)
{

	rb_yaml_emitter_t *remitter = RYAMLEmitter(self);
	remitter->output = output;
	yaml_emitter_t *emitter = remitter->emitter;
	if (!NIL_P(output)) 
	{
		if (CLASS_OF(output) == rb_cByteString)
		{
			yaml_emitter_set_output(emitter, rb_yaml_bytestring_output_handler, (void*)output);
		}
		else if (TYPE(output) == T_FILE)
		{
			yaml_emitter_set_output(emitter, rb_yaml_io_output_handler, (void*)output);
		}
		else
		{
			rb_raise(rb_eArgError, "unsupported YAML output type %s", rb_obj_classname(output));
		}
	}
	return output;
}


static VALUE
rb_yaml_emitter_initialize(VALUE self, SEL sel, int argc, VALUE *argv)
{
	VALUE output = Qnil;
	rb_scan_args(argc, argv, "01", &output);
	if (NIL_P(output))
	{
		output = rb_bytestring_new();
	}
	rb_yaml_emitter_set_output(self, 0, output);
	return self;
}

static VALUE
rb_yaml_emitter_stream(VALUE self, SEL sel)
{
	yaml_event_t ev;
	yaml_emitter_t *emitter = RYAMLEmitter(self)->emitter;
	
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
	yaml_event_t ev;
	yaml_emitter_t *emitter = RYAMLEmitter(self)->emitter;
	VALUE impl_beg = Qnil, impl_end = Qnil;
	rb_scan_args(argc, argv, "02", &impl_beg, &impl_end);
	if(NIL_P(impl_beg)) { impl_beg = Qfalse; }
	if(NIL_P(impl_end)) { impl_end = Qtrue; }
	
	yaml_document_start_event_initialize(&ev, NULL, NULL, NULL, RTEST(impl_beg));
	yaml_emitter_emit(emitter, &ev);
	
	rb_yield(self);
	
	yaml_document_end_event_initialize(&ev, RTEST(impl_end));
	yaml_emitter_emit(emitter, &ev);
	yaml_emitter_flush(emitter);
	return self;
}

static VALUE
rb_yaml_emitter_sequence(VALUE self, SEL sel, VALUE taguri, VALUE style)
{
	yaml_event_t ev;
	yaml_emitter_t *emitter = RYAMLEmitter(self)->emitter;
	yaml_char_t *tag = (yaml_char_t*)RSTRING_PTR(taguri);
	yaml_sequence_start_event_initialize(&ev, NULL, tag, 1, YAML_ANY_SEQUENCE_STYLE);
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
	yaml_emitter_t *emitter = RYAMLEmitter(self)->emitter;
	yaml_char_t *tag = (yaml_char_t*)RSTRING_PTR(taguri);
	yaml_mapping_start_event_initialize(&ev, NULL, tag, 1, YAML_ANY_MAPPING_STYLE);
	yaml_emitter_emit(emitter, &ev);

	rb_yield(self);
		
	yaml_mapping_end_event_initialize(&ev);
	yaml_emitter_emit(emitter, &ev);
	return self;
}

static VALUE
rb_yaml_emitter_scalar(VALUE self, SEL sel, VALUE taguri, VALUE val, VALUE style)
{
	yaml_event_t ev;
	yaml_emitter_t *emitter = RYAMLEmitter(self)->emitter;
	yaml_char_t *output = (yaml_char_t*)RSTRING_PTR(val);
	int can_omit_tag = 0;
	yaml_char_t *tag = rb_yaml_tag_or_null(taguri, &can_omit_tag);
	yaml_scalar_event_initialize(&ev, NULL, tag, output, RSTRING_LEN(val), can_omit_tag, 0, rb_symbol_to_scalar_style(style));
	yaml_emitter_emit(emitter, &ev);
	
	return self;
}

static VALUE
rb_yaml_emitter_add(VALUE self, SEL sel, int argc, VALUE *argv)
{
	VALUE first = Qnil, second = Qnil;
	rb_scan_args(argc, argv, "11", &first, &second);
	rb_vm_call_with_cache(to_yaml_cache, first, sel_to_yaml, 1, &self);
	if(argc == 2)
	{
		rb_vm_call_with_cache(to_yaml_cache, second, sel_to_yaml, 1, &self);
	}
	return self;
	
}

static IMP rb_yaml_emitter_finalize_super = NULL; 

static void
rb_yaml_emitter_finalize(void *rcv, SEL sel)
{
	yaml_emitter_t *emitter;
	Data_Get_Struct(rcv, yaml_emitter_t, emitter);
	yaml_emitter_close(emitter);
	if (rb_yaml_emitter_finalize_super != NULL)
	{
		((void(*)(void *, SEL))rb_yaml_emitter_finalize_super)(rcv, sel);
	}
}

void
Init_libyaml()
{
	id_plain = rb_intern("plain");
	id_quote2 = rb_intern("quote2");
	id_tags_ivar = rb_intern("@tags");
	
	sel_to_yaml = sel_registerName("to_yaml:");
	to_yaml_cache = rb_vm_get_call_cache(sel_to_yaml);
	
	rb_mYAML = rb_define_module("YAML");
	
	rb_mLibYAML = rb_define_module_under(rb_mYAML, "LibYAML");
	rb_define_const(rb_mLibYAML, "VERSION", rb_str_new2(yaml_get_version_string()));
	
	rb_cParser = rb_define_class_under(rb_mLibYAML, "Parser", rb_cObject);
	rb_objc_define_method(*(VALUE *)rb_cParser, "alloc", rb_yaml_parser_alloc, 0);
	rb_objc_define_method(rb_cParser, "input", rb_yaml_parser_input, 0);
	rb_objc_define_method(rb_cParser, "input=", rb_yaml_parser_set_input, 1);
	rb_objc_define_method(rb_cParser, "initialize", rb_yaml_parser_initialize, -1);
	// commented methods here are just unimplemented; i plan to put them in soon.
	//rb_objc_define_method(rb_cParser, "encoding", rb_yaml_parser_encoding, 0);
	//rb_objc_define_method(rb_cParser, "encoding=", rb_yaml_parser_set_encoding, 1);
	rb_objc_define_method(rb_cParser, "error", rb_yaml_parser_error, 0);
	rb_objc_define_method(rb_cParser, "load", rb_yaml_parser_load, 0);
	rb_yaml_parser_finalize_super = rb_objc_install_method2((Class)rb_cParser, "finalize", (IMP)rb_yaml_parser_finalize);
	
	rb_cResolver = rb_define_class_under(rb_mLibYAML, "Resolver", rb_cObject);
	rb_define_attr(rb_cResolver, "tags", 1, 1);
	rb_objc_define_method(rb_cResolver, "initialize", rb_yaml_resolver_initialize, 0);
	//rb_objc_define_method(rb_cResolver, "transfer", rb_yaml_resolver_transfer, 1);
	//rb_objc_define_method(rb_cResolver, "add_domain_type", rb_yaml_resolver_add_domain_type, 2);
	//rb_objc_define_method(rb_cResolver, "add_ruby_type", rb_yaml_resolver_add_ruby_type, 1);
	//rb_objc_define_method(rb_cResolver, "add_builtin_type", rb_yaml_resolver_add_builtin_type, 1);
	//rb_objc_define_method(rb_cResolver, "add_private_type", rb_yaml_resolver_add_private_type, 1);
	rb_oDefaultResolver = rb_vm_call(rb_cResolver, selNew, 0, NULL, true);
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
}
