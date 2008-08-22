require 'hotcocoa'
include HotCocoa

application :name => 'Speak or Quit' do
  m = menu do |main|
    main.item :speak, 
              :on_action => proc { speech_synthesizer.speak('I have a lot to say.') }
    main.item :quit, :key => 'q', :modifiers => [:command],
              :on_action => proc { speech_synthesizer.speak('I have nothing more to say.') }
  end
  status_item :title => 'Speak or Quit', :menu => m
end
