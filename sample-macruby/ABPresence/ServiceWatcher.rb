class ServiceWatcher

  attr_accessor :dataSource

  def startMonitoring
    nCenter = IMService.notificationCenter
    nCenter.addObserver self,
      selector:'imPersonStatusChangedNotification:',
      name:IMPersonStatusChangedNotification,
      object:nil
    nCenter.addObserver self,
      selector:'imPersonInfoChangedNotification:',
      name:IMPersonInfoChangedNotification,
      object:nil
  end
  
  def stopMonitoring
    IMService.notificationCenter.removeObserver self
  end

  def forwardToObservers notification
    service = notification.object
    screenName = notification.userInfo.objectForKey IMPersonScreenNameKey
    nCenter = NSNotificationCenter.defaultCenter
    people = service.peopleWithScreenName screenName
    if people
      nCenter = NSNotificationCenter.defaultCenter
      people.each do |person|
        @dataSource.rebuildStatusInformationForPerson person
      end
    end
  end

  # Received from IMService's custom notification center. Posted when a different user (screenName) logs in, logs off, goes away, 
  # and so on. This notification is for the IMService object.The user information dictionary will always contain an 
  # IMPersonScreenNameKey and an IMPersonStatusKey, and no others.
  def imPersonStatusChangedNotification notification
    forwardToObservers notification
  end

  # Received from IMService's custom notification center. Posted when a screenName changes some aspect of their published information. 
  # This notification is for the IMService object. The user information dictionary will always contain an IMPersonScreenNameKey and may 
  # contain any of the following keys as described by "Dictionary Keys" in this document: <tt>IMPersonStatusMessageKey, IMPersonIdleSinceKey, 
  # IMPersonFirstNameKey, IMPersonLastNameKey, IMPersonEmailKey, IMPersonPictureDataKey, IMPersonAVBusyKey, IMPersonCapabilitiesKey</tt>.
  # If a particular attribute has been removed, the value for the relevant key will be NSNull.
  def imPersonInfoChangedNotification notification 
    forwardToObservers notification
  end
end