HotCocoa::Mappings.map :combo_box => :NSComboBox do
  
  defaults :selectable => true, :editable => true, :completes => true, :layout => {}
  
  def init_with_options(combo_box, options)
    combo_box.initWithFrame options.delete(:frame)
  end

  custom_methods do

    def data=(data_source)
      setUsesDataSource(true)
      data_source = HotCocoa::ComboBoxDataSource.new(data_source) if data_source.kind_of?(Array)
      setDataSource(data_source)
    end
    
  end
  
  delegating "comboBoxSelectionDidChange:",   :to => :selection_did_change
  delegating "comboBoxSelectionIsChanging:",  :to => :selection_is_changing
  delegating "comboBoxWillDismiss:",          :to => :will_dismiss
  delegating "comboBoxWillPopUp:",            :to => :will_pop_up
  
end
