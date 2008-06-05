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
app.delegate = AppDelegate.new

win = NSWindow.alloc.initWithContentRect([200, 300, 250, 100],
    styleMask:NSTitledWindowMask|NSClosableWindowMask|NSMiniaturizableWindowMask|NSResizableWindowMask, 
    backing:NSBackingStoreBuffered, 
    defer:false)
win.title = 'Hello World'
win.level = 3

hel = NSButton.alloc.initWithFrame([10, 10, 80, 80])
win.contentView.addSubview(hel)
hel.bezelStyle = 4
hel.title = 'Hello!'
hel.target = app.delegate
hel.action = 'sayHello:'

beep = NSSound.alloc.initWithContentsOfFile('/System/Library/Sounds/Tink.Aiff', byReference:true)
hel.sound = beep

bye = NSButton.alloc.initWithFrame([100, 10, 80, 80])
win.contentView.addSubview(bye)
bye.bezelStyle = 4
bye.target = app
bye.action = 'stop:'
bye.enabled = true
bye.title = 'Goodbye!'

adios = NSSound.alloc.initWithContentsOfFile('/System/Library/Sounds/Basso.Aiff', byReference:true)
bye.sound = adios

win.display
win.orderFrontRegardless

app.run
