#
#  MarkdownView.rb
#  MarkdownViewer
#
#  Created by Watson on 11/09/16.
#
class MarkdownView < WebView
  attr_accessor :delegate

  def initWithFrame(rect)
    if super
      self.registerForDraggedTypes([NSFilenamesPboardType])
      return self
    end
  end

  def performDragOperation(sender)
    return delegate.performDragOperation(sender)
  end
end
