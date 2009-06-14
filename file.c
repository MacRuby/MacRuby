/**********************************************************************

  file.c -

  $Author: nobu $
  created at: Mon Nov 15 12:24:34 JST 1993

  Copyright (C) 1993-2007 Yukihiro Matsumoto
  Copyright (C) 2000  Network Applied Communication Laboratory, Inc.
  Copyright (C) 2000  Information-technology Promotion Agency, Japan

**********************************************************************/

#include "ruby/ruby.h"
#include "ruby/io.h"
#include "ruby/signal.h"
#include "ruby/util.h"
#include "dln.h"
#include "objc.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_SYS_FILE_H
# include <sys/file.h>
#else
int flock(int, int);
#endif

#ifdef HAVE_SYS_PARAM_H
# include <sys/param.h>
#endif
#ifndef MAXPATHLEN
# define MAXPATHLEN 1024
#endif

#include <ctype.h>

#include <time.h>

#ifdef HAVE_UTIME_H
#include <utime.h>
#elif defined HAVE_SYS_UTIME_H
#include <sys/utime.h>
#endif

#ifdef HAVE_PWD_H
#include <pwd.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>

#ifdef HAVE_SYS_MKDEV_H
#include <sys/mkdev.h>
#endif

#if !defined HAVE_LSTAT && !defined lstat
#define lstat stat
#endif

#ifdef __BEOS__ /* should not change ID if -1 */
static int
be_chown(const char *path, uid_t owner, gid_t group)
{
    if (owner == -1 || group == -1) {
	struct stat st;
	if (stat(path, &st) < 0) return -1;
	if (owner == -1) owner = st.st_uid;
	if (group == -1) group = st.st_gid;
    }
    return chown(path, owner, group);
}
#define chown be_chown
static int
be_fchown(int fd, uid_t owner, gid_t group)
{
    if (owner == -1 || group == -1) {
	struct stat st;
	if (fstat(fd, &st) < 0) return -1;
	if (owner == -1) owner = st.st_uid;
	if (group == -1) group = st.st_gid;
    }
    return fchown(fd, owner, group);
}
#define fchown be_fchown
#endif /* __BEOS__ */

VALUE rb_cFile;
VALUE rb_mFileTest;
VALUE rb_cStat;

static VALUE
rb_get_path_check(VALUE obj, int check)
{
    VALUE tmp;
    static ID to_path;

    if (check) {
	rb_check_safe_obj(obj);
    }
    tmp = rb_check_string_type(obj);
    if (!NIL_P(tmp)) {
	goto exit;
    }

    if (!to_path) {
	to_path = rb_intern("to_path");
    }
    if (rb_respond_to(obj, to_path)) {
	tmp = rb_funcall(obj, to_path, 0, 0);
    }
    else {
	tmp = obj;
    }
  exit:
    StringValueCStr(tmp);
    if (check && obj != tmp) {
	rb_check_safe_obj(tmp);
    }
    return rb_str_new4(tmp);
}

VALUE
rb_get_path_no_checksafe(VALUE obj)
{
    return rb_get_path_check(obj, 0);
}

VALUE
rb_get_path(VALUE obj)
{
    return rb_get_path_check(obj, 1);
}

static long
apply2files(void (*func)(const char *, void *), VALUE vargs, void *arg)
{
    long i;
    volatile VALUE path;

    rb_secure(4);
    for (i=0; i<RARRAY_LEN(vargs); i++) {
	path = rb_get_path(RARRAY_AT(vargs, i));
	(*func)(StringValueCStr(path), arg);
    }

    return RARRAY_LEN(vargs);
}

/*
 *  call-seq:
 *     file.path -> filename
 *  
 *  Returns the pathname used to create <i>file</i> as a string. Does
 *  not normalize the name.
 *     
 *     File.new("testfile").path               #=> "testfile"
 *     File.new("/tmp/../tmp/xxx", "w").path   #=> "/tmp/../tmp/xxx"
 *     
 */

static VALUE
rb_file_path(VALUE obj, SEL sel)
{
    // rb_io_t *fptr;
    // 
    // fptr = RFILE(rb_io_taint_check(obj))->fptr;
    // rb_io_check_initialized(fptr);
    // if (!fptr->path) return Qnil;
    // return rb_tainted_str_new2(fptr->path);
    rb_notimplement();
}

static VALUE
stat_new_0(VALUE klass, struct stat *st)
{
    struct stat *nst = 0;

    if (st) {
	nst = ALLOC(struct stat);
	*nst = *st;
    }
    return Data_Wrap_Struct(klass, NULL, xfree, nst);
}

static VALUE
stat_new(struct stat *st)
{
    return stat_new_0(rb_cStat, st);
}

static struct stat*
get_stat(VALUE self)
{
    struct stat* st;
    Data_Get_Struct(self, struct stat, st);
    if (!st) rb_raise(rb_eTypeError, "uninitialized File::Stat");
    return st;
}

static struct timespec stat_mtimespec(struct stat *st);

/*
 *  call-seq:
 *     stat <=> other_stat    => -1, 0, 1
 *  
 *  Compares <code>File::Stat</code> objects by comparing their
 *  respective modification times.
 *     
 *     f1 = File.new("f1", "w")
 *     sleep 1
 *     f2 = File.new("f2", "w")
 *     f1.stat <=> f2.stat   #=> -1
 */

static VALUE
rb_stat_cmp(VALUE self, SEL sel, VALUE other)
{
    if (rb_obj_is_kind_of(other, rb_obj_class(self))) {
        struct timespec ts1 = stat_mtimespec(get_stat(self));
        struct timespec ts2 = stat_mtimespec(get_stat(other));
        if (ts1.tv_sec == ts2.tv_sec) {
            if (ts1.tv_nsec == ts2.tv_nsec) return INT2FIX(0);
            if (ts1.tv_nsec < ts2.tv_nsec) return INT2FIX(-1);
            return INT2FIX(1);
        }
        if (ts1.tv_sec < ts2.tv_sec) return INT2FIX(-1);
        return INT2FIX(1);
    }
    return Qnil;
}

/*
 *  call-seq:
 *     stat.dev    => fixnum
 *  
 *  Returns an integer representing the device on which <i>stat</i>
 *  resides.
 *     
 *     File.stat("testfile").dev   #=> 774
 */

static VALUE
rb_stat_dev(VALUE self, SEL sel)
{
    return INT2NUM(get_stat(self)->st_dev);
}

/*
 *  call-seq:
 *     stat.dev_major   => fixnum
 *  
 *  Returns the major part of <code>File_Stat#dev</code> or
 *  <code>nil</code>.
 *     
 *     File.stat("/dev/fd1").dev_major   #=> 2
 *     File.stat("/dev/tty").dev_major   #=> 5
 */

static VALUE
rb_stat_dev_major(VALUE self, SEL sel)
{
#if defined(major)
    long dev = get_stat(self)->st_dev;
    return ULONG2NUM(major(dev));
#else
    return Qnil;
#endif
}

/*
 *  call-seq:
 *     stat.dev_minor   => fixnum
 *  
 *  Returns the minor part of <code>File_Stat#dev</code> or
 *  <code>nil</code>.
 *     
 *     File.stat("/dev/fd1").dev_minor   #=> 1
 *     File.stat("/dev/tty").dev_minor   #=> 0
 */

static VALUE
rb_stat_dev_minor(VALUE self, SEL sel)
{
#if defined(minor)
    long dev = get_stat(self)->st_dev;
    return ULONG2NUM(minor(dev));
#else
    return Qnil;
#endif
}


/*
 *  call-seq:
 *     stat.ino   => fixnum
 *  
 *  Returns the inode number for <i>stat</i>.
 *     
 *     File.stat("testfile").ino   #=> 1083669
 *     
 */

static VALUE
rb_stat_ino(VALUE self, SEL sel)
{
#ifdef HUGE_ST_INO
    return ULL2NUM(get_stat(self)->st_ino);
#else
    return ULONG2NUM(get_stat(self)->st_ino);
#endif
}

/*
 *  call-seq:
 *     stat.mode   => fixnum
 *  
 *  Returns an integer representing the permission bits of
 *  <i>stat</i>. The meaning of the bits is platform dependent; on
 *  Unix systems, see <code>stat(2)</code>.
 *     
 *     File.chmod(0644, "testfile")   #=> 1
 *     s = File.stat("testfile")
 *     sprintf("%o", s.mode)          #=> "100644"
 */

static VALUE
rb_stat_mode(VALUE self, SEL sel)
{
#ifdef __BORLANDC__
    return UINT2NUM((unsigned short)(get_stat(self)->st_mode));
#else
    return UINT2NUM(get_stat(self)->st_mode);
#endif
}

/*
 *  call-seq:
 *     stat.nlink   => fixnum
 *  
 *  Returns the number of hard links to <i>stat</i>.
 *     
 *     File.stat("testfile").nlink             #=> 1
 *     File.link("testfile", "testfile.bak")   #=> 0
 *     File.stat("testfile").nlink             #=> 2
 *     
 */

static VALUE
rb_stat_nlink(VALUE self, SEL sel)
{
    return UINT2NUM(get_stat(self)->st_nlink);
}


/*
 *  call-seq:
 *     stat.uid    => fixnum
 *  
 *  Returns the numeric user id of the owner of <i>stat</i>.
 *     
 *     File.stat("testfile").uid   #=> 501
 *     
 */

static VALUE
rb_stat_uid(VALUE self, SEL sel)
{
    return UIDT2NUM(get_stat(self)->st_uid);
}

/*
 *  call-seq:
 *     stat.gid   => fixnum
 *  
 *  Returns the numeric group id of the owner of <i>stat</i>.
 *     
 *     File.stat("testfile").gid   #=> 500
 *     
 */

static VALUE
rb_stat_gid(VALUE self, SEL sel)
{
    return GIDT2NUM(get_stat(self)->st_gid);
}


/*
 *  call-seq:
 *     stat.rdev   =>  fixnum or nil
 *  
 *  Returns an integer representing the device type on which
 *  <i>stat</i> resides. Returns <code>nil</code> if the operating
 *  system doesn't support this feature.
 *     
 *     File.stat("/dev/fd1").rdev   #=> 513
 *     File.stat("/dev/tty").rdev   #=> 1280
 */

static VALUE
rb_stat_rdev(VALUE self, SEL sel)
{
#ifdef HAVE_ST_RDEV
    return ULONG2NUM(get_stat(self)->st_rdev);
#else
    return Qnil;
#endif
}

/*
 *  call-seq:
 *     stat.rdev_major   => fixnum
 *  
 *  Returns the major part of <code>File_Stat#rdev</code> or
 *  <code>nil</code>.
 *     
 *     File.stat("/dev/fd1").rdev_major   #=> 2
 *     File.stat("/dev/tty").rdev_major   #=> 5
 */

static VALUE
rb_stat_rdev_major(VALUE self, SEL sel)
{
#if defined(HAVE_ST_RDEV) && defined(major)
    long rdev = get_stat(self)->st_rdev;
    return ULONG2NUM(major(rdev));
#else
    return Qnil;
#endif
}

/*
 *  call-seq:
 *     stat.rdev_minor   => fixnum
 *  
 *  Returns the minor part of <code>File_Stat#rdev</code> or
 *  <code>nil</code>.
 *     
 *     File.stat("/dev/fd1").rdev_minor   #=> 1
 *     File.stat("/dev/tty").rdev_minor   #=> 0
 */

static VALUE
rb_stat_rdev_minor(VALUE self, SEL sel)
{
#if defined(HAVE_ST_RDEV) && defined(minor)
    long rdev = get_stat(self)->st_rdev;
    return ULONG2NUM(minor(rdev));
#else
    return Qnil;
#endif
}

/*
 *  call-seq:
 *     stat.size    => fixnum
 *  
 *  Returns the size of <i>stat</i> in bytes.
 *     
 *     File.stat("testfile").size   #=> 66
 */

static VALUE
rb_stat_size(VALUE self, SEL sel)
{
    return OFFT2NUM(get_stat(self)->st_size);
}

/*
 *  call-seq:
 *     stat.blksize   => integer or nil
 *  
 *  Returns the native file system's block size. Will return <code>nil</code>
 *  on platforms that don't support this information.
 *     
 *     File.stat("testfile").blksize   #=> 4096
 *     
 */

static VALUE
rb_stat_blksize(VALUE self, SEL sel)
{
#ifdef HAVE_ST_BLKSIZE
    return ULONG2NUM(get_stat(self)->st_blksize);
#else
    return Qnil;
#endif
}

/*
 *  call-seq:
 *     stat.blocks    => integer or nil
 *  
 *  Returns the number of native file system blocks allocated for this
 *  file, or <code>nil</code> if the operating system doesn't 
 *  support this feature.
 *     
 *     File.stat("testfile").blocks   #=> 2
 */

static VALUE
rb_stat_blocks(VALUE self, SEL sel)
{
#ifdef HAVE_ST_BLOCKS
    return ULONG2NUM(get_stat(self)->st_blocks);
#else
    return Qnil;
#endif
}

static struct timespec
stat_atimespec(struct stat *st)
{
    struct timespec ts;
    ts.tv_sec = st->st_atime;
#if defined(HAVE_STRUCT_STAT_ST_ATIM)
    ts.tv_nsec = st->st_atim.tv_nsec;
#elif defined(HAVE_STRUCT_STAT_ST_ATIMESPEC)
    ts.tv_nsec = st->st_atimespec.tv_nsec;
#elif defined(HAVE_STRUCT_STAT_ST_ATIMENSEC)
    ts.tv_nsec = st->st_atimensec;
#else
    ts.tv_nsec = 0;
#endif
    return ts;
}

static VALUE
stat_atime(struct stat *st)
{
    struct timespec ts = stat_atimespec(st);
    return rb_time_nano_new(ts.tv_sec, ts.tv_nsec);
}

static struct timespec
stat_mtimespec(struct stat *st)
{
    struct timespec ts;
    ts.tv_sec = st->st_mtime;
#if defined(HAVE_STRUCT_STAT_ST_MTIM)
    ts.tv_nsec = st->st_mtim.tv_nsec;
#elif defined(HAVE_STRUCT_STAT_ST_MTIMESPEC)
    ts.tv_nsec = st->st_mtimespec.tv_nsec;
#elif defined(HAVE_STRUCT_STAT_ST_MTIMENSEC)
    ts.tv_nsec = st->st_mtimensec;
#else
    ts.tv_nsec = 0;
#endif
    return ts;
}

static VALUE
stat_mtime(struct stat *st)
{
    struct timespec ts = stat_mtimespec(st);
    return rb_time_nano_new(ts.tv_sec, ts.tv_nsec);
}

static struct timespec
stat_ctimespec(struct stat *st)
{
    struct timespec ts;
    ts.tv_sec = st->st_ctime;
#if defined(HAVE_STRUCT_STAT_ST_CTIM)
    ts.tv_nsec = st->st_ctim.tv_nsec;
#elif defined(HAVE_STRUCT_STAT_ST_CTIMESPEC)
    ts.tv_nsec = st->st_ctimespec.tv_nsec;
#elif defined(HAVE_STRUCT_STAT_ST_CTIMENSEC)
    ts.tv_nsec = st->st_ctimensec;
#else
    ts.tv_nsec = 0;
#endif
    return ts;
}

static VALUE
stat_ctime(struct stat *st)
{
    struct timespec ts = stat_ctimespec(st);
    return rb_time_nano_new(ts.tv_sec, ts.tv_nsec);
}

/*
 *  call-seq:
 *     stat.atime   => time
 *  
 *  Returns the last access time for this file as an object of class
 *  <code>Time</code>.
 *     
 *     File.stat("testfile").atime   #=> Wed Dec 31 18:00:00 CST 1969
 *     
 */

static VALUE
rb_stat_atime(VALUE self, SEL sel)
{
    return stat_atime(get_stat(self));
}

/*
 *  call-seq:
 *     stat.mtime -> aTime
 *  
 *  Returns the modification time of <i>stat</i>.
 *     
 *     File.stat("testfile").mtime   #=> Wed Apr 09 08:53:14 CDT 2003
 *     
 */

static VALUE
rb_stat_mtime(VALUE self, SEL sel)
{
    return stat_mtime(get_stat(self));
}

/*
 *  call-seq:
 *     stat.ctime -> aTime
 *  
 *  Returns the change time for <i>stat</i> (that is, the time
 *  directory information about the file was changed, not the file
 *  itself).
 *     
 *     File.stat("testfile").ctime   #=> Wed Apr 09 08:53:14 CDT 2003
 *     
 */

static VALUE
rb_stat_ctime(VALUE self, SEL sel)
{
    return stat_ctime(get_stat(self));
}

/*
 * call-seq:
 *   stat.inspect  =>  string
 *
 * Produce a nicely formatted description of <i>stat</i>.
 *
 *   File.stat("/etc/passwd").inspect
 *      #=> "#<File::Stat dev=0xe000005, ino=1078078, mode=0100644, 
 *           nlink=1, uid=0, gid=0, rdev=0x0, size=1374, blksize=4096, 
 *           blocks=8, atime=Wed Dec 10 10:16:12 CST 2003, 
 *           mtime=Fri Sep 12 15:41:41 CDT 2003, 
 *           ctime=Mon Oct 27 11:20:27 CST 2003>"
 */

static VALUE
rb_stat_inspect(VALUE self, SEL sel)
{
    VALUE str;
    int i;
    static const struct {
	const char *name;
	VALUE (*func)(VALUE, SEL);
    } member[] = {
	{"dev",	    rb_stat_dev},
	{"ino",	    rb_stat_ino},
	{"mode",    rb_stat_mode},
	{"nlink",   rb_stat_nlink},
	{"uid",	    rb_stat_uid},
	{"gid",	    rb_stat_gid},
	{"rdev",    rb_stat_rdev},
	{"size",    rb_stat_size},
	{"blksize", rb_stat_blksize},
	{"blocks",  rb_stat_blocks},
	{"atime",   rb_stat_atime},
	{"mtime",   rb_stat_mtime},
	{"ctime",   rb_stat_ctime},
    };

    str = rb_str_buf_new2("#<");
    rb_str_buf_cat2(str, rb_obj_classname(self));
    rb_str_buf_cat2(str, " ");

    for (i = 0; i < sizeof(member)/sizeof(member[0]); i++) {
	VALUE v;

	if (i > 0) {
	    rb_str_buf_cat2(str, ", ");
	}
	rb_str_buf_cat2(str, member[i].name);
	rb_str_buf_cat2(str, "=");
	v = (*member[i].func)(self, 0);
	if (i == 2) {		/* mode */
	    char buf[32];

	    sprintf(buf, "0%lo", NUM2ULONG(v));
	    rb_str_buf_cat2(str, buf);
	}
	else if (i == 0 || i == 6) { /* dev/rdev */
	    char buf[32];

	    sprintf(buf, "0x%lx", NUM2ULONG(v));
	    rb_str_buf_cat2(str, buf);
	}
	else {
	    rb_str_append(str, rb_inspect(v));
	}
    }
    rb_str_buf_cat2(str, ">");
    OBJ_INFECT(str, self);

    return str;
}

static int
rb_stat(VALUE file, struct stat *st)
{
    VALUE tmp;

    rb_secure(2);
    tmp = rb_check_convert_type(file, T_FILE, "IO", "to_io");
    if (!NIL_P(tmp)) {
	rb_io_t *io_struct = ExtractIOStruct(tmp);
	return fstat(io_struct->fd, st);
    }
    FilePathValue(file);
    return stat(StringValueCStr(file), st);
}

#ifdef _WIN32
static HANDLE
w32_io_info(VALUE *file, BY_HANDLE_FILE_INFORMATION *st)
{
    VALUE tmp;
    HANDLE f, ret = 0;

    tmp = rb_check_convert_type(*file, T_FILE, "IO", "to_io");
    if (!NIL_P(tmp)) {
	rb_io_t *fptr;

	GetOpenFile(tmp, fptr);
	f = (HANDLE)rb_w32_get_osfhandle(fptr->fd);
	if (f == (HANDLE)-1) return INVALID_HANDLE_VALUE;
    }
    else {
	FilePathValue(*file);
	f = CreateFile(StringValueCStr(*file), 0,
	               FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
	               rb_w32_iswin95() ? 0 : FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (f == INVALID_HANDLE_VALUE) return f;
	ret = f;
    }
    if (GetFileType(f) == FILE_TYPE_DISK) {
	ZeroMemory(st, sizeof(*st));
	if (GetFileInformationByHandle(f, st)) return ret;
    }
    if (ret) CloseHandle(ret);
    return INVALID_HANDLE_VALUE;
}
#endif

/*
 *  call-seq:
 *     File.stat(file_name)   =>  stat
 *  
 *  Returns a <code>File::Stat</code> object for the named file (see
 *  <code>File::Stat</code>).
 *     
 *     File.stat("testfile").mtime   #=> Tue Apr 08 12:58:04 CDT 2003
 *     
 */

static VALUE
rb_file_s_stat(VALUE klass, SEL sel, VALUE fname)
{
    struct stat st;

    rb_secure(4);
    FilePathValue(fname);
    if (rb_stat(fname, &st) < 0) {
	rb_sys_fail(StringValueCStr(fname));
    }
    return stat_new(&st);
}

/*
 *  call-seq:
 *     ios.stat    => stat
 *  
 *  Returns status information for <em>ios</em> as an object of type
 *  <code>File::Stat</code>.
 *     
 *     f = File.new("testfile")
 *     s = f.stat
 *     "%o" % s.mode   #=> "100644"
 *     s.blksize       #=> 4096
 *     s.atime         #=> Wed Apr 09 08:53:54 CDT 2003
 *     
 */

static VALUE
rb_io_stat(VALUE obj, SEL sel)
{
    struct stat st;
    struct rb_io_t *io = ExtractIOStruct(obj);
    if (fstat(io->fd, &st) == -1) {
	rb_sys_fail(RSTRING_PTR(io->path));
    }
    return stat_new(&st);
}

/*
 *  call-seq:
 *     File.lstat(file_name)   => stat
 *  
 *  Same as <code>File::stat</code>, but does not follow the last symbolic
 *  link. Instead, reports on the link itself.
 *     
 *     File.symlink("testfile", "link2test")   #=> 0
 *     File.stat("testfile").size              #=> 66
 *     File.lstat("link2test").size            #=> 8
 *     File.stat("link2test").size             #=> 66
 *     
 */

static VALUE
rb_file_s_lstat(VALUE klass, SEL sel, VALUE fname)
{
#ifdef HAVE_LSTAT
    struct stat st;

    rb_secure(2);
    FilePathValue(fname);
    if (lstat(StringValueCStr(fname), &st) == -1) {
	rb_sys_fail(RSTRING_PTR(fname));
    }
    return stat_new(&st);
#else
    return rb_file_s_stat(klass, 0, fname);
#endif
}


/*
 *  call-seq:
 *     file.lstat   =>  stat
 *  
 *  Same as <code>IO#stat</code>, but does not follow the last symbolic
 *  link. Instead, reports on the link itself.
 *     
 *     File.symlink("testfile", "link2test")   #=> 0
 *     File.stat("testfile").size              #=> 66
 *     f = File.new("link2test")
 *     f.lstat.size                            #=> 8
 *     f.stat.size                             #=> 66
 */

static VALUE
rb_file_lstat(VALUE obj, SEL sel)
{
// #ifdef HAVE_LSTAT
//     rb_io_t *fptr;
//     struct stat st;
// 
//     rb_secure(2);
//     GetOpenFile(obj, fptr);
//     if (!fptr->path) return Qnil;
//     if (lstat(fptr->path, &st) == -1) {
//  rb_sys_fail(fptr->path);
//     }
//     return stat_new(&st);
// #else
//     return rb_io_stat(obj, 0);
// #endif
    rb_notimplement();
}

#ifndef HAVE_GROUP_MEMBER
static int
group_member(GETGROUPS_T gid)
{
#ifndef _WIN32
    if (getgid() == gid || getegid() == gid)
	return Qtrue;

# ifdef HAVE_GETGROUPS
#  ifndef NGROUPS
#   ifdef NGROUPS_MAX
#    define NGROUPS NGROUPS_MAX
#   else
#    define NGROUPS 32
#   endif
#  endif
    {
	GETGROUPS_T gary[NGROUPS];
	int anum;

	anum = getgroups(NGROUPS, gary);
	while (--anum >= 0)
	    if (gary[anum] == gid)
		return Qtrue;
    }
# endif
#endif
    return Qfalse;
}
#endif

#ifndef S_IXUGO
#  define S_IXUGO		(S_IXUSR | S_IXGRP | S_IXOTH)
#endif

#if defined(S_IXGRP) && !defined(_WIN32) && !defined(__CYGWIN__)
#define USE_GETEUID 1
#endif

#ifndef HAVE_EACCESS
int
eaccess(const char *path, int mode)
{
#ifdef USE_GETEUID
    struct stat st;
    rb_uid_t euid;

    if (stat(path, &st) < 0) {
	return -1;
    }

    euid = geteuid();

    if (euid == 0) {
	/* Root can read or write any file. */
	if (!(mode & X_OK)) {
	    return 0;
	}

	/* Root can execute any file that has any one of the execute
	   bits set. */
	if (st.st_mode & S_IXUGO) {
	    return 0;
	}

	return -1;
    }

    if (st.st_uid == euid) {       /* owner */
	mode <<= 6;
    }
    else if (group_member(st.st_gid)) {
	mode <<= 3;
    }

    if ((st.st_mode & mode) == mode) {
	return 0;
    }

    return -1;
#else
    return access(path, mode);
#endif
}
#endif


/*
 * Document-class: FileTest
 *
 *  <code>FileTest</code> implements file test operations similar to
 *  those used in <code>File::Stat</code>. It exists as a standalone
 *  module, and its methods are also insinuated into the <code>File</code>
 *  class. (Note that this is not done by inclusion: the interpreter cheats).
 *     
 */


/*
 *   File.directory?(file_name)   =>  true or false
 *   File.directory?(file_name)   =>  true or false
 *
 * Returns <code>true</code> if the named file is a directory,
 * <code>false</code> otherwise.
 *
 *    File.directory?(".")
 */

/*
 * Document-method: exist?
 *
 * call-seq:
 *   Dir.exist?(file_name)   =>  true or false
 *   Dir.exists?(file_name)   =>  true or false
 *
 * Returns <code>true</code> if the named file is a directory,
 * <code>false</code> otherwise.
 *
 */

/*
 * Document-method: directory?
 *
 * call-seq:
 *   File.directory?(file_name)   =>  true or false
 *
 * Returns <code>true</code> if the named file is a directory,
 * <code>false</code> otherwise.
 *
 *    File.directory?(".")
 */

VALUE
rb_file_directory_p(VALUE obj, SEL sel, VALUE fname)
{
#ifndef S_ISDIR
#   define S_ISDIR(m) ((m & S_IFMT) == S_IFDIR)
#endif

    struct stat st;

    if (rb_stat(fname, &st) < 0) return Qfalse;
    if (S_ISDIR(st.st_mode)) return Qtrue;
    return Qfalse;
}


/*
 * call-seq:
 *   File.pipe?(file_name)   =>  true or false
 *
 * Returns <code>true</code> if the named file is a pipe.
 */

static VALUE
rb_file_pipe_p(VALUE obj, SEL sel, VALUE fname)
{
#ifdef S_IFIFO
#  ifndef S_ISFIFO
#    define S_ISFIFO(m) ((m & S_IFMT) == S_IFIFO)
#  endif

    struct stat st;

    if (rb_stat(fname, &st) < 0) return Qfalse;
    if (S_ISFIFO(st.st_mode)) return Qtrue;

#endif
    return Qfalse;
}

/*
 * call-seq:
 *   File.symlink?(file_name)   =>  true or false
 *
 * Returns <code>true</code> if the named file is a symbolic link.
 */

static VALUE
rb_file_symlink_p(VALUE obj, SEL sel, VALUE fname)
{
#ifndef S_ISLNK
#  ifdef _S_ISLNK
#    define S_ISLNK(m) _S_ISLNK(m)
#  else
#    ifdef _S_IFLNK
#      define S_ISLNK(m) ((m & S_IFMT) == _S_IFLNK)
#    else
#      ifdef S_IFLNK
#	 define S_ISLNK(m) ((m & S_IFMT) == S_IFLNK)
#      endif
#    endif
#  endif
#endif

#ifdef S_ISLNK
    struct stat st;

    rb_secure(2);
    FilePathValue(fname);
    if (lstat(StringValueCStr(fname), &st) < 0) return Qfalse;
    if (S_ISLNK(st.st_mode)) return Qtrue;
#endif

    return Qfalse;
}

/*
 * call-seq:
 *   File.socket?(file_name)   =>  true or false
 *
 * Returns <code>true</code> if the named file is a socket.
 */

static VALUE
rb_file_socket_p(VALUE obj, SEL sel, VALUE fname)
{
#ifndef S_ISSOCK
#  ifdef _S_ISSOCK
#    define S_ISSOCK(m) _S_ISSOCK(m)
#  else
#    ifdef _S_IFSOCK
#      define S_ISSOCK(m) ((m & S_IFMT) == _S_IFSOCK)
#    else
#      ifdef S_IFSOCK
#	 define S_ISSOCK(m) ((m & S_IFMT) == S_IFSOCK)
#      endif
#    endif
#  endif
#endif

#ifdef S_ISSOCK
    struct stat st;

    if (rb_stat(fname, &st) < 0) return Qfalse;
    if (S_ISSOCK(st.st_mode)) return Qtrue;

#endif
    return Qfalse;
}

/*
 * call-seq:
 *   File.blockdev?(file_name)   =>  true or false
 *
 * Returns <code>true</code> if the named file is a block device.
 */

static VALUE
rb_file_blockdev_p(VALUE obj, SEL sel, VALUE fname)
{
#ifndef S_ISBLK
#   ifdef S_IFBLK
#	define S_ISBLK(m) ((m & S_IFMT) == S_IFBLK)
#   else
#	define S_ISBLK(m) (0)  /* anytime false */
#   endif
#endif

#ifdef S_ISBLK
    struct stat st;

    if (rb_stat(fname, &st) < 0) return Qfalse;
    if (S_ISBLK(st.st_mode)) return Qtrue;

#endif
    return Qfalse;
}

/*
 * call-seq:
 *   File.chardev?(file_name)   =>  true or false
 *
 * Returns <code>true</code> if the named file is a character device.
 */
static VALUE
rb_file_chardev_p(VALUE obj, SEL sel, VALUE fname)
{
#ifndef S_ISCHR
#   define S_ISCHR(m) ((m & S_IFMT) == S_IFCHR)
#endif

    struct stat st;

    if (rb_stat(fname, &st) < 0) return Qfalse;
    if (S_ISCHR(st.st_mode)) return Qtrue;

    return Qfalse;
}


/*
 * call-seq:
 *    File.exist?(file_name)    =>  true or false
 *    File.exists?(file_name)   =>  true or false
 *
 * Return <code>true</code> if the named file exists.
 */

static VALUE
rb_file_exist_p(VALUE obj, SEL sel, VALUE fname)
{
    struct stat st;

    if (rb_stat(fname, &st) < 0) return Qfalse;
    return Qtrue;
}

/*
 * call-seq:
 *    File.readable?(file_name)   => true or false
 *
 * Returns <code>true</code> if the named file is readable by the effective
 * user id of this process.
 */

static VALUE
rb_file_readable_p(VALUE obj, SEL sel, VALUE fname)
{
    rb_secure(2);
    FilePathValue(fname);
    if (eaccess(StringValueCStr(fname), R_OK) < 0) return Qfalse;
    return Qtrue;
}

/*
 * call-seq:
 *    File.readable_real?(file_name)   => true or false
 *
 * Returns <code>true</code> if the named file is readable by the real
 * user id of this process.
 */

static VALUE
rb_file_readable_real_p(VALUE obj, SEL sel, VALUE fname)
{
    rb_secure(2);
    FilePathValue(fname);
    if (access(StringValueCStr(fname), R_OK) < 0) return Qfalse;
    return Qtrue;
}

#ifndef S_IRUGO
#  define S_IRUGO		(S_IRUSR | S_IRGRP | S_IROTH)
#endif

#ifndef S_IWUGO
#  define S_IWUGO		(S_IWUSR | S_IWGRP | S_IWOTH)
#endif

/*
 * call-seq:
 *    File.world_readable?(file_name)   => fixnum or nil
 *
 * If <i>file_name</i> is readable by others, returns an integer
 * representing the file permission bits of <i>file_name</i>. Returns
 * <code>nil</code> otherwise. The meaning of the bits is platform
 * dependent; on Unix systems, see <code>stat(2)</code>.
 *     
 *    File.world_readable?("/etc/passwd")	    # => 420
 *    m = File.world_readable?("/etc/passwd")
 *    sprintf("%o", m)				    # => "644"
 */

static VALUE
rb_file_world_readable_p(VALUE obj, SEL sel, VALUE fname)
{
#ifdef S_IROTH
    struct stat st;

    if (rb_stat(fname, &st) < 0) return Qnil;
    if ((st.st_mode & (S_IROTH)) == S_IROTH) {
	return UINT2NUM(st.st_mode & (S_IRUGO|S_IWUGO|S_IXUGO));
    }
#endif
    return Qnil;
}

/*
 * call-seq:
 *    File.writable?(file_name)   => true or false
 *
 * Returns <code>true</code> if the named file is writable by the effective
 * user id of this process.
 */

static VALUE
rb_file_writable_p(VALUE obj, SEL sel, VALUE fname)
{
    rb_secure(2);
    FilePathValue(fname);
    if (eaccess(StringValueCStr(fname), W_OK) < 0) return Qfalse;
    return Qtrue;
}

/*
 * call-seq:
 *    File.writable_real?(file_name)   => true or false
 *
 * Returns <code>true</code> if the named file is writable by the real
 * user id of this process.
 */

static VALUE
rb_file_writable_real_p(VALUE obj, SEL sel, VALUE fname)
{
    rb_secure(2);
    FilePathValue(fname);
    if (access(StringValueCStr(fname), W_OK) < 0) return Qfalse;
    return Qtrue;
}

/*
 * call-seq:
 *    File.world_writable?(file_name)   => fixnum or nil
 *
 * If <i>file_name</i> is writable by others, returns an integer
 * representing the file permission bits of <i>file_name</i>. Returns
 * <code>nil</code> otherwise. The meaning of the bits is platform
 * dependent; on Unix systems, see <code>stat(2)</code>.
 *     
 *    File.world_writable?("/tmp")		    #=> 511
 *    m = File.world_writable?("/tmp")
 *    sprintf("%o", m)				    #=> "777"
 */

static VALUE
rb_file_world_writable_p(VALUE obj, SEL sel, VALUE fname)
{
#ifdef S_IWOTH
    struct stat st;

    if (rb_stat(fname, &st) < 0) return Qfalse;
    if ((st.st_mode & (S_IWOTH)) == S_IWOTH) {
	return UINT2NUM(st.st_mode & (S_IRUGO|S_IWUGO|S_IXUGO));
    }
#endif
    return Qnil;
}

/*
 * call-seq:
 *    File.executable?(file_name)   => true or false
 *
 * Returns <code>true</code> if the named file is executable by the effective
 * user id of this process.
 */

static VALUE
rb_file_executable_p(VALUE obj, SEL sel, VALUE fname)
{
    rb_secure(2);
    FilePathValue(fname);
    if (eaccess(StringValueCStr(fname), X_OK) < 0) return Qfalse;
    return Qtrue;
}

/*
 * call-seq:
 *    File.executable_real?(file_name)   => true or false
 *
 * Returns <code>true</code> if the named file is executable by the real
 * user id of this process.
 */

static VALUE
rb_file_executable_real_p(VALUE obj, SEL sel, VALUE fname)
{
    rb_secure(2);
    FilePathValue(fname);
    if (access(StringValueCStr(fname), X_OK) < 0) return Qfalse;
    return Qtrue;
}

#ifndef S_ISREG
#   define S_ISREG(m) ((m & S_IFMT) == S_IFREG)
#endif

/*
 * call-seq:
 *    File.file?(file_name)   => true or false
 *
 * Returns <code>true</code> if the named file exists and is a
 * regular file.
 */

static VALUE
rb_file_file_p(VALUE obj, SEL sel, VALUE fname)
{
    struct stat st;

    if (rb_stat(fname, &st) < 0) return Qfalse;
    if (S_ISREG(st.st_mode)) return Qtrue;
    return Qfalse;
}

/*
 * call-seq:
 *    File.zero?(file_name)   => true or false
 *
 * Returns <code>true</code> if the named file exists and has
 * a zero size.
 */

static VALUE
rb_file_zero_p(VALUE obj, SEL sel, VALUE fname)
{
    struct stat st;

    if (rb_stat(fname, &st) < 0) return Qfalse;
    if (st.st_size == 0) return Qtrue;
    return Qfalse;
}

/*
 * call-seq:
 *    File.size?(file_name)   => Integer or nil
 *
 * Returns +nil+ if +file_name+ doesn't exist or has zero size, the size of the
 * file otherwise.
 */

static VALUE
rb_file_size_p(VALUE obj, SEL sel, VALUE fname)
{
    struct stat st;

    if (rb_stat(fname, &st) < 0) return Qnil;
    if (st.st_size == 0) return Qnil;
    return OFFT2NUM(st.st_size);
}

/*
 * call-seq:
 *    File.owned?(file_name)   => true or false
 *
 * Returns <code>true</code> if the named file exists and the
 * effective used id of the calling process is the owner of
 * the file.
 */

static VALUE
rb_file_owned_p(VALUE obj, SEL sel, VALUE fname)
{
    struct stat st;

    if (rb_stat(fname, &st) < 0) return Qfalse;
    if (st.st_uid == geteuid()) return Qtrue;
    return Qfalse;
}

static VALUE
rb_file_rowned_p(VALUE obj, VALUE fname)
{
    struct stat st;

    if (rb_stat(fname, &st) < 0) return Qfalse;
    if (st.st_uid == getuid()) return Qtrue;
    return Qfalse;
}

/*
 * call-seq:
 *    File.grpowned?(file_name)   => true or false
 *
 * Returns <code>true</code> if the named file exists and the
 * effective group id of the calling process is the owner of
 * the file. Returns <code>false</code> on Windows.
 */

static VALUE
rb_file_grpowned_p(VALUE obj, SEL sel, VALUE fname)
{
#ifndef _WIN32
    struct stat st;

    if (rb_stat(fname, &st) < 0) return Qfalse;
    if (group_member(st.st_gid)) return Qtrue;
#endif
    return Qfalse;
}

#if defined(S_ISUID) || defined(S_ISGID) || defined(S_ISVTX)
static VALUE
check3rdbyte(VALUE fname, int mode)
{
    struct stat st;

    rb_secure(2);
    FilePathValue(fname);
    if (stat(StringValueCStr(fname), &st) < 0) return Qfalse;
    if (st.st_mode & mode) return Qtrue;
    return Qfalse;
}
#endif

/*
 * call-seq:
 *   File.setuid?(file_name)   =>  true or false
 *
 * Returns <code>true</code> if the named file has the setuid bit set.
 */

static VALUE
rb_file_suid_p(VALUE obj, SEL sel, VALUE fname)
{
#ifdef S_ISUID
    return check3rdbyte(fname, S_ISUID);
#else
    return Qfalse;
#endif
}

/*
 * call-seq:
 *   File.setgid?(file_name)   =>  true or false
 *
 * Returns <code>true</code> if the named file has the setgid bit set.
 */

static VALUE
rb_file_sgid_p(VALUE obj, SEL sel, VALUE fname)
{
#ifdef S_ISGID
    return check3rdbyte(fname, S_ISGID);
#else
    return Qfalse;
#endif
}

/*
 * call-seq:
 *   File.sticky?(file_name)   =>  true or false
 *
 * Returns <code>true</code> if the named file has the sticky bit set.
 */

static VALUE
rb_file_sticky_p(VALUE obj, SEL sel, VALUE fname)
{
#ifdef S_ISVTX
    return check3rdbyte(fname, S_ISVTX);
#else
    return Qnil;
#endif
}

/*
 * call-seq:
 *   File.identical?(file_1, file_2)   =>  true or false
 *
 * Returns <code>true</code> if the named files are identical.
 *
 *     open("a", "w") {}
 *     p File.identical?("a", "a")      #=> true
 *     p File.identical?("a", "./a")    #=> true
 *     File.link("a", "b")
 *     p File.identical?("a", "b")      #=> true
 *     File.symlink("a", "c")
 *     p File.identical?("a", "c")      #=> true
 *     open("d", "w") {}
 *     p File.identical?("a", "d")      #=> false
 */

static VALUE
rb_file_identical_p(VALUE obj, SEL sel, VALUE fname1, VALUE fname2)
{
    struct stat st1, st2;

    if (rb_stat(fname1, &st1) < 0) return Qfalse;
    if (rb_stat(fname2, &st2) < 0) return Qfalse;
    if (st1.st_dev != st2.st_dev) return Qfalse;
    if (st1.st_ino != st2.st_ino) return Qfalse;
    return Qtrue;
}

/*
 * call-seq:
 *    File.size(file_name)   => integer
 *
 * Returns the size of <code>file_name</code>.
 */

static VALUE
rb_file_s_size(VALUE klass, SEL sel, VALUE fname)
{
    struct stat st;

    if (rb_stat(fname, &st) < 0)
	rb_sys_fail(StringValueCStr(fname));
    return OFFT2NUM(st.st_size);
}

static VALUE
rb_file_ftype(const struct stat *st)
{
    const char *t;

    if (S_ISREG(st->st_mode)) {
	t = "file";
    }
    else if (S_ISDIR(st->st_mode)) {
	t = "directory";
    }
    else if (S_ISCHR(st->st_mode)) {
	t = "characterSpecial";
    }
#ifdef S_ISBLK
    else if (S_ISBLK(st->st_mode)) {
	t = "blockSpecial";
    }
#endif
#ifdef S_ISFIFO
    else if (S_ISFIFO(st->st_mode)) {
	t = "fifo";
    }
#endif
#ifdef S_ISLNK
    else if (S_ISLNK(st->st_mode)) {
	t = "link";
    }
#endif
#ifdef S_ISSOCK
    else if (S_ISSOCK(st->st_mode)) {
	t = "socket";
    }
#endif
    else {
	t = "unknown";
    }

    return rb_usascii_str_new2(t);
}

/*
 *  call-seq:
 *     File.ftype(file_name)   => string
 *  
 *  Identifies the type of the named file; the return string is one of
 *  ``<code>file</code>'', ``<code>directory</code>'',
 *  ``<code>characterSpecial</code>'', ``<code>blockSpecial</code>'',
 *  ``<code>fifo</code>'', ``<code>link</code>'',
 *  ``<code>socket</code>'', or ``<code>unknown</code>''.
 *     
 *     File.ftype("testfile")            #=> "file"
 *     File.ftype("/dev/tty")            #=> "characterSpecial"
 *     File.ftype("/tmp/.X11-unix/X0")   #=> "socket"
 */

static VALUE
rb_file_s_ftype(VALUE klass, SEL sel, VALUE fname)
{
    struct stat st;

    rb_secure(2);
    FilePathValue(fname);
    if (lstat(StringValueCStr(fname), &st) == -1) {
	rb_sys_fail(RSTRING_PTR(fname));
    }

    return rb_file_ftype(&st);
}

/*
 *  call-seq:
 *     File.atime(file_name)  =>  time
 *  
 *  Returns the last access time for the named file as a Time object).
 *     
 *     File.atime("testfile")   #=> Wed Apr 09 08:51:48 CDT 2003
 *     
 */

static VALUE
rb_file_s_atime(VALUE klass, SEL sel, VALUE fname)
{
    struct stat st;

    if (rb_stat(fname, &st) < 0)
	rb_sys_fail(StringValueCStr(fname));
    return stat_atime(&st);
}

/*
 *  call-seq:
 *     file.atime    => time
 *  
 *  Returns the last access time (a <code>Time</code> object)
 *   for <i>file</i>, or epoch if <i>file</i> has not been accessed.
 *     
 *     File.new("testfile").atime   #=> Wed Dec 31 18:00:00 CST 1969
 *     
 */

static VALUE
rb_file_atime(VALUE obj, SEL sel)
{
    //     rb_io_t *fptr;
    //     struct stat st;
    // 
    //     GetOpenFile(obj, fptr);
    //     if (fstat(fptr->fd, &st) == -1) {
    // rb_sys_fail(fptr->path);
    //     }
    //     return stat_atime(&st);
        rb_notimplement();
}

/*
 *  call-seq:
 *     File.mtime(file_name)  =>  time
 *  
 *  Returns the modification time for the named file as a Time object.
 *     
 *     File.mtime("testfile")   #=> Tue Apr 08 12:58:04 CDT 2003
 *     
 */

static VALUE
rb_file_s_mtime(VALUE klass, SEL sel, VALUE fname)
{
    struct stat st;

    if (rb_stat(fname, &st) < 0)
	rb_sys_fail(RSTRING_PTR(fname));
    return stat_mtime(&st);
}

/*
 *  call-seq:
 *     file.mtime -> time
 *  
 *  Returns the modification time for <i>file</i>.
 *     
 *     File.new("testfile").mtime   #=> Wed Apr 09 08:53:14 CDT 2003
 *     
 */

static VALUE
rb_file_mtime(VALUE obj, SEL sel)
{
    //     rb_io_t *fptr;
    //     struct stat st;
    // 
    //     GetOpenFile(obj, fptr);
    //     if (fstat(fptr->fd, &st) == -1) {
    // rb_sys_fail(fptr->path);
    //     }
    //     return stat_mtime(&st);
    rb_notimplement();
}

/*
 *  call-seq:
 *     File.ctime(file_name)  => time
 *  
 *  Returns the change time for the named file (the time at which
 *  directory information about the file was changed, not the file
 *  itself).
 *     
 *     File.ctime("testfile")   #=> Wed Apr 09 08:53:13 CDT 2003
 *     
 */

static VALUE
rb_file_s_ctime(VALUE klass, SEL sel, VALUE fname)
{
    struct stat st;

    if (rb_stat(fname, &st) < 0)
	rb_sys_fail(RSTRING_PTR(fname));
    return stat_ctime(&st);
}

/*
 *  call-seq:
 *     file.ctime -> time
 *  
 *  Returns the change time for <i>file</i> (that is, the time directory
 *  information about the file was changed, not the file itself).
 *     
 *     File.new("testfile").ctime   #=> Wed Apr 09 08:53:14 CDT 2003
 *     
 */

static VALUE
rb_file_ctime(VALUE obj, SEL sel)
{
    //     rb_io_t *fptr;
    //     struct stat st;
    // 
    //     GetOpenFile(obj, fptr);
    //     if (fstat(fptr->fd, &st) == -1) {
    // rb_sys_fail(fptr->path);
    //     }
    //     return stat_ctime(&st);
    rb_notimplement();
}

static void
chmod_internal(const char *path, void *mode)
{
    if (chmod(path, *(int *)mode) < 0)
	rb_sys_fail(path);
}

/*
 *  call-seq:
 *     File.chmod(mode_int, file_name, ... ) -> integer
 *  
 *  Changes permission bits on the named file(s) to the bit pattern
 *  represented by <i>mode_int</i>. Actual effects are operating system
 *  dependent (see the beginning of this section). On Unix systems, see
 *  <code>chmod(2)</code> for details. Returns the number of files
 *  processed.
 *     
 *     File.chmod(0644, "testfile", "out")   #=> 2
 */

static VALUE
rb_file_s_chmod(VALUE rcv, SEL sel, int argc, VALUE *argv)
{
    VALUE vmode;
    VALUE rest;
    int mode;
    long n;

    rb_secure(2);
    rb_scan_args(argc, argv, "1*", &vmode, &rest);
    mode = NUM2INT(vmode);

    n = apply2files(chmod_internal, rest, &mode);
    return LONG2FIX(n);
}

/*
 *  call-seq:
 *     file.chmod(mode_int)   => 0
 *  
 *  Changes permission bits on <i>file</i> to the bit pattern
 *  represented by <i>mode_int</i>. Actual effects are platform
 *  dependent; on Unix systems, see <code>chmod(2)</code> for details.
 *  Follows symbolic links. Also see <code>File#lchmod</code>.
 *     
 *     f = File.new("out", "w");
 *     f.chmod(0644)   #=> 0
 */

static VALUE
rb_file_chmod(VALUE obj, SEL sel, VALUE vmode)
{
//     rb_io_t *fptr;
//     int mode;
// 
//     rb_secure(2);
//     mode = NUM2INT(vmode);
// 
//     GetOpenFile(obj, fptr);
// #ifdef HAVE_FCHMOD
//     if (fchmod(fptr->fd, mode) == -1)
//  rb_sys_fail(fptr->path);
// #else
//     if (!fptr->path) return Qnil;
//     if (chmod(fptr->path, mode) == -1)
//  rb_sys_fail(fptr->path);
// #endif
// 
//     return INT2FIX(0);
    rb_notimplement();
}

#if defined(HAVE_LCHMOD)
static void
lchmod_internal(const char *path, void *mode)
{
    if (lchmod(path, (int)(VALUE)mode) < 0)
	rb_sys_fail(path);
}

/*
 *  call-seq:
 *     File.lchmod(mode_int, file_name, ...)  => integer
 *  
 *  Equivalent to <code>File::chmod</code>, but does not follow symbolic
 *  links (so it will change the permissions associated with the link,
 *  not the file referenced by the link). Often not available.
 *     
 */

static VALUE
rb_file_s_lchmod(VALUE rcv, SEL sel, int argc, VALUE *argv)
{
    VALUE vmode;
    VALUE rest;
    long mode, n;

    rb_secure(2);
    rb_scan_args(argc, argv, "1*", &vmode, &rest);
    mode = NUM2INT(vmode);

    n = apply2files(lchmod_internal, rest, (void *)(long)mode);
    return LONG2FIX(n);
}
#else
static VALUE
rb_file_s_lchmod(int argc, VALUE *argv)
{
    rb_notimplement();
    return Qnil;		/* not reached */
}
#endif

struct chown_args {
    rb_uid_t owner;
    rb_gid_t group;
};

static void
chown_internal(const char *path, void *arg)
{
    struct chown_args *args = arg;
    if (chown(path, args->owner, args->group) < 0)
	rb_sys_fail(path);
}

/*
 *  call-seq:
 *     File.chown(owner_int, group_int, file_name,... ) -> integer
 *  
 *  Changes the owner and group of the named file(s) to the given
 *  numeric owner and group id's. Only a process with superuser
 *  privileges may change the owner of a file. The current owner of a
 *  file may change the file's group to any group to which the owner
 *  belongs. A <code>nil</code> or -1 owner or group id is ignored.
 *  Returns the number of files processed.
 *     
 *     File.chown(nil, 100, "testfile")
 *     
 */

static VALUE
rb_file_s_chown(VALUE rcv, SEL sel, int argc, VALUE *argv)
{
    VALUE o, g, rest;
    struct chown_args arg;
    long n;

    rb_secure(2);
    rb_scan_args(argc, argv, "2*", &o, &g, &rest);
    if (NIL_P(o)) {
	arg.owner = -1;
    }
    else {
	arg.owner = NUM2UIDT(o);
    }
    if (NIL_P(g)) {
	arg.group = -1;
    }
    else {
	arg.group = NUM2GIDT(g);
    }

    n = apply2files(chown_internal, rest, &arg);
    return LONG2FIX(n);
}

/*
 *  call-seq:
 *     file.chown(owner_int, group_int )   => 0
 *  
 *  Changes the owner and group of <i>file</i> to the given numeric
 *  owner and group id's. Only a process with superuser privileges may
 *  change the owner of a file. The current owner of a file may change
 *  the file's group to any group to which the owner belongs. A
 *  <code>nil</code> or -1 owner or group id is ignored. Follows
 *  symbolic links. See also <code>File#lchown</code>.
 *     
 *     File.new("testfile").chown(502, 1000)
 *     
 */

static VALUE
rb_file_chown(VALUE obj, SEL sel, VALUE owner, VALUE group)
{
    rb_secure(2);

    const int o = NIL_P(owner) ? -1 : NUM2INT(owner);
    const int g = NIL_P(group) ? -1 : NUM2INT(group);
    rb_io_t *io_struct = ExtractIOStruct(obj);

    if (fchown(io_struct->fd, o, g) == -1) {
	rb_sys_fail(RSTRING_PTR(io_struct->path));
    }

    return INT2FIX(0);
}

#if defined(HAVE_LCHOWN) && !defined(__CHECKER__)
static void
lchown_internal(const char *path, void *arg)
{
    struct chown_args *args = arg;
    if (lchown(path, args->owner, args->group) < 0)
	rb_sys_fail(path);
}


/*
 *  call-seq:
 *     file.lchown(owner_int, group_int, file_name,..) => integer
 *  
 *  Equivalent to <code>File::chown</code>, but does not follow symbolic
 *  links (so it will change the owner associated with the link, not the
 *  file referenced by the link). Often not available. Returns number
 *  of files in the argument list.
 *     
 */

static VALUE
rb_file_s_lchown(VALUE rcv, SEL sel, int argc, VALUE *argv)
{
    VALUE o, g, rest;
    struct chown_args arg;
    long n;

    rb_secure(2);
    rb_scan_args(argc, argv, "2*", &o, &g, &rest);
    if (NIL_P(o)) {
	arg.owner = -1;
    }
    else {
	arg.owner = NUM2UIDT(o);
    }
    if (NIL_P(g)) {
	arg.group = -1;
    }
    else {
	arg.group = NUM2GIDT(g);
    }

    n = apply2files(lchown_internal, rest, &arg);
    return LONG2FIX(n);
}
#else
static VALUE
rb_file_s_lchown(int argc, VALUE *argv)
{
    rb_notimplement();
}
#endif

struct timespec rb_time_timespec(VALUE time);

#if defined(HAVE_UTIMES)

static void
utime_internal(const char *path, void *arg)
{
    struct timespec *tsp = arg;
    struct timeval tvbuf[2], *tvp = arg;

#ifdef HAVE_UTIMENSAT
    static int try_utimensat = 1;

    if (try_utimensat) {
        struct timespec *tsp = arg;
        if (utimensat(AT_FDCWD, path, tsp, 0) < 0) {
            if (errno == ENOSYS) {
                try_utimensat = 0;
                goto no_utimensat;
            }
            rb_sys_fail(path);
        }
        return;
    }
no_utimensat:
#endif

    if (tsp) {
        tvbuf[0].tv_sec = tsp[0].tv_sec;
        tvbuf[0].tv_usec = tsp[0].tv_nsec / 1000;
        tvbuf[1].tv_sec = tsp[1].tv_sec;
        tvbuf[1].tv_usec = tsp[1].tv_nsec / 1000;
        tvp = tvbuf;
    }
    if (utimes(path, tvp) < 0)
	rb_sys_fail(path);
}

#else

#if !defined HAVE_UTIME_H && !defined HAVE_SYS_UTIME_H
struct utimbuf {
    long actime;
    long modtime;
};
#endif

static void
utime_internal(const char *path, void *arg)
{
    struct timespec *tsp = arg;
    struct utimbuf utbuf, *utp = NULL;
    if (tsp) {
        utbuf.actime = tsp[0].tv_sec;
        utbuf.modtime = tsp[1].tv_sec;
        utp = &utbuf;
    }
    if (utime(path, utp) < 0)
	rb_sys_fail(path);
}

#endif

/*
 * call-seq:
 *  File.utime(atime, mtime, file_name,...)   =>  integer
 *
 * Sets the access and modification times of each
 * named file to the first two arguments. Returns
 * the number of file names in the argument list.
 */

static VALUE
rb_file_s_utime(VALUE rcv, SEL sel, int argc, VALUE *argv)
{
    VALUE atime, mtime, rest;
    struct timespec tss[2], *tsp = NULL;
    long n;

    rb_secure(2);
    rb_scan_args(argc, argv, "2*", &atime, &mtime, &rest);

    if (!NIL_P(atime) || !NIL_P(mtime)) {
	tsp = tss;
	tsp[0] = rb_time_timespec(atime);
	tsp[1] = rb_time_timespec(mtime);
    }

    n = apply2files(utime_internal, rest, tsp);
    return LONG2FIX(n);
}


NORETURN(static void sys_fail2(VALUE,VALUE));
static void
sys_fail2(VALUE s1, VALUE s2)
{
    char *buf;
#ifdef MAX_PATH
    const int max_pathlen = MAX_PATH;
#else
    const int max_pathlen = MAXPATHLEN;
#endif
    const char *e1, *e2;
    int len = 5;
    int l1 = RSTRING_LEN(s1), l2 = RSTRING_LEN(s2);

    e1 = e2 = "";
    if (l1 > max_pathlen) {
	l1 = max_pathlen - 3;
	e1 = "...";
	len += 3;
    }
    if (l2 > max_pathlen) {
	l2 = max_pathlen - 3;
	e2 = "...";
	len += 3;
    }
    len += l1 + l2;
    buf = ALLOCA_N(char, len);
    snprintf(buf, len, "(%.*s%s, %.*s%s)",
	     l1, RSTRING_PTR(s1), e1,
	     l2, RSTRING_PTR(s2), e2);
    rb_sys_fail(buf);
}

/*
 *  call-seq:
 *     File.link(old_name, new_name)    => 0
 *  
 *  Creates a new name for an existing file using a hard link. Will not
 *  overwrite <i>new_name</i> if it already exists (raising a subclass
 *  of <code>SystemCallError</code>). Not available on all platforms.
 *     
 *     File.link("testfile", ".testfile")   #=> 0
 *     IO.readlines(".testfile")[0]         #=> "This is line one\n"
 */

static VALUE
rb_file_s_link(VALUE klass, SEL sel, VALUE from, VALUE to)
{
#ifdef HAVE_LINK
    rb_secure(2);
    FilePathValue(from);
    FilePathValue(to);

    if (link(StringValueCStr(from), StringValueCStr(to)) < 0) {
	sys_fail2(from, to);
    }
    return INT2FIX(0);
#else
    rb_notimplement();
    return Qnil;		/* not reached */
#endif
}

/*
 *  call-seq:
 *     File.symlink(old_name, new_name)   => 0
 *  
 *  Creates a symbolic link called <i>new_name</i> for the existing file
 *  <i>old_name</i>. Raises a <code>NotImplemented</code> exception on
 *  platforms that do not support symbolic links.
 *     
 *     File.symlink("testfile", "link2test")   #=> 0
 *     
 */

static VALUE
rb_file_s_symlink(VALUE klass, SEL sel, VALUE from, VALUE to)
{
#ifdef HAVE_SYMLINK
    rb_secure(2);
    FilePathValue(from);
    FilePathValue(to);

    if (symlink(StringValueCStr(from), StringValueCStr(to)) < 0) {
	sys_fail2(from, to);
    }
    return INT2FIX(0);
#else
    rb_notimplement();
    return Qnil;		/* not reached */
#endif
}

/*
 *  call-seq:
 *     File.readlink(link_name) -> file_name
 *  
 *  Returns the name of the file referenced by the given link.
 *  Not available on all platforms.
 *     
 *     File.symlink("testfile", "link2test")   #=> 0
 *     File.readlink("link2test")              #=> "testfile"
 */

static VALUE
rb_file_s_readlink(VALUE klass, SEL sel, VALUE path)
{
#ifdef HAVE_READLINK
    char *buf;
    int size = 100;
    int rv;
    VALUE v;

    rb_secure(2);
    FilePathValue(path);
    buf = xmalloc(size);
    while ((rv = readlink(RSTRING_PTR(path), buf, size)) == size
#ifdef _AIX
	    || (rv < 0 && errno == ERANGE) /* quirky behavior of GPFS */
#endif
	) {
	size *= 2;
	buf = xrealloc(buf, size);
    }
    if (rv < 0) {
	xfree(buf);
	rb_sys_fail(RSTRING_PTR(path));
    }
    v = rb_tainted_str_new(buf, rv);
    xfree(buf);

    return v;
#else
    rb_notimplement();
    return Qnil;		/* not reached */
#endif
}

static void
unlink_internal(const char *path, void *arg)
{
    if (unlink(path) < 0)
	rb_sys_fail(path);
}

/*
 *  call-seq:
 *     File.delete(file_name, ...)  => integer
 *     File.unlink(file_name, ...)  => integer
 *  
 *  Deletes the named files, returning the number of names
 *  passed as arguments. Raises an exception on any error.
 *  See also <code>Dir::rmdir</code>.
 */

static VALUE
rb_file_s_unlink(VALUE klass, SEL sel, VALUE args)
{
    long n;

    rb_secure(2);
    n = apply2files(unlink_internal, args, 0);
    return LONG2FIX(n);
}

/*
 *  call-seq:
 *     File.rename(old_name, new_name)   => 0
 *  
 *  Renames the given file to the new name. Raises a
 *  <code>SystemCallError</code> if the file cannot be renamed.
 *     
 *     File.rename("afile", "afile.bak")   #=> 0
 */

static VALUE
rb_file_s_rename(VALUE klass, SEL sel, VALUE from, VALUE to)
{
    const char *src, *dst;

    rb_secure(2);
    FilePathValue(from);
    FilePathValue(to);
    src = StringValueCStr(from);
    dst = StringValueCStr(to);
    if (rename(src, dst) < 0) {
	sys_fail2(from, to);
    }

    return INT2FIX(0);
}

/*
 *  call-seq:
 *     File.umask()          => integer
 *     File.umask(integer)   => integer
 *  
 *  Returns the current umask value for this process. If the optional
 *  argument is given, set the umask to that value and return the
 *  previous value. Umask values are <em>subtracted</em> from the
 *  default permissions, so a umask of <code>0222</code> would make a
 *  file read-only for everyone.
 *     
 *     File.umask(0006)   #=> 18
 *     File.umask         #=> 6
 */

static VALUE
rb_file_s_umask(VALUE rcv, SEL sel, int argc, VALUE *argv)
{
    int omask = 0;

    rb_secure(2);
    if (argc == 0) {
	omask = umask(0);
	umask(omask);
    }
    else if (argc == 1) {
	omask = umask(NUM2INT(argv[0]));
    }
    else {
	rb_raise(rb_eArgError, "wrong number of arguments");
    }
    return INT2FIX(omask);
}

#define isdirsep(x) ((x) == '/')

#define istrailinggabage(x) 0

#ifndef CharNext
# define CharNext(p) ((p) + 1)
#endif

static inline char *
skiproot(const char *path)
{
    while (isdirsep(*path)) path++;
    return (char *)path;
}

#define skipprefix(path) (path)

#define strrdirsep rb_path_last_separator
static char *
rb_path_last_separator(const char *path)
{
    char *last = NULL;
    while (*path) {
	if (isdirsep(*path)) {
	    const char *tmp = path++;
	    while (isdirsep(*path)) path++;
	    if (!*path) break;
	    last = (char *)tmp;
	}
	else {
	    path = CharNext(path);
	}
    }
    return last;
}

static char *
chompdirsep(const char *path)
{
    while (*path) {
	if (isdirsep(*path)) {
	    const char *last = path++;
	    while (isdirsep(*path)) path++;
	    if (!*path) return (char *)last;
	}
	else {
	    path = CharNext(path);
	}
    }
    return (char *)path;
}

char *
rb_path_end(const char *path)
{
    if (isdirsep(*path)) path++;
    return chompdirsep(path);
}

static int is_absolute_path(const char*);

/*
 *  call-seq:
 *     File.expand_path(file_name [, dir_string] ) -> abs_file_name
 *  
 *  Converts a pathname to an absolute pathname. Relative paths are
 *  referenced from the current working directory of the process unless
 *  <i>dir_string</i> is given, in which case it will be used as the
 *  starting point. The given pathname may start with a
 *  ``<code>~</code>'', which expands to the process owner's home
 *  directory (the environment variable <code>HOME</code> must be set
 *  correctly). ``<code>~</code><i>user</i>'' expands to the named
 *  user's home directory.
 *     
 *     File.expand_path("~oracle/bin")           #=> "/home/oracle/bin"
 *     File.expand_path("../../bin", "/tmp/x")   #=> "/bin"
 */

static VALUE
rb_file_s_expand_path(VALUE rcv, SEL sel, int argc, VALUE *argv)
{
    VALUE fname, dname;

    if (argc == 1) {
	return rb_file_expand_path(argv[0], Qnil);
    }
    rb_scan_args(argc, argv, "11", &fname, &dname);

    return rb_file_expand_path(fname, dname);
}

static int
rmext(const char *p, int l1, const char *e)
{
    int l2;

    if (!e) return 0;

    l2 = strlen(e);
    if (l2 == 2 && e[1] == '*') {
	unsigned char c = *e;
	e = p + l1;
	do {
	    if (e <= p) return 0;
	} while (*--e != c);
	return e - p;
    }
    if (l1 < l2) return l1;

#if CASEFOLD_FILESYSTEM
#define fncomp strncasecmp
#else
#define fncomp strncmp
#endif
    if (fncomp(p+l1-l2, e, l2) == 0) {
	return l1-l2;
    }
    return 0;
}

/*
 *  call-seq:
 *     File.basename(file_name [, suffix] ) -> base_name
 *  
 *  Returns the last component of the filename given in <i>file_name</i>,
 *  which must be formed using forward slashes (``<code>/</code>'')
 *  regardless of the separator used on the local file system. If
 *  <i>suffix</i> is given and present at the end of <i>file_name</i>,
 *  it is removed.
 *     
 *     File.basename("/home/gumby/work/ruby.rb")          #=> "ruby.rb"
 *     File.basename("/home/gumby/work/ruby.rb", ".rb")   #=> "ruby"
 */

static VALUE
rb_file_s_basename(VALUE rcv, SEL sel, int argc, VALUE *argv)
{
    VALUE fname, fext, basename;
    const char *name, *p;
    int f, n;

    if (rb_scan_args(argc, argv, "11", &fname, &fext) == 2) {
	StringValue(fext);
    }
    FilePathStringValue(fname);
    if (RSTRING_LEN(fname) == 0 || !*(name = RSTRING_PTR(fname))) {
	return fname;
    }
    name = skipprefix(name);
    while (isdirsep(*name))
	name++;
    if (!*name) {
	p = name - 1;
	f = 1;
    }
    else {
	if (!(p = strrdirsep(name))) {
	    p = name;
	}
	else {
	    while (isdirsep(*p)) p++; /* skip last / */
	}
	n = chompdirsep(p) - p;
	if (NIL_P(fext) || !(f = rmext(p, n, StringValueCStr(fext)))) {
	    f = n;
	}
	if (f == RSTRING_LEN(fname)) return fname;
    }
    basename = rb_str_new(p, f);
    OBJ_INFECT(basename, fname);
    return basename;
}

/*
 *  call-seq:
 *     File.dirname(file_name ) -> dir_name
 *  
 *  Returns all components of the filename given in <i>file_name</i>
 *  except the last one. The filename must be formed using forward
 *  slashes (``<code>/</code>'') regardless of the separator used on the
 *  local file system.
 *     
 *     File.dirname("/home/gumby/work/ruby.rb")   #=> "/home/gumby/work"
 */

static VALUE
rb_file_s_dirname(VALUE klass, SEL sel, VALUE fname)
{
    const char *name, *root, *p;
    VALUE dirname;

    FilePathStringValue(fname);
    name = StringValueCStr(fname);
    root = skiproot(name);
    if (root > name + 1) {
	name = root - 1;
    }
    p = strrdirsep(root);
    if (!p) {
	p = root;
    }
    if (p == name)
	return rb_usascii_str_new2(".");
    dirname = rb_str_new(name, p - name);
    OBJ_INFECT(dirname, fname);
    return dirname;
}

/*
 *  call-seq:
 *     File.extname(path) -> string
 *  
 *  Returns the extension (the portion of file name in <i>path</i>
 *  after the period).
 *     
 *     File.extname("test.rb")         #=> ".rb"
 *     File.extname("a/b/d/test.rb")   #=> ".rb"
 *     File.extname("test")            #=> ""
 *     File.extname(".profile")        #=> ""
 *     
 */

static VALUE
rb_file_s_extname(VALUE klass, SEL sel, VALUE fname)
{
    const char *name, *p, *e;
    VALUE extname;

    FilePathStringValue(fname);
    name = StringValueCStr(fname);
    p = strrdirsep(name);	/* get the last path component */
    if (!p)
	p = name;
    else
	name = ++p;

    e = 0;
    while (*p) {
	if (*p == '.' || istrailinggabage(*p)) {
	    e = p;	  /* get the last dot of the last component */
	}
	else if (isdirsep(*p))
	    break;
	p = CharNext(p);
    }
    if (!e || e == name || e+1 == p)	/* no dot, or the only dot is first or end? */
	return rb_str_new(0, 0);
    extname = rb_str_new(e, p - e);	/* keep the dot, too! */
    OBJ_INFECT(extname, fname);
    return extname;
}

/*
 *  call-seq:
 *     File.path(path) -> string
 *  
 *  Returns the string representation of the path
 * 
 *     File.path("/dev/null")          #=> "/dev/null"
 *     File.path(Pathname.new("/tmp")) #=> "/tmp"
 *     
 */

static VALUE
rb_file_s_path(VALUE klass, SEL sel, VALUE fname)
{
    return rb_get_path(fname);
}

/*
 *  call-seq:
 *     File.split(file_name)   => array
 *  
 *  Splits the given string into a directory and a file component and
 *  returns them in a two-element array. See also
 *  <code>File::dirname</code> and <code>File::basename</code>.
 *     
 *     File.split("/home/gumby/.profile")   #=> ["/home/gumby", ".profile"]
 */

static VALUE
rb_file_s_split(VALUE klass, SEL sel, VALUE path)
{
    FilePathStringValue(path);		/* get rid of converting twice */
    return rb_assoc_new(rb_file_s_dirname(Qnil, 0, path), rb_file_s_basename(0,0,1,&path));
}

static VALUE separator;

static VALUE rb_file_join(VALUE ary, VALUE sep);

static VALUE
rb_file_join(VALUE ary, VALUE sep)
{
    CFMutableStringRef res = CFStringCreateMutable(NULL, 0);
    CFStringRef sep_cf = (CFStringRef)sep;
    long count;

    count = RARRAY_LEN(ary);
    if (count > 0) {
      long i;
      for (i = 0; i < count; i++) {
        VALUE tmp = RARRAY_AT(ary, i);
        switch (TYPE(tmp)) {
        case T_STRING:
          break;
        case T_ARRAY:
          tmp = rb_file_join(tmp, sep);
          break;
        default:
          FilePathStringValue(tmp);
        }

        CFStringRef tmp_cf = (CFStringRef)tmp;

        if (i > 0) {
          if (CFStringHasSuffix(res, sep_cf)) {
            if (CFStringHasPrefix(tmp_cf, sep_cf)) {
              // Remove trailing slash from res if tmp starts with a slash.
              CFStringDelete(res, CFRangeMake(CFStringGetLength(res) - 1, 1));
            }
          }
          else if (!CFStringHasPrefix(tmp_cf, sep_cf)) {
            CFStringAppend(res, sep_cf);
          }
        }

        CFStringAppend(res, tmp_cf);
      }
    }

    CFMakeCollectable(res);
    return (VALUE)res;
}

/*
 *  call-seq:
 *     File.join(string, ...) -> path
 *  
 *  Returns a new string formed by joining the strings using
 *  <code>File::SEPARATOR</code>.
 *     
 *     File.join("usr", "mail", "gumby")   #=> "usr/mail/gumby"
 *     
 */

static VALUE
rb_file_s_join(VALUE klass, SEL sel, VALUE args)
{
    return rb_file_join(args, separator);
}

/*
 *  call-seq:
 *     File.truncate(file_name, integer)  => 0
 *  
 *  Truncates the file <i>file_name</i> to be at most <i>integer</i>
 *  bytes long. Not available on all platforms.
 *     
 *     f = File.new("out", "w")
 *     f.write("1234567890")     #=> 10
 *     f.close                   #=> nil
 *     File.truncate("out", 5)   #=> 0
 *     File.size("out")          #=> 5
 *     
 */

static VALUE
rb_file_s_truncate(VALUE klass, SEL sel, VALUE path, VALUE len)
{
    off_t pos;

    rb_secure(2);
    pos = NUM2OFFT(len);
    FilePathValue(path);
#ifdef HAVE_TRUNCATE
    if (truncate(StringValueCStr(path), pos) < 0)
	rb_sys_fail(RSTRING_PTR(path));
#else
# ifdef HAVE_CHSIZE
    {
	int tmpfd;

#  ifdef _WIN32
	if ((tmpfd = open(StringValueCStr(path), O_RDWR)) < 0) {
	    rb_sys_fail(RSTRING_PTR(path));
	}
#  else
	if ((tmpfd = open(StringValueCStr(path), 0)) < 0) {
	    rb_sys_fail(RSTRING_PTR(path));
	}
#  endif
	if (chsize(tmpfd, pos) < 0) {
	    close(tmpfd);
	    rb_sys_fail(RSTRING_PTR(path));
	}
	close(tmpfd);
    }
# else
    rb_notimplement();
# endif
#endif
    return INT2FIX(0);
}

/*
 *  call-seq:
 *     file.truncate(integer)    => 0
 *  
 *  Truncates <i>file</i> to at most <i>integer</i> bytes. The file
 *  must be opened for writing. Not available on all platforms.
 *     
 *     f = File.new("out", "w")
 *     f.syswrite("1234567890")   #=> 10
 *     f.truncate(5)              #=> 0
 *     f.close()                  #=> nil
 *     File.size("out")           #=> 5
 */

static VALUE
rb_file_truncate(VALUE obj, SEL sel, VALUE len)
{
//     rb_io_t *fptr;
//     off_t pos;
// 
//     rb_secure(2);
//     pos = NUM2OFFT(len);
//     GetOpenFile(obj, fptr);
//     if (!(fptr->mode & FMODE_WRITABLE)) {
//  rb_raise(rb_eIOError, "not opened for writing");
//     }
//     rb_io_flush(obj);
// #ifdef HAVE_FTRUNCATE
//     if (ftruncate(fptr->fd, pos) < 0)
//  rb_sys_fail(fptr->path);
// #else
// # ifdef HAVE_CHSIZE
//     if (chsize(fptr->fd, pos) < 0)
//  rb_sys_fail(fptr->path);
// # else
//     rb_notimplement();
// # endif
// #endif
//     return INT2FIX(0);
    rb_notimplement();
}

# ifndef LOCK_SH
#  define LOCK_SH 1
# endif
# ifndef LOCK_EX
#  define LOCK_EX 2
# endif
# ifndef LOCK_NB
#  define LOCK_NB 4
# endif
# ifndef LOCK_UN
#  define LOCK_UN 8
# endif

#ifdef __CYGWIN__
#include <winerror.h>
extern unsigned long __attribute__((stdcall)) GetLastError(void);
#endif

/*
 *  call-seq:
 *     file.flock (locking_constant ) =>  0 or false
 *  
 *  Locks or unlocks a file according to <i>locking_constant</i> (a
 *  logical <em>or</em> of the values in the table below).
 *  Returns <code>false</code> if <code>File::LOCK_NB</code> is
 *  specified and the operation would otherwise have blocked. Not
 *  available on all platforms.
 *     
 *  Locking constants (in class File):
 *
 *     LOCK_EX   | Exclusive lock. Only one process may hold an
 *               | exclusive lock for a given file at a time.
 *     ----------+------------------------------------------------
 *     LOCK_NB   | Don't block when locking. May be combined
 *               | with other lock options using logical or.
 *     ----------+------------------------------------------------
 *     LOCK_SH   | Shared lock. Multiple processes may each hold a
 *               | shared lock for a given file at the same time.
 *     ----------+------------------------------------------------
 *     LOCK_UN   | Unlock.
 *
 *  Example:
 *
 *     File.new("testfile").flock(File::LOCK_UN)   #=> 0
 *     
 */
#if 0
static VALUE
rb_file_flock(VALUE obj, SEL sel, VALUE operation)
{
#ifndef __CHECKER__
    rb_io_t *fptr;
    int op[2], op1;

    rb_secure(2);
    op[1] = op1 = NUM2INT(operation);
    GetOpenFile(obj, fptr);
    op[0] = fptr->fd;

    if (fptr->mode & FMODE_WRITABLE) {
	rb_io_flush(obj);
    }

    while (flock(op[0], op[1]) < 0) {
//    while ((int)rb_thread_blocking_region(rb_thread_flock, op, RB_UBF_DFL, 0) < 0) {
	switch (errno) {
	  case EAGAIN:
	  case EACCES:
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
	  case EWOULDBLOCK:
#endif
	    if (op1 & LOCK_NB) return Qfalse;
	    rb_thread_polling();
	    rb_io_check_closed(fptr);
	    continue;

	  case EINTR:
#if defined(ERESTART)
	  case ERESTART:
#endif
	    break;

	  default:
	    rb_sys_fail(fptr->path);
	}
    }
#endif
    return INT2FIX(0);
}
#undef flock
#endif

static void
test_check(int n, int argc, VALUE *argv)
{
    int i;

    rb_secure(2);
    n+=1;
    if (n != argc) rb_raise(rb_eArgError, "wrong number of arguments (%d for %d)", argc, n);
    for (i=1; i<n; i++) {
	switch (TYPE(argv[i])) {
	  case T_STRING:
	  default:
	    FilePathValue(argv[i]);
	    break;
	  case T_FILE:
	    break;
	}
    }
}

#define CHECK(n) test_check((n), argc, argv)

/*
 *  call-seq:
 *     test(int_cmd, file1 [, file2] ) => obj
 *  
 *  Uses the integer <i>aCmd</i> to perform various tests on
 *  <i>file1</i> (first table below) or on <i>file1</i> and
 *  <i>file2</i> (second table).
 *     
 *  File tests on a single file:
 *
 *    Test   Returns   Meaning
 *     ?A  | Time    | Last access time for file1
 *     ?b  | boolean | True if file1 is a block device
 *     ?c  | boolean | True if file1 is a character device
 *     ?C  | Time    | Last change time for file1
 *     ?d  | boolean | True if file1 exists and is a directory
 *     ?e  | boolean | True if file1 exists
 *     ?f  | boolean | True if file1 exists and is a regular file
 *     ?g  | boolean | True if file1 has the \CF{setgid} bit
 *         |         | set (false under NT)
 *     ?G  | boolean | True if file1 exists and has a group
 *         |         | ownership equal to the caller's group
 *     ?k  | boolean | True if file1 exists and has the sticky bit set
 *     ?l  | boolean | True if file1 exists and is a symbolic link
 *     ?M  | Time    | Last modification time for file1
 *     ?o  | boolean | True if file1 exists and is owned by 
 *         |         | the caller's effective uid
 *     ?O  | boolean | True if file1 exists and is owned by
 *         |         | the caller's real uid
 *     ?p  | boolean | True if file1 exists and is a fifo
 *     ?r  | boolean | True if file1 is readable by the effective
 *         |         | uid/gid of the caller
 *     ?R  | boolean | True if file is readable by the real
 *         |         | uid/gid of the caller
 *     ?s  | int/nil | If file1 has nonzero size, return the size,
 *         |         | otherwise return nil
 *     ?S  | boolean | True if file1 exists and is a socket
 *     ?u  | boolean | True if file1 has the setuid bit set
 *     ?w  | boolean | True if file1 exists and is writable by
 *         |         | the effective uid/gid
 *     ?W  | boolean | True if file1 exists and is writable by
 *         |         | the real uid/gid
 *     ?x  | boolean | True if file1 exists and is executable by
 *         |         | the effective uid/gid
 *     ?X  | boolean | True if file1 exists and is executable by
 *         |         | the real uid/gid
 *     ?z  | boolean | True if file1 exists and has a zero length
 *
 * Tests that take two files:
 *
 *     ?-  | boolean | True if file1 and file2 are identical
 *     ?=  | boolean | True if the modification times of file1
 *         |         | and file2 are equal
 *     ?<  | boolean | True if the modification time of file1
 *         |         | is prior to that of file2
 *     ?>  | boolean | True if the modification time of file1
 *         |         | is after that of file2
 */

static VALUE
rb_f_test(VALUE rcv, SEL sel, int argc, VALUE *argv)
{
    int cmd;

    if (argc == 0) rb_raise(rb_eArgError, "wrong number of arguments");
    cmd = NUM2CHR(argv[0]);
    if (cmd == 0) goto unknown;
    if (strchr("bcdefgGkloOprRsSuwWxXz", cmd)) {
	CHECK(1);
	switch (cmd) {
	  case 'b':
	    return rb_file_blockdev_p(0, 0, argv[1]);

	  case 'c':
	    return rb_file_chardev_p(0, 0, argv[1]);

	  case 'd':
	    return rb_file_directory_p(0, 0, argv[1]);

	  case 'a':
	  case 'e':
	    return rb_file_exist_p(0, 0, argv[1]);

	  case 'f':
	    return rb_file_file_p(0, 0, argv[1]);

	  case 'g':
	    return rb_file_sgid_p(0, 0, argv[1]);

	  case 'G':
	    return rb_file_grpowned_p(0, 0, argv[1]);

	  case 'k':
	    return rb_file_sticky_p(0, 0, argv[1]);

	  case 'l':
	    return rb_file_symlink_p(0, 0, argv[1]);

	  case 'o':
	    return rb_file_owned_p(0, 0, argv[1]);

	  case 'O':
	    return rb_file_rowned_p(0, argv[1]);

	  case 'p':
	    return rb_file_pipe_p(0, 0, argv[1]);

	  case 'r':
	    return rb_file_readable_p(0, 0, argv[1]);

	  case 'R':
	    return rb_file_readable_real_p(0, 0, argv[1]);

	  case 's':
	    return rb_file_size_p(0, 0, argv[1]);

	  case 'S':
	    return rb_file_socket_p(0, 0, argv[1]);

	  case 'u':
	    return rb_file_suid_p(0, 0, argv[1]);

	  case 'w':
	    return rb_file_writable_p(0, 0, argv[1]);

	  case 'W':
	    return rb_file_world_writable_p(0, 0, argv[1]);

	  case 'x':
	    return rb_file_executable_p(0, 0, argv[1]);

	  case 'X':
	    return rb_file_executable_real_p(0, 0, argv[1]);

	  case 'z':
	    return rb_file_zero_p(0, 0, argv[1]);
	}
    }

    if (strchr("MAC", cmd)) {
	struct stat st;

	CHECK(1);
	if (rb_stat(argv[1], &st) == -1) {
	    rb_sys_fail(RSTRING_PTR(argv[1]));
	}

	switch (cmd) {
	  case 'A':
	    return stat_atime(&st);
	  case 'M':
	    return stat_mtime(&st);
	  case 'C':
	    return stat_ctime(&st);
	}
    }

    if (cmd == '-') {
	CHECK(2);
	return rb_file_identical_p(0, 0, argv[1], argv[2]);
    }

    if (strchr("=<>", cmd)) {
	struct stat st1, st2;

	CHECK(2);
	if (rb_stat(argv[1], &st1) < 0) return Qfalse;
	if (rb_stat(argv[2], &st2) < 0) return Qfalse;

	switch (cmd) {
	  case '=':
	    if (st1.st_mtime == st2.st_mtime) return Qtrue;
	    return Qfalse;

	  case '>':
	    if (st1.st_mtime > st2.st_mtime) return Qtrue;
	    return Qfalse;

	  case '<':
	    if (st1.st_mtime < st2.st_mtime) return Qtrue;
	    return Qfalse;
	}
    }
  unknown:
    /* unknown command */
    if (ISPRINT(cmd)) {
	rb_raise(rb_eArgError, "unknown command ?%c", cmd);
    }
    else {
	rb_raise(rb_eArgError, "unknown command ?\\x%02X", cmd);
    }
    return Qnil;		/* not reached */
}



/*
 *  Document-class: File::Stat
 *
 *  Objects of class <code>File::Stat</code> encapsulate common status
 *  information for <code>File</code> objects. The information is
 *  recorded at the moment the <code>File::Stat</code> object is
 *  created; changes made to the file after that point will not be
 *  reflected. <code>File::Stat</code> objects are returned by
 *  <code>IO#stat</code>, <code>File::stat</code>,
 *  <code>File#lstat</code>, and <code>File::lstat</code>. Many of these
 *  methods return platform-specific values, and not all values are
 *  meaningful on all systems. See also <code>Kernel#test</code>.
 */

static VALUE
rb_stat_s_alloc(VALUE klass)
{
    return stat_new_0(klass, 0);
}

/*
 * call-seq:
 *
 *   File::Stat.new(file_name)  => stat
 *
 * Create a File::Stat object for the given file name (raising an
 * exception if the file doesn't exist).
 */

static VALUE
rb_stat_init(VALUE obj, SEL sel, VALUE fname)
{
    struct stat st, *nst;

    rb_secure(2);
    FilePathValue(fname);
    if (stat(StringValueCStr(fname), &st) == -1) {
	rb_sys_fail(RSTRING_PTR(fname));
    }
    if (DATA_PTR(obj)) {
	free(DATA_PTR(obj));
	DATA_PTR(obj) = NULL;
    }
    nst = ALLOC(struct stat);
    *nst = st;
    GC_WB(&DATA_PTR(obj), nst);

    return Qnil;
}

/* :nodoc: */
static VALUE
rb_stat_init_copy(VALUE copy, SEL sel, VALUE orig)
{
    struct stat *nst;

    if (copy == orig) return orig;
    rb_check_frozen(copy);
    /* need better argument type check */
    if (!rb_obj_is_instance_of(orig, rb_obj_class(copy))) {
	rb_raise(rb_eTypeError, "wrong argument class");
    }
    if (DATA_PTR(copy)) {
	free(DATA_PTR(copy));
	DATA_PTR(copy) = 0;
    }
    if (DATA_PTR(orig)) {
	nst = ALLOC(struct stat);
	*nst = *(struct stat*)DATA_PTR(orig);
	GC_WB(&DATA_PTR(copy), nst);
    }

    return copy;
}

/*
 *  call-seq:
 *     stat.ftype   => string
 *  
 *  Identifies the type of <i>stat</i>. The return string is one of:
 *  ``<code>file</code>'', ``<code>directory</code>'',
 *  ``<code>characterSpecial</code>'', ``<code>blockSpecial</code>'',
 *  ``<code>fifo</code>'', ``<code>link</code>'',
 *  ``<code>socket</code>'', or ``<code>unknown</code>''.
 *     
 *     File.stat("/dev/tty").ftype   #=> "characterSpecial"
 *     
 */

static VALUE
rb_stat_ftype(VALUE obj, SEL sel)
{
    return rb_file_ftype(get_stat(obj));
}

/*
 *  call-seq:
 *     stat.directory?   => true or false
 *  
 *  Returns <code>true</code> if <i>stat</i> is a directory,
 *  <code>false</code> otherwise.
 *     
 *     File.stat("testfile").directory?   #=> false
 *     File.stat(".").directory?          #=> true
 */

static VALUE
rb_stat_d(VALUE obj, SEL sel)
{
    if (S_ISDIR(get_stat(obj)->st_mode)) return Qtrue;
    return Qfalse;
}

/*
 *  call-seq:
 *     stat.pipe?    => true or false
 *  
 *  Returns <code>true</code> if the operating system supports pipes and
 *  <i>stat</i> is a pipe; <code>false</code> otherwise.
 */

static VALUE
rb_stat_p(VALUE obj, SEL sel)
{
#ifdef S_IFIFO
    if (S_ISFIFO(get_stat(obj)->st_mode)) return Qtrue;

#endif
    return Qfalse;
}

/*
 *  call-seq:
 *     stat.symlink?    => true or false
 *  
 *  Returns <code>true</code> if <i>stat</i> is a symbolic link,
 *  <code>false</code> if it isn't or if the operating system doesn't
 *  support this feature. As <code>File::stat</code> automatically
 *  follows symbolic links, <code>symlink?</code> will always be
 *  <code>false</code> for an object returned by
 *  <code>File::stat</code>.
 *     
 *     File.symlink("testfile", "alink")   #=> 0
 *     File.stat("alink").symlink?         #=> false
 *     File.lstat("alink").symlink?        #=> true
 *     
 */

static VALUE
rb_stat_l(VALUE obj, SEL sel)
{
#ifdef S_ISLNK
    if (S_ISLNK(get_stat(obj)->st_mode)) return Qtrue;
#endif
    return Qfalse;
}

/*
 *  call-seq:
 *     stat.socket?    => true or false
 *  
 *  Returns <code>true</code> if <i>stat</i> is a socket,
 *  <code>false</code> if it isn't or if the operating system doesn't
 *  support this feature.
 *     
 *     File.stat("testfile").socket?   #=> false
 *     
 */

static VALUE
rb_stat_S(VALUE obj, SEL sel)
{
#ifdef S_ISSOCK
    if (S_ISSOCK(get_stat(obj)->st_mode)) return Qtrue;

#endif
    return Qfalse;
}

/*
 *  call-seq:
 *     stat.blockdev?   => true or false
 *  
 *  Returns <code>true</code> if the file is a block device,
 *  <code>false</code> if it isn't or if the operating system doesn't
 *  support this feature.
 *     
 *     File.stat("testfile").blockdev?    #=> false
 *     File.stat("/dev/hda1").blockdev?   #=> true
 *     
 */

static VALUE
rb_stat_b(VALUE obj, SEL sel)
{
#ifdef S_ISBLK
    if (S_ISBLK(get_stat(obj)->st_mode)) return Qtrue;

#endif
    return Qfalse;
}

/*
 *  call-seq:
 *     stat.chardev?    => true or false
 *  
 *  Returns <code>true</code> if the file is a character device,
 *  <code>false</code> if it isn't or if the operating system doesn't
 *  support this feature.
 *     
 *     File.stat("/dev/tty").chardev?   #=> true
 *     
 */

static VALUE
rb_stat_c(VALUE obj, SEL sel)
{
    if (S_ISCHR(get_stat(obj)->st_mode)) return Qtrue;

    return Qfalse;
}

/*
 *  call-seq:
 *     stat.owned?    => true or false
 *  
 *  Returns <code>true</code> if the effective user id of the process is
 *  the same as the owner of <i>stat</i>.
 *     
 *     File.stat("testfile").owned?      #=> true
 *     File.stat("/etc/passwd").owned?   #=> false
 *     
 */

static VALUE
rb_stat_owned(VALUE obj, SEL sel)
{
    if (get_stat(obj)->st_uid == geteuid()) return Qtrue;
    return Qfalse;
}

static VALUE
rb_stat_rowned(VALUE obj)
{
    if (get_stat(obj)->st_uid == getuid()) return Qtrue;
    return Qfalse;
}

/*
 *  call-seq:
 *     stat.grpowned?   => true or false
 *  
 *  Returns true if the effective group id of the process is the same as
 *  the group id of <i>stat</i>. On Windows NT, returns <code>false</code>.
 *     
 *     File.stat("testfile").grpowned?      #=> true
 *     File.stat("/etc/passwd").grpowned?   #=> false
 *     
 */

static VALUE
rb_stat_grpowned(VALUE obj, SEL sel)
{
#ifndef _WIN32
    if (group_member(get_stat(obj)->st_gid)) return Qtrue;
#endif
    return Qfalse;
}

/*
 *  call-seq:
 *     stat.readable?    => true or false
 *  
 *  Returns <code>true</code> if <i>stat</i> is readable by the
 *  effective user id of this process.
 *     
 *     File.stat("testfile").readable?   #=> true
 *     
 */

static VALUE
rb_stat_r(VALUE obj, SEL sel)
{
    struct stat *st = get_stat(obj);

#ifdef USE_GETEUID
    if (geteuid() == 0) return Qtrue;
#endif
#ifdef S_IRUSR
    if (rb_stat_owned(obj, 0))
	return st->st_mode & S_IRUSR ? Qtrue : Qfalse;
#endif
#ifdef S_IRGRP
    if (rb_stat_grpowned(obj, 0))
	return st->st_mode & S_IRGRP ? Qtrue : Qfalse;
#endif
#ifdef S_IROTH
    if (!(st->st_mode & S_IROTH)) return Qfalse;
#endif
    return Qtrue;
}



/*
 *  call-seq:
 *     stat.readable_real? -> true or false
 *  
 *  Returns <code>true</code> if <i>stat</i> is readable by the real
 *  user id of this process.
 *     
 *     File.stat("testfile").readable_real?   #=> true
 *     
 */

static VALUE
rb_stat_R(VALUE obj, SEL sel)
{
    struct stat *st = get_stat(obj);

#ifdef USE_GETEUID
    if (getuid() == 0) return Qtrue;
#endif
#ifdef S_IRUSR
    if (rb_stat_rowned(obj))
	return st->st_mode & S_IRUSR ? Qtrue : Qfalse;
#endif
#ifdef S_IRGRP
    if (group_member(get_stat(obj)->st_gid))
	return st->st_mode & S_IRGRP ? Qtrue : Qfalse;
#endif
#ifdef S_IROTH
    if (!(st->st_mode & S_IROTH)) return Qfalse;
#endif
    return Qtrue;
}

/*
 * call-seq:
 *    stat.world_readable? => fixnum or nil
 *
 * If <i>stat</i> is readable by others, returns an integer
 * representing the file permission bits of <i>stat</i>. Returns
 * <code>nil</code> otherwise. The meaning of the bits is platform
 * dependent; on Unix systems, see <code>stat(2)</code>.
 *     
 *    m = File.stat("/etc/passwd").world_readable?  # => 420
 *    sprintf("%o", m)				    # => "644"
 */

static VALUE
rb_stat_wr(VALUE obj, SEL sel)
{
#ifdef S_IROTH
    if ((get_stat(obj)->st_mode & (S_IROTH)) == S_IROTH) {
      return UINT2NUM(get_stat(obj)->st_mode & (S_IRUGO|S_IWUGO|S_IXUGO));
    }
    else {
      return Qnil;
    }
#endif
}

/*
 *  call-seq:
 *     stat.writable? -> true or false
 *  
 *  Returns <code>true</code> if <i>stat</i> is writable by the
 *  effective user id of this process.
 *     
 *     File.stat("testfile").writable?   #=> true
 *     
 */

static VALUE
rb_stat_w(VALUE obj, SEL sel)
{
    struct stat *st = get_stat(obj);

#ifdef USE_GETEUID
    if (geteuid() == 0) return Qtrue;
#endif
#ifdef S_IWUSR
    if (rb_stat_owned(obj, 0))
	return st->st_mode & S_IWUSR ? Qtrue : Qfalse;
#endif
#ifdef S_IWGRP
    if (rb_stat_grpowned(obj, 0))
	return st->st_mode & S_IWGRP ? Qtrue : Qfalse;
#endif
#ifdef S_IWOTH
    if (!(st->st_mode & S_IWOTH)) return Qfalse;
#endif
    return Qtrue;
}

/*
 *  call-seq:
 *     stat.writable_real? -> true or false
 *  
 *  Returns <code>true</code> if <i>stat</i> is writable by the real
 *  user id of this process.
 *     
 *     File.stat("testfile").writable_real?   #=> true
 *     
 */

static VALUE
rb_stat_W(VALUE obj, SEL sel)
{
    struct stat *st = get_stat(obj);

#ifdef USE_GETEUID
    if (getuid() == 0) return Qtrue;
#endif
#ifdef S_IWUSR
    if (rb_stat_rowned(obj))
	return st->st_mode & S_IWUSR ? Qtrue : Qfalse;
#endif
#ifdef S_IWGRP
    if (group_member(get_stat(obj)->st_gid))
	return st->st_mode & S_IWGRP ? Qtrue : Qfalse;
#endif
#ifdef S_IWOTH
    if (!(st->st_mode & S_IWOTH)) return Qfalse;
#endif
    return Qtrue;
}

/*
 * call-seq:
 *    stat.world_writable? => fixnum or nil
 *
 * If <i>stat</i> is writable by others, returns an integer
 * representing the file permission bits of <i>stat</i>. Returns
 * <code>nil</code> otherwise. The meaning of the bits is platform
 * dependent; on Unix systems, see <code>stat(2)</code>.
 *     
 *    m = File.stat("/tmp").world_writable?	    # => 511
 *    sprintf("%o", m)				    # => "777"
 */

static VALUE
rb_stat_ww(VALUE obj, SEL sel)
{
#ifdef S_IROTH
    if ((get_stat(obj)->st_mode & (S_IWOTH)) == S_IWOTH) {
      return UINT2NUM(get_stat(obj)->st_mode & (S_IRUGO|S_IWUGO|S_IXUGO));
    }
    else {
      return Qnil;
    }
#endif
}

/*
 *  call-seq:
 *     stat.executable?    => true or false
 *  
 *  Returns <code>true</code> if <i>stat</i> is executable or if the
 *  operating system doesn't distinguish executable files from
 *  nonexecutable files. The tests are made using the effective owner of
 *  the process.
 *     
 *     File.stat("testfile").executable?   #=> false
 *     
 */

static VALUE
rb_stat_x(VALUE obj, SEL sel)
{
    struct stat *st = get_stat(obj);

#ifdef USE_GETEUID
    if (geteuid() == 0) {
	return st->st_mode & S_IXUGO ? Qtrue : Qfalse;
    }
#endif
#ifdef S_IXUSR
    if (rb_stat_owned(obj, 0))
	return st->st_mode & S_IXUSR ? Qtrue : Qfalse;
#endif
#ifdef S_IXGRP
    if (rb_stat_grpowned(obj, 0))
	return st->st_mode & S_IXGRP ? Qtrue : Qfalse;
#endif
#ifdef S_IXOTH
    if (!(st->st_mode & S_IXOTH)) return Qfalse;
#endif
    return Qtrue;
}

/*
 *  call-seq:
 *     stat.executable_real?    => true or false
 *  
 *  Same as <code>executable?</code>, but tests using the real owner of
 *  the process.
 */


static VALUE
rb_stat_X(VALUE obj, SEL sel)
{
    struct stat *st = get_stat(obj);

#ifdef USE_GETEUID
    if (getuid() == 0) {
	return st->st_mode & S_IXUGO ? Qtrue : Qfalse;
    }
#endif
#ifdef S_IXUSR
    if (rb_stat_rowned(obj))
	return st->st_mode & S_IXUSR ? Qtrue : Qfalse;
#endif
#ifdef S_IXGRP
    if (group_member(get_stat(obj)->st_gid))
	return st->st_mode & S_IXGRP ? Qtrue : Qfalse;
#endif
#ifdef S_IXOTH
    if (!(st->st_mode & S_IXOTH)) return Qfalse;
#endif
    return Qtrue;
}

/*
 *  call-seq:
 *     stat.file?    => true or false
 *  
 *  Returns <code>true</code> if <i>stat</i> is a regular file (not
 *  a device file, pipe, socket, etc.).
 *     
 *     File.stat("testfile").file?   #=> true
 *     
 */

static VALUE
rb_stat_f(VALUE obj, SEL sel)
{
    if (S_ISREG(get_stat(obj)->st_mode)) return Qtrue;
    return Qfalse;
}

/*
 *  call-seq:
 *     stat.zero?    => true or false
 *  
 *  Returns <code>true</code> if <i>stat</i> is a zero-length file;
 *  <code>false</code> otherwise.
 *     
 *     File.stat("testfile").zero?   #=> false
 *     
 */

static VALUE
rb_stat_z(VALUE obj, SEL sel)
{
    if (get_stat(obj)->st_size == 0) return Qtrue;
    return Qfalse;
}


/*
 *  call-seq:
 *     state.size    => integer
 *  
 *  Returns the size of <i>stat</i> in bytes.
 *     
 *     File.stat("testfile").size   #=> 66
 *     
 */

static VALUE
rb_stat_s(VALUE obj, SEL sel)
{
    off_t size = get_stat(obj)->st_size;

    if (size == 0) return Qnil;
    return OFFT2NUM(size);
}

/*
 *  call-seq:
 *     stat.setuid?    => true or false
 *  
 *  Returns <code>true</code> if <i>stat</i> has the set-user-id
 *  permission bit set, <code>false</code> if it doesn't or if the
 *  operating system doesn't support this feature.
 *     
 *     File.stat("/bin/su").setuid?   #=> true
 */

static VALUE
rb_stat_suid(VALUE obj, SEL sel)
{
#ifdef S_ISUID
    if (get_stat(obj)->st_mode & S_ISUID) return Qtrue;
#endif
    return Qfalse;
}

/*
 *  call-seq:
 *     stat.setgid?   => true or false
 *  
 *  Returns <code>true</code> if <i>stat</i> has the set-group-id
 *  permission bit set, <code>false</code> if it doesn't or if the
 *  operating system doesn't support this feature.
 *     
 *     File.stat("/usr/sbin/lpc").setgid?   #=> true
 *     
 */

static VALUE
rb_stat_sgid(VALUE obj, SEL sel)
{
#ifdef S_ISGID
    if (get_stat(obj)->st_mode & S_ISGID) return Qtrue;
#endif
    return Qfalse;
}

/*
 *  call-seq:
 *     stat.sticky?    => true or false
 *  
 *  Returns <code>true</code> if <i>stat</i> has its sticky bit set,
 *  <code>false</code> if it doesn't or if the operating system doesn't
 *  support this feature.
 *     
 *     File.stat("testfile").sticky?   #=> false
 *     
 */

static VALUE
rb_stat_sticky(VALUE obj, SEL sel)
{
#ifdef S_ISVTX
    if (get_stat(obj)->st_mode & S_ISVTX) return Qtrue;
#endif
    return Qfalse;
}

VALUE rb_mFConst;

void
rb_file_const(const char *name, VALUE value)
{
    rb_define_const(rb_mFConst, name, value);
}

static int
is_absolute_path(const char *path)
{
    return path[0] == '/';
}

#ifndef ENABLE_PATH_CHECK
#  define ENABLE_PATH_CHECK 1
#endif

#if ENABLE_PATH_CHECK
static int
path_check_0(VALUE path, int execpath)
{
    struct stat st;
    const char *p0 = StringValueCStr(path);
    char *p = 0, *s;

    if (!is_absolute_path(p0)) {
	char *buf = my_getcwd();
	VALUE newpath;

	newpath = rb_str_new2(buf);
	xfree(buf);

	rb_str_cat2(newpath, "/");
	rb_str_cat2(newpath, p0);
	path = newpath;
	p0 = RSTRING_PTR(path);
    }
    for (;;) {
#ifndef S_IWOTH
# define S_IWOTH 002
#endif
	if (stat(p0, &st) == 0 && S_ISDIR(st.st_mode) && (st.st_mode & S_IWOTH)
#ifdef S_ISVTX
	    && !(p && execpath && (st.st_mode & S_ISVTX))
#endif
	    && !access(p0, W_OK)) {
	    rb_warn("Insecure world writable dir %s in %sPATH, mode 0%o",
		    p0, (execpath ? "" : "LOAD_"), st.st_mode);
	    if (p) *p = '/';
	    return 0;
	}
	s = strrdirsep(p0);
	if (p) *p = '/';
	if (!s || s == p0) return 1;
	p = s;
	*p = '\0';
    }
}
#endif

static int
fpath_check(const char *path)
{
#if ENABLE_PATH_CHECK
    return path_check_0(rb_str_new2(path), Qfalse);
#else
    return 1;
#endif
}

int
rb_path_check(const char *path)
{
#if ENABLE_PATH_CHECK
    const char *p0, *p, *pend;
    const char sep = PATH_SEP_CHAR;

    if (!path) return 1;

    pend = path + strlen(path);
    p0 = path;
    p = strchr(path, sep);
    if (!p) p = pend;

    for (;;) {
	if (!path_check_0(rb_str_new(p0, p - p0), Qtrue)) {
	    return 0;		/* not safe */
	}
	p0 = p + 1;
	if (p0 > pend) break;
	p = strchr(p0, sep);
	if (!p) p = pend;
    }
#endif
    return 1;
}

static int
file_load_ok(const char *path)
{
    return eaccess(path, R_OK) == 0;
}

VALUE rb_get_load_path(void);

int
rb_find_file_ext(VALUE *filep, const char *const *ext)
{
    const char *path, *found;
    const char *f = RSTRING_PTR(*filep);
    VALUE fname, load_path;
    long i, j;

    if (f[0] == '~') {
	fname = rb_file_expand_path(*filep, Qnil);
	if (rb_safe_level() >= 2 && OBJ_TAINTED(fname)) {
	    rb_raise(rb_eSecurityError, "loading from unsafe file %s", f);
	}
	OBJ_FREEZE(fname);
	f = StringValueCStr(fname);
	*filep = fname;
    }

    if (is_absolute_path(f)) {
	for (i=0; ext[i]; i++) {
	    fname = rb_str_dup(*filep);
	    rb_str_cat2(fname, ext[i]);
	    OBJ_FREEZE(fname);
	    if (file_load_ok(StringValueCStr(fname))) {
		*filep = fname;
		return i+1;
	    }
	}
	return 0;
    }

    load_path = rb_get_load_path();
    if (!load_path) return 0;

    for (j=0; ext[j]; j++) {
	fname = rb_str_dup(*filep);
	rb_str_cat2(fname, ext[j]);
	OBJ_FREEZE(fname);
	for (i = 0; i < RARRAY_LEN(load_path); i++) {
	    VALUE str = RARRAY_AT(load_path, i);
	    char fbuf[MAXPATHLEN];

	    FilePathValue(str);
	    if (RSTRING_LEN(str) == 0) continue;
	    path = RSTRING_PTR(str);
	    found = dln_find_file_r(StringValueCStr(fname), path, fbuf, sizeof(fbuf));
	    if (found && file_load_ok(found)) {
		*filep = rb_str_new2(found);
		return j+1;
	    }
	}
    }
    RB_GC_GUARD(load_path);
    return 0;
}

VALUE
rb_find_file(VALUE path)
{
    VALUE tmp, load_path;
    char *f = StringValueCStr(path);
    const char *lpath;
    char fbuf[MAXPATHLEN];

    if (f[0] == '~') {
	path = rb_file_expand_path(path, Qnil);
	if (rb_safe_level() >= 1 && OBJ_TAINTED(path)) {
	    rb_raise(rb_eSecurityError, "loading from unsafe path %s", f);
	}
	OBJ_FREEZE(path);
	f = StringValueCStr(path);
    }

    if (is_absolute_path(f)) {
	if (rb_safe_level() >= 1 && !fpath_check(f)) {
	    rb_raise(rb_eSecurityError, "loading from unsafe file %s", f);
	}
	if (file_load_ok(f)) {
	    return path;
	}
    }

    if (rb_safe_level() >= 4) {
	rb_raise(rb_eSecurityError, "loading from non-absolute path %s", f);
    }

    load_path = rb_get_load_path();
    if (load_path) {
	long i, count;

	tmp = rb_ary_new();
	for (i=0, count=RARRAY_LEN(load_path);i < count;i++) {
	    VALUE str = RARRAY_AT(load_path, i);
	    FilePathValue(str);
	    if (RSTRING_LEN(str) > 0) {
		rb_ary_push(tmp, str);
	    }
	}
	tmp = rb_ary_join(tmp, rb_str_new2(PATH_SEP));
	if (RSTRING_LEN(tmp) == 0) {
	    lpath = 0;
	}
	else {
	    lpath = RSTRING_PTR(tmp);
	}
    }
    else {
	lpath = 0;
    }

    if (!lpath) {
	return 0;		/* no path, no load */
    }
    if (!(f = dln_find_file_r(f, lpath, fbuf, sizeof(fbuf)))) {
	return 0;
    }
    if (rb_safe_level() >= 1 && !fpath_check(f)) {
	rb_raise(rb_eSecurityError, "loading from unsafe file %s", f);
    }
    if (file_load_ok(f)) {
	tmp = rb_str_new2(f);
	OBJ_FREEZE(tmp);
	return tmp;
    }
    return 0;
}

static void
define_filetest_function(const char *name, VALUE (*func)(ANYARGS), int argc)
{
    rb_objc_define_method(*(VALUE *)rb_mFileTest, name, func, argc);
    rb_objc_define_method(*(VALUE *)rb_cFile, name, func, argc);
}


/*
 *  A <code>File</code> is an abstraction of any file object accessible
 *  by the program and is closely associated with class <code>IO</code>
 *  <code>File</code> includes the methods of module
 *  <code>FileTest</code> as class methods, allowing you to write (for
 *  example) <code>File.exist?("foo")</code>.
 *     
 *  In the description of File methods,
 *  <em>permission bits</em> are a platform-specific
 *  set of bits that indicate permissions of a file. On Unix-based
 *  systems, permissions are viewed as a set of three octets, for the
 *  owner, the group, and the rest of the world. For each of these
 *  entities, permissions may be set to read, write, or execute the
 *  file:
 *     
 *  The permission bits <code>0644</code> (in octal) would thus be
 *  interpreted as read/write for owner, and read-only for group and
 *  other. Higher-order bits may also be used to indicate the type of
 *  file (plain, directory, pipe, socket, and so on) and various other
 *  special features. If the permissions are for a directory, the
 *  meaning of the execute bit changes; when set the directory can be
 *  searched.
 *     
 *  On non-Posix operating systems, there may be only the ability to
 *  make a file read-only or read-write. In this case, the remaining
 *  permission bits will be synthesized to resemble typical values. For
 *  instance, on Windows NT the default permission bits are
 *  <code>0644</code>, which means read/write for owner, read-only for
 *  all others. The only change that can be made is to make the file
 *  read-only, which is reported as <code>0444</code>.
 */

void
Init_File(void)
{
    rb_mFileTest = rb_define_module("FileTest");
    rb_cFile = rb_define_class("File", rb_cIO);

    define_filetest_function("directory?", rb_file_directory_p, 1);
    define_filetest_function("exist?", rb_file_exist_p, 1);
    define_filetest_function("exists?", rb_file_exist_p, 1);
    define_filetest_function("readable?", rb_file_readable_p, 1);
    define_filetest_function("readable_real?", rb_file_readable_real_p, 1);
    define_filetest_function("world_readable?", rb_file_world_readable_p, 1);
    define_filetest_function("writable?", rb_file_writable_p, 1);
    define_filetest_function("writable_real?", rb_file_writable_real_p, 1);
    define_filetest_function("world_writable?", rb_file_world_writable_p, 1);
    define_filetest_function("executable?", rb_file_executable_p, 1);
    define_filetest_function("executable_real?", rb_file_executable_real_p, 1);
    define_filetest_function("file?", rb_file_file_p, 1);
    define_filetest_function("zero?", rb_file_zero_p, 1);
    define_filetest_function("size?", rb_file_size_p, 1);
    define_filetest_function("size", rb_file_s_size, 1);
    define_filetest_function("owned?", rb_file_owned_p, 1);
    define_filetest_function("grpowned?", rb_file_grpowned_p, 1);

    define_filetest_function("pipe?", rb_file_pipe_p, 1);
    define_filetest_function("symlink?", rb_file_symlink_p, 1);
    define_filetest_function("socket?", rb_file_socket_p, 1);

    define_filetest_function("blockdev?", rb_file_blockdev_p, 1);
    define_filetest_function("chardev?", rb_file_chardev_p, 1);

    define_filetest_function("setuid?", rb_file_suid_p, 1);
    define_filetest_function("setgid?", rb_file_sgid_p, 1);
    define_filetest_function("sticky?", rb_file_sticky_p, 1);

    define_filetest_function("identical?", rb_file_identical_p, 2);

    VALUE rb_ccFile = *(VALUE *)rb_cFile;
    rb_objc_define_method(rb_ccFile, "stat",  rb_file_s_stat, 1);
    rb_objc_define_method(rb_ccFile, "lstat", rb_file_s_lstat, 1);
    rb_objc_define_method(rb_ccFile, "ftype", rb_file_s_ftype, 1);

    rb_objc_define_method(rb_ccFile, "atime", rb_file_s_atime, 1);
    rb_objc_define_method(rb_ccFile, "mtime", rb_file_s_mtime, 1);
    rb_objc_define_method(rb_ccFile, "ctime", rb_file_s_ctime, 1);

    rb_objc_define_method(rb_ccFile, "utime", rb_file_s_utime, -1);
    rb_objc_define_method(rb_ccFile, "chmod", rb_file_s_chmod, -1);
    rb_objc_define_method(rb_ccFile, "chown", rb_file_s_chown, -1);
    rb_objc_define_method(rb_ccFile, "lchmod", rb_file_s_lchmod, -1);
    rb_objc_define_method(rb_ccFile, "lchown", rb_file_s_lchown, -1);

    rb_objc_define_method(rb_ccFile, "link", rb_file_s_link, 2);
    rb_objc_define_method(rb_ccFile, "symlink", rb_file_s_symlink, 2);
    rb_objc_define_method(rb_ccFile, "readlink", rb_file_s_readlink, 1);

    rb_objc_define_method(rb_ccFile, "unlink", rb_file_s_unlink, -2);
    rb_objc_define_method(rb_ccFile, "delete", rb_file_s_unlink, -2);
    rb_objc_define_method(rb_ccFile, "rename", rb_file_s_rename, 2);
    rb_objc_define_method(rb_ccFile, "umask", rb_file_s_umask, -1);
    rb_objc_define_method(rb_ccFile, "truncate", rb_file_s_truncate, 2);
    rb_objc_define_method(rb_ccFile, "expand_path", rb_file_s_expand_path, -1);
    rb_objc_define_method(rb_ccFile, "basename", rb_file_s_basename, -1);
    rb_objc_define_method(rb_ccFile, "dirname", rb_file_s_dirname, 1);
    rb_objc_define_method(rb_ccFile, "extname", rb_file_s_extname, 1);
    rb_objc_define_method(rb_ccFile, "path", rb_file_s_path, 1);

    separator = rb_obj_freeze(rb_usascii_str_new2("/"));
    rb_define_const(rb_cFile, "Separator", separator);
    rb_define_const(rb_cFile, "SEPARATOR", separator);
    rb_objc_define_method(rb_ccFile, "split",  rb_file_s_split, 1);
    rb_objc_define_method(rb_ccFile, "join",   rb_file_s_join, -2);

    rb_define_const(rb_cFile, "ALT_SEPARATOR", Qnil);
    rb_define_const(rb_cFile, "PATH_SEPARATOR", rb_obj_freeze(rb_str_new2(PATH_SEP)));

    rb_objc_define_method(rb_cIO, "stat",  rb_io_stat, 0); /* this is IO's method */
    rb_objc_define_method(rb_cFile, "lstat",  rb_file_lstat, 0);

    rb_objc_define_method(rb_cFile, "atime", rb_file_atime, 0);
    rb_objc_define_method(rb_cFile, "mtime", rb_file_mtime, 0);
    rb_objc_define_method(rb_cFile, "ctime", rb_file_ctime, 0);

    rb_objc_define_method(rb_cFile, "chmod", rb_file_chmod, 1);
    rb_objc_define_method(rb_cFile, "chown", rb_file_chown, 2);
    rb_objc_define_method(rb_cFile, "truncate", rb_file_truncate, 1);

    //rb_objc_define_method(rb_cFile, "flock", rb_file_flock, 1);

    rb_mFConst = rb_define_module_under(rb_cFile, "Constants");
    rb_include_module(rb_cIO, rb_mFConst);
    rb_file_const("LOCK_SH", INT2FIX(LOCK_SH));
    rb_file_const("LOCK_EX", INT2FIX(LOCK_EX));
    rb_file_const("LOCK_UN", INT2FIX(LOCK_UN));
    rb_file_const("LOCK_NB", INT2FIX(LOCK_NB));

    rb_objc_define_method(rb_cFile, "path",  rb_file_path, 0);
    rb_objc_define_method(rb_cFile, "to_path",  rb_file_path, 0);
    rb_objc_define_method(rb_mKernel, "test", rb_f_test, -1);

    rb_cStat = rb_define_class_under(rb_cFile, "Stat", rb_cObject);
    rb_objc_define_method(*(VALUE *)rb_cStat, "alloc", rb_stat_s_alloc, 0);
    rb_objc_define_method(rb_cStat, "initialize", rb_stat_init, 1);
    rb_objc_define_method(rb_cStat, "initialize_copy", rb_stat_init_copy, 1);

    rb_include_module(rb_cStat, rb_mComparable);

    rb_objc_define_method(rb_cStat, "<=>", rb_stat_cmp, 1);

    rb_objc_define_method(rb_cStat, "dev", rb_stat_dev, 0);
    rb_objc_define_method(rb_cStat, "dev_major", rb_stat_dev_major, 0);
    rb_objc_define_method(rb_cStat, "dev_minor", rb_stat_dev_minor, 0);
    rb_objc_define_method(rb_cStat, "ino", rb_stat_ino, 0);
    rb_objc_define_method(rb_cStat, "mode", rb_stat_mode, 0);
    rb_objc_define_method(rb_cStat, "nlink", rb_stat_nlink, 0);
    rb_objc_define_method(rb_cStat, "uid", rb_stat_uid, 0);
    rb_objc_define_method(rb_cStat, "gid", rb_stat_gid, 0);
    rb_objc_define_method(rb_cStat, "rdev", rb_stat_rdev, 0);
    rb_objc_define_method(rb_cStat, "rdev_major", rb_stat_rdev_major, 0);
    rb_objc_define_method(rb_cStat, "rdev_minor", rb_stat_rdev_minor, 0);
    rb_objc_define_method(rb_cStat, "size", rb_stat_size, 0);
    rb_objc_define_method(rb_cStat, "blksize", rb_stat_blksize, 0);
    rb_objc_define_method(rb_cStat, "blocks", rb_stat_blocks, 0);
    rb_objc_define_method(rb_cStat, "atime", rb_stat_atime, 0);
    rb_objc_define_method(rb_cStat, "mtime", rb_stat_mtime, 0);
    rb_objc_define_method(rb_cStat, "ctime", rb_stat_ctime, 0);

    rb_objc_define_method(rb_cStat, "inspect", rb_stat_inspect, 0);

    rb_objc_define_method(rb_cStat, "ftype", rb_stat_ftype, 0);

    rb_objc_define_method(rb_cStat, "directory?",  rb_stat_d, 0);
    rb_objc_define_method(rb_cStat, "readable?",  rb_stat_r, 0);
    rb_objc_define_method(rb_cStat, "readable_real?",  rb_stat_R, 0);
    rb_objc_define_method(rb_cStat, "world_readable?", rb_stat_wr, 0);
    rb_objc_define_method(rb_cStat, "writable?",  rb_stat_w, 0);
    rb_objc_define_method(rb_cStat, "writable_real?",  rb_stat_W, 0);
    rb_objc_define_method(rb_cStat, "world_writable?", rb_stat_ww, 0);
    rb_objc_define_method(rb_cStat, "executable?",  rb_stat_x, 0);
    rb_objc_define_method(rb_cStat, "executable_real?",  rb_stat_X, 0);
    rb_objc_define_method(rb_cStat, "file?",  rb_stat_f, 0);
    rb_objc_define_method(rb_cStat, "zero?",  rb_stat_z, 0);
    rb_objc_define_method(rb_cStat, "size?",  rb_stat_s, 0);
    rb_objc_define_method(rb_cStat, "owned?",  rb_stat_owned, 0);
    rb_objc_define_method(rb_cStat, "grpowned?",  rb_stat_grpowned, 0);

    rb_objc_define_method(rb_cStat, "pipe?",  rb_stat_p, 0);
    rb_objc_define_method(rb_cStat, "symlink?",  rb_stat_l, 0);
    rb_objc_define_method(rb_cStat, "socket?",  rb_stat_S, 0);

    rb_objc_define_method(rb_cStat, "blockdev?",  rb_stat_b, 0);
    rb_objc_define_method(rb_cStat, "chardev?",  rb_stat_c, 0);

    rb_objc_define_method(rb_cStat, "setuid?",  rb_stat_suid, 0);
    rb_objc_define_method(rb_cStat, "setgid?",  rb_stat_sgid, 0);
    rb_objc_define_method(rb_cStat, "sticky?",  rb_stat_sticky, 0);
}
