#pragma once

#include "sntracer/defines.h"

#if defined(SN_TRACER_STATIC)
    #define SN_API
#else
    #ifdef SN_EXPORT
        #if defined(SN_OS_LINUX) || defined(SN_OS_MAC)
            #define SN_API __attribute__((visibility("default")))
        #elif defined(SN_OS_WINDOWS)
            #define SN_API __declspec(dllexport)
        #else
            #error "Should not reach here!"
        #endif
    #else
        #if defined(SN_OS_LINUX) || defined(SN_OS_MAC)
            #define SN_API
        #elif defined(SN_OS_WINDOWS)
            #define SN_API __declspec(dllimport)
        #else
            #error "Should not reach here!"
        #endif
    #endif
#endif

/**
 * @enum snTracerEventType
 * @brief Type of a tracing event.
 *
 * Some event types carry payload data, others do not.
 * The tracer internally reserves high bits of this value
 * for event validity tracking.
 */
typedef enum snTracerEventType {
    SN_TRACER_EVENT_TYPE_SCOPE_BEGIN, /**< Marks the beginning of a scoped region */
    SN_TRACER_EVENT_TYPE_SCOPE_END, /**< Marks the end of a scoped region */
    SN_TRACER_EVENT_TYPE_INSTANT, /**< A single point in time event */
    SN_TRACER_EVENT_TYPE_COUNTER, /**< A numeric counter sample */
    SN_TRACER_EVENT_TYPE_FLOW_BEGIN, /**< Flow event: begin */
    SN_TRACER_EVENT_TYPE_FLOW_STEP, /**< Flow event: step */
    SN_TRACER_EVENT_TYPE_FLOW_END, /**< Flow event: end */
    SN_TRACER_EVENT_TYPE_METADATA, /**< Metadata event */
} snTracerEventType;

/**
 * @struct snTracerEventHeader
 * @brief Common header stored for every event.
 *
 * The tracer may temporarily mark an event as incomplete
 * using internal bits of the type field.
 */
typedef struct snTracerEventHeader {
    uint64_t timestamp;
    snTracerEventType type;
} snTracerEventHeader;

/**
 * @struct snTracerScopeBeginPayload
 * @brief Payload for scope-begin events.
 *
 * All string pointers must remain valid for the lifetime
 * of the tracing session (string literals recommended).
 */
typedef struct snTracerScopeBeginPayload {
    const char *name;
    const char *func;
    const char *file;
    uint32_t line;
} snTracerScopeBeginPayload;

/**
 * @struct snTracerInstantPayload
 * @brief Payload for instant events.
 */
typedef struct snTracerInstantPayload {
    const char *name;
    const char *func;
    const char *file;
    uint32_t line;
} snTracerInstantPayload;

/**
 * @struct snTracerCounterPayload
 * @brief Payload for counter events.
 */
typedef struct snTracerCounterPayload {
    const char *name;
    int64_t value;
} snTracerCounterPayload;

/**
 * @struct snTracerFlowPayload
 * @brief Payload for flow events.
 */
typedef struct snTracerFlowPayload {
    const char *name;
    uint64_t id;
} snTracerFlowPayload;

/**
 * @struct snTracerMetadataPayload.
 * @brief Payload for metadata events.
 */
typedef struct snTracerMetadataPayload {
    const char *name;
    const char *value;
} snTracerMetadataPayload;

/**
 * @struct snTracerEvent
 * @brief Fully reconstructed event delivered to consumers.
 *
 * This structure is never written directly into buffers.
 * It is assembled during processing and passed to the consumer callback.
 */
typedef struct snTracerEvent {
    uint64_t timestamp;
    union {
        snTracerScopeBeginPayload scope_begin;
        snTracerInstantPayload instant;
        snTracerCounterPayload counter;
        snTracerFlowPayload flow;
        snTracerMetadataPayload metadata;
    };
    snTracerEventType type;
    uint64_t thread_id;
} snTracerEvent;


/**
 * @brief Get the current timestamp.
 *
 * @param data The user data.
 *
 * @return Returns timestamp.
 */
typedef uint64_t (*snGetCurrentTimeFn)(void *data);

/**
 * @brief Get the current thread ID.
 *
 * @param data The user data.
 *
 * @return Returns the thread id.
 */
typedef uint64_t (*snGetCurrentThreadIdFn)(void *data);

/**
 * @brief Per-thread mutex lock.
 *
 * @param data The user data.
 */
typedef void (*snMutexLockFn)(void *data);

/**
 * @brief Per-thread mutex unlock.
 *
 * @param data The user data.
 */
typedef void (*snMutexUnlockFn)(void *data);

/**
 * @brief Read lock for global tracer state.
 *
 * @param data The user data.
 */
typedef void (*snReadLockFn)(void *data);

/**
 * @brief Read unlock for global tracer state.
 *
 * @param data The user data.
 */
typedef void (*snReadUnlockFn)(void *data);

/**
 * @brief Write lock for global tracer state.
 *
 * @param data The user data.
 */
typedef void (*snWriteLockFn)(void *data);

/**
 * @brief Write unlock for global tracer state.
 *
 * @param data The user data.
 */
typedef void (*snWriteUnlockFn)(void *data);

/**
 * @brief Consumes processed tracing events.
 *
 * @param event The event.
 * @param data The user data.
 */
typedef void (*snTracerEventConsumer)(snTracerEvent event, void *data);

/**
 * @struct snTracerHooks
 * @brief Collection of user-provided hooks.
 *
 * Required hooks:
 *  - time_now
 *  - thread_id
 *
 * All locking hooks are optional.
 */
typedef struct snTracerHooks {
    snGetCurrentTimeFn time_now; /**< Timestamp provider */
    void *time_data;

    snGetCurrentThreadIdFn thread_id; /**< Thread id provider */
    void *thread_data;

    snMutexLockFn mutex_lock; /**< Per thread lock */
    snMutexUnlockFn mutex_unlock;

    snReadLockFn read_lock; /**< Global read lock */
    snReadUnlockFn read_unlock;
    snWriteLockFn write_lock; /**< Global write lock */
    snWriteUnlockFn write_unlock;
    void *read_write_lock;

    snTracerEventConsumer consumer; /**< Event consumer callback */
    void *consumer_data;
} snTracerHooks;

/**
 * @struct snTracerThreadBuffer
 * @brief Per-thread ring buffer for tracing events.
 *
 * Each thread that emits events must register one buffer.
 * The buffer memory must outlive all events written to it.
 */
typedef struct snTracerThreadBuffer {
    size_t buffer_size;
    size_t write_offset;
    size_t read_offset;
    size_t dropped;
    struct snTracerThreadBuffer *next;
    void *thread_lock;
    int64_t thread_id;
} snTracerThreadBuffer;


/**
 * @struct snTracer
 * @brief Tracing context.
 */
typedef struct snTracer {
    snTracerThreadBuffer *thread_buffer; // This uses read_write_lock

    snTracerHooks hooks;
    snTracerThreadBuffer *process_buffer;
    bool enabled;
} snTracer;

/**
 * @struct snTracerEventRecord
 * @brief Handle returned by event_begin and finalized by event_commit.
 *
 * This is an internal construction helper, not a public event.
 */
typedef struct snTracerEventRecord {
    snTracerEventHeader *header;
    union {
        snTracerScopeBeginPayload *scope_begin;
        snTracerInstantPayload *instant;
        snTracerCounterPayload *counter;
        snTracerFlowPayload *flow;
        snTracerMetadataPayload *metadata;
    };
} snTracerEventRecord;


/**
 * @brief Initializes a tracer.
 *
 * @param tracer Tracer instance.
 * @param hooks Hook configuration.
 *
 * @return true on success, false if required hooks are missing.
 */
SN_INLINE bool sn_tracer_init(snTracer *tracer, snTracerHooks hooks) {
    if (!hooks.time_now || !hooks.thread_id) return false;

    *tracer = (snTracer) {
        .thread_buffer = NULL,
        .hooks = hooks,
        .enabled = false,
        .process_buffer = NULL
    };

    return true;
}

/**
 * @brief Enables tracing.
 *
 * @param tracer Tracer instance.
 */
SN_FORCE_INLINE void sn_tracer_enable(snTracer *tracer) {
    tracer->enabled = true;
}

/**
 * @brief Disables tracing.
 *
 * @param tracer Tracer instance.
 */
SN_FORCE_INLINE void sn_tracer_disable(snTracer *tracer) {
    tracer->enabled = false;
}

/**
 * @brief Returns whether tracing is enabled.
 *
 * @param tracer Tracer instance.
 *
 * @return Returns true if enabled, else false.
 */
SN_FORCE_INLINE bool sn_tracer_is_enabled(snTracer *tracer) {
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
SN_API size_t sn_tracer_process_n(snTracer *tracer, size_t n);

/**
 * @brief Processes up to @p n events from a specific buffer.
 *
 * @param tracer Tracer instance.
 * @param thread_buffer The thread buffer
 * @param n Number of events to process.
 * 
 * @return Returns number of events processed.
 */
SN_API size_t sn_tracer_process_thread_buffer_n(snTracer *tracer, snTracerThreadBuffer *thread_buffer, size_t n);

/**
 * @brief Processes all available events.
 *
 * @param tracer Tracer instance.
 *
 * @return Returns number of events processed.
 */
SN_FORCE_INLINE size_t sn_tracer_process(snTracer *tracer) {
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
SN_FORCE_INLINE size_t sn_tracer_process_thread_buffer(snTracer *tracer, snTracerThreadBuffer *thread_buffer) {
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
SN_API snTracerThreadBuffer *sn_tracer_add_thread(snTracer *tracer, void *buffer, size_t buffer_size, void *thread_lock);

/**
 * @brief Flushes and deinitializes the tracer.
 *
 * @param tracer Tracer instance.
 */
SN_INLINE void sn_tracer_deinit(snTracer *tracer) {
    while (sn_tracer_process(tracer));

    *tracer = (snTracer){0};
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
SN_API snTracerEventRecord sn_tracer_event_begin(snTracer *tracer, snTracerThreadBuffer *thread_buffer, snTracerEventType type);

/**
 * @brief Finalizes an event record.
 *
 * @param tracer Tracer instance.
 * @param record The event record.
 */
SN_API void sn_tracer_event_commit(snTracer *tracer, snTracerThreadBuffer *thread_buffer, snTracerEventRecord record);

/**
 * @brief Helper function, use @ref SN_TRACER_TRACE_SCOPE_BEGIN macro.
 */
SN_API void sn_tracer_trace_scope_begin(snTracer *tracer, snTracerThreadBuffer *thread_buffer,
        const char *name, const char *func, const char *file, uint32_t line);

/**
 * @brief Helper function, use @ref SN_TRACER_TRACE_SCOPE_END macro.
 */
SN_API void sn_tracer_trace_scope_end(snTracer *tracer, snTracerThreadBuffer *thread_buffer);

/**
 * @brief Helper function, use @ref SN_TRACER_TRACE_INSTANT macro.
 */
SN_API void sn_tracer_trace_instant(snTracer *tracer, snTracerThreadBuffer *thread_buffer,
        const char *name, const char *func, const char *file, uint32_t line);

/**
 * @brief Helper function, use @ref SN_TRACER_TRACE_COUNTER macro.
 */
SN_API void sn_tracer_trace_counter(snTracer *tracer, snTracerThreadBuffer *thread_buffer,
        const char *name, int64_t value);

/**
 * @brief Helper function, use @ref SN_TRACER_TRACE_FLOW_BEGIN macro.
 */
SN_API void sn_tracer_trace_flow_begin(snTracer *tracer, snTracerThreadBuffer *thread_buffer, const char *name, uint64_t id);

/**
 * @brief Helper function, use @ref SN_TRACER_TRACE_FLOW_STEP macro.
 */
SN_API void sn_tracer_trace_flow_step(snTracer *tracer, snTracerThreadBuffer *thread_buffer, const char *name, uint64_t id);

/**
 * @brief Helper function, use @ref SN_TRACER_TRACE_FLOW_END macro.
 */
SN_API void sn_tracer_trace_flow_end(snTracer *tracer, snTracerThreadBuffer *thread_buffer, const char *name, uint64_t id);

/**
 * @brief Helper function, use @ref SN_TRACER_TRACE_METADATA macro.
 */
SN_API void sn_tracer_trace_metadata(snTracer *tracer, snTracerThreadBuffer *thread_buffer, const char *name, const char *value);


#ifdef SN_TRACER_ENABLE
    /**
     * @note All the name passed to sntracer should be static or literals.
     *      They must outlive the tracer.
     */
    #define SN_TRACER_TRACE_SCOPE_BEGIN(tracer, thread_buffer, name) \
        sn_tracer_trace_scope_begin(tracer, thread_buffer, name, __func__, __FILE__, __LINE__)

    /**
     * @note All the name passed to sntracer should be static or literals.
     *      They must outlive the tracer.
     */
    #define SN_TRACER_TRACE_SCOPE_END(tracer, thread_buffer) \
        sn_tracer_trace_scope_end(tracer, thread_buffer)

    // Should not use return, goto, break inside the scope, if early exit is required, use continue to come out first
    // Also should not call sn_tracer_disable within the scope
    /**
     * @brief Scoped tracing macro.
     *
     * Just syntax suger for SN_TRACER_TRACE_SCOPE_BEGIN, SN_TRACER_TRACE_SCOPE_END
     *
     * @note Do not use return, goto, or break inside this scope.
     * @note All the name passed to sntracer should be static or literals.
     *      They must outlive the tracer.
     */
    #define SN_TRACER_TRACE_SCOPE(tracer, thread_buffer, name) \
        for (bool once = (sn_tracer_trace_scope_begin(tracer, thread_buffer, name, __func__, __FILE__, __LINE__), true); \
                once; once = (sn_tracer_trace_scope_end(tracer, thread_buffer), false))

    /**
     * @note All the name passed to sntracer should be static or literals.
     *      They must outlive the tracer.
     */
    #define SN_TRACER_TRACE_INSTANT(tracer, thread_buffer, name) \
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
    #define SN_TRACER_TRACER_SCOPE_BEGIN(tracer, thread_buffer, name)
    #define SN_TRACER_TRACE_SCOPE_END(tracer, thread_buffer)
    #define SN_TRACER_TRACE_SCOPE(tracer, thread_buffer, name)
    #define SN_TRACER_TRACE_INSTANT(tracer, thread_buffer, name)
    #define SN_TRACER_TRACE_COUNTER(tracer, thread_buffer, name, value)
    #define SN_TRACER_TRACE_FLOW_BEGIN(tracer, thread_buffer, name, id)
    #define SN_TRACER_TRACE_FLOW_STEP(tracer, thread_buffer, name, id)
    #define SN_TRACER_TRACE_METADATA(tracer, thread_buffer, name, value)
#endif

#undef SN_API
