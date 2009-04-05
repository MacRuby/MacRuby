require 'mspec/helpers/variables'
require 'mspec/matchers/include'

class IncludeVariablesMatcher < IncludeMatcher
  def initialize(*expected)
    @expected = variables(*expected)
  end
end

class Object
  def include_variables(*variables)
    IncludeVariablesMatcher.new(*variables)
  end
  alias_method :include_variable, :include_variables
end