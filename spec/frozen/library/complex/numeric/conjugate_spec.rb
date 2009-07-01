require File.dirname(__FILE__) + '/../../../spec_helper'
require File.dirname(__FILE__) + '/shared/conjugate'

ruby_version_is ""..."1.9" do

  require 'complex'

  describe "Numeric#conjugate" do
    it_behaves_like :numeric_conjugate, :conjugate
  end
end
