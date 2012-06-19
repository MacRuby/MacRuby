define rp
  set $types = rb_type($arg0)

  if $types == 0x01
    # RUBY_T_OBJECT
    p "T_OBJECT:"
    p (struct RBasic *)($arg0)
  else
  if $types == 0x02
    # RUBY_T_CLASS
    p "T_CLASS:"
    p (struct RBasic *)($arg0)
  else
  if $types == 0x03
    # RUBY_T_MODULE
    p "T_MODULE:"
    p (struct RBasic *)($arg0)
  else
  if $types == 0x04
    # RUBY_T_FLOAT
    p "T_FLOAT:"
    p coerce_ptr_to_double($arg0)
  else
  if $types == 0x05
  # RUBY_T_STRING
    p "T_STRING:"
    p *(struct RString *)($arg0)
  else
  if $types == 0x06
    # RUBY_T_REGEXP
    p "T_REGEXP:"
    p *(struct rb_regexp *)($arg0)
  else
  if $types == 0x07
    # RUBY_T_ARRAY
    p "T_ARRAY:"
    p *(struct RArray *)($arg0)
  else
  if $types == 0x08
    # RUBY_T_HASH
    p "T_HASH:"
    p *(struct RHash *)($arg0)
  else
  if $types == 0x09
    # RUBY_T_STRUCT
    p "T_STRUCT:"
    p *(struct RStruct *)($arg0)
  else
  if $types == 0x0a
    # RUBY_T_BIGNUM
    p "T_BIGNUM:"
    p *(struct RBignum *)($arg0)
  else
  if $types == 0x0b
    # RUBY_T_FILE
    p "T_FILE:"
    p *(struct RFile *)($arg0)
    p *((struct RFile *)($arg0))->fptr
  else
  if $types == 0x0c
    # RUBY_T_DATA
    p "T_DATA:"
    p *(struct RData *)($arg0)
  else
  if $types == 0x0d
    # RUBY_T_MATCH
    p "T_MATCH:"
    p *(struct rb_match *)($arg0)
  else
  if $types == 0x0e
    # RUBY_T_COMPLEX
    p "T_COMPLEX:"
    p *(struct RComplex *)($arg0)
  else
  if $types == 0x0f
    # RUBY_T_RATIONAL
    p "T_RATIONAL:"
    p *(struct RRational *)($arg0)
  else
  if $types == 0x11
    # RUBY_T_NIL
    p "T_NIL:"
    p "nil"
  else
  if $types == 0x12
    # RUBY_T_TRUE
    p "T_TRUE:"
    p "true"
  else
  if $types == 0x14
    # RUBY_T_SYMBOL
    p "T_SYMBOL:"
    p *(struct rb_sym_t *)($arg0)
  else
  if $types == 0x15
    # RUBY_T_FIXNUM
    p "T_FIXNUM:"
    p rb_num2int($arg0)
  end
  if $types == 0x1b
    # RUBY_T_UNDEF
    p "T_UNDEF:"
    p "undef"
  end

end