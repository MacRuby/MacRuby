class MSpecScript
  # TODO: This doesn't work correctly yet.
  # load File.expand_path('../frozen/ruby.1.9.mspec', __FILE__)

  FROZEN_PREFIX = 'spec/frozen'
  load File.join(FROZEN_PREFIX, 'ruby.1.9.mspec')

  # Command-line specs
  set :command_line, ['command_line']

  # Core library specs
  set :core, [
    'core',

    # obsolete in 1.9
    '^core/continuation',
    '^core/kernel/callcc_spec.rb',

    # Currently not working on MacRuby
    '^core/io'
  ]

  # Library specs
  set :library, [
    'library',

    # Obsolete in 1.9
    '^library/fiber', # now part of core
    '^library/syslog',

    # Not supported in MacRuby
    '^library/continuation',

    # Tons of IO issues
    '^library/net/ftp', # exists the specs when running using rake spec:library and reaching net/ftp/chdir_spec.rb
    # Currently not working on MacRuby
    '^library/cgi/htmlextension', # runs fine when run separately, it seems another spec brings IO in a wrong state
    '^library/erb/new_spec.rb', # Loading issues
    '^library/prime/each_spec.rb',  # hangs because of timeout, but if the spec is tagged, crashes at the end, even when tagging everything
  ]

  # Prepend the paths with the proper prefix
  [:command_line, :language, :core, :library].each do |pseudo_dir|
    set(pseudo_dir, get(pseudo_dir).map do |path|
      if path[0,1] == '^'
        "^#{File.join(FROZEN_PREFIX, path[1..-1])}"
      else
        File.join(FROZEN_PREFIX, path)
      end
    end)
  end

  set :macruby, ['spec/macruby']
  set :irb, ['spec/dietrb']
  set :rubyspec, get(:command_line) + get(:language) + get(:core) + get(:library)
  set :full, get(:macruby) + get(:rubyspec)

  # Optional library specs
  set :ffi, File.join(FROZEN_PREFIX, 'optional/ffi')

  # A list of _all_ optional library specs
  set :optional, [get(:ffi)]

  # Don't run with macruby if this env var is set
  unless ENV['RUN_WITH_MRI_19']
    # Make the macruby binary look for the framework in the source root
    source_root = File.expand_path('../../', __FILE__)
    ENV['DYLD_LIBRARY_PATH'] = source_root
    # Setup the proper load paths for lib and extensions
    load_paths = %w{ -I. -I./lib -I./ext }
    load_paths << '-I./ext/ripper/lib' # ripper specific load path fix
    load_paths << '-I./ext/bigdecimal/lib' # bigdecimal specific load path fix
    load_paths.concat Dir.glob('./ext/**/*.bundle').map { |filename| "-I#{File.dirname(filename)}" }.uniq
    load_paths.concat(get(:flags)) if get(:flags)
    set :flags, load_paths

    # The default implementation to run the specs.
    set :target, File.join(source_root, 'macruby')
  end

  set :tags_patterns, [
                        [%r(language/), 'tags/macruby/language/'],
                        [%r(core/),     'tags/macruby/core/'],
                        [%r(library/),  'tags/macruby/library/'],
                        [%r(dietrb/),   'dietrb/tags/'],
                        [/_spec.rb$/,   '_tags.txt']
                      ]
end
