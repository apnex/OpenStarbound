INCLUDE_DIRECTORIES (
    ${STAR_EXTERN_INCLUDES}
    ${STAR_CORE_INCLUDES}
    ${STAR_METRICS_INCLUDES}
  )

SET (star_metrics_HEADERS
    StarMetricSample.hpp
  )

SET (star_metrics_SOURCES
  )

# INTERFACE, not OBJECT, and only until the first .cpp lands. An OBJECT library holding no
# translation unit is not a legal target -- CMake cannot infer a linker language for it and fails the
# GENERATE step, not the build ("CMake can not determine linker language for target: star_metrics"),
# which would take the whole project down rather than this directory. INTERFACE says what is true
# today: the module compiles nothing and exports a header path. Convert it to
# ADD_LIBRARY (star_metrics OBJECT ...) at the same commit that adds star_metrics_SOURCES its first
# entry, and add $<TARGET_OBJECTS:star_metrics> to the consumers then.
ADD_LIBRARY (star_metrics INTERFACE ${star_metrics_SOURCES} ${star_metrics_HEADERS})
TARGET_INCLUDE_DIRECTORIES (star_metrics INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
