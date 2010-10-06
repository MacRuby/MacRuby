require File.dirname(__FILE__) + "/../spec_helper"
FixtureCompiler.require! "mri_abi"

describe "A method written for the MRI ABI" do
  before :each do
    @o = MRI_ABI_TEST.new
    @helper = proc { |*a| a.map { |x| x.object_id.to_s }.join('') }
  end

  it "with arity 0 can be called" do
    @o.test_arity0.should == @helper[@o]
  end

  it "with arity 1 can be called" do
    arg1 = Object.new
    @o.test_arity1(arg1).should == @helper[@o, arg1]
  end

  it "with arity 2 can be called" do
    arg1 = Object.new
    arg2 = Object.new
    @o.test_arity2(arg1, arg2).should == @helper[@o, arg1, arg2]
  end

  it "with arity 3 can be called" do
    arg1 = Object.new
    arg2 = Object.new
    arg3 = Object.new
    @o.test_arity3(arg1, arg2, arg3).should == @helper[@o, arg1, arg2, arg3]
  end

  it "with arity 4 can be called" do
    arg1 = Object.new
    arg2 = Object.new
    arg3 = Object.new
    arg4 = Object.new
    @o.test_arity4(arg1, arg2, arg3, arg4).should == @helper[@o, arg1, arg2, arg3, arg4]
  end

  it "with arity 5 can be called" do
    arg1 = Object.new
    arg2 = Object.new
    arg3 = Object.new
    arg4 = Object.new
    arg5 = Object.new
    @o.test_arity5(arg1, arg2, arg3, arg4, arg5).should == @helper[@o, arg1, arg2, arg3, arg4, arg5]
  end

  it "with arity 6 can be called" do
    arg1 = Object.new
    arg2 = Object.new
    arg3 = Object.new
    arg4 = Object.new
    arg5 = Object.new
    arg6 = Object.new
    @o.test_arity6(arg1, arg2, arg3, arg4, arg5, arg6).should == @helper[@o, arg1, arg2, arg3, arg4, arg5, arg6]
  end

  it "with arity -1 can be called" do
    arg1 = Object.new
    arg2 = Object.new
    arg3 = Object.new
    arg4 = Object.new
    arg5 = Object.new
    @o.test_arity_m1.should == @helper[@o]
    @o.test_arity_m1(arg1).should == @helper[@o, arg1]
    @o.test_arity_m1(arg1, arg2).should == @helper[@o, arg1, arg2]
    @o.test_arity_m1(arg1, arg2, arg3).should == @helper[@o, arg1, arg2, arg3]
    @o.test_arity_m1(arg1, arg2, arg3, arg4).should == @helper[@o, arg1, arg2, arg3, arg4]
    @o.test_arity_m1(arg1, arg2, arg3, arg4, arg5).should == @helper[@o, arg1, arg2, arg3, arg4, arg5]
  end

  it "with arity -2 can be called" do
    arg1 = Object.new
    arg2 = Object.new
    arg3 = Object.new
    arg4 = Object.new
    arg5 = Object.new
    @o.test_arity_m2.should == @helper[@o]
    @o.test_arity_m2(arg1).should == @helper[@o, arg1]
    @o.test_arity_m2(arg1, arg2).should == @helper[@o, arg1, arg2]
    @o.test_arity_m2(arg1, arg2, arg3).should == @helper[@o, arg1, arg2, arg3]
    @o.test_arity_m2(arg1, arg2, arg3, arg4).should == @helper[@o, arg1, arg2, arg3, arg4]
    @o.test_arity_m2(arg1, arg2, arg3, arg4, arg5).should == @helper[@o, arg1, arg2, arg3, arg4, arg5]
  end
end
