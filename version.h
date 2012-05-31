/*
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2007-2011, Apple Inc. All rights reserved.
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 */

#define RUBY_ENGINE 		"macruby"
#define MACRUBY_VERSION 	"0.13"

#define RUBY_VERSION 		"1.9.2"
#define RUBY_RELEASE_DATE 	"2008-06-03"
#define RUBY_VERSION_CODE 	190
#define RUBY_RELEASE_CODE 	20080603
#define RUBY_PATCHLEVEL 	0

#define RUBY_VERSION_MAJOR 	1
#define RUBY_VERSION_MINOR 	9
#define RUBY_VERSION_TEENY 	0
#define RUBY_RELEASE_YEAR 	2008
#define RUBY_RELEASE_MONTH 	6
#define RUBY_RELEASE_DAY 	3

#ifdef RUBY_EXTERN
RUBY_EXTERN const int ruby_version_code;
RUBY_EXTERN const char ruby_version[];
RUBY_EXTERN const char ruby_release_date[];
RUBY_EXTERN const char ruby_platform[];
RUBY_EXTERN const int ruby_patchlevel;
RUBY_EXTERN const char ruby_description[];
RUBY_EXTERN const char ruby_copyright[];
#endif

#define RUBY_AUTHOR 		"The MacRuby Team"
#define RUBY_BIRTH_YEAR 	2012

#ifndef RUBY_REVISION
# define RUBY_REVISION 0
#endif

#if RUBY_VERSION_TEENY > 0 && RUBY_PATCHLEVEL < 5000 && !RUBY_REVISION
#define RUBY_RELEASE_STR "patchlevel"
#define RUBY_RELEASE_NUM RUBY_PATCHLEVEL
#else
#ifdef RUBY_BRANCH_NAME
#define RUBY_RELEASE_STR RUBY_BRANCH_NAME
#else
#define RUBY_RELEASE_STR "revision"
#endif
#define RUBY_RELEASE_NUM RUBY_REVISION
#endif

#if defined(__LP64__)
#  if BYTE_ORDER == BIG_ENDIAN
#    define RUBY_ARCH "ppc64"
#  else
#    define RUBY_ARCH "x86_64"
#  endif
#else
#  if BYTE_ORDER == BIG_ENDIAN
#    define RUBY_ARCH "ppc"
#  else
#    define RUBY_ARCH "i386"
#  endif
#endif

#if MACRUBY_STATIC
# define MACRUBY_NAME "MacRuby-static"
#else
# define MACRUBY_NAME "MacRuby"
#endif

#define RUBY_DESCRIPTION	    	\
    MACRUBY_NAME " " MACRUBY_VERSION	\
    " (ruby "RUBY_VERSION	    	\
    ") ["RUBY_PLATFORM", "RUBY_ARCH"]"

#define RUBY_COPYRIGHT 	    \
    "MacRuby - Copyright (C) "	    \
    STRINGIZE(RUBY_BIRTH_YEAR)"-"   \
    STRINGIZE(RUBY_RELEASE_YEAR)" " \
    RUBY_AUTHOR
