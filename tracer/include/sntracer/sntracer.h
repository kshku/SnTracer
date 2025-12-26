#pragma once

#include "sntracer/defines.h"

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
 * @struct snTracerScopeBeginPayLoad
 * @brief Payload for scope-begin events.
 *
 * All string pointers must remain valid for the lifetime
 * of the tracing session (string literals recommended).
 */
typedef struct snTracerScopeBeginPayLoad {
    const char *name;
    const char *func;
    const char *file;
    uint32_t line;
} snTracerScopeBeginPayLoad;

/**
 * @struct snTracerInstantPayLoad
 * @brief Payload for instant events.
 */
typedef struct snTracerInstantPayLoad {
    const char *name;
    const char *func;
    const char *file;
    uint32_t line;
} snTracerInstantPayLoad;

/**
 * @struct snTracerCounterPayLoad
 * @brief Payload for counter events.
 */
typedef struct snTracerCounterPayLoad {
    const char *name;
    int64_t value;
} snTracerCounterPayLoad;

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
        snTracerScopeBeginPayLoad scope_begin;
        snTracerInstantPayLoad instant;
        snTracerCounterPayLoad counter;
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
        snTracerScopeBeginPayLoad *scope_begin;
        snTracerInstantPayLoad *instant;
        snTracerCounterPayLoad *counter;
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
SN_API bool sn_tracer_init(snTracer *tracer, snTracerHooks hooks);

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
SN_API void sn_tracer_deinit(snTracer *tracer);

/**
 * @brief Enables tracing.
 *
 * @param tracer Tracer instance.
 */
SN_API void sn_tracer_enable(snTracer *tracer);

/**
 * @brief Disables tracing.
 *
 * @param tracer Tracer instance.
 */
SN_API void sn_tracer_disable(snTracer *tracer);

/**
 * @brief Returns whether tracing is enabled.
 *
 * @param tracer Tracer instance.
 *
 * @return Returns true if enabled, else false.
 */
SN_API bool sn_tracer_is_enabled(snTracer *tracer);


/**
 * @brief Processes all available events.
 *
 * @param tracer Tracer instance.
 *
 * @return Returns number of events processed.
 */
SN_API size_t sn_tracer_process(snTracer *tracer);

/**
 * @brief Processes all events from a specific thread buffer.
 *
 * @param tracer Tracer instance.
 * @param thread_buffer The thread buffer.
 *
 * @return Returns number of events processed.
 */
SN_API size_t sn_tracer_process_thread_buffer(snTracer *tracer, snTracerThreadBuffer *thread_buffer);

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
#else
    #define SN_TRACER_TRACER_SCOPE_BEGIN(tracer, thread_buffer, name)
    #define SN_TRACER_TRACE_SCOPE_END(tracer, thread_buffer)
    #define SN_TRACER_TRACE_SCOPE(tracer, thread_buffer, name)
    #define SN_TRACER_TRACE_INSTANT(tracer, thread_buffer, name)
    #define SN_TRACER_TRACE_COUNTER(tracer, thread_buffer, name, value)
#endif

