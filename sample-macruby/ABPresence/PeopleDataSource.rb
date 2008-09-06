class PeopleDataSource

  attr_accessor :table, :serviceWatcher

  # Initialize and register for AddressBook notifications
  def awakeFromNib 
    @imPersonStatus = []
    @abPeople = []

    nCenter = NSNotificationCenter.defaultCenter
    nCenter.addObserver self,
      selector:'abDatabaseChangedExternallyNotification:',
      name:KABDatabaseChangedExternallyNotification,
      object:nil

    reloadABPeople
    @serviceWatcher.startMonitoring
  end

  # Data Loading
  def bestStatusForPerson(person)
    bestStatus = IMPersonStatusOffline # Let's assume they're offline to start
    IMService.allServices.each do |service|
      snames = service.screenNamesForPerson person
      if snames
        snames.each do |screenName|
          dict = service.infoForScreenName screenName
          next if dict.nil?
          status = dict[IMPersonStatusKey]
          next if status.nil?
          thisStatus = status.intValue
          if thisStatus > bestStatus
            bestStatus = thisStatus 
          end
        end
      end
    end
    return bestStatus
  end

  # This dumps all the status information and rebuilds the array against the current @abPeople
  # Fairly expensive, so this is only done when necessary
  def rebuildStatusInformation
    @imPersonStatus = @abPeople.map { |person| bestStatusForPerson(person) }
    @table.reloadData
  end

  # Rebuild status information for a given person, much faster than a full rebuild
  def rebuildStatusInformationForPerson forPerson
    @abPeople.each_with_index do |person, i|
      next unless person == forPerson
      @imPersonStatus[i] = bestStatusForPerson(person)
    end
    @table.reloadData
  end
  
  # This will do a full flush of people in our AB Cache, along with rebuilding their status 
  def reloadABPeople
    @abPeople = ABAddressBook.sharedAddressBook.people.sort do |x, y|
      x.displayName <=> y.displayName
    end
    rebuildStatusInformation
  end

  # NSTableView Data Source
  
  def numberOfRowsInTableView tableView
    @abPeople ? @abPeople.size : 0
  end
  
  def tableView tableView, objectValueForTableColumn:tableColumn, row:row
    case tableColumn.identifier
      when 'image'
        status = @imPersonStatus[row]
        NSImage.imageNamed IMService.imageNameForStatus(status)
      when 'name'
        @abPeople[row].displayName
    end
  end

  # Notifications

  # If the AB database changes, force a reload of everyone
  # We could look in the notification to catch differential updates, but for now
  # this is fine.
  def abDatabaseChangedExternallyNotification notification
    reloadABPeople
  end

end
