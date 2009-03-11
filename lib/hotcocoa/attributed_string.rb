module HotCocoa
    class NSRangedProxyAttributeHash
	ATTRIBUTE_KEYS = { :font => NSFontAttributeName,
	                   :paragraph_style => NSParagraphStyleAttributeName,
                           :color => NSForegroundColorAttributeName,
			   :underline_style => NSUnderlineStyleAttributeName,
			   :superscript => NSSuperscriptAttributeName,
			   :background_color => NSBackgroundColorAttributeName,
			   :attachment => NSAttachmentAttributeName,
			   :ligature => NSLigatureAttributeName,
			   :baseline_offset => NSBaselineOffsetAttributeName,
			   :kerning => NSKernAttributeName,
			   :link => NSLinkAttributeName,
			   :stroke_width => NSStrokeWidthAttributeName,
			   :stroke_color => NSStrokeColorAttributeName,
			   :underline_color => NSUnderlineColorAttributeName,
			   :strikethrough_style => NSStrikethroughStyleAttributeName,
			   :strikethrough_color => NSStrikethroughColorAttributeName,
			   :shadow => NSShadowAttributeName,
			   :obliqueness => NSObliquenessAttributeName,
			   :expansion_factor => NSExpansionAttributeName,
			   :cursor => NSCursorAttributeName,
			   :tool_tip => NSToolTipAttributeName,
			   :character_shape => NSCharacterShapeAttributeName,
			   :glyph_info => NSGlyphInfoAttributeName,
			   :marked_clause_segment => NSMarkedClauseSegmentAttributeName,
			   :spelling_state => NSSpellingStateAttributeName }


	def initialize(proxy)
	    @proxy = proxy
	end

	def [](k)
	    k = attribute_for_key(k)
	    @proxy.string.attribute(k, atIndex:@proxy.range.first, effectiveRange:nil)
	end

	def []=(k,v)
	    k = attribute_for_key(k)
	    @proxy.string.removeAttribute(k, range:@proxy.range.to_NSRange(@proxy.string.length - 1))
	    @proxy.string.addAttribute(k, value:v, range:@proxy.range.to_NSRange(@proxy.string.length - 1))
	end

	def <<(attributes)
	    attributes.each_pair do |k, v|
		self[k] = v
	    end
	    self
	end
	alias :merge :<<

	def to_hash
	    @proxy.string.attributesAtIndex(@proxy.range.first, effectiveRange:nil).inject({}) do |h, pair|
		h[key_for_attribute(pair.first)] = pair.last
		h
	    end
	end

	def inspect
	    to_hash.inspect
	end

	private
	def key_for_attribute(attribute)
	    (ATTRIBUTE_KEYS.select { |k,v| v == attribute }.first || [attribute]).first
	end

	def attribute_for_key(key)
	    ATTRIBUTE_KEYS[key] || key
	end
    end

    class NSRangedProxyAttributedString
	attr_reader :string, :range
	def initialize(string, range)
	    @string = string
	    @range = range
	end

	def attributes
	    NSRangedProxyAttributeHash.new(self)
	end
    end
end

class String
    def with_attributes(attributes = {})
	attributed_string = NSMutableAttributedString.alloc.initWithString(self)
	attributed_string.attributes << attributes
	attributed_string
    end
end

class Range
    def to_NSRange(max = nil)
	location = first
	if last == -1 and max
	    length = max - first + 1
	else
	    length = last - first + 1
	end
	NSRange.new(location, length)
    end
end

class NSMutableAttributedString
    def with_attributes(attributes = {})
	string.with_attributes(attributes)
    end

    def <<(s)
	case s
	when String
	    mutableString.appendString s
	else
	    appendAttributedString s
	end
    end

    def +(s)
	attributed_string = mutableCopy
	attributed_string << s
	attributed_string
    end

    def attributes
	HotCocoa::NSRangedProxyAttributedString.new(self, 0..-1).attributes
    end

    def [](r)
	HotCocoa::NSRangedProxyAttributedString.new(self, r)
    end

    def []=(r, s)
	case s
	when String
	    replaceCharactersInRange(r.to_NSRange(length - 1), :withString => s)
	else
	    replaceCharactersInRange(r.to_NSRange(length - 1), :withAttributedString => s)
	end
    end
end
