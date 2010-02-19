/* 
 * MacRuby Regular Expressions.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 * 
 * Copyright (C) 2007-2010, Apple Inc. All rights reserved.
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 */

#include "unicode/regex.h"
#include "unicode/unistr.h"
#include "ruby/ruby.h"
#include "encoding.h"

extern "C" {

VALUE rb_eRegexpError;
VALUE rb_cRegexp;
VALUE rb_cMatch;

typedef struct rb_regexp {
    struct RBasic basic;
    UnicodeString *unistr;
    RegexPattern *pattern;
} rb_regexp_t;

#define RREGEXP(o) ((rb_regexp_t *)o)

typedef struct rb_match_result {
    unsigned int beg;
    unsigned int end;
} rb_match_result_t;

typedef struct rb_match {
    struct RBasic basic;
    rb_regexp_t *regexp;
    UnicodeString *unistr;
    rb_match_result_t *results;
    int results_count;
} rb_match_t;

#define RMATCH(o) ((rb_match_t *)o)

static rb_regexp_t *
regexp_alloc(void)
{
    NEWOBJ(re, struct rb_regexp);
    OBJSETUP(re, rb_cRegexp, T_REGEXP);
    re->unistr = NULL;
    re->pattern = NULL;
    return re;
}

static rb_match_t *
match_alloc(void)
{
    NEWOBJ(match, struct rb_match);
    OBJSETUP(match, rb_cMatch, T_MATCH);
    match->regexp = NULL;
    match->unistr = NULL;
    return match;
}

static void
regexp_finalize(rb_regexp_t *regexp)
{
    if (regexp->unistr != NULL) {
	delete regexp->unistr;
	regexp->unistr = NULL;
    }
    if (regexp->pattern != NULL) {
	delete regexp->pattern;
	regexp->pattern = NULL;
    }
}

static void
match_finalize(rb_match_t *match)
{
    if (match->unistr != NULL) {
	delete match->unistr;
	match->unistr = NULL;
    }
}

static UnicodeString *
str_to_unistr(VALUE str)
{
    UChar *chars = NULL;
    long chars_len = 0;
    bool need_free = false;

    rb_str_get_uchars(str, &chars, &chars_len, &need_free);

    UnicodeString *unistr = new UnicodeString(chars, chars_len);

    if (need_free && chars != NULL) {
	free(chars);
    }

    return unistr;
}

static bool
init_from_string(rb_regexp_t *regexp, VALUE str, int option, VALUE *excp)
{
    UnicodeString *unistr = str_to_unistr(str);
    assert(unistr != NULL);

    UParseError pe;
    UErrorCode status = U_ZERO_ERROR;
    RegexPattern *pattern = RegexPattern::compile(*unistr, pe, status);

    if (pattern == NULL) {
	delete unistr;
	if (excp != NULL) {
	    char error[1024];
	    snprintf(error, sizeof error, "regexp compilation error: %s",
		    u_errorName(status));
	    *excp = rb_exc_new2(rb_eRegexpError, error);
	}
	return false;
    }

    regexp_finalize(regexp);
    regexp->pattern = pattern;
    regexp->unistr = unistr;

    return true;
}

static VALUE
rb_regexp_alloc(VALUE klass, SEL sel)
{
    return (VALUE)regexp_alloc();
}

static VALUE
rb_regexp_initialize(VALUE self, SEL sel, int argc, VALUE *argv)
{
    if (argc == 0 || argc > 3) {
	rb_raise(rb_eArgError, "wrong number of arguments");
    }
    if (TYPE(argv[0]) == T_REGEXP) {
	VALUE re = argv[0];
	if (argc > 1) {
	    rb_warn("flags ignored");
	}
	assert(RREGEXP(re)->pattern != NULL);
	regexp_finalize(RREGEXP(self));
	RREGEXP(self)->unistr = new UnicodeString(*RREGEXP(re)->unistr);
	RREGEXP(self)->pattern = new RegexPattern(*RREGEXP(re)->pattern);
    }
    else {
	int options = 0;
	if (argc >= 2) {
	    if (FIXNUM_P(argv[1])) {
		options = FIX2INT(argv[1]);
	    }
#if 0 // TODO
	    else if (RTEST(argv[1])) {
		options = ONIG_OPTION_IGNORECASE;
	    }
#endif
	}
	VALUE str = argv[0];
	StringValue(str);

	VALUE exc = Qnil;
	if (!init_from_string(RREGEXP(self), str, options, &exc)) {
	    rb_exc_raise(exc);
	}
    }
    return self;
}

static VALUE
rb_regexp_inspect(VALUE rcv, SEL sel)
{
    assert(RREGEXP(rcv)->unistr != NULL);
    const UChar *chars = RREGEXP(rcv)->unistr->getBuffer();
    const int32_t chars_len = RREGEXP(rcv)->unistr->length();
    assert(chars_len >= 0);
    return rb_unicode_str_new(chars, chars_len);
}

static VALUE
reg_operand(VALUE s, bool check)
{
    if (SYMBOL_P(s)) {
	return rb_sym_to_s(s);
    }
    else {
	VALUE tmp = rb_check_string_type(s);
	if (check && NIL_P(tmp)) {
	    rb_raise(rb_eTypeError, "can't convert %s to String",
		     rb_obj_classname(s));
	}
	return tmp;
    }
}

static int
rb_reg_adjust_startpos(VALUE re, VALUE str, int pos, bool reverse)
{
    return reverse ? -pos : rb_str_chars_len(str) - pos;
}

static int
rb_reg_search(VALUE re, VALUE str, int pos, bool reverse)
{
    const long len = rb_str_chars_len(str);
    if (pos > len || pos < 0) {
	rb_backref_set(Qnil);
	return -1;
    }

    UnicodeString *unistr = str_to_unistr(str);
    assert(unistr != NULL);

    UErrorCode status = U_ZERO_ERROR;
    assert(RREGEXP(re)->pattern != NULL);
    RegexMatcher *matcher = RREGEXP(re)->pattern->matcher(*unistr, status);

    if (matcher == NULL) {
	rb_raise(rb_eRegexpError, "can't create matcher: %s",
		u_errorName(status));
    }

    if (!matcher->find()) {
	delete matcher;
	rb_backref_set(Qnil);
	return -1;
    }

    const int res_count = 1 + matcher->groupCount();
    rb_match_result_t *res =
	(rb_match_result_t *)xmalloc(sizeof(rb_match_result_t) * res_count);

    res[0].beg = matcher->start(status);
    res[0].end = matcher->end(status);

    for (int i = 0; i < matcher->groupCount(); i++) {
	res[i + 1].beg = matcher->start(i + 1, status);
	res[i + 1].end = matcher->end(i + 1, status);
    }

    delete matcher;

    VALUE match = rb_backref_get();
    if (NIL_P(match)) {
	match = (VALUE)match_alloc();
	rb_backref_set(match);
    }

    match_finalize(RMATCH(match));
    GC_WB(&RMATCH(match)->regexp, re);
    RMATCH(match)->unistr = unistr;
    GC_WB(&RMATCH(match)->results, res);
    RMATCH(match)->results_count = res_count;

    return res[0].beg;
}

long
reg_match_pos(VALUE re, VALUE *strp, long pos)
{
    VALUE str = *strp;

    if (NIL_P(str)) {
	rb_backref_set(Qnil);
	return -1;
    }
    *strp = str = reg_operand(str, true);
    if (pos != 0) {
	if (pos < 0) {
	    VALUE l = rb_str_length(str);
	    pos += NUM2INT(l);
	    if (pos < 0) {
		return pos;
	    }
	}
	pos = rb_reg_adjust_startpos(re, str, pos, false);
    }
    return rb_reg_search(re, str, pos, 0);
}

static VALUE
rb_regexp_match(VALUE rcv, SEL sel, int argc, VALUE *argv)
{
    VALUE result, str, initpos;
    long pos;

    if (rb_scan_args(argc, argv, "11", &str, &initpos) == 2) {
	pos = NUM2LONG(initpos);
    }
    else {
	pos = 0;
    }

    pos = reg_match_pos(rcv, &str, pos);
    if (pos < 0) {
	rb_backref_set(Qnil);
	return Qnil;
    }
    result = rb_backref_get();
    rb_match_busy(result);
    if (!NIL_P(result) && rb_block_given_p()) {
	return rb_yield(result);
    }
    return result;
}

VALUE
rb_reg_nth_match(int nth, VALUE match)
{
    if (NIL_P(match)) {
	return Qnil;
    }
    if (nth >= RMATCH(match)->results_count) {
	return Qnil;
    }
    if (nth < 0) {
	nth += RMATCH(match)->results_count;
	if (nth <= 0) {
	    return Qnil;
	}
    }

    const int beg = RMATCH(match)->results[nth].beg;
    const int len = RMATCH(match)->results[nth].end - beg;

    UnicodeString *unistr = RMATCH(match)->unistr;
    assert(unistr != NULL);
    assert(beg + len <= unistr->length());
    const UChar *chars = unistr->getBuffer();
    return rb_unicode_str_new(&chars[beg], len);
}

static VALUE
match_inspect(VALUE rcv, SEL sel)
{
    VALUE str = rb_str_buf_new2("#<");
    rb_str_buf_cat2(str, rb_obj_classname(rcv));
    for (int i = 0; i < RMATCH(rcv)->results_count; i++) {
	rb_str_buf_cat2(str, " ");
	if (i > 0) {
	    char buf[10];
	    snprintf(buf, sizeof buf, "%d:", i);
	    rb_str_buf_cat2(str, buf);
	}
	VALUE v = rb_reg_nth_match(i, rcv);
	if (v == Qnil) {
	    rb_str_buf_cat2(str, "nil");
	}
	else {
	    rb_str_buf_append(str, rb_str_inspect(v));
	}
    }
    rb_str_buf_cat2(str, ">");
    return str;
}

void
Init_Regexp(void)
{
    rb_eRegexpError = rb_define_class("RegexpError", rb_eStandardError);

#if 0
    rb_define_virtual_variable("$~", match_getter, match_setter);
    rb_define_virtual_variable("$&", last_match_getter, 0);
    rb_define_virtual_variable("$`", prematch_getter, 0);
    rb_define_virtual_variable("$'", postmatch_getter, 0);
    rb_define_virtual_variable("$+", last_paren_match_getter, 0);

    rb_define_virtual_variable("$=", ignorecase_getter, ignorecase_setter);
    rb_define_virtual_variable("$KCODE", kcode_getter, kcode_setter);
    rb_define_virtual_variable("$-K", kcode_getter, kcode_setter);
#endif

    rb_cRegexp = rb_define_class("Regexp", rb_cObject);
    rb_objc_define_method(*(VALUE *)rb_cRegexp, "alloc",
	    (void *)rb_regexp_alloc, 0);
#if 0
    rb_objc_define_method(*(VALUE *)rb_cRegexp, "compile",
	    rb_class_new_instance_imp, -1);
    rb_objc_define_method(*(VALUE *)rb_cRegexp, "quote", rb_reg_s_quote, 1);
    rb_objc_define_method(*(VALUE *)rb_cRegexp, "escape", rb_reg_s_quote, 1);
    rb_objc_define_method(*(VALUE *)rb_cRegexp, "union", rb_reg_s_union_m, -2);
    rb_objc_define_method(*(VALUE *)rb_cRegexp, "last_match",
	    rb_reg_s_last_match, -1);
    rb_objc_define_method(*(VALUE *)rb_cRegexp, "try_convert",
	    rb_reg_s_try_convert, 1);

    rb_objc_reg_finalize_super = rb_objc_install_method2((Class)rb_cRegexp,
	    "finalize", (IMP)rb_objc_reg_finalize);
#endif

    rb_objc_define_method(rb_cRegexp, "initialize",
	    (void *)rb_regexp_initialize, -1);

#if 0
    rb_objc_define_method(rb_cRegexp, "initialize_copy", rb_reg_init_copy, 1);
    rb_objc_define_method(rb_cRegexp, "hash", rb_reg_hash, 0);
    rb_objc_define_method(rb_cRegexp, "eql?", rb_reg_equal, 1);
    rb_objc_define_method(rb_cRegexp, "==", rb_reg_equal, 1);
    rb_objc_define_method(rb_cRegexp, "=~", rb_reg_match_imp, 1);
    rb_objc_define_method(rb_cRegexp, "===", rb_reg_eqq, 1);
    rb_objc_define_method(rb_cRegexp, "~", rb_reg_match2, 0);
    rb_objc_define_method(rb_cRegexp, "to_s", rb_reg_to_s, 0);
    rb_objc_define_method(rb_cRegexp, "source", rb_reg_source, 0);
    rb_objc_define_method(rb_cRegexp, "casefold?", rb_reg_casefold_p, 0);
    rb_objc_define_method(rb_cRegexp, "options", rb_reg_options_m, 0);
    rb_objc_define_method(rb_cRegexp, "encoding", rb_reg_encoding, 0);
    rb_objc_define_method(rb_cRegexp, "fixed_encoding?",
	    rb_reg_fixed_encoding_p, 0);
    rb_objc_define_method(rb_cRegexp, "names", rb_reg_names, 0);
    rb_objc_define_method(rb_cRegexp, "named_captures",
	    rb_reg_named_captures, 0);
#endif
    rb_objc_define_method(rb_cRegexp, "match", (void *)rb_regexp_match, -1);
    rb_objc_define_method(rb_cRegexp, "inspect", (void *)rb_regexp_inspect, 0);

    rb_cMatch  = rb_define_class("MatchData", rb_cObject);
#if 0
    rb_objc_define_method(*(VALUE *)rb_cMatch, "alloc", match_alloc, 0);
    rb_undef_method(CLASS_OF(rb_cMatch), "new");

    rb_objc_match_finalize_super = rb_objc_install_method2((Class)rb_cMatch,
	    "finalize", (IMP)rb_objc_match_finalize);

    rb_objc_define_method(rb_cMatch, "initialize_copy", match_init_copy, 1);
    rb_objc_define_method(rb_cMatch, "regexp", match_regexp, 0);
    rb_objc_define_method(rb_cMatch, "names", match_names, 0);
    rb_objc_define_method(rb_cMatch, "size", match_size, 0);
    rb_objc_define_method(rb_cMatch, "length", match_size, 0);
    rb_objc_define_method(rb_cMatch, "offset", match_offset, 1);
    rb_objc_define_method(rb_cMatch, "begin", match_begin, 1);
    rb_objc_define_method(rb_cMatch, "end", match_end, 1);
    rb_objc_define_method(rb_cMatch, "to_a", match_to_a, 0);
    rb_objc_define_method(rb_cMatch, "[]", match_aref, -1);
    rb_objc_define_method(rb_cMatch, "captures", match_captures, 0);
    rb_objc_define_method(rb_cMatch, "values_at", match_values_at, -1);
    rb_objc_define_method(rb_cMatch, "pre_match", rb_reg_match_pre, 0);
    rb_objc_define_method(rb_cMatch, "post_match", rb_reg_match_post, 0);
    rb_objc_define_method(rb_cMatch, "to_s", match_to_s, 0);
    rb_objc_define_method(rb_cMatch, "string", match_string, 0);
#endif

    rb_objc_define_method(rb_cMatch, "inspect", (void *)match_inspect, 0);
}

VALUE
rb_reg_check_preprocess(VALUE str)
{
    return Qnil;
}

static VALUE
rb_str_compile_regexp(VALUE str, int options, VALUE *excp)
{
    rb_regexp_t *regexp = regexp_alloc();
    if (!init_from_string(regexp, str, options, excp)) {
	return Qnil;
    }
    return (VALUE)regexp;
}

VALUE
rb_reg_compile(VALUE str, int options)
{
    VALUE exc = Qnil;
    VALUE regexp = rb_str_compile_regexp(str, options, &exc); 
    if (regexp == Qnil) {
	rb_set_errinfo(exc);
    }
    return regexp;
}

VALUE
rb_reg_new_str(VALUE str, int options)
{
    VALUE exc = Qnil;
    VALUE regexp = rb_str_compile_regexp(str, options, &exc); 
    if (regexp == Qnil) {
	rb_exc_raise(exc);
    }
    return regexp;
}

VALUE
rb_reg_new(const char *cstr, long len, int options)
{
    return rb_reg_new_str(rb_usascii_str_new(cstr, len), options);
}

int
rb_reg_options(VALUE re)
{
    return 0;
}

VALUE
rb_reg_last_match(VALUE match)
{
    return rb_reg_nth_match(0, match);
}

VALUE
rb_reg_match_last(VALUE match)
{
    return Qnil;
}

VALUE
rb_reg_match_pre(VALUE match, SEL sel)
{
    return Qnil;
}

VALUE
rb_reg_match_post(VALUE match, SEL sel)
{
    return Qnil;
}

void
rb_match_busy(VALUE match)
{
    // Do nothing.
}

int
rb_char_to_option_kcode(int c, int *option, int *kcode)
{
    // TODO
    *option = 0;
    *kcode = -1;
    return *option;
}

VALUE
rb_reg_eqq(VALUE re, SEL sel, VALUE str)
{
    return Qfalse;
}

} // extern "C"
