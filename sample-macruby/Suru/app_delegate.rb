#
# AppDelegate.rb
# Suru
#
# Created by Patrick Thomson on 5/25/10.
# Released under the Ruby License.
#

require 'core_data'

class AppDelegate
  attr_writer :window
  include CoreDataSupport

  # Performs the save action for the application, which is to send the save:
  # message to the application's managed object context.  Any encountered errors
  # are presented to the user.
  def saveAction(sender)
    error = Pointer.new_with_type('@')
    unless self.managedObjectContext.save(error)
      NSApplication.sharedApplication.presentError(error[0])
    end
  end
  
  def clearCompleted(sender)
    request = NSFetchRequest.new
    request.entity = NSEntityDescription.entityForName("Entry", inManagedObjectContext: managedObjectContext)
    request.predicate = NSPredicate.predicateWithFormat("isCompleted == TRUE")
    error = Pointer.new_with_type('@')
    results = managedObjectContext.executeFetchRequest(request, error:error)
    if results and not results.empty?
      results.each { |r| managedObjectContext.deleteObject(r) }
      NSSound.soundNamed("Purr").play
    else
      NSBeep()
    end
  end

end
