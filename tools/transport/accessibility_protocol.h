#ifndef ASB_ACCESSIBILITY_PROTOCOL_H
#define ASB_ACCESSIBILITY_PROTOCOL_H

/* Shared by native AX and UI Automation collectors. Transport messages are
 * UTF-8 JSON dictionaries prefixed by a four-byte big-endian byte count.
 * Trees have session and revision identifiers; action replies use request IDs. */
#define ASB_AX_VERSION 1
#define ASB_AX_PORT 9
#define ASB_AX_MAX_MESSAGE (64u * 1024u * 1024u)
#define ASB_AX_MAX_NODES 65536
#define ASB_AX_MAX_STRING (1024u * 1024u)
#define ASB_AX_INPUT_SETTLE_MS 1000
#define ASB_AX_REQUEST_TIMEOUT_MS 5000
#define ASB_AX_CAPTURE_TIMEOUT_MS 30000
#define ASB_AX_MAX_PENDING_REQUESTS 8

#endif
