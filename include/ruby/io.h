/*
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2007-2010, Apple Inc. All rights reserved
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 */

#ifndef RUBY_IO_H
#define RUBY_IO_H 1

#if defined(__cplusplus)
extern "C" {
#endif

#include <stdio.h>
#include <errno.h>
#include <spawn.h>
#include <fcntl.h>
#include "ruby/encoding.h"

typedef struct rb_io_t {
    int fd;
    int read_fd;
    int write_fd;

    VALUE path;
    pid_t pid;
    int lineno;
    int mode;

#if defined(__COREFOUNDATION__)
    CFMutableDataRef buf;
#else
    void *buf;
#endif
    unsigned long buf_offset;
} rb_io_t;

#define FMODE_READABLE  1
#define FMODE_WRITABLE  2
#define FMODE_READWRITE 3
#define FMODE_BINMODE   4
#define FMODE_SYNC      8
#define FMODE_TTY      16
#define FMODE_DUPLEX   32
#define FMODE_APPEND   64
#define FMODE_CREATE  128
#define FMODE_WSPLIT  0x200
#define FMODE_WSPLIT_INITIALIZED  0x400
#define FMODE_TRUNC                 0x00000800
#define FMODE_TEXTMODE              0x00001000
#define FMODE_SYNCWRITE (FMODE_SYNC|FMODE_WRITABLE)

VALUE rb_io_taint_check(VALUE);
NORETURN(void rb_eof_error(void));

long rb_io_primitive_read(struct rb_io_t *io_struct, char *buffer, long len);

int rb_io_modestr_fmode(const char *modestr);
int rb_io_modestr_oflags(const char *modestr);
void rb_io_synchronized(rb_io_t*);
int rb_io_wait_readable(int fd);
int rb_io_wait_writable(int fd);
int rb_io_read_pending(rb_io_t *io_struct);

void rb_io_set_nonblock(rb_io_t *fptr);

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

// For CRuby 1.9 compat.
#define HAVE_RB_IO_T 1
#define rb_io_check_writable rb_io_assert_writable
#define rb_io_check_readable rb_io_assert_readable
#define GetOpenFile(obj,fp) rb_io_check_closed(fp = ExtractIOStruct(rb_io_taint_check(obj)))

#if defined(__cplusplus)
}  // extern "C" {
#endif

#endif /* RUBY_IO_H */
