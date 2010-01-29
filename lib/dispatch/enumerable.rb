# Additional parallel operations for any object supporting +each+

module Dispatch
  module Enumerable
    # Parallel +each+
    def p_each(&block)
      grp = Group.new
      self.each do |obj|
        Dispatch.group(grp) { block.call(obj) }        
      end
      grp.wait
    end

    # Parallel +each_with_index+
    def p_each_with_index(&block)
      grp = Group.new
      self.each_with_index do |obj, i|
        Dispatch.group(grp) { block.call(obj, i) }
      end
      grp.wait
    end

    # Parallel +inject+ (only works if commutative)
    def p_inject(initial=0, &block)
      @result = Dispatch.wrap(initial)
      self.p_each { |obj| block.call(@result, obj) }
      @result
    end

    # Parallel +collect+
    def p_map(&block)
      result = Dispatch.wrap(Array)
      self.p_each_with_index do |obj, i|
        result[i] = block.call(obj)
      end
      result
    end

    # Parallel +detect+
    def p_find(&block)
      @done = false
      @result = nil
      self.p_each_with_index do |obj, i|
        if not @done
          if true == block.call(obj)
            @done = true
            @result = obj
          end
        end
      end
      @result
    end
  end
end
