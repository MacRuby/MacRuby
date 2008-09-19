HotCocoa::Mappings.map :notification => :NSNotification do

  defaults :post => true, :distributed => false
  
  def alloc_with_options(options)
    if options.delete(:post)
      if options.delete(:distributed)
        NSDistributedNotificationCenter.defaultCenter.postNotificationWithName options.delete(:name), object:options.delete(:object), userInfo:options.delete(:info), deliverImmediately:(options.delete(:immediately) == true)
      else
        NSNotificationCenter.defaultCenter.postNotificationWithName options.delete(:name), object:options.delete(:object), userInfo:options.delete(:info)
      end
    else
      NSNotification.notificationWithName options.delete(:name), object:options.delete(:object), userInfo:options.delete(:info)
    end
  end
  
end
