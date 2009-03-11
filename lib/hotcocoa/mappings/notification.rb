HotCocoa::Mappings.map :notification => :NSNotification do

  defaults :post => true, :distributed => false
  
  def alloc_with_options(options)
    if options.delete(:post)
      if options.delete(:distributed)
        NSDistributedNotificationCenter.defaultCenter.postNotificationName options.delete(:name), object:options.delete(:object), userInfo:options.delete(:info), deliverImmediately:(options.delete(:immediately) == true)
      else
        NSNotificationCenter.defaultCenter.postNotificationName options.delete(:name), object:options.delete(:object), userInfo:options.delete(:info)
      end
    else
      NSNotification.notificationName options.delete(:name), object:options.delete(:object), userInfo:options.delete(:info)
    end
  end
  
end
