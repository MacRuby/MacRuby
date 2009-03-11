HotCocoa::Mappings.map :sound => :NSSound do 
  
  defaults :by_reference => true
  
  def init_with_options(sound, options)
    sound.initWithContentsOfFile options.delete(:file), byReference:options.delete(:by_reference)
  end
  
end