=begin
/******************************************************************************
 * $Id: ExpandedPathToIconTransformer.m 6974 2008-10-28 00:08:49Z livings124 $
 * 
 * Copyright (c) 2007-2008 Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/
=end

# ExpandedPathToIconTransformer.rb
# Skreenics
#
# Created by naixn on 29/04/10.
# Copyright 2010 Thibault Martin-Lagardette. All rights reserved.

class ExpandedPathToIconTransformer < NSValueTransformer
    def self.transformedValueClass
        return NSImage
    end

    def self.allowsReverseTransformation
        return false
    end

    def transformedValue(value)
        return nil if value.nil?

        path = value.stringByExpandingTildeInPath
        # show a folder icon if the folder doesn't exist
        if not NSFileManager.defaultManager.fileExistsAtPath(path) and path.pathExtension.isEqualToString("")
            icon = NSWorkspace.sharedWorkspace.iconForFileType(NSFileTypeForHFSTypeCode('fldr'))
        else
            icon = NSWorkspace.sharedWorkspace.iconForFile(value.stringByExpandingTildeInPath)
        end
        icon.setScalesWhenResized(true)
        icon.setSize(NSMakeSize(16.0, 16.0))
        return icon
    end
end
