set(HEADERFILES
    sntracer.h
)

set(SRCS
    sntracer.c
)

set(INCLUDE_BASE "${PROJECT_SOURCE_DIR}/tracer/include/sntracer")

list(TRANSFORM HEADERFILES PREPEND "${INCLUDE_BASE}/")

target_sources(sntracer PRIVATE ${HEADERFILES} ${SRCS})
