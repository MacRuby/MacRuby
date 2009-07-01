require File.dirname(__FILE__) + '/../../../spec_helper'
require File.dirname(__FILE__) + '/shared/image'

ruby_version_is ""..."1.9" do

  require 'complex'

  describe "Numeric#image" do
    it_behaves_like :numeric_image, :image
  end
end
