# SkreenicsAppDelegate.rb
# Skreenics
#
# Created by naixn on 27/04/10.
# Copyright 2010 Thibault Martin-Lagardette. All rights reserved.

require "ExpandedPathToIconTransformer.rb"
require "ExpandedPathToPathTransformer.rb"
require "SKRgbToNSColorTransformer.rb"

### +initialize
# We want to be able to provide alpha colors
NSColor.setIgnoresAlpha(false)
# Set transformers for the prefs
NSValueTransformer.setValueTransformer(ExpandedPathToIconTransformer.alloc.init, forName: "ExpandedPathToIconTransformer")
NSValueTransformer.setValueTransformer(ExpandedPathToPathTransformer.alloc.init, forName: "ExpandedPathToPathTransformer")
# Set the default RGB to NSColor transformer
NSValueTransformer.setValueTransformer(SKRgbToNSColorTransformer.alloc.init, forName: "SKRgbToNSColorTransformer")
# And now we can register our user defaults
NSUserDefaults.standardUserDefaults.registerDefaults(NSDictionary.dictionaryWithContentsOfFile(NSBundle.mainBundle.pathForResource("UserDefaults", ofType: "plist")))


class SkreenicsAppDelegate
    attr_accessor :preferencesController
    attr_accessor :videoView
    attr_accessor :videoTableView
    attr_accessor :suspendToolbarItem
    attr_accessor :suspendButton
    attr_accessor :window

    def init
        if super
            @userDefaults = NSUserDefaults.standardUserDefaults
            @videoCollection = []
            @operationQueue = NSOperationQueue.alloc.init
            @operationQueue.setMaxConcurrentOperationCount(@userDefaults[KSKMaxConcurrentOperationsPrefKey].integerValue)
            @acceptableMovieTypes = QTMovie.movieTypesWithOptions(QTIncludeCommonTypes)
            self
        end
    end

    def applicationDidFinishLaunching(aNotification)
        @userDefaults.addObserver(self, forKeyPath: KSKMaxConcurrentOperationsPrefKey, options: NSKeyValueObservingOptionNew, context: nil)
    end

    def addVideoFromPath(path)
        return unless @acceptableMovieTypes.containsObject(NSWorkspace.sharedWorkspace.typeOfFile(path, error: nil))

        fullPath = path.stringByExpandingTildeInPath
        videoItem = SKVideoItem.alloc.initWithPath(fullPath)
        videoItem.addObserverForInterestingKeyPaths(self)
        #
        @videoCollection << videoItem
        @videoTableView.reloadData
        #
        op = SKGenerateThumbnailOperation.alloc.initWithVideoItem(videoItem)
        videoItem.setAssociatedOperation(op)
        @operationQueue.addOperation(op)
    end

    def addVideosFromFolder(folderPath, recursive:recursive)
        if recursive
            files = NSFileManager.defaultManager.subpathsOfDirectoryAtPath(folderPath, error: nil)
        else
            files = NSFileManager.defaultManager.contentsOfDirectoryAtPath(folderPath, error: nil)
        end
        files.each do |file|
            addVideoFromPath(folderPath.stringByAppendingPathComponent(file))
        end
    end

    def addPathElement(path)
        ptr = Pointer.new_with_type('B')
        NSFileManager.defaultManager.fileExistsAtPath(path, isDirectory: ptr)
        pathIsDirectory = ptr[0]

        if pathIsDirectory
            addVideosFromFolder(path, recursive: @userDefaults.boolForKey(KSKAddSubfoldersOnDropPrefKey))
        else
            addVideoFromPath(path)
        end
    end

    def toggleSuspendedStatus(sender)
        if @operationQueue.isSuspended == false
            @operationQueue.setSuspended(true)
            @suspendToolbarItem.setLabel("Resume")
            @suspendButton.setImage(NSImage.imageNamed("NSRightFacingTriangleTemplate"))
        else
            @operationQueue.setSuspended(false)
            @suspendToolbarItem.setLabel("Suspend")
            @suspendButton.setImage(NSImage.imageNamed("NSRemoveTemplate"))
        end
    end

    def removeSelectedItem(sender)
        selectedSet = @videoTableView.selectedRowIndexes
        if selectedSet.count
            index = selectedSet.firstIndex
            while (index <= selectedSet.lastIndex) do
                videoItem = @videoCollection[index] 
                videoItem.cleanup
                videoItem.removeObserverForInterestingKeyPaths(self)
                videoItem.associatedOperation.cancel
                index = selectedSet.indexGreaterThanIndex(index)
            end
            @videoCollection.removeObjectsAtIndexes(selectedSet)
            @videoTableView.deselectAll(self)
            @videoTableView.reloadData
        end
    end

    def clearVideoList(sender)
        removeSet = NSMutableIndexSet.indexSet
        @videoCollection.each do |videoItem|
            if videoItem.isFinished == true or videoItem.isErroneous == true
                videoItem.cleanup
                videoItem.removeObserverForInterestingKeyPaths(self)
                removeSet.addIndex(@videoCollection.indexOfObject(videoItem))
            end
        end
        @videoCollection.removeObjectsAtIndexes(removeSet)
        @videoTableView.reloadData
    end

    def openPanelDidEnd(panel, returnCode:returnCode, contextInfo:contextInfo)
        if returnCode == NSOKButton
            panel.URLs.each do |url|
                addPathElement(url.path)
            end
        end
    end

    def displayOpenPanel(sender)
        openPanel = NSOpenPanel.openPanel
        openPanel.setTitle("Open Video Files")
        openPanel.setCanChooseFiles(true)
        openPanel.setCanChooseDirectories(true)
        openPanel.setAllowsMultipleSelection(true)
        openPanel.beginForDirectory(nil,
                              file: nil,
                             types: QTMovie.movieFileTypes(QTIncludeCommonTypes),
                  modelessDelegate: self,
                    didEndSelector: "openPanelDidEnd:returnCode:contextInfo:",
                       contextInfo: nil)
    end

    def mainThreadObserveValueWithAttributes(attributes)
        keyPath = attributes[KSKObserverKeyPathKey]
        if keyPath.isEqualToString(KSKVideoItemProgressValuePath)
            @videoTableView.reloadData
        elsif keyPath.isEqualToString(KSKMaxConcurrentOperationsPrefKey)
            @operationQueue.setMaxConcurrentOperationCount(@userDefaults[KSKMaxConcurrentOperationsPrefKey].integerValue)
        end
    end

    def observeValueForKeyPath(keyPath, ofObject:object, change:change, context:context)
        contextValue = NSValue.valueWithPointer(context)
        observedAttributes = {
            KSKObserverKeyPathKey => keyPath,
            KSKObserverObjectKey  => object,
            KSKObserverChangeKey  => change,
            KSKObserverContextKey => contextValue
        }
        performSelectorOnMainThread("mainThreadObserveValueWithAttributes:",
                        withObject: observedAttributes,
                     waitUntilDone: false)
    end

    #pragma mark Drag Delegate Protocol

    def addDragPathElement(path)
        addPathElement(path)
    end

    #pragma mark Table View Data Source

    def numberOfRowsInTableView(tblView)
        @videoCollection.count
    end

    def tableView(aTableView, objectValueForTableColumn:aTableColumn, row:rowIndex)
        @videoCollection[rowIndex]
    end

    #pragma mark Table View Delegate

    def tableView(aTableView, willDisplayCell:cell, forTableColumn:aTableColumn, row:rowIndex)
        cell.setRepresentedObject(@videoCollection[rowIndex])
    end

    #pragma mark Quit

    def windowShouldClose(sender)
        NSApp.terminate(self)
        return false
    end

    def applicationShouldTerminateAfterLastWindowClosed(theApplication)
        return true
    end

    def alertDidEnd(alert, returnCode:returnCode, contextInfo:contextInfo)
        NSApp.replyToApplicationShouldTerminate(returnCode == NSOKButton)
    end

    def applicationShouldTerminate(sender)
        if @operationQueue.operations.count > 0 and @operationQueue.isSuspended == false
            alert = NSAlert.alertWithMessageText("Skreenics is still running",
                                  defaultButton: "Cancel & Continue",
                                alternateButton: "Abort & Quit",
                                    otherButton: nil,
                      informativeTextWithFormat: "There are pending and/or running jobs. Are you sure you want to quit?.")
            alert.beginSheetModalForWindow(@window,
                            modalDelegate: self,
                           didEndSelector: "alertDidEnd:returnCode:contextInfo:",
                              contextInfo: nil)
            return NSTerminateLater
        end
        return NSTerminateNow
    end
end
