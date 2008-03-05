//
//  main.m
//  CircleView
//
//  Created by Laurent Sansonetti on 2/29/08.
//  Copyright __MyCompanyName__ 2008. All rights reserved.
//

#import <Cocoa/Cocoa.h>
#import "ruby/ruby.h"

int main(int argc, char *argv[])
{
    return macruby_main("rb_main.rb", argc, argv);
}
