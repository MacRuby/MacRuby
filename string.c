/* 
 * MacRuby implementation of Ruby 1.9's string.c.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 * 
 * Copyright (C) 2007-2009, Apple Inc. All rights reserved.
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 * Copyright (C) 2000 Network Applied Communication Laboratory, Inc.
 * Copyright (C) 2000 Information-technology Promotion Agency, Japan
 */
#include "encoding.h"
#include "objc.h"
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>

#define OBJC_CLASS(x) (*(VALUE *)(x))

VALUE rb_cMRString;

#undef TYPE // TODO: remove this when MRString has become a child of NSString
extern VALUE rb_cMRString;
static inline int
rb_type2(VALUE obj)
{
    if (CLASS_OF(obj) == rb_cMRString) {
	return T_STRING;
    }
    else {
	return rb_type(obj);
    }
}
#define TYPE(obj) rb_type2(obj)


static void
str_update_flags_utf16(string_t *self)
{
    assert(str_is_stored_in_uchars(self) || NON_NATIVE_UTF16_ENC(self->encoding));

    bool ascii_only = true;
    bool has_supplementary = false;
    bool valid_encoding = true;
    // if the length is an odd number, it can't be valid UTF-16
    if (ODD_NUMBER(self->length_in_bytes)) {
	valid_encoding = false;
    }

    UChar *uchars = self->data.uchars;
    long uchars_count = BYTES_TO_UCHARS(self->length_in_bytes);
    bool native_byte_order = str_is_stored_in_uchars(self);
    UChar32 lead = 0;
    for (int i = 0; i < uchars_count; ++i) {
	UChar32 c;
	if (native_byte_order) {
	    c = uchars[i];
	}
	else {
	    uint8_t *bytes = (uint8_t *)&uchars[i];
	    c = (uint16_t)bytes[0] << 8 | (uint16_t)bytes[1];
	}
	if (U16_IS_SURROGATE(c)) { // surrogate
	    if (U16_IS_SURROGATE_LEAD(c)) { // lead surrogate
		// a lead surrogate should not be
		// after an other lead surrogate
		if (lead != 0) {
		    valid_encoding = false;
		}
		lead = c;
	    }
	    else { // trail surrogate
		// a trail surrogate must follow a lead surrogate
		if (lead == 0) {
		    valid_encoding = false;
		}
		else {
		    has_supplementary = true;
		    c = U16_GET_SUPPLEMENTARY(lead, c);
		    if (!U_IS_UNICODE_CHAR(c)) {
			valid_encoding = false;
		    }
		}
		lead = 0;
	    }
	}
	else { // not a surrogate
	    // a non-surrogate character should not be after a lead surrogate
	    // and it should be a valid Unicode character
	    // Warning: Ruby 1.9 does not do the IS_UNICODE_CHAR check
	    // (for 1.9, 0xffff is valid though it's not a Unicode character)
	    if ((lead != 0) || !U_IS_UNICODE_CHAR(c)) {
		valid_encoding = false;
	    }

	    if (c > 127) {
		ascii_only = false;
	    }
	}
    }
    // the last character should not be a lead surrogate
    if (lead != 0) {
	valid_encoding = false;
    }

    str_set_has_supplementary(self, has_supplementary);
    if (valid_encoding) {
	str_set_valid_encoding(self, true);
	str_set_ascii_only(self, ascii_only);
    }
    else {
	str_set_valid_encoding(self, false);
	str_set_ascii_only(self, false);
    }
}

void
str_update_flags(string_t *self)
{
    if (self->length_in_bytes == 0) {
	str_set_valid_encoding(self, true);
	str_set_ascii_only(self, true);
	str_set_has_supplementary(self, false);
    }
    else if (BINARY_ENC(self->encoding)) {
	str_set_valid_encoding(self, true);
	str_set_has_supplementary(self, false);
	bool ascii_only = true;
	for (long i = 0; i < self->length_in_bytes; ++i) {
	    if ((uint8_t)self->data.bytes[i] > 127) {
		ascii_only = false;
		break;
	    }
	}
	str_set_ascii_only(self, ascii_only);
    }
    else if (str_is_stored_in_uchars(self) || UTF16_ENC(self->encoding)) {
	str_update_flags_utf16(self);
    }
    else {
	self->encoding->methods.update_flags(self);
    }
}

static void
str_invert_byte_order(string_t *self)
{
    assert(NON_NATIVE_UTF16_ENC(self->encoding));

    long length_in_bytes = self->length_in_bytes;
    char *bytes = self->data.bytes;

    if (ODD_NUMBER(length_in_bytes)) {
	--length_in_bytes;
    }

    for (long i = 0; i < length_in_bytes; i += 2) {
	char tmp = bytes[i];
	bytes[i] = bytes[i+1];
	bytes[i+1] = tmp;
    }
    str_negate_stored_in_uchars(self);
}

static encoding_t *
str_compatible_encoding(string_t *str1, string_t *str2)
{
    if (str1->encoding == str2->encoding) {
	return str1->encoding;
    }
    if (str2->length_in_bytes == 0) {
	return str1->encoding;
    }
    if (str1->length_in_bytes == 0) {
	return str2->encoding;
    }
    if (!str1->encoding->ascii_compatible || !str2->encoding->ascii_compatible) {
	return NULL;
    }
    if (str_is_ruby_ascii_only(str1) && str_is_ruby_ascii_only(str2)) {
	return str1->encoding;
    }
    return NULL;
}

static encoding_t *
str_must_have_compatible_encoding(string_t *str1, string_t *str2)
{
    encoding_t *new_encoding = str_compatible_encoding(str1, str2);
    if (new_encoding == NULL) {
	rb_raise(rb_eEncCompatError, "incompatible character encodings: %s and %s",
		str1->encoding->public_name, str2->encoding->public_name);
    }
    return new_encoding;
}


static string_t *
str_alloc(void)
{
    NEWOBJ(str, string_t);
    str->basic.flags = 0;
    str->basic.klass = rb_cMRString;
    str->encoding = encodings[ENCODING_BINARY];
    str->capacity_in_bytes = 0;
    str->length_in_bytes = 0;
    str->data.bytes = NULL;
    str->flags = 0;
    return str;
}

extern VALUE rb_cString;
extern VALUE rb_cCFString;
extern VALUE rb_cNSString;
extern VALUE rb_cNSMutableString;
extern VALUE rb_cSymbol;
extern VALUE rb_cByteString;

static void
str_replace_with_string(string_t *self, string_t *source)
{
    if (self == source) {
	return;
    }
    self->flags = 0;
    self->encoding = source->encoding;
    self->capacity_in_bytes = self->length_in_bytes = source->length_in_bytes;
    self->flags = source->flags;
    if (self->length_in_bytes != 0) {
	GC_WB(&self->data.bytes, xmalloc(self->length_in_bytes));
	memcpy(self->data.bytes, source->data.bytes, self->length_in_bytes);
    }
}

static void
str_replace_with_cfstring(string_t *self, CFStringRef source)
{
    self->flags = 0;
    self->encoding = encodings[ENCODING_UTF16_NATIVE];
    self->capacity_in_bytes = self->length_in_bytes = UCHARS_TO_BYTES(CFStringGetLength(source));
    if (self->length_in_bytes != 0) {
	GC_WB(&self->data.uchars, xmalloc(self->length_in_bytes));
	CFStringGetCharacters((CFStringRef)source, CFRangeMake(0, BYTES_TO_UCHARS(self->length_in_bytes)), self->data.uchars);
	str_set_stored_in_uchars(self, true);
    }
}

static void
str_replace(string_t *self, VALUE arg)
{
    VALUE klass = CLASS_OF(arg);
    if (klass == rb_cMRString) {
	str_replace_with_string(self, STR(arg));
    }
    else if (klass == rb_cByteString) {
	self->encoding = encodings[ENCODING_BINARY];
	self->capacity_in_bytes = self->length_in_bytes = rb_bytestring_length(arg);
	if (self->length_in_bytes != 0) {
	    GC_WB(&self->data.bytes, xmalloc(self->length_in_bytes));
	    assert(self->data.bytes != NULL);
	    memcpy(self->data.bytes, rb_bytestring_byte_pointer(arg), self->length_in_bytes);
	}
    }
    else if (TYPE(arg) == T_STRING) {
	str_replace_with_cfstring(self, (CFStringRef)arg);
    }
    else if (klass == rb_cSymbol) {
	abort(); // TODO
    }
    else {
	str_replace(self, rb_str_to_str(arg));
    }
}

static string_t *
str_dup(VALUE source)
{
    string_t *destination = str_alloc();
    str_replace(destination, source);
    return destination;
}

static void
str_clear(string_t *self)
{
    self->length_in_bytes = 0;
}

static string_t *
str_new_from_string(string_t *source)
{
    string_t *destination = str_alloc();
    str_replace_with_string(destination, source);
    return destination;
}

static string_t *
str_new_from_cfstring(CFStringRef source)
{
    string_t *destination = str_alloc();
    str_replace_with_cfstring(destination, source);
    return destination;
}


static void
str_make_data_binary(string_t *self)
{
    if (!str_is_stored_in_uchars(self) || NATIVE_UTF16_ENC(self->encoding)) {
	// nothing to do
	return;
    }

    if (NON_NATIVE_UTF16_ENC(self->encoding)) {
	// Doing the conversion ourself is faster, and anyway ICU's converter
	// does not like non-paired surrogates.
	str_invert_byte_order(self);
	return;
    }

    self->encoding->methods.make_data_binary(self);
}

static bool
str_try_making_data_uchars(string_t *self)
{
    if (str_is_stored_in_uchars(self)) {
	return true;
    }
    else if (NON_NATIVE_UTF16_ENC(self->encoding)) {
	str_invert_byte_order(self);
	return true;
    }
    else if (BINARY_ENC(self->encoding)) {
	// you can't convert binary to anything
	return false;
    }
    else if (self->length_in_bytes == 0) {
	// for empty strings, nothing to convert
	str_set_stored_in_uchars(self, true);
	return true;
    }
    else if (str_known_to_have_an_invalid_encoding(self)) {
	return false;
    }

    return self->encoding->methods.try_making_data_uchars(self);
}

static void
str_make_same_format(string_t *str1, string_t *str2)
{
    if (str_is_stored_in_uchars(str1) != str_is_stored_in_uchars(str2)) {
	if (str_is_stored_in_uchars(str1)) {
	    if (!str_try_making_data_uchars(str2)) {
		str_make_data_binary(str1);
	    }
	}
	else {
	    str_make_data_binary(str2);
	}
    }
}

static long
str_length(string_t *self, bool ucs2_mode)
{
    if (self->length_in_bytes == 0) {
	return 0;
    }
    if (str_is_stored_in_uchars(self)) {
	long length;
	if (ucs2_mode) {
	    length = BYTES_TO_UCHARS(self->length_in_bytes);
	}
	else {
	    // we must return the length in Unicode code points,
	    // not the number of UChars, even if the probability
	    // we have surrogates is very low
	    length = u_countChar32(self->data.uchars, BYTES_TO_UCHARS(self->length_in_bytes));
	}
	if (ODD_NUMBER(self->length_in_bytes)) {
	    return length + 1;
	}
	else {
	    return length;
	}
    }
    else {
	if (self->encoding->single_byte_encoding) {
	    return self->length_in_bytes;
	}
	else if (ucs2_mode && NON_NATIVE_UTF16_ENC(self->encoding)) {
	    return div_round_up(self->length_in_bytes, 2);
	}
	else {
	    return self->encoding->methods.length(self, ucs2_mode);
	}
    }
}

static long
str_bytesize(string_t *self)
{
    if (str_is_stored_in_uchars(self)) {
	if (UTF16_ENC(self->encoding)) {
	    return self->length_in_bytes;
	}
	else {
	    return self->encoding->methods.bytesize(self);
	}
    }
    else {
	return self->length_in_bytes;
    }
}

static bool
str_getbyte(string_t *self, long index, unsigned char *c)
{
    if (str_is_stored_in_uchars(self) && NATIVE_UTF16_ENC(self->encoding)) {
	if (index < 0) {
	    index += self->length_in_bytes;
	    if (index < 0) {
		return false;
	    }
	}
	if (index >= self->length_in_bytes) {
	    return false;
	}
	if (NATIVE_UTF16_ENC(self->encoding)) {
	    *c = self->data.bytes[index];
	}
	else { // non native byte-order UTF-16
	    if ((index & 1) == 0) { // even
		*c = self->data.bytes[index+1];
	    }
	    else { // odd
		*c = self->data.bytes[index-1];
	    }
	}
    }
    else {
	// work with a binary string
	// (UTF-16 strings could be converted to their binary form
	//  on the fly but that would just add complexity)
	str_make_data_binary(self);

	if (index < 0) {
	    index += self->length_in_bytes;
	    if (index < 0) {
		return false;
	    }
	}
	if (index >= self->length_in_bytes) {
	    return false;
	}
	*c = self->data.bytes[index];
    }
    return true;
}

static void
str_setbyte(string_t *self, long index, unsigned char value)
{
    str_make_data_binary(self);
    if ((index < -self->length_in_bytes) || (index >= self->length_in_bytes)) {
	rb_raise(rb_eIndexError, "index %ld out of string", index);
    }
    if (index < 0) {
	index += self->length_in_bytes;
    }
    self->data.bytes[index] = value;
}

static void
str_force_encoding(string_t *self, encoding_t *enc)
{
    if (enc == self->encoding) {
	return;
    }
    str_make_data_binary(self);
    if (NATIVE_UTF16_ENC(self->encoding)) {
	str_set_stored_in_uchars(self, false);
    }
    self->encoding = enc;
    str_unset_facultative_flags(self);
    if (NATIVE_UTF16_ENC(self->encoding)) {
	str_set_stored_in_uchars(self, true);
    }
}

static string_t *
str_new_similar_empty_string(string_t *self)
{
    string_t *str = str_alloc();
    str->encoding = self->encoding;
    str->flags = self->flags & STRING_REQUIRED_FLAGS;
    return str;
}

static string_t *
str_new_copy_of_part(string_t *self, long offset_in_bytes, long length_in_bytes)
{
    string_t *str = str_alloc();
    str->encoding = self->encoding;
    str->capacity_in_bytes = str->length_in_bytes = length_in_bytes;
    str->flags = self->flags & STRING_REQUIRED_FLAGS;
    GC_WB(&str->data.bytes, xmalloc(length_in_bytes));
    memcpy(str->data.bytes, &self->data.bytes[offset_in_bytes], length_in_bytes);
    return str;
}

// you cannot cut a surrogate in an encoding that is not UTF-16
// (it's in theory possible to store the surrogate in
//  UTF-8 or UTF-32 but that would be incorrect Unicode)
NORETURN(static void
str_cannot_cut_surrogate(void))
{
    rb_raise(rb_eIndexError, "You can't cut a surrogate in two in an encoding that is not UTF-16");
}

static character_boundaries_t
str_get_character_boundaries(string_t *self, long index, bool ucs2_mode)
{
    character_boundaries_t boundaries = {-1, -1};

    if (str_is_stored_in_uchars(self)) {
	if (ucs2_mode || str_known_not_to_have_any_supplementary(self)) {
	    if (index < 0) {
		index += div_round_up(self->length_in_bytes, 2);
		if (index < 0) {
		    return boundaries;
		}
	    }
	    boundaries.start_offset_in_bytes = UCHARS_TO_BYTES(index);
	    boundaries.end_offset_in_bytes = boundaries.start_offset_in_bytes + 2;
	    if (!UTF16_ENC(self->encoding)) {
		long length = BYTES_TO_UCHARS(self->length_in_bytes);
		if ((index < length) && U16_IS_SURROGATE(self->data.uchars[index])) {
		    if (U16_IS_SURROGATE_LEAD(self->data.uchars[index])) {
			boundaries.end_offset_in_bytes = -1;
		    }
		    else { // U16_IS_SURROGATE_TRAIL
			boundaries.start_offset_in_bytes = -1;
		    }
		}
	    }
	}
	else {
	    // we don't have the length of the string, just the number of UChars
	    // (uchars_count >= number of characters)
	    long uchars_count = BYTES_TO_UCHARS(self->length_in_bytes);
	    if ((index < -uchars_count) || (index >= uchars_count)) {
		return boundaries;
	    }
	    const UChar *uchars = self->data.uchars;
	    long offset;
	    if (index < 0) {
		// count the characters from the end
		offset = uchars_count;
		while ((offset > 0) && (index < 0)) {
		    --offset;
		    // if the next character is a paired surrogate
		    // we need to go to the start of the whole surrogate
		    if (U16_IS_TRAIL(uchars[offset]) && (offset > 0) && U16_IS_LEAD(uchars[offset-1])) {
			--offset;
		    }
		    ++index;
		}
		// ended before the index got to 0
		if (index != 0) {
		    return boundaries;
		}
		assert(offset >= 0);
	    }
	    else {
		// count the characters from the start
		offset = 0;
		U16_FWD_N(uchars, offset, uchars_count, index);
		if (offset >= uchars_count) {
		    return boundaries;
		}
	    }

	    long length_in_bytes;
	    if (U16_IS_LEAD(uchars[offset]) && (offset < uchars_count - 1) && (U16_IS_TRAIL(uchars[offset+1]))) {
		// if it's a lead surrogate we must also copy the trail surrogate
		length_in_bytes = UCHARS_TO_BYTES(2);
	    }
	    else {
		length_in_bytes = UCHARS_TO_BYTES(1);
	    }
	    boundaries.start_offset_in_bytes = UCHARS_TO_BYTES(offset);
	    boundaries.end_offset_in_bytes = boundaries.start_offset_in_bytes + length_in_bytes;
	}
    }
    else { // data in binary
	if (self->encoding->single_byte_encoding) {
	    if (index < 0) {
		index += self->length_in_bytes;
		if (index < 0) {
		    return boundaries;
		}
	    }
	    boundaries.start_offset_in_bytes = index;
	    boundaries.end_offset_in_bytes = boundaries.start_offset_in_bytes + 1;
	}
	else if (UTF32_ENC(self->encoding) && (!ucs2_mode || str_known_not_to_have_any_supplementary(self))) {
	    if (index < 0) {
		index += div_round_up(self->length_in_bytes, 4);
		if (index < 0) {
		    return boundaries;
		}
	    }
	    boundaries.start_offset_in_bytes = index * 4;
	    boundaries.end_offset_in_bytes = boundaries.start_offset_in_bytes + 4;
	}
	else if (NON_NATIVE_UTF16_ENC(self->encoding) && (ucs2_mode || str_known_not_to_have_any_supplementary(self))) {
	    if (index < 0) {
		index += div_round_up(self->length_in_bytes, 2);
		if (index < 0) {
		    return boundaries;
		}
	    }
	    boundaries.start_offset_in_bytes = UCHARS_TO_BYTES(index);
	    boundaries.end_offset_in_bytes = boundaries.start_offset_in_bytes + 2;
	}
	else {
	    boundaries = self->encoding->methods.get_character_boundaries(self, index, ucs2_mode);
	}
    }

    return boundaries;
}

static string_t *
str_get_characters(string_t *self, long first, long last, bool ucs2_mode)
{
    if (self->length_in_bytes == 0) {
	if (first == 0) {
	    return str_new_similar_empty_string(self);
	}
	else {
	    return NULL;
	}
    }
    if (!self->encoding->single_byte_encoding && !str_is_stored_in_uchars(self)) {
	str_try_making_data_uchars(self);
    }
    character_boundaries_t first_boundaries = str_get_character_boundaries(self, first, ucs2_mode);
    character_boundaries_t last_boundaries = str_get_character_boundaries(self, last, ucs2_mode);

    if (first_boundaries.start_offset_in_bytes == -1) {
	if (last_boundaries.end_offset_in_bytes == -1) {
	    // you cannot cut a surrogate in an encoding that is not UTF-16
	    str_cannot_cut_surrogate();
	}
	else {
	    return NULL;
	}
    }
    else if (last_boundaries.end_offset_in_bytes == -1) {
	// you cannot cut a surrogate in an encoding that is not UTF-16
	str_cannot_cut_surrogate();
    }

    if (first_boundaries.start_offset_in_bytes == self->length_in_bytes) {
	return str_new_similar_empty_string(self);
    }
    else if (first_boundaries.start_offset_in_bytes > self->length_in_bytes) {
	return NULL;
    }
    if (last_boundaries.end_offset_in_bytes >= self->length_in_bytes) {
	last_boundaries.end_offset_in_bytes = self->length_in_bytes;
    }

    return str_new_copy_of_part(self, first_boundaries.start_offset_in_bytes,
	    last_boundaries.end_offset_in_bytes - first_boundaries.start_offset_in_bytes);
}

static string_t *
str_get_character_at(string_t *self, long index, bool ucs2_mode)
{
    if (self->length_in_bytes == 0) {
	return NULL;
    }
    if (!self->encoding->single_byte_encoding && !str_is_stored_in_uchars(self)) {
	// if we can't access the bytes directly,
	// try to convert the string in UTF-16
	str_try_making_data_uchars(self);
    }
    character_boundaries_t boundaries = str_get_character_boundaries(self, index, ucs2_mode);
    if (boundaries.start_offset_in_bytes == -1) {
	if (boundaries.end_offset_in_bytes == -1) {
	    return NULL;
	}
	else {
	    // you cannot cut a surrogate in an encoding that is not UTF-16
	    str_cannot_cut_surrogate();
	}
    }
    else if (boundaries.end_offset_in_bytes == -1) {
	// you cannot cut a surrogate in an encoding that is not UTF-16
	str_cannot_cut_surrogate();
    }

    if (boundaries.start_offset_in_bytes >= self->length_in_bytes) {
	return NULL;
    }
    if (boundaries.end_offset_in_bytes >= self->length_in_bytes) {
	boundaries.end_offset_in_bytes = self->length_in_bytes;
    }

    return str_new_copy_of_part(self, boundaries.start_offset_in_bytes, boundaries.end_offset_in_bytes - boundaries.start_offset_in_bytes);
}

static string_t *
str_plus_string(string_t *str1, string_t *str2)
{
    encoding_t *new_encoding = str_must_have_compatible_encoding(str1, str2);

    string_t *new_str = str_alloc();
    new_str->encoding = new_encoding;
    if ((str1->length_in_bytes == 0) && (str2->length_in_bytes == 0)) {
	return new_str;
    }

    str_make_same_format(str1, str2);

    str_set_stored_in_uchars(new_str, str_is_stored_in_uchars(str1));
    long length_in_bytes = str1->length_in_bytes + str2->length_in_bytes;
    new_str->data.bytes = xmalloc(length_in_bytes);
    if (str1->length_in_bytes > 0) {
	memcpy(new_str->data.bytes, str1->data.bytes, str1->length_in_bytes);
    }
    if (str2->length_in_bytes > 0) {
	memcpy(new_str->data.bytes + str1->length_in_bytes, str2->data.bytes, str2->length_in_bytes);
    }
    new_str->capacity_in_bytes = new_str->length_in_bytes = length_in_bytes;

    return new_str;
}

static void
str_concat_string(string_t *self, string_t *str)
{
    if (str->length_in_bytes == 0) {
	return;
    }
    if (self->length_in_bytes == 0) {
	str_replace_with_string(self, str);
	return;
    }

    str_must_have_compatible_encoding(self, str);
    str_make_same_format(self, str);

    long new_length_in_bytes = self->length_in_bytes + str->length_in_bytes;
    // TODO: we should maybe merge flags
    // (if both are ASCII-only, the concatenation is ASCII-only,
    //  though I'm not sure all the tests required are worth doing)
    str_unset_facultative_flags(self);
    if (self->capacity_in_bytes < new_length_in_bytes) {
	uint8_t *bytes = xmalloc(new_length_in_bytes);
	memcpy(bytes, self->data.bytes, self->length_in_bytes);
	GC_WB(&self->data.bytes, bytes);
	self->capacity_in_bytes = new_length_in_bytes;
    }
    memcpy(self->data.bytes + self->length_in_bytes, str->data.bytes, str->length_in_bytes);
    self->length_in_bytes = new_length_in_bytes;
}

static bool
str_is_equal_to_string(string_t *self, string_t *str)
{
    if (self == str) {
	return true;
    }

    if (self->length_in_bytes == 0) {
	if (str->length_in_bytes == 0) {
	    // both strings are empty
	    return true;
	}
	else {
	    // only self is empty
	    return false;
	}
    }
    else if (str->length_in_bytes == 0) {
	// only str is empty
	return false;
    }

    if (str_compatible_encoding(self, str) != NULL) {
	if (str_is_stored_in_uchars(self) == str_is_stored_in_uchars(str)) {
	    if (self->length_in_bytes != str->length_in_bytes) {
		return false;
	    }
	    else {
		return (memcmp(self->data.bytes, str->data.bytes, self->length_in_bytes) == 0);
	    }
	}
	else { // one is in uchars and the other is in binary
	    if (!str_try_making_data_uchars(self) || !str_try_making_data_uchars(str)) {
		// one is in uchars but the other one can't be converted in uchars
		return false;
	    }
	    if (self->length_in_bytes != str->length_in_bytes) {
		return false;
	    }
	    else {
		return (memcmp(self->data.bytes, str->data.bytes, self->length_in_bytes) == 0);
	    }
	}
    }
    else { // incompatible encodings
	return false;
    }
}

static long
str_offset_in_bytes_to_index(string_t *self, long offset_in_bytes, bool ucs2_mode)
{
    if ((offset_in_bytes >= self->length_in_bytes) || (offset_in_bytes < 0)) {
	return -1;
    }
    if (offset_in_bytes == 0) {
	return 0;
    }

    if (str_is_stored_in_uchars(self)) {
	if (ucs2_mode || str_known_not_to_have_any_supplementary(self)) {
	    return BYTES_TO_UCHARS(offset_in_bytes);
	}
	else {
	    long length = BYTES_TO_UCHARS(self->length_in_bytes);
	    long index = 0, i = 0;
	    for (;;) {
		if (U16_IS_LEAD(self->data.uchars[i]) && (i+1 < length) && U16_IS_TRAIL(self->data.uchars[i+1])) {
		    i += 2;
		}
		else {
		    ++i;
		}
		if (offset_in_bytes < i) {
		    return index;
		}
		++index;
		if (offset_in_bytes == i) {
		    return index;
		}
	    }
	}
    }
    else {
	if (self->encoding->single_byte_encoding) {
	    return offset_in_bytes;
	}
	else if (UTF32_ENC(self->encoding) && (!ucs2_mode || str_known_not_to_have_any_supplementary(self))) {
	    return offset_in_bytes / 4;
	}
	else if (NON_NATIVE_UTF16_ENC(self->encoding) && (ucs2_mode || str_known_not_to_have_any_supplementary(self))) {
	    return BYTES_TO_UCHARS(offset_in_bytes);
	}
	else {
	    return self->encoding->methods.offset_in_bytes_to_index(self, offset_in_bytes, ucs2_mode);
	}
    }
}

static long
str_offset_in_bytes_for_string(string_t *self, string_t *searched, long start_offset_in_bytes)
{
    if (start_offset_in_bytes >= self->length_in_bytes) {
	return -1;
    }
    if ((self == searched) && (start_offset_in_bytes == 0)) {
	return 0;
    }
    if (searched->length_in_bytes == 0) {
	return start_offset_in_bytes;
    }
    str_must_have_compatible_encoding(self, searched);
    str_make_same_format(self, searched);
    if (searched->length_in_bytes > self->length_in_bytes) {
	return -1;
    }
    long increment;
    if (str_is_stored_in_uchars(self)) {
	increment = 2;
    }
    else {
	increment = self->encoding->min_char_size;
    }
    long max_offset_in_bytes = self->length_in_bytes - searched->length_in_bytes + 1;
    for (long offset_in_bytes = start_offset_in_bytes; offset_in_bytes < max_offset_in_bytes; offset_in_bytes += increment) {
	if (memcmp(self->data.bytes+offset_in_bytes, searched->data.bytes, searched->length_in_bytes) == 0) {
	    return offset_in_bytes;
	}
    }
    return -1;
}

static long
str_index_for_string(string_t *self, string_t *searched, long start_index, bool ucs2_mode)
{
    str_must_have_compatible_encoding(self, searched);
    str_make_same_format(self, searched);

    long start_offset_in_bytes;
    if (start_index == 0) {
	start_offset_in_bytes = 0;
    }
    else {
	character_boundaries_t boundaries = str_get_character_boundaries(self, start_index, ucs2_mode);
	if (boundaries.start_offset_in_bytes == -1) {
	    if (boundaries.end_offset_in_bytes == -1) {
		return -1;
	    }
	    else {
		// you cannot cut a surrogate in an encoding that is not UTF-16
		str_cannot_cut_surrogate();
	    }
	}
	start_offset_in_bytes = boundaries.start_offset_in_bytes;
    }

    long offset_in_bytes = str_offset_in_bytes_for_string(STR(self), searched, start_offset_in_bytes);

    if (offset_in_bytes == -1) {
	return -1;
    }
    return str_offset_in_bytes_to_index(STR(self), offset_in_bytes, ucs2_mode);
}

static bool
str_include_string(string_t *self, string_t *searched)
{
    return (str_offset_in_bytes_for_string(self, searched, 0) != -1);
}

static string_t *
str_need_string(VALUE str)
{
    if (CLASS_OF(str) == rb_cMRString) {
	return (string_t *)str;
    }

    if (TYPE(str) != T_STRING) {
	str = rb_str_to_str(str);
    }
    if (OBJC_CLASS(str) != rb_cMRString) {
	return str_new_from_cfstring((CFStringRef)str);
    }
    else {
	return (string_t *)str;
    }
}

//----------------------------------------------
// Functions called by MacRuby

VALUE
mr_enc_s_is_compatible(VALUE klass, SEL sel, VALUE str1, VALUE str2)
{
    if (SPECIAL_CONST_P(str1) || SPECIAL_CONST_P(str2)) {
	return Qnil;
    }
    assert(OBJC_CLASS(str1) == rb_cMRString); // TODO
    assert(OBJC_CLASS(str2) == rb_cMRString); // TODO
    encoding_t *encoding = str_compatible_encoding(STR(str1), STR(str2));
    if (encoding == NULL) {
	return Qnil;
    }
    else {
	return (VALUE)encoding;
    }
}


static VALUE
mr_str_s_alloc(VALUE klass)
{
    return (VALUE)str_alloc();
}


static VALUE
mr_str_initialize(VALUE self, SEL sel, int argc, VALUE *argv)
{
    if (argc > 0) {
	assert(argc == 1);
	str_replace(STR(self), argv[0]);
    }
    return self;
}

static VALUE
mr_str_replace(VALUE self, SEL sel, VALUE arg)
{
    str_replace(STR(self), arg);
    return self;
}

static VALUE
mr_str_clear(VALUE self, SEL sel)
{
    str_clear(STR(self));
    return self;
}

static VALUE
mr_str_chars_count(VALUE self, SEL sel)
{
    return INT2NUM(str_length(STR(self), false));
}

static VALUE
mr_str_length(VALUE self, SEL sel)
{
    return INT2NUM(str_length(STR(self), true));
}

static VALUE
mr_str_bytesize(VALUE self, SEL sel)
{
    return INT2NUM(str_bytesize(STR(self)));
}

static VALUE
mr_str_encoding(VALUE self, SEL sel)
{
    return (VALUE)STR(self)->encoding;
}

static VALUE
mr_str_getbyte(VALUE self, SEL sel, VALUE index)
{
    unsigned char c;
    if (str_getbyte(STR(self), NUM2LONG(index), &c)) {
	return INT2NUM(c);
    }
    else {
	return Qnil;
    }
}

static VALUE
mr_str_setbyte(VALUE self, SEL sel, VALUE index, VALUE value)
{
    str_setbyte(STR(self), NUM2LONG(index), 0xFF & (unsigned long)NUM2LONG(value));
    return value;
}

static VALUE
mr_str_force_encoding(VALUE self, SEL sel, VALUE encoding)
{
    encoding_t *enc;
    if (SPECIAL_CONST_P(encoding) || (OBJC_CLASS(encoding) != rb_cMREncoding)) {
	abort(); // TODO
    }
    enc = (encoding_t *)encoding;
    str_force_encoding(STR(self), enc);
    return self;
}

static VALUE
mr_str_is_valid_encoding(VALUE self, SEL sel)
{
    return str_is_valid_encoding(STR(self)) ? Qtrue : Qfalse;
}

static VALUE
mr_str_is_ascii_only(VALUE self, SEL sel)
{
    return str_is_ruby_ascii_only(STR(self)) ? Qtrue : Qfalse;
}

static VALUE
mr_str_aref(VALUE self, SEL sel, int argc, VALUE *argv)
{
    string_t *ret;
    if (argc == 1) {
	VALUE index = argv[0];
	switch (TYPE(index)) {
	    case T_FIXNUM:
		{
		    ret = str_get_character_at(STR(self), FIX2LONG(index), true);
		}
		break;

	    case T_REGEXP:
		abort(); // TODO

	    case T_STRING:
		{
		    if (OBJC_CLASS(index) == rb_cMRString) {
			string_t *searched = STR(index);
			if (str_include_string(STR(self), searched)) {
			    return (VALUE)str_new_from_string(searched);
			}
		    }
		    else {
			string_t *searched = str_new_from_cfstring((CFStringRef)index);
			if (str_include_string(STR(self), searched)) {
			    // no need to duplicate the string as we just created it
			    return (VALUE)searched;
			}
		    }
		    return Qnil;
		}

	    default:
		{
		    VALUE rb_start = 0, rb_end = 0;
		    int exclude_end = false;
		    if (rb_range_values(index, &rb_start, &rb_end, &exclude_end)) {
			long start = NUM2LONG(rb_start);
			long end = NUM2LONG(rb_end);
			if (exclude_end) {
			    --end;
			}
			ret = str_get_characters(STR(self), start, end, true);
		    }
		    else {
			ret = str_get_character_at(STR(self), NUM2LONG(index), true);
		    }
		}
		break;
	}
    }
    else if (argc == 2) {
	long length = NUM2LONG(argv[1]);
	long start = NUM2LONG(argv[0]);
	if (length < 0) {
	    return Qnil;
	}
	long end = start + length - 1;
	if ((start < 0) && (end >= 0)) {
	    end = -1;
	}
	ret = str_get_characters(STR(self), start, end, true);
    }
    else {
	rb_raise(rb_eArgError, "wrong number of arguments (%d for 1..2)", argc);
    }

    if (ret == NULL) {
	return Qnil;
    }
    else {
	return (VALUE)ret;
    }
}

static VALUE
mr_str_index(VALUE self, SEL sel, int argc, VALUE *argv)
{
    if ((argc < 1) || (argc > 2)) {
	rb_raise(rb_eArgError, "wrong number of arguments (%d for 1..2)", argc);
    }

    VALUE rb_searched = argv[0];
    if (TYPE(rb_searched) == T_REGEXP) {
	abort(); // TODO
    }

    long start_index = 0;
    if (argc == 2) {
	start_index = NUM2LONG(argv[1]);
    }
    string_t *searched = str_need_string(rb_searched);

    long index = str_index_for_string(STR(self), searched, start_index, true);
    if (index == -1) {
	return Qnil;
    }
    else {
	return INT2NUM(index);
    }
}


static VALUE
mr_str_getchar(VALUE self, SEL sel, VALUE index)
{
    string_t *ret = str_get_character_at(STR(self), FIX2LONG(index), false);
    if (ret == NULL) {
	return Qnil;
    }
    else {
	return (VALUE)ret;
    }
}

static VALUE
mr_str_plus(VALUE self, SEL sel, VALUE to_add)
{
    return (VALUE)str_plus_string(STR(self), str_need_string(to_add));
}

static VALUE
mr_str_concat(VALUE self, SEL sel, VALUE to_concat)
{
    switch (TYPE(to_concat)) {
	case T_FIXNUM:
	case T_BIGNUM:
	    abort(); // TODO

	default:
	    str_concat_string(STR(self), str_need_string(to_concat));
    }
    return self;
}

static VALUE
mr_str_equal(VALUE self, SEL sel, VALUE compared_to)
{
    if (SPECIAL_CONST_P(compared_to)) {
	return Qfalse;
    }

    if (TYPE(compared_to) == T_STRING) {
	string_t *str;
	if (OBJC_CLASS(compared_to) == rb_cMRString) {
	    str = STR(compared_to);
	}
	else {
	    str = str_new_from_cfstring((CFStringRef)compared_to);
	}
	return str_is_equal_to_string(STR(self), str) ? Qtrue : Qfalse;
    }
    else {
	return Qfalse;
    }
}

static VALUE
mr_str_not_equal(VALUE self, SEL sel, VALUE compared_to)
{
    return mr_str_equal(self, sel, compared_to) == Qfalse ? Qtrue : Qfalse;
}

static VALUE
mr_str_include(VALUE self, SEL sel, VALUE searched)
{
    return str_include_string(STR(self), str_need_string(searched)) ? Qtrue : Qfalse;
}

static VALUE
mr_str_is_stored_in_uchars(VALUE self, SEL sel)
{
    return str_is_stored_in_uchars(STR(self)) ? Qtrue : Qfalse;
}

static VALUE
mr_str_to_s(VALUE self, SEL sel)
{
    if (OBJC_CLASS(self) != rb_cMRString) {
	return (VALUE)str_dup(self);
    }
    return self;
}

void
Init_MRString(void)
{
    // encodings must be loaded before strings
    assert(rb_cMREncoding != 0);

    rb_cMRString = rb_define_class("MRString", rb_cObject);
    rb_objc_define_method(OBJC_CLASS(rb_cMRString), "alloc", mr_str_s_alloc, 0);

    rb_objc_define_method(rb_cMRString, "initialize", mr_str_initialize, -1);
    rb_objc_define_method(rb_cMRString, "initialize_copy", mr_str_replace, 1);
    rb_objc_define_method(rb_cMRString, "replace", mr_str_replace, 1);
    rb_objc_define_method(rb_cMRString, "clear", mr_str_clear, 0);
    rb_objc_define_method(rb_cMRString, "encoding", mr_str_encoding, 0);
    rb_objc_define_method(rb_cMRString, "length", mr_str_length, 0);
    rb_objc_define_method(rb_cMRString, "size", mr_str_length, 0); // alias
    rb_objc_define_method(rb_cMRString, "bytesize", mr_str_bytesize, 0);
    rb_objc_define_method(rb_cMRString, "getbyte", mr_str_getbyte, 1);
    rb_objc_define_method(rb_cMRString, "setbyte", mr_str_setbyte, 2);
    rb_objc_define_method(rb_cMRString, "force_encoding", mr_str_force_encoding, 1);
    rb_objc_define_method(rb_cMRString, "valid_encoding?", mr_str_is_valid_encoding, 0);
    rb_objc_define_method(rb_cMRString, "ascii_only?", mr_str_is_ascii_only, 0);
    rb_objc_define_method(rb_cMRString, "[]", mr_str_aref, -1);
    rb_objc_define_method(rb_cMRString, "index", mr_str_index, -1);
    rb_objc_define_method(rb_cMRString, "+", mr_str_plus, 1);
    rb_objc_define_method(rb_cMRString, "<<", mr_str_concat, 1);
    rb_objc_define_method(rb_cMRString, "concat", mr_str_concat, 1);
    rb_objc_define_method(rb_cMRString, "==", mr_str_equal, 1);
    rb_objc_define_method(rb_cMRString, "!=", mr_str_not_equal, 1);
    rb_objc_define_method(rb_cMRString, "include?", mr_str_include, 1);
    rb_objc_define_method(rb_cMRString, "to_s", mr_str_to_s, 0);
    rb_objc_define_method(rb_cMRString, "to_str", mr_str_to_s, 0);

    // added for MacRuby
    rb_objc_define_method(rb_cMRString, "chars_count", mr_str_chars_count, 0);
    rb_objc_define_method(rb_cMRString, "getchar", mr_str_getchar, 1);

    // this method does not exist in Ruby and is there only for debugging purpose
    rb_objc_define_method(rb_cMRString, "stored_in_uchars?", mr_str_is_stored_in_uchars, 0);
}

void Init_MREncoding(void);

void
Init_new_string(void)
{
    Init_MREncoding();
    Init_MRString();
}
