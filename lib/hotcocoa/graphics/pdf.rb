# Ruby Cocoa Graphics is a graphics library providing a simple object-oriented 
# interface into the power of Mac OS X's Core Graphics and Core Image drawing libraries.  
# With a few lines of easy-to-read code, you can write scripts to draw simple or complex 
# shapes, lines, and patterns, process and filter images, create abstract art or visualize 
# scientific data, and much more.
# 
# Inspiration for this project was derived from Processing and NodeBox.  These excellent 
# graphics programming environments are more full-featured than RCG, but they are implemented 
# in Java and Python, respectively.  RCG was created to offer similar functionality using 
# the Ruby programming language.
#
# Author::    James Reynolds  (mailto:drtoast@drtoast.com)
# Copyright:: Copyright (c) 2008 James Reynolds
# License::   Distributes under the same terms as Ruby

module HotCocoa::Graphics
  
  # parse a PDF file to determine pages, width, height
  class Pdf
  
    attr_reader :pages, :pdf
  
    # create a new Pdf object given the original pathname, and password if needed
    def initialize(original, password = nil)
      # http://developer.apple.com/documentation/GraphicsImaging/Reference/CGPDFDocument/Reference/reference.html
      # http://developer.apple.com/documentation/GraphicsImaging/Reference/CGPDFPage/Reference/reference.html
      @pdf = CGPDFDocumentCreateWithURL(NSURL.fileURLWithPath(original)) # => CGPDFDocumentRef
      result = CGPDFDocumentUnlockWithPassword(@pdf, password) if password # unlock if necessary
      @pages = CGPDFDocumentGetNumberOfPages(@pdf) # => 4
      puts "pdf.new #{original} (#{@pages} pages)" if @verbose
      self
    end
  
    # print drawing functions to console if verbose is true
    def verbose(tf)
      @verbose = tf
    end
  
    # get the width of the specified pagenum
    def width(pagenum=1)
      cgpdfpage = page(pagenum)
      mediabox = CGPDFPageGetBoxRect(cgpdfpage, KCGPDFMediaBox) # => CGRect
      width = mediabox.size.width # CGRectGetWidth(mediabox)
      width
    end
  
    # get the height of the specified pagenum
    def height(pagenum=1)
      cgpdfpage = page(pagenum)
      mediabox = CGPDFPageGetBoxRect(cgpdfpage, KCGPDFMediaBox) # => CGRect
      height = mediabox.size.height # CGRectGetHeight(mediabox)
      height
    end
  
    # draw pagenum of the pdf document into a rectangle at x,y with dimensions w,h of drawing context ctx 
    def draw(ctx, x=0, y=0, w=width(pagenum), h=height(pagenum), pagenum=1)
      rect = CGRectMake(x,y,w,h)
      puts "pdf.draw page #{pagenum} at [#{x},#{y}] with #{w}x#{h}" if @verbose
      CGContextDrawPDFDocument(ctx, rect, @pdf, pagenum)
      true
    end
  
    private
  
    # return a CGPDFPageRef for this pagenum
    def page(pagenum)
      CGPDFDocumentGetPage(@pdf, pagenum) # => CGPDFPageRef
    end
  
  end
end