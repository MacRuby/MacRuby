/*
 *
 * rubyext.c - ruby extensions to libYAML
 * author: Patrick Thomson
 * date: July 27, 2009
 *
 */ 

#include "ruby/ruby.h"
#include "ruby/intern.h"
#include "ruby/io.h"
#include "objc.h"
#include "yaml.h"

long rb_io_primitive_read(struct rb_io_t *io_struct, UInt8 *buffer, long len);
VALUE rb_ary_last(VALUE, SEL, int, VALUE*);

VALUE rb_mYAML;
VALUE rb_mLibYAML;
VALUE rb_cParser;
VALUE rb_cEmitter;
VALUE rb_cDocument;
VALUE rb_cResolver;
VALUE rb_cNode;
VALUE rb_cScalar;

VALUE rb_oDefaultResolver;

static VALUE
rb_yaml_parser_alloc(VALUE klass, SEL sel)
{
	yaml_parser_t *yparser = ALLOC(yaml_parser_t);
	yaml_parser_initialize(yparser);
	return Data_Wrap_Struct(klass, NULL, NULL, yparser);
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
	if (rb_io_eof(io, 0) == Qtrue)
	{
		*size_read = 0;
		return 1;
	}
	*size_read = result;
	return 1;
}

static VALUE
rb_yaml_parser_set_input(VALUE self, SEL sel, VALUE input)
{
	yaml_parser_t *parser;
	rb_ivar_set(self, rb_intern("input"), input);
	Data_Get_Struct(self, yaml_parser_t, parser);
	if (!NIL_P(input))
	{
		if (TYPE(input) == T_STRING)
		{
			yaml_parser_set_input_string(parser, (const unsigned char *)(RSTRING_PTR(input)), RSTRING_LEN(input));
		}
		else if (TYPE(input) == T_BYTESTRING)
		{
			// I think this will work. At least, I hope so.
			yaml_parser_set_input_string(parser, (const unsigned char*)rb_bytestring_byte_pointer(input), rb_bytestring_length(input));
		}
		else if (TYPE(input) == T_IO)
		{
			yaml_parser_set_input(parser, rb_yaml_io_read_handler, (void*)input);
		}
	}
	return input;
}

static VALUE
rb_yaml_parser_initialize(VALUE self, SEL sel, int argc, VALUE *argv)
{
	VALUE input = Qnil;
	rb_scan_args(argc, argv, "01", &input);
	rb_yaml_parser_set_input(self, rb_intern("input"), input);
	return self;
}

static VALUE
rb_yaml_parser_load(VALUE self, SEL sel)
{
	yaml_parser_t *parser;
	Data_Get_Struct(rcv, yaml_parser_t, parser);
	yaml_document_t *document = ALLOC(yaml_document_t);
	yaml_parser_load(parser, document);
	return Data_Wrap_Struct(rb_cDocument, NULL, NULL, document);
}

static IMP rb_yaml_parser_finalize_super = NULL; 

static void
rb_yaml_parser_finalize(void *rcv, SEL sel)
{
	yaml_parser_t *parser;
	Data_Get_Struct(rcv, yaml_parser_t, parser);
	yaml_parser_delete(parser);
	if (rb_yaml_parser_finalize_super != NULL)
	{
		((void(*)(void *, SEL))rb_yaml_parser_finalize_super)(rcv, sel);
	}
}


void
Init_libyaml()
{
	rb_mYAML = rb_define_module("YAML");
	
	rb_mLibYAML = rb_define_module_under(rb_mYAML, "LibYAML");
	rb_define_const(rb_mLibYAML, "VERSION", rb_str_new2(yaml_get_version_string()));
	
	rb_cParser = rb_define_class_under(rb_mLibYAML, "Parser", rb_cObject);
	rb_define_attr(rb_cParser, "input", 1, 1);
	rb_objc_define_method(rb_cParser, "input=", rb_yaml_parser_set_input, 1);
	rb_objc_define_method(rb_cParser, "initialize", rb_yaml_parser_initialize, -1);
	// unimplemented
	//rb_objc_define_method(rb_cParser, "encoding", rb_yaml_parser_encoding, 0);
	//rb_objc_define_method(rb_cParser, "encoding=", rb_yaml_parser_set_encoding, 1);
	//rb_objc_define_method(rb_cParser, "error", rb_yaml_parser_error, 0);
	rb_objc_define_method(rb_cParser, "load", rb_yaml_parser_load, 0);
	rb_yaml_parser_finalize_super = rb_objc_install_method2((Class)rb_cParser, "finalize", (IMP)rb_yaml_parser_finalize);
	
	rb_cDocument = rb_define_class_under(rb_mLibYAML, "Document", rb_cObject);
	rb_objc_define_method(rb_cDocument, "<<", rb_yaml_document_add_node, 1);
	rb_objc_define_method(rb_cDocument, "root", rb_yaml_document_root_node, 0);
	rb_objc_define_method(rb_cDocument, "[]", rb_yaml_document_aref, 1);
	rb_objc_define_method(rb_cDocument, "version", rb_yaml_document_version, 0);
	rb_objc_define_method(rb_cDocument, "implicit_start?", rb_yaml_document_implicit_start_p, 0);
	rb_objc_define_method(rb_cDocument, "implicit_end?", rb_yaml_document_implicit_end_p, 0);
	rb_objc_define_method(rb_cDocument, "implicit_start=", rb_yaml_document_implicit_start_set, 1);
	rb_objc_define_method(rb_cDocument, "implicit_end=", rb_yaml_document_implicit_end_set, 1);
	
	rb_cNode = rb_define_class_under(rb_mLibYAML, "Node", rb_cObject);
	rb_objc_define_method(rb_cNode, "type", rb_yaml_node_type, 0);
	rb_objc_define_method(rb_cNode, "scalar?", rb_yaml_node_scalar_p, 0);
	rb_objc_define_method(rb_cNode, "mapping?", rb_yaml_node_mapping_p, 0);
	rb_objc_define_method(rb_cNode, "sequence?", rb_yaml_node_sequence_p, 0);
	rb_objc_define_method(rb_cNode, "style", rb_yaml_node_style, 0);
	rb_objc_define_method(rb_cNode, "tag", rb_yaml_node_tag, 0);
	rb_objc_define_method(rb_cNode, "value", rb_yaml_node_value, 0);
	rb_objc_define_method(rb_cNode, "start_mark", rb_yaml_node_start_mark, 0);
	rb_objc_define_method(rb_cNode, "end_mark", rb_yaml_node_end_mark, 0);
	
	rb_cResolver = rb_define_class_under(rb_mLibYAML, "Resolver", rb_cObject);
	rb_define_attr(rb_cResolver, "tags", 1, 1);
	rb_objc_define_method(rb_cResolver, "transfer", rb_yaml_resolver_transfer, 1);
	rb_objc_define_method(rb_cResolver, "add_domain_type", rb_yaml_resolver_add_domain_type, 2);
	rb_objc_define_method(rb_cResolver, "add_ruby_type", rb_yaml_resolver_add_ruby_type, 1);
	rb_objc_define_method(rb_cResolver, "add_builtin_type", rb_yaml_resolver_add_builtin_type, 1);
	rb_objc_define_method(rb_cResolver, "add_private_type", rb_yaml_resolver_add_private_type, 1);
	rb_oDefaultResolver = rb_vm_call(rb_cResolver, selNew, 0, NULL, true);
	rb_define_const(rb_mLibYAML, "DEFAULT_RESOLVER", rb_oDefaultResolver);
	
	rb_cEmitter = rb_define_class_under(rb_mLibYAML, "Emitter", rb_cObject);
	rb_objc_define_method(*(VALUE *)rb_cEmitter, "alloc", rb_yaml_emitter_alloc, 0);
	rb_define_attr(rb_cEmitter, "output", 1, 1);
	rb_objc_define_method(rb_cEmitter, "output=", rb_yaml_emitter_set_output, 1);
	rb_objc_define_method(rb_cEmitter, "initialize", rb_yaml_emitter_initialize, -1);
	rb_objc_define_method(rb_cEmitter, "dump", rb_yaml_emitter_dump, 1);
	rb_objc_define_method(rb_cEmitter, "error", rb_yaml_emitter_error, 0);
	rb_objc_define_method(rb_cEmitter, "encoding", rb_yaml_emitter_encoding, 0);
	rb_objc_define_method(rb_cEmitter, "encoding=", rb_yaml_emitter_set_encoding, 1);
	rb_objc_define_method(rb_cEmitter, "indentation", rb_yaml_emitter_indent, 0);
	// TODO: fill in the rest of the accessors
	rb_yaml_emitter_finalize_super = rb_objc_install_method2((Class)rb_cEmitter, "finalize", (IMP)rb_yaml_emitter_finalize);
}