# A string constant declared in the header. We haven't bother declaring string
# constants for the other keys mentioned in the header yet because no one
# would be using them. Those keys are all typed directly into Interface
# Builder's bindings inspector.
SKTGridAnyKey = "any"

# The number of seconds that we wait after temporarily showing the grid before
# we hide it again. This number has never been reviewed by an actual user
# interface designer, but it seems nice to at least one engineer at Apple. 
SKTGridTemporaryShowingTime = 1.0

class SKTGrid < NSObject
	attr_reader		:isAlwaysShown, :isConstraining
    attr_accessor	:color, :spacing

	# An override of the superclass' designated initializer.
	def init ()
		super

		# Establish reasonable defaults. 9 points is an eighth of an inch, which
		# is a reasonable default.
		@color = NSColor.lightGrayColor
		@spacing = 9.0

		return self
	end

# - (void)dealloc {
# 
#	  # If we've set a timer to hide the grid invalidate it so it doesn't send a message to this object's zombie.
#	  [_hidingTimer invalidate]
# }


	#*** Private KVC and KVO-Compliance for Public Properties ***

	def self.keyPathsForValuesAffectingAny ()
		# Specify that a KVO-compliant change for any of this class' non-derived
		# properties should result in a KVO change notification for the "any"
		# virtual property. Views that want to use this grid can observe "any"
		# for notification of the need to redraw the grid.
	    NSSet.setWithArray(%w{color spacing alwaysShown constraining})
	end

	def stopShowingGridForTimer (timer)
		# The timer is now invalid and will be releasing itself.
		@hidingTimer = nil
	
		# Tell observing views to redraw. By the way, it is virtually always a
		# mistake to put willChange/didChange invocations together with nothing in
		# between. Doing so can result in bugs that are hard to track down. You
		# should always invoke -willChangeValueForKey:theKey before the result of
		# -valueForKey:theKey would change, and then invoke
		# -didChangeValueForKey:theKey after the result of -valueForKey:theKey
		# would have changed. We can get away with this here because there is no
		# value for the "any" key.
		willChangeValueForKey(SKTGridAnyKey)
		didChangeValueForKey(SKTGridAnyKey)
	end

	def setSpacing (spacing)
		# Weed out redundant invocations.
		if spacing != @spacing
			@spacing = spacing
			
			# If the grid is drawable, make sure the user gets visual feedback of the
			# change. We don't have to do anything special if the grid is being shown
			# right now.	Observers of "any" will get notified of this change because
			# of what we did in +initialize. They're expected to invoke
			# -drawRect:inView:. 
			if @spacing > 0 && !@isAlwaysShown
				# Are we already showing the grid temporarily?
				if @hidingTimer
					# Yes, and now the user's changed the grid spacing again, so put off
					# the hiding of the grid.
					@hidingTimer.setFireDate(NSDate.dateWithTimeIntervalSinceNow(SKTGridTemporaryShowingTime))
			 	else
					# No, so show it the next time -drawRect:inView: is invoked, and
					# then hide it again in one second.
					@hidingTimer = NSTimer.scheduledTimerWithTimeInterval(SKTGridTemporaryShowingTime, target: self, 
											selector: 'stopShowingGridForTimer:', userInfo: nil, repeats: false)
		
					# Don't bother with a separate _showsGridTemporarily instance
					# variable. -drawRect: can just check to see if _hidingTimer is
					# non-nil.
				end
			end
		end
	end

	def self.keyPathsForValuesAffectingCanSetColor ()
	    NSSet.setWithArray(%w{alwaysShown usable})
	end
	
	def canSetColor ()
		# Don't let the user change the color of the grid when that would be useless.
		@isAlwaysShown && isUsable
	end

	def self.keyPathsForValuesAffectingCanSetSpacing ()
	    NSSet.setWithArray(%w{alwaysShown constraining})
	end
	
	def canSetSpacing ()
		# Don't let the user change the spacing of the grid when that would be useless.
		@isAlwaysShown || @isConstraining
	end

 	# *** Public Methods ***

	def setConstraining (isConstraining)
		@isConstraining = isConstraining
	end

	def self.keyPathsForValuesAffectingUsable ()
	    NSSet.setWithArray(%w{spacing})
	end
	
	def isUsable ()
		# The grid isn't usable if the spacing is set to zero. The header comments
		# explain why we don't validate away zero spacing.
		@spacing > 0
	end

	def setAlwaysShown (shown)
		# Weed out redundant invocations.
		if shown != @isAlwaysShown
			@isAlwaysShown = shown

			# If we're temporarily showing the grid then there's a timer that's
			# going to hide it. If we're supposed to show the grid right now then we
			# don't want the timer to undo that. If we're supposed to hide the grid
			# right now then the hiding that the timer would do is redundant.
			if @hidingTimer
				@hidingTimer.invalidate
				@hidingTimer = nil
			end
		end
	end

 	def constrainedPoint (point)
		# The grid might not be usable right now, or constraining might be turned
		# off.
		if isUsable && @isConstraining
			point.x = (point.x / @spacing + 0.5).floor * @spacing
			point.y = (point.y / @spacing + 0.5).floor * @spacing
		end
		point
	end
	
	def canAlign ()
		# You can invoke alignedRect: any time the spacing is valid.
		isUsable
	end

	def alignedRect (rect)
		# Aligning is done even when constraining is not.
		upperRight = NSMakePoint(NSMaxX(rect), NSMaxY(rect))
		rect.origin.x = ((rect.origin.x / @spacing) + 0.5).floor * @spacing
		rect.origin.y = ((rect.origin.y / @spacing) + 0.5).floor * @spacing
		upperRight.x  = ((upperRight.x  / @spacing) + 0.5).floor * @spacing
		upperRight.y  = ((upperRight.y  / @spacing) + 0.5).floor * @spacing
		rect.size.width = upperRight.x - rect.origin.x
		rect.size.height = upperRight.y - rect.origin.y
		rect
	end

	def drawRect (rect, inView: view)
		# The grid might not be usable right now. It might be shown, but only temporarily.
		if isUsable && (isAlwaysShown || @hidingTimer)
			# Figure out a big bezier path that corresponds to the entire grid. It
			# will consist of the vertical lines and then the horizontal lines.
			gridPath = NSBezierPath.bezierPath
			lastVerticalLineNumber = (NSMaxX(rect) / @spacing).floor
			(NSMinX(rect) / @spacing).ceil.upto(lastVerticalLineNumber) do |lineNumber|
				gridPath.moveToPoint(NSMakePoint(lineNumber * @spacing, NSMinY(rect)))
				gridPath.lineToPoint(NSMakePoint(lineNumber * @spacing, NSMaxY(rect)))
			end
		
			lastHorizontalLineNumber = (NSMaxY(rect) / @spacing).floor
			(NSMinY(rect) / @spacing).ceil.upto(lastHorizontalLineNumber) do |lineNumber|
				gridPath.moveToPoint(NSMakePoint(NSMinX(rect), lineNumber * @spacing))
				gridPath.lineToPoint(NSMakePoint(NSMaxX(rect), lineNumber * @spacing))
			end
	
			# Draw the grid as one-pixel-wide lines of a specific color.
			@color.set
			gridPath.lineWidth = 0.0
			gridPath.stroke
		end
	end
end

=begin

Derived from Apple's sample code for Sketch and converted to MacRuby by Dave Baldwin.

Additional comments from corresponding header file:

The values underlying the key-value coding (KVC) and observing (KVO) compliance described below. There isn't a full complement of corresponding getter or setter methods. KVC's direct instance variable access, KVO's autonotifying, and KVO's property dependency mechanism make them unnecessary. If in the future we decide that we need to do more complicated things when these values are gotten or set we can add getter and setter methods then, and no bound object will know the difference (so don't let me hear any more guff about direct ivar access "breaking encapsulation").
    NSColor *_color;
    CGFloat _spacing;
    BOOL _isAlwaysShown;
    BOOL _isConstraining;

Sometimes we temporarily show the grid to provide feedback for user changes to the grid spacing. When we do that we use a timer to turn it off again.
    NSTimer *_hidingTimer;
    
This class is KVC and KVO compliant for these keys:

"color" (an NSColor; read-write) - The color that will be used when the grid is shown.
    
"spacing" (a floating point NSNumber; read-write) - The distance (in user space units) between the grid lines used when showing the grid or constraining points to it.

"alwaysShown" (a boolean NSNumber; read-write) - Whether or not the user wants the grid to always be visible. -drawRect:inView: may draw a visible grid even when the value of this property is NO, if it's animating itself to provide good user feedback about a change to one of its properties.

"constraining" (a boolean NSNumber; read-write) - Whether or not the user wants graphics to be constrained to this grid. Graphic views should not need to access this property. They should invoke -constrainedPoint: instead.

"usable" (a boolean NSNumber; read-only) - Whether or not grid parameters are currently set to values that are valid enough that grid showing and constraining of graphics to the grid can be done. This wouldn't be necessary if we didn't allow zero to be a valid value for grid spacing, but we do, even though we don't want to draw zero-space grids, because there's no other reasonable number to use as the miminum value for the grid spacing slider in the grid panel. Why not use "one-point-oh" you ask? What's so special about that value I ask back. Grid spacing isn't in terms of pixel widths. It's in terms of user space units. Why not implement a validation method for the "gridSpacing" property to catch the user trying to set it to zero? Because the best thing that could come of that would be an alert that's presented to the user whenever they drag the spacing slider all the way to the left, or maybe just a beep, and either would be obnoxious. [User interface advice given in the comments of Sketch sample code is strictly the opinion of the engineer who's rewriting Sketch, and hasn't been reviewed by Apple's actual user interface designers, but, really.] By the way, an alternative to binding to this property would be binding directly to the "spacing" property using a very simple SKTIsGreaterThanZero value transformer of our own making. That would be putting a little to much logic into the nibs though, and a couple of nibs would require updating if we someday had to change the rules about when the grid is useful. This way we would just have to update this class' -isUsable method.

"canSetColor" (a boolean NSNumber; read-only) - Whether or not grid parameters are currently set to values that are valid enough that setting the grid color would do something useful, from the user's point of view.

"canSetSpacing" (a boolean NSNumber; read-only) - Whether or not grid parameters are currently set to values that are valid enough that setting grid spacing would do something useful, from the user's point of view. This wouldn't be necessary if we just forbade the user from changing the grid spacing when the grid wasn't shown, because then we could just bind the "editable" property of controls that set the grid spacing to to "alwaysShown" instead, but that would be a little weak. The grid spacing is useful for constraining graphics to the grid even when the grid isn't shown. Now, whenever we let the user change the grid spacing we have to provide good immediate feedback to the user about it, and Sketch does. See -setSpacing for our solution to that problem.

"any" (no value; not readable or writable) - A virtual property for which KVO change notifications are sent whenever any of the properties that affect the drawing of the grid have changed. We use KVO for this instead of more traditional methods so that we don't have to write any code other than an invocation of KVO's +setKeys:triggerChangeNotificationsForDependentKey:. (To use NSNotificationCenter for instance we would have to write -set...: methods for all of this object's settable properties. That's pretty easy, but it's nice to avoid such boilerplate when possible.) There is no value for this property, because it would not be useful, and this class isn't KVC-compliant for "any." This property is not called "needsDrawing" or some such thing because instances of this class do not know how many views are using it, and potentially there will be moments when it "needs drawing" in some views but not others.

In Sketch various properties of the controls of the grid inspector are bound to the properties (all except for the "any" property) of the grid belonging to the window controller of the main window. Each SKTGraphicView observes the "any" property of the grid to which its bound so it knows when the grid needs drawing.


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