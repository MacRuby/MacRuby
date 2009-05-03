require File.dirname(__FILE__) + "/spec_helper"
FixtureCompiler.require! "method"

describe "A CoreFoundation type" do
  it "behaves like a regular Objective-C/Ruby object" do
    o = CFBundleGetMainBundle()
    o.class.should == NSCFType
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
