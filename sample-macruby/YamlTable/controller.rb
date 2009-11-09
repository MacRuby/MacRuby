# controller.rb
# yaml_table
#
# Created by Matt Aimonetti on 11/8/09.
# Copyright 2009 m|a agile. All rights reserved.

require 'yaml'

class Controller < NSWindowController
 # windows/panels
 attr_accessor :add_sheet, :main_window
 # table view
 attr_accessor :nameTableView
 # fields
 attr_accessor :first_name, :last_name, :close_button, :tags

  def awakeFromNib
    retrieve_names
    nameTableView.doubleAction = "edit:"
  end
  
  def windowWillClose(sender)
   exit
  end
  
  def yaml_file
    yaml_file = File.expand_path('~/macruby_example.yml')
    unless File.exist?(yaml_file)
      File.open(yaml_file, 'w+'){|f| f << [{:first_name => "Matt", :last_name => "Aimonetti", :tags => ['macruby', 'rails'] }].to_yaml}
    end
    yaml_file
  end
  
  def retrieve_names
    @names = YAML.load_file(yaml_file) || []
    @nameTableView.dataSource = self
  end
  
  def numberOfRowsInTableView(view)
    @names.size
  end

  def tableView(view, objectValueForTableColumn:column, row:index)
    filter = @names[index]
    case column.identifier
      when 'first_name'
        filter[:first_name] ? filter[:first_name] : ""
      when 'last_name'
        filter[:last_name] ? filter[:last_name] : ""
      when 'tags'
        filter[:tags] ? filter[:tags].join(', ') : ""
    end
  end
  
  def add(sender)
    @sheet_mode = :add
    close_button.title = 'Add'
    first_name.stringValue, last_name.stringValue, tags.stringValue = '', '', ''
    show_panel
	end
  
  def edit(sender)
    if nameTableView.selectedRow != -1
      first_name.stringValue = @names[nameTableView.selectedRow][:first_name] || ''
      last_name.stringValue = @names[nameTableView.selectedRow][:last_name] || ''
      tags.stringValue =  @names[nameTableView.selectedRow][:tags].join(', ') if @names[nameTableView.selectedRow][:tags]
      @sheet_mode = :edit
      close_button.title = 'Update'
      show_panel
    else
      alert
    end
  end
  
  def close_add(sender)
    if @sheet_mode == :add
      add_name!
    else
      edit_name!
    end
		@add_sheet.orderOut(nil)
    NSApp.endSheet(@add_sheet)
	end
  
  def cancel(sender)
    @add_sheet.orderOut(nil)
    NSApp.endSheet(@add_sheet)
  end
  
  def remove(sender)
    if nameTableView.selectedRow != -1
      @names.delete_at(nameTableView.selectedRow)
      save_names
    else
      alert
    end
  end
  
  def add_name!
    new_filter = {}
    new_filter[:first_name] = first_name.stringValue
    new_filter[:last_name]  = last_name.stringValue
    new_filter[:tags]       = tags.stringValue.split(',').map{|rule| rule.strip} unless tags.stringValue.empty?
    unless new_filter.empty?
     @names << new_filter
     save_names
    end
  end
  
  def edit_name!
    updated_rule = {}
    updated_rule[:first_name] = first_name.stringValue
    updated_rule[:last_name]  = last_name.stringValue
    updated_rule[:tags]       = tags.stringValue.split(',').map{|tag| tag.strip}
    @names[nameTableView.selectedRow] = updated_rule
    save_names
  end
  
  def save_names
    File.open(yaml_file, 'w'){|f| f << @names.to_yaml}
    retrieve_names
    nameTableView.reloadData
  end
  
  def alert(title='Nothing Selected', message='You need to select a row before clicking on this button.')
    NSAlert.alertWithMessageText(title, 
                                    defaultButton: 'OK',
                                    alternateButton: nil, 
                                    otherButton: 'Cancel',
                                    informativeTextWithFormat: message).runModal
  end
  
  def show_panel
    NSApp.beginSheet(@add_sheet, 
			modalForWindow:@main_window, 
			modalDelegate:self, 
			didEndSelector:nil,
			contextInfo:nil)
  end
  
end