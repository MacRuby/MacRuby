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
        file.item :open, :key => "o"
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
