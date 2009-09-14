# Bacon -- small RSpec clone.
#
# "Truth will sooner come out from error than from confusion." ---Francis Bacon

# Copyright (C) 2007, 2008 Christian Neukirchen <purl.org/net/chneukirchen>
#
# Bacon is freely distributable under the terms of an MIT-style license.
# See COPYING or http://www.opensource.org/licenses/mit-license.php.

module Bacon
  VERSION = "1.1"

  Counter = Hash.new(0)
  ErrorLog = ""
  Shared = Hash.new { |_, name|
    raise NameError, "no such context: #{name.inspect}"
  }

  RestrictName    = //  unless defined? RestrictName
  RestrictContext = //  unless defined? RestrictContext

  Backtraces = true  unless defined? Backtraces

  def self.summary_on_exit
    return  if Counter[:installed_summary] > 0
    at_exit {
      handle_summary
      if $!
        raise $!
      elsif Counter[:errors] + Counter[:failed] > 0
        exit 1
      end
    }
    Counter[:installed_summary] += 1
  end
  class <<self; alias summary_at_exit summary_on_exit; end

  module SpecDoxOutput
    def handle_specification(name)
      puts name
      yield
      puts
    end

    def handle_requirement(description)
      print "- #{description}"
      error = yield
      puts error.empty? ? "" : " [#{error}]"
    end

    def handle_summary
      print ErrorLog  if Backtraces
      puts "%d specifications (%d requirements), %d failures, %d errors" %
        Counter.values_at(:specifications, :requirements, :failed, :errors)
    end
  end

  module TestUnitOutput
    def handle_specification(name)  yield  end

    def handle_requirement(description)
      error = yield
      if error.empty?
        print "."
      else
        print error[0..0]
      end
    end

    def handle_summary
      puts
      puts ErrorLog  if Backtraces
      puts "%d tests, %d assertions, %d failures, %d errors" %
        Counter.values_at(:specifications, :requirements, :failed, :errors)
    end
  end

  module TapOutput
    def handle_specification(name)  yield  end

    def handle_requirement(description)
      ErrorLog.replace ""
      error = yield
      if error.empty?
        puts "ok %-3d - %s" % [Counter[:specifications], description]
      else
        puts "not ok %d - %s: %s" %
          [Counter[:specifications], description, error]
        puts ErrorLog.strip.gsub(/^/, '# ')  if Backtraces
      end
    end

    def handle_summary
      puts "1..#{Counter[:specifications]}"
      puts "# %d tests, %d assertions, %d failures, %d errors" %
        Counter.values_at(:specifications, :requirements, :failed, :errors)
    end
  end

  module KnockOutput
    def handle_specification(name)  yield  end

    def handle_requirement(description)
      ErrorLog.replace ""
      error = yield
      if error.empty?
        puts "ok - %s" % [description]
      else
        puts "not ok - %s: %s" % [description, error]
        puts ErrorLog.strip.gsub(/^/, '# ')  if Backtraces
      end
    end

    def handle_summary;  end
  end

  extend SpecDoxOutput          # default

  class Error < RuntimeError
    attr_accessor :count_as

    def initialize(count_as, message)
      @count_as = count_as
      super message
    end
  end

  class Context
    attr_reader :name, :block
    
    def initialize(name, &block)
      @name = name
      @before, @after = [], []
      @block = block
    end
    
    def run
      return  unless name =~ RestrictContext
      Bacon.handle_specification(name) { instance_eval(&block) }
      self
    end

    def before(&block); @before << block; end
    def after(&block);  @after << block; end

    def behaves_like(*names)
      names.each { |name| instance_eval(&Shared[name]) }
    end

    def it(description, &block)
      return  unless description =~ RestrictName
      block ||= lambda { should.flunk "not implemented" }
      Counter[:specifications] += 1
      run_requirement description, block
    end
    
    def should(*args, &block)
      if Counter[:depth]==0
        it('should '+args.first,&block)
      else
        super(*args,&block)
      end
    end

    def run_requirement(description, spec)
      Bacon.handle_requirement description do
        begin
          Counter[:depth] += 1
          rescued = false
          begin
            @before.each { |block| instance_eval(&block) }
            prev_req = Counter[:requirements]
            instance_eval(&spec)
          rescue Object => e
            rescued = true
            raise e
          ensure
            if Counter[:requirements] == prev_req and not rescued
              raise Error.new(:missing,
                              "empty specification: #{@name} #{description}")
            end
            begin
              @after.each { |block| instance_eval(&block) }
            rescue Object => e
              raise e  unless rescued
            end
          end
        rescue Object => e
          ErrorLog << "#{e.class}: #{e.message}\n"
          e.backtrace.find_all { |line| line !~ /bin\/bacon|\/bacon\.rb:\d+/ }.
            each_with_index { |line, i|
            ErrorLog << "\t#{line}#{i==0 ? ": #@name - #{description}" : ""}\n"
          }
          ErrorLog << "\n"

          if e.kind_of? Error
            Counter[e.count_as] += 1
            e.count_as.to_s.upcase
          else
            Counter[:errors] += 1
            "ERROR: #{e.class}"
          end
        else
          ""
        ensure
          Counter[:depth] -= 1
        end
      end
    end

    def describe(*args, &block)
      context = Bacon::Context.new(args.join(' '), &block)
      @before.each { |b| context.before(&b) }
      @after.each { |b| context.after(&b) }
      context.run
    end

    def raise?(*args, &block); block.raise?(*args); end
    def throw?(*args, &block); block.throw?(*args); end
    def change?(*args, &block); block.change?(*args); end
  end
end


class Object
  def true?; false; end
  def false?; false; end
end

class TrueClass
  def true?; true; end
end

class FalseClass
  def false?; true; end
end

class Proc
  def raise?(*exceptions)
    call
  rescue *(exceptions.empty? ? RuntimeError : exceptions) => e
    e
  else
    false
  end

  def throw?(sym)
    catch(sym) {
      call
      return false
    }
    return true
  end

  def change?
    pre_result = yield
    called = call
    post_result = yield
    pre_result != post_result
  end
end

class Numeric
  def close?(to, delta)
    (to.to_f - self).abs <= delta.to_f  rescue false
  end
end


class Object
  def should(*args, &block)    Should.new(self).be(*args, &block)             end
end

module Kernel
  private
  def describe(*args, &block) Bacon::Context.new(args.join(' '), &block).run  end
  def shared(name, &block)    Bacon::Shared[name] = block                     end
end

class Should
  # Kills ==, ===, =~, eql?, equal?, frozen?, instance_of?, is_a?,
  # kind_of?, nil?, respond_to?, tainted?
  instance_methods.each { |name| undef_method name  if name =~ /\?|^\W+$/ }

  def initialize(object)
    @object = object
    @negated = false
  end

  def not(*args, &block)
    @negated = !@negated

    if args.empty?
      self
    else
      be(*args, &block)
    end
  end

  def be(*args, &block)
    if args.empty?
      self
    else
      block = args.shift  unless block_given?
      satisfy(*args, &block)
    end
  end

  alias a  be
  alias an be

  def satisfy(*args, &block)
    if args.size == 1 && String === args.first
      description = args.shift
    else
      description = ""
    end

    r = yield(@object, *args)
    if Bacon::Counter[:depth] > 0
      Bacon::Counter[:requirements] += 1
      raise Bacon::Error.new(:failed, description)  unless @negated ^ r
      r
    else
      @negated ? !r : !!r
    end
  end

  def method_missing(name, *args, &block)
    name = "#{name}?"  if name.to_s =~ /\w[^?]\z/

    desc = @negated ? "not " : ""
    desc << @object.inspect << "." << name.to_s
    desc << "(" << args.map{|x|x.inspect}.join(", ") << ") failed"

    satisfy(desc) { |x| x.__send__(name, *args, &block) }
  end

  def equal(value)         self == value      end
  def match(value)         self =~ value      end
  def identical_to(value)  self.equal? value  end
  alias same_as identical_to

  def flunk(reason="Flunked")
    raise Bacon::Error.new(:failed, reason)
  end
end
