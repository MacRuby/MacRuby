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
#include <spawn.h>
#include "ruby/encoding.h"

typedef struct rb_io_t {
    int fd;
    int read_fd;
    int write_fd;

    CFStringRef path;
    pid_t pid;
    int lineno;
    int mode;

    CFMutableDataRef buf;
    unsigned long buf_offset;
} rb_io_t;

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

long rb_io_primitive_read(struct rb_io_t *io_struct, UInt8 *buffer, long len);

bool rb_io_wait_readable(int fd);
bool rb_io_wait_writable(int fd);

static inline void
rb_io_check_initialized(rb_io_t *fptr)
{
    if (fptr == NULL) {
	rb_raise(rb_eIOError, "uninitialized stream");
    }
}

static inline void
rb_io_check_closed(rb_io_t *io_struct)
{
    rb_io_check_initialized(io_struct);
    if (io_struct->fd == -1) {
	rb_raise(rb_eIOError, "closed stream");
    }
}

static inline bool
rb_io_read_pending(rb_io_t *io_struct)
{
    return io_struct->buf != NULL && CFDataGetLength(io_struct->buf) > 0;
}

static inline void 
rb_io_assert_writable(rb_io_t *io_struct)
{
    rb_io_check_initialized(io_struct);
    if (io_struct->write_fd == -1) {
	rb_raise(rb_eIOError, "not opened for writing");
    }
}

static inline void
rb_io_assert_readable(rb_io_t *io_struct)
{
    rb_io_check_initialized(io_struct);
    if (io_struct->read_fd == -1) {
	rb_raise(rb_eIOError, "not opened for reading");
    }
}

#if defined(__cplusplus)
#if 0
{ /* satisfy cc-mode */
#endif
}  /* extern "C" { */
#endif

#endif /* RUBY_IO_H */
