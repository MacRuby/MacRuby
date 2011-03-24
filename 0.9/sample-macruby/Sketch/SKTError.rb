SKTUnknownFileReadError       = 1
SKTUnknownPasteboardReadError = 2
SKTWriteCouldntMakeTIFFError  = 3

SKTErrorDomain = "SketchErrorDomain"

def SKTErrorWithCode (code)
	# An NSError has a bunch of parameters that determine how it's presented
	# to the user. We specify two of them here. They're localized strings that
	# we look up in SKTError.strings, whose keys are derived from the error
	# code and an indicator of which kind of localized string we're looking
	# up. The value: strings are specified so that at least something is shown
	# if there's a problem with the strings file, but really they should never
	# ever be shown to the user. When testing an app like Sketch you really
	# have to make sure that you've seen every call of SKTErrorWithCode()
	# executed since the last time you did things like change the set of
	# available error codes or edit the strings files.
	mainBundle = NSBundle.mainBundle
	localizedDescription = mainBundle.localizedStringForKey(NSString.stringWithFormat("description%ld", code), 
			value: "Sketch could not complete the operation because an unknown error occurred.", 
			table: "SKTError")
	localizedFailureReason = mainBundle.localizedStringForKey(NSString.stringWithFormat("failureReason%ld", code),
	 		value: "An unknown error occurred.",
	 		table: "SKTError")
	errorUserInfo = {NSLocalizedDescriptionKey => localizedDescription, 
						NSLocalizedFailureReasonErrorKey => localizedFailureReason}
	
	# In Sketch we know that no one's going to be paying attention to the domain
	# and code that we use here, but still we don't specify junk values.
	# Certainly we don't just use NSCocoaErrorDomain and some random error code.
	return NSError.errorWithDomain(SKTErrorDomain, code: code, userInfo: errorUserInfo)
end


=begin

Derived from Apple's sample code for Sketch and converted to MacRuby by Dave Baldwin.

Additional comments from corresponding header file:

None.


---------------------------------------------------------------------------------------------
Apple's original notice:

/*
IMPORTANT:  This Apple software is supplied to you by Apple Computer, Inc. ("Apple") in
consideration of your agreement to the following terms, and your use, installation,
modification or redistribution of this Apple software constitutes acceptance of these
terms.  If you do not agree with these terms, please do not use, install, modify or
redistribute this Apple software.

In consideration of your agreement to abide by the following terms, and subject to these
terms, Apple grants you a personal, non-exclusive license, under Apple's copyrights in
this original Apple software (the "Apple Software"), to use, reproduce, modify and
redistribute the Apple Software, with or without modifications, in source and/or binary
forms; provided that if you redistribute the Apple Software in its entirety and without
modifications, you must retain this notice and the following text and disclaimers in all
such redistributions of the Apple Software.  Neither the name, trademarks, service marks
or logos of Apple Computer, Inc. may be used to endorse or promote products derived from
the Apple Software without specific prior written permission from Apple. Except as expressly
stated in this notice, no other rights or licenses, express or implied, are granted by Apple
herein, including but not limited to any patent rights that may be infringed by your
derivative works or by other works in which the Apple Software may be incorporated.

The Apple Software is provided by Apple on an "AS IS" basis.  APPLE MAKES NO WARRANTIES,
EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION THE IMPLIED WARRANTIES OF NON-INFRINGEMENT,
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, REGARDING THE APPLE SOFTWARE OR ITS
USE AND OPERATION ALONE OR IN COMBINATION WITH YOUR PRODUCTS.

IN NO EVENT SHALL APPLE BE LIABLE FOR ANY SPECIAL, INDIRECT, INCIDENTAL OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) ARISING IN ANY WAY OUT OF THE USE,
REPRODUCTION, MODIFICATION AND/OR DISTRIBUTION OF THE APPLE SOFTWARE, HOWEVER CAUSED AND
WHETHER UNDER THEORY OF CONTRACT, TORT (INCLUDING NEGLIGENCE), STRICT LIABILITY OR
OTHERWISE, EVEN IF APPLE HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

=end