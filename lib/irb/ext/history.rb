# MacRuby implementation of IRB.
#
# This file is covered by the Ruby license. See COPYING for more details.
# 
# Copyright (C) 2009-2010, Eloy Duran <eloy.de.enige@gmail.com>
#
# Portions Copyright (C) 2006-2010 Ben Bleything <ben@bleything.net> (Kernel#history & Kernel#history!)

module IRB
  class History
    class << self
      attr_accessor :file, :max_entries_in_overview
      
      def current
        IRB::Context.current.processors.find do |processor|
          processor.is_a?(IRB::History)
        end
      end
    end
    
    def initialize(context)
      @context = context
      
      to_a.each do |source|
        Readline::HISTORY.push(source)
      end if Readline::HISTORY.to_a.empty?
    end
    
    def input(source)
      File.open(self.class.file, "a") { |f| f.puts(source) }
      source
    end
    
    def to_a
      file = self.class.file
      File.exist?(file) ? File.read(file).split("\n") : []
    end
    
    def clear!
      File.open(self.class.file, "w") { |f| f << "" }
      Readline::HISTORY.clear
    end
    
    def history(number_of_entries = max_entries_in_overview)
      history_size = Readline::HISTORY.size
      start_index = 0
      
      # always remove one extra, because that's the `history' command itself
      if history_size <= number_of_entries
        end_index = history_size - 2
      else
        end_index = history_size - 2
        start_index = history_size - number_of_entries - 1
      end
      
      start_index.upto(end_index) do |i|
        puts "#{i}: #{Readline::HISTORY[i]}"
      end
    end
    
    def history!(entry_or_range)
      # we don't want to execute history! again
      @context.clear_buffer
      
      if entry_or_range.is_a?(Range)
        entry_or_range.to_a.each do |i|
          @context.input_line(Readline::HISTORY[i])
        end
      else
        @context.input_line(Readline::HISTORY[entry_or_range])
      end
    end
  end
end

module Kernel
  def history(number_of_entries = IRB::History.max_entries_in_overview)
    IRB::History.current.history(number_of_entries)
    nil
  end
  alias_method :h, :history
  
  def history!(entry_or_range)
    IRB::History.current.history!(entry_or_range)
    nil
  end
  alias_method :h!, :history!
  
  def clear_history!
    IRB::History.current.clear!
    nil
  end
end

IRB::Context.processors << IRB::History
IRB::History.file = File.expand_path("~/.irb_history")
IRB::History.max_entries_in_overview = 50