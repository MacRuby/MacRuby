require 'fileutils'
require 'rbconfig'

module HotCocoa
  class Template
    def self.copy_to(directory, app_name)
      FileUtils.mkdir_p(directory)
      dir = Config::CONFIG['datadir']
      Dir.glob(File.join(dir, "hotcocoa_template", "**/*")).each do |file|
        short_name = file[(dir.length+19)..-1]
        if File.directory?(file)
          FileUtils.mkdir_p File.join(directory, short_name)
        else
          File.open(File.join(directory, short_name), "w") do |out|
            input =  File.read(file)
            input.gsub!(/__APPLICATION_NAME__/, app_name)
            out.write input
          end
        end
      end
    end
  end
end
