namespace :test do
  
  desc "Runs all the test suites of the libs in test/libs (use the `ruby=' option to specify which Ruby to use)."
  task :libs => 'test:libs:all'
  
  namespace :libs do
    def requires(requires)
      requires.map { |f| '-r ' << f }.join(' ')
    end
    
    TEST_LIBS_ROOT = File.expand_path('../../test/libs', __FILE__)
    
    TEST_LIBS = {
      'rubygems' => [
        "http://rubygems.rubyforge.org/svn/trunk",
        "-I ../minitest/lib -I lib -r rubygems #{requires(Dir.glob("#{TEST_LIBS_ROOT}/rubygems/test/test_*.rb"))} ../minitest/lib/minitest/autorun.rb"
      ],
      'minitest' => [
        "git://github.com/seattlerb/minitest.git",
        "-I lib #{requires(%w{ ./test/test_mini_test.rb ./test/test_mini_spec.rb ./test/test_mini_mock.rb })} -e ''"
      ],
      'test-unit' => [
        "http://test-unit.rubyforge.org/svn/trunk",
        "-I lib test/run-test.rb"
      ],
      'bacon' => [
        "git://github.com/chneukirchen/bacon.git",
        "./bin/bacon -I lib --quiet ./test/*"
      ],
      
      # TODO: Don't actually work on 1.9, need to figure out with the mocha guy how to run these tests.
      # 'test-unit-1.2.3' => [
      #   "http://test-unit.rubyforge.org/svn/tags/1.2.3",
      #   "-I lib #{requires(Dir.glob("#{TEST_LIBS_ROOT}/test-unit-1.2.3/test/**/test_*.rb"))} -e ''"
      # ],
      # 'mocha' => [
      #   "git://github.com/floehopper/mocha.git",
      #   "-I ../test-unit-1.2.3/lib -I lib #{requires(Dir.glob("#{TEST_LIBS_ROOT}/mocha/test/**/*_test.rb"))} -e ''"
      # ]
    }
    
    desc "Exports the latest versions of all libs to test"
    task :export do
      rm_rf TEST_LIBS_ROOT
      mkdir_p TEST_LIBS_ROOT
      
      Dir.chdir(TEST_LIBS_ROOT) do
        TEST_LIBS.each do |name, (url, _)|
          if url =~ /^git:/
            sh "git clone #{url} #{name} && rm -rf #{name}/.git"
          else
            sh "svn export #{url} #{name}"
          end
        end
      end
    end
    
    TEST_LIBS.each do |name, (_, cmd)|
      desc "Runs the tests of the `#{name}' lib"
      task name do
        ruby = ENV['ruby'] || 'macruby'
        sh "cd #{File.join(TEST_LIBS_ROOT, name)} && #{ruby} #{cmd}"
      end
    end
    
    task :all do
      failures = []
      TEST_LIBS.keys.sort.each do |name|
        begin
          puts "", "### Running tests of `#{name}'", ""
          Rake::Task["test:libs:#{name}"].invoke
        rescue
          failures << name
        end
      end
      puts "", failures.empty? ? "No failures" : "Failures in: #{failures.join(', ')}", ""
    end
  end
end