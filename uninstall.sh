#!/bin/bash

# MacRuby
rm -rf /Library/Frameworks/MacRuby.framework

XCODE_DIR=`xcode-select -print-path`
# tool
rm -f "$XCODE_DIR"/usr/bin/rb_nibtool
rm -f "$XCODE_DIR"/Tools/rb_nibtool

# bin
rm -f /usr/local/bin/macgem
rm -f /usr/local/bin/macirb
rm -f /usr/local/bin/macrake
rm -f /usr/local/bin/macrdoc
rm -f /usr/local/bin/macri
rm -f /usr/local/bin/macruby
rm -f /usr/local/bin/macruby_deploy
rm -f /usr/local/bin/macruby_select
rm -f /usr/local/bin/macrubyc
rm -f /usr/local/bin/macrubyd

# man
rm -f usr/local/share/man/man1/macirb.1
rm -f usr/local/share/man/man1/macruby.1
rm -f usr/local/share/man/man1/macruby_deploy.1
rm -f usr/local/share/man/man1/macrubyc.1
rm -f usr/local/share/man/man1/macrubyd.1

# Example
rm -rf ~/Documents/MacRubyExamples
rm -rf /Developer/Examples/Ruby/MacRuby

# Template
rm -rf /Library/Application\ Support/Developer/3.0/Xcode/File\ Templates/MacRuby
rm -rf /Library/Application\ Support/Developer/3.0/Xcode/Project\ Templates/Application/MacRuby\ Application
rm -rf /Library/Application\ Support/Developer/3.0/Xcode/Project\ Templates/Application/MacRuby\ Core\ Data\ Application
rm -rf /Library/Application\ Support/Developer/3.0/Xcode/Project\ Templates/Application/MacRuby\ Document-based\ Application
rm -rf /Library/Application\ Support/Developer/3.0/Xcode/Project\ Templates/Application/MacRuby\ Preference\ Pane
rm -rf /Library/Application\ Support/Developer/3.0/Xcode/Project\ Templates/System\ Plug-in/MacRuby\ Preference\ Pane

rm -rf /Library/Application\ Support/Developer/Shared/Xcode/File\ Templates/MacRuby
rm -rf /Library/Application\ Support/Developer/Shared/Xcode/Project\ Templates/Application/MacRuby\ Application
rm -rf /Library/Application\ Support/Developer/Shared/Xcode/Project\ Templates/Application/MacRuby\ Core\ Data\ Application
rm -rf /Library/Application\ Support/Developer/Shared/Xcode/Project\ Templates/Application/MacRuby\ Document-based\ Application
rm -rf /Library/Application\ Support/Developer/Shared/Xcode/Project\ Templates/Application/MacRuby\ Preference\ Pane
rm -rf /Library/Application\ Support/Developer/Shared/Xcode/Project\ Templates/System\ Plug-in/MacRuby\ Preference\ Pane

rm -rf /Developer/Library/Xcode/Templates/File\ Templates/Ruby/Ruby\ File.xctemplate
rm -rf /Developer/Library/Xcode/Templates/Project\ Templates/Base/MacRuby\ Application.xctemplate
rm -rf /Developer/Library/Xcode/Templates/Project\ Templates/Mac/Application/MacRuby\ Application.xctemplate
rm -rf /Developer/Library/Xcode/Templates/Project\ Templates/Mac/Application/MacRuby\ Core\ Data\ Application.xctemplate
rm -rf /Developer/Library/Xcode/Templates/Project\ Templates/Mac/Application/MacRuby\ Core\ Data\ Spotlight\ Application.xctemplate
rm -rf /Developer/Library/Xcode/Templates/Project\ Templates/Mac/Application/MacRuby\ Document-based\ Application.xctemplate
rm -rf /Developer/Library/Xcode/Templates/Project\ Templates/Mac/System\ Plug-in/MacRuby\ Preference\ Pane.xctemplate

rm -rf /Library/Developer/Xcode/Templates/Application/File\ Templates/Ruby/Ruby\ File.xctemplate
rm -rf /Library/Developer/Xcode/Templates/Application/Project\ Templates/Base/MacRuby\ Application.xctemplate
rm -rf /Library/Developer/Xcode/Templates/Application/Project\ Templates/Mac/Application/MacRuby\ Application.xctemplate
rm -rf /Library/Developer/Xcode/Templates/Application/Project\ Templates/Mac/Application/MacRuby\ Core\ Data\ Application.xctemplate
rm -rf /Library/Developer/Xcode/Templates/Application/Project\ Templates/Mac/Application/MacRuby\ Core\ Data\ Spotlight\ Application.xctemplate
rm -rf /Library/Developer/Xcode/Templates/Application/Project\ Templates/Mac/Application/MacRuby\ Document-based\ Application.xctemplate
rm -rf /Library/Developer/Xcode/Templates/Application/Project\ Templates/Mac/System\ Plug-in/MacRuby\ Preference\ Pane.xctemplate

rm -rf ~/Library/Developer/Xcode/Templates/Application/File\ Templates/Ruby/Ruby\ File.xctemplate
rm -rf ~/Library/Developer/Xcode/Templates/Application/Project\ Templates/Base/MacRuby\ Application.xctemplate
rm -rf ~/Library/Developer/Xcode/Templates/Application/Project\ Templates/Mac/Application/MacRuby\ Application.xctemplate
rm -rf ~/Library/Developer/Xcode/Templates/Application/Project\ Templates/Mac/Application/MacRuby\ Core\ Data\ Application.xctemplate
rm -rf ~/Library/Developer/Xcode/Templates/Application/Project\ Templates/Mac/Application/MacRuby\ Core\ Data\ Spotlight\ Application.xctemplate
rm -rf ~/Library/Developer/Xcode/Templates/Application/Project\ Templates/Mac/Application/MacRuby\ Document-based\ Application.xctemplate
rm -rf ~/Library/Developer/Xcode/Templates/Application/Project\ Templates/Mac/System\ Plug-in/MacRuby\ Preference\ Pane.xctemplate
