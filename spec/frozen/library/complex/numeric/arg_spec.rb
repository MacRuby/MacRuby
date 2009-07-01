require File.dirname(__FILE__) + '/../../../spec_helper'
require File.dirname(__FILE__) + '/shared/arg'

ruby_version_is ""..."1.9" do

  require 'complex'

  describe "Numeric#arg" do
    it_behaves_like :numeric_arg, :arg
  end
end
