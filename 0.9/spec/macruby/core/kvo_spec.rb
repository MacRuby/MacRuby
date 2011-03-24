require File.expand_path("../../spec_helper", __FILE__)
require File.join(FIXTURES, "kvo_wrapper")

describe "An Object being observed through NSKeyValueObservation" do
  it "retains the values for its instance variables" do
    #
    # Was <rdar://problem/7210942> 
    # 
    w = Wrapper.new(42)
    w.addObserver(w, forKeyPath:'whatever', options:0, context:nil)
    w.wrappedValue.should == 42
  end

  it "keeps reporting its instance variables through instance_variables" do
    #
    # <rdar://problem/7210942> 
    # 
    w = Wrapper.new(42)
    w.addObserver(w, forKeyPath:'whatever', options:0, context:nil)
    w.instance_variables.should include(:@wrapped)
  end

  it "can be inspected" do
    #
    # <rdar://problem/7210942> 
    # 
    w = Wrapper.new(42)
    w.addObserver(w, forKeyPath:'whatever', options:0, context:nil)
    lambda { w.inspect }.should_not raise_error
  end
end

describe "A nontrivially derived Object" do
  it "retains the values for its instance variables" do
    w = FancyWrapper.new(42)
    w.wrappedValue.should == 42
  end
end

describe "A nontrivially derived Object being observed through NSKeyValueObservation" do
  it "retains the values for its instance variables" do
    #
    # <rdar://problem/7260995>
    # 
    w = FancyWrapper.new(42)
    w.addObserver(w, forKeyPath:'whatever', options:0, context:nil)
    w.wrappedValue.should == 42
  end

  it "keeps reporting its instance variables through instance_variables" do
    w = FancyWrapper.new(42)
    w.addObserver(w, forKeyPath:'whatever', options:0, context:nil)
    w.instance_variables.should include(:@wrapped)
  end

  it "can be inspected" do
    w = FancyWrapper.new(42)
    w.addObserver(w, forKeyPath:'whatever', options:0, context:nil)
    lambda { w.inspect }.should_not raise_error
  end
end

describe "An accessor defined with Module::attr_writer" do
  it "does not send any KVO notifications unless a framework has been loaded" do
    expected_output = "Manfred is very happy with himself"

    example = %{delegate = Object.new
                def delegate.wrapperWillChangeValueForKey(key)
                  raise "did not expect a KVO notification!"
                end
                w = Wrapper.new
                w.kvoDelegate = delegate
                w.whatever = "#{expected_output}"
                puts w.whatever}.gsub("\n", ";")

    output = ruby_exe(example, :options => "-r #{File.join(FIXTURES, "kvo_wrapper.rb")}")
    output.chomp.should == expected_output
  end
  
  it "sends KVO notifications, around assigning the new value, once a framework has been loaded" do
    @w = Wrapper.new
    @w.kvoDelegate = self
    @w.whatever = "oh come on"
    (@ranBefore && @ranAfter).should == true
  end
  
  def wrapperWillChangeValueForKey(key)
    key.should == "whatever"
    @w.whatever.should == "like, whatever"
    @ranBefore = true
  end
  
  def wrapperDidChangeValueForKey(key)
    key.should == "whatever"
    @w.whatever.should == "oh come on"
    @ranAfter = true
  end
end