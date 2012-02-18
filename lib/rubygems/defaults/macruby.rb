require 'rubygems/config_file'
require 'rbconfig'

module Gem
  ConfigFile::PLATFORM_DEFAULTS['install'] = '--no-rdoc --no-ri'
  ConfigFile::PLATFORM_DEFAULTS['update']  = '--no-rdoc --no-ri'

  Platform::MACRUBY         = Platform.new([Platform.local.cpu, RUBY_ENGINE])
  Platform::MACRUBY_CURRENT = Platform.new([Platform.local.cpu, RUBY_ENGINE,
                                            MACRUBY_VERSION.split('.')[0, 2].join('.')])
  platforms << Platform::MACRUBY_CURRENT

  def self.default_dir
    "/Library/Ruby/Gems/MacRuby/#{MACRUBY_VERSION.to_f}"
  end

  def self.default_bindir
    "#{RbConfig::CONFIG['symdir']}/bin"
  end
end
