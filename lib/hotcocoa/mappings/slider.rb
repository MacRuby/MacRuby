HotCocoa::Mappings.map :slider => :NSSlider do

  defaults :frame => DefaultEmptyRect, :layout => {}
 
  def init_with_options(button, options)
    button.initWithFrame options.delete(:frame)
  end

  custom_methods do
   
    def min=(value)
      setMinValue(value)
    end
 
    def max=(value)
      setMaxValue(value)
    end
    
    def tic_marks=(value)
      setNumberOfTickMarks(value)
    end
    
  end
  
end
