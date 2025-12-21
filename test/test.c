#ifndef SN_OS_WINDOWS

#define _GNU_SOURCE
#define SN_TRACER_ENABLE
#include <sntracer/sntracer.h>

#include <stdio.h>
#include <time.h>


#include <pthread.h>

#define STRINGIFY(x) #x

#define ARRAY_LEN(arr) (sizeof(arr) / sizeof(arr[0]))

uint64_t time_now_hook(void *data) {
    (void)data;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
} 

void time_sleep(uint32_t ms) {
    struct timespec time = {.tv_sec = ms / 1000,
        .tv_nsec = (ms % 1000) * 1000000};

    nanosleep(&time, NULL);
}

uint64_t thread_id_hook(void *data) {
    (void)data;
    return (uint64_t)pthread_self();
}

const char *get_event_name(snTracerEventType type) {
    switch (type) {
        case SN_TRACER_EVENT_TYPE_SCOPE_BEGIN:
            return STRINGIFY(SN_TRACER_EVENT_TYPE_SCOPE_BEGIN);
        case SN_TRACER_EVENT_TYPE_SCOPE_END:
            return STRINGIFY(SN_TRACER_EVENT_TYPE_SCOPE_END);
        case SN_TRACER_EVENT_TYPE_INSTANT:
            return STRINGIFY(SN_TRACER_EVENT_TYPE_INSTANT);
        case SN_TRACER_EVENT_TYPE_COUNTER:
            return STRINGIFY(SN_TRACER_EVENT_TYPE_COUNTER);
        case SN_TRACER_EVENT_TYPE_FLOW_BEGIN:
            return STRINGIFY(SN_TRACER_EVENT_TYPE_FLOW_BEGIN);
        case SN_TRACER_EVENT_TYPE_FLOW_STEP:
            return STRINGIFY(SN_TRACER_EVENT_TYPE_FLOW_STEP);
        case SN_TRACER_EVENT_TYPE_FLOW_END:
            return STRINGIFY(SN_TRACER_EVENT_TYPE_FLOW_END);
        case SN_TRACER_EVENT_TYPE_METADATA:
            return STRINGIFY(SN_TRACER_EVENT_TYPE_METADATA);
    }
}

void consumer_hook(snTracerEvent event, void *data) {
    (void)data;
    printf("timestamp = %ld, ", event.timestamp);
    printf("thread_id = %ld, ", event.thread_id);
    printf("event type = %s\n", get_event_name(event.type));

    switch (event.type) {
        case SN_TRACER_EVENT_TYPE_SCOPE_BEGIN:
            printf("\tname: %s\n", event.scope_begin.name);
            printf("\tfunc: %s\n", event.scope_begin.func);
            printf("\tfile: %s\n", event.scope_begin.file);
            printf("\tline: %u\n", event.scope_begin.line);
            break;
        case SN_TRACER_EVENT_TYPE_INSTANT:
            printf("\tname: %s\n", event.instant.name);
            printf("\tfunc: %s\n", event.instant.func);
            printf("\tfile: %s\n", event.instant.file);
            printf("\tline: %u\n", event.instant.line);
            break;
        case SN_TRACER_EVENT_TYPE_COUNTER:
            printf("\tname: %s\n", event.counter.name);
            printf("\tvalue: %ld\n", event.counter.value);
            break;

        case SN_TRACER_EVENT_TYPE_SCOPE_END:
            break;

        case SN_TRACER_EVENT_TYPE_FLOW_BEGIN:
        case SN_TRACER_EVENT_TYPE_FLOW_STEP:
        case SN_TRACER_EVENT_TYPE_FLOW_END:
        case SN_TRACER_EVENT_TYPE_METADATA:
        default:
            break;
    }
    putchar('\n');
}


int main(void) {
    snTracer tracer;

    snTracerHooks hooks = {
        .time_now = time_now_hook,
        .thread_id = thread_id_hook,
        .consumer = consumer_hook,
    };

    sn_tracer_init(&tracer, hooks);

    char buffer[1024];
    snTracerThreadBuffer *thread_buffer = sn_tracer_add_thread(&tracer, buffer, ARRAY_LEN(buffer), NULL);

    sn_tracer_enable(&tracer);

    SN_TRACER_TRACE_COUNTER(&tracer, thread_buffer, "test counter", 5);
    SN_TRACER_TRACE_SCOPE(&tracer, thread_buffer, "test scope") {
        printf("Inside the test scope\n");
        SN_TRACER_TRACE_INSTANT(&tracer, thread_buffer, "test instant");
        printf("wasting a bit of time\n");
        time_sleep(1);
        printf("done wasting time!\n");
    }

    SN_TRACER_TRACE_COUNTER(&tracer, thread_buffer, "test counter", 10);

    sn_tracer_disable(&tracer);

    SN_TRACER_TRACE_COUNTER(&tracer, thread_buffer, "test counter", 15);

    sn_tracer_deinit(&tracer);

}

#endif
