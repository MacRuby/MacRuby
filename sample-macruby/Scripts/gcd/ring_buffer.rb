#!/usr/local/bin/macruby
# Ruby Fiber Ring Benchmark
# Adapted for GCD from: http://people.equars.com/2008/5/22/ruby-fiber-ring-benchmark

require 'benchmark'
require 'dispatch'

DEBUG = false

START  = DEBUG ? 0 : 1
N_NODES = DEBUG ? 1 : 4
M_MESSAGES = DEBUG ? 0 : 3

class Node
    attr_accessor :successor
    attr_reader :index
    def initialize(g, index, successor)
        @queue = Dispatch::Queue.for(self)
        @group = g
        @index = index
        @successor = successor
        @current = 0
    end
            
    def call(m)
        @queue.async(@group) do
            case m
            when 0
                return
            when @current
                call(m-1)
            else 
                puts "\t#{self}.call(#{m})" if DEBUG
                @current = m
                @successor.call(m)
            end
        end
    end
    
    def to_s
        "##{@index}->#{@successor.index}[#{@current}]"
    end
end

class Ring
    def initialize(n)
        @group = Dispatch::Group.new
        @nodes = []
        setup(n)
    end
    
    def setup(n)
        last = nil
        n.downto(1) do |i|
            @nodes << Node.new(@group, i, last)
            last = @nodes[-1]
        end
        @nodes[0].successor = last
    end
    
    def call(m)
        @nodes[-1].call(m)
        @group.wait
    end
    
    def to_s
        @nodes.reverse.join " | "
    end
end

def bench(n,m)
  tm  = Benchmark.measure {
     yield
  }.format("%8.6r\n").gsub!(/\(|\)/, "")

  puts "#{n}, #{m}, #{tm}"
  
end

START.upto N_NODES do |p|
    n = 10**p
    ring = Ring.new n
    puts "\nRing of size #{n}:"
    puts "\t#{ring}" if DEBUG
    START.upto(M_MESSAGES) do |q|
      r = 10**q
      [r, 2*r, 5*r].each do |m|
          puts "#{m} message(s)" if DEBUG
        bench(n,m) { ring.call m }
      end
    end
end

        

