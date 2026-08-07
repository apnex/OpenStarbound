INCLUDE_DIRECTORIES (
    ${STAR_EXTERN_INCLUDES}
    ${STAR_CORE_INCLUDES}
    ${STAR_METRICS_INCLUDES}
  )

SET (star_metrics_HEADERS
    StarBusyReading.hpp
    StarClientBusyReader.hpp
    StarEngineBusyReader.hpp
    StarMetricSample.hpp
    StarProcFs.hpp
    StarThreadBusyReader.hpp
  )

SET (star_metrics_SOURCES
    StarClientBusyReader.cpp
    StarEngineBusyReader.cpp
    StarProcFs.cpp
    StarThreadBusyReader.cpp
  )

# OBJECT, like star_core and star_base: consumers name $<TARGET_OBJECTS:star_metrics> in their
# ADD_EXECUTABLE rather than linking a library, so a module with no link dependencies of its own
# stays that way.
ADD_LIBRARY (star_metrics OBJECT ${star_metrics_SOURCES} ${star_metrics_HEADERS})
TARGET_INCLUDE_DIRECTORIES (star_metrics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# The module's front door, and the proof it is a module: `metrics --pid N` measures a process from
# OUTSIDE it, so it links star_core and star_metrics and nothing of the game. It can therefore be run
# against the shipped, uninstrumented binary without perturbing what it reads.
ADD_EXECUTABLE (metrics
  $<TARGET_OBJECTS:star_extern> $<TARGET_OBJECTS:star_core> $<TARGET_OBJECTS:star_metrics>
  metrics_main.cpp)
TARGET_LINK_LIBRARIES (metrics ${STAR_EXT_LIBS})
