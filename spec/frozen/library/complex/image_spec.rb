require File.dirname(__FILE__) + '/../../spec_helper'

ruby_version_is ""..."1.9" do

  require 'complex'
  require File.dirname(__FILE__) + '/shared/image'

  describe "Complex#image" do
    it_behaves_like(:complex_image, :image)
  end
end
