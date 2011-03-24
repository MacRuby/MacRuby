require 'hotcocoa'

class Growl
  include HotCocoa

  attr_accessor :delegate
  
  GROWL_IS_READY = 'Lend Me Some Sugar; I Am Your Neighbor!'
  GROWL_NOTIFICATION_CLICKED = 'GrowlClicked!'
  GROWL_NOTIFICATION_TIMED_OUT = 'GrowlTimedOut!'
  GROWL_CLICKED_CONTEXT_KEY = 'ClickedContext'
  
  PRIORITIES = {
    :emergency =>  2,
    :high      =>  1,
    :normal    =>  0,
    :moderate  => -1,
    :very_low  => -2,
  }
  
  def register(app_name, notifications, default_notifications=nil, icon=nil)
    @app_name = app_name
    @app_icon = icon || NSApplication.sharedApplication.applicationIconImage
    @notifications = notifications
    @default_notifications = default_notifications || notifications
    register_to_growl!
  end
  
  def notify(name, title, desc, options={})
    dic = {
      :NotificationName => name,
      :NotificationTitle => title,
      :NotificationDescription => desc,
      :NotificationPriority => PRIORITIES[options[:priority]] || options[:priority] || 0,
      :ApplicationName => @app_name,
      :ApplicationPID => pid,
    }
    dic[:NotificationIcon] = options[:icon].TIFFRepresentation if options[:icon]
    dic[:NotificationSticky] = 1 if options[:sticky]
    
    context = {}
    context[:user_click_context] = options[:click_context] if options[:click_context]
    dic[:NotificationClickContext] = context unless context.empty?
    
    notification :distributed => true, :name => :GrowlNotification, :info => dic
  end
  
  private
  
  def pid
    NSProcessInfo.processInfo.processIdentifier
  end
  
  def register_to_growl!
    on_notification(:distributed => true, :named => GROWL_IS_READY) do |n|
      register_to_growl!
    end

    on_notification(:distributed => true, :named => "#{@app_name}-#{pid}-#{GROWL_NOTIFICATION_CLICKED}") do |n|
      if @delegate and @delegate.respond_to?('growlNotifierClicked:context:')
        ctx = n.userInfo[GROWL_CLICKED_CONTEXT_KEY][:user_click_context]
        @delegate.growlNotifierClicked(self, context:ctx)
      end
    end

    on_notification(:distributed => true, :named => "#{@app_name}-#{pid}-#{GROWL_NOTIFICATION_TIMED_OUT}") do |n|
      if @delegate and @delegate.respond_to?('growlNotifierTimedOut:context:')
        ctx = n.userInfo[GROWL_CLICKED_CONTEXT_KEY][:user_click_context]
        @delegate.growlNotifierTimedOut(self, context:ctx)
      end
    end
  
    dic = {
      :ApplicationName => @app_name,
      :ApplicationIcon => @app_icon.TIFFRepresentation,
      :AllNotifications => @notifications,
      :DefaultNotifications => @default_notifications,
    }
    notification :distributed => true, :name => :GrowlApplicationRegistrationNotification, :info => dic
  end
end
