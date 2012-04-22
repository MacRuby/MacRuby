/*
 * MacRuby implementation of Ruby 1.9's sprintf.c.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2007-2011, Apple Inc. All rights reserved.
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 * Copyright (C) 2000  Network Applied Communication Laboratory, Inc.
 * Copyright (C) 2000  Information-technology Promotion Agency, Japan
 */

#include <stdarg.h>

#include "macruby_internal.h"
#include "ruby/encoding.h"
#include "encoding.h"

/*
 *  call-seq:
 *     format(format_string [, arguments...] )   => string
 *     sprintf(format_string [, arguments...] )  => string
 *  
 *  Returns the string resulting from applying <i>format_string</i> to
 *  any additional arguments.  Within the format string, any characters
 *  other than format sequences are copied to the result. 
 *
 *  The syntax of a format sequence is follows.
 *
 *    %[flags][width][.precision]type
 *
 *  A format
 *  sequence consists of a percent sign, followed by optional flags,
 *  width, and precision indicators, then terminated with a field type
 *  character.  The field type controls how the corresponding
 *  <code>sprintf</code> argument is to be interpreted, while the flags
 *  modify that interpretation.
 *
 *  The field type characters are:
 *
 *      Field |  Integer Format
 *      ------+--------------------------------------------------------------
 *        b   | Convert argument as a binary number.
 *            | Negative numbers will be displayed as a two's complement
 *            | prefixed with `..1'.
 *        B   | Equivalent to `b', but uses an uppercase 0B for prefix
 *            | in the alternative format by #.
 *        d   | Convert argument as a decimal number.
 *        i   | Identical to `d'.
 *        o   | Convert argument as an octal number.
 *            | Negative numbers will be displayed as a two's complement
 *            | prefixed with `..7'.
 *        u   | Identical to `d'.
 *        x   | Convert argument as a hexadecimal number.
 *            | Negative numbers will be displayed as a two's complement
 *            | prefixed with `..f' (representing an infinite string of
 *            | leading 'ff's).
 *        X   | Equivalent to `x', but uses uppercase letters.
 *
 *      Field |  Float Format
 *      ------+--------------------------------------------------------------
 *        e   | Convert floating point argument into exponential notation 
 *            | with one digit before the decimal point as [-]d.dddddde[+-]dd.
 *            | The precision specifies the number of digits after the decimal
 *            | point (defaulting to six).
 *        E   | Equivalent to `e', but uses an uppercase E to indicate
 *            | the exponent.
 *        f   | Convert floating point argument as [-]ddd.dddddd, 
 *            | where the precision specifies the number of digits after
 *            | the decimal point.
 *        g   | Convert a floating point number using exponential form
 *            | if the exponent is less than -4 or greater than or
 *            | equal to the precision, or in dd.dddd form otherwise.
 *            | The precision specifies the number of significant digits.
 *        G   | Equivalent to `g', but use an uppercase `E' in exponent form.
 *
 *      Field |  Other Format
 *      ------+--------------------------------------------------------------
 *        c   | Argument is the numeric code for a single character or
 *            | a single character string itself.
 *        p   | The valuing of argument.inspect.
 *        s   | Argument is a string to be substituted.  If the format
 *            | sequence contains a precision, at most that many characters
 *            | will be copied.
 *     
 *  The flags modifies the behavior of the formats.
 *  The flag characters are:
 *
 *    Flag     | Applies to    | Meaning
 *    ---------+---------------+-----------------------------------------
 *    space    | bBdiouxX      | Leave a space at the start of 
 *             | eEfgG         | non-negative numbers.
 *             | (numeric fmt) | For `o', `x', `X', `b' and `B', use
 *             |               | a minus sign with absolute value for
 *             |               | negative values.
 *    ---------+---------------+-----------------------------------------
 *    (digit)$ | all           | Specifies the absolute argument number
 *             |               | for this field.  Absolute and relative
 *             |               | argument numbers cannot be mixed in a
 *             |               | sprintf string.
 *    ---------+---------------+-----------------------------------------
 *     #       | bBoxX         | Use an alternative format.
 *             | eEfgG         | For the conversions `o', increase the precision
 *             |               | until the first digit will be `0' if
 *             |               | it is not formatted as complements.
 *             |               | For the conversions `x', `X', `b' and `B'
 *             |               | on non-zero, prefix the result with ``0x'',
 *             |               | ``0X'', ``0b'' and ``0B'', respectively.
 *             |               | For `e', `E', `f', `g', and 'G',
 *             |               | force a decimal point to be added,
 *             |               | even if no digits follow.
 *             |               | For `g' and 'G', do not remove trailing zeros.
 *    ---------+---------------+-----------------------------------------
 *    +        | bBdiouxX      | Add a leading plus sign to non-negative
 *             | eEfgG         | numbers.
 *             | (numeric fmt) | For `o', `x', `X', `b' and `B', use
 *             |               | a minus sign with absolute value for
 *             |               | negative values.
 *    ---------+---------------+-----------------------------------------
 *    -        | all           | Left-justify the result of this conversion.
 *    ---------+---------------+-----------------------------------------
 *    0 (zero) | bBdiouxX      | Pad with zeros, not spaces.
 *             | eEfgG         | For `o', `x', `X', `b' and `B', radix-1
 *             | (numeric fmt) | is used for negative numbers formatted as
 *             |               | complements.
 *    ---------+---------------+-----------------------------------------
 *    *        | all           | Use the next argument as the field width. 
 *             |               | If negative, left-justify the result. If the
 *             |               | asterisk is followed by a number and a dollar 
 *             |               | sign, use the indicated argument as the width.
 *
 *  Examples of flags:
 *
 *   # `+' and space flag specifies the sign of non-negative numbers.
 *   sprintf("%d", 123)  #=> "123"
 *   sprintf("%+d", 123) #=> "+123"
 *   sprintf("% d", 123) #=> " 123"
 *
 *   # `#' flag for `o' increases number of digits to show `0'.
 *   # `+' and space flag changes format of negative numbers.
 *   sprintf("%o", 123)   #=> "173"
 *   sprintf("%#o", 123)  #=> "0173"
 *   sprintf("%+o", -123) #=> "-173"
 *   sprintf("%o", -123)  #=> "..7605"
 *   sprintf("%#o", -123) #=> "..7605"
 *
 *   # `#' flag for `x' add a prefix `0x' for non-zero numbers.
 *   # `+' and space flag disables complements for negative numbers.
 *   sprintf("%x", 123)   #=> "7b"
 *   sprintf("%#x", 123)  #=> "0x7b"
 *   sprintf("%+x", -123) #=> "-7b"
 *   sprintf("%x", -123)  #=> "..f85"
 *   sprintf("%#x", -123) #=> "0x..f85"
 *   sprintf("%#x", 0)    #=> "0"
 *
 *   # `#' for `X' uses the prefix `0X'.
 *   sprintf("%X", 123)  #=> "7B"
 *   sprintf("%#X", 123) #=> "0X7B"
 *
 *   # `#' flag for `b' add a prefix `0b' for non-zero numbers.
 *   # `+' and space flag disables complements for negative numbers.
 *   sprintf("%b", 123)   #=> "1111011"
 *   sprintf("%#b", 123)  #=> "0b1111011"
 *   sprintf("%+b", -123) #=> "-1111011"
 *   sprintf("%b", -123)  #=> "..10000101"
 *   sprintf("%#b", -123) #=> "0b..10000101"
 *   sprintf("%#b", 0)    #=> "0"
 *
 *   # `#' for `B' uses the prefix `0B'.
 *   sprintf("%B", 123)  #=> "1111011"
 *   sprintf("%#B", 123) #=> "0B1111011"
 *
 *   # `#' for `e' forces to show the decimal point.
 *   sprintf("%.0e", 1)  #=> "1e+00"
 *   sprintf("%#.0e", 1) #=> "1.e+00"
 *
 *   # `#' for `f' forces to show the decimal point.
 *   sprintf("%.0f", 1234)  #=> "1234"
 *   sprintf("%#.0f", 1234) #=> "1234."
 *
 *   # `#' for `g' forces to show the decimal point.
 *   # It also disables stripping lowest zeros.
 *   sprintf("%g", 123.4)   #=> "123.4"
 *   sprintf("%#g", 123.4)  #=> "123.400"
 *   sprintf("%g", 123456)  #=> "123456"
 *   sprintf("%#g", 123456) #=> "123456."
 *     
 *  The field width is an optional integer, followed optionally by a
 *  period and a precision.  The width specifies the minimum number of
 *  characters that will be written to the result for this field.
 *
 *  Examples of width:
 *
 *   # padding is done by spaces,       width=20
 *   # 0 or radix-1.             <------------------>
 *   sprintf("%20d", 123)   #=> "                 123"
 *   sprintf("%+20d", 123)  #=> "                +123"
 *   sprintf("%020d", 123)  #=> "00000000000000000123"
 *   sprintf("%+020d", 123) #=> "+0000000000000000123"
 *   sprintf("% 020d", 123) #=> " 0000000000000000123"
 *   sprintf("%-20d", 123)  #=> "123                 "
 *   sprintf("%-+20d", 123) #=> "+123                "
 *   sprintf("%- 20d", 123) #=> " 123                "
 *   sprintf("%020x", -123) #=> "..ffffffffffffffff85"
 *
 *  For
 *  numeric fields, the precision controls the number of decimal places
 *  displayed.  For string fields, the precision determines the maximum
 *  number of characters to be copied from the string.  (Thus, the format
 *  sequence <code>%10.10s</code> will always contribute exactly ten
 *  characters to the result.)
 *
 *  Examples of precisions:
 *
 *   # precision for `d', 'o', 'x' and 'b' is
 *   # minimum number of digits               <------>
 *   sprintf("%20.8d", 123)  #=> "            00000123"
 *   sprintf("%20.8o", 123)  #=> "            00000173"
 *   sprintf("%20.8x", 123)  #=> "            0000007b"
 *   sprintf("%20.8b", 123)  #=> "            01111011"
 *   sprintf("%20.8d", -123) #=> "           -00000123"
 *   sprintf("%20.8o", -123) #=> "            ..777605"
 *   sprintf("%20.8x", -123) #=> "            ..ffff85"
 *   sprintf("%20.8b", -11)  #=> "            ..110101"
 *
 *   # "0x" and "0b" for `#x' and `#b' is not counted for
 *   # precision but "0" for `#o' is counted.  <------>
 *   sprintf("%#20.8d", 123)  #=> "            00000123"
 *   sprintf("%#20.8o", 123)  #=> "            00000173"
 *   sprintf("%#20.8x", 123)  #=> "          0x0000007b"
 *   sprintf("%#20.8b", 123)  #=> "          0b01111011"
 *   sprintf("%#20.8d", -123) #=> "           -00000123"
 *   sprintf("%#20.8o", -123) #=> "            ..777605"
 *   sprintf("%#20.8x", -123) #=> "          0x..ffff85"
 *   sprintf("%#20.8b", -11)  #=> "          0b..110101"
 *
 *   # precision for `e' is number of
 *   # digits after the decimal point           <------>
 *   sprintf("%20.8e", 1234.56789) #=> "      1.23456789e+03"
 *                                    
 *   # precision for `f' is number of
 *   # digits after the decimal point               <------>
 *   sprintf("%20.8f", 1234.56789) #=> "       1234.56789000"
 *
 *   # precision for `g' is number of
 *   # significant digits                          <------->
 *   sprintf("%20.8g", 1234.56789) #=> "           1234.5679"
 *
 *   #                                         <------->
 *   sprintf("%20.8g", 123456789)  #=> "       1.2345679e+08"
 *
 *   # precision for `s' is
 *   # maximum number of characters                    <------>
 *   sprintf("%20.8s", "string test") #=> "            string t"
 *     
 *  Examples:
 *
 *     sprintf("%d %04x", 123, 123)               #=> "123 007b"
 *     sprintf("%08b '%4s'", 123, 123)            #=> "01111011 ' 123'"
 *     sprintf("%1$*2$s %2$d %1$s", "hello", 8)   #=> "   hello 8 hello"
 *     sprintf("%1$*2$s %2$d", "hello", -8)       #=> "hello    -8"
 *     sprintf("%+g:% g:%-g", 1.23, 1.23, 1.23)   #=> "+1.23: 1.23:1.23"
 *     sprintf("%u", -123)                        #=> "-123"
 */

#define GETNTHARG(nth) \
    ((nth >= argc) ? (rb_raise(rb_eArgError, "too few arguments"), 0) : \
    argv[nth])

VALUE
rb_f_sprintf_imp(VALUE recv, SEL sel, int argc, VALUE *argv)
{
    return rb_str_format(argc - 1, argv + 1, GETNTHARG(0));
}

VALUE
rb_f_sprintf(int argc, const VALUE *argv)
{
    return rb_str_format(argc - 1, argv + 1, GETNTHARG(0));
}

VALUE
rb_enc_vsprintf(rb_encoding *enc, const char *fmt, va_list ap)
{
    char buffer[512];
    int n;
    n = vsnprintf(buffer, sizeof buffer, fmt, ap);
    return rb_enc_str_new(buffer, n, enc);
}

VALUE
rb_enc_sprintf(rb_encoding *enc, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    VALUE result = rb_enc_vsprintf(enc, format, ap);
    va_end(ap);
    return result;
}

VALUE
rb_vsprintf(const char *fmt, va_list ap)
{
    return rb_enc_vsprintf(NULL, fmt, ap);
}

VALUE
rb_sprintf(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    VALUE result = rb_vsprintf(format, ap);
    va_end(ap);
    return result;
}

VALUE
rb_str_catf(VALUE str, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    VALUE result = rb_vsprintf(format, ap);
    va_end(ap);
    rb_str_concat(str, result);
    return str;
}

#define IS_NEG(num) RBIGNUM_NEGATIVE_P(num)
#define REL_REF	    1
#define ABS_REF	    2
#define NAMED_REF   3

#define REF_NAME(type) \
    ((type) == REL_REF ? "relative" : (type) == ABS_REF ? "absolute" : "named")

#define SET_REF_TYPE(type) \
    if (arg == 0 && ref_type != 0 && (type) != ref_type) { \
	rb_raise(rb_eArgError, "can't mix %s references with %s references", \
		REF_NAME(type), REF_NAME(ref_type)); \
    } \
    ref_type = (type);

#define GET_ARG() \
    if (arg == 0) { \
	SET_REF_TYPE(REL_REF); \
	arg = GETNTHARG(j); \
	j++; \
    }
    
#define isprenum(ch) ((ch) == '-' || (ch) == ' ' || (ch) == '+')

static void
pad_format_value(VALUE arg, long start, long width, VALUE pad)
{
    const long slen = rb_str_chars_len(arg);
    if (width <= slen) {
	return;
    }
    if (start < 0) {
	start += slen + 1;
    }
    width -= slen;
    do {
	rb_str_update(arg, start, 0, pad);
    }
    while (--width > 0);
}

static long
cstr_update(UChar **str, long *str_len, long start, long num, VALUE replace)
{
    const long len = *str_len;
    long replace_len = replace == 0 ? 0 : rb_str_chars_len(replace);
    if (start + num > len) {
	num = len - start;
    }
    if (replace_len >= num) {
	*str_len = len + replace_len - num;
	*str = (UChar *)xrealloc(*str,
		sizeof(UChar) * (len + replace_len - num));
    }
    if (replace_len != num) {
	bcopy(*str + start + num, *str + start + replace_len,
		sizeof(UChar) * (len - start - num));
    }
    if (replace_len > 0) {
	RB_STR_GET_UCHARS(replace, replace_chars, replace_len2);
	assert(replace_len2 == replace_len);
	bcopy(replace_chars, *str + start, sizeof(UChar) * replace_len);
    }
    return replace_len - num;
}

static VALUE
get_named_arg(UChar *format_str, long format_len, long *i, VALUE hash)
{
    if (TYPE(hash) != T_HASH) {
	rb_raise(rb_eArgError,
		 "hash required for named references");
    }
    UChar closing = format_str[(*i)++] + 2;
    UChar *str_ptr = &format_str[*i];
    long  length = 0;
    while (*i < format_len && format_str[*i] != closing) {
	(*i)++;
	length++;
    }
    if (*i == format_len) {
	rb_raise(rb_eArgError,
		 "malformed name - unmatched parenthesis");
    }
    VALUE substr = rb_unicode_str_new(str_ptr, (size_t)length);
    hash = rb_hash_aref(hash, ID2SYM(rb_intern_str(substr)));
    return hash;
}

// XXX look for arguments that are altered but not duped
VALUE
rb_str_format(int argc, const VALUE *argv, VALUE fmt)
{
    bool tainted = OBJ_TAINTED(fmt);

    long format_len = 0;
    UChar *format_str = rb_str_xcopy_uchars(fmt, &format_len);
    if (format_len == 0) {
	goto bail;
    }

    long num, pos;
    int j = 0;
    int ref_type = 0;
    long format_str_capa = format_len;

#define CHECK_FOR_WIDTH()					\
    if (width_flag) {						\
	rb_raise(rb_eArgError, "width given twice");		\
    }								\
    if (precision_flag) {					\
	rb_raise(rb_eArgError, "width after precision");	\
    }
#define CHECK_FOR_FLAGS()					\
    if (width_flag) {						\
	rb_raise(rb_eArgError, "flag after width");		\
    }								\
    if (precision_flag) {					\
	rb_raise(rb_eArgError, "flag after precision");		\
    }

    for (long i = 0; i < format_len; i++) {
	if (format_str[i] != '%') {
	    continue;
	}
	if (format_str[i + 1] == '%') {
	    num = cstr_update(&format_str, &format_str_capa, i, 1, 0);
	    format_len += num;
	    continue;
	}

	bool sharp_flag = false;
	bool space_flag = false;
	bool plus_flag = false;
	bool minus_flag = false;
	bool zero_flag = false;
	bool width_flag = false;
	bool precision_flag = false;
	bool named_flag = false;
	bool complete = false;
	VALUE arg = 0;
	long width = 0;
	long precision = 0;
	int base = 0;
	VALUE negative_pad = 0;
	VALUE sharp_pad = rb_str_new2("");
	const long start = i;

	while (i++ < format_len) {
	    switch (format_str[i]) {
		case '#':
		    CHECK_FOR_FLAGS();
		    sharp_flag = true;
		    break;

		case '*':
		    CHECK_FOR_WIDTH();
		    width_flag = true;
		    if (format_str[++i] == '<' || format_str[i] == '{') {
			SET_REF_TYPE(NAMED_REF);
			width = NUM2LONG(rb_Integer(get_named_arg(format_str,
				format_len, &i, GETNTHARG(0))));
		    }
		    else {
			if (isprenum(format_str[i])) {
			    i--;
			    break;
			}

			num = rb_uchar_strtol(format_str, format_len, i, &pos);
			if (pos == i--) {
			    SET_REF_TYPE(REL_REF);
			    width = NUM2LONG(rb_Integer(GETNTHARG(j)));
			    j++;
			}
			else if (format_str[pos] == '$') {
			    if (num == 0) {
				rb_raise(rb_eArgError, "invalid absolute argument");
			    }
			    SET_REF_TYPE(ABS_REF);
			    width = NUM2LONG(rb_Integer(GETNTHARG(num - 1)));
			    i = pos;
			}
		    }
		    if (width < 0) {
			minus_flag = true;
			width = -width;
		    }
		    break;

		case ' ':
		    CHECK_FOR_FLAGS();
		    if (!plus_flag) {
			space_flag = true;
		    }
		    break;

		case '+':
		    CHECK_FOR_FLAGS();
		    plus_flag = true;
		    space_flag = false;
		    break;

		case '-':
		    CHECK_FOR_FLAGS();
		    zero_flag = false;
		    minus_flag = true;
		    break;

		case '0':
		    CHECK_FOR_FLAGS();
		    if (!precision_flag && !minus_flag) {
			zero_flag = true;
		    }
		    break;

		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
		    num = rb_uchar_strtol(format_str, format_len, i, &pos);
		    i = pos;
		    if (format_str[pos] == '$') {
			if (num == 0) {
			    rb_raise(rb_eArgError, "invalid absolute argument");
			}
			if (arg != 0) {
			    rb_raise(rb_eArgError, "value given twice");
			}
			SET_REF_TYPE(ABS_REF);
			arg = GETNTHARG(num - 1);
		    }
		    else {
			SET_REF_TYPE(REL_REF);
			width = num;
			i--;
			CHECK_FOR_WIDTH();
			width_flag = true;
		    }
		    break;

		case '.':
		    if (precision_flag) {
			rb_raise(rb_eArgError, "precision given twice");
		    }
		    precision_flag = true;
		    if (format_str[++i] == '*') {
			if (format_str[++i] == '<' || format_str[i] == '{') {
			    SET_REF_TYPE(NAMED_REF);
			    precision = NUM2LONG(rb_Integer(get_named_arg(
				    format_str, format_len, &i, GETNTHARG(0))));
			}
			else {
			    if (isprenum(format_str[i])) {
				i--;
				break;
			    }

			    num = rb_uchar_strtol(format_str, format_len,
				    i, &pos);
			    if (format_str[pos] == '$') {
				if (num == 0) {
				    rb_raise(rb_eArgError, "invalid absolute argument");
				}
				SET_REF_TYPE(ABS_REF);
				precision = NUM2LONG(rb_Integer(GETNTHARG(
					num - 1)));
				i = pos;
			    }
			    else {
				SET_REF_TYPE(REL_REF);
				precision = NUM2LONG(rb_Integer(GETNTHARG(j)));
				j++;
				i--;
			    }
			}
		    }
		    else if (isdigit(format_str[i])) {
			precision = rb_uchar_strtol(format_str, format_len,
				i, &pos);
			i = pos - 1;
		    }
		    else {
			i--;
		    }

		    if (precision < 0) {
			precision = 0;
		    }
		    break;

		case '<':
		case '{':
		{
		    char term = (format_str[i] == '<') ? '>' : '}';

		    if (named_flag) {
			rb_raise(rb_eArgError, "named given twice");
		    }
		    if (ref_type == ABS_REF) {
			rb_raise(rb_eArgError, "named after numbered");
		    }
		    named_flag = true;
		    ref_type = 0;
		    SET_REF_TYPE(NAMED_REF);
		    arg = get_named_arg(format_str, format_len, &i,
			    GETNTHARG(0));
		    if (term == '}') {
			if (TYPE(arg) != T_STRING) {
			    arg = rb_obj_as_string(arg);
			}
			goto format_s;
		    }
		    break;
		}

		case 'd':
		case 'D':
		case 'i':
		case 'u':
		case 'U':
		    base = 10;
		    sharp_flag = false;
		    if (precision_flag) {
			zero_flag = false;
		    }
		    complete = true;
		    break;

		case 'x':
		case 'X':
		    base = 16;
		    negative_pad = rb_str_new2("f");
		    sharp_pad = rb_str_new2("0x");
		    if (precision_flag) {
			zero_flag = false;
		    }
		    complete = true;
		    break;

		case 'o':
		case 'O':
		    base = 8;
		    negative_pad = rb_str_new2("7");
		    sharp_pad = rb_str_new2("0");
		    complete = true;
		    break;

		case 'B':
		case 'b':
		    base = 2;
		    negative_pad = rb_str_new2("1");
		    sharp_pad = rb_str_new2("0b");
		    complete = true;
		    break;

		case 'c':
		case 'C':
		    GET_ARG();
		    if (TYPE(arg) == T_STRING) {
			if(RSTRING_LEN(arg) != 1) {
			    rb_raise(rb_eArgError, "%%c requires a character");
			}
		    }
		    else {
			long num = NUM2LONG(arg);
			if (num < 0 || i > 0xff) {
			    rb_raise(rb_eRangeError, "%ld out of char range",
				    num);
			}
			char c = (char)num;
			arg = rb_str_new(&c, 1);
		    }
		    complete = true;
		    break;

		case 'f':
		case 'F':
		case 'e':
		case 'E':
		case 'g':
		case 'G':
		case 'a':
		case 'A':
		{
		    // here we construct a new format str and then use
		    // c's sprintf. why? because floats are retarded
		    GET_ARG();
		    double value = RFLOAT_VALUE(rb_Float(arg));
		    complete = true;
		    
		    if (isnan(value) || isinf(value)) {
			arg = rb_str_new2((char *)(isnan(value) ? "NaN" :
				value < 0 ? "-Inf" : "Inf"));
			if (isnan(value) || value > 0) {
			    if (plus_flag) {
				rb_str_update(arg, 0, 0, rb_str_new2("+"));
			    }
			    else if (space_flag) {
				rb_str_update(arg, 0, 0, rb_str_new2(" "));
			    }
			}
			break;
		    }

		    arg = rb_unicode_str_new(&format_str[i], 1);
		    if (precision_flag) {
			rb_str_update(arg, 0, 0, rb_big2str(LONG2NUM(precision),
				10));
			rb_str_update(arg, 0, 0, rb_str_new2("."));
		    }
		    rb_str_update(arg, 0, 0, rb_big2str(LONG2NUM(width), 10));
		    if (minus_flag) {
			rb_str_update(arg, 0, 0, rb_str_new2("-"));
		    }
		    else if (zero_flag) {
			rb_str_update(arg, 0, 0, rb_str_new2("0"));
		    }
		    if (plus_flag) {
			rb_str_update(arg, 0, 0, rb_str_new2("+"));
		    }
		    else if (space_flag) {
			rb_str_update(arg, 0, 0, rb_str_new2(" "));
		    }
		    if (sharp_flag) {
			rb_str_update(arg, 0, 0, rb_str_new2("#"));
		    }
		    rb_str_update(arg, 0, 0, rb_str_new2("%"));

		    char *ptr;
		    asprintf(&ptr, RSTRING_PTR(arg), value);
		    arg = rb_str_new2(ptr);
		    free(ptr);
		    break;
		}

		case 's':
		case 'S':
		case 'p':
		case '@':
		    GET_ARG();
		    arg = (tolower(format_str[i]) != 's'
			    ? rb_inspect(arg) : TYPE(arg) == T_STRING
				? rb_str_new3(arg) : rb_obj_as_string(arg));
		format_s:
		    if (precision_flag && precision < rb_str_chars_len(arg)) {
			CFStringPad((CFMutableStringRef)arg, NULL, precision,
				0);
		    }
		    complete = true;
		    break;

		case '\n':
		case '\0':
		    if (format_str[i - 1] == '%') {
			if (format_str[i] == '\n') {
			    arg = rb_str_new("%\n", 2);
			}
			else {
			    if (format_len > i) {
				arg = rb_str_new("%\0", 2);
			    }
			    else {
				arg = rb_str_new("%", 1);
			    }
			}
			complete = true;
			break;
		    }
		    rb_raise(rb_eArgError, "malformed format string - %%%c",
			     format_str[i]);
		    break;

		default:
		    rb_raise(rb_eArgError, "malformed format string - %%%c",
			     format_str[i]);
	    }
	    if (!complete) {
		continue;
	    }

	    GET_ARG();

	    if (base != 0) {
		bool sign_pad = false;
		unsigned long num_index = 0;
		unsigned long sharp_beg = 0;
		VALUE zero_pad = rb_str_new2("0");

		VALUE num = rb_Integer(arg);
		if (TYPE(num) == T_FIXNUM) {
		    num = rb_int2big(FIX2LONG(num));
		}
		if (plus_flag || space_flag) {
		    sign_pad = 1;
		}
		if (IS_NEG(num)) {
		    num_index = 1;
		    sharp_beg = 1;
		    if (!sign_pad && negative_pad != 0) {
			zero_pad = negative_pad;
			num = rb_big_clone(num);
			rb_big_2comp(num);
		    }
		}

		arg = rb_big2str(num, base);
		if (!sign_pad && IS_NEG(num) && negative_pad != 0) {
		    UChar neg = rb_str_get_uchar(negative_pad, 0);
		    if (base == 8) {
			UChar c = rb_str_get_uchar(arg, 1);
			const long len = rb_str_chars_len(arg) - 1;
			c |= ((~0 << 3) >> ((3 * len)
				    % (sizeof(BDIGIT) * 8))) & ~(~0 << 3);
			rb_str_update(arg, 1, 1, rb_unicode_str_new(&c, 1));
		    }
		    for (int i = 1, count = rb_str_chars_len(arg); i < count;
			    i++) {
			if (rb_str_get_uchar(arg, i) != neg) {
			    break;
			}
			num_index++;
		    }
		    rb_str_update(arg, 0, num_index, negative_pad);
		    rb_str_update(arg, 0, 0, rb_str_new2(".."));
		    num_index = 2;
		    sharp_beg = 0;
		}
		if (precision_flag) {
		    pad_format_value(arg, num_index,
			    precision + (IS_NEG(num)
				&& (sign_pad || negative_pad == 0) ? 1 : 0),
			    zero_pad);
		}
		if (sharp_flag && rb_cmpint(num, Qfalse, Qfalse) != 0) {
		    rb_str_update(arg, sharp_beg, 0, (VALUE)sharp_pad);
		    num_index += 2;
		}
		if (sign_pad && RBIGNUM_POSITIVE_P(num)) {
		    rb_str_update(arg, 0, 0, (VALUE)(plus_flag ?
			    rb_str_new2("+") : rb_str_new2(" ")));
		    num_index++;
		}
		if (zero_flag) {
		    pad_format_value(arg, num_index, width, zero_pad);
		}
		if (ISUPPER(format_str[i])) {
		    CFStringUppercase((CFMutableStringRef)arg, NULL);
		}
	    }
	    
	    if (OBJ_TAINTED(arg)) {
		tainted = true;
	    }

	    if (TYPE(arg) == T_SYMBOL) {
		// Because symbols are not mutable and pad_format_value()
		// mutates its first argument.
		arg = rb_sym_to_s(arg);
	    }
	    pad_format_value(arg, minus_flag ? -1 : 0, width, rb_str_new2(" "));
	    num = cstr_update(&format_str, &format_str_capa, start,
		    i - start + 1, arg);
	    format_len += num;
	    i += num;
	    break;
	}
    }

bail:
    fmt = rb_unicode_str_new(format_str, format_len);
    if (tainted) {
	OBJ_TAINT(fmt);
    }
    return fmt;
}
