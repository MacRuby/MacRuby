module Test
  module Unit
    module Attribute
      class << self
        def included(base)
          base.extend(BaseClassMethods)
          base.extend(ClassMethods)
        end
      end

      module BaseClassMethods
        def attributes_table
          {}
        end
      end

      module ClassMethods
        def method_added(name)
          super
          return unless defined?(@current_attributes)

          attributes = {}
          kept_attributes = {}
          @current_attributes.each do |attribute_name, attribute|
            attributes[attribute_name] = attribute[:value]
            kept_attributes[attribute_name] = attribute if attribute[:keep]
          end
          set_attributes(name, attributes)
          @current_attributes = kept_attributes
        end

        def attribute(name, value, options={}, *method_names)
          unless options.is_a?(Hash)
            method_names << options
            options = {}
          end
          @current_attributes ||= {}
          if method_names.empty?
            @current_attributes[name] = options.merge(:value => value)
          else
            method_names.each do |method_name|
              set_attributes(method_name, {name => value})
            end
          end
        end

        def attributes_table
          @attributes_table ||= {}
          super.merge(@attributes_table)
        end

        def set_attributes(method_name, new_attributes)
          return if new_attributes.empty?
          method_name = normalize_method_name(method_name)
          @attributes_table ||= {}
          @attributes_table[method_name] ||= {}
          current_attributes = @attributes_table[method_name]
          new_attributes.each do |key, value|
            key = normalize_attribute_name(key)
            observers = attribute_observers(key) || []
            observers.each do |observer|
              observer.call(self,
                            key,
                            (attributes(method_name) || {})[key],
                            value,
                            method_name)
            end
            current_attributes[key] = value
          end
        end

        def attributes(method_name)
          method_name = normalize_method_name(method_name)
          attributes = attributes_table[method_name]
          ancestors[1..-1].each do |ancestor|
            if ancestor.is_a?(Class) and ancestor < Test::Unit::Attribute
              parent_attributes = ancestor.attributes(method_name)
              if attributes
                attributes = (parent_attributes || {}).merge(attributes)
              else
                attributes = parent_attributes
              end
              break
            end
          end
          attributes
        end

        def get_attribute(method_name, attribute_name)
          attribute_name = normalize_attribute_name(attribute_name)
          (attributes(method_name) || {})[attribute_name]
        end

        @@attribute_observers = {}
        def register_attribute_observer(attribute_name, observer=Proc.new)
          attribute_name = normalize_attribute_name(attribute_name)
          @@attribute_observers[attribute_name] ||= []
          @@attribute_observers[attribute_name] << observer
        end

        def attribute_observers(attribute_name)
          attribute_name = normalize_attribute_name(attribute_name)
          @@attribute_observers[attribute_name]
        end

        private
        def normalize_attribute_name(name)
          name.to_s
        end

        def normalize_method_name(name)
          name.to_s
        end
      end

      def attributes
        self.class.attributes(@method_name) || {}
      end

      def [](name)
        self.class.get_attribute(@method_name, name)
      end
    end
  end
end
