require 'test/unit/color'

module Test
  module Unit
    class ColorScheme
      include Enumerable

      class << self
        @@default = nil
        def default
          @@default ||= new("success" => Color.new("green", :bold => true),
                            "failure" => Color.new("red", :bold => true),
                            "pending" => Color.new("magenta", :bold => true),
                            "omission" => Color.new("blue", :bold => true),
                            "notification" => Color.new("cyan", :bold => true),
                            "error" => Color.new("yellow", :bold => true) +
                                       Color.new("black", :foreground => false),
                            "case" => Color.new("white", :bold => true) +
                                       Color.new("blue", :foreground => false),
                            "suite" => Color.new("white", :bold => true) +
                                       Color.new("green", :foreground => false))
        end

        @@schemes = {}
        def all
          @@schemes.merge("default" => default)
        end

        def [](id)
          @@schemes[id.to_s]
        end

        def []=(id, scheme_or_spec)
          if scheme_or_spec.is_a?(self)
            scheme = scheme_or_spec
          else
            scheme = new(scheme_or_spec)
          end
          @@schemes[id.to_s] = scheme
        end
      end

      def initialize(scheme_spec)
        @scheme = {}
        scheme_spec.each do |key, color_spec|
          self[key] = color_spec
        end
      end

      def [](name)
        @scheme[name.to_s]
      end

      def []=(name, color_spec)
        @scheme[name.to_s] = make_color(color_spec)
      end

      def each(&block)
        @scheme.each(&block)
      end

      def to_hash
        hash = {}
        @scheme.each do |key, color|
          hash[key] = color
        end
        hash
      end

      private
      def make_color(color_spec)
        if color_spec.is_a?(Color) or color_spec.is_a?(MixColor)
          color_spec
        else
          color_name = nil
          normalized_color_spec = {}
          color_spec.each do |key, value|
            key = key.to_sym
            if key == :name
              color_name = value
            else
              normalized_color_spec[key] = value
            end
          end
          Color.new(color_name, normalized_color_spec)
        end
      end
    end
  end
end
