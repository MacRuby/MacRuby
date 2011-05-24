require 'rubygems/config_file'

module Gem
  ConfigFile::PLATFORM_DEFAULTS['install'] = '--no-rdoc --no-ri'
  ConfigFile::PLATFORM_DEFAULTS['update']  = '--no-rdoc --no-ri'
end
