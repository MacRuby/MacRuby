/*
 * MacRuby implementation of Ruby 1.9 String.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 *
 * Copyright (C) 2012, The MacRuby Team. All rights reserved.
 * Copyright (C) 2007-2011, Apple Inc. All rights reserved.
 * Copyright (C) 1993-2007 Yukihiro Matsumoto
 * Copyright (C) 2000 Network Applied Communication Laboratory, Inc.
 * Copyright (C) 2000 Information-technology Promotion Agency, Japan
 */

#ifndef __UCNV_H_
#define __UCNV_H_

#include "encoding.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef void (^each_uchar32_callback_t)(UChar32 c, long start_index, long length, bool *stop);

void str_ucnv_update_flags(rb_str_t *self);
long str_ucnv_length(rb_str_t *self, bool ucs2_mode);
character_boundaries_t str_ucnv_get_character_boundaries(rb_str_t *self, long index, bool ucs2_mode);
long str_ucnv_offset_in_bytes_to_index(rb_str_t *self, long offset_in_bytes, bool ucs2_mode);
void str_ucnv_transcode_to_utf16(struct rb_encoding *src_enc, rb_str_t *self, long *pos, UChar **utf16, long *utf16_length);
void str_ucnv_transcode_from_utf16(struct rb_encoding *dst_enc, UChar *utf16, long utf16_length, long *utf16_pos, char **bytes, long *bytes_length);
void str_ucnv_each_uchar32_starting_from(rb_str_t *self,
	long start_offset_in_bytes,
	each_uchar32_callback_t callback);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif /* __UCNV_H_ */
