#ifndef ASB_ACCESSIBILITY_WIRE_MAC_H
#define ASB_ACCESSIBILITY_WIRE_MAC_H

#import <Foundation/Foundation.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/socket.h>

#include "accessibility_protocol.h"

static inline BOOL AsbAXReadBytes(int fd, void *buffer, size_t length) {
    uint8_t *cursor = buffer;
    while (length) {
        ssize_t count = recv(fd, cursor, length, 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return NO;
        cursor += count;
        length -= (size_t)count;
    }
    return YES;
}

static inline BOOL AsbAXWriteBytes(int fd, const void *buffer, size_t length) {
    const uint8_t *cursor = buffer;
    while (length) {
        ssize_t count = send(fd, cursor, length, 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return NO;
        cursor += count;
        length -= (size_t)count;
    }
    return YES;
}

static inline NSDictionary *AsbAXReadMessage(int fd) {
    uint32_t networkLength = 0;
    if (!AsbAXReadBytes(fd, &networkLength, sizeof(networkLength))) return nil;
    uint32_t length = ntohl(networkLength);
    if (!length || length > ASB_AX_MAX_MESSAGE) return nil;
    NSMutableData *data = [NSMutableData dataWithLength:length];
    if (!AsbAXReadBytes(fd, data.mutableBytes, length)) return nil;
    id value = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
    return [value isKindOfClass:NSDictionary.class] ? value : nil;
}

static inline BOOL AsbAXWriteMessage(int fd, NSDictionary *message) {
    if (![NSJSONSerialization isValidJSONObject:message]) return NO;
    NSData *data = [NSJSONSerialization dataWithJSONObject:message options:0 error:nil];
    if (!data.length || data.length > ASB_AX_MAX_MESSAGE) return NO;
    uint32_t networkLength = htonl((uint32_t)data.length);
    return AsbAXWriteBytes(fd, &networkLength, sizeof(networkLength)) &&
           AsbAXWriteBytes(fd, data.bytes, data.length);
}

#endif
