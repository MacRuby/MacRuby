# flip-flop 2 should preserve its state accross different calls to the block it is in
# Test from Tomáš Matoušek of IronRuby
assert "0", %{
  def y *a; yield *a; end
  $a = 0
  def test
    $p = proc do |b, e|
      if b..e
        $a += 1
      else
        $a -= 1
      end
    end

    y false, &$p
    y true, true, &$p
    y false, &$p
    y true, false, &$p
  end
  test
  p $a
}

# flip-flop 3 should preserve its state accross different calls to the block it is in
# Test from Tomáš Matoušek of IronRuby
assert "2", %{
  def y *a; yield *a; end
  $a = 0
  def test
    $p = proc do |b, e|
      if b...e
        $a += 1
      else
        $a -= 1
      end
    end

    y false, &$p
    y true, true, &$p
    y false, &$p
    y true, false, &$p
  end
  test
  p $a
}

# General flip-flop test:
# Not only does it test the general use of a flip-flop operator,
# it also tests the `begin` and `end` block are only evaluated if needed
# Test from Tomáš Matoušek of IronRuby
assert "bfbetetetbetbfbfbf", %{
  F = false
  T = true
  x = X = '!'
  B = [F,T,x,x,x,T,x,F,F]
  E = [x,x,F,F,T,x,T,x,x]

  def b; step('b',B); end
  def e; step('e',E); end

  def step name, value
    r = value[$j]
    putc name
    $j += 1
    $continue = !r.nil?
    r == X ? raise : r
  end

  $j = 0
  $continue = true
  while $continue
    putc (b..e ? 't' : 'f')
  end
}

# Recursive flip-flop test
assert "dadbaddb", %{
  def y *a; yield *a; end
  def test
    $p = proc { |b, e|
      if b..e
        if e..b
          putc 'a'
        else
          putc 'b'
        end
      else
        if e..b
          putc 'c'
        else
          putc 'd'
        end
      end
    }

    y false, &$p
    y true, true, &$p
    y false, &$p
    y true, false, &$p
    y true, true, &$p
    y false, &$p
    y false, &$p
    y true, false, &$p
    puts
  end
  test
}
