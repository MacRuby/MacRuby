class PageInfo 
  def encodeWithCoder(c)
    c.encodeObject @imageRep, forKey:'imageRep'
    c.encodeInt @pageOfRep, forKey:'pageOfRep' 
  end

  def initWithCoder(c)
    super
    @imageRep = c.decodeObjectForKey 'imageRep'
    @pageOfRep = c.decodeObjectForKey 'pageOfRep'
    self
  end

  def preparedImageRep
    if @pageOfRep >= 0
      @imageRep.currentPage = @pageOfRep
    end
    @imageRep
  end

  attr_reader :imageRep
  attr_reader :pageOfRep

  def setImageRep(r)
    @imageRep = r
  end

  def setPageOfRep(i)
    @pageOfRep = i
  end
end