module HotCocoa
  module Behaviors
    def Behaviors.included(klass)
      Mapper.map_class(klass)
    end 
  end
end