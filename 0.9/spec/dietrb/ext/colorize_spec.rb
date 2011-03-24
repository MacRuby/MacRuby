require File.expand_path('../../spec_helper', __FILE__)
require 'irb/ext/colorize'

main = self
  
describe "IRB::ColoredFormatter" do
  before do
    @formatter = IRB::ColoredFormatter.new
    @context = IRB::Context.new(main)
  end
  
  it "colorizes a constant" do
    @formatter.result(Hash).should == "=> \e[1;32mHash\e[0;0m"
  end
  
  it "colorizes a numeric" do
    @formatter.result(1).should == "=> \e[0;36m1\e[0;0m"
    @formatter.result(1.2).should == "=> \e[0;36m1.2\e[0;0m"
  end
  
  # Not Wirble compliant
  it "colorizes a Range" do
    # @formatter.result(1..3).should == "=> \e[0;36m1\e[0;0m\e[0;31m..\e[0;0m\e[0;36m3\e[0;0m"
    @formatter.result(1..3).should == "=> \e[0;36m1\e[0;0m\e[0;34m..\e[0;0m\e[0;36m3\e[0;0m"
  end
  
  it "colorizes a String" do
    @formatter.result("foo bar").should == "=> \e[0;31m\"\e[0;0m\e[0;36mfoo bar\e[0;0m\e[0;31m\"\e[0;0m"
  end
  
  it "colorizes a Hash" do
    @formatter.result(:ok => :foo).should == "=> \e[0;32m{\e[0;0m\e[1;33m:\e[0;0m\e[1;33mok\e[0;0m\e[0;34m=>\e[0;0m\e[1;33m:\e[0;0m\e[1;33mfoo\e[0;0m\e[0;32m}\e[0;0m"
  end
  
  it "colorizes an Array" do
    @formatter.result([1, 2]).should == "=> \e[0;32m[\e[0;0m\e[0;36m1\e[0;0m\e[0;34m,\e[0;0m \e[0;36m2\e[0;0m\e[0;32m]\e[0;0m"
  end
  
  it "colorizes the prompt" do
    @formatter.colors[:prompt] = :light_green
    @formatter.prompt(@context).should == "\e[1;32m#{IRB::Formatter.new.prompt(@context)}\e[0;0m"
  end
  
  it "colorizes the result prefix" do
    @formatter.colors[:result_prefix] = :light_red
    @formatter.result("").should == "\e[1;31m=>\e[0;0m \e[0;31m\"\e[0;0m\e[0;31m\"\e[0;0m"
  end
  
  it "uses the dark_background color scheme by default" do
    @formatter.color_scheme.should == :dark_background
  end
  
  it "changes color scheme" do
    @formatter.color_scheme = :fresh
    @formatter.color_scheme.should == :fresh
    @formatter.colors[:result_prefix].should == :light_purple
  end
end