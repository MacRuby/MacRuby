require File.dirname(__FILE__) + '/../../spec_helper'
require File.dirname(__FILE__) + '/shared/imag'

ruby_version_is "1.9" do
  describe "Numeric#imag" do
    it_behaves_like(:numeric_imag, :imag)
  end
end
