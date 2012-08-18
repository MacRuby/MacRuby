/************************************************

  socket.c -

  $Author: akr $
  created at: Thu Mar 31 12:21:29 JST 1994

  Copyright (C) 1993-2007 Yukihiro Matsumoto

************************************************/

#include "macruby_internal.h"
#include "ruby/io.h"
#include "ruby/signal.h"
#include "ruby/util.h"
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif

#ifdef HAVE_XTI_H
#include <xti.h>
#endif

#ifndef _WIN32
#if defined(__BEOS__)
# include <net/socket.h>
#else
# include <sys/socket.h>
#endif
#include <netinet/in.h>
#ifdef HAVE_NETINET_IN_SYSTM_H
# include <netinet/in_systm.h>
#endif
#ifdef HAVE_NETINET_TCP_H
# include <netinet/tcp.h>
#endif
#ifdef HAVE_NETINET_UDP_H
# include <netinet/udp.h>
#endif
#ifdef HAVE_ARPA_INET_H
# include <arpa/inet.h>
#endif
#include <netdb.h>
#endif
#include <errno.h>
#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif

#if defined(HAVE_FCNTL)
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#endif

#ifdef HAVE_IFADDRS_H
#include <ifaddrs.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#ifdef HAVE_SYS_SOCKIO_H
#include <sys/sockio.h>
#endif
#ifdef HAVE_NET_IF_H
#include <net/if.h>
#endif

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#ifdef HAVE_SYS_UCRED_H
#include <sys/ucred.h>
#endif
#ifdef HAVE_UCRED_H
#include <ucred.h>
#endif

#ifndef EWOULDBLOCK
#define EWOULDBLOCK EAGAIN
#endif
#ifndef HAVE_GETADDRINFO
# include "addrinfo.h"
#endif
#include "sockport.h"


static int do_not_reverse_lookup = 1;
#define FMODE_NOREVLOOKUP 0x100

VALUE rb_cBasicSocket;
VALUE rb_cIPSocket;
VALUE rb_cTCPSocket;
VALUE rb_cTCPServer;
VALUE rb_cUDPSocket;
#ifdef AF_UNIX
VALUE rb_cUNIXSocket;
VALUE rb_cUNIXServer;
#endif
VALUE rb_cSocket;
static VALUE rb_cAddrinfo;

static VALUE rb_eSocket;

#define INET_CLIENT 0
#define INET_SERVER 1
#define INET_SOCKS  2

#ifndef NI_MAXHOST
# define NI_MAXHOST 1025
#endif
#ifndef NI_MAXSERV
# define NI_MAXSERV 32
#endif

#ifdef AF_INET6
# define IS_IP_FAMILY(af) ((af) == AF_INET || (af) == AF_INET6)
#else
# define IS_IP_FAMILY(af) ((af) == AF_INET)
#endif

#ifndef HAVE_SOCKADDR_STORAGE
/*
 * RFC 2553: protocol-independent placeholder for socket addresses
 */
#define _SS_MAXSIZE	128
#define _SS_ALIGNSIZE	(sizeof(double))
#define _SS_PAD1SIZE	(_SS_ALIGNSIZE - sizeof(unsigned char) * 2)
#define _SS_PAD2SIZE	(_SS_MAXSIZE - sizeof(unsigned char) * 2 - \
				_SS_PAD1SIZE - _SS_ALIGNSIZE)

struct sockaddr_storage {
#ifdef HAVE_SA_LEN
	unsigned char ss_len;		/* address length */
	unsigned char ss_family;	/* address family */
#else
	unsigned short ss_family;
#endif
	char	__ss_pad1[_SS_PAD1SIZE];
	double	__ss_align;	/* force desired structure storage alignment */
	char	__ss_pad2[_SS_PAD2SIZE];
};
#endif

static void sock_define_const(char *name, int value);
static void sock_define_uconst(const char *name, unsigned int value);
#include "constants.h"

static int str_is_number(const char *);

/* fix [ruby-core:29427] */
static int
ruby_getaddrinfo__darwin(const char *nodename, const char *servname,
			 struct addrinfo *hints, struct addrinfo **res)
{
    const char *tmp_servname;
    struct addrinfo tmp_hints;
    tmp_servname = servname;
    MEMCPY(&tmp_hints, hints, struct addrinfo, 1);
    if (nodename && servname) {
	if (str_is_number(tmp_servname) && atoi(servname) == 0) {
	    tmp_servname = NULL;
#ifdef AI_NUMERICSERV
	    if (tmp_hints.ai_flags) tmp_hints.ai_flags &= ~AI_NUMERICSERV;
#endif
	}
    }
    int error = getaddrinfo(nodename, tmp_servname, &tmp_hints, res);
    return error;
}
#undef getaddrinfo
#define getaddrinfo(node,serv,hints,res) ruby_getaddrinfo__darwin((node),(serv),(hints),(res))

#if defined(INET6) && (defined(LOOKUP_ORDER_HACK_INET) || defined(LOOKUP_ORDER_HACK_INET6))
#define LOOKUP_ORDERS		3
static int lookup_order_table[LOOKUP_ORDERS] = {
#if defined(LOOKUP_ORDER_HACK_INET)
    PF_INET, PF_INET6, PF_UNSPEC,
#elif defined(LOOKUP_ORDER_HACK_INET6)
    PF_INET6, PF_INET, PF_UNSPEC,
#else
    /* should not happen */
#endif
};

static int
ruby_getaddrinfo(char *nodename, char *servname,
		 struct addrinfo *hints, struct addrinfo **res)
{
    struct addrinfo tmp_hints;
    int i, af, error;

    if (hints->ai_family != PF_UNSPEC) {
	return getaddrinfo(nodename, servname, hints, res);
    }

    for (i = 0; i < LOOKUP_ORDERS; i++) {
	af = lookup_order_table[i];
	MEMCPY(&tmp_hints, hints, struct addrinfo, 1);
	tmp_hints.ai_family = af;
	error = getaddrinfo(nodename, servname, &tmp_hints, res);
	if (error) {
	    if (tmp_hints.ai_family == PF_UNSPEC) {
		break;
	    }
	}
	else {
	    break;
	}
    }

    return error;
}
#define getaddrinfo(node,serv,hints,res) ruby_getaddrinfo((node),(serv),(hints),(res))
#endif

#if defined(_AIX)
static int
ruby_getaddrinfo__aix(char *nodename, char *servname,
		      struct addrinfo *hints, struct addrinfo **res)
{
    int error = getaddrinfo(nodename, servname, hints, res);
    struct addrinfo *r;
    if (error)
	return error;
    for (r = *res; r != NULL; r = r->ai_next) {
	if (r->ai_addr->sa_family == 0)
	    r->ai_addr->sa_family = r->ai_family;
	if (r->ai_addr->sa_len == 0)
	    r->ai_addr->sa_len = r->ai_addrlen;
    }
    return 0;
}
#undef getaddrinfo
#define getaddrinfo(node,serv,hints,res) ruby_getaddrinfo__aix((node),(serv),(hints),(res))
static int
ruby_getnameinfo__aix(const struct sockaddr *sa, size_t salen,
		      char *host, size_t hostlen,
		      char *serv, size_t servlen, int flags)
{
    struct sockaddr_in6 *sa6;
    u_int32_t *a6;

    if (sa->sa_family == AF_INET6) {
	sa6 = (struct sockaddr_in6 *)sa;
	a6 = sa6->sin6_addr.u6_addr.u6_addr32;

	if (a6[0] == 0 && a6[1] == 0 && a6[2] == 0 && a6[3] == 0) {
	    strncpy(host, "::", hostlen);
	    snprintf(serv, servlen, "%d", sa6->sin6_port);
	    return 0;
	}
    }
    return getnameinfo(sa, salen, host, hostlen, serv, servlen, flags);
}
#undef getnameinfo
#define getnameinfo(sa, salen, host, hostlen, serv, servlen, flags) \
            ruby_getnameinfo__aix((sa), (salen), (host), (hostlen), (serv), (servlen), (flags))

#ifndef CMSG_SPACE
# define CMSG_SPACE(len) (_CMSG_ALIGN(sizeof(struct cmsghdr)) + _CMSG_ALIGN(len))
#endif
#ifndef CMSG_LEN
# define CMSG_LEN(len) (_CMSG_ALIGN(sizeof(struct cmsghdr)) + (len))
#endif
#endif

#ifdef __BEOS__
#undef close
#define close closesocket
#endif

#define MakeOpenFile(obj, fp) \
    do { \
	fp = ALLOC(rb_io_t); \
	GC_WB(&RFILE(obj)->fptr, fp); \
	fp->fd = fp->read_fd = fp->write_fd = -1; \
	fp->pid = -1; \
    } \
    while (0)

static int
constant_arg(VALUE arg, int (*str_to_int)(char*, int, int*), const char *errmsg)
{
    VALUE tmp;
    char *ptr;
    int ret;

    if (SYMBOL_P(arg)) {
        arg = rb_sym_to_s(arg);
        goto str;
    }
    else if (!NIL_P(tmp = rb_check_string_type(arg))) {
	arg = tmp;
      str:
	rb_check_safe_obj(arg);
        ptr = RSTRING_PTR(arg);
        if (str_to_int(ptr, RSTRING_LEN(arg), &ret) == -1)
	    rb_raise(rb_eSocket, "%s: %s", errmsg, ptr);
    }
    else {
	ret = NUM2INT(arg);
    }
    return ret;
}

static int
family_arg(VALUE domain)
{
    /* convert AF_INET, etc. */
    return constant_arg(domain, family_to_int, "unknown socket domain");
}

static int
socktype_arg(VALUE type)
{
    /* convert SOCK_STREAM, etc. */
    return constant_arg(type, socktype_to_int, "unknown socket type");
}

static int
level_arg(VALUE level)
{
    /* convert SOL_SOCKET, IPPROTO_TCP, etc. */
    return constant_arg(level, level_to_int, "unknown protocol level");
}

static int
optname_arg(int level, VALUE optname)
{
    switch (level) {
      case SOL_SOCKET:
        return constant_arg(optname, so_optname_to_int, "unknown socket level option name");
      case IPPROTO_IP:
        return constant_arg(optname, ip_optname_to_int, "unknown IP level option name");
#ifdef IPPROTO_IPV6
      case IPPROTO_IPV6:
        return constant_arg(optname, ipv6_optname_to_int, "unknown IPv6 level option name");
#endif
      case IPPROTO_TCP:
        return constant_arg(optname, tcp_optname_to_int, "unknown TCP level option name");
      case IPPROTO_UDP:
        return constant_arg(optname, udp_optname_to_int, "unknown UDP level option name");
      default:
        return NUM2INT(optname);
    }
}

static int
shutdown_how_arg(VALUE how)
{
    /* convert SHUT_RD, SHUT_WR, SHUT_RDWR. */
    return constant_arg(how, shutdown_how_to_int, "unknown shutdown argument");
}

static VALUE
init_sock(VALUE sock, int fd)
{
    rb_io_t *fp;

#ifdef S_ISSOCK
    struct stat sbuf;
    if (fstat(fd, &sbuf) < 0) {
        rb_sys_fail(0);
    }
    if (!S_ISSOCK(sbuf.st_mode)) {
        rb_raise(rb_eArgError, "not a socket file descriptor");
    }
#endif

    MakeOpenFile(sock, fp);
    fp->fd = fp->read_fd = fp->write_fd = fd;
    fp->mode = FMODE_READWRITE|FMODE_DUPLEX;
    rb_io_ascii8bit_binmode(sock);
    if (do_not_reverse_lookup) {
	fp->mode |= FMODE_NOREVLOOKUP;
    }
    fp->mode |= FMODE_SYNC;

    return sock;
}

/*
 * call-seq:
 *   BasicSocket.for_fd(fd) => basicsocket
 *
 * Returns a socket object which contains the file descriptor, _fd_.
 *
 *   # If invoked by inetd, STDIN/STDOUT/STDERR is a socket.
 *   STDIN_SOCK = Socket.for_fd(STDIN.fileno)
 *   p STDIN_SOCK.remote_address
 *
 */
static VALUE
bsock_s_for_fd(VALUE klass, SEL sel, VALUE fd)
{
    rb_io_t *fptr;
    VALUE sock = init_sock(rb_obj_alloc(klass), NUM2INT(fd));

    GetOpenFile(sock, fptr);

    return sock;
}

/*
 * call-seq:
 *   basicsocket.shutdown([how]) => 0
 *
 * Calls shutdown(2) system call.
 *
 * s.shutdown(Socket::SHUT_RD) disallows further read.
 *
 * s.shutdown(Socket::SHUT_WR) disallows further write.
 *
 * s.shutdown(Socket::SHUT_RDWR) disallows further read and write.
 *
 * _how_ can be symbol or string:
 * - :RD, :SHUT_RD, "RD" and "SHUT_RD" are accepted as Socket::SHUT_RD.
 * - :WR, :SHUT_WR, "WR" and "SHUT_WR" are accepted as Socket::SHUT_WR.
 * - :RDWR, :SHUT_RDWR, "RDWR" and "SHUT_RDWR" are accepted as Socket::SHUT_RDWR.
 *
 *   UNIXSocket.pair {|s1, s2|
 *     s1.puts "ping"
 *     s1.shutdown(:WR)
 *     p s2.read          #=> "ping\n"
 *     s2.puts "pong"
 *     s2.close
 *     p s1.read          #=> "pong\n"
 *   }
 *
 */
static VALUE
bsock_shutdown(VALUE sock, SEL sel, int argc, VALUE *argv)
{
    VALUE howto;
    int how;
    rb_io_t *fptr;

    if (rb_safe_level() >= 4 && !OBJ_TAINTED(sock)) {
	rb_raise(rb_eSecurityError, "Insecure: can't shutdown socket");
    }
    rb_scan_args(argc, argv, "01", &howto);
    if (howto == Qnil)
	how = SHUT_RDWR;
    else {
	how = shutdown_how_arg(howto);
	if (how != SHUT_WR && how != SHUT_RD && how != SHUT_RDWR) {
	    rb_raise(rb_eArgError, "`how' should be either :SHUT_RD, :SHUT_WR, :SHUT_RDWR");
	}
    }
    GetOpenFile(sock, fptr);
    if (shutdown(fptr->fd, how) == -1)
	rb_sys_fail(0);

    return INT2FIX(0);
}

/*
 * call-seq:
 *   basicsocket.close_read => nil
 *
 * Disallows further read using shutdown system call.
 *
 *   s1, s2 = UNIXSocket.pair
 *   s1.close_read
 *   s2.puts #=> Broken pipe (Errno::EPIPE)
 */
static VALUE
bsock_close_read(VALUE sock, SEL sel)
{
    rb_io_t *fptr;

    if (rb_safe_level() >= 4 && !OBJ_TAINTED(sock)) {
	rb_raise(rb_eSecurityError, "Insecure: can't close socket");
    }
    GetOpenFile(sock, fptr);
    shutdown(fptr->fd, 0);
    if (!(fptr->mode & FMODE_WRITABLE)) {
	return rb_io_close(sock);
    }
    fptr->mode &= ~FMODE_READABLE;
    fptr->read_fd = -1;

    return Qnil;
}

/*
 * call-seq:
 *   basicsocket.close_write => nil
 *
 * Disallows further write using shutdown system call.
 *
 *   UNIXSocket.pair {|s1, s2|
 *     s1.print "ping"
 *     s1.close_write
 *     p s2.read        #=> "ping"
 *     s2.print "pong"
 *     s2.close
 *     p s1.read        #=> "pong"
 *   }
 */
static VALUE
bsock_close_write(VALUE sock, SEL sel)
{
    rb_io_t *fptr;

    if (rb_safe_level() >= 4 && !OBJ_TAINTED(sock)) {
	rb_raise(rb_eSecurityError, "Insecure: can't close socket");
    }
    GetOpenFile(sock, fptr);
    if (!(fptr->mode & FMODE_READABLE)) {
	return rb_io_close(sock);
    }
    shutdown(fptr->fd, 1);
    fptr->mode &= ~FMODE_WRITABLE;
    fptr->write_fd = -1;

    return Qnil;
}

/*
 * Document-method: setsockopt
 * call-seq:
 *   setsockopt(level, optname, optval)
 *   setsockopt(socketoption)
 *
 * Sets a socket option. These are protocol and system specific, see your
 * local system documentation for details.
 *
 * === Parameters
 * * +level+ is an integer, usually one of the SOL_ constants such as
 *   Socket::SOL_SOCKET, or a protocol level.
 *   A string or symbol of the name, possibly without prefix, is also
 *   accepted.
 * * +optname+ is an integer, usually one of the SO_ constants, such
 *   as Socket::SO_REUSEADDR.
 *   A string or symbol of the name, possibly without prefix, is also
 *   accepted.
 * * +optval+ is the value of the option, it is passed to the underlying
 *   setsockopt() as a pointer to a certain number of bytes. How this is
 *   done depends on the type:
 *   - Fixnum: value is assigned to an int, and a pointer to the int is
 *     passed, with length of sizeof(int).
 *   - true or false: 1 or 0 (respectively) is assigned to an int, and the
 *     int is passed as for a Fixnum. Note that +false+ must be passed,
 *     not +nil+.
 *   - String: the string's data and length is passed to the socket.
 * * +socketoption+ is an instance of Socket::Option
 *
 * === Examples
 *
 * Some socket options are integers with boolean values, in this case
 * #setsockopt could be called like this:
 *   sock.setsockopt(:SOCKET, :REUSEADDR, true)
 *   sock.setsockopt(Socket::SOL_SOCKET,Socket::SO_REUSEADDR, true)
 *   sock.setsockopt(Socket::Option.bool(:INET, :SOCKET, :REUSEADDR, true))
 *
 * Some socket options are integers with numeric values, in this case
 * #setsockopt could be called like this:
 *   sock.setsockopt(:IP, :TTL, 255)
 *   sock.setsockopt(Socket::IPPROTO_IP, Socket::IP_TTL, 255)
 *   sock.setsockopt(Socket::Option.int(:INET, :IP, :TTL, 255))
 *
 * Option values may be structs. Passing them can be complex as it involves
 * examining your system headers to determine the correct definition. An
 * example is an +ip_mreq+, which may be defined in your system headers as:
 *   struct ip_mreq {
 *     struct  in_addr imr_multiaddr;
 *     struct  in_addr imr_interface;
 *   };
 *
 * In this case #setsockopt could be called like this:
 *   optval = IPAddr.new("224.0.0.251").hton +
 *            IPAddr.new(Socket::INADDR_ANY, Socket::AF_INET).hton
 *   sock.setsockopt(Socket::IPPROTO_IP, Socket::IP_ADD_MEMBERSHIP, optval)
 *
*/
static VALUE
bsock_setsockopt(VALUE sock, SEL sel, VALUE lev, VALUE optname, VALUE val)
{
    int level, option;
    rb_io_t *fptr;
    int i;
    const char *v;
    int vlen;

    rb_secure(2);
    level = level_arg(lev);
    option = optname_arg(level, optname);

    switch (TYPE(val)) {
      case T_FIXNUM:
	i = FIX2INT(val);
	goto numval;
      case T_FALSE:
	i = 0;
	goto numval;
      case T_TRUE:
	i = 1;
      numval:
	v = (char*)&i; vlen = (int)sizeof(i);
	break;
      default:
	StringValue(val);
	v = RSTRING_PTR(val);
	vlen = RSTRING_LENINT(val);
	break;
    }

#define rb_sys_fail_path(path) rb_sys_fail(path == 0 ? NULL : RSTRING_PTR(path));

    GetOpenFile(sock, fptr);
    if (setsockopt(fptr->fd, level, option, v, vlen) < 0)
	rb_sys_fail_path(fptr->path);

    return INT2FIX(0);
}

/*
 * Document-method: getsockopt
 * call-seq:
 *   getsockopt(level, optname) => socketoption
 *
 * Gets a socket option. These are protocol and system specific, see your
 * local system documentation for details. The option is returned as
 * a Socket::Option object.
 *
 * === Parameters
 * * +level+ is an integer, usually one of the SOL_ constants such as
 *   Socket::SOL_SOCKET, or a protocol level.
 *   A string or symbol of the name, possibly without prefix, is also
 *   accepted.
 * * +optname+ is an integer, usually one of the SO_ constants, such
 *   as Socket::SO_REUSEADDR.
 *   A string or symbol of the name, possibly without prefix, is also
 *   accepted.
 *
 * === Examples
 *
 * Some socket options are integers with boolean values, in this case
 * #getsockopt could be called like this:
 *
 *   reuseaddr = sock.getsockopt(:SOCKET, :REUSEADDR).bool
 *
 *   optval = sock.getsockopt(Socket::SOL_SOCKET,Socket::SO_REUSEADDR)
 *   optval = optval.unpack "i"
 *   reuseaddr = optval[0] == 0 ? false : true
 *
 * Some socket options are integers with numeric values, in this case
 * #getsockopt could be called like this:
 *
 *   ipttl = sock.getsockopt(:IP, :TTL).int
 *
 *   optval = sock.getsockopt(Socket::IPPROTO_IP, Socket::IP_TTL)
 *   ipttl = optval.unpack("i")[0]
 *
 * Option values may be structs. Decoding them can be complex as it involves
 * examining your system headers to determine the correct definition. An
 * example is a +struct linger+, which may be defined in your system headers
 * as:
 *   struct linger {
 *     int l_onoff;
 *     int l_linger;
 *   };
 *
 * In this case #getsockopt could be called like this:
 *
 *   # Socket::Option knows linger structure.
 *   onoff, linger = sock.getsockopt(:SOCKET, :LINGER).linger
 *
 *   optval =  sock.getsockopt(Socket::SOL_SOCKET, Socket::SO_LINGER)
 *   onoff, linger = optval.unpack "ii"
 *   onoff = onoff == 0 ? false : true
*/
static VALUE
bsock_getsockopt(VALUE sock, SEL sel, VALUE lev, VALUE optname)
{
#if !defined(__BEOS__)
    int level, option;
    socklen_t len;
    char *buf;
    rb_io_t *fptr;

    level = level_arg(lev);
    option = optname_arg(level, optname);
    len = 256;
    buf = ALLOCA_N(char,len);

    GetOpenFile(sock, fptr);
    if (getsockopt(fptr->fd, level, option, buf, &len) < 0)
	rb_sys_fail_path(fptr->path);

    return rb_str_new(buf, len);
#else
    rb_notimplement();
#endif
}

/*
 * call-seq:
 *   basicsocket.getsockname => sockaddr
 *
 * Returns the local address of the socket as a sockaddr string.
 *
 *   TCPServer.open("127.0.0.1", 15120) {|serv|
 *     p serv.getsockname #=> "\x02\x00;\x10\x7F\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00"
 *   }
 *
 * If Addrinfo object is preferred over the binary string,
 * use BasicSocket#local_address.
 */
static VALUE
bsock_getsockname(VALUE sock, SEL sel)
{
    struct sockaddr_storage buf;
    socklen_t len = (socklen_t)sizeof buf;
    rb_io_t *fptr;

    GetOpenFile(sock, fptr);
    if (getsockname(fptr->fd, (struct sockaddr*)&buf, &len) < 0)
	rb_sys_fail("getsockname(2)");
    return rb_str_new((char*)&buf, len);
}

/*
 * call-seq:
 *   basicsocket.getpeername => sockaddr
 *
 * Returns the remote address of the socket as a sockaddr string.
 *
 *   TCPServer.open("127.0.0.1", 1440) {|serv|
 *     c = TCPSocket.new("127.0.0.1", 1440)
 *     s = serv.accept
 *     p s.getpeername #=> "\x02\x00\x82u\x7F\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00"
 *   }
 *
 * If Addrinfo object is preferred over the binary string,
 * use BasicSocket#remote_address.
 *
 */
static VALUE
bsock_getpeername(VALUE sock, SEL sel)
{
    struct sockaddr_storage buf;
    socklen_t len = (socklen_t)sizeof buf;
    rb_io_t *fptr;

    GetOpenFile(sock, fptr);
    if (getpeername(fptr->fd, (struct sockaddr*)&buf, &len) < 0)
	rb_sys_fail("getpeername(2)");
    return rb_str_new((char*)&buf, len);
}

#if defined(HAVE_GETPEEREID) || defined(SO_PEERCRED) || defined(HAVE_GETPEERUCRED)
/*
 * call-seq:
 *   basicsocket.getpeereid => [euid, egid]
 *
 * Returns the user and group on the peer of the UNIX socket.
 * The result is a two element array which contains the effective uid and the effective gid.
 *
 *   Socket.unix_server_loop("/tmp/sock") {|s|
 *     begin
 *       euid, egid = s.getpeereid
 *
 *       # Check the connected client is myself or not.
 *       next if euid != Process.uid
 *
 *       # do something about my resource.
 *
 *     ensure
 *       s.close
 *     end
 *   }
 *
 */
static VALUE
bsock_getpeereid(VALUE self, SEL sel)
{
#if defined(HAVE_GETPEEREID)
    rb_io_t *fptr;
    uid_t euid;
    gid_t egid;
    GetOpenFile(self, fptr);
    if (getpeereid(fptr->fd, &euid, &egid) == -1)
	rb_sys_fail("getpeereid");
    return rb_assoc_new(UIDT2NUM(euid), GIDT2NUM(egid));
#elif defined(SO_PEERCRED) /* GNU/Linux */
    rb_io_t *fptr;
    struct ucred cred;
    socklen_t len = sizeof(cred);
    GetOpenFile(self, fptr);
    if (getsockopt(fptr->fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) == -1)
	rb_sys_fail("getsockopt(SO_PEERCRED)");
    return rb_assoc_new(UIDT2NUM(cred.uid), GIDT2NUM(cred.gid));
#elif defined(HAVE_GETPEERUCRED) /* Solaris */
    rb_io_t *fptr;
    ucred_t *uc = NULL;
    VALUE ret;
    GetOpenFile(self, fptr);
    if (getpeerucred(fptr->fd, &uc) == -1)
	rb_sys_fail("getpeerucred");
    ret = rb_assoc_new(UIDT2NUM(ucred_geteuid(uc)), GIDT2NUM(ucred_getegid(uc)));
    ucred_free(uc);
    return ret;
#endif
}
#else
#define bsock_getpeereid rb_f_notimplement
#endif

static VALUE addrinfo_new(struct sockaddr *addr, socklen_t len, int family, int socktype, int protocol, VALUE canonname, VALUE inspectname);
static VALUE fd_socket_addrinfo(int fd, struct sockaddr *addr, socklen_t len);
static VALUE io_socket_addrinfo(VALUE io, struct sockaddr *addr, socklen_t len);

/*
 * call-seq:
 *   bsock.local_address => addrinfo
 *
 * Returns an Addrinfo object for local address obtained by getsockname.
 *
 * Note that addrinfo.protocol is filled by 0.
 *
 *   TCPSocket.open("www.ruby-lang.org", 80) {|s|
 *     p s.local_address #=> #<Addrinfo: 192.168.0.129:36873 TCP>
 *   }
 *
 *   TCPServer.open("127.0.0.1", 1512) {|serv|
 *     p serv.local_address #=> #<Addrinfo: 127.0.0.1:1512 TCP>
 *   }
 *
 */
static VALUE
bsock_local_address(VALUE sock, SEL sel)
{
    struct sockaddr_storage buf;
    socklen_t len = (socklen_t)sizeof buf;
    rb_io_t *fptr;

    GetOpenFile(sock, fptr);
    if (getsockname(fptr->fd, (struct sockaddr*)&buf, &len) < 0)
	rb_sys_fail("getsockname(2)");
    return fd_socket_addrinfo(fptr->fd, (struct sockaddr *)&buf, len);
}

/*
 * call-seq:
 *   bsock.remote_address => addrinfo
 *
 * Returns an Addrinfo object for remote address obtained by getpeername.
 *
 * Note that addrinfo.protocol is filled by 0.
 *
 *   TCPSocket.open("www.ruby-lang.org", 80) {|s|
 *     p s.remote_address #=> #<Addrinfo: 221.186.184.68:80 TCP>
 *   }
 *
 *   TCPServer.open("127.0.0.1", 1728) {|serv|
 *     c = TCPSocket.new("127.0.0.1", 1728)
 *     s = serv.accept
 *     p s.remote_address #=> #<Addrinfo: 127.0.0.1:36504 TCP>
 *   }
 *
 */
static VALUE
bsock_remote_address(VALUE sock, SEL sel)
{
    struct sockaddr_storage buf;
    socklen_t len = (socklen_t)sizeof buf;
    rb_io_t *fptr;

    GetOpenFile(sock, fptr);
    if (getpeername(fptr->fd, (struct sockaddr*)&buf, &len) < 0)
	rb_sys_fail("getpeername(2)");
    return fd_socket_addrinfo(fptr->fd, (struct sockaddr *)&buf, len);
}

#define SockAddrStringValue(v) sockaddr_string_value(&(v))
#define SockAddrStringValuePtr(v) sockaddr_string_value_ptr(&(v))
static VALUE sockaddr_string_value(volatile VALUE *);
static char *sockaddr_string_value_ptr(volatile VALUE *);

/*
 * call-seq:
 *   basicsocket.send(mesg, flags [, dest_sockaddr]) => numbytes_sent
 *
 * send _mesg_ via _basicsocket_.
 *
 * _mesg_ should be a string.
 *
 * _flags_ should be a bitwise OR of Socket::MSG_* constants.
 *
 * _dest_sockaddr_ should be a packed sockaddr string or an addrinfo.
 *
 *   TCPSocket.open("localhost", 80) {|s|
 *     s.send "GET / HTTP/1.0\r\n\r\n", 0
 *     p s.read
 *   }
 */
static VALUE
bsock_send(VALUE sock, SEL sel, int argc, VALUE *argv)
{
    VALUE mesg, to;
    VALUE flags;
    rb_io_t *fptr;
    int fd, n;

    rb_secure(4);
    rb_scan_args(argc, argv, "21", &mesg, &flags, &to);

    StringValue(mesg);
    if (!NIL_P(to)) SockAddrStringValue(to);
    GetOpenFile(sock, fptr);
    fd = fptr->fd;
  retry:
    rb_thread_fd_writable(fd);
    if (!NIL_P(to)) {
	n = sendto(fd, RSTRING_PTR(mesg), RSTRING_LEN(mesg), NUM2INT(flags),
		   (struct sockaddr*)RSTRING_PTR(to), RSTRING_LEN(to));
    }
    else {
	n = send(fd, RSTRING_PTR(mesg), RSTRING_LEN(mesg), NUM2INT(flags));
    }
    if (n < 0) {
	if (rb_io_wait_writable(fd)) {
	    goto retry;
	}
	rb_sys_fail("send(2)");
    }
    return INT2FIX(n);
}

/*
 * call-seq:
 *   basicsocket.do_not_reverse_lookup => true or false
 *
 * Gets the do_not_reverse_lookup flag of _basicsocket_.
 *
 *   TCPSocket.open("www.ruby-lang.org", 80) {|sock|
 *     p sock.do_not_reverse_lookup      #=> false
 *     p sock.peeraddr                   #=> ["AF_INET", 80, "carbon.ruby-lang.org", "221.186.184.68"]
 *     sock.do_not_reverse_lookup = true
 *     p sock.peeraddr                   #=> ["AF_INET", 80, "221.186.184.68", "221.186.184.68"]
 *   }
 */
static VALUE
bsock_do_not_reverse_lookup(VALUE sock, SEL sel)
{
    rb_io_t *fptr;

    GetOpenFile(sock, fptr);
    return (fptr->mode & FMODE_NOREVLOOKUP) ? Qtrue : Qfalse;
}

/*
 * call-seq:
 *   basicsocket.do_not_reverse_lookup = bool
 *
 * Sets the do_not_reverse_lookup flag of _basicsocket_.
 *
 *   BasicSocket.do_not_reverse_lookup = false
 *   p TCPSocket.new("127.0.0.1", 80).do_not_reverse_lookup #=> false
 *   BasicSocket.do_not_reverse_lookup = true
 *   p TCPSocket.new("127.0.0.1", 80).do_not_reverse_lookup #=> true
 *
 */
static VALUE
bsock_do_not_reverse_lookup_set(VALUE sock, SEL sel, VALUE state)
{
    rb_io_t *fptr;

    rb_secure(4);
    GetOpenFile(sock, fptr);
    if (RTEST(state)) {
	fptr->mode |= FMODE_NOREVLOOKUP;
    }
    else {
	fptr->mode &= ~FMODE_NOREVLOOKUP;
    }
    return sock;
}

static VALUE ipaddr(struct sockaddr*, int);
#ifdef HAVE_SYS_UN_H
static VALUE unixaddr(struct sockaddr_un*, socklen_t);
#endif

enum sock_recv_type {
    RECV_RECV,			/* BasicSocket#recv(no from) */
    RECV_IP,			/* IPSocket#recvfrom */
    RECV_UNIX,			/* UNIXSocket#recvfrom */
    RECV_SOCKET			/* Socket#recvfrom */
};

static VALUE
s_recvfrom(VALUE sock, int argc, VALUE *argv, enum sock_recv_type from)
{
    rb_io_t *fptr;
    VALUE str;
    struct sockaddr_storage buf;
    socklen_t alen = (socklen_t)sizeof buf;
    VALUE len, flg;
    long buflen;
    long slen;
    int fd, flags;

    rb_scan_args(argc, argv, "11", &len, &flg);

    if (flg == Qnil) flags = 0;
    else             flags = NUM2INT(flg);
    buflen = NUM2INT(len);

    GetOpenFile(sock, fptr);
    if (rb_io_read_pending(fptr)) {
	rb_raise(rb_eIOError, "recv for buffered IO");
    }
    fd = fptr->fd;

    str = rb_bstr_new();
    rb_bstr_resize(str, buflen);

  retry:
    rb_thread_wait_fd(fd);
    rb_io_check_closed(fptr);
    if (rb_bstr_length(str) != buflen) {
	rb_raise(rb_eRuntimeError, "buffer string modified");
    }
    slen = recvfrom(fd, rb_bstr_bytes(str), buflen, flags,
	    (struct sockaddr *)&buf, &alen);

    if (slen < 0) {
	if (rb_io_wait_readable(fd)) {
	    goto retry;
	}
	rb_sys_fail("recvfrom(2)");
    }
    if (slen < rb_bstr_length(str)) {
	rb_bstr_resize(str, slen);
    }
    rb_obj_taint(str);
    switch (from) {
      case RECV_RECV:
	return (VALUE)str;
      case RECV_IP:
#if 0
	if (alen != sizeof(struct sockaddr_in)) {
	    rb_raise(rb_eTypeError, "sockaddr size differs - should not happen");
	}
#endif
	if (alen && alen != sizeof(buf)) /* OSX doesn't return a from result for connection-oriented sockets */
	    return rb_assoc_new(str, ipaddr((struct sockaddr*)&buf, fptr->mode & FMODE_NOREVLOOKUP));
	else
	    return rb_assoc_new(str, Qnil);

#ifdef HAVE_SYS_UN_H
      case RECV_UNIX:
	return rb_assoc_new(str, unixaddr((struct sockaddr_un*)&buf, alen));
#endif
      case RECV_SOCKET:
	return rb_assoc_new(str, io_socket_addrinfo(sock, (struct sockaddr*)&buf, alen));
      default:
	rb_bug("s_recvfrom called with bad value");
    }
}

static VALUE
s_recvfrom_nonblock(VALUE sock, int argc, VALUE *argv, enum sock_recv_type from)
{
    rb_io_t *fptr;
    VALUE str;
    struct sockaddr_storage buf;
    socklen_t alen = (socklen_t)sizeof buf;
    VALUE len, flg;
    long buflen;
    long slen;
    int fd, flags;
    VALUE addr = Qnil;

    rb_scan_args(argc, argv, "11", &len, &flg);

    if (flg == Qnil) flags = 0;
    else             flags = NUM2INT(flg);
    buflen = NUM2INT(len);

#ifdef MSG_DONTWAIT
    /* MSG_DONTWAIT avoids the race condition between fcntl and recvfrom.
       It is not portable, though. */
    flags |= MSG_DONTWAIT;
#endif

    GetOpenFile(sock, fptr);
    if (rb_io_read_pending(fptr)) {
	rb_raise(rb_eIOError, "recvfrom for buffered IO");
    }
    fd = fptr->fd;

    str = rb_bstr_new();
    rb_bstr_resize(str, buflen);

    rb_io_check_closed(fptr);
    rb_io_set_nonblock(fptr);
    slen = recvfrom(fd, rb_bstr_bytes(str), buflen, flags,
	    (struct sockaddr *)&buf, &alen);

    if (slen < 0) {
	switch (errno) {
	  case EAGAIN:
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
	  case EWOULDBLOCK:
#endif
            rb_mod_sys_fail(rb_mWaitReadable, "recvfrom(2) would block");
	}
	rb_sys_fail("recvfrom(2)");
    }
    if (slen < rb_bstr_length(str)) {
	rb_bstr_resize(str, slen);
    }
    rb_obj_taint(str);
    switch (from) {
      case RECV_RECV:
        return str;

      case RECV_IP:
        if (alen && alen != sizeof(buf)) /* connection-oriented socket may not return a from result */
            addr = ipaddr((struct sockaddr*)&buf, fptr->mode & FMODE_NOREVLOOKUP);
        break;

      case RECV_SOCKET:
        addr = io_socket_addrinfo(sock, (struct sockaddr*)&buf, alen);
        break;

      default:
        rb_bug("s_recvfrom_nonblock called with bad value");
    }
    return rb_assoc_new(str, addr);
}

/*
 * call-seq:
 *   basicsocket.recv(maxlen) => mesg
 *   basicsocket.recv(maxlen, flags) => mesg
 *
 * Receives a message.
 *
 * _maxlen_ is the maximum number of bytes to receive.
 *
 * _flags_ should be a bitwise OR of Socket::MSG_* constants.
 *
 *   UNIXSocket.pair {|s1, s2|
 *     s1.puts "Hello World"
 *     p s2.recv(4)                     #=> "Hell"
 *     p s2.recv(4, Socket::MSG_PEEK)   #=> "o Wo"
 *     p s2.recv(4)                     #=> "o Wo"
 *     p s2.recv(10)                    #=> "rld\n"
 *   }
 */
static VALUE
bsock_recv(VALUE sock, SEL sel, int argc, VALUE *argv)
{
    return s_recvfrom(sock, argc, argv, RECV_RECV);
}

/*
 * call-seq:
 * 	basicsocket.recv_nonblock(maxlen) => mesg
 * 	basicsocket.recv_nonblock(maxlen, flags) => mesg
 *
 * Receives up to _maxlen_ bytes from +socket+ using recvfrom(2) after
 * O_NONBLOCK is set for the underlying file descriptor.
 * _flags_ is zero or more of the +MSG_+ options.
 * The result, _mesg_, is the data received.
 *
 * When recvfrom(2) returns 0, Socket#recv_nonblock returns
 * an empty string as data.
 * The meaning depends on the socket: EOF on TCP, empty packet on UDP, etc.
 *
 * === Parameters
 * * +maxlen+ - the number of bytes to receive from the socket
 * * +flags+ - zero or more of the +MSG_+ options
 *
 * === Example
 * 	serv = TCPServer.new("127.0.0.1", 0)
 * 	af, port, host, addr = serv.addr
 * 	c = TCPSocket.new(addr, port)
 * 	s = serv.accept
 * 	c.send "aaa", 0
 * 	begin # emulate blocking recv.
 * 	  p s.recv_nonblock(10) #=> "aaa"
 * 	rescue IO::WaitReadable
 * 	  IO.select([s])
 * 	  retry
 * 	end
 *
 * Refer to Socket#recvfrom for the exceptions that may be thrown if the call
 * to _recv_nonblock_ fails.
 *
 * BasicSocket#recv_nonblock may raise any error corresponding to recvfrom(2) failure,
 * including Errno::EWOULDBLOCK.
 *
 * If the exception is Errno::EWOULDBLOCK or Errno::AGAIN,
 * it is extended by IO::WaitReadable.
 * So IO::WaitReadable can be used to rescue the exceptions for retrying recv_nonblock.
 *
 * === See
 * * Socket#recvfrom
 */
static VALUE
bsock_recv_nonblock(VALUE sock, SEL sel, int argc, VALUE *argv)
{
    return s_recvfrom_nonblock(sock, argc, argv, RECV_RECV);
}

/*
 * call-seq:
 *   BasicSocket.do_not_reverse_lookup => true or false
 *
 * Gets the global do_not_reverse_lookup flag.
 *
 *   BasicSocket.do_not_reverse_lookup  #=> false
 */
static VALUE
bsock_do_not_rev_lookup(VALUE self, SEL sel)
{
    return do_not_reverse_lookup ? Qtrue : Qfalse;
}

/*
 * call-seq:
 *   BasicSocket.do_not_reverse_lookup = bool
 *
 * Sets the global do_not_reverse_lookup flag.
 *
 * The flag is used for initial value of do_not_reverse_lookup for each socket.
 *
 *   s1 = TCPSocket.new("localhost", 80)
 *   p s1.do_not_reverse_lookup                 #=> true
 *   BasicSocket.do_not_reverse_lookup = false
 *   s2 = TCPSocket.new("localhost", 80)
 *   p s2.do_not_reverse_lookup                 #=> false
 *   p s1.do_not_reverse_lookup                 #=> true
 *
 */
static VALUE
bsock_do_not_rev_lookup_set(VALUE self, SEL sel, VALUE val)
{
    rb_secure(4);
    do_not_reverse_lookup = RTEST(val);
    return val;
}

NORETURN(static void raise_socket_error(char *, int));
static void
raise_socket_error(char *reason, int error)
{
#ifdef EAI_SYSTEM
    if (error == EAI_SYSTEM) rb_sys_fail(reason);
#endif
    rb_raise(rb_eSocket, "%s: %s", reason, gai_strerror(error));
}

static void
make_ipaddr0(struct sockaddr *addr, char *buf, size_t len)
{
    int error;

    error = getnameinfo(addr, SA_LEN(addr), buf, len, NULL, 0, NI_NUMERICHOST);
    if (error) {
        raise_socket_error("getnameinfo", error);
    }
}

static VALUE
make_ipaddr(struct sockaddr *addr)
{
    char hbuf[1024];

    make_ipaddr0(addr, hbuf, sizeof(hbuf));
    return rb_str_new2(hbuf);
}

static void
make_inetaddr(long host, char *buf, size_t len)
{
    struct sockaddr_in sin;

    MEMZERO(&sin, struct sockaddr_in, 1);
    sin.sin_family = AF_INET;
    SET_SIN_LEN(&sin, sizeof(sin));
    sin.sin_addr.s_addr = host;
    make_ipaddr0((struct sockaddr*)&sin, buf, len);
}

static int
str_is_number(const char *p)
{
    char *ep;

    if (!p || *p == '\0')
       return 0;
    ep = NULL;
    (void)STRTOUL(p, &ep, 10);
    if (ep && *ep == '\0')
       return 1;
    else
       return 0;
}

static char*
host_str(VALUE host, char *hbuf, size_t len, int *flags_ptr)
{
    if (NIL_P(host)) {
        return NULL;
    }
    else if (rb_obj_is_kind_of(host, rb_cInteger)) {
        unsigned int i = NUM2UINT(host);

        make_inetaddr(htonl(i), hbuf, len);
        if (flags_ptr) *flags_ptr |= AI_NUMERICHOST;
        return hbuf;
    }
    else {
        const char *name;

        SafeStringValue(host);
        name = RSTRING_PTR(host);
        if (!name || *name == 0 || (name[0] == '<' && strcmp(name, "<any>") == 0)) {
            make_inetaddr(INADDR_ANY, hbuf, len);
            if (flags_ptr) *flags_ptr |= AI_NUMERICHOST;
        }
        else if (name[0] == '<' && strcmp(name, "<broadcast>") == 0) {
            make_inetaddr(INADDR_BROADCAST, hbuf, len);
            if (flags_ptr) *flags_ptr |= AI_NUMERICHOST;
        }
        else if (strlen(name) >= len) {
            rb_raise(rb_eArgError, "hostname too long (%ld)", strlen(name));
        }
        else {
            strcpy(hbuf, name);
        }
        return hbuf;
    }
}

static char*
port_str(VALUE port, char *pbuf, size_t len, int *flags_ptr)
{
    if (NIL_P(port)) {
        return 0;
    }
    else if (FIXNUM_P(port)) {
        snprintf(pbuf, len, "%ld", FIX2LONG(port));
#ifdef AI_NUMERICSERV
        if (flags_ptr) *flags_ptr |= AI_NUMERICSERV;
#endif
        return pbuf;
    }
    else {
        const char *serv;

        SafeStringValue(port);
        serv = RSTRING_PTR(port);
        if (strlen(serv) >= len) {
            rb_raise(rb_eArgError, "service name too long (%ld)", strlen(serv));
        }
        strcpy(pbuf, serv);
        return pbuf;
    }
}

static struct addrinfo*
sock_getaddrinfo(VALUE host, VALUE port, struct addrinfo *hints, int socktype_hack)
{
    struct addrinfo* res = NULL;
    char *hostp, *portp;
    int error;
    char hbuf[NI_MAXHOST], pbuf[NI_MAXSERV];
    int additional_flags = 0;

    hostp = host_str(host, hbuf, sizeof(hbuf), &additional_flags);
    portp = port_str(port, pbuf, sizeof(pbuf), &additional_flags);

    if (socktype_hack && hints->ai_socktype == 0 && str_is_number(portp)) {
        hints->ai_socktype = SOCK_DGRAM;
    }
    hints->ai_flags |= additional_flags;

    error = getaddrinfo(hostp, portp, hints, &res);
    if (error) {
        if (hostp && hostp[strlen(hostp)-1] == '\n') {
            rb_raise(rb_eSocket, "newline at the end of hostname");
        }
        raise_socket_error("getaddrinfo", error);
    }

#if defined(__APPLE__) && defined(__MACH__)
    {
        struct addrinfo *r;
        r = res;
        while (r) {
            if (! r->ai_socktype) r->ai_socktype = hints->ai_socktype;
            if (! r->ai_protocol) {
                if (r->ai_socktype == SOCK_DGRAM) {
                    r->ai_protocol = IPPROTO_UDP;
                }
                else if (r->ai_socktype == SOCK_STREAM) {
                    r->ai_protocol = IPPROTO_TCP;
                }
            }
            r = r->ai_next;
        }
    }
#endif
    return res;
}

static struct addrinfo*
sock_addrinfo(VALUE host, VALUE port, int socktype, int flags)
{
    struct addrinfo hints;

    MEMZERO(&hints, struct addrinfo, 1);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = socktype;
    hints.ai_flags = flags;
    return sock_getaddrinfo(host, port, &hints, 1);
}

static VALUE
ipaddr(struct sockaddr *sockaddr, int norevlookup)
{
    VALUE family, port, addr1, addr2;
    VALUE ary;
    int error;
    char hbuf[1024], pbuf[1024];
    ID id;

    id = intern_family(sockaddr->sa_family);
    if (id) {
        family = rb_str_dup(rb_id2str(id));
    }
    else {
        sprintf(pbuf, "unknown:%d", sockaddr->sa_family);
        family = rb_str_new2(pbuf);
    }

    addr1 = Qnil;
    if (!norevlookup) {
        error = getnameinfo(sockaddr, SA_LEN(sockaddr), hbuf, sizeof(hbuf),
                            NULL, 0, 0);
        if (! error) {
            addr1 = rb_str_new2(hbuf);
        }
    }
    error = getnameinfo(sockaddr, SA_LEN(sockaddr), hbuf, sizeof(hbuf),
                        pbuf, sizeof(pbuf), NI_NUMERICHOST | NI_NUMERICSERV);
    if (error) {
        raise_socket_error("getnameinfo", error);
    }
    addr2 = rb_str_new2(hbuf);
    if (addr1 == Qnil) {
	addr1 = addr2;
    }
    port = INT2FIX(atoi(pbuf));
    ary = rb_ary_new3(4, family, port, addr1, addr2);

    return ary;
}

static int
ruby_socket(int domain, int type, int proto)
{
    int fd;

    fd = socket(domain, type, proto);
    if (fd < 0) {
	if (errno == EMFILE || errno == ENFILE) {
	    rb_gc();
	    fd = socket(domain, type, proto);
	}
    }
    return fd;
}

static int
wait_connectable0(int fd, rb_fdset_t *fds_w, rb_fdset_t *fds_e)
{
    int sockerr;
    socklen_t sockerrlen;

    for (;;) {
	rb_fd_zero(fds_w);
	rb_fd_zero(fds_e);

	rb_fd_set(fd, fds_w);
	rb_fd_set(fd, fds_e);

	rb_thread_select(fd+1, 0, rb_fd_ptr(fds_w), rb_fd_ptr(fds_e), 0);

	if (rb_fd_isset(fd, fds_w)) {
	    return 0;
	}
	else if (rb_fd_isset(fd, fds_e)) {
	    sockerrlen = (socklen_t)sizeof(sockerr);
	    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (void *)&sockerr,
			   &sockerrlen) == 0) {
		if (sockerr == 0)
		    continue;	/* workaround for winsock */
		errno = sockerr;
	    }
	    return -1;
	}
    }
}

struct wait_connectable_arg {
    int fd;
    rb_fdset_t *fds_w;
    rb_fdset_t *fds_e;
};

#ifdef HAVE_RB_FD_INIT
static VALUE
try_wait_connectable(VALUE arg)
{
    struct wait_connectable_arg *p = (struct wait_connectable_arg *)arg;
    return (VALUE)wait_connectable0(p->fd, p->fds_w, p->fds_e);
}

static VALUE
wait_connectable_ensure(VALUE arg)
{
    struct wait_connectable_arg *p = (struct wait_connectable_arg *)arg;
    rb_fd_term(p->fds_w);
    rb_fd_term(p->fds_e);
    return Qnil;
}
#endif

static int
wait_connectable(int fd)
{
    struct wait_connectable_arg *arg;

    arg = (void *)xmalloc(sizeof(struct wait_connectable_arg));
    GC_WB(&arg->fds_w, xmalloc(sizeof(rb_fdset_t)));
    GC_WB(&arg->fds_e, xmalloc(sizeof(rb_fdset_t)));
    rb_fd_init(arg->fds_w);
    rb_fd_init(arg->fds_e);
#ifdef HAVE_RB_FD_INIT
    arg->fd = fd;
    return (int)rb_ensure(try_wait_connectable, (VALUE)arg,
			  wait_connectable_ensure,(VALUE)arg);
#else
    return wait_connectable0(fd, arg->fds_w, arg->fds_e);
#endif
}

#ifdef __CYGWIN__
#define WAIT_IN_PROGRESS 10
#endif
#ifdef __APPLE__
#define WAIT_IN_PROGRESS 10
#endif
#ifdef __linux__
/* returns correct error */
#define WAIT_IN_PROGRESS 0
#endif
#ifndef WAIT_IN_PROGRESS
/* BSD origin code apparently has a problem */
#define WAIT_IN_PROGRESS 1
#endif

static int
ruby_connect(int fd, struct sockaddr *sockaddr, int len, int socks)
{
    int status;
#if WAIT_IN_PROGRESS > 0
    int wait_in_progress = -1;
    int sockerr;
    socklen_t sockerrlen;
#endif

    for (;;) {
	status = connect(fd, sockaddr, len);
	if (status < 0) {
	    switch (errno) {
	      case EINTR:
#if defined(ERESTART)
	      case ERESTART:
#endif
		continue;

	      case EAGAIN:
#ifdef EINPROGRESS
	      case EINPROGRESS:
#endif
#if WAIT_IN_PROGRESS > 0
		sockerrlen = (socklen_t)sizeof(sockerr);
		status = getsockopt(fd, SOL_SOCKET, SO_ERROR, (void *)&sockerr, &sockerrlen);
		if (status) break;
		if (sockerr) {
		    status = -1;
		    errno = sockerr;
		    break;
		}
#endif
#ifdef EALREADY
	      case EALREADY:
#endif
#if WAIT_IN_PROGRESS > 0
		wait_in_progress = WAIT_IN_PROGRESS;
#endif
		status = wait_connectable(fd);
		if (status) {
		    break;
		}
		errno = 0;
		continue;

#if WAIT_IN_PROGRESS > 0
	      case EINVAL:
		if (wait_in_progress-- > 0) {
		    /*
		     * connect() after EINPROGRESS returns EINVAL on
		     * some platforms, need to check true error
		     * status.
		     */
		    sockerrlen = (socklen_t)sizeof(sockerr);
		    status = getsockopt(fd, SOL_SOCKET, SO_ERROR, (void *)&sockerr, &sockerrlen);
		    if (!status && !sockerr) {
			struct timeval tv = {0, 100000};
			rb_thread_wait_for(tv);
			continue;
		    }
		    status = -1;
		    errno = sockerr;
		}
		break;
#endif

#ifdef EISCONN
	      case EISCONN:
		status = 0;
		errno = 0;
		break;
#endif
	      default:
		break;
	    }
	}
	return status;
    }
}

struct inetsock_arg
{
    VALUE sock;
    struct {
	VALUE host, serv;
	struct addrinfo *res;
    } remote, local;
    int type;
    int fd;
};

static VALUE
inetsock_cleanup(struct inetsock_arg *arg)
{
    if (arg->remote.res) {
	freeaddrinfo(arg->remote.res);
	arg->remote.res = 0;
    }
    if (arg->local.res) {
	freeaddrinfo(arg->local.res);
	arg->local.res = 0;
    }
    if (arg->fd >= 0) {
	close(arg->fd);
    }
    return Qnil;
}

static VALUE
init_inetsock_internal(struct inetsock_arg *arg)
{
    int type = arg->type;
    struct addrinfo *res;
    int fd, status = 0;
    char *syscall = NULL;

    arg->remote.res = sock_addrinfo(arg->remote.host, arg->remote.serv, SOCK_STREAM,
				    (type == INET_SERVER) ? AI_PASSIVE : 0);
    /*
     * Maybe also accept a local address
     */

    if (type != INET_SERVER && (!NIL_P(arg->local.host) || !NIL_P(arg->local.serv))) {
	arg->local.res = sock_addrinfo(arg->local.host, arg->local.serv, SOCK_STREAM, 0);
    }

    arg->fd = fd = -1;
    for (res = arg->remote.res; res; res = res->ai_next) {
#if !defined(INET6) && defined(AF_INET6)
	if (res->ai_family == AF_INET6)
	    continue;
#endif
	status = ruby_socket(res->ai_family,res->ai_socktype,res->ai_protocol);
	syscall = "socket(2)";
	fd = status;
	if (fd < 0) {
	    continue;
	}
	arg->fd = fd;
	if (type == INET_SERVER) {
#if !defined(_WIN32) && !defined(__CYGWIN__)
	    status = 1;
	    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
		       (char*)&status, sizeof(status));
#endif
	    status = bind(fd, res->ai_addr, res->ai_addrlen);
	    syscall = "bind(2)";
	}
	else {
	    if (arg->local.res) {
		status = bind(fd, arg->local.res->ai_addr, arg->local.res->ai_addrlen);
		syscall = "bind(2)";
	    }

	    if (status >= 0) {
		status = ruby_connect(fd, res->ai_addr, res->ai_addrlen,
				      (type == INET_SOCKS));
		syscall = "connect(2)";
	    }
	}

	if (status < 0) {
	    close(fd);
	    arg->fd = fd = -1;
	    continue;
	} else
	    break;
    }
    if (status < 0) {
	rb_sys_fail(syscall);
    }

    arg->fd = -1;

    if (type == INET_SERVER) {
	status = listen(fd, 5);
	if (status < 0) {
	    close(fd);
	    syscall = "listen(2)";
	}
    }

    /* create new instance */
    return init_sock(arg->sock, fd);
}

static VALUE
init_inetsock(VALUE sock, VALUE remote_host, VALUE remote_serv,
	      VALUE local_host, VALUE local_serv, int type)
{
    struct inetsock_arg arg;
    arg.sock = sock;
    arg.remote.host = remote_host;
    arg.remote.serv = remote_serv;
    arg.remote.res = 0;
    arg.local.host = local_host;
    arg.local.serv = local_serv;
    arg.local.res = 0;
    arg.type = type;
    arg.fd = -1;
    return rb_ensure(init_inetsock_internal, (VALUE)&arg,
		     inetsock_cleanup, (VALUE)&arg);
}

/*
 * call-seq:
 *    TCPSocket.new(remote_host, remote_port, local_host=nil, local_port=nil)
 *
 * Opens a TCP connection to +remote_host+ on +remote_port+.  If +local_host+
 * and +local_port+ are specified, then those parameters are used on the local
 * end to establish the connection.
 */
static VALUE
tcp_init(VALUE sock, SEL sel, int argc, VALUE *argv)
{
    VALUE remote_host, remote_serv;
    VALUE local_host, local_serv;

    rb_scan_args(argc, argv, "22", &remote_host, &remote_serv,
			&local_host, &local_serv);

    return init_inetsock(sock, remote_host, remote_serv,
			local_host, local_serv, INET_CLIENT);
}

struct hostent_arg {
    VALUE host;
    struct addrinfo* addr;
    VALUE (*ipaddr)(struct sockaddr*, size_t);
};

static VALUE
make_hostent_internal(struct hostent_arg *arg)
{
    VALUE host = arg->host;
    struct addrinfo* addr = arg->addr;
    VALUE (*ipaddr)(struct sockaddr*, size_t) = arg->ipaddr;

    struct addrinfo *ai;
    struct hostent *h;
    VALUE ary, names;
    char **pch;
    const char* hostp;
    char hbuf[NI_MAXHOST];

    ary = rb_ary_new();
    if (addr->ai_canonname) {
        hostp = addr->ai_canonname;
    }
    else {
        hostp = host_str(host, hbuf, sizeof(hbuf), NULL);
    }
    rb_ary_push(ary, rb_str_new2(hostp));

    if (addr->ai_canonname && (h = gethostbyname(addr->ai_canonname))) {
        names = rb_ary_new();
        if (h->h_aliases != NULL) {
            for (pch = h->h_aliases; *pch; pch++) {
                rb_ary_push(names, rb_str_new2(*pch));
            }
        }
    }
    else {
        names = rb_ary_new2(0);
    }
    rb_ary_push(ary, names);
    rb_ary_push(ary, INT2NUM(addr->ai_family));
    for (ai = addr; ai; ai = ai->ai_next) {
        rb_ary_push(ary, (*ipaddr)(ai->ai_addr, ai->ai_addrlen));
    }

    return ary;
}

static VALUE
make_hostent(VALUE host, struct addrinfo *addr, VALUE (*ipaddr)(struct sockaddr *, size_t))
{
    struct hostent_arg arg;

    arg.host = host;
    arg.addr = addr;
    arg.ipaddr = ipaddr;
    return rb_ensure(make_hostent_internal, (VALUE)&arg,
		     RUBY_METHOD_FUNC(freeaddrinfo), (VALUE)addr);
}

static VALUE
tcp_sockaddr(struct sockaddr *addr, size_t len)
{
    return make_ipaddr(addr);
}

/*
 * call-seq:
 *   TCPSocket.gethostbyname(hostname) => [official_hostname, alias_hostnames, address_family, *address_list]
 *
 * Lookups host information by _hostname_.
 *
 *   TCPSocket.gethostbyname("localhost")
 *   #=> ["localhost", ["hal"], 2, "127.0.0.1"]
 *
 */
static VALUE
tcp_s_gethostbyname(VALUE obj, SEL sel, VALUE host)
{
    rb_secure(3);
    return make_hostent(host, sock_addrinfo(host, Qnil, SOCK_STREAM,
		AI_CANONNAME), tcp_sockaddr);
}

/*
 * call-seq:
 *   TCPServer.new([hostname,] port)                    => tcpserver
 *
 * Creates a new server socket bound to _port_.
 *
 * If _hostname_ is given, the socket is bound to it.
 *
 *   serv = TCPServer.new("127.0.0.1", 28561)
 *   s = serv.accept
 *   s.puts Time.now
 *   s.close
 */
static VALUE
tcp_svr_init(VALUE sock, SEL sel, int argc, VALUE *argv)
{
    VALUE arg1, arg2;

    if (rb_scan_args(argc, argv, "11", &arg1, &arg2) == 2)
	return init_inetsock(sock, arg1, arg2, Qnil, Qnil, INET_SERVER);
    else
	return init_inetsock(sock, Qnil, arg1, Qnil, Qnil, INET_SERVER);
}

static void
make_fd_nonblock(int fd)
{
    int flags;
#ifdef F_GETFL
    flags = fcntl(fd, F_GETFL);
    if (flags == -1) {
        rb_sys_fail(0);
    }
#else
    flags = 0;
#endif
    flags |= O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flags) == -1) {
        rb_sys_fail(0);
    }
}

static VALUE
s_accept_nonblock(VALUE klass, rb_io_t *fptr, struct sockaddr *sockaddr, socklen_t *len)
{
    int fd2;

    rb_secure(3);
    rb_io_set_nonblock(fptr);
    fd2 = accept(fptr->fd, (struct sockaddr*)sockaddr, len);
    if (fd2 < 0) {
	switch (errno) {
	  case EAGAIN:
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
	  case EWOULDBLOCK:
#endif
	  case ECONNABORTED:
#if defined EPROTO
	  case EPROTO:
#endif
            rb_mod_sys_fail(rb_mWaitReadable, "accept(2) would block");
	}
        rb_sys_fail("accept(2)");
    }
    make_fd_nonblock(fd2);
    return init_sock(rb_obj_alloc(klass), fd2);
}

static VALUE
s_accept(VALUE klass, int fd, struct sockaddr *sockaddr, socklen_t *len)
{
    int fd2;
    int retry = 0;

    rb_secure(3);
  retry:
    rb_thread_wait_fd(fd);
#if defined(_nec_ews)
    fd2 = accept(fd, sockaddr, len);
#else
    fd2 = accept(fd, sockaddr, len);
#endif
    if (fd2 < 0) {
	switch (errno) {
	  case EMFILE:
	  case ENFILE:
	    if (retry) break;
	    rb_gc();
	    retry = 1;
	    goto retry;
	  default:
	    if (!rb_io_wait_readable(fd)) break;
	    retry = 0;
	    goto retry;
	}
	rb_sys_fail(0);
    }
    if (!klass) return INT2NUM(fd2);
    return init_sock(rb_obj_alloc(klass), fd2);
}

/*
 * call-seq:
 *   tcpserver.accept => tcpsocket
 *
 *   TCPServer.open("127.0.0.1", 14641) {|serv|
 *     s = serv.accept
 *     s.puts Time.now
 *     s.close
 *   }
 *
 */
static VALUE
tcp_accept(VALUE sock, SEL sel)
{
    rb_io_t *fptr;
    struct sockaddr_storage from;
    socklen_t fromlen;

    GetOpenFile(sock, fptr);
    fromlen = (socklen_t)sizeof(from);
    return s_accept(rb_cTCPSocket, fptr->fd,
		    (struct sockaddr*)&from, &fromlen);
}

/*
 * call-seq:
 * 	tcpserver.accept_nonblock => tcpsocket
 *
 * Accepts an incoming connection using accept(2) after
 * O_NONBLOCK is set for the underlying file descriptor.
 * It returns an accepted TCPSocket for the incoming connection.
 *
 * === Example
 * 	require 'socket'
 * 	serv = TCPServer.new(2202)
 * 	begin # emulate blocking accept
 * 	  sock = serv.accept_nonblock
 * 	rescue IO::WaitReadable, Errno::EINTR
 * 	  IO.select([serv])
 * 	  retry
 * 	end
 * 	# sock is an accepted socket.
 *
 * Refer to Socket#accept for the exceptions that may be thrown if the call
 * to TCPServer#accept_nonblock fails.
 *
 * TCPServer#accept_nonblock may raise any error corresponding to accept(2) failure,
 * including Errno::EWOULDBLOCK.
 *
 * If the exception is Errno::EWOULDBLOCK, Errno::AGAIN, Errno::ECONNABORTED, Errno::EPROTO,
 * it is extended by IO::WaitReadable.
 * So IO::WaitReadable can be used to rescue the exceptions for retrying accept_nonblock.
 *
 * === See
 * * TCPServer#accept
 * * Socket#accept
 */
static VALUE
tcp_accept_nonblock(VALUE sock, SEL sel)
{
    rb_io_t *fptr;
    struct sockaddr_storage from;
    socklen_t fromlen;

    GetOpenFile(sock, fptr);
    fromlen = (socklen_t)sizeof(from);
    return s_accept_nonblock(rb_cTCPSocket, fptr,
			     (struct sockaddr *)&from, &fromlen);
}

/*
 * call-seq:
 *   tcpserver.sysaccept => file_descriptor
 *
 * Returns a file descriptor of a accepted connection.
 *
 *   TCPServer.open("127.0.0.1", 28561) {|serv|
 *     fd = serv.sysaccept
 *     s = IO.for_fd(fd)
 *     s.puts Time.now
 *     s.close
 *   }
 *
 */
static VALUE
tcp_sysaccept(VALUE sock, SEL sel)
{
    rb_io_t *fptr;
    struct sockaddr_storage from;
    socklen_t fromlen;

    GetOpenFile(sock, fptr);
    fromlen = (socklen_t)sizeof(from);
    return s_accept(0, fptr->fd, (struct sockaddr*)&from, &fromlen);
}

#ifdef HAVE_SYS_UN_H
struct unixsock_arg {
    struct sockaddr_un *sockaddr;
    int fd;
};

static VALUE
unixsock_connect_internal(struct unixsock_arg *arg)
{
    return (VALUE)ruby_connect(arg->fd, (struct sockaddr*)arg->sockaddr,
			       (socklen_t)sizeof(*arg->sockaddr), 0);
}

static VALUE
init_unixsock(VALUE sock, VALUE path, int server)
{
    struct sockaddr_un sockaddr;
    int fd, status;
    rb_io_t *fptr;

    SafeStringValue(path);
    fd = ruby_socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
	rb_sys_fail("socket(2)");
    }

    MEMZERO(&sockaddr, struct sockaddr_un, 1);
    sockaddr.sun_family = AF_UNIX;
    if (sizeof(sockaddr.sun_path) <= (size_t)RSTRING_LEN(path)) {
        rb_raise(rb_eArgError, "too long unix socket path (max: %dbytes)",
            (int)sizeof(sockaddr.sun_path)-1);
    }
    memcpy(sockaddr.sun_path, RSTRING_PTR(path), RSTRING_LEN(path));

    if (server) {
        status = bind(fd, (struct sockaddr*)&sockaddr, (socklen_t)sizeof(sockaddr));
    }
    else {
	int prot;
	struct unixsock_arg arg;
	arg.sockaddr = &sockaddr;
	arg.fd = fd;
        status = (int)rb_protect((VALUE(*)(VALUE))unixsock_connect_internal, (VALUE)&arg, &prot);
	if (prot) {
	    close(fd);
	    rb_jump_tag(prot);
	}
    }

    if (status < 0) {
	close(fd);
	rb_sys_fail(sockaddr.sun_path);
    }

    if (server) {
	if (listen(fd, 5) < 0) {
	    close(fd);
	    rb_sys_fail("listen(2)");
	}
    }

    init_sock(sock, fd);
    if (server) {
	GetOpenFile(sock, fptr);
        GC_WB(&fptr->path, rb_str_dup(path));
    }

    return sock;
}
#endif

static ID id_numeric, id_hostname;

int
revlookup_flag(VALUE revlookup, int *norevlookup)
{
#define return_norevlookup(x) {*norevlookup = x; return 1;}
    ID id;

    switch (revlookup) {
      case Qtrue:  return_norevlookup(0);
      case Qfalse: return_norevlookup(1);
      case Qnil: break;
      default:
	Check_Type(revlookup, T_SYMBOL);
	id = SYM2ID(revlookup);
	if (id == id_numeric) return_norevlookup(1);
	if (id == id_hostname) return_norevlookup(0);
	rb_raise(rb_eArgError, "invalid reverse_lookup flag: :%s", rb_id2name(id));
    }
    return 0;
#undef return_norevlookup
}

/*
 * call-seq:
 *   ipsocket.addr([reverse_lookup]) => [address_family, port, hostname, numeric_address]
 *
 * Returns the local address as an array which contains
 * address_family, port, hostname and numeric_address.
 *
 * If +reverse_lookup+ is +true+ or +:hostname+,
 * hostname is obtained from numeric_address using reverse lookup.
 * Or if it is +false+, or +:numeric+,
 * hostname is same as numeric_address.
 * Or if it is +nil+ or ommitted, obeys to +ipsocket.do_not_reverse_lookup+.
 * See +Socket.getaddrinfo+ also.
 *
 *   TCPSocket.open("www.ruby-lang.org", 80) {|sock|
 *     p sock.addr #=> ["AF_INET", 49429, "hal", "192.168.0.128"]
 *     p sock.addr(true)  #=> ["AF_INET", 49429, "hal", "192.168.0.128"]
 *     p sock.addr(false) #=> ["AF_INET", 49429, "192.168.0.128", "192.168.0.128"]
 *     p sock.addr(:hostname)  #=> ["AF_INET", 49429, "hal", "192.168.0.128"]
 *     p sock.addr(:numeric)   #=> ["AF_INET", 49429, "192.168.0.128", "192.168.0.128"]
 *   }
 *
 */
static VALUE
ip_addr(VALUE sock, SEL sel, int argc, VALUE *argv)
{
    rb_io_t *fptr;
    struct sockaddr_storage addr;
    socklen_t len = (socklen_t)sizeof addr;
    int norevlookup;

    GetOpenFile(sock, fptr);

    if (argc < 1 || !revlookup_flag(argv[0], &norevlookup))
	norevlookup = fptr->mode & FMODE_NOREVLOOKUP;
    if (getsockname(fptr->fd, (struct sockaddr*)&addr, &len) < 0)
	rb_sys_fail("getsockname(2)");
    return ipaddr((struct sockaddr*)&addr, norevlookup);
}

/*
 * call-seq:
 *   ipsocket.peeraddr([reverse_lookup]) => [address_family, port, hostname, numeric_address]
 *
 * Returns the remote address as an array which contains
 * address_family, port, hostname and numeric_address.
 * It is defined for connection oriented socket such as TCPSocket.
 *
 * If +reverse_lookup+ is +true+ or +:hostname+,
 * hostname is obtained from numeric_address using reverse lookup.
 * Or if it is +false+, or +:numeric+,
 * hostname is same as numeric_address.
 * Or if it is +nil+ or ommitted, obeys to +ipsocket.do_not_reverse_lookup+.
 * See +Socket.getaddrinfo+ also.
 *
 *   TCPSocket.open("www.ruby-lang.org", 80) {|sock|
 *     p sock.peeraddr #=> ["AF_INET", 80, "carbon.ruby-lang.org", "221.186.184.68"]
 *     p sock.peeraddr(true)  #=> ["AF_INET", 80, "221.186.184.68", "221.186.184.68"]
 *     p sock.peeraddr(false) #=> ["AF_INET", 80, "221.186.184.68", "221.186.184.68"]
 *     p sock.peeraddr(:hostname) #=> ["AF_INET", 80, "carbon.ruby-lang.org", "221.186.184.68"]
 *     p sock.peeraddr(:numeric)  #=> ["AF_INET", 80, "221.186.184.68", "221.186.184.68"]
 *   }
 *
 */
static VALUE
ip_peeraddr(VALUE sock, SEL sel, int argc, VALUE *argv)
{
    rb_io_t *fptr;
    struct sockaddr_storage addr;
    socklen_t len = (socklen_t)sizeof addr;
    int norevlookup;

    GetOpenFile(sock, fptr);

    if (argc < 1 || !revlookup_flag(argv[0], &norevlookup))
	norevlookup = fptr->mode & FMODE_NOREVLOOKUP;
    if (getpeername(fptr->fd, (struct sockaddr*)&addr, &len) < 0)
	rb_sys_fail("getpeername(2)");
    return ipaddr((struct sockaddr*)&addr, norevlookup);
}

/*
 * call-seq:
 *   ipsocket.recvfrom(maxlen)        => [mesg, ipaddr]
 *   ipsocket.recvfrom(maxlen, flags) => [mesg, ipaddr]
 *
 * Receives a message and return the message as a string and
 * an address which the message come from.
 *
 * _maxlen_ is the maximum number of bytes to receive.
 *
 * _flags_ should be a bitwise OR of Socket::MSG_* constants.
 *
 * ipaddr is same as IPSocket#{peeraddr,addr}.
 *
 *   u1 = UDPSocket.new
 *   u1.bind("127.0.0.1", 4913)
 *   u2 = UDPSocket.new
 *   u2.send "uuuu", 0, "127.0.0.1", 4913
 *   p u1.recvfrom(10) #=> ["uuuu", ["AF_INET", 33230, "localhost", "127.0.0.1"]]
 *
 */
static VALUE
ip_recvfrom(VALUE sock, SEL sel, int argc, VALUE *argv)
{
    return s_recvfrom(sock, argc, argv, RECV_IP);
}

/*
 * call-seq:
 *   IPSocket.getaddress(host)        => ipaddress
 *
 * Lookups the IP address of _host_.
 *
 *   IPSocket.getaddress("localhost")     #=> "127.0.0.1"
 *   IPSocket.getaddress("ip6-localhost") #=> "::1"
 *
 */
static VALUE
ip_s_getaddress(VALUE obj, SEL sel, VALUE host)
{
    struct sockaddr_storage addr;
    struct addrinfo *res = sock_addrinfo(host, Qnil, SOCK_STREAM, 0);

    /* just take the first one */
    memcpy(&addr, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);

    return make_ipaddr((struct sockaddr*)&addr);
}

/*
 * call-seq:
 *   UDPSocket.new([address_family]) => socket
 *
 * Creates a new UDPSocket object.
 *
 * _address_family_ should be an integer, a string or a symbol:
 * Socket::AF_INET, "AF_INET", :INET, etc.
 *
 *   UDPSocket.new                   #=> #<UDPSocket:fd 3>
 *   UDPSocket.new(Socket::AF_INET6) #=> #<UDPSocket:fd 4>
 *
 */
static VALUE
udp_init(VALUE sock, SEL sel, int argc, VALUE *argv)
{
    VALUE arg;
    int family = AF_INET;
    int fd;

    rb_secure(3);
    if (rb_scan_args(argc, argv, "01", &arg) == 1) {
	family = family_arg(arg);
    }
    fd = ruby_socket(family, SOCK_DGRAM, 0);
    if (fd < 0) {
	rb_sys_fail("socket(2) - udp");
    }

    return init_sock(sock, fd);
}

struct udp_arg
{
    struct addrinfo *res;
    int fd;
};

static VALUE
udp_connect_internal(struct udp_arg *arg)
{
    int fd = arg->fd;
    struct addrinfo *res;

    for (res = arg->res; res; res = res->ai_next) {
	if (ruby_connect(fd, res->ai_addr, res->ai_addrlen, 0) >= 0) {
	    return Qtrue;
	}
    }
    return Qfalse;
}

/*
 * call-seq:
 *   udpsocket.connect(host, port) => 0
 *
 * Connects _udpsocket_ to _host_:_port_.
 *
 * This makes possible to send without destination address.
 *
 *   u1 = UDPSocket.new
 *   u1.bind("127.0.0.1", 4913)
 *   u2 = UDPSocket.new
 *   u2.connect("127.0.0.1", 4913)
 *   u2.send "uuuu", 0
 *   p u1.recvfrom(10) #=> ["uuuu", ["AF_INET", 33230, "localhost", "127.0.0.1"]]
 *
 */
static VALUE
udp_connect(VALUE sock, SEL sel, VALUE host, VALUE port)
{
    rb_io_t *fptr;
    struct udp_arg arg;
    VALUE ret;

    rb_secure(3);
    arg.res = sock_addrinfo(host, port, SOCK_DGRAM, 0);
    GetOpenFile(sock, fptr);
    arg.fd = fptr->fd;
    ret = rb_ensure(udp_connect_internal, (VALUE)&arg,
		    RUBY_METHOD_FUNC(freeaddrinfo), (VALUE)arg.res);
    if (!ret) rb_sys_fail("connect(2)");
    return INT2FIX(0);
}

/*
 * call-seq:
 *   udpsocket.bind(host, port) #=> 0
 *
 * Binds _udpsocket_ to _host_:_port_.
 *
 *   u1 = UDPSocket.new
 *   u1.bind("127.0.0.1", 4913)
 *   u1.send "message-to-self", 0, "127.0.0.1", 4913
 *   p u1.recvfrom(10) #=> ["message-to", ["AF_INET", 4913, "localhost", "127.0.0.1"]]
 *
 */
static VALUE
udp_bind(VALUE sock, SEL sel, VALUE host, VALUE port)
{
    rb_io_t *fptr;
    struct addrinfo *res0, *res;

    rb_secure(3);
    res0 = sock_addrinfo(host, port, SOCK_DGRAM, 0);
    GetOpenFile(sock, fptr);
    for (res = res0; res; res = res->ai_next) {
	if (bind(fptr->fd, res->ai_addr, res->ai_addrlen) < 0) {
	    continue;
	}
	freeaddrinfo(res0);
	return INT2FIX(0);
    }
    freeaddrinfo(res0);
    rb_sys_fail("bind(2)");
    return INT2FIX(0);
}

/*
 * call-seq:
 *   udpsocket.send(mesg, flags, host, port)  => numbytes_sent
 *   udpsocket.send(mesg, flags, sockaddr_to) => numbytes_sent
 *   udpsocket.send(mesg, flags)              => numbytes_sent
 *
 * Sends _mesg_ via _udpsocket_.
 *
 * _flags_ should be a bitwise OR of Socket::MSG_* constants.
 *
 *   u1 = UDPSocket.new
 *   u1.bind("127.0.0.1", 4913)
 *
 *   u2 = UDPSocket.new
 *   u2.send "hi", 0, "127.0.0.1", 4913
 *
 *   mesg, addr = u1.recvfrom(10)
 *   u1.send mesg, 0, addr[3], addr[1]
 *
 *   p u2.recv(100) #=> "hi"
 *
 */
static VALUE
udp_send(VALUE sock, SEL sel, int argc, VALUE *argv)
{
    VALUE mesg, flags, host, port;
    rb_io_t *fptr;
    int n;
    struct addrinfo *res0, *res;

    if (argc == 2 || argc == 3) {
	return bsock_send(sock, 0, argc, argv);
    }
    rb_secure(4);
    rb_scan_args(argc, argv, "4", &mesg, &flags, &host, &port);

    StringValue(mesg);
    res0 = sock_addrinfo(host, port, SOCK_DGRAM, 0);
    GetOpenFile(sock, fptr);
    for (res = res0; res; res = res->ai_next) {
      retry:
	rb_thread_fd_writable(fptr->fd);
	n = sendto(fptr->fd, RSTRING_PTR(mesg), RSTRING_LEN(mesg), NUM2INT(flags),
		   res->ai_addr, res->ai_addrlen);
	if (n >= 0) {
	    freeaddrinfo(res0);
	    return INT2FIX(n);
	}
	if (rb_io_wait_writable(fptr->fd)) {
	    goto retry;
	}
    }
    freeaddrinfo(res0);
    rb_sys_fail("sendto(2)");
    return INT2FIX(n);
}

/*
 * call-seq:
 * 	udpsocket.recvfrom_nonblock(maxlen) => [mesg, sender_inet_addr]
 * 	udpsocket.recvfrom_nonblock(maxlen, flags) => [mesg, sender_inet_addr]
 *
 * Receives up to _maxlen_ bytes from +udpsocket+ using recvfrom(2) after
 * O_NONBLOCK is set for the underlying file descriptor.
 * If _maxlen_ is omitted, its default value is 65536.
 * _flags_ is zero or more of the +MSG_+ options.
 * The first element of the results, _mesg_, is the data received.
 * The second element, _sender_inet_addr_, is an array to represent the sender address.
 *
 * When recvfrom(2) returns 0,
 * Socket#recvfrom_nonblock returns an empty string as data.
 * It means an empty packet.
 *
 * === Parameters
 * * +maxlen+ - the number of bytes to receive from the socket
 * * +flags+ - zero or more of the +MSG_+ options
 *
 * === Example
 * 	require 'socket'
 * 	s1 = UDPSocket.new
 * 	s1.bind("127.0.0.1", 0)
 * 	s2 = UDPSocket.new
 * 	s2.bind("127.0.0.1", 0)
 * 	s2.connect(*s1.addr.values_at(3,1))
 * 	s1.connect(*s2.addr.values_at(3,1))
 * 	s1.send "aaa", 0
 * 	begin # emulate blocking recvfrom
 * 	  p s2.recvfrom_nonblock(10)  #=> ["aaa", ["AF_INET", 33302, "localhost.localdomain", "127.0.0.1"]]
 * 	rescue IO::WaitReadable
 * 	  IO.select([s2])
 * 	  retry
 * 	end
 *
 * Refer to Socket#recvfrom for the exceptions that may be thrown if the call
 * to _recvfrom_nonblock_ fails.
 *
 * UDPSocket#recvfrom_nonblock may raise any error corresponding to recvfrom(2) failure,
 * including Errno::EWOULDBLOCK.
 *
 * If the exception is Errno::EWOULDBLOCK or Errno::AGAIN,
 * it is extended by IO::WaitReadable.
 * So IO::WaitReadable can be used to rescue the exceptions for retrying recvfrom_nonblock.
 *
 * === See
 * * Socket#recvfrom
 */
static VALUE
udp_recvfrom_nonblock(VALUE sock, SEL sel, int argc, VALUE *argv)
{
    return s_recvfrom_nonblock(sock, argc, argv, RECV_IP);
}

#ifdef HAVE_SYS_UN_H
/*
 * call-seq:
 *   UNIXSocket.new(path) => unixsocket
 *
 * Creates a new UNIX client socket connected to _path_.
 *
 *   s = UNIXSocket.new("/tmp/sock")
 *   s.send "hello", 0
 *
 */
static VALUE
unix_init(VALUE sock, SEL sel, VALUE path)
{
    return init_unixsock(sock, path, 0);
}

static char*
unixpath(struct sockaddr_un *sockaddr, socklen_t len)
{
    if (sockaddr->sun_path < (char*)sockaddr + len)
        return sockaddr->sun_path;
    else
        return "";
}

/*
 * call-seq:
 *   unixsocket.path => path
 *
 * Returns the path of the local address of unixsocket.
 *
 *   s = UNIXServer.new("/tmp/sock")
 *   p s.path #=> "/tmp/sock"
 *
 */
static VALUE
unix_path(VALUE sock, SEL sel)
{
    rb_io_t *fptr;

    GetOpenFile(sock, fptr);
    if (fptr->path == 0) {
	struct sockaddr_un addr;
	socklen_t len = (socklen_t)sizeof(addr);
	if (getsockname(fptr->fd, (struct sockaddr*)&addr, &len) < 0) {
	    rb_sys_fail(0);
	}
	GC_WB(&fptr->path, rb_str_new2(unixpath(&addr, len)));
    }
    return rb_str_dup(fptr->path);
}

/*
 * call-seq:
 *   UNIXServer.new(path) => unixserver
 *
 * Creates a new UNIX server socket bound to _path_.
 *
 *   serv = UNIXServer.new("/tmp/sock")
 *   s = serv.accept
 *   p s.read
 */
static VALUE
unix_svr_init(VALUE sock, SEL sel, VALUE path)
{
    return init_unixsock(sock, path, 1);
}

/*
 * call-seq:
 *   unixsocket.recvfrom(maxlen [, flags]) => [mesg, unixaddress]
 *
 * Receives a message via _unixsocket_.
 *
 * _maxlen_ is the maximum number of bytes to receive.
 *
 * _flags_ should be a bitwise OR of Socket::MSG_* constants.
 *
 *   s1 = Socket.new(:UNIX, :DGRAM, 0)
 *   s1_ai = Addrinfo.unix("/tmp/sock1")
 *   s1.bind(s1_ai)
 *
 *   s2 = Socket.new(:UNIX, :DGRAM, 0)
 *   s2_ai = Addrinfo.unix("/tmp/sock2")
 *   s2.bind(s2_ai)
 *   s3 = UNIXSocket.for_fd(s2.fileno)
 *
 *   s1.send "a", 0, s2_ai
 *   p s3.recvfrom(10) #=> ["a", ["AF_UNIX", "/tmp/sock1"]]
 *
 */
static VALUE
unix_recvfrom(VALUE sock, SEL sel, int argc, VALUE *argv)
{
    return s_recvfrom(sock, argc, argv, RECV_UNIX);
}

#if defined(HAVE_ST_MSG_CONTROL) && defined(SCM_RIGHTS)
#define FD_PASSING_BY_MSG_CONTROL 1
#else
#define FD_PASSING_BY_MSG_CONTROL 0
#endif

#if defined(HAVE_ST_MSG_ACCRIGHTS)
#define FD_PASSING_BY_MSG_ACCRIGHTS 1
#else
#define FD_PASSING_BY_MSG_ACCRIGHTS 0
#endif

/*
 * call-seq:
 *   unixsocket.send_io(io) => nil
 *
 * Sends _io_ as file descriptor passing.
 *
 *   s1, s2 = UNIXSocket.pair
 *
 *   s1.send_io STDOUT
 *   stdout = s2.recv_io
 *
 *   p STDOUT.fileno #=> 1
 *   p stdout.fileno #=> 6
 *
 *   stdout.puts "hello" # outputs "hello\n" to standard output.
 */
static VALUE
unix_send_io(VALUE sock, SEL sel, VALUE val)
{
#if defined(HAVE_SENDMSG) && (FD_PASSING_BY_MSG_CONTROL || FD_PASSING_BY_MSG_ACCRIGHTS)
    int fd;
    rb_io_t *fptr;
    struct msghdr msg;
    struct iovec vec[1];
    char buf[1];

#if FD_PASSING_BY_MSG_CONTROL
    struct {
	struct cmsghdr hdr;
        char pad[8+sizeof(int)+8];
    } cmsg;
#endif

    if (rb_obj_is_kind_of(val, rb_cIO)) {
        rb_io_t *valfptr;
	GetOpenFile(val, valfptr);
	fd = valfptr->fd;
    }
    else if (FIXNUM_P(val)) {
        fd = FIX2INT(val);
    }
    else {
	rb_raise(rb_eTypeError, "neither IO nor file descriptor");
    }

    GetOpenFile(sock, fptr);

    msg.msg_name = NULL;
    msg.msg_namelen = 0;

    /* Linux and Solaris doesn't work if msg_iov is NULL. */
    buf[0] = '\0';
    vec[0].iov_base = buf;
    vec[0].iov_len = 1;
    msg.msg_iov = vec;
    msg.msg_iovlen = 1;

#if FD_PASSING_BY_MSG_CONTROL
    msg.msg_control = (caddr_t)&cmsg;
    msg.msg_controllen = (socklen_t)CMSG_LEN(sizeof(int));
    msg.msg_flags = 0;
    MEMZERO((char*)&cmsg, char, sizeof(cmsg));
    cmsg.hdr.cmsg_len = (socklen_t)CMSG_LEN(sizeof(int));
    cmsg.hdr.cmsg_level = SOL_SOCKET;
    cmsg.hdr.cmsg_type = SCM_RIGHTS;
    *(int *)CMSG_DATA(&cmsg.hdr) = fd;
#else
    msg.msg_accrights = (caddr_t)&fd;
    msg.msg_accrightslen = sizeof(fd);
#endif

    while (sendmsg(fptr->fd, &msg, 0) == -1) {
	if (!rb_io_wait_readable(fptr->fd)) {
	    rb_sys_fail("sendmsg(2)");
	}
    }

    return Qnil;
#else
    rb_notimplement();
    return Qnil;		/* not reached */
#endif
}

/*
 * call-seq:
 *   unixsocket.recv_io([klass [, mode]]) => io
 *
 *   UNIXServer.open("/tmp/sock") {|serv|
 *     UNIXSocket.open("/tmp/sock") {|c|
 *       s = serv.accept
 *
 *       c.send_io STDOUT
 *       stdout = s.recv_io
 *
 *       p STDOUT.fileno #=> 1
 *       p stdout.fileno #=> 7
 *
 *       stdout.puts "hello" # outputs "hello\n" to standard output.
 *     }
 *   }
 *
 */
static VALUE
unix_recv_io(VALUE sock, SEL sel, int argc, VALUE *argv)
{
#if defined(HAVE_RECVMSG) && (FD_PASSING_BY_MSG_CONTROL || FD_PASSING_BY_MSG_ACCRIGHTS)
    VALUE klass, mode;
    rb_io_t *fptr;
    struct msghdr msg;
    struct iovec vec[2];
    char buf[1];

    int fd;
#if FD_PASSING_BY_MSG_CONTROL
    struct {
	struct cmsghdr hdr;
        char pad[8+sizeof(int)+8];
    } cmsg;
#endif

    rb_scan_args(argc, argv, "02", &klass, &mode);
    if (argc == 0)
	klass = rb_cIO;
    if (argc <= 1)
	mode = Qnil;

    GetOpenFile(sock, fptr);

    rb_io_wait_readable(fptr->fd);

    msg.msg_name = NULL;
    msg.msg_namelen = 0;

    vec[0].iov_base = buf;
    vec[0].iov_len = sizeof(buf);
    msg.msg_iov = vec;
    msg.msg_iovlen = 1;

#if FD_PASSING_BY_MSG_CONTROL
    msg.msg_control = (caddr_t)&cmsg;
    msg.msg_controllen = (socklen_t)CMSG_SPACE(sizeof(int));
    msg.msg_flags = 0;
    cmsg.hdr.cmsg_len = (socklen_t)CMSG_LEN(sizeof(int));
    cmsg.hdr.cmsg_level = SOL_SOCKET;
    cmsg.hdr.cmsg_type = SCM_RIGHTS;
    *(int *)CMSG_DATA(&cmsg.hdr) = -1;
#else
    msg.msg_accrights = (caddr_t)&fd;
    msg.msg_accrightslen = sizeof(fd);
    fd = -1;
#endif

    while (recvmsg(fptr->fd, &msg, 0) == -1) {
	if (!rb_io_wait_readable(fptr->fd)) {
	    rb_sys_fail("recvmsg(2)");
	}
    }

#if FD_PASSING_BY_MSG_CONTROL
    if (msg.msg_controllen != CMSG_SPACE(sizeof(int))) {
      rb_raise(rb_eSocket,
          "file descriptor was not passed (msg_controllen=%d, %ld expected)",
          msg.msg_controllen, CMSG_SPACE(sizeof(int)));
    }
    if (cmsg.hdr.cmsg_len != CMSG_LEN(sizeof(int))) {
      rb_raise(rb_eSocket,
          "file descriptor was not passed (cmsg_len=%d, %ld expected)",
          cmsg.hdr.cmsg_len, CMSG_LEN(sizeof(int)));
    }
    if (cmsg.hdr.cmsg_level != SOL_SOCKET) {
      rb_raise(rb_eSocket,
          "file descriptor was not passed (cmsg_level=%d, %d expected)",
          cmsg.hdr.cmsg_level, SOL_SOCKET);
    }
    if (cmsg.hdr.cmsg_type != SCM_RIGHTS) {
      rb_raise(rb_eSocket,
          "file descriptor was not passed (cmsg_type=%d, %d expected)",
          cmsg.hdr.cmsg_type, SCM_RIGHTS);
    }
#else
    if (msg.msg_accrightslen != sizeof(fd)) {
	rb_raise(rb_eSocket,
            "file descriptor was not passed (accrightslen) : %d != %ld",
            msg.msg_accrightslen, sizeof(fd));
    }
#endif

#if FD_PASSING_BY_MSG_CONTROL
    fd = *(int *)CMSG_DATA(&cmsg.hdr);
#endif

    if (klass == Qnil)
	return INT2FIX(fd);
    else {
	static ID for_fd = 0;
	int ff_argc;
	VALUE ff_argv[2];
	if (!for_fd)
	    for_fd = rb_intern("for_fd");
	ff_argc = mode == Qnil ? 1 : 2;
	ff_argv[0] = INT2FIX(fd);
	ff_argv[1] = mode;
        return rb_funcall2(klass, for_fd, ff_argc, ff_argv);
    }
#else
    rb_notimplement();
    return Qnil;		/* not reached */
#endif
}

/*
 * call-seq:
 *   unixserver.accept => unixsocket
 *
 * Accepts a new connection.
 * It returns new UNIXSocket object.
 *
 *   UNIXServer.open("/tmp/sock") {|serv|
 *     UNIXSocket.open("/tmp/sock") {|c|
 *       s = serv.accept
 *       s.puts "hi"
 *       s.close
 *       p c.read #=> "hi\n"
 *     }
 *   }
 *
 */
static VALUE
unix_accept(VALUE sock, SEL sel)
{
    rb_io_t *fptr;
    struct sockaddr_un from;
    socklen_t fromlen;

    GetOpenFile(sock, fptr);
    fromlen = (socklen_t)sizeof(struct sockaddr_un);
    return s_accept(rb_cUNIXSocket, fptr->fd,
		    (struct sockaddr*)&from, &fromlen);
}

/*
 * call-seq:
 * 	unixserver.accept_nonblock => unixsocket
 *
 * Accepts an incoming connection using accept(2) after
 * O_NONBLOCK is set for the underlying file descriptor.
 * It returns an accepted UNIXSocket for the incoming connection.
 *
 * === Example
 * 	require 'socket'
 * 	serv = UNIXServer.new("/tmp/sock")
 * 	begin # emulate blocking accept
 * 	  sock = serv.accept_nonblock
 * 	rescue IO::WaitReadable, Errno::EINTR
 * 	  IO.select([serv])
 * 	  retry
 * 	end
 * 	# sock is an accepted socket.
 *
 * Refer to Socket#accept for the exceptions that may be thrown if the call
 * to UNIXServer#accept_nonblock fails.
 *
 * UNIXServer#accept_nonblock may raise any error corresponding to accept(2) failure,
 * including Errno::EWOULDBLOCK.
 *
 * If the exception is Errno::EWOULDBLOCK, Errno::AGAIN, Errno::ECONNABORTED or Errno::EPROTO,
 * it is extended by IO::WaitReadable.
 * So IO::WaitReadable can be used to rescue the exceptions for retrying accept_nonblock.
 *
 * === See
 * * UNIXServer#accept
 * * Socket#accept
 */
static VALUE
unix_accept_nonblock(VALUE sock, SEL sel)
{
    rb_io_t *fptr;
    struct sockaddr_un from;
    socklen_t fromlen;

    GetOpenFile(sock, fptr);
    fromlen = (socklen_t)sizeof(from);
    return s_accept_nonblock(rb_cUNIXSocket, fptr,
			     (struct sockaddr *)&from, &fromlen);
}

/*
 * call-seq:
 *   unixserver.sysaccept => file_descriptor
 *
 * Accepts a new connection.
 * It returns the new file descriptor which is an integer.
 *
 *   UNIXServer.open("/tmp/sock") {|serv|
 *     UNIXSocket.open("/tmp/sock") {|c|
 *       fd = serv.sysaccept
 *       s = IO.new(fd)
 *       s.puts "hi"
 *       s.close
 *       p c.read #=> "hi\n"
 *     }
 *   }
 *
 */
static VALUE
unix_sysaccept(VALUE sock, SEL sel)
{
    rb_io_t *fptr;
    struct sockaddr_un from;
    socklen_t fromlen;

    GetOpenFile(sock, fptr);
    fromlen = (socklen_t)sizeof(struct sockaddr_un);
    return s_accept(0, fptr->fd, (struct sockaddr*)&from, &fromlen);
}

#ifdef HAVE_SYS_UN_H
static VALUE
unixaddr(struct sockaddr_un *sockaddr, socklen_t len)
{
    return rb_assoc_new(rb_str_new2("AF_UNIX"),
                        rb_str_new2(unixpath(sockaddr, len)));
}
#endif

/*
 * call-seq:
 *   unixsocket.addr => [address_family, unix_path]
 *
 * Returns the local address as an array which contains
 * address_family and unix_path.
 *
 * Example
 *   serv = UNIXServer.new("/tmp/sock")
 *   p serv.addr #=> ["AF_UNIX", "/tmp/sock"]
 */
static VALUE
unix_addr(VALUE sock, SEL sel)
{
    rb_io_t *fptr;
    struct sockaddr_un addr;
    socklen_t len = (socklen_t)sizeof addr;

    GetOpenFile(sock, fptr);

    if (getsockname(fptr->fd, (struct sockaddr*)&addr, &len) < 0)
	rb_sys_fail("getsockname(2)");
    return unixaddr(&addr, len);
}

/*
 * call-seq:
 *   unixsocket.peeraddr => [address_family, unix_path]
 *
 * Returns the remote address as an array which contains
 * address_family and unix_path.
 *
 * Example
 *   serv = UNIXServer.new("/tmp/sock")
 *   c = UNIXSocket.new("/tmp/sock")
 *   p c.peeraddr #=> ["AF_UNIX", "/tmp/sock"]
 */
static VALUE
unix_peeraddr(VALUE sock, SEL sel)
{
    rb_io_t *fptr;
    struct sockaddr_un addr;
    socklen_t len = (socklen_t)sizeof addr;

    GetOpenFile(sock, fptr);

    if (getpeername(fptr->fd, (struct sockaddr*)&addr, &len) < 0)
	rb_sys_fail("getpeername(2)");
    return unixaddr(&addr, len);
}
#endif

static void
setup_domain_and_type(VALUE domain, int *dv, VALUE type, int *tv)
{
    *dv = family_arg(domain);
    *tv = socktype_arg(type);
}

/*
 * call-seq:
 *   Socket.new(domain, socktype [, protocol]) => socket
 *
 * Creates a new socket object.
 *
 * _domain_ should be a communications domain such as: :INET, :INET6, :UNIX, etc.
 *
 * _socktype_ should be a socket type such as: :STREAM, :DGRAM, :RAW, etc.
 *
 * _protocol_ should be a protocol defined in the domain.
 * This is optional.
 * If it is not given, 0 is used internally.
 *
 *   Socket.new(:INET, :STREAM) # TCP socket
 *   Socket.new(:INET, :DGRAM)  # UDP socket
 *   Socket.new(:UNIX, :STREAM) # UNIX stream socket
 *   Socket.new(:UNIX, :DGRAM)  # UNIX datagram socket
 */
static VALUE
sock_initialize(VALUE sock, SEL sel, int argc, VALUE *argv)
{
    VALUE domain, type, protocol;
    int fd;
    int d, t;

    rb_scan_args(argc, argv, "21", &domain, &type, &protocol);
    if (NIL_P(protocol))
        protocol = INT2FIX(0);

    rb_secure(3);
    setup_domain_and_type(domain, &d, type, &t);
    fd = ruby_socket(d, t, NUM2INT(protocol));
    if (fd < 0) rb_sys_fail("socket(2)");

    return init_sock(sock, fd);
}

#if defined HAVE_SOCKETPAIR
static VALUE
io_call_close(VALUE io)
{
    return rb_funcall(io, rb_intern("close"), 0, 0);
}

static VALUE
io_close(VALUE io)
{
    return rb_rescue(io_call_close, io, 0, 0);
}

static VALUE
pair_yield(VALUE pair)
{
    return rb_ensure(rb_yield, pair, io_close, rb_ary_entry(pair, 1));
}
#endif

/*
 * call-seq:
 *   Socket.pair(domain, type, protocol)       => [socket1, socket2]
 *   Socket.socketpair(domain, type, protocol) => [socket1, socket2]
 *
 * Creates a pair of sockets connected each other.
 *
 * _domain_ should be a communications domain such as: :INET, :INET6, :UNIX, etc.
 *
 * _socktype_ should be a socket type such as: :STREAM, :DGRAM, :RAW, etc.
 *
 * _protocol_ should be a protocol defined in the domain.
 * 0 is default protocol for the domain.
 *
 *   s1, s2 = Socket.pair(:UNIX, :DGRAM, 0)
 *   s1.send "a", 0
 *   s1.send "b", 0
 *   p s2.recv(10) #=> "a"
 *   p s2.recv(10) #=> "b"
 *
 */
static VALUE
sock_s_socketpair(VALUE klass, SEL sel, int argc, VALUE *argv)
{
#if defined HAVE_SOCKETPAIR
    VALUE domain, type, protocol;
    int d, t, p, sp[2];
    int ret;
    VALUE s1, s2, r;

    rb_scan_args(argc, argv, "21", &domain, &type, &protocol);
    if (NIL_P(protocol))
        protocol = INT2FIX(0);

    setup_domain_and_type(domain, &d, type, &t);
    p = NUM2INT(protocol);
    ret = socketpair(d, t, p, sp);
    if (ret < 0 && (errno == EMFILE || errno == ENFILE)) {
        rb_gc();
        ret = socketpair(d, t, p, sp);
    }
    if (ret < 0) {
	rb_sys_fail("socketpair(2)");
    }

    s1 = init_sock(rb_obj_alloc(klass), sp[0]);
    s2 = init_sock(rb_obj_alloc(klass), sp[1]);
    r = rb_assoc_new(s1, s2);
    if (rb_block_given_p()) {
        return rb_ensure(pair_yield, r, io_close, s1);
    }
    return r;
#else
    rb_notimplement();
#endif
}

#ifdef HAVE_SYS_UN_H
/*
 * call-seq:
 *   UNIXSocket.pair([type [, protocol]])       => [unixsocket1, unixsocket2]
 *   UNIXSocket.socketpair([type [, protocol]]) => [unixsocket1, unixsocket2]
 *
 * Creates a pair of sockets connected each other.
 *
 * _socktype_ should be a socket type such as: :STREAM, :DGRAM, :RAW, etc.
 *
 * _protocol_ should be a protocol defined in the domain.
 * 0 is default protocol for the domain.
 *
 *   s1, s2 = UNIXSocket.pair
 *   s1.send "a", 0
 *   s1.send "b", 0
 *   p s2.recv(10) #=> "ab"
 *
 */
static VALUE
unix_s_socketpair(VALUE klass, SEL sel, int argc, VALUE *argv)
{
    VALUE domain, type, protocol;
    VALUE args[3];

    domain = INT2FIX(PF_UNIX);
    rb_scan_args(argc, argv, "02", &type, &protocol);
    if (argc == 0)
	type = INT2FIX(SOCK_STREAM);
    if (argc <= 1)
	protocol = INT2FIX(0);

    args[0] = domain;
    args[1] = type;
    args[2] = protocol;

    return sock_s_socketpair(klass, 0, 3, args);
}
#endif

/*
 * call-seq:
 * 	socket.connect(remote_sockaddr) => 0
 *
 * Requests a connection to be made on the given +remote_sockaddr+. Returns 0 if
 * successful, otherwise an exception is raised.
 *
 * === Parameter
 * * +remote_sockaddr+ - the +struct+ sockaddr contained in a string or Addrinfo object
 *
 * === Example:
 * 	# Pull down Google's web page
 * 	require 'socket'
 * 	include Socket::Constants
 * 	socket = Socket.new( AF_INET, SOCK_STREAM, 0 )
 * 	sockaddr = Socket.pack_sockaddr_in( 80, 'www.google.com' )
 * 	socket.connect( sockaddr )
 * 	socket.write( "GET / HTTP/1.0\r\n\r\n" )
 * 	results = socket.read
 *
 * === Unix-based Exceptions
 * On unix-based systems the following system exceptions may be raised if
 * the call to _connect_ fails:
 * * Errno::EACCES - search permission is denied for a component of the prefix
 *   path or write access to the +socket+ is denied
 * * Errno::EADDRINUSE - the _sockaddr_ is already in use
 * * Errno::EADDRNOTAVAIL - the specified _sockaddr_ is not available from the
 *   local machine
 * * Errno::EAFNOSUPPORT - the specified _sockaddr_ is not a valid address for
 *   the address family of the specified +socket+
 * * Errno::EALREADY - a connection is already in progress for the specified
 *   socket
 * * Errno::EBADF - the +socket+ is not a valid file descriptor
 * * Errno::ECONNREFUSED - the target _sockaddr_ was not listening for connections
 *   refused the connection request
 * * Errno::ECONNRESET - the remote host reset the connection request
 * * Errno::EFAULT - the _sockaddr_ cannot be accessed
 * * Errno::EHOSTUNREACH - the destination host cannot be reached (probably
 *   because the host is down or a remote router cannot reach it)
 * * Errno::EINPROGRESS - the O_NONBLOCK is set for the +socket+ and the
 *   connection cannot be immediately established; the connection will be
 *   established asynchronously
 * * Errno::EINTR - the attempt to establish the connection was interrupted by
 *   delivery of a signal that was caught; the connection will be established
 *   asynchronously
 * * Errno::EISCONN - the specified +socket+ is already connected
 * * Errno::EINVAL - the address length used for the _sockaddr_ is not a valid
 *   length for the address family or there is an invalid family in _sockaddr_
 * * Errno::ENAMETOOLONG - the pathname resolved had a length which exceeded
 *   PATH_MAX
 * * Errno::ENETDOWN - the local interface used to reach the destination is down
 * * Errno::ENETUNREACH - no route to the network is present
 * * Errno::ENOBUFS - no buffer space is available
 * * Errno::ENOSR - there were insufficient STREAMS resources available to
 *   complete the operation
 * * Errno::ENOTSOCK - the +socket+ argument does not refer to a socket
 * * Errno::EOPNOTSUPP - the calling +socket+ is listening and cannot be connected
 * * Errno::EPROTOTYPE - the _sockaddr_ has a different type than the socket
 *   bound to the specified peer address
 * * Errno::ETIMEDOUT - the attempt to connect time out before a connection
 *   was made.
 *
 * On unix-based systems if the address family of the calling +socket+ is
 * AF_UNIX the follow exceptions may be raised if the call to _connect_
 * fails:
 * * Errno::EIO - an i/o error occurred while reading from or writing to the
 *   file system
 * * Errno::ELOOP - too many symbolic links were encountered in translating
 *   the pathname in _sockaddr_
 * * Errno::ENAMETOOLLONG - a component of a pathname exceeded NAME_MAX
 *   characters, or an entire pathname exceeded PATH_MAX characters
 * * Errno::ENOENT - a component of the pathname does not name an existing file
 *   or the pathname is an empty string
 * * Errno::ENOTDIR - a component of the path prefix of the pathname in _sockaddr_
 *   is not a directory
 *
 * === Windows Exceptions
 * On Windows systems the following system exceptions may be raised if
 * the call to _connect_ fails:
 * * Errno::ENETDOWN - the network is down
 * * Errno::EADDRINUSE - the socket's local address is already in use
 * * Errno::EINTR - the socket was cancelled
 * * Errno::EINPROGRESS - a blocking socket is in progress or the service provider
 *   is still processing a callback function. Or a nonblocking connect call is
 *   in progress on the +socket+.
 * * Errno::EALREADY - see Errno::EINVAL
 * * Errno::EADDRNOTAVAIL - the remote address is not a valid address, such as
 *   ADDR_ANY TODO check ADDRANY TO INADDR_ANY
 * * Errno::EAFNOSUPPORT - addresses in the specified family cannot be used with
 *   with this +socket+
 * * Errno::ECONNREFUSED - the target _sockaddr_ was not listening for connections
 *   refused the connection request
 * * Errno::EFAULT - the socket's internal address or address length parameter
 *   is too small or is not a valid part of the user space address
 * * Errno::EINVAL - the +socket+ is a listening socket
 * * Errno::EISCONN - the +socket+ is already connected
 * * Errno::ENETUNREACH - the network cannot be reached from this host at this time
 * * Errno::EHOSTUNREACH - no route to the network is present
 * * Errno::ENOBUFS - no buffer space is available
 * * Errno::ENOTSOCK - the +socket+ argument does not refer to a socket
 * * Errno::ETIMEDOUT - the attempt to connect time out before a connection
 *   was made.
 * * Errno::EWOULDBLOCK - the socket is marked as nonblocking and the
 *   connection cannot be completed immediately
 * * Errno::EACCES - the attempt to connect the datagram socket to the
 *   broadcast address failed
 *
 * === See
 * * connect manual pages on unix-based systems
 * * connect function in Microsoft's Winsock functions reference
 */
static VALUE
sock_connect(VALUE sock, SEL sel, VALUE addr)
{
    rb_io_t *fptr;
    int fd, n;

    SockAddrStringValue(addr);
    addr = rb_str_new4(addr);
    GetOpenFile(sock, fptr);
    fd = fptr->fd;
    n = ruby_connect(fd, (struct sockaddr*)RSTRING_PTR(addr), RSTRING_LENINT(addr), 0);
    if (n < 0) {
	rb_sys_fail("connect(2)");
    }

    return INT2FIX(n);
}

/*
 * call-seq:
 * 	socket.connect_nonblock(remote_sockaddr) => 0
 *
 * Requests a connection to be made on the given +remote_sockaddr+ after
 * O_NONBLOCK is set for the underlying file descriptor.
 * Returns 0 if successful, otherwise an exception is raised.
 *
 * === Parameter
 * * +remote_sockaddr+ - the +struct+ sockaddr contained in a string or Addrinfo object
 *
 * === Example:
 * 	# Pull down Google's web page
 * 	require 'socket'
 * 	include Socket::Constants
 * 	socket = Socket.new(AF_INET, SOCK_STREAM, 0)
 * 	sockaddr = Socket.sockaddr_in(80, 'www.google.com')
 * 	begin # emulate blocking connect
 * 	  socket.connect_nonblock(sockaddr)
 * 	rescue IO::WaitWritable
 * 	  IO.select(nil, [socket]) # wait 3-way handshake completion
 * 	  begin
 * 	    socket.connect_nonblock(sockaddr) # check connection failure
 * 	  rescue Errno::EISCONN
 * 	  end
 * 	end
 * 	socket.write("GET / HTTP/1.0\r\n\r\n")
 * 	results = socket.read
 *
 * Refer to Socket#connect for the exceptions that may be thrown if the call
 * to _connect_nonblock_ fails.
 *
 * Socket#connect_nonblock may raise any error corresponding to connect(2) failure,
 * including Errno::EINPROGRESS.
 *
 * If the exception is Errno::EINPROGRESS,
 * it is extended by IO::WaitWritable.
 * So IO::WaitWritable can be used to rescue the exceptions for retrying connect_nonblock.
 *
 * === See
 * * Socket#connect
 */
static VALUE
sock_connect_nonblock(VALUE sock, SEL sel, VALUE addr)
{
    rb_io_t *fptr;
    int n;

    SockAddrStringValue(addr);
    addr = rb_str_new4(addr);
    GetOpenFile(sock, fptr);
    rb_io_set_nonblock(fptr);
    n = connect(fptr->fd, (struct sockaddr*)RSTRING_PTR(addr), RSTRING_LENINT(addr));
    if (n < 0) {
        if (errno == EINPROGRESS) {
            rb_mod_sys_fail(rb_mWaitWritable, "connect(2) would block");
	}
	rb_sys_fail("connect(2)");
    }

    return INT2FIX(n);
}

/*
 * call-seq:
 * 	socket.bind(local_sockaddr) => 0
 *
 * Binds to the given local address.
 *
 * === Parameter
 * * +local_sockaddr+ - the +struct+ sockaddr contained in a string or an Addrinfo object
 *
 * === Example
 * 	require 'socket'
 *
 * 	# use Addrinfo
 * 	socket = Socket.new(:INET, :STREAM, 0)
 * 	socket.bind(Addrinfo.tcp("127.0.0.1", 2222))
 * 	p socket.local_address #=> #<Addrinfo: 127.0.0.1:2222 TCP>
 *
 * 	# use struct sockaddr
 * 	include Socket::Constants
 * 	socket = Socket.new( AF_INET, SOCK_STREAM, 0 )
 * 	sockaddr = Socket.pack_sockaddr_in( 2200, 'localhost' )
 * 	socket.bind( sockaddr )
 *
 * === Unix-based Exceptions
 * On unix-based based systems the following system exceptions may be raised if
 * the call to _bind_ fails:
 * * Errno::EACCES - the specified _sockaddr_ is protected and the current
 *   user does not have permission to bind to it
 * * Errno::EADDRINUSE - the specified _sockaddr_ is already in use
 * * Errno::EADDRNOTAVAIL - the specified _sockaddr_ is not available from the
 *   local machine
 * * Errno::EAFNOSUPPORT - the specified _sockaddr_ is not a valid address for
 *   the family of the calling +socket+
 * * Errno::EBADF - the _sockaddr_ specified is not a valid file descriptor
 * * Errno::EFAULT - the _sockaddr_ argument cannot be accessed
 * * Errno::EINVAL - the +socket+ is already bound to an address, and the
 *   protocol does not support binding to the new _sockaddr_ or the +socket+
 *   has been shut down.
 * * Errno::EINVAL - the address length is not a valid length for the address
 *   family
 * * Errno::ENAMETOOLONG - the pathname resolved had a length which exceeded
 *   PATH_MAX
 * * Errno::ENOBUFS - no buffer space is available
 * * Errno::ENOSR - there were insufficient STREAMS resources available to
 *   complete the operation
 * * Errno::ENOTSOCK - the +socket+ does not refer to a socket
 * * Errno::EOPNOTSUPP - the socket type of the +socket+ does not support
 *   binding to an address
 *
 * On unix-based based systems if the address family of the calling +socket+ is
 * Socket::AF_UNIX the follow exceptions may be raised if the call to _bind_
 * fails:
 * * Errno::EACCES - search permission is denied for a component of the prefix
 *   path or write access to the +socket+ is denied
 * * Errno::EDESTADDRREQ - the _sockaddr_ argument is a null pointer
 * * Errno::EISDIR - same as Errno::EDESTADDRREQ
 * * Errno::EIO - an i/o error occurred
 * * Errno::ELOOP - too many symbolic links were encountered in translating
 *   the pathname in _sockaddr_
 * * Errno::ENAMETOOLLONG - a component of a pathname exceeded NAME_MAX
 *   characters, or an entire pathname exceeded PATH_MAX characters
 * * Errno::ENOENT - a component of the pathname does not name an existing file
 *   or the pathname is an empty string
 * * Errno::ENOTDIR - a component of the path prefix of the pathname in _sockaddr_
 *   is not a directory
 * * Errno::EROFS - the name would reside on a read only filesystem
 *
 * === Windows Exceptions
 * On Windows systems the following system exceptions may be raised if
 * the call to _bind_ fails:
 * * Errno::ENETDOWN-- the network is down
 * * Errno::EACCES - the attempt to connect the datagram socket to the
 *   broadcast address failed
 * * Errno::EADDRINUSE - the socket's local address is already in use
 * * Errno::EADDRNOTAVAIL - the specified address is not a valid address for this
 *   computer
 * * Errno::EFAULT - the socket's internal address or address length parameter
 *   is too small or is not a valid part of the user space addressed
 * * Errno::EINVAL - the +socket+ is already bound to an address
 * * Errno::ENOBUFS - no buffer space is available
 * * Errno::ENOTSOCK - the +socket+ argument does not refer to a socket
 *
 * === See
 * * bind manual pages on unix-based systems
 * * bind function in Microsoft's Winsock functions reference
 */
static VALUE
sock_bind(VALUE sock, SEL sel, VALUE addr)
{
    rb_io_t *fptr;

    SockAddrStringValue(addr);
    GetOpenFile(sock, fptr);
    if (bind(fptr->fd, (struct sockaddr*)RSTRING_PTR(addr), RSTRING_LENINT(addr)) < 0)
	rb_sys_fail("bind(2)");

    return INT2FIX(0);
}

/*
 * call-seq:
 * 	socket.listen( int ) => 0
 *
 * Listens for connections, using the specified +int+ as the backlog. A call
 * to _listen_ only applies if the +socket+ is of type SOCK_STREAM or
 * SOCK_SEQPACKET.
 *
 * === Parameter
 * * +backlog+ - the maximum length of the queue for pending connections.
 *
 * === Example 1
 * 	require 'socket'
 * 	include Socket::Constants
 * 	socket = Socket.new( AF_INET, SOCK_STREAM, 0 )
 * 	sockaddr = Socket.pack_sockaddr_in( 2200, 'localhost' )
 * 	socket.bind( sockaddr )
 * 	socket.listen( 5 )
 *
 * === Example 2 (listening on an arbitrary port, unix-based systems only):
 * 	require 'socket'
 * 	include Socket::Constants
 * 	socket = Socket.new( AF_INET, SOCK_STREAM, 0 )
 * 	socket.listen( 1 )
 *
 * === Unix-based Exceptions
 * On unix based systems the above will work because a new +sockaddr+ struct
 * is created on the address ADDR_ANY, for an arbitrary port number as handed
 * off by the kernel. It will not work on Windows, because Windows requires that
 * the +socket+ is bound by calling _bind_ before it can _listen_.
 *
 * If the _backlog_ amount exceeds the implementation-dependent maximum
 * queue length, the implementation's maximum queue length will be used.
 *
 * On unix-based based systems the following system exceptions may be raised if the
 * call to _listen_ fails:
 * * Errno::EBADF - the _socket_ argument is not a valid file descriptor
 * * Errno::EDESTADDRREQ - the _socket_ is not bound to a local address, and
 *   the protocol does not support listening on an unbound socket
 * * Errno::EINVAL - the _socket_ is already connected
 * * Errno::ENOTSOCK - the _socket_ argument does not refer to a socket
 * * Errno::EOPNOTSUPP - the _socket_ protocol does not support listen
 * * Errno::EACCES - the calling process does not have appropriate privileges
 * * Errno::EINVAL - the _socket_ has been shut down
 * * Errno::ENOBUFS - insufficient resources are available in the system to
 *   complete the call
 *
 * === Windows Exceptions
 * On Windows systems the following system exceptions may be raised if
 * the call to _listen_ fails:
 * * Errno::ENETDOWN - the network is down
 * * Errno::EADDRINUSE - the socket's local address is already in use. This
 *   usually occurs during the execution of _bind_ but could be delayed
 *   if the call to _bind_ was to a partially wildcard address (involving
 *   ADDR_ANY) and if a specific address needs to be committed at the
 *   time of the call to _listen_
 * * Errno::EINPROGRESS - a Windows Sockets 1.1 call is in progress or the
 *   service provider is still processing a callback function
 * * Errno::EINVAL - the +socket+ has not been bound with a call to _bind_.
 * * Errno::EISCONN - the +socket+ is already connected
 * * Errno::EMFILE - no more socket descriptors are available
 * * Errno::ENOBUFS - no buffer space is available
 * * Errno::ENOTSOC - +socket+ is not a socket
 * * Errno::EOPNOTSUPP - the referenced +socket+ is not a type that supports
 *   the _listen_ method
 *
 * === See
 * * listen manual pages on unix-based systems
 * * listen function in Microsoft's Winsock functions reference
 */
static VALUE
sock_listen(VALUE sock, SEL sel, VALUE log)
{
    rb_io_t *fptr;
    int backlog;

    rb_secure(4);
    backlog = NUM2INT(log);
    GetOpenFile(sock, fptr);
    if (listen(fptr->fd, backlog) < 0)
	rb_sys_fail("listen(2)");

    return INT2FIX(0);
}

/*
 * call-seq:
 * 	socket.recvfrom(maxlen) => [mesg, sender_addrinfo]
 * 	socket.recvfrom(maxlen, flags) => [mesg, sender_addrinfo]
 *
 * Receives up to _maxlen_ bytes from +socket+. _flags_ is zero or more
 * of the +MSG_+ options. The first element of the results, _mesg_, is the data
 * received. The second element, _sender_addrinfo_, contains protocol-specific
 * address information of the sender.
 *
 * === Parameters
 * * +maxlen+ - the maximum number of bytes to receive from the socket
 * * +flags+ - zero or more of the +MSG_+ options
 *
 * === Example
 * 	# In one file, start this first
 * 	require 'socket'
 * 	include Socket::Constants
 * 	socket = Socket.new( AF_INET, SOCK_STREAM, 0 )
 * 	sockaddr = Socket.pack_sockaddr_in( 2200, 'localhost' )
 * 	socket.bind( sockaddr )
 * 	socket.listen( 5 )
 * 	client, client_addrinfo = socket.accept
 * 	data = client.recvfrom( 20 )[0].chomp
 * 	puts "I only received 20 bytes '#{data}'"
 * 	sleep 1
 * 	socket.close
 *
 * 	# In another file, start this second
 * 	require 'socket'
 * 	include Socket::Constants
 * 	socket = Socket.new( AF_INET, SOCK_STREAM, 0 )
 * 	sockaddr = Socket.pack_sockaddr_in( 2200, 'localhost' )
 * 	socket.connect( sockaddr )
 * 	socket.puts "Watch this get cut short!"
 * 	socket.close
 *
 * === Unix-based Exceptions
 * On unix-based based systems the following system exceptions may be raised if the
 * call to _recvfrom_ fails:
 * * Errno::EAGAIN - the +socket+ file descriptor is marked as O_NONBLOCK and no
 *   data is waiting to be received; or MSG_OOB is set and no out-of-band data
 *   is available and either the +socket+ file descriptor is marked as
 *   O_NONBLOCK or the +socket+ does not support blocking to wait for
 *   out-of-band-data
 * * Errno::EWOULDBLOCK - see Errno::EAGAIN
 * * Errno::EBADF - the +socket+ is not a valid file descriptor
 * * Errno::ECONNRESET - a connection was forcibly closed by a peer
 * * Errno::EFAULT - the socket's internal buffer, address or address length
 *   cannot be accessed or written
 * * Errno::EINTR - a signal interrupted _recvfrom_ before any data was available
 * * Errno::EINVAL - the MSG_OOB flag is set and no out-of-band data is available
 * * Errno::EIO - an i/o error occurred while reading from or writing to the
 *   filesystem
 * * Errno::ENOBUFS - insufficient resources were available in the system to
 *   perform the operation
 * * Errno::ENOMEM - insufficient memory was available to fulfill the request
 * * Errno::ENOSR - there were insufficient STREAMS resources available to
 *   complete the operation
 * * Errno::ENOTCONN - a receive is attempted on a connection-mode socket that
 *   is not connected
 * * Errno::ENOTSOCK - the +socket+ does not refer to a socket
 * * Errno::EOPNOTSUPP - the specified flags are not supported for this socket type
 * * Errno::ETIMEDOUT - the connection timed out during connection establishment
 *   or due to a transmission timeout on an active connection
 *
 * === Windows Exceptions
 * On Windows systems the following system exceptions may be raised if
 * the call to _recvfrom_ fails:
 * * Errno::ENETDOWN - the network is down
 * * Errno::EFAULT - the internal buffer and from parameters on +socket+ are not
 *   part of the user address space, or the internal fromlen parameter is
 *   too small to accommodate the peer address
 * * Errno::EINTR - the (blocking) call was cancelled by an internal call to
 *   the WinSock function WSACancelBlockingCall
 * * Errno::EINPROGRESS - a blocking Windows Sockets 1.1 call is in progress or
 *   the service provider is still processing a callback function
 * * Errno::EINVAL - +socket+ has not been bound with a call to _bind_, or an
 *   unknown flag was specified, or MSG_OOB was specified for a socket with
 *   SO_OOBINLINE enabled, or (for byte stream-style sockets only) the internal
 *   len parameter on +socket+ was zero or negative
 * * Errno::EISCONN - +socket+ is already connected. The call to _recvfrom_ is
 *   not permitted with a connected socket on a socket that is connection
 *   oriented or connectionless.
 * * Errno::ENETRESET - the connection has been broken due to the keep-alive
 *   activity detecting a failure while the operation was in progress.
 * * Errno::EOPNOTSUPP - MSG_OOB was specified, but +socket+ is not stream-style
 *   such as type SOCK_STREAM. OOB data is not supported in the communication
 *   domain associated with +socket+, or +socket+ is unidirectional and
 *   supports only send operations
 * * Errno::ESHUTDOWN - +socket+ has been shutdown. It is not possible to
 *   call _recvfrom_ on a socket after _shutdown_ has been invoked.
 * * Errno::EWOULDBLOCK - +socket+ is marked as nonblocking and a  call to
 *   _recvfrom_ would block.
 * * Errno::EMSGSIZE - the message was too large to fit into the specified buffer
 *   and was truncated.
 * * Errno::ETIMEDOUT - the connection has been dropped, because of a network
 *   failure or because the system on the other end went down without
 *   notice
 * * Errno::ECONNRESET - the virtual circuit was reset by the remote side
 *   executing a hard or abortive close. The application should close the
 *   socket; it is no longer usable. On a UDP-datagram socket this error
 *   indicates a previous send operation resulted in an ICMP Port Unreachable
 *   message.
 */
static VALUE
sock_recvfrom(VALUE sock, SEL sel, int argc, VALUE *argv)
{
    return s_recvfrom(sock, argc, argv, RECV_SOCKET);
}

/*
 * call-seq:
 * 	socket.recvfrom_nonblock(maxlen) => [mesg, sender_addrinfo]
 * 	socket.recvfrom_nonblock(maxlen, flags) => [mesg, sender_addrinfo]
 *
 * Receives up to _maxlen_ bytes from +socket+ using recvfrom(2) after
 * O_NONBLOCK is set for the underlying file descriptor.
 * _flags_ is zero or more of the +MSG_+ options.
 * The first element of the results, _mesg_, is the data received.
 * The second element, _sender_addrinfo_, contains protocol-specific address
 * information of the sender.
 *
 * When recvfrom(2) returns 0, Socket#recvfrom_nonblock returns
 * an empty string as data.
 * The meaning depends on the socket: EOF on TCP, empty packet on UDP, etc.
 *
 * === Parameters
 * * +maxlen+ - the maximum number of bytes to receive from the socket
 * * +flags+ - zero or more of the +MSG_+ options
 *
 * === Example
 * 	# In one file, start this first
 * 	require 'socket'
 * 	include Socket::Constants
 * 	socket = Socket.new(AF_INET, SOCK_STREAM, 0)
 * 	sockaddr = Socket.sockaddr_in(2200, 'localhost')
 * 	socket.bind(sockaddr)
 * 	socket.listen(5)
 * 	client, client_addrinfo = socket.accept
 * 	begin # emulate blocking recvfrom
 * 	  pair = client.recvfrom_nonblock(20)
 * 	rescue IO::WaitReadable
 * 	  IO.select([client])
 * 	  retry
 * 	end
 * 	data = pair[0].chomp
 * 	puts "I only received 20 bytes '#{data}'"
 * 	sleep 1
 * 	socket.close
 *
 * 	# In another file, start this second
 * 	require 'socket'
 * 	include Socket::Constants
 * 	socket = Socket.new(AF_INET, SOCK_STREAM, 0)
 * 	sockaddr = Socket.sockaddr_in(2200, 'localhost')
 * 	socket.connect(sockaddr)
 * 	socket.puts "Watch this get cut short!"
 * 	socket.close
 *
 * Refer to Socket#recvfrom for the exceptions that may be thrown if the call
 * to _recvfrom_nonblock_ fails.
 *
 * Socket#recvfrom_nonblock may raise any error corresponding to recvfrom(2) failure,
 * including Errno::EWOULDBLOCK.
 *
 * If the exception is Errno::EWOULDBLOCK or Errno::AGAIN,
 * it is extended by IO::WaitReadable.
 * So IO::WaitReadable can be used to rescue the exceptions for retrying recvfrom_nonblock.
 *
 * === See
 * * Socket#recvfrom
 */
static VALUE
sock_recvfrom_nonblock(VALUE sock, SEL sel, int argc, VALUE *argv)
{
    return s_recvfrom_nonblock(sock, argc, argv, RECV_SOCKET);
}

/*
 * call-seq:
 *   socket.accept => [client_socket, client_addrinfo]
 *
 * Accepts a next connection.
 * Returns a new Socket object and Addrinfo object.
 *
 *   serv = Socket.new(:INET, :STREAM, 0)
 *   serv.listen(5)
 *   c = Socket.new(:INET, :STREAM, 0)
 *   c.connect(serv.connect_address)
 *   p serv.accept #=> [#<Socket:fd 6>, #<Addrinfo: 127.0.0.1:48555 TCP>]
 *
 */
static VALUE
sock_accept(VALUE sock, SEL sel)
{
    rb_io_t *fptr;
    VALUE sock2;
    struct sockaddr_storage buf;
    socklen_t len = (socklen_t)sizeof buf;

    GetOpenFile(sock, fptr);
    sock2 = s_accept(rb_cSocket,fptr->fd,(struct sockaddr*)&buf,&len);

    return rb_assoc_new(sock2, io_socket_addrinfo(sock2, (struct sockaddr*)&buf, len));
}

/*
 * call-seq:
 * 	socket.accept_nonblock => [client_socket, client_addrinfo]
 *
 * Accepts an incoming connection using accept(2) after
 * O_NONBLOCK is set for the underlying file descriptor.
 * It returns an array containing the accepted socket
 * for the incoming connection, _client_socket_,
 * and an Addrinfo, _client_addrinfo_.
 *
 * === Example
 * 	# In one script, start this first
 * 	require 'socket'
 * 	include Socket::Constants
 * 	socket = Socket.new(AF_INET, SOCK_STREAM, 0)
 * 	sockaddr = Socket.sockaddr_in(2200, 'localhost')
 * 	socket.bind(sockaddr)
 * 	socket.listen(5)
 * 	begin # emulate blocking accept
 * 	  client_socket, client_addrinfo = socket.accept_nonblock
 * 	rescue IO::WaitReadable, Errno::EINTR
 * 	  IO.select([socket])
 * 	  retry
 * 	end
 * 	puts "The client said, '#{client_socket.readline.chomp}'"
 * 	client_socket.puts "Hello from script one!"
 * 	socket.close
 *
 * 	# In another script, start this second
 * 	require 'socket'
 * 	include Socket::Constants
 * 	socket = Socket.new(AF_INET, SOCK_STREAM, 0)
 * 	sockaddr = Socket.sockaddr_in(2200, 'localhost')
 * 	socket.connect(sockaddr)
 * 	socket.puts "Hello from script 2."
 * 	puts "The server said, '#{socket.readline.chomp}'"
 * 	socket.close
 *
 * Refer to Socket#accept for the exceptions that may be thrown if the call
 * to _accept_nonblock_ fails.
 *
 * Socket#accept_nonblock may raise any error corresponding to accept(2) failure,
 * including Errno::EWOULDBLOCK.
 *
 * If the exception is Errno::EWOULDBLOCK, Errno::AGAIN, Errno::ECONNABORTED or Errno::EPROTO,
 * it is extended by IO::WaitReadable.
 * So IO::WaitReadable can be used to rescue the exceptions for retrying accept_nonblock.
 *
 * === See
 * * Socket#accept
 */
static VALUE
sock_accept_nonblock(VALUE sock, SEL sel)
{
    rb_io_t *fptr;
    VALUE sock2;
    struct sockaddr_storage buf;
    socklen_t len = (socklen_t)sizeof buf;

    GetOpenFile(sock, fptr);
    sock2 = s_accept_nonblock(rb_cSocket, fptr, (struct sockaddr *)&buf, &len);
    return rb_assoc_new(sock2, io_socket_addrinfo(sock2, (struct sockaddr*)&buf, len));
}

/*
 * call-seq:
 * 	socket.sysaccept => [client_socket_fd, client_addrinfo]
 *
 * Accepts an incoming connection returning an array containing the (integer)
 * file descriptor for the incoming connection, _client_socket_fd_,
 * and an Addrinfo, _client_addrinfo_.
 *
 * === Example
 * 	# In one script, start this first
 * 	require 'socket'
 * 	include Socket::Constants
 * 	socket = Socket.new( AF_INET, SOCK_STREAM, 0 )
 * 	sockaddr = Socket.pack_sockaddr_in( 2200, 'localhost' )
 * 	socket.bind( sockaddr )
 * 	socket.listen( 5 )
 * 	client_fd, client_addrinfo = socket.sysaccept
 * 	client_socket = Socket.for_fd( client_fd )
 * 	puts "The client said, '#{client_socket.readline.chomp}'"
 * 	client_socket.puts "Hello from script one!"
 * 	socket.close
 *
 * 	# In another script, start this second
 * 	require 'socket'
 * 	include Socket::Constants
 * 	socket = Socket.new( AF_INET, SOCK_STREAM, 0 )
 * 	sockaddr = Socket.pack_sockaddr_in( 2200, 'localhost' )
 * 	socket.connect( sockaddr )
 * 	socket.puts "Hello from script 2."
 * 	puts "The server said, '#{socket.readline.chomp}'"
 * 	socket.close
 *
 * Refer to Socket#accept for the exceptions that may be thrown if the call
 * to _sysaccept_ fails.
 *
 * === See
 * * Socket#accept
 */
static VALUE
sock_sysaccept(VALUE sock, SEL sel)
{
    rb_io_t *fptr;
    VALUE sock2;
    struct sockaddr_storage buf;
    socklen_t len = (socklen_t)sizeof buf;

    GetOpenFile(sock, fptr);
    sock2 = s_accept(0,fptr->fd,(struct sockaddr*)&buf,&len);

    return rb_assoc_new(sock2, io_socket_addrinfo(sock2, (struct sockaddr*)&buf, len));
}

static inline VALUE
rb_io_check_io(VALUE io)
{
    return rb_check_convert_type(io, T_FILE, "IO", "to_io");
}

VALUE rb_f_open(VALUE io, SEL sel, int argc, VALUE *argv);

/*
 *  call-seq:
 *     socket.sendfile(dest, offset, len)     => integer
 *
 *  Uses the sendfile(2) system call to send the file specified by <code>dest</code>
 *  to <code>socket</code>. <code>dest</code> must either be a readable IO object or
 *  a String representing a path on the file system. The <code>offset</code>
 *  and <code>len</code> parameters determine the offset and length of the sent file.
 *  Returns the number of bytes set. May throw a SystemCallError if the underlying call fails.
 */

static VALUE
socket_sendfile(VALUE self, SEL sel, VALUE file, VALUE offset, VALUE len)
{
    bool needs_to_close = false;
    off_t to_offset = NUM2OFFT(offset);
    off_t to_write  = NUM2OFFT(len);
    rb_io_t *socket;

    if (to_offset < 0 || to_write < 0) {
	rb_raise(rb_eArgError, "negative argument");
    }

    GetOpenFile(self, socket);
    VALUE io = rb_io_check_io(file);
    if (NIL_P(io)) {
        io = rb_f_open(rb_cIO, 0, 1, &file);
        needs_to_close = true;
    }

    rb_io_t *source = ExtractIOStruct(io);
    rb_io_check_closed(source);

    if (sendfile(source->fd, socket->fd, to_offset, &to_write, NULL, 0) == -1) {
        if (needs_to_close) {
            rb_io_close(io);
        }
        rb_sys_fail("sendfile(2) failed.");
    }

    if (needs_to_close) {
        rb_io_close(io);
    }

    return OFFT2NUM(to_write);
}

#ifdef HAVE_GETHOSTNAME
/*
 * call-seq:
 *   Socket.gethostname => hostname
 *
 * Returns the hostname.
 *
 *   p Socket.gethostname #=> "hal"
 *
 * Note that it is not guaranteed to be able to convert to IP address using gethostbyname, getaddrinfo, etc.
 * If you need local IP address, use Socket.ip_address_list.
 */
static VALUE
sock_gethostname(VALUE obj, SEL sel)
{
#ifndef HOST_NAME_MAX
#  define HOST_NAME_MAX 1024
#endif
    char buf[HOST_NAME_MAX+1];

    rb_secure(3);
    if (gethostname(buf, (int)sizeof buf - 1) < 0)
	rb_sys_fail("gethostname");

    buf[sizeof buf - 1] = '\0';
    return rb_str_new2(buf);
}
#else
#ifdef HAVE_UNAME

#include <sys/utsname.h>

static VALUE
sock_gethostname(VALUE obj, SEL sel)
{
    struct utsname un;

    rb_secure(3);
    uname(&un);
    return rb_str_new2(un.nodename);
}
#else
static VALUE
sock_gethostname(VALUE obj, SEL sel)
{
    rb_notimplement();
}
#endif
#endif

static VALUE
make_addrinfo(struct addrinfo *res0, int norevlookup)
{
    VALUE base, ary;
    struct addrinfo *res;

    if (res0 == NULL) {
	rb_raise(rb_eSocket, "host not found");
    }
    base = rb_ary_new();
    for (res = res0; res; res = res->ai_next) {
	ary = ipaddr(res->ai_addr, norevlookup);
	if (res->ai_canonname) {
	    rb_ary_store(ary, 2, rb_str_new2(res->ai_canonname));
	}
	rb_ary_push(ary, INT2FIX(res->ai_family));
	rb_ary_push(ary, INT2FIX(res->ai_socktype));
	rb_ary_push(ary, INT2FIX(res->ai_protocol));
	rb_ary_push(base, ary);
    }
    return base;
}

static VALUE
sock_sockaddr(struct sockaddr *addr, size_t len)
{
    char *ptr;

    switch (addr->sa_family) {
      case AF_INET:
	ptr = (char*)&((struct sockaddr_in*)addr)->sin_addr.s_addr;
	len = sizeof(((struct sockaddr_in*)addr)->sin_addr.s_addr);
	break;
#ifdef INET6
      case AF_INET6:
	ptr = (char*)&((struct sockaddr_in6*)addr)->sin6_addr.s6_addr;
	len = sizeof(((struct sockaddr_in6*)addr)->sin6_addr.s6_addr);
	break;
#endif
      default:
        rb_raise(rb_eSocket, "unknown socket family:%d", addr->sa_family);
	break;
    }
    return rb_str_new(ptr, len);
}

/*
 * call-seq:
 *   Socket.gethostbyname(hostname) => [official_hostname, alias_hostnames, address_family, *address_list]
 *
 * Obtains the host information for _hostname_.
 *
 *   p Socket.gethostbyname("hal") #=> ["localhost", ["hal"], 2, "\x7F\x00\x00\x01"]
 *
 */
static VALUE
sock_s_gethostbyname(VALUE obj, SEL sel, VALUE host)
{
    rb_secure(3);
    return make_hostent(host, sock_addrinfo(host, Qnil, SOCK_STREAM, AI_CANONNAME), sock_sockaddr);
}

/*
 * call-seq:
 *   Socket.gethostbyaddr(address_string [, address_family]) => hostent
 *
 * Obtains the host information for _address_.
 *
 *   p Socket.gethostbyaddr([221,186,184,68].pack("CCCC"))
 *   #=> ["carbon.ruby-lang.org", [], 2, "\xDD\xBA\xB8D"]
 */
static VALUE
sock_s_gethostbyaddr(VALUE self, SEL sel, int argc, VALUE *argv)
{
    VALUE addr, family;
    struct hostent *h;
    struct sockaddr *sa;
    char **pch;
    VALUE ary, names;
    int t = AF_INET;

    rb_scan_args(argc, argv, "11", &addr, &family);
    sa = (struct sockaddr*)StringValuePtr(addr);
    if (!NIL_P(family)) {
	t = family_arg(family);
    }
#ifdef INET6
    else if (RSTRING_LEN(addr) == 16) {
	t = AF_INET6;
    }
#endif
    h = gethostbyaddr(RSTRING_PTR(addr), RSTRING_LENINT(addr), t);
    if (h == NULL) {
#ifdef HAVE_HSTRERROR
	extern int h_errno;
	rb_raise(rb_eSocket, "%s", (char*)hstrerror(h_errno));
#else
	rb_raise(rb_eSocket, "host not found");
#endif
    }
    ary = rb_ary_new();
    rb_ary_push(ary, rb_str_new2(h->h_name));
    names = rb_ary_new();
    rb_ary_push(ary, names);
    if (h->h_aliases != NULL) {
	for (pch = h->h_aliases; *pch; pch++) {
	    rb_ary_push(names, rb_str_new2(*pch));
	}
    }
    rb_ary_push(ary, INT2NUM(h->h_addrtype));
#ifdef h_addr
    for (pch = h->h_addr_list; *pch; pch++) {
	rb_ary_push(ary, rb_str_new(*pch, h->h_length));
    }
#else
    rb_ary_push(ary, rb_str_new(h->h_addr, h->h_length));
#endif

    return ary;
}

/*
 * call-seq:
 *   Socket.getservbyname(service_name)                => port_number
 *   Socket.getservbyname(service_name, protocol_name) => port_number
 *
 * Obtains the port number for _service_name_.
 *
 * If _protocol_name_ is not given, "tcp" is assumed.
 *
 *   Socket.getservbyname("smtp")          #=> 25
 *   Socket.getservbyname("shell")         #=> 514
 *   Socket.getservbyname("syslog", "udp") #=> 514
 */
static VALUE
sock_s_getservbyname(VALUE self, SEL sel, int argc, VALUE *argv)
{
    VALUE service, proto;
    struct servent *sp;
    int port;

    rb_scan_args(argc, argv, "11", &service, &proto);
    if (NIL_P(proto)) proto = rb_str_new2("tcp");
    StringValue(service);
    StringValue(proto);

    sp = getservbyname(StringValueCStr(service),  StringValueCStr(proto));
    if (sp) {
	port = ntohs(sp->s_port);
    }
    else {
	const char *s = RSTRING_PTR(service);
	char *end;

	port = STRTOUL(s, &end, 0);
	if (*end != '\0') {
	    rb_raise(rb_eSocket, "no such service %s/%s", s, RSTRING_PTR(proto));
	}
    }
    return INT2FIX(port);
}

/*
 * call-seq:
 *   Socket.getservbyport(port [, protocol_name]) => service
 *
 * Obtains the port number for _port_.
 *
 * If _protocol_name_ is not given, "tcp" is assumed.
 *
 *   Socket.getservbyport(80)         #=> "www"
 *   Socket.getservbyport(514, "tcp") #=> "shell"
 *   Socket.getservbyport(514, "udp") #=> "syslog"
 *
 */
static VALUE
sock_s_getservbyport(VALUE self, SEL sel, int argc, VALUE *argv)
{
    VALUE port, proto;
    struct servent *sp;
    long portnum;

    rb_scan_args(argc, argv, "11", &port, &proto);
    portnum = NUM2LONG(port);
    if (portnum != (uint16_t)portnum) {
	const char *s = portnum > 0 ? "big" : "small";
	rb_raise(rb_eRangeError, "integer %ld too %s to convert into `int16_t'", portnum, s);
    }
    if (NIL_P(proto)) proto = rb_str_new2("tcp");
    StringValue(proto);

    sp = getservbyport((int)htons((uint16_t)portnum),  StringValueCStr(proto));
    if (!sp) {
	rb_raise(rb_eSocket, "no such service for port %ld/%s", NUM2LONG(port),
		RSTRING_PTR(proto));
    }
    return rb_tainted_str_new2(sp->s_name);
}

/*
 * call-seq:
 *   Socket.getaddrinfo(nodename, servname[, family[, socktype[, protocol[, flags[, reverse_lookup]]]]]) => array
 *
 * Obtains address information for _nodename_:_servname_.
 *
 * _family_ should be an address family such as: :INET, :INET6, :UNIX, etc.
 *
 * _socktype_ should be a socket type such as: :STREAM, :DGRAM, :RAW, etc.
 *
 * _protocol_ should be a protocol defined in the family.
 * 0 is default protocol for the family.
 *
 * _flags_ should be bitwise OR of Socket::AI_* constants.
 *
 *   Socket.getaddrinfo("www.ruby-lang.org", "http", nil, :STREAM)
 *   #=> [["AF_INET", 80, "carbon.ruby-lang.org", "221.186.184.68", 2, 1, 6]] # PF_INET/SOCK_STREAM/IPPROTO_TCP
 *
 *   Socket.getaddrinfo("localhost", nil)
 *   #=> [["AF_INET", 0, "localhost", "127.0.0.1", 2, 1, 6],  # PF_INET/SOCK_STREAM/IPPROTO_TCP
 *   #    ["AF_INET", 0, "localhost", "127.0.0.1", 2, 2, 17], # PF_INET/SOCK_DGRAM/IPPROTO_UDP
 *   #    ["AF_INET", 0, "localhost", "127.0.0.1", 2, 3, 0]]  # PF_INET/SOCK_RAW/IPPROTO_IP
 *
 * _reverse_lookup_ directs the form of the third element, and has to
 * be one of below.
 * If it is ommitted, the default value is +nil+.
 *
 *   +true+, +:hostname+:  hostname is obtained from numeric address using reverse lookup, which may take a time.
 *   +false+, +:numeric+:  hostname is same as numeric address.
 *   +nil+:              obey to the current +do_not_reverse_lookup+ flag.
 *
 * If Addrinfo object is preferred, use Addrinfo.getaddrinfo.
 */
static VALUE
sock_s_getaddrinfo(VALUE self, SEL sel, int argc, VALUE *argv)
{
    VALUE host, port, family, socktype, protocol, flags, ret, revlookup;
    char hbuf[1024], pbuf[1024];
    char *hptr, *pptr;
    struct addrinfo hints, *res;
    int error;
    int norevlookup;

    rb_scan_args(argc, argv, "25", &host, &port, &family, &socktype, &protocol, &flags, &revlookup);
    if (NIL_P(host)) {
	hptr = NULL;
    }
    else {
	strncpy(hbuf, StringValuePtr(host), sizeof(hbuf));
	hbuf[sizeof(hbuf) - 1] = '\0';
	hptr = hbuf;
    }
    if (NIL_P(port)) {
	pptr = NULL;
    }
    else if (FIXNUM_P(port)) {
	snprintf(pbuf, sizeof(pbuf), "%ld", FIX2LONG(port));
	pptr = pbuf;
    }
    else {
	strncpy(pbuf, StringValuePtr(port), sizeof(pbuf));
	pbuf[sizeof(pbuf) - 1] = '\0';
	pptr = pbuf;
    }

    MEMZERO(&hints, struct addrinfo, 1);
    hints.ai_family = NIL_P(family) ? PF_UNSPEC : family_arg(family);

    if (!NIL_P(socktype)) {
	hints.ai_socktype = socktype_arg(socktype);
    }
    if (!NIL_P(protocol)) {
	hints.ai_protocol = NUM2INT(protocol);
    }
    if (!NIL_P(flags)) {
	hints.ai_flags = NUM2INT(flags);
    }
    if (NIL_P(revlookup) || !revlookup_flag(revlookup, &norevlookup)) {
	norevlookup = do_not_reverse_lookup;
    }
    error = getaddrinfo(hptr, pptr, &hints, &res);
    if (error) {
	raise_socket_error("getaddrinfo", error);
    }

    ret = make_addrinfo(res, norevlookup);
    freeaddrinfo(res);
    return ret;
}

/*
 * call-seq:
 *   Socket.getnameinfo(sockaddr [, flags]) => [hostname, servicename]
 *
 * Obtains name information for _sockaddr_.
 *
 * _sockaddr_ should be one of follows.
 * - packed sockaddr string such as Socket.sockaddr_in(80, "127.0.0.1")
 * - 3-elements array such as ["AF_INET", 80, "127.0.0.1"]
 * - 4-elements array such as ["AF_INET", 80, ignored, "127.0.0.1"]
 *
 * _flags_ should be bitwise OR of Socket::NI_* constants.
 *
 * Note that the last form is compatible with IPSocket#{addr,peeraddr}.
 *
 *   Socket.getnameinfo(Socket.sockaddr_in(80, "127.0.0.1"))       #=> ["localhost", "www"]
 *   Socket.getnameinfo(["AF_INET", 80, "127.0.0.1"])              #=> ["localhost", "www"]
 *   Socket.getnameinfo(["AF_INET", 80, "localhost", "127.0.0.1"]) #=> ["localhost", "www"]
 *
 * If Addrinfo object is preferred, use Addrinfo#getnameinfo.
 */
static VALUE
sock_s_getnameinfo(VALUE self, SEL sel, int argc, VALUE *argv)
{
    VALUE sa, af = Qnil, host = Qnil, port = Qnil, flags, tmp;
    char *hptr, *pptr;
    char hbuf[1024], pbuf[1024];
    int fl;
    struct addrinfo hints, *res = NULL, *r;
    int error;
    struct sockaddr_storage ss;
    struct sockaddr *sap;

    sa = flags = Qnil;
    rb_scan_args(argc, argv, "11", &sa, &flags);

    fl = 0;
    if (!NIL_P(flags)) {
	fl = NUM2INT(flags);
    }
    tmp = rb_check_string_type(sa);
    if (!NIL_P(tmp)) {
	sa = tmp;
	if (sizeof(ss) < (size_t)RSTRING_LEN(sa)) {
	    rb_raise(rb_eTypeError, "sockaddr length too big");
	}
	memcpy(&ss, RSTRING_PTR(sa), RSTRING_LEN(sa));
	if ((size_t)RSTRING_LEN(sa) != SS_LEN(&ss)) {
	    rb_raise(rb_eTypeError, "sockaddr size differs - should not happen");
	}
	sap = (struct sockaddr*)&ss;
	goto call_nameinfo;
    }
    tmp = rb_check_array_type(sa);
    if (!NIL_P(tmp)) {
	sa = tmp;
	MEMZERO(&hints, struct addrinfo, 1);
	if (RARRAY_LEN(sa) == 3) {
	    af = RARRAY_AT(sa, 0);
	    port = RARRAY_AT(sa, 1);
	    host = RARRAY_AT(sa, 2);
	}
	else if (RARRAY_LEN(sa) >= 4) {
	    af = RARRAY_AT(sa, 0);
	    port = RARRAY_AT(sa, 1);
	    host = RARRAY_AT(sa, 3);
	    if (NIL_P(host)) {
		host = RARRAY_AT(sa, 2);
	    }
	    else {
		/*
		 * 4th element holds numeric form, don't resolve.
		 * see ipaddr().
		 */
#ifdef AI_NUMERICHOST /* AIX 4.3.3 doesn't have AI_NUMERICHOST. */
		hints.ai_flags |= AI_NUMERICHOST;
#endif
	    }
	}
	else {
	    rb_raise(rb_eArgError, "array size should be 3 or 4, %ld given",
		     RARRAY_LEN(sa));
	}
	/* host */
	if (NIL_P(host)) {
	    hptr = NULL;
	}
	else {
	    strncpy(hbuf, StringValuePtr(host), sizeof(hbuf));
	    hbuf[sizeof(hbuf) - 1] = '\0';
	    hptr = hbuf;
	}
	/* port */
	if (NIL_P(port)) {
	    strcpy(pbuf, "0");
	    pptr = NULL;
	}
	else if (FIXNUM_P(port)) {
	    snprintf(pbuf, sizeof(pbuf), "%ld", NUM2LONG(port));
	    pptr = pbuf;
	}
	else {
	    strncpy(pbuf, StringValuePtr(port), sizeof(pbuf));
	    pbuf[sizeof(pbuf) - 1] = '\0';
	    pptr = pbuf;
	}
	hints.ai_socktype = (fl & NI_DGRAM) ? SOCK_DGRAM : SOCK_STREAM;
	/* af */
	hints.ai_family = NIL_P(af) ? PF_UNSPEC : family_arg(af);
	error = getaddrinfo(hptr, pptr, &hints, &res);
	if (error) goto error_exit_addr;
	sap = res->ai_addr;
    }
    else {
	rb_raise(rb_eTypeError, "expecting String or Array");
    }

  call_nameinfo:
    error = getnameinfo(sap, SA_LEN(sap), hbuf, sizeof(hbuf),
			pbuf, sizeof(pbuf), fl);
    if (error) goto error_exit_name;
    if (res) {
	for (r = res->ai_next; r; r = r->ai_next) {
	    char hbuf2[1024], pbuf2[1024];

	    sap = r->ai_addr;
	    error = getnameinfo(sap, SA_LEN(sap), hbuf2, sizeof(hbuf2),
				pbuf2, sizeof(pbuf2), fl);
	    if (error) goto error_exit_name;
	    if (strcmp(hbuf, hbuf2) != 0|| strcmp(pbuf, pbuf2) != 0) {
		freeaddrinfo(res);
		rb_raise(rb_eSocket, "sockaddr resolved to multiple nodename");
	    }
	}
	freeaddrinfo(res);
    }
    return rb_assoc_new(rb_str_new2(hbuf), rb_str_new2(pbuf));

  error_exit_addr:
    if (res) freeaddrinfo(res);
    raise_socket_error("getaddrinfo", error);

  error_exit_name:
    if (res) freeaddrinfo(res);
    raise_socket_error("getnameinfo", error);
}

/*
 * call-seq:
 *   Socket.sockaddr_in(port, host)      => sockaddr
 *   Socket.pack_sockaddr_in(port, host) => sockaddr
 *
 * Packs _port_ and _host_ as an AF_INET/AF_INET6 sockaddr string.
 *
 *   Socket.sockaddr_in(80, "127.0.0.1")
 *   #=> "\x02\x00\x00P\x7F\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00"
 *
 *   Socket.sockaddr_in(80, "::1")
 *   #=> "\n\x00\x00P\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00"
 *
 */
static VALUE
sock_s_pack_sockaddr_in(VALUE self, SEL sel, VALUE port, VALUE host)
{
    struct addrinfo *res = sock_addrinfo(host, port, 0, 0);
    VALUE addr = rb_str_new((char*)res->ai_addr, res->ai_addrlen);

    freeaddrinfo(res);
    OBJ_INFECT(addr, port);
    OBJ_INFECT(addr, host);

    return addr;
}

/*
 * call-seq:
 *   Socket.unpack_sockaddr_in(sockaddr) => [port, ip_address]
 *
 * Unpacks _sockaddr_ into port and ip_address.
 *
 * _sockaddr_ should be a string or an addrinfo for AF_INET/AF_INET6.
 *
 *   sockaddr = Socket.sockaddr_in(80, "127.0.0.1")
 *   p sockaddr #=> "\x02\x00\x00P\x7F\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00"
 *   p Socket.unpack_sockaddr_in(sockaddr) #=> [80, "127.0.0.1"]
 *
 */
static VALUE
sock_s_unpack_sockaddr_in(VALUE self, SEL sel, VALUE addr)
{
    struct sockaddr_in * sockaddr;
    VALUE host;

    sockaddr = (struct sockaddr_in*)SockAddrStringValuePtr(addr);
    if (RSTRING_LEN(addr) <
        (char*)&((struct sockaddr *)sockaddr)->sa_family +
        sizeof(((struct sockaddr *)sockaddr)->sa_family) -
        (char*)sockaddr)
        rb_raise(rb_eArgError, "too short sockaddr");
    if (((struct sockaddr *)sockaddr)->sa_family != AF_INET
#ifdef INET6
        && ((struct sockaddr *)sockaddr)->sa_family != AF_INET6
#endif
        ) {
#ifdef INET6
        rb_raise(rb_eArgError, "not an AF_INET/AF_INET6 sockaddr");
#else
        rb_raise(rb_eArgError, "not an AF_INET sockaddr");
#endif
    }
    host = make_ipaddr((struct sockaddr*)sockaddr);
    OBJ_INFECT(host, addr);
    return rb_assoc_new(INT2NUM(ntohs(sockaddr->sin_port)), host);
}

#ifdef HAVE_SYS_UN_H
/*
 * call-seq:
 *   Socket.sockaddr_un(path)      => sockaddr
 *   Socket.pack_sockaddr_un(path) => sockaddr
 *
 * Packs _path_ as an AF_UNIX sockaddr string.
 *
 *   Socket.sockaddr_un("/tmp/sock") #=> "\x01\x00/tmp/sock\x00\x00..."
 *
 */
static VALUE
sock_s_pack_sockaddr_un(VALUE self, SEL sel, VALUE path)
{
    struct sockaddr_un sockaddr;
    char *sun_path;
    VALUE addr;

    MEMZERO(&sockaddr, struct sockaddr_un, 1);
    sockaddr.sun_family = AF_UNIX;
    sun_path = StringValueCStr(path);
    if (sizeof(sockaddr.sun_path) <= strlen(sun_path)) {
        rb_raise(rb_eArgError, "too long unix socket path (max: %dbytes)",
            (int)sizeof(sockaddr.sun_path)-1);
    }
    strncpy(sockaddr.sun_path, sun_path, sizeof(sockaddr.sun_path)-1);
    addr = rb_str_new((char*)&sockaddr, sizeof(sockaddr));
    OBJ_INFECT(addr, path);

    return addr;
}

/*
 * call-seq:
 *   Socket.unpack_sockaddr_un(sockaddr) => path
 *
 * Unpacks _sockaddr_ into path.
 *
 * _sockaddr_ should be a string or an addrinfo for AF_UNIX.
 *
 *   sockaddr = Socket.sockaddr_un("/tmp/sock")
 *   p Socket.unpack_sockaddr_un(sockaddr) #=> "/tmp/sock"
 *
 */
static VALUE
sock_s_unpack_sockaddr_un(VALUE self, SEL sel, VALUE addr)
{
    struct sockaddr_un * sockaddr;
    char *sun_path;
    VALUE path;

    sockaddr = (struct sockaddr_un*)SockAddrStringValuePtr(addr);
    if (RSTRING_LEN(addr) <
        (char*)&((struct sockaddr *)sockaddr)->sa_family +
        sizeof(((struct sockaddr *)sockaddr)->sa_family) -
        (char*)sockaddr)
        rb_raise(rb_eArgError, "too short sockaddr");
    if (((struct sockaddr *)sockaddr)->sa_family != AF_UNIX) {
        rb_raise(rb_eArgError, "not an AF_UNIX sockaddr");
    }
    if (sizeof(struct sockaddr_un) < (size_t)RSTRING_LEN(addr)) {
	rb_raise(rb_eTypeError, "too long sockaddr_un - %ld longer than %ld",
		 RSTRING_LEN(addr), sizeof(struct sockaddr_un));
    }
    sun_path = unixpath(sockaddr, RSTRING_LENINT(addr));
    if (sizeof(struct sockaddr_un) == RSTRING_LEN(addr) &&
        sun_path == sockaddr->sun_path &&
        sun_path + strlen(sun_path) == RSTRING_PTR(addr) + RSTRING_LEN(addr)) {
        rb_raise(rb_eArgError, "sockaddr_un.sun_path not NUL terminated");
    }
    path = rb_str_new2(sun_path);
    OBJ_INFECT(path, addr);
    return path;
}
#endif

#if defined(HAVE_GETIFADDRS) || defined(SIOCGLIFCONF) || defined(SIOCGIFCONF) || defined(_WIN32)
static VALUE
sockaddr_obj(struct sockaddr *addr)
{
    socklen_t len;
#if defined(AF_INET6) && defined(__KAME__)
    struct sockaddr_in6 addr6;
#endif

    if (addr == NULL)
        return Qnil;

    switch (addr->sa_family) {
      case AF_INET:
        len = (socklen_t)sizeof(struct sockaddr_in);
        break;

#ifdef AF_INET6
      case AF_INET6:
        len = (socklen_t)sizeof(struct sockaddr_in6);
#  ifdef __KAME__
	/* KAME uses the 2nd 16bit word of link local IPv6 address as interface index internally */
        /* http://orange.kame.net/dev/cvsweb.cgi/kame/IMPLEMENTATION */
	/* convert fe80:1::1 to fe80::1%1 */
	memcpy(&addr6, addr, len);
	addr = (struct sockaddr *)&addr6;
	if (IN6_IS_ADDR_LINKLOCAL(&addr6.sin6_addr) &&
	    addr6.sin6_scope_id == 0 &&
	    (addr6.sin6_addr.s6_addr[2] || addr6.sin6_addr.s6_addr[3])) {
	    addr6.sin6_scope_id = (addr6.sin6_addr.s6_addr[2] << 8) | addr6.sin6_addr.s6_addr[3];
	    addr6.sin6_addr.s6_addr[2] = 0;
	    addr6.sin6_addr.s6_addr[3] = 0;
	}
#  endif
        break;
#endif

#ifdef HAVE_SYS_UN_H
      case AF_UNIX:
        len = (socklen_t)sizeof(struct sockaddr_un);
        break;
#endif

      default:
        len = (socklen_t)sizeof(struct sockaddr_in);
        break;
    }
#ifdef SA_LEN
    if (len < (socklen_t)SA_LEN(addr))
	len = (socklen_t)SA_LEN(addr);
#endif

    return addrinfo_new(addr, len, addr->sa_family, 0, 0, Qnil, Qnil);
}
#endif

#if defined(HAVE_GETIFADDRS) || (defined(SIOCGLIFCONF) && defined(SIOCGLIFNUM) && !defined(__hpux)) || defined(SIOCGIFCONF) ||  defined(_WIN32)
/*
 * call-seq:
 *   Socket.ip_address_list => array
 *
 * Returns local IP addresses as an array.
 *
 * The array contains Addrinfo objects.
 *
 *  pp Socket.ip_address_list
 *  #=> [#<Addrinfo: 127.0.0.1>,
 *       #<Addrinfo: 192.168.0.128>,
 *       #<Addrinfo: ::1>,
 *       ...]
 *
 */
static VALUE
socket_s_ip_address_list(VALUE self, SEL sel)
{
#if defined(HAVE_GETIFADDRS)
    struct ifaddrs *ifp = NULL;
    struct ifaddrs *p;
    int ret;
    VALUE list;

    ret = getifaddrs(&ifp);
    if (ret == -1) {
        rb_sys_fail("getifaddrs");
    }

    list = rb_ary_new();
    for (p = ifp; p; p = p->ifa_next) {
        if (p->ifa_addr != NULL && IS_IP_FAMILY(p->ifa_addr->sa_family)) {
            rb_ary_push(list, sockaddr_obj(p->ifa_addr));
        }
    }

    freeifaddrs(ifp);

    return list;
#elif defined(SIOCGLIFCONF) && defined(SIOCGLIFNUM) && !defined(__hpux)
    /* Solaris if_tcp(7P) */
    /* HP-UX has SIOCGLIFCONF too.  But it uses different struct */
    int fd = -1;
    int ret;
    struct lifnum ln;
    struct lifconf lc;
    char *reason = NULL;
    int save_errno;
    int i;
    VALUE list = Qnil;

    lc.lifc_buf = NULL;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == -1)
        rb_sys_fail("socket");

    memset(&ln, 0, sizeof(ln));
    ln.lifn_family = AF_UNSPEC;

    ret = ioctl(fd, SIOCGLIFNUM, &ln);
    if (ret == -1) {
	reason = "SIOCGLIFNUM";
	goto finish;
    }

    memset(&lc, 0, sizeof(lc));
    lc.lifc_family = AF_UNSPEC;
    lc.lifc_flags = 0;
    lc.lifc_len = sizeof(struct lifreq) * ln.lifn_count;
    lc.lifc_req = xmalloc(lc.lifc_len);

    ret = ioctl(fd, SIOCGLIFCONF, &lc);
    if (ret == -1) {
	reason = "SIOCGLIFCONF";
	goto finish;
    }

    list = rb_ary_new();
    for (i = 0; i < ln.lifn_count; i++) {
	struct lifreq *req = &lc.lifc_req[i];
        if (IS_IP_FAMILY(req->lifr_addr.ss_family)) {
            if (req->lifr_addr.ss_family == AF_INET6 &&
                IN6_IS_ADDR_LINKLOCAL(&((struct sockaddr_in6 *)(&req->lifr_addr))->sin6_addr) &&
                ((struct sockaddr_in6 *)(&req->lifr_addr))->sin6_scope_id == 0) {
                struct lifreq req2;
                memcpy(req2.lifr_name, req->lifr_name, LIFNAMSIZ);
                ret = ioctl(fd, SIOCGLIFINDEX, &req2);
                if (ret == -1) {
                    reason = "SIOCGLIFINDEX";
                    goto finish;
                }
                ((struct sockaddr_in6 *)(&req->lifr_addr))->sin6_scope_id = req2.lifr_index;
            }
            rb_ary_push(list, sockaddr_obj((struct sockaddr *)&req->lifr_addr));
        }
    }

  finish:
    save_errno = errno;
    if (lc.lifc_buf != NULL)
	xfree(lc.lifc_req);
    if (fd != -1)
	close(fd);
    errno = save_errno;

    if (reason)
	rb_sys_fail(reason);
    return list;

#elif defined(SIOCGIFCONF)
    int fd = -1;
    int ret;
#define EXTRA_SPACE (sizeof(struct ifconf) + sizeof(struct sockaddr_storage))
    char initbuf[4096+EXTRA_SPACE];
    char *buf = initbuf;
    int bufsize;
    struct ifconf conf;
    struct ifreq *req;
    VALUE list = Qnil;
    const char *reason = NULL;
    int save_errno;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == -1)
        rb_sys_fail("socket");

    bufsize = sizeof(initbuf);
    buf = initbuf;

  retry:
    conf.ifc_len = bufsize;
    conf.ifc_req = (struct ifreq *)buf;

    /* fprintf(stderr, "bufsize: %d\n", bufsize); */

    ret = ioctl(fd, SIOCGIFCONF, &conf);
    if (ret == -1) {
        reason = "SIOCGIFCONF";
        goto finish;
    }

    /* fprintf(stderr, "conf.ifc_len: %d\n", conf.ifc_len); */

    if (bufsize - EXTRA_SPACE < conf.ifc_len) {
	if (bufsize < conf.ifc_len) {
	    /* NetBSD returns required size for all interfaces. */
	    bufsize = conf.ifc_len + EXTRA_SPACE;
	}
	else {
	    bufsize = bufsize << 1;
	}
	if (buf == initbuf)
	    buf = NULL;
	buf = xrealloc(buf, bufsize);
	goto retry;
    }

    close(fd);
    fd = -1;

    list = rb_ary_new();
    req = conf.ifc_req;
    while ((char*)req < (char*)conf.ifc_req + conf.ifc_len) {
	struct sockaddr *addr = &req->ifr_addr;
        if (IS_IP_FAMILY(addr->sa_family)) {
	    rb_ary_push(list, sockaddr_obj(addr));
	}
#ifdef HAVE_SA_LEN
# ifndef _SIZEOF_ADDR_IFREQ
#  define _SIZEOF_ADDR_IFREQ(r) \
          (sizeof(struct ifreq) + \
           (sizeof(struct sockaddr) < (r).ifr_addr.sa_len ? \
            (r).ifr_addr.sa_len - sizeof(struct sockaddr) : \
            0))
# endif
	req = (struct ifreq *)((char*)req + _SIZEOF_ADDR_IFREQ(*req));
#else
	req = (struct ifreq *)((char*)req + sizeof(struct ifreq));
#endif
    }

  finish:

    save_errno = errno;
    if (buf != initbuf)
        xfree(buf);
    if (fd != -1)
	close(fd);
    errno = save_errno;

    if (reason)
	rb_sys_fail(reason);
    return list;

#undef EXTRA_SPACE
#elif defined(_WIN32)
    typedef struct ip_adapter_unicast_address_st {
	unsigned LONG_LONG dummy0;
	struct ip_adapter_unicast_address_st *Next;
	struct {
	    struct sockaddr *lpSockaddr;
	    int iSockaddrLength;
	} Address;
	int dummy1;
	int dummy2;
	int dummy3;
	long dummy4;
	long dummy5;
	long dummy6;
    } ip_adapter_unicast_address_t;
    typedef struct ip_adapter_anycast_address_st {
	unsigned LONG_LONG dummy0;
	struct ip_adapter_anycast_address_st *Next;
	struct {
	    struct sockaddr *lpSockaddr;
	    int iSockaddrLength;
	} Address;
    } ip_adapter_anycast_address_t;
    typedef struct ip_adapter_addresses_st {
	unsigned LONG_LONG dummy0;
	struct ip_adapter_addresses_st *Next;
	void *dummy1;
	ip_adapter_unicast_address_t *FirstUnicastAddress;
	ip_adapter_anycast_address_t *FirstAnycastAddress;
	void *dummy2;
	void *dummy3;
	void *dummy4;
	void *dummy5;
	void *dummy6;
	BYTE dummy7[8];
	DWORD dummy8;
	DWORD dummy9;
	DWORD dummy10;
	DWORD IfType;
	int OperStatus;
	DWORD dummy12;
	DWORD dummy13[16];
	void *dummy14;
    } ip_adapter_addresses_t;
    typedef ULONG (WINAPI *GetAdaptersAddresses_t)(ULONG, ULONG, PVOID, ip_adapter_addresses_t *, PULONG);
    HMODULE h;
    GetAdaptersAddresses_t pGetAdaptersAddresses;
    ULONG len;
    DWORD ret;
    ip_adapter_addresses_t *adapters;
    VALUE list;

    h = LoadLibrary("iphlpapi.dll");
    if (!h)
	rb_notimplement();
    pGetAdaptersAddresses = (GetAdaptersAddresses_t)GetProcAddress(h, "GetAdaptersAddresses");
    if (!pGetAdaptersAddresses) {
	FreeLibrary(h);
	rb_notimplement();
    }

    ret = pGetAdaptersAddresses(AF_UNSPEC, 0, NULL, NULL, &len);
    if (ret != ERROR_SUCCESS && ret != ERROR_BUFFER_OVERFLOW) {
	errno = rb_w32_map_errno(ret);
	FreeLibrary(h);
	rb_sys_fail("GetAdaptersAddresses");
    }
    adapters = (ip_adapter_addresses_t *)ALLOCA_N(BYTE, len);
    ret = pGetAdaptersAddresses(AF_UNSPEC, 0, NULL, adapters, &len);
    if (ret != ERROR_SUCCESS) {
	errno = rb_w32_map_errno(ret);
	FreeLibrary(h);
	rb_sys_fail("GetAdaptersAddresses");
    }

    list = rb_ary_new();
    for (; adapters; adapters = adapters->Next) {
	ip_adapter_unicast_address_t *uni;
	ip_adapter_anycast_address_t *any;
	if (adapters->OperStatus != 1)	/* 1 means IfOperStatusUp */
	    continue;
	for (uni = adapters->FirstUnicastAddress; uni; uni = uni->Next) {
#ifndef INET6
	    if (uni->Address.lpSockaddr->sa_family == AF_INET)
#else
	    if (IS_IP_FAMILY(uni->Address.lpSockaddr->sa_family))
#endif
		rb_ary_push(list, sockaddr_obj(uni->Address.lpSockaddr));
	}
	for (any = adapters->FirstAnycastAddress; any; any = any->Next) {
#ifndef INET6
	    if (any->Address.lpSockaddr->sa_family == AF_INET)
#else
	    if (IS_IP_FAMILY(any->Address.lpSockaddr->sa_family))
#endif
		rb_ary_push(list, sockaddr_obj(any->Address.lpSockaddr));
	}
    }

    FreeLibrary(h);
    return list;
#endif
}
#else
#define socket_s_ip_address_list rb_f_notimplement
#endif

typedef struct {
    VALUE inspectname;
    VALUE canonname;
    int pfamily;
    int socktype;
    int protocol;
    socklen_t sockaddr_len;
    struct sockaddr_storage addr;
} rb_addrinfo_t;

static void
addrinfo_mark(void *ptr)
{
    rb_addrinfo_t *rai = ptr;
    if (rai) {
        //rb_gc_mark(rai->inspectname);
        //rb_gc_mark(rai->canonname);
    }
}

static void
addrinfo_free(rb_addrinfo_t *rai)
{
    xfree(rai);
}

static VALUE
addrinfo_s_allocate(VALUE klass, SEL sel)
{
    return Data_Wrap_Struct(klass, addrinfo_mark, addrinfo_free, 0);
}

#define IS_ADDRINFO(obj) (RDATA(obj)->dmark == (RUBY_DATA_FUNC)addrinfo_mark)
static rb_addrinfo_t *
check_addrinfo(VALUE self)
{
    Check_Type(self, RUBY_T_DATA);
    if (!IS_ADDRINFO(self)) {
	rb_raise(rb_eTypeError, "wrong argument type %s (expected Addrinfo)",
		 rb_class2name(CLASS_OF(self)));
    }
    return DATA_PTR(self);
}

static rb_addrinfo_t *
get_addrinfo(VALUE self)
{
    rb_addrinfo_t *rai = check_addrinfo(self);

    if (!rai) {
        rb_raise(rb_eTypeError, "uninitialized socket address");
    }
    return rai;
}

static rb_addrinfo_t *
alloc_addrinfo()
{
    rb_addrinfo_t *rai = ALLOC(rb_addrinfo_t);
    memset(rai, 0, sizeof(rb_addrinfo_t));
    rai->inspectname = Qnil;
    rai->canonname = Qnil;
    return rai;
}

static void
init_addrinfo(rb_addrinfo_t *rai, struct sockaddr *sa, socklen_t len,
              int pfamily, int socktype, int protocol,
              VALUE canonname, VALUE inspectname)
{
    if (sizeof(rai->addr) < len)
        rb_raise(rb_eArgError, "sockaddr string too big");
    memcpy((void *)&rai->addr, (void *)sa, len);
    rai->sockaddr_len = len;

    rai->pfamily = pfamily;
    rai->socktype = socktype;
    rai->protocol = protocol;
    GC_WB(&rai->canonname, canonname);
    GC_WB(&rai->inspectname, inspectname);
}

static VALUE
addrinfo_new(struct sockaddr *addr, socklen_t len,
	     int family, int socktype, int protocol,
	     VALUE canonname, VALUE inspectname)
{
    VALUE a;
    rb_addrinfo_t *rai;

    a = addrinfo_s_allocate(rb_cAddrinfo, 0);
    rai = alloc_addrinfo();
    GC_WB(&(DATA_PTR(a)), rai);
    init_addrinfo(rai, addr, len, family, socktype, protocol, canonname, inspectname);
    return a;
}

static struct addrinfo *
call_getaddrinfo(VALUE node, VALUE service,
		 VALUE family, VALUE socktype, VALUE protocol, VALUE flags,
		 int socktype_hack)
{
    struct addrinfo hints, *res;

    MEMZERO(&hints, struct addrinfo, 1);
    hints.ai_family = NIL_P(family) ? PF_UNSPEC : family_arg(family);

    if (!NIL_P(socktype)) {
	hints.ai_socktype = socktype_arg(socktype);
    }
    if (!NIL_P(protocol)) {
	hints.ai_protocol = NUM2INT(protocol);
    }
    if (!NIL_P(flags)) {
	hints.ai_flags = NUM2INT(flags);
    }
    res = sock_getaddrinfo(node, service, &hints, socktype_hack);

    if (res == NULL)
	rb_raise(rb_eSocket, "host not found");
    return res;
}

static VALUE make_inspectname(VALUE node, VALUE service, struct addrinfo *res);

static void
init_addrinfo_getaddrinfo(rb_addrinfo_t *rai, VALUE node, VALUE service,
			  VALUE family, VALUE socktype, VALUE protocol, VALUE flags,
			  VALUE inspectnode, VALUE inspectservice)
{
    struct addrinfo *res = call_getaddrinfo(node, service, family, socktype, protocol, flags, 1);
    VALUE canonname;
    VALUE inspectname = rb_str_equal(node, inspectnode) ? Qnil : make_inspectname(inspectnode, inspectservice, res);

    canonname = Qnil;
    if (res->ai_canonname) {
        canonname = rb_tainted_str_new_cstr(res->ai_canonname);
        OBJ_FREEZE(canonname);
    }

    init_addrinfo(rai, res->ai_addr, res->ai_addrlen,
                  NUM2INT(family), NUM2INT(socktype), NUM2INT(protocol),
                  canonname, inspectname);

    freeaddrinfo(res);
}

static VALUE
make_inspectname(VALUE node, VALUE service, struct addrinfo *res)
{
    VALUE inspectname = Qnil;

    if (res) {
        char hbuf[NI_MAXHOST], pbuf[NI_MAXSERV];
        int ret;
        ret = getnameinfo(res->ai_addr, res->ai_addrlen, hbuf,
                             sizeof(hbuf), pbuf, sizeof(pbuf),
                             NI_NUMERICHOST|NI_NUMERICSERV);
        if (ret == 0) {
            if (TYPE(node) == T_STRING && strcmp(hbuf, RSTRING_PTR(node)) == 0)
                node = Qnil;
            if (TYPE(service) == T_STRING && strcmp(pbuf, RSTRING_PTR(service)) == 0)
                service = Qnil;
            else if (TYPE(service) == T_FIXNUM && atoi(pbuf) == FIX2INT(service))
                service = Qnil;
        }
    }

    if (TYPE(node) == T_STRING) {
        inspectname = rb_str_dup(node);
    }
    if (TYPE(service) == T_STRING) {
        if (NIL_P(inspectname))
            inspectname = rb_sprintf(":%s", StringValueCStr(service));
        else
            rb_str_catf(inspectname, ":%s", StringValueCStr(service));
    }
    else if (TYPE(service) == T_FIXNUM && FIX2INT(service) != 0)
    {
        if (NIL_P(inspectname))
            inspectname = rb_sprintf(":%d", (int)FIX2INT(service));
        else
            rb_str_catf(inspectname, ":%d", (int)FIX2INT(service));
    }
    if (!NIL_P(inspectname)) {
        OBJ_INFECT(inspectname, node);
        OBJ_INFECT(inspectname, service);
        OBJ_FREEZE(inspectname);
    }
    return inspectname;
}

static VALUE
addrinfo_firstonly_new(VALUE node, VALUE service, VALUE family, VALUE socktype, VALUE protocol, VALUE flags)
{
    VALUE ret;
    VALUE canonname;
    VALUE inspectname;

    struct addrinfo *res = call_getaddrinfo(node, service, family, socktype, protocol, flags, 0);

    inspectname = make_inspectname(node, service, res);

    canonname = Qnil;
    if (res->ai_canonname) {
        canonname = rb_tainted_str_new_cstr(res->ai_canonname);
        OBJ_FREEZE(canonname);
    }

    ret = addrinfo_new(res->ai_addr, res->ai_addrlen,
		       res->ai_family, res->ai_socktype, res->ai_protocol,
		       canonname, inspectname);

    freeaddrinfo(res);
    return ret;
}

static VALUE
addrinfo_list_new(VALUE node, VALUE service, VALUE family, VALUE socktype, VALUE protocol, VALUE flags)
{
    VALUE ret;
    struct addrinfo *r;
    VALUE inspectname;

    struct addrinfo *res = call_getaddrinfo(node, service, family, socktype, protocol, flags, 0);

    inspectname = make_inspectname(node, service, res);

    ret = rb_ary_new();
    for (r = res; r; r = r->ai_next) {
        VALUE addr;
        VALUE canonname = Qnil;

        if (r->ai_canonname) {
            canonname = rb_tainted_str_new_cstr(r->ai_canonname);
            OBJ_FREEZE(canonname);
        }

        addr = addrinfo_new(r->ai_addr, r->ai_addrlen,
                            r->ai_family, r->ai_socktype, r->ai_protocol,
                            canonname, inspectname);

        rb_ary_push(ret, addr);
    }

    freeaddrinfo(res);
    return ret;
}


#ifdef HAVE_SYS_UN_H
static void
init_unix_addrinfo(rb_addrinfo_t *rai, VALUE path, int socktype)
{
    struct sockaddr_un un;

    StringValue(path);

    if (sizeof(un.sun_path) <= (size_t)RSTRING_LEN(path))
        rb_raise(rb_eArgError, "too long unix socket path (max: %dbytes)",
            (int)sizeof(un.sun_path)-1);

    MEMZERO(&un, struct sockaddr_un, 1);

    un.sun_family = AF_UNIX;
    memcpy((void*)&un.sun_path, RSTRING_PTR(path), RSTRING_LEN(path));

    init_addrinfo(rai, (struct sockaddr *)&un, (socklen_t)sizeof(un),
		  PF_UNIX, socktype, 0, Qnil, Qnil);
}
#endif

/*
 * call-seq:
 *   Addrinfo.new(sockaddr)                             => addrinfo
 *   Addrinfo.new(sockaddr, family)                     => addrinfo
 *   Addrinfo.new(sockaddr, family, socktype)           => addrinfo
 *   Addrinfo.new(sockaddr, family, socktype, protocol) => addrinfo
 *
 * returns a new instance of Addrinfo.
 * The instance contains sockaddr, family, socktype, protocol.
 * sockaddr means struct sockaddr which can be used for connect(2), etc.
 * family, socktype and protocol are integers which is used for arguments of socket(2).
 *
 * sockaddr is specified as an array or a string.
 * The array should be compatible to the value of IPSocket#addr or UNIXSocket#addr.
 * The string should be struct sockaddr as generated by
 * Socket.sockaddr_in or Socket.unpack_sockaddr_un.
 *
 * sockaddr examples:
 * - ["AF_INET", 46102, "localhost.localdomain", "127.0.0.1"]
 * - ["AF_INET6", 42304, "ip6-localhost", "::1"]
 * - ["AF_UNIX", "/tmp/sock"]
 * - Socket.sockaddr_in("smtp", "2001:DB8::1")
 * - Socket.sockaddr_in(80, "172.18.22.42")
 * - Socket.sockaddr_in(80, "www.ruby-lang.org")
 * - Socket.sockaddr_un("/tmp/sock")
 *
 * In an AF_INET/AF_INET6 sockaddr array, the 4th element,
 * numeric IP address, is used to construct socket address in the Addrinfo instance.
 * If the 3rd element, textual host name, is non-nil, it is also recorded but used only for Addrinfo#inspect.
 *
 * family is specified as an integer to specify the protocol family such as Socket::PF_INET.
 * It can be a symbol or a string which is the constant name
 * with or without PF_ prefix such as :INET, :INET6, :UNIX, "PF_INET", etc.
 * If omitted, PF_UNSPEC is assumed.
 *
 * socktype is specified as an integer to specify the socket type such as Socket::SOCK_STREAM.
 * It can be a symbol or a string which is the constant name
 * with or without SOCK_ prefix such as :STREAM, :DGRAM, :RAW, "SOCK_STREAM", etc.
 * If omitted, 0 is assumed.
 *
 * protocol is specified as an integer to specify the protocol such as Socket::IPPROTO_TCP.
 * It must be an integer, unlike family and socktype.
 * If omitted, 0 is assumed.
 * Note that 0 is reasonable value for most protocols, except raw socket.
 *
 */
static VALUE
addrinfo_initialize(VALUE self, SEL sel, int argc, VALUE *argv)
{
    rb_addrinfo_t *rai;
    VALUE sockaddr_arg, sockaddr_ary, pfamily, socktype, protocol;
    int i_pfamily, i_socktype, i_protocol;
    struct sockaddr *sockaddr_ptr;
    socklen_t sockaddr_len;
    VALUE canonname = Qnil, inspectname = Qnil;

    if (check_addrinfo(self))
        rb_raise(rb_eTypeError, "already initialized socket address");
    rai = alloc_addrinfo();
    GC_WB(&(DATA_PTR(self)), rai);

    rb_scan_args(argc, argv, "13", &sockaddr_arg, &pfamily, &socktype, &protocol);

    i_pfamily = NIL_P(pfamily) ? PF_UNSPEC : family_arg(pfamily);
    i_socktype = NIL_P(socktype) ? 0 : socktype_arg(socktype);
    i_protocol = NIL_P(protocol) ? 0 : NUM2INT(protocol);

    sockaddr_ary = rb_check_array_type(sockaddr_arg);
    if (!NIL_P(sockaddr_ary)) {
        VALUE afamily = rb_ary_entry(sockaddr_ary, 0);
        int af;
        StringValue(afamily);
        if (family_to_int(RSTRING_PTR(afamily), RSTRING_LENINT(afamily), &af) == -1)
	    rb_raise(rb_eSocket, "unknown address family: %s", StringValueCStr(afamily));
        switch (af) {
          case AF_INET: /* ["AF_INET", 46102, "localhost.localdomain", "127.0.0.1"] */
#ifdef INET6
          case AF_INET6: /* ["AF_INET6", 42304, "ip6-localhost", "::1"] */
#endif
          {
            VALUE service = rb_ary_entry(sockaddr_ary, 1);
            VALUE nodename = rb_ary_entry(sockaddr_ary, 2);
            VALUE numericnode = rb_ary_entry(sockaddr_ary, 3);
            int flags;

            service = INT2NUM(NUM2INT(service));
            if (!NIL_P(nodename))
                StringValue(nodename);
            StringValue(numericnode);
            flags = AI_NUMERICHOST;
#ifdef AI_NUMERICSERV
            flags |= AI_NUMERICSERV;
#endif

            init_addrinfo_getaddrinfo(rai, numericnode, service,
                    INT2NUM(i_pfamily ? i_pfamily : af), INT2NUM(i_socktype), INT2NUM(i_protocol),
                    INT2NUM(flags),
                    nodename, service);
            break;
          }

#ifdef HAVE_SYS_UN_H
          case AF_UNIX: /* ["AF_UNIX", "/tmp/sock"] */
          {
            VALUE path = rb_ary_entry(sockaddr_ary, 1);
            StringValue(path);
            init_unix_addrinfo(rai, path, SOCK_STREAM);
            break;
          }
#endif

          default:
            rb_raise(rb_eSocket, "unexpected address family");
        }
    }
    else {
        StringValue(sockaddr_arg);
        sockaddr_ptr = (struct sockaddr *)RSTRING_PTR(sockaddr_arg);
        sockaddr_len = RSTRING_LENINT(sockaddr_arg);
        init_addrinfo(rai, sockaddr_ptr, sockaddr_len,
                      i_pfamily, i_socktype, i_protocol,
                      canonname, inspectname);
    }

    return self;
}

static int
get_afamily(struct sockaddr *addr, socklen_t len)
{
    if ((char*)&addr->sa_family + sizeof(addr->sa_family) - (char*)addr <= len)
        return addr->sa_family;
    else
        return AF_UNSPEC;
}

static int
ai_get_afamily(rb_addrinfo_t *rai)
{
    return get_afamily((struct sockaddr *)&rai->addr, rai->sockaddr_len);
}

static VALUE
inspect_sockaddr(VALUE addrinfo, VALUE ret)
{
    rb_addrinfo_t *rai = get_addrinfo(addrinfo);

    if (rai->sockaddr_len == 0) {
        rb_str_cat2(ret, "empty-sockaddr");
    }
    else if ((long)rai->sockaddr_len < ((char*)&rai->addr.ss_family + sizeof(rai->addr.ss_family)) - (char*)&rai->addr)
        rb_str_cat2(ret, "too-short-sockaddr");
    else {
        switch (rai->addr.ss_family) {
          case AF_INET:
          {
            struct sockaddr_in *addr;
            int port;
            if (rai->sockaddr_len < sizeof(struct sockaddr_in)) {
                rb_str_cat2(ret, "too-short-AF_INET-sockaddr");
            }
            else {
                addr = (struct sockaddr_in *)&rai->addr;
                rb_str_catf(ret, "%d.%d.%d.%d",
                            ((unsigned char*)&addr->sin_addr)[0],
                            ((unsigned char*)&addr->sin_addr)[1],
                            ((unsigned char*)&addr->sin_addr)[2],
                            ((unsigned char*)&addr->sin_addr)[3]);
                port = ntohs(addr->sin_port);
                if (port)
                    rb_str_catf(ret, ":%d", port);
                if (sizeof(struct sockaddr_in) < rai->sockaddr_len)
                    rb_str_catf(ret, "(sockaddr %d bytes too long)", (int)(rai->sockaddr_len - sizeof(struct sockaddr_in)));
            }
            break;
          }

#ifdef AF_INET6
          case AF_INET6:
          {
            struct sockaddr_in6 *addr;
            char hbuf[1024];
            int port;
            int error;
            if (rai->sockaddr_len < sizeof(struct sockaddr_in6)) {
                rb_str_cat2(ret, "too-short-AF_INET6-sockaddr");
            }
            else {
                addr = (struct sockaddr_in6 *)&rai->addr;
                /* use getnameinfo for scope_id.
                 * RFC 4007: IPv6 Scoped Address Architecture
                 * draft-ietf-ipv6-scope-api-00.txt: Scoped Address Extensions to the IPv6 Basic Socket API
                 */
                error = getnameinfo((struct sockaddr *)&rai->addr, rai->sockaddr_len,
                                    hbuf, (socklen_t)sizeof(hbuf), NULL, 0,
                                    NI_NUMERICHOST|NI_NUMERICSERV);
                if (error) {
                    raise_socket_error("getnameinfo", error);
                }
                if (addr->sin6_port == 0) {
                    rb_str_cat2(ret, hbuf);
                }
                else {
                    port = ntohs(addr->sin6_port);
                    rb_str_catf(ret, "[%s]:%d", hbuf, port);
                }
                if (sizeof(struct sockaddr_in6) < rai->sockaddr_len)
                    rb_str_catf(ret, "(sockaddr %d bytes too long)", (int)(rai->sockaddr_len - sizeof(struct sockaddr_in6)));
            }
            break;
          }
#endif

#ifdef HAVE_SYS_UN_H
          case AF_UNIX:
          {
            struct sockaddr_un *addr = (struct sockaddr_un *)&rai->addr;
            char *p, *s, *t, *e;
            s = addr->sun_path;
            e = (char*)addr + rai->sockaddr_len;
            if (e < s)
                rb_str_cat2(ret, "too-short-AF_UNIX-sockaddr");
            else if (s == e)
                rb_str_cat2(ret, "empty-path-AF_UNIX-sockaddr");
            else {
                int printable_only = 1;
                p = s;
                while (p < e && *p != '\0') {
                    printable_only = printable_only && ISPRINT(*p) && !ISSPACE(*p);
                    p++;
                }
                t = p;
                while (p < e && *p == '\0')
                    p++;
                if (printable_only && /* only printable, no space */
                    t < e && /* NUL terminated */
                    p == e) { /* no data after NUL */
		    if (s == t)
			rb_str_cat2(ret, "empty-path-AF_UNIX-sockaddr");
		    else if (s[0] == '/') /* absolute path */
			rb_str_cat2(ret, s);
		    else
                        rb_str_catf(ret, "AF_UNIX %s", s);
                }
                else {
                    rb_str_cat2(ret, "AF_UNIX");
                    e = (char *)addr->sun_path + sizeof(addr->sun_path);
                    while (s < e && *(e-1) == '\0')
                        e--;
                    while (s < e)
                        rb_str_catf(ret, ":%02x", (unsigned char)*s++);
                }
                if (addr->sun_path + sizeof(addr->sun_path) < (char*)&rai->addr + rai->sockaddr_len)
                    rb_str_catf(ret, "(sockaddr %d bytes too long)",
                            (int)(rai->sockaddr_len - (addr->sun_path + sizeof(addr->sun_path) - (char*)&rai->addr)));
            }
            break;
          }
#endif

          default:
          {
            ID id = intern_family(rai->addr.ss_family);
            if (id == 0)
                rb_str_catf(ret, "unknown address family %d", rai->addr.ss_family);
            else
                rb_str_catf(ret, "%s address format unknown", rb_id2name(id));
            break;
          }
        }
    }

    return ret;
}

/*
 * call-seq:
 *   addrinfo.inspect => string
 *
 * returns a string which shows addrinfo in human-readable form.
 *
 *   Addrinfo.tcp("localhost", 80).inspect #=> "#<Addrinfo: 127.0.0.1:80 TCP (localhost:80)>"
 *   Addrinfo.unix("/tmp/sock").inspect    #=> "#<Addrinfo: /tmp/sock SOCK_STREAM>"
 *
 */
static VALUE
addrinfo_inspect(VALUE self, SEL sel)
{
    rb_addrinfo_t *rai = get_addrinfo(self);
    int internet_p;
    VALUE ret;

    ret = rb_sprintf("#<%s: ", rb_obj_classname(self));

    inspect_sockaddr(self, ret);

    if (rai->pfamily && ai_get_afamily(rai) != rai->pfamily) {
        ID id = intern_protocol_family(rai->pfamily);
        if (id)
            rb_str_catf(ret, " %s", rb_id2name(id));
        else
            rb_str_catf(ret, " PF_\?\?\?(%d)", rai->pfamily);
    }

    internet_p = rai->pfamily == PF_INET;
#ifdef INET6
    internet_p = internet_p || rai->pfamily == PF_INET6;
#endif
    if (internet_p && rai->socktype == SOCK_STREAM &&
        (rai->protocol == 0 || rai->protocol == IPPROTO_TCP)) {
        rb_str_cat2(ret, " TCP");
    }
    else if (internet_p && rai->socktype == SOCK_DGRAM &&
        (rai->protocol == 0 || rai->protocol == IPPROTO_UDP)) {
        rb_str_cat2(ret, " UDP");
    }
    else {
        if (rai->socktype) {
            ID id = intern_socktype(rai->socktype);
            if (id)
                rb_str_catf(ret, " %s", rb_id2name(id));
            else
                rb_str_catf(ret, " SOCK_\?\?\?(%d)", rai->socktype);
        }

        if (rai->protocol) {
            if (internet_p) {
                ID id = intern_ipproto(rai->protocol);
                if (id)
                    rb_str_catf(ret, " %s", rb_id2name(id));
                else
                    goto unknown_protocol;
            }
            else {
              unknown_protocol:
                rb_str_catf(ret, " UNKNOWN_PROTOCOL(%d)", rai->protocol);
            }
        }
    }

    if (!NIL_P(rai->canonname)) {
        VALUE name = rai->canonname;
        rb_str_catf(ret, " %s", StringValueCStr(name));
    }

    if (!NIL_P(rai->inspectname)) {
        VALUE name = rai->inspectname;
        rb_str_catf(ret, " (%s)", StringValueCStr(name));
    }

    rb_str_buf_cat2(ret, ">");
    return ret;
}

/*
 * call-seq:
 *   addrinfo.inspect_sockaddr => string
 *
 * returns a string which shows the sockaddr in _addrinfo_ with human-readable form.
 *
 *   Addrinfo.tcp("localhost", 80).inspect_sockaddr     #=> "127.0.0.1:80"
 *   Addrinfo.tcp("ip6-localhost", 80).inspect_sockaddr #=> "[::1]:80"
 *   Addrinfo.unix("/tmp/sock").inspect_sockaddr        #=> "/tmp/sock"
 *
 */
static VALUE
addrinfo_inspect_sockaddr(VALUE self, SEL sel)
{
    return inspect_sockaddr(self, rb_str_new("", 0));
}

/*
 * call-seq:
 *   addrinfo.afamily => integer
 *
 * returns the address family as an integer.
 *
 *   Addrinfo.tcp("localhost", 80).afamily == Socket::AF_INET #=> true
 *
 */
static VALUE
addrinfo_afamily(VALUE self, SEL sel)
{
    rb_addrinfo_t *rai = get_addrinfo(self);
    return INT2NUM(ai_get_afamily(rai));
}

/*
 * call-seq:
 *   addrinfo.pfamily => integer
 *
 * returns the protocol family as an integer.
 *
 *   Addrinfo.tcp("localhost", 80).pfamily == Socket::PF_INET #=> true
 *
 */
static VALUE
addrinfo_pfamily(VALUE self, SEL sel)
{
    rb_addrinfo_t *rai = get_addrinfo(self);
    return INT2NUM(rai->pfamily);
}

/*
 * call-seq:
 *   addrinfo.socktype => integer
 *
 * returns the socket type as an integer.
 *
 *   Addrinfo.tcp("localhost", 80).socktype == Socket::SOCK_STREAM #=> true
 *
 */
static VALUE
addrinfo_socktype(VALUE self, SEL sel)
{
    rb_addrinfo_t *rai = get_addrinfo(self);
    return INT2NUM(rai->socktype);
}

/*
 * call-seq:
 *   addrinfo.protocol => integer
 *
 * returns the socket type as an integer.
 *
 *   Addrinfo.tcp("localhost", 80).protocol == Socket::IPPROTO_TCP #=> true
 *
 */
static VALUE
addrinfo_protocol(VALUE self, SEL sel)
{
    rb_addrinfo_t *rai = get_addrinfo(self);
    return INT2NUM(rai->protocol);
}

/*
 * call-seq:
 *   addrinfo.to_sockaddr => string
 *
 * returns the socket address as packed struct sockaddr string.
 *
 *   Addrinfo.tcp("localhost", 80).to_sockaddr
 *   #=> "\x02\x00\x00P\x7F\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00"
 *
 */
static VALUE
addrinfo_to_sockaddr(VALUE self, SEL sel)
{
    rb_addrinfo_t *rai = get_addrinfo(self);
    VALUE ret;
    ret = rb_str_new((char*)&rai->addr, rai->sockaddr_len);
    OBJ_INFECT(ret, self);
    return ret;
}

/*
 * call-seq:
 *   addrinfo.canonname => string or nil
 *
 * returns the canonical name as an string.
 *
 * nil is returned if no canonical name.
 *
 * The canonical name is set by Addrinfo.getaddrinfo when AI_CANONNAME is specified.
 *
 *   list = Addrinfo.getaddrinfo("www.ruby-lang.org", 80, :INET, :STREAM, nil, Socket::AI_CANONNAME)
 *   p list[0] #=> #<Addrinfo: 221.186.184.68:80 TCP carbon.ruby-lang.org (www.ruby-lang.org:80)>
 *   p list[0].canonname #=> "carbon.ruby-lang.org"
 *
 */
static VALUE
addrinfo_canonname(VALUE self, SEL sel)
{
    rb_addrinfo_t *rai = get_addrinfo(self);
    return rai->canonname;
}

/*
 * call-seq:
 *   addrinfo.ip? => true or false
 *
 * returns true if addrinfo is internet (IPv4/IPv6) address.
 * returns false otherwise.
 *
 *   Addrinfo.tcp("127.0.0.1", 80).ip? #=> true
 *   Addrinfo.tcp("::1", 80).ip?       #=> true
 *   Addrinfo.unix("/tmp/sock").ip?    #=> false
 *
 */
static VALUE
addrinfo_ip_p(VALUE self, SEL sel)
{
    rb_addrinfo_t *rai = get_addrinfo(self);
    int family = ai_get_afamily(rai);
    return IS_IP_FAMILY(family) ? Qtrue : Qfalse;
}

/*
 * call-seq:
 *   addrinfo.ipv4? => true or false
 *
 * returns true if addrinfo is IPv4 address.
 * returns false otherwise.
 *
 *   Addrinfo.tcp("127.0.0.1", 80).ipv4? #=> true
 *   Addrinfo.tcp("::1", 80).ipv4?       #=> false
 *   Addrinfo.unix("/tmp/sock").ipv4?    #=> false
 *
 */
static VALUE
addrinfo_ipv4_p(VALUE self, SEL sel)
{
    rb_addrinfo_t *rai = get_addrinfo(self);
    return ai_get_afamily(rai) == AF_INET ? Qtrue : Qfalse;
}

/*
 * call-seq:
 *   addrinfo.ipv6? => true or false
 *
 * returns true if addrinfo is IPv6 address.
 * returns false otherwise.
 *
 *   Addrinfo.tcp("127.0.0.1", 80).ipv6? #=> false
 *   Addrinfo.tcp("::1", 80).ipv6?       #=> true
 *   Addrinfo.unix("/tmp/sock").ipv6?    #=> false
 *
 */
static VALUE
addrinfo_ipv6_p(VALUE self, SEL sel)
{
#ifdef AF_INET6
    rb_addrinfo_t *rai = get_addrinfo(self);
    return ai_get_afamily(rai) == AF_INET6 ? Qtrue : Qfalse;
#else
    return Qfalse;
#endif
}

/*
 * call-seq:
 *   addrinfo.unix? => true or false
 *
 * returns true if addrinfo is UNIX address.
 * returns false otherwise.
 *
 *   Addrinfo.tcp("127.0.0.1", 80).unix? #=> false
 *   Addrinfo.tcp("::1", 80).unix?       #=> false
 *   Addrinfo.unix("/tmp/sock").unix?    #=> true
 *
 */
static VALUE
addrinfo_unix_p(VALUE self, SEL sel)
{
    rb_addrinfo_t *rai = get_addrinfo(self);
#ifdef AF_UNIX
    return ai_get_afamily(rai) == AF_UNIX ? Qtrue : Qfalse;
#else
    return Qfalse;
#endif
}

/*
 * call-seq:
 *   addrinfo.getnameinfo        => [nodename, service]
 *   addrinfo.getnameinfo(flags) => [nodename, service]
 *
 * returns nodename and service as a pair of strings.
 * This converts struct sockaddr in addrinfo to textual representation.
 *
 * flags should be bitwise OR of Socket::NI_??? constants.
 *
 *   Addrinfo.tcp("127.0.0.1", 80).getnameinfo #=> ["localhost", "www"]
 *
 *   Addrinfo.tcp("127.0.0.1", 80).getnameinfo(Socket::NI_NUMERICSERV)
 *   #=> ["localhost", "80"]
 */
static VALUE
addrinfo_getnameinfo(VALUE self, SEL sel, int argc, VALUE *argv)
{
    rb_addrinfo_t *rai = get_addrinfo(self);
    VALUE vflags;
    char hbuf[1024], pbuf[1024];
    int flags, error;

    rb_scan_args(argc, argv, "01", &vflags);

    flags = NIL_P(vflags) ? 0 : NUM2INT(vflags);

    if (rai->socktype == SOCK_DGRAM)
        flags |= NI_DGRAM;

    error = getnameinfo((struct sockaddr *)&rai->addr, rai->sockaddr_len,
                        hbuf, (socklen_t)sizeof(hbuf), pbuf, (socklen_t)sizeof(pbuf),
                        flags);
    if (error) {
        raise_socket_error("getnameinfo", error);
    }

    return rb_assoc_new(rb_str_new2(hbuf), rb_str_new2(pbuf));
}

/*
 * call-seq:
 *   addrinfo.ip_unpack => [addr, port]
 *
 * Returns the IP address and port number as 2-element array.
 *
 *   Addrinfo.tcp("127.0.0.1", 80).ip_unpack    #=> ["127.0.0.1", 80]
 *   Addrinfo.tcp("::1", 80).ip_unpack          #=> ["::1", 80]
 */
static VALUE
addrinfo_ip_unpack(VALUE self, SEL sel)
{
    rb_addrinfo_t *rai = get_addrinfo(self);
    int family = ai_get_afamily(rai);
    VALUE vflags;
    VALUE ret, portstr;

    if (!IS_IP_FAMILY(family))
	rb_raise(rb_eSocket, "need IPv4 or IPv6 address");

    vflags = INT2NUM(NI_NUMERICHOST|NI_NUMERICSERV);
    ret = addrinfo_getnameinfo(self, 0, 1, &vflags);
    portstr = rb_ary_entry(ret, 1);
    rb_ary_store(ret, 1, INT2NUM(atoi(StringValueCStr(portstr))));
    return ret;
}

/*
 * call-seq:
 *   addrinfo.ip_address => string
 *
 * Returns the IP address as a string.
 *
 *   Addrinfo.tcp("127.0.0.1", 80).ip_address    #=> "127.0.0.1"
 *   Addrinfo.tcp("::1", 80).ip_address          #=> "::1"
 */
static VALUE
addrinfo_ip_address(VALUE self, SEL sel)
{
    rb_addrinfo_t *rai = get_addrinfo(self);
    int family = ai_get_afamily(rai);
    VALUE vflags;
    VALUE ret;

    if (!IS_IP_FAMILY(family))
	rb_raise(rb_eSocket, "need IPv4 or IPv6 address");

    vflags = INT2NUM(NI_NUMERICHOST|NI_NUMERICSERV);
    ret = addrinfo_getnameinfo(self, 0, 1, &vflags);
    return rb_ary_entry(ret, 0);
}

/*
 * call-seq:
 *   addrinfo.ip_port => port
 *
 * Returns the port number as an integer.
 *
 *   Addrinfo.tcp("127.0.0.1", 80).ip_port    #=> 80
 *   Addrinfo.tcp("::1", 80).ip_port          #=> 80
 */
static VALUE
addrinfo_ip_port(VALUE self, SEL sel)
{
    rb_addrinfo_t *rai = get_addrinfo(self);
    int family = ai_get_afamily(rai);
    int port;

    if (!IS_IP_FAMILY(family)) {
      bad_family:
#ifdef AF_INET6
	rb_raise(rb_eSocket, "need IPv4 or IPv6 address");
#else
	rb_raise(rb_eSocket, "need IPv4 address");
#endif
    }

    switch (family) {
      case AF_INET:
        if (rai->sockaddr_len != sizeof(struct sockaddr_in))
            rb_raise(rb_eSocket, "unexpected sockaddr size for IPv4");
        port = ntohs(((struct sockaddr_in *)&rai->addr)->sin_port);
        break;

#ifdef AF_INET6
      case AF_INET6:
        if (rai->sockaddr_len != sizeof(struct sockaddr_in6))
            rb_raise(rb_eSocket, "unexpected sockaddr size for IPv6");
        port = ntohs(((struct sockaddr_in6 *)&rai->addr)->sin6_port);
        break;
#endif

      default:
	goto bad_family;
    }

    return INT2NUM(port);
}

static int
extract_in_addr(VALUE self, uint32_t *addrp)
{
    rb_addrinfo_t *rai = get_addrinfo(self);
    int family = ai_get_afamily(rai);
    if (family != AF_INET) return 0;
    *addrp = ntohl(((struct sockaddr_in *)&rai->addr)->sin_addr.s_addr);
    return 1;
}

/*
 * Returns true for IPv4 private address (10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16).
 * It returns false otherwise.
 */
static VALUE
addrinfo_ipv4_private_p(VALUE self, SEL sel)
{
    uint32_t a;
    if (!extract_in_addr(self, &a)) return Qfalse;
    if ((a & 0xff000000) == 0x0a000000 || /* 10.0.0.0/8 */
        (a & 0xfff00000) == 0xac100000 || /* 172.16.0.0/12 */
        (a & 0xffff0000) == 0xc0a80000)   /* 192.168.0.0/16 */
        return Qtrue;
    return Qfalse;
}

/*
 * Returns true for IPv4 loopback address (127.0.0.0/8).
 * It returns false otherwise.
 */
static VALUE
addrinfo_ipv4_loopback_p(VALUE self, SEL sel)
{
    uint32_t a;
    if (!extract_in_addr(self, &a)) return Qfalse;
    if ((a & 0xff000000) == 0x7f000000) /* 127.0.0.0/8 */
        return Qtrue;
    return Qfalse;
}

/*
 * Returns true for IPv4 multicast address (224.0.0.0/4).
 * It returns false otherwise.
 */
static VALUE
addrinfo_ipv4_multicast_p(VALUE self, SEL sel)
{
    uint32_t a;
    if (!extract_in_addr(self, &a)) return Qfalse;
    if ((a & 0xf0000000) == 0xe0000000) /* 224.0.0.0/4 */
        return Qtrue;
    return Qfalse;
}

#ifdef INET6

static struct in6_addr *
extract_in6_addr(VALUE self)
{
    rb_addrinfo_t *rai = get_addrinfo(self);
    int family = ai_get_afamily(rai);
    if (family != AF_INET6) return NULL;
    return &((struct sockaddr_in6 *)&rai->addr)->sin6_addr;
}

/*
 * Returns true for IPv6 unspecified address (::).
 * It returns false otherwise.
 */
static VALUE
addrinfo_ipv6_unspecified_p(VALUE self, SEL sel)
{
    struct in6_addr *addr = extract_in6_addr(self);
    if (addr && IN6_IS_ADDR_UNSPECIFIED(addr)) return Qtrue;
    return Qfalse;
}

/*
 * Returns true for IPv6 loopback address (::1).
 * It returns false otherwise.
 */
static VALUE
addrinfo_ipv6_loopback_p(VALUE self, SEL sel)
{
    struct in6_addr *addr = extract_in6_addr(self);
    if (addr && IN6_IS_ADDR_LOOPBACK(addr)) return Qtrue;
    return Qfalse;
}

/*
 * Returns true for IPv6 multicast address (ff00::/8).
 * It returns false otherwise.
 */
static VALUE
addrinfo_ipv6_multicast_p(VALUE self, SEL sel)
{
    struct in6_addr *addr = extract_in6_addr(self);
    if (addr && IN6_IS_ADDR_MULTICAST(addr)) return Qtrue;
    return Qfalse;
}

/*
 * Returns true for IPv6 link local address (ff80::/10).
 * It returns false otherwise.
 */
static VALUE
addrinfo_ipv6_linklocal_p(VALUE self, SEL sel)
{
    struct in6_addr *addr = extract_in6_addr(self);
    if (addr && IN6_IS_ADDR_LINKLOCAL(addr)) return Qtrue;
    return Qfalse;
}

/*
 * Returns true for IPv6 site local address (ffc0::/10).
 * It returns false otherwise.
 */
static VALUE
addrinfo_ipv6_sitelocal_p(VALUE self, SEL sel)
{
    struct in6_addr *addr = extract_in6_addr(self);
    if (addr && IN6_IS_ADDR_SITELOCAL(addr)) return Qtrue;
    return Qfalse;
}

/*
 * Returns true for IPv4-mapped IPv6 address (::ffff:0:0/80).
 * It returns false otherwise.
 */
static VALUE
addrinfo_ipv6_v4mapped_p(VALUE self, SEL sel)
{
    struct in6_addr *addr = extract_in6_addr(self);
    if (addr && IN6_IS_ADDR_V4MAPPED(addr)) return Qtrue;
    return Qfalse;
}

/*
 * Returns true for IPv4-compatible IPv6 address (::/80).
 * It returns false otherwise.
 */
static VALUE
addrinfo_ipv6_v4compat_p(VALUE self, SEL sel)
{
    struct in6_addr *addr = extract_in6_addr(self);
    if (addr && IN6_IS_ADDR_V4COMPAT(addr)) return Qtrue;
    return Qfalse;
}

/*
 * Returns true for IPv6 multicast node-local scope address.
 * It returns false otherwise.
 */
static VALUE
addrinfo_ipv6_mc_nodelocal_p(VALUE self, SEL sel)
{
    struct in6_addr *addr = extract_in6_addr(self);
    if (addr && IN6_IS_ADDR_MC_NODELOCAL(addr)) return Qtrue;
    return Qfalse;
}

/*
 * Returns true for IPv6 multicast link-local scope address.
 * It returns false otherwise.
 */
static VALUE
addrinfo_ipv6_mc_linklocal_p(VALUE self, SEL sel)
{
    struct in6_addr *addr = extract_in6_addr(self);
    if (addr && IN6_IS_ADDR_MC_LINKLOCAL(addr)) return Qtrue;
    return Qfalse;
}

/*
 * Returns true for IPv6 multicast site-local scope address.
 * It returns false otherwise.
 */
static VALUE
addrinfo_ipv6_mc_sitelocal_p(VALUE self, SEL sel)
{
    struct in6_addr *addr = extract_in6_addr(self);
    if (addr && IN6_IS_ADDR_MC_SITELOCAL(addr)) return Qtrue;
    return Qfalse;
}

/*
 * Returns true for IPv6 multicast organization-local scope address.
 * It returns false otherwise.
 */
static VALUE
addrinfo_ipv6_mc_orglocal_p(VALUE self, SEL sel)
{
    struct in6_addr *addr = extract_in6_addr(self);
    if (addr && IN6_IS_ADDR_MC_ORGLOCAL(addr)) return Qtrue;
    return Qfalse;
}

/*
 * Returns true for IPv6 multicast global scope address.
 * It returns false otherwise.
 */
static VALUE
addrinfo_ipv6_mc_global_p(VALUE self, SEL sel)
{
    struct in6_addr *addr = extract_in6_addr(self);
    if (addr && IN6_IS_ADDR_MC_GLOBAL(addr)) return Qtrue;
    return Qfalse;
}

/*
 * Returns IPv4 address of IPv4 mapped/compatible IPv6 address.
 * It returns nil if +self+ is not IPv4 mapped/compatible IPv6 address.
 *
 *   Addrinfo.ip("::192.0.2.3").ipv6_to_ipv4      #=> #<Addrinfo: 192.0.2.3>
 *   Addrinfo.ip("::ffff:192.0.2.3").ipv6_to_ipv4 #=> #<Addrinfo: 192.0.2.3>
 *   Addrinfo.ip("::1").ipv6_to_ipv4              #=> nil
 *   Addrinfo.ip("192.0.2.3").ipv6_to_ipv4        #=> nil
 *   Addrinfo.unix("/tmp/sock").ipv6_to_ipv4      #=> nil
 */
static VALUE
addrinfo_ipv6_to_ipv4(VALUE self, SEL sel)
{
    rb_addrinfo_t *rai = get_addrinfo(self);
    struct in6_addr *addr;
    int family = ai_get_afamily(rai);
    if (family != AF_INET6) return Qnil;
    addr = &((struct sockaddr_in6 *)&rai->addr)->sin6_addr;
    if (IN6_IS_ADDR_V4MAPPED(addr) || IN6_IS_ADDR_V4COMPAT(addr)) {
        struct sockaddr_in sin4;
        MEMZERO(&sin4, struct sockaddr_in, 1);
        sin4.sin_family = AF_INET;
        SET_SIN_LEN(&sin4, sizeof(sin4));
        memcpy(&sin4.sin_addr, (char*)addr + sizeof(*addr) - sizeof(sin4.sin_addr), sizeof(sin4.sin_addr));
        return addrinfo_new((struct sockaddr *)&sin4, (socklen_t)sizeof(sin4),
                            PF_INET, rai->socktype, rai->protocol,
                            rai->canonname, rai->inspectname);
    }
    else {
        return Qnil;
    }
}

#endif

#ifdef HAVE_SYS_UN_H
/*
 * call-seq:
 *   addrinfo.unix_path => path
 *
 * Returns the socket path as a string.
 *
 *   Addrinfo.unix("/tmp/sock").unix_path       #=> "/tmp/sock"
 */
static VALUE
addrinfo_unix_path(VALUE self, SEL sel)
{
    rb_addrinfo_t *rai = get_addrinfo(self);
    int family = ai_get_afamily(rai);
    struct sockaddr_un *addr;
    char *s, *e;

    if (family != AF_UNIX)
	rb_raise(rb_eSocket, "need AF_UNIX address");

    addr = (struct sockaddr_un *)&rai->addr;

    s = addr->sun_path;
    e = (char*)addr + rai->sockaddr_len;
    if (e < s)
        rb_raise(rb_eSocket, "too short AF_UNIX address");
    if (addr->sun_path + sizeof(addr->sun_path) < e)
        rb_raise(rb_eSocket, "too long AF_UNIX address");
    while (s < e && *(e-1) == '\0')
        e--;
    return rb_str_new(s, e-s);
}
#endif

/*
 * call-seq:
 *   Addrinfo.getaddrinfo(nodename, service, family, socktype, protocol, flags) => [addrinfo, ...]
 *   Addrinfo.getaddrinfo(nodename, service, family, socktype, protocol)        => [addrinfo, ...]
 *   Addrinfo.getaddrinfo(nodename, service, family, socktype)                  => [addrinfo, ...]
 *   Addrinfo.getaddrinfo(nodename, service, family)                            => [addrinfo, ...]
 *   Addrinfo.getaddrinfo(nodename, service)                                    => [addrinfo, ...]
 *
 * returns a list of addrinfo objects as an array.
 *
 * This method converts nodename (hostname) and service (port) to addrinfo.
 * Since the conversion is not unique, the result is a list of addrinfo objects.
 *
 * nodename or service can be nil if no conversion intended.
 *
 * family, socktype and protocol are hint for preferred protocol.
 * If the result will be used for a socket with SOCK_STREAM,
 * SOCK_STREAM should be specified as socktype.
 * If so, Addrinfo.getaddrinfo returns addrinfo list appropriate for SOCK_STREAM.
 * If they are omitted or nil is given, the result is not restricted.
 *
 * Similarly, PF_INET6 as family restricts for IPv6.
 *
 * flags should be bitwise OR of Socket::AI_??? constants.
 *
 * Note that socktype should be specified whenever application knows the usage of the address.
 * Some platform causes an error when socktype is omitted and servname is specified as an integer
 * because some port numbers, 512 for example, are ambiguous without socktype.
 *
 *   Addrinfo.getaddrinfo("www.kame.net", 80, nil, :STREAM)
 *   #=> [#<Addrinfo: 203.178.141.194:80 TCP (www.kame.net:80)>,
 *   #    #<Addrinfo: [2001:200:0:8002:203:47ff:fea5:3085]:80 TCP (www.kame.net:80)>]
 *
 */
static VALUE
addrinfo_s_getaddrinfo(VALUE self, SEL sel, int argc, VALUE *argv)
{
    VALUE node, service, family, socktype, protocol, flags;

    rb_scan_args(argc, argv, "24", &node, &service, &family, &socktype, &protocol, &flags);
    return addrinfo_list_new(node, service, family, socktype, protocol, flags);
}

/*
 * call-seq:
 *   Addrinfo.ip(host) => addrinfo
 *
 * returns an addrinfo object for IP address.
 *
 * The port, socktype, protocol of the result is filled by zero.
 * So, it is not appropriate to create a socket.
 *
 *   Addrinfo.ip("localhost") #=> #<Addrinfo: 127.0.0.1 (localhost)>
 */
static VALUE
addrinfo_s_ip(VALUE self, SEL sel, VALUE host)
{
    VALUE ret;
    rb_addrinfo_t *rai;
    ret = addrinfo_firstonly_new(host, Qnil,
            INT2NUM(PF_UNSPEC), INT2FIX(0), INT2FIX(0), INT2FIX(0));
    rai = get_addrinfo(ret);
    rai->socktype = 0;
    rai->protocol = 0;
    return ret;
}

/*
 * call-seq:
 *   Addrinfo.tcp(host, port) => addrinfo
 *
 * returns an addrinfo object for TCP address.
 *
 *   Addrinfo.tcp("localhost", "smtp") #=> #<Addrinfo: 127.0.0.1:25 TCP (localhost:smtp)>
 */
static VALUE
addrinfo_s_tcp(VALUE self, SEL sel, VALUE host, VALUE port)
{
    return addrinfo_firstonly_new(host, port,
            INT2NUM(PF_UNSPEC), INT2NUM(SOCK_STREAM), INT2NUM(IPPROTO_TCP), INT2FIX(0));
}

/*
 * call-seq:
 *   Addrinfo.udp(host, port) => addrinfo
 *
 * returns an addrinfo object for UDP address.
 *
 *   Addrinfo.udp("localhost", "daytime") #=> #<Addrinfo: 127.0.0.1:13 UDP (localhost:daytime)>
 */
static VALUE
addrinfo_s_udp(VALUE self, SEL sel, VALUE host, VALUE port)
{
    return addrinfo_firstonly_new(host, port,
            INT2NUM(PF_UNSPEC), INT2NUM(SOCK_DGRAM), INT2NUM(IPPROTO_UDP), INT2FIX(0));
}

#ifdef HAVE_SYS_UN_H

/*
 * call-seq:
 *   Addrinfo.unix(path [, socktype]) => addrinfo
 *
 * returns an addrinfo object for UNIX socket address.
 *
 * _socktype_ specifies the socket type.
 * If it is omitted, :STREAM is used.
 *
 *   Addrinfo.unix("/tmp/sock")         #=> #<Addrinfo: /tmp/sock SOCK_STREAM>
 *   Addrinfo.unix("/tmp/sock", :DGRAM) #=> #<Addrinfo: /tmp/sock SOCK_DGRAM>
 */
static VALUE
addrinfo_s_unix(VALUE self, SEL sel, int argc, VALUE *argv)
{
    VALUE path, vsocktype, addr;
    int socktype;
    rb_addrinfo_t *rai;

    rb_scan_args(argc, argv, "11", &path, &vsocktype);

    if (NIL_P(vsocktype))
        socktype = SOCK_STREAM;
    else
        socktype = socktype_arg(vsocktype);

    addr = addrinfo_s_allocate(rb_cAddrinfo, 0);
    rai = alloc_addrinfo();
    GC_WB(&(DATA_PTR(addr)), rai);
    init_unix_addrinfo(rai, path, socktype);
    OBJ_INFECT(addr, path);
    return addr;
}

#endif

static VALUE
sockaddr_string_value(volatile VALUE *v)
{
    VALUE val = *v;
    if (TYPE(val) == RUBY_T_DATA && IS_ADDRINFO(val)) {
        *v = addrinfo_to_sockaddr(val, 0);
    }
    StringValue(*v);
    return *v;
}

static char *
sockaddr_string_value_ptr(volatile VALUE *v)
{
    sockaddr_string_value(v);
    return RSTRING_PTR(*v);
}

static VALUE
fd_socket_addrinfo(int fd, struct sockaddr *addr, socklen_t len)
{
    int family;
    int socktype;
    int ret;
    socklen_t optlen = (socklen_t)sizeof(socktype);

    /* assumes protocol family and address family are identical */
    family = get_afamily(addr, len);

    ret = getsockopt(fd, SOL_SOCKET, SO_TYPE, (void*)&socktype, &optlen);
    if (ret == -1) {
        rb_sys_fail("getsockopt(SO_TYPE)");
    }

    return addrinfo_new(addr, len, family, socktype, 0, Qnil, Qnil);
}

static VALUE
io_socket_addrinfo(VALUE io, struct sockaddr *addr, socklen_t len)
{
    rb_io_t *fptr;

    switch (TYPE(io)) {
      case T_FIXNUM:
        return fd_socket_addrinfo(FIX2INT(io), addr, len);

      case T_BIGNUM:
        return fd_socket_addrinfo(NUM2INT(io), addr, len);

      case T_FILE:
        GetOpenFile(io, fptr);
        return fd_socket_addrinfo(fptr->fd, addr, len);

      default:
        rb_raise(rb_eTypeError, "neither IO nor file descriptor");
    }
}

static VALUE mConst;

static void
sock_define_const(char *name, int value)
{
    rb_define_const(rb_cSocket, name, INT2FIX(value));
    rb_define_const(mConst, name, INT2FIX(value));
}

static void
sock_define_uconst(const char *name, unsigned int value)
{
    rb_define_const(rb_cSocket, name, UINT2NUM(value));
    rb_define_const(mConst, name, UINT2NUM(value));
}

/*
 * Class +Socket+ provides access to the underlying operating system
 * socket implementations. It can be used to provide more operating system
 * specific functionality than the protocol-specific socket classes but at the
 * expense of greater complexity. In particular, the class handles addresses
 * using +struct+ sockaddr structures packed into Ruby strings, which can be
 * a joy to manipulate.
 *
 * === Exception Handling
 * Ruby's implementation of +Socket+ causes an exception to be raised
 * based on the error generated by the system dependent implementation.
 * This is why the methods are documented in a way that isolate
 * Unix-based system exceptions from Windows based exceptions. If more
 * information on particular exception is needed please refer to the
 * Unix manual pages or the Windows WinSock reference.
 *
 *
 * === Documentation by
 * * Zach Dennis
 * * Sam Roberts
 * * <em>Programming Ruby</em> from The Pragmatic Bookshelf.
 *
 * Much material in this documentation is taken with permission from
 * <em>Programming Ruby</em> from The Pragmatic Bookshelf.
 */
void
Init_socket()
{
    rb_eSocket = rb_define_class("SocketError", rb_eStandardError);

    rb_cBasicSocket = rb_define_class("BasicSocket", rb_cIO);
    rb_undef_method(rb_cBasicSocket, "initialize");

    rb_objc_define_method(*(VALUE *)rb_cBasicSocket, "do_not_reverse_lookup",
	    bsock_do_not_rev_lookup, 0);
    rb_objc_define_method(*(VALUE *)rb_cBasicSocket, "do_not_reverse_lookup=",
	    bsock_do_not_rev_lookup_set, 1);
    rb_objc_define_method(*(VALUE *)rb_cBasicSocket, "for_fd",
	    bsock_s_for_fd, 1);

    rb_objc_define_method(rb_cBasicSocket, "close_read", bsock_close_read, 0);
    rb_objc_define_method(rb_cBasicSocket, "close_write", bsock_close_write, 0);
    rb_objc_define_method(rb_cBasicSocket, "shutdown", bsock_shutdown, -1);
    rb_objc_define_method(rb_cBasicSocket, "setsockopt", bsock_setsockopt, 3);
    rb_objc_define_method(rb_cBasicSocket, "getsockopt", bsock_getsockopt, 2);
    rb_objc_define_method(rb_cBasicSocket, "getsockname", bsock_getsockname, 0);
    rb_objc_define_method(rb_cBasicSocket, "getpeername", bsock_getpeername, 0);
    rb_objc_define_method(rb_cBasicSocket, "getpeereid", bsock_getpeereid, 0);
    rb_objc_define_method(rb_cBasicSocket, "local_address", bsock_local_address, 0);
    rb_objc_define_method(rb_cBasicSocket, "remote_address", bsock_remote_address, 0);
    rb_objc_define_method(rb_cBasicSocket, "send", bsock_send, -1);
    rb_objc_define_method(rb_cBasicSocket, "recv", bsock_recv, -1);
    rb_objc_define_method(rb_cBasicSocket, "recv_nonblock", bsock_recv_nonblock, -1);
    rb_objc_define_method(rb_cBasicSocket, "do_not_reverse_lookup", bsock_do_not_reverse_lookup, 0);
    rb_objc_define_method(rb_cBasicSocket, "do_not_reverse_lookup=", bsock_do_not_reverse_lookup_set, 1);
    rb_objc_define_method(rb_cBasicSocket, "sendfile", socket_sendfile, 3);

    rb_cIPSocket = rb_define_class("IPSocket", rb_cBasicSocket);
    rb_objc_define_method(rb_cIPSocket, "addr", ip_addr, -1);
    rb_objc_define_method(rb_cIPSocket, "peeraddr", ip_peeraddr, -1);
    rb_objc_define_method(rb_cIPSocket, "recvfrom", ip_recvfrom, -1);
    rb_objc_define_method(*(VALUE *)rb_cIPSocket, "getaddress", ip_s_getaddress, 1);

    id_numeric = rb_intern_const("numeric");
    id_hostname = rb_intern_const("hostname");

    rb_cTCPSocket = rb_define_class("TCPSocket", rb_cIPSocket);
    rb_objc_define_method(*(VALUE *)rb_cTCPSocket, "gethostbyname", tcp_s_gethostbyname, 1);
    rb_objc_define_method(rb_cTCPSocket, "initialize", tcp_init, -1);

    rb_cTCPServer = rb_define_class("TCPServer", rb_cTCPSocket);
    rb_objc_define_method(rb_cTCPServer, "accept", tcp_accept, 0);
    rb_objc_define_method(rb_cTCPServer, "accept_nonblock", tcp_accept_nonblock, 0);
    rb_objc_define_method(rb_cTCPServer, "sysaccept", tcp_sysaccept, 0);
    rb_objc_define_method(rb_cTCPServer, "initialize", tcp_svr_init, -1);
    rb_objc_define_method(rb_cTCPServer, "listen", sock_listen, 1);

    rb_cUDPSocket = rb_define_class("UDPSocket", rb_cIPSocket);
    rb_objc_define_method(rb_cUDPSocket, "initialize", udp_init, -1);
    rb_objc_define_method(rb_cUDPSocket, "connect", udp_connect, 2);
    rb_objc_define_method(rb_cUDPSocket, "bind", udp_bind, 2);
    rb_objc_define_method(rb_cUDPSocket, "send", udp_send, -1);
    rb_objc_define_method(rb_cUDPSocket, "recvfrom_nonblock", udp_recvfrom_nonblock, -1);

#ifdef HAVE_SYS_UN_H
    rb_cUNIXSocket = rb_define_class("UNIXSocket", rb_cBasicSocket);
    rb_objc_define_method(rb_cUNIXSocket, "initialize", unix_init, 1);
    rb_objc_define_method(rb_cUNIXSocket, "path", unix_path, 0);
    rb_objc_define_method(rb_cUNIXSocket, "addr", unix_addr, 0);
    rb_objc_define_method(rb_cUNIXSocket, "peeraddr", unix_peeraddr, 0);
    rb_objc_define_method(rb_cUNIXSocket, "recvfrom", unix_recvfrom, -1);
    rb_objc_define_method(rb_cUNIXSocket, "send_io", unix_send_io, 1);
    rb_objc_define_method(rb_cUNIXSocket, "recv_io", unix_recv_io, -1);
    rb_objc_define_method(*(VALUE *)rb_cUNIXSocket, "socketpair", unix_s_socketpair, -1);
    rb_objc_define_method(*(VALUE *)rb_cUNIXSocket, "pair", unix_s_socketpair, -1);

    rb_cUNIXServer = rb_define_class("UNIXServer", rb_cUNIXSocket);
    rb_objc_define_method(rb_cUNIXServer, "initialize", unix_svr_init, 1);
    rb_objc_define_method(rb_cUNIXServer, "accept", unix_accept, 0);
    rb_objc_define_method(rb_cUNIXServer, "accept_nonblock", unix_accept_nonblock, 0);
    rb_objc_define_method(rb_cUNIXServer, "sysaccept", unix_sysaccept, 0);
    rb_objc_define_method(rb_cUNIXServer, "listen", sock_listen, 1);
#endif

    rb_cSocket = rb_define_class("Socket", rb_cBasicSocket);

    rb_objc_define_method(rb_cSocket, "initialize", sock_initialize, -1);
    rb_objc_define_method(rb_cSocket, "connect", sock_connect, 1);
    rb_objc_define_method(rb_cSocket, "connect_nonblock", sock_connect_nonblock, 1);
    rb_objc_define_method(rb_cSocket, "bind", sock_bind, 1);
    rb_objc_define_method(rb_cSocket, "listen", sock_listen, 1);
    rb_objc_define_method(rb_cSocket, "accept", sock_accept, 0);
    rb_objc_define_method(rb_cSocket, "accept_nonblock", sock_accept_nonblock, 0);
    rb_objc_define_method(rb_cSocket, "sysaccept", sock_sysaccept, 0);

    rb_objc_define_method(rb_cSocket, "recvfrom", sock_recvfrom, -1);
    rb_objc_define_method(rb_cSocket, "recvfrom_nonblock", sock_recvfrom_nonblock, -1);

    rb_objc_define_method(*(VALUE *)rb_cSocket, "socketpair", sock_s_socketpair, -1);
    rb_objc_define_method(*(VALUE *)rb_cSocket, "pair", sock_s_socketpair, -1);
    rb_objc_define_method(*(VALUE *)rb_cSocket, "gethostname", sock_gethostname, 0);
    rb_objc_define_method(*(VALUE *)rb_cSocket, "gethostbyname", sock_s_gethostbyname, 1);
    rb_objc_define_method(*(VALUE *)rb_cSocket, "gethostbyaddr", sock_s_gethostbyaddr, -1);
    rb_objc_define_method(*(VALUE *)rb_cSocket, "getservbyname", sock_s_getservbyname, -1);
    rb_objc_define_method(*(VALUE *)rb_cSocket, "getservbyport", sock_s_getservbyport, -1);
    rb_objc_define_method(*(VALUE *)rb_cSocket, "getaddrinfo", sock_s_getaddrinfo, -1);
    rb_objc_define_method(*(VALUE *)rb_cSocket, "getnameinfo", sock_s_getnameinfo, -1);
    rb_objc_define_method(*(VALUE *)rb_cSocket, "sockaddr_in", sock_s_pack_sockaddr_in, 2);
    rb_objc_define_method(*(VALUE *)rb_cSocket, "pack_sockaddr_in", sock_s_pack_sockaddr_in, 2);
    rb_objc_define_method(*(VALUE *)rb_cSocket, "unpack_sockaddr_in", sock_s_unpack_sockaddr_in, 1);
#ifdef HAVE_SYS_UN_H
    rb_objc_define_method(*(VALUE *)rb_cSocket, "sockaddr_un", sock_s_pack_sockaddr_un, 1);
    rb_objc_define_method(*(VALUE *)rb_cSocket, "pack_sockaddr_un", sock_s_pack_sockaddr_un, 1);
    rb_objc_define_method(*(VALUE *)rb_cSocket, "unpack_sockaddr_un", sock_s_unpack_sockaddr_un, 1);
#endif

    rb_objc_define_method(*(VALUE *)rb_cSocket, "ip_address_list", socket_s_ip_address_list, 0);

    rb_cAddrinfo = rb_define_class("Addrinfo", rb_cData);
    rb_objc_define_method(*(VALUE *)rb_cAddrinfo, "alloc", addrinfo_s_allocate, 0);
    rb_objc_define_method(rb_cAddrinfo, "initialize", addrinfo_initialize, -1);
    rb_objc_define_method(rb_cAddrinfo, "inspect", addrinfo_inspect, 0);
    rb_objc_define_method(rb_cAddrinfo, "inspect_sockaddr", addrinfo_inspect_sockaddr, 0);
    rb_objc_define_method(*(VALUE *)rb_cAddrinfo, "getaddrinfo", addrinfo_s_getaddrinfo, -1);
    rb_objc_define_method(*(VALUE *)rb_cAddrinfo, "ip", addrinfo_s_ip, 1);
    rb_objc_define_method(*(VALUE *)rb_cAddrinfo, "tcp", addrinfo_s_tcp, 2);
    rb_objc_define_method(*(VALUE *)rb_cAddrinfo, "udp", addrinfo_s_udp, 2);
#ifdef HAVE_SYS_UN_H
    rb_objc_define_method(*(VALUE *)rb_cAddrinfo, "unix", addrinfo_s_unix, -1);
#endif

    rb_objc_define_method(rb_cAddrinfo, "afamily", addrinfo_afamily, 0);
    rb_objc_define_method(rb_cAddrinfo, "pfamily", addrinfo_pfamily, 0);
    rb_objc_define_method(rb_cAddrinfo, "socktype", addrinfo_socktype, 0);
    rb_objc_define_method(rb_cAddrinfo, "protocol", addrinfo_protocol, 0);
    rb_objc_define_method(rb_cAddrinfo, "canonname", addrinfo_canonname, 0);

    rb_objc_define_method(rb_cAddrinfo, "ipv4?", addrinfo_ipv4_p, 0);
    rb_objc_define_method(rb_cAddrinfo, "ipv6?", addrinfo_ipv6_p, 0);
    rb_objc_define_method(rb_cAddrinfo, "unix?", addrinfo_unix_p, 0);

    rb_objc_define_method(rb_cAddrinfo, "ip?", addrinfo_ip_p, 0);
    rb_objc_define_method(rb_cAddrinfo, "ip_unpack", addrinfo_ip_unpack, 0);
    rb_objc_define_method(rb_cAddrinfo, "ip_address", addrinfo_ip_address, 0);
    rb_objc_define_method(rb_cAddrinfo, "ip_port", addrinfo_ip_port, 0);

    rb_objc_define_method(rb_cAddrinfo, "ipv4_private?", addrinfo_ipv4_private_p, 0);
    rb_objc_define_method(rb_cAddrinfo, "ipv4_loopback?", addrinfo_ipv4_loopback_p, 0);
    rb_objc_define_method(rb_cAddrinfo, "ipv4_multicast?", addrinfo_ipv4_multicast_p, 0);

#ifdef INET6
    rb_objc_define_method(rb_cAddrinfo, "ipv6_unspecified?", addrinfo_ipv6_unspecified_p, 0);
    rb_objc_define_method(rb_cAddrinfo, "ipv6_loopback?", addrinfo_ipv6_loopback_p, 0);
    rb_objc_define_method(rb_cAddrinfo, "ipv6_multicast?", addrinfo_ipv6_multicast_p, 0);
    rb_objc_define_method(rb_cAddrinfo, "ipv6_linklocal?", addrinfo_ipv6_linklocal_p, 0);
    rb_objc_define_method(rb_cAddrinfo, "ipv6_sitelocal?", addrinfo_ipv6_sitelocal_p, 0);
    rb_objc_define_method(rb_cAddrinfo, "ipv6_v4mapped?", addrinfo_ipv6_v4mapped_p, 0);
    rb_objc_define_method(rb_cAddrinfo, "ipv6_v4compat?", addrinfo_ipv6_v4compat_p, 0);
    rb_objc_define_method(rb_cAddrinfo, "ipv6_mc_nodelocal?", addrinfo_ipv6_mc_nodelocal_p, 0);
    rb_objc_define_method(rb_cAddrinfo, "ipv6_mc_linklocal?", addrinfo_ipv6_mc_linklocal_p, 0);
    rb_objc_define_method(rb_cAddrinfo, "ipv6_mc_sitelocal?", addrinfo_ipv6_mc_sitelocal_p, 0);
    rb_objc_define_method(rb_cAddrinfo, "ipv6_mc_orglocal?", addrinfo_ipv6_mc_orglocal_p, 0);
    rb_objc_define_method(rb_cAddrinfo, "ipv6_mc_global?", addrinfo_ipv6_mc_global_p, 0);

    rb_define_method(rb_cAddrinfo, "ipv6_to_ipv4", addrinfo_ipv6_to_ipv4, 0);
#endif

#ifdef HAVE_SYS_UN_H
    rb_objc_define_method(rb_cAddrinfo, "unix_path", addrinfo_unix_path, 0);
#endif

    rb_objc_define_method(rb_cAddrinfo, "to_sockaddr", addrinfo_to_sockaddr, 0);
    rb_objc_define_method(rb_cAddrinfo, "to_s", addrinfo_to_sockaddr, 0); /* compatibility for ruby before 1.9.2 */

    rb_objc_define_method(rb_cAddrinfo, "getnameinfo", addrinfo_getnameinfo, -1);

    /* constants */
    mConst = rb_define_module_under(rb_cSocket, "Constants");
    init_constants(mConst);
}
