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

typedef struct {
	yaml_emitter_t *emitter;
	VALUE bytestring;
} rb_yaml_emitter_t;

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
	if (RARRAY_LEN(stack) == 1)
	{
		return RARRAY_AT(stack, 0);
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

static int
rb_yaml_write_item_to_document(VALUE item, yaml_document_t *document)
{
	int nodeID = 0;
	if (TYPE(item) == T_ARRAY)
	{
		nodeID = yaml_document_add_sequence(document, (yaml_char_t *)YAML_DEFAULT_SEQUENCE_TAG, YAML_ANY_SEQUENCE_STYLE);
		long len = RARRAY_LEN(item);
		int ii;
		for(ii=0; ii<len; ii++)
		{
			int newItem = rb_yaml_write_item_to_document(RARRAY_AT(item, ii), document);
			yaml_document_append_sequence_item(document, nodeID, newItem);
		}
	}
	else if (TYPE(item) == T_HASH)
	{
		nodeID = yaml_document_add_mapping(document, (yaml_char_t *)YAML_DEFAULT_MAPPING_TAG, YAML_ANY_MAPPING_STYLE);
		VALUE keys = rb_hash_keys(item); // this is probably really inefficient.
		long len = RARRAY_LEN(item);
		int ii;
		for(ii=0; ii<len; ii++)
		{
			int keyID = rb_yaml_write_item_to_document(RARRAY_AT(keys, ii), document);
			int valID = rb_yaml_write_item_to_document(rb_hash_aref(item, RARRAY_AT(keys, ii)), document);
			yaml_document_append_mapping_pair(document, nodeID, keyID, valID);
		}
	}
	else
	{
		VALUE to_dump = rb_inspect(item);
		nodeID = yaml_document_add_scalar(document, (yaml_char_t *)YAML_DEFAULT_SCALAR_TAG, 
			(yaml_char_t*)(RSTRING_PTR(to_dump)), RSTRING_LEN(to_dump), YAML_ANY_SCALAR_STYLE);
	}
	return nodeID;
}

static int
rb_yaml_emitter_write_handler(void *bytestring, unsigned char* buffer, size_t size)
{
	VALUE bstr = (VALUE)bytestring;
	CFMutableDataRef data = rb_bytestring_wrapped_data(bstr);
	CFDataAppendBytes(data, (const UInt8*)buffer, (CFIndex)size);
	return 1;
}

static VALUE
rb_yaml_emitter_alloc(VALUE klass, SEL sel)
{
	rb_yaml_emitter_t *emitter = ALLOC(rb_yaml_emitter_t);
	GC_WB(&emitter->emitter, ALLOC(yaml_emitter_t));
	emitter->bytestring = rb_bytestring_new();
	yaml_emitter_initialize(emitter->emitter);
	yaml_emitter_set_output(emitter->emitter, rb_yaml_emitter_write_handler, (void*)emitter->bytestring);
	return Data_Wrap_Struct(klass, NULL, NULL, emitter);
}

static VALUE
rb_yaml_emitter_emit(VALUE self, SEL sel, VALUE obj)
{
	rb_yaml_emitter_t *emitter;
	Data_Get_Struct(self, rb_yaml_emitter_t, emitter);
	yaml_document_t document;
	memset(&document, 0, sizeof(yaml_document_t));
	yaml_document_initialize(&document, NULL, NULL, NULL, 0, 0);
	rb_yaml_write_item_to_document(obj, &document);
    yaml_emitter_dump(emitter->emitter, &document);
	yaml_document_delete(&document);
	return emitter->bytestring;
}

static VALUE
yaml_load(VALUE module, SEL sel, VALUE input)
{
	VALUE parser = rb_yaml_parser_alloc(rb_cParser, 0);
	VALUE ary = rb_yaml_parser_load(parser, 0, input);
	return ary;
}

static VALUE
yaml_dump(VALUE module, SEL sel, VALUE obj)
{
	VALUE rb_em = rb_yaml_emitter_alloc(rb_cEmitter, 0);
	return rb_yaml_emitter_emit(rb_em, 0, obj);
}


void
Init_libyaml()
{
	rb_mYAML = rb_define_module("YAML");
	
	rb_objc_define_method(*(VALUE *)rb_mYAML, "load", yaml_load, 1);
	rb_objc_define_method(*(VALUE *)rb_mYAML, "dump", yaml_dump, 1);
	
	rb_mLibYAML = rb_define_module_under(rb_mYAML, "LibYAML");
	rb_define_const(rb_mLibYAML, "VERSION", rb_str_new2(yaml_get_version_string()));
	
	rb_cParser = rb_define_class_under(rb_mLibYAML, "Parser", rb_cObject);
	rb_objc_define_method(*(VALUE *)rb_cParser, "alloc", rb_yaml_parser_alloc, 0);
	rb_objc_define_method(rb_cParser, "load", rb_yaml_parser_load, 1);
	
	rb_yaml_parser_finalize_super = rb_objc_install_method((Class)rb_cParser, sel_registerName("finalize"), (IMP)rb_yaml_parser_finalize);
	
	rb_cEmitter = rb_define_class_under(rb_mLibYAML, "Emitter", rb_cObject);
	rb_objc_define_method(rb_cEmitter, "emit", rb_yaml_emitter_emit, -1);
}