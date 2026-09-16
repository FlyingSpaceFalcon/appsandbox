#ifndef ASB_KEYBOARD_MODIFIERS_MAC_H
#define ASB_KEYBOARD_MODIFIERS_MAC_H

#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>
#import <IOKit/hidsystem/IOLLEvent.h>

static inline NSArray<NSEvent *> *AsbKeyboardModifierEvents(NSEvent *event, NSEventModifierFlags *state) {
    if (event.type != NSEventTypeFlagsChanged) return @[event];
    if (event.keyCode != 0) {
        *state = event.modifierFlags;
        return @[event];
    }
    static const struct {
        NSEventModifierFlags mask, left, right;
        unsigned short leftCode, rightCode;
    } modifiers[] = {
        {NSEventModifierFlagShift, NX_DEVICELSHIFTKEYMASK, NX_DEVICERSHIFTKEYMASK, kVK_Shift, kVK_RightShift},
        {NSEventModifierFlagControl, NX_DEVICELCTLKEYMASK, NX_DEVICERCTLKEYMASK, kVK_Control, kVK_RightControl},
        {NSEventModifierFlagOption, NX_DEVICELALTKEYMASK, NX_DEVICERALTKEYMASK, kVK_Option, kVK_RightOption},
        {NSEventModifierFlagCommand, NX_DEVICELCMDKEYMASK, NX_DEVICERCMDKEYMASK, kVK_Command, kVK_RightCommand}
    };
    NSEventModifierFlags previous = *state, target = event.modifierFlags;
    for (NSUInteger i = 0; i < sizeof(modifiers) / sizeof(modifiers[0]); i++) {
        NSEventModifierFlags sides = modifiers[i].left | modifiers[i].right;
        if (!(target & modifiers[i].mask)) target &= ~sides;
        else if (!(target & sides)) target |= (previous & sides) ?: modifiers[i].left;
    }
    NSMutableArray<NSEvent *> *events = NSMutableArray.array;
    NSEventModifierFlags flags = previous;
    for (NSUInteger i = 0; i < sizeof(modifiers) / sizeof(modifiers[0]); i++) {
        NSEventModifierFlags sides[] = {modifiers[i].left, modifiers[i].right};
        unsigned short codes[] = {modifiers[i].leftCode, modifiers[i].rightCode};
        for (NSUInteger side = 0; side < 2; side++) {
            if (!sides[side] || !((previous ^ target) & sides[side])) continue;
            flags = (flags & ~sides[side]) | (target & sides[side]);
            if (flags & (modifiers[i].left | modifiers[i].right)) flags |= modifiers[i].mask;
            else flags &= ~modifiers[i].mask;
            NSEvent *transition = [NSEvent keyEventWithType:NSEventTypeFlagsChanged
                location:event.locationInWindow modifierFlags:flags timestamp:event.timestamp
                windowNumber:event.windowNumber context:nil characters:@"" charactersIgnoringModifiers:@""
                isARepeat:NO keyCode:codes[side]];
            if (transition) [events addObject:transition];
        }
    }
    *state = target;
    return events;
}

#endif
