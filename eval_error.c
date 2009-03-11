/* -*-c-*- */
/*
 * included by eval.c
 */

#if 0
static void
warn_printf(const char *fmt, ...)
{
    char buf[BUFSIZ];
    va_list args;

    va_init_list(args, fmt);
    vsnprintf(buf, BUFSIZ, fmt, args);
    va_end(args);
    rb_write_error(buf);
}
#endif

#define warn_print(x) rb_write_error(x)
#define warn_print2(x,l) rb_write_error2(x,l)

VALUE rb_check_backtrace(VALUE);

static VALUE
get_backtrace(VALUE info)
{
    if (NIL_P(info)) {
	return Qnil;
    }
    info = rb_funcall(info, rb_intern("backtrace"), 0);
    if (NIL_P(info)) {
	return Qnil;
    }
    return rb_check_backtrace(info);
}

VALUE
rb_get_backtrace(VALUE info)
{
    return get_backtrace(info);
}

static void
set_backtrace(VALUE info, VALUE bt)
{
    rb_funcall(info, rb_intern("set_backtrace"), 1, bt);
}

static void
error_print(void)
{
#if 0
    VALUE errat = Qnil;		/* OK */
    VALUE errinfo = GET_THREAD()->errinfo;
    volatile VALUE eclass, e;
    const char *einfo;
    long elen;

    if (NIL_P(errinfo))
	return;

    PUSH_TAG();
    if (EXEC_TAG() == 0) {
	errat = get_backtrace(errinfo);
    }
    else {
	errat = Qnil;
    }
    if (EXEC_TAG())
	goto error;
    if (NIL_P(errat)) {
	const char *file = rb_sourcefile();
	int line = rb_sourceline();
	if (file)
	    warn_printf("%s:%d", file, line);
	else
	    warn_printf("%d", line);
    }
    else if (RARRAY_LEN(errat) == 0) {
	error_pos();
    }
    else {
	VALUE mesg = RARRAY_AT(errat, 0);

	if (NIL_P(mesg))
	    error_pos();
	else {
	    warn_print2(RSTRING_PTR(mesg), RSTRING_LEN(mesg));
	}
    }

    eclass = CLASS_OF(errinfo);
    if (EXEC_TAG() == 0) {
	e = rb_funcall(errinfo, rb_intern("message"), 0, 0);
	StringValue(e);
	einfo = RSTRING_PTR(e);
	elen = RSTRING_LEN(e);
    }
    else {
	einfo = "";
	elen = 0;
    }
    if (EXEC_TAG())
	goto error;
    if (eclass == rb_eRuntimeError && elen == 0) {
	warn_print(": unhandled exception\n");
    }
    else {
	VALUE epath;

	epath = rb_class_name(eclass);
	if (elen == 0) {
	    warn_print(": ");
	    warn_print2(RSTRING_PTR(epath), RSTRING_LEN(epath));
	    warn_print("\n");
	}
	else {
	    char *tail = 0;
	    long len = elen;

	    if (RSTRING_PTR(epath)[0] == '#')
		epath = 0;
	    if ((tail = memchr(einfo, '\n', elen)) != 0) {
		len = tail - einfo;
		tail++;		/* skip newline */
	    }
	    warn_print(": ");
	    warn_print2(einfo, len);
	    if (epath) {
		warn_print(" (");
		warn_print2(RSTRING_PTR(epath), RSTRING_LEN(epath));
		warn_print(")\n");
	    }
	    if (tail) {
		warn_print2(tail, elen - len - 1);
		if (einfo[elen-1] != '\n') warn_print2("\n", 1);
	    }
	}
    }

    if (!NIL_P(errat)) {
	long i;
	long len = RARRAY_LEN(errat);
        int skip = eclass == rb_eSysStackError;
	
#define TRACE_MAX (TRACE_HEAD+TRACE_TAIL+5)
#define TRACE_HEAD 8
#define TRACE_TAIL 5

	for (i = 1; i < len; i++) {
	    VALUE v = RARRAY_AT(errat, i);
	    if (TYPE(v) == T_STRING) {
		warn_printf("\tfrom %s\n", RSTRING_PTR(v));
	    }
	    if (skip && i == TRACE_HEAD && len > TRACE_MAX) {
		warn_printf("\t ... %ld levels...\n",
			    len - TRACE_HEAD - TRACE_TAIL);
		i = len - TRACE_TAIL;
	    }
	}
    }
  error:
    POP_TAG();
#endif
}

void
ruby_error_print(void)
{
    error_print();
}

void
rb_print_undef(VALUE klass, ID id, int scope)
{
    const char *v;

    switch (scope) {
      default:
      case NOEX_PUBLIC: v = ""; break;
      case NOEX_PRIVATE: v = " private"; break;
      case NOEX_PROTECTED: v = " protected"; break;
    }
    rb_name_error(id, "undefined%s method `%s' for %s `%s'", v,
		  rb_id2name(id),
		  (TYPE(klass) == T_MODULE) ? "module" : "class",
		  rb_class2name(klass));
}
