HotCocoa::Mappings.map :control => :NSControl do

  constant :alignment, {
    :left      => NSLeftTextAlignment,
    :right     => NSRightTextAlignment,
    :center    => NSCenterTextAlignment,
    :justified => NSJustifiedTextAlignment,
    :natural   => NSNaturalTextAlignment
  }

  custom_methods do
   
    include HotCocoa::Mappings::TargetActionConvenience 
    
    def text=(text)
      setStringValue(text)
    end

    def to_i
      intValue
    end

    def to_f
      doubleValue
    end
    
    def to_s
      stringValue
    end

  end

end
