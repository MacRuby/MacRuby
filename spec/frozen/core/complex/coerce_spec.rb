require File.dirname(__FILE__) + '/../../shared/complex/coerce'

ruby_version_is "1.9" do
  describe "Complex#coerce" do
    it_behaves_like(:complex_coerce, :coerce)
  end
end
