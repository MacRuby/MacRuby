/*  
 *  Copyright (c) 2008, Apple Inc. All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1.  Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *  2.  Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *  3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *      its contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND
 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR
 *  ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 *  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 *  STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 *  IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

#include <Foundation/Foundation.h>
#include "ruby/ruby.h"
#include "ruby/node.h"
#include "ruby/encoding.h"
#include "ruby/objc.h"
#include "roxor.h"
#include "objc.h"
#include "id.h"

#include <unistd.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <sys/mman.h>
#if HAVE_BRIDGESUPPORT_FRAMEWORK
# include <BridgeSupport/BridgeSupport.h>
#else
# include "bs.h"
#endif

static inline const char *
rb_get_bs_method_type(bs_element_method_t *bs_method, int arg)
{
    if (bs_method != NULL) {
	if (arg == -1) {
	    if (bs_method->retval != NULL) {
		return bs_method->retval->type;
	    }
	}
	else {
	    int i;
	    for (i = 0; i < bs_method->args_count; i++) {
		if (bs_method->args[i].index == arg) {
		    return bs_method->args[i].type;
		}
	    }
	}
    }
    return NULL;
}

bool
rb_objc_get_types(VALUE recv, Class klass, SEL sel,
		  bs_element_method_t *bs_method, char *buf, size_t buflen)
{
    Method method;
    const char *type;
    unsigned i;

    method = class_getInstanceMethod(klass, sel);
    if (method != NULL) {
	if (bs_method == NULL) {
	    type = method_getTypeEncoding(method);
	    assert(strlen(type) < buflen);
	    buf[0] = '\0';
	    do {
		const char *type2 = SkipFirstType(type);
		strncat(buf, type, type2 - type);
		type = SkipStackSize(type2);
	    }
	    while (*type != '\0');
	    //strlcpy(buf, method_getTypeEncoding(method), buflen);
	    //sig->argc = method_getNumberOfArguments(method);
	}
	else {
	    char buf2[100];
	    type = rb_get_bs_method_type(bs_method, -1);
	    if (type != NULL) {
		strlcpy(buf, type, buflen);
	    }
	    else {
		method_getReturnType(method, buf2, sizeof buf2);
		strlcpy(buf, buf2, buflen);
	    }

	    //sig->argc = method_getNumberOfArguments(method);
	    int argc = method_getNumberOfArguments(method);
	    for (i = 0; i < argc; i++) {
		if (i >= 2 && (type = rb_get_bs_method_type(bs_method, i - 2))
			!= NULL) {
		    strlcat(buf, type, buflen);
		}
		else {
		    method_getArgumentType(method, i, buf2, sizeof(buf2));
		    strlcat(buf, buf2, buflen);
		}
	    }
	}
	return true;
    }
    else if (!SPECIAL_CONST_P(recv)) {
	NSMethodSignature *msig = [(id)recv methodSignatureForSelector:sel];
	if (msig != NULL) {
	    unsigned i;

	    type = rb_get_bs_method_type(bs_method, -1);
	    if (type == NULL) {
		type = [msig methodReturnType];
	    }
	    strlcpy(buf, type, buflen);

	    //sig->argc = [msig numberOfArguments];
	    int argc = [msig numberOfArguments];
	    for (i = 0; i < argc; i++) {
		if (i < 2 || (type = rb_get_bs_method_type(bs_method, i - 2))
			== NULL) {
		    type = [msig getArgumentTypeAtIndex:i];
		}
		strlcat(buf, type, buflen);
	    }

	    return true;
	}
    }
    return false;
}

static id _symbolicator = nil;
#define SYMBOLICATION_FRAMEWORK @"/System/Library/PrivateFrameworks/Symbolication.framework"

typedef struct _VMURange {
    uint64_t location;
    uint64_t length;
} VMURange;

@interface NSObject (SymbolicatorAPIs) 
- (id)symbolicatorForTask:(mach_port_t)task;
- (id)symbolForAddress:(uint64_t)address;
- (void)forceFullSymbolExtraction;
- (VMURange)addressRange;
@end

static inline id
rb_objc_symbolicator(void) 
{
    if (_symbolicator == nil) {
	NSError *error;

	if (![[NSBundle bundleWithPath:SYMBOLICATION_FRAMEWORK]
		loadAndReturnError:&error]) {
	    NSLog(@"Cannot load Symbolication.framework: %@", error);
	    abort();    
	}

	Class VMUSymbolicator = NSClassFromString(@"VMUSymbolicator");
	_symbolicator = [VMUSymbolicator symbolicatorForTask:mach_task_self()];
	assert(_symbolicator != nil);
    }

    return _symbolicator;
}

bool
rb_objc_symbolize_address(void *addr, void **start, char *name,
			  size_t name_len) 
{
    Dl_info info;
    if (dladdr(addr, &info) != 0) {
	if (start != NULL) {
	    *start = info.dli_saddr;
	}
	if (name != NULL) {
	    strncpy(name, info.dli_sname, name_len);
	}
	return true;
    }

#if 1
    return false;
#else
    id symbolicator = rb_objc_symbolicator();
    id symbol = [symbolicator symbolForAddress:(NSUInteger)addr];
    if (symbol == nil) {
	return false;
    }
    VMURange range = [symbol addressRange];
    if (start != NULL) {
	*start = (void *)(NSUInteger)range.location;
    }
    if (name != NULL) {
	strncpy(name, [[symbol name] UTF8String], name_len);
    }
    return true;
#endif
}

VALUE
rb_file_expand_path(VALUE fname, VALUE dname)
{
    NSString *res = [(NSString *)fname stringByExpandingTildeInPath];
    if (![res isAbsolutePath]) {
	NSString *dir = dname != Qnil
	    	? (NSString *)dname
		: [[NSFileManager defaultManager] currentDirectoryPath];
	res = [dir stringByAppendingPathComponent:res];
    }
    return (VALUE)[res mutableCopy];
}

static VALUE
rb_objc_load_bs(VALUE recv, SEL sel, VALUE path)
{
    rb_vm_load_bridge_support(StringValuePtr(path), NULL, 0);
    return recv;
}

static void
rb_objc_search_and_load_bridge_support(const char *framework_path)
{
    char path[PATH_MAX];

    if (bs_find_path(framework_path, path, sizeof path)) {
	rb_vm_load_bridge_support(path, framework_path,
                                    BS_PARSE_OPTIONS_LOAD_DYLIBS);
    }
}

static void
reload_protocols(void)
{
#if 0
    Protocol **prots;
    unsigned int i, prots_count;

    prots = objc_copyProtocolList(&prots_count);
    for (i = 0; i < prots_count; i++) {
	Protocol *p;
	struct objc_method_description *methods;
	unsigned j, methods_count;

	p = prots[i];

#define REGISTER_MDESCS(t) // TODO

	methods = protocol_copyMethodDescriptionList(p, true, true,
		&methods_count);
	REGISTER_MDESCS(bs_inf_prot_imethods);
	methods = protocol_copyMethodDescriptionList(p, false, true,
		&methods_count);
	REGISTER_MDESCS(bs_inf_prot_imethods);
	methods = protocol_copyMethodDescriptionList(p, true, false,
		&methods_count);
	REGISTER_MDESCS(bs_inf_prot_cmethods);
	methods = protocol_copyMethodDescriptionList(p, false, false,
		&methods_count);
	REGISTER_MDESCS(bs_inf_prot_cmethods);

#undef REGISTER_MDESCS
    }
    free(prots);
#endif
}

VALUE
rb_require_framework(VALUE recv, SEL sel, int argc, VALUE *argv)
{
    VALUE framework;
    VALUE search_network;
    const char *cstr;
    NSFileManager *fileManager;
    NSString *path;
    NSBundle *bundle;
    NSError *error;

    rb_scan_args(argc, argv, "11", &framework, &search_network);

    Check_Type(framework, T_STRING);
    cstr = RSTRING_PTR(framework);

    fileManager = [NSFileManager defaultManager];
    path = [fileManager stringWithFileSystemRepresentation:cstr
	length:strlen(cstr)];

    if (![fileManager fileExistsAtPath:path]) {
	/* framework name is given */
	NSSearchPathDomainMask pathDomainMask;
	NSString *frameworkName;
	NSArray *dirs;
	NSUInteger i, count;

	cstr = NULL;

#define FIND_LOAD_PATH_IN_LIBRARY(dir) 					  \
    do { 								  \
	path = [[dir stringByAppendingPathComponent:@"Frameworks"]	  \
	   stringByAppendingPathComponent:frameworkName];		  \
	if ([fileManager fileExistsAtPath:path])  			  \
	    goto success; 						  \
	path = [[dir stringByAppendingPathComponent:@"PrivateFrameworks"] \
	   stringByAppendingPathComponent:frameworkName];		  \
	if ([fileManager fileExistsAtPath:path]) 			  \
	    goto success; 						  \
    } 									  \
    while(0)

	pathDomainMask = RTEST(search_network)
	    ? NSAllDomainsMask
	    : NSUserDomainMask | NSLocalDomainMask | NSSystemDomainMask;

	frameworkName = [path stringByAppendingPathExtension:@"framework"];

	path = [[[[NSBundle mainBundle] bundlePath] 
	    stringByAppendingPathComponent:@"Contents/Frameworks"] 
		stringByAppendingPathComponent:frameworkName];
	if ([fileManager fileExistsAtPath:path]) {
	    goto success;
	}

	dirs = NSSearchPathForDirectoriesInDomains(NSLibraryDirectory, 
	    pathDomainMask, YES);
	for (i = 0, count = [dirs count]; i < count; i++) {
	    NSString *dir = [dirs objectAtIndex:i];
	    FIND_LOAD_PATH_IN_LIBRARY(dir);
	}	

	dirs = NSSearchPathForDirectoriesInDomains(NSDeveloperDirectory, 
	    pathDomainMask, YES);
	for (i = 0, count = [dirs count]; i < count; i++) {
	    NSString *dir = [[dirs objectAtIndex:i] 
		stringByAppendingPathComponent:@"Library"];
	    FIND_LOAD_PATH_IN_LIBRARY(dir); 
	}

#undef FIND_LOAD_PATH_IN_LIBRARY

	rb_raise(rb_eRuntimeError, "framework `%s' not found", 
		RSTRING_PTR(framework));
    }

success:

    if (cstr == NULL) {
	cstr = [path fileSystemRepresentation];
    }

    bundle = [NSBundle bundleWithPath:path];
    if (bundle == nil) {
	rb_raise(rb_eRuntimeError, 
	         "framework at path `%s' cannot be located",
		 cstr);
    }

    if ([bundle isLoaded]) {
	return Qfalse;
    }

    if (![bundle loadAndReturnError:&error]) {
	rb_raise(rb_eRuntimeError,
		 "framework at path `%s' cannot be loaded: %s",
		 cstr,
		 [[error description] UTF8String]); 
    }

    rb_objc_search_and_load_bridge_support(cstr);
    reload_protocols();

    return Qtrue;
}

static const char *
resources_path(char *path, size_t len)
{
    CFBundleRef bundle;
    CFURLRef url;

    bundle = CFBundleGetMainBundle();
    assert(bundle != NULL);

    url = CFBundleCopyResourcesDirectoryURL(bundle);
    *path = '-'; 
    *(path+1) = 'I';
    assert(CFURLGetFileSystemRepresentation(
	url, true, (UInt8 *)&path[2], len - 2));
    CFRelease(url);

    return path;
}

int
macruby_main(const char *path, int argc, char **argv)
{
    char **newargv;
    char *p1, *p2;
    int n, i;

    newargv = (char **)malloc(sizeof(char *) * (argc + 2));
    for (i = n = 0; i < argc; i++) {
	if (!strncmp(argv[i], "-psn_", 5) == 0) {
	    newargv[n++] = argv[i];
	}
    }
    
    p1 = (char *)malloc(PATH_MAX);
    newargv[n++] = (char *)resources_path(p1, PATH_MAX);

    p2 = (char *)malloc(PATH_MAX);
    snprintf(p2, PATH_MAX, "%s/%s", (path[0] != '/') ? &p1[2] : "", path);
    newargv[n++] = p2;

    argv = newargv;    
    argc = n;

    ruby_sysinit(&argc, &argv);
    {
	void *tree;
	ruby_init();
	tree = ruby_options(argc, argv);
	free(newargv);
	free(p1);
	free(p2);
	return ruby_run_node(tree);
    }
}

static void *
rb_objc_kvo_setter_imp(void *recv, SEL sel, void *value)
{
    const char *selname;
    char buf[128];
    size_t s;   

    selname = sel_getName(sel);
    buf[0] = '@';
    buf[1] = tolower(selname[3]);
    s = strlcpy(&buf[2], &selname[4], sizeof buf - 2);
    buf[s + 1] = '\0';

    rb_ivar_set((VALUE)recv, rb_intern(buf), value == NULL
	    ? Qnil : OC2RB(value));

    return NULL; /* we explicitely return NULL because otherwise a special constant may stay on the stack and be returned to Objective-C, and do some very nasty crap, especially if called via -[performSelector:]. */
}

/*
  Defines an attribute writer method which conforms to Key-Value Coding.
  (See http://developer.apple.com/documentation/Cocoa/Conceptual/KeyValueCoding/KeyValueCoding.html)
  
    attr_accessor :foo
  
  Will create the normal accessor methods, plus <tt>setFoo</tt>
  
  TODO: Does not handle the case were the user might override #foo=
*/
void
rb_objc_define_kvo_setter(VALUE klass, ID mid)
{
    char buf[100];
    const char *mid_name;

    buf[0] = 's'; buf[1] = 'e'; buf[2] = 't';
    mid_name = rb_id2name(mid);

    buf[3] = toupper(mid_name[0]);
    buf[4] = '\0';
    strlcat(buf, &mid_name[1], sizeof buf);
    strlcat(buf, ":", sizeof buf);

    if (!class_addMethod((Class)klass, sel_registerName(buf), 
			 (IMP)rb_objc_kvo_setter_imp, "v@:@")) {
	rb_warn("can't register `%s' as an KVO setter (method `%s')",
		mid_name, buf);
    }
}

VALUE
rb_mod_objc_ib_outlet(VALUE recv, SEL sel, int argc, VALUE *argv)
{
    int i;

    rb_warn("ib_outlet has been deprecated, please use attr_writer instead");

    for (i = 0; i < argc; i++) {
	VALUE sym = argv[i];
	
	Check_Type(sym, T_SYMBOL);
	rb_objc_define_kvo_setter(recv, SYM2ID(sym));
    }

    return recv;
}

#define FLAGS_AS_ASSOCIATIVE_REF 1

static CFMutableDictionaryRef __obj_flags;

long
rb_objc_flag_get_mask(const void *obj)
{
#if FLAGS_AS_ASSOCIATIVE_REF
    return (long)rb_objc_get_associative_ref((void *)obj, &__obj_flags);
#else
    if (__obj_flags == NULL)
	return 0;

    return (long)CFDictionaryGetValue(__obj_flags, obj);
#endif
}

bool
rb_objc_flag_check(const void *obj, int flag)
{
    long v;

    v = rb_objc_flag_get_mask(obj);
    if (v == 0)
	return false;

    return (v & flag) == flag;
}

void
rb_objc_flag_set(const void *obj, int flag, bool val)
{
#if FLAGS_AS_ASSOCIATIVE_REF
    long v = (long)rb_objc_get_associative_ref((void *)obj, &__obj_flags);
    if (val) {
	v |= flag;
    }
    else {
	v ^= flag;
    }
    rb_objc_set_associative_ref((void *)obj, &__obj_flags, (void *)v);
#else
    long v;

    if (__obj_flags == NULL) {
	__obj_flags = CFDictionaryCreateMutable(NULL, 0, NULL, NULL);
    }
    v = (long)CFDictionaryGetValue(__obj_flags, obj);
    if (val) {
	v |= flag;
    }
    else {
	v ^= flag;
    }
    CFDictionarySetValue(__obj_flags, obj, (void *)v);
#endif
}

long
rb_objc_remove_flags(const void *obj)
{
#if FLAGS_AS_ASSOCIATIVE_REF
    long flag = (long)rb_objc_get_associative_ref((void *)obj, &__obj_flags);
    //rb_objc_set_associative_ref((void *)obj, &__obj_flags, (void *)0);
    return flag;
#else
    long flag;
    if (CFDictionaryGetValueIfPresent(__obj_flags, obj, 
	(const void **)&flag)) {
	CFDictionaryRemoveValue(__obj_flags, obj);
	return flag;
    }
    return 0;
#endif
}

#if 0
static void
rb_objc_get_types_for_format_str(char **octypes, const int len, VALUE *args,
				 const char *format_str, char **new_fmt)
{
    unsigned i, j, format_str_len;

    format_str_len = strlen(format_str);
    i = j = 0;

    while (i < format_str_len) {
	bool sharp_modifier = false;
	bool star_modifier = false;
	if (format_str[i++] != '%')
	    continue;
	if (i < format_str_len && format_str[i] == '%') {
	    i++;
	    continue;
	}
	while (i < format_str_len) {
	    char *type = NULL;
	    switch (format_str[i]) {
		case '#':
		    sharp_modifier = true;
		    break;

		case '*':
		    star_modifier = true;
		    type = "i"; // C_INT;
		    break;

		case 'd':
		case 'i':
		case 'o':
		case 'u':
		case 'x':
		case 'X':
		    type = "i"; // _C_INT;
		    break;

		case 'c':
		case 'C':
		    type = "c"; // _C_CHR;
		    break;

		case 'D':
		case 'O':
		case 'U':
		    type = "l"; // _C_LNG;
		    break;

		case 'f':       
		case 'F':
		case 'e':       
		case 'E':
		case 'g':       
		case 'G':
		case 'a':
		case 'A':
		    type = "d"; // _C_DBL;
		    break;

		case 's':
		case 'S':
		    {
			if (i - 1 > 0) {
			    long k = i - 1;
			    while (k > 0 && format_str[k] == '0')
				k--;
			    if (k < i && format_str[k] == '.')
				args[j] = (VALUE)CFSTR("");
			}
			type = "*"; // _C_CHARPTR;
		    }
		    break;

		case 'p':
		    type = "^"; // _C_PTR;
		    break;

		case '@':
		    type = "@"; // _C_ID;
		    break;

		case 'B':
		case 'b':
		    {
			VALUE arg = args[j];
			switch (TYPE(arg)) {
			    case T_STRING:
				arg = rb_str_to_inum(arg, 0, Qtrue);
				break;
			}
			arg = rb_big2str(arg, 2);
			if (sharp_modifier) {
			    VALUE prefix = format_str[i] == 'B'
				? (VALUE)CFSTR("0B") : (VALUE)CFSTR("0b");
			   rb_str_update(arg, 0, 0, prefix);
			}
			if (*new_fmt == NULL)
			    *new_fmt = strdup(format_str);
			(*new_fmt)[i] = '@';
			args[j] = arg;
			type = "@"; 
		    }
		    break;
	    }

	    i++;

	    if (type != NULL) {
		if (len == 0 || j >= len) {
		    rb_raise(rb_eArgError, 
			    "Too much tokens in the format string `%s' "\
			    "for the given %d argument(s)", format_str, len);
		}
		octypes[j++] = type;
		if (!star_modifier) {
		    break;
		}
	    }
	}
    }
    for (; j < len; j++) {
	octypes[j] = "@"; // _C_ID;
    }
}
#endif

VALUE
rb_str_format(int argc, const VALUE *argv, VALUE fmt)
{
#if 0
    char **types;
    ffi_type *ffi_rettype, **ffi_argtypes;
    void *ffi_ret, **ffi_args;
    ffi_cif *cif;
    int i;
    void *null;
    char *new_fmt;

    if (argc == 0)
	return fmt;

    types = (char **)alloca(sizeof(char *) * argc);
    ffi_argtypes = (ffi_type **)alloca(sizeof(ffi_type *) * (argc + 4));
    ffi_args = (void **)alloca(sizeof(void *) * (argc + 4));

    null = NULL;
    new_fmt = NULL;

    rb_objc_get_types_for_format_str(types, argc, (VALUE *)argv, 
	    RSTRING_PTR(fmt), &new_fmt);
    if (new_fmt != NULL) {
	fmt = (VALUE)CFStringCreateWithCString(NULL, new_fmt, 
		kCFStringEncodingUTF8);
	xfree(new_fmt);
	CFMakeCollectable((void *)fmt);
    }  

    for (i = 0; i < argc; i++) {
	ffi_argtypes[i + 3] = rb_objc_octype_to_ffitype(types[i]);
	ffi_args[i + 3] = (void *)alloca(ffi_argtypes[i + 3]->size);
	rb_objc_rval_to_ocval(argv[i], types[i], ffi_args[i + 3]);
    }

    ffi_argtypes[0] = &ffi_type_pointer;
    ffi_args[0] = &null;
    ffi_argtypes[1] = &ffi_type_pointer;
    ffi_args[1] = &null;
    ffi_argtypes[2] = &ffi_type_pointer;
    ffi_args[2] = &fmt;
   
    ffi_argtypes[argc + 4] = NULL;
    ffi_args[argc + 4] = NULL;

    ffi_rettype = &ffi_type_pointer;
    
    cif = (ffi_cif *)alloca(sizeof(ffi_cif));

    if (ffi_prep_cif(cif, FFI_DEFAULT_ABI, argc + 3, ffi_rettype, ffi_argtypes)
        != FFI_OK)
        rb_fatal("can't prepare cif for CFStringCreateWithFormat");

    ffi_ret = NULL;

    ffi_call(cif, FFI_FN(CFStringCreateWithFormat), &ffi_ret, ffi_args);

    if (ffi_ret != NULL) {
        CFMakeCollectable((CFTypeRef)ffi_ret);
        return (VALUE)ffi_ret;
    }
#endif
    return Qnil;
}

extern bool __CFStringIsMutable(void *);
extern bool _CFArrayIsMutable(void *);
extern bool _CFDictionaryIsMutable(void *);

bool
rb_objc_is_immutable(VALUE v)
{
    switch(TYPE(v)) {
	case T_STRING:
	    return !__CFStringIsMutable((void *)v);
	case T_ARRAY:
	    return !_CFArrayIsMutable((void *)v);
	case T_HASH:
	    return !_CFDictionaryIsMutable((void *)v);	    
    }
    return false;
}

static IMP old_imp_isaForAutonotifying;

static Class
rb_obj_imp_isaForAutonotifying(void *rcv, SEL sel)
{
    Class ret;
    long ret_version;

#define KVO_CHECK_DONE 0x100000

    ret = ((Class (*)(void *, SEL)) old_imp_isaForAutonotifying)(rcv, sel);
    if (ret != NULL && ((ret_version = RCLASS_VERSION(ret)) & KVO_CHECK_DONE) == 0) {
	const char *name = class_getName(ret);
	if (strncmp(name, "NSKVONotifying_", 15) == 0) {
	    Class ret_orig;
	    name += 15;
	    ret_orig = objc_getClass(name);
	    if (ret_orig != NULL && RCLASS_VERSION(ret_orig) & RCLASS_IS_OBJECT_SUBCLASS) {
		DLOG("XXX", "marking KVO generated klass %p (%s) as RObject", ret, class_getName(ret));
		ret_version |= RCLASS_IS_OBJECT_SUBCLASS;
	    }
	}
	ret_version |= KVO_CHECK_DONE;
	RCLASS_SET_VERSION(ret, ret_version);
    }
    return ret;
}

void *placeholder_String = NULL;
void *placeholder_Dictionary = NULL;
void *placeholder_Array = NULL;

void
Init_ObjC(void)
{
    rb_objc_define_method(rb_mKernel, "load_bridge_support_file",
	    rb_objc_load_bs, 1);

    Method m = class_getInstanceMethod(objc_getClass("NSKeyValueUnnestedProperty"), sel_registerName("isaForAutonotifying"));
    assert(m != NULL);
    old_imp_isaForAutonotifying = method_getImplementation(m);
    method_setImplementation(m, (IMP)rb_obj_imp_isaForAutonotifying);

    placeholder_String = objc_getClass("NSPlaceholderMutableString");
    placeholder_Dictionary = objc_getClass("__NSPlaceholderDictionary");
    placeholder_Array = objc_getClass("__NSPlaceholderArray");
}

@interface Protocol
@end

@implementation Protocol (MRFindProtocol)
+(id)protocolWithName:(NSString *)name
{
    return (id)objc_getProtocol([name UTF8String]);
} 
@end

extern int ruby_initialized; /* eval.c */

@implementation MacRuby

+ (MacRuby *)sharedRuntime
{
    static MacRuby *runtime = nil;
    if (runtime == nil) {
	runtime = [[MacRuby alloc] init];
	if (ruby_initialized == 0) {
	    int argc = 0;
	    char **argv = NULL;
	    ruby_sysinit(&argc, &argv);
	    ruby_init();
	}
    }
    return runtime;
}

+ (MacRuby *)runtimeAttachedToProcessIdentifier:(pid_t)pid
{
    [NSException raise:NSGenericException format:@"not implemented yet"];
    return nil;
}

static void
rb_raise_ruby_exc_in_objc(VALUE ex)
{
    VALUE ex_name, ex_message, ex_backtrace;
    NSException *ocex;
    static ID name_id = 0;
    static ID message_id = 0;
    static ID backtrace_id = 0;

    if (name_id == 0) {
	name_id = rb_intern("name");
	message_id = rb_intern("message");
	backtrace_id = rb_intern("backtrace");
    }

    ex_name = rb_funcall(CLASS_OF(ex), name_id, 0);
    ex_message = rb_funcall(ex, message_id, 0);
    ex_backtrace = rb_funcall(ex, backtrace_id, 0);

    ocex = [NSException exceptionWithName:(id)ex_name reason:(id)ex_message 
		userInfo:[NSDictionary dictionaryWithObjectsAndKeys:
		    (id)ex, @"object", 
		    (id)ex_backtrace, @"backtrace",
		    NULL]];

    [ocex raise];
}

static VALUE
evaluateString_safe(VALUE expression)
{
    return rb_eval_string([(NSString *)expression UTF8String]);
}

static VALUE
evaluateString_rescue(void)
{
    rb_raise_ruby_exc_in_objc(rb_errinfo());
    return Qnil; /* not reached */
}

- (id)evaluateString:(NSString *)expression
{
    VALUE ret;

    ret = rb_rescue2(evaluateString_safe, (VALUE)expression,
	    	     evaluateString_rescue, Qnil,
		     rb_eException, (VALUE)0);
    return RB2OC(ret);
}

- (id)evaluateFileAtPath:(NSString *)path
{
    return [self evaluateString:[NSString stringWithContentsOfFile:path usedEncoding:nil error:nil]];
}

- (id)evaluateFileAtURL:(NSURL *)URL
{
    if (![URL isFileURL]) {
	[NSException raise:NSInvalidArgumentException format:@"given URL is not a file URL"];
    }
    return [self evaluateFileAtPath:[URL relativePath]];
}

- (void)loadBridgeSupportFileAtPath:(NSString *)path
{
    rb_vm_load_bridge_support([path fileSystemRepresentation], NULL, 0);
}

- (void)loadBridgeSupportFileAtURL:(NSURL *)URL
{
    if (![URL isFileURL]) {
	[NSException raise:NSInvalidArgumentException format:@"given URL is not a file URL"];
    }
    [self loadBridgeSupportFileAtPath:[URL relativePath]];
}

@end

@implementation NSObject (MacRubyAdditions)

- (id)performRubySelector:(SEL)sel
{
    return [self performRubySelector:sel withArguments:NULL];
}

struct performRuby_context
{
    VALUE rcv;
    ID mid;
    int argc;
    VALUE *argv;
    NODE *node;
};

static VALUE
performRuby_safe(VALUE arg)
{
#if 0
    struct performRuby_context *ud = (struct performRuby_context *)arg;
    return rb_vm_call(GET_THREAD(), *(VALUE *)ud->rcv, ud->rcv, ud->mid, Qnil,
		      ud->argc, ud->argv, ud->node, 0);
#endif
    return Qnil;
}

static VALUE
performRuby_rescue(VALUE arg)
{
//    if (arg != 0) {
//	ruby_current_thread = (rb_thread_t *)arg;
//    }
    rb_raise_ruby_exc_in_objc(rb_errinfo());
    return Qnil; /* not reached */
}

- (id)performRubySelector:(SEL)sel withArguments:(id *)argv count:(int)argc
{
//    const bool need_protection = GET_THREAD()->thread_id != pthread_self();
    NODE *node;
    IMP imp;
    VALUE *rargs, ret;
    ID mid;

    imp = NULL;
    node = NULL;//TODO rb_objc_method_node2(*(VALUE *)self, sel, &imp);
    if (node == NULL) {
	if (imp != NULL) {
	    [NSException raise:NSInvalidArgumentException format:
		@"-[%@ %s] is not a pure Ruby method", self, (char *)sel];
	}
	else {
	    [NSException raise:NSInvalidArgumentException format:
		@"receiver %@ does not respond to %s", (char *)sel];
	}
    }

    if (argc == 0) {
	rargs = NULL;
    }
    else {
	int i;
	rargs = (VALUE *)alloca(sizeof(VALUE) * argc);
	for (i = 0; i < argc; i++) {
	    rargs[i] = OC2RB(argv[i]);
	}
    }
   
#if 0
    rb_thread_t *th, *old = NULL;

    if (need_protection) {
	th = rb_thread_wrap_existing_native_thread(pthread_self());
	native_mutex_lock(&GET_THREAD()->vm->global_interpreter_lock);
	old = ruby_current_thread;
	ruby_current_thread = th;
    }
#endif

    mid = rb_intern((char *)sel);

    struct performRuby_context ud;
    ud.rcv = (VALUE)self;
    ud.mid = mid;
    ud.argc = argc;
    ud.argv = rargs;
    ud.node = node->nd_body;

    ret = rb_rescue2(performRuby_safe, (VALUE)&ud,
                     //performRuby_rescue, (VALUE)old,
                     performRuby_rescue, Qnil,
                     rb_eException, (VALUE)0);
 
#if 0
    if (need_protection) {
	native_mutex_unlock(&GET_THREAD()->vm->global_interpreter_lock);
	ruby_current_thread = old;
    }
#endif

    return RB2OC(ret);
}

- (id)performRubySelector:(SEL)sel withArguments:firstArg, ...
{
    va_list args;
    int argc;
    id *argv;

    if (firstArg != nil) {
	int i;

        argc = 1;
        va_start(args, firstArg);
        while (va_arg(args, id)) {
	    argc++;
	}
        va_end(args);
	argv = alloca(sizeof(id) * argc);
        va_start(args, firstArg);
	argv[0] = firstArg;
        for (i = 1; i < argc; i++) {
            argv[i] = va_arg(args, id);
	}
        va_end(args);
    }
    else {
	argc = 0;
	argv = NULL;
    }

    return [self performRubySelector:sel withArguments:argv count:argc];
}

@end
