# encoding: utf-8

# See ./rakelib/*.rake for all tasks.

ENV['RUBYOPT'] = '' # no RUBYOPT for spawned MacRuby processes

desc "Same as all"
task :default => :all

desc "Same as install:all"
task :install => 'install:standard'

desc "Same as macruby:build"
task :macruby => 'macruby:build'

# The old test tasks are now commented since we are switching to RubySpec
# for regression testing. We still add a task to run the VM regression test
# suite, though.
desc "Run the VM regression test suite"
task :test_vm do
  sh "/usr/bin/ruby test_vm.rb"
end
=begin
desc "Run the sample tests"
task :sample_test do
  sh "./miniruby rubytest.rb"
end

desc "Run the unit tests"
task :unit_tests do
  sh "./miniruby test/macruby_runner.rb"
end

desc "Run all tests"
task :test => [:sample_test, :unit_tests]
=end

desc "Clean local and extension build files"
task :clean => ['clean:local', 'clean:rbo', 'clean:ext', 'clean:doc', 'clean:info_plist']

desc "Build everything"
task :all => [:macruby, 'stdlib:build', :extensions, :doc, :info_plist]

desc "Create an archive (GIT only)"
task :git_archive do
  sh "git archive --format=tar --prefix=MacRuby-HEAD/ HEAD | gzip >MacRuby-HEAD.tar.gz"
end

desc "Run all 'known good' specs (task alias for spec:ci)"
task :spec => 'spec:ci'

desc "Run IRB"
task :irb do
  exec './miniruby -I./lib ./bin/irb'
end
