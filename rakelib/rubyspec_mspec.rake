require 'rakelib/upstream_git_repo_tasks'

root = File.expand_path("../../", __FILE__)

Rake::UpstreamGitRepoTasks.new :rubyspec do |r|
  r.local_dir = File.join(root, 'spec/frozen')
  r.upstream_dir = ENV['DIR'] || File.join(root, 'spec/ruby')
  r.anon_url = 'git://github.com/rubyspec/rubyspec.git'
  r.commit_url = 'git@github.com:rubyspec/rubyspec.git'
  r.upstream_options[:exclude].concat %w{ macruby.mspec tags/macruby ruby.1.9.mspec }
end

Rake::UpstreamGitRepoTasks.new :mspec do |r|
  r.local_dir = File.join(root, 'mspec')
  r.upstream_dir = ENV['DIR'] || File.join(root, 'mspec_upstream')
  r.anon_url = 'git://github.com/rubyspec/mspec.git'
  r.commit_url = 'git@github.com:rubyspec/mspec.git'
end