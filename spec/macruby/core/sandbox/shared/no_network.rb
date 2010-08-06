describe :sandbox_no_network, :shared => true do
  
  it "disallows DNS requests through NSHost" do
    add_line 'print NSHost.hostWithName("apple.com").address'
    result.should be_empty
  end
  
  it "disallows NSString#stringWithContentsOfURL" do
    add_line 'url = NSURL.URLWithString("http://apple.com")'
    add_line 'print NSString.stringWithContentsOfURL(url)'
    result.should be_empty
  end
end
