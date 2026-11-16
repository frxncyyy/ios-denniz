#ifndef TEGU_COMPOSITOR_SYSTEM_DEVICE_IDENTITY_H
#define TEGU_COMPOSITOR_SYSTEM_DEVICE_IDENTITY_H

#include <stdint.h>

struct tegu_system_device_identity_ops {
    void *expected_device_class;
    void *expected_metal_superclass;
    void *(*object_class)(void *);
    void *(*class_superclass)(void *);
    uint64_t (*registry_id)(void *);
    void *(*create_default_device)(void);
    void (*release_object)(void *);
};

struct tegu_system_device_identity {
    int class_match;
    int superclass_match;
    int registry_id_nonzero;
    int repeated_discovery_same_object;
};

static int tegu_verify_system_device_identity(
    void *device, const struct tegu_system_device_identity_ops *ops,
    struct tegu_system_device_identity *result) {
    if (!result) return -1;
    *result = (struct tegu_system_device_identity){0};
    if (!device || !ops || !ops->expected_device_class ||
        !ops->expected_metal_superclass || !ops->object_class ||
        !ops->class_superclass || !ops->registry_id ||
        !ops->create_default_device || !ops->release_object) return -1;

    void *actual_class = ops->object_class(device);
    result->class_match = actual_class == ops->expected_device_class;
    result->superclass_match = result->class_match &&
        ops->class_superclass(actual_class) == ops->expected_metal_superclass;
    result->registry_id_nonzero = ops->registry_id(device) != 0;

    void *second = ops->create_default_device();
    result->repeated_discovery_same_object = second == device;
    if (second) ops->release_object(second);

    return result->class_match && result->superclass_match &&
        result->registry_id_nonzero && result->repeated_discovery_same_object ? 0 : -1;
}

#endif
