HotCocoa::Mappings.map :secure_text_field => :NSSecureTextField do
  
  defaults :selectable => true, :editable => true, :echo => true, :layout => {}
  
  def init_with_options(secure_text_field, options)
    secure_text_field.initWithFrame options.delete(:frame)
  end
  
  custom_methods do

    def echo=(value)
      cell.setEchosBullets(value)
    end

  end
  
end
