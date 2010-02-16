#include "encoding.h"
#include "unicode/ucnv.h"

// do not forget to close the converter
// before leaving the function
#define USE_CONVERTER(cnv, str) \
    assert(str->encoding->private_data != NULL); \
    char cnv##_buffer[U_CNV_SAFECLONE_BUFFERSIZE]; \
    UErrorCode cnv##_err = U_ZERO_ERROR; \
    int32_t cnv##_buffer_size = U_CNV_SAFECLONE_BUFFERSIZE; \
    UConverter *cnv = ucnv_safeClone( \
	    (UConverter *)str->encoding->private_data, \
	    cnv##_buffer, \
	    &cnv##_buffer_size, \
	    &cnv##_err \
	); \
    ucnv_reset(cnv);

static void
str_ucnv_update_flags(string_t *self)
{
    assert(!str_is_stored_in_uchars(self));

    USE_CONVERTER(cnv, self);

    bool ascii_only = true;
    bool valid_encoding = true;
    bool has_supplementary = false;

    const char *pos = self->data.bytes;
    const char *end = pos + self->length_in_bytes;
    for (;;) {
	// iterate through the string one Unicode code point at a time
	UErrorCode err = U_ZERO_ERROR;
	UChar32 c = ucnv_getNextUChar(cnv, &pos, end, &err);
	if (U_FAILURE(err)) {
	    if (err == U_INDEX_OUTOFBOUNDS_ERROR) {
		// end of the string
		break;
	    }
	    else {
		// conversion error
		valid_encoding = false;
		ascii_only = false;
	    }
	}
	else {
	    if (c > 127) {
		ascii_only = false;
		if (U_IS_SUPPLEMENTARY(c)) {
		    has_supplementary = true;
		}
	    }
	}
    }

    ucnv_close(cnv);

    str_set_has_supplementary(self, has_supplementary);
    str_set_valid_encoding(self, valid_encoding);
    str_set_ascii_only(self, ascii_only);
}

static void
str_ucnv_make_data_binary(string_t *self)
{
    assert(str_is_stored_in_uchars(self));

    USE_CONVERTER(cnv, self);

    UErrorCode err = U_ZERO_ERROR;
    long capa = UCNV_GET_MAX_BYTES_FOR_STRING(BYTES_TO_UCHARS(self->length_in_bytes), ucnv_getMaxCharSize(cnv));
    char *buffer = xmalloc(capa);
    const UChar *source_pos = self->data.uchars;
    const UChar *source_end = self->data.uchars + BYTES_TO_UCHARS(self->length_in_bytes);
    char *target_pos = buffer;
    char *target_end = buffer + capa;
    ucnv_fromUnicode(cnv, &target_pos, target_end, &source_pos, source_end, NULL, true, &err);
    // there should never be any conversion error here
    // (if there's one it means some checking has been forgotten before)
    assert(U_SUCCESS(err));

    ucnv_close(cnv);

    str_set_stored_in_uchars(self, false);
    self->capacity_in_bytes = capa;
    self->length_in_bytes = target_pos - buffer;
    GC_WB(&self->data.bytes, buffer);
}

static long
utf16_bytesize_approximation(encoding_t *enc, int bytesize)
{
    long approximation;
    if (UTF16_ENC(enc)) {
	approximation = bytesize; // the bytesize in UTF-16 is the same whatever the endianness
    }
    else if (UTF32_ENC(enc)) {
	// the bytesize in UTF-16 is nearly half of the bytesize in UTF-32
	// (if there characters not in the BMP it's a bit more though)
	approximation = bytesize / 2;
    }
    else {
	// take a quite large size to not have to reallocate
	approximation = bytesize * 2;
    }

    if (ODD_NUMBER(approximation)) {
	// the size must be an even number
	++approximation;
    }

    return approximation;
}

static bool
str_ucnv_try_making_data_uchars(string_t *self)
{
    assert(!str_is_stored_in_uchars(self));

    USE_CONVERTER(cnv, self);

    long capa = utf16_bytesize_approximation(self->encoding, self->length_in_bytes);
    const char *source_pos = self->data.bytes;
    const char *source_end = self->data.bytes + self->length_in_bytes;
    UChar *buffer = xmalloc(capa);
    UChar *target_pos = buffer;
    UErrorCode err = U_ZERO_ERROR;
    for (;;) {
	UChar *target_end = buffer + BYTES_TO_UCHARS(capa);
	err = U_ZERO_ERROR;
	ucnv_toUnicode(cnv, &target_pos, target_end, &source_pos, source_end, NULL, true, &err);
	if (err == U_BUFFER_OVERFLOW_ERROR) {
	    long index = target_pos - buffer;
	    capa *= 2; // double the buffer's size
	    buffer = xrealloc(buffer, capa);
	    target_pos = buffer + index;
	}
	else {
	    break;
	}
    }

    ucnv_close(cnv);

    if (U_SUCCESS(err)) {
	str_set_valid_encoding(self, true);
	str_set_stored_in_uchars(self, true);
	self->capacity_in_bytes = capa;
	self->length_in_bytes = UCHARS_TO_BYTES(target_pos - buffer);
	GC_WB(&self->data.uchars, buffer);

	return true;
    }
    else {
	str_set_valid_encoding(self, false);

	return false;
    }
}

static long
str_ucnv_length(string_t *self, bool ucs2_mode)
{
    assert(!str_is_stored_in_uchars(self));

    USE_CONVERTER(cnv, self);

    const char *pos = self->data.bytes;
    const char *end = pos + self->length_in_bytes;
    long len = 0;
    bool valid_encoding = true;
    for (;;) {
	const char *character_start_pos = pos;
	// iterate through the string one Unicode code point at a time
	UErrorCode err = U_ZERO_ERROR;
	UChar32 c = ucnv_getNextUChar(cnv, &pos, end, &err);
	if (err == U_INDEX_OUTOFBOUNDS_ERROR) {
	    // end of the string
	    break;
	}
	else if (U_FAILURE(err)) {
	    valid_encoding = false;
	    long min_char_size = self->encoding->min_char_size;
	    long converted_width = pos - character_start_pos;
	    len += div_round_up(converted_width, min_char_size);
	}
	else {
	    if (ucs2_mode && !U_IS_BMP(c)) {
		len += 2;
	    }
	    else {
		++len;
	    }
	}
    }

    ucnv_close(cnv);

    str_set_valid_encoding(self, valid_encoding);

    return len;
}

#define STACK_BUFFER_SIZE 1024
static long
str_ucnv_bytesize(string_t *self)
{
    assert(str_is_stored_in_uchars(self));

    // for strings stored in UTF-16 for which the Ruby encoding is not UTF-16,
    // we have to convert back the string in its original encoding to get the length in bytes
    USE_CONVERTER(cnv, self);

    UErrorCode err = U_ZERO_ERROR;

    long len = 0;
    char buffer[STACK_BUFFER_SIZE];
    const UChar *source_pos = self->data.uchars;
    const UChar *source_end = self->data.uchars + BYTES_TO_UCHARS(self->length_in_bytes);
    char *target_end = buffer + STACK_BUFFER_SIZE;
    for (;;) {
	err = U_ZERO_ERROR;
	char *target_pos = buffer;
	ucnv_fromUnicode(cnv, &target_pos, target_end, &source_pos, source_end, NULL, true, &err);
	len += target_pos - buffer;
	if (err != U_BUFFER_OVERFLOW_ERROR) {
	    // if the convertion failed, a check was missing somewhere
	    assert(U_SUCCESS(err));
	    break;
	}
    }

    ucnv_close(cnv);
    return len;
}

static character_boundaries_t
str_ucnv_get_character_boundaries(string_t *self, long index, bool ucs2_mode)
{
    assert(!str_is_stored_in_uchars(self));

    character_boundaries_t boundaries = {-1, -1};

    if (index < 0) {
	// calculating the length is slow but we don't have much choice
	index += str_ucnv_length(self, ucs2_mode);
	if (index < 0) {
	    return boundaries;
	}
    }

    // the code has many similarities with str_length
    USE_CONVERTER(cnv, self);

    const char *pos = self->data.bytes;
    const char *end = pos + self->length_in_bytes;
    long current_index = 0;
    for (;;) {
	const char *character_start_pos = pos;
	// iterate through the string one Unicode code point at a time
	// (we dont care what the character is or if it's valid or not)
	UErrorCode err = U_ZERO_ERROR;
	UChar32 c = ucnv_getNextUChar(cnv, &pos, end, &err);
	if (err == U_INDEX_OUTOFBOUNDS_ERROR) {
	    // end of the string
	    break;
	}
	long offset_in_bytes = character_start_pos - self->data.bytes;
	long converted_width = pos - character_start_pos;
	if (U_FAILURE(err)) {
	    long min_char_size = self->encoding->min_char_size;
	    // division of converted_width by min_char_size rounded up
	    long diff = div_round_up(converted_width, min_char_size);
	    long length_in_bytes;
	    if (current_index == index) {
		if (min_char_size > converted_width) {
		    length_in_bytes = converted_width;
		}
		else {
		    length_in_bytes = min_char_size;
		}
		boundaries.start_offset_in_bytes = offset_in_bytes;
		boundaries.end_offset_in_bytes = boundaries.start_offset_in_bytes + length_in_bytes;
		break;
	    }
	    else if (current_index + diff > index) {
		long adjusted_offset = offset_in_bytes + (index - current_index) * min_char_size;
		if (adjusted_offset + min_char_size > offset_in_bytes + converted_width) {
		    length_in_bytes = offset_in_bytes + converted_width - adjusted_offset;
		}
		else {
		    length_in_bytes = min_char_size;
		}
		boundaries.start_offset_in_bytes = adjusted_offset;
		boundaries.end_offset_in_bytes = boundaries.start_offset_in_bytes + length_in_bytes;
		break;
	    }
	    current_index += diff;
	}
	else {
	    if (ucs2_mode && !U_IS_BMP(c)) {
		// you cannot cut a surrogate in an encoding that is not UTF-16
		if (current_index == index) {
		    boundaries.start_offset_in_bytes = offset_in_bytes;
		    break;
		}
		else if (current_index+1 == index) {
		    boundaries.end_offset_in_bytes = offset_in_bytes + converted_width;
		    break;
		}
		++current_index;
	    }

	    if (current_index == index) {
		boundaries.start_offset_in_bytes = offset_in_bytes;
		boundaries.end_offset_in_bytes = boundaries.start_offset_in_bytes + converted_width;
		break;
	    }

	    ++current_index;
	}
    }

    ucnv_close(cnv);

    return boundaries;
}

static long
str_ucnv_offset_in_bytes_to_index(string_t *self, long offset_in_bytes, bool ucs2_mode)
{
    assert(!str_is_stored_in_uchars(self));

    // the code has many similarities with str_length
    USE_CONVERTER(cnv, self);

    const char *current_position = self->data.bytes;
    const char *searched_position = current_position + offset_in_bytes;
    const char *end = current_position + self->length_in_bytes;
    long index = 0;
    for (;;) {
	const char *character_start_position = current_position;
	// iterate through the string one Unicode code point at a time
	UErrorCode err = U_ZERO_ERROR;
	UChar32 c = ucnv_getNextUChar(cnv, &current_position, end, &err);
	if (err == U_INDEX_OUTOFBOUNDS_ERROR) {
	    // end of the string
	    // should not happen because str_offset_in_bytes_to_index
	    // checks before that offset_in_bytes is inferior to the length in bytes
	    abort();
	}
	else if (U_FAILURE(err)) {
	    long min_char_size = self->encoding->min_char_size;
	    long converted_width = current_position - character_start_position;
	    long to_add = div_round_up(converted_width, min_char_size);
	    if (searched_position < character_start_position + to_add) {
		long difference = searched_position - character_start_position;
		index += (difference / min_char_size);
		break;
	    }
	    index += to_add;
	}
	else {
	    if (searched_position < current_position) {
		break;
	    }
	    if (ucs2_mode && !U_IS_BMP(c)) {
		index += 2;
	    }
	    else {
		++index;
	    }
	}
	if (searched_position == current_position) {
	    break;
	}
    }

    ucnv_close(cnv);

    return index;
}

void
enc_init_ucnv_encoding(encoding_t *encoding)
{
    // create the ICU converter
    UErrorCode err = U_ZERO_ERROR;
    UConverter *converter = ucnv_open(encoding->public_name, &err);
    if (!U_SUCCESS(err) || (converter == NULL)) {
	fprintf(stderr, "Couldn't create the encoder for %s\n", encoding->public_name);
	abort();
    }
    // stop the conversion when the conversion failed
    err = U_ZERO_ERROR;
    ucnv_setToUCallBack(converter, UCNV_TO_U_CALLBACK_STOP, NULL, NULL, NULL, &err);
    err = U_ZERO_ERROR;
    ucnv_setFromUCallBack(converter, UCNV_FROM_U_CALLBACK_STOP, NULL, NULL, NULL, &err);

    // fill the fields not filled yet
    encoding->private_data = converter;
    encoding->methods.update_flags = str_ucnv_update_flags;
    encoding->methods.make_data_binary = str_ucnv_make_data_binary;
    encoding->methods.try_making_data_uchars = str_ucnv_try_making_data_uchars;
    encoding->methods.length = str_ucnv_length;
    encoding->methods.bytesize = str_ucnv_bytesize;
    encoding->methods.get_character_boundaries = str_ucnv_get_character_boundaries;
    encoding->methods.offset_in_bytes_to_index = str_ucnv_offset_in_bytes_to_index;
}
