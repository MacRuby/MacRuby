require File.dirname(__FILE__) + '/../../spec_helper'

# This will fail on MacRuby. How to properly spec this.

describe "Encoding#dummy?" do
  before :all do
    @encodings = %w{
      ASCII-8BIT
      Big5
      CP51932 CP850 CP852 CP855 CP949
      EUC-JP EUC-KR EUC-TW Emacs-Mule eucJP-ms
      GB12345 GB18030 GB1988 GB2312 GBK
      IBM437 IBM737 IBM775 IBM852 IBM855 IBM857 IBM860 IBM861 IBM862 IBM863 IBM864 IBM865 IBM866 IBM869
      ISO-2022-JP ISO-2022-JP-2 ISO-8859-1 ISO-8859-10 ISO-8859-11 ISO-8859-13 ISO-8859-14 ISO-8859-15
      ISO-8859-16 ISO-8859-2 ISO-8859-3 ISO-8859-4 ISO-8859-5 ISO-8859-6 ISO-8859-7 ISO-8859-8 ISO-8859-9
      KOI8-R KOI8-U
      MacJapanese macCentEuro macCroatian macCyrillic macGreek macIceland macRoman macRomania macThai
      macTurkish macUkraine
      Shift_JIS stateless-ISO-2022-JP
      TIS-620
      US-ASCII UTF-16BE UTF-16LE UTF-32BE UTF-32LE UTF-7 UTF-8 UTF8-MAC
      Windows-1250 Windows-1251 Windows-1252 Windows-1253 Windows-1254 Windows-1255 Windows-1256
      Windows-1257 Windows-1258 Windows-31J Windows-874
    }

    @dummy_encodings = %w{ ISO-2022-JP ISO-2022-JP-2 UTF-7 }
  end

  it "returns `false' if an encoding can be handled correctly" do
    (@encodings - @dummy_encodings).each do |name|
      Encoding.find(name).dummy?.should be_false
    end
  end

  it "returns `true' if an encoding is a placeholder which can't be handled correctly" do
    @dummy_encodings.each do |name|
      Encoding.find(name).dummy?.should be_true
    end
  end
end
