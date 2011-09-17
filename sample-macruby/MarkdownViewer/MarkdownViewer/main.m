//
//  main.m
//  MarkdownViewer
//
//  Created by Watson on 11/09/16.
//

#import <Cocoa/Cocoa.h>
#import <MacRuby/MacRuby.h>

int main(int argc, char *argv[])
{
  return macruby_main("rb_main.rb", argc, argv);
}
