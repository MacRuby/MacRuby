require File.dirname(__FILE__) + '/../spec_helper'

describe "A MacRuby method" do
  it "uses argument-names + colon + variable syntax to form the method name" do
    o = Object.new
    def o.doSomething(x, withObject:y); x + y; end
    o.respond_to?(:'doSomething:withObject:').should == true
    o.respond_to?(:'doSomething').should == false
  end

  it "can have multiple arguments with the same name" do
    o = Object.new
    def o.doSomething(x, withObject:y, withObject:z); x + y + z; end
    o.respond_to?(:'doSomething:withObject:withObject:').should == true
    o.respond_to?(:'doSomething').should == false
  end

  it "can coexist with other selectors whose first part is similar" do
    o = Object.new
    def o.foo(x); x; end
    def o.foo(x, withObject:y); x + y; end
    def o.foo(x, withObject:y, andObject:z); x + y + z; end
    o.respond_to?(:'foo').should == true
    o.respond_to?(:'foo:withObject:').should == true
    o.respond_to?(:'foo:withObject:andObject:').should == true
  end

  it "must start by a regular argument variable then followed by argument-names" do
    lambda { eval("def foo(x:y); end") }.should raise_error(SyntaxError)
    lambda { eval("def foo(x, y, with:z); end") }.should raise_error(SyntaxError)
    lambda { eval("def foo(x, with:y, z); end") }.should raise_error(SyntaxError)
  end

  it "can be called using argument-names + colon + variable syntax" do
    o = Object.new
    def o.doSomething(x, withObject:y, withObject:z); x + y + z; end
    o.doSomething(30, withObject:10, withObject:2).should == 42
  end

  it "can be called using argument-name-as-symbols + => + variable syntax" do
    o = Object.new
    def o.doSomething(x, withObject:y, withObject:z); x + y + z; end
    o.doSomething(30, :withObject => 10, :withObject => 2).should == 42
  end

  it "can be called mixing both syntaxes" do
    o = Object.new
    def o.doSomething(x, withObject:y, withObject:z); x + y + z; end
    o.doSomething(30, withObject:10, :withObject => 2).should == 42
  end

  it "can be called using #send" do
    o = Object.new
    def o.doSomething(x, withObject:y, withObject:z); x + y + z; end
    o.send(:'doSomething:withObject:withObject:', 30, 10, 2).should == 42
  end

  it "can be called using -[NSObject performSelector:]" do
    o = Object.new
    def o.doSomething; 42; end
    o.performSelector(:'doSomething').should == 42
  end

  it "can be called using -[NSObject performSelector:withObject:]" do
    o = Object.new
    def o.doSomething(x); x; end
    o.performSelector(:'doSomething:', withObject:42).should == 42
  end

  it "can be called using -[NSObject performSelector:withObject:withObject:]" do
    o = Object.new
    def o.doSomething(x, withObject:y); x + y; end
    o.performSelector(:'doSomething:withObject:',
                      withObject:40, withObject:2).should == 42
  end

  it "named using the setFoo pattern cannot be called using #foo=" do
    o = Object.new
    def o.setFoo(x); end
    o.respond_to?(:'foo=').should == false
    lambda { o.foo = 42 }.should raise_error(NoMethodError)
  end

  it "named using the isFoo pattern cannot be called using #foo?" do
    o = Object.new
    def o.isFoo; end
    o.respond_to?(:'foo?').should == false
    lambda { o.foo? }.should raise_error(NoMethodError)
  end
end

framework 'Foundation'

fixture_source = File.dirname(__FILE__) + '/fixtures/method.m'
fixture_ext = '/tmp/method.bundle'
if !File.exist?(fixture_ext) or File.mtime(fixture_source) > File.mtime(fixture_ext)
=begin
  # #system is currently broken
  unless system("/usr/bin/gcc #{fixture_source} -o #{fixture_ext} -g -framework Foundation -dynamiclib -fobjc-gc -arch i386 -arch x86_64 -arch ppc")
    $stderr.puts "cannot compile fixture source file `#{fixture_source}' - aborting"
    exit 1
  end
=end
  `/usr/bin/gcc #{fixture_source} -o #{fixture_ext} -g -framework Foundation -dynamiclib -fobjc-gc -arch i386 -arch x86_64 -arch ppc`
end
require '/tmp/method'

describe "An Objective-C method" do
  it "named using the setFoo pattern can be called using #foo=" do
    o = []
    o.respond_to?(:'setArray').should == true
    o.respond_to?(:'array=').should == true
    o.array = [1, 2, 3]
    o.should == [1, 2, 3]
  end

  it "named using the isFoo pattern can be called using #foo?" do
    o = NSBundle.mainBundle
    o.respond_to?(:'isLoaded').should == true
    o.respond_to?(:'loaded?').should == true
    o.loaded?.should == true
  end

  it "is only exposed in #methods if the second argument is true" do
    o = Object.new
    o.methods.include?(:'performSelector').should == false
    o.methods(true).include?(:'performSelector').should == false
    o.methods(false).include?(:'performSelector').should == false
    o.methods(true, true).include?(:'performSelector').should == true
    o.methods(false, true).include?(:'performSelector').should == true
  end

  it "can be called on an immediate object" do
    123.self.should == 123
    true.self.should == true
    false.self.should == false
    nil.self.should == nil
  end

  it "returning void returns nil in Ruby" do
    o = TestMethod.new
    o.methodReturningVoid.should == nil
  end

  it "returning nil returns nil in Ruby" do
    o = TestMethod.new
    o.methodReturningNil.should == nil
  end

  it "returning self returns the same receiver object" do
    o = TestMethod.new
    o.methodReturningSelf.should == o
    o.methodReturningSelf.object_id == o.object_id
  end

  it "returning kCFBooleanTrue returns true in Ruby" do
    o = TestMethod.new
    o.methodReturningCFTrue.should == true
    o.methodReturningCFTrue.class.should == TrueClass
  end

  it "returning kCFBooleanFalse returns false in Ruby" do
    o = TestMethod.new
    o.methodReturningCFFalse.should == false
    o.methodReturningCFFalse.class.should == FalseClass
  end

  it "returning kCFNull returns nil in Ruby" do
    o = TestMethod.new
    o.methodReturningCFNull.should == nil
    o.methodReturningCFNull.class.should == NilClass
  end

  it "returning 'char' returns a fixnum in Ruby" do
    o = TestMethod.new
    o.methodReturningChar.should == 42
    o.methodReturningChar2.should == -42
  end
 
  it "returning 'unsigned char' returns a fixnum in Ruby" do
    o = TestMethod.new
    o.methodReturningUnsignedChar.should == 42
  end

  it "returning 'short' returns a fixnum in Ruby" do
    o = TestMethod.new
    o.methodReturningShort.should == 42
    o.methodReturningShort2.should == -42
  end

  it "returning 'unsigned short' returns a fixnum in Ruby" do
    o = TestMethod.new
    o.methodReturningUnsignedShort.should == 42
  end

  it "returning 'int' returns a fixnum in Ruby" do
    o = TestMethod.new
    o.methodReturningInt.should == 42
    o.methodReturningInt2.should == -42
  end

  it "returning 'unsigned int' returns a fixnum in Ruby" do
    o = TestMethod.new
    o.methodReturningUnsignedInt.should == 42
  end
end
