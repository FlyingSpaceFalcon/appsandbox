#import <Cocoa/Cocoa.h>

@class VmAccessibilityElement;

@protocol VmAccessibilityElementOwner <NSObject>
- (id)window;
- (BOOL)isCurrentElement:(VmAccessibilityElement *)element;
- (NSArray *)childrenForElement:(VmAccessibilityElement *)element;
- (id)parentForElement:(VmAccessibilityElement *)element;
- (NSRect)frameForElement:(VmAccessibilityElement *)element;
- (id)focusedGuestElement;
- (BOOL)performAction:(NSString *)action value:(id)value element:(VmAccessibilityElement *)element;
- (NSDictionary *)request:(NSDictionary *)message element:(VmAccessibilityElement *)element;
- (id)elementForIdentifier:(NSString *)identifier session:(NSString *)session;
- (NSValue *)transformGeometry:(NSValue *)value toGuest:(BOOL)toGuest;
@end

@interface VmAccessibilityElement : NSObject
@property (weak) id<VmAccessibilityElementOwner> owner;
@property (copy) NSString *nodeID;
@property (copy) NSString *session;
@property (copy) NSDictionary *record;
@property BOOL container;
@property BOOL status;
@property BOOL retired;
@property (readonly) NSRect accessibilityFrame;
@property (readonly) NSRect accessibilityFrameInParentSpace;
@property (readonly) NSString *accessibilityRole;
@property (readonly) id accessibilityValue;
@property (readonly) id accessibilityParent;
@property (readonly) NSArray *accessibilityChildren;
- (BOOL)isAccessibilityElement;
- (id)hitTestGuest:(NSPoint)point;
- (BOOL)hasVisibleGuestContent;
- (id)decode:(id)value geometry:(BOOL)geometry;
- (id)encode:(id)value geometry:(BOOL)geometry;
- (void)retire;
@end
