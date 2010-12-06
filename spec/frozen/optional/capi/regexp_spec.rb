require File.expand_path('../spec_helper', __FILE__)

load_extension('regexp')

describe "C-API Regex functions" do
  before :each do
    @o = CApiRegexpSpecs.new
  end

  describe "rb_reg_options" do
    it "returns the options used to create the regexp" do
      @o.rb_reg_options(/42/im).should == //im.options
      @o.rb_reg_options(/42/i).should == //i.options
      @o.rb_reg_options(/42/m).should == //m.options
    end
  end

  describe "rb_reg_regcomp" do
    it "creates a valid regexp from a string" do
      regexp = /\b([A-Z0-9._%+-]+)\.{2,4}/
      @o.rb_reg_regcomp(regexp.source).should == regexp
    end
  end

  it "allows matching in C, properly setting the back references" do
    mail_regexp = /\b([A-Z0-9._%+-]+)@([A-Z0-9.-]+\.[A-Z]{2,4})\b/i
    name = "john.doe"
    domain = "example.com"
    @o.match(mail_regexp, "#{name}@#{domain}")
    $1.should == name
    $2.should == domain
  end
end
