assert 'nil', 'p case when false then 0 else end'
assert 'nil', 'p case when true then else 1 end'
assert '1', 'p case when false then 0 when true then 1 else 3 end'
assert '1', 'p case when false then 0 when true then 1 end'
assert '1', 'p case 1 when 1 then 1 else 2 end'
assert '2', 'p case -1 when 1 then 1 else 2 end'
assert ':fixnum', "p case 1 when Fixnum then :fixnum else :not_fixnum end"
assert ':string', "p case '' when Fixnum then :fixnum when String then :string else :other end"
assert ':fixnum_or_string', "p case '' when Fixnum, String then :fixnum_or_string else :other end"
assert '1', 'p case 1 when 2, 1 then 1 else 2 end'
assert '2', 'p case when false then 1 when nil, true then 2 else 3 end'
assert "1\n2\n:foobar", %{
  def foo() p 1 end
  def bar() p 2 end
  p case 2
  when foo, bar
    :foobar
  end
}
assert "1\n:foobar", %{
  def foo() p 1 end
  def bar() p 2 end
  p case 1
  when foo, bar
    :foobar
  end
}
assert '1', 'p case 1 when *[2, 1] then 1 else 2 end'
assert '1', 'a = [7, 8]; p case 1 when *a, *[4, 5], 1 then 1 else 2 end'
