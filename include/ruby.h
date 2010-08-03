/**********************************************************************

  ruby/mvm.h -

  $Author$
  created at: Sun 10 12:06:15 Jun JST 2007

  Copyright (C) 2007-2008 Yukihiro Matsumoto

**********************************************************************/

#ifndef RUBY_H
#define RUBY_H 1

#define HAVE_RUBY_DEFINES_H     1
#define HAVE_RUBY_ENCODING_H    1
#define HAVE_RUBY_INTERN_H      1
#define HAVE_RUBY_IO_H          1
#define HAVE_RUBY_MISSING_H     1
#define HAVE_RUBY_RE_H          1
#define HAVE_RUBY_RUBY_H        1
#define HAVE_RUBY_ST_H          1
#define HAVE_RUBY_UTIL_H        1
#define HAVE_RUBY_VERSION_H     1
#define HAVE_RUBY_VM_H          1

#include <ruby/ruby.h>

extern void ruby_set_debug_option(const char *);
#endif /* RUBY_H */
