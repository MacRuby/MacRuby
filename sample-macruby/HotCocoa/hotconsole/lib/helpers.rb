class Object
  # calls a method on the object on the main thread
  def send_on_main_thread(function_name, parameter = nil, asynchronous = true)
    function_name = function_name.to_s
    # if the target method has a parameter, we have to be sure the method name ends with a ':'
    function_name << ':' if parameter and not /:$/.match(function_name)
    performSelectorOnMainThread function_name, withObject: parameter, waitUntilDone: (not asynchronous)
  end
end
