FetchContent_Declare(
    VectorSimilarity
    URL https://github.com/RedisAI/VectorSimilarity/archive/refs/tags/v0.7.1.zip
    URL_HASH SHA256=d767ad3a1c65217c2b49d932aca935bd372a4d09c7dd6d96ebbea55cbeef0ce0
)

SET(VECSIM_BUILD_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(VectorSimilarity)
