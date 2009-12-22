include Dispatch
 
class Future
  def initialize(&block)
    @@queue_count ||= 0
    Thread.current[:futures_queue] ||= Queue.new("org.macruby.futures-#{Thread.current.object_id}")
    @group = Group.new
    Thread.current[:futures_queue].async(@group) { @value = block[] }
  end
  
  def value
    @group.wait
    @value
  end
end
 
f = Future.new do
  sleep 2.5
  'some value'
end
 
p f.value