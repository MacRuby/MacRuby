
# This is ripped from sam/extlib and changed a little bit.
class Object

  # @param name<String> The name of the constant to get, e.g. "Merb::Router".
  #
  # @return <Object> The constant corresponding to the name.
  def full_const_get(name)
    list = name.split("::")
    list.shift if list.first.strip.empty?
    obj = self
    list.each do |x|
      # This is required because const_get tries to look for constants in the
      # ancestor chain, but we only want constants that are HERE
      raise NameError, "uninitialized constant #{self.name}::#{name}" unless obj.const_defined?(x)
      obj = obj.const_get(x)
    end
    obj
  end

end