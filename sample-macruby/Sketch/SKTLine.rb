require 'SKTGraphic'


# String constants declared in the header. They may not be used by any other
# class in the project, but it's a good idea to provide and use them, if only
# to help prevent typos in source code.
SKTLineBeginPointKey = "beginPoint"
SKTLineEndPointKey = "endPoint"

# SKTGraphic's default selection handle machinery draws more handles than we
# need, so this class implements its own.
SKTLineBeginHandle = 1
SKTLineEndHandle = 2


class SKTLine < SKTGraphic

	# *** Private KVC and KVO-Compliance for Public Properties ***

	# The only reason we have to have this many methods for simple KVC and KVO
	# compliance for "beginPoint" and "endPoint" is because reusing SKTGraphic's
	# "bounds" property is so complicated (see the instance variable comments in
	# the header). If we just had _beginPoint and _endPoint we wouldn't need any
	# of these methods because KVC's direct instance variable access and KVO's
	# autonotification would just take care of everything for us (though maybe
	# then we'd have to override -setBounds: and -bounds to fulfill the KVC and
	# KVO compliance obligation for "bounds" that this class inherits from its
	# superclass).


	def self.keyPathsForValuesAffectingBeginPoint ()
		NSSet.setWithObject(SKTGraphicBoundsKey)
	end

	def beginPoint ()
		# Convert from our odd storage format to something natural.
		NSPoint.new(@pointsRight ? NSMinX(bounds) : NSMaxX(bounds),
					@pointsDown ? NSMinY(bounds) : NSMaxY(bounds))
	end

	def self.keyPathsForValuesAffectingEndPoint ()
		NSSet.setWithObject(SKTGraphicBoundsKey)
	end
	
	def endPoint ()
		# Convert from our odd storage format to something natural.
	 	NSPoint.new(@pointsRight ? NSMaxX(bounds) : NSMinX(bounds),
					@pointsDown ? NSMaxY(bounds) : NSMinY(bounds))
	end

	def boundsWithBeginPoint (bPoint, endPoint: ePoint)
		# Convert the begin and end points of the line to its bounds and flags
		# specifying the direction in which it points.
		@pointsRight = bPoint.x < ePoint.x
		@pointsDown = bPoint.y < ePoint.y
		xPosition = @pointsRight ? bPoint.x : ePoint.x
		yPosition = @pointsDown ? bPoint.y : ePoint.y
		width = (ePoint.x - bPoint.x).abs
		height = (ePoint.y - bPoint.y).abs

		NSMakeRect(xPosition, yPosition, width, height);
	end

	def setBeginPoint (bPoint)
		# It's easiest to compute the results of setting these points together.
		setBounds(boundsWithBeginPoint(bPoint, endPoint: endPoint))
	end

	def setEndPoint (ePoint)
		# It's easiest to compute the results of setting these points together.
		setBounds(boundsWithBeginPoint(beginPoint, endPoint: ePoint))
	end

	# *** Overrides of SKTGraphic Methods ***

	def initWithProperties (properties)
		# Let SKTGraphic do its job and then handle the additional properties
		# defined by this subclass.
		super

		# This object still doesn't have a bounds (because of what we do in our
		# override of -properties), so set one and record the other information we
		# need to place the begin and end points. The dictionary entries are all
		# instances of the classes that can be written in property lists. Don't
		# trust the type of something you get out of a property list unless you know
		# your process created it or it was read from your application or
		# framework's resources. We don't have to worry about KVO-compliance in
		# initializers like this by the way; no one should be observing an
		# unitialized object.
		beginPointString = properties[SKTLineBeginPointKey]
		beginPoint = beginPointString.kind_of?(String) ? NSPointFromString(beginPointString) : NSZeroPoint
		endPointString = properties[SKTLineEndPointKey]
		endPoint = endPointString.kind_of?(String) ? NSPointFromString(endPointString) : NSZeroPoint
		setBounds(boundsWithBeginPoint(beginPoint, endPoint: endPoint))
		return self;
	end

	def properties ()
		# Let SKTGraphic do its job but throw out the bounds entry in the dictionary
		# it returned and add begin and end point entries insteads. We do this
		# instead of simply recording the currnet value of _pointsRight and
		# _pointsDown because bounds+pointsRight+pointsDown is just too unnatural to
		# immortalize in a file format. The dictionary must contain nothing but
		# values that can be written in old-style property lists.
		props = super
		props.delete(SKTGraphicBoundsKey)
		props[SKTLineBeginPointKey] = NSStringFromPoint(beginPoint)
		props[SKTLineEndPointKey] = NSStringFromPoint(endPoint)
		return props
	end


	# We don't bother overriding +[SKTGraphic
	# keyPathsForValuesAffectingDrawingBounds] because we don't need to take
	# advantage of the KVO dependency mechanism enabled by that method. We fulfill
	# our KVO compliance obligations (inherited from SKTGraphic) for
	# SKTGraphicDrawingBoundsKey by just always invoking -setBounds: in
	# -setBeginPoint: and -setEndPoint:. "bounds" is always in the set returned by
	# +[SKTGraphic keyPathsForValuesAffectingDrawingBounds]. Now, there's nothing
	# in SKTGraphic.h that actually guarantees that, so we're taking advantage of
	# "undefined" behavior. If we didn't have the source to SKTGraphic right next
	# to the source for this class it would probably be prudent to override
	# +keyPathsForValuesAffectingDrawingBounds, and make sure.

	# We don't bother overriding +[SKTGraphic
	# keyPathsForValuesAffectingDrawingContents] because this class doesn't define
	# any properties that affect drawing without affecting the bounds.


	def drawingFill ()
		# You can't fill a line.
		false
	end

	def drawingStroke
		# You can't not stroke a line.
		true
	end

	def bezierPathForDrawing ()
		path = NSBezierPath.bezierPath
		path.moveToPoint(beginPoint)
		path.lineToPoint(endPoint)
		path.setLineWidth(strokeWidth)
		path
	end

	def drawHandlesInView (view)
		# A line only has two handles.
		drawHandleInView(view, atPoint: beginPoint)
		drawHandleInView(view, atPoint: endPoint)
	end

	def self.creationSizingHandle ()
		# When the user creates a line and is dragging around a handle to size it
		# they're dragging the end of the line.
		return SKTLineEndHandle
	end


	def canSetDrawingFill ()
		# Don't let the user think we can fill a line.
		false
	end

	def canSetDrawingStroke ()
		# Don't let the user think can ever not stroke a line.
		false
	end

	def canMakeNaturalSize
		# What would the "natural size" of a line be?
		false
	end


	def isContentsUnderPoint (point)
		# Do a gross check against the bounds.
 		isContentsUnderPoint = false
		if NSPointInRect(point, bounds)
			# Let the user click within the stroke width plus some slop.
			acceptableDistance = (strokeWidth * 0.5) + 2.0

			# Before doing anything avoid a divide by zero error.
			xDelta = endPoint.x - beginPoint.x
			if xDelta == 0.0 && fabs(point.x - beginPoint.x).abs <= acceptableDistance
				isContentsUnderPoint = true
			else
				# Do a weak approximation of distance to the line segment.
				slope = (endPoint.y - beginPoint.y) / xDelta
				if (((point.x - beginPoint.x) * slope) - (point.y - beginPoint.y)).abs <= acceptableDistance
					isContentsUnderPoint = true
				end
			end
		end
		return isContentsUnderPoint;
	end


	def handleUnderPoint (point)
		# A line just has handles at its ends.
		if isHandleAtPoint(beginPoint, underPoint: point)
			return SKTLineBeginHandle
		elsif isHandleAtPoint(endPoint, underPoint:point) 
			return SKTLineEndHandle
		end
		return SKTGraphicNoHandle
	end


	def resizeByMovingHandle (handle, toPoint: point)
		# A line just has handles at its ends.
		if handle == SKTLineBeginHandle
			setBeginPoint(point)
		elsif handle == SKTLineEndHandle
			setEndPoint(point)
		end
		# We don't have to do the kind of handle flipping that SKTGraphic does.
		return handle;
	end


	def setColor (color)
		# Because lines aren't filled we'll consider the stroke's color to be the one.
		setValue(color, forKey: SKTGraphicStrokeColorKey)
	end


	def keysForValuesToObserveForUndo ()
		# When the user drags one of the handles of a line we don't want to just
		# have changes to "bounds" registered in the undo group. That would be:
		# 1) Insufficient. We would also have to register changes of "pointsRight"
		# and "pointsDown," but we already decided to keep those properties private
		# (see the comments in the header). 2) Not very user-friendly. We don't want
		# the user to see an "Undo Change of Bounds" item in the Edit menu. We want
		# them to see "Undo Change of Endpoint."
		# So, tell the observer of undoable properties (SKTDocument, in Sketch) to
		# observe "beginPoint" and "endPoint" instead of "bounds."
		keys = super.mutableCopy
		keys.removeObject(SKTGraphicBoundsKey)
		keys.addObject(SKTLineBeginPointKey)
		keys.addObject(SKTLineEndPointKey)
		keys
	end

	@@presentablePropertyNamesByKey = {
		SKTLineBeginPointKey => NSLocalizedStringFromTable("Beginpoint", "UndoStrings", "Action name part for SKTLineBeginPointKey."),
		SKTLineEndPointKey => NSLocalizedStringFromTable("Endpoint", "UndoStrings", "Action name part for SKTLineEndPointKey.")
	}
	
	def self.presentablePropertyNameForKey (key)
		# As is usually the case when a key is passed into a method like this, we
		# have to invoke super if we don't recognize the key. As far as the user
		# is concerned both points that define a line are "endpoints."
		@@presentablePropertyNamesByKey[key] || super
	end
end

=begin

Derived from Apple's sample code for Sketch and converted to MacRuby by Dave Baldwin.

Additional comments from corresponding header file:

YES if the line's ending is to the right or below, respectively, it's beginning, NO otherwise. Because we reuse SKTGraphic's "bounds" property, we have to keep track of the corners of the bounds at which the line begins and ends. A more natural thing to do would be to just record two points, but then we'd be wasting an NSRect's worth of ivar space per instance, and have to override more SKTGraphic methods to boot. This of course raises the question of why SKTGraphic has a bounds property when it's not readily applicable to every conceivable subclass. Perhaps in the future it won't, but right now in Sketch it's the handy thing to do for four out of five subclasses.
    BOOL _pointsRight;
    BOOL _pointsDown;

This class is KVC and KVO compliant for these keys:

"beginPoint" and "endPoint" (NSPoint-containing NSValues; read-only) - The two points that define the line segment.

In Sketch "beginPoint" and "endPoint" are two more of the properties that SKTDocument observes so it can register undo actions when they change.

Notice that we don't guarantee KVC or KVO compliance for "pointsRight" and "pointsDown." Those aren't just private instance variables, they're private properties, concepts that no code outside of SKTLine should care about.


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