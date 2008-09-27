require 'growl_helpers'

class GrowlController
  # Makes the #growl and #sticky_growl shortcut methods available.
  include Growl
  
  HELLO_TYPE = 'Hello message received'
  Growl::Notifier.sharedInstance.register('GrowlSample', [HELLO_TYPE])
  
  def init
    if super
      growl HELLO_TYPE, 'Not sticky', 'Hello world' do
        puts "Clicked not sticky: #{ Time.now }"
        NSApp.terminate(nil)
      end
      
      sticky_growl HELLO_TYPE, 'Sticky', 'Hello world' do
        puts "Clicked sticky: #{ Time.now }"
        NSApp.terminate(nil)
      end
      
      self
    end
  end
end

g = GrowlController.alloc.init
NSApp.run
