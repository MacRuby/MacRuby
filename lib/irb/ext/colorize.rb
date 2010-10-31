# MacRuby implementation of IRB.
#
# This file is covered by the Ruby license. See COPYING for more details.
# 
# Copyright (C) 2009-2010, Eloy Duran <eloy.de.enige@gmail.com>
#
# Portions Copyright (C) 2006-2010 Paul Duncan <pabs@pablotron.org> (Wirble)
# Portions Copyright (C) 2009-2010 Jens Wille <jens.wille@gmail.com> (Wirble)
# Portions Copyright (C) 2006-2010 Giles Bowkett (light background color scheme)

module IRB
  class ColoredFormatter < Formatter
    TYPE_ALIASES = {
      :on_comma           => :comma,
      :refers             => :operator, # Wirble compat
      :on_op              => :operator,
      
      :on_lbrace          => :open_hash,
      :on_rbrace          => :close_hash,
      :on_lbracket        => :open_array,
      :on_rbracket        => :close_array,
      
      :on_ident           => :symbol,
      :on_symbeg          => :symbol_prefix,
      
      :on_tstring_beg     => :open_string,
      :on_tstring_content => :string,
      :on_tstring_end     => :close_string,
      
      :on_int             => :number,
      :on_float           => :number,
      :on_kw              => :keyword,
      :on_const           => :constant,
      :class              => :constant # Wirble compat
    }
    
    # # object colors
    # :open_object        => :dark_gray,
    # :object_class       => :purple,
    # :object_addr_prefix => :blue,
    # :object_line_prefix => :blue,
    # :close_object       => :dark_gray,
    
    COLOR_SCHEMES = {
      :dark_background => {
        # :prompt             => :green,
        # :result_prefix      => :light_purple,
        
        :comma              => :blue,
        :operator           => :blue,
        
        :open_hash          => :green,
        :close_hash         => :green,
        :open_array         => :green,
        :close_array        => :green,
        
        :symbol_prefix      => :yellow, # hmm ident...
        :symbol             => :yellow,
        
        :open_string        => :red,
        :string             => :cyan,
        :close_string       => :red,
        
        :number             => :cyan,
        :keyword            => :yellow,
        :constant           => :light_green
      },
      :light_background => {
        :comma              => :purple,
        :operator           => :blue,
        
        :open_hash          => :red,
        :close_hash         => :red,
        :open_array         => :red,
        :close_array        => :red,
        
        :symbol_prefix      => :black,
        :symbol             => :light_gray,
        
        :open_string        => :blue,
        :string             => :dark_gray,
        :close_string       => :blue,
        
        :number             => :black,
        :keyword            => :brown,
        :constant           => :red
      },
      :fresh => {
        :prompt             => :green,
        :result_prefix      => :light_purple,
        
        :comma              => :red,
        :operator           => :red,
        
        :open_hash          => :blue,
        :close_hash         => :blue,
        :open_array         => :green,
        :close_array        => :green,
        
        :symbol_prefix      => :yellow,
        :symbol             => :yellow,
        
        :number             => :cyan,
        :string             => :cyan,
        :keyword            => :white
      }
    }
    
    #
    # Terminal escape codes for colors.
    #
    module Color
      COLORS = {
        :nothing      => '0;0',
        :black        => '0;30',
        :red          => '0;31',
        :green        => '0;32',
        :brown        => '0;33',
        :blue         => '0;34',
        :cyan         => '0;36',
        :purple       => '0;35',
        :light_gray   => '0;37',
        :dark_gray    => '1;30',
        :light_red    => '1;31',
        :light_green  => '1;32',
        :yellow       => '1;33',
        :light_blue   => '1;34',
        :light_cyan   => '1;36',
        :light_purple => '1;35',
        :white        => '1;37',
      }
      
      #
      # Return the escape code for a given color.
      #
      def self.escape(name)
        COLORS.key?(name) && "\e[#{COLORS[name]}m"
      end
      
      CLEAR = escape(:nothing)
    end
    
    attr_reader :colors, :color_scheme
    
    def initialize
      super
      self.color_scheme = :dark_background
    end
    
    def color_scheme=(scheme)
      @colors = COLOR_SCHEMES[scheme].dup
      @color_scheme = scheme
    end
    
    def color(type)
      type = TYPE_ALIASES[type] if TYPE_ALIASES.has_key?(type)
      @colors[type]
    end
    
    def colorize_token(type, token)
      if color = color(type)
        "#{Color.escape(color)}#{token}#{Color::CLEAR}"
      else
        token
      end
    end
    
    def colorize(str)
      Ripper.lex(str).map { |_, type, token| colorize_token(type, token) }.join
    end
    
    def prompt(context, ignore_auto_indent = false, level = nil)
      colorize_token(:prompt, super)
    end
    
    def result_prefix
      colorize_token(:result_prefix, Formatter::RESULT_PREFIX)
    end
    
    def result(object)
      "#{result_prefix} #{colorize(inspect_object(object))}"
    end
  end
end

IRB.formatter = IRB::ColoredFormatter.new
