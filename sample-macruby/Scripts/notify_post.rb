# This example sends a BSD notification using notify_post(3) for each
# given argument.

load_bridge_support_file '/System/Library/BridgeSupport/libSystem.bridgesupport'

ARGV.each { |x| notify_post(x) }
