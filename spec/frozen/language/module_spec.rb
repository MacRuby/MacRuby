require File.dirname(__FILE__) + '/../spec_helper'

module LangModuleSpec
  module Sub1; end
end

module LangModuleSpec::Sub2; end

describe "module" do
  it "has the right name" do
    LangModuleSpec::Sub1.name.should == "LangModuleSpec::Sub1"
    LangModuleSpec::Sub2.name.should == "LangModuleSpec::Sub2"
  end

  it "gets a name when assigned to a constant" do
    LangModuleSpec::Anon = Module.new
    LangModuleSpec::Anon.name.should == "LangModuleSpec::Anon"
  end
end

language_version __FILE__, "module"