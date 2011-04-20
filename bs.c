/*  
 *  Copyright (c) 2008-2011, Apple Inc. All rights reserved.
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

#if !defined(MACRUBY_STATIC)

#include "bs.h"

#include <libxml/xmlreader.h>
#include <dlfcn.h>
#include <libgen.h>
#include <unistd.h>

#include "bs_lex.h"

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

static inline const char *
_bs_main_bundle_bs_path(void)
{
  static bool done = false;
  static char *path = NULL;
  /* XXX not thread-safe */
  if (!done) {
    CFBundleRef bundle;

    done = true;
    bundle = CFBundleGetMainBundle();
    if (bundle != NULL) {
      CFURLRef url;

      url = CFBundleCopyResourceURL(bundle, CFSTR("BridgeSupport"), 
                                    NULL, NULL);
      if (url != NULL) {
        CFStringRef str = CFURLCopyPath(url);
        path = (char *)malloc(sizeof(char) * PATH_MAX);
	ASSERT_ALLOC(path);
        CFStringGetFileSystemRepresentation(str, path, PATH_MAX);
        CFRelease(str);
        CFRelease(url);
      }
    }
  }
  return path;
}

static bool
_bs_find_path(const char *framework_path, char *path, const size_t path_len,
              const char *ext)
{
  const char *main_bundle_bs_path;
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

  main_bundle_bs_path = _bs_main_bundle_bs_path();
  if (main_bundle_bs_path != NULL) {
    snprintf(path, path_len, "%s/%s.%s", main_bundle_bs_path,
             framework_name, ext);
    CHECK_IF_EXISTS();
  }

  snprintf(path, path_len, "%s/Resources/BridgeSupport/%s.%s",
           framework_path, framework_name, ext);
  CHECK_IF_EXISTS();

  home = getenv("HOME");
  if (home != NULL) {
    snprintf(path, path_len, "%s/Library/BridgeSupport/%s.%s",
      home, framework_name, ext);
    CHECK_IF_EXISTS();
  }
  
  snprintf(path, path_len, "/Library/BridgeSupport/%s.%s",
    framework_name, ext);
  CHECK_IF_EXISTS();

  snprintf(path, path_len, "/System/Library/BridgeSupport/%s.%s",
    framework_name, ext);
  CHECK_IF_EXISTS();

#undef CHECK_IF_EXISTS

  free(framework_name);
  return false;  
}

bool 
bs_find_path(const char *framework_path, char *path, const size_t path_len)
{
  return _bs_find_path(framework_path, path, path_len, "bridgesupport");
}

static inline char *
get_attribute(xmlTextReaderPtr reader, const char *name)
{
  return (char *)xmlTextReaderGetAttribute(reader, (const xmlChar *)name);
}

static inline char *
get_type64_attribute(xmlTextReaderPtr reader)
{
  return (char *)xmlTextReaderGetAttribute(reader, (xmlChar *)"type64");
}

static inline char *
get_type_attribute(xmlTextReaderPtr reader)
{
#if __LP64__
  char *value = get_type64_attribute(reader);
  if (value != NULL)
    return value;
#endif
  return (char *)xmlTextReaderGetAttribute(reader, (xmlChar *)"type");
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
  free(type_modifier);
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

#define MAX_ENCODE_LEN 4096

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
      fprintf(stderr, "Can't find the end of field delimiter starting at %d\n", (int)(p_src - src));
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
        fprintf(stderr, "Can't find the field encoding starting at %d\n", (int)(p_src - src));
        goto bails;
      }

      if (opposite == '}' || opposite == ')') {
        char buf[MAX_ENCODE_LEN];
        char buf2[MAX_ENCODE_LEN];
 
        strncpy(buf, p_src, MIN(sizeof buf, i));
        buf[MIN(sizeof buf, i)] = '\0';        
     
        if (!undecorate_struct_type(buf, buf2, sizeof buf2, NULL, 0, NULL)) {
          fprintf(stderr, "Can't un-decode the field encoding '%s'\n", buf);
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

struct _bs_parser {
  unsigned int version_number;
  CFMutableArrayRef loaded_paths; 
};

bs_parser_t *
bs_parser_new(void)
{
  struct _bs_parser *parser;

  parser = (struct _bs_parser *)malloc(sizeof(struct _bs_parser));
  ASSERT_ALLOC(parser);
  parser->loaded_paths = 
    CFArrayCreateMutable(kCFAllocatorMalloc, 0, &kCFTypeArrayCallBacks);

  return parser;
}

void
bs_parser_free(bs_parser_t *parser)
{
  CFRelease(parser->loaded_paths);
  free(parser);
}

bool 
bs_parser_parse(bs_parser_t *parser, const char *path, 
                const char *framework_path, bs_parse_options_t options, 
                bs_parse_callback_t callback, void *context, char **error)
{
  xmlTextReaderPtr reader;
  bs_element_function_t *func;
  bs_element_class_t *klass;
  bs_element_method_t *method;
  unsigned int i;
#define MAX_ARGS 128
  bs_element_arg_t args[MAX_ARGS];
  bs_element_arg_t fptr_args[MAX_ARGS];
  char *protocol_name = NULL;
  int func_ptr_arg_depth;
  bs_element_function_pointer_t *func_ptr;
  bool success;
  CFStringRef cf_path;
  bool nested_func_ptr;
  unsigned int version_number = 0;

  if (callback == NULL)
    return false;

  /* check if the given framework path has not been loaded already */
  cf_path = CFStringCreateWithFileSystemRepresentation(kCFAllocatorMalloc, 
    path);
  CFMakeCollectable(cf_path);
  for (unsigned i = 0, count = CFArrayGetCount(parser->loaded_paths);
       i < count; i++) {
    CFStringRef s = CFArrayGetValueAtIndex(parser->loaded_paths, i);
    if (CFStringCompare(cf_path, s, kCFCompareCaseInsensitive)
        == kCFCompareEqualTo) {
      /* already loaded */
      return true;
    }
  }

  CFArrayAppendValue(parser->loaded_paths, cf_path);

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

#if __LP64__
# define CHECK_TYPE_ATTRIBUTE(var) CHECK_ATTRIBUTE(var, "type")
#else
# define CHECK_TYPE_ATTRIBUTE(var) \
    if (var == NULL && get_type64_attribute(reader) != NULL) { \
	break; \
    } \
    CHECK_ATTRIBUTE(var, "type")
#endif

#define CHECK_ATTRIBUTE_CAN_BE_EMPTY(a, name) \
  CHECK_ATTRIBUTE0(a, name, true)

#define CHECK_ATTRIBUTE(a, name) \
  CHECK_ATTRIBUTE0(a, name, false)

#define CHECK_ATTRIBUTE0(a, name, can_be_empty)         \
  do {                                                  \
    if (a == NULL)                                      \
      BAIL("expected attribute `%s' for element `%s'",  \
           name, xmlTextReaderConstName(reader));       \
    if (!can_be_empty && *a == '\0') {                  \
      free(a);                                          \
      BAIL("empty attribute `%s' for element `%s'",     \
           name, xmlTextReaderConstName(reader));       \
    }                                                   \
  } while (0)                                           \

  reader = xmlNewTextReaderFilename(path);
  if (reader == NULL)
    BAIL("cannot create XML text reader for file at path `%s'", path);

  func = NULL;
  func_ptr = NULL;
  func_ptr_arg_depth = -1;
  nested_func_ptr = false;
  klass = NULL;
  method = NULL;
  protocol_name = NULL;

  while (true) {
    const char *name;
    unsigned int namelen;
    int node_type = -1;
    bool eof = false;
    struct bs_xml_atom *atom;
    void *bs_element;
    bs_element_type_t bs_element_type = 0;

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

    atom = bs_xml_element(name, namelen);
    if (atom == NULL) {
      // TODO: we should include the "signatures" string into the gperf
      // function.
      if (version_number == 0 && strcmp(name, "signatures") == 0) {
        char *str = get_attribute(reader, "version");
        if (str != NULL) {
          char *p = strchr(str, '.');
          if (p != NULL) {
            *p = '\0';
            int major = atoi(str);
            int minor = atoi(&p[1]);
            assert(major < 10 && minor < 10);
            version_number = (major * 10) + minor;
            parser->version_number = version_number;
          }
          free(str);
        }
      }
      continue;
    }

    if (nested_func_ptr) {
      // FIXME: elements nesting function_pointers aren't supported yet by the
      // parser, so we just ignore them.
      if (node_type == XML_READER_TYPE_END_ELEMENT
          && (atom->val == BS_XML_FUNCTION || atom->val == BS_XML_METHOD)) {
        nested_func_ptr = false;
      }
      continue;
    }

    if (node_type == XML_READER_TYPE_ELEMENT) {
      switch (atom->val) {
        case BS_XML_DEPENDS_ON:
        {
          char *depends_on_path;
          char bs_path[PATH_MAX];
          bool bs_path_found;
          
          depends_on_path = get_attribute(reader, "path");
          CHECK_ATTRIBUTE(depends_on_path, "path");

//printf("depends of %s\n", depends_on_path);
          
          bs_path_found = bs_find_path(depends_on_path, bs_path, 
                                       sizeof bs_path);
          if (bs_path_found) {
            if (!bs_parser_parse(parser, bs_path, depends_on_path, options, 
                                 callback, context, error)) {
              free(depends_on_path);
              return false;
	    }
          }
          free(depends_on_path);
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
          CHECK_TYPE_ATTRIBUTE(const_type);

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
          CHECK_ATTRIBUTE_CAN_BE_EMPTY(strconst_value, "value");

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

#if __LP64__
	  enum_value = get_attribute(reader, "value64");
	  if (enum_value == NULL)
#endif
	    enum_value = get_attribute(reader, "value");

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
          CHECK_TYPE_ATTRIBUTE(struct_decorated_type);
          struct_name = get_attribute(reader, "name");
          CHECK_ATTRIBUTE(struct_name, "name");

          if (!undecorate_struct_type(struct_decorated_type, type, 
                                      sizeof type, fields, 128, 
                                      &field_count)) {
            BAIL("Can't handle structure '%s' with type '%s'", 
                 struct_name, struct_decorated_type);
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
          CHECK_TYPE_ATTRIBUTE(opaque_type);

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

          cftype_name = get_attribute(reader, "name");
          CHECK_ATTRIBUTE(cftype_name, "name");
          cftype_type = get_type_attribute(reader);
          CHECK_TYPE_ATTRIBUTE(cftype_type);

          bs_cftype = 
            (bs_element_cftype_t *)malloc(sizeof(bs_element_cftype_t));
          ASSERT_ALLOC(bs_cftype);

          bs_cftype->name = cftype_name;
          bs_cftype->type = cftype_type;

#if 1
          /* the type_id field isn't used in MacRuby */
          bs_cftype->type_id = 0;
#else
          char *cftype_gettypeid_func_name;
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
#endif

          bs_cftype->tollfree = get_attribute(reader, "tollfree");

          bs_element = bs_cftype;
          bs_element_type = BS_ELEMENT_CFTYPE;
          break;
        }
        
        case BS_XML_INFORMAL_PROTOCOL: 
        {
	  if (protocol_name != NULL)
	    free(protocol_name);
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
          func->retval = NULL;

          if (xmlTextReaderIsEmptyElement(reader)) {
            bs_element = func;
            bs_element_type = BS_ELEMENT_FUNCTION;
            func = NULL;
          }
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

	    bs_element_arg_t *args_from =
		(func_ptr == NULL ? args : fptr_args);
	    bs_arg = &args_from[(*argc)++];

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
              if (func_ptr != NULL) {
                func_ptr = NULL; 
		nested_func_ptr = true;
		break;
	      }
              bs_arg->function_pointer = (bs_element_function_pointer_t *)
                calloc(1, sizeof(bs_element_function_pointer_t));
              ASSERT_ALLOC(bs_arg->function_pointer);
              func_ptr = bs_arg->function_pointer;
              func_ptr_arg_depth = xmlTextReaderDepth(reader);
            }
	    else {
              bs_arg->function_pointer = NULL;
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
              if (func->retval != NULL)
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
#if !defined(__LP64__)
		if (get_type64_attribute(reader) != NULL) {
		    // The function has no 32-bit return value type and we
		    // run in 32-bit mode. We just ignore it.
		    func = NULL;
		    break;
		}
#endif
                BAIL("function '%s' return value defined without type", 
                     func->name);
              }
            }
            else {
              method->retval = bs_retval;
            }

            if (get_boolean_attribute(reader, "function_pointer", false)) {
              if (func_ptr != NULL) {
                func_ptr = NULL; 
		nested_func_ptr = true;
		break;
              }
              bs_retval->function_pointer = (bs_element_function_pointer_t *)
                calloc(1, sizeof(bs_element_function_pointer_t));
              ASSERT_ALLOC(bs_retval->function_pointer);
              func_ptr = bs_retval->function_pointer;
              func_ptr_arg_depth = xmlTextReaderDepth(reader);
            }
	    else {
              bs_retval->function_pointer = NULL;
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
            CHECK_TYPE_ATTRIBUTE(method_type);

            bs_informal_method = (bs_element_informal_protocol_method_t *)
              malloc(sizeof(bs_element_informal_protocol_method_t));
            ASSERT_ALLOC(bs_informal_method);

            bs_informal_method->name = sel_registerName(selector);
	    free(selector);
            bs_informal_method->class_method = 
              get_boolean_attribute(reader, "class_method", false);
            bs_informal_method->type = method_type;
            bs_informal_method->protocol_name = strdup(protocol_name);

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
	    free(selector);
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

            if (xmlTextReaderIsEmptyElement(reader)) {
              goto index_method;
            }
          }
          else {
            BAIL("method defined outside a class or informal protocol");
          }
          break;
        }
      }
    }
    else if (node_type == XML_READER_TYPE_END_ELEMENT) {
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

	      bs_element_retval_t *retval = NULL;
	      bs_element_arg_t *arg = NULL;
	      unsigned args_count;

	      if (atom->val == BS_XML_RETVAL) {
		  retval = func != NULL ? func->retval : method->retval;
	      }
	      else {
		  args_count = func != NULL ? func->args_count
		      : method->args_count;
		  arg = &args[args_count - 1];
	      }

              // Determine if we deal with a block or a function pointer.
	      const char *old_type = (retval ? retval->type : arg->type);
              const char lambda_type = *old_type == '@'
		? _MR_C_LAMBDA_BLOCK
		: _MR_C_LAMBDA_FUNCPTR;

	      char tmp_type[1025]; // 3 less to fit <, type and >
	      char new_type[1028];

	      // Function ptr return type
	      strlcpy(tmp_type, func_ptr->retval->type, sizeof(tmp_type));
	      // Function ptr args
	      for (i = 0; i < func_ptr->args_count; i++) {
		  strlcat(tmp_type, fptr_args[i].type, sizeof(tmp_type));
	      }
	      // Clear the final type string
	      memset(new_type, 0, sizeof(new_type));
	      // Append the function pointer type
	      snprintf(new_type, sizeof(new_type), "%c%c%s%c",
		      _MR_C_LAMBDA_B, lambda_type, tmp_type, _MR_C_LAMBDA_E);

	      // Free the old values
	      if (retval) {
		  free(retval->type);
		  retval->type = strdup(new_type);
	      }
	      else {
		  free(arg->type);
		  arg->type = strdup(new_type);
	      }
            
	      if (func_ptr->args_count > 0) {
		  size_t len;
      
		  len = sizeof(bs_element_arg_t) * func_ptr->args_count;
		  func_ptr->args = (bs_element_arg_t *)malloc(len);
		  ASSERT_ALLOC(func_ptr->args);
		  memcpy(func_ptr->args, fptr_args, len);
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
          if (func == NULL) {
            break;
          }
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

index_method:
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

          (*methods_count)++;
          
          if (method->class_method)
            klass->class_methods = methods;
          else
            klass->instance_methods = methods;
         
          free(method);
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
      (*callback)(parser, path, bs_element_type, bs_element, context);
  }
  
  success = true;

bails:
  if (protocol_name != NULL)
    free(protocol_name);

  xmlFreeTextReader(reader);

  if (!success) {
      for (unsigned i = 0, count = CFArrayGetCount(parser->loaded_paths);
	      i < count; i++) {
	  CFStringRef s = CFArrayGetValueAtIndex(parser->loaded_paths, i);
	  if (CFStringCompare(cf_path, s, kCFCompareCaseInsensitive)
		  == kCFCompareEqualTo) {
	      CFArrayRemoveValueAtIndex(parser->loaded_paths, i);
	      break;
	  }
      }
  }

  if (success && options == BS_PARSE_OPTIONS_LOAD_DYLIBS && framework_path != NULL) {
    char buf[PATH_MAX];

    if (_bs_find_path(framework_path, buf, sizeof buf, "dylib")) {
      if (dlopen(buf, RTLD_LAZY) == NULL) {
        if (error != NULL) {
          *error = dlerror();
        }
        success = false;
      }
    }
  }

  return success;
}

unsigned int
bs_parser_current_version_number(bs_parser_t *parser)
{
  return parser->version_number;
}

#define SAFE_FREE(x) do { if ((x) != NULL) free(x); } while (0)

static void bs_free_retval(bs_element_retval_t *bs_retval);
static void bs_free_arg(bs_element_arg_t *bs_arg);

static void
bs_free_function_pointer(bs_element_function_pointer_t *bs_func_ptr)
{
  if (bs_func_ptr != NULL) {
    unsigned i;
    for (i = 0; i < bs_func_ptr->args_count; i++)
      bs_free_arg(&bs_func_ptr->args[i]);
    SAFE_FREE(bs_func_ptr->args);
    bs_free_retval(bs_func_ptr->retval);
    SAFE_FREE(bs_func_ptr);
  }
}

static void
bs_free_retval(bs_element_retval_t *bs_retval)
{
  if (bs_retval == NULL)
    return;
  SAFE_FREE(bs_retval->type);
  bs_free_function_pointer(bs_retval->function_pointer);
}

static void
bs_free_arg(bs_element_arg_t *bs_arg)
{
  SAFE_FREE(bs_arg->type);
  SAFE_FREE(bs_arg->sel_of_type);
  bs_free_function_pointer(bs_arg->function_pointer);
}

static void 
bs_free_method(bs_element_method_t *bs_method)
{
  unsigned i;
  for (i = 0; i < bs_method->args_count; i++)
    bs_free_arg(&bs_method->args[i]);
  SAFE_FREE(bs_method->args);
  bs_free_retval(bs_method->retval);
  SAFE_FREE(bs_method->suggestion); 
}

void 
bs_element_free(bs_element_type_t type, void *value)
{
  assert(value != NULL);

  switch (type) {
    case BS_ELEMENT_STRUCT:
    {
      bs_element_struct_t *bs_struct = (bs_element_struct_t *)value;
      unsigned i;
      SAFE_FREE(bs_struct->name);
      SAFE_FREE(bs_struct->type);
      for (i = 0; i < bs_struct->fields_count; i++) {
        SAFE_FREE(bs_struct->fields[i].name);
        SAFE_FREE(bs_struct->fields[i].type);
      }
      SAFE_FREE(bs_struct->fields);
      break;
    }

    case BS_ELEMENT_CFTYPE:
    {
      bs_element_cftype_t *bs_cftype = (bs_element_cftype_t *)value;
      SAFE_FREE(bs_cftype->name);
      SAFE_FREE(bs_cftype->type);
      SAFE_FREE(bs_cftype->tollfree);
      break;
    }

    case BS_ELEMENT_OPAQUE:
    {
      bs_element_opaque_t *bs_opaque = (bs_element_opaque_t *)value;
      SAFE_FREE(bs_opaque->name);
      SAFE_FREE(bs_opaque->type);
      break;    
    }

    case BS_ELEMENT_CONSTANT:
    {
      bs_element_constant_t *bs_const = (bs_element_constant_t *)value;
      SAFE_FREE(bs_const->name);
      SAFE_FREE(bs_const->type);
      SAFE_FREE(bs_const->suggestion);
      break;
    }

    case BS_ELEMENT_STRING_CONSTANT:
    {
      bs_element_string_constant_t *bs_str_const = 
        (bs_element_string_constant_t *)value;
      SAFE_FREE(bs_str_const->name);
      SAFE_FREE(bs_str_const->value);
      break;
    }

    case BS_ELEMENT_ENUM:
    {
      bs_element_enum_t *bs_enum = (bs_element_enum_t *)value;
      SAFE_FREE(bs_enum->name);
      SAFE_FREE(bs_enum->value);
      SAFE_FREE(bs_enum->suggestion);
      break;
    }

    case BS_ELEMENT_FUNCTION:
    {
      unsigned i;
      bs_element_function_t *bs_func = (bs_element_function_t *)value;
      free(bs_func->name);
      for (i = 0; i < bs_func->args_count; i++)
        bs_free_arg(&bs_func->args[i]);
      SAFE_FREE(bs_func->args);
      bs_free_retval(bs_func->retval);
      break;
    }

    case BS_ELEMENT_FUNCTION_ALIAS:
    {
      bs_element_function_alias_t *bs_func_alias = 
        (bs_element_function_alias_t *)value;
      SAFE_FREE(bs_func_alias->name);
      SAFE_FREE(bs_func_alias->original);
      break;
    }

    case BS_ELEMENT_CLASS:
    {
      bs_element_class_t *bs_class = (bs_element_class_t *)value;
      unsigned i;
      free(bs_class->name);
      for (i = 0; i < bs_class->class_methods_count; i++)
        bs_free_method(&bs_class->class_methods[i]);
      SAFE_FREE(bs_class->class_methods);
      for (i = 0; i < bs_class->instance_methods_count; i++)
        bs_free_method(&bs_class->instance_methods[i]);
      SAFE_FREE(bs_class->instance_methods);
      break;
    }

    case BS_ELEMENT_INFORMAL_PROTOCOL_METHOD:
    {
      bs_element_informal_protocol_method_t *bs_iprotm = 
        (bs_element_informal_protocol_method_t *)value;
      SAFE_FREE(bs_iprotm->protocol_name);
      SAFE_FREE(bs_iprotm->type);
      break;
    }

    default:
      fprintf(stderr, "unknown value %p of type %d passed to bs_free()", 
	      value, type);
  }
  free(value);
}

#endif // !MACRUBY_STATIC
