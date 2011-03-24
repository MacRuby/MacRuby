
SKTDocumentCanvasSizeKey = "canvasSize"
SKTDocumentGraphicsKey = "graphics"

# The document type names that must also be used in the application's
# Info.plist file. We'll take out all uses of SKTDocumentOldTypeName and
# SKTDocumentOldVersion1TypeName (and NSPDFPboardType and NSTIFFPboardType)
# someday when we drop 10.4 compatibility and we can just use UTIs everywhere.
SKTDocumentOldTypeName		   = "Apple Sketch document"
SKTDocumentNewTypeName		   = "com.apple.sketch2"
SKTDocumentOldVersion1TypeName = "Apple Sketch 1 document"
SKTDocumentNewVersion1TypeName = "com.apple.sketch1"

# More keys, and a version number, which are just used in Sketch's
# property-list-based file format.
SKTDocumentVersionKey	  = "version"
SKTDocumentPrintInfoKey	  = "printInfo"
SKTDocumentCurrentVersion = 2


class SKTDocument < NSDocument
	attr_reader :graphics
		
	# An override of the superclass' designated initializer, which means it should always be invoked.
	def init ()
		@graphics = []
		@documentUndoKeysObserver = SKTObserver.new(self, :documentUndoKeysDidChange)
		@documentUndoObserver = SKTObserver.new(self, :documentUndoDidChange)
		super

		# Before anything undoable happens, register for a notification we need.
		NSNotificationCenter.defaultCenter.addObserver(self, 
					selector: 'observeUndoManagerCheckpoint:',
						name: NSUndoManagerCheckpointNotification,
					  object: undoManager)
		self
	end

	# Private KVC-Compliance for Public Properties ***
	def insertGraphics (graphics, atIndexes: indexes)
		@graphics ||= []
	    @graphics.insertObjects(graphics, atIndexes: indexes)

		# For the purposes of scripting, every graphic has to point back to the document that contains it.
		# Not supporting scripting at present.
		# graphics.makeObjectsPerformSelector(:setScriptingContainer, withObject: self)

		# Register an action that will undo the insertion.
		undoManager.registerUndoWithTarget(self, selector: 'removeGraphicsAtIndexes:', object: indexes)

		# Record the inserted graphics so we can filter out observer notifications
		# from them. This way we don't waste memory registering undo operations for
		# changes that wouldn't have any effect because the graphics are going to be
		# removed anyway. In Sketch this makes a difference when you create a graphic
		# and then drag the mouse to set its initial size right away. Why don't we do
		# this if undo registration is disabled? Because we don't want to add to this
		# set during document reading. (See what -readFromData:ofType:error: does with
		# the undo manager.) That would ruin the undoability of the first graphic
		# editing you do after reading a document.
		if undoManager.isUndoRegistrationEnabled
			@undoGroupInsertedGraphics ||= {}
			graphics.each {|g| @undoGroupInsertedGraphics[g] = true}
		end

		# Start observing the just-inserted graphics so that, when they're changed, we can record undo operations.
		startObservingGraphics(graphics)
	end

	def removeGraphicsAtIndexes (indexes)
		# Find out what graphics are being removed. 
		graphics = @graphics.objectsAtIndexes(indexes)

		# Stop observing the just-removed graphics to balance what was done in -insertGraphics:atIndexes:.
		stopObservingGraphics(graphics)

		# Register an action that will undo the removal. Do this before the
		# actual removal so we don't have to worry about the releasing of the
		# graphics that will be done.
		undoManager.prepareWithInvocationTarget(self).insertGraphics(graphics, atIndexes: indexes)
	
		# For the purposes of scripting, every graphic had to point back to the
		# document that contains it. Now they should stop that.
		# scripting not supported at present
		# graphics.makeObjectsPerformSelector(:setScriptingContainer, withObject: nil)

		# Do the actual removal.
		@graphics.removeObjectsAtIndexes(indexes)
	end

	 # There's no need for a -setGraphics: method right now, because
	 # [thisDocument mutableArrayValueForKey:@"graphics"] will happily return a
	 # mutable collection proxy that invokes our insertion and removal methods
	 # when necessary. A pitfall to watch out for is that -setValue:forKey: is
	 # _not_ bright enough to invoke our insertion and removal methods when you
	 # would think it should. If we ever catch anyone sending this object
	 # -setValue:forKey: messages for "graphics" then we have to add
	 # -setGraphics:. When we do, there's another pitfall to watch out for: if
	 # -setGraphics: is implemented in terms of -insertGraphics:atIndexes: and
	 # -removeGraphicsAtIndexes:, or vice versa, then KVO autonotification will
	 # cause observers to get redundant, incorrect, notifications (because all
	 # of the methods involved have KVC-compliant names).


	# Simple Property Getting
	def canvasSize ()
		# A Sketch's canvas size is the size of the piece of paper that the user
		# selects in the Page Setup panel for it, minus the document margins
		# that are set.
		canvasSize = printInfo.paperSize
		canvasSize.width  -= printInfo.leftMargin + printInfo.rightMargin
		canvasSize.height -= printInfo.topMargin + printInfo.bottomMargin
		canvasSize
	end

	# Overrides of NSDocument Methods ***

	# This method will only be invoked on Mac 10.6 and later. It's ignored on
	# Mac OS 10.5.x which just means that documents are opened serially.
	def self.canConcurrentlyReadDocumentsOfType (typeName)
		# There's nothing in Sketch that would cause multithreading trouble when
		# documents are opened in parallel in separate NSOperations.
		true
	end

	def readFromData (data, ofType: typeName, error: outError)
		# This application's Info.plist only declares two document types, which go
		# by the names SKTDocumentOldTypeName/SKTDocumentOldVersion1TypeName (on
		# Mac OS 10.4) or SKTDocumentNewTypeName/SKTDocumentNewVersion1TypeName
		# (on 10.5), for which it can play the "editor" role, and none for which
		# it can play the "viewer" role, so the type better match one of those.
		# Notice that we don't compare uniform type identifiers (UTIs) with
		# -isEqualToString:. We use -[NSWorkspace type:conformsToType:] (new in
		# 10.5), which is nearly always the correct thing to do with UTIs.
		# MacRuby only works on 10.5 or greater so can ignore this.
		workspace = NSWorkspace.sharedWorkspace
		if workspace.type(typeName, conformsToType: SKTDocumentNewTypeName) || typeName == SKTDocumentOldTypeName
			# The file uses Sketch 2's new format. Read in the property list.
			# properties = NSPropertyListSerialization.propertyListFromData(data, mutabilityOption: NSPropertyListImmutable,
			# 						format: nil, errorDescription: nil)
			properties = NSPropertyListSerialization.propertyListWithData(data, options: 0,  format: nil, error: nil)

			if properties
				# Get the graphics. Strictly speaking the property list of an empty
				# document should have an empty graphics array, not no graphics array,
				# but we cope easily with either. Don't trust the type of something
				# you get out of a property list unless you know your process created
				# it or it was read from your application or framework's resources.
				graphicPropertiesArray = properties[SKTDocumentGraphicsKey]
				graphicsList = graphicPropertiesArray.kind_of?(NSArray) ? 
								SKTGraphic.graphicsWithProperties(graphicPropertiesArray) :
								[]
				# Get the page setup. There's no point in considering the opening of
				# the document to have failed if we can't get print info. A more
				# finished app might present a panel warning the user that something's
				# fishy though.
				printInfoData = properties[SKTDocumentPrintInfoKey]
				printInfo = printInfoData.kind_of?(NSData) ?
								NSUnarchiver.unarchiveObjectWithData(printInfoData) : 
								NSPrintInfo.new
			else
				# If property list parsing fails we have no choice but to admit that we
				# don't know what went wrong. The error description returned by
				# +[NSPropertyListSerialization
				# propertyListFromData:mutabilityOption:format:errorDescription:] would be
				# pretty technical, and not the sort of thing that we should show to a
				# user.
				outError[0] = SKTErrorWithCode(SKTUnknownFileReadError) if outError
			end
	
			readSuccessfully = properties
		else
			# The file uses Sketch's old format. Sketch is still a work in progress.
			graphicsList = []
			printInfo = NSPrintInfo.new
			readSuccessfully = true;
		end

		# Did the reading work? In this method we ought to either do nothing and
		# return an error or overwrite every property of the document. Don't leave
		# the document in a half-baked state.
		if readSuccessfully
			# Update the document's list of graphics by going through KVC-compliant
			# mutation methods. KVO notifications will be automatically sent to
			# observers (which does matter, because this might be happening at some time
			# other than document opening; reverting, for instance). Update its page
			# setup the regular way. Don't let undo actions get registered while doing
			# any of this. The fact that we have to explicitly protect against useless
			# undo actions is considered an NSDocument bug nowadays, and will someday be
			# fixed.
			undoManager.disableUndoRegistration
			removeGraphicsAtIndexes(NSIndexSet.indexSetWithIndexesInRange(NSMakeRange(0, @graphics.count)))
			insertGraphics(graphicsList, atIndexes: NSIndexSet.indexSetWithIndexesInRange(NSMakeRange(0, graphicsList.count)))
			printInfo = printInfo
			undoManager.enableUndoRegistration
		else
			# it was the responsibility of something in the previous paragraph to set *outError.
			return readSuccessfully;
		end
	end


	def dataOfType (typeName, error: outError)
		# This method must be prepared for typeName to be any value that might be
		# in the array returned by any invocation of
		# -writableTypesForSaveOperation:. Because this class:
		# doesn't - override -writableTypesForSaveOperation:, and
		# doesn't - override +writableTypes or +isNativeType: (which the default implementation of 
		# -writableTypesForSaveOperation: invokes), and because:
		# - Sketch has a "Save a Copy As..." file menu item that results in NSSaveToOperations,
		# we know that that the type names we have to handle here include:
		# - SKTDocumentOldTypeName (on Mac OS 10.4) or SKTDocumentNewTypeName (on 10.5), because
		# this application's Info.plist file declares that instances of this class can play the "editor" role for it, and
		# - NSPDFPboardType (on 10.4) or kUTTypePDF (on 10.5) and NSTIFFPboardType (on 10.4)
		# or kUTTypeTIFF (on 10.5), because according to the Info.plist a Sketch document is exportable as them.
		# We use -[NSWorkspace type:conformsToType:] (new in 10.5), which is
		# nearly always the correct thing to do with UTIs, but the arguments are
		# reversed here compared to what's typical. Think about it: this method
		# doesn't know how to write any particular subtype of the supported
		# types, so it should assert if it's asked to. It does however
		# effectively know how to write all of the super types of the supported
		# types (like public.data), and there's no reason for it to refuse to do
		# so. Not particularly useful in the context of an app like Sketch, but
		# correct.
		# If we had reason to believe that +[SKTRenderingView
		# pdfDataWithGraphics:] or +[SKTGraphic propertiesWithGraphics:] could
		# return nil we would have to arrange for *outError to be set to a real
		# value when that happens. If you signal failure in a method that takes an
		# error: parameter and outError!=NULL you must set *outError to something
		# decent.
		workspace = NSWorkspace.sharedWorkspace
		if workspace.type(SKTDocumentNewTypeName, conformsToType: typeName) || typeName == SKTDocumentOldTypeName
			# Convert the contents of the document to a property list and then
			# flatten the property list.
			properties = {	SKTDocumentVersionKey	=> SKTDocumentCurrentVersion,
							SKTDocumentGraphicsKey	=> SKTGraphic.propertiesWithGraphics(graphics),
							SKTDocumentPrintInfoKey => NSArchiver.archivedDataWithRootObject(printInfo)
						 }

			# data = NSPropertyListSerialization.dataFromPropertyList(properties, format: NSPropertyListBinaryFormat_v1_0, 
																		# errorDescription: nil)
																				
			data = NSPropertyListSerialization.dataWithPropertyList(properties, format: NSPropertyListXMLFormat_v1_0, options: 0, error: nil)

		elsif workspace.type(kUTTypePDF, conformsToType: typeName) || typeName == NSPDFPboardType
			data = SKTRenderingView.pdfDataWithGraphics(graphics)
		else
			data = SKTRenderingView.tiffDataWithGraphics(graphics, error: outError)
		end
	
		return data;
	end

	def printInfo= printInfo
		# Do the regular Cocoa thing, but also be KVO-compliant for canvasSize, which is derived from the print info.
		willChangeValueForKey(SKTDocumentCanvasSizeKey)
		super
		didChangeValueForKey(SKTDocumentCanvasSizeKey)
	end

	def printOperationWithSettings (printSettings, error: outError)
		# Figure out a title for the print job. It will be used with the .pdf file
		# name extension in a save panel if the user chooses Save As PDF... in the
		# print panel, or in a similar way if the user hits the Preview button in
		# the print panel, or for any number of other uses the printing system might
		# put it to. We don't want the user to see file names like "My Great
		# Sketch.sketch2.pdf", so we can't just use [self displayName], because the
		# document's file name extension might not be hidden. Instead, because we
		# know that all valid Sketch documents have file name extensions, get the
		# last path component of the file URL and strip off its file name extension,
		# and use what's left.	If this document doesn't have a file associated with it. Just use
		# -displayName after all. It will be "Untitled" or "Untitled 2" or
		# something, which is fine.
		printJobTitle = fileURL ? File.basename(fileURL.path, ".*") :  displayName

		# Create a view that will be used just for printing.
		documentSize = canvasSize
		rect = NSMakeRect(0.0, 0.0, documentSize.width, documentSize.height)
		renderingView = SKTRenderingView.alloc.initWithFrame(rect, graphics: graphics, printJobTitle: printJobTitle)
	
		# Create a print operation.
		printOperation = NSPrintOperation.printOperationWithView(renderingView,	 printInfo: printInfo)
	
		# Specify that the print operation can run in a separate thread. This will
		# cause the print progress panel to appear as a sheet on the document
		# window.
		printOperation.canSpawnSeparateThread = true
	
		# Set any print settings that might have been specified in a Print Document
		# Apple event. We do it this way because we shouldn't be mutating the result
		# of printInfo here, and using the result of printOperation.printInfo,
		# a copy of the original print info, means we don't have to make
		# yet another temporary copy of printInfo.
		printOperation.printInfo.dictionary.addEntriesFromDictionary(printSettings)
	
	   # Nothing in this method can fail, so we never return nil, so we don't have to worry about setting *outError.
		return printOperation;
	end

	def makeWindowControllers
		# Start off with one document window.
		addWindowController(SKTWindowController.new)
	end

	# Undo
	def setGraphicProperties (propertiesPerGraphic)
		# The passed-in dictionary is keyed by graphic with values that are
		# dictionaries of properties, keyed by key-value coding key.

		# Use a relatively unpopular method. Here we're effectively "casting" a key
		# path to a key (see how these dictionaries get built in
		# -observeValueForKeyPath:ofObject:change:context:). It had better really be
		# a key or things will get confused. For example, this is one of the things
		# that would need updating if -[SKTGraphic keysForValuesToObserveForUndo]
		# someday becomes -[SKTGraphic keyPathsForValuesToObserveForUndo].
		propertiesPerGraphic.each do |graphic, graphicProperties|
			graphic.setValuesForKeysWithDictionary(graphicProperties)
		end
	end

	def observeUndoManagerCheckpoint (notification)
		# Start the coalescing of graphic property changes over.
		@undoGroupHasChangesToMultipleProperties = false
		@undoGroupPresentablePropertyName = nil
		@undoGroupOldPropertiesPerGraphic = nil
		@undoGroupInsertedGraphics = nil
	end

	def startObservingGraphics (graphics)
		# Each graphic can have a different set of properties that need to be observed.
		graphics.each do |graphic|
			keys = graphic.keysForValuesToObserveForUndo
			keys.allObjects.each do |key|
				# We use NSKeyValueObservingOptionOld because when something changes we
				# want to record the old value, which is what has to be set in the undo
				# operation. We use NSKeyValueObservingOptionNew because we compare the
				# new value against the old value in an attempt to ignore changes that
				# aren't really changes.
				graphic.addObserver(@documentUndoObserver, forKeyPath: key, 
											options: (NSKeyValueObservingOptionNew | NSKeyValueObservingOptionOld),			
											context: nil)
			end

			# The set of properties to be observed can itself change.
			graphic.addObserver(@documentUndoKeysObserver, forKeyPath: SKTGraphicKeysForValuesToObserveForUndoKey, 
									options: (NSKeyValueObservingOptionNew | NSKeyValueObservingOptionOld),					
									context: nil)
		end
	end

	def stopObservingGraphics (graphics)
		# Do the opposite of what's done in -startObservingGraphics:.
		graphics.each do |graphic|
			keys = graphic.keysForValuesToObserveForUndo
			keys.allObjects.each {|key| graphic.removeObserver(@documentUndoObserver, forKeyPath: key)}
		end
	end
	
	def documentUndoKeysDidChange (keyPath, observedObject, change)
		# The set of properties that we should be observing has changed for some graphic. Stop or start observing.
		oldKeys = change[NSKeyValueChangeOldKey].allObjects
		newKeys = change[NSKeyValueChangeNewKey].allObjects
		
		oldKeys.each do |key|
			observedObject.removeObserver(@documentUndoObserver, forKeyPath: key) if !newKeys.find_index(key)
		end
		
		newKeys.each do |key|
			 if !oldKeys.find_index(key)
				observedObject.addObserver(@documentUndoObserver, forKeyPath: key, 
											options: (NSKeyValueObservingOptionNew | NSKeyValueObservingOptionOld),
											context: nil)
			end
		end	
	end
	
	def documentUndoDidChange (keyPath, observedObject, change)
		# The value of some graphic's property has changed. Don't waste memory by
		# recording undo operations affecting graphics that would be removed during
		# undo anyway. In Sketch this check matters when you use a creation tool to
		# create a new graphic and then drag the mouse to resize it; there's no
		# reason to record a change of "bounds" in that situation.
		graphic = observedObject
		if !(@undoGroupInsertedGraphics && @undoGroupInsertedGraphics[graphic])
			# Ignore changes that aren't really changes. Now that Sketch's inspector
			# panel allows you to change a property of all selected graphics at once
			# (it didn't always, as recently as the version that appears in Mac OS
			# 10.4's /Developer/Examples/AppKit), it's easy for the user to cause a
			# big batch of SKTGraphics to be sent -setValue:forKeyPath: messages that
			# don't do anything useful. Try this simple example: create 10 circles,
			# and set all but one to be filled. Select them all. In the inspector
			# panel the Fill checkbox will show the mixed state indicator (a dash).
			# Click on it. Cocoa's bindings machinery sends [theCircle
			# setValue:[NSNumber numberWithBOOL:YES]
			# forKeyPath:SKTGraphicDrawingFillKey] to each selected circle. KVO
			# faithfully notifies this SKTDocument, which is observing all of its
			# graphics, for each circle object, even though the old value of the
			# SKTGraphicDrawingFillKey property for 9 out of the 10 circles was
			# already YES. If we didn't actively filter out useless notifications like
			# these we would be wasting memory by recording undo operations that don't
			# actually do anything. How much processor time does this memory
			# optimization cost? We don't know, because we haven't measured it. The
			# use of NSKeyValueObservingOptionNew in -startObservingGraphics:, which
			# makes NSKeyValueChangeNewKey entries appear in change dictionaries,
			# definitely costs something when KVO notifications are sent (it costs
			# virtually nothing at observer registration time). Regardless, it's
			# probably a good idea to do simple memory optimizations like this as
			# they're discovered and debug just enough to confirm that they're saving
			# the expected memory (and not introducing bugs). Later on it will be
			# easier to test for good responsiveness and sample to hunt down processor
			# time problems than it will be to figure out where all the darn memory
			# went when your app turns out to be notably RAM-hungry (and therefore
			# slowing down _other_ apps on your user's computers too, if the problem
			# is bad enough to cause paging).
			# Is this a premature optimization? No. Leaving out this very simple
			# check, because we're worried about the processor time cost of using
			# NSKeyValueChangeNewKey, would be a premature optimization.
			newValue = change[NSKeyValueChangeNewKey]
			oldValue = change[NSKeyValueChangeOldKey]
			if newValue != oldValue
				# Is this the first observed graphic change in the current undo group?
				if !@undoGroupOldPropertiesPerGraphic
					# We haven't recorded changes for any graphics at all since the
					# last undo manager checkpoint. Get ready to start collecting
					# them.
					@undoGroupOldPropertiesPerGraphic = {}

					# Register an undo operation for any graphic property changes
					# that are going to be coalesced between now and the next
					# invocation of -observeUndoManagerCheckpoint:. The fact that
					# the object: argument here must really be an object is why
					# _undoGroupOldPropertiesPerGraphic is an SKTMapTableOwner
					# instead of just an NSMapTable.
					undoManager.registerUndoWithTarget(self,  selector: 'setGraphicProperties:',
					 									object: @undoGroupOldPropertiesPerGraphic)
				end
				
				# Find the dictionary in which we're recording the old values of
				# properties for the changed graphic.
				oldGraphicProperties = @undoGroupOldPropertiesPerGraphic[graphic]
				if !oldGraphicProperties
					# We have to create a dictionary to hold old values for the
					# changed graphic. -[NSMutableDictionary setObject:forKey:] always
					# makes a copy of the key object, but we don't want to make copies
					# of SKTGraphics here, so we can't use NSMutableDictionary. That's
					# why _undoGroupOldPropertiesPerGraphic uses NSMapTable despite
					# the hassle of having to wrap it in SKTMapTableOwner.
					oldGraphicProperties = {}
					@undoGroupOldPropertiesPerGraphic[graphic] = oldGraphicProperties
				end

				# Record the old value for the changed property, unless an older
				# value has already been recorded for the current undo group. Here
				# we're "casting" a KVC key path to a dictionary key, but that
				# should be OK. -[NSMutableDictionary setObject:forKey:] doesn't
				# know the difference.
				oldGraphicProperties[keyPath] = oldValue if !oldGraphicProperties[keyPath]
				
				# Don't set the undo action name during undoing and redoing. In
				# Sketch, SKTGraphicView sometimes overwrites whatever action name we
				# set up here with something more specific (as in, "Move" or "Resize"
				# instead of "Change of Bounds"), but only during the building of the
				# original undo action. During undoing and redoing SKTGraphicView
				# doesn't get a chance to do that desirable overwriting again. Just
				# leave the action name alone during undoing and redoing and the
				# action name from the original undo group will continue to be used.
				if !undoManager.undoing? && !undoManager.redoing?
					# What's the human-readable name of the property that's just been
					# changed? Here we're effectively "casting" a key path to a key. It
					# had better really be a key or things will get confused. For
					# example, this is one of the things that would need updating if
					# -[SKTGraphic keysForValuesToObserveForUndo] someday becomes
					# -[SKTGraphic keyPathsForValuesToObserveForUndo].
					graphicClass = graphic.class
					presentablePropertyName = graphicClass.presentablePropertyNameForKey(keyPath)
					if !presentablePropertyName
						# Someone overrode -[SKTGraphic keysForValuesToObserveForUndo] but
						# didn't override +[SKTGraphic presentablePropertyNameForKey:] to
						# match. Help debug a little. Hopefully the SKTGraphic public
						# interface makes it so that you only have to test a little bit to
						# find bugs like this.
						graphicClassName = graphicClass.inspect
						raise "Internal Inconsistency"
						# [NSException raise:NSInternalInconsistencyException format:@"[[%@ class] keysForValuesToObserveForUndo] returns a set that includes @\"%@\", but [[%@ class] presentablePropertyNameForKey:@\"%@\"] returns nil.", graphicClassName, keyPath, graphicClassName, keyPath];
					end

					# Have we set an action name for the current undo group yet?
					if @undoGroupPresentablePropertyName || @undoGroupHasChangesToMultipleProperties
						# Yes. Have we already determined that we have to use a generic
						# undo action name, and set it? If so, there's nothing to do.
						if !@undoGroupHasChangesToMultipleProperties
							# So far we've set an action name for the current undo group
							# that mentions a specific property. Is the property that's just
							# been changed the same one mentioned in that action name
							# (regardless of which graphic has been changed)? If so, there's
							# nothing to do.
							if @undoGroupPresentablePropertyName != presentablePropertyName
								# The undo action is going to restore the old values of
								# different properties. Set a generic undo action name and
								# record the fact that we've done so.
								undoManager.actionName = NSLocalizedStringFromTable("Change of Multiple Graphic Properties", 
																						"UndoStrings",
																						"Generic action name for complex graphic property changes.")
								@undoGroupHasChangesToMultipleProperties = true

								# This is useless now.
								@undoGroupPresentablePropertyName = nil
								@undoGroupPresentablePropertyName = nil
							end
						end
					else
						# So far the action of the current undo group is going to be the
						# restoration of the value of one property. Set a specific undo
						# action name and record the fact that we've done so.
						undoManager.actionName = NSLocalizedStringFromTable("Change of #{presentablePropertyName}", "UndoStrings", "Specific action name for simple graphic property changes. The argument is the name of a property.")
						@undoGroupPresentablePropertyName = presentablePropertyName
					end
				end
			end
		end		
	end

end

=begin

Derived from Apple's sample code for Sketch and converted to MacRuby by Dave Baldwin.

Additional comments from corresponding header file:

State that's used by the undo machinery. It all gets cleared out each time the undo manager sends a checkpoint notification.
_undoGroupInsertedGraphics is the set of graphics that have been inserted, if any have been inserted.
_undoGroupOldPropertiesPerGraphic is a dictionary whose keys are graphics and whose values are other dictionaries, each of which
contains old values of graphic properties, if graphic properties have changed. It uses an NSMapTable instead of an
NSMutableDictionary so we can set it up not to copy the graphics that are used as keys, something not possible with
NSMutableDictionary. And then because NSMapTables were not objects in Mac OS 10.4 and earlier we have to wrap them in NSObjects
that can be reference-counted by NSUndoManager, hence SKTMapTableOwner.   In MacRuby a hash is used instead of an NSMapTable - I am not sure what the problem NSMapTable is trying to solve (weak references?) but one of the nice things about Ruby is just to let the memory take care of itself.

_undoGroupPresentablePropertyName is the result of invoking +[SKTGraphic presentablePropertyNameForKey:] for changed graphics,
if the result of each invocation has been the same so far, nil otherwise. _undoGroupHasChangesToMultipleProperties is YES if
changes have been made to more than one property, as determined by comparing the results of invoking +[SKTGraphic
presentablePropertyNameForKey:] for changed graphics, NO otherwise.

NSMutableSet *_undoGroupInsertedGraphics; 
SKTMapTableOwner *_undoGroupOldPropertiesPerGraphic; 
NSString *_undoGroupPresentablePropertyName; 
BOOL _undoGroupHasChangesToMultipleProperties;.

This class is KVC and KVO compliant for these keys:

"canvasSize" (an NSSize-containing NSValue; read-only) - The size of the document's canvas. This is derived from the currently selected paper size and document margins.

"graphics" (an NSArray of SKTGraphics; read-write) - the graphics of the document.

In Sketch the graphics property of each SKTGraphicView is bound to the graphics property of the document whose contents its presented. 

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