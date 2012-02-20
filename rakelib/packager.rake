desc "build a nightly pkg installer"
task :nightly do
  build_destination = '/tmp/macruby-nightly'
  directory build_destination
  rm_rf build_destination
  unless ENV['NO_CLEAN']
    puts "Cleaning the repo"
    `rake clean`
    puts "Updating the repo..."
    if File.exist?('.git')
      `git pull`
    else
      `svn up`
    end
  end

  puts "Building MacRuby"
  `rake`

  puts "Preparing for packaging"
  `rake install DESTDIR=#{build_destination}` 

  ## prepare for post install script
  xcode_dir = `xcode-select -print-path`
  # remove '/tmp/macruby-nightly/Developer/xxx'
  rm_rf "#{build_destination}/#{xcode_dir.split('/')[1]}"

  temporary_dir = "#{build_destination}/tmp/macruby"
  mkdir_p temporary_dir
  cp_r "misc/xcode4-templates/", temporary_dir
  cp_r "sample-macruby", temporary_dir

  puts "Packaging MacRuby"
  package_dir  = ["#{ENV['HOME']}/tmp", "#{ENV['HOME']}/Desktop", '/tmp'].find { |dir| File.exist?(dir) }
  package_date = Time.now.strftime("%Y-%m-%d")
  package      = "#{package_dir}/macruby_nightly-#{package_date}.pkg"
  `/Developer/usr/bin/packagemaker --doc #{File.expand_path(File.dirname(__FILE__))}/../misc/release/macruby_nightly.pmdoc/ --out #{package} --scripts #{File.expand_path(File.dirname(__FILE__))}/../misc/release/package_script --version #{package_date}-nightly`
  if $?.success?
    puts "Package saved to #{package}"
  else
    puts "Failed to save package"
  end
end
