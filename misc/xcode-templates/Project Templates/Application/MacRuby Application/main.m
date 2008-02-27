//
//  main.m
//  ÇPROJECTNAMEÈ
//
//  Created by ÇFULLUSERNAMEÈ on ÇDATEÈ.
//  Copyright ÇORGANIZATIONNAMEÈ ÇYEARÈ. All rights reserved.
//

#import <Cocoa/Cocoa.h>
#import "ruby/ruby.h"

int main(int argc, char *argv[])
{
    return macruby_main("rb_main.rb", argc, argv);
}
