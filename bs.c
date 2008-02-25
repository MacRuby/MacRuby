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

#include "bs_lex.h"
#include "bs.h"

#include <libxml/xmlreader.h>
#include <dlfcn.h>
#include <libgen.h>
#include <unistd.h>

#define ASSERT_ALLOC(ptr) (assert(ptr != NULL))

static inline char *
get_framework_name(const char *path)
{
  char *base;
  char *name;
  char *p;
  
  base = basename((char *)path);
  if (base == NULL)
    return NULL;
    
  p = strrchr(base, '.');
  if (p == NULL)
    return NULL;

  if (strcmp(p + 1, "framework") != 0)
    return NULL;
  
  assert(p - base > 0);
  
  name = (char *)malloc(p - base + 1);
  ASSERT_ALLOC(name);
  strncpy(name, base, p - base);
  name[p - base] = '\0';

  return name;
}

bool 
bs_find_path(const char *framework_path, char *path, const size_t path_len)
{
  char *framework_name;
  char *home;
  
  if (framework_path == NULL || *framework_path == '\0' 
      || path == NULL || path_len == 0)
    return false;

#define CHECK_IF_EXISTS()           \
  do {                              \
    if (access(path, R_OK) == 0) {  \
      free(framework_name);         \
      return true;                  \
    }                               \
  }                                 \
  while (0)

  framework_name = get_framework_name(framework_path);
  if (framework_name == NULL)
    return false;

  snprintf(path, path_len, "%s/Resources/BridgeSupport/%s.bridgesupport",
           framework_path, framework_name);
  CHECK_IF_EXISTS();

  home = getenv("HOME");
  if (home != NULL) {
    snprintf(path, path_len, "%s/Library/BridgeSupport/%s.bridgesupport",
      home, framework_name);
    CHECK_IF_EXISTS();
  }
  
  snprintf(path, path_len, "/Library/BridgeSupport/%s.bridgesupport",
    framework_name);
  CHECK_IF_EXISTS();

  snprintf(path, path_len, "/System/Library/BridgeSupport/%s.bridgesupport",
    framework_name);
  CHECK_IF_EXISTS();

#undef CHECK_IF_EXISTS

  free(framework_name);
  return false;  
}

static inline char *
get_attribute(xmlTextReaderPtr reader, const char *name)
{
  return (char *)xmlTextReaderGetAttribute(reader, (const xmlChar *)name);
}

static inline char *
get_type_attribute(xmlTextReaderPtr reader)
{
  xmlChar * value;

#if __LP64__
  value = xmlTextReaderGetAttribute(reader, (xmlChar *)"type64");
  if (value == NULL)
#endif
  value = xmlTextReaderGetAttribute(reader, (xmlChar *)"type");

  return (char *)value;
}

static void
get_c_ary_type_attribute(xmlTextReaderPtr reader, bs_carray_arg_type_t *type, 
                         int *value)
{
  char *c_ary_type;

  if ((c_ary_type = get_attribute(reader, "c_array_length_in_arg")) != NULL) {
    *type = BS_CARRAY_ARG_LENGTH_IN_ARG;
    *value = atoi(c_ary_type);
  }
  else if ((c_ary_type = get_attribute(reader, "c_array_of_fixed_length")) 
           != NULL) {
    *type = BS_CARRAY_ARG_FIXED_LENGTH;
    *value = atoi(c_ary_type);
  }
  else if ((c_ary_type = get_attribute(reader, "c_array_of_variable_length")) 
           != NULL && strcmp(c_ary_type, "true") == 0) {
    *type = BS_CARRAY_ARG_VARIABLE_LENGTH;
    *value = -1;
  }
  else if ((c_ary_type = get_attribute(reader, "c_array_delimited_by_null")) 
           != NULL && strcmp(c_ary_type, "true") == 0) {
    *type = BS_CARRAY_ARG_DELIMITED_BY_NULL;
    *value = -1;
  }
  else {
    *type = BS_CARRAY_ARG_UNDEFINED;
    *value = -1;
  }

  if (c_ary_type != NULL)
    free(c_ary_type);
}

static void
get_type_modifier_attribute(xmlTextReaderPtr reader, bs_type_modifier_t *type)
{
  char *type_modifier = get_attribute(reader, "type_modifier");
  
  *type = BS_TYPE_MODIFIER_UNDEFINED;
  
  if (type_modifier != NULL && strlen(type_modifier) == 1) {
    switch (*type_modifier) {
      case 'n':
        *type = BS_TYPE_MODIFIER_IN;
        break;
      case 'o':
        *type = BS_TYPE_MODIFIER_OUT;
        break;
      case 'N':
        *type = BS_TYPE_MODIFIER_INOUT;
        break;
    }
  }
} 

static inline bool
get_boolean_attribute(xmlTextReaderPtr reader, const char *name, 
                      bool default_value)
{
  char *value;
  bool ret;

  value = get_attribute(reader, name);
  if (value == NULL)
    return default_value;
  ret = strcmp(value, "true") == 0;
  free(value);
  return ret;
}

#define MAX_ENCODE_LEN 1024

#ifndef MIN
# define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

static bool 
undecorate_struct_type(const char *src, char *dest, size_t dest_len, 
                       bs_element_struct_field_t *fields, 
                       size_t fields_count, int *out_fields_count)
{
  const char *p_src;
  char *p_dst;
  char *pos;
  size_t src_len;
  unsigned field_idx;
  unsigned i;

  p_src = src;
  p_dst = dest;
  src_len = strlen(src);
  field_idx = 0;
  if (out_fields_count != NULL)
    *out_fields_count = 0;

  for (;;) {
    bs_element_struct_field_t *field;
    size_t len;

    field = field_idx < fields_count ? &fields[field_idx] : NULL;

    /* Locate the first field, if any. */
    pos = strchr(p_src, '"');

    /* Copy what's before the first field, or the rest of the source. */
    len = MIN(pos == NULL ? src_len - (p_src - src) + 1 : pos - p_src, dest_len - (p_dst - dest));
    strncpy(p_dst, p_src, len);
    p_dst += len;

    /* We can break if there wasn't any field. */
    if (pos == NULL)
      break;

    /* Jump to the end of the field, saving the field name if necessary. */
    p_src = pos + 1;
    pos = strchr(p_src, '"');
    if (pos == NULL) {
      // XXX NSLog("Can't find the end of field delimiter starting at %d", p_src - src);
      goto bails; 
    }
    if (field != NULL) {
      field->name = (char *)malloc((sizeof(char) * (pos - p_src)) + 1);
      ASSERT_ALLOC(field->name);
      strncpy(field->name, p_src, pos - p_src);
      field->name[pos - p_src] = '\0';
      field_idx++; 
    }
    p_src = pos + 1; 
    pos = NULL;

    /* Save the field encoding if necessary. */
    if (field != NULL) {
      char opposite;
      bool ok;
      int nested;

	  opposite = 
	    *p_src == '{' ? '}' :
        *p_src == '(' ? ')' :
        *p_src == '[' ? ']' : 0;

      for (i = 0, ok = false, nested = 0;
           i < src_len - (p_src - src) && !ok; 
           i++) {

        char c = p_src[i];

        if (opposite != 0) {
          /* Encoding is a structure, we need to match the closing '}',
           * taking into account that other structures can be nested in it.
           */
          if (c == opposite) {
            if (nested == 0)
              ok = true;
            else
              nested--;  
          }
          else if (c == *p_src && i > 0)
            nested++;
        }
        else {
          /* Easy case, just match another field delimiter, or the end
           * of the encoding.
           */
          if (c == '"' || c == '}') {
            i--;
            ok = true;
          } 
        }
      }

      if (ok == false) {
        //XXX DLOG("MDLOSX", "Can't find the field encoding starting at %d", p_src - src);
        goto bails;
      }

      if (opposite == '}' || opposite == ')') {
        char buf[MAX_ENCODE_LEN];
        char buf2[MAX_ENCODE_LEN];
   
        strncpy(buf, p_src, MIN(sizeof buf, i));
        buf[MIN(sizeof buf, i)] = '\0';        
     
        if (!undecorate_struct_type(buf, buf2, sizeof buf2, NULL, 0, NULL)) {
          // XXX DLOG("MDLOSX", "Can't un-decode the field encoding '%s'", buf);
          goto bails;
        }

        len = strlen(buf2); 
        field->type = (char *)malloc((sizeof(char) * len) + 1);
        ASSERT_ALLOC(field->type);
        strncpy(field->type, buf2, len);
        field->type[len] = '\0';
      }
      else {
        field->type = (char *)malloc((sizeof(char) * i) + 1);
        ASSERT_ALLOC(field->type);
        strncpy(field->type, p_src, i);
        field->type[i] = '\0';
        len = i;
      }

      strncpy(p_dst, field->type, len);

      p_src += i;
      p_dst += len;
    }
  }

  *p_dst = '\0';
  if (out_fields_count != NULL)
    *out_fields_count = field_idx;
  return true;

bails:
  /* Free what we allocated! */
  for (i = 0; i < field_idx; i++) {
    free(fields[i].name);
    free(fields[i].type);
  }
  return false;
}

static bs_element_retval_t default_func_retval = 
  { "v", BS_CARRAY_ARG_UNDEFINED, -1, false }; 

static bool 
_bs_parse(const char *path, char **loaded_paths, 
          bs_parse_options_t options, bs_parse_callback_t callback, 
          void *context, char **error)
{
  xmlTextReaderPtr reader;
  bs_element_function_t *func;
  bs_element_class_t *klass;
  bs_element_method_t *method;
  unsigned int i;
#define MAX_ARGS 128
  bs_element_arg_t args[MAX_ARGS];
  char *protocol_name;
  int func_ptr_arg_depth;
  bs_element_function_pointer_t *func_ptr;
  bool success;

  if (callback == NULL)
    return false;

  for (i = 0; i < PATH_MAX; i++) {
    char *p = loaded_paths[i];
    if (p == NULL) {
      loaded_paths[i] = strdup(path);
      break;
    }
    else if (strcmp(p, path) == 0) {
      /* already loaded */
      return true;
    }
  }  
  
  //printf("parsing %s\n", path);

#define BAIL(fmt, args...)                      \
  do {                                          \
    if (error != NULL) {                        \
      char buf[1024];                           \
      snprintf(buf, sizeof buf,                 \
               "%s:%ld - "fmt, path,            \
               xmlGetLineNo(xmlTextReaderCurrentNode(reader)), \
               ##args);                         \
      *error = strdup(buf);                     \
    }                                           \
    success = false;                            \
    goto bails;                                 \
  }                                             \
  while (0)

#define CHECK_ATTRIBUTE(a, name)                        \
  do {                                                  \
    if (a == NULL)                                      \
      BAIL("expected attribute `%s' for element `%s'",  \
           name, xmlTextReaderConstName(reader));       \
    if (*a == '\0') {                                   \
      free(a);                                          \
      BAIL("empty attribute `%s' for element `%s'",     \
           name, xmlTextReaderConstName(reader));       \
    }                                                   \
  } while (0)                                           \

  //DLOG("MDLOSX", "Loading bridge support file `%s'", path);
  
  reader = xmlNewTextReaderFilename(path);
  if (reader == NULL)
    BAIL("cannot create XML text reader for file at path `%s'", path);

  func = NULL;
  func_ptr = NULL;
  klass = NULL;
  method = NULL;
  protocol_name = NULL;

  func_ptr_arg_depth = -1;

  while (true) {
    const char *name;
    unsigned int namelen;
    int node_type = -1;
    bool eof;
    struct bs_xml_atom *atom;
    void *bs_element;
    bs_element_type_t bs_element_type;

    do {
      int retval = xmlTextReaderRead(reader);
      if (retval == 0) {
        eof = true;
        break;
      }
      else if (retval < 0)
        BAIL("parsing error: %d", retval);

      node_type = xmlTextReaderNodeType(reader);
    }
    while (node_type != XML_READER_TYPE_ELEMENT 
           && node_type != XML_READER_TYPE_END_ELEMENT);    
    
    if (eof)
      break;

    name = (const char *)xmlTextReaderConstName(reader);
    namelen = strlen(name); 

    bs_element = NULL;

    if (node_type == XML_READER_TYPE_ELEMENT) {
      atom = bs_xml_element(name, namelen);
      if (atom == NULL)
        continue;

      switch (atom->val) {
        case BS_XML_DEPENDS_ON:
        {
          char *depends_on_path;
          char bs_path[PATH_MAX];
          bool bs_path_found;
          
          depends_on_path = get_attribute(reader, "path");
          CHECK_ATTRIBUTE(depends_on_path, "path");
          
          bs_path_found = bs_find_path(depends_on_path, bs_path, 
                                       sizeof bs_path);
          free(depends_on_path);
          if (bs_path_found) {
            if (!_bs_parse(bs_path, loaded_paths, options, callback, context, 
                           error))
              return false;
          }
          break;
        }

        case BS_XML_CONSTANT: 
        { 
          bs_element_constant_t *bs_const;
          char *const_name;
          char *const_type;

          const_name = get_attribute(reader, "name");
          CHECK_ATTRIBUTE(const_name, "name");
          const_type = get_type_attribute(reader);
          CHECK_ATTRIBUTE(const_type, "type");

          bs_const = (bs_element_constant_t *)
            malloc(sizeof(bs_element_constant_t));
          ASSERT_ALLOC(bs_const);

          bs_const->name = const_name;
          bs_const->type = const_type;
          bs_const->ignore = false;
          bs_const->suggestion = NULL;
          bs_const->magic_cookie = get_boolean_attribute(reader,
            "magic_cookie", false);

          bs_element = bs_const;
          bs_element_type = BS_ELEMENT_CONSTANT;
          break;
        }

        case BS_XML_STRING_CONSTANT:
        {
          bs_element_string_constant_t *bs_strconst;
          char *strconst_name;
          char *strconst_value;

          strconst_name = get_attribute(reader, "name");
          CHECK_ATTRIBUTE(strconst_name, "name");
          strconst_value = get_attribute(reader, "value");
          CHECK_ATTRIBUTE(strconst_value, "value");

          bs_strconst = (bs_element_string_constant_t *)
            malloc(sizeof(bs_element_string_constant_t));
          ASSERT_ALLOC(bs_strconst);

          bs_strconst->name = strconst_name;
          bs_strconst->value = strconst_value;
          bs_strconst->nsstring = get_boolean_attribute(reader, "nsstring", 
            false);

          bs_element = bs_strconst;
          bs_element_type = BS_ELEMENT_STRING_CONSTANT;
          break;
        }

        case BS_XML_ENUM: 
        { 
          char *enum_name;
          char *enum_value;        

          enum_name = get_attribute(reader, "name");
          CHECK_ATTRIBUTE(enum_name, "name");

          enum_value = get_attribute(reader, "value");
#if __LP64__
          if (enum_value == NULL)
            enum_value = get_attribute(reader, "value64");
#endif

#if BYTE_ORDER == BIG_ENDIAN
# define BYTE_ORDER_VALUE_ATTR_NAME "be_value"
#else
# define BYTE_ORDER_VALUE_ATTR_NAME "le_value"
#endif

          if (enum_value == NULL)
            enum_value = get_attribute(reader, BYTE_ORDER_VALUE_ATTR_NAME); 
          
          if (enum_value != NULL) {
            bs_element_enum_t *bs_enum;
   
            bs_enum = (bs_element_enum_t *)malloc(sizeof(bs_element_enum_t));
            ASSERT_ALLOC(bs_enum);

            bs_enum->name = enum_name;
            bs_enum->value = enum_value;
            bs_enum->ignore = get_boolean_attribute(reader, "ignore", false);
            bs_enum->suggestion = get_attribute(reader, "suggestion");
            
            bs_element = bs_enum;
            bs_element_type = BS_ELEMENT_ENUM;
          }
          break;
        }

        case BS_XML_STRUCT: 
        {
          bs_element_struct_t *bs_struct;
          char *struct_decorated_type;
          char *struct_name;
          char type[MAX_ENCODE_LEN];
          bs_element_struct_field_t fields[128];
          int field_count;

          struct_decorated_type = get_type_attribute(reader);
          CHECK_ATTRIBUTE(struct_decorated_type, "type");
          struct_name = get_attribute(reader, "name");
          CHECK_ATTRIBUTE(struct_name, "name");

          if (!undecorate_struct_type(struct_decorated_type, type, 
                                      MAX_ENCODE_LEN, fields, 128, 
                                      &field_count)) {
            BAIL("Can't handle structure '%s' with type '%s'", 
                 name, struct_decorated_type);
          }

          free(struct_decorated_type);

          bs_struct = 
            (bs_element_struct_t *)malloc(sizeof(bs_element_struct_t));
          ASSERT_ALLOC(bs_struct);

          bs_struct->name = struct_name;
          bs_struct->type = strdup(type);
          
          bs_struct->fields = (bs_element_struct_field_t *)malloc(
            sizeof(bs_element_struct_field_t) * field_count);
          ASSERT_ALLOC(bs_struct->fields);
          memcpy(bs_struct->fields, fields, 
                 sizeof(bs_element_struct_field_t) * field_count); 
          
          bs_struct->fields_count = field_count;
          bs_struct->opaque = get_boolean_attribute(reader, "opaque", false);

          bs_element = bs_struct;
          bs_element_type = BS_ELEMENT_STRUCT;
          break;
        }

        case BS_XML_OPAQUE:
        {
          bs_element_opaque_t *bs_opaque;
          char *opaque_name;
          char *opaque_type;

          opaque_name = get_attribute(reader, "name");
          CHECK_ATTRIBUTE(opaque_name, "name");
          opaque_type = get_type_attribute(reader);
          CHECK_ATTRIBUTE(opaque_type, "type");

          bs_opaque = 
            (bs_element_opaque_t *)malloc(sizeof(bs_element_opaque_t));
          ASSERT_ALLOC(bs_opaque);
          
          bs_opaque->name = opaque_name;
          bs_opaque->type = opaque_type;
          
          bs_element = bs_opaque;
          bs_element_type = BS_ELEMENT_OPAQUE;
          break;
        }
        
        case BS_XML_CFTYPE:
        {
          bs_element_cftype_t *bs_cftype;
          char *cftype_name;
          char *cftype_type;
          char *cftype_gettypeid_func_name;

          cftype_name = get_attribute(reader, "name");
          CHECK_ATTRIBUTE(cftype_name, "name");
          cftype_type = get_type_attribute(reader);
          CHECK_ATTRIBUTE(cftype_type, "type");

          bs_cftype = 
            (bs_element_cftype_t *)malloc(sizeof(bs_element_cftype_t));
          ASSERT_ALLOC(bs_cftype);

          bs_cftype->name = cftype_name;
          bs_cftype->type = cftype_type;

          cftype_gettypeid_func_name = get_attribute(reader, "gettypeid_func");
          if (cftype_gettypeid_func_name != NULL) {
            void *sym;

            sym = dlsym(RTLD_DEFAULT, cftype_gettypeid_func_name);
            if (sym == NULL) {
              BAIL("cannot locate gettypeid_func function `%s'",
                   cftype_gettypeid_func_name);
            }
            else {
              CFTypeID (*cb)(void) = sym;
              bs_cftype->type_id = (*cb)();
            }
          }
          else {
            bs_cftype->type_id = 0;
          }

          bs_cftype->tollfree = get_attribute(reader, "tollfree");

          bs_element = bs_cftype;
          bs_element_type = BS_ELEMENT_CFTYPE;
          break;
        }
        
        case BS_XML_INFORMAL_PROTOCOL: 
        {
          protocol_name = get_attribute(reader, "name");
          CHECK_ATTRIBUTE(protocol_name, "name");
          break;
        }

        case BS_XML_FUNCTION: 
        {
          char *func_name;
          
          func_name = get_attribute(reader, "name");
          CHECK_ATTRIBUTE(func_name, "name");

          func = 
            (bs_element_function_t *)malloc(sizeof(bs_element_function_t));
          ASSERT_ALLOC(func);

          func->name = func_name;
          func->variadic = get_boolean_attribute(reader, "variadic", false);
          func->args_count = 0;
          func->args = NULL;
          func->retval = &default_func_retval;
          break;
        }

        case BS_XML_FUNCTION_ALIAS: 
        {
          bs_element_function_alias_t *bs_func_alias;
          char *alias_name;
          char *alias_original;

          alias_name = get_attribute(reader, "name"); 
          CHECK_ATTRIBUTE(alias_name, "name");
          alias_original = get_attribute(reader, "original");
          CHECK_ATTRIBUTE(alias_original, "original");

          bs_func_alias = (bs_element_function_alias_t *)malloc(
            sizeof(bs_element_function_alias_t));
          ASSERT_ALLOC(bs_func_alias);
          
          bs_func_alias->name = alias_name;
          bs_func_alias->original = alias_original;

          bs_element = bs_func_alias;
          bs_element_type = BS_ELEMENT_FUNCTION_ALIAS;
          break;
        }

        case BS_XML_CLASS: 
        {
          char *class_name;
          
          class_name = get_attribute(reader, "name");
          CHECK_ATTRIBUTE(class_name, "name");
        
          klass = (bs_element_class_t *)malloc(sizeof(bs_element_class_t));
          ASSERT_ALLOC(klass);
            
          klass->name = class_name;
          klass->class_methods = klass->instance_methods = NULL;
          klass->class_methods_count = klass->instance_methods_count = 0;
          break;
        }

        case BS_XML_ARG:
        {
          if (func != NULL || method != NULL || func_ptr != NULL) {
            bs_element_arg_t *bs_arg; 
            unsigned *argc;

            argc = func_ptr != NULL
              ? &func_ptr->args_count
              : func != NULL 
                ? &func->args_count 
                : &method->args_count;

            if (*argc >= MAX_ARGS) {
              if (func_ptr != NULL)
                BAIL("maximum number of arguments (%d) reached " \
                     "for function pointer", MAX_ARGS);
              else if (func != NULL)
                BAIL("maximum number of arguments (%d) reached " \
                     "for function '%s'", MAX_ARGS, func->name);
              else
                BAIL("maximum number of arguments (%d) reached " \
                     "for method '%s'", MAX_ARGS, (char *)method->name);
            } 

            bs_arg = &args[(*argc)++];

            if (method != NULL && func_ptr == NULL) {
              char *index = get_attribute(reader, "index");
              CHECK_ATTRIBUTE(index, "index");
              bs_arg->index = strtol(index, NULL, 10);
              free(index);
            }
            else {
              bs_arg->index = -1;
            }
            
            get_type_modifier_attribute(reader, &bs_arg->type_modifier);

#if __LP64__
            bs_arg->sel_of_type = get_attribute(reader, "sel_of_type64");
            if (bs_arg->sel_of_type == NULL)
#endif
              bs_arg->sel_of_type = get_attribute(reader, "sel_of_type");

            bs_arg->printf_format = get_boolean_attribute(reader, 
                "printf_format", false); 
            bs_arg->null_accepted = get_boolean_attribute(reader, 
                "null_accepted", true);
            get_c_ary_type_attribute(reader, 
                &bs_arg->carray_type, &bs_arg->carray_type_value); 
  
            bs_arg->type = get_type_attribute(reader);

            if (get_boolean_attribute(reader, "function_pointer", false)) {
              if (func_ptr != NULL) /* FIXME */
                BAIL("internal error, nested function pointers detected");
              bs_arg->function_pointer = (bs_element_function_pointer_t *)
                calloc(1, sizeof(bs_element_function_pointer_t));
              ASSERT_ALLOC(bs_arg->function_pointer);
              func_ptr = bs_arg->function_pointer;
              func_ptr_arg_depth = xmlTextReaderDepth(reader);
            }
          }
          else {
            BAIL("argument defined outside of a " \
                 "function/method/function_pointer");
          }
          break;
        }

        case BS_XML_RETVAL: 
        {
          if (func != NULL || method != NULL || func_ptr != NULL) {
            bs_element_retval_t *bs_retval;  

            if (func_ptr != NULL) {
              if (func_ptr->retval != NULL)
                BAIL("function pointer return value defined more than once");
            }
            else if (func != NULL) {
              if (func->retval != NULL && func->retval != &default_func_retval)
                BAIL("function '%s' return value defined more than once", 
                     func->name);
            }
            else if (method != NULL) {
              if (method->retval != NULL)
                BAIL("method '%s' return value defined more than once", 
                     (char *)method->name);
            }
    
            bs_retval = 
              (bs_element_retval_t *)malloc(sizeof(bs_element_retval_t));
            ASSERT_ALLOC(bs_retval);

            get_c_ary_type_attribute(reader, &bs_retval->carray_type, 
              &bs_retval->carray_type_value);

            bs_retval->type = get_type_attribute(reader);
            if (bs_retval->type != NULL)
              bs_retval->already_retained = 
                get_boolean_attribute(reader, "already_retained", false);

            if (func_ptr != NULL) {
              if (bs_retval->type != NULL) {
                func_ptr->retval = bs_retval;
              }
              else {
                free(bs_retval);
                BAIL("function pointer return value defined without type"); 
              }
            }
            else if (func != NULL) {
              if (bs_retval->type != NULL) {
                func->retval = bs_retval;
              }
              else {
                free(bs_retval);
                BAIL("function '%s' return value defined without type", 
                     func->name);
              }
            }
            else {
              method->retval = bs_retval;
            }

            if (get_boolean_attribute(reader, "function_pointer", false)) {
              if (func_ptr != NULL) /* FIXME */
                BAIL("internal error, nested function pointers detected");
              bs_retval->function_pointer = (bs_element_function_pointer_t *)
                calloc(1, sizeof(bs_element_function_pointer_t));
              ASSERT_ALLOC(bs_retval->function_pointer);
              func_ptr = bs_retval->function_pointer;
              func_ptr_arg_depth = xmlTextReaderDepth(reader);
            }
          }
          else {
            BAIL("return value defined outside a function/method");
          }
          break;
        }

        case BS_XML_METHOD: 
        {
          if (protocol_name != NULL) {
            bs_element_informal_protocol_method_t *bs_informal_method;
            char *selector;
            char *method_type;

            selector = get_attribute(reader, "selector");
            CHECK_ATTRIBUTE(selector, "selector");
            
            method_type = get_type_attribute(reader);
            CHECK_ATTRIBUTE(method_type, "type");

            bs_informal_method = (bs_element_informal_protocol_method_t *)
              malloc(sizeof(bs_element_informal_protocol_method_t));
            ASSERT_ALLOC(bs_informal_method);

            bs_informal_method->name = sel_registerName(selector);
            bs_informal_method->class_method = 
              get_boolean_attribute(reader, "class_method", false);
            bs_informal_method->type = method_type;
            bs_informal_method->protocol_name = protocol_name;

            bs_element = bs_informal_method;
            bs_element_type = BS_ELEMENT_INFORMAL_PROTOCOL_METHOD;
          }
          else if (klass != NULL) {  
            char *selector;

            selector = get_attribute(reader, "selector");
            CHECK_ATTRIBUTE(selector, "selector");

            method = 
              (bs_element_method_t *)malloc(sizeof(bs_element_method_t));
            ASSERT_ALLOC(method);

            method->name = sel_registerName(selector);
            method->class_method = 
              get_boolean_attribute(reader, "class_method", false);
            method->variadic = 
              get_boolean_attribute(reader, "variadic", false);
            method->ignore = 
              get_boolean_attribute(reader, "ignore", false);
            method->suggestion = get_attribute(reader, "suggestion");
            method->args_count = 0;
            method->args = NULL;
            method->retval = NULL;
          }
          else {
            BAIL("method defined outside a class or informal protocol");
          }
          break;
        }
      }
    }
    else if (node_type == XML_READER_TYPE_END_ELEMENT) {
      atom = bs_xml_element(name, namelen);
      if (atom == NULL)
        continue;

      switch (atom->val) {
        case BS_XML_INFORMAL_PROTOCOL: 
        {
          protocol_name = NULL;
          break;
        }

        case BS_XML_RETVAL:
        case BS_XML_ARG: 
        {
          if (func_ptr != NULL 
              && func_ptr_arg_depth == xmlTextReaderDepth(reader)) {
            
            if (func_ptr->args_count > 0) {
              size_t len;
      
              len = sizeof(bs_element_arg_t) * func_ptr->args_count;
              func_ptr->args = (bs_element_arg_t *)malloc(len);
              ASSERT_ALLOC(func_ptr->args);
              memcpy(func_ptr->args, args, len);
            }
            else {
              func_ptr->args = NULL;
            }
                        
            func_ptr = NULL;
            func_ptr_arg_depth = -1;
          }
          break;
        }
 
        case BS_XML_FUNCTION: 
        { 
          for (i = 0; i < func->args_count; i++) {
            if (args[i].type == NULL)
              BAIL("function '%s' argument #%d type not provided", 
                   func->name, i);
          }
    
          if (func->args_count > 0) {
            size_t len;
    
            len = sizeof(bs_element_arg_t) * func->args_count;
            func->args = (bs_element_arg_t *)malloc(len);
            ASSERT_ALLOC(func->args);
            memcpy(func->args, args, len);
          }
          if (func->retval == NULL || func->retval == &default_func_retval) {
            func->retval = 
              (bs_element_retval_t *)malloc(sizeof(bs_element_retval_t));
            memcpy(func->retval, &default_func_retval, 
                   sizeof(bs_element_retval_t));
          }

          bs_element = func;
          bs_element_type = BS_ELEMENT_FUNCTION;
          func = NULL;
          break;
        }

        case BS_XML_METHOD: 
        {
          bs_element_method_t *methods;
          unsigned *methods_count;
          
          if (method->args_count > 0) {
            size_t len;
      
            len = sizeof(bs_element_arg_t) * method->args_count;
            method->args = (bs_element_arg_t *)malloc(len);
            ASSERT_ALLOC(method->args);
            memcpy(method->args, args, len);
          }

          methods = method->class_method 
            ? klass->class_methods : klass->instance_methods;

          methods_count = method->class_method
            ? &klass->class_methods_count : &klass->instance_methods_count;

          if (methods == NULL) {
            methods = (bs_element_method_t *)malloc(
              sizeof(bs_element_method_t) * (*methods_count + 1));
          }
          else {
            methods = (bs_element_method_t *)realloc(methods, 
              sizeof(bs_element_method_t) * (*methods_count + 1));
          }
          ASSERT_ALLOC(methods);

    //      methods[*methods_count] = method;
    // FIXME this is inefficient
          memcpy(&methods[*methods_count], method, 
            sizeof(bs_element_method_t));
          free(method);

          (*methods_count)++;
          
          if (method->class_method)
            klass->class_methods = methods;
          else
            klass->instance_methods = methods;
          
          method = NULL;
          break;
        }

        case BS_XML_CLASS: 
        {
          bs_element = klass;
          bs_element_type = BS_ELEMENT_CLASS;
          klass = NULL;
          break;
        }
      }
    }

    if (bs_element != NULL)
      (*callback)(path, bs_element_type, bs_element, context);
  }
  
  success = true;

bails:
  xmlFreeTextReader(reader);
  
  return success;
}

bool 
bs_parse(const char *path, bs_parse_options_t options, 
         bs_parse_callback_t callback, void *context, char **error)
{
  char **loaded_paths;
  bool status;
  unsigned i;
  
  loaded_paths = (char **)alloca(sizeof(char *) * PATH_MAX);
  ASSERT_ALLOC(loaded_paths);
  memset(loaded_paths, 0, PATH_MAX);
  
  status = _bs_parse(path, loaded_paths, options, callback, context, error);

  for (i = 0; i < PATH_MAX && loaded_paths[i] != NULL; i++)
    free(loaded_paths[i]);

  return status;
}

void 
bs_element_free(bs_element_type_t type, void *value)
{
  /* TODO */
}

#if 0
struct bs_register_entry {
  bs_parse_callback_t *callback;
  void *context;
};

static struct bs_register_entry **bs_register_entries = NULL;
static unsigned bs_register_entries_count = 0;
static bs_register_token_t tokens = 0;

bs_register_token_t 
bs_register(bs_parse_callback_t *callback, void *context)
{
  struct bs_register_entry *entry;

  entry = (struct bs_register_entry *)malloc(sizeof(struct bs_register_entry));
  ASSERT_ALLOC(entry);
  
  entry->callback = callback;
  entry->context = context;

  if (bs_register_entries == NULL) {
    assert(bs_register_entries_count == 0);
    bs_register_entries = (struct bs_register_entry **)
      malloc(sizeof(struct bs_register_entry *));
  }
  else {
    assert(bs_register_entries_count > 0);
    
  }
}
#endif

bs_register_token_t 
bs_register(bs_parse_callback_t *callback, void *context)
{
  /* TODO */
  return 1;
}

void 
bs_unregister(bs_register_token_t token)
{
  /* TODO */
}

void 
bs_notify(bs_element_type_t type, void *value)
{
  /* TODO */
}
