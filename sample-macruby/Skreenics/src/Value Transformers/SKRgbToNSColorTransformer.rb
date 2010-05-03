# SKRgbToNSColorTransformer.rb
# Skreenics
#
# Created by naixn on 29/04/10.
# Copyright 2010 Thibault Martin-Lagardette. All rights reserved.

class SKRgbToNSColorTransformer < NSValueTransformer
    def self.transformedValueClass
        return NSColor
    end

    def self.allowsReverseTransformation
        return true
    end

    def transformedValue(value)
        return nil if value.nil? or not value.isKindOfClass(NSDictionary)
        return NSColor.colorWithCalibratedRed(value["Red"],
                                       green: value["Green"],
                                        blue: value["Blue"],
                                       alpha: value["Alpha"])
    end

    def reverseTransformedValue(value)
        return nil if value.nil? or value.isKindOfClass(NSColor) == false

        redPtr = Pointer.new_with_type('d')
        greenPtr = Pointer.new_with_type('d')
        bluePtr = Pointer.new_with_type('d')
        alphaPtr = Pointer.new_with_type('d')
        value.colorUsingColorSpaceName(NSCalibratedRGBColorSpace).getRed(redPtr,
                                                                  green: greenPtr,
                                                                   blue: bluePtr,
                                                                  alpha: alphaPtr)
        return {
            "Red" => redPtr[0],
            "Green" => greenPtr[0],
            "Blue" => bluePtr[0],
            "Alpha" => alphaPtr[0]
        }
    end
end

