require 'fileutils'

module HotCocoa
  
  class ApplicationBuilder
    
    ApplicationBundlePackage = "APPL????"
    
    attr_accessor :name, :load_file, :sources, :overwrite, :icon, :version, :info_string, :secure, :resources
    
    def self.build(build_options)
      build_options.each do |key, value|
        build_options[key.intern] = value if key.respond_to?(:intern)
      end
      if build_options[:file]
        require 'yaml'
        build_options = YAML.load(File.read(build_options[:file]))
      end
      builder = new
      builder.secure = (build_options[:secure] == true)
      builder.name = build_options[:name]
      builder.load_file = build_options[:load]
      builder.icon = build_options[:icon] if build_options[:icon] && File.exist?(build_options[:icon])
      builder.version = build_options[:version] || "1.0"
      builder.info_string = build_options[:info_string]
      builder.overwrite = (build_options.include?(:overwrite) ? build_options[:overwrite] : true)
      sources = build_options[:sources] || []
      sources.each do |source|
        builder.add_source_path source
      end
      resources = build_options[:resources] || []
      resources.each do |resource|
        builder.add_resource_path resource
      end
      builder.build
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
      copy_icon_file if icon
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
        if File.exist?(bundle_root)
          if overwrite?
            `rm -rf #{bundle_root}`
          else
            puts "Error, #{bundle_root} already exists, use :overwrite => true to remove"
          end
        end
      end
    
      def build_bundle_structure
        Dir.mkdir(bundle_root)
        Dir.mkdir(contents_root)
        Dir.mkdir(macos_root)
        Dir.mkdir(resources_root)
      end
      
      def write_bundle_files
        write_pkg_info_file
        write_info_plist_file
        build_executable
        write_ruby_main
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
        puts `cd "#{macos_root}" && gcc main.m -o #{name.gsub(/ /, '')} -arch ppc -arch i386 -framework MacRuby -framework Foundation -fobjc-gc-only`
        File.unlink(objective_c_source_file)
      end
      
      def write_ruby_main
        File.open(main_ruby_source_file, "wb") do |f|
          if secure?
            require 'hotcocoa/virtual_file_system'
            f.puts VirtualFileSystem.code_to_load(load_file)
          else
            f.puts %{
              $:.unshift NSBundle.mainBundle.resourcePath.fileSystemRepresentation
              load '#{load_file}'
            }
          end
        end
      end
      
      def bundle_root
        "#{name}.app"
      end
      
      def contents_root
        File.join(bundle_root, "Contents")
      end
      
      def macos_root
        File.join(contents_root, "MacOS")
      end
      
      def resources_root
        File.join(contents_root, "Resources")
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
      
      def objective_c_source_file
        File.join(macos_root, "main.m")
      end
      
      def main_ruby_source_file
        File.join(resources_root, "rb_main.rb")
      end

  end
  
end