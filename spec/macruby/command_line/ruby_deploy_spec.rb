# temporary hack while ruby_deploy specs are dependent on an installed MR
unless `which macruby`.empty? || `macruby --version`.chomp != RUBY_DESCRIPTION

require File.expand_path('../../spec_helper', __FILE__)

module DeploySpecHelper
  include FileUtils

  EMBEDDED_FRAMEWORK = '@executable_path/../Frameworks/MacRuby.framework/Versions/Current/usr/lib/libmacruby.dylib'

  def deploy(args)
    ruby_exe(File.join(SOURCE_ROOT, 'bin/ruby_deploy'), :args => "'#{@app_bundle}' #{args} 2>&1")
  end

  def cached_deploy(args)
    if cache = CACHED_APPS[args]
      @app_bundle = cache[:dir]
      cache[:output]
    else
      output = deploy(args)
      cache_dir = tmp('ruby_deploy_cache')
      mv @app_bundle, cache_dir
      @app_bundle = cache_dir
      CACHED_APPS[args] = { dir: cache_dir, output: output }
      output
    end
  end

  def file(path)
    `/usr/bin/file '#{path}'`
  end

  def install_name(path)
    `/usr/bin/otool -L '#{path}'`
  end

  def glob_join(*path)
    Dir.glob(File.join(*path))
  end

  def resources
    File.join(@app_bundle, 'Contents', 'Resources')
  end

  def rbos
    glob_join(resources, '**', '*.rbo')
  end

  def rbs
    glob_join(resources, '**','*.rb')
  end

  def binaries
    rbos + [File.join(@app_bundle, 'Contents', 'MacOS', 'Dummy')]
  end

  def framework
    File.join(@app_bundle, 'Contents', 'Frameworks', 'MacRuby.framework', 'Versions')
  end

  def framework_resources
    File.join(framework, 'Current', 'Resources')
  end

  def framework_stdlib
    File.join(framework, 'Current', 'usr', 'lib', 'ruby')
  end
end

describe "ruby_deploy, in general," do
  extend DeploySpecHelper

  before do
    @dir = tmp('ruby_deploy')
  end

  after do
    rm_rf @dir
  end

  it 'gives a helpful message if the app bundle does not exist' do
    deploy('--compile').should include('make sure you build the app before running')
  end

  it "checks if the given path is a valid app bundle" do
    @app_bundle = File.join(@dir, 'Dummy.app')
    mkdir_p @app_bundle
    deploy('--compile').should include("doesn't seem to be a valid application bundle")
  end

  describe 'during deployment of apps with spaces in the name,' do
    before do
      mkdir_p @dir
      @app_bundle = File.join(@dir, 'Dummy App.app')
      cp_r File.join(FIXTURES, 'dummy_app'), @app_bundle
      mkdir File.join(@app_bundle, 'Contents/MacOS')
      cp File.join(SOURCE_ROOT, 'lib/irb.rbo'), File.join(@app_bundle, 'Contents/MacOS/Dummy App')
    end

    it 'does not fail to check architectures' do
      before, ENV['ARCHS'] = ENV['ARCHS'], nil
      deploy('--compile')
      ENV['ARCHS'] = before
      $?.success?.should == true
    end

    it 'does not fail to check dynamic linking' do
      deploy('--embed --stdlib bigdecimal')
      $?.success?.should == true
    end
  end
end

describe "ruby_deploy command line options:" do
  extend DeploySpecHelper

  before(:all) do
    CACHED_APPS = {}
  end

  before do
    @dir = tmp('ruby_deploy')
    mkdir_p @dir
    @app_bundle = File.join(@dir, 'Dummy.app')
    cp_r File.join(FIXTURES, 'dummy_app'), @app_bundle
    # we just need a binary file compiled in the arch for the current env
    mkdir File.join(@app_bundle, 'Contents/MacOS')
    cp File.join(SOURCE_ROOT, 'lib/irb.rbo'), File.join(@app_bundle, 'Contents/MacOS/Dummy')
  end

  after do
    rm_rf @dir
  end

  after(:all) do
    CACHED_APPS.each_pair { |_,cache| rm_rf cache[:dir] }
  end

  describe "--compile" do
    it "compiles the ruby source files in the app's Resources directory" do
      cached_deploy('--compile')
      rbos.should_not be_empty
      rbos.each do |rbo|
        file(rbo).should include('Mach-O')
        require rbo
      end
      # check that the classes defined in the rbos actually work
      defined?(DummyModel).should == "constant"
      defined?(DummyController).should == "constant"
    end

    it "does not compile the rb_main.rb file, because this name is hardcoded in the function that starts MacRuby" do
      cached_deploy('--compile')
      rbos.map { |f| File.basename(f) }.should_not include('rb_main.rbo')
      rbs.map { |f| File.basename(f) }.should include('rb_main.rb')
    end

    it "removes the original source files after compilation" do
      cached_deploy('--compile')
      rbs.map { |f| File.basename(f) }.should == %w{ rb_main.rb }
    end

    it "does not change the install_name of binaries if the MacRuby framework is not embedded" do
      cached_deploy('--compile')
      binaries.each do |bin|
        install_name(bin).should_not include(DeploySpecHelper::EMBEDDED_FRAMEWORK)
      end
    end

    it "changes the install_name of binaries to the embedded MacRuby framework" do
      mkdir_p File.join(@app_bundle, 'Contents/Frameworks/MacRuby.framework')
      deploy('--compile')
      binaries.each do |bin|
        install_name(bin).should include(DeploySpecHelper::EMBEDDED_FRAMEWORK)
      end
    end

    it "retrieves the archs that the ruby files should be compiled for from ENV['ARCHS'] and aborts if that leaves no options" do
      before, ENV['ARCHS'] = ENV['ARCHS'], 'klingon'
      begin
        deploy('--compile').should =~ /Can't build for.+?klingon/
        $?.success?.should == false
      ensure
        ENV['ARCHS'] = before
      end
    end

    it "retrieves the arch that the ruby files should be compiled for from the app binary and skips those that can't be used" do
      rm File.join(@app_bundle, 'Contents/MacOS/Dummy')
      # use a hacked file with a ppc Mach-O header and load commands
      cp File.join(FIXTURES, 'ppc_binary'), File.join(@app_bundle, 'Contents/MacOS/Dummy')

      deploy('--compile').should =~ /Can't build for.+?ppc7400/
      $?.success?.should == false # TODO split this into a pair of separate specs
    end
  end

  describe '--embed' do
    it 'copies the framework to Contents/Frameworks' do
      cached_deploy('--embed')
      Dir.exists?(framework).should == true
      Dir.exists?(framework_stdlib).should == true
      File.exists?(File.join(framework, 'Current/usr/lib/libmacruby.1.9.2.dylib'))
    end

    it 'keeps the correct version and Current symlink' do
      cached_deploy('--embed')

      dirs = Dir.entries(framework) - ['.','..']
      dirs.count.should == 2
      dirs.should include('Current')
      dirs.should include(MACRUBY_VERSION)

      File.readlink(File.join(framework, 'Current')).should == MACRUBY_VERSION

      info = load_plist(IO.read(File.join(framework_resources, 'Info.plist')))
      info['CFBundleShortVersionString'].should == MACRUBY_VERSION
    end

    it 'changes the install_name of .rbo files in the embedded framework' do
      cached_deploy('--embed')
      glob_join(framework_stdlib,'**','*.rbo').each do |rbo|
        install_name(rbo).should include(DeploySpecHelper::EMBEDDED_FRAMEWORK)
      end
    end

    it 'does not copy headers, binaries, or documentation into the app bundle' do
      cached_deploy('--embed')
      dirs = Dir.entries(File.join(framework, 'Current', 'usr'))
      ['bin','include','share'].each do |dir|
        dirs.should_not include(dir)
      end
      # TODO is the libmacruby-static.a file used by anyone?
    end

    # TODO this test is too naive
    it 'embeds bridge support files when combined with --bs' do
      cached_deploy('--embed --bs')
      bs_dir = File.join(resources, 'BridgeSupport')
      Dir.exists?(bs_dir)
      (Dir.entries(bs_dir) - ['.', '..']).should_not be_empty
    end

    it 'removes the stdlib when combined with --no-stdlib' do
      cached_deploy('--embed --no-stdlib')
      Dir.exists?(framework_stdlib).should == false
    end

    it 'removes .rb files from the stdlib if an .rbo equivalent exists' do
      cached_deploy('--embed')
      files = glob_join(framework_stdlib,'**','*.rbo')
      files.any? { |rbo| File.exists?("#{rbo.chomp!('o')}") }.should be_false
    end




    describe 'when combined with --stdlib' do
      it 'a specific lib to be embedded' do
        cached_deploy('--embed --stdlib ubygems')
        files = glob_join(framework_stdlib,'**','*.rb*').map do |f|
          File.basename(f).chomp(File.extname(f))
        end.uniq
        files.should == ['ubygems']
      end

      it 'any number of times, all listed libs will be embedded' do
        cached_deploy('--embed --stdlib base64 --stdlib minitest')

        expected = ['base64'] +
          glob_join(SOURCE_ROOT,'lib','minitest','*').map do |lib|
          File.basename(lib).chomp(File.extname(lib))
        end

        actual = glob_join(framework_stdlib,'**','*.rb').map do |lib|
          File.basename(lib).chomp(File.extname(lib))
        end

        actual.should == expected
      end
    end

    it 'removes previous embedded frameworks before a new embedding' do
      mkdir_p framework_resources
      file = File.join(framework_resources, 'fake')
      File.open(file, 'w') { |f| f.write 'This file does not exist' }
      deploy('--embed --no-stdlib')
      File.exists?(file).should be_false
    end
  end


end
end
