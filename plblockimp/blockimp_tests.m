/*
 * Author: Landon Fuller <landonf@plausible.coop>
 *
 * Copyright 2010-2011 Plausible Labs Cooperative, Inc.
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge,
 * to any person obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to permit
 * persons to whom the Software is furnished to do so, subject to the following
 * conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#import "GTMSenTestCase.h"
#import "blockimp.h"

@interface BlockIMPTests : SenTestCase @end

/**
 * BlockIMP Tests
 */
@implementation BlockIMPTests

/**
 * Test basic IMP allocation and execution.
 */
- (void) testAllocateIMP {
    /* A test block */
    __block BOOL didRun = NO;
    void (^Block)(id self) = ^(id blockself) {
        didRun = YES;

        STAssertEqualObjects(blockself, self, @"Incorrect 'self' pointer");
    };

    /* Create the IMP */
    IMP imp = pl_imp_implementationWithBlock(Block);
    STAssertTrue(imp != NULL, @"Returned NULL IMP");

    /* Verify the IMP is valid. */
    void (*ptr)(id self, SEL _cmd) = (void *) imp;
    ptr(self, @selector(testAllocateIMP));
    STAssertTrue(didRun, @"Block was not run");

    /* Clean up */
    pl_imp_removeBlock(imp);
}

/**
 * Test basic stret IMP allocation and execution.
 */
- (void) testAllocateStretIMP {
    /* A test block */
    __block BOOL didRun = NO;
    NSRange (^Block)(id self) = ^NSRange (id blockself) {
        didRun = YES;

        STAssertEqualObjects(blockself, self, @"Incorrect 'self' pointer");
        return NSMakeRange(42, 1);
    };
    
    /* Create the IMP */
    IMP imp = pl_imp_implementationWithBlock(Block);
    STAssertTrue(imp != NULL, @"Returned NULL IMP");
    
    /* Verify the IMP is valid. */
    NSRange (*ptr)(id self, SEL _cmd) = (void *) imp;
    NSRange result = ptr(self, @selector(testAllocateStretIMP));

    STAssertTrue(didRun, @"Block was not run");
    STAssertEquals(result.location, (NSUInteger)42, @"Incorrect location");
    STAssertEquals(result.length, (NSUInteger)1, @"Incorrect length");
    
    /* Clean up */
    pl_imp_removeBlock(imp);
}

/**
 * Test fetching of the Block ptr from the IMP pointer.
 */ 
- (void) testGetBlock {
    /* A test block */
    void (^Block)(id self) = [[^(id blockself) {        
        STAssertEqualObjects(blockself, self, @"Incorrect 'self' pointer");
    } copy] autorelease];
    
    /* Create the IMP */
    IMP imp = pl_imp_implementationWithBlock(Block);
    STAssertTrue(imp != NULL, @"Returned NULL IMP");
    
    /* Try to fetch the underlying block */
    void *b = pl_imp_getBlock(imp);
    STAssertEquals(b, (void *) Block, @"Did not fetch block");
    
    /* Clean up */
    pl_imp_removeBlock(imp);
}

/**
 * Exercise block allocation
 */
- (void) testAllocationExcercise {
    /* A test block */
    __block int callCount = 0;
    void (^Block)(id self) = [[^(id blockself) {
        callCount++;
        STAssertEqualObjects(blockself, self, @"Incorrect 'self' pointer");
    } copy] autorelease];

    /* Use a count larger than what a single page (on any architecture) can likely hold */
    int count=PAGE_SIZE*2;

    /* Generate the IMPs */
    IMP *imps = malloc(sizeof(IMP) * count);
    for (int i = 0; i < count; i++) {
        imps[i] = pl_imp_implementationWithBlock(Block);
    }

    /* Call the IMPs */
    for (int i = 0; i < count; i++) {
        void (*ptr)(id self, SEL _cmd) = (void *) imps[i];
        ptr(self, @selector(testAllocationExcercise));
    }

    /* Clean up the IMPs. We do this in reverse to exercise table reordering. */
    for (int i = count-1; i+1 >= 1; i--) {
        pl_imp_removeBlock(imps[i]);
    }
    free(imps);

    /* Verify the result */
    STAssertEquals(callCount, count, @"Call count does not match expected count; not all IMPs were called");
}

@end
