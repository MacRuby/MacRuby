require File.expand_path('../../../spec_helper', __FILE__)
require File.expand_path('../fixtures/common', __FILE__)

describe "YAML.dump_stream" do
  it "returns an empty string when not passed any objects" do
    YAML.dump_stream.should == ""
  end

  # 
  # We used to compare against []\n\n and {}\n\n, but this was overdetermining the format 
  # just to match Syck
  #
  it "returns a YAML stream containing the objects passed" do
    YAML.dump_stream('foo', 20, [], {}).should =~ /--- foo\n--- 20\n--- \[\]\n+--- \{\}\n+/
  end
end
