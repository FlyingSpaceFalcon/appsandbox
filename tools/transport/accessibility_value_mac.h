#ifndef ASB_ACCESSIBILITY_VALUE_MAC_H
#define ASB_ACCESSIBILITY_VALUE_MAC_H

#import <Cocoa/Cocoa.h>
#import <ApplicationServices/ApplicationServices.h>
#include <dlfcn.h>
#include <math.h>
#import "accessibility_wire_mac.h"

typedef id (^AsbAXReferenceEncoder)(id value);
typedef id (^AsbAXReferenceDecoder)(NSString *identifier);
typedef NSValue *(^AsbAXGeometryTransform)(NSValue *value);

static inline BOOL AsbAXParameterizedAttributeMutates(NSString *attribute) {
    return [attribute isEqual:@"AXTextOperation"] || [attribute isEqual:@"AXSelectTextWithCriteria"] ||
        [attribute isEqual:@"AXReplaceRangeWithText"];
}

static inline BOOL AsbAXIsMaskedValue(id value) {
    if (![value isKindOfClass:NSString.class] || [value length] > ASB_AX_MAX_STRING) return NO;
    NSCharacterSet *mask = [NSCharacterSet characterSetWithCharactersInString:@"\u2022\u25cf\uf79a*"];
    return [value rangeOfCharacterFromSet:mask.invertedSet].location == NSNotFound;
}

static inline BOOL AsbAXFiniteArray(id value, NSUInteger count, BOOL range) {
    if (![value isKindOfClass:NSArray.class] || [value count] != count) return NO;
    for (id number in value) {
        if (![number isKindOfClass:NSNumber.class] || !isfinite([number doubleValue])) return NO;
        if (range && ([number doubleValue] < 0 || [number doubleValue] > 9007199254740991.0 ||
            floor([number doubleValue]) != [number doubleValue])) return NO;
    }
    if (range && [value[0] doubleValue] + [value[1] doubleValue] > 9007199254740991.0) return NO;
    return YES;
}

static inline BOOL AsbAXWireValueValid(id value, NSUInteger depth) {
    if (!value || depth > 24) return NO;
    if (value == NSNull.null) return YES;
    if ([value isKindOfClass:NSString.class]) return [value length] <= ASB_AX_MAX_MESSAGE;
    if ([value isKindOfClass:NSNumber.class]) return isfinite([value doubleValue]);
    if ([value isKindOfClass:NSArray.class]) {
        if ([value count] > ASB_AX_MAX_NODES) return NO;
        for (id child in value) if (!AsbAXWireValueValid(child, depth + 1)) return NO;
        return YES;
    }
    if (![value isKindOfClass:NSDictionary.class] || [value count] > ASB_AX_MAX_NODES) return NO;
    for (id key in value) if (![key isKindOfClass:NSString.class] || [key length] > ASB_AX_MAX_STRING ||
        !AsbAXWireValueValid(value[key], depth + 1)) return NO;
    NSString *kind = value[@"$ax"];
    if (!kind) return YES;
    id data = value[@"value"];
    if ([kind isEqual:@"point"] || [kind isEqual:@"size"]) return AsbAXFiniteArray(data, 2, NO);
    if ([kind isEqual:@"rect"]) return AsbAXFiniteArray(data, 4, NO);
    if ([kind isEqual:@"range"]) return AsbAXFiniteArray(data, 2, YES);
    if ([kind isEqual:@"element"]) return [data isKindOfClass:NSString.class] && [data length] > 0 && [data length] <= 128;
    if ([kind isEqual:@"date"]) return [data isKindOfClass:NSNumber.class];
    if ([kind isEqual:@"double"]) return [data isKindOfClass:NSNumber.class];
    if ([kind isEqual:@"color"]) return [data isKindOfClass:NSArray.class] && [data count] > 0 && [data count] <= 32 && value[@"space"] != nil;
    if ([kind isEqual:@"font"]) return [data isKindOfClass:NSString.class] &&
        [value[@"size"] isKindOfClass:NSNumber.class] && [value[@"size"] doubleValue] > 0;
    if ([kind isEqual:@"dictionary"]) return [data isKindOfClass:NSDictionary.class];
    if ([kind isEqual:@"attributed"]) return [data isKindOfClass:NSString.class] &&
        [value[@"runs"] isKindOfClass:NSArray.class];
    if ([kind isEqual:@"markerRange"]) return [data isKindOfClass:NSArray.class] && [data count] == 2;
    return ([kind isEqual:@"url"] || [kind isEqual:@"data"] || [kind isEqual:@"marker"]) &&
        [data isKindOfClass:NSString.class];
}

static inline id AsbAXEncodeValue(id value, AsbAXReferenceEncoder reference, NSUInteger depth) {
    if (!value || value == NSNull.null) return NSNull.null;
    if (depth > 24) return nil;
    if (reference) { id encoded = reference(value); if (encoded) return encoded; }
    if ([value isKindOfClass:NSString.class]) return [value length] <= ASB_AX_MAX_MESSAGE ? value : nil;
    if ([value isKindOfClass:NSNumber.class]) {
        if (!isfinite([value doubleValue])) return nil;
        if (CFGetTypeID((__bridge CFTypeRef)value) != CFBooleanGetTypeID() &&
            CFNumberIsFloatType((__bridge CFNumberRef)value)) return @{@"$ax": @"double", @"value": value};
        return value;
    }
    if ([value isKindOfClass:NSURL.class]) return @{@"$ax": @"url", @"value": [value absoluteString]};
    if ([value isKindOfClass:NSData.class]) return [value length] <= ASB_AX_MAX_MESSAGE * 3 / 4 ?
        @{@"$ax": @"data", @"value": [value base64EncodedStringWithOptions:0]} : nil;
    if ([value isKindOfClass:NSDate.class]) return @{@"$ax": @"date", @"value": @([value timeIntervalSinceReferenceDate])};
    if ([value isKindOfClass:NSFont.class]) return @{@"$ax": @"font", @"value": [value fontName], @"size": @([value pointSize])};
    if ([value isKindOfClass:NSColor.class]) return AsbAXEncodeValue((__bridge id)[value CGColor], reference, depth + 1);
    if ([value isKindOfClass:NSAttributedString.class]) {
        NSMutableArray *runs = NSMutableArray.array;
        __block BOOL valid = YES;
        [value enumerateAttributesInRange:NSMakeRange(0, [value length]) options:0
            usingBlock:^(NSDictionary *attributes, NSRange range, BOOL *stop) {
                id encoded = AsbAXEncodeValue(attributes, reference, depth + 1);
                if (!encoded) { valid = NO; *stop = YES; return; }
                [runs addObject:@{@"range": @[@(range.location), @(range.length)], @"attributes": encoded}];
            }];
        return valid ? @{@"$ax": @"attributed", @"value": [value string], @"runs": runs} : nil;
    }
    if ([value isKindOfClass:NSArray.class]) {
        if ([value count] > ASB_AX_MAX_NODES) return nil;
        NSMutableArray *encoded = NSMutableArray.array;
        for (id child in value) {
            id item = AsbAXEncodeValue(child, reference, depth + 1);
            if (!item) return nil;
            [encoded addObject:item];
        }
        return encoded;
    }
    if ([value isKindOfClass:NSDictionary.class]) {
        NSMutableDictionary *encoded = NSMutableDictionary.dictionary;
        for (id key in value) {
            if (![key isKindOfClass:NSString.class]) return nil;
            id item = AsbAXEncodeValue(value[key], reference, depth + 1);
            if (!item) return nil;
            encoded[key] = item;
        }
        return @{@"$ax": @"dictionary", @"value": encoded};
    }
    CFTypeID type = CFGetTypeID((__bridge CFTypeRef)value);
    if (type == CGColorGetTypeID()) {
        CGColorRef color = (__bridge CGColorRef)value;
        CGColorSpaceRef space = CGColorGetColorSpace(color);
        if (!space || CGColorGetPattern(color)) return nil;
        id properties = CFBridgingRelease(CGColorSpaceCopyPropertyList(space));
        id encodedSpace = properties ? AsbAXEncodeValue(properties, reference, depth + 1) : nil;
        if (!encodedSpace) return nil;
        NSMutableArray *components = NSMutableArray.array;
        const CGFloat *values = CGColorGetComponents(color);
        for (size_t i = 0; i < CGColorGetNumberOfComponents(color); i++) [components addObject:@(values[i])];
        return @{@"$ax": @"color", @"value": components, @"space": encodedSpace};
    }
    NSValue *geometry = nil;
    if (type == AXValueGetTypeID()) {
        AXValueRef ax = (__bridge AXValueRef)value;
        switch (AXValueGetType(ax)) {
            case kAXValueCGPointType: { CGPoint v; if (AXValueGetValue(ax, kAXValueCGPointType, &v)) geometry = [NSValue valueWithPoint:v]; break; }
            case kAXValueCGSizeType: { CGSize v; if (AXValueGetValue(ax, kAXValueCGSizeType, &v)) geometry = [NSValue valueWithSize:v]; break; }
            case kAXValueCGRectType: { CGRect v; if (AXValueGetValue(ax, kAXValueCGRectType, &v)) geometry = [NSValue valueWithRect:v]; break; }
            case kAXValueCFRangeType: {
                CFRange range;
                if (AXValueGetValue(ax, kAXValueCFRangeType, &range) && range.location >= 0 && range.length >= 0)
                    geometry = [NSValue valueWithRange:NSMakeRange(range.location, range.length)];
                break;
            }
            default: return nil;
        }
    } else if ([value isKindOfClass:NSValue.class]) geometry = value;
    if (geometry) {
        const char *encoding = geometry.objCType;
        NSDictionary *result = nil;
        if (!strcmp(encoding, @encode(NSRange))) { NSRange r = geometry.rangeValue;
            if (r.location != NSNotFound) result = @{@"$ax": @"range", @"value": @[@(r.location), @(r.length)]}; }
        else if (!strcmp(encoding, @encode(NSPoint))) { NSPoint p = geometry.pointValue; result = @{@"$ax": @"point", @"value": @[@(p.x), @(p.y)]}; }
        else if (!strcmp(encoding, @encode(NSSize))) { NSSize s = geometry.sizeValue; result = @{@"$ax": @"size", @"value": @[@(s.width), @(s.height)]}; }
        else if (!strcmp(encoding, @encode(NSRect))) { NSRect r = geometry.rectValue;
            result = @{@"$ax": @"rect", @"value": @[@(r.origin.x), @(r.origin.y), @(r.size.width), @(r.size.height)]}; }
        return AsbAXWireValueValid(result, 0) ? result : nil;
    }
    CFTypeID (*markerType)(void) = dlsym(RTLD_DEFAULT, "AXTextMarkerGetTypeID");
    CFTypeID (*rangeType)(void) = dlsym(RTLD_DEFAULT, "AXTextMarkerRangeGetTypeID");
    if (markerType && type == markerType()) {
        CFIndex (*length)(CFTypeRef) = dlsym(RTLD_DEFAULT, "AXTextMarkerGetLength");
        const UInt8 *(*bytes)(CFTypeRef) = dlsym(RTLD_DEFAULT, "AXTextMarkerGetBytePtr");
        CFIndex count = length ? length((__bridge CFTypeRef)value) : 0;
        if (!bytes || count <= 0 || count > ASB_AX_MAX_STRING) return nil;
        NSData *data = [NSData dataWithBytes:bytes((__bridge CFTypeRef)value) length:count];
        return @{@"$ax": @"marker", @"value": [data base64EncodedStringWithOptions:0]};
    }
    if (rangeType && type == rangeType()) {
        CFTypeRef (*start)(CFTypeRef) = dlsym(RTLD_DEFAULT, "AXTextMarkerRangeCopyStartMarker");
        CFTypeRef (*end)(CFTypeRef) = dlsym(RTLD_DEFAULT, "AXTextMarkerRangeCopyEndMarker");
        if (!start || !end) return nil;
        id a = AsbAXEncodeValue(CFBridgingRelease(start((__bridge CFTypeRef)value)), reference, depth + 1);
        id b = AsbAXEncodeValue(CFBridgingRelease(end((__bridge CFTypeRef)value)), reference, depth + 1);
        return a && b ? @{@"$ax": @"markerRange", @"value": @[a, b]} : nil;
    }
    return nil;
}

static inline id AsbAXDecodeValue(id value, BOOL axValues, AsbAXReferenceDecoder reference,
                         AsbAXGeometryTransform transform, NSUInteger depth) {
    if (depth > 24 || !AsbAXWireValueValid(value, depth)) return nil;
    if (value == NSNull.null) return value;
    if ([value isKindOfClass:NSString.class] || [value isKindOfClass:NSNumber.class]) return value;
    if ([value isKindOfClass:NSArray.class]) {
        NSMutableArray *decoded = NSMutableArray.array;
        for (id child in value) {
            id item = AsbAXDecodeValue(child, axValues, reference, transform, depth + 1);
            if (!item) return nil;
            [decoded addObject:item];
        }
        return decoded;
    }
    NSString *kind = value[@"$ax"];
    id data = value[@"value"];
    if ([kind isEqual:@"element"]) return reference ? reference(data) : nil;
    if ([kind isEqual:@"url"]) return [NSURL URLWithString:data];
    if ([kind isEqual:@"data"]) return [[NSData alloc] initWithBase64EncodedString:data options:0];
    if ([kind isEqual:@"date"]) return [NSDate dateWithTimeIntervalSinceReferenceDate:[data doubleValue]];
    if ([kind isEqual:@"double"]) return @([data doubleValue]);
    if ([kind isEqual:@"font"]) return [NSFont fontWithName:data size:[value[@"size"] doubleValue]];
    if ([kind isEqual:@"color"]) {
        id properties = AsbAXDecodeValue(value[@"space"], axValues, reference, transform, depth + 1);
        CGColorSpaceRef space = properties ? CGColorSpaceCreateWithPropertyList((__bridge CFPropertyListRef)properties) : NULL;
        if (!space) return nil;
        size_t count = CGColorSpaceGetNumberOfComponents(space) + 1;
        if (count != [data count] || count > 32) { CGColorSpaceRelease(space); return nil; }
        CGFloat components[32];
        for (size_t i = 0; i < count; i++) {
            if (![data[i] isKindOfClass:NSNumber.class]) { CGColorSpaceRelease(space); return nil; }
            components[i] = [data[i] doubleValue];
        }
        CGColorRef color = CGColorCreate(space, components);
        CGColorSpaceRelease(space);
        return CFBridgingRelease(color);
    }
    if ([kind isEqual:@"dictionary"] || !kind) {
        NSDictionary *source = kind ? data : value;
        NSMutableDictionary *decoded = NSMutableDictionary.dictionary;
        for (NSString *key in source) {
            id item = AsbAXDecodeValue(source[key], axValues, reference, transform, depth + 1);
            if (!item) return nil;
            decoded[key] = item;
        }
        return decoded;
    }
    if ([kind isEqual:@"attributed"]) {
        NSMutableAttributedString *text = [[NSMutableAttributedString alloc] initWithString:data];
        for (id run in value[@"runs"]) {
            if (![run isKindOfClass:NSDictionary.class] || !AsbAXFiniteArray(run[@"range"], 2, YES)) return nil;
            NSUInteger location = [run[@"range"][0] unsignedIntegerValue], length = [run[@"range"][1] unsignedIntegerValue];
            if (location > text.length || length > text.length - location) return nil;
            id attributes = AsbAXDecodeValue(run[@"attributes"], axValues, reference, transform, depth + 1);
            if (![attributes isKindOfClass:NSDictionary.class]) return nil;
            [text addAttributes:attributes range:NSMakeRange(location, length)];
        }
        return text;
    }
    if ([kind isEqual:@"marker"]) {
        CFTypeRef (*create)(CFAllocatorRef, const UInt8 *, CFIndex) = dlsym(RTLD_DEFAULT, "AXTextMarkerCreate");
        NSData *bytes = [[NSData alloc] initWithBase64EncodedString:data options:0];
        return create && bytes.length && bytes.length <= ASB_AX_MAX_STRING ?
            CFBridgingRelease(create(kCFAllocatorDefault, bytes.bytes, bytes.length)) : nil;
    }
    if ([kind isEqual:@"markerRange"]) {
        CFTypeRef (*create)(CFAllocatorRef, CFTypeRef, CFTypeRef) = dlsym(RTLD_DEFAULT, "AXTextMarkerRangeCreate");
        CFTypeID (*markerType)(void) = dlsym(RTLD_DEFAULT, "AXTextMarkerGetTypeID");
        id a = AsbAXDecodeValue(data[0], axValues, reference, transform, depth + 1);
        id b = AsbAXDecodeValue(data[1], axValues, reference, transform, depth + 1);
        return create && markerType && a && b && CFGetTypeID((__bridge CFTypeRef)a) == markerType() &&
            CFGetTypeID((__bridge CFTypeRef)b) == markerType() ?
            CFBridgingRelease(create(kCFAllocatorDefault, (__bridge CFTypeRef)a, (__bridge CFTypeRef)b)) : nil;
    }
    NSValue *geometry = nil;
    if ([kind isEqual:@"range"]) geometry = [NSValue valueWithRange:NSMakeRange([data[0] unsignedIntegerValue], [data[1] unsignedIntegerValue])];
    if ([kind isEqual:@"point"]) geometry = [NSValue valueWithPoint:NSMakePoint([data[0] doubleValue], [data[1] doubleValue])];
    if ([kind isEqual:@"size"]) geometry = [NSValue valueWithSize:NSMakeSize([data[0] doubleValue], [data[1] doubleValue])];
    if ([kind isEqual:@"rect"]) geometry = [NSValue valueWithRect:NSMakeRect([data[0] doubleValue], [data[1] doubleValue], [data[2] doubleValue], [data[3] doubleValue])];
    if (geometry && transform) geometry = transform(geometry);
    if (!geometry || !axValues) return geometry;
    if (!strcmp(geometry.objCType, @encode(NSRange))) { NSRange r = geometry.rangeValue; CFRange v = CFRangeMake(r.location, r.length); return CFBridgingRelease(AXValueCreate(kAXValueCFRangeType, &v)); }
    if (!strcmp(geometry.objCType, @encode(NSPoint))) { CGPoint v = geometry.pointValue; return CFBridgingRelease(AXValueCreate(kAXValueCGPointType, &v)); }
    if (!strcmp(geometry.objCType, @encode(NSSize))) { CGSize v = geometry.sizeValue; return CFBridgingRelease(AXValueCreate(kAXValueCGSizeType, &v)); }
    if (!strcmp(geometry.objCType, @encode(NSRect))) { CGRect v = geometry.rectValue; return CFBridgingRelease(AXValueCreate(kAXValueCGRectType, &v)); }
    return nil;
}

#endif
