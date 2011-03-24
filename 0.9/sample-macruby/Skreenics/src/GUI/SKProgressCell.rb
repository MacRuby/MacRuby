# SKProgressCell.rb
# Skreenics
#
# Created by naixn on 28/04/10.
# Copyright 2010 Thibault Martin-Lagardette. All rights reserved.

VERTICAL_PADDING = 5.0
HORIZ_PADDING = 13.0
IMAGE_SIZE = 45.0
FILENAME_Y_PADDING = 4.0
PROGRESS_Y_PADDING = 22.0
PROGSTR_Y_PADDING = 37.0
LPADDING = (2 * HORIZ_PADDING) + IMAGE_SIZE
NON_INFO_AREA = (3 * HORIZ_PADDING) + IMAGE_SIZE

class SKProgressCell < NSCell

    def initWithCoder(coder)
        super
        self
    end

    def copyWithZone(zone)
        return super
    end

    #pragma mark AttributedStrings Generation

    def attributedStringForFilename
        filename = (representedObject.filename || "")
        paragraphStyle = NSMutableParagraphStyle.defaultParagraphStyle.mutableCopy
        paragraphStyle.setLineBreakMode(NSLineBreakByTruncatingTail)
        stringAttributes = {
            NSFontAttributeName => NSFont.fontWithName("Lucida Grande", size:11.0),
            NSForegroundColorAttributeName => NSColor.blackColor,
            NSParagraphStyleAttributeName => paragraphStyle
        }
        return NSAttributedString.alloc.initWithString(filename, attributes: stringAttributes)
    end

    def attributedStringForProgress
        progressString = (representedObject.progressString || "")
        paragraphStyle = NSMutableParagraphStyle.defaultParagraphStyle.mutableCopy
        paragraphStyle.setLineBreakMode(NSLineBreakByTruncatingTail)
        stringAttributes = {
            NSFontAttributeName => NSFont.fontWithName("Lucida Grande", size:9.0),
            NSForegroundColorAttributeName => NSColor.grayColor,
            NSParagraphStyleAttributeName => paragraphStyle
        }
        return NSAttributedString.alloc.initWithString(progressString, attributes: stringAttributes)
    end

    #pragma mark Padding calculations

    def infoAreaLeftPadding
        LPADDING
    end

    def infoAreaWidthInBounds(bounds)
        return NSWidth(bounds) - NON_INFO_AREA
    end

    #pragma mark Bounds calculations

    def iconRectForBounds(bounds)
        iconRect = NSMakeRect(0, 0, 0, 0)
        iconRect.size = NSMakeSize(IMAGE_SIZE, IMAGE_SIZE)
        iconRect.origin = bounds.origin
        iconRect.origin.x += HORIZ_PADDING
        iconRect.origin.y += (NSHeight(bounds) / 2) - (IMAGE_SIZE / 2)
        return iconRect
    end

    def progressIndicRectForBounds(bounds)
        progressIndicRect = NSMakeRect(0, 0, 0, 0)
        progressIndicRect.size.height = NSProgressIndicatorPreferredThickness
        progressIndicRect.size.width = infoAreaWidthInBounds(bounds)
        progressIndicRect.origin = bounds.origin
        progressIndicRect.origin.x = infoAreaLeftPadding
        progressIndicRect.origin.y += PROGRESS_Y_PADDING
        return progressIndicRect
    end

    def filenameRectForBounds(bounds, withAttributedString:filenameAttributedString)
        filenameRect = NSMakeRect(0, 0, 0, 0)
        filenameRect.size = filenameAttributedString.size
        filenameRect.size.width = infoAreaWidthInBounds(bounds)
        filenameRect.origin = bounds.origin
        filenameRect.origin.x += infoAreaLeftPadding
        filenameRect.origin.y += FILENAME_Y_PADDING
        return filenameRect
    end

    def progressStringRectForBounds(bounds, withAttributedString:progressAttributedString)
        progressStringRect = NSMakeRect(0, 0, 0, 0)
        progressStringRect.size = progressAttributedString.size
        progressStringRect.size.width = infoAreaWidthInBounds(bounds)
        progressStringRect.origin = bounds.origin
        progressStringRect.origin.x = infoAreaLeftPadding
        progressStringRect.origin.y += PROGSTR_Y_PADDING
        return progressStringRect
    end

    #pragma mark Draw

    def drawInteriorWithFrame(frame, inView:controlView)
        filenameAttributedString = attributedStringForFilename
        progressAttributedString = attributedStringForProgress

        # Draw the icon
        iconRect = iconRectForBounds(frame)
        icon = representedObject.icon
        if not icon.nil?
            icon.setFlipped(controlView.isFlipped)
            icon.drawInRect(iconRect,
                  fromRect: NSZeroRect,
                 operation: NSCompositeSourceOver,
                  fraction: 1.0)
        else
            # If there is no icon, just draw a square
            NSBezierPath.strokeRect(iconRect)
        end

        # Draw the filename
        filenameRect = filenameRectForBounds(frame, withAttributedString: filenameAttributedString)
        filename = representedObject.filename
        if filename.length > 0
            filenameAttributedString.drawInRect(filenameRect)
        end

        # Draw the progress indicator
        progressIndicRect = progressIndicRectForBounds(frame)
        progressIndicator = representedObject.progressIndicator
        if progressIndicator
            if progressIndicator.superview.nil?
                controlView.addSubview(progressIndicator)
            end
            progressIndicator.setFrame(progressIndicRect)
        end

        # Draw the progress string
        progressStringRect = progressStringRectForBounds(frame, withAttributedString: progressAttributedString)
        progressString = representedObject.progressString
        if progressString.length > 0
            progressAttributedString.drawInRect(progressStringRect)
        end

        # Tell the view to display the changes
        controlView.setNeedsDisplay
    end
end

