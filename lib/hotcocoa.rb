framework 'cocoa'

module HotCocoa
  
  def application(&block)
    app = NSApplication.sharedApplication
    if block
      block.call(app)
      app.run
    end
    app
  end
  
  Views = {}
  
end

require 'hotcocoa/mappings'
require 'hotcocoa/mapping_methods'
require 'hotcocoa/mapper'
require 'hotcocoa/delegate_builder'
require 'hotcocoa/table_data_source'
require 'hotcocoa/combo_box_data_source'
require 'hotococa/kernel_ext'
HotCocoa::Mappings.reload
