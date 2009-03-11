# Object.full_const_get was taken from the ‘extlib’ project:
# http://github.com/sam/extlib which is released under a MIT License and
# copyrighted by Sam Smoot (2008).

class Object

  # @param name<String> The name of the constant to get, e.g. "Merb::Router".
  #
  # @return <Object> The constant corresponding to the name.
  def full_const_get(name)
    list = name.split("::")
    list.shift if list.first.empty?
    obj = self
    list.each do |x|
      # This is required because const_get tries to look for constants in the
      # ancestor chain, but we only want constants that are HERE
      obj = obj.const_defined?(x) ? obj.const_get(x) : obj.const_missing(x)
    end
    obj
  end

end