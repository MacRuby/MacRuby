require File.expand_path('../../spec_helper', __FILE__)

module DeploySpecHelper
  EMBEDDED_FRAMEWORK = '@executable_path/../Frameworks/MacRuby.framework/Versions/Current/usr/lib/libmacruby.dylib'
  
  def deploy(args)
    ruby_exe(File.join(SOURCE_ROOT, 'bin/ruby_deploy'), :args => "'#{@app_bundle}' #{args} 2>&1")
  end

  def file(path)
    `/usr/bin/file '#{path}'`
  end

  def install_name(path)
    `/usr/bin/otool -L '#{path}'`
  end

  def rbos
    Dir.glob("#{@app_bundle}/Contents/Resources/**/*.rbo")
  end

  def rbs
    Dir.glob("#{@app_bundle}/Contents/Resources/**/*.rb")
  end

  def binaries
    rbos + [File.join(@app_bundle, 'Contents/MacOS/Dummy')]
  end
end

describe "ruby_deploy, in general," do
  extend DeploySpecHelper

  it "checks if the given path is a valid app bundle" do
    @app_bundle = tmp('ruby_deploy/Dummy.app')
    FileUtils.mkdir_p @app_bundle
    deploy('--compile').should include("doesn't seem to be a valid application bundle")
  end
end

describe "The ruby_deploy --compile option" do
  extend DeploySpecHelper

  before do
    dir = tmp('ruby_deploy')
    FileUtils.mkdir_p dir
    @app_bundle = File.join(dir, 'Dummy.app')
    FileUtils.cp_r File.join(FIXTURES, 'dummy_app'), @app_bundle
    # we just need a binary file compiled in the arch for the current env
    FileUtils.mkdir File.join(@app_bundle, 'Contents/MacOS')
    FileUtils.cp File.join(SOURCE_ROOT, 'lib/irb.rbo'), File.join(@app_bundle, 'Contents/MacOS/Dummy')
  end

  it "compiles the ruby source files in the app's Resources directory" do
    deploy('--compile')
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
    deploy('--compile')
    rbos.map { |f| File.basename(f) }.should_not include('rb_main.rbo')
    rbs.map { |f| File.basename(f) }.should include('rb_main.rb')
  end

  it "removes the original source files after compilation" do
    deploy('--compile')
    rbs.map { |f| File.basename(f) }.should == %w{ rb_main.rb }
  end

  it "does not change the install_name of binaries if the MacRuby framework is not embedded" do
    deploy('--compile')
    binaries.each do |bin|
      install_name(bin).should_not include(DeploySpecHelper::EMBEDDED_FRAMEWORK)
    end
  end

  it "changes the install_name of binaries to the embedded MacRuby framework" do
    FileUtils.mkdir_p File.join(@app_bundle, 'Contents/Frameworks/MacRuby.framework')
    deploy('--compile')
    binaries.each do |bin|
      install_name(bin).should include(DeploySpecHelper::EMBEDDED_FRAMEWORK)
    end
  end

  # TODO is it safe to use `ppc7400' here?
  it "retrieves the archs that the ruby files should be compiled for from ENV['ARCHS'] and aborts if that leaves no options" do
    before, ENV['ARCHS'] = ENV['ARCHS'], 'ppc7400'
    begin
      deploy('--compile').should =~ /Can't build for.+?ppc7400/
      $?.success?.should == false
    ensure
      ENV['ARCHS'] = before
    end
  end

  # TODO is it safe to use `ppc' here?
  it "retrieves the arch that the ruby files should be compiled for from the app binary and skips those that can't be used" do
    # copy the system ruby binary which, amongst others, contains `ppc'
    FileUtils.rm File.join(@app_bundle, 'Contents/MacOS/Dummy')
    FileUtils.cp '/usr/bin/ruby', File.join(@app_bundle, 'Contents/MacOS/Dummy')

    deploy('--compile').should =~ /Can't build for.+?ppc7400/
    $?.success?.should == true
  end
end
