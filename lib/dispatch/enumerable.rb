module Dispatch
  module Enumerable
    def p_map(&block)
      result = []
      # We will access the `result` array from within this serial queue,
      # as without a GIL we cannot assume array access to be thread-safe.
      result_queue = Dispatch::Queue.new('access-queue.#{result.object_id}')
      # Uses Dispatch::Queue#apply to submit many blocks at once
      Dispatch::Queue.concurrent.apply(size) do |idx|
        # run the block in the concurrent queue to maximize parallelism
        temp = block(self[idx])
        # do only the assignment on the serial queue
        result_queue.async { result[idx] = temp }
      end
      result
    end
  end
end
