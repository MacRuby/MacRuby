# String constants declared in the header. A lot of them aren't used by any
# other class in the project, but it's a good idea to provide and use them, if
# only to help prevent typos in source code. 
SKTGraphicCanSetDrawingFillKey = "canSetDrawingFill"
SKTGraphicCanSetDrawingStrokeKey = "canSetDrawingStroke"
SKTGraphicDrawingFillKey = "drawingFill"
SKTGraphicFillColorKey = "fillColor"
SKTGraphicDrawingStrokeKey = "drawingStroke"
SKTGraphicStrokeColorKey = "strokeColor"
SKTGraphicStrokeWidthKey = "strokeWidth"
SKTGraphicXPositionKey = "xPosition"
SKTGraphicYPositionKey = "yPosition"
SKTGraphicWidthKey = "width"
SKTGraphicHeightKey = "height"
SKTGraphicBoundsKey = "bounds"
SKTGraphicDrawingBoundsKey = "drawingBounds"
SKTGraphicDrawingContentsKey = "drawingContents"
SKTGraphicKeysForValuesToObserveForUndoKey = "keysForValuesToObserveForUndo"

# Another constant that's declared in the header.
SKTGraphicNoHandle = 0

# A key that's used in Sketch's property-list-based file and pasteboard formats.
SKTGraphicClassKey = "className"

# The values that might be returned by -[SKTGraphic creationSizingHandle] and
# -[SKTGraphic handleUnderPoint:], and that are understood by -[SKTGraphic
# resizeByMovingHandle:toPoint:]. We provide specific indexes in this
# enumeration so make sure none of them are zero (that's SKTGraphicNoHandle)
# and to make sure the flipping arrays in -[SKTGraphic
# resizeByMovingHandle:toPoint:] work.
SKTGraphicUpperLeftHandle   = 1
SKTGraphicUpperMiddleHandle = 2
SKTGraphicUpperRightHandle  = 3
SKTGraphicMiddleLeftHandle  = 4
SKTGraphicMiddleRightHandle = 5
SKTGraphicLowerLeftHandle   = 6
SKTGraphicLowerMiddleHandle = 7
SKTGraphicLowerRightHandle  = 8


# The handles that graphics draw on themselves are 6 point by 6 point rectangles.
SKTGraphicHandleWidth = 6.0
SKTGraphicHandleHalfWidth = 6.0 / 2.0

class SKTGraphic

	# Make available for KVO otherwise will not be found.
	attr_accessor	:drawingFill, :fillColor, :drawingStroke, :strokeColor, :strokeWidth
	attr_reader		:bounds
	
	# An override of the superclass' designated initializer.
	def init ()
		super

		# Set up decent defaults for a new graphic.
		@bounds = NSZeroRect
		@drawingFill = false
		@fillColor = NSColor.whiteColor
		@drawingStroke = true
		@strokeColor = NSColor.blackColor
		@strokeWidth = 1.0
		return self;
	end

	# *** Private KVC-Compliance for Public Properties ***

	# An override of the NSObject(NSKeyValueObservingCustomization) method.
	def self.automaticallyNotifiesObserversForKey (key)
		# We don't want KVO autonotification for these properties. Because the
		# setters for all of them invoke -setBounds:, and this class is
		# KVO-compliant for "bounds," and we declared that the values of these
		# properties depend on "bounds," we would up end up with double
		# notifications for them. That would probably be unnoticable, but it's a
		# little wasteful. Something you have to think about with codependent
		# mutable properties like these (regardless of what notification mechanism
		# you're using).
		[SKTGraphicXPositionKey, SKTGraphicYPositionKey, SKTGraphicWidthKey, SKTGraphicHeightKey].index(key) || super
	end

	# In Mac OS 10.5 and newer KVO's dependency mechanism invokes class methods
	# to find out what properties affect properties being observed, like these.
	def self.keyPathsForValuesAffectingXPosition ()
		NSSet.setWithObject(SKTGraphicBoundsKey)
	end

	def self.keyPathsForValuesAffectingYPosition ()
		NSSet.setWithObject(SKTGraphicBoundsKey)
	end
	
	def self.keyPathsForValuesAffectingWidth ()
		NSSet.setWithObject(SKTGraphicBoundsKey)
	end
	
	def self.keyPathsForValuesAffectingHeight ()
		NSSet.setWithObject(SKTGraphicBoundsKey)
	end
	
	def xPosition
		bounds.origin.x
	end
	
	def yPosition
		bounds.origin.y
	end
	
	def width 
		bounds.size.width
	end
	
	def height
		bounds.size.height
	end
	
	# We cannot just set the bounds component directly as this will not trigger
	# KVO of bounds.	Also must also edit a new version of the bounds otherwise
	# undo will not have old and new versions.
	def setXPosition (xPosition)
		b = bounds.dup
		b.origin.x = xPosition
		setBounds(b)
	end
	
	def setYPosition (yPosition)
		b = bounds.dup
		b.origin.y = yPosition
		setBounds(b)
	end

	def setWidth (width)
		b = bounds.dup
		b.size.width = width
		setBounds(b)
	end
	
	def setHeight (height)
		b = bounds.dup
		b.size.height = height
		setBounds(b)
	end

	# *** Convenience ***

	def self.boundsOfGraphics (graphics)
		# The bounds of an array of graphics is the union of all of their bounds.
		b = NSZeroRect;
		graphics.each {|g| b = NSUnionRect(b, g.bounds)}
		b
	end

	def self.drawingBoundsOfGraphics (graphics)
		# The drawing bounds of an array of graphics is the union of all of their drawing bounds.
		b = NSZeroRect;
		graphics.each {|g| b = NSUnionRect(b, g.drawingBounds)}
		b
	end

	def self.translateGraphics(graphics, byX: deltaX, y: deltaY)
		graphics.each {|g| g.setBounds(NSOffsetRect(g.bounds, deltaX, deltaY))}
	end


	# *** Persistence ***
	def self.graphicsWithPasteboardData (data, error: outError)
		# Because this data may have come from outside this process, don't assume
		# that any property list object we get back is the right type.
		graphics = nil;
		propertiesArray = NSPropertyListSerialization.propertyListWithData(data, options: 0,  format: nil, error: nil)
		propertiesArray = nil if !propertiesArray.kind_of? NSArray
		if propertiesArray
			# Convert the array of graphic property dictionaries into an array of graphics.
			graphics = graphicsWithProperties(propertiesArray)
		elsif outError
			# If property list parsing fails we have no choice but to admit that we
			# don't know what went wrong. The error description returned by
			# +[NSPropertyListSerialization
			# propertyListFromData:mutabilityOption:format:errorDescription:] would be
			# pretty technical, and not the sort of thing that we should show to a
			# user.
			outError[0] = SKTErrorWithCode(SKTUnknownPasteboardReadError)
		end
		return graphics;
	end


	def self.graphicsWithProperties (propertiesArray)
		# Convert the array of graphic property dictionaries into an array of
		# graphics. Again, don't assume that property list objects are the right
		# type.
		graphics = []
		propertiesArray.each do |properties|
			if properties.kind_of? Hash
				# Figure out the class of graphic to instantiate. The value of the
				# SKTGraphicClassKey entry must be an Objective-C class name. Don't
				# trust the type of something you get out of a property list unless you
				# know your process created it or it was read from your application or
				# framework's resources.
				className = properties[SKTGraphicClassKey]
				klass = NSClassFromString(className)
				if klass
					# Create a new graphic. If it doesn't work then just do nothing. We
					# could return an NSError, but doing things this way 1) means that a
					# user might be able to rescue graphics from a partially corrupted
					# document, and 2) is easier.
					graphics << klass.alloc.initWithProperties(properties)
				end
			end
		end
		return graphics
	end


	# NSPropertyListBinaryFormat_v1_0
	def self.pasteboardDataWithGraphics (graphics)
		# Convert the contents of the document to a property list and then flatten
		# the property list.
		NSPropertyListSerialization.dataWithPropertyList(propertiesWithGraphics(graphics), format: NSPropertyListXMLFormat_v1_0, options: 0, error: nil)
	end

	def self.propertiesWithGraphics (graphics)
		# Convert the array of graphics dictionaries into an array of graphic
		# property dictionaries.
		graphics.map do |graphic|
			# Get the properties of the graphic, add the class name that can be used
			# by +graphicsWithProperties: to it, and add the properties to the array
			# we're building.
			# In Ruby just use the class constant and not the name string.
			properties = graphic.properties
			properties[SKTGraphicClassKey] = NSStringFromClass(graphic.class).sub(/NSKVONotifying_/, '')
			properties
		end
	end


	def initWithProperties (properties)
		init

		# The dictionary entries are all instances of the classes that can be
		# written in property lists. Don't trust the type of something you get out
		# of a property list unless you know your process created it or it was
		# read from your application or framework's resources. We don't have to
		# worry about KVO-compliance in initializers like this by the way; no one
		# should be observing an unitialized object.
		boundsString = properties[SKTGraphicBoundsKey]
		@bounds = NSRectFromString(boundsString) if boundsString.kind_of? NSString
			
		fill = properties[SKTGraphicDrawingFillKey]
		@drawingFill = fill if fill.kind_of?(TrueClass) || fill.kind_of?(FalseClass)
		
		fillColorData = properties[SKTGraphicFillColorKey]
		@fillColor = NSUnarchiver.unarchiveObjectWithData(fillColorData) if fillColorData.kind_of? NSData
		
		stroke = properties[SKTGraphicDrawingStrokeKey]
		@drawingStroke = stroke if stroke.kind_of?(TrueClass) || stroke.kind_of?(FalseClass)

		strokeColorData = properties[SKTGraphicStrokeColorKey]
		@strokeColor = NSUnarchiver.unarchiveObjectWithData(strokeColorData) if strokeColorData.kind_of? NSData

		strokeWidthNumber = properties[SKTGraphicStrokeWidthKey]
		@strokeWidth = strokeWidthNumber.doubleValue
		self
	end

	def properties
		# Return a dictionary that contains nothing but values that can be written
		# in property lists.
		properties = {}
		properties[SKTGraphicBoundsKey] = NSStringFromRect(bounds)
		properties[SKTGraphicDrawingFillKey] = NSNumber.numberWithBool(drawingFill)
		properties[SKTGraphicFillColorKey] = NSArchiver.archivedDataWithRootObject(fillColor) if fillColor
		properties[SKTGraphicDrawingStrokeKey] = NSNumber.numberWithBool(drawingStroke)
		properties[SKTGraphicStrokeColorKey] = NSArchiver.archivedDataWithRootObject(strokeColor) if strokeColor
		properties[SKTGraphicStrokeWidthKey] = NSNumber.numberWithDouble(strokeWidth)
		properties
	end


	# *** Drawing ***

	def self.keyPathsForValuesAffectingDrawingBounds ()
		# The only properties managed by SKTGraphic that affect the drawing bounds
		# are the bounds and the the stroke width.
		NSSet.setWithArray([SKTGraphicBoundsKey, SKTGraphicStrokeWidthKey])
	end


	def self.keyPathsForValuesAffectingDrawingContents ()
		# The only properties managed by SKTGraphic that affect drawing but not
		# the drawing bounds are the fill and stroke parameters.
		NSSet.setWithArray([SKTGraphicDrawingFillKey, SKTGraphicFillColorKey, SKTGraphicDrawingStrokeKey, SKTGraphicStrokeColorKey])
	end

	def drawingBounds ()
		# Assume that -[SKTGraphic drawContentsInView:] and -[SKTGraphic
		# drawHandlesInView:] will be doing the drawing. Start with the plain
		# bounds of the graphic, then take drawing of handles at the corners of
		# the bounds into account, then optional stroke drawing.
		outset = SKTGraphicHandleHalfWidth
		if drawingStroke
			strokeOutset = strokeWidth / 2.0
			if strokeOutset > outset
			 	outset = strokeOutset
			end
		end
		
		inset = 0.0 - outset
		drawBounds = NSInsetRect(bounds, inset, inset)
	
		# -drawHandleInView:atPoint: draws a one-unit drop shadow too.
		drawBounds.size.width += 1.0
		drawBounds.size.height += 1.0
		return drawBounds
	end

	def drawContentsInView (view, isBeingCreateOrEdited: isBeingCreatedOrEditing)
		# If the graphic is so so simple that it can be boiled down to a bezier
		# path then just draw a bezier path. It's -bezierPathForDrawing's
		# responsibility to return a path with the current stroke width.
		path = bezierPathForDrawing
		if path
			if drawingFill
				fillColor.set
				path.fill
			end
			if drawingStroke
				strokeColor.set
				path.stroke
			end
		end
	end

	def bezierPathForDrawing ()
		# Live to be overriden.
		raise "Neither -drawContentsInView: nor -bezierPathForDrawing has been overridden."
	end


	def drawHandlesInView (view)
		# Draw handles at the corners and on the sides.
		drawHandleInView(view, atPoint: NSMakePoint(NSMinX(bounds), NSMinY(bounds)))
		drawHandleInView(view, atPoint: NSMakePoint(NSMidX(bounds), NSMinY(bounds)))
		drawHandleInView(view, atPoint: NSMakePoint(NSMaxX(bounds), NSMinY(bounds)))
		drawHandleInView(view, atPoint: NSMakePoint(NSMinX(bounds), NSMidY(bounds)))
		drawHandleInView(view, atPoint: NSMakePoint(NSMaxX(bounds), NSMidY(bounds)))
		drawHandleInView(view, atPoint: NSMakePoint(NSMinX(bounds), NSMaxY(bounds)))
		drawHandleInView(view, atPoint: NSMakePoint(NSMidX(bounds), NSMaxY(bounds)))
		drawHandleInView(view, atPoint: NSMakePoint(NSMaxX(bounds), NSMaxY(bounds)))
	end

	def drawHandleInView (view, atPoint: point)
		# Figure out a rectangle that's centered on the point but lined up with device pixels.
		handleBounds= NSRect.new
		handleBounds.origin.x = point.x - SKTGraphicHandleHalfWidth
		handleBounds.origin.y = point.y - SKTGraphicHandleHalfWidth
		handleBounds.size.width = SKTGraphicHandleWidth
		handleBounds.size.height = SKTGraphicHandleWidth
		handleBounds = view.centerScanRect(handleBounds)
	
		# Draw the shadow of the handle.
		handleShadowBounds = NSOffsetRect(handleBounds, 1.0, 1.0)
		NSColor.controlDarkShadowColor.set
		NSRectFill(handleShadowBounds)

		# Draw the handle itself.
		NSColor.knobColor.set
		NSRectFill(handleBounds)
	end

	# *** Editing ***
	@@crosshairsCursor = nil
	def self.creationCursor ()
		# By default we use the crosshairs cursor.
		if !@@crosshairsCursor
			crosshairsImage = NSImage.imageNamed("Cross")
			crosshairsImageSize = crosshairsImage.size
			@@crosshairsCursor = NSCursor.alloc.initWithImage(crosshairsImage,  	
											hotSpot: NSMakePoint((crosshairsImageSize.width / 2.0), (crosshairsImageSize.height / 2.0)))
		end
		@@crosshairsCursor
	end

	def self.creationSizingHandle ()
		# Return the number of the handle for the lower-right corner. If the user
		# drags it so that it's no longer in the lower-right,
		# -resizeByMovingHandle:toPoint: will deal with it.
		SKTGraphicLowerRightHandle
	end

	def canSetDrawingFill ()
		# The default implementation of -drawContentsInView: can draw fills.
		return true
	end

	def canSetDrawingStroke ()
		# The default implementation of -drawContentsInView: can draw strokes.
		return true
	end

	def canMakeNaturalSize ()
		# Only return YES if -makeNaturalSize would actually do something.
		return bounds.size.width != bounds.size.height
	end

	def isContentsUnderPoint (point)
		# Just check against the graphic's bounds.
		return NSPointInRect(point, bounds)
	end

	def handleUnderPoint (point)
		# Check handles at the corners and on the sides.
		if isHandleAtPoint(NSMakePoint(NSMinX(bounds), NSMinY(bounds)), underPoint: point)
			return SKTGraphicUpperLeftHandle
		elsif isHandleAtPoint(NSMakePoint(NSMidX(bounds), NSMinY(bounds)), underPoint: point)
			return SKTGraphicUpperMiddleHandle
		elsif isHandleAtPoint(NSMakePoint(NSMaxX(bounds), NSMinY(bounds)), underPoint: point)
			return SKTGraphicUpperRightHandle
		elsif isHandleAtPoint(NSMakePoint(NSMinX(bounds), NSMidY(bounds)), underPoint: point)
			return SKTGraphicMiddleLeftHandle
		elsif isHandleAtPoint(NSMakePoint(NSMaxX(bounds), NSMidY(bounds)), underPoint: point)
			return SKTGraphicMiddleRightHandle
		elsif isHandleAtPoint(NSMakePoint(NSMinX(bounds), NSMaxY(bounds)), underPoint: point)
			return SKTGraphicLowerLeftHandle
		elsif isHandleAtPoint(NSMakePoint(NSMidX(bounds), NSMaxY(bounds)), underPoint: point)
			return SKTGraphicLowerMiddleHandle
		elsif isHandleAtPoint(NSMakePoint(NSMaxX(bounds), NSMaxY(bounds)), underPoint: point)
			return SKTGraphicLowerRightHandle
		else
			return SKTGraphicNoHandle
		end
	end

	def isHandleAtPoint (handlePoint, underPoint: point)
		# Check a handle-sized rectangle that's centered on the handle point.
		handleBounds = NSRect.new
		handleBounds.origin.x = handlePoint.x - SKTGraphicHandleHalfWidth
		handleBounds.origin.y = handlePoint.y - SKTGraphicHandleHalfWidth
		handleBounds.size.width = SKTGraphicHandleWidth
		handleBounds.size.height = SKTGraphicHandleWidth
		return NSPointInRect(point, handleBounds)
	end


	@@horizFlipings = []
	@@horizFlipings[SKTGraphicUpperLeftHandle]   = SKTGraphicUpperRightHandle
	@@horizFlipings[SKTGraphicUpperMiddleHandle] = SKTGraphicUpperMiddleHandle
	@@horizFlipings[SKTGraphicUpperRightHandle]  = SKTGraphicUpperLeftHandle
	@@horizFlipings[SKTGraphicMiddleLeftHandle]  = SKTGraphicMiddleRightHandle
	@@horizFlipings[SKTGraphicMiddleRightHandle] = SKTGraphicMiddleLeftHandle
	@@horizFlipings[SKTGraphicLowerLeftHandle]   = SKTGraphicLowerRightHandle
	@@horizFlipings[SKTGraphicLowerMiddleHandle] = SKTGraphicLowerMiddleHandle
	@@horizFlipings[SKTGraphicLowerRightHandle]  = SKTGraphicLowerLeftHandle

	@@vertFlipings = []
	@@vertFlipings[SKTGraphicUpperLeftHandle]   = SKTGraphicLowerLeftHandle
	@@vertFlipings[SKTGraphicUpperMiddleHandle] = SKTGraphicLowerMiddleHandle
	@@vertFlipings[SKTGraphicUpperRightHandle]  = SKTGraphicLowerRightHandle
	@@vertFlipings[SKTGraphicMiddleLeftHandle]  = SKTGraphicMiddleLeftHandle
	@@vertFlipings[SKTGraphicMiddleRightHandle] = SKTGraphicMiddleRightHandle
	@@vertFlipings[SKTGraphicLowerLeftHandle]   = SKTGraphicUpperLeftHandle
	@@vertFlipings[SKTGraphicLowerMiddleHandle] = SKTGraphicUpperMiddleHandle
	@@vertFlipings[SKTGraphicLowerRightHandle]  = SKTGraphicUpperRightHandle
	
	def resizeByMovingHandle (handle, toPoint: point)
		# Is the user changing the width of the graphic?
		b = bounds.dup	# need own copy as undo needs old and new versions
		case handle
			when SKTGraphicUpperLeftHandle, SKTGraphicMiddleLeftHandle, SKTGraphicLowerLeftHandle
				# Change the left edge of the graphic.
				b.size.width = NSMaxX(b) - point.x;
				b.origin.x = point.x;
			when SKTGraphicUpperRightHandle, SKTGraphicMiddleRightHandle, SKTGraphicLowerRightHandle
				# Change the right edge of the graphic.
				b.size.width = point.x - b.origin.x;
		end

		# Did the user actually flip the graphic over?
		if b.size.width < 0.0
			# The handle is now playing a different role relative to the graphic.
			handle = @@horizFlipings[handle]

			# Make the graphic's width positive again.
			b.size.width = 0.0 - b.size.width
			b.origin.x -= b.size.width

			# Tell interested subclass code what just happened.
			flipHorizontally
		end
	
		# Is the user changing the height of the graphic?
		case handle
			when SKTGraphicUpperLeftHandle, SKTGraphicUpperMiddleHandle, SKTGraphicUpperRightHandle
				# Change the top edge of the graphic.
				b.size.height = NSMaxY(b) - point.y;
				b.origin.y = point.y;
			when SKTGraphicLowerLeftHandle, SKTGraphicLowerMiddleHandle, SKTGraphicLowerRightHandle
				# Change the bottom edge of the graphic.
				b.size.height = point.y - b.origin.y;
		end

		# Did the user actually flip the graphic upside down?
		if b.size.height < 0.0

			# The handle is now playing a different role relative to the graphic.
			handle = @@vertFlipings[handle]
	
			# Make the graphic's height positive again.
			b.size.height = 0.0 - b.size.height;
			b.origin.y -= b.size.height;

			# Tell interested subclass code what just happened.
			flipVertically
		end
		
		# Changing the individual members of bounds will not have told the
		# observers so set bounds to itself to notify them.
		setBounds(b)
		return handle
	end

	def flipHorizontally ()
		# Live to be overridden.
	end


	def flipVertically ()
		# Live to be overridden.
	end


	def makeNaturalSize ()
		b = bounds.dup	# need own copy as undo needs old and new versions
		# Just make the graphic square.
		if b.size.width < b.size.height
			b.size.height = b.size.width
			setBounds(b)
		elsif b.size.width > b.size.height
			b.size.width = b.size.height
			setBounds(b)
		end	
	end

	def setBounds (b)
		@bounds = b
	end

	def setColor (color)
		# This method demonstrates something interesting: we haven't bothered to
		# provide setter methods for the properties we want to change, but we can
		# still change them using KVC. KVO autonotification will make sure
		# observers hear about the change (it works with -setValue:forKey: as well
		# as -set<Key>:). Of course, if we found ourselvings doing this a little
		# more often we would go ahead and just add the setter methods. The point
		# is that KVC direct instance variable access very often makes boilerplate
		# accessors unnecessary but if you want to just put them in right away,
		# eh, go ahead.
		# Can we fill the graphic?
		if canSetDrawingFill
			# Are we filling it? If not, start, using the new color.
			setValue(NSNumber.numberWithBool(true), forKey: SKTGraphicDrawingFillKey) if drawingFill
			setValue(color, forKey: SKTGraphicFillColorKey)
		end
	end

	def newEditingViewWithSuperviewBounds(superviewBounds)
		# Live to be overridden.
		return nil
	end

	def finalizeEditingView (editingView)
		# Live to be overridden.
	end

	# *** Undo ***

	def keysForValuesToObserveForUndo
		# Of the properties managed by SKTGraphic, "drawingingBounds,"
		# "drawingContents," "canSetDrawingFill," and "canSetDrawingStroke" aren't
		# anything that the user changes, so changes of their values aren't
		# registered undo operations. "xPosition," "yPosition," "width," and
		# "height" are all derived from "bounds," so we don't need to register
		# those either. Changes of any other property are undoable.
		NSSet.setWithArray([SKTGraphicDrawingFillKey, SKTGraphicFillColorKey, SKTGraphicDrawingStrokeKey, SKTGraphicStrokeColorKey, SKTGraphicStrokeWidthKey, SKTGraphicBoundsKey])
	end

	@@presentablePropertyNameForKey = {
	SKTGraphicDrawingFillKey => NSLocalizedStringFromTable("Filling", "UndoStrings", "Action name part for SKTGraphicDrawingFillKey."),
		SKTGraphicFillColorKey => NSLocalizedStringFromTable("Fill Color", "UndoStrings","Action name part for SKTGraphicFillColorKey."),
		SKTGraphicDrawingStrokeKey => NSLocalizedStringFromTable("Stroking", "UndoStrings", "Action name part for SKTGraphicDrawingStrokeKey."),
		SKTGraphicStrokeColorKey => NSLocalizedStringFromTable("Stroke Color", "UndoStrings", "Action name part for SKTGraphicStrokeColorKey."),
		SKTGraphicStrokeWidthKey => NSLocalizedStringFromTable("Stroke Width", "UndoStrings", "Action name part for SKTGraphicStrokeWidthKey."),
		SKTGraphicBoundsKey => NSLocalizedStringFromTable("Bounds", "UndoStrings", "Action name part for SKTGraphicBoundsKey.")
	}
	
	def self.presentablePropertyNameForKey (key)
		# Pretty simple. Don't be surprised if you never see "Bounds" appear in an
		# undo action name in Sketch. SKTGraphicView invokes -[NSUndoManager
		# setActionName:] for things like moving, resizing, and aligning, thereby
		# overwriting whatever SKTDocument sets with something more specific.
		@@presentablePropertyNameForKey[key]
	end
end

=begin

Derived from Apple's sample code for Sketch and converted to MacRuby by Dave Baldwin.

Additional comments from corresponding header file:

The values underlying some of the key-value coding (KVC) and observing (KVO) compliance described below. Any corresponding
getter or setter methods are there for invocation by code in subclasses, not for KVC or KVO compliance. KVC's direct instance
variable access, KVO's autonotifying, and KVO's property dependency mechanism makes them unnecessary for the latter purpose. If
you look closely, you'll notice that SKTGraphic itself never touches these instance variables directly except in initializers,
-copyWithZone:, and public accessors. SKTGraphic is following a good rule: if a class publishes getters and setters it should
itself invoke them, because people who override methods to customize behavior are right to expect their overrides to actually be
invoked.

NSRect _bounds;
BOOL _isDrawingFill;
NSColor *_fillColor;
BOOL _isDrawingStroke;
NSColor *_strokeColor;
CGFloat _strokeWidth;

This class is KVC (except for "drawingContents") and KVO (except for the scripting-only properties) compliant for these keys:

"canSetDrawingFill" and "canSetDrawingStroke" (boolean NSNumbers; read-only) - Whether or not it even makes sense to try to change the value of the "drawingFill" or "drawingStroke" property.

"drawingFill" (a boolean NSNumber; read-write) - Whether or not the user wants this graphic to be filled with the "fillColor" when it's drawn.

"fillColor" (an NSColor; read-write) - The color that will be used to fill this graphic when it's drawn. The value of this property is ignored when the value of "drawingFill" is NO.

"drawingStroke" (a boolean NSNumber; read-write) - Whether or not the user wants this graphic to be stroked with a path that is "strokeWidth" units wide, using the "strokeColor," when it's drawn.

"strokeColor" (an NSColor; read-write) - The color that will be used to stroke this graphic when it's drawn. The value of this property is ignored when the value of "drawingStroke" is NO.

"strokeWidth" (a floating point NSNumber; read-write) - The width of the stroke that will be used when this graphic is drawn. The value of this property is ignored when the value of "drawingStroke" is NO.

"xPosition" and "yPosition" (floating point NSNumbers; read-write) - The coordinate of the upper-left corner of the graphic.

"width" and "height" (floating point NSNumbers; read-write) - The size of the graphic.

"bounds" (an NSRect-containing NSValue; read-only) - The basic shape of the graphic. For instance, this doesn't include the width of any strokes that are drawn (so "bounds" is really a bit of a misnomer). Being KVO-compliant for bounds contributes to the automatic KVO compliance for drawingBounds via the use of KVO's dependency mechanism. See +[SKTGraphic keyPathsForValuesAffectingDrawingBounds].

"drawingBounds" (an NSRect-containing NSValue; read-only) - The bounding box of anything the graphic might draw when sent a -drawContentsInView: or -drawHandlesInView: message.

"drawingContents" (no value; not readable or writable) - A virtual property for which KVO change notifications are sent whenever any of the properties that affect the drawing of the graphic without affecting its bounds change. We use KVO for this instead of more traditional methods so that we don't have to write any code other than an invocation of KVO's +setKeys:triggerChangeNotificationsForDependentKey:. (To use NSNotificationCenter for instance we would have to write -set...: methods for all of this object's settable properties. That's pretty easy, but it's nice to avoid such boilerplate when possible.) There is no value for this property, because it would not be useful, so this class isn't actually KVC-compliant for "drawingContents." This property is not called "needsDrawing" or some such thing because instances of this class do not know how many views are using it, and potentially there will moments when it "needs drawing" in some views but not others.

"keysForValuesToObserveForUndo" (an NSSet of NSStrings; read-only) - See the comment for -keysForValuesToObserveForUndo below.

In Sketch various properties of the controls of the grid inspector are bound to the properties of the selection of the graphics controller belonging to the window controller of the main window. Each SKTGraphicView observes the "drawingBounds" and "drawingContents" properties of every graphic that it's displaying so it knows when they need redrawing. Each SKTDocument observes many properties of every of one of its graphics so it can register undo actions when they change; for each graphic the exact set of such properties is determined by the current value of the "keysForValuesToObserveForUndo" property. Also, many of these properties are scriptable.

*/

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