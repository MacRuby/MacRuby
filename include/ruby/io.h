/**********************************************************************

  rubyio.h -

  $Author: akr $
  created at: Fri Nov 12 16:47:09 JST 1993

  Copyright (C) 1993-2007 Yukihiro Matsumoto

**********************************************************************/

#ifndef RUBY_IO_H
#define RUBY_IO_H 1

#if defined(__cplusplus)
extern "C" {
#if 0
} /* satisfy cc-mode */
#endif
#endif

#include <stdio.h>
#include <errno.h>
#include "ruby/encoding.h"

typedef struct rb_io_t {
    CFReadStreamRef readStream;
    CFWriteStreamRef writeStream;
    CFStringRef path;
    pid_t pid;
    int lineno;
    int fd;
} rb_io_t;

#define HAVE_RB_IO_T 1

#define FMODE_READABLE  1
#define FMODE_WRITABLE  2
#define FMODE_READWRITE 3
#define FMODE_APPEND   64
#define FMODE_CREATE  128
#define FMODE_BINMODE   4
#define FMODE_SYNC      8
#define FMODE_TTY      16
#define FMODE_DUPLEX   32
#define FMODE_WSPLIT  0x200
#define FMODE_WSPLIT_INITIALIZED  0x400

VALUE rb_io_taint_check(VALUE);
NORETURN(void rb_eof_error(void));


#if defined(__cplusplus)
#if 0
{ /* satisfy cc-mode */
#endif
}  /* extern "C" { */
#endif

#endif /* RUBY_IO_H */
