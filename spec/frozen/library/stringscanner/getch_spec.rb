require File.dirname(__FILE__) + '/../../spec_helper'
require File.dirname(__FILE__) + '/shared/eucjp'
require 'strscan'

describe "StringScanner#getch" do
  it "scans one character and returns it" do
    s = StringScanner.new('abc')
    s.getch.should == "a"
    s.getch.should == "b"
    s.getch.should == "c"
  end

  it "is multi-byte character sensitive" do
    s = StringScanner.new("あ") # Japanese hira-kana "A" 
    s.getch.should == "あ" 
    s.getch.should be_nil
  end
  
  it "should keep the encoding" do
    s = StringScanner.new(TestStrings.eucjp)
    s.getch.encoding.to_s.should == "EUC-JP"
  end

  it "returns nil at the end of the string" do
    # empty string case
    s = StringScanner.new('')
    s.getch.should == nil
    s.getch.should == nil

    # non-empty string case
    s = StringScanner.new('a')
    s.getch # skip one
    s.getch.should == nil
  end
  
  it "should start from scratch even after a scan was used" do
    s = StringScanner.new('this is a test')
    s.scan(/\w+/)
    s.getch.should == " "
  end

  it "does not accept any arguments" do
    s = StringScanner.new('abc')
    lambda {
      s.getch(5)
    }.should raise_error(ArgumentError, /wrong .* arguments/)
  end

end
