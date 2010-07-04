unless defined?(MSpec)
  require 'rubygems'
  require 'mspec'
end

ENV['SPECCING'] = 'true'

root = File.expand_path('../../', __FILE__)
if File.basename(root) == 'spec'
  # running from the MacRuby repo
  ROOT = File.expand_path('../../../', __FILE__)
else
  ROOT = root
end
$:.unshift File.join(ROOT, 'lib')

require 'irb'