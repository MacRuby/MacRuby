= Test::Unit 2.x

* http://rubyforge.org/projects/test-unit/

== DESCRIPTION

Test::Unit 2.x - Improved version of Test::Unit bundled in
Ruby 1.8.x.

Ruby 1.9.x bundles minitest not Test::Unit. Test::Unit
bundled in Ruby 1.8.x had not been improved but unbundled
Test::Unit (Test::Unit 2.x) will be improved actively.

== FEATURES

* Test::Unit 1.2.3 is the original Test::Unit, taken
  straight from the ruby distribution. It is being
  distributed as a gem to allow tool builders to use it as a
  stand-alone package. (The test framework in ruby is going
  to radically change very soon).

* Test::Unit 2.x will be improved actively and may break
  compatiblity with Test::Unit 1.2.3. (We will not hope it
  if it isn't needed.)

* Some features exist as separated gems like GUI test
  runner. (Tk, GTK+ and Fox) test-unit-full gem package
  provides for installing all Test::Unit related gems
  easily.

== INSTALL

  % sudo gem install test-unit

If you want to use full Test::Unit features:

  % sudo gem install test-unit-full

== LICENSE

(The Ruby License)

This software is distributed under the same terms as ruby.

== Thanks

* Daniel Berger: Suggestions and bug reports.
* Designing Patterns: Suggestions.
* Erik Hollensbe: Suggestions.
* Bill Lear: A suggestion.
* Diego Pettenò: A bug report.
* Angelo Lakra: A bug report.
