INCLUDE_DIRECTORIES (
    ${STAR_EXTERN_INCLUDES}
    ${STAR_CORE_INCLUDES}
    ${STAR_BASE_INCLUDES}
    ${STAR_GAME_INCLUDES}
    ${STAR_PLATFORM_INCLUDES}
    ${STAR_APPLICATION_INCLUDES}
    ${STAR_RENDERING_INCLUDES}
  )

SET (star_rendering_HEADERS
      StarAnchorTypes.hpp
      StarAssetTextureGroup.hpp
      StarBackdropPass.hpp
      StarDrawablePainter.hpp
      StarEnvironmentPainter.hpp
      StarFontTextureGroup.hpp
      StarGpuLightmapPass.hpp
      # L2. Header-only, so it compiles wherever it is included and was absent from this list until #186 --
      # which meant the layer with the strictest sovereignty claim in the subsystem was not a declared source
      # of the target that contains it. Every other rendering header is listed; this one is not optional.
      StarRetainedSurface.hpp
      StarTextPainter.hpp
      StarTilePainter.hpp
      StarWorldPainter.hpp
      StarWorldPass.hpp
    )

SET (star_rendering_SOURCES
      StarAnchorTypes.cpp
      StarAssetTextureGroup.cpp
      StarBackdropPass.cpp
      StarDrawablePainter.cpp
      StarEnvironmentPainter.cpp
      StarFontTextureGroup.cpp
      StarGpuLightmapPass.cpp
      StarTextPainter.cpp
      StarTilePainter.cpp
      StarWorldPainter.cpp
      StarWorldPass.cpp
    )

ADD_LIBRARY (star_rendering OBJECT ${star_rendering_SOURCES} ${star_rendering_HEADERS})

IF(STAR_PRECOMPILED_HEADERS)
  TARGET_PRECOMPILE_HEADERS (star_rendering REUSE_FROM star_core)
ENDIF()