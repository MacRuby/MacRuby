module Dispatch
  class Queue
    # Combines +&block+ up to +stride+ times before passing to Queue::Apply
    def stride(count, stride, &block)
      n_strides = (count / stride).to_int
      apply(n_strides) do |i|
        (i*stride...(i+1)*stride).each { |j| block.call(j) }
      end
      (n_strides*stride...count).each { |j| block.call(j) }
    end
  end
end
