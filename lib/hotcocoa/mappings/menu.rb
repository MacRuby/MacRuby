HotCocoa::Mappings.map :menu => :NSMenu do
  
  defaults :title => ""
  
  def alloc_with_options(options)
    NSMenu.alloc.initWithTitle(options.delete(:title))
  end

  custom_methods do
    
    def submenu(symbol, options={}, &block)
      item = addItemWithTitle((options[:title] || titleize(symbol)), :action => nil, :keyEquivalent => "")
      submenu = builder.menu :title => (options[:title] || titleize(symbol))
      case symbol
      when :apple
        app.setAppleMenu(submenu)
      when :services
        app.setServicesMenu(submenu)
      when :window
        app.setWindowsMenu(submenu)
      end
      item_map[symbol] = submenu
      block.call(submenu) if block
      setSubmenu submenu, :forItem => item
      submenu
    end
    
    def item(symbol, options={})
      options[:title] ||= titleize(symbol)
      options[:action] ||= "on_#{symbol}:"
      item = builder.menu_item(options)
      item_map[symbol] = item
      addItem item
      item
    end
    
    def separator
      addItem NSMenuItem.separatorItem
    end
    
    def [](*symbols)
      symbol = symbols.shift
      symbols.empty? ? item_map[symbol] : item_map[symbol][*symbols]
    end
    
    private
    
      def builder
        @builder || create_builder
      end
      
      def create_builder
        @builder = Object.new
        @builder.extend HotCocoa
        @builder
      end

      def item_map
        @item_map ||= {}
      end
      
      def titleize(symbol)
        symbol.to_s.split("_").collect(&:capitalize).join(" ")
      end
      
      def app
        @app ||= NSApplication.sharedApplication
      end
  end

end