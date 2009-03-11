HotCocoa::Mappings.map :xml_parser => :NSXMLParser do

  def alloc_with_options(options)
    if options[:url]
      url = options.delete(:url)
      url = NSURL.alloc.initWithString(url) if url.is_a?(String)
      NSXMLParser.alloc.initWithContentsOfURL(url)
    elsif options[:file]
      NSXMLParser.alloc.initWithData(NSData.alloc.initWithContentsOfFile(options.delete(:file)))
    elsif options[:data]
      NSXMLParser.alloc.initWithData(options.delete(:data))
    else
      raise "Must provide either :url or :data when constructing an NSXMLParser"
    end
  end
  
  delegating "parserDidStartDocument:",                                                       :to => :on_start_document
  delegating "parserDidEndDocument:",                                                         :to => :on_end_document
  delegating "parser:didStartElement:namespaceURI:qualifiedName:attributes:",                 :to => :on_start_element,               :parameters => [:didStartElement, :namespaceURI, :qualifiedName, :attributes]
  delegating "parser:didEndElement:namespaceURI:qualifiedName:",                              :to => :on_end_element,                 :parameters => [:didEndElement, :namespaceURI, :qualifiedName]
  delegating "parser:didStartMappingPrefix:toURI:",                                           :to => :on_start_mapping_prefix,        :parameters => [:didStartMappingPrefix, :toURI]
  delegating "parser:didEndMappingPrefix:",                                                   :to => :on_end_mapping_prefix,          :parameters => [:didEndMappingPrefix]
  
  delegating "parser:foundAttributeDeclarationWithName:forElement:type:defaultValue:",        :to => :on_attribute_declaration,       :parameters => [:foundAttributeDeclarationWithName, :forElement, :type, :defaultValue]
  delegating "parser:foundCDATA:",                                                            :to => :on_cdata,                       :parameters => [:foundCDATA]
  delegating "parser:foundCharacters:",                                                       :to => :on_characters,                  :parameters => [:foundCharacters]
  delegating "parser:foundComment:",                                                          :to => :on_comment,                     :parameters => [:foundComment]
  delegating "parser:foundElementDeclarationWithName:model:",                                 :to => :on_element_declaration,         :parameters => [:foundElementDeclarationWithName, :model]
  delegating "parser:foundExternalEntityDeclarationWithName:publicID:systemID:",              :to => :on_external_entity_declaration, :parameters => [:foundExternalEntityDeclarationWithName, :publicID, :systemID]
  delegating "parser:foundIgnorableWhitespace:",                                              :to => :on_ignorable_whitespace,        :parameters => [:foundIgnorableWhitespace]
  delegating "parser:foundInternalEntityDeclarationWithName:value:",                          :to => :on_internal_entity_declaration, :parameters => [:foundInternalEntityDeclarationWithName, :value]
  delegating "parser:foundNotationDeclarationWithName:publicID:systemID:",                    :to => :on_notation_declaration,        :parameters => [:foundNotationDeclarationWithName, :data]
  delegating "parser:foundProcessingInstructionWithTarget:data:",                             :to => :on_processing_instruction,      :parameters => [:foundProcessingInstructionWithTarget, :data]
  delegating "parser:foundUnparsedEntityDeclarationWithName:publicID:systemID:notationName:", :to => :on_unparsed_entity_declaration, :parameters => [:foundUnparsedEntityDeclarationWithName, :publicID, :systemID, :notationName]

  delegating "parser:resolveExternalEntityName:systemID:",                                    :to => :resolve_external_entity_name,   :parameters => [:resolveExternalEntityName, :systemID]

  delegating "parser:parseErrorOccurred:",                                                    :to => :on_parse_error,                 :parameters => [:parseErrorOccurred]
  delegating "parser:validationErrorOccurred:",                                               :to => :on_validation_error,            :parameters => [:validationErrorOccurred]

end