module Test
  module Unit
    class Color
      NAMES = ["black", "red", "green", "yellow",
               "blue", "magenta", "cyan", "white"]

      attr_reader :name
      def initialize(name, options={})
        @name = name
        @foreground = options[:foreground]
        @foreground = true if @foreground.nil?
        @intensity = options[:intensity]
        @bold = options[:bold]
        @italic = options[:italic]
        @underline = options[:underline]
      end

      def foreground?
        @foreground
      end

      def intensity?
        @intensity
      end

      def bold?
        @bold
      end

      def italic?
        @italic
      end

      def underline?
        @underline
      end

      def ==(other)
        self.class === other and
          [name, foreground?, intensity?,
           bold?, italic?, underline?] ==
          [other.name, other.foreground?, other.intensity?,
           other.bold?, other.italic?, other.underline?]
      end

      def sequence
        sequence = []
        if @name == "none"
        elsif @name == "reset"
          sequence << "0"
        else
          foreground_parameter = foreground? ? 3 : 4
          foreground_parameter += 6 if intensity?
          sequence << "#{foreground_parameter}#{NAMES.index(@name)}"
        end
        sequence << "1" if bold?
        sequence << "3" if italic?
        sequence << "4" if underline?
        sequence
      end

      def escape_sequence
        "\e[#{sequence.join(';')}m"
      end

      def +(other)
        MixColor.new([self, other])
      end
    end

    class MixColor
      attr_reader :colors
      def initialize(colors)
        @colors = colors
      end

      def sequence
        @colors.inject([]) do |result, color|
          result + color.sequence
        end
      end

      def escape_sequence
        "\e[#{sequence.join(';')}m"
      end

      def +(other)
        self.class.new([self, other])
      end

      def ==(other)
        self.class === other and colors == other.colors
      end
    end
  end
end
