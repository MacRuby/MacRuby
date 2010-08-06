describe :sandbox_no_write, :shared => true do
  
  before do
    @code = "error = Pointer.new_with_type('@'); "
    @filename = fixture('spec/macruby/core/sandbox/shared', 'sample_file.txt')
  end
  
  it "prevents Objective-C methods from writing to a file" do
    @code << "print 'hello'.writeToFile('#{@filename}', atomically:true)"
    ruby_exe(@code).to_i.should == 0
  end
  
  it "prevents Ruby methods from writing to a file" do
    @code << "open('#{@filename}'); file.puts 'this must fail'"
    ruby_exe(@code).should =~ /Errno::EPERM/
  end
  
  it "prevents otherwise changing file attributes through the File module" do
    @code << "File.chmod(0777, '#{@filename}')"
    ruby_exe(@code).should =~ /Errno::EPERM/
  end
end
