/*
 * MacRuby implementation of Ruby 1.9's sprintf.c.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2007-2009, Apple Inc. All rights reserved.
 */
/**********************************************************************

  sprintf.c -

  $Author: yugui $
  created at: Fri Oct 15 10:39:26 JST 1993

  Copyright (C) 1993-2007 Yukihiro Matsumoto
  Copyright (C) 2000  Network Applied Communication Laboratory, Inc.
  Copyright (C) 2000  Information-technology Promotion Agency, Japan

**********************************************************************/

#include "ruby/ruby.h"
#include "ruby/re.h"
#include "ruby/encoding.h"
#include <math.h>
#include <stdarg.h>
#include <ctype.h>

#ifdef HAVE_IEEEFP_H
#include <ieeefp.h>
#endif

#define BIT_DIGITS(N)   (((N)*146)/485 + 1)  /* log2(10) =~ 146/485 */
#define BITSPERDIG (SIZEOF_BDIGITS*CHAR_BIT)
#define EXTENDSIGN(n, l) (((~0 << (n)) >> (((n)*(l)) % BITSPERDIG)) & ~(~0 << (n)))

static void fmt_setup(char*,size_t,int,int,int,int);

static char*
remove_sign_bits(char *str, int base)
{
    char *s, *t;
    
    s = t = str;

    if (base == 16) {
	while (*t == 'f') {
	    t++;
	}
    }
    else if (base == 8) {
	*t |= EXTENDSIGN(3, strlen(t));
	while (*t == '7') {
	    t++;
	}
    }
    else if (base == 2) {
	while (*t == '1') {
	    t++;
	}
    }

    return t;
}

#define FNONE  0
#define FSHARP 1
#define FMINUS 2
#define FPLUS  4
#define FZERO  8
#define FSPACE 16
#define FWIDTH 32
#define FPREC  64
#define FPREC0 128

#define PUSH(s, l) CFStringAppendCharacters((CFMutableStringRef)result, s, l)

#define PUSH_CSTR(s) CFStringAppendCString((CFMutableStringRef)result, s, kCFStringEncodingUTF8)

#define PUSH_CSTR_LEN(s, l) do { \
    char save = 0; \
    if (l < strlen(s)) { \
	save = s[l]; \
	s[l] = 0; \
    } \
    CFStringAppendCString((CFMutableStringRef)result, s, kCFStringEncodingUTF8); \
    if (save) s[l] = save; \
} while(0)


#define FILL(c, l) do {if ((l) > 0) CFStringPad((CFMutableStringRef)result, CFSTR(c), CFStringGetLength((CFMutableStringRef)result) + (l), 0);} while(0)

#define FILL_SIGN_BITS(base, p, l) \
    switch (base) { \
	case 16: \
	    if (*p == 'X') FILL("F", (l)); \
	    else FILL("f", (l)); \
	    break; \
	case 8: \
	    FILL("7", (l)); \
	    break; \
	case 2: \
	    FILL("1", (l)); \
	    break; \
	default: \
	    FILL(".", (l)); \
	    break; \
    }

#define GETARG() (nextvalue != Qundef ? nextvalue : \
    posarg == -1 ? \
    (rb_raise(rb_eArgError, "unnumbered(%d) mixed with numbered", nextarg), 0) : \
    posarg == -2 ? \
    (rb_raise(rb_eArgError, "unnumbered(%d) mixed with named", nextarg), 0) : \
    (posarg = nextarg++, GETNTHARG(posarg)))

#define GETPOSARG(n) (posarg > 0 ? \
    (rb_raise(rb_eArgError, "numbered(%d) after unnumbered(%d)", n, posarg), 0) : \
    posarg == -2 ? \
    (rb_raise(rb_eArgError, "numbered(%d) after named", n), 0) : \
    ((n < 1) ? (rb_raise(rb_eArgError, "invalid index - %d$", n), 0) : \
	       (posarg = -1, GETNTHARG(n))))

#define GETNTHARG(nth) \
    ((nth >= argc) ? (rb_raise(rb_eArgError, "too few arguments"), 0) : argv[nth])

#define GETNAMEARG(id) (posarg > 0 ? \
    (rb_raise(rb_eArgError, "named after unnumbered(%d)", posarg), 0) : \
    posarg == -1 ? \
    (rb_raise(rb_eArgError, "named after numbered"), 0) : \
    rb_hash_lookup(get_hash(&hash, argc, argv), id))

#define GETNUM(n, val) \
    for (; p < end && isdigit(*p); p++) {	\
	int next_n = 10 * n + (*p - '0'); \
        if (next_n / 10 != n) {\
	    rb_raise(rb_eArgError, #val " too big"); \
	} \
	n = next_n; \
    } \
    if (p >= end) { \
	rb_raise(rb_eArgError, "malformed format string - %%*[0-9]"); \
    }

#define GETASTER(val) do { \
    t = p++; \
    n = 0; \
    GETNUM(n, val); \
    if (*p == '$') { \
	tmp = GETPOSARG(n); \
    } \
    else { \
	tmp = GETARG(); \
	p = t; \
    } \
    val = NUM2INT(tmp); \
} while (0)

static VALUE
get_hash(volatile VALUE *hash, int argc, const VALUE *argv)
{
    VALUE tmp;

    if (*hash != Qundef) return *hash;
    if (argc != 2) {
	rb_raise(rb_eArgError, "one hash required");
    }
    tmp = rb_check_convert_type(argv[1], T_HASH, "Hash", "to_hash");
    if (NIL_P(tmp)) {
	rb_raise(rb_eArgError, "one hash required");
    }
    return (*hash = tmp);
}

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
 *        %   | A percent sign itself will be displayed.  No argument taken.
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

VALUE
rb_f_sprintf(int argc, const VALUE *argv)
{
    return rb_str_format(argc - 1, argv + 1, GETNTHARG(0));
}

VALUE rb_hash_lookup(VALUE, VALUE);
VALUE rb_str_new_empty(void);

VALUE
rb_str_format(int argc, const VALUE *argv, VALUE fmt)
{
    const UniChar *p, *end;
    VALUE result;

    int width, prec, flags = FNONE;
    int nextarg = 1;
    int posarg = 0;
    int tainted = 0;
    VALUE nextvalue;
    VALUE tmp;
    VALUE str;
    volatile VALUE hash = Qundef;

    CFIndex leng;

#define CHECK_FOR_WIDTH(f)				 \
    if ((f) & FWIDTH) {					 \
	rb_raise(rb_eArgError, "width given twice");	 \
    }							 \
    if ((f) & FPREC0) {					 \
	rb_raise(rb_eArgError, "width after precision"); \
    }
#define CHECK_FOR_FLAGS(f)				 \
    if ((f) & FWIDTH) {					 \
	rb_raise(rb_eArgError, "flag after width");	 \
    }							 \
    if ((f) & FPREC0) {					 \
	rb_raise(rb_eArgError, "flag after precision"); \
    }

    ++argc;
    --argv;
    if (OBJ_TAINTED(fmt)) tainted = 1;
    StringValue(fmt);
    leng = CFStringGetLength((CFStringRef)fmt);
    if ((p = CFStringGetCharactersPtr((CFStringRef)fmt)) == NULL) {
	CFIndex maxlen = CFStringGetMaximumSizeForEncoding(leng,
		kCFStringEncodingUnicode);
	UniChar *pp = (UniChar *)alloca(sizeof(UniChar) * (maxlen + 1));
	if (!pp) {
	    rb_raise(rb_eRuntimeError,
		    "out of memory converting format to Unicode");
	}
	CFStringGetCharacters((CFStringRef)fmt, CFRangeMake(0, leng), pp);
	pp[leng] = 0;
	p = pp;
    }
    end = p + leng;
    result = rb_str_new_empty();

    for (; p < end; p++) {
	const UniChar *t;
	int n;

	for (t = p; t < end && *t != '%'; t++) ;
	PUSH(p, t - p);
	if (t >= end) {
	    /* end of fmt string */
	    goto sprint_exit;
	}
	p = t + 1;		/* skip `%' */

	width = prec = -1;
	nextvalue = Qundef;
      retry:
	switch (*p) {
	  default:
	    if (isprint(*p))
		rb_raise(rb_eArgError, "malformed format string - %%%c", *p);
	    else
		rb_raise(rb_eArgError, "malformed format string");
	    break;

	  case ' ':
	    CHECK_FOR_FLAGS(flags);
	    flags |= FSPACE;
	    p++;
	    goto retry;

	  case '#':
	    CHECK_FOR_FLAGS(flags);
	    flags |= FSHARP;
	    p++;
	    goto retry;

	  case '+':
	    CHECK_FOR_FLAGS(flags);
	    flags |= FPLUS;
	    p++;
	    goto retry;

	  case '-':
	    CHECK_FOR_FLAGS(flags);
	    flags |= FMINUS;
	    p++;
	    goto retry;

	  case '0':
	    CHECK_FOR_FLAGS(flags);
	    flags |= FZERO;
	    p++;
	    goto retry;

	  case '1': case '2': case '3': case '4':
	  case '5': case '6': case '7': case '8': case '9':
	    n = 0;
	    GETNUM(n, width);
	    if (*p == '$') {
		if (nextvalue != Qundef) {
		    rb_raise(rb_eArgError, "value given twice - %d$", n);
		}
		nextvalue = GETPOSARG(n);
		p++;
		goto retry;
	    }
	    CHECK_FOR_WIDTH(flags);
	    width = n;
	    flags |= FWIDTH;
	    goto retry;

	  case '<':
	  case '{':
	    {
		const UniChar *start = p;
		char term = (*p == '<') ? '>' : '}';
		VALUE key;

		for (; p < end && *p != term; ) {
		    p++;
		}
		if (p >= end) {
		    rb_raise(rb_eArgError, "malformed name - unmatched parenthesis");
		}
		CFStringRef sname = CFStringCreateWithCharactersNoCopy(NULL,
			start + 1, p - start - 1, kCFAllocatorNull);
		if (!sname) {
		    rb_raise(rb_eRuntimeError,
			    "can't create CFStringRef of variable name");
		}
		const char *utf8name = CFStringGetCStringPtr((CFStringRef)sname,
			kCFStringEncodingUTF8);
		if (!utf8name) {
		    CFIndex maxlen = CFStringGetMaximumSizeForEncoding(
			    CFStringGetLength(sname),
			    kCFStringEncodingUTF8);
		    utf8name = (const char *)alloca(sizeof(char) * (maxlen + 1));
		    if (!utf8name) {
			rb_raise(rb_eRuntimeError,
				"out of memory converting variable name");
		    }
		    if (!CFStringGetCString(sname, (char *)utf8name, maxlen + 1,
			    kCFStringEncodingUTF8)) {
			rb_raise(rb_eRuntimeError,
				"can't get c string from Unicode name");
		    }
		}
		key = rb_str_new2(utf8name);
		nextvalue = GETNAMEARG(key);
		CFRelease(sname);
		if (term == '}') goto format_s;
		p++;
		goto retry;
	    }

	  case '*':
	    CHECK_FOR_WIDTH(flags);
	    flags |= FWIDTH;
	    GETASTER(width);
	    if (width < 0) {
		flags |= FMINUS;
		width = -width;
	    }
	    p++;
	    goto retry;

	  case '.':
	    if (flags & FPREC0) {
		rb_raise(rb_eArgError, "precision given twice");
	    }
	    flags |= FPREC|FPREC0;

	    prec = 0;
	    p++;
	    if (*p == '*') {
		GETASTER(prec);
		if (prec < 0) {	/* ignore negative precision */
		    flags &= ~FPREC;
		}
		p++;
		goto retry;
	    }

	    GETNUM(prec, precision);
	    goto retry;

	  case '\n':
	  case '\0':
	    p--;
	  case '%':
	    if (flags != FNONE) {
		rb_raise(rb_eArgError, "invalid format character - %%");
	    }
	    PUSH_CSTR("%");
	    break;

	  case 'c':
	    {
		VALUE val = GETARG();
		VALUE tmp;
		UniChar c;

		tmp = rb_check_string_type(val);
		if (!NIL_P(tmp)) {
		    if (CFStringGetLength((CFStringRef)tmp) != 1) {
			rb_raise(rb_eArgError, "%%c requires a character");
		    }
		    c = CFStringGetCharacterAtIndex((CFStringRef)tmp, 0);
		}
		else {
		    c = NUM2INT(val);
		}
		n = 1;
		if (!(flags & FWIDTH)) width = 1;
		CFStringAppendFormat((CFMutableStringRef)result, NULL,
			(flags & FMINUS) ? CFSTR("%-*c") : CFSTR("%*c"),
			width, c);
	    }
	    break;

	  // %@ ignores all flags, widths and precisions
	  case '@':
	    {
		VALUE arg = GETARG();
		CFStringAppendFormat((CFMutableStringRef)result, NULL,
			CFSTR("%@"), arg);
	    }
	    break;

	  case 's':
	  case 'p':
	  format_s:
	    {
		VALUE arg = GETARG();
		CFIndex len;

		if (*p == 'p') arg = rb_inspect(arg);
		str = rb_obj_as_string(arg);
		if (OBJ_TAINTED(str)) tainted = 1;
		len = CFStringGetLength((CFStringRef)str);
		if (!(flags&FPREC) || prec > len) prec = len;
		if (!(flags&FWIDTH)) width = prec;
		if (!(flags&FMINUS)) FILL(" ", width - prec);
		CFStringRef sub = NULL;
		if (prec > 0) {
		    if (prec < len) {
			sub = CFStringCreateWithSubstring(NULL,
				(CFStringRef)str, CFRangeMake(0, prec));
			if (sub == NULL) {
			    rb_raise(rb_eRuntimeError,
				    "can't create substring");
			}
			str = (VALUE)sub;
		    }
		    CFStringAppend((CFMutableStringRef)result,
			    (CFStringRef)str);
		    if (sub) CFRelease(sub);
		}
		if (flags&FMINUS) FILL(" ", width - prec);
	    }
	    break;

	  case 'd':
	  case 'i':
	  case 'o':
	  case 'x':
	  case 'X':
	  case 'b':
	  case 'B':
	  case 'u':
	    {
		volatile VALUE tmp1;
		volatile VALUE val = GETARG();
		char fbuf[32], nbuf[64], *s;
		const char *prefix = 0;
		int sign = 0, dots = 0;
		const char *sc = NULL;
		long v = 0;
		int base, bignum = 0;
		int len, pos;

		switch (*p) {
		  case 'd':
		  case 'i':
		  case 'u':
		    sign = 1; break;
		  case 'o':
		  case 'x':
		  case 'X':
		  case 'b':
		  case 'B':
		    if (flags&(FPLUS|FSPACE)) sign = 1;
		    break;
		}
		if (flags & FSHARP) {
		    switch (*p) {
		      case 'o':
			prefix = "0"; break;
		      case 'x':
			prefix = "0x"; break;
		      case 'X':
			prefix = "0X"; break;
		      case 'b':
			prefix = "0b"; break;
		      case 'B':
			prefix = "0B"; break;
		    }
		}

	      bin_retry:
		switch (TYPE(val)) {
		  case T_FLOAT:
		    if (FIXABLE(RFLOAT_VALUE(val))) {
			val = LONG2FIX((long)RFLOAT_VALUE(val));
			goto bin_retry;
		    }
		    val = rb_dbl2big(RFLOAT_VALUE(val));
		    if (FIXNUM_P(val)) goto bin_retry;
		    bignum = 1;
		    break;
		  case T_STRING:
		    val = rb_str_to_inum(val, 0, Qtrue);
		    goto bin_retry;
		  case T_BIGNUM:
		    bignum = 1;
		    break;
		  case T_FIXNUM:
		    v = FIX2LONG(val);
		    break;
		  default:
		    val = rb_Integer(val);
		    goto bin_retry;
		}

		switch (*p) {
		  case 'o':
		    base = 8; break;
		  case 'x':
		  case 'X':
		    base = 16; break;
		  case 'b':
		  case 'B':
		    base = 2; break;
		  case 'u':
		  case 'd':
		  case 'i':
		  default:
		    base = 10; break;
		}

		if (!bignum) {
		    if (base == 2) {
			val = rb_int2big(v);
			goto bin_retry;
		    }
		    if (sign) {
			char c = *p;
			if (c == 'i') c = 'd'; /* %d and %i are identical */
			if (v < 0) {
			    v = -v;
			    sc = "-";
			    width--;
			}
			else if (flags & FPLUS) {
			    sc = "+";
			    width--;
			}
			else if (flags & FSPACE) {
			    sc = " ";
			    width--;
			}
			snprintf(fbuf, sizeof(fbuf), "%%l%c", c);
			snprintf(nbuf, sizeof(nbuf), fbuf, v);
			s = nbuf;
		    }
		    else {
			s = nbuf;
			if (v < 0) {
			    dots = 1;
			}
			snprintf(fbuf, sizeof(fbuf), "%%l%c", *p == 'X' ? 'x' : *p);
			snprintf(++s, sizeof(nbuf) - 1, fbuf, v);
			if (v < 0) {
			    char d = 0;

			    s = remove_sign_bits(s, base);
			    switch (base) {
			      case 16:
				d = 'f'; break;
			      case 8:
				d = '7'; break;
			    }
			    if (d && *s != d) {
				*--s = d;
			    }
			}
		    }
		}
		else {
		    if (sign) {
			tmp = rb_big2str(val, base);
			s = (char *)RSTRING_PTR(tmp);
			if (s[0] == '-') {
			    s++;
			    sc = "-";
			    width--;
			}
			else if (flags & FPLUS) {
			    sc = "+";
			    width--;
			}
			else if (flags & FSPACE) {
			    sc = " ";
			    width--;
			}
		    }
		    else {
			if (!RBIGNUM_SIGN(val)) {
			    val = rb_big_clone(val);
			    rb_big_2comp(val);
			}
			tmp1 = tmp = rb_big2str0(val, base, RBIGNUM_SIGN(val));
			s = (char *)RSTRING_PTR(tmp);
			if (*s == '-') {
			    dots = 1;
			    if (base == 10) {
				rb_warning("negative number for %%u specifier");
			    }
			    s = remove_sign_bits(++s, base);
			    switch (base) {
			      case 16:
				if (s[0] != 'f') *--s = 'f'; break;
			      case 8:
				if (s[0] != '7') *--s = '7'; break;
			      case 2:
				if (s[0] != '1') *--s = '1'; break;
			    }
			}
		    }
		}

		pos = -1;
		len = strlen(s);
		if (dots) {
		    prec -= 2;
		    width -= 2;
		}

		if (*p == 'X') {
		    char *pp = s;
		    int c;
		    while ((c = (int)(unsigned char)*pp) != 0) {
			*pp = toupper(c);
			pp++;
		    }
		}
		if (prefix && !prefix[1]) { /* octal */
		    if (dots) {
			prefix = 0;
		    }
		    else if (len == 1 && *s == '0') {
			len = 0;
			if (flags & FPREC) prec--;
		    }
		    else if ((flags & FPREC) && (prec > len)) {
			prefix = 0;
		    }
		}
		else if (len == 1 && *s == '0') {
		    prefix = 0;
		}
		if (prefix) {
		    width -= strlen(prefix);
		}
		if ((flags & (FZERO|FMINUS|FPREC)) == FZERO) {
		    prec = width;
		    width = 0;
		}
		else {
		    if (prec < len) {
			if (!prefix && prec == 0 && len == 1 && *s == '0') len = 0;
			prec = len;
		    }
		    width -= prec;
		}
		if (!(flags&FMINUS) && width > 0) {
		    FILL(" ", width);
		    width = 0;
		}
		if (sc) PUSH_CSTR(sc);
		if (prefix) {
		    PUSH_CSTR(prefix);
		}
		if (dots) PUSH_CSTR("..");
		if (!bignum && v < 0) {
		    FILL_SIGN_BITS(base, p, prec - len);
		}
		else if ((flags & (FMINUS|FPREC)) != FMINUS) {
		    if (!sign && bignum && !RBIGNUM_SIGN(val)) {
			FILL_SIGN_BITS(base, p, prec - len);
		    } else {
			FILL("0", prec - len);
		    }
		}
		PUSH_CSTR_LEN(s, len);
		FILL(" ", width);
	    }
	    break;

	  case 'a':
	  case 'A':
	    if (rb_strict()) {
		rb_raise(rb_eArgError, "malformed format string - %%%c", *p);
	    }
	    /* drop through */
	  case 'f':
	  case 'g':
	  case 'G':
	  case 'e':
	  case 'E':
	    {
		VALUE val = GETARG();
		double fval;
		int need = 6;
		char fbuf[32];

		fval = RFLOAT_VALUE(rb_Float(val));
		if (isnan(fval) || isinf(fval)) {
		    const char *expr;

		    if (isnan(fval)) {
			expr = "NaN";
		    }
		    else {
			expr = "Inf";
		    }
		    need = strlen(expr);
		    if ((!isnan(fval) && fval < 0.0) || (flags & (FPLUS|FSPACE)))
			need++;
		    if ((flags & FWIDTH) && need < width)
			need = width;

		    if (flags & FMINUS) {
			if (!isnan(fval) && fval < 0.0) {
			    PUSH_CSTR("-");
			    need--;
			} else if (flags & FPLUS) {
			    PUSH_CSTR("+");
			    need--;
			} else if (flags & FSPACE) {
			    PUSH_CSTR(" ");
			    need--;
			}
			PUSH_CSTR(expr);
			need -= strlen(expr);
			FILL(" ", need);
		    }
		    else {
			FILL(" ", need - strlen(expr) - 1);
			if (!isnan(fval) && fval < 0.0)
			    PUSH_CSTR("-");
			else if (flags & FPLUS)
			    PUSH_CSTR("+");
			else if ((flags & FSPACE) && need > width)
			    PUSH_CSTR(" ");
			PUSH_CSTR(expr);
		    }
		    break;
		}

		fmt_setup(fbuf, sizeof(fbuf), *p, flags, width, prec);

		char *fout;
		asprintf(&fout, fbuf, fval);
		if (!fout) {
		    rb_raise(rb_eRuntimeError,
			    "out of memory converting floating point value");
		}
		PUSH_CSTR(fout);
		free(fout);
	    }
	    break;
	}
	flags = FNONE;
    }

  sprint_exit:
    /* XXX - We cannot validate the number of arguments if (digit)$ style used.
     */
    if (posarg >= 0 && nextarg < argc) {
	const char *mesg = "too many arguments for format string";
	if (RTEST(ruby_debug)) rb_raise(rb_eArgError, "%s", mesg);
	if (RTEST(ruby_verbose)) rb_warn("%s", mesg);
    }

    if (tainted) OBJ_TAINT(result);
    return result;
}

static void
fmt_setup(char *buf, size_t size, int c, int flags, int width, int prec)
{
    char *end = buf + size;
    *buf++ = '%';
    if (flags & FSHARP) *buf++ = '#';
    if (flags & FPLUS)  *buf++ = '+';
    if (flags & FMINUS) *buf++ = '-';
    if (flags & FZERO)  *buf++ = '0';
    if (flags & FSPACE) *buf++ = ' ';

    if (flags & FWIDTH) {
	snprintf(buf, end - buf, "%d", width);
	buf += strlen(buf);
    }

    if (flags & FPREC) {
	snprintf(buf, end - buf, ".%d", prec);
	buf += strlen(buf);
    }

    *buf++ = c;
    *buf = '\0';
}

VALUE
rb_f_sprintf_imp(VALUE recv, SEL sel, int argc, VALUE *argv)
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
    VALUE result;
    va_list ap;

    va_start(ap, format);
    result = rb_enc_vsprintf(enc, format, ap);
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
    VALUE result;
    va_list ap;

    va_start(ap, format);
    result = rb_vsprintf(format, ap);
    va_end(ap);

    return result;
}
