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
VALUE rb_cResolver;
VALUE rb_cNode;
VALUE rb_cScalar;
VALUE rb_cSeq;
VALUE rb_cMap;

VALUE rb_DefaultResolver;

static ID id_resolver;

static VALUE
rb_yaml_parser_alloc(VALUE klass, SEL sel)
{
	yaml_parser_t *yparser = ALLOC(yaml_parser_t);
	yaml_parser_initialize(yparser);
	return Data_Wrap_Struct(klass, NULL, NULL, yparser);
}

static VALUE
rb_yaml_parser_initialize(VALUE self, SEL sel, int argc, VALUE *argv)
{
	VALUE resolver = Qnil;
	rb_scan_args(argc, argv, "01", &resolver);
	if (NIL_P(resolver))
	{
		resolver = rb_oDefaultResolver;
	}
	rb_ivar_set(self, id_resolver, resolver);
	return self;
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

static int
rb_yaml_parser_io_handler(VALUE io, unsigned char *buffer, size_t size, size_t *size_read)
{
	if (rb_io_eof(io, 0) == Qtrue)
	{
		*size_read = 0;
		return 1;
	}
	long ret = rb_io_primitive_read(ExtractIOStruct(io), (UInt8*)buffer, size);
	if (ret == -1) {
		return 0;
	}
	*size_read = (size_t)ret;
	return 1;
}

static VALUE
rb_yaml_parser_load(VALUE self, SEL sel, int argc, VALUE *argv)
{
	rb_notimplement();
}

static VALUE
rb_yaml_parser_load_documents(VALUE self, SEL sel, int argc, VALUE *argv)
{
	rb_notimplement();
}

static VALUE
rb_yaml_emitter_alloc(VALUE klass, SEL sel)
{
	yaml_emitter_t *yemitter = ALLOC(yaml_emitter_t);
	yaml_emitter_initialize(yemitter);
	return Data_Wrap_Struct(klass, NULL, NULL, yemitter);
}

static VALUE
rb_yaml_emitter_initialize(VALUE self, SEL sel)
{
	return self;
}

static VALUE
rb_yaml_emitter_emit(VALUE self, SEL sel, int argc, VALUE *argv)
{
	VALUE object, output;
	rb_scan_args(argc, argv, "11", &object, &output);
	if (NIL_P(output))
	{
		output = rb_bytestring_new();
	}
	rb_register_emitter_output(self, output);
	rb_emitter_emit(self, object);
	return output;
}

static VALUE
rb_register_emitter_output(VALUE self, VALUE output)
{
	yaml_emitter_t *emitter;
	Data_Get_Struct(self, yaml_emitter_t, emitter);
	if (TYPE(output) = T_BYTESTRING)
	{
		
	}
}

static IMP rb_yaml_emitter_finalize_super = NULL; 

static void
rb_yaml_emitter_finalize(void *rcv, SEL sel)
{
	rb_notimplement();
}

static VALUE
rb_libyaml_compile(VALUE self, SEL sel, VALUE obj)
{
	rb_notimplement();
}


void
Init_libyaml()
{
	rb_mYAML = rb_define_module("YAML");
	
	rb_mLibYAML = rb_define_module_under(rb_mYAML, "LibYAML");
	rb_objc_define_method(*(VALUE *)rb_mLibYAML, "load", rb_libyaml_compile, 1);
	rb_define_const(rb_mLibYAML, "VERSION", rb_str_new2(yaml_get_version_string()));
	
	rb_cParser = rb_define_class_under(rb_mLibYAML, "Parser", rb_cObject);
	rb_objc_define_method(*(VALUE *)rb_cParser, "alloc", rb_yaml_parser_alloc, 0);
	rb_define_attr(rb_cParser, "resolver", 1, 1);
	rb_define_attr(rb_cParser, "input", 1, 1);
	rb_objc_define_method(rb_cParser, "initialize", rb_yaml_parser_initialize, -1);
	rb_objc_define_method(rb_cParser, "load", rb_yaml_parser_load, -1);
	rb_objc_define_method(rb_cParser, "load_documents", rb_yaml_parser_load_documents, -1);
	rb_yaml_parser_finalize_super = rb_objc_install_method2((Class)rb_cParser, "finalize", (IMP)rb_yaml_parser_finalize);
	
	rb_cEmitter = rb_define_class_under(rb_mLibYAML, "Emitter", rb_cObject);
	rb_objc_define_method(*(VALUE *)rb_cEmitter, "alloc", rb_yaml_emitter_alloc, 0);
	rb_objc_define_method(rb_cEmitter, "initialize", rb_yaml_emitter_initialize, -1);
	rb_objc_define_method(rb_cEmitter, "emit", rb_yaml_emitter_emit, -1);
	rb_yaml_emitter_finalize_super = rb_objc_install_method2((Class)rb_cEmitter, "finalize", (IMP)rb_yaml_emitter_finalize);
	
	rb_cResolver = rb_define_class_under(rb_mLibYAML, "Resolver", rb_cObject);
	rb_define_attr(rb_cResolver, "tags", 1, 1);
	rb_objc_define_method(rb_cResolver, "initialize", rb_yaml_resolver_initialize, 0);
	
	rb_cDocument = rb_define_class_under(rb_mLibYAML, "Document", rb_cObject);
	rb_objc_define_method(rb_cDocument, "<<", rb_yaml_document_add_node, 1);
	rb_objc_define_method(rb_cDocument, "root", rb_yaml_document_root_node, 0);
	rb_objc_define_method(rb_cDocument, "[]", rb_yaml_document_aref, 1);
	
	rb_cNode = rb_define_class_under(rb_mLibYAML, "Node", rb_cObject);
	rb_objc_define_method(rb_cNode, "initialize_copy", rb_yaml_node_init_copy, 1);
	rb_define_attr(rb_cNode, "emitter", 1, 1);
	rb_define_attr(rb_cNode, "kind", 1, 0);
	rb_define_attr(rb_cNode, "type_id", 1, 0);
	rb_define_attr(rb_cNode, "resolver", 1, 1);
	rb_define_attr(rb_cNode, "value", 1, 0);
	
	rb_cScalar = rb_define_class_under(rb_mLibYAML, "Scalar", rb_cNode);
	
	rb_cSeq = rb_define_class_under(rb_mLibYAML, "Seq", rb_cNode);
	
	rb_cMap = rb_define_class_under(rb_mLibYAML, "Map", rb_cNode);
}