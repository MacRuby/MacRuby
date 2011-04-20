/* 
 * MacRuby implementation of Ruby 1.9's io.c.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 * 
 * Copyright (C) 2007-2011, Apple Inc. All rights reserved.
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 * Copyright (C) 2000 Network Applied Communication Laboratory, Inc.
 * Copyright (C) 2000 Information-technology Promotion Agency, Japan
 */

#include "macruby_internal.h"
#include "ruby/io.h"
#include "ruby/util.h"
#include "ruby/node.h"
#include "vm.h"
#include "objc.h"
#include "id.h"
#include "encoding.h"

#include <errno.h>
#include <paths.h>
#include <fcntl.h>
#include <unistd.h>
#include <copyfile.h>

#include <sys/select.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/syscall.h>
#include <spawn.h>

#define IS_FD_OF_STDIO(x) (x <= 2)
#define CLOSE_FD(fd)					\
    do {						\
	if (!IS_FD_OF_STDIO(fd)) {			\
	    close(fd);					\
	}						\
    }							\
    while (0)

char ***_NSGetEnviron();

extern void Init_File(void);

VALUE rb_cIO;
VALUE rb_eEOFError;
VALUE rb_eIOError;

VALUE rb_stdin = 0, rb_stdout = 0, rb_stderr = 0;
VALUE rb_deferr;		/* rescue VIM plugin */
static VALUE orig_stdout, orig_stderr;

// TODO: After Object#untrusted? and Object#trusted? get implemented,
// place the appropriate checks on #inspect, #reopen, #close, #close_read,
// and #close_write.

VALUE rb_output_fs;
VALUE rb_rs;
VALUE rb_output_rs;
VALUE rb_default_rs;

static VALUE argf;

static ID id_write, id_read, id_getc, id_flush, id_encode, id_readpartial;

struct argf {
    VALUE filename, current_file;
    int gets_lineno;
    int init_p, next_p;
    VALUE lineno;
    VALUE argv;
    char *inplace;
    int binmode;
    rb_encoding *enc, *enc2;
};

struct foreach_arg {
    int argc;
    VALUE *argv;
    VALUE io;
};

#define argf_of(obj) (*(struct argf *)DATA_PTR(obj))
#define ARGF argf_of(argf)

static VALUE
pop_last_hash(int *argc_p, VALUE *argv)
{
    VALUE last, tmp;
    if (*argc_p == 0) {
	return Qnil;
    }
    last = argv[*argc_p-1];
    tmp = rb_check_convert_type(last, T_HASH, "Hash", "to_hash");
    if (NIL_P(tmp)) {
	return Qnil;
    }
    (*argc_p)--;
    return tmp;
}

static VALUE
rb_io_get_io(VALUE io)
{
    return rb_convert_type(io, T_FILE, "IO", "to_io");
}

static VALUE
rb_io_check_io(VALUE io)
{
    return rb_check_convert_type(io, T_FILE, "IO", "to_io");
}

static int
convert_mode_string_to_fmode(VALUE rstr)
{
    int fmode = 0;
    const char *m = RSTRING_PTR(rstr);

    switch (*m++) {
	case 'r':
	    fmode |= FMODE_READABLE;
	    break;
	case 'w':
	    fmode |= FMODE_WRITABLE | FMODE_TRUNC | FMODE_CREATE;
	    break;
	case 'a':
	    fmode |= FMODE_WRITABLE | FMODE_APPEND | FMODE_CREATE;	
	    break;
	default:
error:
	    rb_raise(rb_eArgError, "invalid access mode %s", m);
    }

    while (*m) {
	switch (*m++) {
	    case 'b':
		fmode |= FMODE_BINMODE;
		break;
	    case 't':
		fmode |= FMODE_TEXTMODE;
		break;
	    case '+':
		fmode |= FMODE_READWRITE;
		break;
	    case ':':
		goto finished;
	    default:
		rb_raise(rb_eArgError, "invalid access mode %s", m);
	}
    }

finished:
    if ((fmode & FMODE_BINMODE) && (fmode & FMODE_TEXTMODE))
	goto error;

    return fmode;
}

static int
convert_fmode_to_oflags(int fmode)
{
    int oflags = 0;

    switch (fmode & FMODE_READWRITE) {
	case FMODE_READABLE:
	    oflags |= O_RDONLY;
	    break;
	case FMODE_WRITABLE:
	    oflags |= O_WRONLY;
	    break;
	case FMODE_READWRITE:
	    oflags |= O_RDWR;
	    break;
    }

    if (fmode & FMODE_APPEND) {
	oflags |= O_APPEND;
    }
    if (fmode & FMODE_TRUNC) {
	oflags |= O_TRUNC;
    }
    if (fmode & FMODE_CREATE) {
	oflags |= O_CREAT;
    }

    return oflags;
}

static int
convert_mode_string_to_oflags(VALUE s) 
{
    if (TYPE(s) == T_FIXNUM) {
	return NUM2INT(s);
    }
    StringValue(s);
    return convert_fmode_to_oflags(convert_mode_string_to_fmode(s));
}

static int
convert_oflags_to_fmode(int mode)
{
    int flags = 0;

    switch (mode & (O_RDONLY|O_WRONLY|O_RDWR)) {
	case O_RDONLY:
	    flags = FMODE_READABLE;
	    break;
	case O_WRONLY:
	    flags = FMODE_WRITABLE;
	    break;
	case O_RDWR:
	    flags = FMODE_READWRITE;
	    break;
    }

    if (mode & O_APPEND) {
	flags |= FMODE_APPEND;
    }
    if (mode & O_CREAT) {
	flags |= FMODE_CREATE;
    }

    return flags;
}

void
rb_eof_error(void)
{
    rb_raise(rb_eEOFError, "end of file reached");
}

VALUE
rb_io_taint_check(VALUE io)
{
    if (!OBJ_TAINTED(io) && rb_safe_level() >= 4) {
	rb_raise(rb_eSecurityError, "Insecure: operation on untainted IO");
    }
    rb_check_frozen(io);
    return io;
}

static bool
rb_io_is_open(rb_io_t *io_struct) 
{
    return io_struct->fd != -1;
}

static void
rb_io_assert_open(rb_io_t *io_struct)
{
    if (!rb_io_is_open(io_struct)) {
	rb_raise(rb_eIOError,
		"cannot perform that operation on a closed stream");
    }
}

static bool
rb_io_is_closed_for_reading(rb_io_t *io_struct) 
{
    return io_struct->read_fd == -1;
}

static bool
rb_io_is_closed_for_writing(rb_io_t *io_struct) 
{
    return io_struct->write_fd == -1;
}

static bool
rb_io_is_readable(rb_io_t *io_struct)
{
    return !rb_io_is_closed_for_reading(io_struct);
}

static bool
rb_io_is_writable(rb_io_t *io_struct)
{
    return !rb_io_is_closed_for_writing(io_struct);
}

static int
rb_io_calculate_mode_flags(rb_io_t *io_struct)
{
    int flags = 0;
    if (rb_io_is_readable(io_struct)) {
	flags |= FMODE_READABLE;
    }
    if (rb_io_is_writable(io_struct)) {
	flags |= FMODE_WRITABLE;
    }
    return flags;
}

static VALUE
io_alloc(VALUE klass, SEL sel)
{
    NEWOBJ(io, struct RFile);
    OBJSETUP(io, klass, T_FILE);
    GC_WB(&io->fptr, ALLOC(rb_io_t));
    io->fptr->fd = -1;
    io->fptr->read_fd = -1;
    io->fptr->write_fd = -1;
    io->fptr->pid = -1;
    return (VALUE)io;
}

static void 
prepare_io_from_fd(rb_io_t *io_struct, int fd, int mode)
{
    // While getting rid of the FMODE_* constants and replacing them with the 
    // POSIX constants would be very nice, it would be a mistake on Darwin,
    // as O_RDONLY|O_WRONLY != O_RDWR, whereas FMODE_READABLE|FMODE_WRITABLE = 
    // FMODE_READWRITE. As such, we have to redefine a whole lot of system-wide
    // constants, which sucks. But we don't have any other option.

    bool read = false, write = false;
    switch (mode & FMODE_READWRITE) {
	case FMODE_READABLE:
	    read = true;
	    break;

	case FMODE_WRITABLE:
	    write = true;
	    break;

	case FMODE_READWRITE:
	    read = write = true;
	    break;
    }
    assert(read || write);

    if (read) {
	io_struct->read_fd = fd;
    }

    if (write) {
	io_struct->write_fd = fd;
    }
 
    io_struct->fd = fd;
    io_struct->pid = -1;
    io_struct->mode = mode;
}

static void
io_close(rb_io_t *io_struct, bool close_read, bool close_write)
{
    // TODO we must check the return value of close(2) and appropriately call
    // rb_sys_fail().
    if (close_read) {
	if (io_struct->read_fd != io_struct->write_fd) {
	    CLOSE_FD(io_struct->read_fd);
	}
	else {
	    io_struct->fd = -1;
	}
	io_struct->read_fd = -1;
    }
    if (close_write) {
	if (io_struct->write_fd != io_struct->read_fd) {
	    CLOSE_FD(io_struct->write_fd);
	}
	else {
	    io_struct->fd = -1;
	}
	io_struct->write_fd = -1;
    }
    if (io_struct->pid != -1) {
	if (close_read && close_write) {
	    rb_last_status_set(0, io_struct->pid);
	    rb_syswait(io_struct->pid);
	    io_struct->pid = -1;
	}
    }
    if (io_struct->fd != -1 && io_struct->read_fd == -1
	    && io_struct->write_fd == -1) {
	CLOSE_FD(io_struct->fd);
	io_struct->fd = -1;
    }
}

static VALUE
prep_io(int fd, int mode, VALUE klass)
{
    VALUE io = io_alloc(klass, 0);
    rb_io_t *io_struct = ExtractIOStruct(io);
    prepare_io_from_fd(io_struct, fd, mode);
    return io;
}

/*
 *  call-seq:
 *     ios.syswrite(string)   => integer
 *
 *  Writes the given string to <em>ios</em> using a low-level write.
 *  Returns the number of bytes written. Do not mix with other methods
 *  that write to <em>ios</em> or you may get unpredictable results.
 *  Raises <code>SystemCallError</code> on error.
 *
 *     f = File.new("out", "w")
 *     f.syswrite("ABCDEF")   #=> 6
 */


static VALUE
rb_io_syswrite(VALUE io, SEL sel, VALUE data)
{
    rb_secure(4);
    rb_io_t *io_struct = ExtractIOStruct(io);
    rb_io_assert_writable(io_struct);

    data = rb_str_bstr(rb_obj_as_string(data));
    
    if (io_struct->buf && CFDataGetLength(io_struct->buf) > 0) {
	rb_warn("Calling #syswrite on buffered I/O may lead to unexpected results");
    }
    
    const uint8_t *buffer = rb_bstr_bytes(data);
    const long length = rb_bstr_length(data);
    
    if (length == 0) {
        return INT2FIX(0);
    }
    
    ssize_t result = write(io_struct->write_fd, buffer, length);
    if (result == -1) {
	rb_sys_fail("write(2) failed.");
    }
    
    return LONG2FIX(result);
}

/*
 *  call-seq:
 *     ios.write(string)    => integer
 *
 *  Writes the given string to <em>ios</em>. The stream must be opened
 *  for writing. If the argument is not a string, it will be converted
 *  to a string using <code>to_s</code>. Returns the number of bytes
 *  written.
 *
 *     count = $stdout.write( "This is a test\n" )
 *     puts "That was #{count} bytes of data"
 *
 *  <em>produces:</em>
 *
 *     This is a test
 *     That was 15 bytes of data
 */

static VALUE
io_write(VALUE io, SEL sel, VALUE data)
{
    rb_secure(4);

    VALUE tmp = rb_io_check_io(io);
    if (NIL_P(tmp)) {
	// receiver is not IO, dispatch the write method on it
	return rb_vm_call(io, selWrite, 1, &data);
    }
    io = tmp;

    data = rb_obj_as_string(data);
    data = rb_str_bstr(data);
    const uint8_t *buffer = rb_bstr_bytes(data);
    const long length = rb_bstr_length(data);

    if (length == 0) {
        return INT2FIX(0);
    }

    rb_io_t *io_struct = ExtractIOStruct(io);
    rb_io_assert_writable(io_struct);

    ssize_t code = write(io_struct->write_fd, buffer, length);
    if (code == -1) {
	rb_sys_fail("write() failed");
    }

    if (io_struct->buf != NULL && CFDataGetLength(io_struct->buf) > 0) {
	if (length > CFDataGetLength(io_struct->buf) - io_struct->buf_offset) {
	    CFDataIncreaseLength(io_struct->buf, length);
	}
	UInt8 *data = CFDataGetMutableBytePtr(io_struct->buf);
	memcpy(data + io_struct->buf_offset, buffer, length);
	io_struct->buf_offset += length;
    }

    return LONG2FIX(code);
}

/*
 *  call-seq:
 *     ios << obj     => ios
 *
 *  String Output---Writes <i>obj</i> to <em>ios</em>.
 *  <i>obj</i> will be converted to a string using
 *  <code>to_s</code>.
 *
 *     $stdout << "Hello " << "world!\n"
 *
 *  <em>produces:</em>
 *
 *     Hello world!
 */


VALUE
rb_io_addstr(VALUE io, SEL sel, VALUE str)
{
    rb_io_write(io, str);
    return io;
}

/*
 *  call-seq:
 *     ios.flush    => ios
 *
 *  Flushes any buffered data within <em>ios</em> to the underlying
 *  operating system (note that this is Ruby internal buffering only;
 *  the OS may buffer the data as well).
 *
 *     $stdout.print "no newline"
 *     $stdout.flush
 *
 *  <em>produces:</em>
 *
 *     no newline
 */

VALUE
rb_io_flush(VALUE io, SEL sel)
{
    rb_io_t *io_struct = ExtractIOStruct(io);
    rb_io_assert_open(io_struct);
    // IO#flush on MacRuby is a no-op, as MacRuby does not buffer its IO
    // streams internally
    return io;
}

/*
 *  call-seq:
 *     ios.pos     => integer
 *     ios.tell    => integer
 *
 *  Returns the current offset (in bytes) of <em>ios</em>.
 *
 *     f = File.new("testfile")
 *     f.pos    #=> 0
 *     f.gets   #=> "This is line one\n"
 *     f.pos    #=> 17
 */


static off_t
ltell(int fd)
{
    off_t code = lseek(fd, 0, SEEK_CUR);
    if (code == -1) {
	rb_sys_fail("lseek() failed");
    }
    return code;
}

static VALUE
rb_io_tell(VALUE io, SEL sel)
{
    rb_io_t *io_struct = ExtractIOStruct(io);
    rb_io_check_closed(io_struct);

    return OFFT2NUM(ltell(io_struct->fd));
}

static VALUE
rb_io_seek(VALUE io, VALUE offset, int whence)
{
    rb_io_t *io_struct = ExtractIOStruct(io);
    rb_io_check_closed(io_struct);
    off_t off = NUM2OFFT(offset);
//    if (whence == SEEK_CUR) {
//	off += ltell(io_struct->read_fd);
//    }
    const off_t code = lseek(io_struct->fd, off, whence);
    if (code == -1) {
	rb_sys_fail("lseek() failed");
    }
    if (io_struct->buf != NULL) {
	io_struct->buf_offset = code;
    }
    return INT2FIX(0);
}

/*
 *  call-seq:
 *     ios.sysseek(offset, whence=SEEK_SET)   => integer
 *
 *  Seeks to a given <i>offset</i> in the stream according to the value
 *  of <i>whence</i> (see <code>IO#seek</code> for values of
 *  <i>whence</i>). Returns the new offset into the file.
 *
 *     f = File.new("testfile")
 *     f.sysseek(-13, IO::SEEK_END)   #=> 53
 *     f.sysread(10)                  #=> "And so on."
 */


/*
 *  call-seq:
 *     ios.seek(amount, whence=SEEK_SET) -> 0
 *
 *  Seeks to a given offset <i>anInteger</i> in the stream according to
 *  the value of <i>whence</i>:
 *
 *    IO::SEEK_CUR  | Seeks to _amount_ plus current position
 *    --------------+----------------------------------------------------
 *    IO::SEEK_END  | Seeks to _amount_ plus end of stream (you probably
 *                  | want a negative value for _amount_)
 *    --------------+----------------------------------------------------
 *    IO::SEEK_SET  | Seeks to the absolute location given by _amount_
 *
 *  Example:
 *
 *     f = File.new("testfile")
 *     f.seek(-13, IO::SEEK_END)   #=> 0
 *     f.readline                  #=> "And so on...\n"
 */

static VALUE
rb_io_seek_m(VALUE io, SEL sel, int argc, VALUE *argv)
{
    VALUE offset, ptrname;
    int whence = SEEK_SET;
 
    if (rb_scan_args(argc, argv, "11", &offset, &ptrname) == 2) {
        whence = NUM2INT(ptrname);
    }
 
    return rb_io_seek(io, offset, whence);
}

/*
 *  call-seq:
 *     ios.pos = integer    => integer
 *
 *  Seeks to the given position (in bytes) in <em>ios</em>.
 *
 *     f = File.new("testfile")
 *     f.pos = 17
 *     f.gets   #=> "This is line two\n"
 */

static VALUE
rb_io_set_pos(VALUE io, SEL sel, VALUE offset)
{
    return rb_io_seek(io, offset, SEEK_SET);
}

/*
 *  call-seq:
 *     ios.rewind    => 0
 *
 *  Positions <em>ios</em> to the beginning of input, resetting
 *  <code>lineno</code> to zero.
 *
 *     f = File.new("testfile")
 *     f.readline   #=> "This is line one\n"
 *     f.rewind     #=> 0
 *     f.lineno     #=> 0
 *     f.readline   #=> "This is line one\n"
 */

static VALUE
rb_io_rewind(VALUE io, SEL sel)
{
    ExtractIOStruct(io)->lineno = 0;
    return rb_io_seek(io, INT2FIX(0), SEEK_SET);
}

/*
 *  call-seq:
 *     ios.eof     => true or false
 *     ios.eof?    => true or false
 *
 *  Returns true if <em>ios</em> is at end of file that means
 *  there are no more data to read.
 *  The stream must be opened for reading or an <code>IOError</code> will be
 *  raised.
 *
 *     f = File.new("testfile")
 *     dummy = f.readlines
 *     f.eof   #=> true
 *
 *  If <em>ios</em> is a stream such as pipe or socket, <code>IO#eof?</code>
 *  blocks until the other end sends some data or closes it.
 *
 *     r, w = IO.pipe
 *     Thread.new { sleep 1; w.close }
 *     r.eof?  #=> true after 1 second blocking
 *
 *     r, w = IO.pipe
 *     Thread.new { sleep 1; w.puts "a" }
 *     r.eof?  #=> false after 1 second blocking
 *
 *     r, w = IO.pipe
 *     r.eof?  # blocks forever
 *
 *  Note that <code>IO#eof?</code> reads data to a input buffer.
 *  So <code>IO#sysread</code> doesn't work with <code>IO#eof?</code>.
 */

static long rb_io_read_internal(rb_io_t *io_struct, UInt8 *buffer, long len);

VALUE
rb_io_eof(VALUE io, SEL sel)
{
    rb_io_t *io_struct = ExtractIOStruct(io);
    rb_io_assert_readable(io_struct);

    if (rb_io_read_pending(io_struct)) {
	return Qfalse;
    }

    UInt8 c;
    const off_t pos = lseek(io_struct->fd, 0, SEEK_CUR);
    if (rb_io_read_internal(io_struct, &c, 1) != 1) {
	return Qtrue;
    }

    lseek(io_struct->fd, pos, SEEK_SET);
    if (io_struct->buf != NULL) {
	io_struct->buf_offset--;
    }
    return Qfalse;
}

/*
 *  call-seq:
 *     ios.sync    => true or false
 *
 *  Returns the current ``sync mode'' of <em>ios</em>. When sync mode is
 *  true, all output is immediately flushed to the underlying operating
 *  system and is not buffered by Ruby internally. See also
 *  <code>IO#fsync</code>.
 *
 *     f = File.new("testfile")
 *     f.sync   #=> false
 */

static VALUE
rb_io_sync(VALUE io, SEL sel)
{
    rb_io_t *io_struct = ExtractIOStruct(io);
    rb_io_assert_open(io_struct);
    return (io_struct->mode & FMODE_SYNC) ? Qtrue : Qfalse;
}

/*
 *  call-seq:
 *     ios.sync = boolean   => boolean
 *
 *  Sets the ``sync mode'' to <code>true</code> or <code>false</code>.
 *  When sync mode is true, all output is immediately flushed to the
 *  underlying operating system and is not buffered internally. Returns
 *  the new state. See also <code>IO#fsync</code>.
 *
 *     f = File.new("testfile")
 *     f.sync = true
 *
 *  <em>(produces no output)</em>
 */

static VALUE
rb_io_set_sync(VALUE io, SEL sel, VALUE mode)
{
    rb_io_t *io_struct = ExtractIOStruct(io);
    rb_io_assert_open(io_struct);
    if (RTEST(mode)) {
	io_struct->mode |= FMODE_SYNC;
    }
    else {
	io_struct->mode &= ~FMODE_SYNC;
    }
    return mode;
}

/*
 *  call-seq:
 *     ios.fsync   => 0 or nil
 *
 *  Immediately writes all buffered data in <em>ios</em> to disk.
 *  Returns <code>nil</code> if the underlying operating system does not
 *  support <em>fsync(2)</em>. Note that <code>fsync</code> differs from
 *  using <code>IO#sync=</code>. The latter ensures that data is flushed
 *  from Ruby's buffers, but doesn't not guarantee that the underlying
 *  operating system actually writes it to disk.
 */

static VALUE
rb_io_fsync(VALUE io, SEL sel)
{
    rb_io_t *io_struct = ExtractIOStruct(io);
    rb_io_assert_writable(io_struct);
    if (fsync(io_struct->fd) < 0) {
	rb_sys_fail("fsync() failed.");
    }
    return INT2FIX(0);
}

/*
 *  call-seq:
 *     ios.fileno    => fixnum
 *     ios.to_i      => fixnum
 *
 *  Returns an integer representing the numeric file descriptor for
 *  <em>ios</em>.
 *
 *     $stdin.fileno    #=> 0
 *     $stdout.fileno   #=> 1
 */

static VALUE
rb_io_fileno(VALUE io, SEL sel)
{
    rb_io_t *io_struct = ExtractIOStruct(io);
    rb_io_assert_open(io_struct);
    return INT2FIX(io_struct->fd);
}


/*
 *  call-seq:
 *     ios.pid    => fixnum
 *
 *  Returns the process ID of a child process associated with
 *  <em>ios</em>. This will be set by <code>IO::popen</code>.
 *
 *     pipe = IO.popen("-")
 *     if pipe
 *       $stderr.puts "In parent, child pid is #{pipe.pid}"
 *     else
 *       $stderr.puts "In child, pid is #{$$}"
 *     end
 *
 *  <em>produces:</em>
 *
 *     In child, pid is 26209
 *     In parent, child pid is 26209
 */

static VALUE
rb_io_pid(VALUE io, SEL sel)
{
    rb_io_t *io_struct = ExtractIOStruct(io);
    rb_io_assert_open(io_struct);
    return io_struct->pid == -1 ? Qnil : INT2FIX(io_struct->pid);
}


/*
 * call-seq:
 *   ios.inspect   => string
 *
 * Return a string describing this IO object.
 */

static VALUE
rb_io_inspect(VALUE io, SEL sel)
{
    rb_io_t *io_struct = ExtractIOStruct(io);
    if (io_struct == NULL || io_struct->path == 0) {
        return rb_any_to_s(io);
    }

    VALUE str = rb_str_new2("#<");
    rb_str_cat2(str, rb_obj_classname(io));
    rb_str_cat2(str, ":");
    rb_str_concat(str, io_struct->path);
    if (!rb_io_is_open(io_struct)) {
	rb_str_cat2(str, " (closed)>");
    }
    else {
	rb_str_cat2(str, ">");
    }
    return str;
}

/*
 *  call-seq:
 *     ios.to_io -> ios
 *
 *  Returns <em>ios</em>.
 */

static VALUE
rb_io_to_io(VALUE io, SEL sel)
{
    return io;
}

static bool
__rb_io_wait_readable(int fd)
{
    if (errno == EINTR) {
	fd_set readset;
	FD_ZERO(&readset);
	FD_SET(fd, &readset);
	return select(fd + 1, &readset, NULL, NULL, NULL) >= 0;
    }
    return false;
}

// Note: not bool since it's exported in a public header which does not
// include stdbool.
int
rb_io_wait_readable(int fd)
{
    return __rb_io_wait_readable(fd);
}

// Note: not bool since it's exported in a public header which does not
// include stdbool.
int
rb_io_wait_writable(int fd)
{
    // TODO
    return false;
}

// Note: not bool since it's exported in a public header which does not
// include stdbool.
int
rb_io_read_pending(rb_io_t *io_struct)
{
    return io_struct->buf != NULL && CFDataGetLength(io_struct->buf) > 0;
}

static long
read_internal(int fd, UInt8 *buffer, long len)
{
    long code;

retry:
    code = read(fd, buffer, len);
    if (code == -1) {
	if (__rb_io_wait_readable(fd)) {
	    goto retry;
	}
	rb_sys_fail("read() failed");
    }
    return code;
}

static void
rb_io_create_buf(rb_io_t *io_struct)
{
    if (io_struct->buf == NULL) {
	CFMutableDataRef data = CFDataCreateMutable(NULL, 0);
	GC_WB(&io_struct->buf, data);
	CFMakeCollectable(data);
    }
}

static void
rb_io_read_update(rb_io_t *io_struct, long len)
{
    io_struct->buf_offset += len;

    lseek(io_struct->read_fd, io_struct->buf_offset, SEEK_SET);

    if (io_struct->buf_offset == CFDataGetLength(io_struct->buf)) {
	CFDataSetLength(io_struct->buf, 0);
	io_struct->buf_offset = 0;
    }
}

static long
rb_io_read_internal(rb_io_t *io_struct, UInt8 *buffer, long len)
{
    assert(io_struct->read_fd != -1);

    if (io_struct->buf == NULL || CFDataGetLength(io_struct->buf) == 0) {
	struct stat buf;
	if (fstat(io_struct->read_fd, &buf) == -1 || buf.st_size == 0
		|| lseek(io_struct->read_fd, 0, SEEK_CUR) > 0) {
	    // Either a pipe, stdio, or a regular file that was seeked.
	    return len == 0
		? 0 : read_internal(io_struct->read_fd, buffer, len);
	}

	// TODO don't pre-read more than a certain threshold...
	const long size = buf.st_size;
	rb_io_create_buf(io_struct);
	CFDataSetLength(io_struct->buf, size);

	const long s = read_internal(io_struct->read_fd,
		CFDataGetMutableBytePtr(io_struct->buf), size);
	CFDataSetLength(io_struct->buf, s);
    }

    const long s = CFDataGetLength(io_struct->buf);
    if (s == 0 || len == 0) {
	return 0;
    }

    const long n = len > s - io_struct->buf_offset
	? s - io_struct->buf_offset : len;
    memcpy(buffer, CFDataGetBytePtr(io_struct->buf) + io_struct->buf_offset, n);

    rb_io_read_update(io_struct, n);

    return n;
}

static VALUE 
rb_io_read_all(rb_io_t *io_struct, VALUE outbuf) 
{
    struct stat st;
    long bufsize = 512;
    if (fstat(io_struct->read_fd, &st) == 0 && S_ISREG(st.st_mode)) {
	const off_t pos = lseek(io_struct->read_fd, 0, SEEK_CUR);
	if (st.st_size >= pos && pos >= 0) {
	    bufsize = st.st_size - pos;
	    if (bufsize > LONG_MAX) {
		rb_raise(rb_eIOError, "file too big for single read");
	    }
	}
    }

    outbuf = rb_str_bstr(outbuf);
    long bytes_read = 0;
    const long original_position = rb_bstr_length(outbuf);

    while (true) {
	rb_bstr_resize(outbuf, original_position + bytes_read + bufsize);
	uint8_t *bytes = rb_bstr_bytes(outbuf) + original_position + bytes_read;
        const long last_read = rb_io_read_internal(io_struct, bytes, bufsize);
        bytes_read += last_read;
	if (last_read == 0) {
	    break;
	}
    }

    rb_bstr_set_length(outbuf, original_position + bytes_read);
    return outbuf; 
}

long
rb_io_primitive_read(rb_io_t *io_struct, char *buffer, long len)
{
    return rb_io_read_internal(io_struct, (UInt8 *)buffer, len);
}

void
rb_io_set_nonblock(rb_io_t *fptr)
{
    int oflags;
#ifdef F_GETFL
    oflags = fcntl(fptr->fd, F_GETFL);
    if (oflags == -1) {
        rb_sys_fail("fcntl(2) failed");
    }
#else
    oflags = 0;
#endif
    if ((oflags & O_NONBLOCK) == 0) {
        oflags |= O_NONBLOCK;
        if (fcntl(fptr->fd, F_SETFL, oflags) == -1) {
            rb_sys_fail("fcntl(2) failed");
        }
    }
}

/*
 *  call-seq:
 *     ios.read_nonblock(maxlen)              => string
 *     ios.read_nonblock(maxlen, outbuf)      => outbuf
 *
 *  Reads at most <i>maxlen</i> bytes from <em>ios</em> using
 *  read(2) system call after O_NONBLOCK is set for
 *  the underlying file descriptor.
 *
 *  If the optional <i>outbuf</i> argument is present,
 *  it must reference a String, which will receive the data.
 *
 *  read_nonblock just calls read(2).
 *  It causes all errors read(2) causes: EAGAIN, EINTR, etc.
 *  The caller should care such errors.
 *
 *  read_nonblock causes EOFError on EOF.
 *
 *  If the read buffer is not empty,
 *  read_nonblock reads from the buffer like readpartial.
 *  In this case, read(2) is not called.
 *
 */

#if 0
static VALUE
io_read_nonblock(VALUE io, SEL sel, int argc, VALUE *argv)
{
    return Qnil;
}
#else
# define io_read_nonblock rb_f_notimplement
#endif

/*
 *  call-seq:
 *     ios.write_nonblock(string)   => integer
 *
 *  Writes the given string to <em>ios</em> using
 *  write(2) system call after O_NONBLOCK is set for
 *  the underlying file descriptor.
 *
 *  write_nonblock just calls write(2).
 *  It causes all errors write(2) causes: EAGAIN, EINTR, etc.
 *  The result may also be smaller than string.length (partial write).
 *  The caller should care such errors and partial write.
 *
 *  If the write buffer is not empty, it is flushed at first.
 *
 */

#if 0
static VALUE
rb_io_write_nonblock(VALUE io, SEL sel, VALUE str)
{
    return Qnil;
}
#else
# define rb_io_write_nonblock rb_f_notimplement
#endif

/*
 *  call-seq:
 *     ios.sysread(integer[, outbuf])    => string
 *
 *  Reads <i>integer</i> bytes from <em>ios</em> using a low-level
 *  read and returns them as a string. Do not mix with other methods
 *  that read from <em>ios</em> or you may get unpredictable results.
 *  If the optional <i>outbuf</i> argument is present, it must reference
 *  a String, which will receive the data.
 *  Raises <code>SystemCallError</code> on error and
 *  <code>EOFError</code> at end of file.
 *
 *     f = File.new("testfile")
 *     f.sysread(16)   #=> "This is line one"
 */

static VALUE
rb_io_sysread(VALUE self, SEL sel, int argc, VALUE *argv)
{
    VALUE count, buffer;
    rb_scan_args(argc, argv, "11", &count, &buffer);
    const long to_read = NUM2LONG(count);

    rb_io_t *io = ExtractIOStruct(self);
    rb_io_assert_readable(io);

    // TODO: throw error if the buffer is not empty;

    if (!NIL_P(buffer)) {
	// TODO: throw an error if the provided string can't be modified in place
	buffer = rb_str_bstr(rb_obj_as_string(buffer));
    }
    else {
	buffer = rb_bstr_new();
    }
    rb_bstr_resize(buffer, to_read);

    if (to_read == 0) {
	return buffer;
    }

    uint8_t *bytes = rb_bstr_bytes(buffer);

    const long r = read(io->read_fd, bytes, (size_t)to_read);
    if (r == -1) {
	rb_sys_fail("read(2) failed.");
    }
    // Resize the buffer to whatever was read
    rb_bstr_resize(buffer, r);

    if (r == 0 && to_read > 0) {
	rb_eof_error();
    }

    return buffer;
}

/*
 *  call-seq:
 *     ios.read([length [, buffer]])    => string, buffer, or nil
 *
 *  Reads at most <i>length</i> bytes from the I/O stream, or to the
 *  end of file if <i>length</i> is omitted or is <code>nil</code>.
 *  <i>length</i> must be a non-negative integer or nil.
 *  If the optional <i>buffer</i> argument is present, it must reference
 *  a String, which will receive the data.
 *
 *  At end of file, it returns <code>nil</code> or <code>""</code>
 *  depend on <i>length</i>.
 *  <code><i>ios</i>.read()</code> and
 *  <code><i>ios</i>.read(nil)</code> returns <code>""</code>.
 *  <code><i>ios</i>.read(<i>positive-integer</i>)</code> returns nil.
 *
 *  <code><i>ios</i>.read(0)</code> returns <code>""</code>.
 *
 *     f = File.new("testfile")
 *     f.read(16)   #=> "This is line one"
 */

static VALUE
io_read(VALUE io, SEL sel, int argc, VALUE *argv)
{
    VALUE len, outbuf;
    rb_scan_args(argc, argv, "02", &len, &outbuf);

    rb_io_t *io_struct = ExtractIOStruct(io);
    rb_io_assert_readable(io_struct);

    bool outbuf_created = false;
    if (NIL_P(outbuf)) {
	outbuf = rb_bstr_new();
	outbuf_created = true;
    }
    else {
	StringValue(outbuf);
	outbuf = rb_str_bstr(outbuf);
	rb_str_set_len(outbuf, 0);
    }

    if (NIL_P(len)) {
	rb_io_read_all(io_struct, outbuf);
	if (outbuf_created) {
	    rb_str_force_encoding(outbuf, rb_encodings[ENCODING_UTF8]);
	}
	return outbuf;
    }

    const long size = FIX2LONG(len);
    if (size < 0) {
	rb_raise(rb_eArgError, "negative length %ld given", size);
    }
    if (size == 0) {
	return rb_str_new2("");
    }

    if (size > 1000000000) {
	rb_raise(rb_eArgError, "given size `%ld' is too big", size);
    }

    rb_bstr_resize(outbuf, size);
    uint8_t *bytes = rb_bstr_bytes(outbuf);

    const long data_read = rb_io_read_internal(io_struct, bytes, size);
    rb_bstr_set_length(outbuf, data_read);

    if (data_read == 0) {
	return Qnil;
    }

    return outbuf;
}

/*
 *  call-seq:
 *     ios.readpartial(maxlen)              => string
 *     ios.readpartial(maxlen, outbuf)      => outbuf
 *
 *  Reads at most <i>maxlen</i> bytes from the I/O stream.
 *  It blocks only if <em>ios</em> has no data immediately available.
 *  It doesn't block if some data available.
 *  If the optional <i>outbuf</i> argument is present,
 *  it must reference a String, which will receive the data.
 *  It raises <code>EOFError</code> on end of file.
 *
 *  readpartial is designed for streams such as pipe, socket, tty, etc.
 *  It blocks only when no data immediately available.
 *  This means that it blocks only when following all conditions hold.
 *  * the buffer in the IO object is empty.
 *  * the content of the stream is empty.
 *  * the stream is not reached to EOF.
 *
 *  When readpartial blocks, it waits data or EOF on the stream.
 *  If some data is reached, readpartial returns with the data.
 *  If EOF is reached, readpartial raises EOFError.
 *
 *  When readpartial doesn't blocks, it returns or raises immediately.
 *  If the buffer is not empty, it returns the data in the buffer.
 *  Otherwise if the stream has some content,
 *  it returns the data in the stream.
 *  Otherwise if the stream is reached to EOF, it raises EOFError.
 *
 *     r, w = IO.pipe           #               buffer          pipe content
 *     w << "abc"               #               ""              "abc".
 *     r.readpartial(4096)      #=> "abc"       ""              ""
 *     r.readpartial(4096)      # blocks because buffer and pipe is empty.
 *
 *     r, w = IO.pipe           #               buffer          pipe content
 *     w << "abc"               #               ""              "abc"
 *     w.close                  #               ""              "abc" EOF
 *     r.readpartial(4096)      #=> "abc"       ""              EOF
 *     r.readpartial(4096)      # raises EOFError
 *
 *     r, w = IO.pipe           #               buffer          pipe content
 *     w << "abc\ndef\n"        #               ""              "abc\ndef\n"
 *     r.gets                   #=> "abc\n"     "def\n"         ""
 *     w << "ghi\n"             #               "def\n"         "ghi\n"
 *     r.readpartial(4096)      #=> "def\n"     ""              "ghi\n"
 *     r.readpartial(4096)      #=> "ghi\n"     ""              ""
 *
 *  Note that readpartial behaves similar to sysread.
 *  The differences are:
 *  * If the buffer is not empty, read from the buffer instead of "sysread for buffered IO (IOError)".
 *  * It doesn't cause Errno::EAGAIN and Errno::EINTR.  When readpartial meets EAGAIN and EINTR by read system call, readpartial retry the system call.
 *
 *  The later means that readpartial is nonblocking-flag insensitive.
 *  It blocks on the situation IO#sysread causes Errno::EAGAIN as if the fd is blocking mode.
 *
 */

static VALUE
io_readpartial(VALUE io, SEL sel, int argc, VALUE *argv)
{
    VALUE maxlen, buffer;
    rb_scan_args(argc, argv, "11", &maxlen, &buffer);
    if (FIX2INT(maxlen) == 0) {
	return rb_str_new2("");
    }
    else if (FIX2INT(maxlen) < 0) {
	rb_raise(rb_eArgError, "negative numbers not valid");
    }
    VALUE read_data = io_read(io, sel, argc, argv);
    if (NIL_P(read_data)) {
	rb_eof_error();
    }
    return read_data;
}

static void
prepare_getline_args(int argc, VALUE *argv, VALUE *rsp, long *lim, VALUE io)
{
    VALUE sep, limit;

    sep = limit = Qnil;
    if (argc != 0) {
	rb_scan_args(argc, argv, "02", &sep, &limit);
    }

    if (NIL_P(rb_rs)) {
	// TODO: Get rid of this when the fix comes in for the $\ variable.
	rb_rs = (VALUE)CFSTR("\n");
    }

    if (NIL_P(sep)) {
	// no arguments were passed at all.
	// FIXME: if you pass nil, it's suppose to read everything. that sucks.
	if (argc == 0) {
	    sep = rb_rs;
	    limit = Qnil;
	}
    }
    else {
	if (TYPE(sep) != T_STRING) {
	    // sep wasn't given, limit was.
	    limit = sep;
	    sep = rb_rs;
	}
	else if (RSTRING_LEN(sep) == 0) {
	    sep = (VALUE)CFSTR("\n\n");
	}
    }

    *rsp = sep;
    *lim = NIL_P(limit) ? -1 : FIX2LONG(limit);
}

static VALUE
rb_io_getline_1(VALUE sep, long line_limit, VALUE io)
{
    rb_io_t *io_struct = ExtractIOStruct(io);
    VALUE bstr = rb_bstr_new();

    rb_io_assert_readable(io_struct);
    if (NIL_P(sep)) {
	if (line_limit != -1) {
	    rb_bstr_resize(bstr, line_limit);
	    uint8_t *bytes = rb_bstr_bytes(bstr);
	    long r = rb_io_read_internal(io_struct, bytes, line_limit);
	    rb_bstr_set_length(bstr, r);
	}
	else {
	    rb_io_read_all(io_struct, bstr);
	}
	if (rb_bstr_length(bstr) == 0 && line_limit != 0) {
	    return Qnil;
	}
    }
    else if (line_limit != -1) {
	rb_bstr_resize(bstr, line_limit);
	uint8_t *bytes = rb_bstr_bytes(bstr);
	long r = rb_io_read_internal(io_struct, bytes, line_limit);
	if (r == 0 && line_limit != 0) {
	    return Qnil;
	}

	CFRange range = CFStringFind((CFStringRef)bstr, (CFStringRef)sep, 0);
	if (range.location != kCFNotFound) {
	    rb_io_create_buf(io_struct);
	    long rest_size = r - (range.location + 1);
	    if (rest_size > 0 && line_limit > r) {
		CFDataAppendBytes(io_struct->buf, bytes + range.location + 1,
				  rest_size);
	    }
	    else {
		io_struct->buf_offset -= rest_size;
	    }
	    r = range.location + 1;
	}
	// Resize the buffer to whatever was actually read (can be different
	// from asked size).
	rb_bstr_resize(bstr, r);
    }
    else {
	const char *sepstr = RSTRING_PTR(sep);
	const long seplen = RSTRING_LEN(sep);
	assert(seplen > 0);

	// Pre-cache if possible.
	rb_io_read_internal(io_struct, NULL, 0);
	if (io_struct->buf != NULL && CFDataGetLength(io_struct->buf) > 0) {
	    // Read from cache (fast).
	    const UInt8 *cache = CFDataGetMutableBytePtr(io_struct->buf)
		+ io_struct->buf_offset;
	    const long cache_len = CFDataGetLength(io_struct->buf)
		- io_struct->buf_offset;
	    const UInt8 *pos = cache;
	    long data_read = 0;
	    while (true) {
		const UInt8 *tmp = memchr(pos, sepstr[0], cache_len);
		if (tmp == NULL) {
		    data_read = cache_len;
		    break;
		}
		if (seplen == 1
			|| memcmp(tmp + 1, &sepstr[1], seplen - 1) == 0) {
		    data_read = tmp - cache + seplen;
		    break;
		}
		pos = tmp + seplen;
	    }
	    if (data_read == 0) {
		return Qnil;
	    }

	    rb_bstr_concat(bstr, cache, data_read);
	    rb_io_read_update(io_struct, data_read);
	}
	else {
	    // Read from IO (slow).
	    long s = 512;
	    long data_read = 0;
	    rb_bstr_resize(bstr, s);

	    uint8_t *buf = rb_bstr_bytes(bstr);
	    while (true) {
		uint8_t c = 0;
		if (rb_io_read_internal(io_struct, &c, 1) != 1) {
		    break;
		}
		if (data_read >= s) {
		    s += s;
		    rb_bstr_resize(bstr, s);
		    buf = rb_bstr_bytes(bstr);
		}
		buf[data_read] = c;
		data_read += 1;

		if (data_read >= seplen
			&& memcmp(&buf[data_read - seplen], sepstr,
			    seplen) == 0) {
		    break;
		}
	    }

	    if (data_read == 0) {
		return Qnil;
	    }
	    rb_bstr_set_length(bstr, data_read);
	}
    }
    OBJ_TAINT(bstr);
    io_struct->lineno += 1;
    ARGF.lineno = INT2FIX(io_struct->lineno);
    rb_str_force_encoding(bstr, rb_encodings[ENCODING_UTF8]);
    return bstr;
}

static VALUE
rb_io_getline(int argc, VALUE *argv, VALUE io)
{
    VALUE rs;
    long limit;

    prepare_getline_args(argc, argv, &rs, &limit, io);
    return rb_io_getline_1(rs, limit, io);
}

/*
 *  call-seq:
 *     ios.gets(sep=$/)     => string or nil
 *     ios.gets(limit)      => string or nil
 *     ios.gets(sep, limit) => string or nil
 *
 *  Reads the next ``line'' from the I/O stream; lines are separated by
 *  <i>sep</i>. A separator of <code>nil</code> reads the entire
 *  contents, and a zero-length separator reads the input a paragraph at
 *  a time (two successive newlines in the input separate paragraphs).
 *  The stream must be opened for reading or an <code>IOError</code>
 *  will be raised. The line read in will be returned and also assigned
 *  to <code>$_</code>. Returns <code>nil</code> if called at end of
 *  file.  If the first argument is an integer, or optional second
 *  argument is given, the returning string would not be longer than the
 *  given value.
 *
 *     File.new("testfile").gets   #=> "This is line one\n"
 *     $_                          #=> "This is line one\n"
 */

static VALUE
rb_io_gets_m(VALUE io, SEL sel, int argc, VALUE *argv)
{
    VALUE str;

    str = rb_io_getline(argc, argv, io);
    rb_lastline_set(str);

    return str;
}

/* 
 *  call-seq:
 *     ios.lineno    => integer
 *
 *  Returns the current line number in <em>ios</em>. The stream must be
 *  opened for reading. <code>lineno</code> counts the number of times
 *  <code>gets</code> is called, rather than the number of newlines
 *  encountered. The two values will differ if <code>gets</code> is
 *  called with a separator other than newline. See also the
 *  <code>$.</code> variable.
 *
 *     f = File.new("testfile")
 *     f.lineno   #=> 0
 *     f.gets     #=> "This is line one\n"
 *     f.lineno   #=> 1
 *     f.gets     #=> "This is line two\n"
 *     f.lineno   #=> 2
 */

static VALUE
rb_io_lineno(VALUE io, SEL sel)
{
    rb_io_t *io_s = ExtractIOStruct(io);
    rb_io_assert_open(io_s);
    return INT2FIX(io_s->lineno);
}

/*
 *  call-seq:
 *     ios.lineno = integer    => integer
 *
 *  Manually sets the current line number to the given value.
 *  <code>$.</code> is updated only on the next read.
 *
 *     f = File.new("testfile")
 *     f.gets                     #=> "This is line one\n"
 *     $.                         #=> 1
 *     f.lineno = 1000
 *     f.lineno                   #=> 1000
 *     $.                         #=> 1         # lineno of last read
 *     f.gets                     #=> "This is line two\n"
 *     $.                         #=> 1001      # lineno of last read
 */

static VALUE
rb_io_set_lineno(VALUE io, SEL sel, VALUE line_no)
{
    rb_io_t *io_s = ExtractIOStruct(io);
    rb_io_assert_open(io_s);
    io_s->lineno = NUM2INT(line_no);
    return line_no;
}

/*
 *  call-seq:
 *     ios.readline(sep=$/)     => string
 *     ios.readline(limit)      => string
 *     ios.readline(sep, limit) => string
 *
 *  Reads a line as with <code>IO#gets</code>, but raises an
 *  <code>EOFError</code> on end of file.
 */

static VALUE
rb_io_readline(VALUE io, SEL sel, int argc, VALUE *argv)
{
    VALUE ret = rb_io_gets_m(io, sel, argc, argv);
    if (NIL_P(ret)) {
	rb_eof_error();
    }
    return ret;
}

/*
 *  call-seq:
 *     ios.readlines(sep=$/)     => array
 *     ios.readlines(limit)      => array
 *     ios.readlines(sep, limit) => array
 *
 *  Reads all of the lines in <em>ios</em>, and returns them in
 *  <i>anArray</i>. Lines are separated by the optional <i>sep</i>. If
 *  <i>sep</i> is <code>nil</code>, the rest of the stream is returned
 *  as a single record.  If the first argument is an integer, or
 *  optional second argument is given, the returning string would not be
 *  longer than the given value. The stream must be opened for reading
 *  or an <code>IOError</code> will be raised.
 *
 *     f = File.new("testfile")
 *     f.readlines[0]   #=> "This is line one\n"
 */

static VALUE
rb_io_readlines(VALUE io, SEL sel, int argc, VALUE *argv)
{
    VALUE rs;
    long limit;

    prepare_getline_args(argc, argv, &rs, &limit, io);
    if (limit == 0) {
	rb_raise(rb_eArgError, "invalid limit: 0 for readlines");
    }

    VALUE lines = rb_ary_new();
    while (true) {
	VALUE line = rb_io_getline_1(rs, limit, io);
	if (NIL_P(line)) {
	    break;
	}
	rb_ary_push(lines, line);
    }
    return lines;
}

/*
 *  call-seq:
 *     ios.each(sep=$/) {|line| block }         => ios
 *     ios.each(limit) {|line| block }          => ios
 *     ios.each(sep,limit) {|line| block }      => ios
 *     ios.each_line(sep=$/) {|line| block }    => ios
 *     ios.each_line(limit) {|line| block }     => ios
 *     ios.each_line(sep,limit) {|line| block } => ios
 *
 *  Executes the block for every line in <em>ios</em>, where lines are
 *  separated by <i>sep</i>. <em>ios</em> must be opened for
 *  reading or an <code>IOError</code> will be raised.
 *
 *     f = File.new("testfile")
 *     f.each {|line| puts "#{f.lineno}: #{line}" }
 *
 *  <em>produces:</em>
 *
 *     1: This is line one
 *     2: This is line two
 *     3: This is line three
 *     4: And so on...
 */

static SEL sel_each_line = 0;
static SEL sel_each_byte = 0;
static SEL sel_each_char = 0;

static VALUE
rb_io_each_line(VALUE io, SEL sel, int argc, VALUE *argv)
{
    VALUE rs;
    long limit;

    RETURN_ENUMERATOR(io, argc, argv);
    prepare_getline_args(argc, argv, &rs, &limit, io);
    if (limit == 0) {
	rb_raise(rb_eArgError, "invalid limit: 0 for each_line");
    }

    while (true) {
	VALUE line = rb_io_getline_1(rs, limit, io);
	if (NIL_P(line)) {
	    break;
	}
	rb_vm_yield(1, &line);
	RETURN_IF_BROKEN();
    }
    return io;
}

/*
 *  call-seq:
 *     ios.each_byte {|byte| block }  => ios
 *
 *  Calls the given block once for each byte (0..255) in <em>ios</em>,
 *  passing the byte as an argument. The stream must be opened for
 *  reading or an <code>IOError</code> will be raised.
 *
 *     f = File.new("testfile")
 *     checksum = 0
 *     f.each_byte {|x| checksum ^= x }   #=> #<File:testfile>
 *     checksum                           #=> 12
 */

static VALUE
rb_io_each_byte(VALUE io, SEL sel)
{
    RETURN_ENUMERATOR(io, 0, 0);

    VALUE b = rb_io_getbyte(io, 0);
    while (!NIL_P(b)) {
	rb_vm_yield(1, &b);
	RETURN_IF_BROKEN();
	b = rb_io_getbyte(io, 0);
    }
    return io;
}

/*
 *  call-seq:
 *     ios.each_char {|c| block }  => ios
 *
 *  Calls the given block once for each character in <em>ios</em>,
 *  passing the character as an argument. The stream must be opened for
 *  reading or an <code>IOError</code> will be raised.
 *
 *     f = File.new("testfile")
 *     f.each_char {|c| print c, ' ' }   #=> #<File:testfile>
 */

static VALUE rb_io_getc(VALUE io, SEL sel);

static VALUE
rb_io_each_char(VALUE io, SEL sel)
{
    RETURN_ENUMERATOR(io, 0, 0);

    VALUE c = rb_io_getc(io, 0);
    while (!NIL_P(c)) {
	rb_vm_yield(1, &c);
	RETURN_IF_BROKEN();
	c = rb_io_getc(io, 0);
    }
    return io;
}

/*
 *  call-seq:
 *     ios.lines(sep=$/)     => anEnumerator
 *     ios.lines(limit)      => anEnumerator
 *     ios.lines(sep, limit) => anEnumerator
 *
 *  Returns an enumerator that gives each line in <em>ios</em>.
 *  The stream must be opened for reading or an <code>IOError</code>
 *  will be raised.
 *
 *     f = File.new("testfile")
 *     f.lines.to_a  #=> ["foo\n", "bar\n"]
 *     f.rewind
 *     f.lines.sort  #=> ["bar\n", "foo\n"]
 */

static VALUE
rb_io_lines(VALUE io, SEL sel, int argc, VALUE *argv)
{
    return rb_enumeratorize(io, sel_each_line, 0, NULL);
}

/*
 *  call-seq:
 *     ios.bytes   => anEnumerator
 *
 *  Returns an enumerator that gives each byte (0..255) in <em>ios</em>.
 *  The stream must be opened for reading or an <code>IOError</code>
 *  will be raised.
 *     
 *     f = File.new("testfile")
 *     f.bytes.to_a  #=> [104, 101, 108, 108, 111]
 *     f.rewind
 *     f.bytes.sort  #=> [101, 104, 108, 108, 111]
 */

static VALUE
rb_io_bytes(VALUE io, SEL sel)
{
    return rb_enumeratorize(io, sel_each_byte, 0, NULL);
}

/*
 *  call-seq:
 *     ios.chars   => anEnumerator
 *  
 *  Returns an enumerator that gives each character in <em>ios</em>.
 *  The stream must be opened for reading or an <code>IOError</code>
 *  will be raised.
 *     
 *     f = File.new("testfile")
 *     f.chars.to_a  #=> ["h", "e", "l", "l", "o"]
 *     f.rewind
 *     f.chars.sort  #=> ["e", "h", "l", "l", "o"]
 */


static VALUE
rb_io_chars(VALUE io, SEL sel)
{
    return rb_enumeratorize(io, sel_each_char, 0, NULL);
}

/*
 *  call-seq:
 *     ios.getc   => fixnum or nil
 *
 *  Reads a one-character string from <em>ios</em>. Returns
 *  <code>nil</code> if called at end of file.
 *
 *     f = File.new("testfile")
 *     f.getc   #=> "8"
 *     f.getc   #=> "1"
 */

static VALUE
rb_io_getc(VALUE io, SEL sel)
{
    rb_io_t *io_struct = ExtractIOStruct(io);
    rb_io_assert_readable(io_struct);

    // TODO should be encoding aware

    UInt8 byte;
    if (rb_io_read_internal(io_struct, &byte, 1) != 1) {
	return Qnil;
    }

    char buf[2];
    buf[0] = byte;
    buf[1] = '\0';

    return rb_str_new2(buf);
}

/*
 *  call-seq:
 *     ios.readchar   => string
 *
 *  Reads a one-character string from <em>ios</em>. Raises an
 *  <code>EOFError</code> on end of file.
 *
 *     f = File.new("testfile")
 *     f.readchar   #=> "8"
 *     f.readchar   #=> "1"
 */

static VALUE
rb_io_readchar(VALUE io, SEL sel)
{
    VALUE c = rb_io_getc(io, 0);

    if (NIL_P(c)) {
	rb_eof_error();
    }
    return c;
}

/*
 *  call-seq:
 *     ios.getbyte   => fixnum or nil
 *
 *  Gets the next 8-bit byte (0..255) from <em>ios</em>. Returns
 *  <code>nil</code> if called at end of file.
 *
 *     f = File.new("testfile")
 *     f.getbyte   #=> 84
 *     f.getbyte   #=> 104
 */

VALUE
rb_io_getbyte(VALUE io, SEL sel)
{
    rb_io_t *io_struct = ExtractIOStruct(io);
    rb_io_assert_readable(io_struct);

    UInt8 byte;
    if (rb_io_read_internal(io_struct, &byte, 1) != 1) {
	return Qnil;
    }

    return INT2FIX(byte);
}

/*
 *  call-seq:
 *     ios.readbyte   => fixnum
 *
 *  Reads a character as with <code>IO#getc</code>, but raises an
 *  <code>EOFError</code> on end of file.
 */

static VALUE
rb_io_readbyte(VALUE io, SEL sel)
{
    VALUE c = rb_io_getbyte(io, 0);

    if (NIL_P(c)) {
	rb_eof_error();
    }
    return c;
}

/*
 *  call-seq:
 *     ios.ungetc(string)   => nil
 *
 *  Pushes back one character (passed as a parameter) onto <em>ios</em>,
 *  such that a subsequent read will read it. When calling <code>ungetc</code>
 *	multiple times, the most-recently-pushed character will be read first.
 *
 *     f = File.new("testfile")   #=> #<File:testfile>
 *     c = f.getc                 #=> "8"
 *     f.ungetc(c)                #=> nil
 *     f.getc                     #=> "8"
 */

VALUE
rb_io_ungetc(VALUE io, SEL sel, VALUE c)
{
    rb_io_t *io_struct = ExtractIOStruct(io);
    rb_io_assert_readable(io_struct);

    if (NIL_P(c)) {
	return Qnil;
    }

    UInt8 *bytes;
    size_t len;

    if (FIXNUM_P(c)) {
        UInt8 cc = (UInt8)FIX2INT(c);

	bytes = (UInt8 *)alloca(2);
        bytes[0] = cc;
        bytes[1] = '\0';
	len = 1;
    }
    else {
        SafeStringValue(c);
	bytes = (UInt8 *)RSTRING_PTR(c);
	len = RSTRING_LEN(c);
    }

    rb_io_create_buf(io_struct);

    if (len <= io_struct->buf_offset) {
	io_struct->buf_offset -= len;
	UInt8 *data = CFDataGetMutableBytePtr(io_struct->buf);
	memcpy(data + io_struct->buf_offset, bytes, len);
    }
    else {
	const long n = len - io_struct->buf_offset;
	CFDataIncreaseLength(io_struct->buf, n);
	// CFDataGetMutableBytePtr must be called after CFDataIncreaseLength
	UInt8 *data = CFDataGetMutableBytePtr(io_struct->buf);
	memmove(data + n, data, CFDataGetLength(io_struct->buf) - n);
	memcpy(data, bytes, len);
    }

    return Qnil;
}

/*
 *  call-seq:
 *     ios.isatty   => true or false
 *     ios.tty?     => true or false
 *
 *  Returns <code>true</code> if <em>ios</em> is associated with a
 *  terminal device (tty), <code>false</code> otherwise.
 *
 *     File.new("testfile").isatty   #=> false
 *     File.new("/dev/tty").isatty   #=> true
 */

static VALUE
rb_io_isatty(VALUE io, SEL sel)
{
    rb_io_t *io_s = ExtractIOStruct(io);
    rb_io_assert_open(io_s);
    return isatty(io_s->fd) ? Qtrue : Qfalse;
}

/*
 *  call-seq:
 *     ios.close_on_exec?   => true or false
 *
 *  Returns <code>true</code> if <em>ios</em> will be closed on exec.
 *
 *     f = open("/dev/null")
 *     f.close_on_exec?                 #=> false
 *     f.close_on_exec = true
 *     f.close_on_exec?                 #=> true
 *     f.close_on_exec = false
 *     f.close_on_exec?                 #=> false
 */

static VALUE
rb_io_close_on_exec_p(VALUE io, SEL sel)
{
    rb_io_t *io_s = ExtractIOStruct(io);
    const int flags = fcntl(io_s->fd, F_GETFD, 0);
    return ((flags & FD_CLOEXEC) ? Qtrue : Qfalse);
}

/*
 *  call-seq:
 *     ios.close_on_exec = bool    => true or false
 *
 *  Sets a close-on-exec flag.
 *
 *     f = open("/dev/null")
 *     f.close_on_exec = true
 *     system("cat", "/proc/self/fd/#{f.fileno}") # cat: /proc/self/fd/3: No such file or directory
 *     f.closed?                #=> false
 */

static VALUE
rb_io_set_close_on_exec(VALUE io, SEL sel, VALUE arg)
{
    rb_io_t *io_s = ExtractIOStruct(io);
    int flags = fcntl(io_s->fd, F_GETFD, 0);
    if (arg == Qtrue) {		
	flags |= FD_CLOEXEC;
    }
    else {
	flags &= ~FD_CLOEXEC;
    }
    fcntl(io_s->fd, F_SETFD, flags);
    return arg;
}

static VALUE
rb_io_close_m(VALUE io, SEL sel)
{
    rb_io_t *io_s = ExtractIOStruct(io);
    if (rb_safe_level() >= 4 && !OBJ_UNTRUSTED(io)) {
	rb_raise(rb_eSecurityError, "Insecure: can't close");
    }
    rb_io_check_closed(io_s);
    io_close(io_s, true, true);
    return Qnil;
}

/*
 *  call-seq:
 *     ios.closed?    => true or false
 *
 *  Returns <code>true</code> if <em>ios</em> is completely closed (for
 *  duplex streams, both reader and writer), <code>false</code>
 *  otherwise.
 *
 *     f = File.new("testfile")
 *     f.close         #=> nil
 *     f.closed?       #=> true
 *     f = IO.popen("/bin/sh","r+")
 *     f.close_write   #=> nil
 *     f.closed?       #=> false
 *     f.close_read    #=> nil
 *     f.closed?       #=> true
 */


static VALUE
rb_io_closed(VALUE io, SEL sel)
{
    rb_io_t *ios = ExtractIOStruct(io);
    return rb_io_is_closed_for_writing(ios) && rb_io_is_closed_for_reading(ios)
	? Qtrue : Qfalse;
}

/*
 *  call-seq:
 *     ios.close_read    => nil
 *
 *  Closes the read end of a duplex I/O stream (i.e., one that contains
 *  both a read and a write stream, such as a pipe). Will raise an
 *  <code>IOError</code> if the stream is not duplexed.
 *
 *     f = IO.popen("/bin/sh","r+")
 *     f.close_read
 *     f.readlines
 *
 *  <em>produces:</em>
 *
 *     prog.rb:3:in `readlines': not opened for reading (IOError)
 *     	from prog.rb:3
 */

static VALUE
rb_io_close_read(VALUE io, SEL sel)
{
    rb_io_t *io_s = ExtractIOStruct(io);
    if (rb_safe_level() >= 4 && !OBJ_UNTRUSTED(io)) {
	rb_raise(rb_eSecurityError, "Insecure: can't close");
    }
    rb_io_check_initialized(io_s);
    if (io_s->read_fd == -1) {
        rb_raise(rb_eIOError, "closed read stream");
    }
    io_close(io_s, true, false);
    return Qnil;
}

/*
 *  call-seq:
 *     ios.close_write   => nil
 *
 *  Closes the write end of a duplex I/O stream (i.e., one that contains
 *  both a read and a write stream, such as a pipe). Will raise an
 *  <code>IOError</code> if the stream is not duplexed.
 *
 *     f = IO.popen("/bin/sh","r+")
 *     f.close_write
 *     f.print "nowhere"
 *
 *  <em>produces:</em>
 *
 *     prog.rb:3:in `write': not opened for writing (IOError)
 *     	from prog.rb:3:in `print'
 *     	from prog.rb:3
 */

static VALUE
rb_io_close_write(VALUE io, SEL sel)
{
    rb_io_t *io_s = ExtractIOStruct(io);
    if (rb_safe_level() >= 4 && !OBJ_UNTRUSTED(io)) {
	rb_raise(rb_eSecurityError, "Insecure: can't close");
    }
    rb_io_check_initialized(io_s);
    if (io_s->write_fd == -1) {
        rb_raise(rb_eIOError, "closed write stream");
    }
    if (io_s->mode & FMODE_READABLE) {
	rb_raise(rb_eIOError, "closing non-duplex IO for writing");
    }
    io_close(io_s, false, true);
    return Qnil;
}

VALUE
rb_io_close(VALUE io)
{
    rb_io_t *io_struct = ExtractIOStruct(io);
    io_close(io_struct, true, true);
    return Qnil;
}

VALUE
rb_io_fdopen(int fd, int mode, const char *path)
{
    VALUE klass = rb_cIO;

    if (path != NULL && strcmp(path, "-") != 0) {
	klass = rb_cFile;
    }
    return prep_io(fd, convert_oflags_to_fmode(mode), klass);
}

VALUE
rb_io_gets(VALUE io, SEL sel)
{
    return rb_io_getline_1(rb_default_rs, -1, io);
}

VALUE
rb_io_binmode(VALUE io, SEL sel)
{
    // binmode does nothing on Mac OS X
    rb_io_t *io_struct = ExtractIOStruct(io);
    rb_io_check_closed(io_struct);
    return io;
}

/*
 *  call-seq:
 *     ios.binmode    => ios
 *
 *  Puts <em>ios</em> into binary mode. This is useful only in
 *  MS-DOS/Windows environments. Once a stream is in binary mode, it
 *  cannot be reset to nonbinary mode.
 */

static VALUE
rb_io_binmode_m(VALUE io, SEL sel)
{
    rb_io_binmode(io, 0);
    return io;
}

/*
 *  call-seq:
 *     IO.popen(cmd, mode="r")               => io
 *     IO.popen(cmd, mode="r") {|io| block } => obj
 *
 *  Runs the specified command as a subprocess; the subprocess's
 *  standard input and output will be connected to the returned
 *  <code>IO</code> object.  If _cmd_ is a +String+
 *  ``<code>-</code>'', then a new instance of Ruby is started as the
 *  subprocess.  If <i>cmd</i> is an +Array+ of +String+, then it will
 *  be used as the subprocess's +argv+ bypassing a shell.
 *  The array can contains a hash at first for environments and
 *  a hash at last for options similar to <code>spawn</code>.  The default
 *  mode for the new file object is ``r'', but <i>mode</i> may be set
 *  to any of the modes listed in the description for class IO.
 *
 *  Raises exceptions which <code>IO::pipe</code> and
 *  <code>Kernel::system</code> raise.
 *
 *  If a block is given, Ruby will run the command as a child connected
 *  to Ruby with a pipe. Ruby's end of the pipe will be passed as a
 *  parameter to the block.
 *  At the end of block, Ruby close the pipe and sets <code>$?</code>.
 *  In this case <code>IO::popen</code> returns
 *  the value of the block.
 *
 *  If a block is given with a _cmd_ of ``<code>-</code>'',
 *  the block will be run in two separate processes: once in the parent,
 *  and once in a child. The parent process will be passed the pipe
 *  object as a parameter to the block, the child version of the block
 *  will be passed <code>nil</code>, and the child's standard in and
 *  standard out will be connected to the parent through the pipe. Not
 *  available on all platforms.
 *
 *     f = IO.popen("uname")
 *     p f.readlines
 *     puts "Parent is #{Process.pid}"
 *     IO.popen("date") { |f| puts f.gets }
 *     IO.popen("-") {|f| $stderr.puts "#{Process.pid} is here, f is #{f}"}
 *     p $?
 *     IO.popen(%w"sed -e s|^|<foo>| -e s&$&;zot;&", "r+") {|f|
 *       f.puts "bar"; f.close_write; puts f.gets
 *     }
 *
 *  <em>produces:</em>
 *
 *     ["Linux\n"]
 *     Parent is 26166
 *     Wed Apr  9 08:53:52 CDT 2003
 *     26169 is here, f is
 *     26166 is here, f is #<IO:0x401b3d44>
 *     #<Process::Status: pid=26166,exited(0)>
 *     <foo>bar;zot;
 */

#define rb_sys_fail_unless(action, msg) do { \
    errno = action; \
    if (errno != 0) { \
	rb_sys_fail(msg); \
    } \
} while(0)

static VALUE 
io_from_spawning_new_process(VALUE prog, VALUE mode)
{
    VALUE io = io_alloc(rb_cIO, 0);
    rb_io_t *io_struct = ExtractIOStruct(io);
    posix_spawn_file_actions_t actions;

    int fd_r[2], fd_w[2];
    if (pipe(fd_r) < 0) {
	rb_sys_fail("pipe() failed");
    }
    if (pipe(fd_w) < 0) {
	close(fd_r[0]);
	close(fd_r[1]);
	rb_sys_fail("pipe() failed");
    }

    // MacRuby               child process
    //  in  : fd_r[0]  -----  fd_r[1] : stdout
    //  out : fd_w[1]  -----  fd_w[0] : stdin
    rb_sys_fail_unless(posix_spawn_file_actions_init(&actions),
	    "could not init file actions");
    rb_sys_fail_unless(posix_spawn_file_actions_adddup2(&actions, fd_w[0],
		STDIN_FILENO), "could not add dup2() to stdin");
    rb_sys_fail_unless(posix_spawn_file_actions_addclose(&actions, fd_w[0]),
	    "could not add a close() to stdin");
    rb_sys_fail_unless(posix_spawn_file_actions_addclose(&actions, fd_w[1]),
	    "could not add a close() to stdin");
    rb_sys_fail_unless(posix_spawn_file_actions_adddup2(&actions, fd_r[1],
		STDOUT_FILENO), "could not add dup2() to stdout");
    rb_sys_fail_unless(posix_spawn_file_actions_addclose(&actions, fd_r[0]),
	    "could not add a close() to stdout");
    rb_sys_fail_unless(posix_spawn_file_actions_addclose(&actions, fd_r[1]),
	    "could not add a close() to stdout");
    pid_t pid;

    VALUE argArray = rb_check_array_type(prog);
    if (!NIL_P(argArray)) {
	const long len = RARRAY_LEN(argArray);
	char **spawnedArgs =
		malloc((len + 1) * sizeof(char *));
	assert(spawnedArgs != NULL);
	for (long i = 0; i < len; i++) {
	    VALUE str = RARRAY_AT(argArray, i);
	    spawnedArgs[i] = StringValuePtr(str);
	}
	spawnedArgs[len] = 0;
	// using posix_spawnP (look up binary in PATH)
	errno = posix_spawnp(&pid, spawnedArgs[0], &actions, NULL, spawnedArgs,
		*(_NSGetEnviron()));
	const int err = errno;
	free(spawnedArgs);
	errno = err;
    }
    else {
	// TODO: Split the process_name up into char* components?
	char *spawnedArgs[] = {(char*)_PATH_BSHELL, "-c",
		StringValuePtr(prog), NULL};
	errno = posix_spawn(&pid, spawnedArgs[0], &actions, NULL, spawnedArgs,
		*(_NSGetEnviron()));
    }

    posix_spawn_file_actions_destroy(&actions);
    if (errno != 0) {
	const int err = errno;
	close(fd_r[0]);
	close(fd_r[1]);
	close(fd_w[0]);
	close(fd_w[1]);
	errno = err;
	rb_sys_fail("posix_spawn() failed");
    }

    io_struct->fd = fd_r[0];
    io_struct->pid = pid;
    io_struct->mode = mode;

    const int fmode = convert_mode_string_to_fmode(mode);
    if (fmode & FMODE_READABLE) {
	io_struct->read_fd = fd_r[0];
    }
    else {
	close(fd_r[0]);
    }
    if (fmode & FMODE_WRITABLE) {
	io_struct->write_fd = fd_w[1];
    }
    else {
	close(fd_w[1]);
    }
    close(fd_r[1]);
    close(fd_w[0]);

    return io;
}

static VALUE
io_pipe_open(VALUE prog, VALUE mode)
{
    if (NIL_P(mode)) {
	mode = (VALUE)CFSTR("r");
    }
    return io_from_spawning_new_process(prog, mode);
}

static VALUE
rb_io_s_popen(VALUE klass, SEL sel, int argc, VALUE *argv)
{
    VALUE process_name, mode;
    rb_scan_args(argc, argv, "11", &process_name, &mode);

    VALUE io = io_pipe_open(process_name, mode);
    if (rb_block_given_p()) {
	VALUE ret = rb_vm_yield(1, &io);
	rb_io_close(io);
	return ret;
    }
    return io;
}

/*
 *  call-seq:
 *     IO.open(fd, mode_string="r" )               => io
 *     IO.open(fd, mode_string="r" ) {|io| block } => obj
 *
 *  With no associated block, <code>open</code> is a synonym for
 *  <code>IO::new</code>. If the optional code block is given, it will
 *  be passed <i>io</i> as an argument, and the IO object will
 *  automatically be closed when the block terminates. In this instance,
 *  <code>IO::open</code> returns the value of the block.
 *
 */

static VALUE rb_io_s_new0(VALUE klass, int argc, VALUE *argv);

static VALUE
rb_io_s_open(VALUE klass, SEL sel, int argc, VALUE *argv)
{
    VALUE io = rb_io_s_new0(klass, argc, argv);
    if (rb_block_given_p()) {
        VALUE ret = rb_vm_yield(1, &io);
        rb_io_close(io);
        return ret;
    }
    return io;
}

static VALUE
check_pipe_command(VALUE fname)
{
    const char *cname = StringValuePtr(fname);
    const size_t len = strlen(cname);

    if (cname[0] == '|') {
        VALUE cmd = rb_str_new(cname + 1, len - 1);
        return cmd;
    }
    return Qnil;
}

/*
 *  call-seq:
 *     open(path [, mode_enc [, perm]] )                => io or nil
 *     open(path [, mode_enc [, perm]] ) {|io| block }  => obj
 *
 *  Creates an <code>IO</code> object connected to the given stream,
 *  file, or subprocess.
 *
 *  If <i>path</i> does not start with a pipe character
 *  (``<code>|</code>''), treat it as the name of a file to open using
 *  the specified mode (defaulting to ``<code>r</code>'').
 *
 *  The mode_enc is
 *  either a string or an integer.  If it is an integer, it must be
 *  bitwise-or of open(2) flags, such as File::RDWR or File::EXCL.
 *  If it is a string, it is either "mode", "mode:ext_enc", or
 *  "mode:ext_enc:int_enc".
 *  The mode is one of the following:
 *
 *   r: read (default)
 *   w: write
 *   a: append
 *
 *  The mode can be followed by "b" (means binary-mode), or "+"
 *  (means both reading and writing allowed) or both.
 *  If ext_enc (external encoding) is specified,
 *  read string will be tagged by the encoding in reading,
 *  and output string will be converted
 *  to the specified encoding in writing.
 *  If two encoding names,
 *  ext_enc and int_enc (external encoding and internal encoding),
 *  are specified, the read string is converted from ext_enc
 *  to int_enc then tagged with the int_enc in read mode,
 *  and in write mode, the output string will be
 *  converted from int_enc to ext_enc before writing.
 *
 *  If a file is being created, its initial permissions may be
 *  set using the integer third parameter.
 *
 *  If a block is specified, it will be invoked with the
 *  <code>File</code> object as a parameter, and the file will be
 *  automatically closed when the block terminates. The call
 *  returns the value of the block.
 *
 *  If <i>path</i> starts with a pipe character, a subprocess is
 *  created, connected to the caller by a pair of pipes. The returned
 *  <code>IO</code> object may be used to write to the standard input
 *  and read from the standard output of this subprocess. If the command
 *  following the ``<code>|</code>'' is a single minus sign, Ruby forks,
 *  and this subprocess is connected to the parent. In the subprocess,
 *  the <code>open</code> call returns <code>nil</code>. If the command
 *  is not ``<code>-</code>'', the subprocess runs the command. If a
 *  block is associated with an <code>open("|-")</code> call, that block
 *  will be run twice---once in the parent and once in the child. The
 *  block parameter will be an <code>IO</code> object in the parent and
 *  <code>nil</code> in the child. The parent's <code>IO</code> object
 *  will be connected to the child's <code>$stdin</code> and
 *  <code>$stdout</code>. The subprocess will be terminated at the end
 *  of the block.
 *
 *     open("testfile") do |f|
 *       print f.gets
 *     end
 *
 *  <em>produces:</em>
 *
 *     This is line one
 *
 *  Open a subprocess and read its output:
 *
 *     cmd = open("|date")
 *     print cmd.gets
 *     cmd.close
 *
 *  <em>produces:</em>
 *
 *     Wed Apr  9 08:56:31 CDT 2003
 *
 *  Open a subprocess running the same Ruby program:
 *
 *     f = open("|-", "w+")
 *     if f == nil
 *       puts "in Child"
 *       exit
 *     else
 *       puts "Got: #{f.gets}"
 *     end
 *
 *  <em>produces:</em>
 *
 *     Got: in Child
 *
 *  Open a subprocess using a block to receive the I/O object:
 *
 *     open("|-") do |f|
 *       if f == nil
 *         puts "in Child"
 *       else
 *         puts "Got: #{f.gets}"
 *       end
 *     end
 *
 *  <em>produces:</em>
 *
 *     Got: in Child
 */

static VALUE
rb_file_open(VALUE io, int argc, VALUE *argv)
{
    if (argc > 0) {
	// First argument must always be a path, we are checking it here to
	// conform to RubySpecs which states that the conversion must only
	// happen once.
	FilePathValue(argv[0]);
    }
    if (argc >= 1) {
	VALUE cmd = check_pipe_command(argv[0]);
	if (cmd != Qnil) {
	    VALUE modes = (argc >= 2) ? argv[1] : Qnil;
	    return io_pipe_open(cmd, modes);
	}
    }
    VALUE path, modes, permissions;
    rb_scan_args(argc, argv, "12", &path, &modes, &permissions);
    if (NIL_P(modes)) {
	modes = (VALUE)CFSTR("r");
    }
    const char *filepath = RSTRING_PTR(path);
    const int flags = convert_mode_string_to_oflags(modes);
    const mode_t perm = NIL_P(permissions) ? 0666 : NUM2UINT(permissions);
    int fd, retry = 0;
    while (true) {
       fd = open(filepath, flags, perm);
       if (fd == -1) {
	   if (retry < 5 && errno == EMFILE) {
		// Too many open files. Let's schedule a GC collection.
	       	rb_gc();
		usleep(1000);
	       	retry++;
	   }
	   else {
	       rb_sys_fail("open() failed");
	   }
       }
       else {
	   break;
       }
    }
    rb_io_t *io_struct = ExtractIOStruct(io);
    prepare_io_from_fd(io_struct, fd, convert_oflags_to_fmode(flags));
    GC_WB(&io_struct->path, path); 
    return io;
}

static VALUE
f_open_body(VALUE io)
{
    return rb_vm_yield(1, &io);
}

static VALUE
f_open_ensure(VALUE io)
{
    rb_io_close(io);
    return Qnil;
}

VALUE
rb_f_open(VALUE klass, SEL sel, int argc, VALUE *argv)
{
    if (argc >= 1) {
	VALUE cmd = check_pipe_command(argv[0]);
	if (cmd != Qnil) {
	    return rb_io_s_popen(rb_cIO, 0, 1, &cmd);
	}
    }
    VALUE io = rb_class_new_instance(argc, argv, rb_cFile);
    if (rb_block_given_p()) {
	return rb_ensure(f_open_body, io, f_open_ensure, io);
    }
    return io;
}

/*
 *  call-seq:
 *     IO.sysopen(path, [mode, [perm]])  => fixnum
 *
 *  Opens the given path, returning the underlying file descriptor as a
 *  <code>Fixnum</code>.
 *
 *     IO.sysopen("testfile")   #=> 3
 *
 */

static VALUE
rb_io_s_sysopen(VALUE klass, SEL sel, int argc, VALUE *argv)
{
    VALUE f = rb_f_open(klass, sel, argc, argv);
    return INT2FIX(ExtractIOStruct(f)->fd);
}

/*
 *  call-seq:
 *     ios.reopen(other_IO)         => ios
 *     ios.reopen(path, mode_str)   => ios
 *
 *  Reassociates <em>ios</em> with the I/O stream given in
 *  <i>other_IO</i> or to a new stream opened on <i>path</i>. This may
 *  dynamically change the actual class of this stream.
 *
 *     f1 = File.new("testfile")
 *     f2 = File.new("testfile")
 *     f2.readlines[0]   #=> "This is line one\n"
 *     f2.reopen(f1)     #=> #<File:testfile>
 *     f2.readlines[0]   #=> "This is line one\n"
 */

static void
io_replace_streams(int fd, rb_io_t *dest, rb_io_t *origin)
{
    prepare_io_from_fd(dest, fd, rb_io_calculate_mode_flags(origin));

    if (origin->buf != NULL) {
	CFMutableDataRef data = CFDataCreateMutableCopy(NULL, 0, origin->buf);
	GC_WB(&dest->buf, data);
	CFMakeCollectable(data);
    }
    else {
	dest->buf = NULL;
    }
    dest->buf_offset = origin->buf_offset;
}

static VALUE
io_reopen(VALUE io, VALUE nfile)
{
    // Reassociate it with a duplicate of the stream given
    nfile = rb_io_get_io(nfile);

    rb_io_t *io_s  = ExtractIOStruct(io);
    rb_io_t *other = ExtractIOStruct(nfile);
    rb_io_assert_open(io_s);
    rb_io_assert_open(other);
    if (io_s == other) {
	return io;
    }

    if (io_s->fd == -1) {
	rb_raise(rb_eRuntimeError,
		 "cannot reopen non file descriptor based IO");
    }
    if (other->fd == -1) {
	rb_raise(rb_eRuntimeError,
		 "cannot reopen from non file descriptor based IO");
    }

    int fd = io_s->fd;
    io_close(io_s, true, true);
    fd = dup2(other->fd, fd);
    if (fd < 0) {
	rb_sys_fail("dup2() failed");
    }
    io_replace_streams(fd, io_s, other);

    *(VALUE *)io = *(VALUE *)nfile;

    return io;
}

static VALUE
rb_io_reopen(VALUE io, SEL sel, int argc, VALUE *argv)
{
    rb_secure(4);
	
    VALUE path_or_io, mode_string;
    if (rb_scan_args(argc, argv, "11", &path_or_io, &mode_string) == 1) {
	VALUE tmp = rb_io_check_io(path_or_io);
	if (!NIL_P(tmp)) {
	    return io_reopen(io, tmp);
	}
    }

    rb_io_t *io_s = ExtractIOStruct(io);
    rb_io_assert_open(io_s);

    // Reassociate it with the stream opened on the given path
    if (NIL_P(mode_string)) {
	mode_string = (VALUE)CFSTR("r");
    }
    FilePathValue(path_or_io); // Sanitize the name
    const char *filepath = RSTRING_PTR(path_or_io);
    const int fd =
	open(filepath, convert_mode_string_to_oflags(mode_string), 0644);
    prepare_io_from_fd(io_s, fd,
		       convert_mode_string_to_fmode(mode_string));
    GC_WB(&io_s->path, path_or_io);
    io_s->buf = NULL;
    io_s->buf_offset = 0;

    return io;
}

/* :nodoc: */
static VALUE
rb_io_init_copy(VALUE dest, SEL sel, VALUE origin)
{
    origin = rb_io_get_io(origin);
    rb_io_t *dest_io = ExtractIOStruct(dest);
    rb_io_t *origin_io = ExtractIOStruct(origin);

    rb_io_check_closed(origin_io);
    if (dest_io == origin_io) {
	return dest;
    }

    if (origin_io->fd == -1) {
	rb_raise(rb_eRuntimeError,
		"cannot copy from non file descriptor based IO");
    }

    const int fd = dup(origin_io->fd);
    if (fd < 0) {
	rb_sys_fail("dup() failed");
    }
    io_replace_streams(fd, dest_io, origin_io);

    return dest;
}

/*
 *  call-seq:
 *     ios.printf(format_string [, obj, ...] )   => nil
 *
 *  Formats and writes to <em>ios</em>, converting parameters under
 *  control of the format string. See <code>Kernel#sprintf</code>
 *  for details.
 */

VALUE
rb_io_printf(VALUE out, SEL sel, int argc, VALUE *argv)
{
    return rb_io_write(out, rb_f_sprintf(argc, argv));
}

/*
 *  call-seq:
 *     printf(io, string [, obj ... ] )    => nil
 *     printf(string [, obj ... ] )        => nil
 *
 *  Equivalent to:
 *     io.write(sprintf(string, obj, ...)
 *  or
 *     $stdout.write(sprintf(string, obj, ...)
 */

static VALUE
rb_f_printf(VALUE klass, SEL sel, int argc, VALUE *argv)
{
    VALUE io;
    if (argc == 0) {
        return Qnil;
    }
    
    if (TYPE(argv[0]) == T_STRING) {
        io = rb_stdout;
    } 
    else {
        io = argv[0];
        argv++;
        argc--;
    }
    
    return rb_io_printf(io, sel, argc, argv);
}

/*
 *  call-seq:
 *     ios.print()             => nil
 *     ios.print(obj, ...)     => nil
 *
 *  Writes the given object(s) to <em>ios</em>. The stream must be
 *  opened for writing. If the output record separator (<code>$\\</code>)
 *  is not <code>nil</code>, it will be appended to the output. If no
 *  arguments are given, prints <code>$_</code>. Objects that aren't
 *  strings will be converted by calling their <code>to_s</code> method.
 *  With no argument, prints the contents of the variable <code>$_</code>.
 *  Returns <code>nil</code>.
 *
 *     $stdout.print("This is ", 100, " percent.\n")
 *
 *  <em>produces:</em>
 *
 *     This is 100 percent.
 */

VALUE
rb_io_print(VALUE io, SEL sel, int argc, VALUE *argv)
{
    VALUE line;
    if (argc == 0) {
        argc = 1;
        line = rb_lastline_get();
        argv = &line;
    }
    while (argc--) {
        rb_io_write(io, *argv++);
        if (!NIL_P(rb_output_fs)) {
            rb_io_write(io, rb_output_fs);
        }
    }
    if (!NIL_P(rb_output_rs)) {
        rb_io_write(io, rb_output_rs);
    }
    return Qnil;
}

/*
 *  call-seq:
 *     print(obj, ...)    => nil
 *
 *  Prints each object in turn to <code>$stdout</code>. If the output
 *  field separator (<code>$,</code>) is not +nil+, its
 *  contents will appear between each field. If the output record
 *  separator (<code>$\\</code>) is not +nil+, it will be
 *  appended to the output. If no arguments are given, prints
 *  <code>$_</code>. Objects that aren't strings will be converted by
 *  calling their <code>to_s</code> method.
 *
 *     print "cat", [1,2,3], 99, "\n"
 *     $, = ", "
 *     $\ = "\n"
 *     print "cat", [1,2,3], 99
 *
 *  <em>produces:</em>
 *
 *     cat12399
 *     cat, 1, 2, 3, 99
 */

static VALUE
rb_f_print(VALUE recv, SEL sel, int argc, VALUE *argv)
{
    rb_io_print(rb_stdout, 0, argc, argv);
    return Qnil;
}

/*
 *  call-seq:
 *     ios.putc(obj)    => obj
 *
 *  If <i>obj</i> is <code>Numeric</code>, write the character whose
 *  code is <i>obj</i>, otherwise write the first character of the
 *  string representation of  <i>obj</i> to <em>ios</em>.
 *
 *     $stdout.putc "A"
 *     $stdout.putc 65
 *
 *  <em>produces:</em>
 *
 *     AA
 */

static VALUE
rb_io_putc(VALUE io, SEL sel, VALUE ch)
{
    char c = NUM2CHR(ch);
    rb_io_write(io, rb_str_new(&c, 1));
    return ch;
}

/*
 *  call-seq:
 *     putc(int)   => int
 *
 *  Equivalent to:
 *
 *    $stdout.putc(int)
 */

static VALUE
rb_f_putc(VALUE recv, SEL sel, VALUE ch)
{
    return rb_io_putc(rb_stdout, sel, ch);
}

/*
 *  call-seq:
 *     ios.puts(obj, ...)    => nil
 *
 *  Writes the given objects to <em>ios</em> as with
 *  <code>IO#print</code>. Writes a record separator (typically a
 *  newline) after any that do not already end with a newline sequence.
 *  If called with an array argument, writes each element on a new line.
 *  If called without arguments, outputs a single record separator.
 *
 *     $stdout.puts("this", "is", "a", "test")
 *
 *  <em>produces:</em>
 *
 *     this
 *     is
 *     a
 *     test
 */
 
VALUE rb_io_puts(VALUE out, SEL sel, int argc, VALUE *argv);
 
static VALUE
io_puts_ary(VALUE ary, VALUE out, int recur)
{
    VALUE tmp;
    long i, count;

    if (recur) {
	tmp = rb_str_new2("[...]");
	rb_io_puts(out, 0, 1, &tmp);
	return Qnil;
    }
    for (i = 0, count = RARRAY_LEN(ary); i < count; i++) {
	tmp = RARRAY_AT(ary, i);
	rb_io_puts(out, 0, 1, &tmp);
    }
    return Qnil;
}

VALUE
rb_io_puts(VALUE out, SEL sel, int argc, VALUE *argv)
{
    VALUE line;
    int i;
    if (argc == 0) {
        rb_io_write(out, rb_default_rs);
        return Qnil;
    }
    for (i = 0; i < argc; i++) {
        line = rb_check_array_type(argv[i]);
        if (!NIL_P(line)) {
            rb_exec_recursive(io_puts_ary, line, out);
            continue;
        }
        line = rb_obj_as_string(argv[i]);
        rb_io_write(out, line);
        if (RSTRING_LEN(line) == 0
		|| RSTRING_PTR(line)[RSTRING_LEN(line)-1] != '\n') {
            // If the last character of line was a newline, there's no reason
	    // to write another.
            rb_io_write(out, rb_default_rs);
        }
    }
    return Qnil;
}

/*
 *  call-seq:
 *     puts(obj, ...)    => nil
 *
 *  Equivalent to
 *
 *      $stdout.puts(obj, ...)
 */

static VALUE
rb_f_puts(VALUE recv, SEL sel, int argc, VALUE *argv)
{
    return rb_io_puts(rb_stdout, sel, argc, argv);
}

void
rb_p(VALUE obj) /* for debug print within C code */
{
    rb_io_write(rb_stdout, rb_obj_as_string(rb_inspect(obj)));
    rb_io_write(rb_stdout, rb_default_rs);
}

VALUE
rb_io_write(VALUE v, VALUE i)
{
    return rb_funcall(v, id_write, 1, i);
}

/*
 *  call-seq:
 *     p(obj)              => obj
 *     p(obj1, obj2, ...)  => [obj, ...]
 *     p()                 => nil
 *
 *  For each object, directly writes
 *  _obj_.+inspect+ followed by the current output
 *  record separator to the program's standard output.
 *
 *     S = Struct.new(:name, :state)
 *     s = S['dave', 'TX']
 *     p s
 *
 *  <em>produces:</em>
 *
 *     #<S name="dave", state="TX">
 */

static VALUE
rb_f_p(VALUE self, SEL sel, int argc, VALUE *argv)
{
    int i;
    VALUE ret = Qnil;

    for (i = 0; i < argc; i++) {
	rb_p(argv[i]);
    }
    if (argc == 1) {
	ret = argv[0];
    }
    else if (argc > 1) {
	ret = rb_ary_new4(argc, argv);
    }

    return ret;
}

/*
 *  call-seq:
 *     obj.display(port=$>)    => nil
 *
 *  Prints <i>obj</i> on the given port (default <code>$></code>).
 *  Equivalent to:
 *
 *     def display(port=$>)
 *       port.write self
 *     end
 *
 *  For example:
 *
 *     1.display
 *     "cat".display
 *     [ 4, 5, 6 ].display
 *     puts
 *
 *  <em>produces:</em>
 *
 *     1cat456
 */
 
// TODO: The output doesn't match the documentation here; why is that?

static VALUE
rb_obj_display(VALUE self, SEL sel, int argc, VALUE *argv)
{
    VALUE port;
    rb_scan_args(argc, argv, "01", &port);
    if (NIL_P(port)) {
	port = rb_stdout;
    }
    return rb_io_write(port, self);
}

// static void
// must_respond_to(ID mid, VALUE val, ID id)
// {
//     if (!rb_respond_to(val, mid)) {
//  rb_raise(rb_eTypeError, "%s must have %s method, %s given",
//       rb_id2name(id), rb_id2name(mid),
//       rb_obj_classname(val));
//     }
// }

// TODO: Fix this; the documentation is wrong.

/*
 *  call-seq:
 *     IO.new(fd, mode)   => io
 *
 *  Returns a new <code>IO</code> object (a stream) for the given
 *  <code>IO</code> object or integer file descriptor and mode
 *  string. See also <code>IO#fileno</code> and
 *  <code>IO::for_fd</code>.
 *
 *     puts IO.new($stdout).fileno # => 1
 *
 *     a = IO.new(2,"w")      # '2' is standard error
 *     $stderr.puts "Hello"
 *     a.puts "World"
 *
 *  <em>produces:</em>
 *
 *     Hello
 *     World
 */

/*
 *  call-seq:
 *     IO.for_fd(fd, mode)    => io
 *
 *  Synonym for <code>IO::new</code>.
 *
 */

static VALUE
rb_io_initialize(VALUE io, SEL sel, int argc, VALUE *argv)
{
    rb_secure(4);
	
    VALUE file_descriptor, mode;
    int mode_flags, fd;
    struct stat st;

    // TODO handle optional hash
    /*VALUE opt =*/ pop_last_hash(&argc, argv);

    rb_scan_args(argc, argv, "11", &file_descriptor, &mode);

    rb_io_t *io_struct = ExtractIOStruct(io);
    file_descriptor = rb_check_to_integer(file_descriptor, "to_int");
    if (NIL_P(file_descriptor)) {
	rb_raise(rb_eTypeError, "can't convert %s into Integer",
		rb_obj_classname(file_descriptor));
    }
    fd = FIX2INT(file_descriptor);

    if (fstat(fd, &st) < 0) {
	rb_sys_fail(0);
    }

    if (NIL_P(mode)) {
	mode_flags = FMODE_READABLE;
    }
    else if (TYPE(mode) == T_STRING) {
	mode_flags = convert_mode_string_to_fmode(mode);
    }
    else {
	mode_flags = NUM2INT(mode);
    }

    prepare_io_from_fd(io_struct, fd, mode_flags);
    return io;
}

static IMP rb_objc_io_finalize_super = NULL; 

static void
rb_objc_io_finalize(void *rcv, SEL sel)
{
    rb_io_t *io_struct = ExtractIOStruct((VALUE)rcv);
    // do not close automatically stdin, stdout and stderr
    if ((io_struct->fd < 0) || (io_struct->fd > 2)) {
	rb_io_close((VALUE)rcv);
	if (rb_objc_io_finalize_super != NULL) {
	    ((void(*)(void *, SEL))rb_objc_io_finalize_super)(rcv, sel);
	}
    }
}

/*
 *  call-seq:
 *     File.new(filename, mode="r")            => file
 *     File.new(filename [, mode [, perm]])    => file
 *
 *  Opens the file named by _filename_ according to
 *  _mode_ (default is ``r'') and returns a new
 *  <code>File</code> object. See the description of class +IO+ for
 *  a description of _mode_. The file mode may optionally be
 *  specified as a +Fixnum+ by _or_-ing together the
 *  flags (O_RDONLY etc, again described under +IO+). Optional
 *  permission bits may be given in _perm_. These mode and permission
 *  bits are platform dependent; on Unix systems, see
 *  <code>open(2)</code> for details.
 *
 *     f = File.new("testfile", "r")
 *     f = File.new("newfile",  "w+")
 *     f = File.new("newfile", File::CREAT|File::TRUNC|File::RDWR, 0644)
 */

static VALUE
rb_file_initialize(VALUE io, SEL sel, int argc, VALUE *argv)
{
    if (0 < argc && argc < 3) {
	VALUE fd = rb_check_convert_type(argv[0], T_FIXNUM, "Fixnum", "to_int");

	if (!NIL_P(fd)) {
	    argv[0] = fd;
	    return rb_io_initialize(io, sel, argc, argv);
	}
    }
    return rb_file_open(io, argc, argv);
}

/*
 *  call-seq:
 *     IO.new(fd, mode_string)   => io
 *
 *  Returns a new <code>IO</code> object (a stream) for the given
 *  integer file descriptor and mode string. See also
 *  <code>IO#fileno</code> and <code>IO::for_fd</code>.
 *
 *     a = IO.new(2,"w")      # '2' is standard error
 *     $stderr.puts "Hello"
 *     a.puts "World"
 *
 *  <em>produces:</em>
 *
 *     Hello
 *     World
 */

static VALUE
rb_io_s_new0(VALUE klass, int argc, VALUE *argv)
{
    return rb_class_new_instance(argc, argv, klass);
}

static VALUE
rb_io_s_new(VALUE klass, SEL sel, int argc, VALUE *argv)
{
    if (rb_block_given_p()) {
	VALUE k = klass;
	bool is_io = true;
	while (k != 0) {
	    if (k == rb_cFile) {
		is_io = false;
		break;
	    }
	    k = RCLASS_SUPER(k);
	}
	if (is_io) {
	    const char *cname = rb_class2name(klass);

	    rb_warn("%s::new() does not take block; use %s::open() instead",
		    cname, cname);
	}
    }
    return rb_io_s_new0(klass, argc, argv);
}

static void
argf_init(struct argf *p, VALUE v)
{
    p->filename = Qnil;
    p->current_file = Qnil;
    p->lineno = Qnil;
    GC_WB(&p->argv, v);
}

static VALUE
argf_alloc(VALUE klass)
{
    struct argf *p;

    VALUE argf = Data_Make_Struct(klass, struct argf, NULL, NULL, p);
    argf_init(p, Qnil);

    return argf;
}

#undef rb_argv

static VALUE
argf_initialize(VALUE argf, SEL sel, VALUE argv)
{
    memset(&ARGF, 0, sizeof(ARGF));
    argf_init(&ARGF, argv);

    return argf;
}

static VALUE
argf_initialize_copy(VALUE argf, SEL sel, VALUE orig)
{
    ARGF = argf_of(orig);
    GC_WB(&(ARGF.argv), rb_obj_dup(ARGF.argv));
    if (ARGF.inplace) {
	const char *inplace = ARGF.inplace;
	ARGF.inplace = 0;
	GC_WB(&ARGF.inplace, ruby_strdup(inplace));
    }
    return argf;
}

static VALUE
argf_set_lineno(VALUE argf, SEL sel, VALUE val)
{
    ARGF.gets_lineno = NUM2INT(val);
    ARGF.lineno = INT2FIX(ARGF.gets_lineno);
    return Qnil;
}

static VALUE
argf_lineno(VALUE argf, SEL sel)
{
    return ARGF.lineno;
}

static VALUE
argf_forward(VALUE argf, SEL sel, int argc, VALUE *argv)
{
    // TODO
    // return rb_funcall3(ARGF. current_file, rb_frame_this_func(), argc, argv);
    rb_notimplement();
}

#define next_argv() argf_next_argv(argf)
#define ARGF_GENERIC_INPUT_P() \
    (ARGF.current_file == rb_stdin && TYPE(ARGF.current_file) != T_FILE)

#define ARGF_FORWARD(argc, argv) do {\
    if (ARGF_GENERIC_INPUT_P())\
	return argf_forward(argf, 0, argc, argv);\
} while (0)

#define NEXT_ARGF_FORWARD(argc, argv) do {\
    if (!next_argv()) return Qnil;\
    ARGF_FORWARD(argc, argv);\
} while (0)

static void
argf_close(VALUE file, SEL sel)
{
    rb_funcall3(file, rb_intern("close"), 0, 0);
}

static int
argf_next_argv(VALUE argf)
{
    rb_io_t *io = NULL;
    char *fn = NULL;
    if (TYPE(rb_stdout) == T_FILE) {
        io = ExtractIOStruct(rb_stdout);
    }
    if (ARGF.init_p == 0) {
	if (!NIL_P(ARGF.argv) && RARRAY_LEN(ARGF.argv) > 0) {
	    ARGF.next_p = 1;
	}
	else {
	    ARGF.next_p = -1;
	}
	ARGF.init_p = 1;
	ARGF.gets_lineno = 0;
    }
    if (ARGF.next_p == 1) {
	// we need to shift ARGV and read it into ARGF.
	ARGF.next_p = 0;
retry:  
	if (RARRAY_LEN(ARGF.argv) > 0) {
	    GC_WB(&ARGF.filename, rb_ary_shift(ARGF.argv));
	    fn = StringValueCStr(ARGF.filename);
	    if (strlen(fn) == 1 && fn[0] == '-') {
		// - means read from standard input, obviously.
		GC_WB(&ARGF.current_file, rb_stdin);
		if (ARGF.inplace) {
		    rb_warn("Can't do inplace edit for stdio; skipping");
		    goto retry;
		}
	    }
	    else {
		int fr = open(fn, O_RDONLY);
		if (ARGF.inplace) {
		    // we need to rename and create new files for inplace mode
		    struct stat st, st2;
		    VALUE str;
		    int fw;
		    if (TYPE(rb_stdout) == T_FILE && rb_stdout != orig_stdout) {
			rb_io_close(rb_stdout);
		    }
		    fstat(fr, &st);
		    if (*ARGF.inplace) {
			// AFAICT, we create a new string here because we need to modify it
			// and we don't want to mess around with ARGF.filename.
			str = rb_str_new2(fn);
			rb_str_cat2(str, ARGF.inplace);
			if (rename(fn, RSTRING_PTR(str)) < 0) {
			    rb_warn("Can't rename %s to %s: %s, skipping file",
				   fn, RSTRING_PTR(str), strerror(errno));
			    close(fr);
			    goto retry;
			}
		    } 
		    else {
			if (unlink(fn) < 0) {
			    rb_warn("Can't remove %s: %s, skipping file",
				    fn, strerror(errno));
			    close(fr);
			    goto retry;
			}
		    }
		    fw = open(fn, O_WRONLY|O_CREAT|O_TRUNC, 0666);
		    fstat(fw, &st2); // pull out its filestats
		    fchmod(fw, st.st_mode); // copy the permissions
		    if ((st.st_uid != st2.st_uid) || (st.st_gid != st2.st_gid)) {
			// copy the groups and owners
			fchown(fw, st.st_uid, st.st_gid);
		    }
		    rb_stdout = prep_io(fw, FMODE_WRITABLE, rb_cFile);
		}
		GC_WB(&ARGF.current_file, prep_io(fr, FMODE_READABLE, rb_cFile));
	    }
#if 0 // TODO once we get encodings sorted out.
	    if (ARGF.encs.enc) {
		rb_io_t *fptr;
		GetOpenFile(ARGF.current_file, fptr);
		fptr->encs = ARGF.encs;
		clear_codeconv(fptr);
	    }
#endif
	}
	else {
	    ARGF.next_p = 1;
	    return Qfalse;
	}
    }
    else if (ARGF.next_p == -1) {
	GC_WB(&ARGF.current_file, rb_stdin);
	GC_WB(&ARGF.filename, rb_str_new2("-"));
    }
    return Qtrue;
}

static VALUE
argf_getline(VALUE argf, SEL sel, int argc, VALUE *argv)
{
    VALUE line;

retry:
    if (!next_argv()) {
	return Qnil;
    }
    if (ARGF_GENERIC_INPUT_P()) {
	line = rb_funcall3(ARGF.current_file, rb_intern("gets"), argc, argv);
    }
    else {
	if (argc == 0 && rb_rs == rb_default_rs) {
	    line = rb_io_gets(ARGF.current_file, 0);
	}
	else {
	    line = rb_io_getline(argc, argv, ARGF.current_file);
	}
	if (NIL_P(line) && ARGF.next_p != -1) {
	    argf_close(ARGF.current_file, 0);
	    ARGF.next_p = 1;
	    goto retry;
	}
    }
    if (!NIL_P(line)) {
	(ARGF.gets_lineno)++;
	ARGF.lineno = INT2FIX(ARGF.gets_lineno);
    }
    return line;
}

static VALUE
argf_lineno_getter(ID id, VALUE *var)
{
    VALUE argf = *var;
    return ARGF.lineno;
}

static void
argf_lineno_setter(VALUE val, ID id, VALUE *var)
{
    VALUE argf = *var;
    int n = NUM2INT(val);
    ARGF.gets_lineno = n;
    ARGF.lineno = INT2FIX(n);    
}

/*
 *  call-seq:
 *     gets(sep=$/)    => string or nil
 *     gets(limit)     => string or nil
 *     gets(sep,limit) => string or nil
 *
 *  Returns (and assigns to <code>$_</code>) the next line from the list
 *  of files in +ARGV+ (or <code>$*</code>), or from standard input if
 *  no files are present on the command line. Returns +nil+ at end of
 *  file. The optional argument specifies the record separator. The
 *  separator is included with the contents of each record. A separator
 *  of +nil+ reads the entire contents, and a zero-length separator
 *  reads the input one paragraph at a time, where paragraphs are
 *  divided by two consecutive newlines.  If the first argument is an
 *  integer, or optional second argument is given, the returning string
 *  would not be longer than the given value.  If multiple filenames are
 *  present in +ARGV+, +gets(nil)+ will read the contents one file at a
 *  time.
 *
 *     ARGV << "testfile"
 *     print while gets
 *
 *  <em>produces:</em>
 *
 *     This is line one
 *     This is line two
 *     This is line three
 *     And so on...
 *
 *  The style of programming using <code>$_</code> as an implicit
 *  parameter is gradually losing favor in the Ruby community.
 */

static VALUE argf_gets(VALUE, SEL, int, VALUE *);

static VALUE
rb_f_gets(VALUE recv, SEL sel, int argc, VALUE *argv)
{
    if (recv == argf) {
	return argf_gets(argf, 0, argc, argv);
    }
    return rb_funcall2(argf, rb_intern("gets"), argc, argv);
}

static VALUE
argf_gets(VALUE recv, SEL sel, int argc, VALUE *argv)
{
    VALUE line;

    line = argf_getline(argf, 0, argc, argv);
    rb_lastline_set(line);
    return line;
}

/*
 *  call-seq:
 *     readline(sep=$/)     => string
 *     readline(limit)      => string
 *     readline(sep, limit) => string
 *
 *  Equivalent to <code>Kernel::gets</code>, except
 *  +readline+ raises +EOFError+ at end of file.
 */

static VALUE
argf_readline(VALUE rcv, SEL sel, int argc, VALUE *argv)
{
    VALUE line;

    if (!next_argv()) {
	rb_eof_error();
    }
    ARGF_FORWARD(argc, argv);
    line = argf_gets(rcv, sel, argc, argv);
    if (NIL_P(line)) {
	rb_eof_error();
    }

    return line;
}

static VALUE
rb_f_readline(VALUE recv, SEL sel, int argc, VALUE *argv)
{
    return argf_readline(recv, sel, argc, argv);
}

static VALUE rb_io_s_readlines(VALUE recv, SEL sel, int argc, VALUE *argv);

/*
 *  call-seq:
 *     readlines(sep=$/)    => array
 *     readlines(limit)     => array
 *     readlines(sep,limit) => array
 *
 *  Returns an array containing the lines returned by calling
 *  <code>Kernel.gets(<i>sep</i>)</code> until the end of file.
 */

static VALUE
argf_readlines(VALUE rcv, SEL sel, int argc, VALUE *argv)
{
    VALUE lines = rb_ary_new();
    while (true) {
	VALUE line = argf_getline(argf, sel, argc, argv);
	if (NIL_P(line)) {
	    break;
	}
	rb_ary_push(lines, line);
    }

    return lines;
}

static VALUE
rb_f_readlines(VALUE rcv, SEL sel, int argc, VALUE *argv)
{
    return argf_readlines(rcv, sel, argc, argv);
}

/*
 *  call-seq:
 *     IO.try_convert(obj) -> io or nil
 *
 *  Try to convert <i>obj</i> into an IO, using to_io method.
 *  Returns converted IO or nil if <i>obj</i> cannot be converted
 *  for any reason.
 *
 *     IO.try_convert(STDOUT)     # => STDOUT
 *     IO.try_convert("STDOUT")   # => nil
 */
static VALUE
rb_io_s_try_convert(VALUE dummy, SEL sel, VALUE obj)
{
    return rb_io_check_io(obj);
}

/*
 *  call-seq:
 *     `cmd`    => string
 *
 *  Returns the standard output of running _cmd_ in a subshell.
 *  The built-in syntax <code>%x{...}</code> uses
 *  this method. Sets <code>$?</code> to the process status.
 *
 *     `date`                   #=> "Wed Apr  9 08:56:30 CDT 2003\n"
 *     `ls testdir`.split[1]    #=> "main.rb"
 *     `echo oops && exit 99`   #=> "oops\n"
 *     $?.exitstatus            #=> 99
 */

static VALUE
rb_f_backquote(VALUE obj, SEL sel, VALUE str)
{
    VALUE io = rb_io_s_popen(rb_cIO, 0, 1, &str);
    rb_io_t *io_s = ExtractIOStruct(io);

    VALUE outbuf = rb_bstr_new();
    rb_io_read_all(ExtractIOStruct(io), outbuf);

    assert(io_s->pid != -1);
    int status;
    if (waitpid(io_s->pid, &status, 0) != -1) {
	rb_last_status_set(status, io_s->pid);
	io_s->pid = -1;
    }

    rb_io_close(io);

    rb_str_force_encoding(outbuf, rb_encodings[ENCODING_UTF8]);
    return outbuf;
}

/*
 *  call-seq:
 *     IO.select(read_array
 *               [, write_array
 *               [, error_array
 *               [, timeout]]] ) =>  array  or  nil
 *
 *  See <code>Kernel#select</code>.
 */

// helper method. returns the highest-valued file descriptor encountered.
static int
build_fd_set_from_io_array(fd_set* set, VALUE arr)
{
    int max_fd = 0;
    FD_ZERO(set);
    if (!NIL_P(arr)) {
	if (TYPE(arr) != T_ARRAY) {
	    rb_raise(rb_eTypeError, "Kernel#select expects arrays of IO objects");
	}

	long n = RARRAY_LEN(arr);
	long ii;
	for (ii = 0; ii < n; ii++) {
	    VALUE io = RARRAY_AT(arr, ii);
	    io = rb_check_convert_type(io, T_FILE, "IO", "to_io");
	    if (NIL_P(io)) {
		rb_raise(rb_eTypeError, "Kernel#select expects arrays of IO objects");
	    }
	    int fd = ExtractIOStruct(io)->fd;
	    FD_SET(fd, set);
	    max_fd = MAX(fd, max_fd);
	}
    }
    return max_fd;
}

static void
build_timeval_from_numeric(struct timeval *tv, VALUE num)
{
    tv->tv_sec = 0L;
    tv->tv_usec = 0L;
    if (FIXNUM_P(num)) {
	if (FIX2LONG(num) < 0) {
	    rb_raise(rb_eArgError, "select() does not accept negative timeouts.");
	}
	tv->tv_sec = FIX2LONG(num);
    }
    else if (FIXFLOAT_P(num)) {
	double quantity = RFLOAT_VALUE(num);
	if (quantity < 0.0) {
	    rb_raise(rb_eArgError, "select() does not accept negative timeouts.");
	}
	tv->tv_sec = (long)floor(quantity);
	tv->tv_usec = (long)(1000 * (quantity - floor(quantity)));
    }
    else if (!NIL_P(num)) {
	rb_raise(rb_eTypeError, "timeout parameter must be numeric.");
    }
}

static VALUE
extract_ready_descriptors(VALUE arr, fd_set* set)
{
    VALUE ready_ios = rb_ary_new();
    if (NIL_P(arr)) {
	return ready_ios;
    }
    long len = RARRAY_LEN(arr);
    long ii;
    for (ii = 0; ii < len; ii++) {
	VALUE io = RARRAY_AT(arr, ii);
	VALUE tmp = rb_check_convert_type(io, T_FILE, "IO", "to_io");
	if (FD_ISSET(ExtractIOStruct(tmp)->fd, set)) {
	    rb_ary_push(ready_ios, io);
	}
    }
    return ready_ios;
}

static VALUE
rb_f_select(VALUE recv, SEL sel, int argc, VALUE *argv)
{
    VALUE read_arr, write_arr, err_arr, timeout_val;
    VALUE readable_arr, writable_arr, errored_arr;
    rb_scan_args(argc, argv, "13", &read_arr, &write_arr, &err_arr, &timeout_val);
    fd_set read_set, write_set, err_set;
    struct timeval timeout;

    // ndfs has to be 1 + the highest fd we're looking for.
    int temp = 0;
    int ndfs = build_fd_set_from_io_array(&read_set, read_arr);
    temp = build_fd_set_from_io_array(&write_set, write_arr);
    ndfs = MAX(temp, ndfs);
    temp = build_fd_set_from_io_array(&err_set, err_arr);
    ndfs = MAX(temp, ndfs);
    ndfs++;

    // A timeval of 0 needs to be distinguished from a NULL timeval, as a 
    // NULL timeval polls indefinitly while a 0 timeval returns immediately.
    build_timeval_from_numeric(&timeout, timeout_val);
    struct timeval *timeval_ptr = (NIL_P(timeout_val) ? NULL : &timeout);

    int ready_count = select(ndfs, &read_set, &write_set, &err_set, timeval_ptr);
    if (ready_count == -1) {
	rb_sys_fail("select(2) failed");
    }
    else if (ready_count == 0) {
	return Qnil; // no ready file descriptors? return 0.
    }

    readable_arr = extract_ready_descriptors(read_arr, &read_set);
    writable_arr = extract_ready_descriptors(write_arr, &write_set);
    errored_arr = extract_ready_descriptors(err_arr, &err_set);

    return rb_ary_new3(3, readable_arr, writable_arr, errored_arr);
}

// Here be dragons.
static VALUE
rb_io_ctl(VALUE io, VALUE arg, VALUE req, int is_io)
{
    rb_secure(2);
	
    unsigned long cmd;
    if (arg == Qnil || arg == Qfalse) {
	cmd = 0;
    }
    else if (FIXNUM_P(arg)) {
	cmd = FIX2LONG(arg);
    }
    else if (arg == Qtrue) {
	cmd = 1;
    }
    else {
	// TODO arg may be a string
	cmd = NUM2LONG(arg);
    }

    unsigned long request;
    rb_io_t *io_s = ExtractIOStruct(io);
    if (TYPE(req) == T_STRING) {
	request = (unsigned long)(intptr_t)RSTRING_PTR(req);
    }
    else {
	request = FIX2ULONG(req);
    }
    int retval = is_io ? ioctl(io_s->fd, cmd, request)
	: fcntl(io_s->fd, cmd, request);
    return INT2FIX(retval);
}

/*
 *  call-seq:
 *     ios.ioctl(integer_cmd, arg)    => integer
 *
 *  Provides a mechanism for issuing low-level commands to control or
 *  query I/O devices. Arguments and results are platform dependent. If
 *  <i>arg</i> is a number, its value is passed directly. If it is a
 *  string, it is interpreted as a binary sequence of bytes. On Unix
 *  platforms, see <code>ioctl(2)</code> for details. Not implemented on
 *  all platforms.
 */

static VALUE
rb_io_ioctl(VALUE recv, SEL sel, int argc, VALUE *argv)
{
    VALUE integer_cmd, arg;
    rb_scan_args(argc, argv, "11", &integer_cmd, &arg);

    rb_io_assert_open(ExtractIOStruct(recv));
    return rb_io_ctl(recv, integer_cmd, arg, 1);
}

/*
 *  call-seq:
 *     ios.fcntl(integer_cmd, arg)    => integer
 *
 *  Provides a mechanism for issuing low-level commands to control or
 *  query file-oriented I/O streams. Arguments and results are platform
 *  dependent. If <i>arg</i> is a number, its value is passed
 *  directly. If it is a string, it is interpreted as a binary sequence
 *  of bytes (<code>Array#pack</code> might be a useful way to build this
 *  string). On Unix platforms, see <code>fcntl(2)</code> for details.
 *  Not implemented on all platforms.
 */

static VALUE
rb_io_fcntl(VALUE recv, SEL sel, int argc, VALUE *argv)
{
    VALUE integer_cmd, arg;
    rb_scan_args(argc, argv, "11", &integer_cmd, &arg);

    rb_io_assert_open(ExtractIOStruct(recv));
    return rb_io_ctl(recv, integer_cmd, arg, 0);
}

/*
 *  call-seq:
 *     syscall(fixnum [, args...])   => integer
 *
 *  Calls the operating system function identified by _fixnum_,
 *  passing in the arguments, which must be either +String+
 *  objects, or +Integer+ objects that ultimately fit within
 *  a native +long+. Up to nine parameters may be passed (14
 *  on the Atari-ST). The function identified by _fixnum_ is system
 *  dependent. On some Unix systems, the numbers may be obtained from a
 *  header file called <code>syscall.h</code>.
 *
 *     syscall 4, 1, "hello\n", 6   # '4' is write(2) on our box
 *
 *  <em>produces:</em>
 *
 *     hello
 */

static VALUE
rb_f_syscall(VALUE recv, SEL sel, int argc, VALUE *argv)
{
    unsigned long arg[8];

    int ii = 1;
    int retval = -1;
    int items = argc - 1;

    rb_secure(2);
    if (argc == 0) {
	rb_raise(rb_eArgError, "too few arguments to syscall()");
    }
    if (argc > 9) {
	rb_raise(rb_eArgError, "too many arguments to syscall()");
    }

    arg[0] = NUM2LONG(argv[0]); argv++;

    while (items--) {
	VALUE v = rb_check_string_type(*argv);
	if (!NIL_P(v)) {
	    StringValue(v);
	    arg[ii] = (unsigned long)StringValueCStr(v);
	}
	else {
	    arg[ii] = (unsigned long)NUM2LONG(*argv);
	}
	argv++;
	ii++;
    }

    switch (argc) {
	case 1:
	    retval = syscall(arg[0]);
	    break;
	case 2:
	    retval = syscall(arg[0],arg[1]);
	    break;
	case 3:
	    retval = syscall(arg[0],arg[1],arg[2]);
	    break;
	case 4:
	    retval = syscall(arg[0],arg[1],arg[2],arg[3]);
	    break;
	case 5:
	    retval = syscall(arg[0],arg[1],arg[2],arg[3],arg[4]);
	    break;
	case 6:
	    retval = syscall(arg[0],arg[1],arg[2],arg[3],arg[4],arg[5]);
	    break;
	case 7:
	    retval = syscall(arg[0],arg[1],arg[2],arg[3],arg[4],arg[5],arg[6]);
	    break;
	case 8:
	    retval = syscall(arg[0],arg[1],arg[2],arg[3],arg[4],arg[5],arg[6],
		    arg[7]);
	    break;
    }

    if (retval < 0) {
	rb_sys_fail("call to syscall() failed.");
    }
    return INT2NUM(retval);
}
/*
 *  call-seq:
 *     IO.pipe                    -> [read_io, write_io]
 *     IO.pipe(ext_enc)           -> [read_io, write_io]
 *     IO.pipe("ext_enc:int_enc") -> [read_io, write_io]
 *     IO.pipe(ext_enc, int_enc)  -> [read_io, write_io]
 *
 *  Creates a pair of pipe endpoints (connected to each other) and
 *  returns them as a two-element array of <code>IO</code> objects:
 *  <code>[</code> <i>read_io</i>, <i>write_io</i> <code>]</code>. Not
 *  available on all platforms.
 *
 *  If an encoding (encoding name or encoding object) is specified as an optional argument,
 *  read string from pipe is tagged with the encoding specified.
 *  If the argument is a colon separated two encoding names "A:B",
 *  the read string is converted from encoding A (external encoding)
 *  to encoding B (internal encoding), then tagged with B.
 *  If two optional arguments are specified, those must be
 *  encoding objects or encoding names,
 *  and the first one is the external encoding,
 *  and the second one is the internal encoding.
 *
 *  In the example below, the two processes close the ends of the pipe
 *  that they are not using. This is not just a cosmetic nicety. The
 *  read end of a pipe will not generate an end of file condition if
 *  there are any writers with the pipe still open. In the case of the
 *  parent process, the <code>rd.read</code> will never return if it
 *  does not first issue a <code>wr.close</code>.
 *
 *     rd, wr = IO.pipe
 *
 *     if fork
 *       wr.close
 *       puts "Parent got: <#{rd.read}>"
 *       rd.close
 *       Process.wait
 *     else
 *       rd.close
 *       puts "Sending message to parent"
 *       wr.write "Hi Dad"
 *       wr.close
 *     end
 *
 *  <em>produces:</em>
 *
 *     Sending message to parent
 *     Parent got: <Hi Dad>
 */

static VALUE
rb_io_s_pipe(VALUE recv, SEL sel, int argc, VALUE *argv)
{
    VALUE rd, wr, ext_enc = Qnil, int_enc = Qnil;
    rb_scan_args(argc, argv, "02", &ext_enc, &int_enc);

    int fd[2] = {-1, -1};
    if (pipe(fd) != 0) {
	rb_sys_fail("pipe() failed");
    }

    rd = prep_io(fd[0], FMODE_READABLE, rb_cIO);
    wr = prep_io(fd[1], FMODE_WRITABLE, rb_cIO);

    return rb_assoc_new(rd, wr);
}

/*
 *  call-seq:
 *     IO.foreach(name, sep=$/) {|line| block }     => nil
 *     IO.foreach(name, limit) {|line| block }      => nil
 *     IO.foreach(name, sep, limit) {|line| block } => nil
 *
 *  Executes the block for every line in the named I/O port, where lines
 *  are separated by <em>sep</em>.
 *
 *     IO.foreach("testfile") {|x| print "GOT ", x }
 *
 *  <em>produces:</em>
 *
 *     GOT This is line one
 *     GOT This is line two
 *     GOT This is line three
 *     GOT And so on...
 *
 *  If the last argument is a hash, it's the keyword argument to open.
 *  See <code>IO.read</code> for detail.
 *
 */

static VALUE
rb_io_s_foreach(VALUE recv, SEL sel, int argc, VALUE *argv)
{
    RETURN_ENUMERATOR(recv, argc, argv);
    VALUE arr = rb_io_s_readlines(recv, sel, argc, argv);
    long i, count;
    for (i = 0, count = RARRAY_LEN(arr); i < count; i++) {
	VALUE at = RARRAY_AT(arr, i);
	rb_vm_yield(1, &at);
	RETURN_IF_BROKEN();
    }
    return Qnil;
}

static VALUE
io_s_read(struct foreach_arg *arg)
{
    return io_read(arg->io, 0, arg->argc, arg->argv);
}

/*
 *  call-seq:
 *     IO.read(name, [length [, offset]] )   => string
 *     IO.read(name, [length [, offset]], opt)   => string
 *
 *  Opens the file, optionally seeks to the given offset, then returns
 *  <i>length</i> bytes (defaulting to the rest of the file).
 *  <code>read</code> ensures the file is closed before returning.
 *
 *  If the last argument is a hash, it specifies option for internal
 *  open().  The key would be the following.  open_args: is exclusive
 *  to others.
 *
 *   encoding: string or encoding
 *
 *    specifies encoding of the read string.  encoding will be ignored
 *    if length is specified.
 *
 *   mode: string
 *
 *    specifies mode argument for open().  it should start with "r"
 *    otherwise it would cause error.
 *
 *   open_args: array of strings
 *
 *    specifies arguments for open() as an array.
 *
 *     IO.read("testfile")           #=> "This is line one\nThis is line two\nThis is line three\nAnd so on...\n"
 *     IO.read("testfile", 20)       #=> "This is line one\nThi"
 *     IO.read("testfile", 20, 10)   #=> "ne one\nThis is line "
 */

static VALUE
rb_io_s_read(VALUE recv, SEL sel, int argc, VALUE *argv)
{
    // TODO handle optional hash
    /*VALUE opt =*/ pop_last_hash(&argc, argv);

    VALUE fname, length, offset;
    rb_scan_args(argc, argv, "13", &fname, &length, &offset, NULL);

    FilePathValue(fname);
    struct foreach_arg arg;
    arg.io = rb_file_open(io_alloc(recv, 0), 1, &fname);
    arg.argc = 1;
    arg.argv = &length;

    if (!NIL_P(offset)) {
	rb_io_seek(arg.io, offset, 0);
    }
    return rb_ensure(io_s_read, (VALUE)&arg, rb_io_close, arg.io);
}

static VALUE
io_s_readlines(struct foreach_arg *arg)
{
    return rb_io_readlines(arg->io, 0, arg->argc, arg->argv);
}

/*
 *  call-seq:
 *     IO.readlines(name, sep=$/)     => array
 *     IO.readlines(name, limit)      => array
 *     IO.readlines(name, sep, limit) => array
 *
 *  Reads the entire file specified by <i>name</i> as individual
 *  lines, and returns those lines in an array. Lines are separated by
 *  <i>sep</i>.
 *
 *     a = IO.readlines("testfile")
 *     a[0]   #=> "This is line one\n"
 *
 *  If the last argument is a hash, it's the keyword argument to open.
 *  See <code>IO.read</code> for detail.
 *
 */

static VALUE
rb_io_s_readlines(VALUE recv, SEL sel, int argc, VALUE *argv)
{
    // TODO handle optional hash
    /*VALUE opt =*/ pop_last_hash(&argc, argv);

    VALUE fname;
    rb_scan_args(argc, argv, "13", &fname, NULL, NULL, NULL);

    struct foreach_arg arg;
    arg.io = rb_file_open(io_alloc(recv, 0), 1, &fname);
    arg.argc = argc - 1;
    arg.argv = argv + 1;

    return rb_ensure(io_s_readlines, (VALUE)&arg, rb_io_close, arg.io);
}

/*
 *  call-seq:
 *     IO.copy_stream(src, dst)
 *     IO.copy_stream(src, dst, copy_length)
 *     IO.copy_stream(src, dst, copy_length, src_offset)
 *
 *  IO.copy_stream copies <i>src</i> to <i>dst</i>.
 *  <i>src</i> and <i>dst</i> is either a filename or an IO.
 *
 *  This method returns the number of bytes copied.
 *
 *  If optional arguments are not given,
 *  the start position of the copy is
 *  the beginning of the filename or
 *  the current file offset of the IO.
 *  The end position of the copy is the end of file.
 *
 *  If <i>copy_length</i> is given,
 *  No more than <i>copy_length</i> bytes are copied.
 *
 *  If <i>src_offset</i> is given,
 *  it specifies the start position of the copy.
 *
 *  When <i>src_offset</i> is specified and
 *  <i>src</i> is an IO,
 *  IO.copy_stream doesn't move the current file offset.
 *
 */
static VALUE
rb_io_s_copy_stream(VALUE rcv, SEL sel, int argc, VALUE *argv)
{
    VALUE src, dst, len, offset;
    rb_scan_args(argc, argv, "22", &src, &dst, &len, &offset);

    bool src_is_path = false, dst_is_path = false;

    VALUE old_src_offset = Qnil;
    if (TYPE(src) != T_FILE) {
	FilePathValue(src);
	src_is_path = true;
    }
    else {
	if (!NIL_P(offset)) {
	    old_src_offset = rb_io_tell(src, 0); // save the old offset
	    rb_io_seek(src, 0, offset);
	}
    }

    if (TYPE(dst) != T_FILE) {
	FilePathValue(dst);
	dst_is_path = true;
    }

    if (src_is_path && dst_is_path) {
	// Fast path!
	copyfile_state_t s = copyfile_state_alloc();
	if (copyfile(RSTRING_PTR(src), RSTRING_PTR(dst), s, COPYFILE_ALL)
		!= 0) {
	    copyfile_state_free(s);
	    rb_sys_fail("copyfile() failed");
	}
	int src_fd = -2; 
	assert(copyfile_state_get(s, COPYFILE_STATE_SRC_FD, &src_fd) == 0);
	struct stat st;
	if (fstat(src_fd, &st) != 0) {
	    rb_sys_fail("fstat() failed");
	}
	copyfile_state_free(s);
	return LONG2NUM(st.st_size);
    }
    else {
	if (src_is_path) {
	    src = rb_f_open(rcv, 0, 1, &src);
	}
	if (dst_is_path) {
	    VALUE args[2];
	    args[0] = dst;
	    args[1] = rb_str_new2("w");
	    dst = rb_f_open(rcv, 0, 2, args);
	}
    }

    VALUE data_read = NIL_P(len)
	? io_read(src, 0, 0, NULL) : io_read(src, 0, 1, &len);

    VALUE copied = io_write(dst, 0, data_read);

    if (!NIL_P(old_src_offset)) {
	rb_io_seek(src, old_src_offset, SEEK_SET); // restore the old offset
    }

    return copied;
}

/*
 *  call-seq:
 *     io.external_encoding   => encoding
 *
 *  Returns the Encoding object that represents the encoding of the file.
 *  If io is write mode and no encoding is specified, returns <code>nil</code>.
 */

static VALUE
rb_io_external_encoding(VALUE io, SEL sel)
{
    // TODO
    return (VALUE)rb_locale_encoding();
}

/*
 *  call-seq:
 *     io.internal_encoding   => encoding
 *
 *  Returns the Encoding of the internal string if conversion is
 *  specified.  Otherwise returns nil.
 */

static VALUE
rb_io_internal_encoding(VALUE io, SEL sel)
{
    // TODO
    return (VALUE)rb_locale_encoding();
}

/*
 *  call-seq:
 *     io.set_encoding(ext_enc)           => io
 *     io.set_encoding("ext_enc:int_enc") => io
 *     io.set_encoding(ext_enc, int_enc)  => io
 *
 *  If single argument is specified, read string from io is tagged
 *  with the encoding specified.  If encoding is a colon separated two
 *  encoding names "A:B", the read string is converted from encoding A
 *  (external encoding) to encoding B (internal encoding), then tagged
 *  with B.  If two arguments are specified, those must be encoding
 *  objects or encoding names, and the first one is the external encoding, and the
 *  second one is the internal encoding.
 */

static VALUE
rb_io_set_encoding(VALUE id, SEL sel, int argc, VALUE *argv)
{
    // TODO
    return Qnil;
}

static VALUE
argf_external_encoding(VALUE argf, SEL sel)
{
    next_argv();
    ARGF_FORWARD(0, 0);
    return rb_io_external_encoding(ARGF.current_file, sel);
}

static VALUE
argf_internal_encoding(VALUE argf, SEL sel)
{
    next_argv();
    ARGF_FORWARD(0, 0);
    return rb_io_internal_encoding(ARGF.current_file, sel);
}

static VALUE
argf_set_encoding(VALUE id, SEL sel, int argc, VALUE *argv)
{
    if (!next_argv()) {
	rb_raise(rb_eArgError, "no stream to set encoding");
    }
    ARGF_FORWARD(0, 0);
    return rb_io_set_encoding(ARGF.current_file, sel, argc, argv);
}

static VALUE
argf_tell(VALUE argf, SEL sel)
{
    if (!next_argv()) {
	rb_raise(rb_eArgError, "no stream to tell");
    }
    ARGF_FORWARD(0, 0);
    return rb_io_tell(ARGF.current_file, 0);
}

static VALUE
argf_seek_m(VALUE argf, SEL sel, int argc, VALUE *argv)
{
    if (!next_argv()) {
	rb_raise(rb_eArgError, "no stream to seek");
    }
    ARGF_FORWARD(0, 0);
    return rb_io_seek_m(ARGF.current_file, sel, argc, argv);
}

static VALUE
argf_set_pos(VALUE argf, SEL sel, VALUE offset)
{
    if (!next_argv()) {
	rb_raise(rb_eArgError, "no stream to set position");
    }
    ARGF_FORWARD(0, 0);
    return rb_io_set_pos(ARGF.current_file, sel, offset);
}

static VALUE
argf_rewind(VALUE argf, SEL sel)
{
    if (!next_argv()) {
	rb_raise(rb_eArgError, "no stream to rewind");
    }
    ARGF_FORWARD(0, 0);
    return rb_io_rewind(ARGF.current_file, 0);
}

static VALUE
argf_fileno(VALUE argf, SEL sel)
{
    if (!next_argv()) {
	rb_raise(rb_eArgError, "no stream");
    }
    ARGF_FORWARD(0, 0);
    return rb_io_fileno(ARGF.current_file, 0);
}

static VALUE
argf_to_io(VALUE argf, SEL sel)
{
    next_argv();
    ARGF_FORWARD(0, 0);
    return argf;
}

static VALUE 
argf_eof(VALUE argf, SEL sel)
{
    next_argv();
    ARGF_FORWARD(0, 0);
    return rb_io_eof(ARGF.current_file, 0);
}

static VALUE
argf_read(VALUE argf, SEL sel, int argc, VALUE *argv)
{
    VALUE tmp, str, length;
    long len = 0;

    rb_scan_args(argc, argv, "02", &length, &str);
    if (!NIL_P(length)) {
	len = NUM2LONG(argv[0]);
    }
    if (!NIL_P(str)) {
	StringValue(str);
	rb_str_resize(str,0);
	argv[1] = Qnil;
    }

  retry:
    if (!next_argv()) {
	return str;
    }
    if (ARGF_GENERIC_INPUT_P()) {
	tmp = argf_forward(argf, sel, argc, argv);
    }
    else {
	tmp = io_read(ARGF.current_file, sel, argc, argv);
    }
    if (NIL_P(str)) {
	str = tmp;
    }
    else if (!NIL_P(tmp)) {
	rb_str_append(str, tmp);
    }

    if (NIL_P(tmp) || NIL_P(length)) {
	if (ARGF.next_p != -1) {
	    argf_close(ARGF.current_file, sel);
	    ARGF.next_p = 1;
	    goto retry;
	}
    }
    else if (argc >= 1) {
	if (RSTRING_LEN(str) < len) {
	    len -= RSTRING_LEN(str);
	    argv[0] = INT2NUM(len);
	    goto retry;
	}
    }
    return str;
}

struct argf_call_arg {
    int argc;
    VALUE *argv;
    VALUE argf;
};

// static VALUE
// argf_forward_call(VALUE arg, SEL sel)
// {
// rb_notimplement();
// }

static VALUE
argf_readpartial(VALUE id, SEL sel, int argc, VALUE *argv)
{
    rb_notimplement();
}

static VALUE
argf_getc(VALUE argf, SEL sel)
{
    VALUE ch;

  retry:
    if (!next_argv()) {
	return Qnil;
    }
    if (ARGF_GENERIC_INPUT_P()) {
	ch = rb_funcall3(ARGF.current_file, rb_intern("getc"), 0, 0);
    }
    else {
	ch = rb_io_getc(ARGF.current_file, sel);
    }
    if (NIL_P(ch) && ARGF.next_p != -1) {
	argf_close(ARGF.current_file, sel);
	ARGF.next_p = 1;
	goto retry;
    }

    return ch;
}

static VALUE
argf_getbyte(VALUE argf, SEL sel)
{
    VALUE ch;

  retry:
    if (!next_argv()) {
	return Qnil;
    }
    if (TYPE(ARGF.current_file) != T_FILE) {
	ch = rb_funcall3(ARGF.current_file, rb_intern("getbyte"), 0, 0);
    }
    else {
	ch = rb_io_getbyte(ARGF.current_file, sel);
    }
    if (NIL_P(ch) && ARGF.next_p != -1) {
	argf_close(ARGF.current_file, sel);
	ARGF.next_p = 1;
	goto retry;
    }

    return ch;
}

static VALUE
argf_readchar(VALUE argf, SEL sel)
{
    VALUE ch;

  retry:
    if (!next_argv()) {
	rb_eof_error();
    }
    if (TYPE(ARGF.current_file) != T_FILE) {
	ch = rb_funcall3(ARGF.current_file, rb_intern("getc"), 0, 0);
    }
    else {
	ch = rb_io_getc(ARGF.current_file, sel);
    }
    if (NIL_P(ch) && ARGF.next_p != -1) {
	argf_close(ARGF.current_file, sel);
	ARGF.next_p = 1;
	goto retry;
    }

    return ch;
}

static VALUE
argf_readbyte(VALUE argf, SEL sel)
{
    VALUE c;

    NEXT_ARGF_FORWARD(0, 0);
    c = argf_getbyte(argf, sel);
    if (NIL_P(c)) {
	rb_eof_error();
    }
    return c;
}

static VALUE
argf_each_line(VALUE argf, SEL sel, int argc, VALUE *argv)
{
    RETURN_ENUMERATOR(argf, argc, argv);
    while (true) {
	if (!next_argv()) {
	    return argf;
	}
	rb_io_each_line(ARGF.current_file, sel, argc, argv);
	ARGF.next_p = 1;
    }
}

static VALUE
argf_each_byte(VALUE argf, SEL sel)
{
    RETURN_ENUMERATOR(argf, 0, 0);
    while (true) {
	if (!next_argv()) {
	    return argf;
	}
	rb_io_each_byte(ARGF.current_file, sel);
	ARGF.next_p = 1;
    }
}

static VALUE
argf_each_char(VALUE argf, SEL sel)
{
    RETURN_ENUMERATOR(argf, 0, 0);
    while (true) {
	if (!next_argv()) {
	    return argf;
	}
	rb_io_each_char(ARGF.current_file, sel);
	ARGF.next_p = 1;
    }
}

static VALUE
argf_filename(VALUE argf, SEL sel)
{
    next_argv();
    return ARGF.filename;
}

static VALUE
argf_filename_getter(ID id, VALUE *var)
{
    return argf_filename(*var, 0);
}

static VALUE
argf_file(VALUE argf, SEL sel)
{
    next_argv();
    return ARGF.current_file;
}

static VALUE
argf_binmode_m(VALUE argf, SEL sel)
{
    ARGF.binmode = 1;
    next_argv();
    ARGF_FORWARD(0, 0);
    rb_io_binmode(ARGF.current_file, 0);
    return argf;
}

static VALUE
argf_skip(VALUE argf, SEL sel)
{
    if (ARGF.init_p && ARGF.next_p == 0) {
	argf_close(ARGF.current_file, 0);
	ARGF.next_p = 1;
    }
    return argf;
}

static VALUE
argf_close_m(VALUE argf, SEL sel)
{
    next_argv();
    argf_close(ARGF.current_file, 0);
    if (ARGF.next_p != -1) {
	ARGF.next_p = 1;
    }
    ARGF.gets_lineno = 0;
    return argf;
}

static VALUE
argf_closed(VALUE argf, SEL sel)
{
    next_argv();
    ARGF_FORWARD(0, 0);
    return rb_io_closed(ARGF.current_file, 0);
}

static VALUE
argf_to_s(VALUE argf, SEL sel)
{
    return rb_str_new2("ARGF");
}

static VALUE
argf_inplace_mode_get(VALUE argf, SEL sel)
{
    if (ARGF.inplace == NULL) {
	return Qnil;
    }
    return rb_str_new2(ARGF.inplace);
}

static VALUE
opt_i_get(ID id, VALUE *var)
{
    return argf_inplace_mode_get(*var, 0);
}

static VALUE
argf_inplace_mode_set(VALUE argf, SEL sel, VALUE val)
{
    if (!RTEST(val)) {
	if (ARGF.inplace != NULL) {
	    free(ARGF.inplace);
	}
	ARGF.inplace = NULL;
    }
    else {
	StringValue(val);
	if (ARGF.inplace != NULL) {
	    free(ARGF.inplace);
	}
	ARGF.inplace = strdup(RSTRING_PTR(val));
    }
    return argf;
}

static void
opt_i_set(VALUE val, ID id, VALUE *var)
{
    argf_inplace_mode_set(*var, 0, val);
}

const char *
ruby_get_inplace_mode(void)
{
    return ARGF.inplace;
}

void
ruby_set_inplace_mode(const char *suffix)
{
    if (ARGF.inplace) free(ARGF.inplace);
    ARGF.inplace = 0;
    if (suffix) ARGF.inplace = strdup(suffix);
}

static VALUE
argf_argv(VALUE argf, SEL sel)
{
    return ARGF.argv;
}

static VALUE
argf_argv_getter(ID id, VALUE *var)
{
    return argf_argv(*var, 0);
}

VALUE
rb_get_argv(void)
{
    return ARGF.argv;
}

/*
 *  Class <code>IO</code> is the basis for all input and output in Ruby.
 *  An I/O stream may be <em>duplexed</em> (that is, bidirectional), and
 *  so may use more than one native operating system stream.
 *
 *  Many of the examples in this section use class <code>File</code>,
 *  the only standard subclass of <code>IO</code>. The two classes are
 *  closely associated.
 *
 *  As used in this section, <em>portname</em> may take any of the
 *  following forms.
 *
 *  * A plain string represents a filename suitable for the underlying
 *    operating system.
 *
 *  * A string starting with ``<code>|</code>'' indicates a subprocess.
 *    The remainder of the string following the ``<code>|</code>'' is
 *    invoked as a process with appropriate input/output channels
 *    connected to it.
 *
 *  * A string equal to ``<code>|-</code>'' will create another Ruby
 *    instance as a subprocess.
 *
 *  Ruby will convert pathnames between different operating system
 *  conventions if possible. For instance, on a Windows system the
 *  filename ``<code>/gumby/ruby/test.rb</code>'' will be opened as
 *  ``<code>\gumby\ruby\test.rb</code>''. When specifying a
 *  Windows-style filename in a Ruby string, remember to escape the
 *  backslashes:
 *
 *     "c:\\gumby\\ruby\\test.rb"
 *
 *  Our examples here will use the Unix-style forward slashes;
 *  <code>File::SEPARATOR</code> can be used to get the
 *  platform-specific separator character.
 *
 *  I/O ports may be opened in any one of several different modes, which
 *  are shown in this section as <em>mode</em>. The mode may
 *  either be a Fixnum or a String. If numeric, it should be
 *  one of the operating system specific constants (O_RDONLY,
 *  O_WRONLY, O_RDWR, O_APPEND and so on). See man open(2) for
 *  more information.
 *
 *  If the mode is given as a String, it must be one of the
 *  values listed in the following table.
 *
 *    Mode |  Meaning
 *    -----+--------------------------------------------------------
 *    "r"  |  Read-only, starts at beginning of file  (default mode).
 *    -----+--------------------------------------------------------
 *    "r+" |  Read-write, starts at beginning of file.
 *    -----+--------------------------------------------------------
 *    "w"  |  Write-only, truncates existing file
 *         |  to zero length or creates a new file for writing.
 *    -----+--------------------------------------------------------
 *    "w+" |  Read-write, truncates existing file to zero length
 *         |  or creates a new file for reading and writing.
 *    -----+--------------------------------------------------------
 *    "a"  |  Write-only, starts at end of file if file exists,
 *         |  otherwise creates a new file for writing.
 *    -----+--------------------------------------------------------
 *    "a+" |  Read-write, starts at end of file if file exists,
 *         |  otherwise creates a new file for reading and
 *         |  writing.
 *    -----+--------------------------------------------------------
 *     "b" |  (DOS/Windows only) Binary file mode (may appear with
 *         |  any of the key letters listed above).
 *
 *
 *  The global constant ARGF (also accessible as $<) provides an
 *  IO-like stream which allows access to all files mentioned on the
 *  command line (or STDIN if no files are mentioned). ARGF provides
 *  the methods <code>#path</code> and <code>#filename</code> to access
 *  the name of the file currently being read.
 */
 
 
void
rb_write_error2(const char *mesg, long len)
{
    if (rb_stderr == orig_stderr || RFILE(orig_stderr)->fptr->fd < 0) {
	fwrite(mesg, sizeof(char), len, stderr);
    }
    else {
	rb_io_write(rb_stderr, rb_str_new(mesg, len));
    }
}

void
rb_write_error(const char *mesg)
{
    rb_write_error2(mesg, strlen(mesg));
}

static void
must_respond_to(ID mid, VALUE val, ID id)
{
    if (!rb_respond_to(val, mid)) {
	rb_raise(rb_eTypeError, "%s must have %s method, %s given",
		 rb_id2name(id), rb_id2name(mid),
		 rb_obj_classname(val));
    }
}

static void
stdout_setter(VALUE val, ID id, VALUE *variable)
{
    must_respond_to(id_write, val, id);
    if (*variable != val) {
	GC_RELEASE(*variable);
	*variable = val;
	GC_RETAIN(*variable);
    }
}

static VALUE
rb_getpass(VALUE self, SEL sel, VALUE prompt)
{
    StringValue(prompt);
    char *pwd = getpass(RSTRING_PTR(prompt));
    return rb_str_new2(pwd);
}

void
Init_IO(void)
{
    VALUE rb_cARGF;

    rb_eIOError = rb_define_class("IOError", rb_eStandardError);
    rb_eEOFError = rb_define_class("EOFError", rb_eIOError);

    id_write = rb_intern("write");
    id_read = rb_intern("read");
    id_getc = rb_intern("getc");
    id_flush = rb_intern("flush");
    id_encode = rb_intern("encode");
    id_readpartial = rb_intern("readpartial");

    rb_objc_define_module_function(rb_mKernel, "syscall", rb_f_syscall, -1);

    rb_objc_define_module_function(rb_mKernel, "open", rb_f_open, -1);
    rb_objc_define_module_function(rb_mKernel, "printf", rb_f_printf, -1);
    rb_objc_define_module_function(rb_mKernel, "print", rb_f_print, -1);
    rb_objc_define_module_function(rb_mKernel, "putc", rb_f_putc, 1);
    rb_objc_define_module_function(rb_mKernel, "puts", rb_f_puts, -1);
    rb_objc_define_module_function(rb_mKernel, "gets", rb_f_gets, -1);
    rb_objc_define_module_function(rb_mKernel, "readline", rb_f_readline, -1);
    rb_objc_define_module_function(rb_mKernel, "select", rb_f_select, -1);

    rb_objc_define_module_function(rb_mKernel, "readlines", rb_f_readlines, -1);

    rb_objc_define_module_function(rb_mKernel, "`", rb_f_backquote, 1);

    rb_objc_define_module_function(rb_mKernel, "p", rb_f_p, -1);
    rb_objc_define_module_function(rb_mKernel, "display", rb_obj_display, -1);

    rb_cIO = rb_define_class("IO", rb_cObject);
    rb_include_module(rb_cIO, rb_mEnumerable);

    rb_objc_define_method(*(VALUE *)rb_cIO, "alloc", io_alloc, 0);
    rb_objc_define_method(*(VALUE *)rb_cIO, "new", rb_io_s_new, -1);
    rb_objc_define_method(*(VALUE *)rb_cIO, "open",  rb_io_s_open, -1);
    rb_objc_define_method(*(VALUE *)rb_cIO, "sysopen",  rb_io_s_sysopen, -1);
    rb_objc_define_method(*(VALUE *)rb_cIO, "for_fd", rb_io_s_new, -1);
    rb_objc_define_method(*(VALUE *)rb_cIO, "popen", rb_io_s_popen, -1);
    rb_objc_define_method(*(VALUE *)rb_cIO, "foreach", rb_io_s_foreach, -1);
    rb_objc_define_method(*(VALUE *)rb_cIO, "readlines", rb_io_s_readlines, -1);
    rb_objc_define_method(*(VALUE *)rb_cIO, "read", rb_io_s_read, -1);
    rb_objc_define_method(*(VALUE *)rb_cIO, "select", rb_f_select, -1);
    rb_objc_define_method(*(VALUE *)rb_cIO, "pipe", rb_io_s_pipe, -1);
    rb_objc_define_method(*(VALUE *)rb_cIO, "try_convert", rb_io_s_try_convert, 1);
    rb_objc_define_method(*(VALUE *)rb_cIO, "copy_stream", rb_io_s_copy_stream, -1);

    rb_objc_define_method(rb_cIO, "initialize", rb_io_initialize, -1);

    rb_objc_io_finalize_super = rb_objc_install_method2((Class)rb_cIO, "finalize",
	    (IMP)rb_objc_io_finalize);

    rb_output_fs = Qnil;
    rb_define_hooked_variable("$,", &rb_output_fs, 0, rb_str_setter);

    rb_rs = rb_default_rs = rb_str_new2("\n");
    GC_RETAIN(rb_default_rs);
    GC_RETAIN(rb_rs);
    rb_output_rs = Qnil;
    OBJ_FREEZE(rb_default_rs);	/* avoid modifying RS_default */
    rb_define_hooked_variable("$/", &rb_rs, 0, rb_str_setter);
    rb_define_hooked_variable("$-0", &rb_rs, 0, rb_str_setter);
    rb_define_hooked_variable("$\\", &rb_output_rs, 0, rb_str_setter);

    rb_define_virtual_variable("$_", rb_lastline_get, rb_lastline_set);

    rb_objc_define_method(rb_cIO, "initialize_copy", rb_io_init_copy, 1);
    rb_objc_define_method(rb_cIO, "reopen", rb_io_reopen, -1);

    rb_objc_define_method(rb_cIO, "print", rb_io_print, -1);
    rb_objc_define_method(rb_cIO, "putc", rb_io_putc, 1);
    rb_objc_define_method(rb_cIO, "puts", rb_io_puts, -1);
    rb_objc_define_method(rb_cIO, "printf", rb_io_printf, -1);

    rb_objc_define_method(rb_cIO, "each",  rb_io_each_line, -1);
    rb_objc_define_method(rb_cIO, "each_line",  rb_io_each_line, -1);
    rb_objc_define_method(rb_cIO, "each_byte",  rb_io_each_byte, 0);
    rb_objc_define_method(rb_cIO, "each_char",  rb_io_each_char, 0);
    rb_objc_define_method(rb_cIO, "lines",  rb_io_lines, -1);
    rb_objc_define_method(rb_cIO, "bytes",  rb_io_bytes, 0);
    rb_objc_define_method(rb_cIO, "chars",  rb_io_chars, 0);

    rb_objc_define_method(rb_cIO, "syswrite", rb_io_syswrite, 1);
    rb_objc_define_method(rb_cIO, "sysread",  rb_io_sysread, -1);

    rb_objc_define_method(rb_cIO, "fileno", rb_io_fileno, 0);
    rb_define_alias(rb_cIO, "to_i", "fileno");
    rb_objc_define_method(rb_cIO, "to_io", rb_io_to_io, 0);

    rb_objc_define_method(rb_cIO, "fsync",   rb_io_fsync, 0);
    rb_objc_define_method(rb_cIO, "sync",   rb_io_sync, 0);
    rb_objc_define_method(rb_cIO, "sync=",  rb_io_set_sync, 1);

    rb_objc_define_method(rb_cIO, "lineno",   rb_io_lineno, 0);
    rb_objc_define_method(rb_cIO, "lineno=",  rb_io_set_lineno, 1);

    rb_objc_define_method(rb_cIO, "readlines",  rb_io_readlines, -1);

    rb_objc_define_method(rb_cIO, "read_nonblock",  io_read_nonblock, -1);
    rb_objc_define_method(rb_cIO, "write_nonblock", rb_io_write_nonblock, 1);
    rb_objc_define_method(rb_cIO, "readpartial",  io_readpartial, -1);
    rb_objc_define_method(rb_cIO, "read",  io_read, -1);
    rb_objc_define_method(rb_cIO, "write", io_write, 1);
    rb_objc_define_method(rb_cIO, "gets",  rb_io_gets_m, -1);
    rb_objc_define_method(rb_cIO, "readline",  rb_io_readline, -1);
    rb_objc_define_method(rb_cIO, "getc",  rb_io_getc, 0);
    rb_objc_define_method(rb_cIO, "getbyte",  rb_io_getbyte, 0);
    rb_objc_define_method(rb_cIO, "readchar",  rb_io_readchar, 0);
    rb_objc_define_method(rb_cIO, "readbyte",  rb_io_readbyte, 0);
    rb_objc_define_method(rb_cIO, "ungetc",rb_io_ungetc, 1);
    rb_objc_define_method(rb_cIO, "<<",    rb_io_addstr, 1);
    rb_objc_define_method(rb_cIO, "flush", rb_io_flush, 0);
    rb_objc_define_method(rb_cIO, "tell", rb_io_tell, 0);
    rb_objc_define_method(rb_cIO, "seek", rb_io_seek_m, -1);
    rb_define_const(rb_cIO, "SEEK_SET", INT2FIX(SEEK_SET));
    rb_define_const(rb_cIO, "SEEK_CUR", INT2FIX(SEEK_CUR));
    rb_define_const(rb_cIO, "SEEK_END", INT2FIX(SEEK_END));
    rb_objc_define_method(rb_cIO, "rewind", rb_io_rewind, 0);
    rb_objc_define_method(rb_cIO, "pos", rb_io_tell, 0);
    rb_objc_define_method(rb_cIO, "pos=", rb_io_set_pos, 1);
    rb_objc_define_method(rb_cIO, "eof", rb_io_eof, 0);
    rb_objc_define_method(rb_cIO, "eof?", rb_io_eof, 0);

    rb_objc_define_method(rb_cIO, "close_on_exec?", rb_io_close_on_exec_p, 0);
    rb_objc_define_method(rb_cIO, "close_on_exec=", rb_io_set_close_on_exec, 1);

    rb_objc_define_method(rb_cIO, "close", rb_io_close_m, 0);
    rb_objc_define_method(rb_cIO, "closed?", rb_io_closed, 0);
    rb_objc_define_method(rb_cIO, "close_read", rb_io_close_read, 0);
    rb_objc_define_method(rb_cIO, "close_write", rb_io_close_write, 0);

    rb_objc_define_method(rb_cIO, "isatty", rb_io_isatty, 0);
    rb_objc_define_method(rb_cIO, "tty?", rb_io_isatty, 0);
    rb_objc_define_method(rb_cIO, "binmode",  rb_io_binmode_m, 0);
    rb_objc_define_method(rb_cIO, "sysseek", rb_io_seek_m, -1);

    rb_objc_define_method(rb_cIO, "ioctl", rb_io_ioctl, -1);
    rb_objc_define_method(rb_cIO, "fcntl", rb_io_fcntl, -1);
    rb_objc_define_method(rb_cIO, "pid", rb_io_pid, 0);
    rb_objc_define_method(rb_cIO, "inspect",  rb_io_inspect, 0);

    rb_objc_define_method(rb_cIO, "external_encoding", rb_io_external_encoding, 0);
    rb_objc_define_method(rb_cIO, "internal_encoding", rb_io_internal_encoding, 0);
    rb_objc_define_method(rb_cIO, "set_encoding", rb_io_set_encoding, -1);

    rb_stdin = prep_io(fileno(stdin), FMODE_READABLE, rb_cIO);
    GC_WB(&(ExtractIOStruct(rb_stdin)->path), CFSTR("<STDIN>"));
    rb_define_variable("$stdin", &rb_stdin);
    rb_define_global_const("STDIN", rb_stdin);
    
    rb_stdout = prep_io(fileno(stdout), FMODE_WRITABLE, rb_cIO);
    GC_WB(&(ExtractIOStruct(rb_stdout)->path), CFSTR("<STDOUT>"));
    rb_define_hooked_variable("$stdout", &rb_stdout, 0, stdout_setter);
    rb_define_hooked_variable("$>", &rb_stdout, 0, stdout_setter);
    rb_define_global_const("STDOUT", rb_stdout);
    
    rb_stderr = prep_io(fileno(stderr), FMODE_WRITABLE|FMODE_SYNC, rb_cIO);
    GC_WB(&(ExtractIOStruct(rb_stderr)->path), CFSTR("<STDERR>"));
    rb_define_hooked_variable("$stderr", &rb_stderr, 0, stdout_setter);
    rb_define_global_const("STDERR", rb_stderr);
 
    orig_stdout = rb_stdout;
    rb_deferr = orig_stderr = rb_stderr;

    rb_cARGF = rb_class_new(rb_cObject);
    rb_set_class_path(rb_cARGF, rb_cObject, "ARGF.class");
    rb_objc_define_method(*(VALUE *)rb_cARGF, "alloc", argf_alloc, 0);

    rb_include_module(rb_cARGF, rb_mEnumerable);

    rb_objc_define_method(rb_cARGF, "initialize", argf_initialize, -2);
    rb_objc_define_method(rb_cARGF, "initialize_copy", argf_initialize_copy, 1);
    rb_objc_define_method(rb_cARGF, "to_s", argf_to_s, 0);
    rb_objc_define_method(rb_cARGF, "argv", argf_argv, 0);

    rb_objc_define_method(rb_cARGF, "fileno", argf_fileno, 0);
    rb_objc_define_method(rb_cARGF, "to_i", argf_fileno, 0);
    rb_objc_define_method(rb_cARGF, "to_io", argf_to_io, 0);
    rb_objc_define_method(rb_cARGF, "each",  argf_each_line, -1);
    rb_objc_define_method(rb_cARGF, "each_line",  argf_each_line, -1);
    rb_objc_define_method(rb_cARGF, "each_byte",  argf_each_byte, 0);
    rb_objc_define_method(rb_cARGF, "each_char",  argf_each_char, 0);
    rb_objc_define_method(rb_cARGF, "lines", argf_each_line, -1);
    rb_objc_define_method(rb_cARGF, "bytes", argf_each_byte, 0);
    rb_objc_define_method(rb_cARGF, "chars", argf_each_char, 0);

    rb_objc_define_method(rb_cARGF, "read",  argf_read, -1);
    rb_objc_define_method(rb_cARGF, "readpartial",  argf_readpartial, -1);
    rb_objc_define_method(rb_cARGF, "readlines", argf_readlines, -1);
    rb_objc_define_method(rb_cARGF, "to_a", argf_readlines, -1);
    rb_objc_define_method(rb_cARGF, "gets", argf_gets, -1);
    rb_objc_define_method(rb_cARGF, "readline", argf_readline, -1);
    rb_objc_define_method(rb_cARGF, "getc", argf_getc, 0);
    rb_objc_define_method(rb_cARGF, "getbyte", argf_getbyte, 0);
    rb_objc_define_method(rb_cARGF, "readchar", argf_readchar, 0);
    rb_objc_define_method(rb_cARGF, "readbyte", argf_readbyte, 0);
    rb_objc_define_method(rb_cARGF, "tell", argf_tell, 0);
    rb_objc_define_method(rb_cARGF, "seek", argf_seek_m, -1);
    rb_objc_define_method(rb_cARGF, "rewind", argf_rewind, 0);
    rb_objc_define_method(rb_cARGF, "pos", argf_tell, 0);
    rb_objc_define_method(rb_cARGF, "pos=", argf_set_pos, 1);
    rb_objc_define_method(rb_cARGF, "eof", argf_eof, 0);
    rb_objc_define_method(rb_cARGF, "eof?", argf_eof, 0);
    rb_objc_define_method(rb_cARGF, "binmode", argf_binmode_m, 0);

    rb_objc_define_method(rb_cARGF, "filename", argf_filename, 0);
    rb_objc_define_method(rb_cARGF, "path", argf_filename, 0);
    rb_objc_define_method(rb_cARGF, "file", argf_file, 0);
    rb_objc_define_method(rb_cARGF, "skip", argf_skip, 0);
    rb_objc_define_method(rb_cARGF, "close", argf_close_m, 0);
    rb_objc_define_method(rb_cARGF, "closed?", argf_closed, 0);

    rb_objc_define_method(rb_cARGF, "lineno",   argf_lineno, 0);
    rb_objc_define_method(rb_cARGF, "lineno=",  argf_set_lineno, 1);

    rb_objc_define_method(rb_cARGF, "inplace_mode", argf_inplace_mode_get, 0);
    rb_objc_define_method(rb_cARGF, "inplace_mode=", argf_inplace_mode_set, 1);

    rb_objc_define_method(rb_cARGF, "external_encoding", argf_external_encoding, 0);
    rb_objc_define_method(rb_cARGF, "internal_encoding", argf_internal_encoding, 0);
    rb_objc_define_method(rb_cARGF, "set_encoding", argf_set_encoding, -1);

    argf = rb_class_new_instance(0, 0, rb_cARGF);

    rb_define_readonly_variable("$<", &argf);
    rb_define_global_const("ARGF", argf);

    rb_define_hooked_variable("$.", &argf, argf_lineno_getter, argf_lineno_setter);
    rb_define_hooked_variable("$FILENAME", &argf, argf_filename_getter, rb_gvar_readonly_setter);
    GC_WB(&(ARGF.filename), rb_str_new2("-"));

    rb_define_hooked_variable("$-i", &argf, opt_i_get, opt_i_set);
    rb_define_hooked_variable("$*", &argf, argf_argv_getter, rb_gvar_readonly_setter);

    Init_File();

    rb_objc_define_method(rb_cFile, "initialize",  rb_file_initialize, -1);

    rb_file_const("RDONLY", INT2FIX(O_RDONLY));
    rb_file_const("WRONLY", INT2FIX(O_WRONLY));
    rb_file_const("RDWR", INT2FIX(O_RDWR));
    rb_file_const("APPEND", INT2FIX(O_APPEND));
    rb_file_const("CREAT", INT2FIX(O_CREAT));
    rb_file_const("EXCL", INT2FIX(O_EXCL));
    rb_file_const("NONBLOCK", INT2FIX(O_NONBLOCK));
    rb_file_const("TRUNC", INT2FIX(O_TRUNC));
    rb_file_const("NOCTTY", INT2FIX(O_NOCTTY));
    rb_file_const("BINARY", INT2FIX(0));
    rb_file_const("SYNC", INT2FIX(O_SYNC));

    sel_each_byte = sel_registerName("each_byte");
    sel_each_char = sel_registerName("each_char");
    sel_each_line = sel_registerName("each_line");

    // MacRuby extensions:
    rb_objc_define_module_function(rb_mKernel, "getpass", rb_getpass, 1);
}
