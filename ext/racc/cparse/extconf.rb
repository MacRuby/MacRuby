# $Id: extconf.rb 12501 2007-06-10 03:06:15Z nobu $

require 'mkmf'
have_func('rb_block_call', 'ruby/ruby.h')
create_makefile 'racc/cparse'
