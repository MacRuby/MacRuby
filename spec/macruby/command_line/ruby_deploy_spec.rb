require File.expand_path('../../spec_helper', __FILE__)

describe "The ruby_deploy --compile option" do
  before do
    dir = tmp('ruby_deploy')
    FileUtils.mkdir_p dir
    @app_bundle = File.join(dir, 'Dummy.app')
    FileUtils.cp_r File.join(FIXTURES, 'Dummy.app'), @app_bundle
  end

  it "checks if the given path is a valid app bundle" do
    FileUtils.rm_rf File.join(@app_bundle, 'Contents')
    deploy('--compile').should include("doesn't seem to be a valid application bundle")
  end

  it "compiles the ruby source files in the app's Resources directory" do
    deploy '--compile'
    $?.success?.should == true
    rbos = Dir.glob("#{@app_bundle}/Contents/Resources/**/*.rbo")
    rbos.should_not be_empty
    rbos.each { |rbo| file(rbo).should include('Mach-O') }
  end

  def deploy(args)
    result = ruby_exe(File.join(SOURCE_ROOT, 'bin/ruby_deploy'), :args => "'#{@app_bundle}' #{args} 2>&1")
    puts result
    result
  end

  def file(path)
    `/usr/bin/file '#{path}'`
  end
end
