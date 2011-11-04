describe "A Range object" do

  it "gets turned into an NSRange struct when passed to an Objective-C method" do
    ary = (0..10).to_a

    [0..10, 2..10, 0...10, 2...10].each do |range|
      ary.subarrayWithRange(range).should == ary.slice(range)
    end
  end

  it "responds to #relative_to, which returns a new Range object without negative indicies" do
    ( 0 ..  10).relative_to(11).should == (0..10)
    ( 1 ..  9 ).relative_to(11).should == (1..9 )
    ##
    # TODO: This spec fails right now, though it is the intended behaviour.
    # ( 0 ..  15).relative_to(11).should == (0..10)

    ( 0 .. -1 ).relative_to(11).should == (0..10)
    ( 0 .. -2 ).relative_to(11).should == (0..9 )
    ( 1 .. -2 ).relative_to(11).should == (1..9 )

    (-11..  10).relative_to(11).should == (0..10)
    (-5 ..  10).relative_to(11).should == (6..10)
    (-10.. -5 ).relative_to(11).should == (1..6 )

    ( 4 ... 11).relative_to(11).should == (4..10)
    ( 4 ... 10).relative_to(11).should == (4..9 )
    ( 4 ...-1 ).relative_to(11).should == (4..9 )
    (-11...-1 ).relative_to(11).should == (0..9 )
  end

end
