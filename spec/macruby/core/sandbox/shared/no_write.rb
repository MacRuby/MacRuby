describe :sandbox_no_write, :shared => true do
  
  it "prevents NSString#writeToFile from writing any characters" do
    with_temporary_file do |tmp|
      add_line "print 'hello'.writeToFile('#{tmp}', atomically:true)"
      result.to_i.should == 0
    end
  end
  
end
