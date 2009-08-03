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

static VALUE
rb_yaml_parser_alloc(VALUE klass, SEL sel)
{
	yaml_parser_t *parser = ALLOC(yaml_parser_t);
	yaml_parser_initialize(parser);
	return Data_Wrap_Struct(klass, NULL, NULL, parser);
}

static VALUE
rb_yaml_object_from_event(yaml_event_t *event)
{
	VALUE str = rb_str_new2((const char*)event->data.scalar.value);
	return str;
}

static VALUE
rb_yaml_parse(yaml_parser_t *parser)
{
    yaml_event_t event;
    int done = 0;
	VALUE obj, temp;
	VALUE stack = rb_ary_new();
    while(!done) {
        if(!yaml_parser_parse(parser, &event)) {
			rb_raise(rb_eRuntimeError, "internal yaml parsing error");
		}
        done = (event.type == YAML_STREAM_END_EVENT);
        switch(event.type) {
            case YAML_SCALAR_EVENT:
				obj = rb_yaml_object_from_event(&event);
				temp = rb_ary_last(stack, 0, 0, 0);
				if (TYPE(temp) == T_ARRAY)
				{
					rb_ary_push(temp, obj);
				}
				else if (TYPE(temp) == T_HASH)
				{
					rb_ary_push(stack, obj);
				}
				else
				{
					rb_objc_retain((void*)temp);
					rb_ary_pop(stack);
					rb_hash_aset(rb_ary_last(stack, 0, 0, 0), temp, obj);
				}                
                break;
            case YAML_SEQUENCE_START_EVENT:
                rb_ary_push(stack, rb_ary_new());
                break;
            case YAML_MAPPING_START_EVENT:
				rb_ary_push(stack, rb_hash_new());
                break;
            case YAML_SEQUENCE_END_EVENT:
            case YAML_MAPPING_END_EVENT:
				// TODO: Check for retain count errors.
				temp = rb_ary_pop(stack);
				VALUE last = rb_ary_last(stack, 0, 0, 0);
				if (NIL_P(last)) 
				{
					rb_ary_push(stack, temp);
				}
				else if (TYPE(last) == T_ARRAY)
				{
					rb_ary_push(last, temp);
				}
				else if (TYPE(last) == T_HASH)
				{
					rb_ary_push(stack, temp);
				}
				else
				{
					obj = rb_ary_last(stack, 0, 0, 0);
					rb_objc_retain((void*)obj);
					rb_hash_aset(rb_ary_last(stack, 0, 0, 0), obj, temp);
				}
                break;
            case YAML_NO_EVENT:
                break;
            default:
                break;
        }
        yaml_event_delete(&event);
    }
    return stack;
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
rb_yaml_parser_load(VALUE self, SEL sel, VALUE io)
{
	yaml_parser_t *parser;
	Data_Get_Struct(self, yaml_parser_t, parser);
	if (TYPE(io) == T_STRING)
	{
		yaml_parser_set_input_string(parser, (const unsigned char*)RSTRING_PTR(io), (size_t)RSTRING_LEN(io));
	} else {
		yaml_parser_set_input(parser, (yaml_read_handler_t*)rb_yaml_parser_io_handler, (void*)io);
	}
	return rb_yaml_parse(parser);
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
	rb_notimplement();
}

static VALUE
yaml_load(VALUE module, SEL sel, VALUE input)
{
	VALUE parser = rb_yaml_parser_alloc(rb_cParser, 0);
	VALUE ary = rb_yaml_parser_load(parser, 0, input);
}

static VALUE
yaml_dump(VALUE module, SEL sel, int argc, VALUE* argv)
{
	return Qnil;
}


void
Init_libyaml()
{
	rb_mYAML = rb_define_module("YAML");
	
	rb_objc_define_method(*(VALUE *)rb_mYAML, "load", yaml_load, 1);
	rb_objc_define_method(*(VALUE *)rb_mYAML, "dump", yaml_dump, -1);
	
	rb_mLibYAML = rb_define_module_under(rb_mYAML, "LibYAML");
	rb_define_const(rb_mLibYAML, "VERSION", rb_str_new2(yaml_get_version_string()));
	
	rb_cParser = rb_define_class_under(rb_mLibYAML, "Parser", rb_cObject);
	rb_objc_define_method(*(VALUE *)rb_cParser, "alloc", rb_yaml_parser_alloc, 0);
	rb_objc_define_method(rb_cParser, "load", rb_yaml_parser_load, 1);
	
	rb_yaml_parser_finalize_super = rb_objc_install_method((Class)rb_cParser, sel_registerName("finalize"), (IMP)rb_yaml_parser_finalize);
	
	rb_cEmitter = rb_define_class_under(rb_mLibYAML, "Emitter", rb_cObject);
	rb_objc_define_method(rb_cEmitter, "emit", rb_yaml_emitter_emit, -1);
}