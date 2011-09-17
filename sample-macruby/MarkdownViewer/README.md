MarkdownViewer
==============

## Requirement

- Rubygems of [rdiscount](https://github.com/rtomayko/rdiscount)

## Modify rdiscount for MacRuby

When you installed rdiscount with "`sudo macgem install rdiscount`", need slight changing.

    $ sudo vi /Library/Frameworks/MacRuby.framework/Versions/Current/usr/lib/ruby/Gems/1.9.2/gems/rdiscount-1.6.8/lib/rdiscount.rb 

Please change the following line to the end of the rdiscount.rb

	require 'rdiscount.bundle' # original "require 'rdiscount.so'"
