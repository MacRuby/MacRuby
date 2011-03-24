assert "lettuce\nbacon", '{:main_ingredient => "lettuce", :total => "bacon"}.each { |_,v| puts v }'

assert "42", %{
  module H; end
  class Hash; include H; end
  d = NSDictionary.dictionaryWithDictionary('answer' => 42)
  p d['answer']
}