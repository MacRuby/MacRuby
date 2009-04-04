require 'rakelib/git'

namespace :rubyspec do
  desc "Initialize spec/ruby with a rubyspec clone"
  task :init do
    if File.exists? spec_ruby
      unless is_git_project spec_ruby, "rubyspec.git"
        raise "#{spec_ruby} is not a rubyspec clone. Please remove before running this task."
      end
    else
      sh "git clone git://github.com/rubyspec/rubyspec.git #{spec_ruby}"
    end
  end

  desc "Update rubyspec"
  task :update => :init do
    puts "\nUpdating rubyspec repository..."
    Dir.chdir spec_ruby do
      git_update
    end
  end

  desc "Report changes to the rubyspec sources"
  task :status do
    Dir.chdir spec_ruby do
      system "git status"
    end
  end

  desc "Commit changes to the rubyspec sources"
  task :commit do
    puts "\nCommitting changes to rubyspec sources..."
    Dir.chdir spec_ruby do
      sh "git commit -a"
    end
  end

  desc "Push changes to the rubyspec repository"
  task :push => :update do
    puts "\nPushing changes to the rubyspec repository..."
    Dir.chdir spec_ruby do
      git_push
    end
  end

  desc "Switch to the `master' branch"
  task :master do
    Dir.chdir spec_ruby do
      git_checkout('master')
    end
  end

  namespace :sync do
    UPSTREAM_OPTIONS = {
      :branch => "merge_upstream",
      :exclude => %w{ upstream macruby.mspec tags/macruby },
      :revert => %w{ ruby.1.9.mspec }
    }

    desc "Synchronize a checkout with spec/frozen (upstream)"
    task :upstream do
      rev = ENV['REV'] || File.read('spec/frozen/upstream')
      puts "\nSwitching to a `#{UPSTREAM_OPTIONS[:branch]}' branch with current revision of spec/frozen: #{rev}"
      Dir.chdir(spec_ruby) { git_checkout(rev, UPSTREAM_OPTIONS[:branch]) }

      dir = ENV['DIR'] || spec_ruby
      sh "rm -rf #{dir}/**"

      rsync_options = Rsync_options.sub("--exclude 'tags'", '')
      rsync_options += UPSTREAM_OPTIONS[:exclude].map { |f| "--exclude '#{f}'" }.join(' ')
      rsync "spec/frozen/*", dir, rsync_options

      Dir.chdir(spec_ruby) do
        sh "git checkout #{UPSTREAM_OPTIONS[:revert].join(' ')}"
        sh "git status"
      end
    end

    desc "Remove the `#{UPSTREAM_OPTIONS[:branch]}' branch and switch to the `master' branch (cleans all untracked files!)"
    task :remove_upstream do
      puts "\nRemoving the `#{UPSTREAM_OPTIONS[:branch]}' branch and all untracked files!"
      Dir.chdir spec_ruby do
        sh "git clean -f"
        sh "git checkout ."
        git_checkout('master')
        sh "git branch -D #{UPSTREAM_OPTIONS[:branch]}"
      end
    end

    desc "Synchronize spec/frozen with a current checkout (downstream)"
    task :downstream => 'rubyspec:update' do
      dir = ENV['DIR'] || spec_ruby

      rm_rf "spec/frozen"
      rsync dir + "/*", "spec/frozen"

      version = Dir.chdir(dir) { `git log --pretty=oneline -1`[0..7] }
      sh "git add spec/frozen/"
      sh "git commit -m 'Updated CI frozen specs to RubySpec #{version}.' spec/frozen"
    end
  end

  namespace :url do
    desc "Switch to the rubyspec commiter URL"
    task :committer do
      Dir.chdir spec_ruby do
        sh "git config remote.origin.url git@github.com:rubyspec/rubyspec.git"
      end
      puts "\nYou're now accessing rubyspec via the committer URL."
    end

    desc "Switch to the rubyspec anonymous URL"
    task :anon do
      Dir.chdir spec_ruby do
        sh "git config remote.origin.url git://github.com/rubyspec/rubyspec.git"
      end
      puts "\nYou're now accessing rubyspec via the anonymous URL."
    end
  end
end