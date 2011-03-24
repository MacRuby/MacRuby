# SKProgressIndicator.rb
# Skreenics
#
# Created by naixn on 29/04/10.
# Copyright 2010 Thibault Martin-Lagardette. All rights reserved.

SKProgressIndicatorPreferredThickness      = 14,
SKProgressIndicatorPreferredSmallThickness = 10,
SKProgressIndicatorPreferredLargeThickness = 18
KSKProgressIndicatorError = -42.0

class SKProgressAnimator < NSAnimation
    def initWithDelegate(delegate, from:from, to:to)
        if initWithDuration(0.20, animationCurve: NSAnimationEaseInOut)
            setDelegate(delegate)
            setFrameRate(60.0)
            setAnimationBlockingMode(NSAnimationNonblocking)
            @originalProgress = from
            @finalProgress = to
            self
        end
    end

    def setCurrentProgress(progress)
        super

        value = currentValue
        if animationCurve == NSAnimationEaseOut
            value = 1.0 - value
        end
        delegate.setProgressValue(@originalProgress + (@finalProgress - @originalProgress) * value)
    end
end

class SKProgressIndicator < NSView
    attr_accessor :maxProgressValue

    def init
        if super
            @progressValue = 0.0
            @maxProgressValue = 100.0
            @progressAnimation = nil
            self
        end
    end

    def initWithCoder(coder)
        if super
            @progressValue = 0.0
            @maxProgressValue = 100.0
            @progressAnimation = nil
            self
        end
    end

    #pragma mark Progress value

    def progressValue
        return @progressValue
    end

    def setProgressValue(value)
        if value > @maxProgressValue
            value = @maxProgressValue
        elsif (value < 0.0 and value != KSKProgressIndicatorError)
            value = 0.0
        end
        @progressValue = value
        setNeedsDisplay(true)
    end

    def setAnimatedProgressValue(value)
        if value != @progressValue
            if @progressValue != KSKProgressIndicatorError
                if @progressAnimation
                    @progressAnimation.stopAnimation
                    @progressAnimation = nil
                end
                @progressAnimation = SKProgressAnimator.alloc.initWithDelegate(self, from: @progressValue, to: value)
                @progressAnimation.startAnimation
            else
                setProgressValue(value)
            end
        end
    end

    #pragma mark Progress Indicator delegate

    def animationDidEnd(animation)
        @progressAnimation = nil
    end

    #pragma mark Draw related

    @@colorComponents = {
        :gradiantHolder => { :red => 0.85, :green => 0.85, :blue => 0.85, :alpha => 1.0 },
        :redGradiant    => { :red => 0.8,  :green => 0.4,  :blue => 0.4,  :alpha => 1.0 },
        :greenGradiant  => { :red => 0.4,  :green => 0.8,  :blue => 0.4,  :alpha => 1.0 },
        :blueGradiant   => { :red => 0.43, :green => 0.64, :blue => 0.93, :alpha => 1.0 }
    }

    def gradiantWithColors(c)
        baseColor = NSColor.colorWithCalibratedRed(c[:red], green: c[:green], blue: c[:blue], alpha: c[:alpha])
        fadedColor1 = NSColor.colorWithCalibratedRed((c[:red] * 0.90), green:(c[:green] * 0.90), blue:(c[:blue] * 0.90), alpha:(c[:alpha]))
        fadedColor2 = NSColor.colorWithCalibratedRed((c[:red] * 0.80), green:(c[:green] * 0.80), blue:(c[:blue] * 0.80), alpha:(c[:alpha]))
        colorList = [baseColor, baseColor, fadedColor1, fadedColor2, baseColor]
        locations = Pointer.new('d', 5)
        locations[0] = 0.0
        locations[1] = 0.5
        locations[2] = 0.8
        locations[3] = 0.9
        locations[4] = 1.0
        return NSGradient.alloc.initWithColors(colorList, atLocations: locations, colorSpace: NSColorSpace.genericRGBColorSpace)
    end

    def drawRect(dirtyRect)
        progressRect = NSMakeRect(0, 0, 0, 0)
        gradiantWithColors(@@colorComponents[:gradiantHolder]).drawInRect(dirtyRect, angle: 90.0)
        if not @progressValue.zero?
            progressRect = dirtyRect
            if @progressValue != KSKProgressIndicatorError
                progressRect.size.width = ((@progressValue * NSWidth(dirtyRect)) / @maxProgressValue).floor
                if @progressValue >= @maxProgressValue
                    gc = @@colorComponents[:greenGradiant]
                else
                    gc = @@colorComponents[:blueGradiant]
                end
            else
                gc = @@colorComponents[:redGradiant]
            end
            gradiantWithColors(gc).drawInRect(progressRect, angle: -90.0)
        end

        NSGraphicsContext.saveGraphicsState
        NSColor.colorWithDeviceWhite(0.0, alpha: 0.2).set
        NSBezierPath.setDefaultLineWidth(1.0)
        NSBezierPath.strokeRect(NSInsetRect(dirtyRect, 0.5, 0.5))
        NSBezierPath.strokeRect(NSInsetRect(progressRect, 0.5, 0.5))
        NSGraphicsContext.restoreGraphicsState
    end
end
