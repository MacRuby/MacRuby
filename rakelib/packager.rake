desc "build a nightly pkg installer, use SCM=git-svn if you are using a git-svn repo" 
task :nightly do
  build_destination = '/tmp/macruby-nightly'
  directory build_destination
  rm_rf build_destination
  unless ENV['NO_CLEAN']
    puts "Cleaning the repo"
    `rake clean`
    puts "Updating the repo..."
    if ENV['SCM'] == 'git-svn'
      `git svn rebase`
    else
      `svn up`
    end
  end
  puts "Building MacRuby"
  `rake`
  puts "Preparing for packaging"
  `rake install DESTDIR=#{build_destination}` 
  puts "Packaging MacRuby"
  `/Developer/usr/bin/packagemaker --doc #{File.expand_path(File.dirname(__FILE__))}/../misc/release/macruby_nightly.pmdoc/ --out ~/tmp/macruby_nightly-#{Time.now.strftime("%Y-%m-%d")}.pkg --version #{Time.now.strftime("%Y-%m-%d")}-nightly`
end
