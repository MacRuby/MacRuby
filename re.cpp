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
#include "objc.h"

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
regexp_alloc(VALUE klass, SEL sel)
{
    NEWOBJ(re, struct rb_regexp);
    OBJSETUP(re, klass, T_REGEXP);
    re->unistr = NULL;
    re->pattern = NULL;
    return re;
}

static rb_match_t *
match_alloc(VALUE klass, SEL sel)
{
    NEWOBJ(match, struct rb_match);
    OBJSETUP(match, klass, T_MATCH);
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

static IMP regexp_finalize_imp_super = NULL; 

static void
regexp_finalize_imp(void *rcv, SEL sel)
{
    regexp_finalize(RREGEXP(rcv));
    if (regexp_finalize_imp_super != NULL) {
	((void(*)(void *, SEL))regexp_finalize_imp_super)(rcv, sel);
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

static IMP match_finalize_imp_super = NULL; 

static void
match_finalize_imp(void *rcv, SEL sel)
{
    match_finalize(RMATCH(rcv));
    if (match_finalize_imp_super != NULL) {
	((void(*)(void *, SEL))match_finalize_imp_super)(rcv, sel);
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
    RegexPattern *pattern = RegexPattern::compile(*unistr, option, pe, status);

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

static void
init_from_regexp(rb_regexp_t *regexp, rb_regexp_t *from)
{
    regexp_finalize(regexp);
    regexp->unistr = new UnicodeString(*from->unistr);
    regexp->pattern = new RegexPattern(*from->pattern);
}

static VALUE
rb_str_compile_regexp(VALUE str, int options, VALUE *excp)
{
    rb_regexp_t *regexp = regexp_alloc(rb_cRegexp, 0);
    if (!init_from_string(regexp, str, options, excp)) {
	return Qnil;
    }
    return (VALUE)regexp;
}

#define REGEXP_OPT_IGNORECASE 	(UREGEX_CASE_INSENSITIVE)
#define REGEXP_OPT_EXTENDED 	(UREGEX_COMMENTS)
#define REGEXP_OPT_MULTILINE	(UREGEX_MULTILINE | UREGEX_DOTALL)

bool
rb_char_to_icu_option(int c, int *option)
{
    assert(option != NULL);
    switch (c) {
	case 'i':
	    *option = REGEXP_OPT_IGNORECASE;
	    return true;
	case 'x':
	    *option = REGEXP_OPT_EXTENDED;
	    return true;
	case 'm':
	    *option = REGEXP_OPT_MULTILINE;
	    return true;
    }
    *option = -1;
    return false;
}

/*
 *  call-seq:
 *     Regexp.new(string [, options])                => regexp
 *     Regexp.new(regexp)                            => regexp
 *     Regexp.compile(string [, options])            => regexp
 *     Regexp.compile(regexp)                        => regexp
 *
 *  Constructs a new regular expression from <i>pattern</i>, which can be either
 *  a <code>String</code> or a <code>Regexp</code> (in which case that regexp's
 *  options are propagated, and new options may not be specified (a change as of
 *  Ruby 1.8). If <i>options</i> is a <code>Fixnum</code>, it should be one or
 *  more of the constants <code>Regexp::EXTENDED</code>,
 *  <code>Regexp::IGNORECASE</code>, and <code>Regexp::MULTILINE</code>,
 *  <em>or</em>-ed together. Otherwise, if <i>options</i> is not
 *  <code>nil</code>, the regexp will be case insensitive.
 *
 *     r1 = Regexp.new('^a-z+:\\s+\w+')           #=> /^a-z+:\s+\w+/
 *     r2 = Regexp.new('cat', true)               #=> /cat/i
 *     r3 = Regexp.new('dog', Regexp::EXTENDED)   #=> /dog/x
 *     r4 = Regexp.new(r2)                        #=> /cat/i
 */

static VALUE
regexp_initialize(VALUE self, SEL sel, int argc, VALUE *argv)
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
	init_from_regexp(RREGEXP(self), RREGEXP(re));
    }
    else {
	int options = 0;
	if (argc >= 2) {
	    if (FIXNUM_P(argv[1])) {
		options = FIX2INT(argv[1]);
	    }
	    else if (RTEST(argv[1])) {
		options = REGEXP_OPT_IGNORECASE;
	    }
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
regexp_initialize_copy(VALUE rcv, SEL sel, VALUE other)
{
    if (TYPE(other) != T_REGEXP) {
	rb_raise(rb_eTypeError, "wrong argument type");
    }
    init_from_regexp(RREGEXP(rcv), RREGEXP(other));
    return rcv;
}

/*
 *  call-seq:
 *     rxp == other_rxp      => true or false
 *     rxp.eql?(other_rxp)   => true or false
 *
 *  Equality---Two regexps are equal if their patterns are identical, they have
 *  the same character set code, and their <code>casefold?</code> values are the
 *  same.
 *
 *     /abc/  == /abc/x   #=> false
 *     /abc/  == /abc/i   #=> false
 *     /abc/  == /abc/n   #=> false
 *     /abc/u == /abc/n   #=> false
 */

static VALUE
regexp_equal(VALUE rcv, SEL sel, VALUE other)
{
    if (rcv == other) {
	return Qtrue;
    }
    if (TYPE(other) != T_REGEXP) {
	return Qfalse;
    }

    assert(RREGEXP(rcv)->unistr != NULL && RREGEXP(rcv)->pattern != NULL);
    assert(RREGEXP(other)->unistr != NULL && RREGEXP(other)->pattern != NULL);

    // Using the == operator on the RegexpPatterns does not work, for a
    // reason... so we are comparing source strings and flags.
    return *RREGEXP(rcv)->unistr == *RREGEXP(other)->unistr
	&& RREGEXP(rcv)->pattern->flags() == RREGEXP(other)->pattern->flags()
	? Qtrue : Qfalse;
}

/*
 *  call-seq:
 *     rxp =~ str    => integer or nil
 *
 *  Match---Matches <i>rxp</i> against <i>str</i>.
 *
 *     /at/ =~ "input data"   #=> 7
 *     /ax/ =~ "input data"   #=> nil
 *
 *  If <code>=~</code> is used with a regexp literal with named captures,
 *  captured strings (or nil) is assigned to local variables named by
 *  the capture names.
 *
 *     /(?<lhs>\w+)\s*=\s*(?<rhs>\w+)/ =~ "  x = y  "
 *     p lhs    #=> "x"
 *     p rhs    #=> "y"
 *
 *  If it is not matched, nil is assigned for the variables.
 *
 *     /(?<lhs>\w+)\s*=\s*(?<rhs>\w+)/ =~ "  x = "   
 *     p lhs    #=> nil
 *     p rhs    #=> nil
 *
 *  This assignment is implemented in the Ruby parser.
 *  So a regexp literal is required for the assignment. 
 *  The assignment is not occur if the regexp is not a literal.
 *
 *     re = /(?<lhs>\w+)\s*=\s*(?<rhs>\w+)/
 *     re =~ "  x = "
 *     p lhs    # undefined local variable
 *     p rhs    # undefined local variable
 *
 *  A regexp interpolation, <code>#{}</code>, also disables
 *  the assignment.
 *
 *     rhs_pat = /(?<rhs>\w+)/
 *     /(?<lhs>\w+)\s*=\s*#{rhs_pat}/ =~ "x = y"
 *     p lhs    # undefined local variable
 */

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
	match = (VALUE)match_alloc(rb_cMatch, 0);
	rb_backref_set(match);
    }

    match_finalize(RMATCH(match));
    GC_WB(&RMATCH(match)->regexp, re);
    RMATCH(match)->unistr = unistr;
    GC_WB(&RMATCH(match)->results, res);
    RMATCH(match)->results_count = res_count;

    return res[0].beg;
}

static long
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
regexp_match(VALUE rcv, SEL sel, VALUE str)
{
    const long pos = reg_match_pos(rcv, &str, 0);
    if (pos < 0) {
	return Qnil;
    }
    return LONG2FIX(pos);
}

/*
 *  call-seq:
 *     rxp.match(str)       => matchdata or nil
 *     rxp.match(str,pos)   => matchdata or nil
 *
 *  Returns a <code>MatchData</code> object describing the match, or
 *  <code>nil</code> if there was no match. This is equivalent to retrieving the
 *  value of the special variable <code>$~</code> following a normal match.
 *  If the second parameter is present, it specifies the position in the string
 *  to begin the search.
 *
 *     /(.)(.)(.)/.match("abc")[2]   #=> "b"
 *     /(.)(.)/.match("abc", 1)[2]   #=> "c"
 *     
 *  If a block is given, invoke the block with MatchData if match succeed, so
 *  that you can write
 *     
 *     pat.match(str) {|m| ...}
 *     
 *  instead of
 *      
 *     if m = pat.match(str)
 *       ...
 *     end
 *      
 *  The return value is a value from block execution in this case.
 */

static VALUE
regexp_match2(VALUE rcv, SEL sel, int argc, VALUE *argv)
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

/*
 *  call-seq:
 *     ~ rxp   => integer or nil
 *
 *  Match---Matches <i>rxp</i> against the contents of <code>$_</code>.
 *  Equivalent to <code><i>rxp</i> =~ $_</code>.
 *
 *     $_ = "input data"
 *     ~ /at/   #=> 7
 */

static VALUE
regexp_match3(VALUE rcv, SEL sel)
{
    VALUE line = rb_lastline_get();
    if (TYPE(line) != T_STRING) {
	rb_backref_set(Qnil);
	return Qnil;
    }

    const long start = rb_reg_search(rcv, line, 0, 0);
    if (start < 0) {
	return Qnil;
    }
    return LONG2FIX(start);
}

/*
 *  call-seq:
 *     rxp === str   => true or false
 *
 *  Case Equality---Synonym for <code>Regexp#=~</code> used in case statements.
 *
 *     a = "HELLO"
 *     case a
 *     when /^[a-z]*$/; print "Lower case\n"
 *     when /^[A-Z]*$/; print "Upper case\n"
 *     else;            print "Mixed case\n"
 *     end
 *
 *  <em>produces:</em>
 *
 *     Upper case
 */

VALUE
regexp_eqq(VALUE rcv, SEL sel, VALUE str)
{
    str = reg_operand(str, Qfalse);
    if (NIL_P(str)) {
	rb_backref_set(Qnil);
	return Qfalse;
    }
    const long start = rb_reg_search(rcv, str, 0, 0);
    if (start < 0) {
	return Qfalse;
    }
    return Qtrue;
}

/*
 *  call-seq:
 *      rxp.source   => str
 *
 *  Returns the original string of the pattern.
 *
 *      /ab+c/ix.source #=> "ab+c"
 *
 *  Note that escape sequences are retained as is.
 *
 *     /\x20\+/.source  #=> "\\x20\\+"
 *
 */

static VALUE
regexp_source(VALUE rcv, SEL sel)
{
    assert(RREGEXP(rcv)->unistr != NULL);

    const UChar *chars = RREGEXP(rcv)->unistr->getBuffer();
    const int32_t chars_len = RREGEXP(rcv)->unistr->length();
    assert(chars_len >= 0);

    VALUE str = rb_unicode_str_new(chars, chars_len);

    if (OBJ_TAINTED(rcv)) {
	OBJ_TAINT(str);
    }
    return str;
}

/*
 * call-seq:
 *    rxp.inspect   => string
 *
 * Produce a nicely formatted string-version of _rxp_. Perhaps surprisingly,
 * <code>#inspect</code> actually produces the more natural version of
 * the string than <code>#to_s</code>.
 *
 *      /ab+c/ix.inspect        #=> "/ab+c/ix"
 *
 */

static VALUE
regexp_inspect(VALUE rcv, SEL sel)
{
    return regexp_source(rcv, 0);
}

/*
 *  call-seq:
 *     rxp.casefold?   => true or false
 *
 *  Returns the value of the case-insensitive flag.
 *
 *      /a/.casefold?           #=> false
 *      /a/i.casefold?          #=> true
 *      /(?i:a)/.casefold?      #=> false
 */

int
rb_reg_options(VALUE re)
{
    assert(RREGEXP(re)->pattern != NULL);
    return RREGEXP(re)->pattern->flags();
}

static VALUE
regexp_casefold(VALUE rcv, SEL sel)
{
    return rb_reg_options(rcv) & REGEXP_OPT_IGNORECASE ? Qtrue : Qfalse;
}

/*
 *  call-seq:
 *     rxp.options   => fixnum
 *
 *  Returns the set of bits corresponding to the options used when creating this
 *  Regexp (see <code>Regexp::new</code> for details. Note that additional bits
 *  may be set in the returned options: these are used internally by the regular
 *  expression code. These extra bits are ignored if the options are passed to
 *  <code>Regexp::new</code>.
 *
 *     Regexp::IGNORECASE                  #=> 1
 *     Regexp::EXTENDED                    #=> 2
 *     Regexp::MULTILINE                   #=> 4
 *
 *     /cat/.options                       #=> 0
 *     /cat/ix.options                     #=> 3
 *     Regexp.new('cat', true).options     #=> 1
 *     /\xa1\xa2/e.options                 #=> 16
 *
 *     r = /cat/ix
 *     Regexp.new(r.source, r.options)     #=> /cat/ix
 */

static VALUE
regexp_options(VALUE rcv, SEL sel)
{
    return INT2FIX(rb_reg_options(rcv));
}

/*
 *  Document-class: Regexp
 *
 *  A <code>Regexp</code> holds a regular expression, used to match a pattern
 *  against strings. Regexps are created using the <code>/.../</code> and
 *  <code>%r{...}</code> literals, and by the <code>Regexp::new</code>
 *  constructor.
 *
 */

static void Init_Match(void);

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
	    (void *)regexp_alloc, 0);
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
#endif

    regexp_finalize_imp_super = rb_objc_install_method2((Class)rb_cRegexp,
	    "finalize", (IMP)regexp_finalize_imp);

    rb_objc_define_method(rb_cRegexp, "initialize",
	    (void *)regexp_initialize, -1);
    rb_objc_define_method(rb_cRegexp, "initialize_copy",
	    (void *)regexp_initialize_copy, 1);
    //rb_objc_define_method(rb_cRegexp, "hash", rb_reg_hash, 0);
    rb_objc_define_method(rb_cRegexp, "eql?", (void *)regexp_equal, 1);
    rb_objc_define_method(rb_cRegexp, "==", (void *)regexp_equal, 1);
    rb_objc_define_method(rb_cRegexp, "=~", (void *)regexp_match, 1);
    rb_objc_define_method(rb_cRegexp, "match", (void *)regexp_match2, -1);
    rb_objc_define_method(rb_cRegexp, "~", (void *)regexp_match3, 0);
    rb_objc_define_method(rb_cRegexp, "===", (void *)regexp_eqq, 1);
    //rb_objc_define_method(rb_cRegexp, "to_s", rb_reg_to_s, 0);
    rb_objc_define_method(rb_cRegexp, "source", (void *)regexp_source, 0);
    rb_objc_define_method(rb_cRegexp, "casefold?", (void *)regexp_casefold, 0);
    rb_objc_define_method(rb_cRegexp, "options", (void *)regexp_options, 0);
#if 0
    rb_objc_define_method(rb_cRegexp, "encoding", rb_reg_encoding, 0);
    rb_objc_define_method(rb_cRegexp, "fixed_encoding?",
	    rb_reg_fixed_encoding_p, 0);
    rb_objc_define_method(rb_cRegexp, "names", rb_reg_names, 0);
    rb_objc_define_method(rb_cRegexp, "named_captures",
	    rb_reg_named_captures, 0);
#endif
    rb_objc_define_method(rb_cRegexp, "inspect", (void *)regexp_inspect, 0);

    rb_define_const(rb_cRegexp, "IGNORECASE", INT2FIX(REGEXP_OPT_IGNORECASE));
    rb_define_const(rb_cRegexp, "EXTENDED", INT2FIX(REGEXP_OPT_EXTENDED));
    rb_define_const(rb_cRegexp, "MULTILINE", INT2FIX(REGEXP_OPT_MULTILINE));

    Init_Match();
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

VALUE
rb_reg_last_match(VALUE match)
{
    return rb_reg_nth_match(0, match);
}

/*
 * call-seq:
 *    mtch.inspect   => str
 *
 * Returns a printable version of <i>mtch</i>.
 *
 *     puts /.$/.match("foo").inspect
 *     #=> #<MatchData "o">
 *
 *     puts /(.)(.)(.)/.match("foo").inspect
 *     #=> #<MatchData "foo" 1:"f" 2:"o" 3:"o">
 *
 *     puts /(.)(.)?(.)/.match("fo").inspect
 *     #=> #<MatchData "fo" 1:"f" 2:nil 3:"o">
 *
 *     puts /(?<foo>.)(?<bar>.)(?<baz>.)/.match("hoge").inspect
 *     #=> #<MatchData "hog" foo:"h" bar:"o" baz:"g">
 *
 */

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

/*
 *  call-seq:
 *     mtch.string   => str
 *
 *  Returns a frozen copy of the string passed in to <code>match</code>.
 *
 *     m = /(.)(.)(\d+)(\d)/.match("THX1138.")
 *     m.string   #=> "THX1138."
 */

static VALUE
match_string(VALUE rcv, SEL sel)
{
    UnicodeString *unistr = RMATCH(rcv)->unistr;
    assert(unistr != NULL);
    VALUE str = rb_unicode_str_new(unistr->getBuffer(), unistr->length());
    OBJ_FREEZE(str);
    return str;
}

/*
 *  call-seq:
 *     mtch.to_s   => str
 *
 *  Returns the entire matched string.
 *
 *     m = /(.)(.)(\d+)(\d)/.match("THX1138.")
 *     m.to_s   #=> "HX1138"
 */

static VALUE
match_to_s(VALUE rcv, SEL sel)
{
    VALUE str = rb_reg_last_match(rcv);

    if (NIL_P(str)) {
	str = rb_str_new(0, 0);
    }
    if (OBJ_TAINTED(rcv)) {
	OBJ_TAINT(str);
    }
    return str;
}

/*
 *  Document-class: MatchData
 *
 *  <code>MatchData</code> is the type of the special variable <code>$~</code>,
 *  and is the type of the object returned by <code>Regexp#match</code> and
 *  <code>Regexp.last_match</code>. It encapsulates all the results of a pattern
 *  match, results normally accessed through the special variables
 *  <code>$&</code>, <code>$'</code>, <code>$`</code>, <code>$1</code>,
 *  <code>$2</code>, and so on.
 *
 */

static void
Init_Match(void)
{
    rb_cMatch = rb_define_class("MatchData", rb_cObject);
    rb_objc_define_method(*(VALUE *)rb_cMatch, "alloc", (void *)match_alloc, 0);
    rb_undef_method(CLASS_OF(rb_cMatch), "new");

    match_finalize_imp_super = rb_objc_install_method2((Class)rb_cMatch,
	    "finalize", (IMP)match_finalize_imp);

#if 0
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
#endif
    rb_objc_define_method(rb_cMatch, "to_s", (void *)match_to_s, 0);
    rb_objc_define_method(rb_cMatch, "string", (void *)match_string, 0);
    rb_objc_define_method(rb_cMatch, "inspect", (void *)match_inspect, 0);
}

VALUE
rb_reg_check_preprocess(VALUE str)
{
    return Qnil;
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

} // extern "C"
