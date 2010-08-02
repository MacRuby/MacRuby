class SKTRenderingView < NSView

	attr_reader	:printJobTitle
	
	def self.pdfDataWithGraphics (graphics)
		# Create a view that will be used just for making PDF.
		bounds = SKTGraphic.drawingBoundsOfGraphics(graphics)
		view = SKTRenderingView.alloc.initWithFrame(bounds, graphics: graphics, printJobTitle: nil)
		pdfData = view.dataWithPDFInsideRect(bounds)
		return pdfData;
	end

	def self.tiffDataWithGraphics (graphics, error: outError)
		# How big a of a TIFF are we going to make? Regardless of what NSImage
		# supports, Sketch doesn't support the creation of TIFFs that are 0 by 0
		# pixels. (We have to demonstrate a custom saving error somewhere, and
		# this is an easy place to do it...)
		tiffData = nil;
		bounds = SKTGraphic.drawingBoundsOfGraphics(graphics)
		if !NSIsEmptyRect(bounds)
			# Create a new image and prepare to draw in it. Get the graphics context
			# for it after we lock focus, not before.
			image = NSImage.alloc.initWithSize(bounds.size)
			image.setFlipped(true)
			image.lockFocus
			currentContext = NSGraphicsContext.currentContext
	
			# We're not drawing a page image here, just the rectangle that contains
			# the graphics being drawn, so make sure they get drawn in the right
			# place.
			transform = NSAffineTransform.transform
			transform.translateXBy((0.0 - bounds.origin.x), yBy: (0.0 - bounds.origin.y))
			transform.concat
	
			# Draw the graphics back to front.
			(graphics.count - 1).downto(0) do |graphicIndex|
				graphic = graphics[graphicIndex]
				currentContext.saveGraphicsState
				NSBezierPath.clipRect(graphic.drawingBounds)
				graphic.drawContentsInView(nil, isBeingCreateOrEdited: false)
				currentContext.restoreGraphicsState
			end
	
			# We're done drawing.
			image.unlockFocus
			tiffData = image.TIFFRepresentation
	
		elsif outError
			# In Sketch there are lots of places to catch this situation earlier. For
			# example, we could have overridden -writableTypesForSaveOperation: and
			# made it not return NSTIFFPboardType, but then the user would have no
			# idea why TIFF isn't showing up in the save panel's File Format popup.
			# This way we can present a nice descriptive errror message.
			outError[0] = SKTErrorWithCode(SKTWriteCouldntMakeTIFFError)
		end
		return tiffData;
	end

	def initWithFrame (frame, graphics: graphics, printJobTitle: printJobTitle)
		super
		@graphics = graphics.copy
		@printJobTitle = printJobTitle.copy
		return self
	end

	# An override of the NSView method.
	def drawRect (rect)
		# Draw the background background.
		NSColor.whiteColor.set
		NSRectFill(rect)
	
		# Draw every graphic that intersects the rectangle to be drawn. In Sketch
		# the frontmost graphics have the lowest indexes.
		currentContext = NSGraphicsContext.currentContext
		(@graphics.count - 1).downto(0) do |index|
			graphic = @graphics[index]
			graphicDrawingBounds = graphic.drawingBounds
			if NSIntersectsRect(rect, graphicDrawingBounds)
				# Draw the graphic.
				currentContext.saveGraphicsState
				NSBezierPath.clipRect(graphicDrawingBounds)
				graphic.drawContentsInView(self, isBeingCreateOrEdited: false)
				currentContext.restoreGraphicsState
			end
		end
	end

	# An override of the NSView method.
	def isFlipped ()
		# Put (0, 0) at the top-left of the view.
		return true
	end

	# An override of the NSView method.
	def isOpaque ()
		# Our override of -drawRect: always draws a background.
		return true
	end
end

=begin

Derived from Apple's sample code for Sketch and converted to MacRuby by Dave Baldwin.

Additional comments from corresponding header file:

None.


---------------------------------------------------------------------------------------------
Apple's original notice:

/*
IMPORTANT:  This Apple software is supplied to you by Apple Computer, Inc. ("Apple") in
consideration of your agreement to the following terms, and your use, installation,
modification or redistribution of this Apple software constitutes acceptance of these
terms.  If you do not agree with these terms, please do not use, install, modify or
redistribute this Apple software.

In consideration of your agreement to abide by the following terms, and subject to these
terms, Apple grants you a personal, non-exclusive license, under Apple's copyrights in
this original Apple software (the "Apple Software"), to use, reproduce, modify and
redistribute the Apple Software, with or without modifications, in source and/or binary
forms; provided that if you redistribute the Apple Software in its entirety and without
modifications, you must retain this notice and the following text and disclaimers in all
such redistributions of the Apple Software.  Neither the name, trademarks, service marks
or logos of Apple Computer, Inc. may be used to endorse or promote products derived from
the Apple Software without specific prior written permission from Apple. Except as expressly
stated in this notice, no other rights or licenses, express or implied, are granted by Apple
herein, including but not limited to any patent rights that may be infringed by your
derivative works or by other works in which the Apple Software may be incorporated.

The Apple Software is provided by Apple on an "AS IS" basis.  APPLE MAKES NO WARRANTIES,
EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION THE IMPLIED WARRANTIES OF NON-INFRINGEMENT,
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, REGARDING THE APPLE SOFTWARE OR ITS
USE AND OPERATION ALONE OR IN COMBINATION WITH YOUR PRODUCTS.

IN NO EVENT SHALL APPLE BE LIABLE FOR ANY SPECIAL, INDIRECT, INCIDENTAL OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) ARISING IN ANY WAY OUT OF THE USE,
REPRODUCTION, MODIFICATION AND/OR DISTRIBUTION OF THE APPLE SOFTWARE, HOWEVER CAUSED AND
WHETHER UNDER THEORY OF CONTRACT, TORT (INCLUDING NEGLIGENCE), STRICT LIABILITY OR
OTHERWISE, EVEN IF APPLE HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

=end