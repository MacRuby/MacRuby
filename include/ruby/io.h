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
    // The streams.
    CFReadStreamRef readStream;
    CFWriteStreamRef writeStream;
    
    // The Unixy low-level file handles.
    int fd; // You can expect this to be what the above CFStreams point to.
    FILE *fp; // NOTE: Only used by #popen. Don't depend on it!

    // Additional information.
    CFStringRef path;
    pid_t pid;
    int lineno;
    bool sync;

    // For ungetc.
    UInt8 *ungetc_buf;
    long ungetc_buf_len;
    long ungetc_buf_pos;
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
#define FMODE_TRUNC                 0x00000800
#define FMODE_TEXTMODE              0x00001000
#define FMODE_SYNCWRITE (FMODE_SYNC|FMODE_WRITABLE)

#ifndef SEEK_CUR
# define SEEK_SET 0
# define SEEK_CUR 1
# define SEEK_END 2
#endif

#ifndef O_ACCMODE
#define O_ACCMODE (O_RDONLY | O_WRONLY | O_RDWR)
#endif

VALUE rb_io_taint_check(VALUE);
NORETURN(void rb_eof_error(void));


#if defined(__cplusplus)
#if 0
{ /* satisfy cc-mode */
#endif
}  /* extern "C" { */
#endif

#endif /* RUBY_IO_H */
