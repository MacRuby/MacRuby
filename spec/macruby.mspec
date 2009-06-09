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
    
    # Currently not working on MacRuby
    '^core/marshal',
    '^core/precision',
    '^core/proc',
    '^core/process',
    '^core/thread',
    '^core/threadgroup'
  ]
  
  # Prepend the paths with the proper prefix
  [:language, :core].each do |pseudo_dir|
    set(pseudo_dir, get(pseudo_dir).map do |path|
      if path[0,1] == '^'
        "^#{File.join(FROZEN_PREFIX, path[1..-1])}"
      else
        File.join(FROZEN_PREFIX, path)
      end
    end)
  end
  
  set :macruby, ['spec/macruby']
  
  set :full, get(:macruby) + get(:language) + get(:core)
  
  # The default implementation to run the specs.
  # TODO: this needs to be more sophisticated since the
  # executable is not consistently named.
  set :target, './miniruby'
  
  set :tags_patterns, [
                        [%r(language/), 'tags/macruby/language/'],
                        [%r(core/),     'tags/macruby/core/'],
                        [%r(library/),  'tags/macruby/library/'],
                        [/_spec.rb$/,   '_tags.txt']
                      ]
end
