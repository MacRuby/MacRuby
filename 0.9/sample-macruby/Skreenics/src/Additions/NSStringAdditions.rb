=begin
/******************************************************************************
 * $Id: NSStringAdditions.m 9140 2009-09-18 03:49:55Z livings124 $
 *
 * Copyright (c) 2005-2009 Transmission authors and contributors
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

# NSStringAdditions.rb
# Skreenics
#
# Created by naixn on 29/04/10.
# Copyright 2010 Thibault Martin-Lagardette. All rights reserved.

class NSString
    def self.stringForFileSize(size)
        if size < 1024
            if size != 1
                return "%lld bytes" % size
            else
                return "1 byte"
            end
        end

        if size < (1024 ** 2)
            convertedSize = size / 1024.0
            unit = "KB"
        elsif size < (1024 ** 3)
            convertedSize = size / (1024.0 ** 2.0)
            unit = "MB"
        elsif size < (1024 ** 4)
            convertedSize = size / (1024.0 ** 3.0)
            unit = "GB"
        else
            convertedSize = size / (1024.0 ** 4.0)
            unit = "TB"
        end

        # attempt to have minimum of 3 digits with at least 1 decimal
        if convertedSize <= 9.995
            return ("%.2f %s" % [convertedSize, unit])
        else
            return ("%.1f %s" % [convertedSize, unit])
        end
    end
end

