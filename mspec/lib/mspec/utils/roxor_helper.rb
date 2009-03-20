class File
  def self.exist?(path)
    NSFileManager.defaultManager.fileExistsAtPath(path) == 1
  end
end