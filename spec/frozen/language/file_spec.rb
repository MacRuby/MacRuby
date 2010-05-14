require File.expand_path('../../spec_helper', __FILE__)
require File.expand_path('../../fixtures/code_loading', __FILE__)
require File.expand_path('../shared/__FILE__', __FILE__)
require File.expand_path('../shared/pseudo_variable', __FILE__)

describe "The __FILE__ pseudo-variable" do
  it_behaves_like :language_pseudo_variable, "__FILE__", "/an/example/file/path.rb".dump

  it "equals (eval) inside an eval" do
    eval("__FILE__").should == "(eval)"
  end
end

describe "The __FILE__ pseudo-variable" do
  it_behaves_like :language___FILE__, :require, CodeLoadingSpecs::Method.new
end

describe "The __FILE__ pseudo-variable" do
  it_behaves_like :language___FILE__, :require, Kernel
end

describe "The __FILE__ pseudo-variable" do
  it_behaves_like :language___FILE__, :load, CodeLoadingSpecs::Method.new
end

describe "The __FILE__ pseudo-variable" do
  it_behaves_like :language___FILE__, :load, Kernel
end
