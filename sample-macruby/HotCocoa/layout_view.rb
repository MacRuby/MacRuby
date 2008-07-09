require 'hotcocoa'
include HotCocoa

class MyView < NSView

  DefaultSize = [30, 30]

  def self.create 
    alloc.initWithFrame([0, 0, *DefaultSize])
  end

  def reset_size
    setFrameSize(DefaultSize)
  end

  attr_accessor :number
 
  def drawRect(rect)
    (color :red => 0.29, :green => 0.26, :blue => 0.55).set
    NSFrameRect(rect)
    (color :red => 0.51, :green => 0.45, :blue => 0.95).set
    NSRectFill(NSInsetRect(rect, 1, 1))

    @attributes ||= {
      NSFontAttributeName => NSFont.systemFontOfSize(10)
    }
    str = @number.to_s
    strsize = str.sizeWithAttributes @attributes
    point = [(rect.size.width / 2.0) - (strsize.width / 2.0),
	     (rect.size.height / 2.0) - (strsize.height / 2.0)]
    @number.to_s.drawAtPoint point, withAttributes:@attributes
  end
  
end

def create_slider_packing_view(label, &block)
  pv = packing_view :mode => :horizontal, :frame => [0, 0, 0, 24]
  l = label(:text => label)
  pv.pack l, :other => :align_center
  s = slider :min => 0, :max => 50, :tic_marks => 20,
	     :on_action => block
  s.setFrameSize([0, 24]) # TODO sizeToFit doesn't set the height for us
  pv.pack s, :expand => true
  pv
end

application do |app|

  window :frame => [100, 100, 500, 500], :title => "Packing View Madness" do |win|

    pv = packing_view :frame => [0, 0, 500, 500]
    win.contentView = pv
    views = []

    window :frame => [700, 100, 200, 500] do |pane|

      pane_pv = packing_view :frame => [0, 0, 200, 500], :spacing => 10, :margin => 10
      pane.contentView = pane_pv

      v = create_slider_packing_view('Spacing') { |x| pv.spacing = x.to_i }
      pane_pv.pack v, :start => false, :other => :fill

      v = create_slider_packing_view('Margin') { |x| pv.margin = x.to_i }
      pane_pv.pack v, :start => false, :other => :fill

      vertical_b = button :title => "Vertical", :type => :switch, :state => :on
      vertical_b.on_action do |b| 
	      views.each { |v| v.reset_size }
        pv.mode = b.on? ? :vertical : :horizontal
      end
      pane_pv.pack vertical_b, :start => false
      
      selected_view = nil
      expand_b = nil
      left_padding_s = right_padding_s = top_padding_s = bottom_padding_s = nil
      other_p = nil
      views_p = popup :items => ['No View']
      views_p.on_action do |p| 
        selected_view = views[p.items.selected_index]
        options = pv.options_for_view(selected_view)
        expand_b.state = options[:expand] ? :on : :off
      	left_padding_s.intValue = options[:left_padding]
      	right_padding_s.intValue = options[:right_padding]
      	top_padding_s.intValue = options[:top_padding]
      	bottom_padding_s.intValue = options[:bottom_padding]
      	other_p.items.selected = case options[:other]
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
        pv.pack view
        views_p.items = views.map { |x| "View #{x.number}" }
        selected_view = views[0]
      end
      pane_pv.pack add_b, :start => false

      pane_pv.pack views_p, :start => false, :other => :fill

      expand_b = button :title => "Expand", :type => :switch, :state => :off
      expand_b.on_action do |b| 
        selected_view.reset_size unless b.on?
        pv.change_option_for_view(selected_view, :expand, b.on?)
      end
      pane_pv.pack expand_b, :start => false

      v = create_slider_packing_view('Left') { |x| pv.change_option_for_view(selected_view, :left_padding, x.to_i) }
      left_padding_s = v.subviews[1]
      pane_pv.pack v, :start => false, :other => :fill
 
      v = create_slider_packing_view('Right') { |x| pv.change_option_for_view(selected_view, :right_padding, x.to_i) }
      right_padding_s = v.subviews[1]
      pane_pv.pack v, :start => false, :other => :fill
 
      v = create_slider_packing_view('Top') { |x| pv.change_option_for_view(selected_view, :top_padding, x.to_i) }
      top_padding_s = v.subviews[1]
      pane_pv.pack v, :start => false, :other => :fill
 
      v = create_slider_packing_view('Bottom') { |x| pv.change_option_for_view(selected_view, :bottom_padding, x.to_i) }
      bottom_padding_s = v.subviews[1]
      pane_pv.pack v, :start => false, :other => :fill
  
      tmp_pv = packing_view :mode => :horizontal, :frame => [0, 0, 0, 24]
      l = label(:text => 'Other')
      tmp_pv.pack l, :other => :align_center
      other_p = popup :items => ['Align Head', 'Align Center', 'Align Tail', 'Fill']
      other_p.on_action do |x|
      	selected_view.reset_size 
      	pv.change_option_for_view(selected_view, :other, x.items.selected.downcase.tr(' ', '_').intern)
      end
      tmp_pv.pack other_p, :expand => true
      pane_pv.pack tmp_pv, :start => false, :other => :fill
    end
  end
end
