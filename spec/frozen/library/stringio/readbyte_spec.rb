require File.dirname(__FILE__) + '/../../spec_helper'
require 'stringio'
require File.dirname(__FILE__) + "/shared/readbyte"


ruby_version_is "1.8.7" do
  
  describe "StringIO#readbyte" do
    it_behaves_like :stringio_readbyte, :readbyte
  end

  describe "StringIO#readbyte when self is not readable" do
    it_behaves_like :stringio_readbyte_not_readable, :readbyte
  end
end