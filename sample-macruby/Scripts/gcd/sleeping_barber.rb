# A GCD-based implementation of the sleeping barber problem:
#   http://en.wikipedia.org/wiki/Sleeping_barber_problem
#   http://www.madebysofa.com/archive/blog/the-sleeping-barber/

waiting_chairs = Dispatch::Queue.new('com.apple.waiting_chairs')
semaphore = Dispatch::Semaphore.new(3)
index = -1
while true
  index += 1
  success = semaphore.wait(Dispatch::TIME_NOW)
  unless success
    puts "Customer turned away #{index}"
    next
  end
  waiting_chairs.async do
    semaphore.signal
    puts "Shave and a haircut #{index}"
  end
end
