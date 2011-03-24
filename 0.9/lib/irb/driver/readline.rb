require 'readline'
require 'irb/driver/tty'
require 'irb/ext/history'
require 'irb/ext/completion'

module IRB
  module Driver
    class Readline < TTY
      
      def initialize(input = $stdin, output = $stdout)
        super
        ::Readline.input  = @input
        ::Readline.output = @output
        ::Readline.completion_proc = IRB::Completion.new
      end
      
      def readline
        source = ::Readline.readline(context.prompt, true)
        IRB::History.input(source)
        source
      end
    end
  end
end

IRB::Driver.current = IRB::Driver::Readline.new
