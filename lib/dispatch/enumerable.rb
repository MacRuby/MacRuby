# Additional parallel operations for any object supporting +each+

module Enumerable
  # Parallel +each+
  def p_each(&block)
    grp = Dispatch::Group.new
    self.each do |obj|
      Dispatch.group(grp) { block.call(obj) }        
    end
    grp.wait
  end

  # Parallel +each_with_index+
  def p_each_with_index(&block)
    grp = Dispatch::Group.new
    self.each_with_index do |obj, i|
      Dispatch.group(grp) { block.call(obj, i) }
    end
    grp.wait
  end

  # Parallel +collect+
  # Results match the order of the original array
  def p_map(&block)
    result = Dispatch.wrap(Array)
    self.p_each_with_index do |obj, i|
      result[i] = block.call(obj)
    end
    result._done_
  end

  # Parallel +collect+ plus +inject+
  # Accumulates from +initial+ via +op+ (default = '+')
  def p_mapreduce(result, op=:+, &block)
    raise ArgumentError if not result.respond_to? op
    # Since exceptions from a Dispatch block act funky 
    q = Dispatch.queue_for(result)
    self.p_each do |obj|
      val = block.call(obj)
      q.async do
        result = result.send(op, val)
      end
    end
    q.sync {}
    result
  end


  # Parallel +select+; will return array of objects for which
  # +&block+ returns true.
  def p_find_all(&block)
    found_all = Dispatch.wrap(Array)
    self.p_each { |obj| found_all << obj if block.call(obj) }
    found_all._done # will this leak?
  end

  # Parallel +detect+; will return -one- match for +&block+
  # but it may not be the 'first'
  # Only useful if the test block is very expensive to run
  def p_find(&block)
    found = Dispatch.wrap(nil)
    self.p_each do |obj|
      found = found.nil? ? block.call(obj) : nil
      found = obj if found and found.nil? 
    end
    found._done # will this leak?
  end
end
