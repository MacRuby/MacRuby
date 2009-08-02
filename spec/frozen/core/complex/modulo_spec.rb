require File.dirname(__FILE__) + '/../../shared/complex/modulo'

=begin # Looks like Complex#% doesn't work even with ruby 1.9.2
ruby_version_is "1.9" do
  describe "Complex#% with Complex" do
    it_behaves_like(:complex_modulo_complex, :%)
  end

  describe "Complex#% with Integer" do
    it_behaves_like(:complex_modulo_integer, :%)
  end

  describe "Complex#% with Object" do
    it_behaves_like(:complex_modulo_object, :%)
  end
end
=end
