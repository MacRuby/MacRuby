class MatchFilter
  def initialize(what, *strings)
    @what = what
    
    # MR Hack: roxor does not work nicely with encodings yet,
    # until it does let's assume that no regexps are used in the tags.
    #@descriptions = to_regexp(*strings)
    @descriptions = strings
  end

  def to_regexp(*strings)
    strings.map { |str| Regexp.new Regexp.escape(str) }
  end

  def ===(string)
    @descriptions.any? { |d| d === string }
  end

  def register
    MSpec.register @what, self
  end

  def unregister
    MSpec.unregister @what, self
  end
end
