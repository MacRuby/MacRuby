module HotCocoa
  module Behaviors
    def Behaviors.included(klass)
      Mappings::Mapper.map_class(klass)
    end 
  end
end