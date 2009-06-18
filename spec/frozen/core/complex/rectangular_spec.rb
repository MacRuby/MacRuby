require File.dirname(__FILE__) + '/../../spec_helper'
require File.dirname(__FILE__) + '/shared/rect'

ruby_version_is "1.9" do
  describe "Complex#rectangular" do
    it_behaves_like(:complex_rect, :rectangular)
  end
end
