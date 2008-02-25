/*  
 *  Copyright (c) 2008, Apple Inc. All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1.  Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *  2.  Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *  3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *      its contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND
 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR
 *  ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 *  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 *  STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 *  IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __BS_H_
#define __BS_H_

#include <CoreFoundation/CoreFoundation.h>
#include <objc/runtime.h>

/* Attribute and element representations.
 * See BridgeSupport(5) for more information.
 */

typedef enum {
  BS_CARRAY_ARG_UNDEFINED = 0,
  BS_CARRAY_ARG_LENGTH_IN_ARG,
  BS_CARRAY_ARG_FIXED_LENGTH,
  BS_CARRAY_ARG_VARIABLE_LENGTH,
  BS_CARRAY_ARG_DELIMITED_BY_NULL
} bs_carray_arg_type_t;

typedef enum {
  BS_TYPE_MODIFIER_UNDEFINED = 0,
  BS_TYPE_MODIFIER_IN,
  BS_TYPE_MODIFIER_OUT,
  BS_TYPE_MODIFIER_INOUT
} bs_type_modifier_t;

typedef struct {
  char *name;
  char *type;
} bs_element_struct_field_t;

typedef struct {
  char *name;
  char *type;
  bs_element_struct_field_t *fields;
  unsigned fields_count;
  bool opaque;
} bs_element_struct_t;

typedef struct {
  char *name;
  char *type;
  char *tollfree;
  CFTypeID type_id;
} bs_element_cftype_t;

typedef struct {
  char *name;
  char *type;
} bs_element_opaque_t;

typedef struct {
  char *name;
  char *type;
  bool magic_cookie;
  bool ignore;
  char *suggestion;
} bs_element_constant_t;

typedef struct {
  char *name;
  char *value;
  bool nsstring;
} bs_element_string_constant_t;

typedef struct {
  char *name;
  char *value;
  bool ignore;
  char *suggestion;  
} bs_element_enum_t;

struct __bs_element_arg;
struct __bs_element_retval;
typedef struct __bs_element_arg bs_element_arg_t;
typedef struct __bs_element_retval bs_element_retval_t;

typedef struct {
  bs_element_arg_t *args;
  unsigned args_count;
  bs_element_retval_t *retval;
} bs_element_function_pointer_t;

struct __bs_element_arg {
  int index; /* if -1, not used */
  char *type;
  bs_type_modifier_t type_modifier;
  bs_carray_arg_type_t carray_type;
  bs_element_function_pointer_t *function_pointer; /* can be NULL */
  int carray_type_value;
  bool null_accepted;
  bool printf_format;
  char *sel_of_type;
};

struct __bs_element_retval {
  char *type;
  bs_carray_arg_type_t carray_type;
  bs_element_function_pointer_t *function_pointer; /* can be NULL */
  int carray_type_value;
  bool already_retained;
};

typedef struct {
  char *name;
  bs_element_arg_t *args;
  unsigned args_count;
  bs_element_retval_t *retval;
  bool variadic;
} bs_element_function_t;

typedef struct {
  char *name;
  char *original;
} bs_element_function_alias_t;

typedef struct {
  SEL name;
  bs_element_arg_t *args;
  unsigned args_count;
  bs_element_retval_t *retval;
  bool class_method;
  bool variadic;
  bool ignore;
  char *suggestion;
} bs_element_method_t;

typedef struct {
  char *name;
  bs_element_method_t *class_methods;
  unsigned class_methods_count;
  bs_element_method_t *instance_methods;
  unsigned instance_methods_count;
} bs_element_class_t;

typedef struct {
  SEL name;
  char *type;
  char *protocol_name;
  bool class_method;
} bs_element_informal_protocol_method_t;

typedef enum {
  BS_ELEMENT_STRUCT,            /* bs_element_struct_t */
  BS_ELEMENT_CFTYPE,            /* bs_element_cftype_t */
  BS_ELEMENT_OPAQUE,            /* bs_element_opaque_t */
  BS_ELEMENT_CONSTANT,          /* bs_element_constant_t */
  BS_ELEMENT_STRING_CONSTANT,   /* bs_element_string_constant_t */
  BS_ELEMENT_ENUM,              /* bs_element_enum_t */
  BS_ELEMENT_FUNCTION,          /* bs_element_function_t */
  BS_ELEMENT_FUNCTION_ALIAS,    /* bs_element_function_alias_t */
  BS_ELEMENT_CLASS,             /* bs_element_class_t */
  BS_ELEMENT_INFORMAL_PROTOCOL_METHOD  
                                /* bs_element_informal_protocol_method_t */
} bs_element_type_t;

/* bs_find_path()
 *
 * Finds the path of a framework's bridge support file, by looking at the
 * following locations, in order of priority:
 *  - inside the framework bundle, in the Resources/BridgeSupport directory ;
 *  - in ~/Library/BridgeSupport ;
 *  - in /Library/BridgeSupport ;
 *  - in /System/Library/BridgeSupport.
 * Returns true on success, false otherwise.
 *
 * framework_path: the full path of the framework.
 * path: a pointer to a pre-allocated string that will contain the bridge
 * support path on success.
 * path_len: the size of the path argument.
 */
bool bs_find_path(const char *framework_path, char *path, const size_t path_len);

typedef void (*bs_parse_callback_t)
  (const char *path, bs_element_type_t type, void *value, void *context);

typedef enum {
  BS_PARSE_DEFAULT = 0
} bs_parse_options_t;

/* bs_parse() 
 *
 * Parses a given bridge support file, calling back a given function pointer 
 * for every parsed element. You are responsible to free every element passed 
 * to the callback function, using bs_element_free().
 * Returns true on success, otherwise false.
 *
 * path: the full path of the bridge support file to parse.
 * options: parsing options, reserved for future use. You can pass 0 for now.
 * callback: a callback function pointer.
 * context: a contextual data pointer that will be passed to the callback 
 * function.
 * error: in case this function returns false, this variable is set to a newly 
 * allocated error message. You are responsible to free it. Pass NULL if you 
 * don't need it.  
 */
bool bs_parse(const char *path, bs_parse_options_t options, 
  bs_parse_callback_t callback, void *context, char **error);

/* bs_element_free()
 *
 * Frees a bridge support element that was returned by bs_parse() through the 
 * callback method.
 *
 * type: the type of the bridge support element.
 * value: a pointer to the bridge support element.
 */
void bs_element_free(bs_element_type_t type, void *value);

typedef unsigned bs_register_token_t;

/* bs_register()
 *
 * Registers an asynchronous callback that will be called by bs_notify(). 
 * This function is meant to be called by scripting language bridges, to 
 * register themselves for future notifications by frameworks that dynamically
 * generate APIs. You are responsible to free every element passed to the 
 * callback function, using bs_element_free(). 
 * Returns a token identifier that can be passed to bs_unregister() to 
 * unregister the callback.
 *
 * callback: a callback function pointer.
 * context: a contextual data pointer that will be passed to the callback 
 * function.
*/
bs_register_token_t bs_register(bs_parse_callback_t *callback, void *context);

/* bs_unregister()
 *
 * Unregisters a previously-registered callback.
 *
 * token: a token that was returned by bs_register().
 */
void bs_unregister(bs_register_token_t token);

/* bs_notify()
 *
 * Notifies all clients registered through bs_register() that a new bridge 
 * support element is available. This function is meant to be called by 
 * frameworks that generate APIs at runtime. 
 *
 * type: the type of the bridge support element.
 * value: a pointer to the bridge support element. The element should be 
 * entierely composed of memory allocated by malloc(3).
 */
void bs_notify(bs_element_type_t type, void *value);

#endif /* __BS_H_ */
