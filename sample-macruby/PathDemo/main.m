//
//  main.m
//  PathDemo
//
//  Created by Laurent Sansonetti on 3/12/08.
//  Copyright __MyCompanyName__ 2008. All rights reserved.
//

#import <MacRuby/MacRuby.h>

int main(int argc, char *argv[])
{
    setenv("GC_DEBUG", "1", 1);
    return macruby_main("rb_main.rb", argc, argv);
}
