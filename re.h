/* 
 * MacRuby Regular Expressions.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 * 
 * Copyright (C) 2010, Apple Inc. All rights reserved.
 */

#ifndef __RE_H_
#define __RE_H_

#if defined(__cplusplus)
extern "C" {
#endif

bool rb_char_to_icu_option(int c, int *option);
VALUE regexp_eqq(VALUE rcv, SEL sel, VALUE str);
int rb_reg_search(VALUE re, VALUE str, int pos, bool reverse);

static inline int
rb_reg_adjust_startpos(VALUE re, VALUE str, int pos, bool reverse)
{
    return reverse ? -pos : rb_str_chars_len(str) - pos;
}

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // __RE_H_
