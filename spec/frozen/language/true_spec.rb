require File.expand_path('../../spec_helper', __FILE__)
require File.expand_path('../shared/pseudo_variable', __FILE__)

describe "The true pseudo-variable" do
  it_behaves_like :language_pseudo_variable, "true", "something else".dump
end

