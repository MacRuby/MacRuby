framework 'Cocoa'

module Growl
  class Notifier
    VERSION = '1.0.1'
    
    GROWL_IS_READY = "Lend Me Some Sugar; I Am Your Neighbor!"
    GROWL_NOTIFICATION_CLICKED = "GrowlClicked!"
    GROWL_NOTIFICATION_TIMED_OUT = "GrowlTimedOut!"
    GROWL_KEY_CLICKED_CONTEXT = "ClickedContext"
    
    PRIORITIES = {
      :emergency =>  2,
      :high      =>  1,
      :normal    =>  0,
      :moderate  => -1,
      :very_low  => -2,
    }
    
    class << self
      # Returns the singleton instance of Growl::Notifier with which you register and send your Growl notifications.
      def sharedInstance
        @sharedInstance ||= alloc.init
      end
    end
    
    attr_reader :application_name, :application_icon, :notifications, :default_notifications
    attr_accessor :delegate
    
    # Registers the applications metadata and the notifications, that your application might send, to Growl.
    # The +default_notifications+ are notifications that will be enabled by default, the regular +notifications+ are
    # optional and should be enabled by the user in the Growl system preferences.
    #
    # Register the applications name and the notifications that will be used.
    # * +default_notifications+ defaults to the regular +notifications+.
    # * +application_icon+ defaults to NSApplication.sharedApplication.applicationIconImage.
    #
    #   Growl::Notifier.sharedInstance.register 'FoodApp', ['YourHamburgerIsReady', 'OhSomeoneElseAteIt']
    #
    # Register the applications name, the notifications plus the default notifications that will be used and the icon that's to be used in the Growl notifications.
    #
    #   Growl::Notifier.sharedInstance.register 'FoodApp', ['YourHamburgerIsReady', 'OhSomeoneElseAteIt'], ['DefaultNotification], NSImage.imageNamed('GreasyHamburger')
    def register(application_name, notifications, default_notifications = nil, application_icon = nil)
      @application_name, @application_icon = application_name, (application_icon || NSApplication.sharedApplication.applicationIconImage)
      @notifications, @default_notifications = notifications, (default_notifications || notifications)
      @callbacks = {}
      send_registration!
    end
    
    # Sends a Growl notification.
    #
    # * +notification_name+ : the name of one of the notifcations that your apllication registered with Growl. See register for more info.
    # * +title+ : the title that should be used in the Growl notification.
    # * +description+ : the body of the Grow notification.
    # * +options+ : specifies a few optional options:
    #   * <tt>:sticky</tt> : indicates if the Grow notification should "stick" to the screen. Defaults to +false+.
    #   * <tt>:priority</tt> : sets the priority level of the Growl notification. Defaults to 0.
    #   * <tt>:icon</tt> : specifies the icon to be used in the Growl notification. Defaults to the registered +application_icon+, see register for more info.
    #
    # Simple example:
    #
    #   name = 'YourHamburgerIsReady'
    #   title = 'Your hamburger is ready for consumption!'
    #   description = 'Please pick it up at isle 4.'
    #   
    #   Growl::Notifier.sharedInstance.notify(name, title, description)
    #
    # Example with optional options:
    #
    #   Growl::Notifier.sharedInstance.notify(name, title, description, :sticky => true, :priority => 1, :icon => NSImage.imageNamed('SuperBigHamburger'))
    #
    # When you pass notify a block, that block will be used as the callback handler if the Growl notification was clicked. Eg:
    #
    #   Growl::Notifier.sharedInstance.notify(name, title, description, :sticky => true) do
    #     user_clicked_notification_so_do_something!
    #   end
    def notify(notification_name, title, description, options = {}, &callback)
      dict = {
        :ApplicationName => @application_name,
        :ApplicationPID => pid,
        :NotificationName => notification_name,
        :NotificationTitle => title,
        :NotificationDescription => description,
        :NotificationPriority => PRIORITIES[options[:priority]] || options[:priority] || 0
      }
      dict[:NotificationIcon] = options[:icon].TIFFRepresentation if options[:icon]
      dict[:NotificationSticky] = 1 if options[:sticky]
      
      context = {}
      context[:user_click_context] = options[:click_context] if options[:click_context]
      if block_given?
        @callbacks[callback.object_id] = callback
        context[:callback_object_id] = callback.object_id.to_s
      end
      dict[:NotificationClickContext] = context unless context.empty?
      
      notification_center.postNotificationName :GrowlNotification, object:nil, userInfo:dict, deliverImmediately:true
    end
    
    def onReady(notification)
      send_registration!
    end
    
    def onClicked(notification)
      user_context = nil
      if context = notification.userInfo[GROWL_KEY_CLICKED_CONTEXT]
        user_context = context[:user_click_context]
        if callback_object_id = context[:callback_object_id]
          @callbacks.delete(callback_object_id.to_i).call
        end
      end
      
      @delegate.growlNotifierClicked_context(self, user_context) if @delegate && @delegate.respond_to?(:growlNotifierClicked_context)
    end
    
    def onTimeout(notification)
      user_context = nil
      if context = notification.userInfo[GROWL_KEY_CLICKED_CONTEXT]
        @callbacks.delete(context[:callback_object_id].to_i) if context[:callback_object_id]
        user_context = context[:user_click_context]
      end
      
      @delegate.growlNotifierTimedOut_context(self, user_context) if @delegate && @delegate.respond_to?(:growlNotifierTimedOut_context)
    end
    
    private
    
    def pid
      NSProcessInfo.processInfo.processIdentifier.to_i
    end
    
    def notification_center
      NSDistributedNotificationCenter.defaultCenter
    end
    
    def send_registration!
      add_observer 'onReady:', GROWL_IS_READY, false
      add_observer 'onClicked:', GROWL_NOTIFICATION_CLICKED, true
      add_observer 'onTimeout:', GROWL_NOTIFICATION_TIMED_OUT, true
      
      dict = {
        :ApplicationName => @application_name,
        :ApplicationIcon => application_icon.TIFFRepresentation,
        :AllNotifications => @notifications,
        :DefaultNotifications => @default_notifications
      }
      
      notification_center.postNotificationName :GrowlApplicationRegistrationNotification, object:nil, userInfo:dict, deliverImmediately:true
    end
    
    def add_observer(selector, name, prepend_name_and_pid)
      name = "#{@application_name}-#{pid}-#{name}" if prepend_name_and_pid
      notification_center.addObserver self, selector:selector, name:name, object:nil
    end
  end
end
