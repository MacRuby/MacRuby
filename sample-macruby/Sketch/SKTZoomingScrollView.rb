# The name of the binding supported by this class, in addition to the ones
# whose support is inherited from NSView.
SKTZoomingScrollViewFactor = "factor"

# Default labels and values for the menu items that will be in the popup
# button that we build.
SKTZoomingScrollViewFactors = [0.1, 0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 4.0, 8.0, 16.0]

# /* We're going to be passing SKTZoomingScrollViewLabels elements into NSLocalizedStringFromTable, but genstrings won't understand that. List the menu item labels in a way it will understand.
# NSLocalizedStringFromTable(@"10%", @"SKTZoomingScrollView", @"A level of zooming in a view.")
# NSLocalizedStringFromTable(@"25%", @"SKTZoomingScrollView", @"A level of zooming in a view.")
# NSLocalizedStringFromTable(@"50%", @"SKTZoomingScrollView", @"A level of zooming in a view.")
# NSLocalizedStringFromTable(@"75%", @"SKTZoomingScrollView", @"A level of zooming in a view.")
# NSLocalizedStringFromTable(@"100%", @"SKTZoomingScrollView", @"A level of zooming in a view.")
# NSLocalizedStringFromTable(@"125%", @"SKTZoomingScrollView", @"A level of zooming in a view.")
# NSLocalizedStringFromTable(@"150%", @"SKTZoomingScrollView", @"A level of zooming in a view.")
# NSLocalizedStringFromTable(@"200%", @"SKTZoomingScrollView", @"A level of zooming in a view.")
# NSLocalizedStringFromTable(@"400%", @"SKTZoomingScrollView", @"A level of zooming in a view.")
# NSLocalizedStringFromTable(@"800%", @"SKTZoomingScrollView", @"A level of zooming in a view.")
# NSLocalizedStringFromTable(@"1600%", @"SKTZoomingScrollView", @"A level of zooming in a view.")
# */
# 

class SKTZoomingScrollView < NSScrollView
	attr_accessor	:factor
	
	def validateFactorPopUpButton ()
		# Ignore redundant invocations.
		if !@factorPopUpButton

			# Create the popup button and configure its appearance. The initial size doesn't matter.
			@factorPopUpButton = NSPopUpButton.alloc.initWithFrame(NSZeroRect, pullsDown: false)
			factorPopUpButtonCell = @factorPopUpButton.cell
			factorPopUpButtonCell.setArrowPosition(NSPopUpArrowAtBottom)
			factorPopUpButtonCell.setBezelStyle(NSShadowlessSquareBezelStyle)
			@factorPopUpButton.setFont(NSFont.systemFontOfSize(NSFont.smallSystemFontSize))

			# Populate it and size it to fit the just-added menu item cells.
			# Derive the labels from the zoom factors and don't bother trying to translate 10%, for example
			SKTZoomingScrollViewFactors.each_with_index do |factor, i|
				@factorPopUpButton.addItemWithTitle("#{(factor * 100).to_i}%")
			   	@factorPopUpButton.itemAtIndex(i).setRepresentedObject(factor)
			end

			@factorPopUpButton.sizeToFit

			# Make it appear
			addSubview(@factorPopUpButton)
		end
	end

	# *** Bindings ***
	def setFactor (factor)
		# The default implementation of key-value binding is informing this object
		# that the value to which our "factor" property is bound has changed.
		# Record the value, and apply the zoom factor by fooling with the bounds
		# of the clip view that every scroll view has. (We leave its frame alone.)
		@factor = factor;
		clipView = documentView.superview
		clipViewFrameSize = clipView.frame.size
		clipView.setBoundsSize(NSMakeSize((clipViewFrameSize.width / factor), (clipViewFrameSize.height / factor)))
	end

	# An override of the NSObject(NSKeyValueBindingCreation) method.
	def bind (bindingName, toObject: observableObject, withKeyPath: observableKeyPath, options: options)
		# For the one binding that this class recognizes, automatically bind the zoom factor popup button's value to the same object...
		if bindingName == SKTZoomingScrollViewFactor
			validateFactorPopUpButton
			@factorPopUpButton.bind(NSSelectedObjectBinding, toObject: observableObject, withKeyPath: observableKeyPath, options: options)
		end

		# ...but still use NSObject's default implementation, which will send
		# _this_ object -setFactor: messages (via key-value coding) whenever the
		# bound-to value changes, for whatever reason, including the user changing
		# it with the zoom factor popup button. Also, NSView supports a few simple
		# bindings of its own, and there's no reason to get in the way of those.
		super
	end

	# An override of the NSObject(NSKeyValueBindingCreation) method.
	def unbind (bindingName)
		# Undo what we did in our override of -bind:toObject:withKeyPath:options:.
		super
		@factorPopUpButton.unbind(NSSelectedObjectBinding) if bindingName == SKTZoomingScrollViewFactor
	end


	# *** View Customization ***
	# An override of the NSScrollView method.
	def tile ()
		# This class lives to put a popup button next to a horizontal scroll bar.
		raise "SKTZoomingScrollView doesn't support use without a horizontal scroll bar." if !hasHorizontalScroller

		# Do NSScrollView's regular tiling, and find out where it left the horizontal scroller.
		super
		horizontalScrollerFrame = horizontalScroller.frame

		# Place the zoom factor popup button to the left of where the horizontal
		# scroller will go, creating it first if necessary, and leaving its width
		# alone.
		validateFactorPopUpButton
		factorPopUpButtonFrame = @factorPopUpButton.frame
		factorPopUpButtonFrame.origin.x = horizontalScrollerFrame.origin.x
		factorPopUpButtonFrame.origin.y = horizontalScrollerFrame.origin.y
		factorPopUpButtonFrame.size.height = horizontalScrollerFrame.size.height
		@factorPopUpButton.frame = factorPopUpButtonFrame

		# Adjust the scroller's frame to make room for the zoom factor popup button next to it.
		horizontalScrollerFrame.origin.x += factorPopUpButtonFrame.size.width
		horizontalScrollerFrame.size.width -= factorPopUpButtonFrame.size.width
		horizontalScroller.frame = horizontalScrollerFrame
	end
end


=begin

Derived from Apple's sample code for Sketch and converted to MacRuby by Dave Baldwin.

Additional comments from corresponding header file:

The name of the binding supported by this class, in addition to the ones whose support is inherited from NSScrollView.
extern NSString *SKTZoomingScrollViewFactor;

Every instance of this class creates a popup button with zoom factors in it and places it next to the horizontal scroll bar.
    NSPopUpButton *_factorPopUpButton;

The current zoom factor. This instance variable isn't actually read by any SKTZoomingScrollView code and wouldn't be necessary if it weren't for an oddity in the default implementation of key-value binding (KVB): -[NSObject(NSKeyValueBindingCreation) bind:toObject:withKeyPath:options:] sends the receiver a -valueForKeyPath: message, even though the returned value is typically not interesting. With this here key-value coding (KVC) direct instance variable access makes -valueForKeyPath: happy.
    CGFloat _factor;


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