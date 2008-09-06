module HotCocoa
  class ComboBoxDataSource
    attr_reader :data

    def initialize(data)
      @data = data
    end
    
    def comboBox(combo_box, completedString:string)
      data.length.times do |index|
        value = string_value_of_index(index)
        return value if value.start_with?(string)
      end
      nil
    end
    
    def comboBox(combo_box, indexOfItemWithStringValue:string)
      data.length.times do |index|
        return index if string_value_of_index(index) == string
      end
      NSNotFound
    end
    
    def comboBox(combo_box, objectValueForItemAtIndex:index)
      string_value_of_index(index)
    end
    
    def numberOfItemsInComboBox(combo_box)
      data.length
    end
    
    private
    
      def string_value_of_index(i)
        item = data[i]
        if item.kind_of?(Hash)
          item.values.first
        else
          item.to_s
        end
      end

  end
end
