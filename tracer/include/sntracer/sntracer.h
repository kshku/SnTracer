#pragma once

#include "sntracer/defines.h"

typedef enum snTracerEventType {
    SN_TRACER_EVENT_TYPE_SCOPE_BEGIN,
    SN_TRACER_EVENT_TYPE_SCOPE_END,
    SN_TRACER_EVENT_TYPE_INSTANT,
    SN_TRACER_EVENT_TYPE_COUNTER,
    SN_TRACER_EVENT_TYPE_FLOW_BEGIN,
    SN_TRACER_EVENT_TYPE_FLOW_STEP,
    SN_TRACER_EVENT_TYPE_FLOW_END,
    SN_TRACER_EVENT_TYPE_METADATA,
} snTracerEventType;

typedef struct snTracerEventHeader {
    uint64_t timestamp;
    snTracerEventType type;
} snTracerEventHeader;

typedef struct snTracerScopeBeginPayLoad {
    const char *name;
    const char *func;
    const char *file;
    uint32_t line;
} snTracerScopeBeginPayLoad;

typedef struct snTracerInstantPayLoad {
    const char *name;
    const char *func;
    const char *file;
    uint32_t line;
} snTracerInstantPayLoad;

typedef struct snTracerCounterPayLoad {
    const char *name;
    int64_t value;
} snTracerCounterPayLoad;

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

typedef uint64_t (*snGetCurrentTimeFn)(void *data);

typedef uint64_t (*snGetCurrentThreadIdFn)(void *data);

// Used for per thread lock
typedef void (*snMutexLockFn)(void *data);
typedef void (*snMutexUnlockFn)(void *data);

// Used for global lock
typedef void (*snReadLockFn)(void *data);
typedef void (*snReadUnlockFn)(void *data);
typedef void (*snWriteLockFn)(void *data);
typedef void (*snWriteUnlockFn)(void *data);

typedef void (*snTracerEventConsumer)(snTracerEvent event, void *data);

typedef struct snTracerHooks {
    snGetCurrentTimeFn time_now;
    void *time_data;

    snGetCurrentThreadIdFn thread_id;
    void *thread_data;

    // Used for per thread lock
    snMutexLockFn mutex_lock;
    snMutexUnlockFn mutex_unlock;

    // Used for global lock
    snReadLockFn read_lock;
    snReadUnlockFn read_unlock;
    snWriteLockFn write_lock;
    snWriteUnlockFn write_unlock;
    void *read_write_lock;

    snTracerEventConsumer consumer;
    void *consumer_data;
} snTracerHooks;

typedef struct snTracerThreadBuffer {
    size_t buffer_size;
    size_t write_offset;
    size_t read_offset;
    size_t dropped;
    struct snTracerThreadBuffer *next;
    void *thread_lock;
    int64_t thread_id;
} snTracerThreadBuffer;

typedef struct snTracer {
    snTracerThreadBuffer *thread_buffer; // This uses read_write_lock

    snTracerHooks hooks;
    snTracerThreadBuffer *process_buffer;
    bool enabled;
} snTracer;

typedef struct snTracerEventRecord {
    snTracerEventHeader *header;
    union {
        snTracerScopeBeginPayLoad *scope_begin;
        snTracerInstantPayLoad *instant;
        snTracerCounterPayLoad *counter;
    };
} snTracerEventRecord;

SN_API bool sn_tracer_init(snTracer *tracer, snTracerHooks hooks);

SN_API snTracerThreadBuffer *sn_tracer_add_thread(snTracer *tracer, void *buffer, size_t buffer_size, void *thread_lock);

SN_API void sn_tracer_deinit(snTracer *tracer);

SN_API void sn_tracer_enable(snTracer *tracer);

SN_API void sn_tracer_disable(snTracer *tracer);

SN_API bool sn_tracer_is_enabled(snTracer *tracer);

SN_API size_t sn_tracer_process(snTracer *tracer);

SN_API size_t sn_tracer_process_n(snTracer *tracer, size_t n);

SN_API snTracerEventRecord sn_tracer_event_begin(snTracer *tracer, snTracerThreadBuffer *thread_buffer, snTracerEventType type);

SN_API void sn_tracer_event_commit(snTracer *tracer, snTracerThreadBuffer *thread_buffer, snTracerEventRecord record);

SN_API void sn_tracer_trace_scope_begin(snTracer *tracer, snTracerThreadBuffer *thread_buffer,
        const char *name, const char *func, const char *file, uint32_t line);

SN_API void sn_tracer_trace_scope_end(snTracer *tracer, snTracerThreadBuffer *thread_buffer);

SN_API void sn_tracer_trace_instant(snTracer *tracer, snTracerThreadBuffer *thread_buffer,
        const char *name, const char *func, const char *file, uint32_t line);

SN_API void sn_tracer_trace_counter(snTracer *tracer, snTracerThreadBuffer *thread_buffer,
        const char *name, int64_t value);

#ifdef SN_TRACER_ENABLE
    #define SN_TRACER_TRACE_SCOPE_BEGIN(tracer, thrad_buffer, name) \
        sn_tracer_trace_scope_begin(tracer, thread_buffer, name, __func__, __FILE__, __LINE__)

    #define SN_TRACER_TRACE_SCOPE_END(tracer, thread_buffer) \
        sn_tracer_trace_scope_end(tracer, thread_buffer)

    // Just syntax suger for SN_TRACER_TRACE_SCOPE_BEGIN, SN_TRACER_TRACE_SCOPE_END
    // Should not use return, goto, break inside the scope, if early exit is required, use continue to come out first
    // Also should not call sn_tracer_disable within the scope
    #define SN_TRACER_TRACE_SCOPE(tracer, thread_buffer, name) \
        for (bool once = (sn_tracer_trace_scope_begin(tracer, thread_buffer, name, __func__, __FILE__, __LINE__), true); \
                once; once = (sn_tracer_trace_scope_end(tracer, thread_buffer), false))

    #define SN_TRACER_TRACE_INSTANT(tracer, thread_buffer, name) \
        sn_tracer_trace_instant(tracer, thread_buffer, name, __func__, __FILE__, __LINE__)

    #define SN_TRACER_TRACE_COUNTER(tracer, thraed_buffer, name, value) \
        sn_tracer_trace_counter(tracer, thread_buffer, name, value)
#else
    #define SN_TRACER_TRACER_SCOPE_BEGIN(tracer, thread_buffer, name)
    #define SN_TRACER_TRACE_SCOPE_END(tracer, thread_buffer)
    #define SN_TRACER_TRACE_SCOPE(tracer, thread_buffer, name)
    #define SN_TRACER_TRACE_INSTANT(tracer, thread_buffer, name)
    #define SN_TRACER_TRACE_COUNTER(tracer, thraed_buffer, name, value)
#endif

