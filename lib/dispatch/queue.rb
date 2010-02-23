# Adds convenience methods to Queues

module Dispatch
  class Queue
    
    def fork(job = nil, &block)
      if job.nil?
        Job.new block
      else
        async(job.group) { block.call }
      end
    end
    
  end
end
