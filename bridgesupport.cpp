/*
 * MacRuby BridgeSupport implementation.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2007-2011, Apple Inc. All rights reserved.
 */

#if MACRUBY_STATIC
# include <vector>
# include <map>
# include <string>
#else
# include <llvm/Module.h>
# include <llvm/DerivedTypes.h>
# include <llvm/Constants.h>
# include <llvm/CallingConv.h>
# include <llvm/Instructions.h>
# include <llvm/Intrinsics.h>
# include <llvm/Analysis/DebugInfo.h>
# if !defined(LLVM_TOT)
#  include <llvm/Analysis/DIBuilder.h>
# endif
# include <llvm/ExecutionEngine/JIT.h>
# include <llvm/PassManager.h>
# include <llvm/Target/TargetData.h>
using namespace llvm;
#endif

#include "macruby_internal.h"
#include "ruby/node.h"
#include "vm.h"
#include "compiler.h"
#include "bridgesupport.h"
#include "objc.h"
#include "class.h"

#include <execinfo.h>
#include <dlfcn.h>
#include <sys/stat.h>

static ID boxed_ivar_type = 0;
static VALUE bs_const_magic_cookie = Qnil;

VALUE rb_cBoxed;
VALUE rb_cPointer;

typedef struct rb_vm_pointer {
    VALUE type;
    size_t type_size;
    VALUE (*convert_to_rval)(void *);
    void (*convert_to_ocval)(VALUE rval, void *);
    void *val;
    size_t len; // if 0, we don't know...
} rb_vm_pointer_t;

static inline ID
generate_const_name(char *name)
{
    ID id;
    if (islower(name[0])) {
	name[0] = toupper(name[0]);
	id = rb_intern(name);
	name[0] = tolower(name[0]);
	return id;
    }
    else {
	return rb_intern(name);
    }
}
extern "C"
VALUE
rb_vm_resolve_const_value(VALUE v, VALUE klass, ID id)
{
    if (v == bs_const_magic_cookie) {
	bs_element_constant_t *bs_const = GET_CORE()->find_bs_const(id);
	if (bs_const == NULL) {
	    rb_bug("unresolved BridgeSupport constant `%s'",
		    rb_id2name(id));
	}

	void *sym = dlsym(RTLD_DEFAULT, bs_const->name);
	if (sym == NULL) {
	    // The symbol can't be located, it's probably because the current
	    // program links against a different version of the framework.
	    rb_raise(rb_eRuntimeError, "can't locate symbol for constant %s",
		rb_id2name(id));
	}

	if (bs_const->magic_cookie) {
	    // Constant is a magic cookie. We don't support these yet.
	    rb_raise(rb_eRuntimeError,
		    "magic-cookie constant %s is not supported yet",
		    rb_id2name(id));
	}

	void *convertor = GET_CORE()->gen_to_rval_convertor(bs_const->type);
	v = ((VALUE (*)(void *))convertor)(sym);

	CFMutableDictionaryRef iv_dict = rb_class_ivar_dict(rb_cObject);
	assert(iv_dict != NULL);
	CFDictionarySetValue(iv_dict, (const void *)id, (const void *)v);
    }
    return v;
}

bs_element_constant_t *
RoxorCore::find_bs_const(ID name)
{
    std::map<ID, bs_element_constant_t *>::iterator iter =
	bs_consts.find(name);

    return iter == bs_consts.end() ? NULL : iter->second;
}

bs_element_method_t *
RoxorCore::find_bs_method(Class klass, SEL sel)
{
    std::map<std::string, std::map<SEL, bs_element_method_t *> *> &map =
	class_isMetaClass(klass) ? bs_classes_class_methods
	: bs_classes_instance_methods;

    do {
	std::map<std::string,
		 std::map<SEL, bs_element_method_t *> *>::iterator iter =
		     map.find(class_getName(klass));

	if (iter != map.end()) {
	    std::map<SEL, bs_element_method_t *> *map2 = iter->second;
	    std::map<SEL, bs_element_method_t *>::iterator iter2 =
		map2->find(sel);

	    if (iter2 != map2->end()) {
		return iter2->second;
	    }
	}

	klass = class_getSuperclass(klass);
    }
    while (klass != NULL);

    return NULL;
}

rb_vm_bs_boxed_t *
RoxorCore::find_bs_boxed(std::string type)
{
    std::map<std::string, rb_vm_bs_boxed_t *>::iterator iter =
	bs_boxed.find(type);

    if (iter == bs_boxed.end()) {
	return NULL;
    }

    return iter->second;
}

rb_vm_bs_boxed_t *
RoxorCore::find_bs_struct(std::string type)
{
    rb_vm_bs_boxed_t *boxed = find_bs_boxed(type);
    if (boxed != NULL) {
	if (boxed->is_struct()) {
	    return boxed;
	}
	return NULL;
    }

#if MACRUBY_STATIC
    return NULL;
#else
    // Given structure type does not exist... but it may be an anonymous
    // type (like {?=qq}) which is sometimes present in BridgeSupport files...
    return register_anonymous_bs_struct(type.c_str());
#endif
}

rb_vm_bs_boxed_t *
RoxorCore::find_bs_opaque(std::string type)
{
    rb_vm_bs_boxed_t *boxed = find_bs_boxed(type);
    return boxed == NULL ? NULL : boxed->is_struct() ? NULL : boxed;
}

bs_element_cftype_t *
RoxorCore::find_bs_cftype(std::string type)
{
    std::map<std::string, bs_element_cftype_t *>::iterator iter =
	bs_cftypes.find(type);

    return iter == bs_cftypes.end() ? NULL : iter->second;
}

std::string *
RoxorCore::find_bs_informal_protocol_method(SEL sel, bool class_method)
{
    std::map<SEL, std::string *> &map = class_method
	? bs_informal_protocol_cmethods : bs_informal_protocol_imethods;

    std::map<SEL, std::string *>::iterator iter = map.find(sel);

    return iter == map.end() ? NULL : iter->second;
}

bs_element_function_t *
RoxorCore::find_bs_function(std::string &name)
{
    std::map<std::string, bs_element_function_t *>::iterator iter =
	bs_funcs.find(name);

    return iter == bs_funcs.end() ? NULL : iter->second;
}

static rb_vm_bs_boxed_t *
locate_bs_boxed(VALUE klass, const bool struct_only=false)
{
    VALUE type = rb_attr_get(klass, boxed_ivar_type);
    assert(type != Qnil);
    rb_vm_bs_boxed_t *bs_boxed = GET_CORE()->find_bs_boxed(RSTRING_PTR(type));
    assert(bs_boxed != NULL);
    if (struct_only) {
	assert(bs_boxed->is_struct());
    }
    return bs_boxed;
}

#if !defined(MACRUBY_STATIC)
extern "C"
void
rb_vm_check_arity(int given, int requested)
{
    if (given != requested) {
	rb_raise(rb_eArgError, "wrong number of arguments (%d for %d)",
		given, requested);
    }
}

void
RoxorCompiler::compile_check_arity(Value *given, Value *requested)
{
    if (checkArityFunc == NULL) {
	// void rb_vm_check_arity(int given, int requested);
	checkArityFunc = cast<Function>(module->getOrInsertFunction(
		    "rb_vm_check_arity",
		    VoidTy, Int32Ty, Int32Ty, NULL));
    }

    Value *args[] = {
	given,
	requested
    };
    compile_protected_call(checkArityFunc, args, args + 2);
}

extern "C"
void
rb_vm_set_struct(VALUE rcv, int field, VALUE val)
{
    VALUE *data;
    Data_Get_Struct(rcv, VALUE, data);
    GC_WB(&data[field], val);
}

void
RoxorCompiler::compile_set_struct(Value *rcv, int field, Value *val)
{
    if (setStructFunc == NULL) {
	// void rb_vm_set_struct(VALUE rcv, int field, VALUE val);
	setStructFunc = cast<Function>(module->getOrInsertFunction(
		    "rb_vm_set_struct",
		    VoidTy, RubyObjTy, Int32Ty, RubyObjTy, NULL));
    }

    Value *args[] = {
	rcv,
	ConstantInt::get(Int32Ty, field),
	val
    };
    CallInst::Create(setStructFunc, args, args + 3, "", bb);
}

Function *
RoxorCompiler::compile_bs_struct_writer(rb_vm_bs_boxed_t *bs_boxed, int field)
{
    // VALUE foo(VALUE self, SEL sel, VALUE val);
    Function *f = cast<Function>(module->getOrInsertFunction("",
		RubyObjTy, RubyObjTy, PtrTy, RubyObjTy, NULL));
    Function::arg_iterator arg = f->arg_begin();
    Value *self = arg++; 	// self
    arg++;			// sel
    Value *val = arg++; 	// val

    bb = BasicBlock::Create(context, "EntryBlock", f);

    assert((unsigned)field < bs_boxed->as.s->fields_count);
    const char *ftype = bs_boxed->as.s->fields[field].type;
    const Type *llvm_type = convert_type(ftype);

    Value *fval = new AllocaInst(llvm_type, "", bb);
    val = compile_conversion_to_c(ftype, val, fval);
    val = compile_conversion_to_ruby(ftype, llvm_type, val);

    compile_set_struct(self, field, val);

    ReturnInst::Create(context, val, bb);

    return f;
}

Function *
RoxorCompiler::compile_bs_struct_new(rb_vm_bs_boxed_t *bs_boxed)
{
    // VALUE foo(VALUE self, SEL sel, int argc, VALUE *argv);
    Function *f = cast<Function>(module->getOrInsertFunction("",
		RubyObjTy, RubyObjTy, PtrTy, Int32Ty, RubyObjPtrTy,
		NULL));
    Function::arg_iterator arg = f->arg_begin();
    Value *klass = arg++; 	// self
    arg++;			// sel
    Value *argc = arg++; 	// argc
    Value *argv = arg++; 	// argv

    bb = BasicBlock::Create(context, "EntryBlock", f);

    BasicBlock *no_args_bb = BasicBlock::Create(context, "no_args", f);
    BasicBlock *args_bb  = BasicBlock::Create(context, "args", f);
    Value *has_args = new ICmpInst(*bb, ICmpInst::ICMP_EQ, argc,
	    ConstantInt::get(Int32Ty, 0));

    BranchInst::Create(no_args_bb, args_bb, has_args, bb);

    // No arguments are given, let's create Ruby field objects based on a
    // zero-filled memory slot.
    bb = no_args_bb;
    std::vector<Value *> fields;

    for (unsigned i = 0; i < bs_boxed->as.s->fields_count; i++) {
	const char *ftype = bs_boxed->as.s->fields[i].type;
	const Type *llvm_type = convert_type(ftype);
	Value *fval = new AllocaInst(llvm_type, "", bb);

	const size_t type_size = GET_CORE()->get_sizeof(llvm_type);

	Value *args[] = {
	    new BitCastInst(fval, PtrTy, "", bb),  	// start
	    ConstantInt::get(Int8Ty, 0),		// value
	    ConstantInt::get(IntTy, type_size),		// size
	    ConstantInt::get(Int32Ty, 0),		// align
	    ConstantInt::get(Int1Ty, 0)			// volatile
	};
	const Type *Tys[] = { args[0]->getType(), args[2]->getType() };
	Function *memset_func = Intrinsic::getDeclaration(module,
		Intrinsic::memset, Tys, 2);
	assert(memset_func != NULL);
	CallInst::Create(memset_func, args, args + 5, "", bb);

	fval = new LoadInst(fval, "", bb);
	fval = compile_conversion_to_ruby(ftype, llvm_type, fval);

	fields.push_back(fval);
    }

    ReturnInst::Create(context, compile_new_struct(klass, fields), bb);

    // Arguments are given. Need to check given arity, then convert the given
    // Ruby values into the requested struct field types.
    bb = args_bb;
    fields.clear();

    compile_check_arity(argc,
	    ConstantInt::get(Int32Ty, bs_boxed->as.s->fields_count));

    for (unsigned i = 0; i < bs_boxed->as.s->fields_count; i++) {
	const char *ftype = bs_boxed->as.s->fields[i].type;
	const Type *llvm_type = convert_type(ftype);
	Value *fval = new AllocaInst(llvm_type, "", bb);

	Value *index = ConstantInt::get(Int32Ty, i);
	Value *arg = GetElementPtrInst::Create(argv, index, "", bb);
	arg = new LoadInst(arg, "", bb);
	compile_conversion_to_c(ftype, arg, fval);
	arg = new LoadInst(fval, "", bb);
	arg = compile_conversion_to_ruby(ftype, llvm_type, arg);

	fields.push_back(arg);
    }

    ReturnInst::Create(context, compile_new_struct(klass, fields), bb);

    return f;
}

static VALUE
rb_vm_struct_fake_new(VALUE rcv, SEL sel, int argc, VALUE *argv)
{
    // Generate the real #new method.
    rb_vm_bs_boxed_t *bs_boxed = locate_bs_boxed(rcv, true);
    GET_CORE()->lock();
    Function *f = RoxorCompiler::shared->compile_bs_struct_new(bs_boxed);
    IMP imp = GET_CORE()->compile(f);
    GET_CORE()->unlock();

    // Replace the fake method with the new one in the runtime.
    rb_objc_define_method(*(VALUE *)rcv, "new", (void *)imp, -1);

    // Call the new method.
    return ((VALUE (*)(VALUE, SEL, int, VALUE *))imp)(rcv, sel, argc, argv);
}

static VALUE
rb_vm_struct_fake_set(VALUE rcv, SEL sel, VALUE val)
{
    // Locate the given field.
    char buf[100];
    const char *selname = sel_getName(sel);
    size_t s = strlcpy(buf, selname, sizeof buf);
    if (buf[s - 1] == ':') {
	s--;
    }
    assert(buf[s - 1] == '=');
    buf[s - 1] = '\0';
    rb_vm_bs_boxed_t *bs_boxed = locate_bs_boxed(CLASS_OF(rcv), true);
    int field = -1;
    for (unsigned i = 0; i < bs_boxed->as.s->fields_count; i++) {
	const char *fname = bs_boxed->as.s->fields[i].name;
	if (strcmp(fname, buf) == 0) {
	    field = i;
	    break;
	}
    }
    assert(field != -1);

    // Generate the new setter method.
    GET_CORE()->lock();
    Function *f = RoxorCompiler::shared->compile_bs_struct_writer(
	    bs_boxed, field);
    IMP imp = GET_CORE()->compile(f);
    GET_CORE()->unlock();

    // Replace the fake method with the new one in the runtime.
    buf[s - 1] = '=';
    buf[s] = '\0';
    rb_objc_define_method(*(VALUE *)rcv, buf, (void *)imp, 1);

    // Call the new method.
    return ((VALUE (*)(VALUE, SEL, VALUE))imp)(rcv, sel, val);
}

// Readers are statically generated.
#include "bs_struct_readers.c"

static VALUE
rb_vm_boxed_equal(VALUE rcv, SEL sel, VALUE val)
{
    if (rcv == val) {
	return Qtrue;
    }
    VALUE klass = CLASS_OF(rcv);
    if (!rb_obj_is_kind_of(val, klass)) {
	return Qfalse;
    }

    rb_vm_bs_boxed_t *bs_boxed = locate_bs_boxed(klass);

    VALUE *rcv_data; Data_Get_Struct(rcv, VALUE, rcv_data);
    VALUE *val_data; Data_Get_Struct(val, VALUE, val_data);

    if (bs_boxed->bs_type == BS_ELEMENT_STRUCT) {
	for (unsigned i = 0; i < bs_boxed->as.s->fields_count; i++) {
	    if (!rb_equal(rcv_data[i], val_data[i])) {
		return Qfalse;
	    }
	}
	return Qtrue;
    }

    return rcv_data == val_data ? Qtrue : Qfalse;
}

static VALUE
rb_vm_struct_inspect(VALUE rcv, SEL sel)
{
    VALUE str = rb_str_new2("#<");
    rb_str_cat2(str, rb_obj_classname(rcv));

    VALUE *rcv_data;
    Data_Get_Struct(rcv, VALUE, rcv_data);
    rb_vm_bs_boxed_t *bs_boxed = locate_bs_boxed(CLASS_OF(rcv), true);

    for (unsigned i = 0; i < bs_boxed->as.s->fields_count; i++) {
	rb_str_cat2(str, " ");
	rb_str_cat2(str, bs_boxed->as.s->fields[i].name);
	rb_str_cat2(str, "=");
	rb_str_append(str, rb_inspect(rcv_data[i]));
    }

    rb_str_cat2(str, ">");

    return str;
}

static VALUE
rb_vm_struct_to_a(VALUE rcv, SEL sel)
{
    VALUE ary = rb_ary_new();

    VALUE *rcv_data;
    Data_Get_Struct(rcv, VALUE, rcv_data);
    rb_vm_bs_boxed_t *bs_boxed = locate_bs_boxed(CLASS_OF(rcv), true);

    for (unsigned i = 0; i < bs_boxed->as.s->fields_count; i++) {
	rb_ary_push(ary, rcv_data[i]);
    }

    return ary;
}

static VALUE
rb_vm_struct_aref(VALUE rcv, SEL sel, VALUE index)
{
    const long idx = NUM2LONG(index);

    rb_vm_bs_boxed_t *bs_boxed = locate_bs_boxed(CLASS_OF(rcv), true);
    if (idx < 0 || (unsigned long)idx >= bs_boxed->as.s->fields_count) {
	rb_raise(rb_eArgError, "given index %ld out of bounds", idx);
    }

    VALUE *rcv_data;
    Data_Get_Struct(rcv, VALUE, rcv_data);

    return rcv_data[idx];
}

static VALUE
rb_vm_struct_aset(VALUE rcv, SEL sel, VALUE index, VALUE val)
{
    const long idx = NUM2LONG(index);

    rb_vm_bs_boxed_t *bs_boxed = locate_bs_boxed(CLASS_OF(rcv), true);
    if (idx < 0 || (unsigned long)idx >= bs_boxed->as.s->fields_count) {
	rb_raise(rb_eArgError, "given index %ld out of bounds", idx);
    }

    char buf[100];
    snprintf(buf, sizeof buf, "%s=:", bs_boxed->as.s->fields[idx].name);

    return rb_vm_call(rcv, sel_registerName(buf), 1, &val);
}

static VALUE
rb_vm_struct_dup(VALUE rcv, SEL sel)
{
    VALUE klass = CLASS_OF(rcv);
    rb_vm_bs_boxed_t *bs_boxed = locate_bs_boxed(klass, true);

    VALUE *rcv_data;
    Data_Get_Struct(rcv, VALUE, rcv_data);
    VALUE *new_data = (VALUE *)xmalloc(
	    bs_boxed->as.s->fields_count * sizeof(VALUE));
    for (unsigned i = 0; i < bs_boxed->as.s->fields_count; i++) {
	VALUE field = rcv_data[i];
	// Numeric values cannot be duplicated.
	if (!rb_obj_is_kind_of(field, rb_cNumeric)) {
	    field = rb_send_dup(field);
	}
	GC_WB(&new_data[i], field);
    }

    return Data_Wrap_Struct(klass, NULL, NULL, new_data);
}

static VALUE
rb_boxed_fields(VALUE rcv, SEL sel)
{
    rb_vm_bs_boxed_t *bs_boxed = locate_bs_boxed(rcv);
    VALUE ary = rb_ary_new();
    if (bs_boxed->bs_type == BS_ELEMENT_STRUCT) {
	for (unsigned i = 0; i < bs_boxed->as.s->fields_count; i++) {
	    VALUE field = ID2SYM(rb_intern(bs_boxed->as.s->fields[i].name));
	    rb_ary_push(ary, field);
	}
    }
    return ary;
}

static VALUE
rb_boxed_size(VALUE rcv, SEL sel)
{
    rb_vm_bs_boxed_t *bs_boxed = locate_bs_boxed(rcv);
    if (bs_boxed->bs_type == BS_ELEMENT_STRUCT) {
	return LONG2NUM(GET_CORE()->get_sizeof(bs_boxed->as.s->type));
    }
    return Qnil;
}

static VALUE
rb_vm_opaque_new(VALUE rcv, SEL sel)
{
    // XXX instead of doing this, we should perhaps simply delete the new
    // method on the class...
    rb_raise(rb_eRuntimeError, "can't allocate opaque type `%s'",
	    rb_class2name(rcv));
}

bool
RoxorCore::register_bs_boxed(bs_element_type_t type, void *value)
{
    std::string octype(((bs_element_opaque_t *)value)->type);

    std::map<std::string, rb_vm_bs_boxed_t *>::iterator iter =
	bs_boxed.find(octype);

    if (iter != bs_boxed.end()) {
	// A boxed class of this type already exists, so let's create an
	// alias to it.
	rb_vm_bs_boxed_t *boxed = iter->second;
	const ID name = rb_intern(((bs_element_opaque_t *)value)->name);
	rb_const_set(rb_cObject, name, boxed->klass);
	return false;
    }

    rb_vm_bs_boxed_t *boxed = (rb_vm_bs_boxed_t *)malloc(
	    sizeof(rb_vm_bs_boxed_t));
    assert(boxed != NULL);

    boxed->bs_type = type;
    boxed->as.v = value;
    boxed->type = NULL; // lazy
    boxed->klass = rb_define_class(((bs_element_opaque_t *)value)->name,
	    rb_cBoxed);

    rb_ivar_set(boxed->klass, boxed_ivar_type, rb_str_new2(octype.c_str()));

    if (type == BS_ELEMENT_STRUCT && !boxed->as.s->opaque) {
	// Define the fake #new method.
	rb_objc_define_method(*(VALUE *)boxed->klass, "new",
		(void *)rb_vm_struct_fake_new, -1);

	// Define accessors.
	assert(boxed->as.s->fields_count <= BS_STRUCT_MAX_FIELDS);
	for (unsigned i = 0; i < boxed->as.s->fields_count; i++) {
	    // Readers.
	    rb_objc_define_method(boxed->klass, boxed->as.s->fields[i].name,
		    (void *)struct_readers[i], 0);
	    // Writers.
	    char buf[100];
	    snprintf(buf, sizeof buf, "%s=", boxed->as.s->fields[i].name);
	    rb_objc_define_method(boxed->klass, buf,
		    (void *)rb_vm_struct_fake_set, 1);
	}

	// Define other utility methods.
	rb_objc_define_method(*(VALUE *)boxed->klass, "fields",
		(void *)rb_boxed_fields, 0);
	rb_objc_define_method(*(VALUE *)boxed->klass, "size",
		(void *)rb_boxed_size, 0);
	rb_objc_define_method(boxed->klass, "dup",
		(void *)rb_vm_struct_dup, 0);
	rb_objc_define_method(boxed->klass, "clone",
		(void *)rb_vm_struct_dup, 0);
	rb_objc_define_method(boxed->klass, "inspect",
		(void *)rb_vm_struct_inspect, 0);
	rb_objc_define_method(boxed->klass, "to_a",
		(void *)rb_vm_struct_to_a, 0);
	rb_objc_define_method(boxed->klass, "[]",
		(void *)rb_vm_struct_aref, 1);
	rb_objc_define_method(boxed->klass, "[]=",
		(void *)rb_vm_struct_aset, 2);
    }
    else {
	// Opaque methods.
	rb_objc_define_method(*(VALUE *)boxed->klass, "new",
		(void *)rb_vm_opaque_new, -1);
    }
    // Common methods.
    rb_objc_define_method(boxed->klass, "==", (void *)rb_vm_boxed_equal, 1);

    bs_boxed[octype] = boxed;

    return true;
}

rb_vm_bs_boxed_t *
RoxorCore::register_anonymous_bs_struct(const char *type)
{
    const size_t type_len = strlen(type);
    assert(type_len > 0);

    if (type_len < 3 || type[0] != _C_STRUCT_B || type[1] != '?'
	    || type[2] != '=') {
	// Does not look like an anonymous struct...
	return NULL;
    }

    // Prepare the list of field types.
    const size_t buf_len = type_len + 1;
    char *buf = (char *)malloc(buf_len);
    assert(buf != NULL);
    std::vector<std::string> s_types;
    const char *p = &type[3];
    while (*p != _C_STRUCT_E) {
	p = GetFirstType(p, buf, buf_len);
	assert(*p != '\0');
	s_types.push_back(buf);
    }
    free(buf);

    // Prepare the BridgeSupport structure.
    bs_element_struct_t *bs_struct = (bs_element_struct_t *)
	malloc(sizeof(bs_element_struct_t));
    assert(bs_struct != NULL);
    bs_struct->name = (char *)"?";
    bs_struct->type = strdup(type);
    bs_struct->fields_count = s_types.size();
    assert(bs_struct->fields_count > 0);
    bs_struct->fields = (bs_element_struct_field_t *)
	malloc(sizeof(bs_element_struct_field_t) * bs_struct->fields_count);
    assert(bs_struct->fields != NULL);
    for (unsigned i = 0; i < bs_struct->fields_count; i++) {
	bs_element_struct_field_t *field = &bs_struct->fields[i];
	field->name = (char *)"?";
	field->type = strdup(s_types.at(i).c_str());
    }
    bs_struct->opaque = false;

    // Prepare the boxed structure.
    rb_vm_bs_boxed_t *boxed = (rb_vm_bs_boxed_t *)
	malloc(sizeof(rb_vm_bs_boxed_t));
    assert(boxed != NULL);
    boxed->bs_type = BS_ELEMENT_STRUCT;
    boxed->as.s = bs_struct;
    boxed->type = NULL; // Lazy
    boxed->klass = rb_cBoxed; // This type has no class

    // Register it to the runtime.
    bs_boxed[type] = boxed;
    return boxed;
}

static inline long
rebuild_new_struct_ary(const StructType *type, VALUE orig, VALUE new_ary)
{
    long n = 0;
    for (StructType::element_iterator iter = type->element_begin();
	    iter != type->element_end();
	    ++iter) {
	const Type *ftype = *iter;
	if (ftype->getTypeID() == Type::StructTyID) {
            const long n2 = rebuild_new_struct_ary(cast<StructType>(ftype), orig, new_ary);
            VALUE tmp = rb_ary_new();
            for (long i = 0; i < n2; i++) {
                if (RARRAY_LEN(orig) == 0) {
                    return 0;
		}
                rb_ary_push(tmp, rb_ary_shift(orig));
            }
            rb_ary_push(new_ary, tmp);
        }
        n++;
    }
    return n;
}

extern "C"
void
rb_vm_get_struct_fields(VALUE rval, VALUE *buf, rb_vm_bs_boxed_t *bs_boxed)
{
    if (TYPE(rval) == T_ARRAY) {
	unsigned n = RARRAY_LEN(rval);
	if (n < bs_boxed->as.s->fields_count) {
	    rb_raise(rb_eArgError,
		    "not enough elements in array `%s' to create " \
		    "structure `%s' (%d for %d)",
		    RSTRING_PTR(rb_inspect(rval)), bs_boxed->as.s->name, n,
		    bs_boxed->as.s->fields_count);
	}

	if (n > bs_boxed->as.s->fields_count) {
	    VALUE new_rval = rb_ary_new();
	    VALUE orig = rval;
	    rval = rb_ary_dup(rval);
	    rebuild_new_struct_ary(cast<StructType>(bs_boxed->type), rval,
		    new_rval);
	    n = RARRAY_LEN(new_rval);
	    if (RARRAY_LEN(rval) != 0 || n != bs_boxed->as.s->fields_count) {
		rb_raise(rb_eArgError,
			"too much elements in array `%s' to create " \
			"structure `%s' (%ld for %d)",
			RSTRING_PTR(rb_inspect(orig)),
			bs_boxed->as.s->name, RARRAY_LEN(orig),
			bs_boxed->as.s->fields_count);
	    }
	    rval = new_rval;
	}

	for (unsigned i = 0; i < n; i++) {
	    buf[i] = RARRAY_AT(rval, i);
	}
    }
    else if (CLASS_OF(rval) == rb_cRange &&
		(strcmp(bs_boxed->as.s->name, "NSRange") == 0 ||
		    strcmp(bs_boxed->as.s->name, "CFRange") == 0)) {
	VALUE b, e;
	int exclusive;
	rb_range_values(rval, &b, &e, &exclusive);
	int begin = NUM2INT(b);
	int end = NUM2INT(e);
	if (begin < 0 || end < 0) {
	    // We don't know what the max length of the range will be, so we
	    // can't count backwards.
	    rb_raise(rb_eArgError,
		    "negative values are not allowed in ranges " \
		    "that are converted to %s structures: `%s'. " \
        "Use `Range#relative_to(max)' to expand them.",
		    bs_boxed->as.s->name,
		    RSTRING_PTR(rb_inspect(rval)));
	}
	int length = exclusive ? end-begin : end-begin+1;
	buf[0] = INT2NUM(begin);
	buf[1] = INT2NUM(length);
    }
    else {
	if (!rb_obj_is_kind_of(rval, bs_boxed->klass)) {
	    rb_raise(rb_eTypeError,
		    "expected instance of `%s', got `%s' (%s)",
		    rb_class2name(bs_boxed->klass),
		    RSTRING_PTR(rb_inspect(rval)),
		    rb_obj_classname(rval));
	}

	if (bs_boxed->klass == rb_cBoxed) {
	    // An anonymous type...
	    // Let's check that the given boxed object matches the types.
	    rb_vm_bs_boxed_t *rval_bs_boxed =
		locate_bs_boxed(CLASS_OF(rval), true);
	    assert(rval_bs_boxed != NULL);

	    if (rval_bs_boxed->as.s->fields_count
		    != bs_boxed->as.s->fields_count) {
		rb_raise(rb_eTypeError,
			"expected instance of Boxed with %d fields, got %d",
			bs_boxed->as.s->fields_count,
			rval_bs_boxed->as.s->fields_count);
	    }

	    for (unsigned i = 0; i < bs_boxed->as.s->fields_count; i++) {
		if (strcmp(bs_boxed->as.s->fields[i].type,
			    rval_bs_boxed->as.s->fields[i].type) != 0) {
		    rb_raise(rb_eTypeError,
			"field %d of given instance of `%s' does not match " \
			"the type expected (%s, got %s)",
			i,
			rb_class2name(bs_boxed->klass),
			bs_boxed->as.s->fields[i].type,
			rval_bs_boxed->as.s->fields[i].type);
		}
	    }
	}

	VALUE *data;
	Data_Get_Struct(rval, VALUE, data);

	for (unsigned i = 0; i < bs_boxed->as.s->fields_count; i++) {
	    buf[i] = data[i];
	}
    }
}

static const char *convert_ffi_type(VALUE type,
	bool raise_exception_if_unknown);

static void
rb_pointer_init_type(rb_vm_pointer_t *ptr, VALUE type)
{
    const char *type_str = StringValuePtr(type);
    // LLVM doesn't allow to get a pointer to Type::VoidTy, and for convenience
    // reasons we map a pointer to void as a pointer to unsigned char.
    if (*type_str == 'v') {
        type_str = "C";
        type = rb_str_new2(type_str);
    }
    GC_WB(&ptr->type, type);

    RoxorCore *core = GET_CORE();

    ptr->type_size = core->get_sizeof(type_str);
    assert(ptr->type_size > 0);

    ptr->convert_to_rval =
	(VALUE (*)(void *))core->gen_to_rval_convertor(type_str);
    ptr->convert_to_ocval =
	(void (*)(VALUE, void *))core->gen_to_ocval_convertor(type_str);
}

extern "C"
VALUE
rb_pointer_new(const char *type_str, void *val, size_t len)
{
    rb_vm_pointer_t *ptr = (rb_vm_pointer_t *)xmalloc(sizeof(rb_vm_pointer_t));

    rb_pointer_init_type(ptr, rb_str_new2(type_str));

    GC_WB(&ptr->val, val);
    ptr->len = len;

    return Data_Wrap_Struct(rb_cPointer, NULL, NULL, ptr);
}

static VALUE
rb_pointer_s_new(VALUE rcv, SEL sel, int argc, VALUE *argv)
{
    VALUE type, len;

    rb_scan_args(argc, argv, "11", &type, &len);

    size_t rlen = 1;
    if (!NIL_P(len)) {
	const long n = FIX2LONG(len);
	if (n <= 0) {
	    rb_raise(rb_eArgError, "given len must be greater than 0");
	}
	rlen = (size_t)n;
    }

    const char *type_str = convert_ffi_type(type, false);
    // There's no such thing as void type in ruby
    if (*type_str == 'v') {
	rb_raise(rb_eTypeError, "Void pointer is not allowed");
    }

    return rb_pointer_new(type_str,
	    xmalloc(GET_CORE()->get_sizeof(type_str) * rlen), rlen);
}

static VALUE
rb_pointer_s_magic_cookie(VALUE rcv, SEL sel, VALUE val)
{
    long magic_cookie = NUM2LONG(val);

    rb_vm_pointer_t *ptr = (rb_vm_pointer_t *)xmalloc(sizeof(rb_vm_pointer_t));
    GC_WB(&ptr->type, rb_str_new2("^v"));
    ptr->type_size = sizeof(void *);
    ptr->convert_to_rval = NULL;
    ptr->convert_to_ocval = NULL;
    ptr->val = (void *)magic_cookie;
    ptr->len = 1;

    return Data_Wrap_Struct(rb_cPointer, NULL, NULL, ptr);
}

static void
check_no_magic_cookie(rb_vm_pointer_t *ptr)
{
    if (ptr->convert_to_rval == NULL || ptr->convert_to_ocval == NULL) {
	rb_raise(rb_eArgError, "cannot access magic cookie pointers");
    }
}

static inline void *
pointer_val(rb_vm_pointer_t *ptr, VALUE idx)
{
    const long i = NUM2LONG(idx);
    if (i < 0) {
	rb_raise(rb_eArgError, "index must not be negative");
    }
    if (ptr->len > 0 && (size_t)i >= ptr->len) {
	rb_raise(rb_eArgError, "index %ld out of bounds (%ld)", i, ptr->len);
    }
    return (void *)((char *)ptr->val + (i * ptr->type_size));
}

static VALUE
rb_pointer_aref(VALUE rcv, SEL sel, VALUE idx)
{
    rb_vm_pointer_t *ptr;
    Data_Get_Struct(rcv, rb_vm_pointer_t, ptr);

    check_no_magic_cookie(ptr);
    return ptr->convert_to_rval(pointer_val(ptr, idx));
}

static VALUE
rb_pointer_value(VALUE rcv, SEL sel)
{
    return rb_pointer_aref(rcv, 0, INT2FIX(0));
}

static VALUE
rb_pointer_aset(VALUE rcv, SEL sel, VALUE idx, VALUE val)
{
    rb_vm_pointer_t *ptr;
    Data_Get_Struct(rcv, rb_vm_pointer_t, ptr);

    check_no_magic_cookie(ptr);
    ptr->convert_to_ocval(val, pointer_val(ptr, idx));
    return val;
}

static VALUE
rb_pointer_assign(VALUE rcv, SEL sel, VALUE val)
{
    return rb_pointer_aset(rcv, 0, INT2FIX(0), val);
}

static VALUE
rb_pointer_type(VALUE rcv, SEL sel)
{
    rb_vm_pointer_t *ptr;
    Data_Get_Struct(rcv, rb_vm_pointer_t, ptr);

    return ptr->type;
}

static VALUE
rb_pointer_cast(VALUE rcv, SEL sel, VALUE type)
{
    rb_vm_pointer_t *ptr;
    Data_Get_Struct(rcv, rb_vm_pointer_t, ptr);

    check_no_magic_cookie(ptr);

    const char *type_str = convert_ffi_type(type, false);
    type = rb_str_new_cstr(type_str);
    rb_pointer_init_type(ptr, type);
    return rcv;
}

static VALUE
rb_pointer_to_obj(VALUE rcv, SEL sel)
{
    rb_vm_pointer_t *ptr;
    Data_Get_Struct(rcv, rb_vm_pointer_t, ptr);

    check_no_magic_cookie(ptr);
    if (ptr->len != 0) {
	rb_raise(rb_eRuntimeError, "This method is not support to call with the object which is created by Pointer.new");
    }
    return (VALUE)ptr->val;
}

static VALUE
rb_pointer_offset(VALUE rcv, long off)
{
    rb_vm_pointer_t *ptr;
    Data_Get_Struct(rcv, rb_vm_pointer_t, ptr);

    check_no_magic_cookie(ptr);

    size_t new_len = 0;
    if (ptr->len > 0) {
	if (off > 0 && (size_t)off >= ptr->len) {
	    rb_raise(rb_eArgError, "offset %ld out of bounds (%ld)", off,
		    ptr->len);
	}
	new_len = ptr->len - off;
    }

    const char *type_str = StringValuePtr(ptr->type);
    const size_t delta = off * ptr->type_size;
    return rb_pointer_new(type_str, (char *)ptr->val + delta, new_len);
}

static VALUE
rb_pointer_plus(VALUE rcv, SEL sel, VALUE offset)
{
    return rb_pointer_offset(rcv, NUM2LONG(offset));
}

static VALUE
rb_pointer_minus(VALUE rcv, SEL sel, VALUE offset)
{
    return rb_pointer_offset(rcv, -NUM2LONG(offset));
}

static void
index_bs_class_methods(const char *name,
	std::map<std::string, std::map<SEL, bs_element_method_t *> *> &map,
	bs_element_method_t *methods,
	unsigned method_count)
{
    std::map<std::string, std::map<SEL, bs_element_method_t *> *>::iterator
	iter = map.find(name);

    std::map<SEL, bs_element_method_t *> *methods_map = NULL;
    if (iter == map.end()) {
	methods_map = new std::map<SEL, bs_element_method_t *>();
	map[name] = methods_map;
    }
    else {
	methods_map = iter->second;
    }

    for (unsigned i = 0; i < method_count; i++) {
	bs_element_method_t *m = &methods[i];
	methods_map->insert(std::make_pair(m->name, m));
    }
}

void
RoxorCore::register_bs_class(bs_element_class_t *bs_class)
{
    if (bs_class->class_methods_count > 0) {
	index_bs_class_methods(bs_class->name,
		bs_classes_class_methods,
		bs_class->class_methods,
		bs_class->class_methods_count);
    }
    if (bs_class->instance_methods_count > 0) {
	index_bs_class_methods(bs_class->name,
		bs_classes_instance_methods,
		bs_class->instance_methods,
		bs_class->instance_methods_count);
    }
}

static void
__bs_parse_cb(bs_parser_t *parser, const char *path, bs_element_type_t type,
	void *value, void *ctx)
{
    GET_CORE()->bs_parse_cb(type, value, ctx);
}

void
RoxorCore::bs_parse_cb(bs_element_type_t type, void *value, void *ctx)
{
    bool do_not_free = false;
    CFMutableDictionaryRef rb_cObject_dict = (CFMutableDictionaryRef)ctx;
    const unsigned int bs_vers = bs_parser_current_version_number(bs_parser);

    switch (type) {
	case BS_ELEMENT_ENUM:
	{
	    bs_element_enum_t *bs_enum = (bs_element_enum_t *)value;
	    ID name = generate_const_name(bs_enum->name);
	    if (!CFDictionaryGetValueIfPresent(rb_cObject_dict,
			(const void *)name, NULL)) {
#if defined(__LP64__)
		if (bs_vers < BS_VERSION_1_0) {
		    static bool R6399046_fixed = false;
		    if (!R6399046_fixed
			    && strcmp(bs_enum->name, "NSNotFound") == 0) {
			// XXX work around for
			// <rdar://problem/6399046> NSNotFound 64-bit value is incorrect
			const void *real_val = (const void *)ULL2NUM(LONG_MAX);
			CFDictionarySetValue(rb_cObject_dict,
				(const void *)name, real_val);
			R6399046_fixed = true;
			break;
		    }
		}
#endif
		VALUE val = strchr(bs_enum->value, '.') != NULL
		    ? rb_float_new(rb_cstr_to_dbl(bs_enum->value, 0))
		    : rb_cstr_to_inum(bs_enum->value, 10, 0);
		CFDictionarySetValue(rb_cObject_dict, (const void *)name,
			(const void *)val);
	    }
	    break;
	}

	case BS_ELEMENT_CONSTANT:
	{
	    bs_element_constant_t *bs_const = (bs_element_constant_t *)value;
	    ID name = generate_const_name(bs_const->name);
	    if (!CFDictionaryGetValueIfPresent(rb_cObject_dict,
			(const void *)name, NULL)) {

		bs_consts[name] = bs_const;
		CFDictionarySetValue(rb_cObject_dict, (const void *)name,
			(const void *)bs_const_magic_cookie);
		do_not_free = true;
	    }
	    break;
	}

	case BS_ELEMENT_STRING_CONSTANT:
	{
	    bs_element_string_constant_t *bs_strconst =
		(bs_element_string_constant_t *)value;
	    ID name = generate_const_name(bs_strconst->name);
	    if (!CFDictionaryGetValueIfPresent(rb_cObject_dict,
			(const void *)name, NULL)) {

		VALUE val = rb_str_new2(bs_strconst->value);
		CFDictionarySetValue(rb_cObject_dict, (const void *)name,
			(const void *)val);
	    }
	    break;
	}

	case BS_ELEMENT_FUNCTION:
	{
	    bs_element_function_t *bs_func = (bs_element_function_t *)value;
	    std::string name(bs_func->name);

	    std::map<std::string, bs_element_function_t *>::iterator iter =
		bs_funcs.find(name);
	    if (iter == bs_funcs.end()) {
		bs_funcs[name] = bs_func;
		do_not_free = true;
	    }
	    break;
	}

	case BS_ELEMENT_FUNCTION_ALIAS:
	{
#if 0 // TODO
	    bs_element_function_alias_t *bs_func_alias =
		(bs_element_function_alias_t *)value;
	    bs_element_function_t *bs_func_original;
	    if (st_lookup(bs_functions,
			(st_data_t)rb_intern(bs_func_alias->original),
			(st_data_t *)&bs_func_original)) {
		st_insert(bs_functions,
			(st_data_t)rb_intern(bs_func_alias->name),
			(st_data_t)bs_func_original);
	    }
	    else {
		rb_raise(rb_eRuntimeError,
			"cannot alias '%s' to '%s' because it doesn't exist",
			bs_func_alias->name, bs_func_alias->original);
	    }
#endif
	    break;
	}

	case BS_ELEMENT_OPAQUE:
	case BS_ELEMENT_STRUCT:
	{
	    if (register_bs_boxed(type, value)) {
		do_not_free = true;
	    }
	    break;
	}

	case BS_ELEMENT_CLASS:
	{
	    bs_element_class_t *bs_class = (bs_element_class_t *)value;
	    register_bs_class(bs_class);

	    if (bs_vers < BS_VERSION_1_0) {
		static bool R7281806_fixed = false;
		if (!R7281806_fixed
			&& strcmp(bs_class->name, "NSObject") == 0) {
		    // XXX work around for
		    // <rdar://problem/7281806> -[NSObject performSelector:] has wrong sel_of_type attributes
		    bs_element_method_t *bs_method =
			GET_CORE()->find_bs_method((Class)rb_cNSObject,
				sel_registerName("performSelector:"));
		    if (bs_method != NULL) {
			bs_element_arg_t *arg = bs_method->args;
			while (arg != NULL) {
			    if (arg->index == 0
				    && arg->sel_of_type != NULL
				    && arg->sel_of_type[0] != '@') {
				arg->sel_of_type[0] = '@';
				break;
			    }
			    arg++;
			}
			R7281806_fixed = true;
		    }
		}
	    }

	    free(bs_class);
	    do_not_free = true;
	    break;
	}

	case BS_ELEMENT_INFORMAL_PROTOCOL_METHOD:
	{
	    bs_element_informal_protocol_method_t *bs_inf_prot_method =
		(bs_element_informal_protocol_method_t *)value;

	    std::map<SEL, std::string *> &map =
		bs_inf_prot_method->class_method
		? bs_informal_protocol_cmethods
		: bs_informal_protocol_imethods;

	    char *type;
	    if (bs_vers >= BS_VERSION_1_0) {
		type = bs_inf_prot_method->type;
	    }
	    else {
#if __LP64__
		// XXX work around for
		// <rdar://problem/7318177> 64-bit informal protocol annotations are missing
		// Manually converting some 32-bit types to 64-bit...
		const size_t typelen = strlen(bs_inf_prot_method->type) + 1;
		type = (char *)alloca(typelen);
		*type = '\0';
		const char *p = bs_inf_prot_method->type;
		do {
		    const char *p2 = (char *)SkipFirstType(p);
		    size_t len = p2 - p;
		    if (*p == _C_PTR && len > 1) {
			strlcat(type, "^", typelen);
			p++;
			len--;
		    }
		    if (len == 1 && *p == _C_FLT) {
			// float -> double
			strlcat(type, "d", typelen);
		    }
		    else if (strncmp(p, "{_NSPoint=", 10) == 0) {
			strlcat(type, "{CGPoint=dd}", typelen);
		    }
		    else if (strncmp(p, "{_NSSize=", 9) == 0) {
			strlcat(type, "{CGSize=dd}", typelen);
		    }
		    else if (strncmp(p, "{_NSRect=", 9) == 0) {
			strlcat(type, "{CGRect={CGPoint=dd}{CGSize=dd}}", typelen);
		    }
		    else if (strncmp(p, "{_NSRange=", 10) == 0) {
			strlcat(type, "{_NSRange=QQ}", typelen);
		    }
		    else {
			char buf[100];
			strncpy(buf, p, len);
			buf[len] = '\0';
			strlcat(type, buf, typelen);
		    }
		    p = SkipStackSize(p2);
		}
		while (*p != '\0');
#else
		type = bs_inf_prot_method->type;
#endif
	    }
	    map[bs_inf_prot_method->name] = new std::string(type);
	    break;
	}

	case BS_ELEMENT_CFTYPE:
	{
	    bs_element_cftype_t *bs_cftype = (bs_element_cftype_t *)value;
	    assert(bs_cftype->type[0] == _C_PTR);
	    if (bs_cftype->type[1] == _C_VOID) {
		// Do not register ^v as a valid CFType.
		break;
	    }
	    std::map<std::string, bs_element_cftype_t *>::iterator
		iter = bs_cftypes.find(bs_cftype->type);
	    if (iter == bs_cftypes.end()) {
		std::string s(bs_cftype->type);
		bs_cftypes[s] = bs_cftype;
		if (s.compare(s.size() - 2, 2, "=}") == 0) {
		    // For ^{__CFError=}, also registering ^{__CFError}.
		    // This is because functions or methods returning CF types
		    // by reference strangely omit the '=' character as part
		    // of their BridgeSupport signature.
		    s.replace(s.size() - 2, 2, "}");
		    bs_cftypes[s] = bs_cftype;
		}
		do_not_free = true;
	    }
	    break;
	}
    }

    if (!do_not_free) {
	bs_element_free(type, value);
    }
}

extern "C"
void
rb_vm_load_bridge_support(const char *path, const char *framework_path,
	int options)
{
    GET_CORE()->load_bridge_support(path, framework_path, options);
}

void
RoxorCore::load_bridge_support(const char *path, const char *framework_path,
	int options)
{
    CFMutableDictionaryRef rb_cObject_dict;

    if (bs_parser == NULL) {
	bs_parser = bs_parser_new();
    }

    rb_cObject_dict = rb_class_ivar_dict(rb_cObject);
    assert(rb_cObject_dict != NULL);

    char *error = NULL;
    const bool ok = bs_parser_parse(bs_parser, path, framework_path,
	    (bs_parse_options_t)options, __bs_parse_cb, rb_cObject_dict,
	    &error);
    if (!ok) {
	rb_raise(rb_eRuntimeError, "%s", error);
    }
}

// FFI

static std::map<ID, std::string> ffi_type_shortcuts;

static void
init_ffi_type_shortcuts(void)
{
#define SHORTCUT(name, type) \
    ffi_type_shortcuts.insert(std::make_pair(rb_intern(name), type))

    // Ruby-FFI types.
    SHORTCUT("char", "c");
    SHORTCUT("uchar", "C");
    SHORTCUT("short", "s");
    SHORTCUT("ushort", "S");
    SHORTCUT("int", "i");
    SHORTCUT("uint", "I");
    SHORTCUT("long", "l");
    SHORTCUT("ulong", "L");
    SHORTCUT("long_long", "q");
    SHORTCUT("ulong_long", "Q");
    SHORTCUT("float", "f");
    SHORTCUT("double", "d");
    SHORTCUT("string", "*");
    SHORTCUT("pointer", "^");

    // MacRuby extensions.
    SHORTCUT("object", "@");
    SHORTCUT("id", "@");
    SHORTCUT("class", "#");
    SHORTCUT("boolean", "B");
    SHORTCUT("bool", "B");
    SHORTCUT("selector", ":");
    SHORTCUT("sel", ":");

#undef SHORTCUT
}

static const char *
convert_ffi_type(VALUE type, bool raise_exception_if_unknown)
{
    // Only accept symbols as type shortcuts.
    if (TYPE(type) != T_SYMBOL) {
	return StringValueCStr(type);
    }

    std::map<ID, std::string>::iterator iter =
	ffi_type_shortcuts.find(SYM2ID(type));

    if (iter == ffi_type_shortcuts.end()) {
	rb_raise(rb_eTypeError, "unrecognized symbol :%s given as FFI type",
		rb_id2name(SYM2ID(type)));
    }

    return iter->second.c_str();
}

Function *
RoxorCompiler::compile_ffi_function(void *stub, void *imp, int argc)
{
    // VALUE func(VALUE rcv, SEL sel, VALUE arg1, VALUE arg2, ...) {
    //     VALUE *argv = alloca(...);
    //     return stub(imp, argc, argv);
    // }
    std::vector<const Type *> f_types;
    f_types.push_back(RubyObjTy);
    f_types.push_back(PtrTy);
    for (int i = 0; i < argc; i++) {
	f_types.push_back(RubyObjTy);
    }
    FunctionType *ft = FunctionType::get(RubyObjTy, f_types, false);
    Function *f = cast<Function>(module->getOrInsertFunction("", ft));

    bb = BasicBlock::Create(context, "EntryBlock", f);

    Function::arg_iterator arg = f->arg_begin();
    arg++; // skip self
    arg++; // skip sel

    std::vector<Value *> params;
    std::vector<const Type *> stub_types;

    // First argument is the function implementation.
    params.push_back(compile_const_pointer(imp));
    stub_types.push_back(PtrTy);

    // Second argument is arity;
    params.push_back(ConstantInt::get(Int32Ty, argc));
    stub_types.push_back(Int32Ty);

    // Third is an array of arguments.
    Value *argv;
    if (argc == 0) {
	argv = new BitCastInst(compile_const_pointer(NULL), RubyObjPtrTy,
		"", bb);
    }
    else {
	argv = new AllocaInst(RubyObjTy, ConstantInt::get(Int32Ty, argc),
		"", bb);
	for (int i = 0; i < argc; i++) {
	    Value *index = ConstantInt::get(Int32Ty, i);
	    Value *slot = GetElementPtrInst::Create(argv, index, "", bb);
	    new StoreInst(arg++, slot, "", bb);
	}
    }
    params.push_back(argv);
    stub_types.push_back(RubyObjPtrTy);

    // Cast the given stub using the correct function signature.
    FunctionType *stub_ft = FunctionType::get(RubyObjTy, stub_types, false);
    Value *stub_val = new BitCastInst(compile_const_pointer(stub),
	    PointerType::getUnqual(stub_ft), "", bb);

    // Call the stub and return its return value.
    CallInst *stub_call = CallInst::Create(stub_val, params.begin(),
	    params.end(), "", bb);
    ReturnInst::Create(context, stub_call, bb);

    return f;
}

static VALUE
rb_ffi_attach_function(VALUE rcv, SEL sel, VALUE name, VALUE args, VALUE ret)
{
    const char *symname;
    if (TYPE(name) == T_SYMBOL) {
	symname = rb_id2name(SYM2ID(name));
    }
    else {
	StringValue(name);
	symname = RSTRING_PTR(name);
    }
    void *sym = dlsym(RTLD_DEFAULT, symname);
    if (sym == NULL) {
	rb_raise(rb_eArgError, "given function `%s' could not be located",
		symname);
    }

    std::string types;
    types.append(convert_ffi_type(ret, true));

    Check_Type(args, T_ARRAY);
    const int argc = RARRAY_LEN(args);
    for (int i = 0; i < argc; i++) {
	types.append(convert_ffi_type(RARRAY_AT(args, i), true));
    }

    rb_vm_c_stub_t *stub = (rb_vm_c_stub_t *)GET_CORE()->gen_stub(types,
	    false, argc, false);
    Function *f = RoxorCompiler::shared->compile_ffi_function((void *)stub,
	    sym, argc);
    IMP imp = GET_CORE()->compile(f);

    VALUE klass = rb_singleton_class(rcv);
    rb_objc_define_method(klass, symname, (void *)imp, argc);

    return Qnil;
}

#endif // !MACRUBY_STATIC

static VALUE
rb_boxed_objc_type(VALUE rcv, SEL sel)
{
    return rb_attr_get(rcv, boxed_ivar_type);
}

static VALUE
rb_boxed_is_opaque(VALUE rcv, SEL sel)
{
    rb_vm_bs_boxed_t *bs_boxed = locate_bs_boxed(rcv);
    return bs_boxed->bs_type == BS_ELEMENT_OPAQUE ? Qtrue : Qfalse;
}

extern "C"
void
Init_BridgeSupport(void)
{
    // Boxed
    rb_cBoxed = rb_define_class("Boxed", rb_cObject);
    rb_objc_define_method(*(VALUE *)rb_cBoxed, "type",
	    (void *)rb_boxed_objc_type, 0);
    rb_objc_define_method(*(VALUE *)rb_cBoxed, "opaque?",
	    (void *)rb_boxed_is_opaque, 0);
    boxed_ivar_type = rb_intern("__octype__");

    // Pointer
    rb_cPointer = rb_define_class("Pointer", rb_cObject);
#if !defined(MACRUBY_STATIC)
    rb_objc_define_method(*(VALUE *)rb_cPointer, "new",
	    (void *)rb_pointer_s_new, -1);
    rb_objc_define_method(*(VALUE *)rb_cPointer, "new_with_type",
	    (void *)rb_pointer_s_new, -1);
    rb_objc_define_method(*(VALUE *)rb_cPointer, "magic_cookie",
	    (void *)rb_pointer_s_magic_cookie, 1);
    rb_objc_define_method(rb_cPointer, "[]", (void *)rb_pointer_aref, 1);
    rb_objc_define_method(rb_cPointer, "[]=", (void *)rb_pointer_aset, 2);
    rb_objc_define_method(rb_cPointer, "value", (void *)rb_pointer_value, 0);
    rb_objc_define_method(rb_cPointer, "assign", (void *)rb_pointer_assign, 1);
    rb_objc_define_method(rb_cPointer, "type", (void *)rb_pointer_type, 0);
    rb_objc_define_method(rb_cPointer, "cast!", (void *)rb_pointer_cast, 1);
    rb_objc_define_method(rb_cPointer, "to_object", (void *)rb_pointer_to_obj, 0);
    rb_objc_define_method(rb_cPointer, "+", (void *)rb_pointer_plus, 1);
    rb_objc_define_method(rb_cPointer, "-", (void *)rb_pointer_minus, 1);
#endif

    bs_const_magic_cookie = rb_str_new2("bs_const_magic_cookie");
    GC_RETAIN(bs_const_magic_cookie);
}

extern "C"
void
Init_FFI(void)
{
#if !defined(MACRUBY_STATIC)
    VALUE mFFI = rb_define_module("FFI");
    VALUE mFFILib = rb_define_module_under(mFFI, "Library");
    rb_objc_define_method(mFFILib, "attach_function",
	    (void *)rb_ffi_attach_function, 3);

    init_ffi_type_shortcuts();
#endif
}

// Comparing types by making sure '=' characters are ignored, since these
// may be sometimes present, sometimes not present in signatures returned by
// the runtime or BridgeSupport files.
static bool
compare_types(const char *t1, const char *t2)
{
    while (true) {
	if (*t1 == '=') {
	    t1++;
	}
	if (*t2 == '=') {
	    t2++;
	}
	if (*t1 != *t2) {
	    return false;
	}
	if (*t1 == '\0') {
	    break;
	}
	t1++;
	t2++;
    }
    return true;
}

// Called by the kernel:

extern "C"
void *
rb_pointer_get_data(VALUE rcv, const char *type)
{
    if (!rb_obj_is_kind_of(rcv, rb_cPointer)) {
	rb_raise(rb_eTypeError,
		"expected instance of Pointer, got `%s' (%s)",
		RSTRING_PTR(rb_inspect(rcv)),
		rb_obj_classname(rcv));
    }

    rb_vm_pointer_t *ptr;
    Data_Get_Struct(rcv, rb_vm_pointer_t, ptr);
    const char *ptr_type = RSTRING_PTR(ptr->type);

    assert(type[0] == _C_PTR);
    // Sanity pointer type comparison check, unless the given pointer type
    // is 'C' (which means converted from void*) or the target argument pointer
    // type is void*.
    const char *p = &type[1];
    while (*p == _C_PTR) {
	p++;
    }
    if (*p != _C_VOID && ptr_type[0] != _C_UCHR
	    && !compare_types(ptr_type, &type[1])) {
	rb_raise(rb_eTypeError,
		"expected instance of Pointer of type `%s', got `%s'",
		type + 1,
		ptr_type);
    }

    return ptr->val;
}

extern "C"
bool
rb_boxed_is_type(VALUE klass, const char *type)
{
    VALUE rtype = rb_boxed_objc_type(klass, 0);
    if (rtype == Qnil) {
	return false;
    }
    if (strcmp(RSTRING_PTR(rtype), type) != 0) {
	rb_raise(rb_eTypeError,
		"expected instance of Boxed class of type `%s', "\
		"got `%s' of type `%s'",
		type,
		rb_class2name(klass),
		RSTRING_PTR(rtype));
    }
    return true;
}

extern "C"
VALUE
rb_pointer_new2(const char *type_str, VALUE rval)
{
#if MACRUBY_STATIC
    abort(); // TODO
#else
    VALUE p;

    if (TYPE(rval) == T_ARRAY) {
	const long len = RARRAY_LEN(rval);
	if (len == 0) {
	    rb_raise(rb_eArgError,
		    "can't convert an empty array to a `%s' pointer",
		    type_str);
	}
	p = rb_pointer_new(type_str,
		xmalloc(GET_CORE()->get_sizeof(type_str) * len), len);
	for (int i = 0; i < len; i++) {
	    rb_pointer_aset(p, 0, INT2FIX(i), RARRAY_AT(rval, i));
	}
    }
    else {
	p = rb_pointer_new(type_str,
		xmalloc(GET_CORE()->get_sizeof(type_str)), 1);
	rb_pointer_aset(p, 0, INT2FIX(0), rval);
    }

    return p;
#endif
}
