#include "ruby/ruby.h"

static VALUE
known_bug_rb_str_split2(VALUE recv, VALUE str, VALUE sep)
{
  VALUE parts = rb_str_split2(str, sep);
  // Accessing the array goes wrong, has it been collected?
  // Because at the end of rb_str_split_m the `result' _is_ the array as expected…
  return rb_ary_entry(parts, 0);
}

void Init_known_bugs_in_c() {
  VALUE cKnownBugsInC;
  
  cKnownBugsInC = rb_define_module("KnownBugsInC");
  rb_define_module_function(cKnownBugsInC, "test_rb_str_split2", known_bug_rb_str_split2, 2);
}