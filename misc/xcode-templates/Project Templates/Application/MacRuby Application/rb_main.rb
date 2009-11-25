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

# Loading all the Ruby project files.
dir_path = NSBundle.mainBundle.resourcePath.fileSystemRepresentation
Dir.glob(dir_path + '/*.{rb,rbo}').map { |x| File.basename(x, File.extname(x)) }.uniq.each do |path|
  if path != File.basename(__FILE__)
    require(path)
  end
end

# Starting the Cocoa main loop.
NSApplicationMain(0, nil)