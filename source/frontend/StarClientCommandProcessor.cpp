#include "StarClientCommandProcessor.hpp"
#include "StarItem.hpp"
#include "StarAssets.hpp"
#include "StarItemDatabase.hpp"
#include "StarPlayer.hpp"
#include "StarPlayerTech.hpp"
#include "StarPlayerInventory.hpp"
#include "StarPlayerLog.hpp"
#include "StarWorldClient.hpp"
#include "StarAiInterface.hpp"
#include "StarQuestInterface.hpp"
#include "StarStatistics.hpp"
#include "StarInterfaceLuaBindings.hpp"
#include "StarInput.hpp"
#include "StarTelemetry.hpp"
#include "StarTelemetryReporter.hpp"

namespace Star {

ClientCommandProcessor::ClientCommandProcessor(UniverseClientPtr universeClient, CinematicPtr cinematicOverlay,
  MainInterfacePaneManager* paneManager, StringMap<StringList> macroCommands)
  : m_universeClient(std::move(universeClient)), m_cinematicOverlay(std::move(cinematicOverlay)),
  m_paneManager(paneManager), m_macroCommands(std::move(macroCommands)) {
  m_builtinCommands = {
    {"reload", bind(&ClientCommandProcessor::reload, this)},
    {"hotReload", bind(&ClientCommandProcessor::hotReload, this)},
    {"whoami", bind(&ClientCommandProcessor::whoami, this)},
    {"gravity", bind(&ClientCommandProcessor::gravity, this)},
    {"debug", bind(&ClientCommandProcessor::debug, this, _1)},
    {"boxes", bind(&ClientCommandProcessor::boxes, this)},
    {"fullbright", bind(&ClientCommandProcessor::fullbright, this)},
    {"asyncLighting", bind(&ClientCommandProcessor::asyncLighting, this)},
    {"setGravity", bind(&ClientCommandProcessor::setGravity, this, _1)},
    {"resetGravity", bind(&ClientCommandProcessor::resetGravity, this)},
    {"fixedCamera", bind(&ClientCommandProcessor::fixedCamera, this)},
    {"monochromeLighting", bind(&ClientCommandProcessor::monochromeLighting, this)},
    {"radioMessage", bind(&ClientCommandProcessor::radioMessage, this, _1)},
    {"clearRadioMessages", bind(&ClientCommandProcessor::clearRadioMessages, this)},
    {"clearCinematics", bind(&ClientCommandProcessor::clearCinematics, this)},
    {"startQuest", bind(&ClientCommandProcessor::startQuest, this, _1)},
    {"completeQuest", bind(&ClientCommandProcessor::completeQuest, this, _1)},
    {"failQuest", bind(&ClientCommandProcessor::failQuest, this, _1)},
    {"previewNewQuest", bind(&ClientCommandProcessor::previewNewQuest, this, _1)},
    {"previewQuestComplete", bind(&ClientCommandProcessor::previewQuestComplete, this, _1)},
    {"previewQuestFailed", bind(&ClientCommandProcessor::previewQuestFailed, this, _1)},
    {"clearScannedObjects", bind(&ClientCommandProcessor::clearScannedObjects, this)},
    {"played", bind(&ClientCommandProcessor::playTime, this)},
    {"deaths", bind(&ClientCommandProcessor::deathCount, this)},
    {"cinema", bind(&ClientCommandProcessor::cinema, this, _1)},
    {"suicide", bind(&ClientCommandProcessor::suicide, this)},
    {"naked", bind(&ClientCommandProcessor::naked, this)},
    {"resetAchievements", bind(&ClientCommandProcessor::resetAchievements, this)},
    {"statistic", bind(&ClientCommandProcessor::statistic, this, _1)},
    {"giveessentialitem", bind(&ClientCommandProcessor::giveEssentialItem, this, _1)},
    {"maketechavailable", bind(&ClientCommandProcessor::makeTechAvailable, this, _1)},
    {"enabletech", bind(&ClientCommandProcessor::enableTech, this, _1)},
    {"upgradeship", bind(&ClientCommandProcessor::upgradeShip, this, _1)},
    {"swap", bind(&ClientCommandProcessor::swap, this, _1)},
    {"respawnInWorld", bind(&ClientCommandProcessor::respawnInWorld, this, _1)},
    {"render", bind(&ClientCommandProcessor::render, this, _1)},
    {"telemetry", bind(&ClientCommandProcessor::telemetry, this, _1)},
    {"lighting", bind(&ClientCommandProcessor::lighting, this, _1)}
  };
  // The drawable-cache command would prefer /render, but that name is taken by
  // the image-render command above, so it falls back to /rendercache
  // (subcommands unchanged: cache on|off, cache shadow on|off, cache status).
  m_builtinCommands.set(m_builtinCommands.contains("render") ? "rendercache" : "render",
      bind(&ClientCommandProcessor::renderCache, this, _1));
}

bool ClientCommandProcessor::adminCommandAllowed() const {
  return Root::singleton().configuration()->get("allowAdminCommandsFromAnyone").toBool() ||
    m_universeClient->mainPlayer()->isAdmin();
}

String ClientCommandProcessor::previewQuestPane(StringList const& arguments, function<PanePtr(QuestPtr)> createPane) {
  Maybe<String> templateId = {};
  templateId = arguments[0];
  if (auto quest = createPreviewQuest(*templateId, arguments.at(1), arguments.at(2), m_universeClient->mainPlayer().get())) {
    auto pane = createPane(quest);
    m_paneManager->displayPane(PaneLayer::ModalWindow, pane);
    return "Previewed quest";
  }
  return "No such quest";
}

StringList ClientCommandProcessor::handleCommand(String const& commandLine, bool userInput) {
  Maybe<Input::ClipboardUnlock> unlock;
  if (userInput) // allow clipboard usage during this code
    unlock = Input::singleton().unlockClipboard();
  try {
    if (!commandLine.beginsWith("/"))
      throw StarException("ClientCommandProcessor expected command, does not start with '/'");

    String allArguments = commandLine.substr(1);
    String command = allArguments.extract();

    StringList result;
    if (auto builtinCommand = m_builtinCommands.maybe(command)) {
      result.append((*builtinCommand)(allArguments));
    } else if (auto macroCommand = m_macroCommands.maybe(command)) {
      for (auto const& c : *macroCommand) {
        if (c.beginsWith("/"))
          result.appendAll(handleCommand(c));
        else
          result.append(c);
      }
    } else {
      auto player = m_universeClient->mainPlayer();
      if (auto messageResult = player->receiveMessage(connectionForEntity(player->entityId()), "/" + command, {allArguments})) {
        if (messageResult->isType(Json::Type::String))
          result.append(*messageResult->stringPtr());
        else if (!messageResult->isNull())
          result.append(messageResult->repr(1, true));
      } else
        m_universeClient->sendChat(commandLine, ChatSendMode::Broadcast);
    }
    return result;
  } catch (ShellParsingException const& e) {
    Logger::error("Shell parsing exception: {}", outputException(e, false));
    return {"Shell parsing exception"};
  } catch (std::exception const& e) {
    Logger::error("Exception caught handling client command {}: {}", commandLine, outputException(e, true));
    return {strf("Exception caught handling client command {}", commandLine)};
  }
}

bool ClientCommandProcessor::debugDisplayEnabled() const {
  return m_debugDisplayEnabled;
}

bool ClientCommandProcessor::debugHudEnabled() const {
  return m_debugHudEnabled;
}

bool ClientCommandProcessor::fixedCameraEnabled() const {
  return m_fixedCameraEnabled;
}

String ClientCommandProcessor::reload() {
  Root::singleton().reload();
  return "Client Star::Root reloaded";
}

String ClientCommandProcessor::hotReload() {
  Root::singleton().hotReload();
  return "Hot-reloaded assets";
}

String ClientCommandProcessor::whoami() {
  return strf("Client: You are {}. You are {}an Admin.",
              m_universeClient->mainPlayer()->name(), m_universeClient->mainPlayer()->isAdmin() ? "" : "not ");
}

String ClientCommandProcessor::gravity() {
  if (!adminCommandAllowed())
    return "You must be an admin to use this command.";

  return toString(m_universeClient->worldClient()->gravity(m_universeClient->mainPlayer()->position()));
}

String ClientCommandProcessor::debug(String const& argumentsString) {
  auto arguments = m_parser.tokenizeToStringList(argumentsString);
  if (!arguments.empty() && arguments.at(0).equalsIgnoreCase("hud")) {
    m_debugHudEnabled = !m_debugHudEnabled;
    return strf("Debug HUD {}", m_debugHudEnabled ? "enabled" : "disabled");
  }
  else {
    m_debugDisplayEnabled = !m_debugDisplayEnabled;
    return strf("Debug display {}", m_debugDisplayEnabled ? "enabled" : "disabled");
  }
}

String ClientCommandProcessor::boxes() {
  auto worldClient = m_universeClient->worldClient();
  bool state = !worldClient->collisionDebug();
  worldClient->setCollisionDebug(state);
  return strf("Geometry debug display {}", state ? "enabled" : "disabled");
}

String ClientCommandProcessor::fullbright() {
  auto worldClient = m_universeClient->worldClient();
  bool state = !worldClient->fullBright();
  worldClient->setFullBright(state);
  return strf("Fullbright render lighting {}", state ? "enabled" : "disabled");
}

String ClientCommandProcessor::asyncLighting() {
  auto worldClient = m_universeClient->worldClient();
  bool state = !worldClient->asyncLighting();
  worldClient->setAsyncLighting(state);
  return strf("Asynchronous render lighting {}", state ? "enabled" : "disabled");
}

String ClientCommandProcessor::setGravity(String const& argumentsString) {
  auto arguments = m_parser.tokenizeToStringList(argumentsString);
  if (!adminCommandAllowed())
    return "You must be an admin to use this command.";

  m_universeClient->worldClient()->overrideGravity(lexicalCast<float>(arguments.at(0)));
  return strf("Gravity set to {} (This is client-side!)", arguments.at(0));
}

String ClientCommandProcessor::resetGravity() {
  if (!adminCommandAllowed())
    return "You must be an admin to use this command.";

  m_universeClient->worldClient()->resetGravity();
  return "Gravity reset";
}

String ClientCommandProcessor::fixedCamera() {
  m_fixedCameraEnabled = !m_fixedCameraEnabled;
  return strf("Fixed camera {}", m_fixedCameraEnabled ? "enabled" : "disabled");
}

String ClientCommandProcessor::monochromeLighting() {
  bool monochrome = !Root::singleton().configuration()->get("monochromeLighting").toBool();
  Root::singleton().configuration()->set("monochromeLighting", monochrome);
  return strf("Monochrome lighting {}", monochrome ? "enabled" : "disabled");
}

String ClientCommandProcessor::radioMessage(String const& argumentsString) {
  auto arguments = m_parser.tokenizeToStringList(argumentsString);
  if (!adminCommandAllowed())
    return "You must be an admin to use this command.";

  if (arguments.size() != 1)
    return "Must provide one argument";

  m_universeClient->mainPlayer()->queueRadioMessage(arguments.at(0));
  return "Queued radio message";
}

String ClientCommandProcessor::clearRadioMessages() {
  if (!adminCommandAllowed())
    return "You must be an admin to use this command.";

  m_universeClient->mainPlayer()->log()->clearRadioMessages();
  return "Player radio message records cleared!";
}

String ClientCommandProcessor::clearCinematics() {
  if (!adminCommandAllowed())
    return "You must be an admin to use this command.";

  m_universeClient->mainPlayer()->log()->clearCinematics();
  return "Player cinematic records cleared!";
}

String ClientCommandProcessor::startQuest(String const& argumentsString) {
  auto arguments = m_parser.tokenizeToStringList(argumentsString);
  if (!adminCommandAllowed())
    return "You must be an admin to use this command.";

  auto questArc = QuestArcDescriptor::fromJson(Json::parseSequence(arguments.at(0)).get(0));
  m_universeClient->questManager()->offer(make_shared<Quest>(questArc, 0, m_universeClient->mainPlayer().get()));
  return "Quest started";
}

String ClientCommandProcessor::completeQuest(String const& argumentsString) {
  auto arguments = m_parser.tokenizeToStringList(argumentsString);
  if (!adminCommandAllowed())
    return "You must be an admin to use this command.";

  m_universeClient->questManager()->getQuest(arguments.at(0))->complete();
  return strf("Quest {} complete", arguments.at(0));
}

String ClientCommandProcessor::failQuest(String const& argumentsString) {
  auto arguments = m_parser.tokenizeToStringList(argumentsString);
  if (!adminCommandAllowed())
    return "You must be an admin to use this command.";

  m_universeClient->questManager()->getQuest(arguments.at(0))->fail();
  return strf("Quest {} failed", arguments.at(0));
}

String ClientCommandProcessor::previewNewQuest(String const& argumentsString) {
  auto arguments = m_parser.tokenizeToStringList(argumentsString);
  if (!adminCommandAllowed())
    return "You must be an admin to use this command.";

  return previewQuestPane(arguments, [this](QuestPtr const& quest) {
    return make_shared<NewQuestInterface>(m_universeClient->questManager(), quest, m_universeClient->mainPlayer());
  });
}

String ClientCommandProcessor::previewQuestComplete(String const& argumentsString) {
  auto arguments = m_parser.tokenizeToStringList(argumentsString);
  if (!adminCommandAllowed())
    return "You must be an admin to use this command.";

  return previewQuestPane(arguments, [this](QuestPtr const& quest) {
    return make_shared<QuestCompleteInterface>(quest, m_universeClient->mainPlayer(), CinematicPtr{});
  });
}

String ClientCommandProcessor::previewQuestFailed(String const& argumentsString) {
  auto arguments = m_parser.tokenizeToStringList(argumentsString);
  if (!adminCommandAllowed())
    return "You must be an admin to use this command.";

  return previewQuestPane(arguments, [this](QuestPtr const& quest) {
    return make_shared<QuestFailedInterface>(quest, m_universeClient->mainPlayer());
  });
}

String ClientCommandProcessor::clearScannedObjects() {
  if (!adminCommandAllowed())
    return "You must be an admin to use this command.";

  m_universeClient->mainPlayer()->log()->clearScannedObjects();
  return "Player scanned objects cleared!";
}

String ClientCommandProcessor::playTime() {
  return strf("Total play time: {}", Time::printDuration(m_universeClient->mainPlayer()->log()->playTime()));
}

String ClientCommandProcessor::deathCount() {
  auto deaths = m_universeClient->mainPlayer()->log()->deathCount();
  return deaths ? strf("Total deaths: {}", deaths) : "Total deaths: 0. Well done!";
}

String ClientCommandProcessor::cinema(String const& argumentsString) {
  auto arguments = m_parser.tokenizeToStringList(argumentsString);
  if (!adminCommandAllowed())
    return "You must be an admin to use this command.";

  m_cinematicOverlay->load(Root::singleton().assets()->json(arguments.at(0)));
  if (arguments.size() > 1)
    m_cinematicOverlay->setTime(lexicalCast<float>(arguments.at(1)));
  return strf("Started cinematic {} at {}", arguments.at(0), arguments.size() > 1 ? arguments.at(1) : "beginning");
}

String ClientCommandProcessor::suicide() {
  m_universeClient->mainPlayer()->kill();
  return "You are now dead";
}

String ClientCommandProcessor::naked() {
  auto playerInventory = m_universeClient->mainPlayer()->inventory();
  for (auto slot : EquipmentSlotNames.leftValues())
    playerInventory->addItems(playerInventory->addToBags(playerInventory->takeSlot(slot)));
  return "You are now naked";
}

String ClientCommandProcessor::resetAchievements() {
  if (!adminCommandAllowed())
    return "You must be an admin to use this command.";

  if (m_universeClient->statistics()->reset()) {
    return "Achievements reset";
  }
  return "Unable to reset achievements";
}

String ClientCommandProcessor::statistic(String const& argumentsString) {
  auto arguments = m_parser.tokenizeToStringList(argumentsString);
  if (!adminCommandAllowed())
    return "You must be an admin to use this command.";

  StringList values;
  for (String const& statName : arguments) {
    values.append(strf("{} = {}", statName, m_universeClient->statistics()->stat(statName)));
  }
  return values.join("\n");
}

String ClientCommandProcessor::giveEssentialItem(String const& argumentsString) {
  auto arguments = m_parser.tokenizeToStringList(argumentsString);
  if (!adminCommandAllowed())
    return "You must be an admin to use this command.";

  if (arguments.size() < 2)
    return "Not enough arguments to /giveessentialitem";

  try {
    auto item = Root::singleton().itemDatabase()->item(ItemDescriptor(arguments.at(0)));
    auto slot = EssentialItemNames.getLeft(arguments.at(1));
    m_universeClient->mainPlayer()->inventory()->setEssentialItem(slot, item);
    return strf("Put {} in player slot {}", item->name(), arguments.at(1));
  } catch (MapException const& e) {
    return strf("Invalid essential item slot {}.", arguments.at(1));
  }
}

String ClientCommandProcessor::makeTechAvailable(String const& argumentsString) {
  auto arguments = m_parser.tokenizeToStringList(argumentsString);
  if (!adminCommandAllowed())
    return "You must be an admin to use this command.";

  if (arguments.size() == 0)
    return "Not enough arguments to /maketechavailable";

  m_universeClient->mainPlayer()->techs()->makeAvailable(arguments.at(0));
  return strf("Added {} to player's visible techs", arguments.at(0));
}

String ClientCommandProcessor::enableTech(String const& argumentsString) {
  auto arguments = m_parser.tokenizeToStringList(argumentsString);
  if (!adminCommandAllowed())
    return "You must be an admin to use this command.";

  if (arguments.size() == 0)
    return "Not enough arguments to /enabletech";

  m_universeClient->mainPlayer()->techs()->makeAvailable(arguments.at(0));
  m_universeClient->mainPlayer()->techs()->enable(arguments.at(0));
  return strf("Player tech {} enabled", arguments.at(0));
}

String ClientCommandProcessor::upgradeShip(String const& argumentsString) {
  auto arguments = m_parser.tokenizeToStringList(argumentsString);
  if (!adminCommandAllowed())
    return "You must be an admin to use this command.";

  if (arguments.size() == 0)
    return "Not enough arguments to /upgradeship";

  auto shipUpgrades = Json::parseJson(arguments.at(0));
  m_universeClient->rpcInterface()->invokeRemote("ship.applyShipUpgrades", shipUpgrades);
  return strf("Upgraded ship");
}

String ClientCommandProcessor::swap(String const& argumentsString) {
  auto arguments = m_parser.tokenizeToStringList(argumentsString);

  if (arguments.size() == 0) {
    m_paneManager->displayRegisteredPane(MainInterfacePanes::CharacterSwap);
    return "";
  }

  if (m_universeClient->switchPlayer(arguments[0]))
    return "Successfully swapped player";
  else
    return "Failed to swap player";
}

String ClientCommandProcessor::respawnInWorld(String const& argumentsString) {
  auto arguments = m_parser.tokenizeToStringList(argumentsString);
  auto worldClient = m_universeClient->worldClient();
  
  if (arguments.size() == 0)
    return strf("Respawn in this world is currently {}", worldClient->respawnInWorld() ? "true" : "false");

  bool respawnInWorld = Json::parse(arguments.at(0)).toBool();
  worldClient->setRespawnInWorld(respawnInWorld);
  return strf("Respawn in this world set to {} (This is client-side!)", respawnInWorld ? "true" : "false");
}

// Hardcoded render command, future version will write to the clipboard and possibly be implemented in Lua
String ClientCommandProcessor::render(String const& path) {
  if (path.empty()) {
    return "Specify a path to render an image, or for worn armor: "
           "^cyan;hat^reset;/^cyan;chest^reset;/^cyan;legs^reset;/^cyan;back^reset; "
           "or for body parts: "
           "^cyan;head^reset;/^cyan;body^reset;/^cyan;hair^reset;/^cyan;facialhair^reset;/"
           "^cyan;facialmask^reset;/^cyan;frontarm^reset;/^cyan;backarm^reset;/^cyan;emote^reset;";
  }
  AssetPath assetPath;
  bool outputSheet = false;
  String outputName = "render";
  auto player = m_universeClient->mainPlayer();
  if (player && path.utf8Size() < 100) {
    auto args = m_parser.tokenizeToStringList(path);
    auto first = args.maybeFirst().value().toLower();
    auto humanoid = player->humanoid();
    auto& identity = humanoid->identity();
    auto species = identity.imagePath.value(identity.species);
    outputSheet = true;
    outputName = first;
    if (first.equals("hat")) {
      assetPath.basePath = humanoid->headArmorFrameset();
      assetPath.directives += humanoid->headArmorDirectives();
    } else if (first.equals("chest")) {
      if (args.size() <= 1) {
        return "Chest armors have multiple spritesheets. Do: "
               "^white;/render chest ^cyan;front^reset;/^cyan;torso^reset;/^cyan;back^reset;. "
               "To repair old generated clothes, then also specify ^cyan;old^reset;.";
      }
      String sheet = args[1].toLower();
      outputName += " " + sheet;
      if (sheet == "torso") {
        assetPath.basePath = humanoid->chestArmorFrameset();
        assetPath.directives += humanoid->chestArmorDirectives();
      } else if (sheet == "front") {
        assetPath.basePath = humanoid->frontSleeveFrameset();
        assetPath.directives += humanoid->chestArmorDirectives();
      } else if (sheet == "back") {
        assetPath.basePath = humanoid->backSleeveFrameset();
        assetPath.directives += humanoid->chestArmorDirectives();
      } else {
        return strf("^red;Invalid chest sheet type '{}'^reset;", sheet);
      }
      // recovery for custom chests made by a very old generator
      if (args.size() > 2 && args[2].toLower() == "old" && assetPath.basePath.beginsWith("/items/armors/avian/avian-tier6separator/"))
          assetPath.basePath = "/items/armors/avian/avian-tier6separator/old/" + assetPath.basePath.substr(41);
    } else if (first.equals("legs")) {
      assetPath.basePath = humanoid->legsArmorFrameset();
      assetPath.directives += humanoid->legsArmorDirectives();
    } else if (first.equals("back")) {
      assetPath.basePath = humanoid->backArmorFrameset();
      assetPath.directives += humanoid->backArmorDirectives();
    } else if (first.equals("body")) {
      assetPath.basePath = humanoid->getBodyFromIdentity();
      assetPath.directives += identity.bodyDirectives;
    } else if (first.equals("head")) {
      outputSheet = false;
      assetPath.basePath = humanoid->getHeadFromIdentity();
      assetPath.subPath = String("normal");
      assetPath.directives += identity.bodyDirectives;
    } else if (first.equals("hair")) {
      outputSheet = false;
      assetPath.basePath = humanoid->getHairFromIdentity();
      assetPath.subPath = String("normal");
      assetPath.directives += identity.hairDirectives;
    } else if (first.equals("facialhair")) {
      outputSheet = false;
      assetPath.basePath = humanoid->getFacialHairFromIdentity();
      assetPath.subPath = String("normal");
      assetPath.directives += identity.facialHairDirectives;
    } else if (first.equals("facialmask")) {
      outputSheet = false;
      assetPath.basePath = humanoid->getFacialMaskFromIdentity();
      assetPath.subPath = String("normal");
      assetPath.directives += identity.facialMaskDirectives;
    } else if (first.equals("frontarm")) {
      assetPath.basePath = humanoid->getFrontArmFromIdentity();
      assetPath.directives += identity.bodyDirectives;
    } else if (first.equals("backarm")) {
      assetPath.basePath = humanoid->getBackArmFromIdentity();
      assetPath.directives += identity.bodyDirectives;
    } else if (first.equals("emote")) {
      assetPath.basePath = humanoid->getFacialEmotesFromIdentity();
      assetPath.directives += identity.emoteDirectives;
    } else {
      outputSheet = false;
      outputName = "render";
    }
  }
  if (assetPath == AssetPath()) {
    assetPath = AssetPath::split(path);
    if (!assetPath.basePath.beginsWith("/"))
      assetPath.basePath = "/assetmissing.png" + assetPath.basePath;
  }
  auto assets = Root::singleton().assets();
  ImageConstPtr image;
  if (outputSheet) {
    auto sheet = make_shared<Image>(*assets->image(assetPath.basePath));
    sheet->convert(PixelFormat::RGBA32);
    AssetPath framePath = assetPath;

    StringMap<pair<RectU, ImageConstPtr>> frames;
    if (auto imageFrames = assets->imageFrames(assetPath.basePath))
      for (auto& pair : imageFrames->frames)
        frames[pair.first] = make_pair(pair.second, ImageConstPtr());

    if (frames.empty())
      return "^red;Failed to save image^reset;";

    for (auto& entry : frames) {
      framePath.subPath = entry.first;
      entry.second.second = assets->image(framePath);
    }

    Vec2U frameSize = frames.begin()->second.first.size();
    Vec2U imageSize = frames.begin()->second.second->size().piecewiseMin(Vec2U{256, 256});
    if (imageSize.min() == 0)
      return "^red;Resulting image is empty^reset;";

    for (auto& frame : frames) {
      RectU& box = frame.second.first;
      box.setXMin((box.xMin() / frameSize[0]) * imageSize[0]);
      box.setYMin(((sheet->height() - box.yMin() - box.height()) / frameSize[1]) * imageSize[1]);
      box.setXMax(box.xMin() + imageSize[0]);
      box.setYMax(box.yMin() + imageSize[1]);
    }

    if (frameSize != imageSize) {
      unsigned sheetWidth  = (sheet->width()  / frameSize[0]) * imageSize[0];
      unsigned sheetHeight = (sheet->height() / frameSize[0]) * imageSize[0];
      sheet->reset(sheetWidth, sheetHeight, PixelFormat::RGBA32);
    }

    for (auto& entry : frames)
      sheet->copyInto(entry.second.first.min(), *entry.second.second);

    image = std::move(sheet);
  } else {
    image = assets->image(assetPath);
  }
  if (image->size().min() == 0)
    return "^red;Resulting image is empty^reset;";
  auto outputDirectory = Root::singleton().toStoragePath("output");
  auto outputPath = File::relativeTo(outputDirectory, strf("{}.png", outputName));
  if (!File::isDirectory(outputDirectory))
    File::makeDirectory(outputDirectory);

  auto buffer = make_shared<Buffer>();
  image->writePng(buffer);
  auto file = File::open(outputPath, IOMode::Write | IOMode::Truncate);
  file->writeFull(buffer->ptr(), buffer->size());
  file->close();
  auto fullPath = File::fullPath(outputPath);
  GuiContext::singleton().setClipboardImage(*image, &buffer->data(), &fullPath);
  return strf("Saved '{}.png' ({}x{}) and copied to clipboard", outputName, image->width(), image->height());
}

String ClientCommandProcessor::telemetry(String const& argumentsString) {
  auto args = m_parser.tokenizeToStringList(argumentsString);
  auto cfg = Root::singleton().configuration();
  if (args.empty())
    return strf("telemetry: enabled={} deep={} hud={} interval={}",
      cfg->get("telemetryEnabled", true).toBool(), cfg->get("telemetryDeepTracing", false).toBool(),
      cfg->get("telemetryHud", false).toBool(), cfg->get("telemetryReportInterval", 0).toInt());

  String sub = args.at(0);
  if (sub == "on") {
    cfg->set("telemetryEnabled", true);
    Telemetry::setEnabled(true);
    return "telemetry on";
  }
  if (sub == "off") {
    cfg->set("telemetryEnabled", false);
    Telemetry::setEnabled(false);
    return "telemetry off";
  }
  if (sub == "deep") {
    bool v = args.size() < 2 || args.at(1) != "off";
    cfg->set("telemetryDeepTracing", v);
    Telemetry::setDeepEnabled(v);
    return strf("telemetry deep={}", v);
  }
  if (sub == "hud") {
    bool v = !cfg->get("telemetryHud", false).toBool();
    cfg->set("telemetryHud", v);
    return strf("telemetry hud={}", v);
  }
  if (sub == "interval") {
    int s = args.size() > 1 ? lexicalCast<int>(args.at(1)) : 0;
    cfg->set("telemetryReportInterval", s);
    return strf("telemetry interval={}", s);
  }
  if (sub == "snapshot") {
    String p = TelemetryReporter::writeSnapshot(Root::singleton().toStoragePath(""), JsonObject{
      {"vsync", Json(Root::singleton().configuration()->get("vsync", true).optBool().value(true))}
    });
    return strf("wrote {}", p);
  }
  return "usage: /telemetry [on|off|deep [off]|hud|interval <s>|snapshot]";
}

// Drawable-cache toggles (registered as /rendercache when /render is taken).
// Frontend-only: reads/writes the runtime config keys the NetworkedAnimator
// drawable path consults each call -- no game-logic side effects.
String ClientCommandProcessor::renderCache(String const& argumentsString) {
  auto args = m_parser.tokenizeToStringList(argumentsString);
  auto cfg = Root::singleton().configuration();
  String const usage = "usage: /rendercache cache [on|off|perpart on|off|shadow on|off|status] | envrefresh <N> | envoracle on|off | parallaxrefresh <N> | paralloracle on|off";
  auto status = [&]() {
    return strf("render cache: enabled={} perPart={} shadowCompare={} envRefreshInterval={} envOracle={} parallaxRefreshInterval={} parallaxOracle={} backdropComposeMerge={}",
      cfg->getOrDefault("renderDrawableCache").toBool(),
      cfg->getOrDefault("renderDrawableCachePerPart").toBool(),
      cfg->getOrDefault("renderDrawableCacheShadowCompare").toBool(),
      cfg->getOrDefault("envRefreshInterval").toUInt(),
      cfg->getOrDefault("envOracle").toBool(),
      cfg->getOrDefault("parallaxRefreshInterval").toUInt(),
      cfg->getOrDefault("parallaxOracle").toBool(),
      cfg->getOrDefault("backdropComposeMerge").toBool());
  };

  if (!args.empty() && args.at(0) == "envrefresh") {
    // Environment-cache probe: refresh the cached env FBO every N frames (1 = current behavior,
    // bit-identical). It is composited into "main" every frame regardless of N. (AA-off only; the
    // renderer runs the direct env path when antiAliasing is on.)
    if (args.size() >= 2) {
      auto n = maybeLexicalCast<unsigned>(args.at(1));
      if (!n || *n < 1)
        return "usage: /rendercache envrefresh <N>  (N >= 1; 1 = every frame = current behavior)";
      cfg->set("envRefreshInterval", *n);
    }
    return strf("render cache envRefreshInterval={}", cfg->getOrDefault("envRefreshInterval").toUInt());
  }

  if (!args.empty() && args.at(0) == "envoracle") {
    // Env-cache bit-identity oracle (default off). When on, each AA-off frame ALSO renders the env pass
    // into the "envRef" reference FBO (direct clear:true black -- NOT the cache) and pixel-compares it
    // against the composited "main", logging [envoracle] DIFF/MATCH. Offline validate gate only
    // (glReadPixels stalls). At N=1 a MATCH proves the cache path is bit-identical to the direct path.
    if (args.size() >= 2)
      cfg->set("envOracle", args.at(1) == "on");
    bool on = cfg->getOrDefault("envOracle").toBool();
    if (on && cfg->getOrDefault("antiAliasing").toBool())
      return "render cache envOracle=true -- NOTE: antiAliasing is ON; the oracle runs only with AA off (disable AA in graphics settings), otherwise no [envoracle] lines are emitted";
    return strf("render cache envOracle={}", on);
  }

  if (!args.empty() && args.at(0) == "parallaxrefresh") {
    // Parallax retained-cache (SP-2): refresh the cached parallax FBO every N frames (1 = direct/stock).
    // Composited (premultiplied-over) into "main" every frame at N>1; also force-refreshes on camera move /
    // zoom (Option B). AA-off only.
    if (args.size() >= 2) {
      auto n = maybeLexicalCast<unsigned>(args.at(1));
      if (!n)
        return "usage: /rendercache parallaxrefresh <N>  (0 = ADAPTIVE, auto-derived per world [default]; 1 = off/direct; >1 = manual fixed N)";
      cfg->set("parallaxRefreshInterval", *n);
    }
    unsigned pn = cfg->getOrDefault("parallaxRefreshInterval").toUInt();
    return strf("render cache parallaxRefreshInterval={}{}", pn, pn == 0 ? " (ADAPTIVE -- auto per world; see [parallaxauto] in the log)" : (pn == 1 ? " (off/direct)" : " (manual)"));
  }

  if (!args.empty() && args.at(0) == "paralloracle") {
    // Parallax bit-identity oracle (default off). When on, builds parallaxRef = env_bg + parallax DIRECT and
    // pixel-compares it against the composited main (env_bg + premultiplied cache); logs [paralloracle]
    // DIFF/MATCH. At N=1 a MATCH proves the premultiplied cache path is bit-identical. AA-off only.
    if (args.size() >= 2)
      cfg->set("parallaxOracle", args.at(1) == "on");
    bool on = cfg->getOrDefault("parallaxOracle").toBool();
    if (on && cfg->getOrDefault("antiAliasing").toBool())
      return "render cache parallaxOracle=true -- NOTE: antiAliasing is ON; the oracle runs only with AA off";
    return strf("render cache parallaxOracle={}", on);
  }

  if (args.empty() || args.at(0) != "cache")
    return usage;
  if (args.size() < 2 || args.at(1) == "status")
    return status();

  String sub = args.at(1);
  if (sub == "on") {
    cfg->set("renderDrawableCache", true);
    return "render cache on";
  }
  if (sub == "off") {
    cfg->set("renderDrawableCache", false);
    return "render cache off";
  }
  if (sub == "perpart") {
    bool v = args.size() < 3 || args.at(2) != "off";
    cfg->set("renderDrawableCachePerPart", v);
    return strf("render cache perPart={}", v);
  }
  if (sub == "shadow") {
    bool v = args.size() < 3 || args.at(2) != "off";
    cfg->set("renderDrawableCacheShadowCompare", v);
    return strf("render cache shadow={}", v);
  }
  return usage;
}

// GPU lighting toggle (Slice 1: passthrough plumbing spike). Frontend-only:
// reads/writes the runtime config key WorldPainter consults each frame.
String ClientCommandProcessor::lighting(String const& argumentsString) {
  auto args = m_parser.tokenizeToStringList(argumentsString);
  auto cfg = Root::singleton().configuration();
  String const usage = "usage: /lighting gpu [on|off|status|shadow on|off|iterations <n>|brightness <f>] | promotedynamic [<0..1>|off] | promoteminintensity <f> | tonemap [on|off] | temporal [on|off|floor <ms>] | bilinear [on|off] | upscale <n> | gathercache [on|off] | gridbucket <n> | spreadoracle [on|off]";
  auto status = [&]() {
    // defensive: coerce a stale bool-typed lightingPromoteDynamic instead of throwing toFloat().
    Json pd = cfg->get("lightingPromoteDynamic", 0.0f);
    float pdf = pd.isType(Json::Type::Bool) ? (pd.toBool() ? 0.5f : 0.0f) : pd.optFloat().value(0.0f);
    return strf("lighting gpu: enabled={} shadowCompare={} spreadIterations(cap)={} brightness={} | promoteDynamic={} promoteMinIntensity={} tonemap={}",
      cfg->get("lightingGpu", false).toBool(),
      cfg->get("lightingGpuShadowCompare", false).toBool(),
      // getOrDefault, so the STATUS LINE cannot disagree with what the renderer actually reads. The
      // literal 64 that was here was a fourth undeclared default for a knob that already has one.
      cfg->getOrDefault("lightingGpuSpreadIterations").toUInt(),
      cfg->get("lightingGpuBrightness", 1.0f).toFloat(),
      pdf,
      cfg->get("lightingPromoteMinIntensity", 0.1f).toFloat(),
      cfg->get("lightingTonemap", false).toBool())
      + strf(" | temporal={} floorMs={} | worldSampleBilinear={} upscale={} gatherCache={} gridBucket={}",
        cfg->get("lightingTemporalDecouple", true).toBool(),
        cfg->get("lightingTemporalFloorMs", 33.0f).toFloat(),
        cfg->get("lightingWorldSampleBilinear", false).toBool(),
        cfg->get("lightingWorldUpscale", 1.0f).toFloat(),
        cfg->get("lightingGatherCache", true).toBool(),
        cfg->get("lightingGridSizeBucket", 8).toUInt());
  };

  if (args.empty())
    return usage;

  // CDL flags (independent of the `gpu` sub-commands): promote all Spread lights to
  // PointAsSpread (consumed at dispatch, T4), and tonemap the additive HDR compose
  // instead of hard-clamping (consumed at the GPU compose + CPU mirror, T3).
  if (args.at(0) == "promotedynamic") {
    // fraction in [0,1]: 0 = off (Spread unchanged), ~0.15 = old hybrid, 1.0 = full Point.
    float p = 0.0f;
    if (args.size() >= 2 && args.at(1) != "off") {
      auto f = maybeLexicalCast<float>(args.at(1));
      if (!f)
        return "usage: /lighting promotedynamic <0..1 | off>";
      p = *f < 0.0f ? 0.0f : (*f > 1.0f ? 1.0f : *f);
    }
    cfg->set("lightingPromoteDynamic", p);
    return strf("lighting promoteDynamic={}", p);
  }
  if (args.at(0) == "promoteminintensity") {
    // Spread lights with max colour channel below this are NOT promoted to dynamic points (they stay
    // soft spreads) -- excludes ultra-dim fill lights like item drops (~0.078) that flicker. 0 = off.
    if (args.size() < 2)
      return "usage: /lighting promoteminintensity <f>  (e.g. 0.1; 0 = promote everything)";
    auto f = maybeLexicalCast<float>(args.at(1));
    if (!f || *f < 0.0f)
      return "usage: /lighting promoteminintensity <f>=0";
    cfg->set("lightingPromoteMinIntensity", *f);
    return strf("lighting promoteMinIntensity={}", *f);
  }
  if (args.at(0) == "temporal") {
    // Recompute the lightmap at a floor cadence when calm (only flicker/particles/ambient), 60Hz on
    // real activity. floor <ms> sets the calm cadence (0 = recompute every frame = off).
    if (args.size() >= 2 && args.at(1) == "floor") {
      if (args.size() < 3) return "usage: /lighting temporal floor <ms>  (0 = recompute every frame)";
      auto f = maybeLexicalCast<float>(args.at(2));
      if (!f || *f < 0.0f) return "usage: /lighting temporal floor <ms>=0";
      cfg->set("lightingTemporalFloorMs", *f);
      return strf("lighting temporalFloorMs={}", *f);
    }
    bool v = args.size() < 2 || args.at(1) != "off";
    cfg->set("lightingTemporalDecouple", v);
    return strf("lighting temporalDecouple={}", v);
  }
  if (args.at(0) == "tonemap") {
    bool v = args.size() < 2 || args.at(1) != "off";
    cfg->set("lightingTonemap", v);
    return strf("lighting tonemap={}", v);
  }
  if (args.at(0) == "bilinear") {
    // R-A: sample the lightMap with a single bilinear tap in world.frag instead of the 4-tap bicubic.
    bool v = args.size() < 2 || args.at(1) != "off";
    cfg->set("lightingWorldSampleBilinear", v);
    return strf("lighting worldSampleBilinear={}", v);
  }
  if (args.at(0) == "upscale") {
    // R-A Form 2: bicubic-upscale factor (1 = off/bicubic; 2..4 = bilinear of the N-upscaled map).
    if (args.size() < 2)
      return "usage: /lighting upscale <n>  (1 = off; 2-4 typical)";
    auto f = maybeLexicalCast<float>(args.at(1));
    if (!f || *f < 1.0f)
      return "usage: /lighting upscale <n>=1";
    cfg->set("lightingWorldUpscale", *f);
    return strf("lighting worldUpscale={}", *f);
  }
  if (args.at(0) == "spreadoracle") {
    // J-2 lighting bit-identity oracle (default off). When on, the Jacobi spread runs TWICE per recompute in
    // the SAME frame on the SAME inputs -- once reading each neighbour's obstacle bit from its own sampler
    // (the original 17-taps-per-texel path), once from the alpha the spread now carries in lightState (the
    // 9-tap path) -- and pixel-compares them, logging [spreadoracle] MATCH/DIFF. In-frame by construction, so
    // it is immune to the world divergence that makes cross-run frame hashes useless. Costs a whole extra
    // spread solve: validation only, never left on.
    if (args.size() >= 2)
      cfg->set("lightingSpreadOracle", args.at(1) == "on");
    return strf("lighting spreadOracle={}", cfg->get("lightingSpreadOracle", false).toBool());
  }
  if (args.at(0) == "gathercache") {
    // A1/A2: reuse the per-frame-invariant tile gather across frames (cache hit on a still camera /
    // shift on scroll), re-applying environmentLight via the sky-exposed bit. off = re-gather every
    // frame (the A/B baseline). Visual-only; default on (kill-switch).
    bool v = args.size() < 2 || args.at(1) != "off";
    cfg->set("lightingGatherCache", v);
    return strf("lighting gatherCache={}", v);
  }
  if (args.at(0) == "gridbucket") {
    // Stable lighting grid (#127): light-query size rounded up to this bucket (1 = exact size / off).
    if (args.size() < 2)
      return "usage: /lighting gridbucket <n>=1  (default 8; 1 = exact size)";
    auto n = maybeLexicalCast<unsigned>(args.at(1));
    if (!n || *n < 1)
      return "usage: /lighting gridbucket <n>=1";
    cfg->set("lightingGridSizeBucket", *n);
    return strf("lighting gridSizeBucket={}", *n);
  }

  if (args.at(0) != "gpu")
    return usage;
  if (args.size() < 2 || args.at(1) == "status")
    return status();

  String sub = args.at(1);
  if (sub == "on") {
    cfg->set("lightingGpu", true);
    return "lighting gpu on";
  }
  if (sub == "off") {
    cfg->set("lightingGpu", false);
    return "lighting gpu off";
  }
  if (sub == "shadow") {
    bool v = args.size() < 3 || args.at(2) != "off";
    cfg->set("lightingGpuShadowCompare", v);
    return strf("lighting gpu shadow={}", v);
  }
  if (sub == "iterations" && args.size() >= 3) {
    auto n = maybeLexicalCast<unsigned>(args.at(2));
    if (n) {
      cfg->set("lightingGpuSpreadIterations", *n);
      return strf("lighting gpu spreadIterations={}", *n);
    }
  }
  if (sub == "brightness" && args.size() >= 3) {
    auto v = maybeLexicalCast<float>(args.at(2));
    if (v) {
      cfg->set("lightingGpuBrightness", *v);
      return strf("lighting gpu brightness={}", *v);
    }
  }
  return usage;
}


}
