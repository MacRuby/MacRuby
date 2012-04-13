#
#  AppDelegate.rb
#  MarkdownViewer
#
#  Created by Watson on 12/04/13.
#
require 'rubygems'
require 'redcarpet'

module Markdown
  module_function

  def convert(string)
    markdown = Redcarpet::Markdown.new(Redcarpet::Render::SmartyHTML,
      :fenced_code_blocks => true,
      :no_intra_emphasis => true,
      :autolink => true,
      :strikethrough => true,
      :lax_html_blocks => true,
      :superscript => true,
      :hard_wrap => true,
      :tables => true,
      :xhtml => true)
    markdown.render(string)
  end
end
