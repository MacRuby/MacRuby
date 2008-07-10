require 'hotcocoa'
include HotCocoa

class MyView < NSView

  DefaultSize = [30, 30]

  def self.create 
    alloc.initWithFrame([0, 0, *DefaultSize])
  end
  
  def initWithFrame(frame)
    super
    @layout = HotCocoa::LayoutOptions.new(self)
    self
  end
  
  def reset_size
    setFrameSize(DefaultSize)
  end
  
  attr_accessor :number, :layout
 
  def drawRect(rect)
    (color :red => 0.29, :green => 0.26, :blue => 0.55).set
    NSFrameRect(rect)
    (color :red => 0.51, :green => 0.45, :blue => 0.95).set
    NSRectFill(NSInsetRect(rect, 1, 1))

    @attributes ||= { NSFontAttributeName => NSFont.systemFontOfSize(10) }
    str = @number.to_s
    strsize = str.sizeWithAttributes @attributes
    point = [
      (rect.size.width / 2.0) - (strsize.width / 2.0),
      (rect.size.height / 2.0) - (strsize.height / 2.0)
    ]
    @number.to_s.drawAtPoint point, withAttributes:@attributes
  end
  
end

def create_slider_layout(label, &block)
  layout_view :mode => :horizontal, :frame => [0, 0, 0, 24], :layout => {:other => :fill} do |view|
    view << label(:text => label, :layout => {:other => :align_center})
    s = slider :min => 0, :max => 50, :tic_marks => 20, :on_action => block, :layout => {:expand => true, :other => :align_center}
    s.setFrameSize([0, 24]) # TODO sizeToFit doesn't set the height for us
    view << s
  end
end

application do |app|

  window :frame => [100, 100, 500, 500], :title => "Packing View Madness" do |win|
    views = []

    window :frame => [700, 100, 200, 500], :default_layout => {:start => false} do |pane|

      pane.view << create_slider_layout('Spacing') { |x| win.view.spacing = x.to_i }

      pane.view << create_slider_layout('Margin') { |x| win.view.margin = x.to_i }

      pane.view << button(:title => "Vertical", :type => :switch, :state => :on) do |b|
        b.on_action do |b| 
          views.each { |v| v.reset_size }
          win.view.mode = b.on? ? :vertical : :horizontal
        end
      end
      
      selected_view = nil
      expand_b = nil
      left_padding_s = right_padding_s = top_padding_s = bottom_padding_s = nil
      other_p = nil
      views_p = popup :items => ['No View'], :layout => {:other => :fill}
      views_p.on_action do |p| 
        selected_view = views[p.items.selected_index]
        options = selected_view.layout
        expand_b.state = options.expand? ? :on : :off
        left_padding_s.intValue = options.left_padding
        right_padding_s.intValue = options.right_padding
        top_padding_s.intValue = options.top_padding
        bottom_padding_s.intValue = options.bottom_padding
        other_p.items.selected = case options.other
          when :align_head then 0
          when :align_center then 1
          when :align_tail then 2
          when :fill then 3
        end
      end
 
      add_b = button :title => "Add view"
      add_b.on_action do
        view = MyView.create
        views << view
        view.number = views.size
        win.view.addSubview view
        views_p.items = views.map { |x| "View #{x.number}" }
        selected_view = views[0]
      end
      pane.view << add_b
      pane.view << views_p
      expand_b = button :title => "Expand", :type => :switch, :state => :off
      expand_b.on_action do |b| 
        selected_view.reset_size unless b.on?
        selected_view.layout.expand = b.on?
      end
      pane.view << expand_b

      v = create_slider_layout('Left') { |x| selected_view.layout.left_padding = x.to_i }
      left_padding_s = v.subviews[1]
      pane.view << v
 
      v = create_slider_layout('Right') { |x| selected_view.layout.right_padding = x.to_i }
      right_padding_s = v.subviews[1]
      pane.view << v
 
      v = create_slider_layout('Top') { |x| selected_view.layout.top_padding = x.to_i }
      top_padding_s = v.subviews[1]
      pane.view << v
 
      v = create_slider_layout('Bottom') { |x| selected_view.layout.bottom_padding = x.to_i }
      bottom_padding_s = v.subviews[1]
      pane.view << v
  
      pane.view << layout_view(:mode => :horizontal, :frame => [0, 0, 0, 24], :layout => {:other => :fill}) do |view|
        view << label(:text => 'Other', :layout => {:other => :align_center})
        view << popup(:items => ['Align Head', 'Align Center', 'Align Tail', 'Fill'], :layout => {:expand => true, :other => :align_center}) do |p|
          p.on_action do  |x|
            selected_view.reset_size
            selected_view.layout.other = x.items.selected.downcase.tr(' ', '_').intern
          end
          other_p = p
        end
      end
    end
  end
end
