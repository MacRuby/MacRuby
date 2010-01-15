#
# rb_main.rb
# ÇPROJECTNAMEÈ
#
# Created by ÇFULLUSERNAMEÈ on ÇDATEÈ.
# Copyright ÇORGANIZATIONNAMEÈ ÇYEARÈ. All rights reserved.
#

# Loading the Cocoa framework. If you need to load more frameworks, you can
# do that here too.
framework 'Cocoa'
framework 'CoreData'

main = File.basename(__FILE__, File.extname(__FILE__))
resources = NSBundle.mainBundle.resourcePath.fileSystemRepresentation

# Loading all the Ruby project files.
Dir.glob(File.join(resources, '*.{rb,rbo}')).each do |file|
  file = File.basename(file, File.extname(file))
  require file unless file == main
end

# Starting the Cocoa main loop.
NSApplicationMain(0, nil)