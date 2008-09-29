module HotCocoa
  def read_plist(data, mutability=:all)
    mutability = case mutability
      when :none
        NSPropertyListImmutable
      when :containers_only
        NSPropertyListMutableContainers
      when :all
        NSPropertyListMutableContainersAndLeaves
      else
        raise ArgumentError, "invalid mutability `#{mutability}'"
    end
    if data.is_a?(String)
      data = data.dataUsingEncoding(NSUTF8StringEncoding)
      if data.nil?
        raise ArgumentError, "cannot convert string `#{data}' to data"
      end
    end
    # TODO retrieve error description and raise it if there is an error.
    NSPropertyListSerialization.propertyListFromData(data,
      mutabilityOption:mutability,
      format:nil,
      errorDescription:nil)
  end
end

class Object
  def to_plist(format=:xml)
    format = case format
      when :xml
        NSPropertyListXMLFormat_v1_0 
      when :binary
        NSPropertyListBinaryFormat_v1_0
      when :open_step
        NSPropertyListOpenStepFormat 
      else
        raise ArgumentError, "invalid format `#{format}'"
    end
    # TODO retrieve error description and raise it if there is an error.
    data = NSPropertyListSerialization.dataFromPropertyList(self,
      format:format,
      errorDescription:nil)
    NSMutableString.alloc.initWithData(data, encoding:NSUTF8StringEncoding) 
  end
end
