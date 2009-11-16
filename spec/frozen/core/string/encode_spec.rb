# coding: utf-8
require File.dirname(__FILE__) + '/shared/encode'

ruby_version_is "1.9" do
  describe "String#encode" do
    it_behaves_like :encode_string, :encode
    
    it "returns a copy of self when called with only a target encoding" do
      str = "strung".force_encoding(Encoding::UTF_8)
      copy = str.encode('ascii')
      str.encoding.should == Encoding::UTF_8
      copy.encoding.should == Encoding::US_ASCII
    end

    it "returns self when called with only a target encoding" do
      str = "strung"
      copy = str.encode(Encoding::BINARY,Encoding::ASCII)
      copy.object_id.should_not == str.object_id
      str.encoding.should == Encoding::UTF_8
    end
    
    it "returns a copy of self even when no changes are made" do
      str = "strung".force_encoding('ASCII')
      str.encode(Encoding::UTF_8).object_id.should_not == str.object_id
      str.encoding.should == Encoding::US_ASCII
    end
    
    it "returns a String with the given encoding" do
      str = "ürst"
      str.encoding.should == Encoding::UTF_8
      copy = str.encode(Encoding::UTF_16LE)
      copy.encoding.should == Encoding::UTF_16LE
      str.encoding.should == Encoding::UTF_8
    end
  end
end