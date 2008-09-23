require File.expand_path('../growl', __FILE__)

# Defines a few convenience methods that you can use in your class if you include the Growl module.
# Eg:
#
#   class FoodReporter < NSObject
#     include Growl
#
#     def hamburger_time!
#       growl 'YourHamburgerIsReady', 'Your hamburger is ready for consumption!', 'Please pick it up at isle 4.', :priority => 1 do
#         throw_it_away_before_user_reaches_counter!
#       end
#     end
#   end
module Growl
  # Sends a Growl notification. See Growl::Notifier#notify for more info.
  def growl(name, title, description, options = {}, &callback)
    Growl::Notifier.sharedInstance.notify name, title, description, options, &callback
  end
  
  # Sends a sticky Growl notification. See Growl::Notifier#notify for more info.
  def sticky_growl(name, title, description, options = {}, &callback)
    growl name, title, description, options.merge!(:sticky => true), &callback
  end
end
