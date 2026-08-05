INCLUDE_DIRECTORIES (
    ${STAR_EXTERN_INCLUDES}
    ${STAR_CORE_INCLUDES}
    ${STAR_METRICS_INCLUDES}
  )

SET (star_metrics_HEADERS
    StarBusyReading.hpp
    StarClientBusyReader.hpp
    StarMetricSample.hpp
  )

SET (star_metrics_SOURCES
    StarClientBusyReader.cpp
  )

# OBJECT, like star_core and star_base: consumers name $<TARGET_OBJECTS:star_metrics> in their
# ADD_EXECUTABLE rather than linking a library, so a module with no link dependencies of its own
# stays that way.
ADD_LIBRARY (star_metrics OBJECT ${star_metrics_SOURCES} ${star_metrics_HEADERS})
TARGET_INCLUDE_DIRECTORIES (star_metrics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
