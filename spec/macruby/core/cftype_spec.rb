require File.dirname(__FILE__) + "/../spec_helper"
FixtureCompiler.require! "method"

describe "A CoreFoundation type" do
  it "behaves like a regular Objective-C/Ruby object" do
    o = CFBundleGetMainBundle()
    if MACOSX_VERSION <= 10.5
      o.class.should == NSCFType
    else
      # __NSCFType isn't a constant in Ruby so we have to cheat.
      o.class.should == NSClassFromString('__NSCFType')
    end
    o.inspect.class.should == String
  end

  it "can be passed to a C/Objective-C API" do
    o = CFBundleGetMainBundle()
    CFBundleIsExecutableLoaded(o).should == true
  end

  it "toll-free bridged to an Objective-C type behaves like the Objective-C version" do
    s = CFStringCreateWithCString(nil, "foo", KCFStringEncodingUTF8)
    s.class.should == NSString
    s.should == 'foo'
    CFRelease(s)
  end

  it "can be substitued with the toll-free bridged equivalent" do
    s = CFStringCreateMutableCopy(nil, 0, "foo")
    s.class.should == NSMutableString
    s.should == 'foo'
    CFRelease(s)
  end
end
