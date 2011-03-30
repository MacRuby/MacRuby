require File.expand_path('../../spec_helper', __FILE__)
require 'irb/driver'
require 'irb/ext/completion'

module CompletionHelper
  def complete(str)
    # IRB::Completion.new(@context, str).results
    @completion.call(str)
  end
  
  def imethods(klass, receiver = nil)
    wrap_array klass.instance_methods, receiver
  end
  
  def methods(object, receiver = nil)
    wrap_array object.methods, receiver
  end

  def wrap_array(array, receiver)
    array.map { |m| [receiver, m.to_s].compact.join('.') }.sort
  end
end

class CompletionStub
  def self.a_cmethod
  end
  
  def an_imethod
  end
end

class Playground
  CompletionStub = Object.new
  def CompletionStub.a_singleton_method; end
  
  def a_local_method; end

  def init; end
  def initWithNothing; end
end

$a_completion_stub = CompletionStub.new

describe "IRB::Completion" do
  extend CompletionHelper
  
  before :all do
    @completion = IRB::Completion.new
    @context = IRB::Context.new(Playground.new)
    IRB::Driver.current = StubDriver.new(@context)
  end
  
  it "quacks like a Proc" do
    @completion.call('//.').should == imethods(Regexp, '//')
  end

  it "completes reserved words as variables or constants" do
    (IRB::Completion::RESERVED_DOWNCASE_WORDS +
      IRB::Completion::RESERVED_UPCASE_WORDS).each do |word|
      complete(word[0..-2]).should include(word)
    end
  end

  it "completes file paths" do
    complete("'/").should == Dir.glob('/*').sort.map { |f| "'#{f}" }
    complete("'#{ROOT}/lib/../Ra").should == ["'#{File.join(ROOT, "Rakefile")}"]
    complete("%{#{ROOT}/li").should == ['LICENSE', 'lib'].map { |f| "%{#{File.join(ROOT, f)}" }
    complete("\"#{ROOT}/lib/").should == ['irb', 'irb.rb'].map { |f| "\"#{File.join(ROOT, 'lib', f)}" }
  end

  it "does not crash when trying to complete garbage" do
    complete("").should == nil
    complete("/").should == nil
    complete("./Rake").should == nil
  end
  
  describe "when doing a method call on an explicit receiver," do
    describe "and the source ends with a period," do
      describe "returns *all* public methods of the receiver that" do
        it "matches as a local variable" do
          @context.__evaluate__('foo = ::CompletionStub.new')
          complete('foo.').should == imethods(::CompletionStub, 'foo')
          
          @context.__evaluate__('def foo.singleton_method; end')
          complete('foo.').should include('foo.singleton_method')
        end
        
        it "matches as an instance variable" do
          @context.__evaluate__('@an_instance_variable = ::CompletionStub.new')
          complete('@an_instance_variable.').should == imethods(::CompletionStub, '@an_instance_variable')
        end
        
        it "matches as a global variable" do
          complete('$a_completion_stub.').should == imethods(::CompletionStub, '$a_completion_stub')
        end
        
        # TODO: fix
        # it "matches as a local constant" do
        #   complete('CompletionStub.').should == methods(Playground::CompletionStub)
        # end
        
        it "matches as a top level constant" do
          complete('::CompletionStub.').should == methods(::CompletionStub, '::CompletionStub')
        end
      end
      
      describe "returns *all* public instance methods of the class (the receiver) that" do
        it "matches as a Regexp literal" do
          complete('//.').should == imethods(Regexp, '//')
          complete('/^(:[^:.]+)\.([^.]*)$/.').should == imethods(Regexp, '/^(:[^:.]+)\.([^.]*)$/')
          complete('/^(#{oops})\.([^.]*)$/.').should == imethods(Regexp, '/^(#{oops})\.([^.]*)$/')
          complete('%r{/foo/.*/bar}.').should == imethods(Regexp, '%r{/foo/.*/bar}')
        end
        
        it "matches as an Array literal" do
          complete('[].').should == imethods(Array, '[]')
          complete('[:ok, {}, "foo",].').should == imethods(Array, '[:ok, {}, "foo",]')
          complete('[*foo].').should == imethods(Array, '[*foo]')
          complete('%w{foo}.').should == imethods(Array, '%w{foo}')
          complete('%W{#{:foo}}.').should == imethods(Array, '%W{#{:foo}}')
        end
        
        # fails on MacRuby
        it "matches as a lambda literal" do
          complete('->{}.').should == imethods(Proc, '->{}')
          complete('->{x=:ok}.').should == imethods(Proc, '->{x=:ok}')
          complete('->(x){x=:ok}.').should == imethods(Proc, '->(x){x=:ok}')
        end
        
        it "matches as a Hash literal" do
          complete('{}.').should == imethods(Hash, '{}')
          complete('{:foo=>:bar,}.').should == imethods(Hash, '{:foo=>:bar,}')
          complete('{foo:"bar"}.').should == imethods(Hash, '{foo:"bar"}')
        end
        
        it "matches as a Symbol literal" do
          complete(':foo.').should == imethods(Symbol, ':foo')
          complete(':"foo.bar".').should == imethods(Symbol, ':"foo.bar"')
          complete(':"foo.#{"bar"}".').should == imethods(Symbol, ':"foo.#{"bar"}"')
          complete(':\'foo.#{"bar"}\'.').should == imethods(Symbol, ':\'foo.#{"bar"}\'')
          complete('%s{foo.bar}.').should == imethods(Symbol, '%s{foo.bar}')
        end
        
        it "matches as a String literal" do
          complete("'foo\\'bar'.").should == imethods(String, "'foo\\'bar'")
          complete('"foo\"bar".').should == imethods(String, '"foo\"bar"')
          complete('"foo#{"bar"}".').should == imethods(String, '"foo#{"bar"}"')
          complete('%{foobar}.').should == imethods(String, '%{foobar}')
          complete('%q{foo#{:bar}}.').should == imethods(String, '%q{foo#{:bar}}')
          complete('%Q{foo#{:bar}}.').should == imethods(String, '%Q{foo#{:bar}}')
        end
        
        it "matches as a Range literal" do
          complete('1..10.').should == imethods(Range, '1..10')
          complete('1...10.').should == imethods(Range, '1...10')
          complete('"a".."z".').should == imethods(Range, '"a".."z"')
          complete('"a"..."z".').should == imethods(Range, '"a"..."z"')
        end
        
        it "matches as a Fixnum literal" do
          complete('42.').should == imethods(Fixnum, '42')
          complete('+42.').should == imethods(Fixnum, '+42')
          complete('-42.').should == imethods(Fixnum, '-42')
          complete('42_000.').should == imethods(Fixnum, '42_000')
        end
        
        it "matches as a Bignum literal as a Fixnum" do
          complete('100_000_000_000_000_000_000.').should == imethods(Fixnum, '100_000_000_000_000_000_000')
          complete('-100_000_000_000_000_000_000.').should == imethods(Fixnum, '-100_000_000_000_000_000_000')
          complete('+100_000_000_000_000_000_000.').should == imethods(Fixnum, '+100_000_000_000_000_000_000')
        end
        
        it "matches as a Float with exponential literal" do
          complete('1.2e-3.').should == imethods(Float, '1.2e-3')
          complete('+1.2e-3.').should == imethods(Float, '+1.2e-3')
          complete('-1.2e-3.').should == imethods(Float, '-1.2e-3')
        end
        
        it "matches as a hex literal as a Fixnum" do
          complete('0xffff.').should == imethods(Fixnum, '0xffff')
          complete('+0xffff.').should == imethods(Fixnum, '+0xffff')
          complete('-0xffff.').should == imethods(Fixnum, '-0xffff')
        end
        
        it "matches as a binary literal as a Fixnum" do
          complete('0b01011.').should == imethods(Fixnum, '0b01011')
          complete('-0b01011.').should == imethods(Fixnum, '-0b01011')
          complete('+0b01011.').should == imethods(Fixnum, '+0b01011')
        end
        
        it "matches as an octal literal as a Fixnum" do
          complete('0377.').should == imethods(Fixnum, '0377')
          complete('-0377.').should == imethods(Fixnum, '-0377')
          complete('+0377.').should == imethods(Fixnum, '+0377')
        end
        
        it "matches as a Float literal" do
          complete('42.0.').should == imethods(Float, '42.0')
          complete('-42.0.').should == imethods(Float, '-42.0')
          complete('+42.0.').should == imethods(Float, '+42.0')
          complete('42_000.0.').should == imethods(Float, '42_000.0')
        end
        
        it "matches as a Bignum float literal as a Float" do
          complete('100_000_000_000_000_000_000.0.').should == imethods(Float, '100_000_000_000_000_000_000.0')
          complete('+100_000_000_000_000_000_000.0.').should == imethods(Float, '+100_000_000_000_000_000_000.0')
          complete('-100_000_000_000_000_000_000.0.').should == imethods(Float, '-100_000_000_000_000_000_000.0')
        end
      end
      
      it "returns *all* public instance methods of the class (the receiver) that ::new is called on" do
        if IRB::Completion::INCLUDE_MACRUBY_HELPERS
          imethods = Playground.instance_methods(true, true)
        else
          imethods = Playground.instance_methods(true)
        end

        complete("Playground.new.").should == wrap_array(imethods, 'Playground.new')
        complete("Playground.new.a_local_m").should == %w{ Playground.new.a_local_method }
        
        @context.__evaluate__("klass = Playground")
        complete("klass.new.").should == wrap_array(imethods, 'klass.new')
        complete("klass.new.a_local_m").should == %w{ klass.new.a_local_method }

        complete("DoesNotExist.new").should == nil
        complete("DoesNotExist.new.a_local_m").should == nil
      end

      if IRB::Completion::INCLUDE_MACRUBY_HELPERS
        it "returns *all* instance methods that are prefixed with `init' of the class (the receiver) that ::alloc is called on" do
          framework 'AppKit'
          complete("NSSpeechSynthesizer.alloc.ini").should == %w{ NSSpeechSynthesizer.alloc.init NSSpeechSynthesizer.alloc.initWithVoice }
          complete("NSSpeechSynthesizer.alloc.initWithVo").should == %w{ NSSpeechSynthesizer.alloc.initWithVoice }
          @context.__evaluate__("klass = NSSpeechSynthesizer")
          complete("klass.alloc.ini").should == %w{ klass.alloc.init klass.alloc.initWithVoice }

          complete("DoesNotExist.alloc").should == nil
          complete("DoesNotExist.alloc.initWithVo").should == nil
        end
      end
    end
    
    describe "and the source does *not* end with a period," do
      it "filters the methods, of the literal receiver, by the given method name" do
        complete('//.nam').should == %w{ //.named_captures //.names }
        complete('//.named').should == %w{ //.named_captures }
      end
      
      it "filters the methods, of the variable receiver, by the given method name" do
        @context.__evaluate__('foo = @an_instance_variable = ::CompletionStub.new')
        complete('foo.an_im').should == %w{ foo.an_imethod }
        complete('$a_completion_stub.an_im').should == %w{ $a_completion_stub.an_imethod }
        complete('@an_instance_variable.an_im').should == %w{ @an_instance_variable.an_imethod }
        # TODO: fix
        # complete('CompletionStub.a_sing').should == %w{ CompletionStub.a_singleton_method }
      end
    end
  end
  
  describe "when *not* doing a method call on an explicit receiver" do
    before do
      @context.__evaluate__("a_local_variable = @an_instance_variable = :ok")
    end
    
    it "matches local variables" do
      complete("a_local_v").should == %w{ a_local_variable }
    end
    
    it "matches instance methods of the context object" do
      complete("a_local_m").should == %w{ a_local_method }
    end
    
    it "matches local variables and instance method of the context object" do
      complete("a_loc").should == %w{ a_local_method a_local_variable }
    end
    
    it "matches instance variables" do
      complete("@an_inst").should == %w{ @an_instance_variable }
    end
    
    it "matches global variables" do
      complete("$a_completion_s").should == %w{ $a_completion_stub }
    end
    
    it "matches constants" do
      complete("Playgr").should == %w{ Playground }
    end
    
    it "matches top level constants" do
      complete("::CompletionSt").should == %w{ ::CompletionStub }
    end
  end
end
