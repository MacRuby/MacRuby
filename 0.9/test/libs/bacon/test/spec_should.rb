require File.expand_path('../../lib/bacon', __FILE__)

describe "#should shortcut for #it('should')" do
  
  should "be called" do
    @called = true
    @called.should.be == true
  end
  
  should "save some characters by typing should" do
    lambda { should.satisfy { 1 == 1 } }.should.not.raise
  end

  should "save characters even on failure" do
    lambda { should.satisfy { 1 == 2 } }.should.raise Bacon::Error
  end

  should "work nested" do
    should.satisfy {1==1}
  end
  
  count = Bacon::Counter[:specifications]
  should "add new specifications" do
    # XXX this should +=1 but it's +=2
    (count+1).should == Bacon::Counter[:specifications]
  end

  should "have been called" do
    @called.should.be == true
  end

end
