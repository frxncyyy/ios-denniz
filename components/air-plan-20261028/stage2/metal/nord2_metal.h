/*
 * Nord 2 private Metal compute frontend core (transport-independent).
 *
 * The Objective-C shim only adapts selectors; this C core owns the state
 * machine, bounds checks, buffer/pipeline ownership and the request built for
 * the kernel user client.  It can be tested on the host with a fake transport,
 * which is why the frontend is not a scaffold.  It is compute-only and does
 * not implement render/textures/IOSurfaces.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#ifndef NORD2_METAL_H
#define NORD2_METAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

#define N2M_MAX_BINDINGS 8u
#define N2M_MAX_BUFFERS 32u
#define N2M_MAX_BINARY 4096u
#define N2M_SELECTOR_STATUS 0u
#define N2M_SELECTOR_ALLOC 1u
#define N2M_SELECTOR_RELEASE 2u
#define N2M_SELECTOR_SUBMIT 3u
#define N2M_SELECTOR_WAIT 4u

struct n2m_compute_request {
        uint32_t version, flags;
        uint64_t shader_handle, resource_handle, tls_handle, fau_handle;
        uint32_t shader_offset, resource_offset, tls_offset, fau_offset;
        uint32_t workgroup_size[3], workgroup_count[3];
        uint32_t fau_count, attribute_offset, allow_merging, reserved;
};

static_assert(sizeof(struct n2m_compute_request) == 96,
              "compute request ABI");
struct n2m_fault {
        uint32_t job_id_lo, job_id_hi, state, status;
        uint32_t fault_status, first_incomplete_task;
        uint64_t fault_address;
};

static_assert(sizeof(struct n2m_fault) == 32, "fault ABI");

struct n2m_ops {
        unsigned (*scalar)(void *ctx, unsigned selector, const uint64_t *args,
                           unsigned argc, uint64_t *out, unsigned *out_count);
        int (*method)(void *ctx, unsigned selector, const uint64_t *args,
                      unsigned argc, const void *input, size_t input_size,
                      uint64_t *out, unsigned *out_count, void *out_struct,
                      unsigned *out_struct_size);
        int (*map)(void *ctx, unsigned handle, uint64_t *address,
                   uint64_t *size);
        int (*unmap)(void *ctx, unsigned handle, uint64_t address);
};

struct n2m_device {
        struct n2m_ops ops;
        void *ctx;
        uint64_t allocated_bytes;
        unsigned buffers_live;
        bool ready;
};

struct n2m_buffer {
        struct n2m_device *device;
        unsigned handle;
        uint64_t address;
        uint64_t length;
        uint64_t mapped;
        unsigned refs;
        bool executable;
        bool live;
};

struct n2m_pipeline {
        struct n2m_device *device;
        struct n2m_buffer *binary;
        uint32_t offset;
        bool live;
};

struct n2m_command {
        struct n2m_device *device;
        struct n2m_compute_request req;
        uint64_t job;
        unsigned state; /* 0 empty, 1 encoded, 2 submitted, 3 done, 4 failed */
        struct n2m_fault fault;
};

struct n2m_encoder {
        struct n2m_command *command;
        struct n2m_pipeline *pipeline;
        struct n2m_buffer *buffers[N2M_MAX_BINDINGS];
        uint32_t offsets[N2M_MAX_BINDINGS];
        bool used[N2M_MAX_BINDINGS];
};

bool n2m_device_init(struct n2m_device *dev, const struct n2m_ops *ops,
                     void *ctx);
bool n2m_buffer_new(struct n2m_device *dev, uint64_t length, bool executable,
                    struct n2m_buffer *buffer);
bool n2m_buffer_free(struct n2m_device *dev, struct n2m_buffer *buffer);
bool n2m_pipeline_new(struct n2m_device *dev, struct n2m_buffer *binary,
                      uint32_t offset, struct n2m_pipeline *pipeline);
void n2m_pipeline_free(struct n2m_pipeline *pipeline);
bool n2m_command_begin(struct n2m_device *dev, struct n2m_command *command);
bool n2m_command_encoder(struct n2m_command *command,
                         struct n2m_encoder *encoder);
bool n2m_encoder_set_pipeline(struct n2m_encoder *encoder,
                              struct n2m_pipeline *pipeline);
bool n2m_encoder_set_buffer(struct n2m_encoder *encoder, unsigned index,
                            struct n2m_buffer *buffer, uint32_t offset);
bool n2m_encoder_dispatch(struct n2m_encoder *encoder, uint32_t group_x,
                          uint32_t group_y, uint32_t group_z,
                          uint32_t threads_x, uint32_t threads_y,
                          uint32_t threads_z);
bool n2m_encoder_end(struct n2m_encoder *encoder);
bool n2m_command_commit(struct n2m_command *command);
bool n2m_command_wait(struct n2m_command *command, uint32_t timeout_ms);
unsigned n2m_command_state(const struct n2m_command *command);
const struct n2m_fault *n2m_command_fault(const struct n2m_command *command);

#ifdef __cplusplus
}
#endif

#endif /* NORD2_METAL_H */
