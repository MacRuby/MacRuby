# Keys that are used in Sketch's user defaults.
SKTAppAutosavesPreferenceKey 		= "autosaves"
SKTAppAutosavingDelayPreferenceKey 	= "autosavingDelay"


# Replace the original C macro
def NSLocalizedStringFromTable (key, value, table)
	NSBundle.mainBundle.localizedStringForKey(key, value: value, table: table)
	
	return key		## FIX - localisation errors in menu
end

class NSWindowController
	def isWindowShown ()
		window && window.isVisible
	end

	def showOrHideWindow ()
		isWindowShown ? window.orderOut(self) : showWindow(self)
	end
end


class SKTAppDelegate
	def applicationDidFinishLaunching (notification)
		# The tool palette should always show up right away.
		showOrHideToolPalette(self)

		# We always show the same inspector panel. Its controller doesn't get
		# deallocated when the user closes it. Make the panel appear in the same 
		# place when the user quits and relaunches the application.
		@graphicsInspectorController = NSWindowController.alloc.initWithWindowNibName("Inspector")
		@graphicsInspectorController.shouldCascadeWindows = false
		@graphicsInspectorController.windowFrameAutosaveName = "Inspector"

		# We always show the same grid panel. Its controller doesn't get
		# deallocated when the user closes it.  Make the panel appear in the same
		# place when the user quits and relaunches the application.
		@gridInspectorController = NSWindowController.alloc.initWithWindowNibName("GridPanel")
		@gridInspectorController.shouldCascadeWindows = false
		@gridInspectorController.windowFrameAutosaveName = "Grid"
	end

	def applicationWillFinishLaunching (notification)
		# Set up the default values of our autosaving preferences very early,
		# before there's any chance of a binding using them. The default is for
		# autosaving to be off, but 60 seconds if the user turns it on.
		userDefaultsController = NSUserDefaultsController.sharedUserDefaultsController
		userDefaultsController.setInitialValues(SKTAppAutosavesPreferenceKey => false, SKTAppAutosavingDelayPreferenceKey => 60.0)


		# Bind this object's "autosaves" and "autosavingDelay" properties to the
		# user defaults of the same name. We don't bother with ivars for these
		# values. This is just the quick way to get our -setAutosaves: and
		# -setAutosavingDelay: methods invoked.
=begin
	[self bind:SKTAppAutosavesPreferenceKey toObject:userDefaultsController withKeyPath:[@"values." stringByAppendingString:SKTAppAutosavesPreferenceKey] options:nil];
	[self bind:SKTAppAutosavingDelayPreferenceKey toObject:userDefaultsController withKeyPath:[@"values." stringByAppendingString:SKTAppAutosavingDelayPreferenceKey] options:nil];
=end	
	end

	def setAutosaves (autosaves)	
		# The user has toggled the "autosave documents" checkbox in the preferences panel.
		if autosaves
			# Get the autosaving delay and set it in the NSDocumentController.
			NSDocumentController.sharedDocumentController.autosavingDelay = @autosavingDelay
		else
			# Set a zero autosaving delay in the NSDocumentController. This tells it to turn off autosaving.
			NSDocumentController.sharedDocumentController.autosavingDelay = 0.0
		end
	
		@autosaves = autosaves
	end


	def autosavingDelay= (autosaveDelay)
		# Is autosaving even turned on right now?
		if @autosaves
			# Set the new autosaving delay in the document controller, but only if
			# autosaving is being done right now.
			NSDocumentController.sharedDocumentController.autosavingDelay = autosaveDelay
		end

		@autosavingDelay = autosaveDelay;
	end

	# IBActions
	
	def showPreferencesPanel (sender)
		#	We always show the same preferences panel. Its controller doesn't get
		#	deallocated when the user closes it.
		if !@preferencesPanelController
			@preferencesPanelController = NSWindowController.alloc.initWithWindowNibName("Preferences")

	 		# Make the panel appear in a good default location.
			@preferencesPanelController.window.center
		end
	
		@preferencesPanelController.showWindow(sender)
	end

	def showOrHideGraphicsInspector (sender)
		@graphicsInspectorController.showOrHideWindow
	end

	def showOrHideGridInspector (sender)
		@gridInspectorController.showOrHideWindow
	end

	def showOrHideToolPalette (sender)
		# We always show the same tool palette panel. Its controller doesn't get
		# deallocated when the user closes it.
		SKTToolPaletteController.sharedToolPaletteController.showOrHideWindow
	end

	def chooseSelectionTool (sender)
		SKTToolPaletteController.sharedToolPaletteController.selectArrowTool
	end

	# Conformance to the NSObject(NSMenuValidation) informal protocol.
	def validateMenuItem (menuItem)
		# A few menu item's names change between starting with "Show" and "Hide."
		toolPaletteController = SKTToolPaletteController.sharedToolPaletteController
		
		str = case menuItem.action
			when :'showOrHideGraphicsInspector:'
				@graphicsInspectorController.isWindowShown ? "Hide Inspector" : "Show Inspector"
			when :'showOrHideGridInspector:'
				@gridInspectorController.isWindowShown 	   ? "Hide Grid Options" : "Show Grid Options"
			when :'showOrHideToolPalette:'
				toolPaletteController.isWindowShown		   ? "Hide Tools" : "Show Tools"
		end

		menuItem.title = NSLocalizedStringFromTable(str, "SKTAppDelegate", "A main menu item title.") if str
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