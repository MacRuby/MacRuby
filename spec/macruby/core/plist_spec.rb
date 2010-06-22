require File.expand_path("../../spec_helper", __FILE__)

describe "Kernel#load_plist" do
  it "should work with Fixnums" do
    load_plist(100.to_plist).should == 100
    load_plist(-42.to_plist).should == -42
  end
  
  it "should work with Strings" do
    load_plist("hello there".to_plist).should == "hello there"
    load_plist("MacRuby \n FTW".to_plist).should == "MacRuby \n FTW"
  end
  
  it "should work with Bignums" do
    bn = 100_000_000_000_000
    load_plist(bn.to_plist).should == bn
  end
  
  it "should work with arrays" do
    arr = [1, "two", 3.0, true, false]
    load_plist(arr.to_plist).should == arr
  end
  
  it "should work with simple hashes" do
    hash = { "a" => "b", "c" => "d", "e" => "f"}
    load_plist(hash.to_plist).should == hash
  end
  
  it "should work with booleans" do
    load_plist(true.to_plist).should == true
    load_plist(false.to_plist).should == false
  end

  it "should work with dates" do
    time = Time.now
    load_plist(time.to_plist).description.should == time.description
  end
  
  it "should raise an TypeError when given something not a String" do
    lambda { load_plist(nil) }.should raise_error(TypeError)
    lambda { load_plist(42) }.should raise_error(TypeError)
  end
  
  it "should raise an ArgumentError when given invalid String data" do
    lambda { load_plist("hello dolly") }.should raise_error(ArgumentError)
  end
end

describe "Kernel#to_plist" do
  it "should serialize everything to a string" do
    100.to_plist.should be_kind_of(String)
    "hello".to_plist.should be_kind_of(String)
    true.to_plist.should be_kind_of(String)
    false.to_plist.should be_kind_of(String)
  end
  
  it "should raise an ArgumentError if given a non-serializable data type" do
    lambda { Object.new.to_plist }.should raise_error(ArgumentError)
  end
  
end
