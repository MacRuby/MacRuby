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
