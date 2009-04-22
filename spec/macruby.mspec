# TODO: This doesn't work correctly yet.
# load File.expand_path('../frozen/ruby.1.9.mspec', __FILE__)
load "spec/frozen/ruby.1.9.mspec"

class MSpecScript
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