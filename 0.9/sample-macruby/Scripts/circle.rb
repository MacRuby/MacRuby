# Create a new PDF file and draw a red circle in it, using Core Graphics.
framework 'Cocoa'

url = NSURL.fileURLWithPath('circle.pdf')
prect = Pointer.new(CGRect.type)
prect[0] = [[0, 0], [617, 792]]
pdf = CGPDFContextCreateWithURL(url, prect, nil)

CGPDFContextBeginPage(pdf, nil)
CGContextSetRGBFillColor(pdf, 1.0, 0.0, 0.0, 1.0)
CGContextAddArc(pdf, 300, 300, 100, 0, 2 * Math::PI, 1)
CGContextFillPath(pdf)
CGPDFContextEndPage(pdf)
CGContextFlush(pdf)

# CGContextRef is a resourceful object, so it must be released manually.
CFRelease(pdf)
