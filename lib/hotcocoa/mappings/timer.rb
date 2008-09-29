HotCocoa::Mappings.map :timer => :NSTimer do

  defaults :scheduled => true, :repeats => false

  def alloc_with_options(options)
    raise ArgumentError, "timer requires :interval" unless options.has_key?(:interval)
    target = options.delete(:target)
    selector = options.delete(:selector)
    if !target || !selector
      raise ArgumentError, "timer requires either :target and :selector or :on_action" unless options.has_key?(:on_action)
      target = Object.new
      target.instance_variable_set(:@block, options.delete(:on_action))
      def target.fire(timer)
        @block.call(timer)
      end
      selector = "fire:"
    end
    if options.delete(:scheduled)
      NSTimer.scheduledTimerWithTimeInterval(options.delete(:interval), target:target, selector:selector, userInfo:options.delete(:info), repeats:options.delete(:repeats))
    else
      NSTimer.timerWithTimeInterval(options.delete(:interval), target:target, selector:selector, userInfo:options.delete(:info), repeats:options.delete(:repeats))
    end
  end
  
end