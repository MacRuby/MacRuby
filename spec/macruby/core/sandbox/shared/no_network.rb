describe :sandbox_no_network, :shared => true do
  
  it "disallows looking up the address of the current NSHost" do
    unless NSHost.currentHost.address.empty?
      add_line "print NSHost.currentHost.address"
      result.should be_empty
    end
  end
  
  it "disallows DNS requests through NSHost" do
    add_line 'print NSHost.hostWithName("apple.com").address'
    result.should be_empty
  end
  
  it "disallows NSString#stringWithContentsOfURL" do
    add_line 'url = NSURL.URLWithString("http://apple.com")'
    add_line 'print NSString.stringWithContentsOfURL(url)'
    result.should be_empty
  end
  
  it "disallows NSURLConnection.connectionWithRequest:delegate:" do
    add_line 'req = NSURLRequest.requestWithURL(NSURL.URLWithString("http://apple.com"))'
    add_line 'print NSURLConnection.connectionWithRequest(req, delegate:nil)'
    result.should be_empty
  end
end
