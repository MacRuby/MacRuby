require File.dirname(__FILE__) + '/../../shared/complex/denominator'

ruby_version_is "1.9" do
  describe "Complex#denominator" do
    it_behaves_like(:complex_denominator, :denominator)
  end
end
