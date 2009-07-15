/* 
 * MacRuby implementation of Ruby 1.9's string.c.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 * 
 * Copyright (C) 2007-2008, Apple Inc. All rights reserved.
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 * Copyright (C) 2000 Network Applied Communication Laboratory, Inc.
 * Copyright (C) 2000 Information-technology Promotion Agency, Japan
 */

#include "ruby/ruby.h"
#include "ruby/re.h"
#include "ruby/encoding.h"
#include "id.h"
#include "objc.h"
#include "ruby/node.h"
#include "vm.h"

#define BEG(no) regs->beg[no]
#define END(no) regs->end[no]

#include <math.h>
#include <ctype.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

VALUE rb_cString;
VALUE rb_cCFString;
VALUE rb_cNSString;
VALUE rb_cNSMutableString;
VALUE rb_cSymbol;
VALUE rb_cByteString;

static ptrdiff_t wrappedDataOffset;
#define WRAPPED_DATA_IV_NAME "wrappedData"
#define BYTESTRING_ENCODING_IV_NAME "encoding"

VALUE
rb_str_freeze(VALUE str)
{
    rb_obj_freeze(str);
    return str;
}

#define is_ascii_string(str) (1)
#define is_broken_string(str) (0)
#define STR_ENC_GET(str) (NULL)
#define str_mod_check(x,y,z)

VALUE rb_fs;

static inline void
str_frozen_check(VALUE s)
{
    if (OBJ_FROZEN(s)) {
	rb_raise(rb_eRuntimeError, "string frozen");
    }
}

static inline void
str_change_class(VALUE str, VALUE klass)
{
    if (klass != 0 
	&& klass != rb_cNSString 
	&& klass != rb_cNSMutableString
	&& klass != rb_cSymbol
	&& klass != rb_cByteString) {
	*(VALUE *)str = (VALUE)klass;
    }
}

static inline VALUE
str_alloc(VALUE klass)
{
    VALUE str = (VALUE)CFStringCreateMutable(NULL, 0);
    str_change_class(str, klass);
    CFMakeCollectable((CFTypeRef)str);

    return (VALUE)str;
}

VALUE
rb_str_new_empty(void)
{
    return str_alloc(0);
}

VALUE
rb_str_new_fast(int argc, ...)
{
    VALUE str;
   
    str = str_alloc(0);

    if (argc > 0) {
	va_list ar;
	int i;

	va_start(ar, argc);
	for (i = 0; i < argc; ++i) {
	    VALUE fragment;
	   
	    fragment = va_arg(ar, VALUE);
	    fragment = rb_obj_as_string(fragment);
	    CFStringAppend((CFMutableStringRef)str, (CFStringRef)fragment);
	}
	va_end(ar);
    }

    return str;
}

static VALUE
str_new(VALUE klass, const char *ptr, long len)
{
    VALUE str;

    if (len < 0) {
	rb_raise(rb_eArgError, "negative string size (or size too big)");
    }

    if (ptr != NULL && len > 0) {
	const long slen = len == 1
	    ? 1 /* XXX in the case ptr is actually a pointer to a single char
		   character, which is not NULL-terminated. */
	    : strlen(ptr);

	if (len <= slen) {
	    str = str_alloc(klass);
	    CFStringAppendCString((CFMutableStringRef)str, ptr, 
		    kCFStringEncodingUTF8);
	    if (len < slen) {
		CFStringPad((CFMutableStringRef)str, NULL, len, 0);
	    }
	    if (CFStringGetLength((CFStringRef)str) != len) {
		str = rb_bytestring_new_with_data((const UInt8 *)ptr, len);
	    }
	}
	else {
	    str = rb_bytestring_new_with_data((const UInt8 *)ptr, len);
	}
    }
    else {
	if (len == 0) {
	    str = str_alloc(klass);
	}
	else {
	    str = rb_bytestring_new();
	    rb_bytestring_resize(str, len);
	}
    }

    return str;
}

VALUE
rb_str_new(const char *ptr, long len)
{
    return str_new(rb_cString, ptr, len);
}

VALUE
rb_usascii_str_new(const char *ptr, long len)
{
    VALUE str = str_new(rb_cString, ptr, len);

    return str;
}

VALUE
rb_enc_str_new(const char *ptr, long len, rb_encoding *enc)
{
    VALUE str = str_new(rb_cString, ptr, len);

    return str;
}

VALUE
rb_str_new2(const char *ptr)
{
    long len;
    if (!ptr) {
	rb_raise(rb_eArgError, "NULL pointer given");
    }
    len = strlen(ptr);
    return rb_str_new(len == 0 ? NULL : ptr, len);
}

VALUE
rb_usascii_str_new2(const char *ptr)
{
    if (!ptr) {
	rb_raise(rb_eArgError, "NULL pointer given");
    }
    return rb_usascii_str_new(ptr, strlen(ptr));
}

VALUE
rb_tainted_str_new(const char *ptr, long len)
{
    VALUE str = rb_str_new(ptr, len);
    OBJ_TAINT(str);
    return str;
}

VALUE
rb_tainted_str_new2(const char *ptr)
{
    VALUE str = rb_str_new2(ptr);
    OBJ_TAINT(str);
    return str;
}

static inline VALUE
str_new3(VALUE klass, VALUE str)
{
    VALUE str2 = rb_str_dup(str);
    str_change_class(str2, klass);
    return str2;
}

VALUE
rb_str_new3(VALUE str)
{
    return str_new3(rb_obj_class(str), str);
}

VALUE
rb_str_new4(VALUE orig)
{
    return rb_str_new3(orig);
}

VALUE
rb_str_new5(VALUE obj, const char *ptr, long len)
{
    return str_new(rb_obj_class(obj), ptr, len);
}

#define STR_BUF_MIN_SIZE 128

VALUE
rb_str_buf_new(long capa)
{
    return rb_bytestring_new();
}

VALUE
rb_str_buf_new2(const char *ptr)
{
    VALUE str = rb_bytestring_new();
    long len = strlen(ptr);
    if (ptr != NULL && len > 0) {
	CFDataAppendBytes(rb_bytestring_wrapped_data(str), (const UInt8 *)ptr, len);
    }
    return str;
}

VALUE
rb_str_tmp_new(long len)
{
    VALUE str = rb_bytestring_new();
    rb_bytestring_resize(str, len);
    return str;
}

VALUE
rb_str_to_str(VALUE str)
{
    return rb_convert_type(str, T_STRING, "String", "to_str");
}

void
rb_str_shared_replace(VALUE str, VALUE str2)
{
    rb_str_modify(str);
    CFStringReplaceAll((CFMutableStringRef)str, (CFStringRef)str2);
}

static ID id_to_s;

VALUE
rb_obj_as_string(VALUE obj)
{
    VALUE str;

    if (TYPE(obj) == T_STRING || TYPE(obj) == T_SYMBOL) {
	return obj;
    }
    //str = rb_funcall(obj, id_to_s, 0);
    str = rb_vm_call(obj, selToS, 0, NULL, false);
    if (TYPE(str) != T_STRING) {
	return rb_any_to_s(obj);
    }
    if (OBJ_TAINTED(obj)) {
	OBJ_TAINT(str);
    }
    return str;
}

static VALUE rb_str_replace(VALUE, VALUE);

static VALUE
rb_str_dup_imp(VALUE str, SEL sel)
{
    VALUE dup;

#if 0
    if (RSTRING_LEN(str) == 0) {
	return str_alloc(0);
    }
    dup = (VALUE)CFStringCreateMutableCopy(NULL, 0, (CFStringRef)str);
    CFMakeCollectable((CFTypeRef)dup);
#else
    dup = (VALUE)objc_msgSend((id)str, selMutableCopy);
#endif

    if (OBJ_TAINTED(str)) {
	OBJ_TAINT(dup);
    }

    return dup;
}

VALUE
rb_str_dup(VALUE str)
{
    return rb_str_dup_imp(str, 0);
}

static VALUE
rb_str_clone(VALUE str, SEL sel)
{
    VALUE clone = rb_str_dup(str);
    if (OBJ_FROZEN(str)) {
	OBJ_FREEZE(clone);
    }
    return clone;
}

/*
 *  call-seq:
 *     String.new(str="")   => new_str
 *  
 *  Returns a new string object containing a copy of <i>str</i>.
 */

static VALUE
rb_str_init(VALUE str, SEL sel, int argc, VALUE *argv)
{
    VALUE orig;

    str = (VALUE)objc_msgSend((id)str, selInit);

    if (argc > 0 && rb_scan_args(argc, argv, "01", &orig) == 1) {
	rb_str_replace(str, orig);
    }
    return str;
}

static long
str_strlen(VALUE str, rb_encoding *enc)
{
    /* TODO should use CFStringGetMaximumSizeForEncoding too */
    return RSTRING_LEN(str);
}

/*
 *  call-seq:
 *     str.length   => integer
 *     str.size     => integer
 *  
 *  Returns the character length of <i>str</i>.
 */

static VALUE
rb_str_length_imp(VALUE str, SEL sel)
{
    int len;

    len = str_strlen(str, STR_ENC_GET(str));
    return INT2NUM(len);
}

VALUE
rb_str_length(VALUE str)
{
    return rb_str_length_imp(str, 0);
}

/*
 *  call-seq:
 *     str.bytesize  => integer
 *  
 *  Returns the length of <i>str</i> in bytes.
 */

static VALUE
rb_str_bytesize(VALUE str, SEL sel)
{
    // TODO Not super accurate...
    CFStringEncoding encoding = CFStringGetSmallestEncoding((CFStringRef)str);
    long size = CFStringGetMaximumSizeForEncoding(RSTRING_LEN(str), encoding);
    return LONG2NUM(size);
}

/*
 *  call-seq:
 *     str.empty?   => true or false
 *  
 *  Returns <code>true</code> if <i>str</i> has a length of zero.
 *     
 *     "hello".empty?   #=> false
 *     "".empty?        #=> true
 */

static VALUE
rb_str_empty(VALUE str, SEL sel)
{
    return RSTRING_LEN(str) == 0 ? Qtrue : Qfalse;
}

/*
 *  call-seq:
 *     str + other_str   => new_str
 *  
 *  Concatenation---Returns a new <code>String</code> containing
 *  <i>other_str</i> concatenated to <i>str</i>.
 *     
 *     "Hello from " + self.to_s   #=> "Hello from main"
 */

static VALUE
rb_str_plus(VALUE str1, SEL sel, VALUE str2)
{
    VALUE str3 = rb_str_new(0, 0);
    rb_str_buf_append(str3, str1);
    rb_str_buf_append(str3, str2);
    if (OBJ_TAINTED(str1) || OBJ_TAINTED(str2)) {
	OBJ_TAINT(str3);
    }
    return str3;
}

/*
 *  call-seq:
 *     str * integer   => new_str
 *  
 *  Copy---Returns a new <code>String</code> containing <i>integer</i> copies of
 *  the receiver.
 *     
 *     "Ho! " * 3   #=> "Ho! Ho! Ho! "
 */

static VALUE
rb_str_times(VALUE str, SEL sel, VALUE times)
{
    VALUE str2;
    long n, len;

    n = RSTRING_LEN(str);
    len = NUM2LONG(times);
    if (len < 0) {
	rb_raise(rb_eArgError, "negative argument");
    }
    if (len && LONG_MAX/len < n) {
	rb_raise(rb_eArgError, "argument too big");
    }

    str2 = rb_str_new(NULL, 0);
    CFStringPad((CFMutableStringRef)str2, (CFStringRef)str,
	len * n, 0);

    return str2;
}

/*
 *  call-seq:
 *     str % arg   => new_str
 *  
 *  Format---Uses <i>str</i> as a format specification, and returns the result
 *  of applying it to <i>arg</i>. If the format specification contains more than
 *  one substitution, then <i>arg</i> must be an <code>Array</code> containing
 *  the values to be substituted. See <code>Kernel::sprintf</code> for details
 *  of the format string.
 *     
 *     "%05d" % 123                              #=> "00123"
 *     "%-5s: %08x" % [ "ID", self.object_id ]   #=> "ID   : 200e14d6"
 */

static VALUE
rb_str_format_m(VALUE str, SEL sel, VALUE arg)
{
    VALUE tmp = rb_check_array_type(arg);

    if (!NIL_P(tmp)) {
	return rb_str_format(RARRAY_LEN(tmp), RARRAY_PTR(tmp), str);
    }
    return rb_str_format(1, &arg, str);
}

static inline void
str_modifiable(VALUE str)
{
    long mask;
#ifdef __LP64__
    mask = RCLASS_RC_FLAGS(str);
#else
    mask = rb_objc_flag_get_mask((void *)str);
#endif
    if (RSTRING_IMMUTABLE(str)) {
	mask |= FL_FREEZE;
    }
    if ((mask & FL_FREEZE) == FL_FREEZE) {
	rb_raise(rb_eRuntimeError, "can't modify frozen/immutable string");
    }
    if ((mask & FL_TAINT) == FL_TAINT && rb_safe_level() >= 4) {
	rb_raise(rb_eSecurityError, "Insecure: can't modify string");
    }
}

void
rb_str_modify(VALUE str)
{
#if WITH_OBJC
    str_modifiable(str);
#else
    if (!str_independent(str)) {
	str_make_independent(str);
    }
    ENC_CODERANGE_CLEAR(str);
#endif
}

void
rb_str_associate(VALUE str, VALUE add)
{
    /* sanity check */
    if (OBJ_FROZEN(str)) rb_error_frozen("string");
}

VALUE
rb_str_associated(VALUE str)
{
    return Qfalse;
}

VALUE
rb_string_value(volatile VALUE *ptr)
{
    VALUE s = *ptr;
    if (TYPE(s) != T_STRING) {
	s = rb_str_to_str(s);
	*ptr = s;
    }
    return s;
}

char *
rb_string_value_ptr(volatile VALUE *ptr)
{
    return (char *)RSTRING_PTR(rb_string_value(ptr));
}

const char *
rb_str_cstr(VALUE ptr)
{
    if (*(VALUE *)ptr == rb_cSymbol) {
	return RSYMBOL(ptr)->str;
    }
    if (*(VALUE *)ptr == rb_cByteString) {
	return (const char *)rb_bytestring_byte_pointer(ptr);
    }

    if (RSTRING_LEN(ptr) == 0) {
	return "";
    }

    char *cptr = (char *)CFStringGetCStringPtr((CFStringRef)ptr, 0);
    if (cptr != NULL) {
	return cptr;
    }

    // XXX this is quite inefficient, but we don't really have the
    // choice.

    const long max = CFStringGetMaximumSizeForEncoding(
	    CFStringGetLength((CFStringRef)ptr),
	    kCFStringEncodingUTF8);

    cptr = (char *)xmalloc(max + 1);
    assert(CFStringGetCString((CFStringRef)ptr, cptr,
		max, kCFStringEncodingUTF8));

    return cptr;
}

long
rb_str_clen(VALUE ptr)
{
    return CFStringGetLength((CFStringRef)ptr);
}

char *
rb_string_value_cstr(volatile VALUE *ptr)
{
    VALUE str = rb_string_value(ptr);
    return (char *)rb_str_cstr(str);
}

VALUE
rb_check_string_type(VALUE str)
{
    str = rb_check_convert_type(str, T_STRING, "String", "to_str");
    return str;
}

/*
 *  call-seq:
 *     String.try_convert(obj) -> string or nil
 *
 *  Try to convert <i>obj</i> into a String, using to_str method.
 *  Returns converted regexp or nil if <i>obj</i> cannot be converted
 *  for any reason.
 *
 *     String.try_convert("str")     # => str
 *     String.try_convert(/re/)      # => nil
 */
static VALUE
rb_str_s_try_convert(VALUE dummy, SEL sel, VALUE str)
{
    return rb_check_string_type(str);
}

/* byte offset to char offset */
long
rb_str_sublen(VALUE str, long pos)
{
    return pos;
}

VALUE
rb_str_subseq(VALUE str, long beg, long len)
{
    CFMutableStringRef substr;
    long n;

    n = CFStringGetLength((CFStringRef)str);

    if (beg < 0) {
	beg += n;
    }
    if (beg > n || beg < 0) {
	return Qnil;
    }
    if (beg + len > n) {
	return (VALUE)CFSTR("");
    }

    substr = CFStringCreateMutable(NULL, 0);

    if (len == 1) {
	UniChar c = CFStringGetCharacterAtIndex((CFStringRef)str, beg);
	CFStringAppendCharacters(substr, &c, 1);
    }
    else {
	UniChar *buffer = alloca(sizeof(UniChar) * len);
	CFStringGetCharacters((CFStringRef)str, CFRangeMake(beg, len), 
		buffer);
	CFStringAppendCharacters(substr, buffer, len);
    }

    CFMakeCollectable(substr);

    return (VALUE)substr;
}

VALUE
rb_str_substr(VALUE str, long beg, long len)
{
    return rb_str_subseq(str, beg, len);
}

VALUE
rb_str_dup_frozen(VALUE str)
{
    str = rb_str_dup(str);
    rb_str_freeze(str);
    return str;
}

VALUE
rb_str_locktmp(VALUE str)
{
    return str;
}

VALUE
rb_str_unlocktmp(VALUE str)
{
    return str;
}

void
rb_str_set_len(VALUE str, long len)
{
    rb_str_resize(str, len);    
}

VALUE
rb_str_resize(VALUE str, long len)
{
    long slen;

    if (len < 0) {
	rb_raise(rb_eArgError, "negative string size (or size too big)");
    }

    rb_str_modify(str);
    slen = RSTRING_LEN(str);
    if (slen != len) {
	CFStringPad((CFMutableStringRef)str, CFSTR(" "), len, 0);
    }
    return str;
}

__attribute__((always_inline))
static void
rb_objc_str_cat(VALUE str, const char *ptr, long len, int cfstring_encoding)
{
    if (*(VALUE *)str == rb_cByteString) {
	CFMutableDataRef data = rb_bytestring_wrapped_data(str);
	CFDataAppendBytes(data, (const UInt8 *)ptr, len);
    }
    else {
	long plen = strlen(ptr);
	if (plen >= len) {
	    const char *cstr;
	    if (plen > len) {
		// Sometimes the given string is bigger than the given length.
		char *tmp = alloca(len + 1);
		strncpy(tmp, ptr, len);
		tmp[len] = '\0';
		cstr = (const char *)tmp;
	    }
	    else {
		cstr = ptr;
	    }
	    CFStringAppendCString((CFMutableStringRef)str, cstr,
		    cfstring_encoding);
	}
	else {
	    // Promoting as bytestring!
	    CFDataRef data = CFStringCreateExternalRepresentation(NULL, (CFStringRef)str,
		    kCFStringEncodingUTF8, 0);
	    assert(data != NULL);
	    CFMutableDataRef mdata = CFDataCreateMutableCopy(NULL, 0, data);
	    CFRelease(data);
	    *(VALUE *)str = rb_cByteString;
	    *(void **)((char *)str + sizeof(void *)) = (void *)mdata;
	    CFMakeCollectable(mdata);
	}
    }
}

VALUE
rb_str_buf_cat(VALUE str, const char *ptr, long len)
{
    rb_objc_str_cat(str, ptr, len, kCFStringEncodingASCII);

    return str;
}

VALUE
rb_str_buf_cat2(VALUE str, const char *ptr)
{
    return rb_str_buf_cat(str, ptr, strlen(ptr));
}

VALUE
rb_str_cat(VALUE str, const char *ptr, long len)
{
    if (len < 0) {
	rb_raise(rb_eArgError, "negative string size (or size too big)");
    }

    return rb_str_buf_cat(str, ptr, len);
}

VALUE
rb_str_cat2(VALUE str, const char *ptr)
{
    return rb_str_cat(str, ptr, strlen(ptr));
}

VALUE
rb_enc_str_buf_cat(VALUE str, const char *ptr, long len, rb_encoding *ptr_enc)
{
    rb_objc_str_cat(str, ptr, len, kCFStringEncodingUTF8);
    return str;
}

VALUE
rb_str_buf_cat_ascii(VALUE str, const char *ptr)
{
    rb_objc_str_cat(str, ptr, strlen(ptr), kCFStringEncodingASCII);
    return str;
}

static inline VALUE
rb_str_buf_append0(VALUE str, VALUE str2)
{
    if (TYPE(str2) != T_SYMBOL) {
	Check_Type(str2, T_STRING);
    }

    const char *ptr;
    long len;

    ptr = RSTRING_PTR(str2);
    len = RSTRING_LEN(str2);

    rb_objc_str_cat(str, ptr, len, kCFStringEncodingASCII);

    return str;
}

VALUE
rb_str_buf_append(VALUE str, VALUE str2)
{
   return rb_str_buf_append0(str, str2);
}

VALUE
rb_str_append(VALUE str, VALUE str2)
{
    StringValue(str2);
    rb_str_modify(str);
    return rb_str_buf_append0(str, str2);
}


/*
 *  call-seq:
 *     str << fixnum        => str
 *     str.concat(fixnum)   => str
 *     str << obj           => str
 *     str.concat(obj)      => str
 *  
 *  Append---Concatenates the given object to <i>str</i>. If the object is a
 *  <code>Fixnum</code>, it is considered as a codepoint, and is converted
 *  to a character before concatenation.
 *     
 *     a = "hello "
 *     a << "world"   #=> "hello world"
 *     a.concat(33)   #=> "hello world!"
 */

static VALUE
rb_str_concat_imp(VALUE str1, SEL sel, VALUE str2)
{
    if (FIXNUM_P(str2)) {
        int c = FIX2INT(str2);
	char buf[2];

	rb_str_modify(str1);
	buf[0] = (char)c;
	buf[1] = '\0';
	CFStringAppendCString((CFMutableStringRef)str1, buf, 
			      kCFStringEncodingUTF8);
	return str1;
    }
    return rb_str_append(str1, str2);
}

VALUE
rb_str_concat(VALUE str1, VALUE str2)
{
    return rb_str_concat_imp(str1, 0, str2);
}

int
rb_memhash(const void *ptr, long len)
{
    CFDataRef data;
    int code;

    data = CFDataCreate(NULL, (const UInt8 *)ptr, len);
    code = CFHash(data);
    CFRelease((CFTypeRef)data);
    return code;
}

int
rb_str_hash(VALUE str)
{
    return CFHash((CFTypeRef)str);
}

int
rb_str_hash_cmp(VALUE str1, VALUE str2)
{
    return CFEqual((CFTypeRef)str1, (CFTypeRef)str2) ? 0 : 1;
}

#define lesser(a,b) (((a)>(b))?(b):(a))

int
rb_str_comparable(VALUE str1, VALUE str2)
{
    return Qtrue;
}

int
rb_str_cmp(VALUE str1, VALUE str2)
{
    return CFStringCompare((CFStringRef)str1, (CFStringRef)str2, 0);
}

bool rb_objc_str_is_pure(VALUE);

/*
 *  call-seq:
 *     str == obj   => true or false
 *  
 *  Equality---If <i>obj</i> is not a <code>String</code>, returns
 *  <code>false</code>. Otherwise, returns <code>true</code> if <i>str</i>
 *  <code><=></code> <i>obj</i> returns zero.
 */

static VALUE
rb_str_equal_imp(VALUE str1, SEL sel, VALUE str2)
{
    int len;

    if (str1 == str2) {
	return Qtrue;
    }
    if (TYPE(str2) != T_STRING) {
	if (!rb_respond_to(str2, rb_intern("to_str"))) {
	    return Qfalse;
	}
	return rb_equal(str2, str1);
    }
    len = RSTRING_LEN(str1);
    if (len != RSTRING_LEN(str2)) {
	return Qfalse;
    }
    if (!rb_objc_str_is_pure(str2)) {
	/* This is to work around a strange bug in CFEqual's objc 
	 * dispatching.
	 */
	VALUE tmp = str1;
	str1 = str2;
	str2 = tmp;
    }
    if (CFEqual((CFTypeRef)str1, (CFTypeRef)str2)) {
	return Qtrue;
    }

    return Qfalse;
}

VALUE
rb_str_equal(VALUE str1, VALUE str2)
{
    return rb_str_equal_imp(str1, 0, str2);
}

/*
 * call-seq:
 *   str.eql?(other)   => true or false
 *
 * Two strings are equal if the have the same length and content.
 */

static VALUE
rb_str_eql(VALUE str1, SEL sel, VALUE str2)
{
    if (TYPE(str2) != T_STRING) {
	return Qfalse;
    }

    if (CFEqual((CFTypeRef)str1, (CFTypeRef)str2)) {
	return Qtrue;
    }

    return Qfalse;
}

/*
 *  call-seq:
 *     str <=> other_str   => -1, 0, +1
 *  
 *  Comparison---Returns -1 if <i>other_str</i> is less than, 0 if
 *  <i>other_str</i> is equal to, and +1 if <i>other_str</i> is greater than
 *  <i>str</i>. If the strings are of different lengths, and the strings are
 *  equal when compared up to the shortest length, then the longer string is
 *  considered greater than the shorter one. In older versions of Ruby, setting
 *  <code>$=</code> allowed case-insensitive comparisons; this is now deprecated
 *  in favor of using <code>String#casecmp</code>.
 *
 *  <code><=></code> is the basis for the methods <code><</code>,
 *  <code><=</code>, <code>></code>, <code>>=</code>, and <code>between?</code>,
 *  included from module <code>Comparable</code>.  The method
 *  <code>String#==</code> does not use <code>Comparable#==</code>.
 *     
 *     "abcdef" <=> "abcde"     #=> 1
 *     "abcdef" <=> "abcdef"    #=> 0
 *     "abcdef" <=> "abcdefg"   #=> -1
 *     "abcdef" <=> "ABCDEF"    #=> 1
 */

static VALUE
rb_str_cmp_m(VALUE str1, SEL sel, VALUE str2)
{
    long result;

    if (TYPE(str2) != T_STRING) {
	if (!rb_respond_to(str2, rb_intern("to_str"))) {
	    return Qnil;
	}
	else if (!rb_respond_to(str2, rb_intern("<=>"))) {
	    return Qnil;
	}
	else {
	    VALUE tmp = rb_funcall(str2, rb_intern("<=>"), 1, str1);

	    if (NIL_P(tmp)) return Qnil;
	    if (!FIXNUM_P(tmp)) {
		return rb_funcall(LONG2FIX(0), '-', 1, tmp);
	    }
	    result = -FIX2LONG(tmp);
	}
    }
    else {
	result = rb_str_cmp(str1, str2);
    }
    return LONG2NUM(result);
}

/*
 *  call-seq:
 *     str.casecmp(other_str)   => -1, 0, +1
 *  
 *  Case-insensitive version of <code>String#<=></code>.
 *     
 *     "abcdef".casecmp("abcde")     #=> 1
 *     "aBcDeF".casecmp("abcdef")    #=> 0
 *     "abcdef".casecmp("abcdefg")   #=> -1
 *     "abcdef".casecmp("ABCDEF")    #=> 0
 */

static VALUE
rb_str_casecmp(VALUE str1, SEL sel, VALUE str2)
{
    return INT2FIX(CFStringCompare((CFStringRef)str1, (CFStringRef)str2,
	kCFCompareCaseInsensitive));
}

static long
rb_str_index(VALUE str, VALUE sub, long offset)
{
    CFRange r;
    return (CFStringFindWithOptions((CFStringRef)str, 
		(CFStringRef)sub,
		CFRangeMake(offset, CFStringGetLength((CFStringRef)str) - offset),
		0,
		&r))
	? r.location : -1;
}

/*
 *  call-seq:
 *     str.index(substring [, offset])   => fixnum or nil
 *     str.index(fixnum [, offset])      => fixnum or nil
 *     str.index(regexp [, offset])      => fixnum or nil
 *  
 *  Returns the index of the first occurrence of the given <i>substring</i>,
 *  character (<i>fixnum</i>), or pattern (<i>regexp</i>) in <i>str</i>. Returns
 *  <code>nil</code> if not found. If the second parameter is present, it
 *  specifies the position in the string to begin the search.
 *     
 *     "hello".index('e')             #=> 1
 *     "hello".index('lo')            #=> 3
 *     "hello".index('a')             #=> nil
 *     "hello".index(?e)              #=> 1
 *     "hello".index(101)             #=> 1
 *     "hello".index(/[aeiou]/, -3)   #=> 4
 */

static VALUE
rb_str_index_m(VALUE str, SEL sel, int argc, VALUE *argv)
{
    VALUE sub;
    VALUE initpos;
    long pos;

    if (rb_scan_args(argc, argv, "11", &sub, &initpos) == 2) {
	pos = NUM2LONG(initpos);
    }
    else {
	pos = 0;
    }
    if (pos < 0) {
	pos += str_strlen(str, STR_ENC_GET(str));
	if (pos < 0) {
	    if (TYPE(sub) == T_REGEXP) {
		rb_backref_set(Qnil);
	    }
	    return Qnil;
	}
    }

    switch (TYPE(sub)) {
      case T_REGEXP:
	pos = rb_reg_adjust_startpos(sub, str, pos, 0);
	pos = rb_reg_search(sub, str, pos, 0);
	pos = rb_str_sublen(str, pos);
	break;

      default: {
	VALUE tmp;

	tmp = rb_check_string_type(sub);
	if (NIL_P(tmp)) {
	    rb_raise(rb_eTypeError, "type mismatch: %s given",
		     rb_obj_classname(sub));
	}
	sub = tmp;
      }
	/* fall through */
      case T_STRING:
	pos = rb_str_index(str, sub, pos);
	pos = rb_str_sublen(str, pos);
	break;
    }

    if (pos == -1) return Qnil;
    return LONG2NUM(pos);
}

static long
rb_str_rindex(VALUE str, VALUE sub, long pos)
{
    CFRange r;
    long sublen, strlen;
    sublen = RSTRING_LEN(sub);
    strlen = RSTRING_LEN(str);
    if (sublen == 0 && strlen == 0)
	return 0;
    if (pos <= sublen) {
	pos = strlen < sublen ? strlen : sublen;
    }
    return (CFStringFindWithOptions((CFStringRef)str, 
		(CFStringRef)sub,
		CFRangeMake(0, pos+1),
		kCFCompareBackwards,
		&r))
	? r.location : -1;
}


/*
 *  call-seq:
 *     str.rindex(substring [, fixnum])   => fixnum or nil
 *     str.rindex(fixnum [, fixnum])   => fixnum or nil
 *     str.rindex(regexp [, fixnum])   => fixnum or nil
 *  
 *  Returns the index of the last occurrence of the given <i>substring</i>,
 *  character (<i>fixnum</i>), or pattern (<i>regexp</i>) in <i>str</i>. Returns
 *  <code>nil</code> if not found. If the second parameter is present, it
 *  specifies the position in the string to end the search---characters beyond
 *  this point will not be considered.
 *     
 *     "hello".rindex('e')             #=> 1
 *     "hello".rindex('l')             #=> 3
 *     "hello".rindex('a')             #=> nil
 *     "hello".rindex(?e)              #=> 1
 *     "hello".rindex(101)             #=> 1
 *     "hello".rindex(/[aeiou]/, -2)   #=> 1
 */

static VALUE
rb_str_rindex_m(VALUE str, SEL sel, int argc, VALUE *argv)
{
    VALUE sub;
    VALUE vpos;
    rb_encoding *enc = STR_ENC_GET(str);
    long pos, len = str_strlen(str, enc);

    if (rb_scan_args(argc, argv, "11", &sub, &vpos) == 2) {
	pos = NUM2LONG(vpos);
	if (pos < 0) {
	    pos += len;
	    if (pos < 0) {
		if (TYPE(sub) == T_REGEXP) {
		    rb_backref_set(Qnil);
		}
		return Qnil;
	    }
	}
	if (pos > len) pos = len;
    }
    else {
	pos = len;
    }

    switch (TYPE(sub)) {
      case T_REGEXP:
	/* enc = rb_get_check(str, sub); */
	if (RREGEXP(sub)->len) {
	    pos = rb_reg_adjust_startpos(sub, str, pos, 1);
	    pos = rb_reg_search(sub, str, pos, 1);
	    pos = rb_str_sublen(str, pos);
	}
	if (pos >= 0) return LONG2NUM(pos);
	break;

      default: {
	VALUE tmp;

	tmp = rb_check_string_type(sub);
	if (NIL_P(tmp)) {
	    rb_raise(rb_eTypeError, "type mismatch: %s given",
		     rb_obj_classname(sub));
	}
	sub = tmp;
      }
	/* fall through */
      case T_STRING:
	pos = rb_str_rindex(str, sub, pos);
	if (pos >= 0) return LONG2NUM(pos);
	break;
    }
    return Qnil;
}

/*
 *  call-seq:
 *     str =~ obj   => fixnum or nil
 *  
 *  Match---If <i>obj</i> is a <code>Regexp</code>, use it as a pattern to match
 *  against <i>str</i>,and returns the position the match starts, or 
 *  <code>nil</code> if there is no match. Otherwise, invokes
 *  <i>obj.=~</i>, passing <i>str</i> as an argument. The default
 *  <code>=~</code> in <code>Object</code> returns <code>false</code>.
 *     
 *     "cat o' 9 tails" =~ /\d/   #=> 7
 *     "cat o' 9 tails" =~ 9      #=> nil
 */

static VALUE
rb_str_match(VALUE x, SEL sel, VALUE y)
{
    switch (TYPE(y)) {
      case T_STRING:
	rb_raise(rb_eTypeError, "type mismatch: String given");

      case T_REGEXP:
	return rb_reg_match(y, x);

      default:
	return rb_funcall(y, rb_intern("=~"), 1, x);
    }
}


static VALUE get_pat(VALUE, int);


/*
 *  call-seq:
 *     str.match(pattern)   => matchdata or nil
 *  
 *  Converts <i>pattern</i> to a <code>Regexp</code> (if it isn't already one),
 *  then invokes its <code>match</code> method on <i>str</i>.  If the second
 *  parameter is present, it specifies the position in the string to begin the
 *  search.
 *     
 *     'hello'.match('(.)\1')      #=> #<MatchData "ll" 1:"l">
 *     'hello'.match('(.)\1')[0]   #=> "ll"
 *     'hello'.match(/(.)\1/)[0]   #=> "ll"
 *     'hello'.match('xx')         #=> nil
 *     
 *  If a block is given, invoke the block with MatchData if match succeed, so
 *  that you can write
 *     
 *     str.match(pat) {|m| ...}
 *     
 *  instead of
 *      
 *     if m = str.match(pat)
 *       ...
 *     end
 *      
 *  The return value is a value from block execution in this case.
 */

static VALUE
rb_str_match_m(VALUE str, SEL sel, int argc, VALUE *argv)
{
    VALUE re, result;
    if (argc < 1) {
	rb_raise(rb_eArgError, "wrong number of arguments (%d for 1)", argc);
    }
    re = argv[0];
    argv[0] = str;
    result = rb_funcall2(get_pat(re, 0), rb_intern("match"), argc, argv);
    if (!NIL_P(result) && rb_block_given_p()) {
	return rb_yield(result);
    }
    return result;
}

/*
 *  call-seq:
 *     str.succ   => new_str
 *     str.next   => new_str
 *  
 *  Returns the successor to <i>str</i>. The successor is calculated by
 *  incrementing characters starting from the rightmost alphanumeric (or
 *  the rightmost character if there are no alphanumerics) in the
 *  string. Incrementing a digit always results in another digit, and
 *  incrementing a letter results in another letter of the same case.
 *  Incrementing nonalphanumerics uses the underlying character set's
 *  collating sequence.
 *     
 *  If the increment generates a ``carry,'' the character to the left of
 *  it is incremented. This process repeats until there is no carry,
 *  adding an additional character if necessary.
 *     
 *     "abcd".succ        #=> "abce"
 *     "THX1138".succ     #=> "THX1139"
 *     "<<koala>>".succ   #=> "<<koalb>>"
 *     "1999zzz".succ     #=> "2000aaa"
 *     "ZZZ9999".succ     #=> "AAAA0000"
 *     "***".succ         #=> "**+"
 */

static VALUE
rb_str_succ(VALUE orig, SEL sel)
{
    UniChar *buf;
    UniChar carry;
    long i, len;
    bool modified;

    len = CFStringGetLength((CFStringRef)orig);
    if (len == 0)
	return orig;

    buf = (UniChar *)alloca(sizeof(UniChar) * (len + 1));
    buf++;
    
    CFStringGetCharacters((CFStringRef)orig, CFRangeMake(0, len), buf);
    modified = false;
    carry = 0;

    for (i = len - 1; i >= 0; i--) {
	UniChar c = buf[i];
	if (iswdigit(c)) {
	    modified = true;
	    if (c != '9') {
		buf[i]++;
		carry = 0;
		break;
	    }
	    else {
		buf[i] = '0';
		carry = '1';
	    }
	}
	else if (iswalpha(c)) {
	    bool lower = islower(c);
	    UniChar e = lower ? 'z' : 'Z';
	    modified = true;
	    if (c != e) {
		buf[i]++;
		carry = 0;
		break;
	    }
	    else {
		carry = buf[i] = lower ? 'a' : 'A';
	    }
	}
    }

    if (!modified) {
	buf[len-1]++;
    }
    else if (carry != 0) {
	buf--;
	*buf = carry;
	len++;
    }

    CFMutableStringRef newstr;

    newstr = CFStringCreateMutable(NULL, 0);
    CFStringAppendCharacters(newstr, buf, len);
    CFMakeCollectable(newstr);

    return (VALUE)newstr;
}


/*
 *  call-seq:
 *     str.succ!   => str
 *     str.next!   => str
 *  
 *  Equivalent to <code>String#succ</code>, but modifies the receiver in
 *  place.
 */

static VALUE
rb_str_succ_bang(VALUE str, SEL sel)
{
    rb_str_shared_replace(str, rb_str_succ(str, 0));

    return str;
}


/*
 *  call-seq:
 *     str.upto(other_str, exclusive=false) {|s| block }   => str
 *  
 *  Iterates through successive values, starting at <i>str</i> and
 *  ending at <i>other_str</i> inclusive, passing each value in turn to
 *  the block. The <code>String#succ</code> method is used to generate
 *  each value.  If optional second argument exclusive is omitted or is <code>false</code>,
 *  the last value will be included; otherwise it will be excluded.
 *     
 *     "a8".upto("b6") {|s| print s, ' ' }
 *     for s in "a8".."b6"
 *       print s, ' '
 *     end
 *     
 *  <em>produces:</em>
 *     
 *     a8 a9 b0 b1 b2 b3 b4 b5 b6
 *     a8 a9 b0 b1 b2 b3 b4 b5 b6
 */

static VALUE
rb_str_upto(VALUE beg, SEL sel, int argc, VALUE *argv)
{
    VALUE end, exclusive;
    VALUE current, after_end;
    ID succ;
    int n, excl;

    rb_scan_args(argc, argv, "11", &end, &exclusive);
    excl = RTEST(exclusive);
    succ = rb_intern("succ");
    StringValue(end);
    if (RSTRING_LEN(beg) == 1 && RSTRING_LEN(end) == 1) {
	UniChar c = CFStringGetCharacterAtIndex((CFStringRef)beg, 0);
	UniChar e = CFStringGetCharacterAtIndex((CFStringRef)end, 0);
	
	if (c > e || (excl && c == e)) 
	    return beg;
	for (;;) {
	    CFMutableStringRef substr;
	    substr = CFStringCreateMutable(NULL, 0);
	    CFStringAppendCharacters(substr, &c, 1);
	    CFMakeCollectable(substr);
	    rb_yield((VALUE)substr);
	    RETURN_IF_BROKEN();
	    if (!excl && c == e) 
		break;
	    c++;
	    if (excl && c == e) 
		break;
	}
	return beg;
    }
    n = rb_str_cmp(beg, end);
    if (n > 0 || (excl && n == 0)) return beg;
	
    after_end = rb_funcall(end, succ, 0, 0);
    current = beg;
    while (!rb_str_equal(current, after_end)) {
	rb_yield(current);
	RETURN_IF_BROKEN();
	if (!excl && rb_str_equal(current, end)) break;
	current = rb_funcall(current, succ, 0, 0);
	StringValue(current);
	if (excl && rb_str_equal(current, end)) break;
	if (RSTRING_LEN(current) > RSTRING_LEN(end) || RSTRING_LEN(current) == 0)
	    break;
    }

    return beg;
}

static VALUE
rb_str_subpat(VALUE str, VALUE re, int nth)
{
    if (rb_reg_search(re, str, 0, 0) >= 0) {
	return rb_reg_nth_match(nth, rb_backref_get());
    }
    return Qnil;
}

static VALUE
rb_str_aref(VALUE str, VALUE indx)
{
    long idx;

    switch (TYPE(indx)) {
      case T_FIXNUM:
	idx = FIX2LONG(indx);

      num_index:
	str = rb_str_substr(str, idx, 1);
	if (!NIL_P(str) && RSTRING_LEN(str) == 0) return Qnil;
	return str;

      case T_REGEXP:
	return rb_str_subpat(str, indx, 0);

      case T_STRING:
	if (rb_str_index(str, indx, 0) != -1)
	    return rb_str_dup(indx);
	return Qnil;

      default:
	/* check if indx is Range */
	{
	    long beg, len;
	    VALUE tmp;

	    len = str_strlen(str, STR_ENC_GET(str));
	    switch (rb_range_beg_len(indx, &beg, &len, len, 0)) {
	      case Qfalse:
		break;
	      case Qnil:
		return Qnil;
	      default:
		tmp = rb_str_substr(str, beg, len);
		return tmp;
	    }
	}
	idx = NUM2LONG(indx);
	goto num_index;
    }
    return Qnil;		/* not reached */
}


/*
 *  call-seq:
 *     str[fixnum]                 => new_str or nil
 *     str[fixnum, fixnum]         => new_str or nil
 *     str[range]                  => new_str or nil
 *     str[regexp]                 => new_str or nil
 *     str[regexp, fixnum]         => new_str or nil
 *     str[other_str]              => new_str or nil
 *     str.slice(fixnum)           => new_str or nil
 *     str.slice(fixnum, fixnum)   => new_str or nil
 *     str.slice(range)            => new_str or nil
 *     str.slice(regexp)           => new_str or nil
 *     str.slice(regexp, fixnum)   => new_str or nil
 *     str.slice(other_str)        => new_str or nil
 *  
 *  Element Reference---If passed a single <code>Fixnum</code>, returns a
 *  substring of one character at that position. If passed two <code>Fixnum</code>
 *  objects, returns a substring starting at the offset given by the first, and
 *  a length given by the second. If given a range, a substring containing
 *  characters at offsets given by the range is returned. In all three cases, if
 *  an offset is negative, it is counted from the end of <i>str</i>. Returns
 *  <code>nil</code> if the initial offset falls outside the string, the length
 *  is negative, or the beginning of the range is greater than the end.
 *     
 *  If a <code>Regexp</code> is supplied, the matching portion of <i>str</i> is
 *  returned. If a numeric parameter follows the regular expression, that
 *  component of the <code>MatchData</code> is returned instead. If a
 *  <code>String</code> is given, that string is returned if it occurs in
 *  <i>str</i>. In both cases, <code>nil</code> is returned if there is no
 *  match.
 *     
 *     a = "hello there"
 *     a[1]                   #=> "e"
 *     a[1,3]                 #=> "ell"
 *     a[1..3]                #=> "ell"
 *     a[-3,2]                #=> "er"
 *     a[-4..-2]              #=> "her"
 *     a[12..-1]              #=> nil
 *     a[-2..-4]              #=> ""
 *     a[/[aeiou](.)\1/]      #=> "ell"
 *     a[/[aeiou](.)\1/, 0]   #=> "ell"
 *     a[/[aeiou](.)\1/, 1]   #=> "l"
 *     a[/[aeiou](.)\1/, 2]   #=> nil
 *     a["lo"]                #=> "lo"
 *     a["bye"]               #=> nil
 */

static VALUE
rb_str_aref_m(VALUE str, SEL sel, int argc, VALUE *argv)
{
    if (argc == 2) {
	if (TYPE(argv[0]) == T_REGEXP) {
	    return rb_str_subpat(str, argv[0], NUM2INT(argv[1]));
	}
	return rb_str_substr(str, NUM2LONG(argv[0]), NUM2LONG(argv[1]));
    }
    if (argc != 1) {
	rb_raise(rb_eArgError, "wrong number of arguments (%d for 1)", argc);
    }
    return rb_str_aref(str, argv[0]);
}

static void
rb_str_splice_0(VALUE str, long beg, long len, VALUE val)
{
    rb_str_modify(str);
    CFStringReplace((CFMutableStringRef)str, CFRangeMake(beg, len), 
	(CFStringRef)val);
}

static void
rb_str_splice(VALUE str, long beg, long len, VALUE val)
{
    long slen;

    if (len < 0) {
	rb_raise(rb_eIndexError, "negative length %ld", len);
    }

    StringValue(val);
    rb_str_modify(str);
    slen = CFStringGetLength((CFStringRef)str);

    if (slen < beg) {
out_of_range:
	rb_raise(rb_eIndexError, "index %ld out of string", beg);
    }
    if (beg < 0) {
	if (-beg > slen) {
	    goto out_of_range;
	}
	beg += slen;
    }
    if (slen < len || slen < beg + len) {
	len = slen - beg;
    }
    rb_str_splice_0(str, beg, len, val);

    if (OBJ_TAINTED(val)) {
	OBJ_TAINT(str);
    }
}

void
rb_str_update(VALUE str, long beg, long len, VALUE val)
{
    rb_str_splice(str, beg, len, val);
}

static void
rb_str_subpat_set(VALUE str, VALUE re, int nth, VALUE val)
{
    VALUE match;
    long start, end, len;
    struct re_registers *regs;

    if (rb_reg_search(re, str, 0, 0) < 0) {
	rb_raise(rb_eIndexError, "regexp not matched");
    }
    match = rb_backref_get();
    regs = RMATCH_REGS(match);
    if (nth >= regs->num_regs) {
      out_of_range:
	rb_raise(rb_eIndexError, "index %d out of regexp", nth);
    }
    if (nth < 0) {
	if (-nth >= regs->num_regs) {
	    goto out_of_range;
	}
	nth += regs->num_regs;
    }

    start = BEG(nth);
    if (start == -1) {
	rb_raise(rb_eIndexError, "regexp group %d not matched", nth);
    }
    end = END(nth);
    len = end - start;
    StringValue(val);
    rb_str_splice_0(str, start, len, val);
}

static VALUE
rb_str_aset(VALUE str, VALUE indx, VALUE val)
{
    long idx, beg;

    switch (TYPE(indx)) {
	case T_FIXNUM:
	    idx = FIX2LONG(indx);
num_index:
	    rb_str_splice(str, idx, 1, val);
	    return val;

	case T_REGEXP:
	    rb_str_subpat_set(str, indx, 0, val);
	    return val;

	case T_STRING:
	    beg = rb_str_index(str, indx, 0);
	    if (beg < 0) {
		rb_raise(rb_eIndexError, "string not matched");
	    }
	    beg = rb_str_sublen(str, beg);
	    rb_str_splice(str, beg, str_strlen(indx, 0), val);
	    return val;

	default:
	    /* check if indx is Range */
	    {
		long beg, len;
		if (rb_range_beg_len(indx, &beg, &len, str_strlen(str, 0), 2)) {
		    rb_str_splice(str, beg, len, val);
		    return val;
		}
	    }
	    idx = NUM2LONG(indx);
	    goto num_index;
    }
}

/*
 *  call-seq:
 *     str[fixnum] = new_str
 *     str[fixnum, fixnum] = new_str
 *     str[range] = aString
 *     str[regexp] = new_str
 *     str[regexp, fixnum] = new_str
 *     str[other_str] = new_str
 *  
 *  Element Assignment---Replaces some or all of the content of <i>str</i>. The
 *  portion of the string affected is determined using the same criteria as
 *  <code>String#[]</code>. If the replacement string is not the same length as
 *  the text it is replacing, the string will be adjusted accordingly. If the
 *  regular expression or string is used as the index doesn't match a position
 *  in the string, <code>IndexError</code> is raised. If the regular expression
 *  form is used, the optional second <code>Fixnum</code> allows you to specify
 *  which portion of the match to replace (effectively using the
 *  <code>MatchData</code> indexing rules. The forms that take a
 *  <code>Fixnum</code> will raise an <code>IndexError</code> if the value is
 *  out of range; the <code>Range</code> form will raise a
 *  <code>RangeError</code>, and the <code>Regexp</code> and <code>String</code>
 *  forms will silently ignore the assignment.
 */

static VALUE
rb_str_aset_m(VALUE str, SEL sel, int argc, VALUE *argv)
{
    if (argc == 3) {
	if (TYPE(argv[0]) == T_REGEXP) {
	    rb_str_subpat_set(str, argv[0], NUM2INT(argv[1]), argv[2]);
	}
	else {
	    rb_str_splice(str, NUM2LONG(argv[0]), NUM2LONG(argv[1]), argv[2]);
	}
	return argv[2];
    }
    if (argc != 2) {
	rb_raise(rb_eArgError, "wrong number of arguments (%d for 2)", argc);
    }
    return rb_str_aset(str, argv[0], argv[1]);
}

/*
 *  call-seq:
 *     str.insert(index, other_str)   => str
 *  
 *  Inserts <i>other_str</i> before the character at the given
 *  <i>index</i>, modifying <i>str</i>. Negative indices count from the
 *  end of the string, and insert <em>after</em> the given character.
 *  The intent is insert <i>aString</i> so that it starts at the given
 *  <i>index</i>.
 *     
 *     "abcd".insert(0, 'X')    #=> "Xabcd"
 *     "abcd".insert(3, 'X')    #=> "abcXd"
 *     "abcd".insert(4, 'X')    #=> "abcdX"
 *     "abcd".insert(-3, 'X')   #=> "abXcd"
 *     "abcd".insert(-1, 'X')   #=> "abcdX"
 */

static VALUE
rb_str_insert(VALUE str, SEL sel, VALUE idx, VALUE str2)
{
    long pos = NUM2LONG(idx);

    if (pos == -1) {
	return rb_str_append(str, str2);
    }
    else if (pos < 0) {
	pos++;
    }
    rb_str_splice(str, pos, 0, str2);
    return str;
}


/*
 *  call-seq:
 *     str.slice!(fixnum)           => fixnum or nil
 *     str.slice!(fixnum, fixnum)   => new_str or nil
 *     str.slice!(range)            => new_str or nil
 *     str.slice!(regexp)           => new_str or nil
 *     str.slice!(other_str)        => new_str or nil
 *  
 *  Deletes the specified portion from <i>str</i>, and returns the portion
 *  deleted.
 *     
 *     string = "this is a string"
 *     string.slice!(2)        #=> "i"
 *     string.slice!(3..6)     #=> " is "
 *     string.slice!(/s.*t/)   #=> "sa st"
 *     string.slice!("r")      #=> "r"
 *     string                  #=> "thing"
 */

static VALUE
rb_str_slice_bang(VALUE str, SEL sel, int argc, VALUE *argv)
{
    VALUE result;
    VALUE buf[3];
    int i;

    if (argc < 1 || 2 < argc) {
	rb_raise(rb_eArgError, "wrong number of arguments (%d for 1)", argc);
    }
    for (i=0; i<argc; i++) {
	buf[i] = argv[i];
    }
    rb_str_modify(str);
    buf[i] = rb_str_new(0,0);
    result = rb_str_aref_m(str, 0, argc, buf);
    if (!NIL_P(result)) {
	rb_str_aset_m(str, 0, argc+1, buf);
    }
    return result;
}

static VALUE
get_pat(VALUE pat, int quote)
{
    VALUE val;

    switch (TYPE(pat)) {
      case T_REGEXP:
	return pat;

      case T_STRING:
	break;

      default:
	val = rb_check_string_type(pat);
	if (NIL_P(val)) {
	    Check_Type(pat, T_REGEXP);
	}
	pat = val;
    }

    if (quote) {
	pat = rb_reg_quote(pat);
    }

    return rb_reg_regcomp(pat);
}


/*
 *  call-seq:
 *     str.sub!(pattern, replacement)          => str or nil
 *     str.sub!(pattern) {|match| block }      => str or nil
 *  
 *  Performs the substitutions of <code>String#sub</code> in place,
 *  returning <i>str</i>, or <code>nil</code> if no substitutions were
 *  performed.
 */

static VALUE
rb_str_sub_bang(VALUE str, SEL sel, int argc, VALUE *argv)
{
    VALUE pat, repl, match, hash = Qnil;
    struct re_registers *regs;
    int iter = 0;
    int tainted = 0;

    if (argc == 1 && rb_block_given_p()) {
	iter = 1;
    }
    else if (argc == 2) {
	repl = argv[1];
	hash = rb_check_convert_type(argv[1], T_HASH, "Hash", "to_hash");
	if (NIL_P(hash)) {
	    StringValue(repl);
	}
	if (OBJ_TAINTED(repl)) tainted = 1;
    }
    else {
	rb_raise(rb_eArgError, "wrong number of arguments (%d for 2)", argc);
    }

    pat = get_pat(argv[0], 1);
    if (rb_reg_search(pat, str, 0, 0) >= 0) {

	match = rb_backref_get();
	regs = RMATCH_REGS(match);

	if (iter || !NIL_P(hash)) {

            if (iter) {
                rb_match_busy(match);
                repl = rb_obj_as_string(rb_yield(rb_reg_nth_match(0, match)));
            }
            else {
                repl = rb_hash_aref(hash, rb_str_subseq(str, BEG(0), END(0) - BEG(0)));
                repl = rb_obj_as_string(repl);
            }
	    str_frozen_check(str);
	    if (iter) rb_backref_set(match);
	}
	else {
	    repl = rb_reg_regsub(repl, str, regs, pat);
	}

	rb_str_modify(str);
	rb_str_splice_0(str, BEG(0), END(0) - BEG(0), repl);
	if (OBJ_TAINTED(repl)) tainted = 1;

	if (tainted) OBJ_TAINT(str);

	return str;
    }
    return Qnil;
}


/*
 *  call-seq:
 *     str.sub(pattern, replacement)         => new_str
 *     str.sub(pattern) {|match| block }     => new_str
 *  
 *  Returns a copy of <i>str</i> with the <em>first</em> occurrence of
 *  <i>pattern</i> replaced with either <i>replacement</i> or the value of the
 *  block. The <i>pattern</i> will typically be a <code>Regexp</code>; if it is
 *  a <code>String</code> then no regular expression metacharacters will be
 *  interpreted (that is <code>/\d/</code> will match a digit, but
 *  <code>'\d'</code> will match a backslash followed by a 'd').
 *     
 *  If the method call specifies <i>replacement</i>, special variables such as
 *  <code>$&</code> will not be useful, as substitution into the string occurs
 *  before the pattern match starts. However, the sequences <code>\1</code>,
 *  <code>\2</code>, <code>\k<group_name></code>, etc., may be used.
 *     
 *  In the block form, the current match string is passed in as a parameter, and
 *  variables such as <code>$1</code>, <code>$2</code>, <code>$`</code>,
 *  <code>$&</code>, and <code>$'</code> will be set appropriately. The value
 *  returned by the block will be substituted for the match on each call.
 *     
 *  The result inherits any tainting in the original string or any supplied
 *  replacement string.
 *     
 *     "hello".sub(/[aeiou]/, '*')                  #=> "h*llo"
 *     "hello".sub(/([aeiou])/, '<\1>')             #=> "h<e>llo"
 *     "hello".sub(/./) {|s| s[0].ord.to_s + ' ' }  #=> "104 ello"
 *     "hello".sub(/(?<foo>[aeiou])/, '*\k<foo>*')  #=> "h*e*llo"
 */

static VALUE
rb_str_sub(VALUE str, SEL sel, int argc, VALUE *argv)
{
    str = rb_str_new3(str);
    rb_str_sub_bang(str, 0, argc, argv);
    return str;
}

static VALUE
str_gsub(SEL sel, int argc, VALUE *argv, VALUE str, int bang)
{
    VALUE pat, val, repl, match, dest, hash = Qnil;
    struct re_registers *regs;
    long beg, n;
    long offset, slen, len;
    int iter = 0;
    const char *sp, *cp;
    int tainted = 0;
    rb_encoding *str_enc;
    
    switch (argc) {
      case 1:
	RETURN_ENUMERATOR(str, argc, argv);
	iter = 1;
	break;
      case 2:
	repl = argv[1];
	hash = rb_check_convert_type(argv[1], T_HASH, "Hash", "to_hash");
	if (NIL_P(hash)) {
	    StringValue(repl);
	}
	if (OBJ_TAINTED(repl)) {
	    tainted = 1;
	}
	break;
      default:
	rb_raise(rb_eArgError, "wrong number of arguments (%d for 2)", argc);
    }

    pat = get_pat(argv[0], 1);
    offset=0; n=0;
    beg = rb_reg_search(pat, str, 0, 0);
    if (beg < 0) {
	if (bang) {
	    return Qnil;	/* no match, no substitution */
	}
	return rb_str_new3(str);
    }

    dest = rb_str_new5(str, NULL, 0);
    slen = RSTRING_LEN(str);
    sp = RSTRING_PTR(str);
    cp = sp;
    str_enc = NULL;

    do {
	n++;
	match = rb_backref_get();
	regs = RMATCH_REGS(match);
	if (iter || !NIL_P(hash)) {
            if (iter) {
                rb_match_busy(match);
                val = rb_obj_as_string(rb_yield(rb_reg_nth_match(0, match)));
            }
            else {
                val = rb_hash_aref(hash, rb_str_subseq(str, BEG(0), END(0) - BEG(0)));
                val = rb_obj_as_string(val);
            }
	    str_mod_check(str, sp, slen);
	    if (bang) {
		str_frozen_check(str);
	    }
	    if (val == dest) { 	/* paranoid check [ruby-dev:24827] */
		rb_raise(rb_eRuntimeError, "block should not cheat");
	    }
	    if (iter) {
		rb_backref_set(match);
		RETURN_IF_BROKEN();
	    }
	}
	else {
	    val = rb_reg_regsub(repl, str, regs, pat);
	}

	if (OBJ_TAINTED(val)) {
	    tainted = 1;
	}

	len = beg - offset;	/* copy pre-match substr */
        if (len) {
	    rb_enc_str_buf_cat(dest, cp, len, str_enc);
        }

        rb_str_buf_append(dest, val);

	offset = END(0);
	if (BEG(0) == END(0)) {
	    /*
	     * Always consume at least one character of the input string
	     * in order to prevent infinite loops.
	     */
	    if (slen <= END(0)) break;
	    len = 1;
            rb_enc_str_buf_cat(dest, sp+END(0), len, str_enc);
	    offset = END(0) + len;
	}
	cp = sp + offset;
	if (offset > slen) break;
	beg = rb_reg_search(pat, str, offset, 0);
    } 
    while (beg >= 0);
    if (slen > offset) {
        rb_enc_str_buf_cat(dest, cp, slen - offset, str_enc);
    }
    rb_backref_set(match);
    if (bang) {
	rb_str_modify(str);
	CFStringReplaceAll((CFMutableStringRef)str, (CFStringRef)dest);
    }
    else {
    	if (!tainted && OBJ_TAINTED(str)) {
	    tainted = 1;
	}
	str = dest;
    }

    if (tainted) {
	OBJ_TAINT(str);
    }
    return str;
}


/*
 *  call-seq:
 *     str.gsub!(pattern, replacement)        => str or nil
 *     str.gsub!(pattern) {|match| block }    => str or nil
 *  
 *  Performs the substitutions of <code>String#gsub</code> in place, returning
 *  <i>str</i>, or <code>nil</code> if no substitutions were performed.
 */

static VALUE
rb_str_gsub_bang(VALUE str, SEL sel, int argc, VALUE *argv)
{
    return str_gsub(sel, argc, argv, str, 1);
}


/*
 *  call-seq:
 *     str.gsub(pattern, replacement)       => new_str
 *     str.gsub(pattern) {|match| block }   => new_str
 *  
 *  Returns a copy of <i>str</i> with <em>all</em> occurrences of <i>pattern</i>
 *  replaced with either <i>replacement</i> or the value of the block. The
 *  <i>pattern</i> will typically be a <code>Regexp</code>; if it is a
 *  <code>String</code> then no regular expression metacharacters will be
 *  interpreted (that is <code>/\d/</code> will match a digit, but
 *  <code>'\d'</code> will match a backslash followed by a 'd').
 *     
 *  If a string is used as the replacement, special variables from the match
 *  (such as <code>$&</code> and <code>$1</code>) cannot be substituted into it,
 *  as substitution into the string occurs before the pattern match
 *  starts. However, the sequences <code>\1</code>, <code>\2</code>,
 *  <code>\k<group_name></code>, and so on may be used to interpolate
 *  successive groups in the match.
 *     
 *  In the block form, the current match string is passed in as a parameter, and
 *  variables such as <code>$1</code>, <code>$2</code>, <code>$`</code>,
 *  <code>$&</code>, and <code>$'</code> will be set appropriately. The value
 *  returned by the block will be substituted for the match on each call.
 *     
 *  The result inherits any tainting in the original string or any supplied
 *  replacement string.
 *     
 *     "hello".gsub(/[aeiou]/, '*')                  #=> "h*ll*"
 *     "hello".gsub(/([aeiou])/, '<\1>')             #=> "h<e>ll<o>"
 *     "hello".gsub(/./) {|s| s[0].ord.to_s + ' '}   #=> "104 101 108 108 111 "
 *     "hello".gsub(/(?<foo>[aeiou])/, '{\k<foo>}')  #=> "h{e}ll{o}"
 */

static VALUE
rb_str_gsub(VALUE str, SEL sel, int argc, VALUE *argv)
{
    return str_gsub(sel, argc, argv, str, 0);
}


/*
 *  call-seq:
 *     str.replace(other_str)   => str
 *  
 *  Replaces the contents and taintedness of <i>str</i> with the corresponding
 *  values in <i>other_str</i>.
 *     
 *     s = "hello"         #=> "hello"
 *     s.replace "world"   #=> "world"
 */

static VALUE
rb_str_replace_imp(VALUE str, SEL sel, VALUE str2)
{
    if (str == str2) {
	return str;
    }
    rb_str_modify(str);
    CFStringReplaceAll((CFMutableStringRef)str, (CFStringRef)str2);
    if (OBJ_TAINTED(str2)) {
	OBJ_TAINT(str);
    }
    return str;
}

VALUE
rb_str_replace(VALUE str, VALUE str2)
{
    return rb_str_replace_imp(str, 0, str2);
}

/*
 *  call-seq:
 *     string.clear    ->  string
 *
 *  Makes string empty.
 *
 *     a = "abcde"
 *     a.clear    #=> ""
 */

static VALUE
rb_str_clear(VALUE str, SEL sel)
{
    rb_str_modify(str);
    CFStringDelete((CFMutableStringRef)str, 
	CFRangeMake(0, CFStringGetLength((CFStringRef)str)));
    return str;
}

/*
 *  call-seq:
 *     string.chr    ->  string
 *
 *  Returns a one-character string at the beginning of the string.
 *
 *     a = "abcde"
 *     a.chr    #=> "a"
 */

static VALUE
rb_str_chr(VALUE str, SEL sel)
{
    return rb_str_substr(str, 0, 1);
}

/*
 *  call-seq:
 *     str.getbyte(index)          => 0 .. 255
 *
 *  returns the <i>index</i>th byte as an integer.
 */
static VALUE
rb_str_getbyte(VALUE str, SEL sel, VALUE index)
{
    // TODO
#if 0
    long pos = NUM2LONG(index);
    long n = RSTRING_BYTELEN(str);

    if (pos < 0) {
        pos += n;
    }
    if (pos < 0 || n <= pos) {
        return Qnil;
    }

    return INT2FIX((unsigned char)RSTRING_BYTEPTR(str)[pos]);
#endif
    abort();
}

/*
 *  call-seq:
 *     str.setbyte(index, int) => int
 *
 *  modifies the <i>index</i>th byte as <i>int</i>.
 */
static VALUE
rb_str_setbyte(VALUE str, SEL sel, VALUE index, VALUE value)
{
    // TODO promote to ByteString
#if 0
    long pos = NUM2LONG(index);
    int byte = NUM2INT(value);
    long n = RSTRING_BYTELEN(str);

    rb_str_modify(str);

    if (pos < -n || n <= pos)
        rb_raise(rb_eIndexError, "index %ld out of string", pos);
    if (pos < 0)
        pos += n;

    RSTRING_BYTEPTR(str)[pos] = byte;
    RSTRING_SYNC(str);

    return value;
#endif
    abort();
}


/*
 *  call-seq:
 *     str.reverse!   => str
 *  
 *  Reverses <i>str</i> in place.
 */

static VALUE
rb_str_reverse_bang(VALUE str, SEL sel)
{
    CFIndex i, n;
    UniChar *buffer;

    n = CFStringGetLength((CFStringRef)str);
    if (n <= 1) {
	return rb_str_dup(str);
    }
   
    buffer = (UniChar *)alloca(sizeof(UniChar) * n);
    CFStringGetCharacters((CFStringRef)str, CFRangeMake(0, n), buffer);
    for (i = 0; i < (n / 2); i++) {
	UniChar c = buffer[i];
	buffer[i] = buffer[n - i - 1];
	buffer[n - i - 1] = c;
    }
    CFStringDelete((CFMutableStringRef)str, CFRangeMake(0, n));
    CFStringAppendCharacters((CFMutableStringRef)str, (const UniChar *)buffer, n);

    return str;
}

/*
 *  call-seq:
 *     str.reverse   => new_str
 *  
 *  Returns a new string with the characters from <i>str</i> in reverse order.
 *     
 *     "stressed".reverse   #=> "desserts"
 */

static VALUE
rb_str_reverse(VALUE str, SEL sel)
{
    VALUE obj = rb_str_dup(str);
    rb_str_reverse_bang(obj, 0);
    return obj;
}

/*
 *  call-seq:
 *     str.include? other_str   => true or false
 *     str.include? fixnum      => true or false
 *  
 *  Returns <code>true</code> if <i>str</i> contains the given string or
 *  character.
 *     
 *     "hello".include? "lo"   #=> true
 *     "hello".include? "ol"   #=> false
 *     "hello".include? ?h     #=> true
 */

static VALUE
rb_str_include(VALUE str, SEL sel, VALUE arg)
{
    long i;

    StringValue(arg);
    i = rb_str_index(str, arg, 0);

    return (i == -1) ? Qfalse : Qtrue;
}


/*
 *  call-seq:
 *     str.to_i(base=10)   => integer
 *  
 *  Returns the result of interpreting leading characters in <i>str</i> as an
 *  integer base <i>base</i> (between 2 and 36). Extraneous characters past the
 *  end of a valid number are ignored. If there is not a valid number at the
 *  start of <i>str</i>, <code>0</code> is returned. This method never raises an
 *  exception.
 *     
 *     "12345".to_i             #=> 12345
 *     "99 red balloons".to_i   #=> 99
 *     "0a".to_i                #=> 0
 *     "0a".to_i(16)            #=> 10
 *     "hello".to_i             #=> 0
 *     "1100101".to_i(2)        #=> 101
 *     "1100101".to_i(8)        #=> 294977
 *     "1100101".to_i(10)       #=> 1100101
 *     "1100101".to_i(16)       #=> 17826049
 */

static VALUE
rb_str_to_i(VALUE str, SEL sel, int argc, VALUE *argv)
{
    int base;

    if (argc == 0) base = 10;
    else {
	VALUE b;

	rb_scan_args(argc, argv, "01", &b);
	base = NUM2INT(b);
    }
    if (base < 0) {
	rb_raise(rb_eArgError, "invalid radix %d", base);
    }
    return rb_str_to_inum(str, base, Qfalse);
}


/*
 *  call-seq:
 *     str.to_f   => float
 *  
 *  Returns the result of interpreting leading characters in <i>str</i> as a
 *  floating point number. Extraneous characters past the end of a valid number
 *  are ignored. If there is not a valid number at the start of <i>str</i>,
 *  <code>0.0</code> is returned. This method never raises an exception.
 *     
 *     "123.45e1".to_f        #=> 1234.5
 *     "45.67 degrees".to_f   #=> 45.67
 *     "thx1138".to_f         #=> 0.0
 */

static VALUE
rb_str_to_f(VALUE str, SEL sel)
{
    return DOUBLE2NUM(rb_str_to_dbl(str, Qfalse));
}


/*
 *  call-seq:
 *     str.to_s     => str
 *     str.to_str   => str
 *  
 *  Returns the receiver.
 */

static VALUE
rb_str_to_s(VALUE str, SEL sel)
{
    if (!rb_objc_str_is_pure(str)) {
	VALUE dup = str_alloc(rb_cString);
	rb_str_replace(dup, str);
	return dup;
    }
    return str;
}

static void
str_cat_char(VALUE str, int c, rb_encoding *enc)
{
    char buf[2];
    buf[0] = (char)c;
    buf[1] = '\0';
    CFStringAppendCString((CFMutableStringRef)str, buf, kCFStringEncodingUTF8);
}

static void
prefix_escape(VALUE str, int c, rb_encoding *enc)
{
    str_cat_char(str, '\\', enc);
    str_cat_char(str, c, enc);
}

/*
 * call-seq:
 *   str.inspect   => string
 *
 * Returns a printable version of _str_, surrounded by quote marks,
 * with special characters escaped.
 *
 *    str = "hello"
 *    str[3] = "\b"
 *    str.inspect       #=> "\"hel\\bo\""
 */

VALUE
rb_str_inspect(VALUE str, SEL sel)
{
    rb_encoding *enc = STR_ENC_GET(str);
    const char *p, *pend;
    VALUE result;

    p = RSTRING_PTR(str); 
    pend = p + RSTRING_LEN(str);
    if (p == NULL) {
	return rb_str_new2("\"\"");
    }
    result = rb_str_buf_new2("");
    str_cat_char(result, '"', enc);
    while (p < pend) {
	int c;
	int n;
	int cc;

	c = *p;
	n = 1;

	p += n;
	if (c == '"'|| c == '\\' ||
	    (c == '#' &&
             p < pend &&
	     ((cc = *p),
              (cc == '$' || cc == '@' || cc == '{')))) {
	    prefix_escape(result, c, enc);
	}
	else if (c == '\n') {
	    prefix_escape(result, 'n', enc);
	}
	else if (c == '\r') {
	    prefix_escape(result, 'r', enc);
	}
	else if (c == '\t') {
	    prefix_escape(result, 't', enc);
	}
	else if (c == '\f') {
	    prefix_escape(result, 'f', enc);
	}
	else if (c == '\013') {
	    prefix_escape(result, 'v', enc);
	}
	else if (c == '\010') {
	    prefix_escape(result, 'b', enc);
	}
	else if (c == '\007') {
	    prefix_escape(result, 'a', enc);
	}
	else if (c == 033) {
	    prefix_escape(result, 'e', enc);
	}
	else if (rb_enc_isprint(c, enc)) {
	    rb_enc_str_buf_cat(result, p-n, n, enc);
	}
	else {
	    char buf[5];
	    char *s;
            const char *q;

            for (q = p-n; q < p; q++) {
                s = buf;
                sprintf(buf, "\\x%02X", *q & 0377);
                while (*s) {
                    str_cat_char(result, *s++, enc);
                }
            }
	}
    }
    str_cat_char(result, '"', enc);

    return result;
}

#define IS_EVSTR(p,e) ((p) < (e) && (*(p) == '$' || *(p) == '@' || *(p) == '{'))

/*
 *  call-seq:
 *     str.dump   => new_str
 *  
 *  Produces a version of <i>str</i> with all nonprinting characters replaced by
 *  <code>\nnn</code> notation and all special characters escaped.
 */

static VALUE
rb_str_dump(VALUE str, SEL sel)
{
    rb_encoding *enc0 = rb_enc_get(str);
    long len;
    const char *p, *pend;
    char *q, *qend;

    len = 2;			/* "" */
    p = RSTRING_PTR(str); 
    pend = p + RSTRING_LEN(str);
    while (p < pend) {
	unsigned char c = *p++;
	switch (c) {
	  case '"':  case '\\':
	  case '\n': case '\r':
	  case '\t': case '\f':
	  case '\013': case '\010': case '\007': case '\033':
	    len += 2;
	    break;

	  case '#':
	    len += IS_EVSTR(p, pend) ? 2 : 1;
	    break;

	  default:
	    if (ISPRINT(c)) {
		len++;
	    }
	    else {
		len += 4;		/* \xNN */
	    }
	    break;
	}
    }
    if (!rb_enc_asciicompat(enc0)) {
	len += 19;		/* ".force_encoding('')" */
	len += strlen(rb_enc_name(enc0));
    }

    p = RSTRING_PTR(str);
    pend = p + RSTRING_LEN(str);

    char *q_beg = q = (char *)alloca(len);
    qend = q + len;

    *q++ = '"';
    while (p < pend) {
	unsigned char c = *p++;

	if (c == '"' || c == '\\') {
	    *q++ = '\\';
	    *q++ = c;
	}
	else if (c == '#') {
	    if (IS_EVSTR(p, pend)) *q++ = '\\';
	    *q++ = '#';
	}
	else if (c == '\n') {
	    *q++ = '\\';
	    *q++ = 'n';
	}
	else if (c == '\r') {
	    *q++ = '\\';
	    *q++ = 'r';
	}
	else if (c == '\t') {
	    *q++ = '\\';
	    *q++ = 't';
	}
	else if (c == '\f') {
	    *q++ = '\\';
	    *q++ = 'f';
	}
	else if (c == '\013') {
	    *q++ = '\\';
	    *q++ = 'v';
	}
	else if (c == '\010') {
	    *q++ = '\\';
	    *q++ = 'b';
	}
	else if (c == '\007') {
	    *q++ = '\\';
	    *q++ = 'a';
	}
	else if (c == '\033') {
	    *q++ = '\\';
	    *q++ = 'e';
	}
	else if (ISPRINT(c)) {
	    *q++ = c;
	}
	else {
	    *q++ = '\\';
	    sprintf(q, "x%02X", c);
	    q += 3;
	}
    }
    *q++ = '"';
    if (!rb_enc_asciicompat(enc0)) {
	sprintf(q, ".force_encoding(\"%s\")", rb_enc_name(enc0));
    }

    /* result from dump is ASCII */

    VALUE res = rb_str_new5(str, q_beg, strlen(q_beg));
    if (OBJ_TAINTED(str)) {
	OBJ_TAINT(res);
    }
    return res;
}


/*
 *  call-seq:
 *     str.upcase!   => str or nil
 *  
 *  Upcases the contents of <i>str</i>, returning <code>nil</code> if no changes
 *  were made.
 *  Note: case replacement is effective only in ASCII region.
 */

static VALUE
rb_str_upcase_bang(VALUE str, SEL sel)
{
    CFHashCode h;
    rb_str_modify(str);
    h = CFHash((CFTypeRef)str);
    CFStringUppercase((CFMutableStringRef)str, NULL);
    if (h == CFHash((CFTypeRef)str)) {
	return Qnil;
    }
    return str;
}


/*
 *  call-seq:
 *     str.upcase   => new_str
 *  
 *  Returns a copy of <i>str</i> with all lowercase letters replaced with their
 *  uppercase counterparts. The operation is locale insensitive---only
 *  characters ``a'' to ``z'' are affected.
 *  Note: case replacement is effective only in ASCII region.
 *     
 *     "hEllO".upcase   #=> "HELLO"
 */

static VALUE
rb_str_upcase(VALUE str, SEL sel)
{
    str = rb_str_new3(str);
    rb_str_upcase_bang(str, 0);
    return str;
}


/*
 *  call-seq:
 *     str.downcase!   => str or nil
 *  
 *  Downcases the contents of <i>str</i>, returning <code>nil</code> if no
 *  changes were made.
 *  Note: case replacement is effective only in ASCII region.
 */

static VALUE
rb_str_downcase_bang(VALUE str, SEL sel)
{
    CFHashCode h;
    rb_str_modify(str);
    h = CFHash((CFTypeRef)str);
    CFStringLowercase((CFMutableStringRef)str, NULL);
    if (h == CFHash((CFTypeRef)str)) {
	return Qnil;
    }
    return str;
}


/*
 *  call-seq:
 *     str.downcase   => new_str
 *  
 *  Returns a copy of <i>str</i> with all uppercase letters replaced with their
 *  lowercase counterparts. The operation is locale insensitive---only
 *  characters ``A'' to ``Z'' are affected.
 *  Note: case replacement is effective only in ASCII region.
 *     
 *     "hEllO".downcase   #=> "hello"
 */

static VALUE
rb_str_downcase(VALUE str, SEL sel)
{
    str = rb_str_new3(str);
    rb_str_downcase_bang(str, 0);
    return str;
}


/*
 *  call-seq:
 *     str.capitalize!   => str or nil
 *  
 *  Modifies <i>str</i> by converting the first character to uppercase and the
 *  remainder to lowercase. Returns <code>nil</code> if no changes are made.
 *  Note: case conversion is effective only in ASCII region.
 *     
 *     a = "hello"
 *     a.capitalize!   #=> "Hello"
 *     a               #=> "Hello"
 *     a.capitalize!   #=> nil
 */

static VALUE
rb_str_capitalize_bang(VALUE str, SEL sel)
{
    CFStringRef tmp;
    long i, n;
    bool changed;
    UniChar *buffer;

    rb_str_modify(str);
    n = CFStringGetLength((CFStringRef)str);
    if (n == 0) {
	return Qnil;
    }
    buffer = (UniChar *)alloca(sizeof(UniChar) * n);
    CFStringGetCharacters((CFStringRef)str, CFRangeMake(0, n), buffer);
    changed = false;
    if (iswascii(buffer[0]) && iswlower(buffer[0])) {
	buffer[0] = towupper(buffer[0]);
	changed = true;
    }
    for (i = 1; i < n; i++) {
	if (iswascii(buffer[0]) && iswupper(buffer[i])) {
	    buffer[i] = towlower(buffer[i]);
	    changed = true;
	}
    }
    if (!changed) {
	return Qnil;
    }
    tmp = CFStringCreateWithCharacters(NULL, buffer, n);
    CFStringReplaceAll((CFMutableStringRef)str, tmp);
    CFRelease(tmp);
    return str;
}


/*
 *  call-seq:
 *     str.capitalize   => new_str
 *  
 *  Returns a copy of <i>str</i> with the first character converted to uppercase
 *  and the remainder to lowercase.
 *  Note: case conversion is effective only in ASCII region.
 *     
 *     "hello".capitalize    #=> "Hello"
 *     "HELLO".capitalize    #=> "Hello"
 *     "123ABC".capitalize   #=> "123abc"
 */

static VALUE
rb_str_capitalize(VALUE str, SEL sel)
{
    str = rb_str_new3(str);
    rb_str_capitalize_bang(str, 0);
    return str;
}


/*
 *  call-seq: 
*     str.swapcase!   => str or nil
 *  
 *  Equivalent to <code>String#swapcase</code>, but modifies the receiver in
 *  place, returning <i>str</i>, or <code>nil</code> if no changes were made.
 *  Note: case conversion is effective only in ASCII region.
 */

static VALUE
rb_str_swapcase_bang(VALUE str, SEL sel)
{
    CFIndex i, n;
    UniChar *buffer;
    bool changed;

    rb_str_modify(str);

    n = CFStringGetLength((CFStringRef)str);
    if (n == 0) {
	return Qnil;
    }
   
    buffer = (UniChar *)CFStringGetCharactersPtr((CFStringRef)str);
    if (buffer == NULL) {
	buffer = (UniChar *)alloca(sizeof(UniChar) * n);
    	CFStringGetCharacters((CFStringRef)str, CFRangeMake(0, n), buffer);
    }
    for (i = 0, changed = false; i < n; i++) {
	UniChar c = buffer[i];
	if (!iswascii(c)) {
	    continue;
	}
	if (iswlower(c)) {
	    c = towupper(c);
	}
	else if (iswupper(c)) {
	    c = towlower(c);
	}
	else {
	    continue;
	}
	changed = true;
	buffer[i] = c;
    }
    if (!changed) {
	return Qnil;
    }
    CFStringDelete((CFMutableStringRef)str, CFRangeMake(0, n));
    CFStringAppendCharacters((CFMutableStringRef)str,
	    (const UniChar *)buffer, n);
    return str;
}


/*
 *  call-seq:
 *     str.swapcase   => new_str
 *  
 *  Returns a copy of <i>str</i> with uppercase alphabetic characters converted
 *  to lowercase and lowercase characters converted to uppercase.
 *  Note: case conversion is effective only in ASCII region.
 *     
 *     "Hello".swapcase          #=> "hELLO"
 *     "cYbEr_PuNk11".swapcase   #=> "CyBeR_pUnK11"
 */

static VALUE
rb_str_swapcase(VALUE str, SEL sel)
{
    str = rb_str_new3(str);
    rb_str_swapcase_bang(str, 0);
    return str;
}

typedef void str_charset_find_cb
(CFRange *, const CFRange *, CFStringRef, UniChar, void *);

static void
str_charset_find(CFStringRef str, VALUE *charsets, int charset_count,
		 bool squeeze_mode, str_charset_find_cb *cb, void *ctx)
{
    int i;
    long n;
    CFMutableCharacterSetRef charset;
    CFRange search_range, result_range; 

    if (charset_count == 0)
	return;

    n = CFStringGetLength((CFStringRef)str);
    if (n == 0)
    	return;

    charset = NULL;
    for (i = 0; i < charset_count; i++) {
	VALUE s = charsets[i];
	bool exclude;
	const char *sptr, *p;

	StringValue(s);

	sptr = RSTRING_PTR(s);
	exclude = sptr[0] == '^';

	p = NULL;
	if (exclude || (p = strchr(sptr, '-')) != NULL) {
	    CFMutableCharacterSetRef subset;
	    const char *b, *e;

	    b = exclude ? sptr + 1 : sptr;
	    e = sptr + strlen(sptr) - 1;
	    subset = CFCharacterSetCreateMutable(NULL);
	    if (p == NULL) {
		p = strchr(b, '-');
	    }
	    while (p != NULL) {
		if (p > b && *(p - 1) != '\\' && *(p + 1) != '\0') {
		    CFCharacterSetAddCharactersInRange(subset,
			    CFRangeMake(*(p - 1), *(p + 1) - *(p - 1) + 1));
		}
		if (p > b) {
		    CFStringRef substr;
		    substr = CFStringCreateWithBytes(NULL,
			    (const UInt8 *)b,
			    (CFIndex)p - (CFIndex)b,
			    kCFStringEncodingUTF8,
			    false);
		    assert(substr != NULL);
		    CFCharacterSetAddCharactersInString(subset, substr);
		    CFRelease(substr);
		}
		if (p == b) {
		    p = NULL; 
		}
		else {
		    b = p + 2;
		    p = strchr(b, '-');
		}
	    }
	    if (b <= e) {
		CFStringRef substr;
		substr = CFStringCreateWithBytes(NULL,
			(const UInt8 *)b,
			(CFIndex)e - (CFIndex)b + 1,
			kCFStringEncodingUTF8,
			false);
		assert(substr != NULL);
		CFCharacterSetAddCharactersInString(subset, substr);
		CFRelease(substr);
	    }

	    if (exclude) {
		CFCharacterSetInvert(subset);
	    }

	    if (charset == NULL) {
		charset = subset;
	    }
	    else {
		CFCharacterSetIntersect(charset, subset);
		CFRelease(subset);
	    }
	}
	else {
	    if (charset == NULL) {
		charset = CFCharacterSetCreateMutable(NULL);
		CFCharacterSetAddCharactersInString(charset, (CFStringRef)s);
	    }
	    else {
		CFCharacterSetRef subset;
		subset = CFCharacterSetCreateWithCharactersInString(NULL,
			(CFStringRef)s);
		CFCharacterSetIntersect(charset, subset);
		CFRelease(subset);	
	    }
	}
    }

    search_range = CFRangeMake(0, n);
#if 0 
    while (search_range.length != 0 
	    && CFStringFindCharacterFromSet(
		(CFStringRef)str,
		(CFCharacterSetRef)charset,
		search_range,
		0,
		&result_range)) {
	(*cb)(&search_range, (const CFRange *)&result_range, str, ctx);
    }
#else
    CFStringInlineBuffer buf;
    UniChar previous_char = 0;
    CFStringInitInlineBuffer((CFStringRef)str, &buf, search_range);
    do {
        long i;
	bool mutated = false;

	if (search_range.location + search_range.length < n) {
	    n = search_range.location + search_range.length;
	    CFStringInitInlineBuffer((CFStringRef)str, &buf, CFRangeMake(0, n));
	}

	result_range.length = 0;

	for (i = search_range.location;
	     i < search_range.location + search_range.length; 
	     i++) {

	    UniChar c;

	    c = CFStringGetCharacterFromInlineBuffer(&buf, i);
	    if (CFCharacterSetIsCharacterMember((CFCharacterSetRef)charset, 
						c)) {
		if (result_range.length == 0) {
		    result_range.location = i;
		    result_range.length = 1;
		    previous_char = c;
		}
		else {
		    if (result_range.location + result_range.length == i
			&& (!squeeze_mode || previous_char == c)) {
			result_range.length++;
		    }
		    else {
			(*cb)(&search_range, (const CFRange *)&result_range, 
			    str, previous_char, ctx);
			result_range.location = i;
			result_range.length = 1;
			previous_char = c;
			if (search_range.location + search_range.length < n) {
			    result_range.location -= n 
				- (search_range.location + search_range.length);
			    mutated = true;
			    break;
			}
		    }
		}
	    }
	}
	if (!mutated) {
	    if (result_range.length != 0) {
		(*cb)(&search_range, (const CFRange *)&result_range, str, 
			previous_char, ctx);
		result_range.length = 0;
		previous_char = 0;
	    }
	}
    }
    while (search_range.length != 0 && result_range.length != 0); 
#endif

    CFRelease(charset);	
}

struct tr_trans_cb_ctx {
    VALUE orepl;
    const char *src;
    long src_len;
    const char *repl;
    long repl_len;
    int sflag;
    bool changed;
    CFStringRef opt;
};

static inline void
trans_replace(CFMutableStringRef str, const CFRange *result_range, 
	      CFStringRef substr, CFRange *search_range, int sflag)
{
    assert(result_range->location + result_range->length 
	<= CFStringGetLength((CFStringRef)str));
    if (sflag == 0) {
	long n;
	for (n = result_range->location; 
	     n < result_range->location + result_range->length; 
	     n++)
	    CFStringReplace(str, CFRangeMake(n, 1), substr);
    }
    else {
	CFStringReplace(str, *result_range, substr);
	search_range->location = result_range->location + 1;
	search_range->length = RSTRING_LEN(str) - search_range->location;
    }	    
}

static void
rb_str_trans_cb(CFRange *search_range, const CFRange *result_range, 
    CFStringRef str, UniChar character, void *ctx)
{
    struct tr_trans_cb_ctx *_ctx;

    _ctx = (struct tr_trans_cb_ctx *)ctx;
    if (_ctx->repl_len == 0) {
	CFStringDelete((CFMutableStringRef)str, *result_range);
	search_range->length -= result_range->length 
	    + (result_range->location - search_range->location);
	search_range->location = result_range->location;
    }
    else if (_ctx->repl_len == 1) {
	trans_replace((CFMutableStringRef)str, result_range, 
	    (CFStringRef)_ctx->orepl, search_range, _ctx->sflag);
    }
    else if (_ctx->repl_len > 1) {
	if (_ctx->src_len == 1) {
	    if (_ctx->opt == NULL) {
		_ctx->opt = CFStringCreateWithBytes(NULL, 
		    (const UInt8 *)_ctx->repl, 1, kCFStringEncodingUTF8,
		    false);
	    }
	    trans_replace((CFMutableStringRef)str, result_range, 
	        (CFStringRef)_ctx->opt, search_range, _ctx->sflag);
	}
	else {
	    /* TODO: support all syntaxes */
	    char sb, se, rb, re;
	    bool s_is_range, r_is_range;
	    CFStringRef substr;
	    bool release_substr;
	    long delta;

	    sb = se = rb = re = 0;

	    if (_ctx->src_len == 3 && _ctx->src[1] == '-') {
		sb = _ctx->src[0];
		se = _ctx->src[2];
		s_is_range = true;
	    }
	    else {
		s_is_range = false;
		if (_ctx->src[0] == '^' || strchr(_ctx->src, '-') != NULL)
		    rb_raise(rb_eRuntimeError, "src argument value (%s) not " \
			    "supported yet", _ctx->src);
	    }

	    if (_ctx->repl_len == 3 && _ctx->repl[1] == '-') {
		rb = _ctx->repl[0];
		re = _ctx->repl[2];
		r_is_range = true;
	    }
	    else {
		r_is_range = false;
		if (_ctx->repl[0] == '^' || strchr(_ctx->repl, '-') != NULL)
		    rb_raise(rb_eRuntimeError, "repl argument value (%s) not " \
			    "supported yet", _ctx->repl);
	    }

	    if (s_is_range) {
		assert(sb <= character && se >= character);
		delta = character - sb;
	    }
	    else {
		char *p;
		p = strchr(_ctx->src, character);
		assert(p != NULL);
		delta = (long)p - (long)_ctx->src;
	    }

	    if ((r_is_range && delta > (re - rb))
		    || (!r_is_range && delta > _ctx->repl_len)) {
		if (_ctx->opt == NULL) {
		    _ctx->opt = CFStringCreateWithBytes(NULL, 
			    (const UInt8 *)&_ctx->repl[_ctx->repl_len - 1], 
			    1, 
			    kCFStringEncodingUTF8,
			    false);
		}
		substr = _ctx->opt;
		release_substr = false;
	    }
	    else {
		const char r = r_is_range
		    ? rb + delta : _ctx->repl[delta];
		substr = CFStringCreateWithBytes(NULL, (const UInt8 *)&r, 1, 
			kCFStringEncodingUTF8, false);
		release_substr = true;
	    }

	    trans_replace((CFMutableStringRef)str, result_range, 
	        (CFStringRef)substr, search_range, _ctx->sflag);

	    if (release_substr)
		CFRelease(substr);
	}
    }
    _ctx->changed = true;
}

static VALUE
tr_trans(VALUE str, VALUE src, VALUE repl, int sflag)
{
    struct tr_trans_cb_ctx _ctx;

    StringValue(src);
    StringValue(repl);
    
    if (RSTRING_LEN(str) == 0)
       return Qnil;
  
    rb_str_modify(str);

    _ctx.orepl = repl; 
    _ctx.src = RSTRING_PTR(src);
    _ctx.repl = RSTRING_PTR(repl);

    /* TODO: support non-8-bit src/repl */
    assert(_ctx.src != NULL && _ctx.repl != NULL);

    _ctx.src_len = strlen(_ctx.src);
    _ctx.repl_len = strlen(_ctx.repl);
    _ctx.sflag = sflag;
    _ctx.changed = false;
    _ctx.opt = NULL;

    str_charset_find((CFStringRef)str, &src, 1, _ctx.repl_len > 1,
	rb_str_trans_cb, &_ctx); 

    if (_ctx.opt != NULL)
	CFRelease(_ctx.opt);

    return _ctx.changed ? str : Qnil;
}

/*
 *  call-seq:
 *     str.tr!(from_str, to_str)   => str or nil
 *  
 *  Translates <i>str</i> in place, using the same rules as
 *  <code>String#tr</code>. Returns <i>str</i>, or <code>nil</code> if no
 *  changes were made.
 */

static VALUE
rb_str_tr_bang(VALUE str, SEL sel, VALUE src, VALUE repl)
{
    return tr_trans(str, src, repl, 0);
}


/*
 *  call-seq:
 *     str.tr(from_str, to_str)   => new_str
 *  
 *  Returns a copy of <i>str</i> with the characters in <i>from_str</i> replaced
 *  by the corresponding characters in <i>to_str</i>. If <i>to_str</i> is
 *  shorter than <i>from_str</i>, it is padded with its last character. Both
 *  strings may use the c1--c2 notation to denote ranges of characters, and
 *  <i>from_str</i> may start with a <code>^</code>, which denotes all
 *  characters except those listed.
 *     
 *     "hello".tr('aeiou', '*')    #=> "h*ll*"
 *     "hello".tr('^aeiou', '*')   #=> "*e**o"
 *     "hello".tr('el', 'ip')      #=> "hippo"
 *     "hello".tr('a-y', 'b-z')    #=> "ifmmp"
 */

static VALUE
rb_str_tr(VALUE str, SEL sel, VALUE src, VALUE repl)
{
    str = rb_str_new3(str);
    rb_str_tr_bang(str, 0, src, repl);
    return str;
}

/*
 *  call-seq:
 *     str.delete!([other_str]+)   => str or nil
 *  
 *  Performs a <code>delete</code> operation in place, returning <i>str</i>, or
 *  <code>nil</code> if <i>str</i> was not modified.
 */

static void
rb_str_delete_bang_cb(CFRange *search_range, const CFRange *result_range, 
    CFStringRef str, UniChar character, void *ctx)
{
    CFStringDelete((CFMutableStringRef)str, *result_range);
    search_range->length -= result_range->length 
	+ (result_range->location - search_range->location);
    search_range->location = result_range->location;
    *(bool *)ctx = true;
}

static VALUE
rb_str_delete_bang(VALUE str, SEL sel, int argc, VALUE *argv)
{
    bool changed;
    if (argc < 1) {
	rb_raise(rb_eArgError, "wrong number of arguments");
    }
    rb_str_modify(str);
    changed = false;
    str_charset_find((CFStringRef)str, argv, argc, false,
	rb_str_delete_bang_cb, &changed);
    if (!changed) {
    	return Qnil;
    }
    return str;
}

/*
 *  call-seq:
 *     str.delete([other_str]+)   => new_str
 *  
 *  Returns a copy of <i>str</i> with all characters in the intersection of its
 *  arguments deleted. Uses the same rules for building the set of characters as
 *  <code>String#count</code>.
 *     
 *     "hello".delete "l","lo"        #=> "heo"
 *     "hello".delete "lo"            #=> "he"
 *     "hello".delete "aeiou", "^e"   #=> "hell"
 *     "hello".delete "ej-m"          #=> "ho"
 */

static VALUE
rb_str_delete(VALUE str, SEL sel, int argc, VALUE *argv)
{
    str = rb_str_new3(str);
    rb_str_delete_bang(str, 0, argc, argv);
    return str;
}


/*
 *  call-seq:
 *     str.squeeze!([other_str]*)   => str or nil
 *  
 *  Squeezes <i>str</i> in place, returning either <i>str</i>, or
 *  <code>nil</code> if no changes were made.
 */

static void
rb_str_squeeze_bang_cb(CFRange *search_range, const CFRange *result_range, 
    CFStringRef str, UniChar character, void *ctx)
{
    if (result_range->length > 1) {
	CFRange to_delete = *result_range;
	to_delete.length--;
	CFStringDelete((CFMutableStringRef)str, to_delete);
	search_range->length -= result_range->length 
	    + (result_range->location - search_range->location);
	search_range->location = result_range->location + 1;
	*(bool *)ctx = true;
    }
}

static VALUE
rb_str_squeeze_bang(VALUE str, SEL sel, int argc, VALUE *argv)
{
    bool changed;
    VALUE all_chars;
    if (argc == 0) {
	argc = 1;
	all_chars = (VALUE)CFSTR("a-z");
	argv = &all_chars;
    }
    rb_str_modify(str);
    changed = false;
    str_charset_find((CFStringRef)str, argv, argc, true,
	rb_str_squeeze_bang_cb, &changed);
    if (!changed)
    	return Qnil;
    return str;
}


/*
 *  call-seq:
 *     str.squeeze([other_str]*)    => new_str
 *  
 *  Builds a set of characters from the <i>other_str</i> parameter(s) using the
 *  procedure described for <code>String#count</code>. Returns a new string
 *  where runs of the same character that occur in this set are replaced by a
 *  single character. If no arguments are given, all runs of identical
 *  characters are replaced by a single character.
 *     
 *     "yellow moon".squeeze                  #=> "yelow mon"
 *     "  now   is  the".squeeze(" ")         #=> " now is the"
 *     "putters shoot balls".squeeze("m-z")   #=> "puters shot balls"
 */

static VALUE
rb_str_squeeze(VALUE str, SEL sel, int argc, VALUE *argv)
{
    str = rb_str_new3(str);
    rb_str_squeeze_bang(str, 0, argc, argv);
    return str;
}


/*
 *  call-seq:
 *     str.tr_s!(from_str, to_str)   => str or nil
 *  
 *  Performs <code>String#tr_s</code> processing on <i>str</i> in place,
 *  returning <i>str</i>, or <code>nil</code> if no changes were made.
 */

static VALUE
rb_str_tr_s_bang(VALUE str, SEL sel, VALUE src, VALUE repl)
{
    return tr_trans(str, src, repl, 1);
}


/*
 *  call-seq:
 *     str.tr_s(from_str, to_str)   => new_str
 *  
 *  Processes a copy of <i>str</i> as described under <code>String#tr</code>,
 *  then removes duplicate characters in regions that were affected by the
 *  translation.
 *     
 *     "hello".tr_s('l', 'r')     #=> "hero"
 *     "hello".tr_s('el', '*')    #=> "h*o"
 *     "hello".tr_s('el', 'hx')   #=> "hhxo"
 */

static VALUE
rb_str_tr_s(VALUE str, SEL sel, VALUE src, VALUE repl)
{
    str = rb_str_new3(str);
    rb_str_tr_s_bang(str, 0, src, repl);
    return str;
}


/*
 *  call-seq:
 *     str.count([other_str]+)   => fixnum
 *  
 *  Each <i>other_str</i> parameter defines a set of characters to count.  The
 *  intersection of these sets defines the characters to count in
 *  <i>str</i>. Any <i>other_str</i> that starts with a caret (^) is
 *  negated. The sequence c1--c2 means all characters between c1 and c2.
 *     
 *     a = "hello world"
 *     a.count "lo"            #=> 5
 *     a.count "lo", "o"       #=> 2
 *     a.count "hello", "^l"   #=> 4
 *     a.count "ej-m"          #=> 4
 */

static void
rb_str_count_cb(CFRange *search_range, const CFRange *result_range, 
    CFStringRef str, UniChar character, void *ctx)
{
    (*(int *)ctx) += result_range->length;
}

static VALUE
rb_str_count(VALUE str, SEL sel, int argc, VALUE *argv)
{
    int count;
    if (argc < 1) {
	rb_raise(rb_eArgError, "wrong number of arguments");
    }
    count = 0;
    str_charset_find((CFStringRef)str, argv, argc, false,
	rb_str_count_cb, &count); 
    return INT2NUM(count);
}

/*
 *  call-seq:
 *     str.split(pattern=$;, [limit])   => anArray
 *  
 *  Divides <i>str</i> into substrings based on a delimiter, returning an array
 *  of these substrings.
 *     
 *  If <i>pattern</i> is a <code>String</code>, then its contents are used as
 *  the delimiter when splitting <i>str</i>. If <i>pattern</i> is a single
 *  space, <i>str</i> is split on whitespace, with leading whitespace and runs
 *  of contiguous whitespace characters ignored.
 *     
 *  If <i>pattern</i> is a <code>Regexp</code>, <i>str</i> is divided where the
 *  pattern matches. Whenever the pattern matches a zero-length string,
 *  <i>str</i> is split into individual characters. If <i>pattern</i> contains
 *  groups, the respective matches will be returned in the array as well.
 *     
 *  If <i>pattern</i> is omitted, the value of <code>$;</code> is used.  If
 *  <code>$;</code> is <code>nil</code> (which is the default), <i>str</i> is
 *  split on whitespace as if ` ' were specified.
 *     
 *  If the <i>limit</i> parameter is omitted, trailing null fields are
 *  suppressed. If <i>limit</i> is a positive number, at most that number of
 *  fields will be returned (if <i>limit</i> is <code>1</code>, the entire
 *  string is returned as the only entry in an array). If negative, there is no
 *  limit to the number of fields returned, and trailing null fields are not
 *  suppressed.
 *     
 *     " now's  the time".split        #=> ["now's", "the", "time"]
 *     " now's  the time".split(' ')   #=> ["now's", "the", "time"]
 *     " now's  the time".split(/ /)   #=> ["", "now's", "", "the", "time"]
 *     "1, 2.34,56, 7".split(%r{,\s*}) #=> ["1", "2.34", "56", "7"]
 *     "hello".split(//)               #=> ["h", "e", "l", "l", "o"]
 *     "hello".split(//, 3)            #=> ["h", "e", "llo"]
 *     "hi mom".split(%r{\s*})         #=> ["h", "i", "m", "o", "m"]
 *     
 *     "mellow yellow".split("ello")   #=> ["m", "w y", "w"]
 *     "1,2,,3,4,,".split(',')         #=> ["1", "2", "", "3", "4"]
 *     "1,2,,3,4,,".split(',', 4)      #=> ["1", "2", "", "3,4,,"]
 *     "1,2,,3,4,,".split(',', -4)     #=> ["1", "2", "", "3", "4", "", ""]
 */

static VALUE
rb_str_split_m(VALUE str, SEL sel, int argc, VALUE *argv)
{
    rb_encoding *enc;
    VALUE spat;
    VALUE limit;
    int awk_split = Qfalse;
    int spat_string = Qfalse;
    long beg, end, i = 0;
    int lim = 0;
    VALUE result, tmp;
    long clen;

    clen = RSTRING_LEN(str);

    if (rb_scan_args(argc, argv, "02", &spat, &limit) == 2) {
	lim = NUM2INT(limit);
	if (lim <= 0) limit = Qnil;
	else if (lim == 1) {
	    if (clen == 0)
		return rb_ary_new2(0);
	    return rb_ary_new3(1, str);
	}
	i = 1;
    }

    enc = STR_ENC_GET(str);
    result = rb_ary_new();
    if (NIL_P(spat)) {
	if (!NIL_P(rb_fs)) {
	    spat = rb_fs;
	    goto fs_set;
	}
	awk_split = Qtrue;
    }
    else {
      fs_set:
	if (TYPE(spat) == T_STRING) {
	    spat_string = Qtrue;
	    if (RSTRING_LEN(spat) == 1
		&& CFStringGetCharacterAtIndex((CFStringRef)spat, 0) == ' ') {
		awk_split = Qtrue;
	    }
	}
	else {
	    spat = get_pat(spat, 1);
	}
    }

    beg = 0;
    if (awk_split || spat_string) {
	CFRange search_range;
	CFCharacterSetRef charset = NULL;
	if (spat == Qnil)
	    charset = CFCharacterSetGetPredefined(
		kCFCharacterSetWhitespaceAndNewline);
	search_range = CFRangeMake(0, clen);
	do {
	    CFRange result_range;
	    CFRange substr_range;
	    if (spat != Qnil) {
		if (!CFStringFindWithOptions((CFStringRef)str, 
		    (CFStringRef)spat,
		    search_range,
		    0,
		    &result_range))
		    break;
	    }
	    else {
		if (!CFStringFindCharacterFromSet((CFStringRef)str,
		    charset, 
		    search_range,
		    0,
		    &result_range))
		    break;
	    }

	    substr_range.location = search_range.location;
	    substr_range.length = result_range.location 
		- search_range.location;

	    if (awk_split == Qfalse || substr_range.length > 0) {
		VALUE substr;
	       
		substr = rb_str_subseq(str, substr_range.location,
		    substr_range.length);

		if (awk_split == Qtrue) {
		    CFStringTrimWhitespace((CFMutableStringRef)substr);
		    if (CFStringGetLength((CFStringRef)substr) > 0)
			rb_ary_push(result, substr);
		}
		else {
		    rb_ary_push(result, substr);
		}
	    }

	    search_range.location = result_range.location 
		+ result_range.length;
	    search_range.length = clen - search_range.location;
	}
	while ((limit == Qnil || --lim > 1));
	beg = search_range.location;
    }
    else {
	long start = beg;
	long idx;
	int last_null = 0;
	struct re_registers *regs;

	while ((end = rb_reg_search(spat, str, start, 0)) >= 0) {
	    regs = RMATCH_REGS(rb_backref_get());
	    if (start == end && BEG(0) == END(0)) {
		if (0) {
		    break;
		}
		else if (last_null == 1) {
		    rb_ary_push(result, rb_str_subseq(str, beg, 1));
		    beg = start;
		}
		else {
                    if (start == clen)
                        start++;
                    else
			start += 1;
		    last_null = 1;
		    continue;
		}
	    }
	    else {
		rb_ary_push(result, rb_str_subseq(str, beg, end-beg));
		beg = start = END(0);
	    }
	    last_null = 0;

	    for (idx=1; idx < regs->num_regs; idx++) {
		if (BEG(idx) == -1) continue;
		if (BEG(idx) == END(idx))
		    tmp = rb_str_new5(str, 0, 0);
		else
		    tmp = rb_str_subseq(str, BEG(idx), END(idx)-BEG(idx));
		rb_ary_push(result, tmp);
	    }
	    if (!NIL_P(limit) && lim <= ++i) break;
	}
    }
    if (clen > 0 && (!NIL_P(limit) || clen > beg || lim < 0)) {
	if (clen == beg) {
	    tmp = rb_str_new5(str, 0, 0);
	}
	else {
	    tmp = rb_str_subseq(str, beg, clen-beg);
	}
	rb_ary_push(result, tmp);
    }
    if (NIL_P(limit) && lim == 0) {
	while (RARRAY_LEN(result) > 0 &&
	       RSTRING_LEN(RARRAY_AT(result, RARRAY_LEN(result)-1)) == 0)
	    rb_ary_pop(result);
    }

    return result;
}

VALUE
rb_str_split(VALUE str, const char *sep0)
{
    VALUE sep;

    StringValue(str);
    sep = rb_str_new2(sep0);
    return rb_str_split_m(str, 0, 1, &sep);
}

VALUE
rb_str_split2(VALUE str, VALUE sep)
{
    StringValue(str);
    StringValue(sep);
    return rb_str_split_m(str, 0, 1, &sep);
}

/*
 *  Document-method: lines
 *  call-seq:
 *     str.lines(separator=$/)   => anEnumerator
 *     str.lines(separator=$/) {|substr| block }        => str
 *  
 *  Returns an enumerator that gives each line in the string.  If a block is
 *  given, it iterates over each line in the string.
 *     
 *     "foo\nbar\n".lines.to_a   #=> ["foo\n", "bar\n"]
 *     "foo\nb ar".lines.sort    #=> ["b ar", "foo\n"]
 */

/*
 *  Document-method: each_line
 *  call-seq:
 *     str.each_line(separator=$/) {|substr| block }   => str
 *  
 *  Splits <i>str</i> using the supplied parameter as the record separator
 *  (<code>$/</code> by default), passing each substring in turn to the supplied
 *  block. If a zero-length record separator is supplied, the string is split
 *  into paragraphs delimited by multiple successive newlines.
 *     
 *     print "Example one\n"
 *     "hello\nworld".each {|s| p s}
 *     print "Example two\n"
 *     "hello\nworld".each('l') {|s| p s}
 *     print "Example three\n"
 *     "hello\n\n\nworld".each('') {|s| p s}
 *     
 *  <em>produces:</em>
 *     
 *     Example one
 *     "hello\n"
 *     "world"
 *     Example two
 *     "hel"
 *     "l"
 *     "o\nworl"
 *     "d"
 *     Example three
 *     "hello\n\n\n"
 *     "world"
 */

static VALUE
rb_str_each_line(VALUE str, SEL sel, int argc, VALUE *argv)
{
    VALUE rs;
    long n;
    CFStringRef substr;
    CFRange sub_range, search_range, res_range;
    bool zero_sep;

    if (rb_scan_args(argc, argv, "01", &rs) == 0) {
	rs = rb_rs;
    }
    RETURN_ENUMERATOR(str, argc, argv);
    if (NIL_P(rs)) {
	rb_yield(str);
	return str;
    }
    StringValue(rs);
    zero_sep = CFStringGetLength((CFStringRef)rs) == 0;
    if (zero_sep) {
	rs = rb_default_rs;
    }
    n = CFStringGetLength((CFStringRef)str);
    search_range = CFRangeMake(0, n);
    sub_range = CFRangeMake(0, 0);

#define YIELD_SUBSTR(range) \
    do { \
	VALUE mcopy; \
	substr = CFStringCreateWithSubstring(NULL, (CFStringRef)str,  \
	    range); \
	mcopy = (VALUE)CFStringCreateMutableCopy(NULL, 0, \
	    (CFStringRef)substr); \
	CFMakeCollectable((CFTypeRef)mcopy); \
	rb_yield(mcopy); \
	CFRelease(substr); \
	RETURN_IF_BROKEN(); \
    } \
    while (0)

    while (CFStringFindWithOptions((CFStringRef)str, (CFStringRef)rs,
	search_range, 0, &res_range)) {
	if (zero_sep
	    && sub_range.length > 0 
	    && sub_range.location + sub_range.length 
	       == res_range.location) {
	    sub_range.length += res_range.length;
	}		
	else {
	    if (sub_range.length > 0)
		YIELD_SUBSTR(sub_range);
	    sub_range = CFRangeMake(search_range.location, 
		res_range.location - search_range.location + res_range.length);
	}
	search_range.location = res_range.location + res_range.length;
	search_range.length = n - search_range.location;
    }

    if (sub_range.length != 0)
	YIELD_SUBSTR(sub_range);

    if (search_range.location < n)
	YIELD_SUBSTR(CFRangeMake(search_range.location, 
	    n - search_range.location));

#undef YIELD_SUBSTR

    return str;
}

/*
 *  Document-method: bytes
 *  call-seq:
 *     str.bytes   => anEnumerator
 *     str.bytes {|fixnum| block }    => str
 *  
 *  Returns an enumerator that gives each byte in the string.  If a block is
 *  given, it iterates over each byte in the string.
 *     
 *     "hello".bytes.to_a        #=> [104, 101, 108, 108, 111]
 */

/*
 *  Document-method: each_byte
 *  call-seq:
 *     str.each_byte {|fixnum| block }    => str
 *  
 *  Passes each byte in <i>str</i> to the given block.
 *     
 *     "hello".each_byte {|c| print c, ' ' }
 *     
 *  <em>produces:</em>
 *     
 *     104 101 108 108 111
 */

static VALUE
rb_str_each_byte(VALUE str, SEL sel)
{
    RETURN_ENUMERATOR(str, 0, 0);

    long n = RSTRING_LEN(str);
    if (n == 0) {
	return str;
    }

    CFStringEncoding encoding = CFStringGetSmallestEncoding((CFStringRef)str);
    const long buflen = CFStringGetMaximumSizeForEncoding(n, encoding);
    UInt8 *buffer = (UInt8 *)alloca(buflen + 1);
    long used_buflen = 0;

    CFStringGetBytes((CFStringRef)str,
	    CFRangeMake(0, n),
	    encoding,
	    0,
	    false,
	    buffer,
	    buflen+1,
	    &used_buflen);

    long i;
    for (i = 0; i < used_buflen; i++) {
	rb_yield(INT2FIX(buffer[i]));
	RETURN_IF_BROKEN();
    }

    return str;
}


/*
 *  Document-method: chars
 *  call-seq:
 *     str.chars                   => anEnumerator
 *     str.chars {|substr| block } => str
 *  
 *  Returns an enumerator that gives each character in the string.
 *  If a block is given, it iterates over each character in the string.
 *     
 *     "foo".chars.to_a   #=> ["f","o","o"]
 */

/*
 *  Document-method: each_char
 *  call-seq:
 *     str.each_char {|cstr| block }    => str
 *  
 *  Passes each character in <i>str</i> to the given block.
 *     
 *     "hello".each_char {|c| print c, ' ' }
 *     
 *  <em>produces:</em>
 *     
 *     h e l l o 
 */

static VALUE
rb_str_each_char(VALUE str, SEL sel)
{
    CFStringInlineBuffer buf;
    long i, n;

    RETURN_ENUMERATOR(str, 0, 0);
    n = CFStringGetLength((CFStringRef)str);
    CFStringInitInlineBuffer((CFStringRef)str, &buf, CFRangeMake(0, n));
    for (i = 0; i < n; i++) {
	UniChar c;
	VALUE s;

	c = CFStringGetCharacterFromInlineBuffer(&buf, i);
	s = rb_str_new(NULL, 0);
	CFStringAppendCharacters((CFMutableStringRef)s, &c, 1);
	rb_yield(s);
	RETURN_IF_BROKEN();
    }
    return str;
}

/*
 *  call-seq:
 *     str.chop!   => str or nil
 *  
 *  Processes <i>str</i> as for <code>String#chop</code>, returning <i>str</i>,
 *  or <code>nil</code> if <i>str</i> is the empty string.  See also
 *  <code>String#chomp!</code>.
 */

static VALUE
rb_str_chop_bang(VALUE str, SEL sel)
{
    long n;
    const char *p;
    CFRange r;

    n = CFStringGetLength((CFStringRef)str);
    if (n == 0)
	return Qnil;
    rb_str_modify(str);
    p = RSTRING_PTR(str);
    r = CFRangeMake(n - 1, 1);
    if (n >= 2 && p[n - 1] == '\n' && p[n - 2] == '\r') {
	/* We need this to pass the tests, but this is most probably 
	 * unnecessary.
	 */
	r.location--;
	r.length++;
    }
    CFStringDelete((CFMutableStringRef)str, r);
    return str;
}


/*
 *  call-seq:
 *     str.chop   => new_str
 *  
 *  Returns a new <code>String</code> with the last character removed.  If the
 *  string ends with <code>\r\n</code>, both characters are removed. Applying
 *  <code>chop</code> to an empty string returns an empty
 *  string. <code>String#chomp</code> is often a safer alternative, as it leaves
 *  the string unchanged if it doesn't end in a record separator.
 *     
 *     "string\r\n".chop   #=> "string"
 *     "string\n\r".chop   #=> "string\n"
 *     "string\n".chop     #=> "string"
 *     "string".chop       #=> "strin"
 *     "x".chop.chop       #=> ""
 */

static VALUE
rb_str_chop(VALUE str, SEL sel)
{
    VALUE str2 = rb_str_new3(str);
    rb_str_chop_bang(str2, 0);
    return str2;
}


/*
 *  call-seq:
 *     str.chomp!(separator=$/)   => str or nil
 *  
 *  Modifies <i>str</i> in place as described for <code>String#chomp</code>,
 *  returning <i>str</i>, or <code>nil</code> if no modifications were made.
 */

static VALUE
rb_str_chomp_bang(VALUE str, SEL sel, int argc, VALUE *argv)
{
    VALUE rs;
    long len, rslen;
    CFRange range_result;

    if (rb_scan_args(argc, argv, "01", &rs) == 0)
	rs = rb_rs;
    rb_str_modify(str);
    if (rs == Qnil)
	return Qnil;
    len = CFStringGetLength((CFStringRef)str);
    if (len == 0)
	return Qnil;
    rslen = CFStringGetLength((CFStringRef)rs);
    range_result = CFRangeMake(len, 0);
    if (rs == rb_default_rs
	|| rslen == 0
	|| (rslen == 1 
	    && CFStringGetCharacterAtIndex((CFStringRef)rs, 0) == '\n')) {
	UniChar c;
	c = CFStringGetCharacterAtIndex((CFStringRef)str, 
		range_result.location - 1);
	if (c == '\n') {
	    range_result.location--;
	    range_result.length++;
	    c = CFStringGetCharacterAtIndex((CFStringRef)str, 
		    range_result.location - 1);
	}
	if (c == '\r' && (rslen > 0 || range_result.location != len)) {
	    /* MS is the devil */
	    range_result.location--;
	    range_result.length++;
	}
    }
    else {
	StringValue(rs);
	CFStringFindWithOptions((CFStringRef)str, (CFStringRef)rs,
	    CFRangeMake(len - rslen, rslen), 0, &range_result);
    }
    if (range_result.length == 0 
	|| range_result.location + range_result.length > len)
	return Qnil;
    CFStringDelete((CFMutableStringRef)str, range_result);
    return str;
}


/*
 *  call-seq:
 *     str.chomp(separator=$/)   => new_str
 *  
 *  Returns a new <code>String</code> with the given record separator removed
 *  from the end of <i>str</i> (if present). If <code>$/</code> has not been
 *  changed from the default Ruby record separator, then <code>chomp</code> also
 *  removes carriage return characters (that is it will remove <code>\n</code>,
 *  <code>\r</code>, and <code>\r\n</code>).
 *     
 *     "hello".chomp            #=> "hello"
 *     "hello\n".chomp          #=> "hello"
 *     "hello\r\n".chomp        #=> "hello"
 *     "hello\n\r".chomp        #=> "hello\n"
 *     "hello\r".chomp          #=> "hello"
 *     "hello \n there".chomp   #=> "hello \n there"
 *     "hello".chomp("llo")     #=> "he"
 */

static VALUE
rb_str_chomp(VALUE str, SEL sel, int argc, VALUE *argv)
{
    str = rb_str_new3(str);
    rb_str_chomp_bang(str, 0, argc, argv);
    return str;
}

/*
 *  call-seq:
 *     str.lstrip!   => self or nil
 *  
 *  Removes leading whitespace from <i>str</i>, returning <code>nil</code> if no
 *  change was made. See also <code>String#rstrip!</code> and
 *  <code>String#strip!</code>.
 *     
 *     "  hello  ".lstrip   #=> "hello  "
 *     "hello".lstrip!      #=> nil
 */

static VALUE
rb_str_strip_bang2(VALUE str, int direction)
{
    long i, n, orig_n;
    CFStringInlineBuffer buf;
    CFCharacterSetRef charset;
    bool changed;

    rb_str_modify(str);
    n = orig_n = CFStringGetLength((CFStringRef)str);
    if (n == 0)
	return Qnil;
    CFStringInitInlineBuffer((CFStringRef)str, &buf, CFRangeMake(0, n));
    charset = CFCharacterSetGetPredefined(kCFCharacterSetWhitespaceAndNewline);
    changed = false;

    if (direction >= 0) {
	for (i = n - 1; i >= 0; i--) {
	    UniChar c = CFStringGetCharacterFromInlineBuffer(&buf, i);
	    if (!CFCharacterSetIsCharacterMember(charset, c))
		break;
	}
	if (i < n - 1) {
	    CFRange range = CFRangeMake(i + 1, n - i - 1);
	    CFStringDelete((CFMutableStringRef)str, range);
	    n -= range.length;	    
	}
    }

    if (direction <= 0) {
	for (i = 0; i < n; i++) {
	    UniChar c = CFStringGetCharacterFromInlineBuffer(&buf, i);
	    if (!CFCharacterSetIsCharacterMember(charset, c))
		break;
	}
	if (i > 0) {
	    CFRange range = CFRangeMake(0, i);
	    CFStringDelete((CFMutableStringRef)str, range);
	}
    }

    return orig_n != n ? str : Qnil;
}

static VALUE
rb_str_lstrip_bang(VALUE str, SEL sel)
{
    return rb_str_strip_bang2(str, -1);
}


/*
 *  call-seq:
 *     str.lstrip   => new_str
 *  
 *  Returns a copy of <i>str</i> with leading whitespace removed. See also
 *  <code>String#rstrip</code> and <code>String#strip</code>.
 *     
 *     "  hello  ".lstrip   #=> "hello  "
 *     "hello".lstrip       #=> "hello"
 */

static VALUE
rb_str_lstrip(VALUE str)
{
    str = rb_str_dup(str);
    rb_str_lstrip_bang(str, 0);
    return str;
}


/*
 *  call-seq:
 *     str.rstrip!   => self or nil
 *  
 *  Removes trailing whitespace from <i>str</i>, returning <code>nil</code> if
 *  no change was made. See also <code>String#lstrip!</code> and
 *  <code>String#strip!</code>.
 *     
 *     "  hello  ".rstrip   #=> "  hello"
 *     "hello".rstrip!      #=> nil
 */

static VALUE
rb_str_rstrip_bang(VALUE str, SEL sel)
{
    return rb_str_strip_bang2(str, 1);
}


/*
 *  call-seq:
 *     str.rstrip   => new_str
 *  
 *  Returns a copy of <i>str</i> with trailing whitespace removed. See also
 *  <code>String#lstrip</code> and <code>String#strip</code>.
 *     
 *     "  hello  ".rstrip   #=> "  hello"
 *     "hello".rstrip       #=> "hello"
 */

static VALUE
rb_str_rstrip(VALUE str)
{
    str = rb_str_dup(str);
    rb_str_rstrip_bang(str, 0);
    return str;
}


/*
 *  call-seq:
 *     str.strip!   => str or nil
 *  
 *  Removes leading and trailing whitespace from <i>str</i>. Returns
 *  <code>nil</code> if <i>str</i> was not altered.
 */

static VALUE
rb_str_strip_bang(VALUE str, SEL sel)
{
    return rb_str_strip_bang2(str, 0);
}


/*
 *  call-seq:
 *     str.strip   => new_str
 *  
 *  Returns a copy of <i>str</i> with leading and trailing whitespace removed.
 *     
 *     "    hello    ".strip   #=> "hello"
 *     "\tgoodbye\r\n".strip   #=> "goodbye"
 */

static VALUE
rb_str_strip(VALUE str, SEL sel)
{
    str = rb_str_dup(str);
    rb_str_strip_bang(str, 0);
    return str;
}

static VALUE
scan_once(VALUE str, VALUE pat, long *start, long strlen, bool pat_is_string)
{
    VALUE result, match;
    struct re_registers *regs;
    long i;

    if (pat_is_string) {
	/* XXX this is sometimes slower than the regexp search, especially for
	 * long pattern strings 
	 */
	CFRange result_range;
	if (CFStringFindWithOptions((CFStringRef)str, 
	    (CFStringRef)pat,
	    CFRangeMake(*start, strlen - *start),
	    0,
	    &result_range)) {
	    CFStringRef substr = CFStringCreateWithSubstring(NULL, 
		(CFStringRef)str, result_range);
	    *start = result_range.location + result_range.length + 1;
	    result = (VALUE)CFStringCreateMutableCopy(NULL, 0, substr);
	    CFRelease(substr);
	    CFMakeCollectable((CFTypeRef)result);
	}
	else {
	    result = Qnil;
	}
	return result;
    }

    if (rb_reg_search(pat, str, *start, 0) >= 0) {
	match = rb_backref_get();
	regs = RMATCH_REGS(match);
	if (BEG(0) == END(0)) {
	    /*
	     * Always consume at least one character of the input string
	     */
		*start = END(0)+1;
	}
	else {
	    *start = END(0);
	}
	if (regs->num_regs == 1) {
	    return rb_reg_nth_match(0, match);
	}
	result = rb_ary_new2(regs->num_regs);
	for (i=1; i < regs->num_regs; i++) {
	    rb_ary_push(result, rb_reg_nth_match(i, match));
	}

	return result;
    }
    return Qnil;
}


/*
 *  call-seq:
 *     str.scan(pattern)                         => array
 *     str.scan(pattern) {|match, ...| block }   => str
 *  
 *  Both forms iterate through <i>str</i>, matching the pattern (which may be a
 *  <code>Regexp</code> or a <code>String</code>). For each match, a result is
 *  generated and either added to the result array or passed to the block. If
 *  the pattern contains no groups, each individual result consists of the
 *  matched string, <code>$&</code>.  If the pattern contains groups, each
 *  individual result is itself an array containing one entry per group.
 *     
 *     a = "cruel world"
 *     a.scan(/\w+/)        #=> ["cruel", "world"]
 *     a.scan(/.../)        #=> ["cru", "el ", "wor"]
 *     a.scan(/(...)/)      #=> [["cru"], ["el "], ["wor"]]
 *     a.scan(/(..)(..)/)   #=> [["cr", "ue"], ["l ", "wo"]]
 *     
 *  And the block form:
 *     
 *     a.scan(/\w+/) {|w| print "<<#{w}>> " }
 *     print "\n"
 *     a.scan(/(.)(.)/) {|x,y| print y, x }
 *     print "\n"
 *     
 *  <em>produces:</em>
 *     
 *     <<cruel>> <<world>>
 *     rceu lowlr
 */

static VALUE
rb_str_scan(VALUE str, SEL sel, VALUE pat)
{
    VALUE result;
    long start = 0;
    VALUE match = Qnil;
    long len = CFStringGetLength((CFStringRef)str);
    bool pat_is_string = TYPE(pat) == T_STRING;
    
    if (!pat_is_string) {
	pat = get_pat(pat, 1);
    }
    if (!rb_block_given_p()) {
	VALUE ary = rb_ary_new();

	while (!NIL_P(result = scan_once(str, pat, &start, len, 
					 pat_is_string))) {
	    match = rb_backref_get();
	    rb_ary_push(ary, result);
	}
	rb_backref_set(match);
	return ary;
    }

    while (!NIL_P(result = scan_once(str, pat, &start, len, pat_is_string))) {
	match = rb_backref_get();
	rb_match_busy(match);
	rb_yield(result);
	RETURN_IF_BROKEN();
	rb_backref_set(match);	/* restore $~ value */
    }
    rb_backref_set(match);
    return str;
}


/*
 *  call-seq:
 *     str.hex   => integer
 *  
 *  Treats leading characters from <i>str</i> as a string of hexadecimal digits
 *  (with an optional sign and an optional <code>0x</code>) and returns the
 *  corresponding number. Zero is returned on error.
 *     
 *     "0x0a".hex     #=> 10
 *     "-1234".hex    #=> -4660
 *     "0".hex        #=> 0
 *     "wombat".hex   #=> 0
 */

static VALUE
rb_str_hex(VALUE str, SEL sel)
{
    rb_encoding *enc = rb_enc_get(str);

    if (!rb_enc_asciicompat(enc)) {
	rb_raise(rb_eArgError, "ASCII incompatible encoding: %s", rb_enc_name(enc));
    }
    return rb_str_to_inum(str, 16, Qfalse);
}


/*
 *  call-seq:
 *     str.oct   => integer
 *  
 *  Treats leading characters of <i>str</i> as a string of octal digits (with an
 *  optional sign) and returns the corresponding number.  Returns 0 if the
 *  conversion fails.
 *     
 *     "123".oct       #=> 83
 *     "-377".oct      #=> -255
 *     "bad".oct       #=> 0
 *     "0377bad".oct   #=> 255
 */

static VALUE
rb_str_oct(VALUE str, SEL sel)
{
    rb_encoding *enc = rb_enc_get(str);

    if (!rb_enc_asciicompat(enc)) {
	rb_raise(rb_eArgError, "ASCII incompatible encoding: %s", rb_enc_name(enc));
    }
    return rb_str_to_inum(str, -8, Qfalse);
}


/*
 *  call-seq:
 *     str.crypt(other_str)   => new_str
 *  
 *  Applies a one-way cryptographic hash to <i>str</i> by invoking the standard
 *  library function <code>crypt</code>. The argument is the salt string, which
 *  should be two characters long, each character drawn from
 *  <code>[a-zA-Z0-9./]</code>.
 */

extern char *crypt(const char *, const char *);

static VALUE
rb_str_crypt(VALUE str, SEL sel, VALUE salt)
{
    StringValue(salt);
    if (RSTRING_LEN(salt) < 2) {
	rb_raise(rb_eArgError, "salt too short (need >=2 bytes)");
    }

    size_t str_len = RSTRING_LEN(str);
    char *s = alloca(str_len + 1);
    strncpy(s, RSTRING_PTR(str), str_len + 1);

    return rb_str_new2(crypt(s, RSTRING_PTR(salt)));
}


/*
 *  call-seq:
 *     str.intern   => symbol
 *     str.to_sym   => symbol
 *  
 *  Returns the <code>Symbol</code> corresponding to <i>str</i>, creating the
 *  symbol if it did not previously exist. See <code>Symbol#id2name</code>.
 *     
 *     "Koala".intern         #=> :Koala
 *     s = 'cat'.to_sym       #=> :cat
 *     s == :cat              #=> true
 *     s = '@cat'.to_sym      #=> :@cat
 *     s == :@cat             #=> true
 *
 *  This can also be used to create symbols that cannot be represented using the
 *  <code>:xxx</code> notation.
 *     
 *     'cat and dog'.to_sym   #=> :"cat and dog"
 */

VALUE
rb_str_intern_fast(VALUE s)
{
    return ID2SYM(rb_intern_str(s));
}

static VALUE
rb_str_intern(VALUE str, SEL sel)
{
    if (OBJ_TAINTED(str) && rb_safe_level() >= 1) {
	rb_raise(rb_eSecurityError, "Insecure: can't intern tainted string");
    }
    return rb_str_intern_fast(str);
}

/*
 *  call-seq:
 *     str.ord   => integer
 *  
 *  Return the <code>Integer</code> ordinal of a one-character string.
 *     
 *     "a".ord         #=> 97
 */

static VALUE
rb_str_ord(VALUE s, SEL sel)
{
    if (CFStringGetLength((CFStringRef)s) == 0) {
	rb_raise(rb_eArgError, "empty string");
    }
    return INT2NUM(CFStringGetCharacterAtIndex((CFStringRef)s, 0));
}

/*
 *  call-seq:
 *     str.sum(n=16)   => integer
 *  
 *  Returns a basic <em>n</em>-bit checksum of the characters in <i>str</i>,
 *  where <em>n</em> is the optional <code>Fixnum</code> parameter, defaulting
 *  to 16. The result is simply the sum of the binary value of each character in
 *  <i>str</i> modulo <code>2n - 1</code>. This is not a particularly good
 *  checksum.
 */

static VALUE
rb_str_sum(VALUE str, SEL sel, int argc, VALUE *argv)
{
    VALUE vbits;
    int bits;
    const char *ptr, *p, *pend;
    long len;

    if (argc == 0) {
	bits = 16;
    }
    else {
	rb_scan_args(argc, argv, "01", &vbits);
	bits = NUM2INT(vbits);
    }
    ptr = p = RSTRING_PTR(str);
    len = RSTRING_LEN(str);
    pend = p + len;
    if (bits >= sizeof(long)*CHAR_BIT) {
	VALUE sum = INT2FIX(0);

	while (p < pend) {
	    str_mod_check(str, ptr, len);
	    sum = rb_funcall(sum, '+', 1, INT2FIX((unsigned char)*p));
	    p++;
	}
	if (bits != 0) {
	    VALUE mod;

	    mod = rb_funcall(INT2FIX(1), rb_intern("<<"), 1, INT2FIX(bits));
	    mod = rb_funcall(mod, '-', 1, INT2FIX(1));
	    sum = rb_funcall(sum, '&', 1, mod);
	}
	return sum;
    }
    else {
       unsigned long sum = 0;

	while (p < pend) {
	    str_mod_check(str, ptr, len);
	    sum += (unsigned char)*p;
	    p++;
	}
	if (bits != 0) {
           sum &= (((unsigned long)1)<<bits)-1;
	}
	return rb_int2inum(sum);
    }
}

static inline void
rb_str_justify0(VALUE str, VALUE pad, long width, long padwidth, long index)
{
    do {
	if (padwidth > width) {
	    pad = (VALUE)CFStringCreateWithSubstring(
		    NULL,
		    (CFStringRef)pad,
		    CFRangeMake(0, width));
	    CFMakeCollectable((CFTypeRef)pad);
	}
	CFStringInsert((CFMutableStringRef)str, index, (CFStringRef)pad);
	width -= padwidth;
	index += padwidth;
    }
    while (width > 0);
}

static VALUE
rb_str_justify(int argc, VALUE *argv, VALUE str, char jflag)
{
    VALUE w, pad;
    long n, width, padwidth;

    rb_scan_args(argc, argv, "11", &w, &pad);
    width = NUM2LONG(w);

    if (NIL_P(pad)) {
	pad = rb_str_new(" ", 1);
	padwidth = 1;
    }
    else {
	StringValue(pad);
	padwidth = CFStringGetLength((CFStringRef)pad);
    }

    if (padwidth == 0) {
	rb_raise(rb_eArgError, "zero width padding");
    }

    n = CFStringGetLength((CFStringRef)str);
   
    str = rb_str_new3(str);
    if (width < 0 || width <= n) {
	return str;
    }
    width -= n;

    if (jflag == 'c') {
	rb_str_justify0(str, pad, ceil(width / 2.0), padwidth, n);
	rb_str_justify0(str, pad, floor(width / 2.0), padwidth, 0);
    }
    else if (jflag == 'l') {
	rb_str_justify0(str, pad, width, padwidth, n);
    }
    else if (jflag == 'r') {
	rb_str_justify0(str, pad, width, padwidth, 0);
    }
    else {
	rb_bug("invalid jflag");
    }

    if (OBJ_TAINTED(pad)) {
	OBJ_TAINT(str);
    }

    return str;
}


/*
 *  call-seq:
 *     str.ljust(integer, padstr=' ')   => new_str
 *  
 *  If <i>integer</i> is greater than the length of <i>str</i>, returns a new
 *  <code>String</code> of length <i>integer</i> with <i>str</i> left justified
 *  and padded with <i>padstr</i>; otherwise, returns <i>str</i>.
 *     
 *     "hello".ljust(4)            #=> "hello"
 *     "hello".ljust(20)           #=> "hello               "
 *     "hello".ljust(20, '1234')   #=> "hello123412341234123"
 */

static VALUE
rb_str_ljust(VALUE str, SEL sel, int argc, VALUE *argv)
{
    return rb_str_justify(argc, argv, str, 'l');
}


/*
 *  call-seq:
 *     str.rjust(integer, padstr=' ')   => new_str
 *  
 *  If <i>integer</i> is greater than the length of <i>str</i>, returns a new
 *  <code>String</code> of length <i>integer</i> with <i>str</i> right justified
 *  and padded with <i>padstr</i>; otherwise, returns <i>str</i>.
 *     
 *     "hello".rjust(4)            #=> "hello"
 *     "hello".rjust(20)           #=> "               hello"
 *     "hello".rjust(20, '1234')   #=> "123412341234123hello"
 */

static VALUE
rb_str_rjust(VALUE str, SEL sel, int argc, VALUE *argv)
{
    return rb_str_justify(argc, argv, str, 'r');
}


/*
 *  call-seq:
 *     str.center(integer, padstr)   => new_str
 *  
 *  If <i>integer</i> is greater than the length of <i>str</i>, returns a new
 *  <code>String</code> of length <i>integer</i> with <i>str</i> centered and
 *  padded with <i>padstr</i>; otherwise, returns <i>str</i>.
 *     
 *     "hello".center(4)         #=> "hello"
 *     "hello".center(20)        #=> "       hello        "
 *     "hello".center(20, '123') #=> "1231231hello12312312"
 */

static VALUE
rb_str_center(VALUE str, SEL sel, int argc, VALUE *argv)
{
    return rb_str_justify(argc, argv, str, 'c');
}

/*
 *  call-seq:
 *     str.partition(sep)              => [head, sep, tail]
 *  
 *  Searches the string for <i>sep</i> and returns the part before
 *  it, the <i>sep</i>, and the part after it.  If <i>sep</i> is not found,
 *  returns <i>str</i> and two empty strings.
 *     
 *     "hello".partition("l")         #=> ["he", "l", "lo"]
 *     "hello".partition("x")         #=> ["hello", "", ""]
 */

static VALUE
rb_str_partition(VALUE str, SEL sel, VALUE sep)
{
    long pos;
    int regex = Qfalse;
    long strlen, seplen = 0;

    if (TYPE(sep) == T_REGEXP) {
	pos = rb_reg_search(sep, str, 0, 0);
	regex = Qtrue;
    }
    else {
	VALUE tmp;

	tmp = rb_check_string_type(sep);
	if (NIL_P(tmp)) {
	    rb_raise(rb_eTypeError, "type mismatch: %s given",
		     rb_obj_classname(sep));
	}
	pos = rb_str_index(str, sep, 0);
	seplen = CFStringGetLength((CFStringRef)sep);
    }
    if (pos < 0) {
      failed:
	return rb_ary_new3(3, str, rb_str_new(0,0),rb_str_new(0,0));
    }
    if (regex) {
	sep = rb_str_subpat(str, sep, 0);
	seplen = CFStringGetLength((CFStringRef)sep);
	if (pos == 0 && seplen == 0) goto failed;
    }
    strlen = CFStringGetLength((CFStringRef)str);
    return rb_ary_new3(3, rb_str_subseq(str, 0, pos),
		          sep,
		          rb_str_subseq(str, pos+seplen,
					     strlen-pos-seplen));
}

/*
 *  call-seq:
 *     str.rpartition(sep)            => [head, sep, tail]
 *  
 *  Searches <i>sep</i> in the string from the end of the string, and
 *  returns the part before it, the <i>sep</i>, and the part after it.
 *  If <i>sep</i> is not found, returns two empty strings and
 *  <i>str</i>.
 *     
 *     "hello".rpartition("l")         #=> ["hel", "l", "o"]
 *     "hello".rpartition("x")         #=> ["", "", "hello"]
 */

static VALUE
rb_str_rpartition(VALUE str, SEL sel, VALUE sep)
{
    long pos = RSTRING_LEN(str);
    int regex = Qfalse;
    long seplen;

    if (TYPE(sep) == T_REGEXP) {
	pos = rb_reg_search(sep, str, pos, 1);
	regex = Qtrue;
    }
    else {
	VALUE tmp;

	tmp = rb_check_string_type(sep);
	if (NIL_P(tmp)) {
	    rb_raise(rb_eTypeError, "type mismatch: %s given",
		     rb_obj_classname(sep));
	}
	pos = rb_str_sublen(str, pos);
	pos = rb_str_rindex(str, sep, pos);
    }
    if (pos < 0) {
	return rb_ary_new3(3, rb_str_new(0,0),rb_str_new(0,0), str);
    }
    if (regex) {
	sep = rb_reg_nth_match(0, rb_backref_get());
	if (sep == Qnil)
	    return rb_ary_new3(3, rb_str_new(0,0),rb_str_new(0,0), str);
    }
    seplen = RSTRING_LEN(sep);
    return rb_ary_new3(3, rb_str_substr(str, 0, pos),
		          sep,
		          rb_str_substr(str, pos + seplen, seplen));
}

/*
 *  call-seq:
 *     str.start_with?([prefix]+)   => true or false
 *  
 *  Returns true if <i>str</i> starts with the prefix given.
 */

static VALUE
rb_str_start_with(VALUE str, SEL sel, int argc, VALUE *argv)
{
    int i;

    for (i = 0; i < argc; i++) {
	VALUE tmp = rb_check_string_type(argv[i]);
	if (NIL_P(tmp)) {
	    continue;
	}
	if (CFStringHasPrefix((CFStringRef)str, (CFStringRef)tmp)) {
	    return Qtrue;
	}
    }
    return Qfalse;
}

/*
 *  call-seq:
 *     str.end_with?([suffix]+)   => true or false
 *  
 *  Returns true if <i>str</i> ends with the suffix given.
 */

static VALUE
rb_str_end_with(VALUE str, SEL sel, int argc, VALUE *argv)
{
    int i;

    for (i = 0; i < argc; i++) {
	VALUE tmp = rb_check_string_type(argv[i]);
	if (NIL_P(tmp)) {
	    continue;
	}
	if (CFStringHasSuffix((CFStringRef)str, (CFStringRef)tmp)) {
	    return Qtrue;
	}
    }
    return Qfalse;
}

void
rb_str_setter(VALUE val, ID id, VALUE *var)
{
    if (!NIL_P(val) && TYPE(val) != T_STRING) {
	rb_raise(rb_eTypeError, "value of %s must be String", rb_id2name(id));
    }
    *var = val;
}


/*
 *  call-seq:
 *     str.force_encoding(encoding)   => str
 *
 *  Changes the encoding to +encoding+ and returns self.
 */

static VALUE
rb_str_force_encoding(VALUE str, SEL sel, VALUE enc)
{
    // TODO
    str_modifiable(str);
    return str;
}

/*
 *  call-seq:
 *     str.valid_encoding?  => true or false
 *  
 *  Returns true for a string which encoded correctly.
 *
 *    "\xc2\xa1".force_encoding("UTF-8").valid_encoding? => true
 *    "\xc2".force_encoding("UTF-8").valid_encoding? => false
 *    "\x80".force_encoding("UTF-8").valid_encoding? => false
 */

static VALUE
rb_str_valid_encoding_p(VALUE str, SEL sel)
{
    rb_notimplement();
}

/*
 *  call-seq:
 *     str.ascii_only?  => true or false
 *  
 *  Returns true for a string which has only ASCII characters.
 *
 *    "abc".force_encoding("UTF-8").ascii_only? => true
 *    "abc\u{6666}".force_encoding("UTF-8").ascii_only? => false
 */

static VALUE
rb_str_is_ascii_only_p(VALUE str, SEL sel)
{
    rb_notimplement();
}

static VALUE
rb_str_transform_bang(VALUE str, SEL sel, VALUE transform_name)
{
    CFRange range;

    rb_str_modify(str);
    StringValue(transform_name);
    
    range = CFRangeMake(0, RSTRING_LEN(str));

    if (!CFStringTransform((CFMutableStringRef)str, 
		&range,
		(CFStringRef)transform_name,
		false)) {
	rb_raise(rb_eRuntimeError, "cannot apply transformation `%s' to `%s'",
		RSTRING_PTR(transform_name), RSTRING_PTR(str));
    }

    return range.length == kCFNotFound ? Qnil : str;
}

static VALUE
rb_str_transform(VALUE str, SEL sel, VALUE transform_name)
{
    str = rb_str_dup(str);
    rb_str_transform_bang(str, 0, transform_name);
    return str;
}

/**********************************************************************
 * Document-class: Symbol
 *
 *  <code>Symbol</code> objects represent names and some strings
 *  inside the Ruby
 *  interpreter. They are generated using the <code>:name</code> and
 *  <code>:"string"</code> literals
 *  syntax, and by the various <code>to_sym</code> methods. The same
 *  <code>Symbol</code> object will be created for a given name or string
 *  for the duration of a program's execution, regardless of the context
 *  or meaning of that name. Thus if <code>Fred</code> is a constant in
 *  one context, a method in another, and a class in a third, the
 *  <code>Symbol</code> <code>:Fred</code> will be the same object in
 *  all three contexts.
 *     
 *     module One
 *       class Fred
 *       end
 *       $f1 = :Fred
 *     end
 *     module Two
 *       Fred = 1
 *       $f2 = :Fred
 *     end
 *     def Fred()
 *     end
 *     $f3 = :Fred
 *     $f1.object_id   #=> 2514190
 *     $f2.object_id   #=> 2514190
 *     $f3.object_id   #=> 2514190
 *     
 */


/*
 *  call-seq:
 *     sym == obj   => true or false
 *  
 *  Equality---If <i>sym</i> and <i>obj</i> are exactly the same
 *  symbol, returns <code>true</code>. Otherwise, compares them
 *  as strings.
 */

static VALUE
sym_equal(VALUE sym1, SEL sel, VALUE sym2)
{
    return sym1 == sym2 ? Qtrue : Qfalse;
}

static VALUE
sym_cmp(VALUE sym1, VALUE sym2)
{
    int code;

    if (CLASS_OF(sym2) != rb_cSymbol) {
	return Qnil;
    }
    code = strcmp(RSYMBOL(sym1)->str, RSYMBOL(sym2)->str);
    if (code > 0) {
	code = 1;
    }
    else if (code < 0) {
	code = -1;
    }
    return INT2FIX(code);
}

/*
 *  call-seq:
 *     sym.inspect    => string
 *  
 *  Returns the representation of <i>sym</i> as a symbol literal.
 *     
 *     :fred.inspect   #=> ":fred"
 */

static inline bool
sym_printable(const char *str, long len)
{
    // TODO multibyte symbols
    long i;
    for (i = 0; i < len; i++) {
	if (!isprint(str[i])) {
	    return false;
	}
    }
    return true;
}

static VALUE
sym_inspect(VALUE sym, SEL sel)
{
    const char *symstr = RSYMBOL(sym)->str;

    long len = strlen(symstr);
    if (len == 0) {
	return rb_str_new2(":\"\"");
    }

    VALUE str = rb_str_new2(":");
    if (!rb_symname_p(symstr) || !sym_printable(symstr, len)) {
	rb_str_buf_cat2(str, "\"");
	rb_str_buf_append(str, sym);
	rb_str_buf_cat2(str, "\"");
    }
    else {
	rb_str_buf_append(str, sym);
    }

    return str;
}


/*
 *  call-seq:
 *     sym.id2name   => string
 *     sym.to_s      => string
 *  
 *  Returns the name or string corresponding to <i>sym</i>.
 *     
 *     :fred.id2name   #=> "fred"
 */


static VALUE
rb_sym_to_s_imp(VALUE sym, SEL sel)
{
    return rb_str_new2(RSYMBOL(sym)->str);
}

VALUE
rb_sym_to_s(VALUE sym)
{
    return rb_sym_to_s_imp(sym, 0);
}

/*
 * call-seq:
 *   sym.to_sym   => sym
 *   sym.intern   => sym
 *
 * In general, <code>to_sym</code> returns the <code>Symbol</code> corresponding
 * to an object. As <i>sym</i> is already a symbol, <code>self</code> is returned
 * in this case.
 */

static VALUE
sym_to_sym(VALUE sym, SEL sel)
{
    return sym;
}

/*
 * call-seq:
 *   sym.to_proc
 *
 * Returns a _Proc_ object which respond to the given method by _sym_.
 *
 *   (1..3).collect(&:to_s)  #=> ["1", "2", "3"]
 */

static VALUE
sym_to_proc(VALUE sym, SEL sel)
{
    SEL msel = sel_registerName(rb_id2name(SYM2ID(sym)));
    rb_vm_block_t *b = rb_vm_create_block_calling_sel(msel);
    return rb_proc_alloc_with_block(rb_cProc, b);
}

ID
rb_to_id(VALUE name)
{
    VALUE tmp;
    ID id;

    switch (TYPE(name)) {
      default:
	tmp = rb_check_string_type(name);
	if (NIL_P(tmp)) {
	    rb_raise(rb_eTypeError, "%s is not a symbol",
		     RSTRING_PTR(rb_inspect(name)));
	}
	name = tmp;
	/* fall through */
      case T_STRING:
	name = rb_str_intern(name, 0);
	/* fall through */
      case T_SYMBOL:
	return SYM2ID(name);
    }
    return id;
}

#define PREPARE_RCV(x) \
    Class old = *(Class *)x; \
    *(Class *)x = (Class)rb_cCFString;

#define RESTORE_RCV(x) \
    *(Class *)x = old;

bool
rb_objc_str_is_pure(VALUE str)
{
    return *(Class *)str == (Class)rb_cCFString;
}

static CFIndex
imp_rb_str_length(void *rcv, SEL sel)
{
    CFIndex length;
    PREPARE_RCV(rcv);
    length = CFStringGetLength((CFStringRef)rcv);
    RESTORE_RCV(rcv);
    return length;
}

static UniChar
imp_rb_str_characterAtIndex(void *rcv, SEL sel, CFIndex idx)
{
    UniChar character;
    PREPARE_RCV(rcv);
    character = CFStringGetCharacterAtIndex((CFStringRef)rcv, idx);
    RESTORE_RCV(rcv);
    return character;
}

static void
imp_rb_str_getCharactersRange(void *rcv, SEL sel, UniChar *buffer, 
			      CFRange range)
{
    PREPARE_RCV(rcv);
    CFStringGetCharacters((CFStringRef)rcv, range, buffer);
    RESTORE_RCV(rcv);
}

static void
imp_rb_str_replaceCharactersInRangeWithString(void *rcv, SEL sel, 
					      CFRange range, void *str)
{
    PREPARE_RCV(rcv);
    CFStringReplace((CFMutableStringRef)rcv, range, (CFStringRef)str);
    RESTORE_RCV(rcv);
}

static const UniChar *
imp_rb_str_fastCharacterContents(void *rcv, SEL sel)
{
    const UniChar *ptr;
    PREPARE_RCV(rcv);
    ptr = CFStringGetCharactersPtr((CFStringRef)rcv);
    RESTORE_RCV(rcv);
    return ptr;
}

static const char *
imp_rb_str_fastCStringContents(void *rcv, SEL sel, bool nullTerminaisonRequired)
{
    const char *cstr;
    PREPARE_RCV(rcv);
    cstr = CFStringGetCStringPtr((CFStringRef)rcv, 0);
    /* XXX nullTerminaisonRequired should perhaps be honored */
    RESTORE_RCV(rcv);
    return cstr;
}

static CFStringEncoding
imp_rb_str_fastestEncodingInCFStringEncoding(void *rcv, SEL sel)
{
    CFStringEncoding encoding;
    PREPARE_RCV(rcv);
    encoding =  CFStringGetFastestEncoding((CFStringRef)rcv);
    RESTORE_RCV(rcv);
    return encoding;
}

static bool
imp_rb_str_isEqual(void *rcv, SEL sel, void *other)
{
    bool flag;
    PREPARE_RCV(rcv);
    flag = CFEqual((CFTypeRef)rcv, (CFTypeRef)other);    
    RESTORE_RCV(rcv);
    return flag;
}

void
rb_objc_install_string_primitives(Class klass)
{
    rb_objc_install_method2(klass, "length", (IMP)imp_rb_str_length);
    rb_objc_install_method2(klass, "characterAtIndex:",
	    (IMP)imp_rb_str_characterAtIndex);
    rb_objc_install_method2(klass, "getCharacters:range:",
	    (IMP)imp_rb_str_getCharactersRange);
    rb_objc_install_method2(klass, "_fastCharacterContents",
	    (IMP)imp_rb_str_fastCharacterContents);
    rb_objc_install_method2(klass, "_fastCStringContents:",
	    (IMP)imp_rb_str_fastCStringContents);
    rb_objc_install_method2(klass, "_fastestEncodingInCFStringEncoding",
	(IMP)imp_rb_str_fastestEncodingInCFStringEncoding);
    rb_objc_install_method2(klass, "isEqual:", (IMP)imp_rb_str_isEqual);

    const bool mutable = class_getSuperclass(klass)
	== (Class)rb_cNSMutableString;

    if (mutable) {
	rb_objc_install_method2(klass, "replaceCharactersInRange:withString:", 
		(IMP)imp_rb_str_replaceCharactersInRangeWithString);
    }

    rb_objc_define_method(*(VALUE *)klass, "alloc", str_alloc, 0);
}

static CFIndex
imp_rb_symbol_length(void *rcv, SEL sel)
{
    return RSYMBOL(rcv)->len;
}

static UniChar
imp_rb_symbol_characterAtIndex(void *rcv, SEL sel, CFIndex idx)
{
    if (idx < 0 || idx > RSYMBOL(rcv)->len) {
	rb_bug("[Symbol characterAtIndex:] out of bounds");
    }
    return RSYMBOL(rcv)->str[idx];
}

static void
imp_rb_symbol_getCharactersRange(void *rcv, SEL sel, UniChar *buffer, 
	CFRange range)
{
    int i;

    if (range.location + range.length > RSYMBOL(rcv)->len) {
	rb_bug("[Symbol getCharacters:range:] out of bounds");
    }

    for (i = range.location; i < range.location + range.length; i++) {
	*buffer = RSYMBOL(rcv)->str[i];
	buffer++;
    }
}

static bool
imp_rb_symbol_isEqual(void *rcv, SEL sel, void *other)
{
    if (rcv == other) {
	return true;
    }
    if (other == NULL || *(VALUE *)other != rb_cSymbol) {
	return false;
    }
    return CFStringCompare((CFStringRef)rcv, (CFStringRef)other, 0) == 0;
}

static void *
imp_rb_symbol_mutableCopy(void *rcv, SEL sel)
{
    CFMutableStringRef new_str = CFStringCreateMutable(NULL, 0);
    CFStringAppendCString(new_str, RSYMBOL(rcv)->str, kCFStringEncodingUTF8);
    CFMakeCollectable(new_str);
    return new_str;
}

static void
install_symbol_primitives(void)
{
    Class klass = (Class)rb_cSymbol;

    rb_objc_install_method2(klass, "length", (IMP)imp_rb_symbol_length);
    rb_objc_install_method2(klass, "characterAtIndex:", (IMP)imp_rb_symbol_characterAtIndex);
    rb_objc_install_method2(klass, "getCharacters:range:", (IMP)imp_rb_symbol_getCharactersRange);
    rb_objc_install_method2(klass, "isEqual:", (IMP)imp_rb_symbol_isEqual);
    rb_objc_install_method2(klass, "mutableCopy", (IMP)imp_rb_symbol_mutableCopy);
}

#undef INSTALL_METHOD

static inline void **
rb_bytestring_ivar_addr(VALUE bstr)
{
    return (void **)((char *)bstr + wrappedDataOffset);
}

CFMutableDataRef 
rb_bytestring_wrapped_data(VALUE bstr)
{
    void **addr = rb_bytestring_ivar_addr(bstr);
    return (CFMutableDataRef)(*addr); 
}

inline void
rb_bytestring_set_wrapped_data(VALUE bstr, CFMutableDataRef data)
{
    void **addr = rb_bytestring_ivar_addr(bstr);
    GC_WB(addr, data);
}

UInt8 *
rb_bytestring_byte_pointer(VALUE bstr)
{
    return CFDataGetMutableBytePtr(rb_bytestring_wrapped_data(bstr));
}

static inline VALUE
bytestring_alloc(void)
{
    return (VALUE)class_createInstance((Class)rb_cByteString, sizeof(void *));
}

static VALUE
rb_bytestring_alloc(VALUE klass, SEL sel)
{
    VALUE bstr = bytestring_alloc();

    CFMutableDataRef data = CFDataCreateMutable(NULL, 0);
    rb_bytestring_set_wrapped_data(bstr, data);
    CFMakeCollectable(data);

    return bstr;
}

VALUE 
rb_bytestring_new() 
{
    VALUE bs = rb_bytestring_alloc(0, 0);
    bs = (VALUE)objc_msgSend((id)bs, selInit); // [recv init];
    return bs;
}

VALUE
rb_bytestring_new_with_data(const UInt8 *buf, long size)
{
    VALUE v = rb_bytestring_new();
    CFDataAppendBytes(rb_bytestring_wrapped_data(v), buf, size);
    return v;
}

VALUE
rb_bytestring_new_with_cfdata(CFMutableDataRef data)
{
    VALUE v = bytestring_alloc();
    rb_bytestring_set_wrapped_data(v, data);
    return v;
}

static void inline
rb_bytestring_copy_cfstring_content(VALUE bstr, CFStringRef str)
{
	if (CFStringGetLength(str) == 0) return;
    const char *cptr = CFStringGetCStringPtr(str, kCFStringEncodingUTF8);
    assert(cptr != NULL); // TODO handle UTF-16 strings

    CFDataAppendBytes(rb_bytestring_wrapped_data(bstr), (UInt8 *)cptr, 
	    CFStringGetLength(str));
}

static VALUE
rb_bytestring_initialize(VALUE recv, SEL sel, int argc, VALUE *argv)
{
    VALUE orig;

    rb_scan_args(argc, argv, "01", &orig);

    recv = (VALUE)objc_msgSend((id)recv, selInit); // [recv init];

    if (!NIL_P(orig)) {
	StringValue(orig);
	rb_bytestring_copy_cfstring_content(recv, (CFStringRef)orig);
    }
    return orig;
}

VALUE
rb_coerce_to_bytestring(VALUE str)
{
    VALUE new = rb_bytestring_alloc(0, 0);
    rb_bytestring_copy_cfstring_content(new, (CFStringRef)str);
    return new;
}

inline long 
rb_bytestring_length(VALUE str)
{
    return CFDataGetLength(rb_bytestring_wrapped_data(str));
}

void
rb_bytestring_resize(VALUE str, long newsize)
{
    CFDataSetLength(rb_bytestring_wrapped_data(str), newsize);
}

static CFIndex
imp_rb_bytestring_length(void *rcv, SEL sel) 
{
    return rb_bytestring_length((VALUE)rcv);
}

static VALUE
rb_bytestring_getbyte(VALUE bstr, SEL sel, VALUE idx)
{
	long index = NUM2LONG(idx);
	while(idx < 0)
	{
		// adjusting for negative indices
		idx += rb_bytestring_length(bstr);
	}
	return INT2FIX(rb_bytestring_byte_pointer(bstr)[index]);
}

static VALUE
rb_bytestring_setbyte(VALUE bstr, SEL sel, VALUE idx, VALUE newbyte)
{
	long index = NUM2LONG(idx);
	while(idx < 0)
	{
		// adjusting for negative indices
		idx += rb_bytestring_length(bstr);
	}
	rb_bytestring_byte_pointer(bstr)[index] = FIX2UINT(newbyte);
	return Qnil;
}


static UniChar
imp_rb_bytestring_characterAtIndex(void *rcv, SEL sel, CFIndex idx)
{
    // XXX should be encoding aware
    return rb_bytestring_byte_pointer((VALUE)rcv)[idx];
}

static void
imp_rb_bytestring_replaceCharactersInRange_withString(void *rcv, SEL sel,
	CFRange range, void *str)
{
    const UInt8 *bytes = (const UInt8 *)RSTRING_PTR(str);
    const long length = RSTRING_LEN(str);
    CFMutableDataRef data = rb_bytestring_wrapped_data((VALUE)rcv);

    // No need to check if the given range fits in the data's bounds,
    // CFDataReplaceBytes() will grow the object automatically for us.
    CFDataReplaceBytes(data, range, bytes, length);
}

static void *
imp_rb_bytestring_mutableCopy(void *rcv, SEL sel)
{
    VALUE new_bstr = rb_bytestring_new();
    CFMutableDataRef rcv_data = rb_bytestring_wrapped_data((VALUE)rcv);
    CFMutableDataRef new_data = rb_bytestring_wrapped_data(new_bstr);
    CFDataAppendBytes(new_data, (const UInt8 *)CFDataGetMutableBytePtr(rcv_data),
	    CFDataGetLength(rcv_data));
    return (void *)new_bstr;
}

static void
imp_rb_bytestring_cfAppendCString_length(void *rcv, SEL sel, const UInt8 *cstr,
					 long len)
{
    CFDataAppendBytes(rb_bytestring_wrapped_data((VALUE)rcv), cstr, len);
}

static void
imp_rb_bytestring_setString(void *rcv, SEL sel, void *new_str)
{
    CFMutableDataRef data = rb_bytestring_wrapped_data((VALUE)rcv);
    CFRange data_range = CFRangeMake(0, CFDataGetLength(data));
    const char *cstr = RSTRING_PTR(new_str);
    const long len = RSTRING_LEN(new_str);
    CFDataReplaceBytes(data, data_range, (const UInt8 *)cstr, len);
} 

/*
 *  A <code>String</code> object holds and manipulates an arbitrary sequence of
 *  bytes, typically representing characters. String objects may be created
 *  using <code>String::new</code> or as literals.
 *     
 *  Because of aliasing issues, users of strings should be aware of the methods
 *  that modify the contents of a <code>String</code> object.  Typically,
 *  methods with names ending in ``!'' modify their receiver, while those
 *  without a ``!'' return a new <code>String</code>.  However, there are
 *  exceptions, such as <code>String#[]=</code>.
 *     
 */

void
Init_String(void)
{
    rb_cCFString = (VALUE)objc_getClass("NSCFString");
    rb_const_set(rb_cObject, rb_intern("NSCFString"), rb_cCFString);
    rb_cString = rb_cNSString = (VALUE)objc_getClass("NSString");
    rb_cNSMutableString = (VALUE)objc_getClass("NSMutableString");
    rb_const_set(rb_cObject, rb_intern("String"), rb_cNSMutableString);
    rb_set_class_path(rb_cNSMutableString, rb_cObject, "NSMutableString");

    rb_include_module(rb_cString, rb_mComparable);

    rb_objc_define_method(*(VALUE *)rb_cString, "try_convert", rb_str_s_try_convert, 1);
    rb_objc_define_method(rb_cString, "initialize", rb_str_init, -1);
    rb_objc_define_method(rb_cString, "initialize_copy", rb_str_replace_imp, 1);
    rb_objc_define_method(rb_cString, "<=>", rb_str_cmp_m, 1);
    rb_objc_define_method(rb_cString, "==", rb_str_equal_imp, 1);
    rb_objc_define_method(rb_cString, "eql?", rb_str_eql, 1);
    rb_objc_define_method(rb_cString, "casecmp", rb_str_casecmp, 1);
    rb_objc_define_method(rb_cString, "+", rb_str_plus, 1);
    rb_objc_define_method(rb_cString, "*", rb_str_times, 1);
    rb_objc_define_method(rb_cString, "%", rb_str_format_m, 1);
    rb_objc_define_method(rb_cString, "[]", rb_str_aref_m, -1);
    rb_objc_define_method(rb_cString, "[]=", rb_str_aset_m, -1);
    rb_objc_define_method(rb_cString, "insert", rb_str_insert, 2);
    rb_objc_define_method(rb_cString, "size", rb_str_length_imp, 0);
    rb_objc_define_method(rb_cString, "bytesize", rb_str_bytesize, 0);
    rb_objc_define_method(rb_cString, "empty?", rb_str_empty, 0);
    rb_objc_define_method(rb_cString, "=~", rb_str_match, 1);
    rb_objc_define_method(rb_cString, "match", rb_str_match_m, -1);
    rb_objc_define_method(rb_cString, "succ", rb_str_succ, 0);
    rb_objc_define_method(rb_cString, "succ!", rb_str_succ_bang, 0);
    rb_objc_define_method(rb_cString, "next", rb_str_succ, 0);
    rb_objc_define_method(rb_cString, "next!", rb_str_succ_bang, 0);
    rb_objc_define_method(rb_cString, "upto", rb_str_upto, -1);
    rb_objc_define_method(rb_cString, "index", rb_str_index_m, -1);
    rb_objc_define_method(rb_cString, "rindex", rb_str_rindex_m, -1);
    rb_objc_define_method(rb_cString, "replace", rb_str_replace_imp, 1);
    rb_objc_define_method(rb_cString, "clear", rb_str_clear, 0);
    rb_objc_define_method(rb_cString, "chr", rb_str_chr, 0);
    rb_objc_define_method(rb_cString, "getbyte", rb_str_getbyte, 1);
    rb_objc_define_method(rb_cString, "setbyte", rb_str_setbyte, 2);

    rb_objc_define_method(rb_cString, "to_i", rb_str_to_i, -1);
    rb_objc_define_method(rb_cString, "to_f", rb_str_to_f, 0);
    rb_objc_define_method(rb_cString, "to_s", rb_str_to_s, 0);
    rb_objc_define_method(rb_cString, "to_str", rb_str_to_s, 0);
    rb_objc_define_method(rb_cString, "inspect", rb_str_inspect, 0);
    rb_objc_define_method(rb_cString, "dump", rb_str_dump, 0);

    rb_objc_define_method(rb_cString, "upcase", rb_str_upcase, 0);
    rb_objc_define_method(rb_cString, "downcase", rb_str_downcase, 0);
    rb_objc_define_method(rb_cString, "capitalize", rb_str_capitalize, 0);
    rb_objc_define_method(rb_cString, "swapcase", rb_str_swapcase, 0);

    rb_objc_define_method(rb_cString, "upcase!", rb_str_upcase_bang, 0);
    rb_objc_define_method(rb_cString, "downcase!", rb_str_downcase_bang, 0);
    rb_objc_define_method(rb_cString, "capitalize!", rb_str_capitalize_bang, 0);
    rb_objc_define_method(rb_cString, "swapcase!", rb_str_swapcase_bang, 0);

    rb_objc_define_method(rb_cString, "hex", rb_str_hex, 0);
    rb_objc_define_method(rb_cString, "oct", rb_str_oct, 0);
    rb_objc_define_method(rb_cString, "split", rb_str_split_m, -1);
    rb_objc_define_method(rb_cString, "lines", rb_str_each_line, -1);
    rb_objc_define_method(rb_cString, "bytes", rb_str_each_byte, 0);
    rb_objc_define_method(rb_cString, "chars", rb_str_each_char, 0);
    rb_objc_define_method(rb_cString, "reverse", rb_str_reverse, 0);
    rb_objc_define_method(rb_cString, "reverse!", rb_str_reverse_bang, 0);
    rb_objc_define_method(rb_cString, "concat", rb_str_concat_imp, 1);
    rb_objc_define_method(rb_cString, "<<", rb_str_concat_imp, 1);
    rb_objc_define_method(rb_cString, "crypt", rb_str_crypt, 1);
    rb_objc_define_method(rb_cString, "intern", rb_str_intern, 0);
    rb_objc_define_method(rb_cString, "to_sym", rb_str_intern, 0);
    rb_objc_define_method(rb_cString, "ord", rb_str_ord, 0);

    rb_objc_define_method(rb_cString, "include?", rb_str_include, 1);
    rb_objc_define_method(rb_cString, "start_with?", rb_str_start_with, -1);
    rb_objc_define_method(rb_cString, "end_with?", rb_str_end_with, -1);

    rb_objc_define_method(rb_cString, "scan", rb_str_scan, 1);

    rb_objc_define_method(rb_cString, "ljust", rb_str_ljust, -1);
    rb_objc_define_method(rb_cString, "rjust", rb_str_rjust, -1);
    rb_objc_define_method(rb_cString, "center", rb_str_center, -1);

    rb_objc_define_method(rb_cString, "sub", rb_str_sub, -1);
    rb_objc_define_method(rb_cString, "gsub", rb_str_gsub, -1);
    rb_objc_define_method(rb_cString, "chop", rb_str_chop, 0);
    rb_objc_define_method(rb_cString, "chomp", rb_str_chomp, -1);
    rb_objc_define_method(rb_cString, "strip", rb_str_strip, 0);
    rb_objc_define_method(rb_cString, "lstrip", rb_str_lstrip, 0);
    rb_objc_define_method(rb_cString, "rstrip", rb_str_rstrip, 0);

    rb_objc_define_method(rb_cString, "sub!", rb_str_sub_bang, -1);
    rb_objc_define_method(rb_cString, "gsub!", rb_str_gsub_bang, -1);
    rb_objc_define_method(rb_cString, "chop!", rb_str_chop_bang, 0);
    rb_objc_define_method(rb_cString, "chomp!", rb_str_chomp_bang, -1);
    rb_objc_define_method(rb_cString, "strip!", rb_str_strip_bang, 0);
    rb_objc_define_method(rb_cString, "lstrip!", rb_str_lstrip_bang, 0);
    rb_objc_define_method(rb_cString, "rstrip!", rb_str_rstrip_bang, 0);

    rb_objc_define_method(rb_cString, "tr", rb_str_tr, 2);
    rb_objc_define_method(rb_cString, "tr_s", rb_str_tr_s, 2);
    rb_objc_define_method(rb_cString, "delete", rb_str_delete, -1);
    rb_objc_define_method(rb_cString, "squeeze", rb_str_squeeze, -1);
    rb_objc_define_method(rb_cString, "count", rb_str_count, -1);

    rb_objc_define_method(rb_cString, "tr!", rb_str_tr_bang, 2);
    rb_objc_define_method(rb_cString, "tr_s!", rb_str_tr_s_bang, 2);
    rb_objc_define_method(rb_cString, "delete!", rb_str_delete_bang, -1);
    rb_objc_define_method(rb_cString, "squeeze!", rb_str_squeeze_bang, -1);

    rb_objc_define_method(rb_cString, "each_line", rb_str_each_line, -1);
    rb_objc_define_method(rb_cString, "each_byte", rb_str_each_byte, 0);
    rb_objc_define_method(rb_cString, "each_char", rb_str_each_char, 0);

    rb_objc_define_method(rb_cString, "sum", rb_str_sum, -1);

    rb_objc_define_method(rb_cString, "slice", rb_str_aref_m, -1);
    rb_objc_define_method(rb_cString, "slice!", rb_str_slice_bang, -1);

    rb_objc_define_method(rb_cString, "partition", rb_str_partition, 1);
    rb_objc_define_method(rb_cString, "rpartition", rb_str_rpartition, 1);

    rb_objc_define_method(rb_cString, "encoding", rb_obj_encoding, 0); /* in encoding.c */
    rb_objc_define_method(rb_cString, "force_encoding", rb_str_force_encoding, 1);
    rb_objc_define_method(rb_cString, "valid_encoding?", rb_str_valid_encoding_p, 0);
    rb_objc_define_method(rb_cString, "ascii_only?", rb_str_is_ascii_only_p, 0);

    rb_objc_define_method(rb_cString, "transform", rb_str_transform, 1);
    rb_objc_define_method(rb_cString, "transform!", rb_str_transform_bang, 1);

    /* to return mutable copies */
    rb_objc_define_method(rb_cString, "dup", rb_str_dup_imp, 0);
    rb_objc_define_method(rb_cString, "clone", rb_str_clone, 0);

    id_to_s = rb_intern("to_s");

    rb_fs = Qnil;
    rb_define_variable("$;", &rb_fs);
    rb_define_variable("$-F", &rb_fs);

    /* rb_cSymbol is defined in parse.y because it's needed early */
    rb_set_class_path(rb_cSymbol, rb_cObject, "Symbol");
    rb_const_set(rb_cObject, rb_intern("Symbol"), rb_cSymbol);

    rb_undef_alloc_func(rb_cSymbol);
    rb_undef_method(CLASS_OF(rb_cSymbol), "new");
    rb_define_singleton_method(rb_cSymbol, "all_symbols", rb_sym_all_symbols, 0); /* in parse.y */

    rb_objc_define_method(rb_cSymbol, "==", sym_equal, 1);
    rb_objc_define_method(rb_cSymbol, "eql?", sym_equal, 1);
    rb_objc_define_method(rb_cSymbol, "<=>", sym_cmp, 1);
    rb_objc_define_method(rb_cSymbol, "inspect", sym_inspect, 0);
    rb_objc_define_method(rb_cSymbol, "description", sym_inspect, 0);
    rb_objc_define_method(rb_cSymbol, "dup", rb_obj_dup, 0);
    rb_objc_define_method(rb_cSymbol, "to_proc", sym_to_proc, 0);
    rb_objc_define_method(rb_cSymbol, "to_s", rb_sym_to_s_imp, 0);
    rb_objc_define_method(rb_cSymbol, "id2name", rb_sym_to_s_imp, 0);
    rb_objc_define_method(rb_cSymbol, "intern", sym_to_sym, 0);
    rb_objc_define_method(rb_cSymbol, "to_sym", sym_to_sym, 0);
 
    rb_undef_method(rb_cSymbol, "to_str");
    rb_undef_method(rb_cSymbol, "include?");

    install_symbol_primitives();

    rb_cByteString = (VALUE)objc_allocateClassPair((Class)rb_cNSMutableString,
	    "ByteString", sizeof(void *));
    RCLASS_SET_VERSION_FLAG(rb_cByteString, RCLASS_IS_STRING_SUBCLASS);
    class_addIvar((Class)rb_cByteString, WRAPPED_DATA_IV_NAME, sizeof(id), 
	    0, "@");
    objc_registerClassPair((Class)rb_cByteString);

    rb_objc_install_method2((Class)rb_cByteString, "length",
	    (IMP)imp_rb_bytestring_length);
    rb_objc_install_method2((Class)rb_cByteString, "characterAtIndex:",
	    (IMP)imp_rb_bytestring_characterAtIndex);
    rb_objc_install_method2((Class)rb_cByteString,
	    "replaceCharactersInRange:withString:",
	    (IMP)imp_rb_bytestring_replaceCharactersInRange_withString);
    rb_objc_install_method2((Class)rb_cByteString, "mutableCopy",
	    (IMP)imp_rb_bytestring_mutableCopy);
    rb_objc_install_method2((Class)rb_cByteString, "_cfAppendCString:length:",
	    (IMP)imp_rb_bytestring_cfAppendCString_length);
    rb_objc_install_method2((Class)rb_cByteString, "setString:",
	    (IMP)imp_rb_bytestring_setString);
    rb_objc_define_method(rb_cByteString, "initialize",
	    rb_bytestring_initialize, -1);
    rb_objc_define_method(*(VALUE *)rb_cByteString, "alloc",
	    rb_bytestring_alloc, 0);
	rb_objc_define_method(rb_cByteString, "getbyte", rb_bytestring_getbyte, 1);
	rb_objc_define_method(rb_cByteString, "setbyte", rb_bytestring_setbyte, 2);
    wrappedDataOffset = ivar_getOffset(
	    class_getInstanceVariable((Class)rb_cByteString,
		WRAPPED_DATA_IV_NAME));
}
