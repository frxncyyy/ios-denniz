/*
 * Nord 2 private Metal compute frontend core.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#include "nord2_metal.h"

#include <string.h>

bool n2m_device_init(struct n2m_device *dev, const struct n2m_ops *ops,
                     void *ctx)
{
        if (!dev || !ops || !ops->scalar || !ops->method || !ops->map ||
            !ops->unmap)
                return false;
        memset(dev, 0, sizeof(*dev));
        dev->ops = *ops;
        dev->ctx = ctx;
        dev->ready = true;
        return true;
}

bool n2m_buffer_new(struct n2m_device *dev, uint64_t length, bool executable,
                    struct n2m_buffer *buffer)
{
        if (!dev || !dev->ready || !buffer || !length ||
            length > (16ull << 20) || dev->buffers_live >= N2M_MAX_BUFFERS)
                return false;
        memset(buffer, 0, sizeof(*buffer));
        uint64_t args[2] = {length, executable ? 1 : 0};
        uint64_t reply[3] = {0, 0, 0};
        unsigned count = 3;
        if (dev->ops.scalar(dev->ctx, N2M_SELECTOR_ALLOC, args, 2, reply,
                            &count) ||
            count < 3 || !reply[1])
                return false;
        uint64_t address = 0, mapped = 0;
        unsigned handle = (unsigned)reply[0];
        if (dev->ops.map(dev->ctx, handle, &address, &mapped) || !address ||
            mapped < length) {
                uint64_t one = handle;
                unsigned n = 0;
                (void)dev->ops.scalar(dev->ctx, N2M_SELECTOR_RELEASE, &one, 1,
                                      0, &n);
                return false;
        }
        buffer->device = dev;
        buffer->handle = handle;
        buffer->address = address;
        buffer->length = length;
        buffer->mapped = mapped;
        buffer->executable = executable;
        buffer->refs = 1;
        buffer->live = true;
        dev->allocated_bytes += length;
        dev->buffers_live++;
        return true;
}

bool n2m_buffer_free(struct n2m_device *dev, struct n2m_buffer *buffer)
{
        if (!dev || !buffer || !buffer->live || buffer->refs > 1)
                return false;
        if (buffer->address)
                (void)dev->ops.unmap(dev->ctx, buffer->handle,
                                     buffer->address);
        uint64_t handle = buffer->handle;
        unsigned n = 0;
        bool ok = dev->ops.scalar(dev->ctx, N2M_SELECTOR_RELEASE, &handle, 1,
                                  0, &n) == 0;
        if (dev->allocated_bytes >= buffer->length)
                dev->allocated_bytes -= buffer->length;
        if (dev->buffers_live)
                dev->buffers_live--;
        buffer->live = false;
        buffer->address = 0;
        return ok;
}

bool n2m_pipeline_new(struct n2m_device *dev, struct n2m_buffer *binary,
                      uint32_t offset, struct n2m_pipeline *pipeline)
{
        if (!dev || !binary || !binary->live || !binary->executable ||
            !pipeline || offset >= binary->length ||
            binary->length - offset < 4)
                return false;
        memset(pipeline, 0, sizeof(*pipeline));
        pipeline->device = dev;
        pipeline->binary = binary;
        pipeline->offset = offset;
        pipeline->live = true;
        binary->refs++;
        return true;
}

void n2m_pipeline_free(struct n2m_pipeline *pipeline)
{
        if (!pipeline || !pipeline->live)
                return;
        if (pipeline->binary && pipeline->binary->refs)
                pipeline->binary->refs--;
        pipeline->live = false;
        pipeline->binary = NULL;
}

bool n2m_command_begin(struct n2m_device *dev, struct n2m_command *command)
{
        if (!dev || !dev->ready || !command)
                return false;
        memset(command, 0, sizeof(*command));
        command->device = dev;
        return true;
}

bool n2m_command_encoder(struct n2m_command *command,
                         struct n2m_encoder *encoder)
{
        if (!command || !encoder || command->state != 0)
                return false;
        memset(encoder, 0, sizeof(*encoder));
        encoder->command = command;
        return true;
}

bool n2m_encoder_set_pipeline(struct n2m_encoder *encoder,
                              struct n2m_pipeline *pipeline)
{
        if (!encoder || !pipeline || !pipeline->live ||
            encoder->command->state != 0)
                return false;
        encoder->pipeline = pipeline;
        return true;
}

bool n2m_encoder_set_buffer(struct n2m_encoder *encoder, unsigned index,
                            struct n2m_buffer *buffer, uint32_t offset)
{
        if (!encoder || index >= N2M_MAX_BINDINGS || !buffer || !buffer->live ||
            offset >= buffer->length || encoder->command->state != 0)
                return false;
        encoder->buffers[index] = buffer;
        encoder->offsets[index] = offset;
        encoder->used[index] = true;
        return true;
}

bool n2m_encoder_dispatch(struct n2m_encoder *encoder, uint32_t group_x,
                          uint32_t group_y, uint32_t group_z,
                          uint32_t threads_x, uint32_t threads_y,
                          uint32_t threads_z)
{
        if (!encoder || !encoder->pipeline || !encoder->pipeline->live ||
            encoder->command->state != 0 || !group_x || !group_y || !group_z ||
            !threads_x || !threads_y || !threads_z || threads_x > 1024 ||
            threads_y > 1024 || threads_z > 1024)
                return false;
        uint64_t total = (uint64_t)threads_x * threads_y * threads_z;
        if (total > 1024)
                return false;
        struct n2m_compute_request *req = &encoder->command->req;
        memset(req, 0, sizeof(*req));
        req->version = 1;
        req->shader_handle = encoder->pipeline->binary->handle;
        req->shader_offset = encoder->pipeline->offset;
        if (encoder->used[0]) {
                req->resource_handle = encoder->buffers[0]->handle;
                req->resource_offset = encoder->offsets[0];
        }
        if (encoder->used[1]) {
                req->fau_handle = encoder->buffers[1]->handle;
                req->fau_offset = encoder->offsets[1];
        }
        req->workgroup_size[0] = threads_x;
        req->workgroup_size[1] = threads_y;
        req->workgroup_size[2] = threads_z;
        req->workgroup_count[0] = group_x;
        req->workgroup_count[1] = group_y;
        req->workgroup_count[2] = group_z;
        req->fau_count = encoder->used[1] ? 1 : 0;
        encoder->command->state = 1;
        return true;
}

bool n2m_encoder_end(struct n2m_encoder *encoder)
{
        (void)encoder;
        return encoder && encoder->command;
}

bool n2m_command_commit(struct n2m_command *command)
{
        if (!command || !command->device || command->state != 1)
                return false;
        uint64_t out[1] = {0};
        unsigned count = 1;
        if (command->device->ops.method(command->device->ctx,
                                        N2M_SELECTOR_SUBMIT, 0, 0,
                                        &command->req, sizeof(command->req),
                                        out, &count, 0, 0) ||
            !count) {
                command->state = 4;
                return false;
        }
        command->job = out[0];
        command->state = 2;
        return true;
}

bool n2m_command_wait(struct n2m_command *command, uint32_t timeout_ms)
{
        if (!command || !command->device || command->state != 2)
                return false;
        uint64_t args[2] = {command->job, timeout_ms};
        uint64_t out[1] = {0};
        unsigned count = 1;
        unsigned fault_size = sizeof(command->fault);
        memset(&command->fault, 0, sizeof(command->fault));
        if (command->device->ops.method(command->device->ctx,
                                        N2M_SELECTOR_WAIT, args, 2, 0, 0, out,
                                        &count, &command->fault,
                                        &fault_size) ||
            !count) {
                command->state = 4;
                return false;
        }
        /* Kernel job state 3 == DONE. */
        command->state = out[0] == 3 ? 3 : 4;
        return command->state == 3;
}

unsigned n2m_command_state(const struct n2m_command *command)
{
        return command ? command->state : 0;
}

const struct n2m_fault *n2m_command_fault(const struct n2m_command *command)
{
        return command ? &command->fault : NULL;
}
