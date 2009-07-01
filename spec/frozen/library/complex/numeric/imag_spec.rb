require File.dirname(__FILE__) + '/../../../spec_helper'
require File.dirname(__FILE__) + '/shared/image'

ruby_version_is ""..."1.9" do

  require 'complex'

  describe "Numeric#imag" do
    it_behaves_like :numeric_image, :imag
  end
end
