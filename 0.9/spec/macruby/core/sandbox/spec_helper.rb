require File.expand_path("../shared/no_network", __FILE__)
require File.expand_path("../shared/no_write", __FILE__)

def add_line(line)
  ScratchPad.record("") unless ScratchPad.recorded
  ScratchPad << line << "; "
end

def result
  ruby_exe(ScratchPad.recorded)
end

def with_temporary_file(temp = tmp('sandbox', false))
  FileUtils.touch temp
  yield temp
  FileUtils.rm temp
end
  