class Object

    def self.kvo_array(key, &b)
        capitalized_key = key.to_s.capitalize
        signatures = {:size      => { selector: :"countOf#{capitalized_key}",                  type_signature: "i@:",   flip: false },
                      :[]        => { selector: :"objectIn#{capitalized_key}AtIndex:",         type_signature: "@@:i",  flip: false },
                      :insert    => { selector: :"insertObject:in#{capitalized_key}AtIndex:",  type_signature: "v@:@i", flip: true  },
                      :delete_at => { selector: :"removeObjectFrom#{capitalized_key}AtIndex:", type_signature: "v@:i",  flip: false }
        }

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
                raise ArgumentError, "Method `#{m}' isn't a KVO array accessor"
            end

        end

        include c
    end

end
