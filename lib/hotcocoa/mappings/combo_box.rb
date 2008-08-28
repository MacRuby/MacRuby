HotCocoa::Mappings.map :combo_box => :NSComboBox do
  
  defaults :selectable => true, :editable => true, :completes => true, :layout => {}
  
  def init_with_options(combo_box, options)
    combo_box.initWithFrame options.delete(:frame)
  end

  custom_methods do

    def data=(data_source)
      setUsesDataSource(true)
      data_source = ComboBoxDataSource.new(data_source) if data_source.kind_of?(Array)
      setDataSource(data_source)
    end
    
  end
  
end
