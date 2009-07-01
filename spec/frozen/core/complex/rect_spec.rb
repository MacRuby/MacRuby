require File.dirname(__FILE__) + '/../../shared/complex/rect'

ruby_version_is "1.9" do
  describe "Complex#rect" do
    it_behaves_like(:complex_rect, :rect)
  end
end
