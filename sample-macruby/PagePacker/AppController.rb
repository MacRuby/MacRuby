class AppController
  def applicationDidFinishLaunching(note)
    CatalogController.sharedCatalogController.showWindow nil
  end
end