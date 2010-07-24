unless defined?(MSpec)
  require 'rubygems'
  require 'mspec'
end

ENV['SPECCING'] = 'true'

root = File.expand_path('../../', __FILE__)
if File.basename(root) == 'spec'
  # running from the MacRuby repo
  ROOT = File.expand_path('../../../', __FILE__)
else
  ROOT = root
end
$:.unshift File.join(ROOT, 'lib')

require 'irb'

module InputStubMixin
  def stub_input(*input)
    @input = input
  end
  
  def readline(prompt)
    # print prompt
    @input.shift
  end
  
  def gets
    @input.shift
  end
end

class InputStub
  include InputStubMixin
end

module OutputStubMixin
  def printed
    @printed ||= ''
  end
  
  def write(string)
    printed << string
  end
  
  def print(string)
    printed << string
  end
  
  def puts(*args)
    print "#{args.join("\n")}\n"
  end
  
  def clear_printed!
    @printed = ''
  end
end

class OutputStub
  include OutputStubMixin
end

class StubDriver
  attr_reader :context
  attr_writer :output
  
  def initialize(context = nil)
    @context = context
  end
  
  def output
    @output || $stdout
  end
end
