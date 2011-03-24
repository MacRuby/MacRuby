#!/usr/bin/env ruby
#
# pdf2tiff.rb
#
# Convert a PDF document into a series of TIFF images
#
# Created by Ernest Prabhakar. Copyright 2007 Apple, Inc. All Rights Reserved
# Ported to MacRuby by Laurent Sansonetti.
#

framework 'Quartz'

unless ARGV.size >= 1
  $stderr.puts "Usage: #{__FILE__} [file1.pdf] [file2.pdf] ..." 
  exit 1
end

NSApplication.sharedApplication

ARGV.each do |path|
  url = NSURL.fileURLWithPath path
  file = path.split("/")[-1]
  root = file.split(".")[0]

  pdfdoc = PDFDocument.alloc.initWithURL url

  pdfdoc.pageCount.times do |i|
    page = pdfdoc.pageAtIndex i
    pdfdata = page.dataRepresentation
    image = NSImage.alloc.initWithData pdfdata
    tiffdata = image.TIFFRepresentation
    outfile = "#{root}_#{i}.tiff"
    puts "Writing #{page.description} to #{outfile} for #{path}"
    tiffdata.writeToFile outfile, atomically:false
  end
end
