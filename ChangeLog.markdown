# 0.13
 * Move the MacRuby Samples to https://github.com/MacRuby/MacRubySamples
 * MacRuby Xcode integration can now be re-installed (GH-90)

```bash
$ sudo /usr/local/bin/macruby_install_xcode_support
```

  * Upgrade RubyGems to version 1.8.24
  * Upgrade json to version 1.7.5

# 0.12

MacRuby 0.12 was a milestone release...

 * Changed `RUBY_AUTHOR` to "The MacRuby Team"
 * Add support for XCode 4.3
 * Drop support for XCode 4.2 and earlier
 * Upgrade to RubyGems 1.8.20
 * Upgrade to Rake 0.9.2.2
 * Add the `--codesign` option to `macruby_deploy`
 * Gems are now installed to `/Library/Ruby/Gems/MacRuby`
 * Remove obsoleted constants `RUBY_FRAMEWORK` and `RUBY_FRAMEWORK_VERSION` from `RbConfig`
 * macrubyc/macruby\_deploy now uses proper exit codes for `--help` and `--version` options
 * The `instruby.rb` script has been replaced with a set of rake tasks
 * Upgrade JSON to 1.6.5
 * Cocoa objects now use `#description` for their `#inspect` output

```ruby
    p NSURL.URLWithString("http://macruby.org/").inspect
```

 * Added `Pointer#value` shortcut to `Pointer#[0]`
 * Improve stability and compatibility by over total 630 commits

# ~~0.11~~
 * Update to new ruby license
 * Improve some methods of String/Array performance
 * Add `Range#relative_to(max)`
 * Add `Pointer#to_object`
 * Import JSON 1.5.1
 * Upgrade RDoc to 3.5.3
 * Improve stability and compatibility by total 515 commits

![Business Cat Doin His Thang](http://i.imgur.com/2KmJW.jpg)
