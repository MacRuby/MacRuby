# 
# dispatch.rb - Grand Central Dispatch support library
# 
# Copyright (C) 2010  Apple, Inc.
# 
# == Overview ==
#
# Grand Central Dispatch (GCD) is a novel approach to multicore computing
# first released in Mac OS X version 10.6 Snow Leopard.
# The Dispatch module and associated classes (Queue, Group, Semaphore, Source)
# in MacRuby core provides a simple wrapping on top of the libdispatch C API.
# This library provides higher-level services and convenience methods
# to make it easier for traditional Ruby programmers to add multicore support.

require 'dispatch/source'
require 'dispatch/queue'
require 'dispatch/proxy'
require 'dispatch/job'
require 'dispatch/enumerable'
