# This file keeps track of known bugs. Only enter bugs here that cannot be
# described in a RubySpec file at this point of the development.

assert 'true', %{
  class X; end
  p X.object_id != X.dup.object_id
}, :known_bug => true

assert '123456789012345678901234567890', %{
  puts '%d' % 123456789012345678901234567890
}

assert '42', %{
  module Foo; def foo; 42; end; end
  class ::Class; include Foo; end
  p Class.new.foo
}, :known_bug => true

assert '42', %{
  module Foo; def foo; 42; end; end
  class ::Module; include Foo; end
  p Module.new.foo
}, :known_bug => true

