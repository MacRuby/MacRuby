//
//  MyController.h
//  EmbeddedMacRuby
//
//  Created by Laurent Sansonetti on 10/7/08.
//  Copyright 2008 __MyCompanyName__. All rights reserved.
//

#import <Cocoa/Cocoa.h>

@interface MyController : NSObject 
{
    IBOutlet NSTextView *expressionTextView;
    IBOutlet NSTextView *resultTextView;
}

- (IBAction)evaluate:(id)sender;

@end
