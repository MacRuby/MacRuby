# MacRuby implementation of iconv.
#
# This file is covered by the Ruby license. See COPYING for more details.
# 
# Copyright (C) 2009, Apple Inc. All rights reserved.

framework 'Foundation'

class Iconv
  # TODO: lots of things... help!

  def self.iconv(to, from, str)
    new(from, to).iconv(str)
  end

  def initialize(to, from, options=nil)
    @to_enc = ns_encoding(to)
    @from_enc = ns_encoding(from) 
  end

  def iconv(str, start=0, length=-1)
    # Not sure if it's the right thing to do...
    data = CFStringCreateExternalRepresentation(nil, str, @to_enc, 0)
    if data.nil?
      raise "can't retrieve data from `#{str}'"
    end
    dest = CFStringCreateFromExternalRepresentation(nil, data, @to_enc)
    if dest.nil?
      raise "can't convert data from `#{str}'"
    end
    CFRelease(data)
    CFRelease(dest)
    dest.mutableCopy 
  end

  private

  ENCS = {}
  def ns_encoding(str)
    if ENCS.empty?
      # Build database.
      ptr = CFStringGetListOfAvailableEncodings()
      i = 0
      while (enc = ptr[i]) != KCFStringEncodingInvalidId
        enc_name = CFStringConvertEncodingToIANACharSetName(enc)
        ENCS[enc_name] = enc
        i += 1
      end
    end
    str = str.downcase
    enc = ENCS[str]
    if enc.nil?
      raise "unrecognized encoding `#{enc}'"
    end
    return enc
  end
end
