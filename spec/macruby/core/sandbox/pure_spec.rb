require File.expand_path(File.dirname(__FILE__) + '/../../spec_helper')
require File.expand_path('../shared/no_write', __FILE__)

describe "Sandbox.pure_computation" do
  
  # These tests are applicable beyond just the pure_computation spec.
  # Eventually the tests themselves will be farmed out to /sandbox/shared
  # and all sandbox specs will just be aggregations of should_behave_like calls.
  
  it_behaves_like :sandbox_no_write, :no_write
  
  before do
    @code << "Sandbox.pure_computation.apply!; "
  end
  
end