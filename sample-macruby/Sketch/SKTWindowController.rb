
class SKTWindowController < NSWindowController
	attr_accessor :graphicsController, :graphicView, :zoomingScrollView, :grid, :zoomFactor

	def init ()
		# Do the regular Cocoa thing, specifying a particular nib.
		super.initWithWindowNibName ("DrawWindow")

		# Create a grid for use by graphic views whose "grid" property is bound to
		# this object's "grid" property.
		@grid = SKTGrid.alloc.init

		# Set the zoom factor to a reasonable default (100%).
		@zoomFactor = 1.0
		
		self
	end

	# Observing
	def canvasSizeDidChange (keyPath, observedObject, change)
		# The "new value" in the change dictionary will be NSNull, instead of
		# just not existing, if the value for some key in the key path is nil.
		# In this case there are times in an NSWindowController's life cycle
		# when its document is nil. Don't update the graphic view's size when we
		# get notifications about that.
		documentCanvasSizeValue = change[NSKeyValueChangeNewKey]
		observeDocumentCanvasSize (documentCanvasSize) if documentCanvasSizeValue
	end
	
	def observeDocumentCanvasSize (documentCanvasSize)
		# The document's canvas size changed. Invoking -setNeedsDisplay: twice
		# like this makes sure everything gets redrawn if the view gets smaller
		# in one direction or the other.
		@graphicView.setNeedsDisplay(true)
		@graphicView.frameSize = documentCanvasSize
		@graphicView.setNeedsDisplay(true)
	end

	def selectedToolDidChange (notification)
		# Just set the correct cursor
		theClass = SKTToolPaletteController.sharedToolPaletteController.currentGraphicClass
		theCursor = theClass.creationCursor if theClass
		theCursor ||= NSCursor.arrowCursor
		graphicView.enclosingScrollView.setDocumentCursor(theCursor)
	end

	# Overrides of NSWindowController Methods

	def setDocument (document)
		
		# Cocoa Bindings makes many things easier. Unfortunately, one of the
		# things it makes easier is creation of reference counting cycles. In
		# Mac OS 10.4 and later NSWindowController has a feature that keeps
		# bindings to File's Owner, when File's Owner is a window controller,
		# from retaining the window controller in a way that would prevent its
		# deallocation. We're setting up bindings programmatically in
		# -windowDidLoad though, so that feature doesn't kick in, and we have to
		# explicitly unbind to make sure this window controller and everything
		# in the nib it owns get deallocated. We do this here instead of in an
		# override of -[NSWindowController close] because window controllers
		# aren't sent -close messages for every kind of window closing.
		# Fortunately, window controllers are sent -setDocument:nil messages
		# during window closing.
		if !document
			@zoomingScrollView.unbind(SKTZoomingScrollViewFactor)
			@graphicView.unbind(SKTGraphicViewGridBindingName)
			@graphicView.unbind(SKTGraphicViewGraphicsBindingName)
		end

		# Redo the observing of the document's canvas size when the document
		# changes. You would think we would just be able to observe self's
		# "document.canvasSize" in -windowDidLoad or maybe even -init, but KVO
		# wasn't really designed with observing of self in mind so things get a
		# little squirrelly.
	    document.removeObserver(@canvasSizeObserver, forKeyPath: SKTDocumentCanvasSizeKey) if document && @canvasSizeObserver
		super

		@canvasSizeObserver ||= SKTObserver.new(self, :canvasSizeDidChange)

		if document
			document.addObserver(@canvasSizeObserver, forKeyPath: SKTDocumentCanvasSizeKey, 
									options: NSKeyValueObservingOptionNew,
									context: nil)
		end
	end

	def windowDidLoad ()
		super

		# Set up the graphic view and its enclosing scroll view.
		enclosingScrollView = @graphicView.enclosingScrollView
		enclosingScrollView.hasHorizontalRuler = true
		enclosingScrollView.hasVerticalRuler = true

		# We're already observing the document's canvas size in case it changes,
		# but we haven't been able to size the graphic view to match until now.
		observeDocumentCanvasSize(document.canvasSize)

		# Bind the graphic view's selection indexes to the controller's
		# selection indexes. The graphics controller's content array is bound to
		# the document's graphics in the nib, so it knows when graphics are
		# added and remove, so it can keep the selection indexes consistent.
		@graphicView.bind(SKTGraphicViewSelectionIndexesBindingName, toObject: @graphicsController,  
								withKeyPath: "selectionIndexes", options: nil)

		# Bind the graphic view's graphics to the document's graphics. We do
		# this instead of binding to the graphics controller because
		# NSArrayController is not KVC-compliant enough for "arrangedObjects" to
		# work properly when the graphic view sends its bound-to object a
		# -mutableArrayValueForKeyPath: message. The binding to self's
		# "document.graphics" is 1) easy and 2) appropriate for a window
		# controller that may someday be able to show one of several documents
		# in its window. If we instead bound the graphic view to [self document]
		# then we would have to redo the binding in -setDocument:.
		@graphicView.bind (SKTGraphicViewGraphicsBindingName, toObject: self, 
								withKeyPath: "document.#{SKTDocumentGraphicsKey}", options: nil)

		# Bind the graphic view's grid to this window controller's grid.
		@graphicView.bind(SKTGraphicViewGridBindingName, toObject: self, withKeyPath: "grid",  options: nil)

		# Bind the zooming scroll view's factor to this window's controller's
		# zoom factor.
		zoomingScrollView.bind(SKTZoomingScrollViewFactor, toObject: self, withKeyPath: "zoomFactor", options: nil)

		# Start observing the tool palette.
		selectedToolDidChange(nil)
		NSNotificationCenter.defaultCenter.addObserver(self, selector: 'selectedToolDidChange:', 
						name: SKTSelectedToolDidChangeNotification, object: SKTToolPaletteController.sharedToolPaletteController)
	end


		# *** Actions ***

 	# Conformance to the NSObject(NSMenuValidation) informal protocol.
	def validateMenuItem (menuItem)
		# Which menu item?
		action = menuItem.action
		if action == :'newDocumentWindow:'
			# Give the menu item that creates new sibling windows for this
			# document a reasonably descriptive title. It's important to use the
			# document's "display name" in places like this; it takes things like
			# file name extension hiding into account. We could do a better job
			# with the punctuation!
			menuItem.title = NSLocalizedStringFromTable("New window for '#{document.displayName}'", "MenuItems", "Formatter string for the new document window menu item. Argument is a the display name of the document.")
			enabled = true
		elsif action == :'toggleGridConstraining:' || action == :'toggleGridShowing:'
			# The Snap to Grid and Show Grid menu items are toggles.
			menuItemIsOn = action == :'toggleGridConstraining:' ? @grid.isConstraining : @grid.isAlwaysShown
			menuItem.state = menuItemIsOn ? NSOnState : NSOffState
			# The grid can be in an unusable state, in which case the menu items that control it are disabled.
			@grid.isUsable
		else
			super
		end
	end

	def newDocumentWindow (sender)
		# Do the same thing that a typical override of -[NSDocument
		# makeWindowControllers] would do, but then also show the window. This is
		# here instead of in SKTDocument, though it would work there too, with one
		# small alteration, because it's really view-layer code.
		windowController = SKTWindowController.alloc.init
		document.addWindowController(windowController)
		windowController.showWindow(self)
	end

	def toggleGridConstraining (sender)
		@grid.setConstraining(!@grid.isConstraining)
	end

	def toggleGridShowing (sender)
		@grid.setAlwaysShown(!@grid.isAlwaysShown)
	end
end

=begin

Derived from Apple's sample code for Sketch and converted to MacRuby by Dave Baldwin.

Additional comments from corresponding header file:

This class is KVC and KVO compliant for this key:

"graphicsController" (an NSArrayController; read-only) - The controller that manages the selection for the graphic view in the controlled window.

"grid" (an SKTGrid; read-only) - An instance of SKTGrid.

"zoomFactor" (a floating point NSNumber; read-write) - The zoom factor for the graphic view, following the meaning established by SKTZoomingScrollView's bindable "factor" property.

In Sketch:

Each SKTGraphicView's graphics and selection indexes properties are bound to the arranged objects and selection indexes properties of the containing SKTWindowController's graphics controller.

Each SKTGraphicView's grid property is bound to the grid property of the SKTWindowController that contains it.

Each SKTZoomingScrollView's factor property is bound to the zoom factor property of the SKTWindowController that contains it.

Various properties of the controls of the graphics inspector are bound to properties of the selection of the graphics controller of the main window's SKTWindowController.

Various properties of the controls of the grid inspector are bound to properties of the grid of the main window's SKTWindowController.

Grids and zoom factors are owned by window controllers instead of the views that use them; in the future we may want to make the same grid and zoom factor apply to multiple views, or make the grid parameters and zoom factor into stored per-document preferences.


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