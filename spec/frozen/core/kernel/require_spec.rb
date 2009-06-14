require File.dirname(__FILE__) + '/../../spec_helper'

$require_fixture_dir = (File.dirname(__FILE__) + '/../../fixtures/require')
$require_tmp_dir = tmp("require_specs")
$LOAD_PATH << $require_fixture_dir
$LOAD_PATH << $require_tmp_dir

$require_spec   = nil
$require_spec_1 = nil
$require_spec_2 = nil
$require_spec_3 = nil
$require_spec_4 = nil
$require_spec_5 = nil
$require_spec_6 = nil
$require_spec_7 = nil
$require_spec_8 = nil
$require_spec_9 = nil
$require_spec_10 = nil
$require_spec_rooby = nil
$require_spec_recursive = nil

require 'rbconfig'

describe "Kernel#require" do
  conflicts_with :Gem do
    # rubygems redefines #require without setting its
    # visibility back to module_function or private
    it "is a private method" do
      Kernel.should have_private_instance_method(:require)
    end
  end

  # Avoid storing .rbc in repo
  before :all do
    Dir.mkdir($require_tmp_dir)
    Dir.chdir($require_tmp_dir) { 
      FileUtils.touch("require_spec_dummy.#{Config::CONFIG['DLEXT']}")
      FileUtils.touch("require_spec_dummy.rb")
    }
  end

  after :all do
    FileUtils.rm_rf($require_tmp_dir)
  end

  # The files used below just contain code that assigns
  # Time.now to the respective global variable so that
  # reloads can easily be verified.

  # CAUTION: Some of these work because a different path is used to
  #          load the same file. Be careful if you change the order
  #          or add items.

  it "requires arbitrarily complex files (files with large numbers of AST nodes)" do
    lambda {require File.expand_path(File.dirname(__FILE__)) + '/fixtures/test'}.should_not raise_error
  end
  it "loads a .rb from an absolute path and returns true" do
    path = File.expand_path(File.dirname(__FILE__) + '/../../fixtures/require/require_spec_1.rb')

    require(path).should == true
    $require_spec_1.nil?.should == false
  end

  ruby_version_is ""..."1.9" do
    it "loads a .rb from a relative path and returns true" do
      Dir.chdir($require_fixture_dir) do |dir|
        $require_spec_1 = nil
        require('../../fixtures/require/require_spec_1.rb').should == true
        $require_spec_1.nil?.should == false

        $require_spec_1 = nil
        require('./../require/require_spec_1.rb').should == true
        $require_spec_1.nil?.should == false
      end
    end
  end

  ruby_version_is "1.9" do
    it "can load .rb files from relative paths" do
      Dir.chdir($require_fixture_dir) do |dir|
        abs_path = File.expand_path('./../require/require_spec_1.rb')
        $LOADED_FEATURES.delete abs_path
        $require_spec_1 = nil
        require('../../fixtures/require/require_spec_1.rb').should be_true
        $require_spec_1.should_not be_nil
        $LOADED_FEATURES.grep(/require_spec_1\.rb/).should == [abs_path]
      end
    end

    it "collapses consecutive path separators" do
      Dir.chdir($require_fixture_dir) do |dir|
        abs_path = File.expand_path('./../require/require_spec_1.rb')
        $LOADED_FEATURES.delete_if {|p| p =~ /require_spec_1\.rb$/ }
        # Put the absolute path into $LOADED_FEATURES
        require(abs_path).should be_true
        path_parts = File.split(abs_path)
        # Mangle the absolute path so it contains multiple consecutive separator
        # characters, then require it. We've already require'd the canonical
        # form of this path, so this should return false
        require([
          path_parts[0], File::Separator, path_parts[1]
        ].join(File::Separator)).should be_false

        # $LOADED_FEATURES should only contain the absolute path
        $LOADED_FEATURES.grep(/require_spec_1\.rb/).should == [abs_path]
      end
    end

    it "collapses '../' inside an absolute path" do
      Dir.chdir($require_fixture_dir) do |dir|
        abs_path = File.expand_path('./../require/require_spec_1.rb')
        $LOADED_FEATURES.delete_if {|p| p =~ /require_spec_1\.rb$/ }
        # Put the absolute path into $LOADED_FEATURES
        require(abs_path).should be_true
        # Create an absolute path (beginning at the filesystem root), containing
        # '../', then the real absolute path.
        #FIXME: Fix to work on Windows. :-(
        require("/tmp/../#{abs_path}").should be_false

        # $LOADED_FEATURES should only contain the absolute path
        $LOADED_FEATURES.grep(/require_spec_1\.rb/).should == [abs_path]
      end
    end

    it "canonicalizes relative paths" do
      Dir.chdir($require_fixture_dir) do |dir|
        abs_path = File.expand_path('../require/require_spec_1.rb')
        $LOADED_FEATURES.delete_if {|p| p =~ /require_spec_1\.rb$/ }
        # Put the absolute path into $LOADED_FEATURES
        require(abs_path).should be_true
        require("../require/require_spec_1.rb").should be_false

        # $LOADED_FEATURES should only contain the absolute path
        $LOADED_FEATURES.grep(/require_spec_1\.rb/).should == [abs_path]
      end
    end
  end

  it "loads an unqualified .rb by looking in $LOAD_PATH and returns true" do
    require('require_spec_2.rb').should == true
    $require_spec_2.nil?.should == false
  end

  # TODO: add an implementation-agnostic method for creating
  # an extension file
  it "loads extension files"

  it "does not expand/resolve qualified files against $LOAD_PATH" do
    num_features = $LOADED_FEATURES.size
    Dir.chdir($require_fixture_dir + '/../') do |dir|
      # This would be a valid path if expanded against the fixture dir
      lambda { require '../require/require_spec_2.rb' }.should raise_error LoadError
    end
    $LOADED_FEATURES.size.should == num_features
  end

  ruby_version_is ""..."1.9" do
    it "allows unqualified files to contain path information (just not in the beginning)" do
      name = (File.dirname(__FILE__) + '/../../fixtures')
      $LOAD_PATH << name

      $require_spec2 = nil
      require('require/../require/require_spec_2.rb').should == true
      $require_spec_2.nil?.should == false
      
      $LOAD_PATH.delete name
    end
  end

  ruby_version_is "1.9" do
    it "allows unqualified files to contain path information (just not in the beginning)" do
      # Forget that we've already loaded this file
      $LOADED_FEATURES.delete File.expand_path(
        File.join($require_fixture_dir, 'require_spec_2.rb'))
      name = (File.dirname(__FILE__) + '/../../fixtures')
      # Append fixtures directory to $LOAD_PATH
      $LOAD_PATH << name
      $require_spec2 = nil
      # Require fixtures file using path relative to $LOAD_PATH
      require('require/../require/require_spec_2.rb').should be_true
      $require_spec_2.should_not be_nil
      $LOAD_PATH.delete File.expand_path(name)
    end
  end

  it "loads a file with ./filename even if . is not in path" do
    Dir.chdir($require_fixture_dir) do |dir| 
	 path_backup = $LOAD_PATH.clone
	 $LOAD_PATH.clear
	 $LOAD_PATH << "Someirrelevantpath"
     begin
      require('./require_spec.rb').should == true    
     ensure
	  $LOAD_PATH.clear
	  $LOAD_PATH.concat(path_backup)    
	 end 
	end
  end
  
  it "appends a file with no extension with .rb/.<ext> in that order to locate file" do
    load('require_spec')
    $require_spec.should == :noext
    load('require_spec.rb')
    $require_spec.should == :rb

    $require_spec = nil

    # 1.9 won't re-require 'require_spec.rb' because it already appears in
    # $LOADED_FEATURES, so we delete it first. On 1.8 this has no effect.
    $LOADED_FEATURES.delete File.expand_path(
      File.join($require_fixture_dir, 'require_spec.rb'))
    require('require_spec')
    $require_spec.should == :rb
  end

  ruby_version_is ""..."1.9" do
    it "prefers to use .rb over .<ext> if given non-extensioned file and both exist" do
      require('require_spec_dummy').should == true
      $LOADED_FEATURES.include?('require_spec_dummy.rb').should == true
      $LOADED_FEATURES.include?("require_spec_dummy.#{Config::CONFIG['DLEXT']}").should == false
    end
  end

  ruby_version_is "1.9" do
    it "prefers to use .rb over .<ext> if given non-extensioned file and both exist" do
      rb_abs_path = File.expand_path(
        File.join($require_tmp_dir, 'require_spec_dummy.rb'))
      ext_abs_path = rb_abs_path.sub(/\.rb$/, '.' + Config::CONFIG['DLEXT']) 
      require('require_spec_dummy').should be_true
      $LOADED_FEATURES.include?(rb_abs_path).should be_true
      $LOADED_FEATURES.include?(ext_abs_path).should be_false
    end
  end

  runner_is_not :rspec do
    it "will not add a bad load to LOADED_FEATURES" do
      lambda { require('require_spec_raises') }.should raise_error(RuntimeError)
    
      $LOADED_FEATURES.include?('require_spec_raises.rb').should == false
    end
  end

  ruby_version_is ""..."1.9" do
    it "will load file.rb when given 'file' if it exists even if file.<ext> is loaded" do
      $LOADED_FEATURES << "require_spec_3.#{Config::CONFIG['DLEXT']}"
      require('require_spec_3.rb').should == true
      $LOADED_FEATURES.include?('require_spec_3.rb').should == true
    end
  end

  ruby_version_is "1.9" do
    it "will load file.rb when given 'file' if it exists even if file.<ext> is loaded" do
      $LOADED_FEATURES << File.expand_path(
        File.join($require_fixture_dir, "require_spec_3.#{Config::CONFIG['DLEXT']}"))
      require('require_spec_3.rb').should be_true
      $LOADED_FEATURES.include?(File.expand_path(
        File.join($require_fixture_dir, 'require_spec_3.rb'))).should be_true
    end
  end

  it "will not load file.<ext> when given 'file' if file.rb already loaded" do
    unless $LOADED_FEATURES.include?('require_spec_dummy.rb')
      require('require_spec_dummy.rb') 
    end

    require('require_spec_dummy').should == false
  end

  # TODO: add an implementation-agnostic method for creating
  # an extension file
  it "will load explicit file.<ext> even if file.rb already loaded and vice versa"

  it "appends any non-ruby extensioned file with .rb/.<ext> in that order to locate file" do
    load('require_spec.rooby')
    $require_spec_rooby.should == :rooby
    load('require_spec.rooby.rb')
    $require_spec_rooby.should == :rb

    $require_spec_rooby = nil

    require('require_spec.rooby')
    $require_spec_rooby.should == :rb
  end

  ruby_version_is ""..."1.9" do
    it "produces __FILE__ as the given filename and __LINE__ as the source line number" do
      Dir.chdir($require_fixture_dir) do |dir|
        require('require_spec_4').should == true 
        $require_spec_4.should == [['./require_spec_4.rb', 1], ['./require_spec_4.rb', 10]]

        extended_on :rubinius do
          `rm require_spec_4.rbc`
        end
      end

      $require_spec_4 = nil

      require("#{$require_fixture_dir}/require_spec_4").should == true 
      $require_spec_4[0][0].should =~ %r[^.*/fixtures/require/require_spec_4.rb]
      $require_spec_4[0][1].should == 1
      $require_spec_4[1][0].should =~ %r[^.*/fixtures/require/require_spec_4.rb]
      $require_spec_4[1][1].should == 10
    end
  end

  ruby_version_is "1.9" do
    it "produces __FILE__ as the given filename and __LINE__ as the source line number" do
      abs_path = File.expand_path(
        File.join($require_fixture_dir, 'require_spec_4.rb'))
      Dir.chdir($require_fixture_dir) do |dir|
        require('require_spec_4').should be_true 
        $require_spec_4.should == [[abs_path, 1], [abs_path, 10]]

        extended_on :rubinius do
          `rm require_spec_4.rbc`
        end
      end

      $require_spec_4 = nil
      $LOADED_FEATURES.delete abs_path
      require('require_spec_4').should be_true 
      $require_spec_4[0][0].should =~ %r[^.*/fixtures/require/require_spec_4.rb]
      $require_spec_4[0][1].should == 1
      $require_spec_4[1][0].should =~ %r[^.*/fixtures/require/require_spec_4.rb]
      $require_spec_4[1][1].should == 10
    end
  end

  ruby_version_is ""..."1.9" do
    it "stores the loaded file in $LOADED_FEATURES" do
      $LOADED_FEATURES.include?('require_spec_6.rb').should == false
      require('require_spec_6.rb').should == true
      $LOADED_FEATURES.include?('require_spec_6.rb').should == true
    end
  end

  ruby_version_is "1.9" do
    it "stores the loaded file in $LOADED_FEATURES" do
      abs_path = File.expand_path(
        File.join($require_fixture_dir, 'require_spec_6.rb'))
      $LOADED_FEATURES.include?(abs_path).should be_false
      require('require_spec_6.rb').should be_true
      $LOADED_FEATURES.include?(abs_path).should be_true
    end
  end

  ruby_version_is ""..."1.9" do
    it "stores a non-extensioned file with its located suffix" do
      $LOADED_FEATURES.delete 'require_spec_6.rb'

      require('require_spec_6').should == true

      $LOADED_FEATURES.include?('require_spec_6.rb').should == true
    end
  end

  ruby_version_is "1.9" do
    it "stores a non-extensioned file with its located suffix" do
      abs_path = File.expand_path(
        File.join($require_fixture_dir, 'require_spec_6.rb'))
      $LOADED_FEATURES.delete abs_path

      require('require_spec_6').should be_true

      $LOADED_FEATURES.include?(abs_path).should be_true
    end
  end

  # The 1.9 version of this test is described in it "normalises .rb paths
  #  before storing them in $LOADED_FEATURES"
  ruby_version_is ""..."1.9" do
    it "bases the filename in $LOADED_FEATURES on the path given, not just basename" do
      $LOADED_FEATURES.include?('require_spec_6.rb').should == true

      Dir.chdir($require_fixture_dir) do |dir|
        require('../../fixtures/require/require_spec_6.rb').should == true 
      end

      $LOADED_FEATURES.include?('require_spec_6.rb').should == true
      $LOADED_FEATURES.include?('../../fixtures/require/require_spec_6.rb').should == true
    end
  end

  ruby_version_is ""..."1.9" do
    it "will not load the same file twice, returns false instead" do
      $LOADED_FEATURES.include?('require_spec_7.rb').should == false

      require('require_spec_7.rb').should == true
      a = $require_spec_7
      a.nil?.should == false
      
      $LOADED_FEATURES.include?('require_spec_7.rb').should == true

      require('require_spec_7.rb').should == false
      b = $require_spec_7
      b.nil?.should == false

      # Timestamps should not differ
      a.should eql(b)

      $LOADED_FEATURES.include?('require_spec_7.rb').should == true
    end
  end

  ruby_version_is "1.9" do
    it "will not load the same file twice, returns false instead" do
      abs_path = File.expand_path(
        File.join($require_fixture_dir, 'require_spec_7.rb'))
      $LOADED_FEATURES.include?(abs_path).should be_false

      require('require_spec_7.rb').should be_true
      a = $require_spec_7
      a.should_not be_nil
      
      $LOADED_FEATURES.include?(abs_path).should be_true

      require('require_spec_7.rb').should be_false
      b = $require_spec_7
      b.should_not be_nil

      # Timestamps should not differ
      a.should eql(b)

      $LOADED_FEATURES.include?(abs_path).should be_true
    end
  end

  ruby_version_is ""..."1.9" do
    it "checks $LOADED_FEATURES to see whether file is already loaded" do
      $LOADED_FEATURES.include?('require_spec_7.rb').should == true
      require('require_spec_7.rb').should == false

      $LOADED_FEATURES.delete 'require_spec_7.rb'
      require('require_spec_7.rb').should == true
      require('require_spec_7.rb').should == false

      $LOADED_FEATURES.include?('require_spec_7.rb').should == true
    end
  end
  
  ruby_version_is "1.9" do
    it "checks $LOADED_FEATURES to see whether file is already loaded" do
      abs_path = File.expand_path(
        File.join($require_fixture_dir, 'require_spec_7.rb'))
      $LOADED_FEATURES.include?(abs_path).should be_true
      require('require_spec_7.rb').should be_false

      $LOADED_FEATURES.delete abs_path
      require('require_spec_7.rb').should be_true
      require('require_spec_7.rb').should be_false

      $LOADED_FEATURES.include?(abs_path).should be_true
    end
  end

  it "raises a LoadError if the file can't be found" do
    lambda { require "nonesuch#{$$}#{Time.now.to_f}" }.should raise_error LoadError
  end

  ruby_version_is ""..."1.9" do
    it "only accepts strings" do
      lambda { require(nil) }.should raise_error(TypeError)
      lambda { require(42)  }.should raise_error(TypeError)
      lambda { require([])  }.should raise_error(TypeError)
    end
  end

  ruby_version_is "1.9" do
    it "only accepts strings or objects with #to_path" do
      lambda { require(nil) }.should raise_error(TypeError)
      lambda { require(42)  }.should raise_error(TypeError)
      lambda { require([])  }.should raise_error(TypeError)
    end
  end

  ruby_version_is ""..."1.9" do
    it "does not infinite loop on an rb file that requires itself" do
      $require_spec_recursive = nil
      $LOADED_FEATURES.delete 'require_spec_recursive.rb'

      $LOADED_FEATURES.include?('require_spec_recursive.rb').should == false
      require('require_spec_recursive').should == true
      $LOADED_FEATURES.include?('require_spec_recursive.rb').should == true
      $require_spec_recursive.nil?.should == false
    end
  end

  ruby_version_is "1.9" do
    it "does not infinite loop on an rb file that requires itself" do
      abs_path = File.expand_path(
        File.join($require_fixture_dir, 'require_spec_recursive.rb'))
      $require_spec_recursive = nil
      $LOADED_FEATURES.delete abs_path

      $LOADED_FEATURES.include?(abs_path).should be_false
      require('require_spec_recursive').should be_true
      $LOADED_FEATURES.include?(abs_path).should be_true
      $require_spec_recursive.should_not be_nil
    end
    
    it "calls #to_path on non-String arguments" do
      abs_path = File.expand_path(
        File.join($require_fixture_dir, 'require_spec.rb'))
      path = mock('path')
      path.should_receive(:to_path).and_return('require_spec.rb')
      $LOADED_FEATURES.delete abs_path
      require(path).should be_true
      $LOADED_FEATURES.include?(abs_path).should be_true
    end  
  end
end

describe "Shell expansion in Kernel#require" do
  before :all do
    @rs_home = ENV["HOME"]
    ENV["HOME"] = $require_fixture_dir
    @rs_short = "~/require_spec_1.rb"
    @rs_long  = "#{$require_fixture_dir}/require_spec_1.rb"
    @rs_abs = File.expand_path(@rs_long)
  end

  after :all do
    ENV["HOME"] = @rs_home
  end

  before :each do
    [@rs_long, @rs_short, @rs_abs].each do |path|
      $LOADED_FEATURES.delete path
    end      
  end

  it "expands a preceding ~/ to the user's home directory for building the path to search" do
    $require_spec_1 = nil
    require(@rs_short).should == true
    $require_spec_1.nil?.should == false
  end

  it "adds the path to $LOADED_FEATURES" do
    $require_spec_1 = nil
    require(@rs_short).should == true
    $require_spec_1.nil?.should == false

    $LOADED_FEATURES.find {|f| 
      f == @rs_short || f == @rs_long || f == @rs_abs
    }.nil?.should == false
  end
end

describe "Kernel.require" do
  it "needs to be reviewed for spec completeness"
end
