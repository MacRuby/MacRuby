/*
 * MacRuby Regular Expressions.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2011, Apple Inc. All rights reserved.
 */

#ifndef __RE_H_
#define __RE_H_

#if defined(__cplusplus)
extern "C" {
#endif

bool rb_char_to_icu_option(int c, int *option);

VALUE regexp_eqq(VALUE rcv, SEL sel, VALUE str);
VALUE regexp_match(VALUE rcv, SEL sel, VALUE str);
VALUE regexp_match2(VALUE rcv, SEL sel, int argc, VALUE *argv);

VALUE rb_reg_quote(VALUE pat);
VALUE rb_reg_regcomp(VALUE str);
VALUE rb_regexp_source(VALUE re);

VALUE rb_reg_matcher_new(VALUE re, VALUE str);
int rb_reg_matcher_search_find(VALUE re, VALUE matcher, int pos, bool reverse,
	bool findFirst);
void rb_reg_matcher_destroy(VALUE matcher);

static inline int
rb_reg_matcher_search_first(VALUE re, VALUE matcher, int pos, bool reverse)
{
    return rb_reg_matcher_search_find(re, matcher, pos, reverse, true);
}

static inline int
rb_reg_matcher_search_next(VALUE re, VALUE matcher, int pos, bool reverse)
{
    return rb_reg_matcher_search_find(re, matcher, pos, reverse, false);
}

static inline int
rb_reg_matcher_search(VALUE re, VALUE matcher, int pos, bool reverse)
{
    return rb_reg_matcher_search_next(re, matcher, pos, reverse);
}

static inline int
rb_reg_search(VALUE re, VALUE str, int pos, bool reverse)
{
    VALUE matcher = rb_reg_matcher_new(re, str);
    const int res = rb_reg_matcher_search_first(re, matcher, pos, reverse);
    rb_reg_matcher_destroy(matcher);
    return res; 
}

int rb_reg_options_to_mri(int opt);
int rb_reg_options_from_mri(int mri_opt);

void regexp_get_uchars(VALUE re, const UChar **chars_p, int32_t *chars_len_p);

typedef struct rb_match_result {
    unsigned int beg;
    unsigned int end;
} rb_match_result_t;

rb_match_result_t *rb_reg_match_results(VALUE match, int *count);

static inline int
rb_reg_adjust_startpos(VALUE re, VALUE str, int pos, bool reverse)
{
    return reverse ? -pos : pos;
}

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // __RE_H_
