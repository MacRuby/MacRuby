#!/usr/bin/env macruby

require 'hotcocoa'
include HotCocoa

@table = table_view(
  :columns => [ 
    column(:id => :klass, :text => "Class"),
    column(:id => :ancestors, :text => "Ancestors") 
  ]  )