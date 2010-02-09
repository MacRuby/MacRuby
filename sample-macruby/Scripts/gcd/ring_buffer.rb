#!/usr/bin/env ruby

N_NODES = ARGV[0] || 4
M_MESSAGES = ARGV[1] || 3

class Node
    attr_accessor :successor
    attr :index
    def initialize(index, successor)
        @index = index
        @successor = successor
        @current = 0
    end
            
    def call(m)
        case m
        when 0
            return
        when @current
            call(m-1)
        else 
            puts "#{self}.call #{m}"
            @current = m
            @successor.call(m)
        end
    end
    
    def to_s
        "#{@index}->[#{@successor.index}]@#{@current}"
    end
end

class Ring
    def initialize(n)
        @nodes = []
        setup(n)
    end
    
    def setup(n)
        last = nil
        n.downto(1) do |i|
            @nodes << Node.new(i, last)
            last = @nodes[-1]
        end
        @nodes[0].successor = last
    end
    
    def call(m)
        @nodes[0].call(m)
    end
    
    def to_s
        @nodes.join " | "
    end
end

1.upto N_NODES do |n|
    ring = Ring.new n
    puts "\nRing of size #{n}: #{ring}"
    1.upto(M_MESSAGES) { |m|  puts "m=#{m}"; ring.call m }
end
        

