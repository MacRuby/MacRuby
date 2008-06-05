//
//  main.m
//  CircleView
//
//  Created by Laurent Sansonetti on 2/29/08.
//  Copyright __MyCompanyName__ 2008. All rights reserved.
//

#import <MacRuby/MacRuby.h>

#import <Cocoa/Cocoa.h>
@interface Foo
@end
@implementation Foo
+(id)foo:(id)rcv
{
  return [NSTimer scheduledTimerWithTimeInterval:1.0/30.0 target:rcv selector:@selector(performAnimation:) userInfo:nil repeats:true];
}
@end

int main(int argc, char *argv[])
{
    return macruby_main("rb_main.rb", argc, argv);
}
