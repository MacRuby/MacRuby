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

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // __RE_H_
