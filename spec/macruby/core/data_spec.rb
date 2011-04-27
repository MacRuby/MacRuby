# -*- coding: utf-8 -*-

describe 'An NSData instance' do

  it 'responds to #to_str and returns a UTF8 encoded string' do
    vowels = 'aeiou'
    vowels.dataUsingEncoding(NSASCIIStringEncoding).to_str.should == vowels

    unicode_string = 'あえいおう'
    unicode_string.to_data.to_str.should == unicode_string
  end

end
