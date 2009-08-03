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
#include "id.h"
#include "yaml.h"

VALUE rb_vm_call(VALUE self, SEL sel, int argc, const VALUE *args, bool super);
long rb_io_primitive_read(struct rb_io_t *io_struct, UInt8 *buffer, long len);
VALUE rb_ary_last(VALUE, SEL, int, VALUE*);

VALUE rb_mYAML;
VALUE rb_mLibYAML;
VALUE rb_cParser;
VALUE rb_cEmitter;
VALUE rb_cDocument;
VALUE rb_cResolver;
VALUE rb_cNode;
VALUE rb_cSeqNode;
VALUE rb_cScalar;
VALUE rb_cOut;

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
		if (CLASS_OF(input) == rb_cByteString)
		{
			yaml_parser_set_input_string(parser, (const unsigned char *)(RSTRING_PTR(input)), RSTRING_LEN(input));
		}
		else if (TYPE(input) == T_STRING)
		{
			// I think this will work. At least, I hope so.
			yaml_parser_set_input_string(parser, (const unsigned char*)rb_bytestring_byte_pointer(input), rb_bytestring_length(input));
		}
		else if (TYPE(input) == T_FILE)
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
	rb_yaml_parser_set_input(self, 0, input);
	return self;
}

static VALUE
rb_yaml_parser_load(VALUE self, SEL sel)
{
	yaml_parser_t *parser;
	Data_Get_Struct(self, yaml_parser_t, parser);
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

static VALUE rb_yaml_node_new(yaml_node_t *node, int id, VALUE document);

static VALUE 
rb_yaml_document_alloc(VALUE klass, SEL sel)
{
	yaml_document_t *document = ALLOC(yaml_document_t);
	yaml_document_initialize(document, NULL, NULL, NULL, 0, 1);
	return Data_Wrap_Struct(rb_cDocument, NULL, NULL, document);
}

static VALUE 
rb_yaml_document_add_node(VALUE self, SEL sel, VALUE obj)
{
	rb_notimplement();
}

static VALUE
rb_yaml_document_add_sequence(VALUE self, SEL sel, VALUE taguri, VALUE style)
{
	yaml_document_t *document = (yaml_document_t*)DATA_PTR(self);
	// TODO: stop ignoring the style parameter
	yaml_char_t *tag = (yaml_char_t*)RSTRING_PTR(taguri);
	int nodeID = yaml_document_add_sequence(document, tag, YAML_ANY_SEQUENCE_STYLE);
	if (rb_block_given_p())
	{
		yaml_node_t *node = yaml_document_get_node(document, nodeID);
		VALUE n = rb_yaml_node_new(node, nodeID, self);
		rb_vm_yield(1, &n);
	}
	return self;
}

static VALUE
rb_yaml_document_add_scalar(VALUE self, SEL sel, VALUE taguri, VALUE str, VALUE style)
{
	yaml_document_t *document = (yaml_document_t*)DATA_PTR(self);
	// TODO: stop ignoring the style
	yaml_char_t *tag = (yaml_char_t*)RSTRING_PTR(taguri);
	yaml_char_t *val = (yaml_char_t*)RSTRING_PTR(str);
	int scalID = yaml_document_add_scalar(document, tag, val, RSTRING_LEN(str), YAML_ANY_SCALAR_STYLE);
	return rb_yaml_node_new(yaml_document_get_node(document, scalID), scalID, self);
}

static VALUE
rb_yaml_document_root_node(VALUE self, SEL sel)
{
	yaml_document_t *document;
	Data_Get_Struct(self, yaml_document_t, document);
	return rb_yaml_node_new(yaml_document_get_root_node(document), 0, self);
}

static VALUE
rb_yaml_document_implicit_start_p(VALUE self, SEL sel)
{
	yaml_document_t *document;
	Data_Get_Struct(self, yaml_document_t, document);
	return (document->start_implicit) ? Qtrue : Qfalse;
}

static VALUE
rb_yaml_document_implicit_end_p(VALUE self, SEL sel)
{
	yaml_document_t *document;
	Data_Get_Struct(self, yaml_document_t, document);
	return (document->end_implicit) ? Qtrue : Qfalse;
}

static IMP rb_yaml_document_finalize_super = NULL; 

static void
rb_yaml_document_finalize(void *rcv, SEL sel)
{
	yaml_document_t *document;
	Data_Get_Struct(rcv, yaml_document_t, document);
	yaml_document_delete(document);
	if (rb_yaml_document_finalize_super != NULL)
	{
		((void(*)(void *, SEL))rb_yaml_document_finalize_super)(rcv, sel);
	}
}

static VALUE
rb_yaml_node_new(yaml_node_t *node, int id, VALUE document)
{
	VALUE klass = rb_cNode;
	switch (node->type)
	{
		case YAML_SCALAR_NODE:
		klass = rb_cNode; // fix me.
		break;
		
		case YAML_MAPPING_NODE:
		klass = rb_cNode; // fix me, too.
		break;
		
		case YAML_SEQUENCE_NODE:
		klass = rb_cSeqNode;
		break;
		
		case YAML_NO_NODE:
		rb_raise(rb_eRuntimeError, "unexpected empty node");
	}
	VALUE n = Data_Wrap_Struct(klass, NULL, NULL, node);
	rb_ivar_set(n, rb_intern("node_id"), INT2FIX(id));
	rb_ivar_set(n, rb_intern("document"), document);
	return n;
}

static VALUE
rb_sequence_node_add(VALUE self, SEL sel, VALUE obj)
{
	VALUE doc = rb_ivar_get(self, rb_intern("document"));
	yaml_document_t *document;
	Data_Get_Struct(doc, yaml_document_t, document);
	VALUE scalar_node = rb_funcall(obj, rb_intern("to_yaml"), 1, doc);
	int seqID = FIX2INT(rb_ivar_get(self, rb_intern("node_id")));
	int scalID = FIX2INT(rb_ivar_get(scalar_node, rb_intern("node_id")));
	yaml_document_append_sequence_item(document, seqID, scalID);
	return self;
}

#if 0 // still need to think about this some more.
static VALUE
rb_yaml_node_tag(VALUE self, SEL sel)
{
	yaml_node_tag *node;
	Data_Get_Struct(self, yaml_node_tag, node);
	return rb_str_new2(node->tag);
}
#endif

static VALUE
rb_yaml_resolver_initialize(VALUE self, SEL sel)
{
	rb_ivar_set(self, rb_intern("tags"), rb_hash_new());
	return self;
}

static VALUE
rb_yaml_resolver_transfer(VALUE self, SEL sel, VALUE obj)
{
	if (rb_obj_is_kind_of(obj, rb_cNode))
	{
		// check the tags first, see if there's a corresponding Proc that will accept us
		// otherwise, try calling to_yaml
		// otherwise, go up a level
	}
	else 
	{
		VALUE document = rb_vm_call(rb_cDocument, selNew, 0, NULL, true);
		rb_vm_call(obj, (SEL)"to_yaml", 1, &document, true);
		return document;
	}
	return Qnil;
}

static VALUE
rb_yaml_emitter_alloc(VALUE klass, SEL sel)
{
	yaml_emitter_t *emitter = ALLOC(yaml_emitter_t);
	yaml_emitter_initialize(emitter);
	return Data_Wrap_Struct(klass, NULL, NULL, emitter);
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
	yaml_emitter_t *emitter;
	Data_Get_Struct(self, yaml_emitter_t, emitter);
	rb_ivar_set(self, rb_intern("output"), output);
	if (CLASS_OF(output) == rb_cByteString)
	{
		yaml_emitter_set_output(emitter, rb_yaml_bytestring_output_handler, (void*)output);
	}
	else
	{
		yaml_emitter_set_output(emitter, rb_yaml_io_output_handler, (void*)output);
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
rb_yaml_emitter_dump(VALUE self, SEL sel, VALUE doc)
{
	yaml_emitter_t *emitter;
	yaml_document_t *document;
	Data_Get_Struct(self, yaml_emitter_t, emitter);
	Data_Get_Struct(doc, yaml_document_t, document);
	yaml_emitter_open(emitter);
	yaml_emitter_dump(emitter, document);
	yaml_emitter_flush(emitter);
	return rb_ivar_get(self, rb_intern("output"));
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
	rb_mYAML = rb_define_module("YAML");
	
	rb_mLibYAML = rb_define_module_under(rb_mYAML, "LibYAML");
	rb_define_const(rb_mLibYAML, "VERSION", rb_str_new2(yaml_get_version_string()));
	
	rb_cParser = rb_define_class_under(rb_mLibYAML, "Parser", rb_cObject);
	rb_define_attr(rb_cParser, "input", 1, 1);
	rb_objc_define_method(*(VALUE *)rb_cParser, "alloc", rb_yaml_parser_alloc, 0);
	rb_objc_define_method(rb_cParser, "input=", rb_yaml_parser_set_input, 1);
	rb_objc_define_method(rb_cParser, "initialize", rb_yaml_parser_initialize, -1);
	// commented methods here are just unimplemented; i plan to put them in soon.
	//rb_objc_define_method(rb_cParser, "encoding", rb_yaml_parser_encoding, 0);
	//rb_objc_define_method(rb_cParser, "encoding=", rb_yaml_parser_set_encoding, 1);
	//rb_objc_define_method(rb_cParser, "error", rb_yaml_parser_error, 0);
	rb_objc_define_method(rb_cParser, "load", rb_yaml_parser_load, 0);
	rb_yaml_parser_finalize_super = rb_objc_install_method2((Class)rb_cParser, "finalize", (IMP)rb_yaml_parser_finalize);
	
	rb_cDocument = rb_define_class_under(rb_mLibYAML, "Document", rb_cObject);
	rb_objc_define_method(*(VALUE *)rb_cDocument, "alloc", rb_yaml_document_alloc, 0);
	rb_objc_define_method(rb_cDocument, "<<", rb_yaml_document_add_node, 1);
	rb_objc_define_method(rb_cDocument, "root", rb_yaml_document_root_node, 0);
	rb_objc_define_method(rb_cDocument, "seq", rb_yaml_document_add_sequence, 2);
	rb_objc_define_method(rb_cDocument, "scalar", rb_yaml_document_add_scalar, 3);
	//rb_objc_define_method(rb_cDocument, "[]", rb_yaml_document_aref, 1);
	//rb_objc_define_method(rb_cDocument, "version", rb_yaml_document_version, 0);
	rb_objc_define_method(rb_cDocument, "implicit_start?", rb_yaml_document_implicit_start_p, 0);
	rb_objc_define_method(rb_cDocument, "implicit_end?", rb_yaml_document_implicit_end_p, 0);
	//rb_objc_define_method(rb_cDocument, "implicit_start=", rb_yaml_document_implicit_start_set, 1);
	//rb_objc_define_method(rb_cDocument, "implicit_end=", rb_yaml_document_implicit_end_set, 1);
	rb_yaml_document_finalize_super = rb_objc_install_method2((Class)rb_cDocument, "finalize", (IMP)rb_yaml_document_finalize);
	
	rb_cNode = rb_define_class_under(rb_mLibYAML, "Node", rb_cObject);
	rb_define_attr(rb_cNode, "document", 1, 1);
	rb_define_attr(rb_cNode, "node_id", 1, 1);
	//rb_objc_define_method(rb_cNode, "type", rb_yaml_node_type, 0);
	//rb_objc_define_method(rb_cNode, "scalar?", rb_yaml_node_scalar_p, 0);
	//rb_objc_define_method(rb_cNode, "mapping?", rb_yaml_node_mapping_p, 0);
	//rb_objc_define_method(rb_cNode, "sequence?", rb_yaml_node_sequence_p, 0);
	//rb_objc_define_method(rb_cNode, "style", rb_yaml_node_style, 0);
	//rb_objc_define_method(rb_cNode, "tag", rb_yaml_node_tag, 0);
	//rb_objc_define_method(rb_cNode, "value", rb_yaml_node_value, 0);
	//rb_objc_define_method(rb_cNode, "start_mark", rb_yaml_node_start_mark, 0);
	//rb_objc_define_method(rb_cNode, "end_mark", rb_yaml_node_end_mark, 0);
	
	rb_cSeqNode = rb_define_class_under(rb_mLibYAML, "Seq", rb_cNode);
	rb_objc_define_method(rb_cNode, "add", rb_sequence_node_add, 1);
	
	rb_cResolver = rb_define_class_under(rb_mLibYAML, "Resolver", rb_cObject);
	rb_define_attr(rb_cResolver, "tags", 1, 1);
	rb_objc_define_method(rb_cResolver, "initialize", rb_yaml_resolver_initialize, 0);
	rb_objc_define_method(rb_cResolver, "transfer", rb_yaml_resolver_transfer, 1);
	//rb_objc_define_method(rb_cResolver, "add_domain_type", rb_yaml_resolver_add_domain_type, 2);
	//rb_objc_define_method(rb_cResolver, "add_ruby_type", rb_yaml_resolver_add_ruby_type, 1);
	//rb_objc_define_method(rb_cResolver, "add_builtin_type", rb_yaml_resolver_add_builtin_type, 1);
	//rb_objc_define_method(rb_cResolver, "add_private_type", rb_yaml_resolver_add_private_type, 1);
	rb_oDefaultResolver = rb_vm_call(rb_cResolver, selNew, 0, NULL, true);
	rb_define_const(rb_mLibYAML, "DEFAULT_RESOLVER", rb_oDefaultResolver);
	
	#if 0
	rb_cOut = rb_define_class_under(rb_mLibYAML, "Out", rb_cObject);
    rb_define_attr(cOut, "document", 1, 1 );
    rb_objc_define_method(rb_cOut, "initialize", rb_yaml_out_initialize, 1);
    rb_objc_define_method(rb_cOut, "map", rb_yaml_out_map, -1);
    rb_objc_define_method(rb_cOut, "seq", rb_yaml_out_seq, -1);
    rb_objc_define_method(rb_cOut, "scalar", rb_yaml_out_scalar, -1);
	#endif
	
	rb_cEmitter = rb_define_class_under(rb_mLibYAML, "Emitter", rb_cObject);
	rb_objc_define_method(*(VALUE *)rb_cEmitter, "alloc", rb_yaml_emitter_alloc, 0);
	rb_define_attr(rb_cEmitter, "output", 1, 1);
	rb_objc_define_method(rb_cEmitter, "initialize", rb_yaml_emitter_initialize, -1);
	rb_objc_define_method(rb_cEmitter, "output=", rb_yaml_emitter_set_output, 1);
	rb_objc_define_method(rb_cEmitter, "dump", rb_yaml_emitter_dump, 1);
	//rb_objc_define_method(rb_cEmitter, "error", rb_yaml_emitter_error, 0);
	//rb_objc_define_method(rb_cEmitter, "encoding", rb_yaml_emitter_encoding, 0);
	//rb_objc_define_method(rb_cEmitter, "encoding=", rb_yaml_emitter_set_encoding, 1);
	//rb_objc_define_method(rb_cEmitter, "indentation", rb_yaml_emitter_indent, 0);
	// TODO: fill in the rest of the accessors
	rb_yaml_emitter_finalize_super = rb_objc_install_method2((Class)rb_cEmitter, "finalize", (IMP)rb_yaml_emitter_finalize);
}