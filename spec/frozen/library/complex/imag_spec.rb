require File.dirname(__FILE__) + '/../../spec_helper'
require File.dirname(__FILE__) + '/shared/image'

ruby_version_is ""..."1.9" do
  require 'complex'

  describe "Complex#imag" do
    it_behaves_like(:complex_image, :imag)
  end
end
