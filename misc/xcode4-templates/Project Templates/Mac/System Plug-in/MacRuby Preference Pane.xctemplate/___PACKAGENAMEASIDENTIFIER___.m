//
//  ___FILENAME___
//  ___PACKAGENAME___
//
//  Created by ___FULLUSERNAME___ on ___DATE___.
//  Copyright (c) ___YEAR___ ___ORGANIZATIONNAME___. All rights reserved.
//

#import <MacRuby/MacRuby.h>
#import <PreferencePanes/PreferencePanes.h>

@interface ___PACKAGENAMEASIDENTIFIER___ : NSPreferencePane
{}
@end

@implementation ___PACKAGENAMEASIDENTIFIER___

+ (void)initialize
{
  NSBundle *bundle = [NSBundle bundleForClass:[self class]];
  [[MacRuby sharedRuntime] evaluateFileAtPath:[bundle pathForResource:NSStringFromClass([self class]) ofType:@"rb"]];
}

@end
