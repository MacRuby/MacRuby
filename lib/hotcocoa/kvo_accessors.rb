class Object

  def self.kvo_array(key, &b)
    key = key.to_s
    capitalized_key = key[0].capitalize + key[1..-1]
    signatures = { :size      => { selector: :"countOf#{capitalized_key}",                  type_signature: "i@:",   flip: false },
                   :[]        => { selector: :"objectIn#{capitalized_key}AtIndex:",         type_signature: "@@:i",  flip: false },
                   :insert    => { selector: :"insertObject:in#{capitalized_key}AtIndex:",  type_signature: "v@:@i", flip: true  },
                   :delete_at => { selector: :"removeObjectFrom#{capitalized_key}AtIndex:", type_signature: "v@:i",  flip: false }
    }
    define_methods_with_signatures(signatures, &b)
  end

  def self.kvo_set(key, &b)
    key = key.to_s
    capitalized_key = key[0].capitalize + key[1..-1]
    signatures = { :add       => { selector: :"add#{capitalized_key}Object:",    type_signature: "v@:@", flip: false },
                   :delete    => { selector: :"remove#{capitalized_key}Object:", type_signature: "v@:@", flip: false},
                   :merge     => { selector: :"add#{capitalized_key}:",          type_signature: "v@:@", flip: false },
                   :subtract  => { selector: :"remove#{capitalized_key}:",       type_signature: "v@:@", flip: false },
                   :set       => { selector: :"#{key}",                          type_signature: "@@:",  flip: false }
    }
    define_methods_with_signatures(signatures, &b)
  end

  private
  
  def self.define_methods_with_signatures(signatures, &b)
    c = Module.new
    c.module_eval &b
    c.instance_methods.each do |m|
      signature = signatures[m]
      if signature
      	method = c.instance_method(m)
      	if signature[:flip]
      	  method = Proc.new { |a, b| method.bind(self).call(b, a)}
      	end
      	c.send(:define_method, signature[:selector], method)
      	c.send(:remove_method, m)
      	c.send(:method_signature, signature[:selector], signature[:type_signature])
      elsif not Module.instance_methods.include?(m)
          raise ArgumentError, "Method `#{m}' isn't a KVO accessor"
      end
    end
    include c
  end

end
