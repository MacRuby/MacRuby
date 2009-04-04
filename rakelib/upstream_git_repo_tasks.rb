require 'rakelib/git'

class Rake::UpstreamGitRepoTasks
  attr_accessor :name, :local_dir, :upstream_dir, :anon_url, :commit_url, :upstream_options
  
  def initialize(name)
    @name = name
    @upstream_options = {
      :branch => 'merge_upstream',
      :exclude => %w{ upstream }
    }
    yield self
    define
  end
  
  def upstream_rev
    @upstream_rev ||= ENV['REV'] || File.read(File.join(@local_dir, 'upstream'))
  end
  
  def define
    namespace @name do
      desc "Initialize #{@upstream_dir} with a #{@name} clone"
      task :init do
        if File.exists?(@upstream_dir)
          unless is_git_project(@upstream_dir, "#{@name}.git")
            raise "#{@upstream_dir} is not a #{@name} clone. Please remove before running this task."
          end
        else
          sh "git clone #{@anon_url} #{@upstream_dir}"
        end
      end

      desc "Update #{@name}"
      task :update => :init do
        puts "\nUpdating #{@name} repository..."
        Dir.chdir @upstream_dir do
          git_update
        end
      end

      desc "Report changes to the #{@name} sources"
      task :status do
        Dir.chdir @upstream_dir do
          system "git status"
        end
      end

      desc "Commit changes to the #{@name} sources"
      task :commit do
        puts "\nCommitting changes to #{@name} sources..."
        Dir.chdir @upstream_dir do
          sh "git commit -a"
        end
      end

      desc "Push changes to the #{@name} repository"
      task :push => :update do
        puts "\nPushing changes to the #{@name} repository..."
        Dir.chdir @upstream_dir do
          git_push
        end
      end

      desc "Switch to the `master' branch"
      task :master do
        Dir.chdir @upstream_dir do
          git_checkout('master')
        end
      end

      namespace :url do
        desc "Switch to the #{@name} commiter URL"
        task :committer do
          Dir.chdir @upstream_dir do
            sh "git config remote.origin.url #{@commit_url}"
          end
          puts "\nYou're now accessing #{@name} via the committer URL."
        end

        desc "Switch to the #{@name} anonymous URL"
        task :anon do
          Dir.chdir @upstream_dir do
            sh "git config remote.origin.url #{@anon_url}"
          end
          puts "\nYou're now accessing #{@name} via the anonymous URL."
        end
      end

      namespace :sync do
        desc "Synchronize a checkout with #{@local_dir} (upstream)"
        task :upstream do
          puts "\nSwitching to a `#{@upstream_options[:branch]}' branch with current revision of #{@local_dir}: #{upstream_rev}"
          Dir.chdir(@upstream_dir) { git_checkout(upstream_rev, @upstream_options[:branch]) }
          sh "rm -rf #{@upstream_dir}/**"

          rsync_options = Rsync_options.sub("--exclude 'tags'", '')
          rsync_options += @upstream_options[:exclude].map { |f| "--exclude '#{f}'" }.join(' ')
          rsync "#{@local_dir}/*", @upstream_dir, rsync_options

          Dir.chdir(@upstream_dir) { system "git status" }
        end

        namespace :upstream do
          desc "Creates all individual patches in #{@local_dir}/upstream_patches since upstream revision: #{upstream_rev}"
          task :patches do
            create_patches = "git format-patch --numbered --output-directory #{File.join(@local_dir, 'upstream_patches')} #{upstream_rev}"
            Dir.chdir(@upstream_dir) do
              git_checkout('master')
              sh create_patches
            end
          end

          desc "Remove the `#{@upstream_options[:branch]}' branch and switch to the `master' branch (cleans all untracked files!)"
          task :remove do
            puts "\nRemoving the `#{@upstream_options[:branch]}' branch and all untracked files!"
            Dir.chdir @upstream_dir do
              sh "git clean -f"
              sh "git checkout ."
              git_checkout('master')
              sh "git branch -D #{@upstream_options[:branch]}"
            end
          end
        end

        # desc "Synchronize spec/frozen with a current checkout (downstream)"
        # task :downstream => 'rubyspec:update' do
        #   dir = ENV['DIR'] || spec_ruby
        # 
        #   rm_rf "spec/frozen"
        #   rsync dir + "/*", "spec/frozen"
        # 
        #   version = Dir.chdir(dir) { `git log --pretty=oneline -1`[0..7] }
        #   sh "git add spec/frozen/"
        #   sh "git commit -m 'Updated CI frozen specs to RubySpec #{version}.' spec/frozen"
        # end
      end

    end
  end
end