# SKGenerateThumbnailOperation.rb
# Skreenics
#
# Created by naixn on 28/04/10.
# Copyright 2010 Thibault Martin-Lagardette. All rights reserved.

class SKGenerateThumbnailOperation < NSOperation
    def initWithVideoItem(videoItem)
        if init
            @userDefaults = NSUserDefaults.standardUserDefaults
            @videoItem = videoItem
            @rows = @userDefaults[KSKNumberOfRowsPrefKey].integerValue
            @cols = @userDefaults[KSKNumberOfColumnsPrefKey].integerValue
            self
        end
    end

    #pragma mark Attributed Strings Generation

    def videoResolutionStringFromMovie(movie, withAttributes:stringAttributes)
        videoSize = movie.attributeForKey(QTMovieNaturalSizeAttribute).sizeValue
        videoSizeFormat = "\n\tResolution: %.0fx%.0f" % [videoSize.width, videoSize.height]
        return NSAttributedString.alloc.initWithString(videoSizeFormat, attributes: stringAttributes)
    end

    def videoFileSizeStringFromMovie(movie, withAttributes:stringAttributes)
        videoFileSize = NSFileManager.defaultManager.attributesOfItemAtPath(movie.attributeForKey(QTMovieFileNameAttribute), error: nil).objectForKey(NSFileSize)
        videoFileSizeFormat = "\n\tFilesize: %s" % NSString.stringForFileSize(videoFileSize.unsignedLongLongValue)
        return NSAttributedString.alloc.initWithString(videoFileSizeFormat, attributes: stringAttributes)
    end

    def preferenceColorForKey(key)
        NSValueTransformer.valueTransformerForName("SKRgbToNSColorTransformer").transformedValue(@userDefaults[key])
    end

    def detailsFromMovie(movie)
        movieFilename = movie.attributeForKey(QTMovieFileNameAttribute).lastPathComponent

        # Setup shadow
        descriptionShadow = NSShadow.alloc.init
        descriptionShadow.setShadowOffset(NSMakeSize(1.75, -1.75))
        descriptionShadow.setShadowColor(preferenceColorForKey(KSKImageShadowColorPrefKey))
        descriptionShadow.setShadowBlurRadius(3.0)

        # Create default attributes
        stringAttributes = {
            NSFontAttributeName => NSFont.fontWithName("Arial Bold", size:20.0),
            NSForegroundColorAttributeName => preferenceColorForKey(KSKImageMovieInfoColorPrefKey),
            NSShadowAttributeName => descriptionShadow
        }

        # Init the result with the filename
        resultString = NSMutableAttributedString.alloc.initWithString(movieFilename, attributes: stringAttributes)

        # Change attributes for "sub"-info
        stringAttributes.removeObjectForKey(NSShadowAttributeName)
        stringAttributes[NSFontAttributeName] = NSFont.fontWithName("Arial Bold", size:15.0)

        # Add video resolution
        resultString.appendAttributedString(videoResolutionStringFromMovie(movie, withAttributes: stringAttributes))
        # Add file size
        resultString.appendAttributedString(videoFileSizeStringFromMovie(movie, withAttributes: stringAttributes))

        # Return the final string
        return resultString
    end

    def attributedStringForQTTime(time)
        # Convert the time into a string, and ommit some non-interesting data
        timeString = QTStringFromTime(time)[2...10]

        # Setup string attributes
        stringAttributes = {
            NSFontAttributeName => NSFont.fontWithName("Arial Bold", size:18.0),
            NSForegroundColorAttributeName => NSColor.colorWithCalibratedWhite(1.0, alpha:0.75),
            NSStrokeColorAttributeName => NSColor.colorWithCalibratedWhite(0.0, alpha:0.75),
            NSStrokeWidthAttributeName => NSNumber.numberWithFloat(-5.0)
        }

        return NSAttributedString.alloc.initWithString(timeString, attributes: stringAttributes)
    end

    def prefWidth; @userDefaults.floatForKey(KSKImageFileWidthPrefKey); end
    def prefSpacing; @userDefaults.floatForKey(KSKSpacingBetweenThumbnailsPrefKey); end
    def prefMovieInfo; true; end

    def main
        QTMovie.enterQTKitOnThread

        @videoItem.setNumberOfSteps(5 + @cols * @rows)

        # ----------- Step 0: Init movie
        return if isCancelled
        @videoItem.setProgressString("Opening movie...", incrementProgressValue: false)
        movieFilePath = @videoItem.filepath
        openAttributes = {
            QTMovieFileNameAttribute => movieFilePath,
            QTMovieOpenAsyncOKAttribute => NSNumber.numberWithBool(false)
        }
        movie = QTMovie.alloc.initWithAttributes(openAttributes, error: nil)

        # ----------- Step 1: Check if the movie actually has a movie track
        if isCancelled
            movie.release
            QTMovie.exitQTKitOnThread
            return
        end
        @videoItem.setProgressString("Checking if file has a movie track...", incrementProgressValue: true)
        if (movie.tracksOfMediaType(QTMediaTypeVideo).count == 0 and
                movie.tracksOfMediaType(QTMediaTypeMPEG).count == 0 and
                movie.tracksOfMediaType(QTMediaTypeMovie).count == 0)
            @videoItem.setError("File does not contain a video track")
            movie.release
            QTMovie.exitQTKitOnThread
            return
        end

        # ----------- Step 2: Init some other values
        @videoItem.setProgressString("Preparing...", incrementProgressValue: true)

        # Init some other values
        movieSize = movie.attributeForKey(QTMovieNaturalSizeAttribute).sizeValue
        frameAreaSize = NSMakeSize(0, 0)
        frameAreaSize.width = (prefWidth - ((@cols + 1) * prefSpacing)) / @cols
        frameAreaSize.height = (movieSize.height * frameAreaSize.width) / movieSize.width
        imageSize = NSMakeSize(prefWidth, frameAreaSize.height * @rows + (@rows + 1) * prefSpacing)

        # Get the time we will pad around the movie, and set the initial value
        incrementTime = movie.duration
        incrementTime.timeValue /= (@cols * @rows)
        currentTime = incrementTime
        currentTime.timeValue /= 2.0

        # If we need to display some movie details, we need to generate the
        # attributed string and add its size to the result image size
        if prefMovieInfo
            movieDetails = detailsFromMovie(movie)
            movieDetailsRectOrigin = movieDetails.boundingRectWithSize(NSZeroSize, options:(NSStringDrawingUsesLineFragmentOrigin | NSStringDrawingDisableScreenFontSubstitution))
            imageSize.height += movieDetailsRectOrigin.size.height + prefSpacing
        end

        # ----------- Step 3: create original image
        if isCancelled
            movie.release
            QTMovie.exitQTKitOnThread
            return
        end
        @videoItem.setProgressString("Creating initial image...", incrementProgressValue: true)

        # Allocate the image in which we will draw, erase everything, and set ready to draw
        resultImage = NSImage.alloc.initWithSize(imageSize)
        resultImage.recache
        resultImage.lockFocus

        # Draw background
        preferenceColorForKey(KSKImageBackgroundColorPrefKey).set
        imageRect = NSMakeRect(0, 0, 0, 0)
        imageRect.origin = NSZeroPoint
        imageRect.size = imageSize
        NSBezierPath.fillRect(imageRect)
        NSBezierPath.setDefaultLineWidth(1.5)

        # Draw movie info
        if prefMovieInfo and movieDetails
            movieDetails.drawAtPoint(NSMakePoint(prefSpacing, imageSize.height - prefSpacing - movieDetailsRectOrigin.size.height))
        end

        # Setup the shadow
        thumbnailShadow = NSShadow.alloc.init
        thumbnailShadow.setShadowOffset(NSMakeSize(2.0, -2.0))
        thumbnailShadow.setShadowColor(preferenceColorForKey(KSKImageShadowColorPrefKey))
        thumbnailShadow.setShadowBlurRadius(3.0)

        @rows.times do |row|
            @cols.times do |col|
                # ----------- Step 4: create thumbnail
                if isCancelled
                    resultImage.unlockFocus
                    resultImage.release
                    thumbnailShadow.release
                    movie.release
                    QTMovie.exitQTKitOnThread
                    return
                end
                @videoItem.setProgressString("Processing frame %d of %d..." % [(row * @cols) + col + 1, @rows * @cols],
                    incrementProgressValue: true)

                # Get current frame image, and compute frame position
                currentFrameImage = movie.frameImageAtTime(currentTime)
                x = (col * frameAreaSize.width) + ((col + 1) * prefSpacing)
                y = ((@rows - row - 1) * frameAreaSize.height) + ((@rows - row) * prefSpacing)

                # Draw frame image
                NSGraphicsContext.saveGraphicsState
                thumbnailShadow.set
                drawingRect = NSMakeRect(x, y, frameAreaSize.width, frameAreaSize.height)
                sourceRect = NSMakeRect(0, 0, currentFrameImage.size.width, currentFrameImage.size.height)
                currentFrameImage.drawInRect(drawingRect, fromRect: sourceRect, operation: NSCompositeCopy, fraction: 1.0)
                NSGraphicsContext.restoreGraphicsState

                # Draw border
                NSColor.blackColor.colorWithAlphaComponent(0.75).set
                NSBezierPath.strokeRect(drawingRect)

                # Draw timestamp
                attributedStringForQTTime(currentTime).drawAtPoint(NSMakePoint(x + 5.0, y + 5.0))

                # Get further in the video
                currentTime = QTTimeIncrement(currentTime, incrementTime)
            end
        end

        # We are done drawing on the image
        resultImage.unlockFocus

        # ----------- Step 5: Write result to HD
        if not isCancelled
            imageExtension = SKPreferencesController.imageFileExtension
            imageFileType = SKPreferencesController.imageFileType

            @videoItem.setProgressString("Writing image...", incrementProgressValue: true)

            # Write the result on the hard drive
            if @userDefaults[KSKPreferMovieFileFolderPrefKey]
                savePath = movieFilePath.stringByDeletingPathExtension.stringByAppendingPathExtension(imageExtension)
            else
                outputFolder = @userDefaults[KSKOuputFolderPrefKey].stringByExpandingTildeInPath
                outputFileName = @videoItem.filename.stringByDeletingPathExtension.stringByAppendingPathExtension(imageExtension)
                savePath = outputFolder.stringByAppendingPathComponent(outputFileName)
            end
            repr = NSBitmapImageRep.imageRepWithData(resultImage.TIFFRepresentation)
            repr.representationUsingType(imageFileType, properties: nil).writeToFile(savePath, atomically: true)
        end

        # Since the code is garbage collected, there is no need to release
        # the allocated variables (unlike the Obj-C version)
        # thumbnailShadow.release
        # resultImage.release
        # movie.release

        QTMovie.exitQTKitOnThread

        # ----------- Done
        unless isCancelled
            @videoItem.setProgressString("Done!", incrementProgressValue: true)
        end
    end
end
