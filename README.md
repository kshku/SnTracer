# SnTracer

A tracing library written in C.

It records timestamped events into user provided buffers and lets
the user consume and export them however they want.

It **does not**:
- Allocate memory
- Create threads
- Guarantee thread-safety (unless hooks are provided)

## Supported Event Types
| Event Type | Purpose |
| ---------- | ------- |
| SCOPE\_BEGIN | Begin a timed scope. |
| SCOPE\_END | End a timed scope. |
| INSTANT | Point-in-time event. |
| COUNTER | Numeric value over time. |
| FLOW\_BEGIN | Start async flow. |
| FLOW\_STEP | Intermediate async step. |
| FLOW\_END | End async flow. |
| METADATA | Global metadata. |

## Thread buffers
SnTracer uses one buffer per thread.
- Threads itself should register it's buffer.
- Buffer lifetime should exceed tracer lifetime.
- Lock are optional (external synchronization assumed)

## Usage

### Including
```c
#define SN_TRACER_ENABLE
#include <sntracer/sntracer.h>
```

### Initialization
```c
snTracer tracer;

snTracerHooks hooks = {
    .time_now = my_time_now,
    .thread_id = my_thread_id,
    .mutex_lock = my_mutex_lock,
    .mutex_unlock = my_mutex_unlock,
    .read_lock = my_rw_read_lock,
    .read_unlock = my_rw_read_unlock,
    .write_lock = my_rw_write_lock,
    .write_unlock = my_rw_write_unlock,
    .read_write_lock = &global_lock,
    .consumer = my_event_consumer,
    .consumer_data = NULL,
};

sn_tracer_init(&tracer, hooks);
sn_tracer_enable(&tracer);
```

### Recording events

#### Scope tracing
```c
SN_TRACER_TRACE_SCOPE(tracer, thread_buffer, "Test update") {
    do_something();
}
```
equivalent to
```c
SN_TRACER_TRACE_SCOPE_BEGIN(tracer, thread_buffer, "Test update");
do_something();
SN_TRACER_TRACE_SCOPE_END(tracer, thread_buffer);
```
Important rules:
- Do NOT use return, goto, or break inside the scope
- If early exit is required, use continue
- Do NOT call sn\_tracer\_disable() inside a scope

#### Instant Event
```c
SN_TRACER_TRACE_INSTANT(tracer, thread_buffer, "Player Jump");
```

### Processing
Recording just writes to buffer, nothing is processed.
**If buffer is full, events will be dropped.**

You explicitly process events:
```c
sn_tracer_process(&tracer);
```

Or per-thread:
```c
sn_tracer_process_thread_buffer(&tracer, thread_buffer);
```

Or limited batches:
```c
sn_tracer_process_n(&tracer, 128);
```

);

Processing:
- Reads events in order
- Converts them to snTracerEvent
- Calls the user-provided consumer

### Event Consumer
```c
void my_event_consumer(snTracerEvent event, void *data) {
    // write to file
    // stream over socket
    // convert to Chrome Trace JSON
    // aggregate statistics
}
```
SnTracer does not care what you do here.

#### Chrome Trace Example (Conceptual)
A consumer can emit JSON like:
```json
{
  "name": "Physics Update",
  "cat": "scope",
  "ph": "B",
  "ts": 12345678,
  "pid": 0,
  "tid": 3
}
```
And for SCOPE\_END:
```json
{
  "ph": "E",
  "ts": 12345789,
  "pid": 0,
  "tid": 3
}
```
Write to a file, open it in chrome://tracing.

***Checkout `test/example_tracing.c`***
