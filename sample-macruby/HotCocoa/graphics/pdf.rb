#!/usr/bin/env macruby

require 'hotcocoa/graphics'
require 'test/unit'
include HotCocoa::Graphics


class TestPdf < Test::Unit::TestCase
  
  # def test_pdf
  #   pdf_to_png('/Volumes/catalog/inprocess/GV/lookinsides/GV.1505.pdf','/Volumes/catalog/inprocess/GV/converted')
   # end
  #Mon Jul 28 19:53:55 toastmacbook-3.local macruby[16693] <Error>: CGBitmapContextCreateImage: failed to allocate 144365952 bytes.
  #Mon Jul 28 19:53:55 toastmacbook-3.local macruby[16693] <Error>: CGImageCreate: invalid image provider: NULL.
  
  def test_parse_dir
    scale = 3.0
    sourcedir = '/Volumes/catalog/inprocess/GV/lookinsides'
    destdir = '/Volumes/catalog/inprocess/GV/converted'
    for file in Dir.entries(sourcedir)
      next unless File.extname(file).downcase == '.pdf'
      pdf_to_png(File.join(sourcedir,file),destdir,scale)
    end
  end
  
end

def pdf_to_png(file,destdir,scale=3.0)
  puts file
  newfilename = File.basename(file, File.extname(file))
  newfilename, pagenum = newfilename.split('_')
  pagenum ||= 0
  pagenum = sprintf("%02d", pagenum)
  pdf = Pdf.new(file)
  pages = pdf.pages
  w = pdf.width * scale
  h = pdf.height * scale
  for p in 1..pages do
    pdisplay = sprintf("%02d", p)
    canvas = Canvas.for_image(:size => [400,400], :filename => "#{destdir}/#{newfilename}_#{pagenum}-#{pdisplay}.png")
    canvas.background(Color.white)
    canvas.draw(pdf,0,0,w,h,p)
    canvas.save
    canvas = nil
    GC.start
  end

end