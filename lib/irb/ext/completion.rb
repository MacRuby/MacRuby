# MacRuby implementation of IRB.
#
# This file is covered by the Ruby license. See COPYING for more details.
# 
# Copyright (C) 2009-2010, Eloy Duran <eloy.de.enige@gmail.com>

require 'ripper'

module IRB
  class Completion
    # Convenience constants for sexp access of Ripper::SexpBuilder.
    TYPE   = 0
    VALUE  = 1
    CALLEE = 3
    
    RESERVED_UPCASE_WORDS = %w{
      BEGIN  END
    }
    
    RESERVED_DOWNCASE_WORDS = %w{
      alias  and
      begin  break
      case   class
      def    defined do
      else   elsif   end   ensure
      false  for
      if     in
      module
      next   nil     not
      or
      redo   rescue  retry return
      self   super
      then   true
      undef  unless  until
      when   while
      yield
    }
    
    attr_reader :source
    
    def context
      IRB::Driver.current.context
    end
    
    # Returns an array of possible completion results, with the current
    # IRB::Context.
    #
    # This is meant to be used with Readline which takes a completion proc.
    def call(source)
      @source = source
      results
    end
    
    def evaluate(s)
      context.__evaluate__(s)
    end
    
    def local_variables
      evaluate('local_variables').map(&:to_s)
    end
    
    def instance_variables
      context.object.instance_variables.map(&:to_s)
    end
    
    def global_variables
      super.map(&:to_s)
    end
    
    def instance_methods
      context.object.methods.map(&:to_s)
    end
    
    def instance_methods_of(klass)
      evaluate(klass).instance_methods
    end
    
    # TODO: test and or fix the fact that we need to get constants from the
    # singleton class.
    def constants
      evaluate('Object.constants + self.class.constants + (class << self; constants; end)').map(&:to_s)
    end
    
    def results
      return if @source.strip.empty?

      source = @source
      filter = nil
      
      # if ends with period, remove it to remove the syntax error it causes
      call = (source[-1,1] == '.')
      receiver = source = source[0..-2] if call
      
      # root node:
      # [:program, [:stmts_add, [:stmts_new], [x, …]]]
      #                                        ^
      if (sexp = Ripper::SexpBuilder.new(source).parse) && root = sexp[1][2]
        # [:call, [:hash, nil], :".", [:@ident, x, …]]
        if root[TYPE] == :call
          call  = true
          stack = unwind_callstack(root)
          # [[:var_ref, [:@const, "Klass", [1, 0]]], [:call, "new"]]
          # [[:var_ref, [:@ident, "klass", [1, 0]]], [:call, "new"], [:call, "filter"]]
          if stack[1][VALUE] == 'new'
            klass    = stack[0][VALUE][VALUE]
            filter   = stack[2][VALUE] if stack[2]
            receiver = "#{klass}.new"
            methods  = instance_methods_of(klass)
          else
            filter   = root[CALLEE][VALUE]
            filter   = stack[1][VALUE]
            receiver = source[0..-(filter.length + 2)]
            root     = root[VALUE]
          end
        end
        
        result = if call
          if m = (methods || methods_of_object(root))
            format_methods(receiver, m, filter)
          end
        elsif root[TYPE] == :string_literal && root[VALUE][TYPE] == :string_content
          # in the form of: "~/code/
          expand_path(source)
        else
          match_methods_vars_or_consts_in_scope(root)
        end
        result.sort.uniq if result
      end
    end

    def expand_path(source)
      tokens         = Ripper.lex(source)
      path           = tokens[0][2]
      string_open    = tokens[1][2]
      end_with_slash = path.length > 1 && path.end_with?('/')
      path           = File.expand_path(path)
      path          << '/' if end_with_slash
      Dir.glob("#{path}*", File::FNM_CASEFOLD).map { |f| "#{string_open}#{f}" }
    end
    
    def unwind_callstack(root, stack = [])
      if root[TYPE] == :call
        stack.unshift [:call, root[CALLEE][VALUE]]
        unwind_callstack(root[VALUE], stack)
      else
        stack.unshift root
      end
      stack
    end
    
    def match_methods_vars_or_consts_in_scope(symbol)
      var    = symbol[VALUE]
      filter = var[VALUE]
      result = case var[TYPE]
      when :@ident
        local_variables + instance_methods + RESERVED_DOWNCASE_WORDS
      when :@ivar
        instance_variables
      when :@gvar
        global_variables
      when :@const
        if symbol[TYPE] == :top_const_ref
          filter = "::#{filter}"
          Object.constants.map { |c| "::#{c}" }
        else
          constants + RESERVED_UPCASE_WORDS
        end
      end
      (result && filter) ? result.grep(/^#{Regexp.quote(filter)}/) : result
    end
    
    def format_methods(receiver, methods, filter)
      (filter ? methods.grep(/^#{filter}/) : methods).map { |m| "#{receiver}.#{m}" }
    end
    
    def methods_of_object(root)
      result = case root[TYPE]
      # [:unary, :-@, [x, …]]
      #               ^
      when :unary                          then return methods_of_object(root[2]) # TODO: do we really need this?
      when :var_ref, :top_const_ref        then return methods_of_object_in_variable(root)
      when :array, :words_add, :qwords_add then Array
      when :@int                           then Fixnum
      when :@float                         then Float
      when :hash                           then Hash
      when :lambda                         then Proc
      when :dot2, :dot3                    then Range
      when :regexp_literal                 then Regexp
      when :string_literal                 then String
      when :symbol_literal, :dyna_symbol   then Symbol
      end.instance_methods
    end
    
    def methods_of_object_in_variable(path)
      type, name = path[VALUE][0..1]
      
      if path[TYPE] == :top_const_ref
        if type == :@const && Object.constants.include?(name.to_sym)
          evaluate("::#{name}").methods
        end
      else
        case type
        when :@ident
          evaluate(name).methods if local_variables.include?(name)
        when :@ivar
          evaluate(name).methods if instance_variables.include?(name)
        when :@gvar
          eval(name).methods if global_variables.include?(name)
        when :@const
          evaluate(name).methods if constants.include?(name)
        end
      end
    end
  end
end

if defined?(Readline)
  if Readline.respond_to?("basic_word_break_characters=")
    # IRB adds a few breaking chars. that would break literals for us:
    # * String: " and '
    # * Hash: = and >
    Readline.basic_word_break_characters= " \t\n`<;|&("
  end
  # Readline.completion_proc = IRB::Completion
end
