/* Per-view accessibility service. Configure/start/stop on the main thread. */
#import <Cocoa/Cocoa.h>
#import <Virtualization/Virtualization.h>

@class AsbIvshmemTransport;

@interface VmAccessibilityMac : NSObject
@property(nonatomic, weak, readonly) NSView *view;
@property(nonatomic, readonly) BOOL hasAccessibilityContent;
@property(nonatomic, readonly) NSDictionary *snapshotStatistics;
- (instancetype)initWithView:(NSView *)view vmName:(NSString *)name;
- (void)configureWithSocketDevice:(VZVirtioSocketDevice *)device virtualMachine:(VZVirtualMachine *)machine;
- (void)configureWithTransport:(AsbIvshmemTransport *)transport;
- (BOOL)configureWithFileDescriptor:(int)fileDescriptor;
- (void)noteGuestInput;
- (void)startAccessibility;
- (void)stopAccessibility;
- (void)observeWindow;
- (void)geometryDidChange;
- (void)updateDisplayPixels:(NSSize)pixels;
- (NSString *)accessibilityLabel;
- (NSArray *)accessibilityChildren;
- (id)accessibilityFocusedUIElement;
- (id)accessibilityHitTest:(NSPoint)point;
@end
