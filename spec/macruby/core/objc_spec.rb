require File.dirname(__FILE__) + "/../spec_helper"
# TODO: the MacRuby class should also be tested from Objective-C.
#FixtureCompiler.require! "objc"

describe "-[MacRuby sharedRuntime]" do
  before :each do
    @r = MacRuby.sharedRuntime
  end

  it "initializes and return a singleton instance representing the runtime" do
    @r.class.should == MacRuby
    @r.should == MacRuby.sharedRuntime
  end

  it "can evaluate a given Ruby expression" do
    o = @r.evaluateString('1+2')
    o.class.should == Fixnum
    o.should == 3

    lambda { @r.evaluateString('1+') }.should raise_error(SyntaxError)
    lambda { @r.evaluateString('foo') }.should raise_error(NameError)
  end

  it "can evaluate a given Ruby file using a path or an URL" do
    p1 = File.join(FIXTURES, 'test_objc1.rb')
    p2 = File.join(FIXTURES, 'test_objc2.rb')

    o = @r.evaluateFileAtPath(p1)
    o.class.should == Fixnum
    o.should == 3

    o = @r.evaluateFileAtURL(NSURL.fileURLWithPath(p1))
    o.class.should == Fixnum
    o.should == 3

    lambda { @r.evaluateFileAtPath(p2) }.should raise_error(NameError)
    lambda { @r.evaluateFileAtURL(NSURL.fileURLWithPath(p2)) }.should raise_error(NameError)

    # TODO: add tests that should raise_error for the following cases:
    # - given path is nil
    # - given path does not exist
    # - given URL is nil
    # - given file:// URL does not exist
    # - given URL is not file://
  end
end
