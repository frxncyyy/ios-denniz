/* SPDX-License-Identifier: MIT
 * Minimal libxpc declarations for n2btd (the iOS SDK ships no public <xpc/xpc.h>; libxpc is re-exported by libSystem).
 * Only opaque pointer types and the C functions n2btd calls; signatures as in Apple's public macOS headers. */
#ifndef N2XPC_H
#define N2XPC_H
#include <stdint.h>
#include <stddef.h>
#include <dispatch/dispatch.h>
typedef void *xpc_object_t;
typedef void *xpc_connection_t;
typedef const struct _xpc_type_s *xpc_type_t;
typedef void (^xpc_handler_t)(xpc_object_t object);
extern const struct _xpc_type_s _xpc_type_connection, _xpc_type_dictionary, _xpc_type_error;
#define XPC_TYPE_CONNECTION (&_xpc_type_connection)
#define XPC_TYPE_DICTIONARY (&_xpc_type_dictionary)
#define XPC_TYPE_ERROR (&_xpc_type_error)
#define XPC_CONNECTION_MACH_SERVICE_LISTENER (1 << 0)
xpc_connection_t xpc_connection_create_mach_service(const char *name, dispatch_queue_t targetq, uint64_t flags);
void xpc_connection_set_event_handler(xpc_connection_t connection, xpc_handler_t handler);
void xpc_connection_resume(xpc_connection_t connection);
void xpc_connection_send_message(xpc_connection_t connection, xpc_object_t message);
xpc_type_t xpc_get_type(xpc_object_t object);
xpc_object_t xpc_dictionary_create_reply(xpc_object_t original);
const char *xpc_dictionary_get_string(xpc_object_t xdict, const char *key);
xpc_object_t xpc_dictionary_get_value(xpc_object_t xdict, const char *key);
void xpc_dictionary_set_int64(xpc_object_t xdict, const char *key, int64_t value);
void xpc_dictionary_set_string(xpc_object_t xdict, const char *key, const char *string);
void xpc_release(xpc_object_t object);
#endif
