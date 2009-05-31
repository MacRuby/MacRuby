# coding: UTF-8
ruby_version_is "1.9" do
  describe "String#getbyte" do
    it "returns the same value whatever the encoding, even if valid_encoding? is false" do
      str = 'こんにちは'
      ['ASCII-8BIT', 'EUC-JP'].each do |encoding|
        str2 = str.dup
        str2.force_encoding(encoding)
        (1...str.bytesize).each do |i|
          str2.getbyte(i).should == str.getbyte(i)
        end
      end
    end

    it "returns the (n+1)th byte from the start of a string if n >= 0" do
      str = "\xE3\x81\x82" # 'あ' in UTF-8
      str.getbyte(0).should == 0xE3
      str.getbyte(1).should == 0x81
      str.getbyte(2).should == 0x82
    end

    it "returns the (-n)th byte from the end of a string if n < 0" do
      str = "\xE3\x81\x82" # 'あ' in UTF-8
      str.getbyte(-3).should == 0xE3
      str.getbyte(-2).should == 0x81
      str.getbyte(-1).should == 0x82
    end

    it "returns nil for an offset outside the string" do
      str = "abcdef"
      str.getbyte(6).should be_nil
      str.getbyte(100).should be_nil
      str.getbyte(-100).should be_nil
      str.getbyte(-7).should be_nil
    end
  end
end
