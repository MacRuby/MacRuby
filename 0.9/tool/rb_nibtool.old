#!/usr/bin/env ruby
# Copyright (c) 2006-2007, The RubyCocoa Project.
# Copyright (c) 2007 Chris Mcgrath.
# All Rights Reserved.
#
# RubyCocoa is free software, covered under either the Ruby's license or the 
# LGPL. See the COPYRIGHT file for more information.

require 'osx/cocoa'
include OSX
require 'erb'

def log(*s)
  $stderr.puts s if $DEBUG
end

def die(*s)
  $stderr.puts s
  exit 1
end

# Requires rubygems if present.
begin require 'rubygems'; rescue LoadError; end

class OSX::NSObject
  class << self
    @@subklasses = {}

    def subklasses
      @@subklasses
    end
  end
end

begin
  require 'rubynode'
  require 'enumerator'
  # RubyNode is found, we can get the IB metadata by parsing the code. 
  class OSX::NSObject
    class << self
      def collect_ib_metadata(ruby_file)
        @current_class = nil
        __parse_nodes(File.read(ruby_file).parse_to_nodes.transform)
      end

      def __parse_nodes(ary)
        ary.each_slice(2) { |key, val| __parse_nodes_pair(key, val) }
      end
      
      def __parse_nodes_pair(key, val)
        case val
        when Array
          if val.all? { |e| e.is_a?(Array) }
            val.each { |p| __parse_nodes(p) }
          else
            __parse_nodes(val)
          end
        when Hash 
          case key
          when :class
            a = val[:super]
            if a and a.is_a?(Array) and a[1].is_a?(Hash)
              # This class inherits from another class, let's memorize the 
              # class name. We could actually check that the super class is
              # an Objective-C class, but this would require to load all the
              # required frameworks.
              sclass = (a[1][:vid] or a[1][:mid])
              a = val[:cpath]
              if sclass and a and a.is_a?(Array) and a[1].is_a?(Hash)
                @current_class = a[1][:mid]
                if @current_class
                  subklasses[@current_class] ||= {}
                  subklasses[@current_class][:super] = sclass
                end
              end
            end
          when :fcall
            case val[:mid]
            # Memorize IB outlets.
            when :ns_outlet, :ib_outlet, :ns_outlets, :ib_outlets
              if @current_class.nil?
                $stderr.puts "ib_outlet detected without current_class, skipping..."
              elsif val[:args].is_a?(Array) and !val[:args].empty?
                c = (subklasses[@current_class][:outlets] ||= [])
                val[:args][1].each do |key2, val2|
                  if key2 == :lit and val2.is_a?(Hash) and val2[:lit]
                    c << val2[:lit]
                  end
                end
              end
            # Memorize IB actions.
            when :ib_action
              if @current_class.nil?
                $stderr.puts "ib_action detected without current_class, skipping..."
              elsif val[:args].is_a?(Array) and !val[:args].empty?
                c = (subklasses[@current_class][:actions] ||= [])
                a = val[:args][1][0]
                if val[:args][1].size != 1
                  $stderr.puts "ib_action called without or with more than one argument, skipping..."
                elsif a[0] == :lit and 
                      a[1].is_a?(Hash) and
                      a[1][:lit]
                  c << a[1][:lit]
                end
              end
            end
          end
          val.each do |key2, val2|
            if val2.is_a?(Array)
              __parse_nodes_pair(key2, val2)
            end
          end
        end
      end
    end
  end
  RUBYNODE_LOADED = true
rescue LoadError
  # We don't have a Ruby parser handy, let's evaluate/interpret the code as
  # a second alternative.
  class OSX::NSObject
    class << self
      @@collect_child_classes = false
    
      def ib_outlets(*args)
        args.each do |arg|
          log "found outlet #{arg} in #{$current_class}"
          (subklasses[$current_class][:outlets] ||= []) << arg
        end
      end
    
      alias_method :ns_outlet,  :ib_outlets
      alias_method :ib_outlet,  :ib_outlets
      alias_method :ns_outlets, :ib_outlets
  
      def ib_action(name, &blk)
        log "found action #{name} in #{$current_class}"
        (subklasses[$current_class][:actions] ||= []) << name
      end
    
      alias_method :_before_classes_nib_inherited, :inherited
      def inherited(subklass)
        if @@collect_child_classes
          unless subklass.to_s == ""
            log "current class: #{subklass.to_s}"
            $current_class = subklass.to_s
            subklasses[$current_class] ||= {}
            subklasses[$current_class][:super] = subklass.superclass.to_s
          end
        end
        _before_classes_nib_inherited(subklass)
      end

      def collect_ib_metadata(ruby_file)
        @@collect_child_classes = true
        require ruby_file
        @@collect_child_classes = false 
      end
    end
  end
  RUBYNODE_LOADED = false 
end

class ClassesNibPlist
  attr_reader :plist
  
  def initialize(plist_path=nil)
    @plist_path = plist_path
    if plist_path and File.exist?(plist_path)
      plist_data = NSData.alloc.initWithContentsOfFile(plist_path)
      @plist, format, error = NSPropertyListSerialization.propertyListFromData_mutabilityOption_format_errorDescription(plist_data, NSPropertyListMutableContainersAndLeaves)
      die "Can't deserialize property list at path '#{plist_path}' : #{error}" if @plist.nil?
    else
      @plist = NSMutableDictionary.alloc.init
    end
    self
  end
  
  def find_ruby_class(ruby_class)
    log "Looking for #{ruby_class} in plist"
    # be nice if NSDictionary had the same methods as hash
    ruby_class_plist = nil
    classes = @plist['IBClasses']
    if classes
      classes.each do |klass|
        next unless klass['CLASS'].to_s == ruby_class.to_s
        ruby_class_plist = klass
      end
    else
      @plist['IBClasses'] = []
    end
    if ruby_class_plist.nil?
      log "Didn't find #{ruby_class} in plist, creating dictionary"
      # didn't find one, create a new one
      ruby_class_plist = NSMutableDictionary.alloc.init
      ruby_class_plist['CLASS'] = ruby_class
      ruby_class_plist['LANGUAGE'] = 'ObjC' # Hopefully one day we can put Ruby here :)
      plist['IBClasses'].addObject(ruby_class_plist)
    end
    ruby_class_plist
  end

  def write_plist_data
    data, error = NSPropertyListSerialization.objc_send \
      :dataFromPropertyList, @plist,
      :format, NSPropertyListXMLFormat_v1_0,
      :errorDescription
    if data.nil?
      $stderr.puts error
      exit 1
    end
    data = OSX::NSString.alloc.initWithData_encoding(data, NSUTF8StringEncoding)

    if @plist_path
      log "Writing updated classes.nib plist back to file"
      File.open(@plist_path, "w+") { |io| io.puts data }
    else
      log "Writing updated classes.nib plist back to standard output"
      puts data
    end
  end
  
  def each_class(&block)
    plist['IBClasses'].each do |klass|
      next if klass['CLASS'].to_s == 'FirstResponder'
      yield(klass)
    end
  end
end

class ClassesNibUpdater
  def self.update_nib(plist_path, ruby_file, sorted_plist)
    plist = ClassesNibPlist.new(plist_path)
    updater = new
    updater.find_classes_outlets_and_actions(ruby_file)
    log "Found #{NSObject.subklasses.size} classes in #{ruby_file}"
    NSObject.subklasses.each do |klass, data|
      ruby_class_plist = plist.find_ruby_class(klass)
      updater.update_superclass(klass, ruby_class_plist)
      updater.add_outlets_and_actions_to_plist(klass, ruby_class_plist, 
        sorted_plist)
    end
    plist.write_plist_data
  end
 
  # we've taken over ns_outlets and ns_actions above, so just requiring the
  # class will cause it to be parsed an the methods to be called so we can get
  # at them
  def find_classes_outlets_and_actions(ruby_file)
    log "Getting classes, outlets and actions"
    NSObject.collect_ib_metadata(ruby_file)
  end

  def update_superclass(ruby_class, ruby_class_plist)
    superklass = NSObject.subklasses[ruby_class][:super].to_s.sub(/^OSX::/, '')
    unless RUBYNODE_LOADED
      # If the class has a superclass which isn't defined in the classes in the nib/ib
      # then the class will still not show up. Because we can assume that it will be a
      # descendant of NSObject use that as a default if the superclass can't be found.
      begin
        Object.const_get(superklass)
      rescue NameError
        superklass = :NSObject
      end
    end
    ruby_class_plist.setObject_forKey(superklass, "SUPERCLASS")
  end
  
  def add_outlets_and_actions_to_plist(klass, ruby_class_plist, sorted_plist)
    log "Adding outlets and actions to plist for #{klass}"
   
    [:outlets, :actions].each do |sym|
      cont = NSObject.subklasses[klass][sym]
      next if cont.nil?
      unless sorted_plist
        hash = {}
        cont.each do |val|
          log "adding #{sym.to_s} #{val}"
          hash[val] = 'id' 
        end 
        cont = hash
      end
      ruby_class_plist[sym.to_s.upcase] = cont unless cont.empty?
    end
  end
end

class ClassesNibCreator
  def self.create_from_nib(plist_path, output_dir)
    creator = new
    plist = ClassesNibPlist.new(plist_path)
    plist.each_class { |klass| creator.create_class(output_dir, klass) }
  end
  
  def initialize
    @class_template = ERB.new(DATA.read, 0, "-")
  end
  
  def create_class(output_dir, klass)
    @class_name = klass['CLASS']
    output_file = File.join(output_dir, "#{@class_name}.rb")
    if File.exists?(output_file)
      log "#{output_file} exists, skipping"
      return
    end
    @superclass = klass['SUPERCLASS']
    if klass['OUTLETS'].nil?
      @outlets = []
    else
      @outlets = klass['OUTLETS'].allKeys.to_a
    end
    if klass['ACTIONS'].nil?
      @actions = []
    else
      @actions = klass['ACTIONS'].allKeys.to_a
    end
    log "Writing #{@class_name} to #{output_file}"
    File.open(output_file, "w+") do |file|
      file.write(@class_template.result(binding))
    end 
  end
end

require 'optparse'
class Options
  def self.parse(args)
    options = {}
    options[:update] = false
    options[:create] = false
    options[:plist] = false
    options[:sorted_plist] = false
    opts = OptionParser.new do |opts|
      opts.banner = "Usage: #{__FILE__} [options]"
      opts.on("-u", "--update", "Update the classes.nib file from a Ruby",
                                "class (requires -f and -n options)") do |_|
        options[:update] = true
      end
      opts.on("-c", "--create", "Create new Ruby classes from a nib",
                                "(requires -d and -n options)") do |_|
        options[:create] = true
      end
      opts.on("-p", "--plist", "Dump on standard output a property list", "of the Ruby class IB metadata",
                               "(requires -f option)") do |_|
        options[:plist] = true
      end
      opts.on("-s", "--sorted-plist", 
        "Dump a property list where the actions and",
        "outlets are in sorted collections.",
        "NOT compatible with the nib format.",
         "(requires -p and -f options)") do |_|
        options[:sorted_plist] = true
      end
      opts.on("-d", "--directory PATH", "Path to directory to create Ruby classes", "(requires -c option)") do |dir|
        options[:dir] = dir == "" ? nil : dir
      end
      opts.on("-f", "--file PATH", "Path to file containing Ruby class(es)", "(requires -u or -p options)") do |file|
        options[:file] = case file
          when '' then nil
          when '-' then '/dev/stdin'
          else file
        end
      end
      opts.on("-n", "--nib PATH", "Path to .nib to update") do |nib|
        options[:nib] = nib == "" ? nil : nib
      end
      opts.on_tail("-h", "--help", "Show this message") do
        puts opts
        exit
      end  
    end
    opts.parse!(args)
    unless options[:update] || options[:create] || options[:plist]
      puts "Must supply --update or --create or --plist"
      exit_with_opts(opts)
    end
    if [:update, :create, :plist].map { |x| (options[x] or nil) }.compact.size > 1
      puts "Can only specify one of --update or --create or --plist"
      exit_with_opts(opts)
    end
    if options[:update]
      if options[:file].nil? || options[:nib].nil?
        puts "Must supply the ruby file and the nib paths"
        exit_with_opts(opts)
      end
    end
    if options[:create]
      if options[:dir].nil? || options[:nib].nil?
        puts "Must supply the output directory and the nib paths"
        exit_with_opts(opts)
      end
    end
    if options[:plist]
      if options[:file].nil?
        puts "Must supply the ruby file"
        exit_with_opts(opts)
      end
    end
    if options[:sorted_plist]
      unless options[:plist]
        puts "Must specify plist format"
        exit_with_opts(opts)
      end
    end
    options
  end
  
  def self.exit_with_opts(opts)
    puts opts
    exit
  end
end

options = Options.parse(ARGV)
nib_plist = 
  if options[:nib]
    "#{options[:nib]}/classes.nib"
  else
    nil 
  end
if options[:update] || options[:plist]
  ClassesNibUpdater.update_nib(nib_plist, options[:file], 
    options[:sorted_plist])
elsif options[:create]
  ClassesNibCreator.create_from_nib(nib_plist, options[:dir])
else
  puts "Unknown options"
end

# wierd indentation here seems to be needed to produce nice output
__END__
require 'osx/cocoa'
include OSX

class <%= @class_name %> < <%= @superclass %>
  <% unless @outlets.size == 0 -%>
  ib_outlets <%= @outlets.map { |o| ":#{o}" }.join(", ") -%>
  <% end -%>
  <% @actions.each do |action| %>
  ib_action :<%= action %> do |sender|
    NSLog("Need to implement <%= @class_name%>.<%= action %>")
  end
  <% end %>
end
