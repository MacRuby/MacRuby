# 0.12

MacRuby 0.12 was a milestone release...

 * Add support for XCode 4.3
 * Drop support for XCode 4.2 and earlier
 * Upgrade to RubyGems 1.8.x
 * Upgrade to Rake 0.9.x
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

![Business Cat Doin His Thang](http://i.imgur.com/2KmJW.jpg)
