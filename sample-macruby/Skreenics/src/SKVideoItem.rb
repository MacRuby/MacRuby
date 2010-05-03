# SKVideoItem.rb
# Skreenics
#
# Created by naixn on 28/04/10.
# Copyright 2010 Thibault Martin-Lagardette. All rights reserved.

class SKVideoItem
    attr_accessor :associatedOperation
    attr_accessor :videoItem

    def initWithPath(path)
        if init
            fullPath = path.stringByExpandingTildeInPath
            @videoItem = {
                KSKFilePathKey          => fullPath,
                KSKFileNameKey          => fullPath.lastPathComponent,
                KSKIconKey              => NSWorkspace.sharedWorkspace.iconForFile(fullPath),
                KSKProgressIndicatorKey => SKProgressIndicator.alloc.init,
                KSKProgressValueKey     => NSNumber.numberWithInt(0),
                KSKProgressStringKey    => "Waiting...",
                KSKNumberOfStepsKey     => NSNumber.numberWithInt(100)
            }
            self
        end
    end

    def copyWithZone(zone)
        return nil
    end

    #pragma mark Misc

    def cleanup
        progressIndicator.removeFromSuperview
    end

    #pragma mark Interesting getters

    def isFinished
        progressValue.isEqualToNumber(numberOfSteps)
    end

    def isErroneous
        progressValue.doubleValue == KSKProgressIndicatorError
    end

    def setNumberOfSteps(steps)
        # We want to perform the change on the main thread for UI changes
        if NSThread.isMainThread == false
            performSelectorOnMainThread("setNumberOfSteps:",
                            withObject: steps,
                         waitUntilDone: true)
        else
            @videoItem[KSKNumberOfStepsKey] = steps
            progressIndicator.setMaxProgressValue(steps.doubleValue)
        end
    end

    def setProgressString(str)
        # We want to perform the change on the main thread for UI changes
        if not NSThread.isMainThread
            performSelectorOnMainThread("setProgressString:",
                            withObject: str,
                         waitUntilDone: true)
        else
            @videoItem[KSKProgressStringKey] = str
        end
    end

    def setProgressValue(progress)
        # We want to perform the change on the main thread for UI changes
        if not NSThread.isMainThread
            performSelectorOnMainThread("setProgressValue:",
                            withObject: progress,
                         waitUntilDone: true)
        else
            @videoItem[KSKProgressValueKey] = progress
            progressIndicator.setAnimatedProgressValue(progress.doubleValue)
        end
    end

    def setProgressString(str, incrementProgressValue:increment)
        setProgressString(str)
        setProgressValue(progressValue + 1.0) if increment
    end

    def setError(errorString)
        setProgressString(errorString)
        setProgressValue(KSKProgressIndicatorError)
    end

    #pragma mark Observing

    def addObserverForInterestingKeyPaths(observer)
        addObserver(observer,
        forKeyPath: KSKVideoItemProgressValuePath,
           options: NSKeyValueObservingOptionNew,
           context: nil)
    end

    def removeObserverForInterestingKeyPaths(observer)
        removeObserver(observer, forKeyPath: KSKVideoItemProgressValuePath)
    end

    #pragma mark Simple getters

    def filepath
        @videoItem[KSKFilePathKey]
    end

    def filename
        @videoItem[KSKFileNameKey]
    end

    def icon
        @videoItem[KSKIconKey]
    end

    def progressIndicator
        @videoItem[KSKProgressIndicatorKey]
    end

    def progressValue
        @videoItem[KSKProgressValueKey]
    end

    def progressString
        @videoItem[KSKProgressStringKey]
    end

    def numberOfSteps
        @videoItem[KSKNumberOfStepsKey]
    end
end
