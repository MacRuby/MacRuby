describe :sandbox_no_write, :shared => true do
  
  it "prevents NSString#writeToFile from writing any characters" do
    with_temporary_file do |tmp|
      add_line "print 'hello'.writeToFile('#{tmp}', atomically:true)"
      result.to_i.should == 0
    end
  end
  
  it "throws an error when trying to write to a Ruby IO object" do
    with_temporary_file do |fn|
      add_line "open('#{fn}', 'w').puts 'This must fail'"
      result.should_not be_empty
    end
  end
  
end
