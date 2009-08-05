desc "Create config.h"
task :config_h do
  config_h = 'include/ruby/config.h'
  new_config_h = File.read(config_h + '.in') << "\n"
  flag = ['/System/Library/Frameworks', '/Library/Frameworks'].any? do |p|
    File.exist?(File.join(p, 'BridgeSupport.framework'))
  end 
  new_config_h << "#define HAVE_BRIDGESUPPORT_FRAMEWORK #{flag ? 1 : 0}\n"
  flag = File.exist?('/usr/include/auto_zone.h')
  new_config_h << "#define HAVE_AUTO_ZONE_H #{flag ? 1 : 0}\n"
  new_config_h << "#define ENABLE_DEBUG_LOGGING 1\n" if ENABLE_DEBUG_LOGGING
  new_config_h << "#define RUBY_PLATFORM \"#{NEW_RUBY_PLATFORM}\"\n"
  new_config_h << "#define RUBY_LIB \"#{RUBY_LIB}\"\n"
  new_config_h << "#define RUBY_ARCHLIB \"#{RUBY_ARCHLIB}\"\n"
  new_config_h << "#define RUBY_SITE_LIB \"#{RUBY_SITE_LIB}\"\n"
  new_config_h << "#define RUBY_SITE_LIB2 \"#{RUBY_SITE_LIB2}\"\n"
  new_config_h << "#define RUBY_SITE_ARCHLIB \"#{RUBY_SITE_ARCHLIB}\"\n"
  new_config_h << "#define RUBY_VENDOR_LIB \"#{RUBY_VENDOR_LIB}\"\n"
  new_config_h << "#define RUBY_VENDOR_LIB2 \"#{RUBY_VENDOR_LIB2}\"\n"
  new_config_h << "#define RUBY_VENDOR_ARCHLIB \"#{RUBY_VENDOR_ARCHLIB}\"\n"
  if !File.exist?(config_h) or File.read(config_h) != new_config_h
    File.open(config_h, 'w') { |io| io.print new_config_h }
    ext_dir = ".ext/include/#{NEW_RUBY_PLATFORM}/ruby"
    mkdir_p(ext_dir)
    cp(config_h, ext_dir)
  end
end

desc "Create dtrace.h"
task :dtrace_h do
  dtrace_h = 'dtrace.h'
  if !File.exist?(dtrace_h) or File.mtime(dtrace_h) < File.mtime('dtrace.d')
    sh "/usr/sbin/dtrace -h -s dtrace.d -o new_dtrace.h"
    if !File.exist?(dtrace_h) or File.read(dtrace_h) != File.read('new_dtrace.h')
      mv 'new_dtrace.h', dtrace_h
    end
  end
end

desc "Create revision.h"
task :revision_h do
  revision_h = 'revision.h'
  current_revision = nil
  if File.exist?('.svn')
    info = `sh -c 'LANG=C svn info' 2>&1`
    md_revision = /^Revision: (\d+)$/.match(info)
    md_url = /^URL: (.+)$/.match(info)
    current_revision = "svn revision #{md_revision[1]} from #{md_url[1]}" if md_revision and md_url
  end
  if not current_revision and File.exist?('.git/HEAD')
    commit_sha1 = nil
    head = File.read('.git/HEAD').strip
    md_ref = /^ref: (.+)$/.match(head)
    if md_ref
      # if the HEAD file contains "ref: XXXX", it's the reference to a branch
      # so we read the file indicating the last commit of the branch
      head_file = ".git/#{md_ref[1]}"
      commit_sha1 = File.read(head_file).strip if File.exist?(head_file)
    else
      # otherwise, it's a detached head so it should be the SHA1 of the last commit
      commit_sha1 = head
    end
    current_revision = "git commit #{commit_sha1}" if commit_sha1 and not commit_sha1.empty?
  end
  current_revision = 'unknown revision' unless current_revision
  
  new_revision_h = "#define MACRUBY_REVISION \"#{current_revision}\"\n"
  
  must_recreate_header = true
  if File.exist?(revision_h)
    must_recreate_header = false if File.read(revision_h) == new_revision_h
  end
  
  if must_recreate_header
    File.open(revision_h, 'w') do |f|
      f.print new_revision_h
    end
  end
end