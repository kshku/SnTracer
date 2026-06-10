#include "sntracer/sntracer.h"

#include <sncore/defines.h>

#define sn_tracer_lock_thread(tracer, thread_buffer)                                         \
    if ((tracer)->hooks.mutex_lock) (tracer)->hooks.mutex_lock((thread_buffer)->thread_lock)

#define sn_tracer_unlock_thread(tracer, thread_buffer)                                           \
    if ((tracer)->hooks.mutex_unlock) (tracer)->hooks.mutex_unlock((thread_buffer)->thread_lock)

#define sn_tracer_get_thread_buffer(tracer, write_thread_buffer)                                   \
    do {                                                                                           \
        if ((tracer)->hooks.read_lock) (tracer)->hooks.read_lock((tracer)->hooks.read_write_lock); \
        (write_thread_buffer) = (tracer)->thread_buffer;                                           \
        if ((tracer)->hooks.read_unlock)                                                           \
            (tracer)->hooks.read_unlock((tracer)->hooks.read_write_lock);                          \
    } while (0)

#define sn_tracer_set_thread_buffer(tracer, read_thread_buffer)            \
    do {                                                                   \
        if ((tracer)->hooks.write_lock)                                    \
            (tracer)->hooks.write_lock((tracer)->hooks.read_write_lock);   \
        (tracer)->thread_buffer = (read_thread_buffer);                    \
        if ((tracer)->hooks.write_unlock)                                  \
            (tracer)->hooks.write_unlock((tracer)->hooks.read_write_lock); \
    } while (0)

#define sn_tracer_get_thread_id(tracer) (tracer)->hooks.thread_id((tracer)->hooks.thread_data)
#define sn_tracer_get_time_now(tracer) (tracer)->hooks.time_now((tracer)->hooks.time_data)



#define EVENT_VALIDITY_MASK (1 << 15)
#define SET_EVENT_COMPLETED(header) (header)->type &= ~EVENT_VALIDITY_MASK;
#define SET_EVENT_INCOMPLETE(header) (header)->type |= EVENT_VALIDITY_MASK;
#define IS_EVENT_INCOMPLETE(header) ((header)->type & EVENT_VALIDITY_MASK)



SnTracerThreadBuffer *
    sn_tracer_add_thread(SnTracer *tracer, void *buffer, size_t buffer_size, void *thread_lock) {
    SnTracerThreadBuffer *thread_buffer = SN_GET_ALIGNED_PTR(buffer, SnTracerThreadBuffer);
    buffer_size -= (size_t)SN_PTR_DIFF(thread_buffer, buffer);
    size_t rb_size = buffer_size - sizeof(SnTracerThreadBuffer);
    sn_ring_buffer_allocator_init(&thread_buffer->ring_buffer, (char *)(thread_buffer + 1), (uint64_t)rb_size);
    thread_buffer->dropped = 0;
    thread_buffer->thread_lock = thread_lock;
    thread_buffer->thread_id = sn_tracer_get_thread_id(tracer);

    // thread_buffer->next = tracer->thread_buffer
    sn_tracer_get_thread_buffer(tracer, thread_buffer->next);

    // tracer->thread_buffer = thread_buffer;
    sn_tracer_set_thread_buffer(tracer, thread_buffer);

    return thread_buffer;
}

SnTracerEventRecord
    sn_tracer_event_begin(SnTracer *tracer, SnTracerThreadBuffer *thread_buffer, SnTracerEventType type) {
    if (!sn_tracer_is_enabled(tracer)) return (SnTracerEventRecord){0};

    sn_tracer_lock_thread(tracer, thread_buffer);

#define allocate_from_ring_buffer(type)                                      \
    (type *)sn_ring_buffer_allocator_allocate(&thread_buffer->ring_buffer, sizeof(type), alignof(type))
    SnTracerEventRecord record = {0};
    uint64_t saved_write = thread_buffer->ring_buffer.write_offset;
    record.header = allocate_from_ring_buffer(SnTracerEventHeader);
    if (!record.header) goto failed_header_allocation;

    *record.header = (SnTracerEventHeader){.timestamp = sn_tracer_get_time_now(tracer), .type = type};

    SET_EVENT_INCOMPLETE(record.header);

    switch (type) {
        case SN_TRACER_EVENT_TYPE_SCOPE_BEGIN:
            record.scope_begin = allocate_from_ring_buffer(SnTracerScopeBeginPayload);
            if (!record.scope_begin) goto failed_payload_allocation;
            break;
        case SN_TRACER_EVENT_TYPE_INSTANT:
            record.instant = allocate_from_ring_buffer(SnTracerInstantPayload);
            if (!record.instant) goto failed_payload_allocation;
            break;
        case SN_TRACER_EVENT_TYPE_COUNTER:
            record.counter = allocate_from_ring_buffer(SnTracerCounterPayload);
            if (!record.counter) goto failed_payload_allocation;
            break;
        case SN_TRACER_EVENT_TYPE_FLOW_BEGIN:
        case SN_TRACER_EVENT_TYPE_FLOW_STEP:
        case SN_TRACER_EVENT_TYPE_FLOW_END:
            record.flow = allocate_from_ring_buffer(SnTracerFlowPayload);
            if (!record.flow) goto failed_payload_allocation;
            break;
        case SN_TRACER_EVENT_TYPE_METADATA:
            record.metadata = allocate_from_ring_buffer(SnTracerMetadataPayload);
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
    thread_buffer->ring_buffer.write_offset = saved_write;
failed_header_allocation:
    sn_tracer_unlock_thread(tracer, thread_buffer);
    thread_buffer->dropped++;
    return (SnTracerEventRecord){0};
}

void sn_tracer_event_commit(SnTracer *tracer, SnTracerThreadBuffer *thread_buffer, SnTracerEventRecord record) {
    if (!sn_tracer_is_enabled(tracer)) return;

    sn_tracer_lock_thread(tracer, thread_buffer);

    SET_EVENT_COMPLETED(record.header);

    sn_tracer_unlock_thread(tracer, thread_buffer);
}

size_t sn_tracer_process_n(SnTracer *tracer, size_t n) {
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

size_t sn_tracer_process_thread_buffer_n(SnTracer *tracer, SnTracerThreadBuffer *thread_buffer, size_t n) {
    size_t count = 0;
    uint8_t *ring_buffer = thread_buffer->ring_buffer.buffer;
    uint64_t buffer_size = thread_buffer->ring_buffer.size;
    SnTracerEvent event = {.thread_id = thread_buffer->thread_id};

    sn_tracer_lock_thread(tracer, thread_buffer);

    while (thread_buffer->ring_buffer.read_offset != thread_buffer->ring_buffer.write_offset && count < n) {
        if (buffer_size - thread_buffer->ring_buffer.read_offset < sizeof(SnTracerEventHeader))
            thread_buffer->ring_buffer.read_offset = 0;

        void *ptr = ring_buffer + thread_buffer->ring_buffer.read_offset;
        SnTracerEventHeader *header = SN_GET_ALIGNED_PTR(ptr, SnTracerEventHeader);

        if (IS_EVENT_INCOMPLETE(header)) break;

        event.type = header->type;
        event.timestamp = header->timestamp;
        thread_buffer->ring_buffer.read_offset += (uint64_t)SN_PTR_DIFF(header + 1, ptr);
        if (thread_buffer->ring_buffer.read_offset >= buffer_size)
            thread_buffer->ring_buffer.read_offset = 0;

        sn_tracer_unlock_thread(tracer, thread_buffer);

        if (header->type != SN_TRACER_EVENT_TYPE_SCOPE_END) {
            // Just to avoid creating scope inside switch
            union {
                SnTracerScopeBeginPayload *scope_begin;
                SnTracerInstantPayload *instant;
                SnTracerCounterPayload *counter;
                SnTracerFlowPayload *flow;
                SnTracerMetadataPayload *metadata;
            } payload_ptr;

            void *end_ptr = NULL;

            sn_tracer_lock_thread(tracer, thread_buffer);
            ptr = ring_buffer + thread_buffer->ring_buffer.read_offset;

            switch (header->type) {
                case SN_TRACER_EVENT_TYPE_SCOPE_BEGIN:
                    if (buffer_size - thread_buffer->ring_buffer.read_offset < sizeof(SnTracerScopeBeginPayload)) {
                        thread_buffer->ring_buffer.read_offset = 0;
                        ptr = ring_buffer + thread_buffer->ring_buffer.read_offset;
                    }
                    payload_ptr.scope_begin = SN_GET_ALIGNED_PTR(ptr, SnTracerScopeBeginPayload);
                    event.scope_begin = *payload_ptr.scope_begin;
                    end_ptr = (void *)(payload_ptr.scope_begin + 1);
                    break;
                case SN_TRACER_EVENT_TYPE_INSTANT:
                    if (buffer_size - thread_buffer->ring_buffer.read_offset < sizeof(SnTracerInstantPayload)) {
                        thread_buffer->ring_buffer.read_offset = 0;
                        ptr = ring_buffer + thread_buffer->ring_buffer.read_offset;
                    }
                    payload_ptr.instant = SN_GET_ALIGNED_PTR(ptr, SnTracerInstantPayload);
                    event.instant = *payload_ptr.instant;
                    end_ptr = (void *)(payload_ptr.instant + 1);
                    break;
                case SN_TRACER_EVENT_TYPE_COUNTER:
                    if (buffer_size - thread_buffer->ring_buffer.read_offset < sizeof(SnTracerCounterPayload)) {
                        thread_buffer->ring_buffer.read_offset = 0;
                        ptr = ring_buffer + thread_buffer->ring_buffer.read_offset;
                    }
                    payload_ptr.counter = SN_GET_ALIGNED_PTR(ptr, SnTracerCounterPayload);
                    event.counter = *payload_ptr.counter;
                    end_ptr = (void *)(payload_ptr.counter + 1);
                    break;
                case SN_TRACER_EVENT_TYPE_FLOW_BEGIN:
                case SN_TRACER_EVENT_TYPE_FLOW_STEP:
                case SN_TRACER_EVENT_TYPE_FLOW_END:
                    if (buffer_size - thread_buffer->ring_buffer.read_offset < sizeof(SnTracerFlowPayload)) {
                        thread_buffer->ring_buffer.read_offset = 0;
                        ptr = ring_buffer + thread_buffer->ring_buffer.read_offset;
                    }
                    payload_ptr.flow = SN_GET_ALIGNED_PTR(ptr, SnTracerFlowPayload);
                    event.flow = *payload_ptr.flow;
                    end_ptr = (void *)(payload_ptr.flow + 1);
                    break;
                case SN_TRACER_EVENT_TYPE_METADATA:
                    if (buffer_size - thread_buffer->ring_buffer.read_offset < sizeof(SnTracerMetadataPayload)) {
                        thread_buffer->ring_buffer.read_offset = 0;
                        ptr = ring_buffer + thread_buffer->ring_buffer.read_offset;
                    }
                    payload_ptr.metadata = SN_GET_ALIGNED_PTR(ptr, SnTracerMetadataPayload);
                    event.metadata = *payload_ptr.metadata;
                    end_ptr = (void *)(payload_ptr.metadata + 1);
                    break;

                case SN_TRACER_EVENT_TYPE_SCOPE_END:
                default:
                    // Will not reach here
                    break;
            }

            thread_buffer->ring_buffer.read_offset += (uint64_t)SN_PTR_DIFF(end_ptr, ptr);
            if (thread_buffer->ring_buffer.read_offset >= buffer_size)
                thread_buffer->ring_buffer.read_offset = 0;
            sn_tracer_unlock_thread(tracer, thread_buffer);
        }

        count++;
        if (tracer->hooks.consumer) tracer->hooks.consumer(event, tracer->hooks.consumer_data);

        sn_tracer_lock_thread(tracer, thread_buffer);
    }

    sn_tracer_unlock_thread(tracer, thread_buffer);

    return count;
}

void sn_tracer_trace_scope_begin(SnTracer *tracer, SnTracerThreadBuffer *thread_buffer,
                                 const char *name, const char *func, const char *file, uint32_t line) {
    SnTracerEventRecord record = sn_tracer_event_begin(tracer, thread_buffer, SN_TRACER_EVENT_TYPE_SCOPE_BEGIN);
    if (!record.header || !record.scope_begin) return;

    *record.scope_begin = (SnTracerScopeBeginPayload){
        .name = name,
        .func = func,
        .file = file,
        .line = line,
    };

    sn_tracer_event_commit(tracer, thread_buffer, record);
}

void sn_tracer_trace_scope_end(SnTracer *tracer, SnTracerThreadBuffer *thread_buffer) {
    SnTracerEventRecord record = sn_tracer_event_begin(tracer, thread_buffer, SN_TRACER_EVENT_TYPE_SCOPE_END);

    if (!record.header) return;

    sn_tracer_event_commit(tracer, thread_buffer, record);
}

void sn_tracer_trace_instant(SnTracer *tracer, SnTracerThreadBuffer *thread_buffer,
                             const char *name, const char *func, const char *file, uint32_t line) {
    SnTracerEventRecord record = sn_tracer_event_begin(tracer, thread_buffer, SN_TRACER_EVENT_TYPE_INSTANT);

    if (!record.header || !record.instant) return;

    *record.instant = (SnTracerInstantPayload){.name = name, .func = func, .file = file, .line = line};

    sn_tracer_event_commit(tracer, thread_buffer, record);
}

void sn_tracer_trace_counter(SnTracer *tracer, SnTracerThreadBuffer *thread_buffer, const char *name, int64_t value) {
    SnTracerEventRecord record = sn_tracer_event_begin(tracer, thread_buffer, SN_TRACER_EVENT_TYPE_COUNTER);

    if (!record.header || !record.counter) return;

    *record.counter = (SnTracerCounterPayload){.name = name, .value = value};

    sn_tracer_event_commit(tracer, thread_buffer, record);
}

void sn_tracer_trace_flow_begin(
    SnTracer *tracer, SnTracerThreadBuffer *thread_buffer, const char *name, uint64_t id) {
    SnTracerEventRecord record = sn_tracer_event_begin(tracer, thread_buffer, SN_TRACER_EVENT_TYPE_FLOW_BEGIN);

    if (!record.header || !record.flow) return;

    *record.flow = (SnTracerFlowPayload){.id = id, .name = name};

    sn_tracer_event_commit(tracer, thread_buffer, record);
}

void sn_tracer_trace_flow_step(
    SnTracer *tracer, SnTracerThreadBuffer *thread_buffer, const char *name, uint64_t id) {
    SnTracerEventRecord record = sn_tracer_event_begin(tracer, thread_buffer, SN_TRACER_EVENT_TYPE_FLOW_STEP);

    if (!record.header || !record.flow) return;

    *record.flow = (SnTracerFlowPayload){.id = id, .name = name};

    sn_tracer_event_commit(tracer, thread_buffer, record);
}

void sn_tracer_trace_flow_end(SnTracer *tracer, SnTracerThreadBuffer *thread_buffer, const char *name, uint64_t id) {
    SnTracerEventRecord record = sn_tracer_event_begin(tracer, thread_buffer, SN_TRACER_EVENT_TYPE_FLOW_END);

    if (!record.header || !record.flow) return;

    *record.flow = (SnTracerFlowPayload){.id = id, .name = name};

    sn_tracer_event_commit(tracer, thread_buffer, record);
}

void sn_tracer_trace_metadata(
    SnTracer *tracer, SnTracerThreadBuffer *thread_buffer, const char *name, const char *value) {
    SnTracerEventRecord record = sn_tracer_event_begin(tracer, thread_buffer, SN_TRACER_EVENT_TYPE_METADATA);

    if (!record.header || !record.metadata) return;

    *record.metadata = (SnTracerMetadataPayload){.name = name, .value = value};

    sn_tracer_event_commit(tracer, thread_buffer, record);
}

