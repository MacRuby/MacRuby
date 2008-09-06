HotCocoa::Mappings.map :speech_synthesizer => :NSSpeechSynthesizer do

  def init_with_options(obj, options)
    if voice = options.delete(:voice)
      obj.initWithVoice voice
    else
      obj.init
    end
  end

  custom_methods do
    
    def speak(what, to=nil)
      if to
        url = to.is_a?(String) ?  NSURL.fileURLWithPath(to) : to
        startSpeakingString(what, toURL:url)
      else
        startSpeakingString(what)
      end
    end

  end

end

