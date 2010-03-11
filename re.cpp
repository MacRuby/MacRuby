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
#include "re.h"

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

#define REGEXP_OPT_DEFAULT	(UREGEX_MULTILINE)
#define REGEXP_OPT_IGNORECASE 	(UREGEX_CASE_INSENSITIVE)
#define REGEXP_OPT_EXTENDED 	(UREGEX_COMMENTS)
#define REGEXP_OPT_MULTILINE	(UREGEX_DOTALL)

typedef struct rb_match {
    struct RBasic basic;
    rb_regexp_t *regexp;
    VALUE str;
    rb_match_result_t *results;
    int results_count;
} rb_match_t;

#define RMATCH(o) ((rb_match_t *)o)
#define MATCH_BUSY FL_USER2

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
    match->str = 0;
    match->results = NULL;
    match->results_count = 0;
    return match;
}

static void
regexp_finalize(rb_regexp_t *regexp)
{
    if (regexp->pattern != NULL) {
	delete regexp->pattern;
	regexp->pattern = NULL;
    }
    if (regexp->unistr != NULL) {
	delete regexp->unistr;
	regexp->unistr = NULL;
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

static UnicodeString *
str_to_unistr(VALUE str)
{
    UChar *chars = NULL;
    long chars_len = 0;
    bool need_free = false;

    rb_str_get_uchars(str, &chars, &chars_len, &need_free);

    UnicodeString *unistr = new UnicodeString(false, (const UChar *)chars, chars_len);

    if (need_free) {
	free(chars);
    }
    return unistr;
}

static void
sanitize_regexp_string(UnicodeString *unistr)
{
    // ICU does not support [[:word::], so we need to replace all
    // occurences by \w.
    UChar word_chars[10] = {'[', '[', ':', 'w', 'o', 'r', 'd', ':', ']', ']'};
    UnicodeString word_str(word_chars, 10);
    UChar repl_chars[2] = {'\\', 'w'};
    UnicodeString repl_str(repl_chars, 2);
    int32_t pos;
    while ((pos = unistr->indexOf(word_str)) >= 0) {
	unistr->replace(pos, 10, repl_str);
    }
}

static bool
init_from_string(rb_regexp_t *regexp, VALUE str, int option, VALUE *excp)
{
    option |= REGEXP_OPT_DEFAULT;

    UnicodeString *unistr = str_to_unistr(str);
    assert(unistr != NULL);

    sanitize_regexp_string(unistr);

    UErrorCode status = U_ZERO_ERROR;
    RegexPattern *pattern = RegexPattern::compile(*unistr, option, status);

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

	// Stupid MRI encoding flags, let's ignore them for now.
	case 'n':
	case 'e':
	case 'u':
	case 's':
	    *option = 0;
	    return true;
    }
    *option = -1;
    return false;
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

static VALUE
rb_check_regexp_type(VALUE re)
{
    return rb_check_convert_type(re, T_REGEXP, "Regexp", "to_regexp");
}

/*
 *  call-seq:
 *     Regexp.escape(str)   => string
 *     Regexp.quote(str)    => string
 *
 *  Escapes any characters that would have special meaning in a regular
 *  expression. Returns a new escaped string, or self if no characters are
 *  escaped.  For any string,
 *  <code>Regexp.new(Regexp.escape(<i>str</i>))=~<i>str</i></code> will be true.
 *
 *     Regexp.escape('\*?{}.')   #=> \\\*\?\{\}\.
 *
 */

static VALUE
regexp_quote(VALUE klass, SEL sel, VALUE pat)
{
    return rb_reg_quote(reg_operand(pat, true));
}

/*
 *  call-seq:
 *     Regexp.union(pat1, pat2, ...)            => new_regexp
 *     Regexp.union(pats_ary)                   => new_regexp
 *
 *  Return a <code>Regexp</code> object that is the union of the given
 *  <em>pattern</em>s, i.e., will match any of its parts. The <em>pattern</em>s
 *  can be Regexp objects, in which case their options will be preserved, or
 *  Strings. If no patterns are given, returns <code>/(?!)/</code>.
 *
 *     Regexp.union                         #=> /(?!)/
 *     Regexp.union("penzance")             #=> /penzance/
 *     Regexp.union("a+b*c")                #=> /a\+b\*c/
 *     Regexp.union("skiing", "sledding")   #=> /skiing|sledding/
 *     Regexp.union(["skiing", "sledding"]) #=> /skiing|sledding/
 *     Regexp.union(/dogs/, /cats/i)        #=> /(?-mix:dogs)|(?i-mx:cats)/
 */

static VALUE regexp_to_s(VALUE rcv, SEL sel);

static VALUE
regexp_union(VALUE klass, SEL sel, int argc, VALUE *argv)
{
    const VALUE *args;

    if (argc == 0) {
	return rb_reg_new_str(rb_str_new2("(?!)"), 0);
    }
    else if (argc == 1) {
	VALUE v = rb_check_regexp_type(argv[0]);
	if (!NIL_P(v)) {
	    return v;
	}
	v = rb_check_array_type(argv[0]);
	if (!NIL_P(v)) {
	    argc = RARRAY_LEN(argv[0]);
	    args = RARRAY_PTR(argv[0]);
	}
	else {
	    StringValue(argv[0]);
	    return rb_reg_new_str(rb_reg_quote(argv[0]), 0);
	}
    }
    else {
	args = argv;
    }

    VALUE source = rb_unicode_str_new(NULL, 0);

    for (int i = 0; i < argc; i++) {
	VALUE arg = args[i];

	if (i > 0) {
	    rb_str_cat2(source, "|");
	}

	VALUE substr;
	VALUE re = rb_check_regexp_type(arg);
	if (!NIL_P(re)) {
	    substr = regexp_to_s(re, 0);
	}
	else {
	    StringValue(arg);
	    substr = rb_reg_quote(arg);
	}

	rb_str_append(source, substr);
    }

    return rb_reg_new_str(source, 0);
}

/*
 *  call-seq:
 *     Regexp.last_match           => matchdata
 *     Regexp.last_match(n)        => str
 *
 *  The first form returns the <code>MatchData</code> object generated by the
 *  last successful pattern match. Equivalent to reading the global variable
 *  <code>$~</code>. The second form returns the <i>n</i>th field in this
 *  <code>MatchData</code> object.
 *  <em>n</em> can be a string or symbol to reference a named capture.
 *
 *     /c(.)t/ =~ 'cat'        #=> 0
 *     Regexp.last_match       #=> #<MatchData "cat" 1:"a">
 *     Regexp.last_match(0)    #=> "cat"
 *     Regexp.last_match(1)    #=> "a"
 *     Regexp.last_match(2)    #=> nil
 *
 *     /(?<lhs>\w+)\s*=\s*(?<rhs>\w+)/ =~ "var = val"
 *     Regexp.last_match       #=> #<MatchData "var = val" lhs:"var" rhs:"val">
 *     Regexp.last_match(:lhs) #=> "var"
 *     Regexp.last_match(:rhs) #=> "val"
 */

static VALUE match_getter(void);
static int match_backref_number(VALUE match, VALUE backref, bool check);

static VALUE
regexp_last_match(VALUE klass, SEL sel, int argc, VALUE *argv)
{
    VALUE nth;

    if (argc > 0 && rb_scan_args(argc, argv, "01", &nth) == 1) {
	VALUE match = rb_backref_get();
	if (NIL_P(match)) {
	    return Qnil;
	}
	const int n = match_backref_number(match, nth, true);
	return rb_reg_nth_match(n, match);
    }
    return match_getter();
}

/*
 *  call-seq:
 *     Regexp.try_convert(obj) -> re or nil
 *
 *  Try to convert <i>obj</i> into a Regexp, using to_regexp method.
 *  Returns converted regexp or nil if <i>obj</i> cannot be converted
 *  for any reason.
 *
 *     Regexp.try_convert(/re/)         #=> /re/
 *     Regexp.try_convert("re")         #=> nil
 *
 *     o = Object.new
 *     Regexp.try_convert(o)            #=> nil
 *     def o.to_regexp() /foo/ end
 *     Regexp.try_convert(o)            #=> /foo/
 *
 */

static VALUE
regexp_try_convert(VALUE klass, SEL sel, VALUE obj)
{
    return rb_check_regexp_type(obj);
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

int
rb_reg_search(VALUE re, VALUE str, int pos, bool reverse)
{
    if (reverse) {
	rb_raise(rb_eRuntimeError, "reverse searching is not implemented yet");
    }

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
	delete unistr;
	rb_raise(rb_eRegexpError, "can't create matcher: %s",
		u_errorName(status));
    }

    if (!matcher->find(pos, status)) {
	// No match.
	rb_backref_set(Qnil);
	delete matcher;
	delete unistr;
	return -1;
    }

    // Match found.
    const int res_count = 1 + matcher->groupCount();
    rb_match_result_t *res = NULL;

    VALUE match = rb_backref_get();
    if (NIL_P(match) || FL_TEST(match, MATCH_BUSY)) {
	// Creating a new Match object.
	match = (VALUE)match_alloc(rb_cMatch, 0);
	res = (rb_match_result_t *)xmalloc(sizeof(rb_match_result_t)
		* res_count);
	GC_WB(&RMATCH(match)->results, res);
	GC_WB(&RMATCH(match)->str, rb_str_new(NULL, 0));
	rb_backref_set(match);
    }
    else {
	// Reusing the previous Match object.
	assert(RMATCH(match)->results != NULL);
	if (res_count > RMATCH(match)->results_count) {
	    res = (rb_match_result_t *)xrealloc(RMATCH(match)->results,
		    sizeof(rb_match_result_t) * res_count);
	    if (res != RMATCH(match)->results) {
		GC_WB(&RMATCH(match)->results, res);
	    }
	}
	else {
	    res = RMATCH(match)->results;
	    memset(res, 0, sizeof(rb_match_result_t) * res_count);
	}
	assert(RMATCH(match)->str != 0);
    }

    RMATCH(match)->results_count = res_count;
    GC_WB(&RMATCH(match)->regexp, re);

    rb_str_set_len(RMATCH(match)->str, 0);
    rb_str_append_uchars(RMATCH(match)->str, unistr->getBuffer(),
	    unistr->length());

    res[0].beg = matcher->start(status);
    res[0].end = matcher->end(status);

    for (int i = 0; i < matcher->groupCount(); i++) {
	res[i + 1].beg = matcher->start(i + 1, status);
	res[i + 1].end = matcher->end(i + 1, status);
    }

    delete matcher;
    delete unistr;

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

VALUE
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

VALUE
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
    const long start = rb_reg_search(rcv, str, 0, false);
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
    VALUE str = rb_str_new2("/");
    rb_str_concat(str, regexp_source(rcv, 0));
    rb_str_cat2(str, "/");

    const uint32_t options = rb_reg_options(rcv);
    const bool mode_m = options & REGEXP_OPT_MULTILINE;
    const bool mode_i = options & REGEXP_OPT_IGNORECASE;
    const bool mode_x = options & REGEXP_OPT_EXTENDED;

    if (mode_m) {
	rb_str_cat2(str, "m");
    }
    if (mode_i) {
	rb_str_cat2(str, "i");
    }
    if (mode_x) {
	rb_str_cat2(str, "x");
    }
    
    return str;
}

/*
 *  call-seq:
 *     rxp.to_s   => str
 *
 *  Returns a string containing the regular expression and its options (using the
 *  <code>(?opts:source)</code> notation. This string can be fed back in to
 *  <code>Regexp::new</code> to a regular expression with the same semantics as
 *  the original. (However, <code>Regexp#==</code> may not return true when
 *  comparing the two, as the source of the regular expression itself may
 *  differ, as the example shows).  <code>Regexp#inspect</code> produces a
 *  generally more readable version of <i>rxp</i>.
 *
 *      r1 = /ab+c/ix           #=> /ab+c/ix
 *      s1 = r1.to_s            #=> "(?ix-m:ab+c)"
 *      r2 = Regexp.new(s1)     #=> /(?ix-m:ab+c)/
 *      r1 == r2                #=> false
 *      r1.source               #=> "ab+c"
 *      r2.source               #=> "(?ix-m:ab+c)"
 */

static VALUE
regexp_to_s(VALUE rcv, SEL sel)
{
    VALUE str = rb_str_new2("(?");

    const uint32_t options = rb_reg_options(rcv);
    const bool mode_m = options & REGEXP_OPT_MULTILINE;
    const bool mode_i = options & REGEXP_OPT_IGNORECASE;
    const bool mode_x = options & REGEXP_OPT_EXTENDED;

    if (mode_m) {
	rb_str_cat2(str, "m");
    }
    if (mode_i) {
	rb_str_cat2(str, "i");
    }
    if (mode_x) {
	rb_str_cat2(str, "x");
    }

    if (!mode_m || !mode_i || !mode_x) {
	rb_str_cat2(str, "-");
	if (!mode_m) {
	    rb_str_cat2(str, "m");
	}
	if (!mode_i) {
	    rb_str_cat2(str, "i");
	}
	if (!mode_x) {
	    rb_str_cat2(str, "x");
	}
    }

    rb_str_cat2(str, ":");
    rb_str_concat(str, regexp_source(rcv, 0));
    rb_str_cat2(str, ")");

    return str;
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

static VALUE
match_getter(void)
{
    VALUE match = rb_backref_get();
    if (NIL_P(match)) {
	return Qnil;
    }
    rb_match_busy(match);
    return match;
}

static void
match_setter(VALUE val)
{
    if (!NIL_P(val)) {
	Check_Type(val, T_MATCH);
    }
    rb_backref_set(val);
}

static VALUE
last_match_getter(void)
{
    return rb_reg_last_match(rb_backref_get());
}

static VALUE
prematch_getter(void)
{
    return rb_reg_match_pre(rb_backref_get());
}

static VALUE
postmatch_getter(void)
{
    return rb_reg_match_post(rb_backref_get());
}

static VALUE
last_paren_match_getter(void)
{
    return rb_reg_match_last(rb_backref_get());
}

static VALUE
kcode_getter(void)
{
    rb_warn("variable $KCODE is no longer effective");
    return Qnil;
}

static void
kcode_setter(VALUE val, ID id)
{
    rb_warn("variable $KCODE is no longer effective; ignored");
}

static VALUE
ignorecase_getter(void)
{
    rb_warn("variable $= is no longer effective");
    return Qfalse;
}

static void
ignorecase_setter(VALUE val, ID id)
{
    rb_warn("variable $= is no longer effective; ignored");
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

#define DEFINE_GVAR(name, getter, setter) \
    rb_define_virtual_variable(name, (VALUE (*)(...))getter, \
	    (void (*)(...))setter)

    DEFINE_GVAR("$~", match_getter, match_setter);
    DEFINE_GVAR("$&", last_match_getter, 0);
    DEFINE_GVAR("$`", prematch_getter, 0);
    DEFINE_GVAR("$'", postmatch_getter, 0);
    DEFINE_GVAR("$+", last_paren_match_getter, 0);
    DEFINE_GVAR("$=", ignorecase_getter, ignorecase_setter);
    DEFINE_GVAR("$KCODE", kcode_getter, kcode_setter);
    DEFINE_GVAR("$-K", kcode_getter, kcode_setter);

#undef DEFINE_GVAR

    rb_cRegexp = rb_define_class("Regexp", rb_cObject);
    rb_objc_define_method(*(VALUE *)rb_cRegexp, "alloc",
	    (void *)regexp_alloc, 0);
    rb_objc_define_method(*(VALUE *)rb_cRegexp, "compile",
	    (void *)rb_class_new_instance_imp, -1);
    rb_objc_define_method(*(VALUE *)rb_cRegexp, "quote",
	    (void *)regexp_quote, 1);
    rb_objc_define_method(*(VALUE *)rb_cRegexp, "escape",
	    (void *)regexp_quote, 1);
    rb_objc_define_method(*(VALUE *)rb_cRegexp, "union",
	    (void *)regexp_union, -1);
    rb_objc_define_method(*(VALUE *)rb_cRegexp, "last_match",
	    (void *)regexp_last_match, -1);
    rb_objc_define_method(*(VALUE *)rb_cRegexp, "try_convert",
	    (void *)regexp_try_convert, 1);

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
    rb_objc_define_method(rb_cRegexp, "to_s", (void *)regexp_to_s, 0);
    rb_objc_define_method(rb_cRegexp, "inspect", (void *)regexp_inspect, 0);

    regexp_finalize_imp_super = rb_objc_install_method2((Class)rb_cRegexp,
	    "finalize", (IMP)regexp_finalize_imp);

    rb_define_const(rb_cRegexp, "IGNORECASE", INT2FIX(REGEXP_OPT_IGNORECASE));
    rb_define_const(rb_cRegexp, "EXTENDED", INT2FIX(REGEXP_OPT_EXTENDED));
    rb_define_const(rb_cRegexp, "MULTILINE", INT2FIX(REGEXP_OPT_MULTILINE));

    Init_Match();
}

static VALUE
match_initialize_copy(VALUE rcv, SEL sel, VALUE other)
{
    if (TYPE(other) != T_MATCH) {
	rb_raise(rb_eTypeError, "wrong argument type");
    }

    GC_WB(&RMATCH(rcv)->str, RMATCH(other)->str);
    GC_WB(&RMATCH(rcv)->regexp, RMATCH(other)->regexp);

    const long len = sizeof(rb_match_result_t) * RMATCH(other)->results_count;
    rb_match_result_t *res = (rb_match_result_t *)xmalloc(len);
    memcpy(res, RMATCH(other)->results, len);
    GC_WB(&RMATCH(rcv)->results, res);

    return rcv;
}

/*
 * call-seq:
 *    mtch.regexp   => regexp
 *
 * Returns the regexp.
 *
 *     m = /a.*b/.match("abc")
 *     m.regexp #=> /a.*b/
 */

static VALUE
match_regexp(VALUE rcv, SEL sel)
{
    assert(RMATCH(rcv)->regexp != NULL);
    return (VALUE)RMATCH(rcv)->regexp;
}

/*
 * call-seq:
 *    mtch.names   => [name1, name2, ...]
 *
 * Returns a list of names of captures as an array of strings.
 * It is same as mtch.regexp.names.
 *
 *     /(?<foo>.)(?<bar>.)(?<baz>.)/.match("hoge").names
 *     #=> ["foo", "bar", "baz"]
 *
 *     m = /(?<x>.)(?<y>.)?/.match("a") #=> #<MatchData "a" x:"a" y:nil>
 *     m.names                          #=> ["x", "y"]
 */

static VALUE
match_names(VALUE rcv, SEL sel)
{
    // TODO
    return rb_ary_new();
}

/*
 *  call-seq:
 *     mtch.length   => integer
 *     mtch.size     => integer
 *
 *  Returns the number of elements in the match array.
 *
 *     m = /(.)(.)(\d+)(\d)/.match("THX1138.")
 *     m.length   #=> 5
 *     m.size     #=> 5
 */

static VALUE
match_size(VALUE rcv, SEL sel)
{
    return INT2FIX(RMATCH(rcv)->results_count);
}

/*
 *  call-seq:
 *     mtch.offset(n)   => array
 *
 *  Returns a two-element array containing the beginning and ending offsets of
 *  the <em>n</em>th match.
 *  <em>n</em> can be a string or symbol to reference a named capture.
 *
 *     m = /(.)(.)(\d+)(\d)/.match("THX1138.")
 *     m.offset(0)      #=> [1, 7]
 *     m.offset(4)      #=> [6, 7]
 *
 *     m = /(?<foo>.)(.)(?<bar>.)/.match("hoge")
 *     p m.offset(:foo) #=> [0, 1]
 *     p m.offset(:bar) #=> [2, 3]
 *
 */

static int
match_backref_number(VALUE match, VALUE backref, bool check)
{
    const char *name;

    switch (TYPE(backref)) {
	default:
	    {
		const int pos = NUM2INT(backref);
		if (check) {
		    if (pos < 0 || pos >= RMATCH(match)->results_count) {
			rb_raise(rb_eIndexError,
				"index %d out of matches", pos);
		    }
		}
		return pos;
	    }

	case T_SYMBOL:
	    name = rb_sym2name(backref);
	    break;

	case T_STRING:
	    name = StringValueCStr(backref);
	    break;
    }

    // TODO
    rb_raise(rb_eIndexError, "named captures are not yet supported");
}
 
static VALUE
match_offset(VALUE rcv, SEL sel, VALUE backref)
{
    const int pos = match_backref_number(rcv, backref, true);
    return rb_assoc_new(INT2FIX(RMATCH(rcv)->results[pos].beg),
	    INT2FIX(RMATCH(rcv)->results[pos].end));
}

/*
 *  call-seq:
 *     mtch.begin(n)   => integer
 *
 *  Returns the offset of the start of the <em>n</em>th element of the match
 *  array in the string.
 *  <em>n</em> can be a string or symbol to reference a named capture.
 *
 *     m = /(.)(.)(\d+)(\d)/.match("THX1138.")
 *     m.begin(0)       #=> 1
 *     m.begin(2)       #=> 2
 *
 *     m = /(?<foo>.)(.)(?<bar>.)/.match("hoge")
 *     p m.begin(:foo)  #=> 0
 *     p m.begin(:bar)  #=> 2
 */

static VALUE
match_begin(VALUE rcv, SEL sel, VALUE backref)
{
    const int pos = match_backref_number(rcv, backref, true);
    return INT2FIX(RMATCH(rcv)->results[pos].beg);
}

/*
 *  call-seq:
 *     mtch.end(n)   => integer
 *
 *  Returns the offset of the character immediately following the end of the
 *  <em>n</em>th element of the match array in the string.
 *  <em>n</em> can be a string or symbol to reference a named capture.
 *
 *     m = /(.)(.)(\d+)(\d)/.match("THX1138.")
 *     m.end(0)         #=> 7
 *     m.end(2)         #=> 3
 *
 *     m = /(?<foo>.)(.)(?<bar>.)/.match("hoge")
 *     p m.end(:foo)    #=> 1
 *     p m.end(:bar)    #=> 3
 */

static VALUE
match_end(VALUE rcv, SEL sel, VALUE backref)
{
    const int pos = match_backref_number(rcv, backref, true);
    return INT2FIX(RMATCH(rcv)->results[pos].end);
}

/*
 *  call-seq:
 *     mtch.to_a   => anArray
 *
 *  Returns the array of matches.
 *
 *     m = /(.)(.)(\d+)(\d)/.match("THX1138.")
 *     m.to_a   #=> ["HX1138", "H", "X", "113", "8"]
 *
 *  Because <code>to_a</code> is called when expanding
 *  <code>*</code><em>variable</em>, there's a useful assignment
 *  shortcut for extracting matched fields. This is slightly slower than
 *  accessing the fields directly (as an intermediate array is
 *  generated).
 *
 *     all,f1,f2,f3 = *(/(.)(.)(\d+)(\d)/.match("THX1138."))
 *     all   #=> "HX1138"
 *     f1    #=> "H"
 *     f2    #=> "X"
 *     f3    #=> "113"
 */

static VALUE
match_array(VALUE match, int start)
{
    const int len = RMATCH(match)->results_count;
    assert(start >= 0 && start < len);
    const bool tainted = OBJ_TAINTED(match);

    VALUE ary = rb_ary_new2(len);
    for (int i = start; i < len; i++) {
	VALUE str = rb_reg_nth_match(i, match);
	if (tainted) {
	    OBJ_TAINT(str);
	}
	rb_ary_push(ary, str);
    }
    return ary;
}

static VALUE
match_to_a(VALUE rcv, SEL sel)
{
    return match_array(rcv, 0);
}

/*
 *  call-seq:
 *     mtch.captures   => array
 *
 *  Returns the array of captures; equivalent to <code>mtch.to_a[1..-1]</code>.
 *
 *     f1,f2,f3,f4 = /(.)(.)(\d+)(\d)/.match("THX1138.").captures
 *     f1    #=> "H"
 *     f2    #=> "X"
 *     f3    #=> "113"
 *     f4    #=> "8"
 */

static VALUE
match_captures(VALUE rcv, SEL sel)
{
    return match_array(rcv, 1);
}

/*
 *  call-seq:
 *     mtch[i]               => str or nil
 *     mtch[start, length]   => array
 *     mtch[range]           => array
 *     mtch[name]            => str or nil
 *
 *  Match Reference---<code>MatchData</code> acts as an array, and may be
 *  accessed using the normal array indexing techniques.  <i>mtch</i>[0] is
 *  equivalent to the special variable <code>$&</code>, and returns the entire
 *  matched string.  <i>mtch</i>[1], <i>mtch</i>[2], and so on return the values
 *  of the matched backreferences (portions of the pattern between parentheses).
 *
 *     m = /(.)(.)(\d+)(\d)/.match("THX1138.")
 *     m          #=> #<MatchData "HX1138" 1:"H" 2:"X" 3:"113" 4:"8">
 *     m[0]       #=> "HX1138"
 *     m[1, 2]    #=> ["H", "X"]
 *     m[1..3]    #=> ["H", "X", "113"]
 *     m[-3, 2]   #=> ["X", "113"]
 *
 *     m = /(?<foo>a+)b/.match("ccaaab")
 *     m          #=> #<MatchData "aaab" foo:"aaa">
 *     m["foo"]   #=> "aaa"
 *     m[:foo]    #=> "aaa"
 */

static VALUE
match_aref(VALUE rcv, SEL sel, int argc, VALUE *argv)
{
    VALUE backref, rest;

    rb_scan_args(argc, argv, "11", &backref, &rest);

    if (NIL_P(rest)) {
	switch (TYPE(backref)) {
	    case T_STRING:
	    case T_SYMBOL:
	    case T_FIXNUM:
		const int pos = match_backref_number(rcv, backref, false);
		return rb_reg_nth_match(pos, rcv);
	}
    }
    return rb_ary_aref(match_to_a(rcv, 0), 0, argc, argv);
}

/*
 *  call-seq:
 *
 *     mtch.values_at([index]*)   => array
 *
 *  Uses each <i>index</i> to access the matching values, returning an array of
 *  the corresponding matches.
 *
 *     m = /(.)(.)(\d+)(\d)/.match("THX1138: The Movie")
 *     m.to_a               #=> ["HX1138", "H", "X", "113", "8"]
 *     m.values_at(0, 2, -2)   #=> ["HX1138", "X", "113"]
 */

static VALUE
match_entry(VALUE match, long n)
{
    return rb_reg_nth_match(n, match);
}

static VALUE
match_values_at(VALUE rcv, SEL sel, int argc, VALUE *argv)
{
    return rb_get_values_at(rcv, RMATCH(rcv)->results_count, argc, argv,
	    match_entry);
}

/*
 *  call-seq:
 *     mtch.pre_match   => str
 *
 *  Returns the portion of the original string before the current match.
 *  Equivalent to the special variable <code>$`</code>.
 *
 *     m = /(.)(.)(\d+)(\d)/.match("THX1138.")
 *     m.pre_match   #=> "T"
 */

static VALUE
match_pre(VALUE rcv, SEL sel)
{
    assert(RMATCH(rcv)->results_count > 0);

    VALUE str = rb_str_substr(RMATCH(rcv)->str, 0,
	    RMATCH(rcv)->results[0].beg);

    if (OBJ_TAINTED(rcv)) {
	OBJ_TAINT(str);
    }
    return str;
}

VALUE
rb_reg_match_pre(VALUE rcv)
{
    if (NIL_P(rcv)) {
	return Qnil;
    }
    return match_pre(rcv, 0);
}

/*
 *  call-seq:
 *     mtch.post_match   => str
 *
 *  Returns the portion of the original string after the current match.
 *  Equivalent to the special variable <code>$'</code>.
 *
 *     m = /(.)(.)(\d+)(\d)/.match("THX1138: The Movie")
 *     m.post_match   #=> ": The Movie"
 */

static VALUE
match_post(VALUE rcv, SEL sel)
{
    assert(RMATCH(rcv)->results_count > 0);

    const int pos = RMATCH(rcv)->results[0].end;
    VALUE str = rb_str_substr(RMATCH(rcv)->str, pos,
	    rb_str_chars_len(RMATCH(rcv)->str) - pos);

    if (OBJ_TAINTED(rcv)) {
	OBJ_TAINT(str);
    }
    return str;
}

VALUE
rb_reg_match_post(VALUE rcv)
{
    if (NIL_P(rcv)) {
	return Qnil;
    }
    return match_post(rcv, 0);
}

VALUE
rb_reg_match_last(VALUE rcv)
{
    if (NIL_P(rcv)) {
	return Qnil;
    }
    assert(RMATCH(rcv)->results_count > 0);
    return rb_reg_nth_match(RMATCH(rcv)->results_count - 1, rcv);
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

rb_match_result_t *
rb_reg_match_results(VALUE match, int *count)
{
    assert(match != Qnil);
    if (count != NULL) {
	*count = RMATCH(match)->results_count;
    }
    return RMATCH(match)->results;
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
    const int end = RMATCH(match)->results[nth].end;
    if (beg == -1 || end == -1) {
	return Qnil;
    }

    return rb_str_substr(RMATCH(match)->str, beg, end - beg);
}

VALUE
rb_reg_last_match(VALUE match)
{
    return rb_reg_nth_match(0, match);
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
    assert(RMATCH(rcv)->str != 0);
    VALUE str = rb_str_dup(RMATCH(rcv)->str);
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
    rb_undef_method(CLASS_OF(rb_cMatch), "new");

    rb_objc_define_method(*(VALUE *)rb_cMatch, "alloc", (void *)match_alloc, 0);
    rb_objc_define_method(rb_cMatch, "initialize_copy",
	    (void *)match_initialize_copy, 1);
    rb_objc_define_method(rb_cMatch, "regexp", (void *)match_regexp, 0);
    rb_objc_define_method(rb_cMatch, "names", (void *)match_names, 0);
    rb_objc_define_method(rb_cMatch, "size", (void *)match_size, 0);
    rb_objc_define_method(rb_cMatch, "length", (void *)match_size, 0);
    rb_objc_define_method(rb_cMatch, "offset", (void *)match_offset, 1);
    rb_objc_define_method(rb_cMatch, "begin", (void *)match_begin, 1);
    rb_objc_define_method(rb_cMatch, "end", (void *)match_end, 1);
    rb_objc_define_method(rb_cMatch, "to_a", (void *)match_to_a, 0);
    rb_objc_define_method(rb_cMatch, "captures", (void *)match_captures, 0);
    rb_objc_define_method(rb_cMatch, "[]", (void *)match_aref, -1);
    rb_objc_define_method(rb_cMatch, "values_at", (void *)match_values_at, -1);
    rb_objc_define_method(rb_cMatch, "pre_match", (void *)match_pre, 0);
    rb_objc_define_method(rb_cMatch, "post_match", (void *)match_post, 0);
    rb_objc_define_method(rb_cMatch, "to_s", (void *)match_to_s, 0);
    rb_objc_define_method(rb_cMatch, "string", (void *)match_string, 0);
    rb_objc_define_method(rb_cMatch, "inspect", (void *)match_inspect, 0);
}

// Compiler primitives.

void
regexp_get_uchars(VALUE re, const UChar **chars_p, long *chars_len_p)
{
    assert(chars_p != NULL && chars_len_p != NULL);

    UnicodeString *unistr = RREGEXP(re)->unistr;
    assert(unistr != NULL);

    *chars_p = unistr->getBuffer();
    *chars_len_p = unistr->length();
}

VALUE
rb_unicode_regex_new_retained(UChar *chars, int chars_len, int options)
{
    VALUE str = rb_unicode_str_new(chars, chars_len);
    VALUE re = rb_reg_new_str(str, options);
    GC_RETAIN(re);
    return re;
}

// MRI compatibility.

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
rb_reg_regcomp(VALUE str)
{
    // XXX MRI caches the regexp here, maybe we should do the same...
    return rb_reg_new_str(str, 0);
}

VALUE
rb_reg_new(const char *cstr, long len, int options)
{
    return rb_reg_new_str(rb_usascii_str_new(cstr, len), options);
}

VALUE
rb_reg_quote(VALUE pat)
{
    UChar *chars = NULL;
    long chars_len = 0;
    bool need_free = false;
    VALUE result;

    rb_str_get_uchars(pat, &chars, &chars_len, &need_free);

    long pos = 0;
    for (; pos < chars_len; pos++) {
	switch (chars[pos]) {
	    case '[': case ']': case '{': case '}':
	    case '(': case ')': case '|': case '-':
	    case '*': case '.': case '\\': case '?':
	    case '+': case '^': case '$': case ' ':
	    case '#': case '\t': case '\f': case '\v':
	    case '\n': case '\r':
		goto meta_found;
	} 
    }

    result = rb_unicode_str_new(chars, chars_len);
    goto bail;

meta_found:
    result = rb_unicode_str_new(NULL, (chars_len * 2) + 1);

    // Copy up to metacharacter.
    rb_str_append_uchars(result, &chars[0], pos);

    for (; pos < chars_len; pos++) {
	UChar c = chars[pos];
	switch (c) {
	    case '[': case ']': case '{': case '}':
	    case '(': case ')': case '|': case '-':
	    case '*': case '.': case '\\': case '?':
	    case '+': case '^': case '$': case '#':
		rb_str_append_uchar(result, '\\');
		break;

	    case ' ':
		rb_str_append_uchar(result, '\\');
		rb_str_append_uchar(result, ' ');
		continue;

	    case '\t':
		rb_str_append_uchar(result, '\\');
		rb_str_append_uchar(result, 't');
		continue;

	    case '\n':
		rb_str_append_uchar(result, '\\');
		rb_str_append_uchar(result, 'n');
		continue;

	    case '\r':
		rb_str_append_uchar(result, '\\');
		rb_str_append_uchar(result, 'r');
		continue;

	    case '\f':
		rb_str_append_uchar(result, '\\');
		rb_str_append_uchar(result, 'f');
		continue;

	    case '\v':
		rb_str_append_uchar(result, '\\');
		rb_str_append_uchar(result, 'v');
		continue;
	}
	rb_str_append_uchar(result, c);
    }

bail:
    if (need_free) {
	free(chars);
    }
    return result;
}

void
rb_match_busy(VALUE match)
{
    FL_SET(match, MATCH_BUSY);
}

} // extern "C"
