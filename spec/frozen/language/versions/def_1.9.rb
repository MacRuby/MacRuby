describe "An instance method definition with a splat" do
  it "raises a SyntaxError when invoked with an inline hash argument" do
    def foo(a,b,*c); [a,b,c] end

    lambda do
      instance_eval "foo('abc', 'specs' => 'fail sometimes', 'on' => '1.9', *[789, 'yeah'])"
    end.should raise_error(SyntaxError)
  end
end