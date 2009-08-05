#import <Foundation/Foundation.h>

id ConstantObject;
Class ConstantClass;
SEL ConstantSEL;
char ConstantChar;
unsigned char ConstantUnsignedChar;
short ConstantShort;
unsigned short ConstantUnsignedShort;
int ConstantInt;
unsigned int ConstantUnsignedInt;
long ConstantLong;
unsigned long ConstantUnsignedLong;
long long ConstantLongLong;
unsigned long long ConstantUnsignedLongLong;
float ConstantFloat;
double ConstantDouble;
BOOL ConstantYES;
BOOL ConstantNO;
NSPoint ConstantNSPoint;
NSSize ConstantNSSize;
NSRect ConstantNSRect;

void
Init_constant(void)
{
    ConstantObject = @"foo";
    ConstantClass = [NSObject class];
    ConstantSEL = @selector(foo:with:with:);
    ConstantChar = 42; 
    ConstantUnsignedChar = 42; 
    ConstantShort = 42; 
    ConstantUnsignedShort = 42; 
    ConstantInt = 42; 
    ConstantUnsignedInt = 42; 
    ConstantLong = 42; 
    ConstantUnsignedLong = 42; 
    ConstantLongLong = 42; 
    ConstantUnsignedLongLong = 42; 
    ConstantFloat = 3.1415; 
    ConstantDouble = 3.1415;
    ConstantYES = YES;
    ConstantNO = NO;
    ConstantNSPoint = NSMakePoint(1, 2);
    ConstantNSSize = NSMakeSize(3, 4);
    ConstantNSRect = NSMakeRect(1, 2, 3, 4);
}
