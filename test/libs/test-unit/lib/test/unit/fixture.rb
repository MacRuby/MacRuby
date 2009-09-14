module Test
  module Unit
    module Fixture
      class << self
        def included(base)
          base.extend(ClassMethods)

          [:setup, :teardown].each do |fixture|
            observer = Proc.new do |test_case, _, _, value, method_name|
              if value.nil?
                test_case.send("unregister_#{fixture}_method", method_name)
              else
                test_case.send("register_#{fixture}_method", method_name,
                               value)
              end
            end
            base.register_attribute_observer(fixture, &observer)
          end
        end
      end

      module ClassMethods
        def setup(*method_names)
          register_fixture(:setup, *method_names)
        end

        def unregister_setup(*method_names)
          unregister_fixture(:setup, *method_names)
        end

        def teardown(*method_names)
          register_fixture(:teardown, *method_names)
        end

        def unregister_teardown(*method_names)
          unregister_fixture(:teardown, *method_names)
        end

        def register_setup_method(method_name, options)
          register_fixture_method(:setup, method_name, options, :after, :append)
        end

        def unregister_setup_method(method_name)
          unregister_fixture_method(:setup, method_name)
        end

        def register_teardown_method(method_name, options)
          register_fixture_method(:teardown, method_name, options,
                                  :before, :prepend)
        end

        def unregister_teardown_method(method_name)
          unregister_fixture_method(:teardown, method_name)
        end

        def before_setup_methods
          collect_fixture_methods(:setup, :before)
        end

        def after_setup_methods
          collect_fixture_methods(:setup, :after)
        end

        def before_teardown_methods
          collect_fixture_methods(:teardown, :before)
        end

        def after_teardown_methods
          collect_fixture_methods(:teardown, :after)
        end

        private
        def register_fixture(fixture, *method_names)
          options = {}
          options = method_names.pop if method_names.last.is_a?(Hash)
          attribute(fixture, options, *method_names)
        end

        def unregister_fixture(fixture, *method_names)
          attribute(fixture, nil, *method_names)
        end

        def valid_register_fixture_options?(options)
          return true if options.empty?
          return false if options.size > 1

          key = options.keys.first
          [:before, :after].include?(key) and
            [:prepend, :append].include?(options[key])
        end

        def add_fixture_method_name(how, variable_name, method_name)
          methods = instance_eval("#{variable_name} ||= []")

          if how == :prepend
            methods = [method_name] | methods
          else
            methods = methods | [method_name]
          end
          instance_variable_set(variable_name, methods)
        end

        def registered_methods_variable_name(fixture, order)
          "@#{order}_#{fixture}_methods"
        end

        def unregistered_methods_variable_name(fixture)
          "@unregistered_#{fixture}_methods"
        end

        def register_fixture_method(fixture, method_name, options,
                                    default_order, default_how)
          unless valid_register_fixture_options?(options)
            message = "must be {:before => :prepend}, " +
              "{:before => :append}, {:after => :prepend} or " +
              "{:after => :append}: #{options.inspect}"
            raise ArgumentError, message
          end

          if options.empty?
            order, how = default_order, default_how
          else
            order, how = options.to_a.first
          end
          variable_name = registered_methods_variable_name(fixture, order)
          add_fixture_method_name(how, variable_name, method_name)
        end

        def unregister_fixture_method(fixture, method_name)
          variable_name = unregistered_methods_variable_name(fixture)
          add_fixture_method_name(:append, variable_name, method_name)
        end

        def collect_fixture_methods(fixture, order)
          methods_variable = registered_methods_variable_name(fixture, order)
          unregistered_methods_variable =
            unregistered_methods_variable_name(fixture)

          base_index = ancestors.index(Fixture)
          interested_ancestors = ancestors[0, base_index].reverse
          interested_ancestors.inject([]) do |result, ancestor|
            if ancestor.is_a?(Class)
              ancestor.class_eval do
                methods = instance_eval("#{methods_variable} ||= []")
                unregistered_methods =
                  instance_eval("#{unregistered_methods_variable} ||= []")
                (result | methods) - unregistered_methods
              end
            else
              result
            end
          end
        end
      end

      private
      def run_fixture(fixture)
        [
         self.class.send("before_#{fixture}_methods"),
         fixture,
         self.class.send("after_#{fixture}_methods")
        ].flatten.each do |method_name|
          send(method_name) if respond_to?(method_name, true)
        end
      end

      def run_setup
        run_fixture(:setup)
      end

      def run_teardown
        run_fixture(:teardown)
      end
    end
  end
end
