module Dispatch
  class Queue
    # Combines +&block+ up to +stride+ times before passing to Queue::Apply
    def stride(count, stride=1, &block)
      sub_count = (count / stride).to_int
      puts "\nsub_count: #{sub_count} (#{count} / #{stride})"
      apply(sub_count) do |i|
        i0 = i*stride
        (i0..i0+stride).each { |j| "inner #{j}"; block.call(j) } #nested dispatch blocks fails
      end
      done = sub_count*stride;
      puts "\ndone: #{done} (#{sub_count}*#{stride})"
      (done..count).each { |j| p "inner #{j}"; block.call(j) }
    end
  end
end
