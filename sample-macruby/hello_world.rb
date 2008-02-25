framework 'appkit'

class AppDelegate
  def applicationDidFinishLaunching(notification)
    puts "Hello, World!"
  end

  def sayHello(sender)
    puts "Hello again, World!"
  end
end

app = NSApplication.sharedApplication
delegate = AppDelegate.alloc.init
app.setDelegate(delegate)

frame = NSRect.new(NSPoint.new(200, 300), NSSize.new(250, 100))
win = NSWindow.alloc.initWithContentRect(frame, styleMask:15, backing:2, defer:0)
win.setTitle('Hello World')
win.setLevel(3)

hel = NSButton.alloc.initWithFrame(NSRect.new(NSPoint.new(10, 10), NSSize.new(80, 80)))
win.contentView.addSubview(hel)
hel.setBezelStyle(4)
hel.setTitle('Hello!')
hel.setTarget(app.delegate)
hel.setAction('sayHello:')

beep = NSSound.alloc.initWithContentsOfFile('/System/Library/Sounds/Tink.Aiff', byReference:true)
hel.setSound(beep)

bye = NSButton.alloc.initWithFrame(NSRect.new(NSPoint.new(100, 10), NSSize.new(80, 80)))
win.contentView.addSubview(bye)
bye.setBezelStyle(4)
bye.setTarget(app)
bye.setAction('stop:')
bye.setEnabled(true)
bye.setTitle('Goodbye!')

adios = NSSound.alloc.initWithContentsOfFile('/System/Library/Sounds/Basso.Aiff', byReference:true)
bye.setSound(adios)

win.display
win.orderFrontRegardless

app.run
