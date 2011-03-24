require 'irb/driver'

module IRB
  module Driver
    class TTY
      move_one_line_up      = "\e[1A"
      move_to_begin_of_line = "\r"
      clear_to_end_of_line  = "\e[0K"
      CLEAR_LAST_LINE       = move_one_line_up + move_to_begin_of_line + clear_to_end_of_line

      attr_reader :input, :output, :context_stack
      
      def initialize(input = $stdin, output = $stdout)
        @input  = input
        @output = output
        @context_stack = []
      end
      
      def context
        @context_stack.last
      end

      def readline
        @output.print(context.prompt)
        @input.gets
      end
      
      # TODO make it take the current context instead of storing it
      def consume
        readline
      rescue Interrupt
        context.clear_buffer
        ""
      end

      def update_last_line(prompt, reformatted_line)
        @output.print CLEAR_LAST_LINE
        @output.puts("#{prompt}#{reformatted_line}")
      end

      def process_input(line)
        context.process_line(line) do |prompt, updated_line|
          update_last_line(prompt, updated_line)
        end
      end
      
      # Feeds input into a given context.
      #
      # Ensures that the standard output object is a OutputRedirector, or a
      # subclass thereof.
      def run(context)
        @context_stack << context
        while line = consume
          continue = process_input(line)
          break unless continue
        end
      ensure
        @context_stack.pop
      end
    end
  end
end

IRB::Driver.current = IRB::Driver::TTY.new

module Kernel
  # Creates a new IRB::Context with the given +object+ and runs it.
  def irb(object, binding = nil)
    IRB::Driver.current.run(IRB::Context.new(object, binding))
  end
  
  private :irb
end
