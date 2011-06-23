require File.expand_path('../../../spec_helper', __FILE__)

module JSONSpecHelper
  # run once with json and then again with json/pure
  def serialize input, expected
    ['json','json/pure'].each do |lib|
      ret = ruby_exe("p (#{input}).to_json == '#{expected}'", options: "-r #{lib}")
      ret.chomp.should == 'true'
    end
  end
end


describe 'JSON serialization using the C extension' do
  extend JSONSpecHelper

  it 'works with NSString and NSMutableString' do
    serialize('NSString.stringWithString("test")','"test"')
    serialize('NSMutableString.stringWithString("test")','"test"')
    # TODO test a string with a different encoding
  end

  it 'works with NSDictionary and NSMutableDictionary' do
    serialize('NSDictionary.dictionaryWithObject(2, forKey:1)','{"1":2}')
    serialize('NSMutableDictionary.dictionaryWithObject(2, forKey:1)','{"1":2}')
  end

  it 'works with NSArray and NSMutableArray' do
    serialize('NSArray.arrayWithArray(["test",1,[]])','["test",1,[]]')
    serialize('NSMutableArray.arrayWithArray(["test",1,[]])','["test",1,[]]')
  end

  it 'works with NSNumber' do
    serialize('NSNumber.numberWithDouble(3.14159265)','3.14159265')
    serialize('NSNumber.numberWithInt(1234)','1234')
    serialize('NSNumber.numberWithChar("a")','97')
  end

  it 'works with other Objective-C objects' do
    serialize('NSDate.distantPast', "\"#{NSDate.distantPast.to_s}\"")
  end

  # ticket #1313
  it 'does not segfault when an Objective-C object is nested in a ruby Array' do
    serialize('[NSString.stringWithString("")]','[""]')
    serialize('[NSArray.arrayWithArray([])]','[[]]')
  end

  # basic tests to make sure objective-c adjustments didn't break regular
  # ruby stuff, to be thorough you should run the MRI test suite
  it 'still works with ruby objects' do
    serialize('nil','null')
    serialize('false','false')
    serialize('true','true')
    serialize('{a: 2}','{"a":2}')
    serialize('["test",1,[]]','["test",1,[]]')
    serialize('"hello"','"hello"')
    serialize('42','42')
    serialize('1000000000000000000000','1000000000000000000000')
    serialize('3.14159265','3.14159265')
    serialize('Class','"Class"')
  end

end
