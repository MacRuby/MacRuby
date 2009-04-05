require File.dirname(__FILE__) + '/../spec_helper'
require 'mspec/helpers/variables'

describe Object, "#variables" do
  before :each do
    @ruby_version = Object.const_get :RUBY_VERSION
  end

  after :each do
    Object.const_set :RUBY_VERSION, @ruby_version
  end

  it "casts as strings if RUBY_VERSION < 1.9" do
    Object.const_set :RUBY_VERSION, "1.8.6"
    variables(:foo, :bar).should == %w{ foo bar }
    variables('foo', 'bar').should == %w{ foo bar }
  end

  it "casts as symbols if RUBY_VERSION >= 1.9" do
    Object.const_set :RUBY_VERSION, "1.9.0"
    variables(:foo, :bar).should == [:foo, :bar]
    variables('foo', 'bar').should == [:foo, :bar]
  end
end
