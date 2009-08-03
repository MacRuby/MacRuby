/*
 *
 * rubyext.c - ruby extensions to libYAML
 * author: Patrick Thomson
 * date: July 27, 2009
 *
 */ 

#include "ruby/ruby.h"
#include "yaml.h"

static VALUE
yaml_load(VALUE module, SEL sel, VALUE input)
{
	return Qnil;
}

static VALUE
yaml_dump(VALUE module, SEL sel, int argc, VALUE* argv)
{
	return Qnil;
}

static VALUE
rb_yaml_parser_alloc(VALUE klass, SEL sel)
{
	yaml_parser_t *parser = ALLOC(yaml_parser_t);
	yaml_parser_initialize(parser);
	// XXX: Figure out how to pass the yaml_parser_delete() method to the parser upon deallocation.
	return Data_Wrap_Struct(klass, NULL, NULL, parser);
}

static int
rb_yaml_parser_io_handler(VALUE io, unsigned char *out_buffer, size_t size, size_t *size_read)
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
rb_yaml_parser_load(VALUE self, SEL sel, VALUE io)
{
	yaml_parser_t *parser;
	Data_Get_Struct(self, yaml_parser_t, parser);
	yaml_parser_set_input(parser, (yaml_read_handler_t*)rb_yaml_parser_io_handler, io);
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

static VALUE
rb_yaml_emitter_emit(VALUE self, SEL sel, int argc, VALUE *argv)
{
	return Qnil;
}

void
Init_yaml()
{
	VALUE rb_mYAML = rb_define_module("YAML");
	
	rb_objc_define_method(*(VALUE *)rb_mYAML, "load", yaml_load, 1);
	rb_objc_define_method(*(VALUE *)rb_mYAML, "dump", yaml_dump, -1);
	
	VALUE rb_mLibYAML = rb_define_module_under(rb_mYAML, "LibYAML");
	rb_define_const(rb_mLibYAML, "VERSION", rb_str_new2(yaml_get_version_string()));
	
	VALUE rb_cParser = rb_define_class_under(rb_mLibYAML, "Parser", rb_cObject);
	rb_objc_define_method(*(VALUE *)rb_cParser, "alloc", rb_yaml_parser_alloc, 0);
	rb_objc_define_method(rb_cParser, "load", rb_yaml_parser_load, 1);
	
	rb_yaml_parser_finalize_super = rb_objc_install_method2((Class)rb_cParser, "finalize", (IMP)rb_yaml_parser_finalize);
	
	VALUE rb_cEmitter = rb_define_class_under(rb_mLibYAML, "Emitter", rb_cObject);
	rb_objc_define_method(rb_cEmitter, "emit", rb_yaml_emitter_emit, -1);
}