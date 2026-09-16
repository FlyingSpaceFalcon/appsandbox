#import <Virtualization/Virtualization.h>

@interface VzAccessibilityView : VZVirtualMachineView
- (void)configureWithSocketDevice:(VZVirtioSocketDevice *)device vmName:(NSString *)name;
- (void)resetKeyboardModifiers;
- (void)startAccessibility;
- (void)stopAccessibility;
@end
