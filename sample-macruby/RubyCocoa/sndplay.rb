require 'osx/cocoa'

snd_files =
  if ARGV.size == 0 then
    `ls /System/Library/Sounds/*.aiff`.split
  else
    ARGV
  end

snd_files.each do |path|
  snd = OSX::NSSound.alloc.initWithContentsOfFile_byReference(path, true)
  snd.play
  sleep 0.25 while snd.playing?
end