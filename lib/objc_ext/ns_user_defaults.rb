class MacRuby
  module ObjcExt
    module NSUserDefaults
      def [](key)
        valueForKey(key)
      end

      def []=(key, value)
        setValue(value, forKey: key)
      end

      def delete(key)
        removeObjectForKey(key)
      end
    end
  end
end

NSUserDefaults.send(:include, MacRuby::ObjcExt::NSUserDefaults)
