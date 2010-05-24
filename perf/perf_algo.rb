def fib(n)
  if n < 3
    1
  else
    fib(n - 1) + fib(n - 2)
  end
end

perf_test('fib') { fib(37) }

def tak(x, y, z)
  unless y < x
    z
  else
    tak(tak(x-1, y, z),
        tak(y-1, z, x),
        tak(z-1, x, y))
  end
end

perf_test('tak') { tak(18, 9, 0) }

def ack(m, n)
  if m == 0 then
    n + 1
  elsif n == 0 then
    ack(m - 1, 1)
  else
    ack(m - 1, ack(m, n - 1))
  end
end

perf_test('ack') { ack(3, 9) }

def mandelbrot_i(x, y)
  cr = y - 0.5
  ci = x
  zi = 0.0
  zr = 0.0
  i = 0

  while true
    i += 1
    temp = zr * zi
    zr2 = zr * zr
    zi2 = zi * zi
    zr = zr2 - zi2 + cr
    zi = temp + temp + ci
    return i if (zi2 + zr2 > 16)
    return 0 if (i > 1000)
  end
end

def mandelbrot
  y = -39
  while y < 39
    x = -39
    while x < 39
      i = mandelbrot_i(x / 40.0, y/40.0)
      x += 1
    end
    y += 1
  end
end

perf_test('mandelbrot') { mandelbrot }
