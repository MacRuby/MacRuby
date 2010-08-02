# The names of the bindings supported by this class, in addition to the ones
# whose support is inherited from NSView.
SKTGraphicViewGraphicsBindingName = "graphics"
SKTGraphicViewSelectionIndexesBindingName = "selectionIndexes"
SKTGraphicViewGridBindingName = "grid"

# The type name that this class uses when putting flattened graphics on the
# pasteboard during cut, copy, and paste operations. The format that's
# identified by it is not the exact same thing as the native document format
# used by SKTDocument, because SKTDocuments store NSPrintInfos (and maybe
# other stuff too in the future). We could easily use the exact same format
# for pasteboard data and document files if we decide it's worth it, but so
# far we haven't.
SKTGraphicViewPasteboardType = "Apple Sketch 2 pasteboard type"

# The default value by which repetitively pasted sets of graphics are offset
# from each other, so the user can paste repeatedly and not end up with a pile
# of graphics that overlay each other so perfectly only the top set can be
# selected with the mouse.
SKTGraphicViewDefaultPasteCascadeDelta = 10.0


# # Some methods that are invoked by methods above them in this file.
# @interface SKTGraphicView(SKTForwardDeclarations)
# - (NSArray *)graphics;
# - (void)stopEditing;
# - (void)stopObservingGraphics:(NSArray *)graphics;
# @end
# 
# 
# # Some methods that really should be declared in AppKit's NSWindow.h, but are not. You can consider them public. (In general though Cocoa methods that are not declared in header files are not public, and you run a bad risk of your application breaking on future versions of Mac OS X if you invoke or override them.) See their uses down below in SKTGraphicView's own implementations of -undo: and -redo:.
# @interface NSWindow(SKTWellTheyrePublicNow)
# def undo:(id)sender;
# def redo:(id)sender;
# @end

GraphicInfo = Struct.new(:graphic, :index, :isSelected, :handle) 

class SKTGraphicView < NSView

	attr_reader	:grid		# for KVO to work
	
	# An override of the superclass' designated initializer.
	def initWithFrame (frame)
		super

		# Create the proxy objects used during observing
		@gridObserver = SKTObserver.new(self, :gridAnyDidChange)
		@graphicsBoundsObserver = SKTObserver.new(self, :graphicsBoundDidChange)
		@graphicsContentObserver = SKTObserver.new(self, :graphicsContentDidChange)
		@graphicsSelectionIndexesObserver = SKTObserver.new(self, :graphicsSelectionIndicesDidChange)
		@graphicsContainerObserver = SKTObserver.new(self, :graphicsContainerDidChange)

	    @marqueeSelectionBounds = NSZeroRect

		# Specify what kind of pasteboard types this view can handle being dropped
		# on it.
		registerForDraggedTypes([NSColorPboardType, NSFilenamesPboardType] + NSImage.imagePasteboardTypes)

		# Initalize the cascading of pasted graphics.
		@pasteboardChangeCount = -1
		@pasteCascadeNumber = 0
		@pasteCascadeDelta = NSMakePoint(SKTGraphicViewDefaultPasteCascadeDelta, SKTGraphicViewDefaultPasteCascadeDelta)
		return self;
	end


	# - (void)dealloc {
	# 
	#	  # If we've set a timer to show handles invalidate it so it doesn't send a message to this object's zombie.
	#	  [_handleShowingTimer invalidate];
	# 
	#	  # Make sure any outstanding editing view doesn't cause leaks.
	#	  [self stopEditing];
	# 
	#	  # Stop observing grid changes.
	#	  [_grid removeObserver:self forKeyPath:SKTGridAnyKey];
	# 
	#	  # Stop observing objects for the bindings whose support isn't implemented using NSObject's default implementations.
	#	  [self unbind:SKTGraphicViewGraphicsBindingName];
	#	  [self unbind:SKTGraphicViewSelectionIndexesBindingName];
	# 
	#	  # Do the regular Cocoa thing.
	#	  [_grid release];
	#	  [super dealloc];
	# 
	# }


	# *** Bindings ***

	def graphics ()
		# A graphic view doesn't hold onto an array of the graphics it's
		# presenting. That would be a cache that hasn't been justified by
		# performance measurement (not yet anyway). Get the array of graphics
		# from the bound-to object (an array controller, in Sketch's case).
		# It's poor practice for a method that returns a collection to return
		# nil, so never return nil.
	    @graphicsContainer.valueForKeyPath(@graphicsKeyPath) || []
	end

	def mutableGraphics ()
		# Get a mutable array of graphics from the bound-to object (an array
		# controller, in Sketch's case). The bound-to object is responsible for
		# being KVO-compliant enough that all observers of the bound-to property
		# get notified of whatever mutation we perform on the returned array.
		# Trying to mutate the graphics of a graphic view whose graphics aren't
		# bound to anything is a programming error.
	    @graphicsContainer.mutableArrayValueForKeyPath(@graphicsKeyPath)
	end

	def selectionIndexes
		# A graphic view doesn't hold onto the selection indexes. That would be a
		# cache that hasn't been justified by performance measurement (not yet
		# anyway). Get the selection indexes from the bound-to object (an array
		# controller, in Sketch's case). It's poor practice for a method that
		# returns a collection (and an index set is a collection) to return nil,
		# so never return nil.
	    @selectionIndexesContainer.valueForKeyPath(@selectionIndexesKeyPath) || NSIndexSet.indexSet
	end

	# Why isn't this method called -setSelectionIndexes:? Mostly to encourage a
	# naming convention that's useful for a few reasons: NSObject's default
	# implementation of key-value binding (KVB) uses key-value coding (KVC) to
	# invoke methods like -set<BindingName>: on the bound object when the bound-to
	# property changes, to make it simple to support binding in the simple case of
	# a view property that affects the way a view is drawn but whose value isn't
	# directly manipulated by the user. If NSObject's default implementation of
	# KVB were good enough to use for this "selectionIndexes" property maybe we
	# _would_ implement a -setSelectionIndexes: method instead of stuffing so much
	# code in -observeValueForKeyPath:ofObject:change:context: down below (but
	# it's not, because it doesn't provide a way to get at the old and new
	# selection indexes when they change). So, this method isn't here to take
	# advantage of NSObject's default implementation of KVB. It's here to
	# centralize the bindings work that must be done when the user changes the
	# selection (check out all of the places it's invoked down below). Hopefully
	# the different verb in this method name is a good reminder of the
	# distinction.

	# A person who assumes that a -set... method always succeeds, and always sets
	# the exact value that was passed in (or throws an exception for invalid
	# values to signal the need for some debugging), isn't assuming anything
	# unreasonable. Setters that invalidate that assumption make a class'
	# interface unnecessarily unpredictable and hard to program against. Sometimes
	# they require people to write code that sets a value and then gets it right
	# back again to keep multiple copies of the value synchronized, in case the
	# setting didn't "take." So, avoid that. When validation is appropriate don't
	# put it in your setter. Instead, implement a separate validation method.
	# Follow the naming pattern established by KVC's -validateValue:forKey:error:
	# when applicable. Now, _this_ method can't guarantee that, when it's invoked,
	# an immediately subsequent invocation of -selectionIndexes will return the
	# passed-in value. It's supposed to set the value of a property in the
	# bound-to object using KVC, but only after asking the bound-to object to
	# validate the value. So, again, -setSelectionIndexes: wouldn't be a very good
	# name for it.

	def changeSelectionIndexes (indexes)
		# After all of that talk, this method isn't invoking
		# -validateValue:forKeyPath:error:. It will, once we come up with an
		# example of invalid selection indexes for this case. It will also someday
		# take any value transformer specified as a binding option into account,
		# so you have an example of how to do that.
		# Set the selection index set in the bound-to object (an array controller,
		# in Sketch's case). The bound-to object is responsible for being
		# KVO-compliant enough that all observers of the bound-to property get
		# notified of the setting. Trying to set the selection indexes of a
		# graphic view whose selection indexes aren't bound to anything is a
		# programming error.
		raise "An SKTGraphicView's 'selectionIndexes' property is not bound to anything." if !(@selectionIndexesContainer && @selectionIndexesKeyPath)
		@selectionIndexesContainer.setValue(indexes,  forKeyPath: @selectionIndexesKeyPath)
	end

	def setGrid (grid)
		# Weed out redundant invocations.
		if grid != @grid
			# Stop observing changes in the old grid.

			@grid.removeObserver(@gridObserver, forKeyPath: SKTGridAnyKey) if @grid
			@grid = grid

			# Start observing changes in the new grid so we know when to redraw it.
			@grid.addObserver(@gridObserver, forKeyPath: SKTGridAnyKey, options: 0, context: nil)
		end
	end

	def startObservingGraphics (graphics)
		# Start observing "drawingBounds" in each of the graphics. Use KVO's
		# options for getting the old and new values in change notifications so we
		# can invalidate just the old and new drawing bounds of changed graphics
		# when they move or change size, instead of the whole view. (The new
		# drawing bounds is easy to otherwise get using regular KVC, but the old
		# one would otherwise have been forgotten by the time we get the
		# notification.) Instances of SKTGraphic must therefore be KVC- and
		# KVO-compliant for drawingBounds. SKTGraphics's use of KVO's dependency
		# mechanism means that being KVO-compliant for drawingBounds when
		# subclassing is as easy as overriding -drawingBounds (to compute an
		# accurate value) and +keyPathsForValuesAffectingDrawingBounds (to trigger
		# KVO's dependency mechanism) though.
		allGraphicIndexes = NSIndexSet.indexSetWithIndexesInRange(NSMakeRange(0, graphics.count))
		
		graphics.addObserver(@graphicsBoundsObserver, toObjectsAtIndexes: allGraphicIndexes, 
									forKeyPath: SKTGraphicDrawingBoundsKey, 		
									options: NSKeyValueObservingOptionNew | NSKeyValueObservingOptionOld, 
									context: nil)

		# Start observing "drawingContents" in each of the graphics. Don't bother
		# using KVO's options for getting the old and new values because there is
		# no value for drawingContents. It's just something that depends on all of
		# the properties that affect drawing of a graphic but don't affect the
		# drawing bounds of the graphic. Similar to what we do for drawingBounds,
		# SKTGraphics' use of KVO's dependency mechanism means that being
		# KVO-compliant for drawingContents when subclassing is as easy as
		# overriding +keyPathsForValuesAffectingDrawingContents (there is no
		# -drawingContents method to override).
		graphics.addObserver(@graphicsContentObserver, toObjectsAtIndexes: allGraphicIndexes, 
								forKeyPath: SKTGraphicDrawingContentsKey,
								options: 0, context: nil)
	end

	def stopObservingGraphics (graphics)
		# Undo what we do in -startObservingGraphics:.
		allGraphicIndexes = NSIndexSet.indexSetWithIndexesInRange(NSMakeRange(0, graphics.count))
		graphics.removeObserver(@graphicsContentObserver, fromObjectsAtIndexes: allGraphicIndexes, 
									forKeyPath: SKTGraphicDrawingContentsKey)
		graphics.removeObserver(@graphicsBoundsObserver, fromObjectsAtIndexes: allGraphicIndexes, 
									forKeyPath: SKTGraphicDrawingBoundsKey)
	end

	# An override of the NSObject(NSKeyValueBindingCreation) method.
	def bind (bindingName, toObject: observableObject, withKeyPath: observableKeyPath, options: options)
		# SKTGraphicView supports several different bindings.
		if bindingName == SKTGraphicViewGraphicsBindingName
			# We don't have any options to support for our custom "graphics" binding.
			raise "SKTGraphicView doesn't support any options for the 'graphics' binding." if options && options.count == 0

			# Rebinding is just as valid as resetting.
			unbind(SKTGraphicViewGraphicsBindingName) if @graphicsContainer || @graphicsKeyPath

			# Record the information about the binding.
			@graphicsContainer = observableObject
			@graphicsKeyPath = observableKeyPath

			# Start observing changes to the array of graphics to which we're bound,
			# and also start observing properties of the graphics themselves that
			# might require redrawing.
			@graphicsContainer.addObserver(@graphicsContainerObserver, forKeyPath: @graphicsKeyPath, 
															options: NSKeyValueObservingOptionNew | NSKeyValueObservingOptionOld, 
															context: nil)
			startObservingGraphics(@graphicsContainer.valueForKeyPath(@graphicsKeyPath))

			# Redraw the whole view to make the binding take immediate visual
			# effect. We could be much cleverer about this and just redraw the part
			# of the view that needs it, but in typical usage the view isn't even
			# visible yet, so that would probably be a waste of time (the
			# programmer's and the computer's). If this view ever gets reused in
			# some wildly dynamic situation where the bindings come and go we can
			# reconsider optimization decisions like this then.
			setNeedsDisplay(true)
		elsif bindingName == SKTGraphicViewSelectionIndexesBindingName
			# We don't have any options to support for our custom "selectionIndexes"
			# binding either. Maybe in the future someone will imagine a use for a
			# value transformer on this, and we'll add support for it then.
			raise "SKTGraphicView doesn't support any options for the 'selectionIndexes' binding." if options && options.count == 0

			# Rebinding is just as valid as resetting.
			unbind(SKTGraphicViewSelectionIndexesBindingName) if @selectionIndexesContainer || @selectionIndexesKeyPath

			# Record the information about the binding.
			@selectionIndexesContainer = observableObject
			@selectionIndexesKeyPath = observableKeyPath

			# Start observing changes to the selection indexes to which we're bound.
			@selectionIndexesContainer.addObserver(@graphicsSelectionIndexesObserver, forKeyPath: @selectionIndexesKeyPath,
			 											options: NSKeyValueObservingOptionNew | NSKeyValueObservingOptionOld, 
														context: nil)

			# Same comment as above.
			setNeedsDisplay(true)
		else
			# For every binding except "graphics" and "selectionIndexes" just use
			# NSObject's default implementation. It will start observing the
			# bound-to property. When a KVO notification is sent for the bound-to
			# property, this object will be sent a [self setValue:theNewValue
			# forKey:theBindingName] message, so this class just has to be
			# KVC-compliant for a key that is the same as the binding name, like
			# "grid." That's why this class has a -setGrid: method. Also, NSView
			# supports a few simple bindings of its own, and there's no reason to
			# get in the way of those.
			super
		end
	end

	# An override of the NSObject(NSKeyValueBindingCreation) method.
	def unbind (bindingName)
		# SKTGraphicView supports several different bindings. For the ones that
		# don't use NSObject's default implementation of key-value binding, undo
		# what we do in -bind:toObject:withKeyPath:options:, and then redraw the
		# whole view to make the unbinding take immediate visual effect.
		if bindingName == SKTGraphicViewGraphicsBindingName
			stopObservingGraphics(graphics)
			@graphicsContainer.removeObserver(@graphicsContainerObserver, forKeyPath: @graphicsKeyPath) if @graphicsContainer
			@graphicsContainer = nil
			@graphicsKeyPath = nil
			setNeedsDisplay(true)
		elsif bindingName == SKTGraphicViewSelectionIndexesBindingName
			@selectionIndexesContainer.removeObserver(@graphicsSelectionIndexesObserver, forKeyPath: @selectionIndexesKeyPath)
			@selectionIndexesContainer = nil
			@selectionIndexesKeyPath = nil
			setNeedsDisplay(true)
		else
			# For every binding except "graphics" and "selectionIndexes" just use
			# NSObject's default implementation. Also, NSView supports a few simple
			# bindings of its own, and there's no reason to get in the way of those.
			super
		end
	end

	def graphicsContainerDidChange (keyPath, observedObject, change)
		# The "old value" or "new value" in a change dictionary will be NSNull,
		# instead of just not existing, if the corresponding option was
		# specified at KVO registration time and the value for some key in the
		# key path is nil. In Sketch's case there are times in an
		# SKTGraphicView's life cycle when it's bound to the graphics of a
		# window controller's document, and the window controller's document is
		# nil. Don't redraw the graphic view when we get notifications about
		# that.

		# Have graphics been removed from the bound-to container?
		oldGraphics = change[NSKeyValueChangeOldKey]
		if oldGraphics
			# Yes. Stop observing them because we don't want to leave dangling observations.
			stopObservingGraphics(oldGraphics)

			# Redraw just the parts of the view that they used to occupy.
			oldGraphics.each {|g| setNeedsDisplayInRect(g.drawingBounds)}

			# If a graphic is being edited right now, and the graphic is being
			# removed, stop the editing. This way we don't strand an editing view
			# whose graphic has been pulled out from under it. This situation can
			# arise from undoing and scripting.
			stopEditing if @editingGraphic && oldGraphics.index(@editingGraphic)
		end

		# Have graphics been added to the bound-to container?
		newGraphics = change[NSKeyValueChangeNewKey]
		if newGraphics
			# Yes. Start observing them so we know when we need to redraw the
			# parts of the view where they sit.
			startObservingGraphics(newGraphics)

			# Redraw just the parts of the view that they now occupy.
			newGraphics.each {|g| setNeedsDisplayInRect(g.drawingBounds)}

			# If undoing or redoing is being done we have to select the graphics
			# that are being added. For NSKeyValueChangeSetting the change
			# dictionary has no NSKeyValueChangeIndexesKey entry, so we have to
			# figure out the indexes ourselves, which is easy. For
			# NSKeyValueChangeRemoval the indexes are not the indexes of anything
			# being added. You might notice that this is only place in this entire
			# method that we check the value of the NSKeyValueChangeKindKey entry.
			# In general, doing so should be pretty uncommon in overrides of
			# -observeValueForKeyPath:ofObject:change:context:, because the values
			# of the other entries are usually all you need, and handling all of
			# the possible NSKeyValueChange values requires care. In Sketch we'll
			# never see NSKeyValueChangeSetting or NSKeyValueChangeReplacement but
			# we want to demonstrate a reusable class so we handle them anyway.
			additionalUndoSelectionIndexes = nil;
			changeKind = change[NSKeyValueChangeKindKey].to_i
			if changeKind == NSKeyValueChangeSetting
				additionalUndoSelectionIndexes = NSIndexSet.indexSetWithIndexesInRange(NSMakeRange(0, newGraphics.count))
			elsif changeKind != NSKeyValueChangeRemoval
				additionalUndoSelectionIndexes = change[NSKeyValueChangeIndexesKey]
			end
			
			if additionalUndoSelectionIndexes && @undoSelectionIndexes
				# Use -[NSIndexSet addIndexes:] instead of just replacing the value
				# of _undoSelectionIndexes because we don't know that a single undo
				# action won't include more than one addition of graphics.
				@undoSelectionIndexes.addIndexes(additionalUndoSelectionIndexes) 
			end
		end
	end
	
	def gridAnyDidChange (keyPath, observedObject, change)
		# Either a new grid is to be used (this only happens once in Sketch) or
		# one of the properties of the grid has changed. Regardless, redraw
		# everything.
		setNeedsDisplay(true)
	end
		
		
	def graphicsBoundDidChange (keyPath, observedObject, change)
		# Redraw the part of the view that the graphic used to occupy, and the
		# part that it now occupies.
		setNeedsDisplay(change[NSKeyValueChangeOldKey])
		setNeedsDisplayInRect(change[NSKeyValueChangeNewKey])

		# If undoing or redoing is being done add this graphic to the set that
		# will be selected at the end of the undo action. -[NSArray
		# indexOfObject:] is a dangerous method from a performance standpoint.
		# Maybe an undo action that affects many graphics at once will be slow.
		# Maybe something else in this very simple-looking bit of code will be a
		# problem. We just don't yet know whether there will be a performance
		# problem that the user can notice here. We'll check when we do real
		# performance measurement on Sketch someday. At least we've limited the
		# potential problem to undoing and redoing by checking
		# _undoSelectionIndexes!=nil. One thing we do know right now is that we're
		# not using memory to record selection changes on the undo/redo stacks,
		# and that's a good thing.
		if @undoSelectionIndexes
			graphicIndex = graphics.index(observedObject)
			if graphicIndex
				@undoSelectionIndexes.addIndex(graphicIndex)
			end
		end
	end
	
	def graphicsContentDidChange (keyPath, observedObject, change)
		# The graphic's drawing bounds hasn't changed, so just redraw the part
		# of the view that it occupies right now.
			setNeedsDisplayInRect(observedObject.drawingBounds)

		# If undoing or redoing is being done add this graphic to the set that
		# will be selected at the end of the undo action. -[NSArray
		# indexOfObject:] is a dangerous method from a performance standpoint.
		# Maybe an undo action that affects many graphics at once will be slow.
		# Maybe something else in this very simple-looking bit of code will be a
		# problem. We just don't yet know whether there will be a performance
		# problem that the user can notice here. We'll check when we do real
		# performance measurement on Sketch someday. At least we've limited the
		# potential problem to undoing and redoing by checking
		# _undoSelectionIndexes!=nil. One thing we do know right now is that we're
		# not using memory to record selection changes on the undo/redo stacks,
		# and that's a good thing.
		if @undoSelectionIndexes
			graphicIndex = graphics.index(observedObject)
			if graphicIndex
				@undoSelectionIndexes.addIndex(graphicIndex)
			end
		end
	end
	
	def graphicsSelectionIndicesDidChange (keyPath, observedObject, change)
		# Some selection indexes might have been removed, some might have been
		# added. Redraw the selection handles for any graphic whose selectedness
		# has changed, unless the binding is changing completely (signalled by
		# null old or new value), in which case just redraw the whole view.
		oldSelectionIndexes = change[NSKeyValueChangeOldKey]
		newSelectionIndexes = change[NSKeyValueChangeNewKey]
		if oldSelectionIndexes && newSelectionIndexes
			oldSelectionIndex = oldSelectionIndexes.firstIndex
			while oldSelectionIndex != NSNotFound
				if !newSelectionIndexes.containsIndex(oldSelectionIndex)
					deselectedGraphic = graphics[oldSelectionIndex]
					setNeedsDisplayInRect(deselectedGraphic.drawingBounds)
				end
				oldSelectionIndex = oldSelectionIndexes.indexGreaterThanIndex(oldSelectionIndex)
			end

			newSelectionIndex = newSelectionIndexes.firstIndex
			while newSelectionIndex != NSNotFound
				if !oldSelectionIndexes.containsIndex(newSelectionIndex)
					selectedGraphic = graphics[newSelectionIndex]
					setNeedsDisplayInRect(selectedGraphic.drawingBounds)
				end
				newSelectionIndex = newSelectionIndexes.indexGreaterThanIndex(newSelectionIndex)
			end
		else
			setNeedsDisplay(true)
		end	
	end

	# This doesn't contribute to any KVC or KVO compliance. It's just a convenience method that's invoked down below.
	def selectedGraphics ()
		# Simple, because we made sure -graphics and -selectionIndexes never return nil.
		graphics.objectsAtIndexes(selectionIndexes)
	end

	# *** Drawing ***

	# An override of the NSView method.
	def drawRect (rect)
		# Draw the background background.
		NSColor.whiteColor.set
		NSRectFill(rect)

		# Draw the grid.
		@grid.drawRect(rect, inView: self)

		# Draw every graphic that intersects the rectangle to be drawn. In Sketch
		# the frontmost graphics have the lowest indexes.
		currentContext = NSGraphicsContext.currentContext
		(graphics.count - 1).downto(0) do |index|
			graphic = graphics[index]
			graphicDrawingBounds = graphic.drawingBounds

			if NSIntersectsRect(rect, graphicDrawingBounds)
				# Figure out whether or not to draw selection handles on the graphic.
				# Selection handles are drawn for all selected objects except:
				# - While the selected objects are being moved. - For the object
				# actually being created or edited, if there is one.
				drawSelectionHandles = false
				if !@isHidingHandles && graphic != @creatingGraphic && graphic != @editingGraphic
					drawSelectionHandles = selectionIndexes.containsIndex(index)
				end

				# Draw the graphic, possibly with selection handles.
				currentContext.saveGraphicsState
				NSBezierPath.clipRect(graphicDrawingBounds)
				graphic.drawContentsInView(self, isBeingCreateOrEdited: graphic == @creatingGraphic || graphic == @editingGraphic)
				graphic.drawHandlesInView(self) if drawSelectionHandles
				currentContext.restoreGraphicsState
			end
		end

		# If the user is in the middle of selecting draw the selection rectangle.
		if !NSEqualRects(@marqueeSelectionBounds, NSZeroRect)
			NSColor.knobColor.set
			NSFrameRect(@marqueeSelectionBounds)
		end
	end

	def beginEchoingMoveToRulers (echoRect)
		horizontalRuler = enclosingScrollView.horizontalRulerView
		verticalRuler = enclosingScrollView.verticalRulerView
	
		newHorizontalRect = convertRect(echoRect, toView: horizontalRuler)
		newVerticalRect = convertRect(echoRect, toView: verticalRuler)
	
		horizontalRuler.moveRulerlineFromLocation(-1.0, toLocation: NSMinX(newHorizontalRect))
		horizontalRuler.moveRulerlineFromLocation(-1.0, toLocation: NSMidX(newHorizontalRect))
		horizontalRuler.moveRulerlineFromLocation(-1.0, toLocation: NSMaxX(newHorizontalRect))
	
		verticalRuler.moveRulerlineFromLocation(-1.0, toLocation: NSMinY(newVerticalRect))
		verticalRuler.moveRulerlineFromLocation(-1.0, toLocation: NSMidY(newVerticalRect))
		verticalRuler.moveRulerlineFromLocation(-1.0, toLocation: NSMaxY(newVerticalRect))
	
		@rulerEchoedBounds = echoRect.dup
	end


	def continueEchoingMoveToRulers (echoRect)
		horizontalRuler = enclosingScrollView.horizontalRulerView
		verticalRuler = enclosingScrollView.verticalRulerView

		oldHorizontalRect = convertRect(@rulerEchoedBounds, toView: horizontalRuler)
		oldVerticalRect = convertRect(@rulerEchoedBounds, toView: verticalRuler)	

		newHorizontalRect = convertRect(echoRect, toView: horizontalRuler)
		newVerticalRect = convertRect(echoRect, toView: verticalRuler)

		horizontalRuler.moveRulerlineFromLocation(NSMinX(oldHorizontalRect), toLocation: NSMinX(newHorizontalRect))
		horizontalRuler.moveRulerlineFromLocation(NSMidX(oldHorizontalRect), toLocation: NSMidX(newHorizontalRect))
		horizontalRuler.moveRulerlineFromLocation(NSMaxX(oldHorizontalRect), toLocation: NSMaxX(newHorizontalRect))
	
		verticalRuler.moveRulerlineFromLocation(NSMinY(oldVerticalRect), toLocation: NSMinY(newVerticalRect))
		verticalRuler.moveRulerlineFromLocation(NSMidY(oldVerticalRect), toLocation: NSMidY(newVerticalRect))
		verticalRuler.moveRulerlineFromLocation(NSMaxY(oldVerticalRect), toLocation: NSMaxY(newVerticalRect))
	
		# Need to store actual extent rather than reference of bounds object with may be being updated.
		@rulerEchoedBounds = echoRect.dup	
	end

	def stopEchoingMoveToRulers ()
		horizontalRuler = enclosingScrollView.horizontalRulerView
		verticalRuler = enclosingScrollView.verticalRulerView
	
		oldHorizontalRect = convertRect(@rulerEchoedBounds, toView:horizontalRuler)
		oldVerticalRect = convertRect(@rulerEchoedBounds, toView:verticalRuler)	
	
		horizontalRuler.moveRulerlineFromLocation(NSMinX(oldHorizontalRect), toLocation: -1.0);
		horizontalRuler.moveRulerlineFromLocation(NSMidX(oldHorizontalRect), toLocation: -1.0);
		horizontalRuler.moveRulerlineFromLocation(NSMaxX(oldHorizontalRect), toLocation: -1.0);
	
		verticalRuler.moveRulerlineFromLocation(NSMinY(oldVerticalRect), toLocation: -1.0);
		verticalRuler.moveRulerlineFromLocation(NSMidY(oldVerticalRect), toLocation: -1.0);
		verticalRuler.moveRulerlineFromLocation(NSMaxY(oldVerticalRect), toLocation: -1.0);
	
		@rulerEchoedBounds = NSZeroRect
	end


	# *** Editing Subviews ***
	def setNeedsDisplayForEditingViewFrameChangeNotification (viewFrameDidChangeNotification)
		# If the editing view got smaller we have to redraw where it was or cruft
		# will be left on the screen. If the editing view got larger we might be
		# doing some redundant invalidation (not a big deal), but we're not doing
		# any redundant drawing (which might be a big deal). If the editing view
		# actually moved then we might be doing substantial redundant drawing, but
		# so far that wouldn't happen in Sketch.
		# In Sketch this prevents cruft being left on the screen when the user 1)
		# creates a great big text area and fills it up with text, 2) sizes the text
		# area so not all of the text fits, 3) starts editing the text area but
		# doesn't actually change it, so the text area hasn't been automatically
		# resized and the text editing view is actually bigger than the text area,
		# and 4) deletes so much text in one motion (Select All, then Cut) that the
		# text editing view suddenly becomes smaller than the text area. In every
		# other text editing situation the text editing view's invalidation or the
		# fact that the SKTText's "drawingBounds" changes is enough to cause the
		# proper redrawing.
		newEditingViewFrame = viewFrameDidChangeNotification.object.frame
		setNeedsDisplayInRect(NSUnionRect(@editingViewFrame, newEditingViewFrame))
		@editingViewFrame = newEditingViewFrame
	end

	def startEditingGraphic (graphic)
		# It's the responsibility of invokers to not invoke this method when
		# editing has already been started.
		raise "#{SKTGraphicView.startEditingGraphic} is being mis-invoked." if !(!@editingGraphic && !@editingView)

		# Can the graphic even provide an editing view?
		@editingView = graphic.newEditingViewWithSuperviewBounds(bounds)
		if @editingView
			# Keep a pointer to the graphic around so we can ask it to draw its
			# "being edited" look, and eventually send it a -finalizeEditingView:
			# message.
			@editingGraphic = graphic

			# If the editing view adds a ruler accessory view we're going to remove
			# it when editing is done, so we have to remember the old reserved
			# accessory view thickness so we can restore it. Otherwise there will be
			# a big blank space in the ruler.
			@oldReservedThicknessForRulerAccessoryView = enclosingScrollView.horizontalRulerView.reservedThicknessForAccessoryView

			# Make the editing view a subview of this one. It was the graphic's job
			# to make sure that it was created with the right frame and bounds.
			addSubview(@editingView)

			# Make the editing view the first responder so it takes key events and
			# relevant menu item commands.
			window.makeFirstResponder(@editingView)

			# Get notified if the editing view's frame gets smaller, because we may
			# have to force redrawing when that happens. Record the view's frame
			# because it won't be available when we get the notification.
			NSNotificationCenter.defaultCenter.addObserver(self, 
											selector: 'setNeedsDisplayForEditingViewFrameChangeNotification:', 	
											name: NSViewFrameDidChangeNotification, object: @editingView)
			@editingViewFrame = @editingView.frame

			# Give the graphic being edited a chance to draw one more time. In Sketch, SKTText draws a focus ring.
			setNeedsDisplayInRect(@editingGraphic.drawingBounds)
		end
	end

	def stopEditing ()
		# Make it harmless to invoke this method unnecessarily.
		if @editingView
			# Undo what we did in -startEditingGraphic:.
			NSNotificationCenter.defaultCenter.removeObserver(self, name: NSViewFrameDidChangeNotification, object: @editingView)

			# Pull the editing view out of this one. When editing is being stopped
			# because the user has clicked in this view, outside of the editing
			# view, NSWindow will have already made this view the window's first
			# responder, and that's good. However, when editing is being stopped
			# because the edited graphic is being removed (by undoing or scripting,
			# for example), the invocation of -[NSView removeFromSuperview] we do
			# here will leave the window as its own first responder, and that would
			# be bad, so also fix the window's first responder if appropriate. It
			# wouldn't be appropriate to steal first-respondership from sibling
			# views here.
			makeSelfFirstResponder = window.firstResponder == @editingView ? true : false
			@editingView.removeFromSuperview
			window.makeFirstResponder(self) if makeSelfFirstResponder
			
			# If the editing view added a ruler accessory view then remove it
			# because it's not applicable anymore, and get rid of the blank space in
			# the ruler that would otherwise result. In Sketch the NSTextViews
			# created by SKTTexts leave horizontal ruler accessory views.
			horizontalRulerView = enclosingScrollView.horizontalRulerView
			horizontalRulerView.accessoryView = nil
			horizontalRulerView.reservedThicknessForAccessoryView = @oldReservedThicknessForRulerAccessoryView
		
			# Give the graphic that created the editing view a chance to tear down their relationships and then forget about them both.
			@editingGraphic.finalizeEditingView(@editingView)
			@editingGraphic = nil
			@editingView = nil
		end
	end

	# *** Mouse Event Handling ***
	def graphicUnderPoint (point)
		# Search through all of the graphics, front to back, looking for one that
		# claims that the point is on a selection handle (if it's selected) or in
		# the contents of the graphic itself.
		gi = GraphicInfo.new
		graphics.each_with_index do |graphic, index|
			# Do a quick check to weed out graphics that aren't even in the neighborhood.
			if NSPointInRect(point, graphic.drawingBounds)
				# Check the graphic's selection handles first, because they take
				# precedence when they overlap the graphic's contents.
				graphicIsSelected = selectionIndexes.containsIndex(index)
				if graphicIsSelected
					handle = graphic.handleUnderPoint(point)
					if handle != SKTGraphicNoHandle
						# The user clicked on a handle of a selected graphic.
						gi.graphic = graphic
						gi.handle = handle
					end
				end

				if !gi.graphic
					clickedOnGraphicContents = graphic.isContentsUnderPoint(point)
					if clickedOnGraphicContents
						# The user clicked on the contents of a graphic.
						gi.graphic = graphic
						gi.handle = SKTGraphicNoHandle
					end
				end

				if gi.graphic
					# Return values and stop looking.
					gi.index = index
					gi.isSelected = graphicIsSelected
					break;
				end
			end
		end
		return gi
	end

	def moveSelectedGraphicsWithEvent (event)
		selGraphics = selectedGraphics
		didMove = false
		isMoving = false
		echoToRulers = enclosingScrollView.rulersVisible
		selBounds = SKTGraphic.boundsOfGraphics(selGraphics)
	
		c = selGraphics.count
	
		lastPoint = convertPoint(event.locationInWindow, fromView: nil)
		selOriginOffset = NSMakePoint((lastPoint.x - selBounds.origin.x), (lastPoint.y - selBounds.origin.y))
		beginEchoingMoveToRulers(selBounds) if echoToRulers
	
		while event.type != NSLeftMouseUp
			event = window.nextEventMatchingMask(NSLeftMouseDraggedMask | NSLeftMouseUpMask)
			autoscroll(event)
			curPoint = convertPoint(event.locationInWindow, fromView: nil)
			if !isMoving && ((curPoint.x - lastPoint.x).abs >= 2.0) || ((curPoint.y - lastPoint.y).abs >= 2.0)
				isMoving = true;
				@isHidingHandles = true
			end
		
			if isMoving
				if @grid
					boundsOrigin = NSPoint.new(curPoint.x - selOriginOffset.x, curPoint.y - selOriginOffset.y)
					boundsOrigin  = @grid.constrainedPoint(boundsOrigin)
					curPoint.x = boundsOrigin.x + selOriginOffset.x
					curPoint.y = boundsOrigin.y + selOriginOffset.y
				end
				if !NSEqualPoints(lastPoint, curPoint)
					SKTGraphic.translateGraphics(selGraphics, byX: (curPoint.x - lastPoint.x), y: (curPoint.y - lastPoint.y))
					didMove = true
					if echoToRulers
						continueEchoingMoveToRulers(NSMakeRect(curPoint.x - selOriginOffset.x, curPoint.y - selOriginOffset.y,
						 								NSWidth(selBounds), NSHeight(selBounds)))
					end
					# Adjust the delta that is used for cascading pastes.	Pasting and
					# then moving the pasted graphic is the way you determine the
					# cascade delta for subsequent pastes.
					@pasteCascadeDelta.x += (curPoint.x - lastPoint.x)
					@pasteCascadeDelta.y += (curPoint.y - lastPoint.y)
				end
				lastPoint = curPoint
			end
		end

		stopEchoingMoveToRulers if echoToRulers
		
		if isMoving
			@isHidingHandles = false
			setNeedsDisplayInRect(SKTGraphic.drawingBoundsOfGraphics(selGraphics))
			if didMove
				# Only if we really moved.
				undoManager.setActionName(NSLocalizedStringFromTable("Move", "UndoStrings", "Action name for moves."))
			end
		end
	end

	def resizeGraphic (graphic, usingHandle: handle,  withEvent: event)
		echoToRulers = enclosingScrollView.rulersVisible
		beginEchoingMoveToRulers(graphic.bounds) if echoToRulers
		while event.type != NSLeftMouseUp
			event = window.nextEventMatchingMask(NSLeftMouseDraggedMask | NSLeftMouseUpMask)
			autoscroll(event)
			handleLocation = convertPoint(event.locationInWindow, fromView: nil)
			handleLocation = @grid.constrainedPoint(handleLocation) if @grid
			handle = graphic.resizeByMovingHandle(handle, toPoint: handleLocation)
			continueEchoingMoveToRulers(graphic.bounds) if echoToRulers	
		end

		stopEchoingMoveToRulers if echoToRulers
		undoManager.setActionName(NSLocalizedStringFromTable("Resize", "UndoStrings", "Action name for resizes."))
	end


	def indexesOfGraphicsIntersectingRect (rect)
		indexSetToReturn = NSMutableIndexSet.indexSet
		graphics.each_with_index do |graphic, index|
			indexSetToReturn.addIndex(index) if NSIntersectsRect(rect, graphic.drawingBounds)
		end
		return indexSetToReturn
	end

	def createGraphicOfClass (graphicClass, withEvent: event)
		# Before we invoke -[NSUndoManager beginUndoGrouping] turn off automatic
		# per-event-loop group creation. If we don't turn it off now,
		# -beginUndoGrouping will actually create _two_ undo groups: the top-level
		# automatically-created one and then the nested one that we're explicitly
		# creating. When we invoke -undoNestedGroup down below, the
		# automatically-created undo group will be left on the undo stack. It will
		# be ended automatically at the end of the event loop, which is good, and
		# it will be empty, which is expected, but it will be left on the undo
		# stack so the user will see a useless undo action in the Edit menu, which
		# is bad. Is this a bug in NSUndoManager? Well it's certainly surprising
		# that NSUndoManager isn't bright enough to ignore empty undo groups,
		# especially ones that it itself created automatically, so NSUndoManager
		# could definitely use a little improvement here.
		undoManagerWasGroupingByEvent = undoManager.groupsByEvent
		undoManager.groupsByEvent = false
	
		# We will want to undo the creation of the graphic if the user sizes it to
		# nothing, so create a new group for everything undoable that's going to
		# happen during graphic creation. 
		undoManager.beginUndoGrouping
	
		# Clear the selection.
		changeSelectionIndexes(NSIndexSet.indexSet)

		# Where is the mouse pointer as graphic creation is starting? Should the
		# location be constrained to the grid?
		graphicOrigin = convertPoint(event.locationInWindow, fromView: nil)
		graphicOrigin = @grid.constrainedPoint(graphicOrigin) if @grid

		# Create the new graphic and set what little we know of its location.
		@creatingGraphic = graphicClass.alloc.init
		@creatingGraphic.setBounds(NSMakeRect(graphicOrigin.x, graphicOrigin.y, 0.0, 0.0))

		# Add it to the set of graphics right away so that it will show up in
		# other views of the same array of graphics as the user sizes it.
		mutableGraphics.insertObject(@creatingGraphic, atIndex: 0)

		# Let the user size the new graphic until they let go of the mouse.
		# Because different kinds of graphics have different kinds of handles,
		# first ask the graphic class what handle the user is dragging during this
		# initial sizing.
		resizeGraphic(@creatingGraphic, usingHandle: graphicClass.creationSizingHandle, withEvent: event)

		# Why don't we do [undoManager endUndoGrouping] here, once, instead of
		# twice in the following paragraphs? Because of the [undoManager
		# setGroupsByEvent:NO] game we're playing. If we invoke -[NSUndoManager
		# setActionName:] down below after invoking [undoManager endUndoGrouping]
		# there won't be any open undo group, and NSUndoManager will raise an
		# exception. If we weren't playing the [undoManager setGroupsByEvent:NO]
		# game then it would be OK to invoke -[NSUndoManager setActionName:] after
		# invoking [undoManager endUndoGrouping] because the action name would
		# apply to the top-level automatically-created undo group, which is fine.

		# Did we really create a graphic? Don't check with
		# !NSIsEmptyRect(createdGraphicBounds) because the bounds of a perfectly
		# horizontal or vertical line is "empty" but of course we want to let
		# people create those.
		createdGraphicBounds = @creatingGraphic.bounds
		if NSWidth(createdGraphicBounds) != 0.0 || NSHeight(createdGraphicBounds) != 0.0
			# Select it.
			changeSelectionIndexes(NSIndexSet.indexSetWithIndex(0))
	
			# The graphic wasn't sized to nothing during mouse tracking. Present its
			# editing interface it if it's that kind of graphic (like Sketch's
			# SKTTexts). Invokers of the method we're in right now should have
			# already cleared out _editingView.
			startEditingGraphic(@creatingGraphic)

			# Overwrite whatever undo action name was registered during all of that with a more specific one.
			cn = NSBundle.mainBundle.localizedStringForKey(graphicClass.to_s, value: "", table: "GraphicClassNames")

			undoManager.setActionName(NSLocalizedStringFromTable("Create #{cn}", "UndoStrings", "Action name for newly created graphics. Class name is inserted at the substitution."))

			# Balance the invocation of -[NSUndoManager beginUndoGrouping] that we did up above.
			undoManager.endUndoGrouping
		else
			# Balance the invocation of -[NSUndoManager beginUndoGrouping] that we did up above.
			undoManager.endUndoGrouping

			# The graphic was sized to nothing during mouse tracking. Undo
			# everything that was just done. Disable undo registration while undoing
			# so that we don't create a spurious redo action.
			undoManager.disableUndoRegistration
			undoManager.undoNestedGroup
			undoManager.enableUndoRegistration
		end

		# Balance the invocation of -[NSUndoManager setGroupsByEvent:] that we did
		# up above. We're careful to restore the old value instead of merely
		# invoking -setGroupsByEvent:YES because we don't know that the method
		# we're in right now won't in the future be invoked by some other method
		# that plays its own NSUndoManager games.
		undoManager.groupsByEvent = undoManagerWasGroupingByEvent

		# Done.
		@creatingGraphic = nil
	end

	def marqueeSelectWithEvent (event)
		# Dequeue and handle mouse events until the user lets go of the mouse button.
		oldSelectionIndexes = selectionIndexes
		originalMouseLocation = convertPoint(event.locationInWindow, fromView: nil)
		while event.type != NSLeftMouseUp
			event = window.nextEventMatchingMask(NSLeftMouseDraggedMask | NSLeftMouseUpMask)
			autoscroll(event)
			currentMouseLocation = convertPoint(event.locationInWindow, fromView: nil)

			# Figure out a new a selection rectangle based on the mouse location.
			newMarqueeSelectionBounds = NSMakeRect([originalMouseLocation.x, currentMouseLocation.x].min, 
													[originalMouseLocation.y, currentMouseLocation.y].min, 
													(currentMouseLocation.x - originalMouseLocation.x).abs,
												    (currentMouseLocation.y - originalMouseLocation.y).abs)
			if !NSEqualRects(newMarqueeSelectionBounds, @marqueeSelectionBounds)
				# Erase the old selection rectangle and draw the new one.
				setNeedsDisplayInRect(@marqueeSelectionBounds)
				@marqueeSelectionBounds = newMarqueeSelectionBounds
				setNeedsDisplayInRect(@marqueeSelectionBounds)

				# Either select or deselect all of the graphics that intersect the
				# selection rectangle.
				indexesOfGraphicsInRubberBand = indexesOfGraphicsIntersectingRect(@marqueeSelectionBounds)
				newSelectionIndexes = oldSelectionIndexes.mutableCopy
				
				# TODO extend NSIndexSet with an each method
				index = indexesOfGraphicsInRubberBand.firstIndex
				while index != NSNotFound
					if newSelectionIndexes.containsIndex(index)
						newSelectionIndexes.removeIndex(index)
					else
						newSelectionIndexes.addIndex(index)
					end
					index = indexesOfGraphicsInRubberBand.indexGreaterThanIndex(index)
				end
				changeSelectionIndexes(newSelectionIndexes)
			end
		end

		# Schedule the drawing of the place wherew the rubber band isn't anymore.
		setNeedsDisplayInRect(@marqueeSelectionBounds)

		# Make it not there.
		@marqueeSelectionBounds = NSZeroRect
	end

	def selectAndTrackMouseWithEvent (event)
		# Are we changing the existing selection instead of setting a new one?
		modifyingExistingSelection = (event.modifierFlags & NSShiftKeyMask) == NSShiftKeyMask
		# Has the user clicked on a graphic?
		mouseLocation = convertPoint(event.locationInWindow, fromView: nil)
		clickedGraphic = graphicUnderPoint(mouseLocation)
		if clickedGraphic.graphic
			# Clicking on a graphic knob takes precedence.
			if clickedGraphic.handle != SKTGraphicNoHandle
					# The user clicked on a graphic's handle. Let the user drag it around.
					resizeGraphic(clickedGraphic.graphic, usingHandle: clickedGraphic.handle, withEvent: event)
			else
				# The user clicked on a graphic's contents. Update the selection.
				if modifyingExistingSelection
					if clickedGraphic.isSelected
						# Remove the graphic from the selection.
						newSelectionIndexes = selectionIndexes.mutableCopy
						newSelectionIndexes.removeIndex(clickedGraphic.index)
						changeSelectionIndexes(newSelectionIndexes)
						clickedGraphic.isSelected = false
					else
						# Add the graphic to the selection.
						newSelectionIndexes = selectionIndexes.mutableCopy
						newSelectionIndexes.addIndex(clickedGraphic.index)
						changeSelectionIndexes(newSelectionIndexes)
						clickedGraphic.isSelected = true
					end
				else
					# If the graphic wasn't selected before then it is now, and none of the rest are.
					if !clickedGraphic.isSelected
						changeSelectionIndexes(NSIndexSet.indexSetWithIndex(clickedGraphic.index))
						clickedGraphic.isSelected = true
					end
				end
		
				# Is the graphic that the user has clicked on now selected?
				if clickedGraphic.isSelected
					# Yes. Let the user move all of the selected objects.
					moveSelectedGraphicsWithEvent(event)
				else
					# No. Just swallow mouse events until the user lets go of the mouse
					# button. We don't even bother autoscrolling here.
					while event.type != NSLeftMouseUp
						event = window.nextEventMatchingMask(NSLeftMouseDraggedMask | NSLeftMouseUpMask)
					end
				end
			end
		else
			# The user clicked somewhere other than on a graphic. Clear the selection,
			# unless the user is holding down the shift key.
			changeSelectionIndexes(NSIndexSet.indexSet) if !modifyingExistingSelection
	
			# The user clicked on a point where there is no graphic. Select and
			# deselect graphics until the user lets go of the mouse button.
			marqueeSelectWithEvent(event)
		end
	end


	# An override of the NSView method.
	def acceptsFirstMouse (event)
		# In general we don't want to make people click once to activate the
		# window then again to actually do something, but we do want to help users
		# not accidentally throw away the current selection, if there is one.
		selectionIndexes.count == 0
	end

	# An override of the NSResponder method.
	def mouseDown (event)
		# If a graphic has been being edited (in Sketch SKTTexts are the only ones
		# that are "editable" in this sense) then end editing.
		stopEditing

		# Is a tool other than the Selection tool selected?
		graphicClassToInstantiate = SKTToolPaletteController.sharedToolPaletteController.currentGraphicClass
		if graphicClassToInstantiate
			# Create a new graphic and then track to size it.
			createGraphicOfClass(graphicClassToInstantiate, withEvent: event)
		else
			# Double-clicking with the selection tool always means "start editing,"
			# or "do nothing" if no editable graphic is double-clicked on.
			doubleClickedGraphic = nil
			if event.clickCount > 1
				mouseLocation = convertPoint(event.locationInWindow, fromView: nil)
				doubleClickedGraphic = graphicUnderPoint(mouseLocation).graphic
				startEditingGraphic(doubleClickedGraphic) if doubleClickedGraphic
			end
			if !doubleClickedGraphic
				# Update the selection and/or move graphics or resize graphics.
				selectAndTrackMouseWithEvent(event)
			end
		end
	end
	
	# *** Keyboard Event Handling ***

	# An override of the NSResponder method. NSResponder's implementation
	# would just forward the message to the next responder (an NSClipView, in
	# Sketch's case) and our overrides like -delete: would never be invoked.
	def keyDown (event)
		# Ask the key binding manager to interpret the event for us.
		interpretKeyEvents([event])
	end

	def delete (sender)
		mutableGraphics.removeObjectsAtIndexes(selectionIndexes)
		undoManager.setActionName(NSLocalizedStringFromTable("Delete", "UndoStrings", "Action name for deletions."))
	end

	# Overrides of the NSResponder(NSStandardKeyBindingMethods) methods.
	def deleteBackward (sender)
		delete(sender)
	end
	
	def deleteForward (sender)
		delete(sender)
	end

	def invalidateHandlesOfGraphics (graphics)
		graphics.each {|g| setNeedsDisplayInRect(g.drawingBounds)}
	end
	
	def unhideHandlesForTimer (timer)
		@isHidingHandles = false
		@handleShowingTimer = nil
		setNeedsDisplayInRect(SKTGraphic.drawingBoundsOfGraphics(selectedGraphics))
	end

	def hideHandlesMomentarily ()
		@handleShowingTimer.invalidate if @handleShowingTimer
		@handleShowingTimer = NSTimer.scheduledTimerWithTimeInterval(0.5, target: self, selector: 'unhideHandlesForTimer:',
		 						userInfo: nil, repeats: false)
		@isHidingHandles = true
		setNeedsDisplayInRect(SKTGraphic.drawingBoundsOfGraphics(selectedGraphics))
	end


	def moveSelectedGraphicsByX (x, y: y)
		# Don't do anything if there's nothing to do.
		if selectedGraphics.count > 0
			# Don't draw and redraw the selection rectangles while the user holds
			# an arrow key to autorepeat.
			hideHandlesMomentarily

			# Move the selected graphics.
			SKTGraphic.translateGraphics(selectedGraphics, byX: x, y: y)

			# Overwrite whatever undo action name was registered during all of
			# that with a more specific one.
			undoManager.setActionName(NSLocalizedStringFromTable("Nudge", "UndoStrings", "Action name for nudge keyboard commands."))
		end
	end

	# Overrides of the NSResponder(NSStandardKeyBindingMethods) methods.
	def moveLeft (sender)
		moveSelectedGraphicsByX(-1.0, y: 0.0)
	end

	def moveRight (sender)
		moveSelectedGraphicsByX(1.0, y: 0.0)
	end

	def moveUp (sender)
		moveSelectedGraphicsByX(0.0, y: -1.0)
	end

	def moveDown (sender)
		moveSelectedGraphicsByX(0.0, y: 1.0)
	end

	# *** Copy and Paste ***
	def makeNewImageFromContentsOfFile(filename, atPoint: point)
		extension = filename.pathExtension
		if NSImage.imageFileTypes.index(extension)
			contents = NSImage.alloc.initWithContentsOfFile(filename)
			if contents
				newImage = SKTImage.alloc.initWithPosition(point, contents: contents)
				mutableGraphics.insertObject(newImage, atIndex: 0)
				changeSelectionIndexes(NSIndexSet.indexSetWithIndex(0))
				return true
			end
		end
		return false
	end

	def makeNewImageFromPasteboard (pboard, atPoint: point)
		type = pboard.availableTypeFromArray(NSImage.imagePasteboardTypes)
		if type
			contents = NSImage.alloc.initWithPasteboard(pboard)
			if contents
				imageOrigin = NSMakePoint(point.x, (point.y - contents.size.height))
				newImage = SKTImage.alloc.initWithPosition(imageOrigin, contents: contents)
				mutableGraphics.insertObject(newImage, atIndex: 0)
				changeSelectionIndexes(NSIndexSet.indexSetWithIndex(0))
				return true
			end
		end
		return false
	end

	def copy (sender)
		pasteboard = NSPasteboard.generalPasteboard
		pasteboard.declareTypes([SKTGraphicViewPasteboardType, NSPDFPboardType, NSTIFFPboardType], owner: nil)
		pasteboard.setData(SKTGraphic.pasteboardDataWithGraphics(selectedGraphics), forType: SKTGraphicViewPasteboardType)
		pasteboard.setData(SKTRenderingView.pdfDataWithGraphics(selectedGraphics), forType: NSPDFPboardType)
		pasteboard.setData(SKTRenderingView.tiffDataWithGraphics(selectedGraphics, error: nil), forType: NSTIFFPboardType)
		@pasteboardChangeCount = pasteboard.changeCount
		@pasteCascadeNumber = 1;
		@pasteCascadeDelta = NSMakePoint(SKTGraphicViewDefaultPasteCascadeDelta, SKTGraphicViewDefaultPasteCascadeDelta);
	end

	def cut (sender)
		copy(sender)
		delete(sender)
		undoManager.setActionName(NSLocalizedStringFromTable("Cut", "UndoStrings", "Action name for cut."))
	end

	def paste (sender)
		# We let the user paste graphics, image files, and image data.
		pasteboard = NSPasteboard.generalPasteboard
		typeName = pasteboard.availableTypeFromArray([SKTGraphicViewPasteboardType, NSFilenamesPboardType])
		if typeName == SKTGraphicViewPasteboardType
			# You can't trust anything that might have been put on the pasteboard by
			# another application, so be ready for +[SKTGraphic
			# graphicsWithPasteboardData:error:] to fail and return nil.
			error = Pointer.new_with_type('@')
			graphics = SKTGraphic.graphicsWithPasteboardData(pasteboard.dataForType(typeName), error: error)
			if graphics
				# Should we reset the cascading of pasted graphics?
				pasteboardChangeCount = pasteboard.changeCount
				if @pasteboardChangeCount != pasteboardChangeCount
					@pasteboardChangeCount = pasteboardChangeCount
					@pasteCascadeNumber = 0;
					@pasteCascadeDelta = NSMakePoint(SKTGraphicViewDefaultPasteCascadeDelta, SKTGraphicViewDefaultPasteCascadeDelta)
				end
				# An empty array doesn't signal an error, but it's still not useful to
				# paste it.
				graphicCount = graphics.count
				if graphicCount > 0
					# If this is a repetitive paste, or a paste of something that was just
					# copied from this same view, then offset the graphics by a little
					# bit.
					if @pasteCascadeNumber > 0
						SKTGraphic.translateGraphics(graphics, byX: (@pasteCascadeNumber * @pasteCascadeDelta.x), y: (@pasteCascadeNumber * @pasteCascadeDelta.y))
					end
					@pasteCascadeNumber += 1;

					# Add the pasted graphics in front of all others and select them.
					insertionIndexes = NSIndexSet.indexSetWithIndexesInRange(NSMakeRange(0, graphicCount))
					mutableGraphics.insertObjects(graphics, atIndexes: insertionIndexes)
					changeSelectionIndexes(insertionIndexes)

					# Override any undo action name that might have been set with one that
					# is more specific to this operation.
					undoManager.setActionName(NSLocalizedStringFromTable("Paste", "UndoStrings", "Action name for paste."))
				end
			else
				# Something went wrong? Present the error to the user in a sheet. It was
				# entirely +[SKTGraphic graphicsWithPasteboardData:error:]'s
				# responsibility to set the error to something when it returned nil. It
				# was also entirely responsible for not crashing if we had passed in
				# error:NULL.
				presentError(error[0], modalForWindow: window, delegate: nil, didPresentSelector: nil, contextInfo: nil)
			end
		elsif typeName == NSFilenamesPboardType
			filenames = pasteboard.propertyListForType(NSFilenamesPboardType)
			if filenames.count == 1
				filename = filenames[0]
				if makeNewImageFromContentsOfFile(filename, atPoint: NSMakePoint(50, 50))
					undoManager.setActionName(NSLocalizedStringFromTable("Paste", "UndoStrings", "Action name for paste."))
				end
			end
		elsif makeNewImageFromPasteboard(pasteboard, atPoint: NSMakePoint(50, 50))
			undoManager.setActionName(NSLocalizedStringFromTable("Paste", "UndoStrings", "Action name for paste."))
		end
	end


	# *** Drag and Drop ***
	def dragOperationForDraggingInfo (sender)
		pboard = sender.draggingPasteboard
		type = pboard.availableTypeFromArray([NSColorPboardType, NSFilenamesPboardType])
		if type
			if type == NSColorPboardType
				point = convertPoint(sender.draggingLocation, fromView: nil)
				return NSDragOperationGeneric if graphicUnderPoint(point).graphic
			end
			return NSDragOperationCopy if type == NSFilenamesPboardType
		end
	
		return NSDragOperationCopy if pboard.availableTypeFromArray(NSImage.imagePasteboardTypes)
		return NSDragOperationNone
	end

	# Conformance to the NSObject(NSDraggingDestination) informal protocol.
	def draggingEntered (sender)
		dragOperationForDraggingInfo(sender)
	end

	def draggingUpdated (sender)
		dragOperationForDraggingInfo(sender)
	end
	
	def draggingExited (sender)
	end

	def prepareForDragOperation (sender)
		true
	end
	
	def performDragOperation (sender)
		true
	end
	
	def concludeDragOperation (sender)
		pboard = sender.draggingPasteboard
		type = pboard.availableTypeFromArray([NSColorPboardType, NSFilenamesPboardType])
		point = convertPoint(sender.draggingLocation, fromView: nil)
		draggedImageLocation = convertPoint(sender.draggedImageLocation, fromView: nil)
		if type
			if type == NSColorPboardType
				hitGraphic = graphicUnderPoint(point).graphic
				if hitGraphic
					color = NSColor.colorFromPasteboard(pboard, colorWithAlphaComponent: 1.0)
					hitGraphic.color = color
				end
			elsif type == NSFilenamesPboardType
				filenames = pboard.propertyListForType(NSFilenamesPboardType)
				# Handle multiple files (cascade them?)
				if filenames.count == 1
					makeNewImageFromContentsOfFile(filenames[0], atPoint: point)
				end
			end
			return
		end
	
		makeNewImageFromPasteboard(pboard, atPoint: draggedImageLocation)
	end

	# *** Other View Customization ***
	# An override of the NSResponder method.
	def acceptsFirstResponder ()
		# This view can of course handle lots of action messages.
		return true
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

	# Conformance to the NSObject(NSMenuValidation) informal protocol.
	def validateMenuItem (item)
		case item.action
			when :'makeNaturalSize:'
				# Return YES if we have at least one selected graphic that has a natural size.
				return selectedGraphics.any? {|g| g.canMakeNaturalSize}
			when :'alignWithGrid:'
				return false if !@grid.canAlign
				# Only apply if there is a selection
				return selectedGraphics.count > 0
				
			when :'delete:', :'bringToFront:', :'sendToBack:', :'cut:', :'copy:'
				# These only apply if there is a selection
				return selectedGraphics.count > 0
	 		when :'alignLeftEdges:', :'alignRightEdges:', :'alignTopEdges:',  :'alignBottomEdges:',  :'alignHorizontalCenters:', 
				:'alignVerticalCenters:', :'makeSameWidth:', :'makeSameHeight:'
				# These only apply to multiple selection
				return selectedGraphics.count > 1
			when :'undo:', :'redo:'
				# Because we implement -undo: and redo: action methods we must
				# validate the actions too. Messaging the window directly like this is
				# not strictly correct, because there may be other responders in the
				# chain between this view and the window (superviews maybe?) that want
				# control over undoing and redoing, but there's no AppKit method we
				# can invoke to simply find the next responder that responds to -undo:
				# and -redo:.
				return window.validateMenuItem(item)
			when :'showOrHideRulers:'
				# The Show/Hide Ruler menu item is always enabled, but we have to set
				# its title.
				title = enclosingScrollView.rulersVisible ? 'Hide Ruler' : 'Show Ruler'
				item.setTitle(NSLocalizedStringFromTable(title, "SKTGraphicView", "A main menu item title.")) 
				return true
			else
				return true
		end
	end

	# An action method that isn't declared in any AppKit header, despite the
	# fact that NSWindow implements it. Because this is here we have to handle
	# the action in our override of -validateMenuItem:, and we do.
	def undo (sender)
		# Applications are supposed to update the selection during undo and redo
		# operations. Start keeping track of which graphics are added or changed
		# during this operation so we can select them afterward. We don't do have
		# to do anything when graphics are removed because the bound-to array
		# controller keeps the selection indexes consistent when that happens.
		# (This is the one place where SKTGraphicView assumes anything about the
		# class of an object to which its bound, and it's not really assuming that
		# it's bound to an array controller. It's just assuming that the bound-to
		# object is somehow keeping the bound-to indexes property consistent with
		# the bound-to graphics.)
		@undoSelectionIndexes = NSMutableIndexSet.alloc.init

		# Do the regular Cocoa thing. Unfortunately, before you saw this there was
		# no easy way for you know what "the regular Cocoa thing" is, but now you
		# know: NSWindow has -undo: and -redo: methods, and is usually the object
		# in the responder chain that performs these actions when the user chooses
		# the corresponding items in the Edit menu. It would be more correct to
		# write this as [[self nextResponder] tryToPerform:_cmd with:sender],
		# because perhaps someday this class will be reused in a situation where
		# the superview has opinions of its own about what should be done during
		# undoing. We message the window directly just to be consistent with what
		# we do in our implementation of -validateMenuItem:, where we have no
		# choice.
		window.undo(sender)

		# Were graphics added or changed by undoing?
		if @undoSelectionIndexes.count > 0
			# Yes, so replace the current selection with them.
			changeSelectionIndexes(@undoSelectionIndexes)
		end
		# else apparently nothing happening while undoing except maybe the removal
		# of graphics, so we leave the selection alone.

		# Don't leak, and don't let -observeValueForKeyPath:ofObject:change:context: message a zombie.
		@undoSelectionIndexes = nil

		# We overrode this method to find out when undoing is done, instead of
		# observing NSUndoManagerWillUndoChangeNotification and
		# NSUndoManagerDidUndoChangeNotification, because we only want to do what
		# we do here when the user is focused on this view, and those
		# notifications won't tell us the focused view. In Sketch this matters
		# when the user has more than one window open for a document, but the
		# concept applies whenever there are multiple views of the same data. Most
		# of the time actions taken by the user in a view shouldn't affect the
		# selection used in other views of the same data, with the obvious
		# exception that removed items can no longer be selected in any view.
	end

	# The same as above, but for redoing instead of undoing. It doesn't look like so much work when you leave out the comments!
	def redo (sender)
		@undoSelectionIndexes = NSMutableIndexSet.alloc.init
		window.redo(sender)
		changeSelectionIndexes(@undoSelectionIndexes) if @undoSelectionIndexes.count > 0
		@undoSelectionIndexes = nil
	end


	# *** Other Actions ***
	def alignLeftEdges (sender)
		leftX = selectedGraphics[0].bounds.origin.x
		# would be easier to just to selectedGraphics.each {|g| g.bounds.origin.x = leftX}
		# but don't know if supported and might dirty state unnecessary 
		selectedGraphics.each do |g|
			curBounds = g.bounds.dup
			if curBounds.origin.x != leftX
				curBounds.origin.x = leftX
				g.setBounds(curBounds)
			end
		end
		undoManager.setActionName(NSLocalizedStringFromTable("Align Left Edges", "UndoStrings", "Action name for align left edges."))
	end

	def alignRightEdges (sender)
		rightX = NSMaxX(selectedGraphics[0].bounds)
		selectedGraphics.each do |g|
			curBounds = g.bounds.dup
			if NSMaxX(curBounds) != rightX
				curBounds.origin.x = rightX - curBounds.size.width
				g.setBounds(curBounds)
			end
		end
		undoManager.setActionName(NSLocalizedStringFromTable("Align Right Edges", "UndoStrings", "Action name for align right edges."))
	end

	def alignTopEdges (sender)
		topY = selectedGraphics[0].bounds.origin.y
		# would be easier to just to selectedGraphics.each {|g| g.bounds.origin.x = leftX}
		# but don't know if supported and might dirty state unnecessary 
		selectedGraphics.each do |g|
			curBounds = g.bounds.dup
			if curBounds.origin.y != topY
				curBounds.origin.y = topY
				g.setBounds(curBounds)
			end
		end
		undoManager.setActionName(NSLocalizedStringFromTable("Align Top Edges", "UndoStrings", "Action name for align top edges."))
	end

	def alignBottomEdges (sender)
		bottomY = NSMaxY(selectedGraphics[0].bounds)
		selectedGraphics.each do |g|
			curBounds = g.bounds.dup
			if NSMaxY(curBounds) != bottomY
				curBounds.origin.y = bottomY - curBounds.size.height
				g.setBounds(curBounds)
			end
		end
		undoManager.setActionName(NSLocalizedStringFromTable("Align Bottom Edges", "UndoStrings", "Action name for bottom edges."))
	end


	def alignHorizontalCenters (sender)
		hCenter = NSMidX(selectedGraphics[0].bounds)
		selectedGraphics.each do |g|
			curBounds = g.bounds.dup
			if NSMidX(curBounds) != hCenter
				curBounds.origin.x = hCenter - (curBounds.size.width / 2.0)
				g.setBounds(curBounds)
			end
		end
		undoManager.setActionName(NSLocalizedStringFromTable("Align Horizontal Centers", "UndoStrings", "Action name for align horizontal centers."))
	end

	def alignVerticalCenters (sender)
		vCenter = NSMidY(selectedGraphics[0].bounds)
		selectedGraphics.each do |g|
			curBounds = g.bounds.dup
			if NSMidX(curBounds) != vCenter
				curBounds.origin.y = vCenter - (curBounds.size.height / 2.0)
				g.setBounds(curBounds)
			end
		end
		undoManager.setActionName(NSLocalizedStringFromTable("Align Vertical Centers", "UndoStrings", "Action name for align vertical centers."))
	end

	def alignWithGrid (sender)
		selectedGraphics.each {|g| g.setBounds(@grid.alignedRect(g.bounds.dup))}
		undoManager.setActionName(NSLocalizedStringFromTable("Grid Selected Graphics", "UndoStrings", "Action name for grid selected graphics."))
	end

	def bringToFront (sender)
		selectedObjects = selectedGraphics.dup
		if selectionIndexes.count > 0
			mutableGraphics.removeObjectsAtIndexes(selectionIndexes)
			insertionIndexes = NSIndexSet.indexSetWithIndexesInRange(NSMakeRange(0, selectedObjects.count))
			mutableGraphics.insertObjects(selectedObjects, atIndexes: insertionIndexes)
			changeSelectionIndexes(insertionIndexes)
			undoManager.setActionName(NSLocalizedStringFromTable("Bring To Front", "UndoStrings", "Action name for bring to front."))
		end
	end


	def sendToBack (sender)
		selectedObjects = selectedGraphics.dup
		if selectionIndexes.count > 0
			mutableGraphics.removeObjectsAtIndexes(selectionIndexes)
			insertionIndexes = NSIndexSet.indexSetWithIndexesInRange(NSMakeRange(mutableGraphics.count, selectedObjects.count))
			mutableGraphics.insertObjects(selectedObjects, atIndexes: insertionIndexes)
			changeSelectionIndexes(insertionIndexes)
			undoManager.setActionName(NSLocalizedStringFromTable("Send To Back", "UndoStrings", "Action name for send to back."))
		end
	end

	# Conformance to the NSObject(NSColorPanelResponderMethod) informal protocol.
	def changeColor (sender)
		# Change the color of every selected graphic.
		selectedGraphics.makeObjectsPerformSelector('setColor:', withObject: sender.color)
	end


	def makeSameWidth (sender)
		width = selectedGraphics[0].bounds.size.width
		selectedGraphics.each do |g|
			curBounds = g.bounds.dup
			if curBounds.size.width != width
				curBounds.size.width = width
				g.setBounds(curBounds)
			end
		end

		undoManager.setActionName(NSLocalizedStringFromTable("Make Same Width", "UndoStrings", "Action name for make same width."))
	end

	def makeSameHeight (sender)
		height = selectedGraphics[0].bounds.size.height
		selectedGraphics.each do |g|
			curBounds = g.bounds.dup
			if curBounds.size.height != height
				curBounds.size.height = height
				g.setBounds(curBounds)
			end
		end

		undoManager.setActionName(NSLocalizedStringFromTable("Make Same Height", "UndoStrings", "Action name for make same height."))
	end
	
	def makeNaturalSize (sender)
		if selectedGraphics.count > 0
			selectedGraphics.makeObjectsPerformSelector('makeNaturalSize')
			undoManager.setActionName(NSLocalizedStringFromTable("Make Natural Size", "UndoStrings", "Action name for natural size."))
		end
	end


	# An override of an NSResponder(NSStandardKeyBindingMethods) method and a
	# matching method of our own.
	def selectAll (sender)
		changeSelectionIndexes(NSIndexSet.indexSetWithIndexesInRange(NSMakeRange(0, graphics.count)))
	end
	
	def deselectAll (sender)
		changeSelectionIndexes(NSIndexSet.indexSet)
	end

	# See the comment in the header about why we're not using -toggleRuler:.
	def showOrHideRulers (sender)
		enclosingScrollView.rulersVisible = !enclosingScrollView.rulersVisible
	end
end

=begin

Derived from Apple's sample code for Sketch and converted to MacRuby by Dave Baldwin.

Additional comments from corresponding header file:

Information that is recorded when the "graphics" and "selectionIndexes" bindings are established. Notice that we don't keep around copies of the actual graphics array and selection indexes. Those would just be unnecessary (as far as we know, so far, without having ever done any relevant performance measurement) caches of values that really live in the bound-to objects.

    NSObject *_graphicsContainer;
    NSString *_graphicsKeyPath;
    NSObject *_selectionIndexesContainer;
    NSString *_selectionIndexesKeyPath;

The grid that is drawn in the view and used to constrain graphics as they're created and moved. In Sketch this is just a cache of a value that canonically lives in the SKTWindowController to which this view's grid property is bound (see SKTWindowController's comments for an explanation of why the grid lives there).

    SKTGrid *_grid;

The bounds of moved objects that is echoed in the ruler, if objects are being moved right now.
    NSRect _rulerEchoedBounds;

The graphic that is being created right now, if a graphic is being created right now (not explicitly retained, because it's always allocated and forgotten about in the same method).
    SKTGraphic *_creatingGraphic;

The graphic that is being edited right now, the view that it gave us to present its editing interface, and the last known frame of that view, if a graphic is being edited right now. We have to record the editing view frame because when it changes we need its old value, and the old value isn't available when this view gets the NSViewFrameDidChangeNotification. Also, the reserved thickness for the horizontal ruler accessory view before editing began, so we can restore it after editing is done. (We could do the same for the vertical ruler, but so far in Sketch there are no vertical ruler accessory views.)
    SKTGraphic *_editingGraphic;
    NSView *_editingView;
    NSRect _editingViewFrame;
    CGFloat _oldReservedThicknessForRulerAccessoryView;

The bounds of the marquee selection, if marquee selection is being done right now, NSZeroRect otherwise.
    NSRect _marqueeSelectionBounds;

Whether or not selection handles are being hidden while the user moves graphics.
    BOOL _isHidingHandles;

Sometimes we temporarily hide the selection handles when the user moves graphics using the keyboard. When we do that this is the timer to start showing them again.
    NSTimer *_handleShowingTimer;

    // The state of the cascading of graphics that we do during repeated pastes.
    NSInteger _pasteboardChangeCount;
    NSInteger _pasteCascadeNumber;
    NSPoint _pasteCascadeDelta;

Applications are supposed to update the selection during undo and redo operations. These are the indexes of the graphics that are going to be selected at the end of an undo or redo operation.
    NSMutableIndexSet *_undoSelectionIndexes;

Action methods that are unique to SKTGraphicView, or at least are not declared by NSResponder. SKTGraphicView implements other action methods, but they're all declared by NSResponder and there's not much reason to redeclare them here. We use -showOrHideRulers: instead of -toggleRuler: because we don't want to cause accidental invocation of -[NSTextView toggleRuler:], which doesn't quite work when the text view has been added to a view that already has rulers shown in it, a situation that can arise in Sketch.
- (IBAction)alignBottomEdges:(id)sender;
- (IBAction)alignHorizontalCenters:(id)sender;
- (IBAction)alignLeftEdges:(id)sender;
- (IBAction)alignRightEdges:(id)sender;
- (IBAction)alignTopEdges:(id)sender;
- (IBAction)alignVerticalCenters:(id)sender;
- (IBAction)alignWithGrid:(id)sender;
- (IBAction)bringToFront:(id)sender;
- (IBAction)copy:(id)sender;
- (IBAction)cut:(id)sender;
- (IBAction)delete:(id)sender;
- (IBAction)deselectAll:(id)sender;
- (IBAction)makeNaturalSize:(id)sender;
- (IBAction)makeSameHeight:(id)sender;
- (IBAction)makeSameWidth:(id)sender;
- (IBAction)paste:(id)sender;
- (IBAction)sendToBack:(id)sender;
- (IBAction)showOrHideRulers:(id)sender;


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