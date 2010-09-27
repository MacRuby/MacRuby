#!/usr/local/bin/macruby
# -*- coding: utf-8 -*-
# This script is sample of WEBrick and Bonjour.
# You could look for this server using Bonjour on Safari.
framework "Foundation"
require "webrick"

DOC_ROOT    = "."
SERVER_PORT = 8000
SERVER_NAME = "webrick"

s = WEBrick::HTTPServer.new(
  :Port => SERVER_PORT,
  :DocumentRoot => File.join(Dir::pwd, DOC_ROOT)
)
trap("INT") { s.shutdown }

# Bonjour
netservice = NSNetService.alloc.initWithDomain("", type:"_http._tcp", name:SERVER_NAME, port:SERVER_PORT)
netservice.publish()

s.start
