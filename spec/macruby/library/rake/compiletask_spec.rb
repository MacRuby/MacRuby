require File.expand_path('../../../spec_helper', __FILE__)
require 'rake/compiletask'
require 'stringio'

module CompileTaskSpecHelper
  include FileUtils

  def files
    [
     'lib/lib.rb',
     'lib/lib/zomg.rb',
     'lib/lib/cake.rb',
     'lib/lib/cake.m',
     'test/helper.rb',
     'test/test_compile_task.rb'
    ]
  end

  def prepare_fixture
    dir = tmp('compile_task')

    mkdir_p File.join(dir, 'ext')
    mkdir_p File.join(dir, 'lib/lib')
    mkdir_p File.join(dir, 'test')

    fixture_file = File.join(FIXTURES, 'object.rb')
    files.each do |file|
      cp fixture_file, File.join(dir, file)
    end

    dir
  end

end

describe "MacRuby's CompileTask extension for rake" do

  before(:each) do
    Rake.application = Rake::Application.new
  end

  it 'creates a task named :compile by default' do # and other defaults
    ct = Rake::CompileTask.new
    ct.should_not be_nil
    ct.name.should == :compile
    ct.libs.should == ['lib']
    ct.files.should be_empty
    ct.verbose.should be_true

    Rake::Task.task_defined?(:compile).should be_true
    Rake::Task.task_defined?(:clobber_compile).should be_true
    Rake::Task.task_defined?(:clobber).should be_true

    clobber = Rake::Task.tasks.find { |x| x.name == 'clobber' }
    clobber.prerequisites.should include('clobber_compile')
  end

  it 'allows configuration of the task' do
    ct = Rake::CompileTask.new(:rbo_ify) do |t|
      t.libs    = ['ext', 'test']
      t.files   = ['docs/sample_code.rb', 'bin/foo']
      t.verbose = false
    end
    ct.should_not be_nil
    ct.name.should == :rbo_ify
    ct.libs.should == ['ext', 'test']
    ct.files.should == ['docs/sample_code.rb', 'bin/foo']
    ct.verbose.should be_false

    Rake::Task.task_defined?(:rbo_ify).should be_true
    Rake::Task.task_defined?(:clobber_rbo_ify).should be_true

    clobber = Rake::Task.tasks.find { |x| x.name == 'clobber' }
    clobber.prerequisites.should include('clobber_rbo_ify')
  end


  describe ', in action,' do
    extend CompileTaskSpecHelper

    before(:all) do
      @output = $stdout
    end

    after(:all) do
      $stdout = @output
    end

    before(:each) do
      @original_dir = Dir.pwd
      @dir = prepare_fixture
      Dir.chdir @dir
      $stdout = StringIO.new
    end

    after(:each) do
      Dir.chdir @original_dir
      rm_rf @dir
    end

    it 'compiles files listed in @files or in the directories listed in @libs' do
      Rake::CompileTask.new do |t| t.files = ['test/helper.rb'] end
      Rake::Task.tasks.find { |x| x.name == 'compile' }.execute
      expected_files = files.select { |x|
        x.match(/^lib/) && x.match(/rb$/)
      }.concat(['test/helper.rb']).map! { |x| x + 'o' }.sort!
      Dir.glob('**/*.rbo').sort!.should == expected_files
    end

    it 'avoids recompiling files again unless the source has been modified' do
      test_files = ['lib/lib/zomg.rb', 'lib/lib/cake.rb']
      rbos       = test_files.map { |x| "#{x}o" }

      Rake::CompileTask.new do |t| t.libs, t.files = [], test_files end
      task = Rake::Task.tasks.find { |x| x.name == 'compile' }

      task.execute
      File.open(File.join(@dir, test_files[0]), 'w') do |f| f.puts '#' end
      mtimes = rbos.map { |x| File.mtime File.join(@dir, x) }

      # TODO: should we sleep to create a safety buffer for the mtime?
      task.execute
      mtimes[0].should_not == File.mtime(File.join(@dir, rbos[0]))
      mtimes[1].should     == File.mtime(File.join(@dir, rbos[1]))
    end

    it 'lists the names of files being compiled when verbose' do
      Rake::CompileTask.new do |t|
        t.libs  = []
        t.files = ['lib/lib/cake.rb', 'test/test_compile_task.rb']
      end
      Rake::Task.tasks.find { |x| x.name == 'compile' }.execute
      $stdout.string.should =~ /lib\/lib\/cake.rb/
      $stdout.string.should =~ /test\/test_compile_task.rb/
    end

    it 'creates a working clobber subtask' do
      Rake::CompileTask.new do |t| t.libs, t.files = [], ['test/helper.rb'] end
      Rake::Task.tasks.find { |x| x.name == 'compile' }.execute
      Rake::Task.tasks.find { |x| x.name == 'clobber_compile' }.execute
      File.exists?(File.join(@dir, 'test/helper.rbo')).should be_false
    end

  # temporary hack while the integration specs depend on an installed macrubyc
  # this is the same problem that we have with testing macruby_deploy
  end unless `which macruby`.empty? || `macruby --version`.chomp != RUBY_DESCRIPTION

end
