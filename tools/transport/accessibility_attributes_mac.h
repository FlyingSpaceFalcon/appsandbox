#ifndef ASB_ACCESSIBILITY_ATTRIBUTES_MAC_H
#define ASB_ACCESSIBILITY_ATTRIBUTES_MAC_H

#import <Foundation/Foundation.h>
#import <dispatch/dispatch.h>

// Automatic snapshots have a defined state contract. Advertising an attribute
// does not make its getter passive or cheap: getters may navigate content or
// perform an expensive text/layout calculation. Unlisted attributes stay
// advertised and use native queries when a client explicitly requests them.
// Keep this policy shared by the collector and proxy; never key it on an app,
// website, element label, or client. Actions and setters remain capability based.
static inline BOOL AsbAXAttributeRequiresLiveQuery(NSString *attribute) {
    static NSSet<NSString *> *snapshotAttributes;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        snapshotAttributes = [NSSet setWithArray:@[
            // Identity, presentation, geometry, and current control state.
            @"AXRole", @"AXSubrole", @"AXRoleDescription", @"AXIdentifier",
            @"AXTitle", @"AXDescription", @"AXAttributedTitle", @"AXAttributedDescription",
            @"AXHelp", @"AXValue", @"AXValueDescription", @"AXPlaceholderValue",
            @"AXEnabled", @"AXFocused", @"AXSelected", @"AXExpanded", @"AXDisclosing",
            @"AXRequired", @"AXHidden", @"AXElementBusy", @"AXMain", @"AXMinimized", @"AXModal",
            @"AXPosition", @"AXSize", @"AXOrientation", @"AXMinValue", @"AXMaxValue",
            @"AXValueIncrement", @"AXValueWraps", @"AXURL", @"AXDocument", @"AXFilename",
            // Tree, selection, and the relationships used by native controls.
            @"AXParent", @"AXChildren", @"AXVisibleChildren", @"AXSelectedChildren",
            @"AXWindow", @"AXTopLevelUIElement", @"AXFocusedUIElement",
            @"AXTitleUIElement", @"AXServesAsTitleForUIElements", @"AXLinkedUIElements",
            @"AXContents", @"AXTabs", @"AXHeader", @"AXSplitters", @"AXHandles",
            @"AXCloseButton", @"AXMinimizeButton", @"AXZoomButton", @"AXFullScreenButton",
            @"AXDefaultButton", @"AXCancelButton", @"AXToolbarButton", @"AXOverflowButton",
            @"AXHorizontalScrollBar", @"AXVerticalScrollBar", @"AXIncrementor",
            @"AXIncrementButton", @"AXDecrementButton",
            // Table/outline structure. Native order is preserved separately
            // from the visible-first traversal used to gather descendants.
            @"AXRows", @"AXVisibleRows", @"AXSelectedRows", @"AXRowCount",
            @"AXColumns", @"AXVisibleColumns", @"AXSelectedColumns", @"AXColumnCount",
            @"AXVisibleCells", @"AXColumnTitles", @"AXRowHeaderUIElements", @"AXColumnHeaderUIElements",
            @"AXRowIndexRange", @"AXColumnIndexRange", @"AXDisclosedRows", @"AXDisclosedByRow", @"AXDisclosureLevel",
            // Current editing state. Navigation, text markers, and computed
            // visible ranges are queried on demand instead of during traversal.
            @"AXSelectedText", @"AXSelectedTextRange", @"AXSelectedTextRanges",
            @"AXNumberOfCharacters", @"AXInsertionPointLineNumber",
            // Menu command metadata exposed by AppKit and other AX providers.
            @"AXMenuItemCmdChar", @"AXMenuItemCmdVirtualKey", @"AXMenuItemCmdGlyph",
            @"AXMenuItemCmdModifiers", @"AXMenuItemMarkChar", @"AXMenuItemPrimaryUIElement"
        ]];
    });
    return ![snapshotAttributes containsObject:attribute];
}

static inline NSArray<NSString *> *AsbAXCaptureAttributeNames(NSArray<NSString *> *names) {
    NSMutableArray *captured = [NSMutableArray arrayWithCapacity:names.count];
    for (NSString *name in names)
        if (!AsbAXAttributeRequiresLiveQuery(name)) [captured addObject:name];
    return captured;
}

#endif
