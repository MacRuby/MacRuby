/**********************************************************************

  io.c -

  $Author: mame $
  created at: Fri Oct 15 18:08:59 JST 1993

  Copyright (C) 1993-2007 Yukihiro Matsumoto
  Copyright (C) 2000  Network Applied Communication Laboratory, Inc.
  Copyright (C) 2000  Information-technology Promotion Agency, Japan

**********************************************************************/

#include "ruby/ruby.h"
#include "ruby/io.h"
#include "ruby/signal.h"
#include "ruby/node.h"
#include "roxor.h"
#include <ctype.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include <sys/stat.h>

#include <sys/param.h>

#if !defined NOFILE
# define NOFILE 64
#endif

#include <unistd.h>
#include <sys/syscall.h>

extern void Init_File(void);

#include "ruby/util.h"

#ifndef O_ACCMODE
#define O_ACCMODE (O_RDONLY | O_WRONLY | O_RDWR)
#endif

#if SIZEOF_OFF_T > SIZEOF_LONG && !defined(HAVE_LONG_LONG)
# error off_t is bigger than long, but you have no long long...
#endif

VALUE rb_cIO;
VALUE rb_eEOFError;
VALUE rb_eIOError;

VALUE rb_stdin, rb_stdout, rb_stderr;
VALUE rb_deferr;		/* rescue VIM plugin */
static VALUE orig_stdout, orig_stderr;

VALUE rb_output_fs;
VALUE rb_rs;
VALUE rb_output_rs;
VALUE rb_default_rs;

static VALUE argf;

static ID id_write, id_read, id_getc, id_flush, id_encode, id_readpartial;

struct timeval rb_time_interval(VALUE);

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

//static int max_file_descriptor = NOFILE;
#define UPDATE_MAXFD(fd) \
    do { \
        if (max_file_descriptor < (fd)) max_file_descriptor = (fd); \
    } while (0)
#define UPDATE_MAXFD_PIPE(filedes) \
    do { \
        UPDATE_MAXFD((filedes)[0]); \
        UPDATE_MAXFD((filedes)[1]); \
    } while (0)

#define argf_of(obj) (*(struct argf *)DATA_PTR(obj))
#define ARGF argf_of(argf)

// static int
// is_socket(int fd, const char *path)
// {
//     struct stat sbuf;
//     if (fstat(fd, &sbuf) < 0)
//         rb_sys_fail(path);
//     return S_ISSOCK(sbuf.st_mode);
// }

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

static void
rb_io_check_initialized(rb_io_t *fptr)
{
    if (fptr == NULL) {
	rb_raise(rb_eIOError, "uninitialized stream");
    }
}

static inline void
rb_io_assert_usable(CFStreamStatus status)
{
    if (status == kCFStreamStatusNotOpen
	|| status == kCFStreamStatusClosed
	|| status == kCFStreamStatusError) {
	rb_raise(rb_eIOError, "stream is not usable");
    }
}

static void 
rb_io_assert_writable(rb_io_t *io_struct)
{
    rb_io_check_initialized(io_struct);
    if (io_struct->writeStream == NULL) {
	rb_raise(rb_eIOError, "not opened for writing");
    }
    rb_io_assert_usable(CFWriteStreamGetStatus(io_struct->writeStream));
}

static void
rb_io_assert_readable(rb_io_t *io_struct)
{
    rb_io_check_initialized(io_struct);
    if (io_struct->readStream == NULL) {
	rb_raise(rb_eIOError, "not opened for reading");
    }
    rb_io_assert_usable(CFReadStreamGetStatus(io_struct->readStream));
}

static bool
rb_io_is_open(rb_io_t *io_struct) 
{
    return (io_struct->readStream == NULL
	    || CFReadStreamGetStatus(io_struct->readStream) == kCFStreamStatusOpen)
	&& (io_struct->writeStream == NULL
	    || CFWriteStreamGetStatus(io_struct->writeStream) == kCFStreamStatusOpen);
}

#if 0
// These methods are not used yet.
static int
rb_io_get_fd_from_data(CFDataRef data)
{
    assert(data != NULL);
    int fd;
    assert(CFDataGetLength(data) == sizeof(fd));
    CFDataGetBytes(data, CFRangeMake(0, sizeof(fd)), (UInt8 *)&fd);
    CFRelease(data);
    return fd;
}

static int
rb_io_get_read_stream_fd(rb_io_t *io_struct)
{
    CFDataRef data = CFReadStreamCopyProperty(io_struct->readStream, 
	    kCFStreamPropertySocketNativeHandle);
    return rb_io_get_fd_from_data(data);
}

static int
rb_io_get_write_stream_fd(rb_io_t *io_struct)
{
    CFDataRef data = CFWriteStreamCopyProperty(io_struct->writeStream, 
	    kCFStreamPropertySocketNativeHandle);
    return rb_io_get_fd_from_data(data);
}
#endif

#define FMODE_PREP (1<<16)
#define IS_PREP_STDIO(f) ((f)->mode & FMODE_PREP)
#define PREP_STDIO_NAME(f) ((f)->path)

static inline int
rb_io_modenum_flags(int mode)
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
#ifdef O_BINARY
    if (mode & O_BINARY) {
	flags |= FMODE_BINMODE;
    }
#endif

    return flags;
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
rb_io_s_try_convert(VALUE dummy, SEL sel, VALUE io)
{
rb_notimplement();
}

#ifndef SEEK_CUR
# define SEEK_SET 0
# define SEEK_CUR 1
# define SEEK_END 2
#endif

#define FMODE_SYNCWRITE (FMODE_SYNC|FMODE_WRITABLE)


static VALUE
io_alloc(VALUE klass, SEL sel)
{
    struct RFile *io = ALLOC(struct RFile);
    OBJSETUP(io, klass, T_FILE);
    GC_WB(&io->fptr, ALLOC(rb_io_t));
    return (VALUE)io;
}

static inline CFReadStreamRef
rb_io_open_read_stream(int fd, CFURLRef url)
{
    CFReadStreamRef r;

    if (url != NULL) {
	r = CFReadStreamCreateWithFile(NULL, url);
	assert(r != NULL);
    }
    else {
	// TODO
	abort();
    }

    if (!CFReadStreamOpen(r)) {
	rb_raise(rb_eRuntimeError, "cannot open read stream");
    }

    return r;
}

static inline CFWriteStreamRef
rb_io_open_write_stream(int fd, CFURLRef url)
{
    CFWriteStreamRef w;

    if (url != NULL) {
	w = CFWriteStreamCreateWithFile(NULL, url);
	assert(w != NULL);
    }
    else {
	// TODO
	abort();
    }

    if (!CFWriteStreamOpen(w)) {
	rb_raise(rb_eRuntimeError, "cannot open read stream");
    }

    return w;
}

static inline void
prep_io_struct(rb_io_t *io_struct, int fd, int mode, const char *path)
{
    CFReadStreamRef r = NULL;
    CFWriteStreamRef w = NULL;
    
    if (path != NULL) {
	CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL,
		(const UInt8 *)path, strlen(path), false);
	assert(url != NULL);
	if (mode & FMODE_READABLE) {
	    r = rb_io_open_read_stream(fd, url);
	}
	if (mode & FMODE_WRITABLE) {
	    w = rb_io_open_write_stream(fd, url);
	}
	CFRelease(url);	
    }
    else {
	// TODO
	//CFStreamCreatePairWithSocket(NULL, fd, &r, &w);
	abort();
    }

    assert(r != NULL || w != NULL);

    if (r != NULL) {
	GC_WB(&io_struct->readStream, r);
	CFMakeCollectable(r);
    }
    else {
	io_struct->readStream = NULL;
    }

    if (w != NULL) {
	GC_WB(&io_struct->writeStream, w);
	CFMakeCollectable(w);
    }
    else {
	io_struct->writeStream = NULL;
    }
    
    io_struct->fd = fd;
    io_struct->ungetc_buf = NULL;
    io_struct->ungetc_buf_len = 0;
    io_struct->ungetc_buf_pos = 0;
}

static VALUE
prep_io(int fd, int mode, VALUE klass, const char *path)
{
    VALUE io = io_alloc(rb_cIO, 0);

    rb_io_t *io_struct = RFILE(io)->fptr;

    prep_io_struct(io_struct, fd, mode, path);

    rb_objc_keep_for_exit_finalize((VALUE)io);

    return io;
}

static VALUE
prep_stdio(FILE *f, int mode, VALUE klass, const char *path)
{
    VALUE io = prep_io(fileno(f), mode|FMODE_PREP, klass, path);
    return io;
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
io_write(VALUE io, SEL sel, VALUE to_write)
{
    rb_io_t *io_struct;
    UInt8 *buffer;
    CFIndex length;
    
    rb_secure(4);
    
    io_struct = ExtractIOStruct(io);
    rb_io_assert_writable(io_struct);

    // TODO: Account for the port not being IO, use funcall to call .write()
    // instead.

    to_write = rb_obj_as_string(to_write);

    if (CLASS_OF(to_write) == rb_cByteString) {
	CFMutableDataRef data = rb_bytestring_wrapped_data(to_write);
	buffer = CFDataGetMutableBytePtr(data);
	length = CFDataGetLength(data);
    }
    else {
	buffer = (UInt8 *)RSTRING_PTR(to_write);
	if (buffer != NULL) {
	    length = RSTRING_LEN(to_write);
	}
	else {
	    const long max = CFStringGetMaximumSizeForEncoding(
		    CFStringGetLength((CFStringRef)to_write),
		    kCFStringEncodingUTF8);

	    buffer = (UInt8 *)alloca(max + 1);
	    if (!CFStringGetCString((CFStringRef)to_write, (char *)buffer, 
			max, kCFStringEncodingUTF8)) {
		// XXX what could we do?
		abort();
	    }
	    length = strlen((char *)buffer);
	}
    }

    if (length == 0) {
        return INT2FIX(0);
    }

    return LONG2FIX(CFWriteStreamWrite(io_struct->writeStream, buffer, length));
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
    io_write(io, 0, str);
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
    rb_notimplement();
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

static inline long
rb_io_read_stream_get_offset(CFReadStreamRef stream)
{
    long result = 0L;

    CFNumberRef pos = CFReadStreamCopyProperty(stream,
	    kCFStreamPropertyFileCurrentOffset);
    CFNumberGetValue(pos, kCFNumberSInt32Type, &result);
    CFRelease(pos);

    return result;
}

static inline void
rb_io_read_stream_set_offset(CFReadStreamRef stream, long offset)
{
    CFNumberRef pos = CFNumberCreate(NULL, kCFNumberSInt32Type, &offset);
    CFReadStreamSetProperty(stream, kCFStreamPropertyFileCurrentOffset, pos);
    CFRelease(pos);
}

static VALUE
rb_io_tell(VALUE io, SEL sel)
{
    rb_io_t *io_struct = ExtractIOStruct(io);
    rb_io_assert_readable(io_struct);

    return LONG2FIX(rb_io_read_stream_get_offset(io_struct->readStream)); 
}

static VALUE
rb_io_seek(VALUE io, VALUE offset, int whence)
{
    rb_io_t *io_struct = ExtractIOStruct(io);
    rb_io_assert_readable(io_struct); 
    rb_io_assert_writable(io_struct);

    // TODO: make this work with IO::SEEK_CUR, SEEK_END, etc.
    rb_io_read_stream_set_offset(io_struct->readStream, FIX2LONG(offset));

    return INT2FIX(0); // is this right?
}

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
    // minor inefficiency here in that i'm creating and then destroying
    // a Fixnum containing zero.
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

VALUE
rb_io_eof(VALUE io, SEL sel)
{
    rb_io_t *io_struct = ExtractIOStruct(io);
    rb_io_assert_readable(io_struct);
    return CONDITION_TO_BOOLEAN(
	    CFReadStreamGetStatus(io_struct->readStream) == kCFStreamStatusAtEnd);
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
    rb_notimplement();
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
    rb_notimplement();
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
     rb_notimplement();   
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
    rb_notimplement();
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
    if ((io_struct == NULL) || (io_struct->path == NULL)) {
        return rb_any_to_s(io);
    }
    const char *status = (rb_io_is_open(io_struct) ? "" : " (closed)");

    CFStringRef s = CFStringCreateWithFormat(NULL, NULL, CFSTR("#<%s:%@%s>"),
	    rb_obj_classname(io), io_struct->path, status);
    CFMakeCollectable(s);
    return (VALUE)s;
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

static inline long
rb_io_stream_read_internal(CFReadStreamRef readStream, UInt8 *buffer, long len)
{
    long data_read = 0;

    while (data_read < len) {
	int code = CFReadStreamRead(readStream, &buffer[data_read],
		len - data_read);

	if (code == 0) {
	    // EOF
	    break;
	}
	else if (code == -1) {
	    rb_raise(rb_eRuntimeError, "internal error while reading stream");
	}

	data_read += code;
    }

    return data_read;
}

static inline long
rb_io_read_internal(rb_io_t *io_struct, UInt8 *buffer, long len)
{
    assert(io_struct->readStream != NULL);

    long data_read = 0;

    // First let's check if there is something to read in our ungetc buffer.
    if (io_struct->ungetc_buf_len > 0) {
	data_read = MIN(io_struct->ungetc_buf_len, len);
	memcpy(buffer, &io_struct->ungetc_buf[io_struct->ungetc_buf_pos], 
		data_read);
	io_struct->ungetc_buf_len -= len;
	io_struct->ungetc_buf_pos += len;
	if (io_struct->ungetc_buf_len == 0) {
	    xfree(io_struct->ungetc_buf);
	    io_struct->ungetc_buf = NULL;
	}
	if (data_read == len) {
	    return data_read;
	}
    }

    // Read from the stream.
    data_read += rb_io_stream_read_internal(io_struct->readStream,
	    &buffer[data_read], len - data_read);

    return data_read;
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
    // TODO factorize code from io_read()
    rb_notimplement();
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

static VALUE
io_read_nonblock(VALUE io, SEL sel, int argc, VALUE *argv)
{
    rb_notimplement();
}

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

static VALUE
rb_io_write_nonblock(VALUE io, SEL sel, VALUE str)
{
    rb_notimplement();
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
    rb_io_t *io_struct;

    rb_scan_args(argc, argv, "11", &len, &outbuf);

    io_struct = ExtractIOStruct(io);
    rb_io_assert_readable(io_struct);

    long size = FIX2LONG(len);
    if (size == 0) {
	return rb_str_new2("");
    }

    if (NIL_P(outbuf)) {
        outbuf = rb_bytestring_new();
    } 
    else {
	// TODO
	// if outbuf is a bytestring, let's get a pointer to its mutable storage
	// if outbuf is a string, we need to allocate a new buffer and then copy
	// it into the string.
        //outbuf = rb_coerce_to_bytestring(outbuf);
	abort();
    }

    CFMutableDataRef data = rb_bytestring_wrapped_data(outbuf);
    CFDataIncreaseLength(data, size);
    UInt8 *buf = CFDataGetMutableBytePtr(data);

    long data_read = rb_io_read_internal(io_struct, buf, size);
    if (data_read < size) {
	rb_eof_error();
    }
    CFDataSetLength(data, data_read);

    return outbuf;
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
    rb_notimplement();
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
rb_notimplement();
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
rb_io_set_lineno(VALUE io, SEL sel, VALUE lineno)
{
rb_notimplement();
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
rb_notimplement();
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
rb_notimplement();
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

static VALUE
rb_io_each_line(VALUE io, SEL sel, int argc, VALUE *argv)
{
rb_notimplement();
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
    rb_notimplement();
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

static VALUE
rb_io_each_char(VALUE io, SEL sel)
{
rb_notimplement();
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
rb_notimplement();
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
rb_notimplement();
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
rb_notimplement();
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
 *  such that a subsequent buffered read will return it. Only one character
 *  may be pushed back before a subsequent read operation (that is,
 *  you will be able to read only the last of several characters that have been pushed
 *  back). Has no effect with unbuffered reads (such as <code>IO#sysread</code>).
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

    if (len > io_struct->ungetc_buf_pos) {
    	const long delta = io_struct->ungetc_buf_len
	    - io_struct->ungetc_buf_pos;

	// Reallocate the buffer.
	GC_WB(&io_struct->ungetc_buf, xrealloc(io_struct->ungetc_buf,
		    delta + len));

	// Shift the buffer.
	memmove(&io_struct->ungetc_buf[delta], 
		&io_struct->ungetc_buf[io_struct->ungetc_buf_pos], delta);
    }

    // Update position.
    io_struct->ungetc_buf_pos -= len;
    if (io_struct->ungetc_buf_pos < 0) {
	io_struct->ungetc_buf_pos = 0;
    }

    // Copy the bytes at the position.
    memcpy(&io_struct->ungetc_buf[io_struct->ungetc_buf_pos], bytes, len);

    // Update buffer size.
    io_struct->ungetc_buf_len += len - io_struct->ungetc_buf_pos;

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
    rb_notimplement();
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
rb_notimplement();
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
    rb_notimplement();
}

static inline void
io_close(VALUE io, bool close_read, bool close_write)
{
    rb_io_t *io_struct = ExtractIOStruct(io);

    if (close_read && io_struct->readStream != NULL) {
	CFReadStreamClose(io_struct->readStream);
    }
    if (close_write && io_struct->writeStream != NULL) {
	CFWriteStreamClose(io_struct->writeStream);
    }
}

static VALUE
rb_io_close_m(VALUE io, SEL sel)
{
    io_close(io, true, true);
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
    rb_notimplement();
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
    io_close(io, true, false);
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
    io_close(io, false, true);
    return Qnil;
}

VALUE
rb_io_close(VALUE io, SEL sel)
{
    io_close(io, true, true);
    return Qnil;
}

VALUE
rb_io_fdopen(int fd, int mode, const char *path)
{
    VALUE klass = rb_cIO;

    if (path != NULL && strcmp(path, "-") != 0) {
	klass = rb_cFile;
    }
    return prep_io(fd, rb_io_modenum_flags(mode), klass, path);
}

static VALUE
rb_io_getline(int argc, VALUE *argv, VALUE io)
{
#if 0 // TODO
    VALUE rs;
    long limit;

    prepare_getline_args(argc, argv, &rs, &limit, io);
    return rb_io_getline_1(rs, limit, io);
#endif
    abort();
}

VALUE
rb_io_gets(VALUE io, SEL sel)
{
    rb_io_t *io_struct = ExtractIOStruct(io);
    rb_io_assert_readable(io_struct);

    VALUE outbuf = rb_bytestring_new();
    CFMutableDataRef data = rb_bytestring_wrapped_data(outbuf);

    long s = 128;
    CFDataIncreaseLength(data, s);
    UInt8 *buf = CFDataGetMutableBytePtr(data);

    // FIXME this is a very naive implementation

    long data_read = 0;
    while (true) {
	UInt8 byte;
	if (rb_io_read_internal(io_struct, &byte, 1) != 1) {
	    break;
	}

	if (data_read > s) {
	    s += s;
	    CFDataIncreaseLength(data, s);
	    buf = CFDataGetMutableBytePtr(data);
	}
	buf[data_read++] = byte;

	if (byte == '\n') {
	    break;    
	}
    }

    if (data_read == 0) {
	return Qnil;
    }

    CFDataSetLength(data, data_read);

    return outbuf;
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

static VALUE
rb_io_sysseek(VALUE io, SEL sel, int argc, VALUE *argv)
{
rb_notimplement();
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
rb_io_syswrite(VALUE io, SEL sel, VALUE str)
{
rb_notimplement();
}

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
rb_io_sysread(VALUE io, SEL sel, int argc, VALUE *argv)
{
rb_notimplement();
}

VALUE
rb_io_binmode(VALUE io, SEL sel)
{
    // TODO
#if 0
    rb_io_t *fptr;

    GetOpenFile(io, fptr);
    fptr->mode |= FMODE_BINMODE;
    return io;
#endif
    abort();
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

static VALUE
rb_io_s_popen(VALUE klass, SEL sel, int argc, VALUE *argv)
{
rb_notimplement();
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

static VALUE
rb_io_s_open(VALUE klass, SEL sel, int argc, VALUE *argv)
{
rb_notimplement();
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
rb_notimplement();
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
rb_f_open(VALUE klass, SEL sel, int argc, VALUE *argv)
{
rb_notimplement();
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

static VALUE
rb_io_reopen(VALUE io, SEL sel, int argc, VALUE *argv)
{
rb_notimplement();
}

/* :nodoc: */
static VALUE
rb_io_init_copy(VALUE dest, VALUE io)
{
rb_notimplement();
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
    rb_notimplement();
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
rb_notimplement();
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
    if(argc == 0) {
        // No arguments? Bloody Perlisms...
        argc = 1;
        line = rb_lastline_get();
        argv = &line;
    }
    while(argc--) {
        rb_io_write(rb_stdout, 0, *argv++);
        if(!NIL_P(rb_output_fs)) {
            rb_io_write(rb_stdout, 0, rb_output_fs);
        }
    }
    if(!NIL_P(rb_output_rs)) {
        rb_io_write(rb_stdout, 0, rb_output_rs);
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
rb_notimplement();
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
rb_notimplement();
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

VALUE
rb_io_puts(VALUE out, SEL sel, int argc, VALUE *argv)
{
rb_notimplement();
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
    while (argc--) {
        rb_io_write(rb_stdout, 0, *argv++);
        rb_io_write(rb_stdout, 0, rb_default_rs);
    }
    return Qnil;
}

void
rb_p(VALUE obj, SEL sel) /* for debug print within C code */
{
    rb_io_write(rb_stdout, 0, rb_obj_as_string(rb_inspect(obj)));
    rb_io_write(rb_stdout, 0, rb_default_rs);
}

VALUE rb_io_write(VALUE v, SEL sel, VALUE i)
{
    io_write(v, 0, i);
    return Qnil;
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
	rb_p(argv[i], 0);
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

static VALUE
rb_obj_display(VALUE self, SEL sel, int argc, VALUE *argv)
{
rb_notimplement();
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
    rb_notimplement();
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
    rb_notimplement();
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
rb_io_s_new(VALUE klass, SEL sel, VALUE fd, VALUE mode)
{
    rb_notimplement();
}


static inline void
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
#define filename          ARGF.filename
#define current_file      ARGF.current_file
#define gets_lineno       ARGF.gets_lineno
#define init_p            ARGF.init_p
#define next_p            ARGF.next_p
#define lineno            ARGF.lineno
#define ruby_inplace_mode ARGF.inplace
#define argf_binmode      ARGF.binmode
#define argf_enc          ARGF.enc
#define argf_enc2         ARGF.enc2
#define rb_argv           ARGF.argv

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
    GC_WB(&rb_argv, rb_obj_dup(rb_argv));
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
    gets_lineno = NUM2INT(val);
    lineno = INT2FIX(gets_lineno);
    return Qnil;
}

static VALUE
argf_lineno(VALUE argf, SEL sel)
{
    return lineno;
}

static VALUE
argf_forward(VALUE argf, SEL sel, int argc, VALUE *argv)
{
    // TODO
    //return rb_funcall3(current_file, rb_frame_this_func(), argc, argv);
    abort();
}

#define next_argv() argf_next_argv(argf)
#define ARGF_GENERIC_INPUT_P() \
    (current_file == rb_stdin && TYPE(current_file) != T_FILE)

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
#if 0 // TODO
    char *fn;
    rb_io_t *fptr;
    int stdout_binmode = 0;

    if (TYPE(rb_stdout) == T_FILE) {
	GetOpenFile(rb_stdout, fptr);
	if (fptr->mode & FMODE_BINMODE)
	    stdout_binmode = 1;
    }

    if (init_p == 0) {
	if (!NIL_P(rb_argv) && RARRAY_LEN(rb_argv) > 0) {
	    next_p = 1;
	}
	else {
	    next_p = -1;
	}
	init_p = 1;
	gets_lineno = 0;
    }

    if (next_p == 1) {
	next_p = 0;
retry:
	if (RARRAY_LEN(rb_argv) > 0) {
	    filename = rb_ary_shift(rb_argv);
	    fn = StringValueCStr(filename);
	    if (strlen(fn) == 1 && fn[0] == '-') {
		current_file = rb_stdin;
		if (ruby_inplace_mode) {
		    rb_warn("Can't do inplace edit for stdio; skipping");
		    goto retry;
		}
	    }
	    else {
		int fr = rb_sysopen(fn, O_RDONLY, 0);

		if (ruby_inplace_mode) {
		    struct stat st;
#ifndef NO_SAFE_RENAME
		    struct stat st2;
#endif
		    VALUE str;
		    int fw;

		    if (TYPE(rb_stdout) == T_FILE && rb_stdout != orig_stdout) {
			rb_io_close(rb_stdout);
		    }
		    fstat(fr, &st);
		    if (*ruby_inplace_mode) {
			str = rb_str_new2(fn);
#ifdef NO_LONG_FNAME
			ruby_add_suffix(str, ruby_inplace_mode);
#else
			rb_str_cat2(str, ruby_inplace_mode);
#endif
#ifdef NO_SAFE_RENAME
			(void)close(fr);
			(void)unlink(RSTRING_PTR(str));
			(void)rename(fn, RSTRING_PTR(str));
			fr = rb_sysopen(RSTRING_PTR(str), O_RDONLY, 0);
#else
			if (rename(fn, RSTRING_PTR(str)) < 0) {
			    rb_warn("Can't rename %s to %s: %s, skipping file",
				    fn, RSTRING_PTR(str), strerror(errno));
			    close(fr);
			    goto retry;
			}
#endif
		    }
		    else {
#ifdef NO_SAFE_RENAME
			rb_fatal("Can't do inplace edit without backup");
#else
			if (unlink(fn) < 0) {
			    rb_warn("Can't remove %s: %s, skipping file",
				    fn, strerror(errno));
			    close(fr);
			    goto retry;
			}
#endif
		    }
		    fw = rb_sysopen(fn, O_WRONLY|O_CREAT|O_TRUNC, 0666);
#ifndef NO_SAFE_RENAME
		    fstat(fw, &st2);
#ifdef HAVE_FCHMOD
		    fchmod(fw, st.st_mode);
#else
		    chmod(fn, st.st_mode);
#endif
		    if (st.st_uid!=st2.st_uid || st.st_gid!=st2.st_gid) {
			fchown(fw, st.st_uid, st.st_gid);
		    }
#endif
		    rb_stdout = prep_io(fw, FMODE_WRITABLE, rb_cFile, fn);
		    if (stdout_binmode) {
			rb_io_binmode(rb_stdout, 0);
		    }
		}
		current_file = prep_io(fr, FMODE_READABLE, rb_cFile, fn);
	    }
	    if (argf_binmode) {
		rb_io_binmode(current_file);
	    }
	    if (argf_enc) {
		rb_io_t *fptr;

		GetOpenFile(current_file, fptr);
		fptr->enc = argf_enc;
		fptr->enc2 = argf_enc2;
	    }
	}
	else {
	    next_p = 1;
	    return Qfalse;
	}
    }
    else if (next_p == -1) {
	current_file = rb_stdin;
	filename = rb_str_new2("-");
	if (ruby_inplace_mode) {
	    rb_warn("Can't do inplace edit for stdio");
	    rb_stdout = orig_stdout;
	}
    }
    return Qtrue;
#endif
    abort();
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
	line = rb_funcall3(current_file, rb_intern("gets"), argc, argv);
    }
    else {
	if (argc == 0 && rb_rs == rb_default_rs) {
	    line = rb_io_gets(current_file, 0);
	}
	else {
	    line = rb_io_getline(argc, argv, current_file);
	}
	if (NIL_P(line) && next_p != -1) {
	    argf_close(current_file, 0);
	    next_p = 1;
	    goto retry;
	}
    }
    if (!NIL_P(line)) {
	gets_lineno++;
	lineno = INT2FIX(gets_lineno);
    }
    return line;
}

static VALUE
argf_lineno_getter(ID id, VALUE *var)
{
    VALUE argf = *var;
    return lineno;
}

static void
argf_lineno_setter(VALUE val, ID id, VALUE *var)
{
    VALUE argf = *var;
    int n = NUM2INT(val);
    gets_lineno = n;
    lineno = INT2FIX(n);    
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
rb_f_readline(VALUE recv, SEL sel, int argc, VALUE *argv)
{
rb_notimplement();
}

static VALUE
argf_readline(VALUE recv, SEL sel, int argc, VALUE *argv)
{
rb_notimplement();
}

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
rb_f_readlines(VALUE recv, SEL sel, int argc, VALUE *argv)
{
rb_notimplement();
}

static VALUE
argf_readlines(VALUE recv, SEL sel, int argc, VALUE *argv)
{
rb_notimplement();
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
rb_notimplement();
}


// static VALUE
// select_call(VALUE arg)
// {
// rb_notimplement();
// }
// 
// static VALUE
// select_end(VALUE arg)
// {
// rb_notimplement();
// }


/*
 *  call-seq:
 *     IO.select(read_array
 *               [, write_array
 *               [, error_array
 *               [, timeout]]] ) =>  array  or  nil
 *
 *  See <code>Kernel#select</code>.
 */

static VALUE
rb_f_select(VALUE recv, SEL sel, int argc, VALUE *argv)
{
rb_notimplement();
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
rb_io_ioctl(VALUE recv, SEL sel, VALUE integer_cmd, VALUE arg)
{
rb_notimplement();
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
rb_io_fcntl(VALUE recv, SEL sel, VALUE integer_cmd, VALUE arg)
{
rb_notimplement();
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
    rb_notimplement();
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
rb_notimplement();
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
rb_notimplement();
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
    VALUE fname, length, offset, opt;

    rb_scan_args(argc, argv, "13", &fname, &length, &offset, &opt);

    // TODO honor opt

    StringValue(fname);

    CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL,
	    (const UInt8 *)RSTRING_PTR(fname), RSTRING_LEN(fname), false);
    assert(url != NULL);

    CFReadStreamRef readStream = rb_io_open_read_stream(-1, url);

    CFRelease(url);

    if (!NIL_P(offset)) {
	long o = FIX2LONG(offset);
	rb_io_read_stream_set_offset(readStream, o);
    }

    VALUE outbuf = rb_bytestring_new();
    CFMutableDataRef data = rb_bytestring_wrapped_data(outbuf);
    long data_read = 0;

    if (NIL_P(length)) {
	// Read all
	long size = 128;
	CFDataIncreaseLength(data, size);
	UInt8 *buf = CFDataGetMutableBytePtr(data);
	while (true) {
	    const long fragment = rb_io_stream_read_internal(readStream,
		    &buf[data_read], size);
	    data_read += fragment;
	    if (fragment < size) {
		break;
	    }
	    size += size;
	    CFDataIncreaseLength(data, size);
	    buf = CFDataGetMutableBytePtr(data);
	}
    }
    else {
	const long size = FIX2LONG(length);
	CFDataIncreaseLength(data, size);
	UInt8 *buf = CFDataGetMutableBytePtr(data);
	data_read = rb_io_stream_read_internal(readStream, buf, size);
    }

    CFDataSetLength(data, data_read);

    CFReadStreamClose(readStream);
    CFRelease(readStream);

    return outbuf;
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
    VALUE fname, arg2, arg3, opt;

    rb_scan_args(argc, argv, "13", &fname, &arg2, &arg3, &opt);

    // Read everything.
    VALUE io_s_read_args[] = { fname, Qnil, Qnil, opt };
    VALUE outbuf = rb_io_s_read(recv, 0, 4, io_s_read_args);

    // Prepare arguments.
    VALUE rs, limit;
    if (argc == 1) {
	rs = rb_rs;
	limit = Qnil;
    }
    else {
	if (!NIL_P(arg2) && NIL_P(arg3)) {
	    if (TYPE(arg2) == T_STRING) {
		rs = arg2;
		limit = Qnil;
	    }
	    else {
		limit = arg2;
		rs = rb_rs;
	    }
	}
	else {
	    StringValue(arg2);
	    rs = arg2;
	    limit = arg3;
	}
    }

    CFMutableDataRef data = rb_bytestring_wrapped_data(outbuf);
    UInt8 *buf = CFDataGetMutableBytePtr(data);
    const long length = CFDataGetLength(data);

    VALUE ary = rb_ary_new();

    if (RSTRING_LEN(rs) == 1) {
	UInt8 byte = RSTRING_PTR(rs)[0];

	long pos = 0;
	void *ptr;
	while ((ptr = memchr(&buf[pos], byte, length - pos)) != NULL) {
	    const long s =  (long)ptr - (long)&buf[pos] + 1;
	    rb_ary_push(ary, rb_bytestring_new_with_data(&buf[pos], s));
	    pos += s; 
	}
    }
    else {
	// TODO
	abort();
    }	

    return ary;
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
rb_io_s_copy_stream(VALUE id, SEL sel, int argc, VALUE *argv)
{
rb_notimplement();
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
rb_notimplement();
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
rb_notimplement();
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
rb_notimplement();
}

static VALUE
argf_external_encoding(VALUE argf, SEL sel)
{
rb_notimplement();
}

static VALUE
argf_internal_encoding(VALUE argf, SEL sel)
{
rb_notimplement();
}

static VALUE
argf_set_encoding(VALUE id, SEL sel, int argc, VALUE *argv)
{
rb_notimplement();
}

static VALUE
argf_tell(VALUE argf, SEL sel)
{
rb_notimplement();
}

static VALUE
argf_seek_m(VALUE id, SEL sel, int argc, VALUE *argv)
{
rb_notimplement();
}

static VALUE
argf_set_pos(VALUE argf, SEL sel, VALUE offset)
{
rb_notimplement();
}

static VALUE
argf_rewind(VALUE argf, SEL sel)
{
rb_notimplement();
}

static VALUE
argf_fileno(VALUE argf, SEL sel)
{
rb_notimplement();
}

static VALUE
argf_to_io(VALUE argf, SEL sel)
{
rb_notimplement();
}

static VALUE 
argf_eof(VALUE argf, SEL sel)
{
rb_notimplement();
}

static VALUE
argf_read(VALUE id, SEL sel, int argc, VALUE *argv)
{
rb_notimplement();
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
rb_notimplement();
}

static VALUE
argf_getbyte(VALUE argf, SEL sel)
{
rb_notimplement();
}

static VALUE
argf_readchar(VALUE argf, SEL sel)
{
rb_notimplement();
}

static VALUE
argf_readbyte(VALUE argf)
{
rb_notimplement();
}

static VALUE
argf_each_line(VALUE id, SEL sel, int argc, VALUE *argv)
{
rb_notimplement();
}

static VALUE
argf_each_byte(VALUE argf, SEL sel)
{
rb_notimplement();
}

static VALUE
argf_each_char(VALUE argf, SEL sel)
{
rb_notimplement();
}

static VALUE
argf_filename(VALUE argf, SEL sel)
{
    next_argv();
    return filename;
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
    return current_file;
}

static VALUE
argf_binmode_m(VALUE argf, SEL sel)
{
    argf_binmode = 1;
    next_argv();
    ARGF_FORWARD(0, 0);
    rb_io_binmode(current_file, 0);
    return argf;
}

static VALUE
argf_skip(VALUE argf, SEL sel)
{
    if (next_p != -1) {
	argf_close(current_file, 0);
	next_p = 1;
    }
    return argf;
}

static VALUE
argf_close_m(VALUE argf, SEL sel)
{
    next_argv();
    argf_close(current_file, 0);
    if (next_p != -1) {
	next_p = 1;
    }
    gets_lineno = 0;
    return argf;
}

static VALUE
argf_closed(VALUE argf, SEL sel)
{
    next_argv();
    ARGF_FORWARD(0, 0);
    return rb_io_closed(current_file, 0);
}

static VALUE
argf_to_s(VALUE argf, SEL sel)
{
    return rb_str_new2("ARGF");
}

static VALUE
argf_inplace_mode_get(VALUE argf, SEL sel)
{
    if (ruby_inplace_mode == NULL) {
	return Qnil;
    }
    return rb_str_new2(ruby_inplace_mode);
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
	if (ruby_inplace_mode != NULL) {
	    free(ruby_inplace_mode);
	}
	ruby_inplace_mode = NULL;
    }
    else {
	StringValue(val);
	if (ruby_inplace_mode != NULL) {
	    free(ruby_inplace_mode);
	}
	ruby_inplace_mode = strdup(RSTRING_PTR(val));
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
rb_notimplement();
}

void
ruby_set_inplace_mode(const char *suffix)
{
rb_notimplement();
}

static VALUE
argf_argv(VALUE argf, SEL sel)
{
    return rb_argv;
}

static VALUE
argf_argv_getter(ID id, VALUE *var)
{
    return argf_argv(*var, 0);
}

VALUE
rb_get_argv(void)
{
    return rb_argv;
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
	rb_io_write(rb_stderr, 0, rb_str_new(mesg, len));
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
    *variable = val;
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

    rb_objc_define_method(rb_mKernel, "syscall", rb_f_syscall, -1);

    rb_objc_define_method(rb_mKernel, "open", rb_f_open, -1);
    rb_objc_define_method(rb_mKernel, "printf", rb_f_printf, -1);
    rb_objc_define_method(rb_mKernel, "print", rb_f_print, -1);
    rb_objc_define_method(rb_mKernel, "putc", rb_f_putc, 1);
    rb_objc_define_method(rb_mKernel, "puts", rb_f_puts, -1);
    rb_objc_define_method(rb_mKernel, "gets", rb_f_gets, -1);
    rb_objc_define_method(rb_mKernel, "readline", rb_f_readline, -1);
    rb_objc_define_method(rb_mKernel, "select", rb_f_select, -1);

    rb_objc_define_method(rb_mKernel, "readlines", rb_f_readlines, -1);

    rb_objc_define_method(rb_mKernel, "`", rb_f_backquote, 1);

    rb_objc_define_method(rb_mKernel, "p", rb_f_p, -1);
    rb_objc_define_method(rb_mKernel, "display", rb_obj_display, -1);

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

    rb_output_fs = Qnil;
    rb_define_hooked_variable("$,", &rb_output_fs, 0, rb_str_setter);

    rb_global_variable(&rb_default_rs);
    rb_rs = rb_default_rs = rb_str_new2("\n");
    rb_objc_retain((void *)rb_rs);
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
    rb_objc_define_method(rb_cIO, "sysseek", rb_io_sysseek, -1);

    rb_objc_define_method(rb_cIO, "ioctl", rb_io_ioctl, -1);
    rb_objc_define_method(rb_cIO, "fcntl", rb_io_fcntl, -1);
    rb_objc_define_method(rb_cIO, "pid", rb_io_pid, 0);
    rb_objc_define_method(rb_cIO, "inspect",  rb_io_inspect, 0);

    rb_objc_define_method(rb_cIO, "external_encoding", rb_io_external_encoding, 0);
    rb_objc_define_method(rb_cIO, "internal_encoding", rb_io_internal_encoding, 0);
    rb_objc_define_method(rb_cIO, "set_encoding", rb_io_set_encoding, -1);

    // TODO: Replace these with their real equivalents - they're nil now.
    rb_stdin = prep_stdio(stdin, FMODE_READABLE, rb_cIO, "/dev/stdin");
    rb_define_variable("$stdin", &rb_stdin);
    rb_define_global_const("STDIN", rb_stdin);
    
    rb_stdout = prep_stdio(stdout, FMODE_WRITABLE, rb_cIO, "/dev/stdout");
    rb_define_hooked_variable("$stdout", &rb_stdout, 0, stdout_setter);
    rb_define_hooked_variable("$>", &rb_stdout, 0, stdout_setter);
    rb_define_global_const("STDOUT", rb_stdout);
    
    rb_stderr = prep_stdio(stderr, FMODE_WRITABLE|FMODE_SYNC, rb_cIO, "/dev/stderr");
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
    rb_define_hooked_variable("$FILENAME", &argf, argf_filename_getter, 0);
    GC_WB(&filename, rb_str_new2("-"));

    rb_define_hooked_variable("$-i", &argf, opt_i_get, opt_i_set);
    rb_define_hooked_variable("$*", &argf, argf_argv_getter, 0);

    Init_File();

    rb_objc_define_method(rb_cFile, "initialize",  rb_file_initialize, -1);

    rb_file_const("RDONLY", INT2FIX(O_RDONLY));
    rb_file_const("WRONLY", INT2FIX(O_WRONLY));
    rb_file_const("RDWR", INT2FIX(O_RDWR));
    rb_file_const("APPEND", INT2FIX(O_APPEND));
    rb_file_const("CREAT", INT2FIX(O_CREAT));
    rb_file_const("EXCL", INT2FIX(O_EXCL));
#if defined(O_NDELAY) || defined(O_NONBLOCK)
# ifdef O_NONBLOCK
    rb_file_const("NONBLOCK", INT2FIX(O_NONBLOCK));
# else
    rb_file_const("NONBLOCK", INT2FIX(O_NDELAY));
# endif
#endif
    rb_file_const("TRUNC", INT2FIX(O_TRUNC));
#ifdef O_NOCTTY
    rb_file_const("NOCTTY", INT2FIX(O_NOCTTY));
#endif
#ifdef O_BINARY
    rb_file_const("BINARY", INT2FIX(O_BINARY));
#else
    rb_file_const("BINARY", INT2FIX(0));
#endif
#ifdef O_SYNC
    rb_file_const("SYNC", INT2FIX(O_SYNC));
#endif
}
