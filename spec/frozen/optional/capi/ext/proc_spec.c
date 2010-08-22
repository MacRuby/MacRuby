#include <string.h>

#include "ruby.h"
#include "rubyspec.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_RB_PROC_NEW
VALUE concat_func0(int argc, VALUE *argv) {
#define BUFLEN 500
  int i, len = 0;
  char buffer[BUFLEN] = {0};

  for (i = 0; i < argc; ++i) {
    VALUE v = argv[i];
    len += RSTRING_LEN(v) + 1;
    if (len > BUFLEN - 1) return Qnil;
    strcat(buffer, StringValuePtr(v));
    strcat(buffer, "_");
  }

  buffer[len - 1] = 0;
  return rb_str_new2(buffer);
}
#endif

#ifdef HAVE_RB_PROC_NEW
#ifdef RUBY_VERSION_IS_1_8
VALUE concat_func(VALUE args) {
  return concat_func0(RARRAY_LEN(args), RARRAY_PTR(args));
}
#endif
#endif

#ifdef HAVE_RB_PROC_NEW
#ifdef RUBY_VERSION_IS_1_9
VALUE concat_func(VALUE arg1, VALUE val, int argc, VALUE *argv, VALUE passed_proc) {
  return concat_func0(argc, argv);
}
#endif
#endif

#ifdef HAVE_RB_PROC_NEW
VALUE sp_underline_concat_proc(VALUE self) {
  return rb_proc_new(concat_func, Qnil);
}
#endif

void Init_proc_spec() {
  VALUE cls;
  cls = rb_define_class("CApiProcSpecs", rb_cObject);

#ifdef HAVE_RB_PROC_NEW
  rb_define_method(cls, "underline_concat_proc", sp_underline_concat_proc, 0);
#endif
}

#ifdef __cplusplus
}
#endif
