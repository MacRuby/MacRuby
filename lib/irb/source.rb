# MacRuby implementation of IRB.
#
# This file is covered by the Ruby license. See COPYING for more details.
# 
# Copyright (C) 2009-2010, Eloy Duran <eloy.de.enige@gmail.com>

require 'ripper'

module IRB
  class Source
    attr_reader :buffer
    
    def initialize(buffer = [])
      @buffer = buffer
    end
    
    # Adds a source line to the buffer and flushes the cached reflection.
    def <<(source)
      source = source.strip
      unless source.empty?
        @reflection = nil
        @buffer << source
      end
    end
    
    # Removes the last line from the buffer and flushes the cached reflection.
    def pop
      @reflection = nil
      @buffer.pop
    end
    
    # Returns the accumulated source as a string, joined by newlines.
    def source
      @buffer.join("\n")
    end
    
    alias_method :to_s, :source
    
    # Reflects on the accumulated source and returns the current code block
    # indentation level.
    def level
      reflect.level
    end
    
    # Reflects on the accumulated source to see if it's a valid code block.
    def code_block?
      reflect.code_block?
    end
    
    # Reflects on the accumulated source to see if it contains a syntax error.
    def syntax_error?
      reflect.syntax_error?
    end
    
    # Reflects on the accumulated source and returns the actual syntax error
    # message if one occurs.
    def syntax_error
      reflect.syntax_error
    end
    
    # Returns a Reflector for the accumulated source and caches it.
    def reflect
      @reflection ||= Reflector.new(source)
    end
    
    class Reflector < Ripper::SexpBuilder
      def initialize(source)
        super
        @level = 0
        @code_block = !parse.nil?
      end
      
      # Returns the code block indentation level.
      #
      #   Reflector.new("").level # => 0
      #   Reflector.new("class Foo").level # => 1
      #   Reflector.new("class Foo; def foo").level # => 2
      #   Reflector.new("class Foo; def foo; end").level # => 1
      #   Reflector.new("class Foo; def foo; end; end").level # => 0
      attr_reader :level
      
      # Returns the actual syntax error message if one occurs.
      attr_reader :syntax_error
      
      # Returns whether or not the source is a valid code block, but does not
      # take syntax errors into account. In short, it's a valid code block if
      # after parsing the level is at zero.
      #
      # For example, this is not a valid full code block:
      #
      #   def foo; p :ok
      #
      # This however is:
      #
      #   def foo; p :ok; end
      def code_block?
        @code_block
      end
      
      # Returns whether or not the source contains a syntax error. However, it
      # ignores a syntax error resulting in a code block not ending yet.
      #
      # For example, this contains a syntax error:
      #
      #   def; foo
      #
      # This does not:
      #
      #   def foo
      def syntax_error?
        !@syntax_error.nil?
      end
      
      UNEXPECTED_END = "syntax error, unexpected $end"
      
      def on_parse_error(error) #:nodoc:
        if code_block? || !error.start_with?(UNEXPECTED_END)
          @syntax_error = error
        end
      end
      
      INCREASE_LEVEL_KEYWORDS = %w{ class module def begin if unless case while for do }
      
      def on_kw(token) #:nodoc:
        case token
        when *INCREASE_LEVEL_KEYWORDS
          @level += 1
        when "end"
          @level -= 1
        end
        super
      end
      
      def on_lbracket(token) #:nodoc:
        @level += 1
        super
      end
      
      def on_rbracket(token) #:nodoc:
        @level -= 1
        super
      end
      
      def on_embexpr_beg(token) #:nodoc:
        @level += 1
        super
      end
      
      def on_lbrace(token) #:nodoc:
        @level += 1
        super
      end
      
      def on_rbrace(token) #:nodoc:
        @level -= 1
        super
      end
    end
  end
end