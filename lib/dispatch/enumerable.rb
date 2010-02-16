# Additional parallel operations for any object supporting +each+

class Integer
  # Applies the +&block+ +Integer+ number of times in parallel
  # -- passing in stride (default 1) iterations at a time --
  # on a concurrent queue of the given (optional) +priority+
  # 
  #   @sum = 0
  #   10.p_times(3) { |j| @sum += j }
  #   p @sum # => 55
  #
  def p_times(stride=1, priority=nil, &block)
    n_times = self.to_int
    puts "\np_times: n_times=#{n_times}, stride=#{stride}"
    q = Dispatch::Queue.concurrent(priority)
    n_strides = (n_times / stride).to_int
    q.apply(n_strides) do |i|
      j0 = i*stride
      stride.times { |j| block.call(j0+j); puts "\n#{i}=>#{j0}:j=#{j}" }
    end
    # Runs the remainder (if any) sequentially on the current thread
    (n_strides*stride).upto(n_times - 1) { |j| block.call(j); puts "\nj'=#{j}" }
  end
end

module Enumerable

  # Parallel +each+
  def p_each(stride=1, priority=nil,  &block)
    ary = self.to_a
    size.p_times(stride, priority) { |i| block.call(ary[i]) }
  end

  # Parallel +each+
  def p_each_with_index(stride=1, priority=nil,  &block)
    ary = self.to_a
    size.p_times(stride, priority) { |i| block.call(ary[i], i) }
  end

  # Parallel +collect+
  # Results match the order of the original array
  def p_map(stride=1, priority=nil,  &block)
    result = Dispatch.wrap(Array)
    self.p_each_with_index(stride, priority) { |obj, i| result[i] = block.call(obj) }
    result._done_
  end

  # Parallel +collect+ plus +inject+
  # Accumulates from +initial+ via +op+ (default = '+')
  # Note: each object will only run one p_mapreduce at a time
  def p_mapreduce(initial, op=:+, &block)
    # Check first, since exceptions from a Dispatch block can act funky 
    raise ArgumentError if not initial.respond_to? op
    # TODO: assign from within a Dispatch.once to avoid race condition
    @mapreduce_q ||= Dispatch.queue(self)
    @mapreduce_q.sync do # in case called more than once at a time
      @mapreduce_result = initial
      q = Dispatch.queue(@mapreduce_result)
      self.p_each do |obj|
        val = block.call(obj)
        q.async { @mapreduce_result = @mapreduce_result.send(op, val) }
      end
      q.sync {}
      return @mapreduce_result
    end
  end

  # Parallel +select+; will return array of objects for which
  # +&block+ returns true.
  def p_find_all(stride=1, priority=nil,  &block)
    found_all = Dispatch.wrap(Array)
    self.p_each(stride, priority) { |obj| found_all << obj if block.call(obj) }
    found_all._done_
  end

  # Parallel +detect+; will return -one- match for +&block+
  # but it may not be the 'first'
  # Only useful if the test block is very expensive to run
  # Note: each object can only run one p_find at a time

  def p_find(stride=1, priority=nil,  &block)
    @find_q ||= Dispatch.queue(self)
    @find_q.sync do 
      @find_result = nil
      q = Dispatch.queue(@find_result)
      self.p_each(stride, priority) do |obj|
        if @find_result.nil?
          found = block.call(obj)
          q.async { @find_result = obj if found }
        end
      end
      q.sync {} #if @find_result.nil?
      return @find_result
    end
  end
end
