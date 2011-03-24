# String constants declared in the header. They may not be used by any other
# class in the project, but it's a good idea to provide and use them, if only
# to help prevent typos in source code.
SKTTextScriptingContentsKey = "scriptingContents"
SKTTextUndoContentsKey = "undoContents"

# A key that's used in Sketch's property-list-based file and pasteboard formats.
SKTTextContentsKey = "contents"


class SKTText < SKTGraphic

	def contents ()
		# Never return nil.
		if !@contents
			@contents = NSTextStorage.alloc.init

			# We need to be notified whenever the text storage changes.
			@contents.setDelegate(self)
		end
		@contents
	end


	# *** Text Layout ***
	@@layoutManager = nil
	
	# This is a class method to ensure that it doesn't need to access the state
	# of any particular SKTText.
	def self.sharedLayoutManager ()
		# Return a layout manager that can be used for any drawing.
		if !@@layoutManager
			textContainer = NSTextContainer.alloc.initWithContainerSize(NSMakeSize(1.0e7, 1.0e7))
			@@layoutManager = NSLayoutManager.alloc.init
			textContainer.setWidthTracksTextView(false)
			textContainer.setHeightTracksTextView(false)
			@@layoutManager.addTextContainer(textContainer)
		end
		@@layoutManager
	end

	def naturalSize ()
		# Figure out how big this graphic would have to be to show all of its
		# contents. -glyphRangeForTextContainer: forces layout.
		layoutManager = SKTText.sharedLayoutManager
		textContainer = layoutManager.textContainers[0]
		textContainer.setContainerSize(NSMakeSize(bounds.size.width, 1.0e7))
		contents.addLayoutManager(layoutManager)
		layoutManager.glyphRangeForTextContainer(textContainer)
		naturalSize = layoutManager.usedRectForTextContainer(textContainer).size;
		contents.removeLayoutManager(layoutManager)
		return naturalSize
	end

	def setHeightToMatchContents ()
		# Update the bounds of this graphic to match the height of the text. Make
		# sure that doesn't result in the registration of a spurious undo action.
		# There might be a noticeable performance win to be had during editing by
		# making this object a delegate of the text views it creates, implementing
		# -[NSObject(NSTextDelegate) textDidChange:], and using information that's
		# already calculated by the editing text view instead of invoking
		# -makeNaturalSize like this.
		willChangeValueForKey(SKTGraphicKeysForValuesToObserveForUndoKey)
		@boundsBeingChangedToMatchContents = true
		didChangeValueForKey(SKTGraphicKeysForValuesToObserveForUndoKey)
		setBounds(NSMakeRect(bounds.origin.x, bounds.origin.y, bounds.size.width, naturalSize.height))
		willChangeValueForKey(SKTGraphicKeysForValuesToObserveForUndoKey)
		@boundsBeingChangedToMatchContents = false
		didChangeValueForKey(SKTGraphicKeysForValuesToObserveForUndoKey)
	end

	# Conformance to the NSTextStorageDelegate protocol.
	def textStorageDidProcessEditing (notification)

		# The work we're going to do here involves sending
		# -glyphRangeForTextContainer: to a layout manager, but you can't send
		# that message to a layout manager attached to a text storage that's still
		# responding to -endEditing, so defer the work to a point where
		# -endEditing has returned.
		performSelector('setHeightToMatchContents', withObject: nil, afterDelay: 0.0)
	end

#pragma mark *** Private KVC-Compliance for Public Properties ***

	def undoContents ()
		# Never return an object whose value will change after it's been returned.
		# This is generally good behavior for any getter method that returns the
		# value of an attribute or a to-many relationship. (For to-one
		# relationships just returning the related object is the right thing to
		# do, as in this class' -contents method.) However, this particular
		# implementation of this good behavior might not be fast enough for all
		# situations. If the copying here causes a performance problem, an
		# alternative might be to return [[contents retain] autorelease], set a
		# bit that indicates that the contents should be lazily replaced with a
		# copy before any mutation, and then heed that bit in other methods of
		# this class.
		contents.copy
	end


#pragma mark *** Overrides of SKTGraphic Methods ***

	def initWithProperties (props)
		# Let SKTGraphic do its job and then handle the one additional property
		# defined by this subclass.
		super
	
		# The dictionary entries are all instances of the classes that can be
		# written in property lists. Don't trust the type of something you get out
		# of a property list unless you know your process created it or it was read
		# from your application or framework's resources. We don't have to worry
		# about KVO-compliance in initializers like this by the way; no one should
		# be observing an unitialized object.
		contentsData = props[SKTTextContentsKey]
		if contentsData.kind_of? NSData
			contents = NSUnarchiver.unarchiveObjectWithData(contentsData)
			if contents.kind_of? NSTextStorage
				@contents = contents

				# We need to be notified whenever the text storage changes.
				@contents.setDelegate(self)
			end
		end
		return self;
	end

	def properties
		# Let SKTGraphic do its job and then handle the one additional property
		# defined by this subclass. The dictionary must contain nothing but values
		# that can be written in old-style property lists.
		props = super
		props[SKTTextContentsKey] = NSArchiver.archivedDataWithRootObject(contents)
		return props
	end

	def drawingStroke ()
		# We never draw a stroke on this kind of graphic.
		return false
	end

	def drawingBounds ()
		# The drawing bounds must take into account the focus ring that might be
		# drawn by this class' override of
		# -drawContentsInView:isBeingCreatedOrEdited:. It can't forget to take
		# into account drawing done by -drawHandleInView:atPoint: though. Because
		# this class doesn't override -drawHandleInView:atPoint:, it should invoke
		# super to let SKTGraphic take care of that, and then alter the results.
		NSUnionRect(super, NSInsetRect(bounds, -1.0, -1.0))
	end

	def drawContentsInView (view, isBeingCreateOrEdited: isBeingCreatedOrEditing)
		# Draw the fill color if appropriate.
		if drawingFill
			fillColor.set
			NSRectFill(bounds)
		end

		# If this graphic is being created it has no text. If it is being edited
		# then the editor returned by -newEditingViewWithSuperviewBounds: will
		# draw the text.
		if isBeingCreatedOrEditing
			# Just draw a focus ring.
			NSColor.knobColor.set
			NSFrameRect(NSInsetRect(bounds, -1.0, -1.0))
		else
			# Don't bother doing anything if there isn't actually any text.
			if contents.length > 0
				# Get a layout manager, size its text container, and use it to draw
				# text. -glyphRangeForTextContainer: forces layout and tells us how
				# much of text fits in the container.
				layoutManager = SKTText.sharedLayoutManager
				textContainer = layoutManager.textContainers[0]
				textContainer.setContainerSize(bounds.size)
				contents.addLayoutManager(layoutManager)
				glyphRange = layoutManager.glyphRangeForTextContainer(textContainer)
				if glyphRange.length > 0
					layoutManager.drawBackgroundForGlyphRange(glyphRange, atPoint: bounds.origin)
					layoutManager.drawGlyphsForGlyphRange(glyphRange, atPoint: bounds.origin)
				end
				contents.removeLayoutManager(layoutManager)
			end
		end
	end


	def canSetDrawingStroke ()
		# Don't let the user think we would even try to draw a stroke on this kind of graphic.
		return false
	end


	def makeNaturalSize ()
		# The real work is done in code shared with -setHeightToMatchContents:.
		setBounds(NSMakeRect(bounds.origin.x, bounds.origin.y, naturalSize.width, naturalSize.height))
	end

	def setBounds (bound)
		# In Sketch the user can change the bounds of a text area while it's being
		# edited using the graphics inspector, scripting, or undo. When that
		# happens we have to update the editing views (there might be more than
		# one, in different windows) to keep things consistent. We don't need to
		# do this when the bounds is being changed to keep up with changes to the
		# contents, because the text views we set up take care of that themselves.
		super
		if !@boundsBeingChangedToMatchContents
			# We didn't set up any multiple-text-view layout managers in
			# -newEditingViewWithSuperviewBounds:, so we're not expecting to have to
			# deal with any here.
			layoutManagers = contents.layoutManagers.each {|lm| lm.firstTextView.setFrame(bound)}
		end
	end

	def newEditingViewWithSuperviewBounds (superviewBounds)
		# Create a text view that has the same frame as this graphic. We use
		# -[NSTextView initWithFrame:textContainer:] instead of -[NSTextView
		# initWithFrame:] because the latter method creates the entire collection
		# of objects associated with an NSTextView - its NSTextContainer,
		# NSLayoutManager, and NSTextStorage - and we already have an
		# NSTextStorage. The text container should be the width of this graphic
		# but very high to accomodate whatever text is typed into it.
		textContainer = NSTextContainer.alloc.initWithContainerSize(NSMakeSize(bounds.size.width, 1.0e7))
		textView = NSTextView.alloc.initWithFrame(bounds, textContainer: textContainer)

		# Create a layout manager that will manage the communication between our
		# text storage and the text container, and hook it up.
		layoutManager = NSLayoutManager.alloc.init
		layoutManager.addTextContainer(textContainer)
		contents.addLayoutManager(layoutManager)

		# Of course text editing should be as undoable as anything else.
		textView.setAllowsUndo(true)

		# This kind of graphic shouldn't appear opaque just because it's being edited.
		textView.setDrawsBackground(false)

		# This is has been handy for debugging text editing view size problems though.
		# textView.setBackgroundColor(NSColor.greenColor)
		# textView.setDrawsBackground(true)

		# Start off with the all of the text selected.
		textView.setSelectedRange(NSMakeRange(0, contents.length))

		# Specify that the text view should grow and shrink to fit the text as
		# text is added and removed, but only in the vertical direction. With
		# these settings the NSTextView will always be large enough to show an
		# extra line fragment but never so large that the user won't be able to
		# see just-typed text on the screen. Sending -setVerticallyResizable:YES
		# to the text view without also sending -setMinSize: or -setMaxSize: would
		# be useless by the way; the default minimum and maximum sizes of a text
		# view are the size of the frame that is specified at initialization time.
		textView.setMinSize(NSMakeSize(bounds.size.width, 0.0))
		textView.setMaxSize(NSMakeSize(bounds.size.width, superviewBounds.size.height - bounds.origin.y))
		textView.setVerticallyResizable(true)
		# The invoker doesn't have to release this object.
		return textView
	end

	def finalizeEditingView (editingView)
		# Tell our text storage that it doesn't have to talk to the editing view's
		# layout manager anymore.
		contents.removeLayoutManager(editingView.layoutManager)
	end

	def keysForValuesToObserveForUndo ()
		# Observation of "undoContents," and the observer's resulting registration
		# of changes with the undo manager, is only valid when changes are made to
		# text contents via scripting. When changes are made directly by the user
		# in a text view the text view will register better, more specific, undo
		# actions. Also, we don't want some changes of bounds to result in undo
		# actions.
		keysToReturn = super
		if @contentsBeingChangedByScripting || @boundsBeingChangedToMatchContents
			keys = keysToReturn.mutableCopy
			keys.addObject(SKTTextUndoContentsKey) if @contentsBeingChangedByScripting
			keys.removeObject(SKTGraphicBoundsKey) if @boundsBeingChangedToMatchContents
			keysToReturn = keys
		end
		return keysToReturn
	end

	@@presentablePropertyNameForTextKey = {
		SKTTextUndoContentsKey => NSLocalizedStringFromTable("Text", "UndoStrings", "Action name part for SKTTextUndoContentsKey.")
	}
	
	def self.presentablePropertyNameForKey (key)
		@@presentablePropertyNameForTextKey[key] || super
	end
end


=begin

Derived from Apple's sample code for Sketch and converted to MacRuby by Dave Baldwin.

Additional comments from corresponding header file:

This class is KVC and KVO (kind of) compliant for this key:

"undoContents" (an NSAttributedString; read-write) - Also the text being presented by this object. This is an attribute, and no one should be surprised if each invocation of -valueForKey:@"undoContents" returns a different object. One _should_ be surprised if the object returned by an invocation of -valueForKey:@"undoContents" changes after it's returned. (In an ideal world, this is true of pretty much all getting of attribute values and to-many relationships, regardless of whether the getting is done via KVC or via a directly-invoked accessor method). This class is only KVO-compliant for this key while -keysForValuesToObserveForUndo would return a set containing the key. That (and, in Sketch, SKTDocument's observing of "keysForValuesToObserveForUndo") are all the KVO-compliance that's necessary to make scripted changes of the contents undoable. More complete KVO-compliance is very difficult to implement because NSTextView's undo mechanism changes NSTextStorages directly, and listening in on that conversation is a lot of work.


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