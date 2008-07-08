module HotCocoa
    
  class DelegateBuilder
    
    attr_reader :control, :delegate, :method_count
    
    def initialize(control)
      @control = control
      @method_count = 0
      @delegate = Object.new
    end
    
    def add_delegated_method(block, selector_name, *parameters)
      clear_delegate
      increment_method_count
      bind_block_to_delegate_instance_variable(block)
      create_delegate_method(selector_name, parameters)
      set_delegate
    end
    
    private 
    
      def increment_method_count
        @method_count += 1
      end
      
      def bind_block_to_delegate_instance_variable(block)
        delegate.instance_variable_set(block_instance_variable, block)
      end
      
      def create_delegate_method(selector_name, parameters)
        eval %{
          def delegate.#{parameterize_selector_name(selector_name)}
            #{block_instance_variable}.call(#{parameter_values_for_mapping(selector_name, parameters)})
          end
        }
      end
      
      def clear_delegate
        control.setDelegate(nil)
      end
      
      def set_delegate
        control.setDelegate(delegate)
      end
      
      def block_instance_variable
        "@block#{method_count}"
      end
    
      def parameterize_selector_name(selector_name)
        return selector_name unless selector_name.include?(":")
        params = selector_name.split(":")
        result = "#{params.shift}(p1"
        params.each_with_index do |param, i|
          result << ", #{param}:p#{i+2}"
        end
        result + ")"
      end
      
      def parameter_values_for_mapping(selector_name, parameters)
        return if parameters.empty?
        result = []
        selector_params = selector_name.split(":")
        parameters.each do |parameter|
          if (dot = parameter.index("."))
            result << "p#{selector_params.index(parameter[0...dot])+1}#{parameter[dot..-1]}"
          else
            result << "p#{selector_params.index(parameter)+1}"
          end
        end
        result.join(", ")
      end
    
  end
  
end