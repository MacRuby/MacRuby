# coding: utf-8
ruby_version_is "1.9" do
  describe "String#encode!" do

    it "transcodes to the default internal encoding with no argument" do
      begin
        old_default_internal = Encoding.default_internal
        Encoding.default_internal = Encoding::EUC_JP
        str = "問か".force_encoding('utf-8')
        str.encoding.should_not == Encoding.default_internal
        str.encode.encoding.should == Encoding.default_internal
      ensure
        Encoding.default_internal = old_default_internal
      end
    end

    it "accepts a target encoding name as a String for an argument" do
      str = "Füll"
      lambda do
        str.encode!('binary')
      end.should_not raise_error(ArgumentError)
    end

    it "accepts a target encoding as an Encoding for an argument" do
      str = "Füll"
      lambda do
        str.encode!(Encoding::BINARY)
      end.should_not raise_error(ArgumentError)
    end

    it "accepts a target and source encoding given as Encoding objects" do
      str = "Füll"
      lambda do
        str.encode!(Encoding::ASCII, Encoding::BINARY)
      end.should_not raise_error(ArgumentError)
    end

    it "accepts a target and source encoding given as Strings" do
      str = "Füll"
      lambda do
        str.encode!('ascii', 'binary')
      end.should_not raise_error(ArgumentError)
    end

    it "accepts a target encoding given as an Encoding, and a source encoding given as a String" do
      str = "Füll"
      lambda do
        str.encode!(Encoding::ASCII, 'binary')
      end.should_not raise_error(ArgumentError)
    end

    it "accepts a target encoding given as a String, and a source encoding given as an Encoding" do
      str = "Füll"
      lambda do
        str.encode!('ascii', Encoding::BINARY)
      end.should_not raise_error(ArgumentError)
    end

    it "accepts an options hash as the final argument" do
      lambda do
        "yes".encode!('ascii', Encoding::BINARY, {:xml => :text})
      end.should_not raise_error(ArgumentError)

      lambda do
        "yes".encode!(Encoding::BINARY, {:xml => :text})
      end.should_not raise_error(ArgumentError)
    end

    it "returns self when called with only a target encoding" do
      str = "strung"
      str.encode!(Encoding::BINARY).should == str.force_encoding(Encoding::BINARY)
    end

    it "returns self when called with only a target encoding" do
      str = "strung"
      str.encode!(Encoding::BINARY,Encoding::ASCII).should == str.force_encoding(Encoding::BINARY)
    end

    it "returns self even when no changes are made" do
      str = "strung"
      str.encode!(Encoding::UTF_8).should == str
    end

    it "tags the String with the given encoding" do
      str = "ürst"
      str.encoding.should == Encoding::UTF_8
      str.encode!(Encoding::UTF_16LE)
      str.encoding.should == Encoding::UTF_16LE
    end

    it "transcodes self to the given encoding" do
      str = "\u3042".force_encoding('UTF-8')
      str.encode!(Encoding::EUC_JP)
      str.should == "\xA4\xA2".force_encoding('EUC-JP')
    end
   
    it "can convert between encodings where a multi-stage conversion path is needed" do
      str = "big?".force_encoding(Encoding::US_ASCII)
      str.encode!(Encoding::Big5, Encoding::US_ASCII)
      str.encoding.should == Encoding::Big5
    end

    it "raises an Encoding::InvalidByteSequenceError for invalid byte sequences" do
      lambda do
        "\xa4".force_encoding(Encoding::EUC_JP).encode!('iso-8859-1')
      end.should raise_error(Encoding::InvalidByteSequenceError)
    end

    it "raises UndefinedConversionError if the String contains characters invalid for the target     encoding" do
      str = "\u{6543}"
      lambda do
        str.encode!(Encoding.find('macCyrillic'))
      end.should raise_error(Encoding::UndefinedConversionError)
    end
    
    it "raises Encoding::ConverterNotFoundError for invalid target encodings" do
      lambda do
        "\u{9878}".encode!('xyz')
      end.should raise_error(Encoding::ConverterNotFoundError)
    end

    it "raises a RuntimeError when called on a frozen String" do
      lambda do
        "foo".freeze.encode!(Encoding::ANSI_X3_4_1968) 
      end.should raise_error(RuntimeError)
    end

    # http://redmine.ruby-lang.org/issues/show/1836
    it "raises a RuntimeError when called on a frozen String when it's a no-op" do
      lambda do
        "foo".freeze.encode!("foo".encoding) 
      end.should raise_error(RuntimeError)
    end
  end

  describe "String#encode" do

    it "transcodes to the default internal encoding with no argument" do
      begin
        old_default_internal = Encoding.default_internal
        Encoding.default_internal = Encoding::EUC_JP
        str = "問か".force_encoding('utf-8')
        str.encoding.should_not == Encoding.default_internal
        str.encode!
        str.encoding.should == Encoding.default_internal
      ensure
        Encoding.default_internal = old_default_internal
      end
    end

    it "accepts a target encoding name as a String for an argument" do
      str = "Füll"
      lambda do
        str.encode('binary')
      end.should_not raise_error(ArgumentError)
    end

    it "accepts a target encoding as an Encoding for an argument" do
      str = "Füll"
      lambda do
        str.encode(Encoding::BINARY)
      end.should_not raise_error(ArgumentError)
    end

    it "accepts a target and source encoding given as Encoding objects" do
      str = "Füll"
      lambda do
        str.encode(Encoding::ASCII, Encoding::BINARY)
      end.should_not raise_error(ArgumentError)
    end

    it "accepts a target and source encoding given as Strings" do
      str = "Füll"
      lambda do
        str.encode('ascii', 'binary')
      end.should_not raise_error(ArgumentError)
    end

    it "accepts a target encoding given as an Encoding, and a source encoding given as a String" do
      str = "Füll"
      lambda do
        str.encode(Encoding::ASCII, 'binary')
      end.should_not raise_error(ArgumentError)
    end

    it "accepts a target encoding given as a String, and a source encoding given as an Encoding" do
      str = "Füll"
      lambda do
        str.encode('ascii', Encoding::BINARY)
      end.should_not raise_error(ArgumentError)
    end

    it "accepts an options hash as the final argument" do
      lambda do
        "yes".encode('ascii', Encoding::BINARY, {:xml => :text})
      end.should_not raise_error(ArgumentError)

      lambda do
        "yes".encode(Encoding::BINARY, {:xml => :text})
      end.should_not raise_error(ArgumentError)
    end

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

    it "transcodes self to the given encoding" do
      str = "\u3042".force_encoding('UTF-8')
      str.encode(Encoding::EUC_JP).should == "\xA4\xA2".force_encoding('EUC-JP')
    end
   
    it "can convert between encodings where a multi-stage conversion path is needed" do
      str = "big?".force_encoding(Encoding::US_ASCII)
      str.encode(Encoding::Big5, Encoding::US_ASCII).encoding.should == Encoding::Big5
    end

    it "raises an Encoding::InvalidByteSequenceError for invalid byte sequences" do
      lambda do
        "\xa4".force_encoding(Encoding::EUC_JP).encode('iso-8859-1')
      end.should raise_error(Encoding::InvalidByteSequenceError)
    end

    it "raises UndefinedConversionError if the String contains characters invalid for the target     encoding" do
      str = "\u{6543}"
      lambda do
        str.encode(Encoding.find('macCyrillic'))
      end.should raise_error(Encoding::UndefinedConversionError)
    end
    
    it "raises Encoding::ConverterNotFoundError for invalid target encodings" do
      lambda do
        "\u{9878}".encode('xyz')
      end.should raise_error(Encoding::ConverterNotFoundError)
    end

  end
end
