require 'test/unit/color-scheme'
require 'optparse'

module Test
  module Unit
    class AutoRunner
      RUNNERS = {}
      COLLECTORS = {}
      ADDITIONAL_OPTIONS = []

      class << self
        def register_runner(id, runner_builder=Proc.new)
          RUNNERS[id] = runner_builder
          RUNNERS[id.to_s] = runner_builder
        end

        def runner(id)
          RUNNERS[id.to_s]
        end

        def register_collector(id, collector_builder=Proc.new)
          COLLECTORS[id] = collector_builder
          COLLECTORS[id.to_s] = collector_builder
        end

        def collector(id)
          COLLECTORS[id.to_s]
        end

        def register_color_scheme(id, scheme)
          ColorScheme[id] = scheme
        end

        def setup_option(option_builder=Proc.new)
          ADDITIONAL_OPTIONS << option_builder
        end
      end

      def self.run(force_standalone=false, default_dir=nil, argv=ARGV, &block)
        r = new(force_standalone || standalone?, &block)
        r.base = default_dir
        r.process_args(argv)
        r.run
      end
      
      def self.standalone?
        return false unless("-e" == $0)
        ObjectSpace.each_object(Class) do |klass|
          return false if(klass < TestCase)
        end
        true
      end

      register_collector(:descendant) do |auto_runner|
        require 'test/unit/collector/descendant'
        collector = Collector::Descendant.new
        collector.filter = auto_runner.filters
        collector.collect($0.sub(/\.rb\Z/, ''))
      end

      register_collector(:load) do |auto_runner|
        require 'test/unit/collector/load'
        collector = Collector::Load.new
        collector.patterns.concat(auto_runner.pattern) if auto_runner.pattern
        collector.excludes.concat(auto_runner.exclude) if auto_runner.exclude
        collector.base = auto_runner.base
        collector.filter = auto_runner.filters
        collector.collect(*auto_runner.to_run)
      end

      # deprecated
      register_collector(:object_space) do |auto_runner|
        require 'test/unit/collector/objectspace'
        c = Collector::ObjectSpace.new
        c.filter = auto_runner.filters
        c.collect($0.sub(/\.rb\Z/, ''))
      end

      # deprecated
      register_collector(:dir) do |auto_runner|
        require 'test/unit/collector/dir'
        c = Collector::Dir.new
        c.filter = auto_runner.filters
        c.pattern.concat(auto_runner.pattern) if auto_runner.pattern
        c.exclude.concat(auto_runner.exclude) if auto_runner.exclude
        c.base = auto_runner.base
        $:.push(auto_runner.base) if auto_runner.base
        c.collect(*(auto_runner.to_run.empty? ? ['.'] : auto_runner.to_run))
      end

      attr_reader :suite, :runner_options
      attr_accessor :filters, :to_run, :pattern, :exclude, :base, :workdir
      attr_accessor :color_scheme
      attr_writer :runner, :collector

      def initialize(standalone)
        Unit.run = true
        @standalone = standalone
        @runner = default_runner
        @collector = default_collector
        @filters = []
        @to_run = []
        @color_scheme = ColorScheme.default
        @runner_options = {}
        @default_arguments = []
        @workdir = nil
        config_file = "test-unit.yml"
        if File.exist?(config_file)
          load_config(config_file)
        else
          global_config_file = File.expand_path("~/.test-unit.xml")
          load_config(global_config_file) if File.exist?(global_config_file)
        end
        yield(self) if block_given?
      end

      def process_args(args = ARGV)
        default_arguments = @default_arguments.dup
        begin
          @default_arguments.concat(args)
          options.order!(@default_arguments) {|arg| @to_run << arg}
        rescue OptionParser::ParseError => e
          puts e
          puts options
          exit(false)
        else
          @filters << proc{false} unless(@filters.empty?)
        end
        not @to_run.empty?
      ensure
        @default_arguments = default_arguments
      end

      def options
        @options ||= OptionParser.new do |o|
          o.banner = "Test::Unit automatic runner."
          o.banner << "\nUsage: #{$0} [options] [-- untouched arguments]"

          o.on
          o.on('-r', '--runner=RUNNER', RUNNERS,
               "Use the given RUNNER.",
               "(" + keyword_display(RUNNERS) + ")") do |r|
            @runner = r
          end

          if (@standalone)
            o.on('-b', '--basedir=DIR', "Base directory of test suites.") do |b|
              @base = b
            end

            o.on('-w', '--workdir=DIR', "Working directory to run tests.") do |w|
              @workdir = w
            end

            o.on('-a', '--add=TORUN', Array,
                 "Add TORUN to the list of things to run;",
                 "can be a file or a directory.") do |a|
              @to_run.concat(a)
            end

            @pattern = []
            o.on('-p', '--pattern=PATTERN', Regexp,
                 "Match files to collect against PATTERN.") do |e|
              @pattern << e
            end

            @exclude = []
            o.on('-x', '--exclude=PATTERN', Regexp,
                 "Ignore files to collect against PATTERN.") do |e|
              @exclude << e
            end
          end

          o.on('-n', '--name=NAME', String,
               "Runs tests matching NAME.",
               "(patterns may be used).") do |n|
            n = (%r{\A/(.*)/\Z} =~ n ? Regexp.new($1) : n)
            case n
            when Regexp
              @filters << proc{|t| n =~ t.method_name ? true : nil}
            else
              @filters << proc{|t| n == t.method_name ? true : nil}
            end
          end

          o.on('-t', '--testcase=TESTCASE', String,
               "Runs tests in TestCases matching TESTCASE.",
               "(patterns may be used).") do |n|
            n = (%r{\A/(.*)/\Z} =~ n ? Regexp.new($1) : n)
            case n
            when Regexp
              @filters << proc{|t| n =~ t.class.name ? true : nil}
            else
              @filters << proc{|t| n == t.class.name ? true : nil}
            end
          end

          priority_filter = Proc.new do |test|
            if @filters.size > 2
              nil
            else
              Priority::Checker.new(test).need_to_run? or nil
            end
          end
          o.on("--[no-]priority-mode",
               "Runs some tests based on their priority.") do |priority_mode|
            if priority_mode
              Priority.enable
              @filters |= [priority_filter]
            else
              Priority.disable
              @filters -= [priority_filter]
            end
          end

          o.on("--default-priority=PRIORITY",
               Priority.available_values,
               "Uses PRIORITY as default priority",
               "(#{keyword_display(Priority.available_values)})") do |priority|
            Priority.default = priority
          end

          o.on('-I', "--load-path=DIR[#{File::PATH_SEPARATOR}DIR...]",
               "Appends directory list to $LOAD_PATH.") do |dirs|
            $LOAD_PATH.concat(dirs.split(File::PATH_SEPARATOR))
          end

          color_schemes = ColorScheme.all
          o.on("--color-scheme=SCHEME", color_schemes,
               "Use SCHEME as color scheme.",
               "(#{keyword_display(color_schemes)})") do |scheme|
            @color_scheme = scheme
          end

          o.on("--config=FILE",
               "Use YAML fomat FILE content as configuration file.") do |file|
            load_config(file)
          end

          o.on("--order=ORDER", TestCase::AVAILABLE_ORDERS,
               "Run tests in a test case in ORDER order.",
               "(#{keyword_display(TestCase::AVAILABLE_ORDERS)})") do |order|
            TestCase.test_order = order
          end

          ADDITIONAL_OPTIONS.each do |option_builder|
            option_builder.call(self, o)
          end

          o.on('--',
               "Stop processing options so that the",
               "remaining options will be passed to the",
               "test."){o.terminate}

          o.on('-h', '--help', 'Display this help.'){puts o; exit}

          o.on_tail
          o.on_tail('Deprecated options:')

          o.on_tail('--console', 'Console runner (use --runner).') do
            warn("Deprecated option (--console).")
            @runner = self.class.runner(:console)
          end

          if RUNNERS[:fox]
            o.on_tail('--fox', 'Fox runner (use --runner).') do
              warn("Deprecated option (--fox).")
              @runner = self.class.runner(:fox)
            end
          end

          o.on_tail
        end
      end

      def keyword_display(keywords)
        keywords = keywords.collect do |keyword, _|
          keyword.to_s
        end.uniq.sort

        i = 0
        keywords.collect do |keyword|
          if (i > 0 and keyword[0] == keywords[i - 1][0]) or
              ((i < keywords.size - 1) and (keyword[0] == keywords[i + 1][0]))
            n = 2
          else
            n = 1
          end
          i += 1
          keyword.sub(/^(.{#{n}})([A-Za-z]+)(?=\w*$)/, '\\1[\\2]')
        end.join(", ")
      end

      def run
        suite = @collector[self]
        return false if suite.nil?
        runner = @runner[self]
        return false if runner.nil?
        @runner_options[:color_scheme] ||= @color_scheme
        Dir.chdir(@workdir) if @workdir
        runner.run(suite, @runner_options).passed?
      end

      def load_config(file)
        require 'yaml'
        config = YAML.load(File.read(file))
        runner_name = config["runner"]
        @runner = self.class.runner(runner_name) || @runner
        @collector = self.class.collector(config["collector"]) || @collector
        (config["color_schemes"] || {}).each do |name, options|
          ColorScheme[name] = options
        end
        runner_options = {}
        (config["#{runner_name}_options"] || {}).each do |key, value|
          key = key.to_sym
          value = ColorScheme[value] if key == :color_scheme
          if key == :arguments
            @default_arguments.concat(value.split)
          else
            runner_options[key.to_sym] = value
          end
        end
        @runner_options = @runner_options.merge(runner_options)
      end

      private
      def default_runner
        if ENV["EMACS"] == "t"
          self.class.runner(:emacs)
        else
          self.class.runner(:console)
        end
      end

      def default_collector
        self.class.collector(@standalone ? :load : :descendant)
      end
    end
  end
end

require 'test/unit/runner/console'
require 'test/unit/runner/emacs'
require 'test/unit/runner/tap'
