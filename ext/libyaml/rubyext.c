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

// too lazy to find out what headers these belong to.
VALUE rb_vm_yield(int argc, const VALUE *argv);
VALUE rb_vm_call(VALUE self, SEL sel, int argc, const VALUE *args, bool super);
long rb_io_primitive_read(struct rb_io_t *io_struct, UInt8 *buffer, long len);
VALUE rb_ary_last(VALUE, SEL, int, VALUE*);

// Ideas to speed this up:
// Nodes: Stop relying on @document and @node_id as ivars; embed them in a
// struct that I can access through Data_Get_Struct();
// Nodes: Cache the tag as a Ruby string

typedef struct rb_yaml_node_s {
	struct RBasic basic;
	yaml_node_t *node;
	int node_id;
	yaml_document_t *document;
} rb_yaml_node_t;

#define RYAMLNode(val) ((rb_yaml_node_t*)val)

typedef struct rb_yaml_document_s {
	struct RBasic basic;
	yaml_document_t *document;
} rb_yaml_document_t;

#define RYAMLDoc(val) ((rb_yaml_document_t*)val)

typedef struct rb_yaml_parser_s {
	struct RBasic basic;
	yaml_parser_t *parser;
	VALUE input;
} rb_yaml_parser_t;

#define RYAMLParser(val) ((rb_yaml_parser_t*)val)

static VALUE rb_mYAML;
static VALUE rb_mLibYAML;
static VALUE rb_cParser;
static VALUE rb_cEmitter;
static VALUE rb_cDocument;
static VALUE rb_cResolver;
static VALUE rb_cNode;
static VALUE rb_cSeqNode;
static VALUE rb_cMapNode;
static VALUE rb_cScalarNode;

static ID id_plain;
static ID id_quote2;
static ID id_tags_ivar; 
static ID id_input_ivar;
static ID id_node_id_ivar;
static ID id_document_ivar;

static VALUE rb_oDefaultResolver;

static VALUE
rb_yaml_parser_alloc(VALUE klass, SEL sel)
{
	NEWOBJ(parser, struct rb_yaml_parser_s);
	OBJSETUP(parser, klass, T_OBJECT);
	// XXX: check with Laurent to see if this is going to be correct, as parser
	// will have stuff assigned to its members...if not, we should just be able
	// to replace this with malloc...actually, maybe we should do that anyway.
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
	yaml_parser_t *parser = RYAMLParser(self)->parser;
	yaml_document_t *document = ALLOC(yaml_document_t);
	if(yaml_parser_load(parser, document) == 0) {
		rb_exc_raise(rb_yaml_parser_error(self, sel));
	}
	return Data_Wrap_Struct(rb_cDocument, NULL, NULL, document);
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

static VALUE rb_yaml_node_new(yaml_node_t *node, int id, VALUE document);

static VALUE 
rb_yaml_document_alloc(VALUE klass, SEL sel)
{
	yaml_document_t *document = ALLOC(yaml_document_t);
	yaml_document_initialize(document, NULL, NULL, NULL, 0, 1);
	return Data_Wrap_Struct(rb_cDocument, NULL, NULL, document);
}

static VALUE
rb_yaml_document_add_sequence(VALUE self, SEL sel, VALUE taguri, VALUE style)
{
	yaml_document_t *document = (yaml_document_t*)DATA_PTR(self);
	// TODO: stop ignoring the style parameter
	yaml_char_t *tag = (yaml_char_t*)RSTRING_PTR(taguri);
	int nodeID = yaml_document_add_sequence(document, tag, YAML_ANY_SEQUENCE_STYLE);
	if (nodeID == 0)
	{
		rb_exc_raise(rb_yaml_parser_error(self, sel));
	}
	if (rb_block_given_p())
	{
		yaml_node_t *node = yaml_document_get_node(document, nodeID);
		VALUE n = rb_yaml_node_new(node, nodeID, self);
		rb_vm_yield(1, &n);
		return n;
	}
	return self;
}

static VALUE
rb_yaml_document_add_mapping(VALUE self, SEL sel, VALUE taguri, VALUE style)
{
	yaml_document_t *document = (yaml_document_t*)DATA_PTR(self);
	yaml_char_t *tag = (yaml_char_t*)RSTRING_PTR(taguri);
	int nodeID = yaml_document_add_mapping(document, tag, YAML_ANY_MAPPING_STYLE);
	if (nodeID == 0)
	{
		rb_exc_raise(rb_yaml_parser_error(self, sel));
	}
	if (rb_block_given_p())
	{
		yaml_node_t *node = yaml_document_get_node(document, nodeID);
		VALUE n = rb_yaml_node_new(node, nodeID, self);
		rb_vm_yield(1, &n);
		return n;
	}
	return self;
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
rb_yaml_tag_or_null(VALUE tagstr)
{
	const char *tag = RSTRING_PTR(tagstr);
	if ((strcmp(tag, "tag:yaml.org,2002:int") == 0) ||
		(strcmp(tag, "tag:yaml.org,2002:float") == 0) ||
		(strcmp(tag, "tag:ruby.yaml.org,2002:symbol") == 0) ||
		(strcmp(tag, "tag:yaml.org,2002:bool") == 0))
	{
		return NULL;	
	}
	return (yaml_char_t*)tag;
}

static VALUE
rb_yaml_document_add_scalar(VALUE self, SEL sel, VALUE taguri, VALUE str, VALUE style)
{
	yaml_document_t *document = (yaml_document_t*)DATA_PTR(self);
	// TODO: stop ignoring the style
	// yaml_char_t *tag = (yaml_char_t*)RSTRING_PTR(taguri);
	yaml_char_t *val = (yaml_char_t*)RSTRING_PTR(str);
	int scalID = yaml_document_add_scalar(document, rb_yaml_tag_or_null(taguri), val, RSTRING_LEN(str), rb_symbol_to_scalar_style(style));
	if (scalID == 0)
	{
		rb_exc_raise(rb_yaml_parser_error(self, sel));
	}
	return rb_yaml_node_new(yaml_document_get_node(document, scalID), scalID, self);
}

static VALUE
rb_yaml_document_root_node(VALUE self, SEL sel)
{
	yaml_document_t *document;
	Data_Get_Struct(self, yaml_document_t, document);
	yaml_node_t *node = yaml_document_get_root_node(document);
	if(node == NULL)
	{
		return Qnil;
	}
	return rb_yaml_node_new(node, 0, self);
}

static VALUE
rb_yaml_document_empty_p(VALUE self, SEL sel)
{
	yaml_document_t *document;
	Data_Get_Struct(self, yaml_document_t, document);
	return (yaml_document_get_root_node(document) == NULL) ? Qtrue : Qfalse;
}

static VALUE
rb_yaml_document_implicit_start_p(VALUE self, SEL sel)
{
	yaml_document_t *document;
	Data_Get_Struct(self, yaml_document_t, document);
	assert(document != NULL);
	return (document->start_implicit) ? Qtrue : Qfalse;
}

static VALUE
rb_yaml_document_implicit_end_p(VALUE self, SEL sel)
{
	yaml_document_t *document;
	Data_Get_Struct(self, yaml_document_t, document);
	assert(document != NULL);
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
		klass = rb_cScalarNode;
		break;
		
		case YAML_MAPPING_NODE:
		klass = rb_cMapNode;
		break;
		
		case YAML_SEQUENCE_NODE:
		klass = rb_cSeqNode;
		break;
		
		case YAML_NO_NODE:
		rb_raise(rb_eRuntimeError, "unexpected empty node");
	}
	VALUE n = Data_Wrap_Struct(klass, NULL, NULL, node);
	rb_ivar_set(n, id_node_id_ivar, INT2FIX(id));
	rb_ivar_set(n, id_document_ivar, document);
	return n;
}

static VALUE
rb_sequence_node_add(VALUE self, SEL sel, VALUE obj)
{
	VALUE doc = rb_ivar_get(self, id_document_ivar);
	yaml_document_t *document;
	Data_Get_Struct(doc, yaml_document_t, document);
	assert(document != NULL);
	VALUE scalar_node = rb_funcall(obj, rb_intern("to_yaml"), 1, doc);
	int seqID = FIX2INT(rb_ivar_get(self, id_node_id_ivar));
	int scalID = FIX2INT(rb_ivar_get(scalar_node, id_node_id_ivar));
	assert((seqID != 0) && (scalID != 0));
	if (yaml_document_append_sequence_item(document, seqID, scalID) == 0)
	{
		rb_exc_raise(rb_yaml_parser_error(self, sel));
	}
	return self;
}

static VALUE
rb_mapping_node_add(VALUE self, SEL sel, VALUE key, VALUE val)
{
	VALUE doc = rb_ivar_get(self, id_document_ivar);
	yaml_document_t *document;
	Data_Get_Struct(doc, yaml_document_t, document);
	VALUE key_node = rb_funcall(key, rb_intern("to_yaml"), 1, doc);
	VALUE val_node = rb_funcall(val, rb_intern("to_yaml"), 1, doc);
	int myID = FIX2INT(rb_ivar_get(self, id_node_id_ivar));
	int keyID = FIX2INT(rb_ivar_get(key_node, id_node_id_ivar));
	int valID = FIX2INT(rb_ivar_get(val_node, id_node_id_ivar));
	assert((myID != 0) && (keyID != 0) && (valID != 0));
	if(yaml_document_append_mapping_pair(document, myID, keyID, valID) == 0)
	{
		rb_exc_raise(rb_yaml_parser_error(self, sel));
	}
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

static VALUE
rb_yaml_resolver_initialize(VALUE self, SEL sel)
{
	rb_ivar_set(self, id_tags_ivar, rb_hash_new());
	return self;
}

static VALUE
rb_yaml_resolve_node(yaml_node_t *node, yaml_document_t *document, VALUE tags)
{
	VALUE tag = rb_str_new2((const char*)node->tag);
	VALUE handler = rb_hash_lookup(tags, tag);
	switch(node->type)
	{
		case YAML_SCALAR_NODE:
		{
			if (node->data.scalar.style == YAML_PLAIN_SCALAR_STYLE)
			{
				rb_yaml_guess_type_of_plain_node(node);
				tag = rb_str_new2((const char*)node->tag);
				handler = rb_hash_lookup(tags, tag);
			}
			VALUE scalarval = rb_str_new((const char*)node->data.scalar.value, node->data.scalar.length);
			if (rb_respond_to(handler, rb_intern("call")))
			{
				return rb_funcall(handler, rb_intern("call"), 1, scalarval);
			}
			else if (rb_respond_to(handler, rb_intern("yaml_new")))
			{
				return rb_funcall(handler, rb_intern("yaml_new"), 1, scalarval);
			}
			return scalarval;
		}
		break;
		case YAML_SEQUENCE_NODE:
		{
			yaml_node_item_t *item;
			VALUE arr = rb_ary_new();
			for(item = node->data.sequence.items.start; item < node->data.sequence.items.top; item++)
			{
				int item_id = *item;
				yaml_node_t *subnode = yaml_document_get_node(document, item_id);
				VALUE new_obj = rb_yaml_resolve_node(subnode, document, tags);
				rb_ary_push(arr, new_obj);
			}
			if (rb_respond_to(handler, rb_intern("call")))
			{
				return rb_funcall(handler, rb_intern("call"), 1, arr);
			}
			else if (rb_respond_to(handler, rb_intern("yaml_new")))
			{
				return rb_funcall(handler, rb_intern("yaml_new"), 1, arr);
			}
			return arr;
		}
		break;
		
		case YAML_MAPPING_NODE:
		{
			yaml_node_pair_t *pair;
			VALUE hash = rb_hash_new();
			for(pair = node->data.mapping.pairs.start; pair < node->data.mapping.pairs.top; pair++)
			{
				VALUE k = rb_yaml_resolve_node(yaml_document_get_node(document, pair->key), document, tags);
				VALUE v = rb_yaml_resolve_node(yaml_document_get_node(document, pair->value), document, tags);
				rb_hash_aset(hash, k, v);
			}
			if (rb_respond_to(handler, rb_intern("call")))
			{
				return rb_funcall(handler, rb_intern("call"), 1, hash);
			}
			else if (rb_respond_to(handler, rb_intern("yaml_new")))
			{
				return rb_funcall(handler, rb_intern("yaml_new"), 1, hash);
			}
			return hash;
		}
		
		case YAML_NO_NODE:
		default:
		break;
	}
	return Qnil;
}

static VALUE
rb_yaml_resolver_transfer(VALUE self, SEL sel, VALUE obj)
{
	VALUE tags = rb_ivar_get(self, id_tags_ivar);
	if (rb_obj_is_kind_of(obj, rb_cDocument))
	{
		yaml_document_t *document;
		Data_Get_Struct(obj, yaml_document_t, document);
		yaml_node_t *root = yaml_document_get_root_node(document);
		if (root == NULL)
		{
			return Qnil;
		}
		return rb_yaml_resolve_node(root, document, tags);
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
	assert(emitter != NULL);
	rb_ivar_set(self, rb_intern("output"), output);
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
rb_yaml_emitter_dump(VALUE self, SEL sel, VALUE doc)
{
	yaml_emitter_t *emitter;
	yaml_document_t *document;
	Data_Get_Struct(self, yaml_emitter_t, emitter);
	Data_Get_Struct(doc, yaml_document_t, document);
	assert(emitter != NULL);
	assert(document != NULL);
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
	id_plain = rb_intern("plain");
	id_quote2 = rb_intern("quote2");
	id_tags_ivar = rb_intern("@tags");
	id_input_ivar = rb_intern("@input");
	id_node_id_ivar = rb_intern("@node_id");
	id_document_ivar = rb_intern("@document");
	
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
	//rb_objc_define_method(rb_cParser, "error", rb_yaml_parser_error, 0);
	rb_objc_define_method(rb_cParser, "load", rb_yaml_parser_load, 0);
	rb_yaml_parser_finalize_super = rb_objc_install_method2((Class)rb_cParser, "finalize", (IMP)rb_yaml_parser_finalize);
	
	rb_cDocument = rb_define_class_under(rb_mLibYAML, "Document", rb_cObject);
	rb_objc_define_method(*(VALUE *)rb_cDocument, "alloc", rb_yaml_document_alloc, 0);
	rb_objc_define_method(rb_cDocument, "root", rb_yaml_document_root_node, 0);
	rb_objc_define_method(rb_cDocument, "seq", rb_yaml_document_add_sequence, 2);
	rb_objc_define_method(rb_cDocument, "map", rb_yaml_document_add_mapping, 2);
	rb_objc_define_method(rb_cDocument, "scalar", rb_yaml_document_add_scalar, 3);
	//rb_objc_define_method(rb_cDocument, "[]", rb_yaml_document_aref, 1);
	//rb_objc_define_method(rb_cDocument, "version", rb_yaml_document_version, 0);
	rb_objc_define_method(rb_cDocument, "empty?", rb_yaml_document_empty_p, 0);
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
	rb_objc_define_method(rb_cSeqNode, "add", rb_sequence_node_add, 1);
	
	rb_cMapNode = rb_define_class_under(rb_mLibYAML, "Map", rb_cNode);
	rb_objc_define_method(rb_cMapNode, "add", rb_mapping_node_add, 2);
	
	rb_cScalarNode = rb_define_class_under(rb_mLibYAML, "Scalar", rb_cNode);
	
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