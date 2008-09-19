module HotCocoa
  
  class NotificationListener
    
    DistributedBehaviors = {
      :drop                 => NSNotificationSuspensionBehaviorDrop,
      :coalesce             => NSNotificationSuspensionBehaviorCoalesce,
      :hold                 => NSNotificationSuspensionBehaviorHold,
      :deliver_immediately  => NSNotificationSuspensionBehaviorDeliverImmediately
    }
    
    attr_reader :callback, :name, :sender, :suspension_behavior
    
    def initialize(options={}, &block)
      @callback = block
      @distributed = (options[:distributed] == true)
      @suspension_behavior = DistributedBehaviors[options[:when_suspended] || :coalesce]
      @name = options[:named]
      @sender = options[:sent_by]
      observe
    end
    
    def distributed?
      @distributed
    end
    
    def receive(notification)
      callback.call(notification)
    end
    
    def stop_notifications(options={})
      if options.has_key?(:named) || options.has_key?(:sent_by)
        notification_center.removeObserver(self, name:options[:named], object:options[:sender])
      else
        notification_center.removeObserver(self)
      end
    end
    
    private
    
      def observe
        if distributed?
          notification_center.addObserver self, selector:'receive:', name:name, object:sender, suspensionBehavior:suspension_behavior
        else
          notification_center.addObserver self, selector:'receive:', name:name, object:sender
        end
      end
      
      def notification_center
        @notification_center ||= (distributed? ? NSDistributedNotificationCenter.defaultCenter : NSNotificationCenter.defaultCenter)
      end
  end
  
  def on_notification(options={}, &block)
    NotificationListener.new(options, &block)
  end
end