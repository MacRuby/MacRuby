# This is a small helper class to help manage observers.	The original Sketch
# used the context parameter to identify what was being observed and had large
# switch statements to choose the correct code to inform.	This looks ugly and
# also requires a unique context void* pointer to be generated, taking into
# account potential sub classing.	This was done by using a constant assigned
# to a static string describing the context.	Generating this pointer to be
# compatible with MacRuby (Pointer.new(:char))has been problematic as it
# doesn't bet passed back intact for some reason - not debugged as using a
# helper object is much tidier.

# Usage is:
# @varObserver = SKTObserver.new(self, :someMethod)
# var.addObserver(@varObserver, forKeyPath: kp, options: opt, context: nil)
# var.removeObserver(@varObserver, forKeyPath: kp)

class SKTObserver
	def initialize (target, method)
		@target = target
		@method = method
	end
	
	def observeValueForKeyPath (keyPath, ofObject: observedObject, change: change, context: context)
# puts "In observer, invoking #{@method} on #{@target} for keyPath #{keyPath} with object #{observedObject} for changes #{change}"
		@target.send(@method, keyPath, observedObject, change)
	end
end

