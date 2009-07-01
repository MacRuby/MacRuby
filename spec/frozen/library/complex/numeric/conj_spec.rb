require File.dirname(__FILE__) + '/../../../spec_helper'
require File.dirname(__FILE__) + '/shared/conjugate'

ruby_version_is ""..."1.9" do
  
  require 'complex'

  describe "Numeric#conj" do
    it_behaves_like :numeric_conjugate, :conj
  end
end
