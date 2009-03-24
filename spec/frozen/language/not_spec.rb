require File.dirname(__FILE__) + '/../spec_helper'

describe "The not keyword" do
  it "negates a `true' value" do
    (not true).should be_false
    (not 'true').should be_false
  end

  it "negates a `false' value" do
    (not false).should be_true
    (not nil).should be_true
  end
end

describe "The `!' keyword" do
  it "negates a `true' value" do
    (!true).should be_false
    (!'true').should be_false
  end

  it "negates a `false' value" do
    (!false).should be_true
    (!nil).should be_true
  end
end