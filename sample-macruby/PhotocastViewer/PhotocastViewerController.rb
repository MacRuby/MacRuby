class PhotocastViewerController < NSWindowController
  
  ib_outlet :imageBrowserView
  
  def awakeFromNib
    @cache = []
    @imageBrowserView.animates = true
    @imageBrowserView.dataSource = self
    @imageBrowserView.delegate = self
    
    NSNotificationCenter.defaultCenter.addObserver self,
      selector:'feedRefreshed:',
      name:PSFeedRefreshingNotification,
      object:nil
  end
  
  # Actions
  
  def zoomChanged(sender)
    @imageBrowserView.zoomValue = sender.floatValue
  end
  
  def parse(sender)
    urlString = sender.stringValue
    url = NSURL.URLWithString(urlString)
    feed = PSFeed.alloc.initWithURL(url)
    feed.refresh(nil)
  end

  def feedRefreshed(notification)
    feed = notification.object
    @results = feed.entryEnumeratorSortedBy(nil).allObjects
    @cache.clear
    @imageBrowserView.reloadData
  end
  
  # Image browser datasource/delegate

  def numberOfItemsInImageBrowser(browser)
    @results ? @results.size : 0
  end
  
  def imageBrowser(browser, itemAtIndex:index)
    photo = @cache[index]
    if photo.nil? 
      entry = @results[index]
      url = entry.content.HTMLString.scan(/<img\s+src="([^"]+)"/)[0][0]
      photo = RSSPhoto.new(url)
      @cache[index] = photo
    end
    return photo
  end

  def imageBrowser(browser, cellWasDoubleClickedAtIndex:index)
    NSWorkspace.sharedWorkspace.openURL @cache[index].url
  end
end

class RSSPhoto
  attr_reader :url
  
  def initialize(url)
    @urlString = url
    @url = NSURL.alloc.initWithString url
  end
  
  # IKImageBrowserItem protocol conformance
  
  def imageUID
    @urlString
  end
    
  def imageRepresentationType
    :IKImageBrowserNSImageRepresentationType
  end
  
  def imageRepresentation    
    @image ||= NSImage.alloc.initByReferencingURL @url
  end
end
