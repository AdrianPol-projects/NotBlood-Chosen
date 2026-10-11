#include "compat.h"
#include "osxbits.h"
#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>

#ifndef MAC_OS_X_VERSION_10_5
# define NSImageScaleNone NSScaleNone
#endif

#ifndef MAC_OS_X_VERSION_10_12
# define NSEventModifierFlagOption NSAlternateKeyMask
# define NSEventModifierFlagCommand NSCommandKeyMask
# define NSEventMaskAny NSAnyEventMask
# define NSWindowStyleMaskTitled NSTitledWindowMask
# define NSWindowStyleMaskClosable NSClosableWindowMask
# define NSWindowStyleMaskMiniaturizable NSMiniaturizableWindowMask
# define NSWindowStyleMaskResizable NSResizableWindowMask
# define NSAlertStyleInformational NSInformationalAlertStyle
# define NSControlSizeSmall NSSmallControlSize
#endif

#ifndef MAC_OS_VERSION_10_3
# define MAC_OS_VERSION_10_3 1030
#endif

id nsapp;

void osx_preopen(void)
{
    // fix for "ld: absolute address to symbol _NSApp in a different linkage unit not supported"
    // (OS X 10.6) when building for PPC
    nsapp = [NSApplication sharedApplication];
}

void osx_postopen(void)
{
    [nsapp finishLaunching];
}

int osx_msgbox(const char *name, const char *msg)
{
	NSString *mmsg = [[NSString alloc] initWithUTF8String:msg];

#if MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_3
	NSAlert *alert = [[NSAlert alloc] init];
	[alert addButtonWithTitle: @"OK"];
	[alert setInformativeText: mmsg];
	[alert setAlertStyle: NSAlertStyleInformational];

	[alert runModal];

	[alert release];

#else
	NSRunAlertPanel(nil, mmsg, @"OK", nil, nil);
#endif

	[mmsg release];

	UNREFERENCED_PARAMETER(name);

	return 0;
}

int osx_ynbox(const char *name, const char *msg)
{
	NSString *mmsg = [[NSString alloc] initWithUTF8String:msg];
	int r;

#if MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_3
	NSAlert *alert = [[NSAlert alloc] init];

	[alert addButtonWithTitle:@"Yes"];
	[alert addButtonWithTitle:@"No"];
	[alert setInformativeText: mmsg];
	[alert setAlertStyle: NSAlertStyleInformational];

	r = ([alert runModal] == NSAlertFirstButtonReturn);

	[alert release];
#else
	r = (NSRunAlertPanel(nil, mmsg, @"Yes", @"No", nil) == NSAlertDefaultReturn);
#endif

	[mmsg release];

	UNREFERENCED_PARAMETER(name);

	return r;
}

char *osx_gethomedir(void)
{
    NSString *path = NSHomeDirectory();
    const char *Cpath = [path UTF8String];
    char *returnpath = NULL;

    if (Cpath)
        returnpath = Xstrdup(Cpath);

    // NSHomeDirectory() isn't ours to release: releasing it freed it under the rest of the program

    return returnpath;
}

char *osx_getsupportdir(int32_t local)
{
    NSArray *paths = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, local ? NSUserDomainMask : NSLocalDomainMask, YES);
    char *returnpath = NULL;

    if ([paths count] > 0)
    {
        const char *Cpath = [[paths objectAtIndex:0] UTF8String];

        if (Cpath)
            returnpath = Xstrdup(Cpath);
    }

    // the array is autoreleased: not ours to release

    return returnpath;
}

char *osx_getappdir(void)
{
    // CFBundleGetMainBundle() isn't ours to release: releasing it (as this did before) freed the
    // app's bundle and macOS crashed at startup in the keyboard setup. fileSystemRepresentation
    // also works for folders with non-ASCII names, which CFStringGetCStringPtr() didn't.
    char *dir = NULL;
    @autoreleasepool
    {
        NSString *path = [[NSBundle mainBundle] resourcePath];
        const char *s = path ? [path fileSystemRepresentation] : NULL;
        if (s)
            dir = Xstrdup(s);
    }
    return dir;
}

char *osx_getapplicationsdir(int32_t local)
{
    NSArray *paths = NSSearchPathForDirectoriesInDomains(NSAllApplicationsDirectory, local ? NSUserDomainMask : NSLocalDomainMask, YES);
    char *returnpath = NULL;

    if ([paths count] > 0)
    {
        const char *Cpath = [[paths objectAtIndex:0] UTF8String];

        if (Cpath)
            returnpath = Xstrdup(Cpath);
    }

    // the array is autoreleased: not ours to release

    return returnpath;
}

char *osx_getdocumentsdir(int32_t local)
{
    NSArray *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, local ? NSUserDomainMask : NSLocalDomainMask, YES);
    char *returnpath = NULL;

    if ([paths count] > 0)
    {
        const char *Cpath = [[paths objectAtIndex:0] UTF8String];

        if (Cpath)
            returnpath = Xstrdup(Cpath);
    }

    // the array is autoreleased: not ours to release

    return returnpath;
}
