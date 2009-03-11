#!/usr/bin/env macruby

require File.expand_path('../../test_helper', __FILE__)

class TestString < Test::Unit::TestCase

  def setup
    framework 'Foundation'
    bundle = '/tmp/_test_string.bundle'
    if !File.exist?(bundle) or File.mtime(bundle) < File.mtime(__FILE__)
      s = <<EOS
#import <Foundation/Foundation.h>
@interface MyTestClass : NSObject
@end
@implementation MyTestClass
- (NSString*)testSubstring:(NSString*)str range:(NSRange)range
{
    unichar buf[1024];
    [str getCharacters:buf range:range];
    return [[NSString alloc] initWithCharacters:buf length:range.length];
}
@end
EOS
      File.open('/tmp/_test_string.m', 'w') { |io| io.write(s) }
      system("gcc /tmp/_test_string.m -bundle -o #{bundle} -framework Foundation -fobjc-gc-only") or exit 1
    end
    require 'dl'; DL.dlopen(bundle)
  end
  
  def test_characterAtIndex
    s = 'abcあいうxyz'
    [0,3,6,8].each do |d|
      assert_equal(s[d].ord, s.characterAtIndex(d))
      assert_equal(s[d].ord, s.characterAtIndex(d))
    end
  end
  
  def test_getCharactersRange
    s = 'abcあいうxzy'
    obj = MyTestClass.alloc.init
    [[0,1], [1,7], [4,3], [7,2]].each do |d|
      assert_equal(s[*d], obj.testSubstring(s, range:NSRange.new(*d)))
    end
  end
  
  #
  # Tests for UTF-16 surrogate pairs
  # These tests should be fixed in the future.
  #
  # Memo:
  #   If an NSString consists of a character which is UTF-16 surrogate pair,
  #   the length of the string will be 2 in Cocoa, while 1 in ruby 1.9.
  #
  
  def test_characterAtIndex_surrogate_pairs
    s = "abc\xf0\xa3\xb4\x8exzy"
    assert_equal(s[3].ord, s.characterAtIndex(3))
  end
  
  def test_getCharactersRange_surrogate_pairs
    s = "abc\xf0\xa3\xb4\x8exzy"
    obj = MyTestClass.alloc.init
    assert_equal(s[2,3], obj.testSubstring(s, range:NSRange.new(2,3)))
  end
  
end
