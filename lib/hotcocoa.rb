framework 'cocoa'

module HotCocoa
  
  Views = {}
  
end

require 'hotcocoa/mappings'
require 'hotcocoa/mapping_methods'
require 'hotcocoa/mapper'
require 'hotcocoa/delegate_builder'
require 'hotcocoa/data_sources/table_data_source'
require 'hotcocoa/data_sources/combo_box_data_source'
require 'hotcocoa/kernel_ext'

HotCocoa::Mappings.reload
