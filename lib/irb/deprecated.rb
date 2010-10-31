module IRB
  def self.deprecated(message, caller)
    return unless $DEBUG
    caller = caller.first.split(':')[0..-2].join(':')
    warn "[!] Deprecation warning from #{caller}: #{message}"
  end
  
  def self.deprecated_feature(old_feature, new_feature, caller)
    deprecated "Usage of #{old_feature} will be deprecated, #{new_feature}", caller
  end
  
  def self.conf
    @conf ||= DeprecatedConf.new
  end
  
  class DeprecatedConf
    DEFAULT_MESSAGE = "please create a patch/ticket"
    
    def deprecated_conf(key, message, caller)
      message ||= DEFAULT_MESSAGE
      IRB.deprecated_feature("IRB.conf[:#{key}]", message, caller)
    end
    
    def [](key)
      IRB.deprecated("Usage of the IRB.conf hash for configuration is, currently, not supported", caller)
      nil
    end
    
    def []=(key, value)
      message = nil
      case key
      when :PROMPT_MODE
        message = "use `IRB.formatter.prompt = :#{value.downcase}'"
        IRB.formatter.prompt = "#{value.to_s.downcase}".to_sym
      when :AUTO_INDENT
        message = "use `IRB.formatter.auto_indent = #{value}'"
        IRB.formatter.auto_indent = value
      when :USE_READLINE
        message = "for now DietRB only has a readline module"
      when :SAVE_HISTORY
        message = "history is always saved"
      end
      deprecated_conf key, message, caller
      value
    end
  end
end
