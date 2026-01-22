#define _GNU_SOURCE
#define SN_TRACER_ENABLE
#include <sntracer/sntracer.h>

#include <stdio.h>

#ifndef SN_OS_WINDOWS

#include <time.h>
#include <stdatomic.h>

#include <pthread.h>

#define STRINGIFY(x) #x
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

void mutex_lock_hook(void *data) {
    pthread_mutex_t *mutex = data;
    pthread_mutex_lock(mutex);
}

void mutex_unlock_hook(void *data) {
    pthread_mutex_t *mutex = data;
    pthread_mutex_unlock(mutex);
}

void read_lock_hook(void *data) {
    pthread_rwlock_t *rwlock = data;
    pthread_rwlock_rdlock(rwlock);
}

void read_unlock_hook(void *data) {
    pthread_rwlock_t *rwlock = data;
    pthread_rwlock_unlock(rwlock);
}

void write_lock_hook(void *data) {
    pthread_rwlock_t *rwlock = data;
    pthread_rwlock_wrlock(rwlock);
}

void write_unlock_hook(void *data) {
    pthread_rwlock_t *rwlock = data;
    pthread_rwlock_unlock(rwlock);
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

typedef struct {
    snTracer *tracer;
    void *buffer;
    size_t buffer_size;
    pthread_mutex_t *mutex;
} ProducerArags;

void *producer_thread(void *args) {
    ProducerArags *pa = args;

    snTracerThreadBuffer *thread_buffer = sn_tracer_add_thread(pa->tracer, pa->buffer, pa->buffer_size, pa->mutex);

    for (int i = 0; i < 100; ++i) {
        SN_TRACER_TRACE_COUNTER(pa->tracer, thread_buffer, "loop counter", i);
        SN_TRACER_TRACE_SCOPE(pa->tracer, thread_buffer, "test scope") {
            printf("Inside the test scope\n");
            SN_TRACER_TRACE_INSTANT(pa->tracer, thread_buffer, "test instant");
            printf("wasting a bit of time\n");
            time_sleep(1);
            printf("done wasting time!\n");
        }
    }

    SN_TRACER_TRACE_COUNTER(pa->tracer, thread_buffer, "test counter", 10);

    return NULL;
}

typedef struct {
    snTracer *tracer;
    atomic_int *done;
} ConsumerArags;

void *consumer_thread(void *args) {
    ConsumerArags *ca = args;

    while (!atomic_load(ca->done)) {
        sn_tracer_process(ca->tracer);
        time_sleep(1);
    }

    while (sn_tracer_process(ca->tracer));
    return NULL;
}

int main(void) {
    snTracer tracer;

    pthread_rwlock_t read_write_lock;
    pthread_rwlock_init(&read_write_lock, NULL);

    snTracerHooks hooks = {
        .time_now = time_now_hook,
        .time_data = NULL,

        .thread_id = thread_id_hook,
        .thread_data = NULL,

        .mutex_lock = mutex_lock_hook,
        .mutex_unlock = mutex_unlock_hook,

        .read_lock = read_lock_hook,
        .read_unlock = read_unlock_hook,
        .write_lock = write_lock_hook,
        .write_unlock = write_unlock_hook,
        .read_write_lock = &read_write_lock,

        .consumer = consumer_hook,
        .consumer_data = NULL
    };

    sn_tracer_init(&tracer, hooks);
    sn_tracer_enable(&tracer);

    atomic_int done = 0;
    ConsumerArags ca = {.tracer = &tracer, .done = &done};
    pthread_t consumer;

    pthread_create(&consumer, NULL, consumer_thread, &ca);

#define NUM_PRODUCERS 4
#define BUFFER_SIZE_PER_PRODUCER 1024

    char buffer[BUFFER_SIZE_PER_PRODUCER * NUM_PRODUCERS];
    pthread_mutex_t mutexes[NUM_PRODUCERS];
    pthread_t producers[NUM_PRODUCERS];
    ProducerArags pas[NUM_PRODUCERS];

    for (int i = 0; i < NUM_PRODUCERS; ++i) {
        pthread_mutex_init(&mutexes[i], NULL);
        pas[i] = (ProducerArags){
            .tracer = &tracer,
            .buffer = &buffer[i * BUFFER_SIZE_PER_PRODUCER],
            .buffer_size = BUFFER_SIZE_PER_PRODUCER,
            .mutex = &mutexes[i]
        };

        pthread_create(&producers[i], NULL, producer_thread, &pas[i]);
    }

    for (int i = 0; i < NUM_PRODUCERS; ++i) {
        pthread_join(producers[i], NULL);
        pthread_mutex_destroy(&mutexes[i]);
    }

    atomic_store(&done, 1);
    pthread_join(consumer, NULL);

    sn_tracer_disable(&tracer);

    sn_tracer_deinit(&tracer);
    pthread_rwlock_destroy(&read_write_lock);
}

#else

int main(void) {
	printf("Test not implemented for windows!");
}

#endif
