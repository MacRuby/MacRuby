class MSpecScript
  # TODO: This doesn't work correctly yet.
  # load File.expand_path('../frozen/ruby.1.9.mspec', __FILE__)
  
  FROZEN_PREFIX = 'spec/frozen'
  load File.join(FROZEN_PREFIX, 'ruby.1.9.mspec')
  
  # Core library specs
  set :core, [
    'core',
    
    # obsolete in 1.9
    '^core/continuation',
    '^core/kernel/callcc_spec.rb',
    
    # Currently not working on MacRuby
    '^core/encoding',
    '^core/marshal',
    '^core/numeric/to_c_spec.rb',
    '^core/precision',
    '^core/proc',
  ]

  # Library specs
  set :library, [
    'library/date',
    'library/digest',
    'library/getoptlong',
    'library/mutex',
    'library/queue',
    'library/observer',
    'library/pathname',
    'library/readline',
    'library/scanf',
    'library/stringscanner',
    'library/time',
    'library/tmpdir'
	'library/yaml/load_spec.rb',
	'library/yaml/dump_spec.rb'
  ]
  
  # Prepend the paths with the proper prefix
  [:language, :core, :library].each do |pseudo_dir|
    set(pseudo_dir, get(pseudo_dir).map do |path|
      if path[0,1] == '^'
        "^#{File.join(FROZEN_PREFIX, path[1..-1])}"
      else
        File.join(FROZEN_PREFIX, path)
      end
    end)
  end
  
  set :macruby, ['spec/macruby']
  
  set :full, get(:macruby) + get(:language) + get(:core) + get(:library)
  
  # Optional library specs
  set :ffi, File.join(FROZEN_PREFIX, 'optional/ffi')
  
  # A list of _all_ optional library specs
  set :optional, [get(:ffi)]
  
  # The default implementation to run the specs.
  set :target, File.expand_path('../../macruby', __FILE__)
  
  set :tags_patterns, [
                        [%r(language/), 'tags/macruby/language/'],
                        [%r(core/),     'tags/macruby/core/'],
                        [%r(library/),  'tags/macruby/library/'],
                        [/_spec.rb$/,   '_tags.txt']
                      ]
end
