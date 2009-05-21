assert ":ok", %q{
  begin
    require 'doesnotexist'
  rescue LoadError
    p :ok
  end
}

assert ":ok", "$:.unshift('fixtures'); require 'foo'"

assert ":ok", "begin; require 'fixtures/raise'; rescue NameError; p :ok; end"

assert ":ok", "begin; require '/doesnotexist'; rescue NameError; p :ok; end"