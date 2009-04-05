require File.dirname(__FILE__) + '/../spec_helper'
require 'mspec/expectations/expectations'
require 'mspec/matchers/include_variables'

describe IncludeVariablesMatcher do
  it "inherits from IncludeMatcher" do
    IncludeVariablesMatcher.new('@foo').should be_kind_of(IncludeMatcher)
  end
end

describe IncludeVariablesMatcher, "if RUBY_VERSION < 1.9" do
  before :each do
    @ruby_version = Object.const_get :RUBY_VERSION
    Object.const_set :RUBY_VERSION, "1.8.6"
  end

  after :each do
    Object.const_set :RUBY_VERSION, @ruby_version
  end

  it "matches when the array of strings includes the variable name" do
    matcher = IncludeVariablesMatcher.new('@foo')
    matcher.matches?(%w{ @foo @bar }).should be_true

    matcher = IncludeVariablesMatcher.new(:@foo)
    matcher.matches?(%w{ @foo @bar }).should be_true
  end

  it "does not match when the array of strings does not include the variable name" do
    matcher = IncludeVariablesMatcher.new('@baz')
    matcher.matches?(%w{ @foo @bar }).should be_false

    matcher = IncludeVariablesMatcher.new(:@baz)
    matcher.matches?(%w{ @foo @bar }).should be_false
  end
end

describe IncludeVariablesMatcher, "if RUBY_VERSION >= 1.9" do
  before :each do
    @ruby_version = Object.const_get :RUBY_VERSION
    Object.const_set :RUBY_VERSION, "1.9.0"
  end

  after :each do
    Object.const_set :RUBY_VERSION, @ruby_version
  end

  it "matches when the array of symbols includes the variable name" do
    matcher = IncludeVariablesMatcher.new('@foo')
    matcher.matches?([:@foo, :@bar]).should be_true

    matcher = IncludeVariablesMatcher.new(:@foo)
    matcher.matches?([:@foo, :@bar]).should be_true
  end

  it "does not match when the array of symbols does not include the variable name" do
    matcher = IncludeVariablesMatcher.new('@baz')
    matcher.matches?([:@foo, :@bar]).should be_false

    matcher = IncludeVariablesMatcher.new(:@baz)
    matcher.matches?([:@foo, :@bar]).should be_false
  end
end