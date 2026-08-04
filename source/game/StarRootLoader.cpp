#include "StarRootLoader.hpp"
#include "StarLexicalCast.hpp"
#include "StarJsonExtra.hpp"

namespace Star {

Json const BaseAssetsSettings = Json::parseJson(R"JSON(
    {
      "assetTimeToLive" : 30,

      // In seconds, audio less than this long will be decompressed in memory.
      "audioDecompressLimit" : 4.0,

      "workerPoolSize" : 2,

      "pathIgnore" : [
        "/\\.",
        "/~",
        "thumbs\\.db$",
        "\\.bak$",
        "\\.tmp$",
        "\\.zip$",
        "\\.orig$",
        "\\.fail$",
        "\\.psd$",
        "\\.tmx$"
      ],

      "digestIgnore" : [
        "\\.ogg$",
        "\\.wav$",
        "\\.abc$"
      ]
    }
  )JSON");

Json const BaseDefaultConfiguration = Json::parseJson(R"JSON(
    {
      "configurationVersion" : {
        "basic" : 2
      },

      "gameServerPort" : 21025,
)JSON"
#ifdef STAR_SYSTEM_WINDOWS
                                                      R"JSON(
      "gameServerBind" : "*",
      "queryServerBind" : "*",
      "rconServerBind" : "*",
)JSON"
#else
                                                      R"JSON(
      "gameServerBind" : "::",
)JSON"
#endif
R"JSON(
      "serverUsers" : {},
      "allowAnonymousConnections" : true,

      "bannedUuids" : [],
      "bannedIPs" : [],

      "serverName" : "A Starbound Server",
      "maxPlayers" : 8,
      "maxTeamSize" : 4,
      "serverFidelity" : "automatic",

      "checkAssetsDigest" : false,

      "safeScripts" : true,
      "scriptRecursionLimit" : 100,
      "scriptInstructionLimit" : 10000000,
      "scriptProfilingEnabled" : false,
      "scriptInstructionMeasureInterval" : 10000,
      "scriptProtoCacheEnabled" : true,

      "allowAdminCommands" : true,
      "allowAdminCommandsFromAnyone" : false,
      "anonymousConnectionsAreAdmin" : false,
      "connectionSettings" : {
        "compression" : "Zstd"
      },

      "clientP2PJoinable" : true,
      "clientIPJoinable" : false,

      "clearUniverseFiles" : false,
      "clearPlayerFiles" : false,
      "playerBackupFileCount" : 3,

      "tutorialMessages" : true,

      "interactiveHighlight" : true,

      "monochromeLighting" : false,

      "telemetryEnabled" : true,
      "telemetryDeepTracing" : false,
      "telemetryReportInterval" : 0,
      "telemetryHud" : false,

      // 2026-07-26 (#185). Defaults are jsonMerge'd from THREE blocks -- this one, the client's
      // AdditionalDefaultConfiguration, and bootconfig's -- and call-site literals act as a fourth,
      // unwritten one. Both keys below now live here, in the block that the game layer and every test
      // Root see, because that is where their readers are.
      //
      // newLighting was declared in NONE of the three. Undeclared is strictly worse than a wrong default:
      // the key is absent from the config, Configuration::get(key) returns null, and the render A/B
      // harness restores that null on exit -- which ERASES the key and persists the erase. It is read by
      // StarWorldClient (lighting calc) and StarWorldPainter (pointAdditive); both are below the client.
      //
      // antiAliasing MOVED here from StarClientApplication's block. It was declared once and correctly,
      // but one layer too high: StarWorldPainter reads it to build BackdropParams, and the rendering
      // layer must not depend on a key that only exists when the client application supplied it.
      //
      // Both values are the ones every call site already agreed on, so this changes no behaviour today.
      // It makes the agreement a fact of the tree rather than a coincidence.
      "antiAliasing" : false,
      "newLighting" : true,

      "renderVboOrphan" : true,
      "renderDrawableCache" : false,
      "renderDrawableCacheShadowCompare" : false,
      "renderDrawableCachePerPart" : true,

      "envRefreshInterval" : 4,
      // The env cache's perceptual bound, and the counterpart of parallaxMaxDriftStepPx below: refresh
      // as soon as the backdrop has drifted this many screen pixels since the cached image was drawn,
      // instead of waiting out the N-frame cadence.
      //
      // DECLARED HERE ON PURPOSE. envRefreshInterval read as "off by default" to anyone who looked at
      // the in-source fallback of 1 in StarBackdropPass.cpp, while the value that actually ships is the
      // 4 above -- which is how a cache with no motion term at all survived to reach the Director as
      // choppy stars during warp. A tuning constant that exists only as a call-site fallback is a
      // constant nobody can find.
      "envMaxDriftStepPx" : 0.75,
      "envOracle" : false,
      "lightingSpreadOracle" : false,
      "parallaxRefreshInterval" : 0,
      "parallaxMaxDriftStepPx" : 0.75,
      "parallaxOracle" : false,
      "backdropComposeMerge" : true,

      "lightingGpu" : true,
      "lightingGpuSpreadIterations" : 32,
      "lightingGpuBrightness" : 1.0,
      "lightingGpuShadowCompare" : false,

      "lightingPromoteDynamic" : 0.5,
      "lightingPromoteMinIntensity" : 0.1,
      "lightingTonemap" : true,
      "lightingTemporalDecouple" : true,
      "lightingTemporalFloorMs" : 33.0,
      "lightingWorldSampleBilinear" : false,
      "lightingWorldUpscale" : 2.0,
      "lightingGatherCache" : true,
      // Diagnostic, default OFF: rebuilds the stable grid every recompute to check the cache against it,
      // which roughly doubles gather cost. Armed in the render harness, never in a shipping run.
      "lightingGatherOracle" : false,
      "lightingGridSizeBucket" : 32,

      "safe" : {
        "alwaysAllowClipboard" : false,
        "enableImGui" : false,
        "luaHttp" : {
          "enabled" : false,
          "trustedSites" : []
        }
      },

      "crafting" : {
        "filterHaveMaterials" : false
      },

      "inventory" : {
        "pickupToActionBar" : true
      },

      "discord" : {
        "activityDetails" : "<playerName> | <worldName>"
      }
    }
  )JSON");

RootLoader::RootLoader(Defaults defaults) {
  String baseConfigFile;
  Maybe<String> userConfigFile;

  addParameter("bootconfig", "bootconfig", Optional,
      strf("Boot time configuration file, defaults to sbinit.config"));
  addParameter("logfile", "logfile", Optional,
      strf("Log to the given logfile relative to the root directory, defaults to {}",
        defaults.logFile ? *defaults.logFile : "no log file"));
  addParameter("loglevel", "level", Optional,
      strf("Sets the logging level (debug|info|warn|error), defaults to {}",
        LogLevelNames.getRight(defaults.logLevel)));
  addSwitch("quiet", strf("Do not log to stdout, defaults to {}", defaults.quiet));
  addSwitch("verbose", strf("Log to stdout, defaults to {}", !defaults.quiet));
  addSwitch("runtimeconfig",
      strf("Sets the path to the runtime configuration storage file relative to root directory, defauts to {}",
        defaults.runtimeConfigFile ? *defaults.runtimeConfigFile : "no storage file"));
  addSwitch("telemetrydeep", "enable telemetry deep tracing (timers)");
  addParameter("telemetryinterval", "seconds", Optional, "telemetry JSON report interval (0=off)");
  m_defaults = std::move(defaults);
}

pair<Root::Settings, RootLoader::Options> RootLoader::parseOrDie(
    StringList const& cmdLineArguments) const {
  auto options = VersionOptionParser::parseOrDie(cmdLineArguments);
  return {rootSettingsForOptions(options), options};
}

pair<RootUPtr, RootLoader::Options> RootLoader::initOrDie(StringList const& cmdLineArguments) const {
  auto p = parseOrDie(cmdLineArguments);
  auto root = make_unique<Root>(p.first);
  return {std::move(root), p.second};
}

pair<Root::Settings, RootLoader::Options> RootLoader::commandParseOrDie(int argc, char** argv) {
  auto options = VersionOptionParser::commandParseOrDie(argc, argv);
  return {rootSettingsForOptions(options), options};
}

pair<RootUPtr, RootLoader::Options> RootLoader::commandInitOrDie(int argc, char** argv) {
  auto p = commandParseOrDie(argc, argv);
  auto root = make_unique<Root>(p.first);
  return {std::move(root), p.second};
}

Root::Settings RootLoader::rootSettingsForOptions(Options const& options) const {
  try {
    String bootConfigFile = options.parameters.value("bootconfig").maybeFirst().value("sbinit.config");
    Json bootConfig = Json::parseJson(File::readFileString(bootConfigFile));

    Json assetsSettings = jsonMerge(
        BaseAssetsSettings,
        m_defaults.additionalAssetsSettings,
        bootConfig.get("assetsSettings", {})
      );

    Root::Settings rootSettings;
    rootSettings.assetsSettings.assetTimeToLive = assetsSettings.getInt("assetTimeToLive");
    rootSettings.assetsSettings.audioDecompressLimit = assetsSettings.getFloat("audioDecompressLimit");
    rootSettings.assetsSettings.workerPoolSize = assetsSettings.getUInt("workerPoolSize");
    rootSettings.assetsSettings.missingImage = assetsSettings.optString("missingImage");
    rootSettings.assetsSettings.missingAudio = assetsSettings.optString("missingAudio");
    rootSettings.assetsSettings.pathIgnore = jsonToStringList(assetsSettings.get("pathIgnore"));
    rootSettings.assetsSettings.digestIgnore = jsonToStringList(assetsSettings.get("digestIgnore"));

    rootSettings.assetDirectories = jsonToStringList(bootConfig.get("assetDirectories", JsonArray()));
    rootSettings.assetSources     = jsonToStringList(bootConfig.get("assetSources",     JsonArray()));

    rootSettings.defaultConfiguration = jsonMerge(
        BaseDefaultConfiguration,
        m_defaults.additionalDefaultConfiguration,
        bootConfig.get("defaultConfiguration", {})
      );

    if (options.switches.contains("telemetrydeep"))
      rootSettings.defaultConfiguration = rootSettings.defaultConfiguration.set("telemetryDeepTracing", true);
    if (auto ti = options.parameters.value("telemetryinterval").maybeFirst())
      rootSettings.defaultConfiguration = rootSettings.defaultConfiguration.set("telemetryReportInterval", lexicalCast<int>(*ti));

    rootSettings.storageDirectory = bootConfig.getString("storageDirectory");
    rootSettings.logDirectory = bootConfig.optString("logDirectory");
    rootSettings.logFile = options.parameters.value("logfile").maybeFirst().orMaybe(m_defaults.logFile);
    rootSettings.logFileBackups = bootConfig.getUInt("logFileBackups", 10);
    rootSettings.includeUGC = bootConfig.getBool("includeUGC", true);

    if (auto ll = options.parameters.value("loglevel").maybeFirst())
      rootSettings.logLevel = LogLevelNames.getLeft(*ll);
    else
      rootSettings.logLevel = m_defaults.logLevel;

    if (options.switches.contains("quiet"))
      rootSettings.quiet = true;
    else if (options.switches.contains("verbose"))
      rootSettings.quiet = false;
    else
      rootSettings.quiet = m_defaults.quiet;

    if (auto rc = options.parameters.value("runtimeconfig").maybeFirst())
      rootSettings.runtimeConfigFile = *rc;
    else
      rootSettings.runtimeConfigFile = m_defaults.runtimeConfigFile;

    return rootSettings;

  } catch (std::exception const& e) {
    throw StarException("Could not perform initial Root load", e);
  }
}

}
