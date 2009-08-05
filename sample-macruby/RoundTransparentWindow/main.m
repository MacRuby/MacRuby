//
//  main.m
//  RoundTransparentWindow
//
//  Created by Matt Aimonetti on 2/14/09.
//  Copyright m|a agile 2009. No rights reserved.
//
// This is a MacRuby port of Apple's RoundTransparentWindow sample
// http://developer.apple.com/samplecode/RoundTransparentWindow/index.html

#import <MacRuby/MacRuby.h>

int main(int argc, char *argv[])
{
    return macruby_main("rb_main.rb", argc, argv);
}
