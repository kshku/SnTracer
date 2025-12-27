#include "sntracer/sntracer.h"

#define sn_tracer_lock_thread(tracer, thread_buffer) \
    if ((tracer)->hooks.mutex_lock) \
        (tracer)->hooks.mutex_lock((thread_buffer)->thread_lock)

#define sn_tracer_unlock_thread(tracer, thread_buffer) \
    if ((tracer)->hooks.mutex_unlock) \
        (tracer)->hooks.mutex_unlock((thread_buffer)->thread_lock)

#define sn_tracer_get_thread_buffer(tracer, write_thread_buffer) do { \
        if ((tracer)->hooks.read_lock) \
            (tracer)->hooks.read_lock((tracer)->hooks.read_write_lock); \
        (write_thread_buffer) = (tracer)->thread_buffer; \
        if ((tracer)->hooks.read_unlock) \
            (tracer)->hooks.read_unlock((tracer)->hooks.read_write_lock); \
    } while (0)

#define sn_tracer_set_thread_buffer(tracer, read_thread_buffer) do { \
        if ((tracer)->hooks.write_lock) \
            (tracer)->hooks.write_lock((tracer)->hooks.read_write_lock); \
        (tracer)->thread_buffer = (read_thread_buffer); \
        if ((tracer)->hooks.write_unlock) \
            (tracer)->hooks.write_unlock((tracer)->hooks.read_write_lock); \
    } while (0)

#define sn_tracer_get_thread_id(tracer) (tracer)->hooks.thread_id((tracer)->hooks.thread_data)
#define sn_tracer_get_time_now(tracer) (tracer)->hooks.time_now((tracer)->hooks.time_data)

#define GET_ALIGNED(x, align) ((((size_t)x) + (align) - 1) & ~((align) - 1))
#define PTR_BYTE_DIFF(x, y) (((size_t)(x)) - ((size_t)(y)))

#define GET_ALIGNED_PTR(x, type) ((type *)GET_ALIGNED((x), alignof(type)))

#define EVENT_VALIDITY_MASK (1 << 15)
#define SET_EVENT_COMPLETED(header) (header)->type &= ~EVENT_VALIDITY_MASK;
#define SET_EVENT_INCOMPLETE(header) (header)->type |= EVENT_VALIDITY_MASK;
#define IS_EVENT_INCOMPLETE(header) ((header)->type & EVENT_VALIDITY_MASK)

static size_t ring_buffer_free_size(snTracerThreadBuffer *thread_buffer) {
    if (thread_buffer->write_offset >= thread_buffer->read_offset)
        return thread_buffer->buffer_size - (thread_buffer->write_offset - thread_buffer->read_offset);

    return thread_buffer->read_offset - thread_buffer->write_offset;
}

static void *ring_buffer_allocate(snTracerThreadBuffer *thread_buffer, size_t size, size_t align) {
    char *ring_buffer = (char *)(thread_buffer + 1);
    size += align;

    size_t free = ring_buffer_free_size(thread_buffer);

    if (free < size) return NULL;

    if ((thread_buffer->write_offset >= thread_buffer->read_offset && thread_buffer->write_offset + size <= thread_buffer->buffer_size) ||
            (thread_buffer->write_offset < thread_buffer->read_offset && thread_buffer->write_offset + size < thread_buffer->read_offset)) {
        void *p = (ring_buffer + thread_buffer->write_offset);
        void *aligned = (void *)GET_ALIGNED(p, align);
        thread_buffer->write_offset += size - align + PTR_BYTE_DIFF(aligned, p);
        return aligned;
    }

    if (thread_buffer->write_offset >= thread_buffer->read_offset && thread_buffer->read_offset > size) {
        void *aligned = (void *)GET_ALIGNED(ring_buffer, align);
        thread_buffer->write_offset = size - align + PTR_BYTE_DIFF(aligned, ring_buffer);
        return aligned;
    }

    return NULL;
}

snTracerThreadBuffer *sn_tracer_add_thread(snTracer *tracer, void *buffer, size_t buffer_size, void *thread_lock) {
    snTracerThreadBuffer *thread_buffer = GET_ALIGNED_PTR(buffer, snTracerThreadBuffer);
    buffer_size -= PTR_BYTE_DIFF(thread_buffer, buffer);
    *thread_buffer = (snTracerThreadBuffer){
        .buffer_size = buffer_size - sizeof(snTracerThreadBuffer),
        .write_offset = 0,
        .read_offset = 0,
        .dropped = 0,
        .thread_lock = thread_lock,
        .thread_id = sn_tracer_get_thread_id(tracer)
    };

    // thread_buffer->next = tracer->thread_buffer
    sn_tracer_get_thread_buffer(tracer, thread_buffer->next);

    // tracer->thread_buffer = thread_buffer;
    sn_tracer_set_thread_buffer(tracer, thread_buffer);

    return thread_buffer;
}

snTracerEventRecord sn_tracer_event_begin(snTracer *tracer, snTracerThreadBuffer *thread_buffer, snTracerEventType type) {
    if (!sn_tracer_is_enabled(tracer)) return (snTracerEventRecord){0};

    sn_tracer_lock_thread(tracer, thread_buffer);

#define allocate_from_ring_buffer(type) (type *)ring_buffer_allocate(thread_buffer, sizeof(type), alignof(type))
    snTracerEventRecord record = {0};
    record.header = allocate_from_ring_buffer(snTracerEventHeader);
    if (!record.header) goto failed_header_allocation;

    *record.header = (snTracerEventHeader){
        .timestamp = sn_tracer_get_time_now(tracer),
        .type = type
    };

    SET_EVENT_INCOMPLETE(record.header);

    switch (type) {
        case SN_TRACER_EVENT_TYPE_SCOPE_BEGIN:
            record.scope_begin = allocate_from_ring_buffer(snTracerScopeBeginPayload);
            if (!record.scope_begin) goto failed_payload_allocation;
            break;
        case SN_TRACER_EVENT_TYPE_INSTANT:
            record.instant = allocate_from_ring_buffer(snTracerInstantPayload);
            if (!record.instant) goto failed_payload_allocation;
            break;
        case SN_TRACER_EVENT_TYPE_COUNTER:
            record.counter = allocate_from_ring_buffer(snTracerCounterPayload);
            if (!record.counter) goto failed_payload_allocation;
            break;
        case SN_TRACER_EVENT_TYPE_FLOW_BEGIN:
        case SN_TRACER_EVENT_TYPE_FLOW_STEP:
        case SN_TRACER_EVENT_TYPE_FLOW_END:
            record.flow = allocate_from_ring_buffer(snTracerFlowPayload);
            if (!record.flow) goto failed_payload_allocation;
            break;
        case SN_TRACER_EVENT_TYPE_METADATA:
            record.metadata = allocate_from_ring_buffer(snTracerMetadataPayload);
            if (!record.flow) goto failed_payload_allocation;
            break;
        case SN_TRACER_EVENT_TYPE_SCOPE_END:
        default:
            break;
    }
#undef allocate_from_ring_buffer
    sn_tracer_unlock_thread(tracer, thread_buffer);
    return record;

failed_payload_allocation:
    thread_buffer->write_offset -= sizeof(snTracerEventHeader);
failed_header_allocation:
    sn_tracer_unlock_thread(tracer, thread_buffer);
    thread_buffer->dropped++;
    return (snTracerEventRecord){0};
}

void sn_tracer_event_commit(snTracer *tracer, snTracerThreadBuffer *thread_buffer, snTracerEventRecord record) {
    if (!sn_tracer_is_enabled(tracer)) return;

    sn_tracer_lock_thread(tracer, thread_buffer);

    SET_EVENT_COMPLETED(record.header);

    sn_tracer_unlock_thread(tracer, thread_buffer);
}

size_t sn_tracer_process_n(snTracer *tracer, size_t n) {
    size_t count = 0;

process_one_event_in_all_buffers:
    if (tracer->process_buffer == NULL)
        // tracer->process_buffer = tracer->thread_buffer;
        sn_tracer_get_thread_buffer(tracer, tracer->process_buffer);

    size_t iter_count = 0;

    while (tracer->process_buffer && count + iter_count < n) {
        iter_count += sn_tracer_process_thread_buffer_n(tracer, tracer->process_buffer, 1);
        tracer->process_buffer = tracer->process_buffer->next;
    }

    count += iter_count;
    if (iter_count != 0 && count < n) goto process_one_event_in_all_buffers;

    return count;
}

size_t sn_tracer_process_thread_buffer_n(snTracer *tracer, snTracerThreadBuffer *thread_buffer, size_t n) {
    size_t count = 0;
    char *ring_buffer = ((char *)thread_buffer) + sizeof(snTracerThreadBuffer);
    snTracerEvent event = {.thread_id = thread_buffer->thread_id};

    sn_tracer_lock_thread(tracer, thread_buffer);

    while (thread_buffer->read_offset != thread_buffer->write_offset && count < n) {
        if (thread_buffer->buffer_size - thread_buffer->read_offset < sizeof(snTracerEventHeader))
            thread_buffer->read_offset = 0;

        void *ptr = ring_buffer + thread_buffer->read_offset;
        snTracerEventHeader *header = GET_ALIGNED_PTR(ptr, snTracerEventHeader);

        if (IS_EVENT_INCOMPLETE(header)) break;

        event.type = header->type;
        event.timestamp = header->timestamp;
        thread_buffer->read_offset += PTR_BYTE_DIFF(header + 1, ptr);
        if (thread_buffer->read_offset >= thread_buffer->buffer_size)
            thread_buffer->read_offset = 0;

        sn_tracer_unlock_thread(tracer, thread_buffer);

        if (header->type != SN_TRACER_EVENT_TYPE_SCOPE_END) {
            // Just to avoid creating scope inside switch 
            union {
                snTracerScopeBeginPayload *scope_begin;
                snTracerInstantPayload *instant;
                snTracerCounterPayload *counter;
                snTracerFlowPayload *flow;
                snTracerMetadataPayload *metadata;
            } payload_ptr;
            void *end_ptr;

            sn_tracer_lock_thread(tracer, thread_buffer);
            ptr = ring_buffer + thread_buffer->read_offset;

            switch (header->type) {
                case SN_TRACER_EVENT_TYPE_SCOPE_BEGIN:
                    if (thread_buffer->buffer_size - thread_buffer->read_offset < sizeof(snTracerScopeBeginPayload)) {
                        thread_buffer->read_offset = 0;
                        ptr = ring_buffer + thread_buffer->read_offset;
                    }
                    payload_ptr.scope_begin = GET_ALIGNED_PTR(ptr, snTracerScopeBeginPayload);
                    event.scope_begin = *payload_ptr.scope_begin;
                    end_ptr = (void *)(payload_ptr.scope_begin + 1);
                    break;
                case SN_TRACER_EVENT_TYPE_INSTANT:
                    if (thread_buffer->buffer_size - thread_buffer->read_offset < sizeof(snTracerInstantPayload)) {
                        thread_buffer->read_offset = 0;
                        ptr = ring_buffer + thread_buffer->read_offset;
                    }
                    payload_ptr.instant = GET_ALIGNED_PTR(ptr, snTracerInstantPayload);
                    event.instant = *payload_ptr.instant;
                    end_ptr = (void *)(payload_ptr.instant + 1);
                    break;
                case SN_TRACER_EVENT_TYPE_COUNTER:
                    if (thread_buffer->buffer_size - thread_buffer->read_offset < sizeof(snTracerCounterPayload)) {
                        thread_buffer->read_offset = 0;
                        ptr = ring_buffer + thread_buffer->read_offset;
                    }
                    payload_ptr.counter = GET_ALIGNED_PTR(ptr, snTracerCounterPayload);
                    event.counter = *payload_ptr.counter;
                    end_ptr = (void *)(payload_ptr.counter + 1);
                    break;
                case SN_TRACER_EVENT_TYPE_FLOW_BEGIN:
                case SN_TRACER_EVENT_TYPE_FLOW_STEP:
                case SN_TRACER_EVENT_TYPE_FLOW_END:
                    if (thread_buffer->buffer_size - thread_buffer->read_offset < sizeof(snTracerFlowPayload)) {
                        thread_buffer->read_offset = 0;
                        ptr = ring_buffer + thread_buffer->read_offset;
                    }
                    payload_ptr.flow = GET_ALIGNED_PTR(ptr, snTracerFlowPayload);
                    event.flow = *payload_ptr.flow;
                    end_ptr = (void *)(payload_ptr.flow + 1);
                    break;
                case SN_TRACER_EVENT_TYPE_METADATA:
                    if (thread_buffer->buffer_size - thread_buffer->read_offset < sizeof(snTracerMetadataPayload)) {
                        thread_buffer->read_offset = 0;
                        ptr = ring_buffer + thread_buffer->read_offset;
                    }
                    payload_ptr.metadata = GET_ALIGNED_PTR(ptr, snTracerMetadataPayload);
                    event.metadata = *payload_ptr.metadata;
                    end_ptr = (void *)(payload_ptr.metadata + 1);
                    break;

                case SN_TRACER_EVENT_TYPE_SCOPE_END:
                default:
                    // Will not reach here
                    break;
            }

            thread_buffer->read_offset += PTR_BYTE_DIFF(end_ptr, ptr);
            if (thread_buffer->read_offset >= thread_buffer->buffer_size)
                thread_buffer->read_offset = 0;
            sn_tracer_unlock_thread(tracer, thread_buffer);
        }

        count++;
        if (tracer->hooks.consumer)
            tracer->hooks.consumer(event, tracer->hooks.consumer_data);

        sn_tracer_lock_thread(tracer, thread_buffer);
    }

    sn_tracer_unlock_thread(tracer, thread_buffer);

    return count;
}

void sn_tracer_trace_scope_begin(snTracer *tracer, snTracerThreadBuffer *thread_buffer,
        const char *name, const char *func, const char *file, uint32_t line) {
    snTracerEventRecord record = sn_tracer_event_begin(tracer, thread_buffer, SN_TRACER_EVENT_TYPE_SCOPE_BEGIN);
    if (!record.header || !record.scope_begin) return;

    *record.scope_begin = (snTracerScopeBeginPayload){
        .name = name,
        .func = func,
        .file = file,
        .line = line,
    };

    sn_tracer_event_commit(tracer, thread_buffer, record);
}

void sn_tracer_trace_scope_end(snTracer *tracer, snTracerThreadBuffer *thread_buffer) {
    snTracerEventRecord record = sn_tracer_event_begin(tracer, thread_buffer, SN_TRACER_EVENT_TYPE_SCOPE_END);

    if (!record.header) return;

    sn_tracer_event_commit(tracer, thread_buffer, record);
}

void sn_tracer_trace_instant(snTracer *tracer, snTracerThreadBuffer *thread_buffer,
        const char *name, const char *func, const char *file, uint32_t line) {
    snTracerEventRecord record = sn_tracer_event_begin(tracer, thread_buffer, SN_TRACER_EVENT_TYPE_INSTANT);

    if (!record.header || !record.instant) return;

    *record.instant = (snTracerInstantPayload) {
        .name = name,
        .func = func,
        .file = file,
        .line = line
    };

    sn_tracer_event_commit(tracer, thread_buffer, record);
}

void sn_tracer_trace_counter(snTracer *tracer, snTracerThreadBuffer *thread_buffer,
        const char *name, int64_t value) {
    snTracerEventRecord record = sn_tracer_event_begin(tracer, thread_buffer, SN_TRACER_EVENT_TYPE_COUNTER);

    if (!record.header || !record.counter) return;

    *record.counter = (snTracerCounterPayload) {
        .name = name,
        .value = value
    };

    sn_tracer_event_commit(tracer, thread_buffer, record);
}

void sn_tracer_trace_flow_begin(snTracer *tracer, snTracerThreadBuffer *thread_buffer, const char *name, uint64_t id) {
    snTracerEventRecord record = sn_tracer_event_begin(tracer, thread_buffer, SN_TRACER_EVENT_TYPE_FLOW_BEGIN);

    if (!record.header || !record.flow) return;

    *record.flow = (snTracerFlowPayload) {
        .id = id,
        .name = name
    };

    sn_tracer_event_commit(tracer, thread_buffer, record);
}

void sn_tracer_trace_flow_step(snTracer *tracer, snTracerThreadBuffer *thread_buffer, const char *name, uint64_t id) {
    snTracerEventRecord record = sn_tracer_event_begin(tracer, thread_buffer, SN_TRACER_EVENT_TYPE_FLOW_STEP);

    if (!record.header || !record.flow) return;

    *record.flow = (snTracerFlowPayload) { 
        .id = id,
        .name = name
    };

    sn_tracer_event_commit(tracer, thread_buffer, record);
}

void sn_tracer_trace_flow_end(snTracer *tracer, snTracerThreadBuffer *thread_buffer, const char *name, uint64_t id) {
    snTracerEventRecord record = sn_tracer_event_begin(tracer, thread_buffer, SN_TRACER_EVENT_TYPE_FLOW_END);

    if (!record.header || !record.flow) return;

    *record.flow = (snTracerFlowPayload) {
        .id = id,
        .name = name
    };

    sn_tracer_event_commit(tracer, thread_buffer, record);
}

void sn_tracer_trace_metadata(snTracer *tracer, snTracerThreadBuffer *thread_buffer, const char *name, const char *value) {
    snTracerEventRecord record = sn_tracer_event_begin(tracer, thread_buffer, SN_TRACER_EVENT_TYPE_METADATA);

    if (!record.header || !record.metadata) return;

    *record.metadata = (snTracerMetadataPayload) {
        .name = name,
        .value = value
    };

    sn_tracer_event_commit(tracer, thread_buffer, record);
}

