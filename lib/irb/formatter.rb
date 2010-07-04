# MacRuby implementation of IRB.
#
# This file is covered by the Ruby license. See COPYING for more details.
# 
# Copyright (C) 2009-2010, Eloy Duran <eloy.de.enige@gmail.com>

module IRB
  class << self
    attr_accessor :formatter
  end
  
  class Formatter
    DEFAULT_PROMPT = "irb(%s):%03d:%d> "
    SIMPLE_PROMPT  = ">> "
    NO_PROMPT      = ""
    RESULT_PREFIX  = "=>"
    SYNTAX_ERROR   = "SyntaxError: compile error\n(irb):%d: %s"
    SOURCE_ROOT    = /^#{File.expand_path('../../../', __FILE__)}/
    
    attr_writer   :prompt
    attr_accessor :inspect
    attr_reader   :filter_from_backtrace
    
    def initialize
      @prompt  = :default
      @inspect = true
      @filter_from_backtrace = [SOURCE_ROOT]
    end
    
    def prompt(context)
      case @prompt
      when :default then DEFAULT_PROMPT % [context.object.inspect, context.line, context.source.level]
      when :simple  then SIMPLE_PROMPT
      else
        NO_PROMPT
      end
    end
    
    def inspect_object(object)
      if @inspect
        result = object.respond_to?(:pretty_inspect) ? object.pretty_inspect : object.inspect
        result.strip!
        result
      else
        address = object.__id__ * 2
        address += 0x100000000 if address < 0
        "#<#{object.class}:0x%x>" % address
      end
    end
    
    def result(object)
      "#{RESULT_PREFIX} #{inspect_object(object)}"
    end
    
    def syntax_error(line, message)
      SYNTAX_ERROR % [line, message]
    end
    
    def exception(exception)
      backtrace = $DEBUG ? exception.backtrace : filter_backtrace(exception.backtrace)
      "#{exception.class.name}: #{exception.message}\n\t#{backtrace.join("\n\t")}"
    end
    
    def filter_backtrace(backtrace)
      backtrace.reject do |line|
        @filter_from_backtrace.any? { |pattern| pattern.match(line) }
      end
    end
  end
end

IRB.formatter = IRB::Formatter.new