//
//  MyController.m
//  EmbeddedMacRuby
//
//  Created by Laurent Sansonetti on 10/7/08.
//  Copyright 2008 __MyCompanyName__. All rights reserved.
//

#import "MyController.h"
#import <MacRuby/MacRuby.h>

@implementation MyController

- (void)awakeFromNib
{
    NSFont *niceFont;
    
    niceFont = [NSFont fontWithName:@"Monaco" size:14.0];
    [expressionTextView setFont:niceFont];
    [resultTextView setFont:niceFont];
    
    [expressionTextView setString:@"(0..42).to_a"];
}

- (IBAction)evaluate:(id)sender
{
    id object;
    
    object = [[MacRuby sharedRuntime] evaluateString:[expressionTextView string]];
    [resultTextView setString:[object description]];
}


@end
