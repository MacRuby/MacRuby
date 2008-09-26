#
# Port of Jon Lipsky's weblog post: http://blog.elevenworks.com/?p=38
#
require 'hotcocoa'

include HotCocoa

class Calculator
  
  def self.on
    self.new.show
  end
  
  attr_accessor :accumulator, :value, :operand_pressed, :button_view
  
  def initialize
    @accumulator = []
  end

  def show
    application :name => "Calculator" do |app|
      main_window << value
      main_window << button_view
      main_window.will_close { exit }
    end
  end
  
  private
  
    def main_window(&block)
      @main_window ||= window(:frame => [100, 800, 220, 280], :title => "Calculator", :view => :nolayout, :style => [:titled, :closable, :miniaturizable], &block)
    end
  
    def value
      @value ||= text_field(:frame => [10, 230, 200, 40], :text => "0", :font => font(:name => "Tahoma", :size => 22), :text_align => :right)
    end
    
    def button_view
      @button_view || create_button_view
    end
    
    def create_button_view
      @button_view = view(:frame => [10, 10, 200, 240])
      add_buttons
      @button_view
    end
  
    def add_buttons
      calc_button("CL",  0, 4)         { clear }
      calc_button("SQR", 1, 4)         { sqrt }
      calc_button("/",   2, 4)         { operand :/ }
      calc_button("*",   3, 4)         { operand :* }
      calc_button("7",   0, 3)         { press 7 }
      calc_button("8",   1, 3)         { press 8 }
      calc_button("9",   2, 3)         { press 9 }
      calc_button("-",   3, 3)         { operand :- }
      calc_button("4",   0, 2)         { press 4 }
      calc_button("5",   1, 2)         { press 5 }
      calc_button("6",   2, 2)         { press 6 }
      calc_button("+",   3, 2)         { operand :+ }
      calc_button("1",   0, 1)         { press 1 }
      calc_button("2",   1, 1)         { press 2 }
      calc_button("3",   2, 1)         { press 3 }
      calc_button("=",   3, 1, 0, 1)   { evaluate }
      calc_button("0",   0, 0, 1, 0)   { press 0 }
      calc_button(".",   2, 0)         { press '.' }
    end
  
    def calc_button(name, x, y, w=0, h=0, &block)
      button_view << button(:title => name, :bezel => :regular_square, :frame => [x*50, y*43-h*43, 47+w*50, 40+h*43]).on_action(&block)
    end
    
    def evaluate
      accumulator << float_value
      result = eval(accumulator.join(" "))
      value.text = (result.to_i == result ? result.to_i : result.to_s)
      accumulator.clear
    end
  
    def press(key)
      if operand_pressed
        value.text = key.to_s
      else
        value.text = (value.to_s == "0" ? key.to_s : (value.to_s + key.to_s))
      end
      @operand_pressed = false
    end
  
    def operand(key)
      accumulator << float_value
      accumulator << key
      @operand_pressed = true
    end
    
    def float_value
      (value.to_s[0,1] == "." ? "0#{value.to_s}" : value.to_s).to_f
    end
  
    def sqrt
      value.text = Math.sqrt(float_value).to_s
    end
  
    def clear
      value.text = "0"
    end

end

Calculator.on
