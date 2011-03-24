assert ":ok", %q{
  begin
    require 'doesnotexist'
  rescue LoadError
    p :ok
  end
}

assert ":ok", "require '#{Dir.pwd + '/fixtures/foo'}'"
assert ":ok", "require '#{Dir.pwd + '/fixtures/foo.rb'}'"

assert ":ok", "$:.unshift('fixtures'); require 'foo'"

assert ":ok", "begin; require 'fixtures/raise'; rescue NameError; p :ok; end"

assert ":ok", "begin; require '/doesnotexist'; rescue LoadError; p :ok; end"
