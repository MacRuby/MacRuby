class String
    def with_attributes(attributes = {})
	NSMutableAttributedString.alloc.initWithString(self, :attributes => attributes)
    end
end

class Range
    def to_NSRange(max = nil)
	if last == -1 and max
	    last = max
	end
	NSRange.new(first, last - first + 1)
    end
end

class NSRangedProxyAttributeHash < Hash
    def initialize(proxy)
	@proxy = proxy
    end

    def [](k)
	@proxy.string.attribute(k, atIndex:@proxy.range.first, effectiveRange:nil)
    end

    def []=(k,v)
	@proxy.string.removeAttribute(k, range:@proxy.range.to_NSRange(@proxy.string.length - 1))
	@proxy.string.addAttribute(k, value:v, range:@proxy.range.to_NSRange(@proxy.string.length - 1))
    end

    def <<(attributes)
	attributes.each_pair do |k, v|
	    self[k] = v
	end
    end
    alias :merge :<<
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

    def attributes
	NSRangedProxyAttributedString.new(self, 0..-1).attributes
    end

    def [](r)
	NSRangedProxyAttributedString.new(self, r)
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
