module HotCocoa

class LayoutOptions
  attr_accessor :view
  attr_reader :left_padding, :right_padding, :top_padding, :bottom_padding, :padding, :other
  
  def initialize(options={})
    @start = options[:start].nil? ? true : options[:start]
    @expand = options[:expand].nil? ? false : options[:expand]
    @padding = options[:padding]
    @left_padding   = options[:left_padding]    || @padding || 0.0
    @right_padding  = options[:right_padding]   || @padding || 0.0
    @top_padding    = options[:top_padding]     || @padding || 0.0
    @bottom_padding = options[:bottom_padding]  || @padding || 0.0
    @other = options[:other] || :align_head
    @view = options[:view]
    update_view!
  end
  
  def start?
    @start
  end
  
  def expand?
    @expand
  end
  
  def other=(value)
    @other = value
    update_view!
  end
  
  def expand=(value)
    @expand = value
    update_view!
  end
  
  def start=(value)
    @start = value
    update_view!
  end
  
  def left_padding=(value)
    @left_padding = value
    @padding = nil
    update_view!
  end
  
  def right_padding=(value)
    @right_padding = value
    @padding = nil
    update_view!
  end
  
  def top_padding=(value)
    @top_padding = value
    @padding = nil
    update_view!
  end

  def bottom_padding=(value)
    @bottom_padding = value
    @padding = nil
    update_view!
  end

  def padding=(value)
    @right_padding = @left_padding = @top_padding = @bottom_padding = value
    @padding = value
    update_view!
  end
  
  private

    def update_view!
      @view.views_updated! if @view && @view.respond_to?(:views_updated!)
    end
    
end

class LayoutView < NSView

  def initWithFrame(frame)
    super
    @mode = :vertical
    @spacing = 10.0
    @margin = 10.0
    @layout_views = []
    self
  end

  def vertical?
    @mode == :vertical
  end

  def horizontal?
    @mode == :horizonal
  end

  def mode=(mode)
    if mode != :horizontal and mode != :vertical
      raise ArgumentError, "invalid mode value #{mode}"
    end
    if mode != @mode
      @mode = mode
      relayout!
    end
  end

  def spacing
    @spacing
  end

  def spacing=(spacing)
    if spacing != @spacing
      @spacing = spacing.to_i
      relayout!
    end
  end
 
  def margin
    @margin
  end

  def margin=(margin)
    if margin != @margin
      @margin = margin.to_i
      relayout!
    end
  end
  
  def <<(view)
    if subviews.include?(view)
      raise ArgumentError, "view #{view} already in this layout view"
    end
    addSubview(view)
    if view.respond_to?(:layout) && !view.layout.nil?
      @layout_views << view
    end
    relayout!
  end
  
  # options can be
  #
  #  :start -> bool
  #    Whether the view is packed at the start or the end of the packing view.
  #    Default value is true.
  #
  #  :expand -> bool
  #    Whether the view's first dimension (width for horizontal and height for vertical)
  #    should be expanded to the maximum possible size, and should be variable according
  #    to the packing view frame.
  #    Default value is false.
  #
  #  :padding         -> float
  #  :left_padding    -> float
  #  :right_padding   -> float
  #  :top_padding     -> float
  #  :bottom_padding  -> float
  #    Controls the padding area around the view. :padding controls all areas, while
  #    :left_padding for example only controls the left side. If :padding is set, other
  #    padding flags are ignored.
  #    Default value is 0.0 for all flags.
  #
  #  :other -> mode
  #    Controls the view's second dimension (height for horizontal and width for vertical).
  #    Modes can be:
  #      :align_head
  #        Will be aligned to the head (start) of the packing area.
  #      :align_center
  #        Will be centered inside the packing area.
  #      :align_tail
  #        Will be aligned to the tail (end) of the packing area.
  #      :fill
  #        Will be filled to the maximum size.

  def views_updated!
    relayout!
  end

  def remove_view(view)
    unless subviews.include?(view)
      raise ArgumentError, "view #{view} not packed"
    end
    view.removeFromSuperview
    @layout_views.delete(view)
  end

  def remove_all_views
    @layout_views.each {|view| view.layout.view = nil}
    subviews.each { |view| view.removeFromSuperview }
    @layout_views.clear
  end

  if $DEBUG
    def drawRect(frame)
      NSColor.redColor.set
      NSFrameRect(frame)
    end
  end 

  def setFrame(frame)
    super
    relayout!
  end

  def setFrameSize(size)
    super
    relayout!
  end

  private

  def relayout!
    vertical = @mode == :vertical
    view_size = frameSize    
    dimension = @margin
    end_dimension = vertical ? view_size.height : view_size.width
    end_dimension -= (@margin * 2)

    expandable_size = end_dimension
    expandable_views = 0
    @layout_views.each do |view|
      if view.layout.expand?
        expandable_views += 1
      else
        expandable_size -= vertical ? view.frameSize.height : view.frameSize.width
        expandable_size -= @spacing
      end
    end
    expandable_size /= expandable_views

    @layout_views.each do |view|
      options = view.layout
      subview_size = view.frameSize
      view_frame = NSMakeRect(0, 0, *subview_size)
      subview_dimension = vertical ? subview_size.height : subview_size.width

      if vertical
        view_frame.origin.x = @margin
        if options.start?
          view_frame.origin.y = dimension
        else
          view_frame.origin.y = end_dimension - subview_dimension
        end        
      else
        if options.start?
          view_frame.origin.x = dimension
        else
          view_frame.origin.x = end_dimension - subview_dimension
        end        
        view_frame.origin.y = @margin
      end

      if options.expand?
        if vertical
          view_frame.size.height = expandable_size
        else
          view_frame.size.width = expandable_size
        end
        subview_dimension = expandable_size
      end

      case options.other
      when :fill
        if vertical
          view_frame.size.width = view_size.width - (2 * @margin)
        else
          view_frame.size.height = view_size.height - (2 * @margin)
        end           

      when :align_head
        # Nothing to do

      when :align_center
        if vertical
          view_frame.origin.x = (view_size.width / 2.0) - (subview_size.width / 2.0)
        else
          view_frame.origin.y = (view_size.height / 2.0) - (subview_size.height / 2.0)
        end

      when :align_tail
        if vertical
          view_frame.origin.x = view_size.width - subview_size.width - @margin
        else
          view_frame.origin.y = view_size.height - subview_size.height - @margin
        end
      end

      puts "view #{view} options #{options} final frame #{view_frame}" if $DEBUG

      view_frame.origin.x += options.left_padding
      view_frame.origin.y += options.bottom_padding

      if options.start?
        dimension += subview_dimension + @spacing
        if vertical
          dimension += options.bottom_padding + options.top_padding
        else
          dimension += options.left_padding + options.right_padding
        end
      else
        end_dimension -= subview_dimension + @spacing
      end

      view.frame = view_frame      
    end    
  end
end
end
