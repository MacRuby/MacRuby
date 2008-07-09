require 'hotcocoa/packing_view'

Mappings.map :packing_view => :PackingView do

  defaults :frame => DefaultEmptyRect

  def init_with_options(view, options)
    view.initWithFrame options.delete(:frame)
  end

end
