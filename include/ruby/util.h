/*
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2007-2010, Apple Inc. All rights reserved
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 */

#ifndef RUBY_UTIL_H
#define RUBY_UTIL_H 1

#if defined(__cplusplus)
extern "C" {
#endif

unsigned long ruby_scan_oct(const char *, size_t, size_t *);
unsigned long ruby_scan_hex(const char *, size_t, size_t *);
#define scan_oct ruby_scan_oct
#define scan_hex ruby_scan_hex

void ruby_qsort(void *, const size_t, const size_t,
	int (*)(const void *, const void *, void *), void *);

void ruby_setenv(const char *, const char *);
void ruby_unsetenv(const char *);

char *ruby_strdup(const char *);

VALUE ruby_getcwd(void);

double ruby_strtod(const char *, char **);

void ruby_each_words(const char *, void (*)(const char*, int, void*), void *);

#if defined(__cplusplus)
} // extern "C" {
#endif

#endif /* RUBY_UTIL_H */
