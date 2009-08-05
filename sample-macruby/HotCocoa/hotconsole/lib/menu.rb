module HotCocoa
  def application_menu
    menu do |main|
      main.submenu :apple do |apple|
        apple.item :about, :title => "About #{NSApp.name}"
        apple.separator
        apple.item :preferences, :key => ","
        apple.separator
        apple.submenu :services
        apple.separator
        apple.item :hide, :title => "Hide #{NSApp.name}", :key => "h"
        apple.item :hide_others, :title => "Hide Others", :key => "h", :modifiers => [:command, :alt]
        apple.item :show_all, :title => "Show All"
        apple.separator
        apple.item :quit, :title => "Quit #{NSApp.name}", :key => "q"
      end
      main.submenu :file do |file|
        file.item :new, :key => "n"
        file.item :close, :key => "w", :modifiers => [:command]
      end
      main.submenu :edit do |edit|
        edit.item :undo, :key => "z", :modifiers => [:command], :action => "undo:"
        edit.item :redo, :key => "z", :modifiers => [:command, :shift], :action => "redo:"
        edit.separator
        edit.item :cut, :key => "x", :action => "cut:"
        edit.item :copy, :key => "c", :action => "copy:"
        edit.item :paste, :key => "v", :action => "paste:"
      end
      main.submenu :view do |view|
        view.item :bigger, :key => "+", :modifiers => [:command], :action => "makeTextLarger:"
        view.item :smaller, :key => "-", :modifiers => [:command], :action => "makeTextSmaller:"
        view.separator
        view.item :clear, :key => "k", :modifiers => [:command]
      end
      main.submenu :window do |win|
        win.item :minimize, :key => "m"
        win.item :zoom
        win.separator
        win.item :bring_all_to_front, :title => "Bring All to Front", :key => "o"
      end
      main.submenu :help do |help|
        help.item :help, :title => "#{NSApp.name} Help"
      end
    end
  end
end
