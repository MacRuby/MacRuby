require File::join( File::dirname(__FILE__), %w{ .. .. spec_helper } )
require File::join( File::dirname(__FILE__), %w{ shared behavior } )

ruby_version_is "1.9".."1.9.9" do

  describe "BasicObject class" do

    it "has no ancestors" do
      BasicObject.ancestors.should == [ BasicObject ]
    end
    
    it "is a class" do
      ( Class === BasicObject ).should == true
    end

  end
end

