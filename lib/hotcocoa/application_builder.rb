framework 'Foundation'

require 'fileutils'

module HotCocoa
  
  class ApplicationBuilder
    
    class Configuration
      
      attr_reader :name, :version, :icon, :resources, :sources, :info_string, :load
      
      def initialize(file)
        require 'yaml'
        yml = YAML.load(File.read(file))
        @name = yml["name"]
        @load = yml["load"]
        @version = yml["version"] || "1.0"
        @icon = yml["icon"]
        @info_string = yml["info_string"]
        @sources = yml["sources"] || []
        @resources = yml["resources"] || []
        @overwrite = yml["overwrite"] == true ? true : false
        @secure = yml["secure"] == true ? true : false
      end
      
      def overwrite?
        @overwrite
      end

      def secure?
        @secure
      end
      
      def icon_exist?
        @icon ? File.exist?(@icon) : false
      end

    end
    
    ApplicationBundlePackage = "APPL????"
    
    attr_accessor :name, :load_file, :sources, :overwrite, :icon, :version, :info_string, :secure, :resources, :deploy
    
    def self.build(config, options={:deploy => false})
      if !config.kind_of?(Configuration) || !$LOADED_FEATURES.detect {|f| f.include?("standard_rake_tasks")}
        require 'rbconfig'
        puts "Your Rakefile needs to be updated.  Please copy the Rakefile from:"
        puts File.expand_path(File.join(Config::CONFIG['datadir'], "hotcocoa_template", "Rakefile"))
        exit
      end
      builder = new
      builder.deploy = options[:deploy] == true ? true : false
      builder.secure = config.secure?
      builder.name = config.name
      builder.load_file = config.load
      builder.icon = config.icon if config.icon_exist?
      builder.version = config.version
      builder.info_string = config.info_string
      builder.overwrite = config.overwrite?
      config.sources.each do |source|
        builder.add_source_path source
      end
      config.resources.each do |resource|
        builder.add_resource_path resource
      end
      builder.build
    end

    # Used by the "Embed MacRuby" Xcode target.
    def self.deploy(path)
      raise "Given path `#{path}' does not exist" unless File.exist?(path)
      raise "Given path `#{path}' does not look like an application bundle" unless File.extname(path) == '.app'
      deployer = new
      Dir.chdir(File.dirname(path)) do
        deployer.name = File.basename(path, '.app') 
        deployer.deploy
      end
    end

    def initialize
      @sources = []
      @resources = []
    end
      
    def build
      check_for_bundle_root
      build_bundle_structure
      write_bundle_files
      copy_sources
      copy_resources
      deploy if deploy?
      copy_icon_file if icon
    end
   
    def deploy 
      copy_framework
    end

    def deploy?
      @deploy
    end
    
    def overwrite?
      @overwrite
    end
    
    def add_source_path(source_file_pattern)
      Dir.glob(source_file_pattern).each do |source_file|
        sources << source_file
      end
    end

    def add_resource_path(resource_file_pattern)
      Dir.glob(resource_file_pattern).each do |resource_file|
        resources << resource_file
      end
    end
    
    def secure?
      secure
    end
    
    private
    
      def check_for_bundle_root
        if File.exist?(bundle_root) && overwrite?
          `rm -rf #{bundle_root}`
        end
      end
    
      def build_bundle_structure
        Dir.mkdir(bundle_root) unless File.exist?(bundle_root)
        Dir.mkdir(contents_root) unless File.exist?(contents_root)
        Dir.mkdir(frameworks_root) unless File.exist?(frameworks_root)
        Dir.mkdir(macos_root) unless File.exist?(macos_root)
        Dir.mkdir(resources_root) unless File.exist?(resources_root)
      end
      
      def write_bundle_files
        write_pkg_info_file
        write_info_plist_file
        build_executable unless File.exist?(File.join(macos_root, objective_c_executable_file))
        write_ruby_main
      end
      
      def copy_framework
        unless File.exist?(File.join(frameworks_root, 'MacRuby.framework'))
          FileUtils.mkdir_p frameworks_root 
          FileUtils.cp_r macruby_framework_path, frameworks_root
        end
        `install_name_tool -change #{current_macruby_path}/usr/lib/libmacruby.dylib @executable_path/../Frameworks/MacRuby.framework/Versions/#{current_macruby_version}/usr/lib/libmacruby.dylib '#{macos_root}/#{objective_c_executable_file}'`
      end
      
      def copy_sources
        if secure?
          data = {}
          data["/"+load_file] = File.open(load_file, "r") {|f| f.read}
          sources.each do |source|
            data["/"+source] = File.open(source, "r") {|f| f.read}
          end
          File.open(File.join(resources_root, "vfs.db"), "wb") do |db|
            db.write Marshal.dump(data)
          end
        else
          FileUtils.cp_r load_file, resources_root unless sources.include?(load_file)
          sources.each do |source|
            destination = File.join(resources_root, source)
            FileUtils.mkdir_p(File.dirname(destination)) unless File.exist?(File.dirname(destination))
            FileUtils.cp_r source, destination
          end
        end
      end
      
      def copy_resources
        resources.each do |resource|
          destination = File.join(resources_root, resource.split("/")[1..-1].join("/"))
          FileUtils.mkdir_p(File.dirname(destination)) unless File.exist?(File.dirname(destination))
          FileUtils.cp_r resource, destination
        end
      end
      
      def copy_icon_file
        FileUtils.cp(icon, icon_file) unless File.exist?(icon_file)
      end
      
      def write_pkg_info_file
        File.open(pkg_info_file, "wb") {|f| f.write ApplicationBundlePackage}
      end

      def write_info_plist_file
        File.open(info_plist_file, "w") do |f|
          f.puts %{<?xml version="1.0" encoding="UTF-8"?>}
          f.puts %{<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">}
          f.puts %{<plist version="1.0">}
          f.puts %{<dict>}
          f.puts %{	<key>CFBundleDevelopmentRegion</key>}
          f.puts %{ <string>English</string>}
          f.puts %{ <key>CFBundleIconFile</key>} if icon
          f.puts %{ <string>#{name}.icns</string>} if icon
          f.puts %{ <key>CFBundleGetInfoString</key>} if info_string
          f.puts %{ <string>#{info_string}</string>} if info_string
          f.puts %{	<key>CFBundleExecutable</key>}
          f.puts %{	<string>#{name.gsub(/ /, '')}</string>}
          f.puts %{	<key>CFBundleIdentifier</key>}
          f.puts %{	<string>com.yourcompany.#{name}</string>}
          f.puts %{	<key>CFBundleInfoDictionaryVersion</key>}
          f.puts %{	<string>6.0</string>}
          f.puts %{	<key>CFBundleName</key>}
          f.puts %{	<string>#{name}</string>}
          f.puts %{	<key>CFBundlePackageType</key>}
          f.puts %{	<string>APPL</string>}
          f.puts %{	<key>CFBundleSignature</key>}
          f.puts %{	<string>????</string>}
          f.puts %{	<key>CFBundleVersion</key>}
          f.puts %{	<string>#{version}</string>}
          f.puts %{	<key>NSPrincipalClass</key>}
          f.puts %{	<string>NSApplication</string>}
          f.puts %{</dict>}
          f.puts %{</plist>}
        end
      end
      
      def build_executable
        File.open(objective_c_source_file, "wb") do |f| 
          f.puts %{
            
            #import <MacRuby/MacRuby.h>

            int main(int argc, char *argv[])
            {
                return macruby_main("rb_main.rb", argc, argv);
            }
          }
        end
        archs = RUBY_ARCH.include?('ppc') ? '-arch ppc' : '-arch i386 -arch x86_64'
        puts `cd "#{macos_root}" && gcc main.m -o #{objective_c_executable_file} #{archs} -framework MacRuby -framework Foundation -fobjc-gc-only`
        File.unlink(objective_c_source_file)
      end
      
      def write_ruby_main
        File.open(main_ruby_source_file, "wb") do |f|
          if secure?
            require 'hotcocoa/virtual_file_system'
            f.puts VirtualFileSystem.code_to_load(load_file)
          else
            f.puts "$:.map! { |x| x.sub(/^\\/Library\\/Frameworks/, NSBundle.mainBundle.privateFrameworksPath) }" if deploy?
            f.puts "$:.unshift NSBundle.mainBundle.resourcePath.fileSystemRepresentation"
            f.puts "load '#{load_file}'"
          end
        end
      end
      
      def bundle_root
        "#{name}.app"
      end
      
      def contents_root
        File.join(bundle_root, "Contents")
      end

      def frameworks_root
        File.join(contents_root, "Frameworks")
      end
      
      def macos_root
        File.join(contents_root, "MacOS")
      end
      
      def resources_root
        File.join(contents_root, "Resources")
      end

      def bridgesupport_root
        File.join(resources_root, "BridgeSupport")
      end
      
      def info_plist_file
        File.join(contents_root, "Info.plist")
      end
      
      def icon_file
        File.join(resources_root, "#{name}.icns")
      end
      
      def pkg_info_file
        File.join(contents_root, "PkgInfo")
      end
      
      def objective_c_executable_file
        name.gsub(/ /, '')
      end
      
      def objective_c_source_file
        File.join(macos_root, "main.m")
      end
      
      def main_ruby_source_file
        File.join(resources_root, "rb_main.rb")
      end
      
      def current_macruby_version
        NSFileManager.defaultManager.pathContentOfSymbolicLinkAtPath(File.join(macruby_versions_path, "Current"))
      end
      
      def current_macruby_path
        File.join(macruby_versions_path, current_macruby_version)
      end
      
      def macruby_versions_path
        File.join(macruby_framework_path, "Versions")
      end
      
      def macruby_framework_path
        "/Library/Frameworks/MacRuby.framework"
      end

  end
  
end
