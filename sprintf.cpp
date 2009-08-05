/*
 * MacRuby implementation of Ruby 1.9's sprintf.c.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2007-2008, Apple Inc. All rights reserved.
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 * Copyright (C) 2000  Network Applied Communication Laboratory, Inc.
 * Copyright (C) 2000  Information-technology Promotion Agency, Japan
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

#include <stdarg.h>

#include "ruby/ruby.h"
#include "ruby/node.h"
#include "ruby/encoding.h"
#include "vm.h"
#include "compiler.h"

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
     ((nth >= argc) ? (rb_raise(rb_eArgError, "too few arguments"), 0) : argv[nth])

extern "C" {

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

static void
get_types_for_format_str(std::string &octypes, const unsigned int len,
			 VALUE *args, const char *format_str, char **new_fmt)
{
    size_t format_str_len = strlen(format_str);
    unsigned int i = 0, j = 0;

    while (i < format_str_len) {
	bool sharp_modifier = false;
	bool star_modifier = false;
	if (format_str[i++] != '%') {
	    continue;
	}
	if (i < format_str_len && format_str[i] == '%') {
	    i++;
	    continue;
	}
	while (i < format_str_len) {
	    char type = 0;
	    switch (format_str[i]) {
		case '#':
		    sharp_modifier = true;
		    break;

		case '*':
		    star_modifier = true;
		    type = _C_INT;
		    break;

		case 'd':
		case 'i':
		case 'o':
		case 'u':
		case 'x':
		case 'X':
		    type = _C_INT;
		    break;

		case 'c':
		case 'C':
		    type = _C_CHR;
		    break;

		case 'D':
		case 'O':
		case 'U':
		    type = _C_LNG;
		    break;

		case 'f':       
		case 'F':
		case 'e':       
		case 'E':
		case 'g':       
		case 'G':
		case 'a':
		case 'A':
		    type = _C_DBL;
		    break;

		case 's':
		case 'S':
		    {
			if (i - 1 > 0) {
			    unsigned long k = i - 1;
			    while (k > 0 && format_str[k] == '0') {
				k--;
			    }
			    if (k < i && format_str[k] == '.') {
				args[j] = (VALUE)CFSTR("");
			    }
			}
#if 1
			// In Ruby, '%s' is supposed to convert the argument
			// as a string, calling #to_s on it. In order to
			// support this behavior we are changing the format
			// to '@' which sends the -[NSObject description]
			// message, exactly what we want.
			if (*new_fmt == NULL) {
			    *new_fmt = strdup(format_str);
			}
			(*new_fmt)[i] = '@';
			type = _C_ID;
#else
			type = _C_CHARPTR;
#endif
		    }
		    break;

		case 'p':
		    type = _C_PTR;
		    break;

		case '@':
		    type = _C_ID;
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
			if (*new_fmt == NULL) {
			    *new_fmt = strdup(format_str);
			}
			(*new_fmt)[i] = '@';
			args[j] = arg;
			type = _C_ID;
		    }
		    break;
	    }

	    i++;

	    if (type != 0) {
		if (len == 0 || j >= len) {
		    rb_raise(rb_eArgError, 
			    "Too much tokens in the format string `%s' "\
			    "for the given %d argument(s)", format_str, len);
		}
		octypes.push_back(type);
		j++;
		if (!star_modifier) {
		    break;
		}
	    }
	}
    }
    for (; j < len; j++) {
	octypes.push_back(_C_ID);
    }
}

VALUE
rb_str_format(int argc, const VALUE *argv, VALUE fmt)
{
    if (argc == 0) {
	return fmt;
    }

    char *new_fmt = NULL;
    std::string types("@@@@");
    get_types_for_format_str(types, (unsigned int)argc, (VALUE *)argv, 
	    RSTRING_PTR(fmt), &new_fmt);

    if (new_fmt != NULL) {
	fmt = rb_str_new2(new_fmt);
    }  

    VALUE *stub_args = (VALUE *)alloca(sizeof(VALUE) * argc + 4);
    stub_args[0] = Qnil; // allocator
    stub_args[1] = Qnil; // format options
    stub_args[2] = fmt;  // format string
    for (int i = 0; i < argc; i++) {
	stub_args[3 + i] = argv[i];
    }

    rb_vm_c_stub_t *stub = (rb_vm_c_stub_t *)GET_CORE()->gen_stub(types,
	    3, false);

    VALUE str = (*stub)((IMP)&CFStringCreateWithFormat, argc + 3, stub_args);
    CFMakeCollectable((void *)str);
    return str;
}

} // extern "C"
