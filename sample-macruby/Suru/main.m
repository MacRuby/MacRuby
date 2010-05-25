//
//  main.m
//  Suru
//
//  Created by Patrick Thomson on 5/25/10.
//  Released under the Ruby License.
//

#import <MacRuby/MacRuby.h>

int main(int argc, char *argv[])
{
    return macruby_main("rb_main.rb", argc, argv);
}
