# SKPreferencesController.rb
# Skreenics
#
# Created by naixn on 28/04/10.
# Copyright 2010 Thibault Martin-Lagardette. All rights reserved.

class SKPreferencesController
    attr_accessor :window
    attr_accessor :menuItem_outputFolder
    attr_accessor :menuItem_sameAsVideoFolder
    attr_accessor :popup_downloadFolder
    attr_accessor :fileTypes

    @@availableFileTypes = [
        {:displayName => "PNG",  :extension => "png", :file_type => NSPNGFileType},
        {:displayName => "JPEG", :extension => "jpg", :file_type => NSJPEGFileType}
    ]

    def init
        if super
            @fileTypes = @@availableFileTypes.map { |ftype| ftype[:displayName] }
            self
        end
    end

    def self.imageFileType
        imageFormatFromPrefs = NSUserDefaults.standardUserDefaults[KSKImageFormatPrefKey]
        ftype = @@availableFileTypes.select { |ft| ft[:displayName] == imageFormatFromPrefs }.first
        unless ftype.nil?
            return ftype[:file_type]
        end
        return NSPNGFileType
    end

    def self.imageFileExtension
        imageFormatFromPrefs = NSUserDefaults.standardUserDefaults[KSKImageFormatPrefKey]
        ftype = @@availableFileTypes.select { |ft| ft[:displayName] == imageFormatFromPrefs }.first
        unless ftype.nil?
            return ftype[:extension]
        end
        return "png"
    end

    #pragma mark UI

    def showPreferences(sender)
        @window.makeKeyAndOrderFront(self)
    end

    def awakeFromNib
        selectMenuItemFromPrefs
    end

    def selectMenuItemFromPrefs
        if NSUserDefaults.standardUserDefaults[KSKPreferMovieFileFolderPrefKey]
            popup_downloadFolder.selectItem(menuItem_sameAsVideoFolder)
        else
            popup_downloadFolder.selectItem(menuItem_outputFolder)
        end
    end

    def setSaveFolder(sender)
        NSUserDefaults.standardUserDefaults[KSKPreferMovieFileFolderPrefKey] = 
            (popup_downloadFolder.selectedItem == menuItem_sameAsVideoFolder)
    end

    def folderSheetClosed(openPanel, returnCode:code, contextInfo:info)
        if code == NSOKButton
            popup_downloadFolder.selectItem(menuItem_outputFolder)
            NSUserDefaults.standardUserDefaults[KSKOuputFolderPrefKey] = openPanel.URLs[0].path
            setSaveFolder(self)
        else
            selectMenuItemFromPrefs
        end
    end

    def openSelectOutputFolderSheet(sender)
        panel = NSOpenPanel.openPanel
        panel.setTitle("Select Save Folder")
        panel.setPrompt("Select")
        panel.setAllowsMultipleSelectio(false)
        panel.setCanChooseFiles(false)
        panel.setCanChooseDirectories(true)
        panel.setCanCreateDirectories(true)
        panel.beginSheetForDirectory(nil,
                               file: nil,
                              types: nil,
                     modalForWindow: window,
                      modalDelegate: self,
                     didEndSelector: "folderSheetClosed:returnCode:contextInfo:",
                        contextInfo: nil)
    end
end
