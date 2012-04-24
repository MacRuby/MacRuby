/*
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2007-2010, Apple Inc. All rights reserved
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 */

#ifndef RUBY_DEFINES_H
#define RUBY_DEFINES_H 1

#if defined(__cplusplus)
extern "C" {
#endif

#define RUBY

#include <stdlib.h>
#ifdef __cplusplus
# ifndef  HAVE_PROTOTYPES
#  define HAVE_PROTOTYPES 1
# endif
# ifndef  HAVE_STDARG_PROTOTYPES
#  define HAVE_STDARG_PROTOTYPES 1
# endif
#endif

#undef _
#ifdef HAVE_PROTOTYPES
# define _(args) args
#else
# define _(args) ()
#endif

#undef __
#ifdef HAVE_STDARG_PROTOTYPES
# define __(args) args
#else
# define __(args) ()
#endif

#ifdef __cplusplus
#define ANYARGS ...
#else
#define ANYARGS
#endif

#define xmalloc ruby_xmalloc
#define xmalloc_ptrs ruby_xmalloc_ptrs
#define xmalloc2 ruby_xmalloc2
#define xcalloc ruby_xcalloc
#define xrealloc ruby_xrealloc
#define xrealloc2 ruby_xrealloc2
#define xfree ruby_xfree

void *xmalloc(size_t);
void *xmalloc_ptrs(size_t);
void *xmalloc2(size_t,size_t);
void *xcalloc(size_t,size_t);
void *xrealloc(void*,size_t);
void *xrealloc2(void*,size_t,size_t);
void xfree(void*);

#define STRINGIZE(expr) STRINGIZE0(expr)
#ifndef STRINGIZE0
#define STRINGIZE0(expr) #expr
#endif

#if SIZEOF_LONG_LONG > 0
# define LONG_LONG long long
#elif SIZEOF___INT64 > 0
# define HAVE_LONG_LONG 1
# define LONG_LONG __int64
# undef SIZEOF_LONG_LONG
# define SIZEOF_LONG_LONG SIZEOF___INT64
#endif

#include <AvailabilityMacros.h>

#if defined(__LP64__) && (MAC_OS_X_VERSION_MAX_ALLOWED >= 1060)
# define BDIGIT uint64_t
# define SIZEOF_BDIGITS 8
# define BDIGIT_DBL __uint128_t
# define BDIGIT_DBL_SIGNED __int128_t
#elif SIZEOF_INT*2 <= SIZEOF_LONG_LONG
# define BDIGIT unsigned int
# define SIZEOF_BDIGITS SIZEOF_INT
# define BDIGIT_DBL unsigned LONG_LONG
# define BDIGIT_DBL_SIGNED LONG_LONG
#elif SIZEOF_INT*2 <= SIZEOF_LONG
# define BDIGIT unsigned int
# define SIZEOF_BDIGITS SIZEOF_INT
# define BDIGIT_DBL unsigned long
# define BDIGIT_DBL_SIGNED long
#elif SIZEOF_SHORT*2 <= SIZEOF_LONG
# define BDIGIT unsigned short
# define SIZEOF_BDIGITS SIZEOF_SHORT
# define BDIGIT_DBL unsigned long
# define BDIGIT_DBL_SIGNED long
#else
# define BDIGIT unsigned short
# define SIZEOF_BDIGITS (SIZEOF_LONG/2)
# define BDIGIT_DBL unsigned long
# define BDIGIT_DBL_SIGNED long
#endif

#if defined(__NeXT__) || defined(__APPLE__)
/* Do not trust WORDS_BIGENDIAN from configure since -arch compiler flag may
   result in a different endian.  Instead trust __BIG_ENDIAN__ and
   __LITTLE_ENDIAN__ which are set correctly by -arch. */
#undef WORDS_BIGENDIAN
#ifdef __BIG_ENDIAN__
#define WORDS_BIGENDIAN
#endif
#endif

#ifdef RUBY_EXPORT
# undef RUBY_EXTERN
#endif

#ifndef RUBY_EXTERN
# define RUBY_EXTERN extern
#endif

#ifndef EXTERN
# define EXTERN RUBY_EXTERN	/* deprecated */
#endif

#ifndef RUBY_MBCHAR_MAXSIZE
#define RUBY_MBCHAR_MAXSIZE INT_MAX
        /* MB_CUR_MAX will not work well in C locale */
#endif

#define PATH_SEP ":"
#define PATH_SEP_CHAR PATH_SEP[0]
#define PATH_ENV "PATH"

#define CASEFOLD_FILESYSTEM 0

#ifndef DLEXT_MAXLEN
# define DLEXT_MAXLEN 4
#endif

#ifndef RUBY_PLATFORM
# define RUBY_PLATFORM "unknown-unknown"
#endif

#define WITH_OBJC 1
#define __MACRUBY__ 1

#define force_inline __attribute__((always_inline))

#if defined(__cplusplus)
} // extern "C" {
#endif

#endif /* RUBY_DEFINES_H */
