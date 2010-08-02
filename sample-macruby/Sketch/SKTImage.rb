# String constants declared in the header. They may not be used by any other
# class in the project, but it's a good idea to provide and use them, if only
# to help prevent typos in source code.
SKTImageIsFlippedHorizontallyKey = "flippedHorizontally"
SKTImageIsFlippedVerticallyKey = "flippedVertically"
SKTImageFilePathKey = "filePath"

# Another key, which is just used in persistent property dictionaries.
SKTImageContentsKey = "contents"

class SKTImage < SKTGraphic


	# - (id)copyWithZone:(NSZone *)zone {
	# 
	#	  # Do the regular Cocoa thing.
	#	  SKTImage *copy = [super copyWithZone:zone];
	#	  copy->_contents = [_contents copy];
	#	  return copy;
	# 
	# }



	# *** Private KVC-Compliance for Public Properties ***


	def setFlippedHorizontally (isFlippedHorizontally)
		# Record the value and flush the transformed contents cache.
		@isFlippedHorizontally = isFlippedHorizontally
	end

	def setFlippedVertically (isFlippedVertically)
		# Record the value and flush the transformed contents cache.
		@isFlippedVertically = isFlippedVertically;
	end

	def setFilePath (filePath)
		# If there's a transformed version of the contents being held as a cache,
		# it's invalid now.
		@contents = NSImage.alloc.initWithContentsOfFile(filePath.stringByStandardizingPath)
	end

	# *** Public Methods ***

	def initWithPosition (position, contents: contents)
		init
		@contents = contents

		# Leave the image centered on the mouse pointer.
		contentsSize = @contents.size
		setBounds(NSMakeRect((position.x - (contentsSize.width / 2.0)), (position.y - (contentsSize.height / 2.0)), contentsSize.width, contentsSize.height))

		self
	end

	# *** Overrides of SKTGraphic Methods ***

	def initWithProperties (properties)
		# Let SKTGraphic do its job and then handle the additional properties
		# defined by this subclass.
		super

		# The dictionary entries are all instances of the classes that can be
		# written in property lists. Don't trust the type of something you get out
		# of a property list unless you know your process created it or it was
		# read from your application or framework's resources. We don't have to
		# worry about KVO-compliance in initializers like this by the way; no one
		# should be observing an unitialized object.
		contentsData = properties[SKTImageContentsKey]
		if contentsData.kind_of? NSData
			contents = NSUnarchiver.unarchiveObjectWithData(contentsData)
			@contents = contents if contents.kind_of? NSImage
		end

		if properties[SKTImageIsFlippedHorizontallyKey].kind_of? NSNumber
			@isFlippedHorizontally = properties[SKTImageIsFlippedHorizontallyKey].boolValue
		end

		if properties[SKTImageIsFlippedVerticallyKey].kind_of? NSNumber
			@isFlippedVertically = properties[SKTImageIsFlippedVerticallyKey].boolValue
		end
		
		self
	end


	def properties ()
		# Let SKTGraphic do its job and then handle the one additional property
		# defined by this subclass. The dictionary must contain nothing but values
		# that can be written in old-style property lists.
		props = super
		props[SKTImageContentsKey] = NSArchiver.archivedDataWithRootObject(@contents)
		props[SKTImageIsFlippedHorizontallyKey] = NSNumber.numberWithBool(@isFlippedHorizontally)
		props[SKTImageIsFlippedVerticallyKey] = NSNumber.numberWithBool(@isFlippedVertically)
		props
	end

	def isDrawingFill ()
		# We never fill an image with color.
		false
	end


	def isDrawingStroke ()
		# We never draw a stroke on an image.
		false
	end


	def self.keyPathsForValuesAffectingDrawingContents ()
		# Flipping affects drawing but not the drawing bounds. So of course do the
		# properties managed by the superclass.
		keys = super.mutableCopy
		keys.addObject(SKTImageIsFlippedHorizontallyKey)
		keys.addObject(SKTImageIsFlippedVerticallyKey)
		keys
	end


	def drawContentsInView (view, isBeingCreateOrEdited: isBeingCreatedOrEditing)
		# Fill the background with the fill color. Maybe it will show, if the image has an alpha channel.
		if isDrawingFill
			fillColor.set
			NSRectFill(bounds);
		end

		# Surprisingly, NSImage's -draw... methods don't take into account whether
		# or not the view is flipped. In Sketch, SKTGraphicViews are flipped (and
		# this model class is not supposed to have dependencies on the oddities of
		# any particular view class anyway). So, just do our own transformation
		# matrix manipulation.
		transform = NSAffineTransform.transform

		# Translating to actually place the image (as opposed to translating as
		# part of flipping).
		transform.translateXBy(bounds.origin.x, yBy: bounds.origin.y)

		# Flipping according to the user's wishes.
		transform.translateXBy(@isFlippedHorizontally ? bounds.size.width : 0.0, yBy: @isFlippedVertically ? bounds.size.height : 0.0)
		transform.scaleXBy(@isFlippedHorizontally ? -1.0 : 1.0, yBy:@isFlippedVertically ? -1.0 : 1.0)

		# Scaling to actually size the image (as opposed to scaling as part of flipping).
		contentsSize = @contents.size
		transform.scaleXBy(bounds.size.width / contentsSize.width, yBy: bounds.size.height / contentsSize.height)

		# Flipping to accomodate -[NSImage
		# drawAtPoint:fromRect:operation:fraction:]'s odd behavior.
		if view && view.isFlipped
			transform.translateXBy(0.0, yBy: contentsSize.height)
			transform.scaleXBy(1.0, yBy: -1.0)
		end

		# Do the actual drawing, saving and restoring the graphics state so as not
		# to interfere with the drawing of selection handles or anything else in
		# the same view.
		NSGraphicsContext.currentContext.saveGraphicsState
		transform.concat
		@contents.drawAtPoint(NSZeroPoint, fromRect: NSMakeRect(0.0, 0.0, contentsSize.width, contentsSize.height), operation: NSCompositeSourceOver, fraction: 1.0)
		NSGraphicsContext.currentContext.restoreGraphicsState
	end


	def canSetDrawingFill ()
		# Don't let the user think we would even try to fill an image with color.
		return false
	end


	def canSetDrawingStroke ()
		# Don't let the user think we would even try to draw a stroke on image.
		return false
	end

	def flipHorizontally ()
		setFlippedHorizontally(@isFlippedHorizontally ? false : true)
	end

	def flipVertically ()
		setFlippedVertically(@isFlippedVertically ? false : true)
	end

	def makeNaturalSize ()
		# Return the image to its natural size and stop flipping it.
		b = bounds
		b.size = @contents.size
		setBounds(b)
		setFlippedHorizontally(false)
		setFlippedVertically(false)
	end

	def setBounds (bounds)
		# Flush the transformed contents cache and then do the regular SKTGraphic
		# thing.
		super
	end

	def keysForValuesToObserveForUndo ()
		# This class defines a few properties for which changes are undoable, in
		# addition to the ones inherited from SKTGraphic.
		keys = super.mutableCopy
		keys.addObject(SKTImageIsFlippedHorizontallyKey)
		keys.addObject(SKTImageIsFlippedVerticallyKey)
		keys
	end

	@@presentablePropertyNamesByKey = {
		SKTImageIsFlippedHorizontallyKey => NSLocalizedStringFromTable("Horizontal Flipping", "UndoStrings", "Action name part for SKTImageIsFlippedHorizontallyKey."),
		SKTImageIsFlippedVerticallyKey => NSLocalizedStringFromTable("Vertical Flipping", "UndoStrings", "Action name part for SKTImageIsFlippedVerticallyKey.")
		
	}
	
	def self.presentablePropertyNameForKey (key)
		@@presentablePropertyNamesByKey[key] || super
	end
end


=begin

Derived from Apple's sample code for Sketch and converted to MacRuby by Dave Baldwin.

Additional comments from corresponding header file:

This class is KVC and KVO compliant for these keys:

"flippedHorizontally" and "flippedVertically" (boolean NSNumbers; read-only) - Whether or not the image is flipped relative to its natural orientation.

"filePath" (an NSString containing a path to an image file; write-only) - the scriptable property that can specified as an alias in the record passed as the "with properties" parameter of a "make" command, so you can create images via AppleScript.

In Sketch "flippedHorizontally" and "flippedVertically" are two more of the properties that SKTDocument observes so it can register undo actions when they change. Also, "imageFilePath" is scriptable.


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