# coding: UTF-8
ruby_version_is "1.9" do
  describe "String#valid_encoding?" do
    it "returns true for a valid encoding" do
      "abcdef".valid_encoding?.should be_true
      "こんにちは".valid_encoding?.should be_true
      "\xE3\x81\x82".valid_encoding?.should be_true # 'あ' in UTF-8
    end

    it "returns true for a valid encoding and a null character" do
      "abc\0def".valid_encoding?.should be_true
      "こん\0にちは".valid_encoding?.should be_true
      "\0\xE3\x81\x82".valid_encoding?.should be_true # 'あ' in UTF-8
    end

    it "always returns true for binary encoding" do
      'abcdef'.force_encoding('ASCII-8BIT').valid_encoding?.should be_true
      'こんにちは'.force_encoding('ASCII-8BIT').valid_encoding?.should be_true
      "\xE3\x81\x82".force_encoding('ASCII-8BIT').valid_encoding?.should be_true # 'あ' in UTF-8
      "\xE3".force_encoding('ASCII-8BIT').valid_encoding?.should be_true
    end

    it "returns false for an invalid encoding" do
      "\xE3".valid_encoding?.should be_false
      "\xE3\x0\x82".valid_encoding?.should be_false
      "\xA4\xA2".valid_encoding?.should be_false # 'あ' in EUC-JP (invalid UTF-8)
    end

    it "returns false when a valid encoding has been made invalid" do
      str = 'こんにちは'
      str.setbyte(0, 0)
      str.valid_encoding?.should be_false

      str = 'こんにちは'
      str.force_encoding('EUC-JP')
      str.valid_encoding?.should be_false
    end

    it "returns true for an invalid encoding made valid" do
      str = "\xE3\x0\x82"
      str.setbyte(1, 0x81)
      str.valid_encoding?.should be_true

      str = "\xA4\xA2" # 'あ' in EUC-JP
      str.force_encoding('EUC-JP') # set the correct encoding
      str.valid_encoding?.should be_true
    end
  end
end
