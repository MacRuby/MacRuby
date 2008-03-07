class FileSystemItem
  
  attr_reader :relativePath
  
  def initWithPath path, parent:obj
    if init
      @relativePath = File.basename(path)
      @parent = obj
    end
    self
  end
  
  def self.rootItem
    @rootItem ||= self.alloc.initWithPath '/', parent:nil
  end
  
  def children
    unless @children
       if File.directory?(fullPath) and File.readable?(fullPath)
         @children = Dir.entries(fullPath).select { |path|
            path != '.' and path != '..'
         }.map { |path|
            FileSystemItem.alloc.initWithPath path, parent:self
         }
       else
         @children = -1
       end
    end
    @children
  end
  
  def fullPath
    @parent ? File.join(@parent.fullPath, @relativePath) : @relativePath
  end
  
  def childAtIndex n
    children[n]
  end
  
  def numberOfChildren
    children == -1 ? -1 : children.size
  end
  
end