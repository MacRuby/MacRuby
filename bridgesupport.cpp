/*
 * MacRuby BridgeSupport implementation.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 * 
 * Copyright (C) 2007-2009, Apple Inc. All rights reserved.
 */

#include <llvm/Module.h>
#include <llvm/DerivedTypes.h>
#include <llvm/Constants.h>
#include <llvm/CallingConv.h>
#include <llvm/Instructions.h>
#include <llvm/ModuleProvider.h>
#include <llvm/Intrinsics.h>
#include <llvm/ExecutionEngine/JIT.h>
#include <llvm/PassManager.h>
#include <llvm/Target/TargetData.h>
using namespace llvm;

#include "ruby/ruby.h"
#include "ruby/node.h"
#include "vm.h"
#include "compiler.h"
#include "bridgesupport.h"

#include <execinfo.h>
#include <dlfcn.h>

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

static VALUE bs_const_magic_cookie = Qnil;

extern "C"
VALUE
rb_vm_resolve_const_value(VALUE v, VALUE klass, ID id)
{
    void *sym;
    bs_element_constant_t *bs_const;

    if (v == bs_const_magic_cookie) {
	std::map<ID, bs_element_constant_t *>::iterator iter =
	    GET_VM()->bs_consts.find(id);
	if (iter == GET_VM()->bs_consts.end()) {
	    rb_bug("unresolved BridgeSupport constant `%s'",
		    rb_id2name(id));
	}
	bs_const = iter->second;

	sym = dlsym(RTLD_DEFAULT, bs_const->name);
	if (sym == NULL) {
	    rb_bug("cannot locate symbol for BridgeSupport constant `%s'",
		    bs_const->name);
	}

	void *convertor = GET_VM()->gen_to_rval_convertor(bs_const->type);
	v = ((VALUE (*)(void *))convertor)(sym);

	CFMutableDictionaryRef iv_dict = rb_class_ivar_dict(rb_cObject);
	assert(iv_dict != NULL);
	CFDictionarySetValue(iv_dict, (const void *)id, (const void *)v);
    }

    return v;
}

VALUE rb_cBoxed;

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
		    Type::VoidTy, Type::Int32Ty, Type::Int32Ty, NULL));
    }

    std::vector<Value *> params;
    params.push_back(given);
    params.push_back(requested);

    compile_protected_call(checkArityFunc, params);
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
		    Type::VoidTy, RubyObjTy, Type::Int32Ty, RubyObjTy, NULL));
    }

    std::vector<Value *> params;
    params.push_back(rcv);
    params.push_back(ConstantInt::get(Type::Int32Ty, field));
    params.push_back(val);

    CallInst::Create(setStructFunc, params.begin(), params.end(), "", bb);
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

    bb = BasicBlock::Create("EntryBlock", f);

    assert((unsigned)field < bs_boxed->as.s->fields_count);
    const char *ftype = bs_boxed->as.s->fields[field].type;
    const Type *llvm_type = convert_type(ftype);

    Value *fval = new AllocaInst(llvm_type, "", bb);
    val = compile_conversion_to_c(ftype, val, fval);
    val = compile_conversion_to_ruby(ftype, llvm_type, val);

    compile_set_struct(self, field, val);

    ReturnInst::Create(val, bb);

    return f;
}

Function *
RoxorCompiler::compile_bs_struct_new(rb_vm_bs_boxed_t *bs_boxed)
{
    // VALUE foo(VALUE self, SEL sel, int argc, VALUE *argv);
    Function *f = cast<Function>(module->getOrInsertFunction("",
		RubyObjTy, RubyObjTy, PtrTy, Type::Int32Ty, RubyObjPtrTy,
		NULL));
    Function::arg_iterator arg = f->arg_begin();
    Value *klass = arg++; 	// self
    arg++;			// sel
    Value *argc = arg++; 	// argc
    Value *argv = arg++; 	// argv

    bb = BasicBlock::Create("EntryBlock", f);

    BasicBlock *no_args_bb = BasicBlock::Create("no_args", f);
    BasicBlock *args_bb  = BasicBlock::Create("args", f);
    Value *has_args = new ICmpInst(ICmpInst::ICMP_EQ, argc,
	    ConstantInt::get(Type::Int32Ty, 0), "", bb);

    BranchInst::Create(no_args_bb, args_bb, has_args, bb);

    // No arguments are given, let's create Ruby field objects based on a
    // zero-filled memory slot.
    bb = no_args_bb;
    std::vector<Value *> fields;

    for (unsigned i = 0; i < bs_boxed->as.s->fields_count; i++) {
	const char *ftype = bs_boxed->as.s->fields[i].type;
	const Type *llvm_type = convert_type(ftype);
	Value *fval = new AllocaInst(llvm_type, "", bb);

	const Type *Tys[] = { IntTy };
	Function *memset_func = Intrinsic::getDeclaration(module,
		Intrinsic::memset, Tys, 1);
	assert(memset_func != NULL);

	std::vector<Value *> params;
	params.push_back(new BitCastInst(fval, PtrTy, "", bb));
	params.push_back(ConstantInt::get(Type::Int8Ty, 0));
	params.push_back(ConstantInt::get(IntTy,
		    GET_VM()->get_sizeof(llvm_type)));
	params.push_back(ConstantInt::get(Type::Int32Ty, 0));
	CallInst::Create(memset_func, params.begin(), params.end(), "", bb);

	fval = new LoadInst(fval, "", bb);
	fval = compile_conversion_to_ruby(ftype, llvm_type, fval);

	fields.push_back(fval);
    }

    ReturnInst::Create(compile_new_struct(klass, fields), bb);

    // Arguments are given. Need to check given arity, then convert the given
    // Ruby values into the requested struct field types.
    bb = args_bb;
    fields.clear();

    compile_check_arity(argc,
	    ConstantInt::get(Type::Int32Ty, bs_boxed->as.s->fields_count));

    for (unsigned i = 0; i < bs_boxed->as.s->fields_count; i++) {
	const char *ftype = bs_boxed->as.s->fields[i].type;
	const Type *llvm_type = convert_type(ftype);
	Value *fval = new AllocaInst(llvm_type, "", bb);

	Value *index = ConstantInt::get(Type::Int32Ty, i);
	Value *arg = GetElementPtrInst::Create(argv, index, "", bb);
	arg = new LoadInst(arg, "", bb);
	arg = compile_conversion_to_c(ftype, arg, fval);
	arg = compile_conversion_to_ruby(ftype, llvm_type, arg);

	fields.push_back(arg);
    }

    ReturnInst::Create(compile_new_struct(klass, fields), bb);

    return f;
}

static ID boxed_ivar_type = 0;

static inline rb_vm_bs_boxed_t *
locate_bs_boxed(VALUE klass, const bool struct_only=false)
{
    VALUE type = rb_ivar_get(klass, boxed_ivar_type);
    assert(type != Qnil);
    rb_vm_bs_boxed_t *bs_boxed = GET_VM()->find_bs_boxed(RSTRING_PTR(type));
    assert(bs_boxed != NULL);
    if (struct_only) {
	assert(bs_boxed->is_struct());
    }
    return bs_boxed;
}

static VALUE
rb_vm_struct_fake_new(VALUE rcv, SEL sel, int argc, VALUE *argv)
{
    // Generate the real #new method.
    rb_vm_bs_boxed_t *bs_boxed = locate_bs_boxed(rcv, true);
    Function *f = RoxorCompiler::shared->compile_bs_struct_new(bs_boxed);
    IMP imp = GET_VM()->compile(f);

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
    Function *f = RoxorCompiler::shared->compile_bs_struct_writer(
	    bs_boxed, field);
    IMP imp = GET_VM()->compile(f);

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
rb_vm_opaque_new(VALUE rcv, SEL sel)
{
    // XXX instead of doing this, we should perhaps simply delete the new
    // method on the class...
    rb_raise(rb_eRuntimeError, "can't allocate opaque type `%s'",
	    rb_class2name(rcv)); 
}

static bool
register_bs_boxed(bs_element_type_t type, void *value)
{
    std::string octype(((bs_element_opaque_t *)value)->type);

    std::map<std::string, rb_vm_bs_boxed_t *>::iterator iter =
	GET_VM()->bs_boxed.find(octype);

    if (iter != GET_VM()->bs_boxed.end()) {
	// A boxed class of this type already exists, so let's create an
	// alias to it.
	rb_vm_bs_boxed_t *boxed = iter->second;
	const ID name = rb_intern(((bs_element_opaque_t *)value)->name);
	rb_const_set(rb_cObject, name, boxed->klass);
	return false;
    }

    rb_vm_bs_boxed_t *boxed = (rb_vm_bs_boxed_t *)malloc(
	    sizeof(rb_vm_bs_boxed_t));

    boxed->bs_type = type;
    boxed->as.v = value;
    boxed->type = NULL; // lazy
    boxed->klass = rb_define_class(((bs_element_opaque_t *)value)->name,
	    rb_cBoxed);

    rb_ivar_set(boxed->klass, boxed_ivar_type, rb_str_new2(octype.c_str()));

    if (type == BS_ELEMENT_STRUCT) {
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
	rb_objc_define_method(boxed->klass, "dup",
		(void *)rb_vm_struct_dup, 0);
	rb_objc_define_method(boxed->klass, "clone",
		(void *)rb_vm_struct_dup, 0);
	rb_objc_define_method(boxed->klass, "inspect",
		(void *)rb_vm_struct_inspect, 0);
    }
    else {
	// Opaque methods.
	rb_objc_define_method(*(VALUE *)boxed->klass, "new",
		(void *)rb_vm_opaque_new, -1);
    }
    // Common methods.
    rb_objc_define_method(boxed->klass, "==", (void *)rb_vm_boxed_equal, 1);

    GET_VM()->bs_boxed[octype] = boxed;

    return true;
}

static VALUE
rb_boxed_objc_type(VALUE rcv, SEL sel)
{
    return rb_ivar_get(rcv, boxed_ivar_type);
}

static VALUE
rb_boxed_is_opaque(VALUE rcv, SEL sel)
{
    rb_vm_bs_boxed_t *bs_boxed = locate_bs_boxed(rcv);
    return bs_boxed->bs_type == BS_ELEMENT_OPAQUE ? Qtrue : Qfalse;
}

VALUE rb_cPointer;

typedef struct {
    VALUE type;
    size_t type_size;
    VALUE (*convert_to_rval)(void *);
    void (*convert_to_ocval)(VALUE rval, void *);
    void *val;
} rb_vm_pointer_t;

static const char *convert_ffi_type(VALUE type,
	bool raise_exception_if_unknown);

static VALUE
rb_pointer_new(VALUE rcv, SEL sel, int argc, VALUE *argv)
{
    VALUE type, len;
    rb_scan_args(argc, argv, "11", &type, &len);
    const size_t rlen = NIL_P(len) ? 1 : FIX2LONG(len);

    StringValuePtr(type);
    const char *type_str = convert_ffi_type(type, false);

    rb_vm_pointer_t *ptr = (rb_vm_pointer_t *)xmalloc(sizeof(rb_vm_pointer_t));
    GC_WB(&ptr->type, rb_str_new2(type_str));

    ptr->convert_to_rval =
	(VALUE (*)(void *))GET_VM()->gen_to_rval_convertor(type_str);
    ptr->convert_to_ocval =
	(void (*)(VALUE, void *))GET_VM()->gen_to_ocval_convertor(type_str);

    ptr->type_size = GET_VM()->get_sizeof(type_str);
    assert(ptr->type_size > 0);
    GC_WB(&ptr->val, xmalloc(ptr->type_size * rlen));

    return Data_Wrap_Struct(rb_cPointer, NULL, NULL, ptr);
}

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

    assert(*type == _C_PTR);
    if (strcmp(RSTRING_PTR(ptr->type), type + 1) != 0) {
	rb_raise(rb_eTypeError,
		"expected instance of Pointer of type `%s', got `%s'",
		RSTRING_PTR(ptr->type),
		type + 1);
    }

    return ptr->val;
}

#define POINTER_VAL(ptr, idx) \
    (void *)((char *)ptr->val + (FIX2INT(idx) * ptr->type_size))

static VALUE
rb_pointer_aref(VALUE rcv, SEL sel, VALUE idx)
{
    Check_Type(idx, T_FIXNUM);

    rb_vm_pointer_t *ptr;
    Data_Get_Struct(rcv, rb_vm_pointer_t, ptr);

    return ptr->convert_to_rval(POINTER_VAL(ptr, idx));
}

static VALUE
rb_pointer_aset(VALUE rcv, SEL sel, VALUE idx, VALUE val)
{
    Check_Type(idx, T_FIXNUM);

    rb_vm_pointer_t *ptr;
    Data_Get_Struct(rcv, rb_vm_pointer_t, ptr);

    ptr->convert_to_ocval(val, POINTER_VAL(ptr, idx));

    return val;
}

static VALUE
rb_pointer_assign(VALUE rcv, SEL sel, VALUE val)
{
    return rb_pointer_aset(rcv, 0, FIX2INT(0), val);
}

static VALUE
rb_pointer_type(VALUE rcv, SEL sel)
{
    rb_vm_pointer_t *ptr;
    Data_Get_Struct(rcv, rb_vm_pointer_t, ptr);

    return ptr->type;
}

static inline void
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

bs_element_method_t *
RoxorVM::find_bs_method(Class klass, SEL sel)
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
RoxorVM::find_bs_boxed(std::string type)
{
    std::map<std::string, rb_vm_bs_boxed_t *>::iterator iter =
	bs_boxed.find(type);

    if (iter == bs_boxed.end()) {
	return NULL;
    }

    return iter->second;
}

rb_vm_bs_boxed_t *
RoxorVM::find_bs_struct(std::string type)
{
    rb_vm_bs_boxed_t *boxed = find_bs_boxed(type);
    return boxed == NULL ? NULL : boxed->is_struct() ? boxed : NULL;
}

rb_vm_bs_boxed_t *
RoxorVM::find_bs_opaque(std::string type)
{
    rb_vm_bs_boxed_t *boxed = find_bs_boxed(type);
    return boxed == NULL ? NULL : boxed->is_struct() ? NULL : boxed;
}

static inline void
register_bs_class(bs_element_class_t *bs_class)
{
    if (bs_class->class_methods_count > 0) {
	index_bs_class_methods(bs_class->name,
		GET_VM()->bs_classes_class_methods,
		bs_class->class_methods,
		bs_class->class_methods_count);
    }
    if (bs_class->instance_methods_count > 0) {
	index_bs_class_methods(bs_class->name,
		GET_VM()->bs_classes_instance_methods,
		bs_class->instance_methods,
		bs_class->instance_methods_count);
    }
}

static void
bs_parse_cb(bs_parser_t *parser, const char *path, bs_element_type_t type, 
            void *value, void *ctx)
{
    bool do_not_free = false;
    CFMutableDictionaryRef rb_cObject_dict = (CFMutableDictionaryRef)ctx;

    switch (type) {
	case BS_ELEMENT_ENUM:
	{
	    bs_element_enum_t *bs_enum = (bs_element_enum_t *)value;
	    ID name = generate_const_name(bs_enum->name);
	    if (!CFDictionaryGetValueIfPresent(rb_cObject_dict,
			(const void *)name, NULL)) {

		VALUE val = strchr(bs_enum->value, '.') != NULL
		    ? rb_float_new(rb_cstr_to_dbl(bs_enum->value, 1))
		    : rb_cstr_to_inum(bs_enum->value, 10, 1);
		CFDictionarySetValue(rb_cObject_dict, (const void *)name, 
			(const void *)val);
	    }
	    else {
		rb_warning("bs: enum `%s' already defined", rb_id2name(name));
	    }
	    break;
	}

	case BS_ELEMENT_CONSTANT:
	{
	    bs_element_constant_t *bs_const = (bs_element_constant_t *)value;
	    ID name = generate_const_name(bs_const->name);
	    if (!CFDictionaryGetValueIfPresent(rb_cObject_dict,
			(const void *)name, NULL)) {

		GET_VM()->bs_consts[name] = bs_const;
		CFDictionarySetValue(rb_cObject_dict, (const void *)name, 
			(const void *)bs_const_magic_cookie);
		do_not_free = true;
	    }
	    else {
		rb_warning("bs: constant `%s' already defined", 
			   rb_id2name(name));
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

		VALUE val;
#if 0 // this is likely not needed anymore
	    	if (bs_strconst->nsstring) {
		    CFStringRef string = CFStringCreateWithCString(NULL,
			    bs_strconst->value, kCFStringEncodingUTF8);
		    val = (VALUE)string;
	    	}
	    	else {
#endif
		    val = rb_str_new2(bs_strconst->value);
//	    	}
		CFDictionarySetValue(rb_cObject_dict, (const void *)name, 
			(const void *)val);
	    }
	    else {
		rb_warning("bs: string constant `%s' already defined", 
			   rb_id2name(name));
	    }
	    break;
	}

	case BS_ELEMENT_FUNCTION:
	{
	    bs_element_function_t *bs_func = (bs_element_function_t *)value;
	    std::string name(bs_func->name);

	    std::map<std::string, bs_element_function_t *>::iterator iter =
		GET_VM()->bs_funcs.find(name);
	    if (iter == GET_VM()->bs_funcs.end()) {
		GET_VM()->bs_funcs[name] = bs_func;
		do_not_free = true;
	    }
	    else {
		rb_warning("bs: function `%s' already defined", bs_func->name);
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
	    else {
		rb_warning("bs: boxed `%s' already defined",
			((bs_element_opaque_t *)value)->name);
	    }
	    break;
	}

	case BS_ELEMENT_CLASS:
	{
	    bs_element_class_t *bs_class = (bs_element_class_t *)value;
	    register_bs_class(bs_class);
	    free(bs_class);
	    do_not_free = true;
	    break;
	}

	case BS_ELEMENT_INFORMAL_PROTOCOL_METHOD:
	{
#if 0
	    bs_element_informal_protocol_method_t *bs_inf_prot_method = 
		(bs_element_informal_protocol_method_t *)value;
	    struct st_table *t = bs_inf_prot_method->class_method
		? bs_inf_prot_cmethods
		: bs_inf_prot_imethods;

	    st_insert(t, (st_data_t)bs_inf_prot_method->name,
		(st_data_t)bs_inf_prot_method->type);

	    free(bs_inf_prot_method->protocol_name);
	    free(bs_inf_prot_method);
	    do_not_free = true;
#endif
	    break;
	}

	case BS_ELEMENT_CFTYPE:
	{
	    
	    bs_element_cftype_t *bs_cftype = (bs_element_cftype_t *)value;
	    std::map<std::string, bs_element_cftype_t *>::iterator
		iter = GET_VM()->bs_cftypes.find(bs_cftype->type);
	    if (iter == GET_VM()->bs_cftypes.end()) {
		GET_VM()->bs_cftypes[bs_cftype->type] = bs_cftype;
		do_not_free = true;
	    }
	    else {
		rb_warning("bs: CF type `%s' already defined",
			bs_cftype->type);
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
    char *error;
    bool ok;
    CFMutableDictionaryRef rb_cObject_dict;  

    if (GET_VM()->bs_parser == NULL) {
	GET_VM()->bs_parser = bs_parser_new();
    }

    rb_cObject_dict = rb_class_ivar_dict(rb_cObject);
    assert(rb_cObject_dict != NULL);

    ok = bs_parser_parse(GET_VM()->bs_parser, path, framework_path,
			 (bs_parse_options_t)options,
			 bs_parse_cb, rb_cObject_dict, &error);
    if (!ok) {
	rb_raise(rb_eRuntimeError, "%s", error);
    }
#if 0 //TODO //MAC_OS_X_VERSION_MAX_ALLOWED <= 1060
    /* XXX we should introduce the possibility to write prelude scripts per
     * frameworks where this kind of changes could be located.
     */
#if defined(__LP64__)
    static bool R6399046_fixed = false;
    /* XXX work around for <rdar://problem/6399046> NSNotFound 64-bit value is incorrect */
    if (!R6399046_fixed) {
	ID nsnotfound = rb_intern("NSNotFound");
	VALUE val = 
	    (VALUE)CFDictionaryGetValue(rb_cObject_dict, (void *)nsnotfound);
	if ((VALUE)val == INT2FIX(-1)) {
	    CFDictionarySetValue(rb_cObject_dict, 
		    (const void *)nsnotfound,
		    (const void *)ULL2NUM(NSNotFound));
	    R6399046_fixed = true;
	    DLOG("XXX", "applied work-around for rdar://problem/6399046");
	}
    }
#endif
    static bool R6401816_fixed = false;
    /* XXX work around for <rdar://problem/6401816> -[NSObject performSelector:withObject:] has wrong sel_of_type attributes*/
    if (!R6401816_fixed) {
	bs_element_method_t *bs_method = 
	    rb_bs_find_method((Class)rb_cNSObject, 
			      @selector(performSelector:withObject:));
	if (bs_method != NULL) {
	    bs_element_arg_t *arg = bs_method->args;
	    while (arg != NULL) {
		if (arg->index == 0 
		    && arg->sel_of_type != NULL
		    && arg->sel_of_type[0] != '@') {
		    arg->sel_of_type[0] = '@';
		    R6401816_fixed = true;
		    DLOG("XXX", "applied work-around for rdar://problem/6401816");
		    break;
		}
		arg++;
	    }
	}	
    }
#endif
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
    rb_objc_define_method(*(VALUE *)rb_cPointer, "new",
	    (void *)rb_pointer_new, -1);
    rb_objc_define_method(*(VALUE *)rb_cPointer, "new_with_type",
	    (void *)rb_pointer_new, -1);
    rb_objc_define_method(rb_cPointer, "[]",
	    (void *)rb_pointer_aref, 1);
    rb_objc_define_method(rb_cPointer, "[]=",
	    (void *)rb_pointer_aset, 2);
    rb_objc_define_method(rb_cPointer, "assign",
	    (void *)rb_pointer_assign, 1);
    rb_objc_define_method(rb_cPointer, "type",
	    (void *)rb_pointer_type, 0);

    bs_const_magic_cookie = rb_str_new2("bs_const_magic_cookie");
    rb_objc_retain((void *)bs_const_magic_cookie);
}

// FFI

static const char *
convert_ffi_type(VALUE type, bool raise_exception_if_unknown)
{
    const char *typestr = StringValueCStr(type);
    assert(typestr != NULL);

    // Ruby-FFI types.

    if (strcmp(typestr, "char") == 0) {
	return "c";
    }
    if (strcmp(typestr, "uchar") == 0) {
	return "C";
    }
    if (strcmp(typestr, "short") == 0) {
	return "s";
    }
    if (strcmp(typestr, "ushort") == 0) {
	return "S";
    }
    if (strcmp(typestr, "int") == 0) {
	return "i";
    }
    if (strcmp(typestr, "uint") == 0) {
	return "I";
    }
    if (strcmp(typestr, "long") == 0) {
	return "l";
    }
    if (strcmp(typestr, "ulong") == 0) {
	return "L";
    }
    if (strcmp(typestr, "long_long") == 0) {
	return "q";
    }
    if (strcmp(typestr, "ulong_long") == 0) {
	return "Q";
    }
    if (strcmp(typestr, "float") == 0) {
	return "f";
    }
    if (strcmp(typestr, "double") == 0) {
	return "d";
    }
    if (strcmp(typestr, "string") == 0) {
	return "*";
    }
    if (strcmp(typestr, "pointer") == 0) {
	return "^";
    }

    // MacRuby extensions.

    if (strcmp(typestr, "object") == 0) {
	return "@";
    }

    if (raise_exception_if_unknown) {
	rb_raise(rb_eTypeError, "unrecognized string `%s' given as FFI type",
		typestr);
    }
    return typestr;
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

    bb = BasicBlock::Create("EntryBlock", f);

    Function::arg_iterator arg = f->arg_begin();
    arg++; // skip self
    arg++; // skip sel

    std::vector<Value *> params;
    std::vector<const Type *> stub_types;

    // First argument is the function implementation. 
    params.push_back(compile_const_pointer(imp));
    stub_types.push_back(PtrTy);

    // Second argument is arity;
    params.push_back(ConstantInt::get(Type::Int32Ty, argc));
    stub_types.push_back(Type::Int32Ty);

    // Third is an array of arguments.
    Value *argv;
    if (argc == 0) {
	argv = new BitCastInst(compile_const_pointer(NULL), RubyObjPtrTy,
		"", bb);
    }
    else {
	argv = new AllocaInst(RubyObjTy, ConstantInt::get(Type::Int32Ty, argc),
		"", bb);
	for (int i = 0; i < argc; i++) {
	    Value *index = ConstantInt::get(Type::Int32Ty, i);
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
    ReturnInst::Create(stub_call, bb);

    return f;
}

static VALUE
rb_ffi_attach_function(VALUE rcv, SEL sel, VALUE name, VALUE args, VALUE ret)
{
    const char *symname = StringValueCStr(name);
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

    rb_vm_c_stub_t *stub = (rb_vm_c_stub_t *)GET_VM()->gen_stub(types, argc,
	    false);
    Function *f = RoxorCompiler::shared->compile_ffi_function((void *)stub,
	    sym, argc);
    IMP imp = GET_VM()->compile(f);

    VALUE klass = rb_singleton_class(rcv);
    rb_objc_define_method(klass, symname, (void *)imp, argc);

    return Qnil;
}

extern "C"
void
Init_FFI(void)
{
    VALUE mFFI = rb_define_module("FFI");
    VALUE mFFILib = rb_define_module_under(mFFI, "Library");
    rb_objc_define_method(mFFILib, "attach_function",
	    (void *)rb_ffi_attach_function, 3);
}
