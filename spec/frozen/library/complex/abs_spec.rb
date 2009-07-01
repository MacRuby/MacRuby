require File.dirname(__FILE__) + '/../../shared/complex/abs'
require 'complex'

ruby_version_is ""..."1.9" do
  describe "Complex#abs" do
    it_behaves_like(:complex_abs, :abs)
  end
end
