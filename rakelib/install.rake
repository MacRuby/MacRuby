require 'fileutils'

# INSTRUBY_ARGS = "#{SCRIPT_ARGS} --data-mode=0644 --prog-mode=0755
# --installed-list #{INSTALLED_LIST} --mantype=\"doc\" --sym-dest-dir=\"#{SYM_INSTDIR}\"
# --rdoc-output=\"doc\""

namespace :install do

  def xcode_dir
    @xcode_dir ||= `xcode-select -print-path`.chomp
  end

  task :all => [:nibtool]

  desc 'Install MacRuby support for Interface Builder'
  task :nibtool do
    puts 'installing IB support'
    ib_dest = "#{xcode_dir}/usr/bin"
    mkdir_p ib_dest
    ln_sfh File.join('../../..', CONFIG['bindir'], 'rb_nibtool'), ib_dest
  end

  task :xcode_templates do
  end

end
