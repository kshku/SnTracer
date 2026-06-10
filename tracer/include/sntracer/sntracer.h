#pragma once

#include <sncore/defines.h>
#include "sntracer/api.h"
#include <snmemory/ring_buffer.h>

/**
 * @enum SnTracerEventType
 * @brief Type of a tracing event.
 *
 * Some event types carry payload data, others do not.
 * The tracer internally reserves high bits of this value
 * for event validity tracking.
 */
typedef enum SnTracerEventType {
    SN_TRACER_EVENT_TYPE_SCOPE_BEGIN, /**< Marks the beginning of a scoped region */
    SN_TRACER_EVENT_TYPE_SCOPE_END, /**< Marks the end of a scoped region */
    SN_TRACER_EVENT_TYPE_INSTANT, /**< A single point in time event */
    SN_TRACER_EVENT_TYPE_COUNTER, /**< A numeric counter sample */
    SN_TRACER_EVENT_TYPE_FLOW_BEGIN, /**< Flow event: begin */
    SN_TRACER_EVENT_TYPE_FLOW_STEP, /**< Flow event: step */
    SN_TRACER_EVENT_TYPE_FLOW_END, /**< Flow event: end */
    SN_TRACER_EVENT_TYPE_METADATA, /**< Metadata event */
} SnTracerEventType;

/**
 * @struct SnTracerEventHeader
 * @brief Common header stored for every event.
 *
 * The tracer may temporarily mark an event as incomplete
 * using internal bits of the type field.
 */
typedef struct SnTracerEventHeader {
    uint64_t timestamp;
    SnTracerEventType type;
} SnTracerEventHeader;

/**
 * @struct SnTracerScopeBeginPayload
 * @brief Payload for scope-begin events.
 *
 * All string pointers must remain valid for the lifetime
 * of the tracing session (string literals recommended).
 */
typedef struct SnTracerScopeBeginPayload {
    const char *name;
    const char *func;
    const char *file;
    uint32_t line;
} SnTracerScopeBeginPayload;

/**
 * @struct SnTracerInstantPayload
 * @brief Payload for instant events.
 */
typedef struct SnTracerInstantPayload {
    const char *name;
    const char *func;
    const char *file;
    uint32_t line;
} SnTracerInstantPayload;

/**
 * @struct SnTracerCounterPayload
 * @brief Payload for counter events.
 */
typedef struct SnTracerCounterPayload {
    const char *name;
    int64_t value;
} SnTracerCounterPayload;

/**
 * @struct SnTracerFlowPayload
 * @brief Payload for flow events.
 */
typedef struct SnTracerFlowPayload {
    const char *name;
    uint64_t id;
} SnTracerFlowPayload;

/**
 * @struct SnTracerMetadataPayload.
 * @brief Payload for metadata events.
 */
typedef struct SnTracerMetadataPayload {
    const char *name;
    const char *value;
} SnTracerMetadataPayload;

/**
 * @struct SnTracerEvent
 * @brief Fully reconstructed event delivered to consumers.
 *
 * This structure is never written directly into buffers.
 * It is assembled during processing and passed to the consumer callback.
 */
typedef struct SnTracerEvent {
    uint64_t timestamp;

    union {
        SnTracerScopeBeginPayload scope_begin;
        SnTracerInstantPayload instant;
        SnTracerCounterPayload counter;
        SnTracerFlowPayload flow;
        SnTracerMetadataPayload metadata;
    };

    SnTracerEventType type;
    uint64_t thread_id;
} SnTracerEvent;

/**
 * @brief Get the current timestamp.
 *
 * @param data The user data.
 *
 * @return Returns timestamp.
 */
typedef uint64_t (*SnGetCurrentTimeFn)(void *data);

/**
 * @brief Get the current thread ID.
 *
 * @param data The user data.
 *
 * @return Returns the thread id.
 */
typedef uint64_t (*SnGetCurrentThreadIdFn)(void *data);

/**
 * @brief Per-thread mutex lock.
 *
 * @param data The user data.
 */
typedef void (*SnMutexLockFn)(void *data);

/**
 * @brief Per-thread mutex unlock.
 *
 * @param data The user data.
 */
typedef void (*SnMutexUnlockFn)(void *data);

/**
 * @brief Read lock for global tracer state.
 *
 * @param data The user data.
 */
typedef void (*SnReadLockFn)(void *data);

/**
 * @brief Read unlock for global tracer state.
 *
 * @param data The user data.
 */
typedef void (*SnReadUnlockFn)(void *data);

/**
 * @brief Write lock for global tracer state.
 *
 * @param data The user data.
 */
typedef void (*SnWriteLockFn)(void *data);

/**
 * @brief Write unlock for global tracer state.
 *
 * @param data The user data.
 */
typedef void (*SnWriteUnlockFn)(void *data);

/**
 * @brief Consumes processed tracing events.
 *
 * @param event The event.
 * @param data The user data.
 */
typedef void (*SnTracerEventConsumer)(SnTracerEvent event, void *data);

/**
 * @struct SnTracerHooks
 * @brief Collection of user-provided hooks.
 *
 * Required hooks:
 *  - time_now
 *  - thread_id
 *
 * All locking hooks are optional.
 */
typedef struct SnTracerHooks {
    SnGetCurrentTimeFn time_now; /**< Timestamp provider */
    void *time_data;

    SnGetCurrentThreadIdFn thread_id; /**< Thread id provider */
    void *thread_data;

    SnMutexLockFn mutex_lock; /**< Per thread lock */
    SnMutexUnlockFn mutex_unlock;

    SnReadLockFn read_lock; /**< Global read lock */
    SnReadUnlockFn read_unlock;
    SnWriteLockFn write_lock; /**< Global write lock */
    SnWriteUnlockFn write_unlock;
    void *read_write_lock;

    SnTracerEventConsumer consumer; /**< Event consumer callback */
    void *consumer_data;
} SnTracerHooks;

/**
 * @struct SnTracerThreadBuffer
 * @brief Per-thread ring buffer for tracing events.
 *
 * Each thread that emits events must register one buffer.
 * The buffer memory must outlive all events written to it.
 */
typedef struct SnTracerThreadBuffer {
    SnRingBufferAllocator ring_buffer;
    size_t dropped;
    struct SnTracerThreadBuffer *next;
    void *thread_lock;
    int64_t thread_id;
} SnTracerThreadBuffer;

/**
 * @struct SnTracer
 * @brief Tracing context.
 */
typedef struct SnTracer {
    SnTracerThreadBuffer *thread_buffer;  // This uses read_write_lock

    SnTracerHooks hooks;
    SnTracerThreadBuffer *process_buffer;
    bool enabled;
} SnTracer;

/**
 * @struct SnTracerEventRecord
 * @brief Handle returned by event_begin and finalized by event_commit.
 *
 * This is an internal construction helper, not a public event.
 */
typedef struct SnTracerEventRecord {
    SnTracerEventHeader *header;

    union {
        SnTracerScopeBeginPayload *scope_begin;
        SnTracerInstantPayload *instant;
        SnTracerCounterPayload *counter;
        SnTracerFlowPayload *flow;
        SnTracerMetadataPayload *metadata;
    };
} SnTracerEventRecord;

/**
 * @brief Initializes a tracer.
 *
 * @param tracer Tracer instance.
 * @param hooks Hook configuration.
 *
 * @return true on success, false if required hooks are missing.
 */
SN_INLINE bool sn_tracer_init(SnTracer *tracer, SnTracerHooks hooks) {
    if (!hooks.time_now || !hooks.thread_id) return false;

    *tracer = (SnTracer){.thread_buffer = NULL, .hooks = hooks, .enabled = false, .process_buffer = NULL};

    return true;
}

/**
 * @brief Enables tracing.
 *
 * @param tracer Tracer instance.
 */
SN_FORCE_INLINE void sn_tracer_enable(SnTracer *tracer) {
    tracer->enabled = true;
}

/**
 * @brief Disables tracing.
 *
 * @param tracer Tracer instance.
 */
SN_FORCE_INLINE void sn_tracer_disable(SnTracer *tracer) {
    tracer->enabled = false;
}

/**
 * @brief Returns whether tracing is enabled.
 *
 * @param tracer Tracer instance.
 *
 * @return Returns true if enabled, else false.
 */
SN_FORCE_INLINE bool sn_tracer_is_enabled(SnTracer *tracer) {
    return tracer->enabled;
}

/**
 * @brief Processes up to @p n events across all buffers.
 *
 * @param tracer Tracer instance.
 * @param n Number of events to process.
 *
 * @return Returns number of events processed.
 */
SN_TRACER_API size_t sn_tracer_process_n(SnTracer *tracer, size_t n);

/**
 * @brief Processes up to @p n events from a specific buffer.
 *
 * @param tracer Tracer instance.
 * @param thread_buffer The thread buffer
 * @param n Number of events to process.
 *
 * @return Returns number of events processed.
 */
SN_TRACER_API size_t sn_tracer_process_thread_buffer_n(SnTracer *tracer, SnTracerThreadBuffer *thread_buffer, size_t n);

/**
 * @brief Processes all available events.
 *
 * @param tracer Tracer instance.
 *
 * @return Returns number of events processed.
 */
SN_FORCE_INLINE size_t sn_tracer_process(SnTracer *tracer) {
    return sn_tracer_process_n(tracer, -1);
}

/**
 * @brief Processes all events from a specific thread buffer.
 *
 * @param tracer Tracer instance.
 * @param thread_buffer The thread buffer.
 *
 * @return Returns number of events processed.
 */
SN_FORCE_INLINE size_t sn_tracer_process_thread_buffer(SnTracer *tracer, SnTracerThreadBuffer *thread_buffer) {
    return sn_tracer_process_thread_buffer_n(tracer, thread_buffer, -1);
}

/**
 * @brief Registers a per-thread buffer.
 *
 * @param tracer Tracer instance.
 * @param buffer Memory block for the buffer.
 * @param buffer_size Size of memory block.
 * @param thread_lock Optional lock object.
 *
 * @return Pointer to initialized thread buffer.
 */
SN_TRACER_API SnTracerThreadBuffer *
    sn_tracer_add_thread(SnTracer *tracer, void *buffer, size_t buffer_size, void *thread_lock);

/**
 * @brief Flushes and deinitializes the tracer.
 *
 * @param tracer Tracer instance.
 */
SN_INLINE void sn_tracer_deinit(SnTracer *tracer) {
    while (sn_tracer_process(tracer));

    *tracer = (SnTracer){0};
}

/**
 * @brief Begins a new event record.
 *
 * @param tracer Tracer instance.
 * @param thread_buffer The thread buffer.
 * @param type The even type.
 *
 * @return Returns the event record.
 */
SN_TRACER_API SnTracerEventRecord sn_tracer_event_begin(
    SnTracer *tracer, SnTracerThreadBuffer *thread_buffer, SnTracerEventType type);

/**
 * @brief Finalizes an event record.
 *
 * @param tracer Tracer instance.
 * @param record The event record.
 */
SN_TRACER_API void sn_tracer_event_commit(SnTracer *tracer, SnTracerThreadBuffer *thread_buffer, SnTracerEventRecord record);

/**
 * @brief Helper function, use @ref SN_TRACER_TRACE_SCOPE_BEGIN macro.
 */
SN_TRACER_API void sn_tracer_trace_scope_begin(
    SnTracer *tracer, SnTracerThreadBuffer *thread_buffer, const char *name, const char *func,
    const char *file, uint32_t line);

/**
 * @brief Helper function, use @ref SN_TRACER_TRACE_SCOPE_END macro.
 */
SN_TRACER_API void sn_tracer_trace_scope_end(SnTracer *tracer, SnTracerThreadBuffer *thread_buffer);

/**
 * @brief Helper function, use @ref SN_TRACER_TRACE_INSTANT macro.
 */
SN_TRACER_API void sn_tracer_trace_instant(SnTracer *tracer, SnTracerThreadBuffer *thread_buffer,
                                    const char *name, const char *func, const char *file, uint32_t line);

/**
 * @brief Helper function, use @ref SN_TRACER_TRACE_COUNTER macro.
 */
SN_TRACER_API void sn_tracer_trace_counter(
    SnTracer *tracer, SnTracerThreadBuffer *thread_buffer, const char *name, int64_t value);

/**
 * @brief Helper function, use @ref SN_TRACER_TRACE_FLOW_BEGIN macro.
 */
SN_TRACER_API void sn_tracer_trace_flow_begin(
    SnTracer *tracer, SnTracerThreadBuffer *thread_buffer, const char *name, uint64_t id);

/**
 * @brief Helper function, use @ref SN_TRACER_TRACE_FLOW_STEP macro.
 */
SN_TRACER_API void sn_tracer_trace_flow_step(
    SnTracer *tracer, SnTracerThreadBuffer *thread_buffer, const char *name, uint64_t id);

/**
 * @brief Helper function, use @ref SN_TRACER_TRACE_FLOW_END macro.
 */
SN_TRACER_API void sn_tracer_trace_flow_end(
    SnTracer *tracer, SnTracerThreadBuffer *thread_buffer, const char *name, uint64_t id);

/**
 * @brief Helper function, use @ref SN_TRACER_TRACE_METADATA macro.
 */
SN_TRACER_API void sn_tracer_trace_metadata(
    SnTracer *tracer, SnTracerThreadBuffer *thread_buffer, const char *name, const char *value);

#ifdef SN_TRACER_ENABLE
    /**
     * @note All the name passed to sntracer should be static or literals.
     *      They must outlive the tracer.
     */
    #define SN_TRACER_TRACE_SCOPE_BEGIN(tracer, thread_buffer, name)                           \
        sn_tracer_trace_scope_begin(tracer, thread_buffer, name, __func__, __FILE__, __LINE__)

    /**
     * @note All the name passed to sntracer should be static or literals.
     *      They must outlive the tracer.
     */
    #define SN_TRACER_TRACE_SCOPE_END(tracer, thread_buffer) \
        sn_tracer_trace_scope_end(tracer, thread_buffer)

    // Should not use return, goto, break inside the scope, if early exit is required, use continue
    // to come out first Also should not call sn_tracer_disable within the scope
    /**
     * @brief Scoped tracing macro.
     *
     * Just syntax suger for SN_TRACER_TRACE_SCOPE_BEGIN, SN_TRACER_TRACE_SCOPE_END
     *
     * @note Do not use return, goto, or break inside this scope.
     * @note All the name passed to sntracer should be static or literals.
     *      They must outlive the tracer.
     */
    #define SN_TRACER_TRACE_SCOPE(tracer, thread_buffer, name)                                                 \
        for (bool once                                                                                         \
             = (sn_tracer_trace_scope_begin(tracer, thread_buffer, name, __func__, __FILE__, __LINE__), true); \
             once; once = (sn_tracer_trace_scope_end(tracer, thread_buffer), false))

    /**
     * @note All the name passed to sntracer should be static or literals.
     *      They must outlive the tracer.
     */
    #define SN_TRACER_TRACE_INSTANT(tracer, thread_buffer, name)                           \
        sn_tracer_trace_instant(tracer, thread_buffer, name, __func__, __FILE__, __LINE__)

    /**
     * @note All the name passed to sntracer should be static or literals.
     *      They must outlive the tracer.
     */
    #define SN_TRACER_TRACE_COUNTER(tracer, thread_buffer, name, value) \
        sn_tracer_trace_counter(tracer, thread_buffer, name, value)

    /**
     * @note All the name passed to sntracer should be static or literals.
     *      They must outlive the tracer.
     */
    #define SN_TRACER_TRACE_FLOW_BEGIN(tracer, thread_buffer, name, id) \
        sn_tracer_trace_flow_begin(tracer, thread_buffer, name, id)
    /**
     * @note All the name passed to sntracer should be static or literals.
     *      They must outlive the tracer.
     */
    #define SN_TRACER_TRACE_FLOW_STEP(tracer, thread_buffer, name, id) \
        sn_tracer_trace_flow_step(tracer, thread_buffer, name, id)
    /**
     * @note All the name passed to sntracer should be static or literals.
     *      They must outlive the tracer.
     */
    #define SN_TRACER_TRACE_FLOW_END(tracer, thread_buffer, name, id) \
        sn_tracer_trace_flow_end(tracer, thread_buffer, name, id)

    /**
     * @note All the name passed to sntracer should be static or literals.
     *      They must outlive the tracer.
     */
    #define SN_TRACER_TRACE_METADATA(tracer, thread_buffer, name, value) \
        sn_tracer_trace_metadata(tracer, thread_buffer, name, value)
#else
    #define SN_TRACER_TRACE_SCOPE_BEGIN(tracer, thread_buffer, name)
    #define SN_TRACER_TRACE_SCOPE_END(tracer, thread_buffer)
    #define SN_TRACER_TRACE_SCOPE(tracer, thread_buffer, name)
    #define SN_TRACER_TRACE_INSTANT(tracer, thread_buffer, name)
    #define SN_TRACER_TRACE_COUNTER(tracer, thread_buffer, name, value)
    #define SN_TRACER_TRACE_FLOW_BEGIN(tracer, thread_buffer, name, id)
    #define SN_TRACER_TRACE_FLOW_STEP(tracer, thread_buffer, name, id)
    #define SN_TRACER_TRACE_FLOW_END(tracer, thread_buffer, name, id)
    #define SN_TRACER_TRACE_METADATA(tracer, thread_buffer, name, value)
#endif

