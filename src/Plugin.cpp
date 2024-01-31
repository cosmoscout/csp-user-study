////////////////////////////////////////////////////////////////////////////////////////////////////
//                               This file is part of CosmoScout VR                               //
////////////////////////////////////////////////////////////////////////////////////////////////////

// SPDX-FileCopyrightText: German Aerospace Center (DLR) <cosmoscout@dlr.de>
// SPDX-License-Identifier: MIT

#include "Plugin.hpp"
#include "../../../src/cs-core/GuiManager.hpp"
#include "../../../src/cs-core/InputManager.hpp"
#include "../../../src/cs-core/SolarSystem.hpp"
#include "../../../src/cs-core/TimeControl.hpp"
#include "../../../src/cs-scene/CelestialAnchor.hpp"
#include "logger.hpp"
#include "resultsLogger.hpp"

#include <VistaKernel/DisplayManager/VistaDisplayManager.h>
#include <VistaKernel/DisplayManager/VistaDisplaySystem.h>
#include <VistaKernel/GraphicsManager/VistaOpenGLNode.h>
#include <VistaKernel/GraphicsManager/VistaSceneGraph.h>
#include <VistaKernel/GraphicsManager/VistaTransformNode.h>
#include <VistaKernel/InteractionManager/VistaUserPlatform.h>
#include <VistaKernel/VistaSystem.h>
#include <VistaKernelOpenSGExt/VistaOpenSGMaterialTools.h>

#include <glm/gtc/type_ptr.hpp>
#include <optional>

////////////////////////////////////////////////////////////////////////////////////////////////////

EXPORT_FN cs::core::PluginBase* create() {
  return new csp::userstudy::Plugin;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EXPORT_FN void destroy(cs::core::PluginBase* pluginBase) {
  delete pluginBase; // NOLINT(cppcoreguidelines-owning-memory)
}

////////////////////////////////////////////////////////////////////////////////////////////////////

namespace csp::userstudy {

////////////////////////////////////////////////////////////////////////////////////////////////////

// clang-format off

// NOLINTNEXTLINE
NLOHMANN_JSON_SERIALIZE_ENUM(Plugin::Settings::Checkpoint::Type, {
  {Plugin::Settings::Checkpoint::Type::eSimple, "simple"},
  {Plugin::Settings::Checkpoint::Type::eRequestFMS, "requestFMS"},
  {Plugin::Settings::Checkpoint::Type::eRequestCOG, "requestCOG"},
  {Plugin::Settings::Checkpoint::Type::eMessage, "message"},
  {Plugin::Settings::Checkpoint::Type::eSwitchScenario, "switchScenario"},
});

// clang-format on

////////////////////////////////////////////////////////////////////////////////////////////////////

void from_json(nlohmann::json const& j, Plugin::Settings::Checkpoint& o) {
  cs::core::Settings::deserialize(j, "type", o.mType);
  cs::core::Settings::deserialize(j, "bookmark", o.mBookmarkName);
  cs::core::Settings::deserialize(j, "scale", o.mScaling);
  cs::core::Settings::deserialize(j, "data", o.mData);
}

void to_json(nlohmann::json& j, Plugin::Settings::Checkpoint const& o) {
  cs::core::Settings::serialize(j, "type", o.mType);
  cs::core::Settings::serialize(j, "bookmark", o.mBookmarkName);
  cs::core::Settings::serialize(j, "scale", o.mScaling);
  cs::core::Settings::serialize(j, "data", o.mData);
}

void from_json(nlohmann::json const& j, Plugin::Settings::Scenario& o) {
  cs::core::Settings::deserialize(j, "name", o.mName);
  cs::core::Settings::deserialize(j, "path", o.mPath);
}

void to_json(nlohmann::json& j, Plugin::Settings::Scenario const& o) {
  cs::core::Settings::serialize(j, "name", o.mName);
  cs::core::Settings::serialize(j, "path", o.mPath);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void from_json(nlohmann::json const& j, Plugin::Settings& o) {
  cs::core::Settings::deserialize(j, "otherScenarios", o.mOtherScenarios);
  cs::core::Settings::deserialize(j, "recordingInterval", o.pRecordingInterval);
  cs::core::Settings::deserialize(j, "checkpoints", o.mCheckpoints);
}

void to_json(nlohmann::json& j, Plugin::Settings const& o) {
  cs::core::Settings::serialize(j, "otherScenarios", o.mOtherScenarios);
  cs::core::Settings::serialize(j, "recordingInterval", o.pRecordingInterval);
  cs::core::Settings::serialize(j, "checkpoints", o.mCheckpoints);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void Plugin::init() {

  logger().info("Loading plugin ...");

  mOnLoadConnection = mAllSettings->onLoad().connect([this]() { onLoad(); });
  mOnSaveConnection = mAllSettings->onSave().connect(
      [this]() { mAllSettings->mPlugins["csp-user-study"] = *mPluginSettings; });

  mGuiManager->addSettingsSectionToSideBarFromHTML(
      "User Study", "people", "../share/resources/gui/user_study_settings.html");
  mGuiManager->executeJavascriptFile("../share/resources/gui/js/csp-user-study.js");

  mGuiManager->getGui()->registerCallback("userStudy.deleteAllCheckpoints",
      "Deletes all checkpoints and all bookmarks.", std::function([this]() {
        mPluginSettings->mCheckpoints.clear();

        bool bookmarksFound = false;

        do {
          bookmarksFound = false;

          for (auto [id, bookmark] : mGuiManager->getBookmarks()) {
            if (bookmark.mName.find("user-study-bookmark-") != std::string::npos) {
              logger().info("Removing bookmark {}.", bookmark.mName);
              mGuiManager->removeBookmark(id);
              bookmarksFound = true;
              break;
            }
          }
        } while (bookmarksFound);

        mCurrentCheckpointIdx = 0;

        updateCheckpointVisibility();
      }));

  mGuiManager->getGui()->registerCallback("userStudy.setRecordingInterval",
      "Sets the checkpoint recording interval in seconds.", std::function([this](double val) {
        mPluginSettings->pRecordingInterval = static_cast<uint32_t>(val);
      }));
  mPluginSettings->pRecordingInterval.connectAndTouch(
      [this](uint32_t val) { mGuiManager->setSliderValue("userStudy.setRecordingInterval", val); });

  // Set the mEnableRecording value based on the corresponding checkbox.
  mGuiManager->getGui()->registerCallback("userStudy.setEnableRecording",
      "Enables or disables frame time recording.", std::function([this](bool enable) {
        mEnableRecording = enable;

        if (enable) {
          mGuiManager->getGui()->executeJavascript(
              "document.querySelector('.user-study-record-button').innerHTML = "
              "'<i class=\"material-icons\">stop</i> Stop Recording';");
          mLastRecordTime = std::chrono::steady_clock::now();
        } else {
          mGuiManager->getGui()->executeJavascript(
              "document.querySelector('.user-study-record-button').innerHTML = "
              "'<i class=\"material-icons\">fiber_manual_record</i> Start New Recording';");

          for (std::size_t i = 0; i < mCheckpointViews.size(); i++) {
            showCheckpoint(i);
          }
          updateCheckpointVisibility();
        }
      }));

  mGuiManager->getGui()->registerCallback(
      "userStudy.gotoFirst", "Teleports to the first checkpoint.", std::function([this]() {
        while (mCurrentCheckpointIdx > 0) {
          previousCheckpoint();
        }
        teleportToCurrent();
      }));
  mGuiManager->getGui()->registerCallback(
      "userStudy.gotoPrevious", "Teleports to the previous checkpoint.", std::function([this]() {
        previousCheckpoint();
        teleportToCurrent();
      }));
  mGuiManager->getGui()->registerCallback(
      "userStudy.gotoNext", "Teleports to the next checkpoint.", std::function([this]() {
        nextCheckpoint();
        teleportToCurrent();
      }));
  mGuiManager->getGui()->registerCallback(
      "userStudy.gotoLast", "Teleports to the last checkpoint.", std::function([this]() {
        if (mPluginSettings->mCheckpoints.size() > 0) {
          while (mCurrentCheckpointIdx < mPluginSettings->mCheckpoints.size() - 1) {
            nextCheckpoint();
          }
        }
        teleportToCurrent();
      }));

  GetVistaSystem()->GetKeyboardSystemControl()->BindAction(VISTA_KEY_BACKSPACE, [this]() {
    if (mInputManager->pSelectedGuiItem.get() &&
        mInputManager->pSelectedGuiItem.get()->getIsKeyboardInputElementFocused()) {
      return;
    }

    resultsLogger().info(
        "{}: RESET", mPluginSettings->mCheckpoints[mCurrentCheckpointIdx].mBookmarkName);

    teleportToCurrent();
  });

  GetVistaSystem()->GetKeyboardSystemControl()->BindAction(VISTA_KEY_HOME, [this]() {
    if (mInputManager->pSelectedGuiItem.get() &&
        mInputManager->pSelectedGuiItem.get()->getIsKeyboardInputElementFocused()) {
      return;
    }

    resultsLogger().info("RESTART");

    while (mCurrentCheckpointIdx > 0) {
      previousCheckpoint();
    }
    mSolarSystem->flyObserverTo(mAllSettings->mObserver.pCenter.get(),
        mAllSettings->mObserver.pFrame.get(), mAllSettings->mObserver.pPosition.get(),
        mAllSettings->mObserver.pRotation.get(), 5.0);
  });

  onLoad();

  logger().info("Loading done.");
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void Plugin::deInit() {
  logger().info("Unloading plugin...");

  // remove checkpoints
  unload();

  mAllSettings->onLoad().disconnect(mOnLoadConnection);
  mAllSettings->onSave().disconnect(mOnSaveConnection);

  mGuiManager->removeSettingsSection("User Study");
  mGuiManager->getGui()->unregisterCallback("userStudy.setRecordingInterval");
  mGuiManager->getGui()->unregisterCallback("userStudy.deleteAllCheckpoints");
  mGuiManager->getGui()->unregisterCallback("userStudy.setEnableRecording");
  mGuiManager->getGui()->unregisterCallback("userStudy.gotoFirst");
  mGuiManager->getGui()->unregisterCallback("userStudy.gotoPrevious");
  mGuiManager->getGui()->unregisterCallback("userStudy.gotoNext");
  mGuiManager->getGui()->unregisterCallback("userStudy.gotoLast");

  GetVistaSystem()->GetKeyboardSystemControl()->UnbindAction(VISTA_KEY_BACKSPACE);
  GetVistaSystem()->GetKeyboardSystemControl()->UnbindAction(VISTA_KEY_HOME);

  logger().info("Unloading done.");
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void Plugin::onLoad() {

  unload();

  mCurrentCheckpointIdx = 0;

  // Read settings from JSON
  from_json(mAllSettings->mPlugins.at("csp-user-study"), *mPluginSettings);

  // Get scenegraph to init checkpoints
  VistaSceneGraph* pSG = GetVistaSystem()->GetGraphicsManager()->GetSceneGraph();

  for (std::size_t i = 0; i < mCheckpointViews.size(); i++) {
    auto& view = mCheckpointViews[i];

    // Create and setup gui area
    view.mGuiArea = std::make_unique<cs::gui::WorldSpaceGuiArea>(720, 720);
    view.mGuiArea->setEnableBackfaceCulling(false);

    // Create transform node
    view.mTransformNode =
        std::unique_ptr<VistaTransformNode>(pSG->NewTransformNode(pSG->GetRoot()));

    // Create gui node
    view.mGuiNode = std::unique_ptr<VistaOpenGLNode>(
        pSG->NewOpenGLNode(view.mTransformNode.get(), view.mGuiArea.get()));

    // Register selectable
    mInputManager->registerSelectable(view.mGuiNode.get());

    // Create gui item & attach it to gui area
    view.mGuiItem = std::make_unique<cs::gui::GuiItem>(
        "file://{csp-user-study-cp}../share/resources/gui/user-study-checkpoint.html");
    view.mGuiArea->addItem(view.mGuiItem.get());
    view.mGuiItem->waitForFinishedLoading();
    view.mGuiItem->setZoomFactor(2);

    VistaOpenSGMaterialTools::SetSortKeyOnSubtree(
        view.mGuiNode.get(), static_cast<int>(cs::utils::DrawOrder::eTransparentItems) + 1);

    // register callbacks
    view.mGuiItem->registerCallback("setFMS", "Callback to get slider value",
        std::function([this](double value) { mCurrentFMS = static_cast<uint32_t>(value); }));
    view.mGuiItem->registerCallback(
        "confirmFMS", "Call this to submit the FMS rating", std::function([this]() {
          resultsLogger().info("{}: FMS: {}",
              mPluginSettings->mCheckpoints[mCurrentCheckpointIdx].mBookmarkName,
              mCurrentFMS.get());
          nextCheckpoint();
        }));
    view.mGuiItem->registerCallback(
        "confirmMSG", "Call this to advance to the next checkpoint", std::function([this]() {
          resultsLogger().info(
              "{}: MSG", mPluginSettings->mCheckpoints[mCurrentCheckpointIdx].mBookmarkName);
          nextCheckpoint();
        }));
    view.mGuiItem->registerCallback(
        "loadScenario", "Call this to load a new scenario", std::function([this](std::string path) {
          resultsLogger().info("Loading Scenario at " + path);
          mGuiManager->getGui()->callJavascript("CosmoScout.callbacks.core.load", path);
        }));
    view.mGuiItem->registerCallback("setEnableCOGMeasurement",
        "Enables or disables center of gravity recording.", std::function([this](bool enable) {
          mEnableCOGMeasurement = enable;

          if (!enable) {
            nextCheckpoint();
          }
        }));

    showCheckpoint(i);
  }

  updateCheckpointVisibility();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void Plugin::unload() {
  VistaSceneGraph* pSG = GetVistaSystem()->GetGraphicsManager()->GetSceneGraph();
  for (auto& view : mCheckpointViews) {

    // unregister callbacks
    if (view.mGuiItem) {
      view.mGuiItem->unregisterCallback("setFMS");
      view.mGuiItem->unregisterCallback("confirmFMS");
      view.mGuiItem->unregisterCallback("confirmMSG");
      view.mGuiItem->unregisterCallback("loadScenario");
      view.mGuiItem->unregisterCallback("setEnableCOGMeasurement");
    }

    // disconnect from scene graph
    if (view.mTransformNode) {
      pSG->GetRoot()->DisconnectChild(view.mTransformNode.get());
    }

    // unregister selectable
    if (view.mGuiNode) {
      mInputManager->unregisterSelectable(view.mGuiNode.get());
    }

    view.mGuiArea       = {};
    view.mGuiNode       = {};
    view.mTransformNode = {};
    view.mGuiItem       = {};
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void Plugin::update() {

  if (mEnableRecording) {
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(now - mLastRecordTime).count() >=
        mPluginSettings->pRecordingInterval.get()) {
      mLastRecordTime = now;

      cs::core::Settings::Bookmark bookmark;
      bookmark.mName =
          "user-study-bookmark-" + std::to_string(mPluginSettings->mCheckpoints.size());
      bookmark.mLocation = {this->mSolarSystem->getObserver().getCenterName(),
          this->mSolarSystem->getObserver().getFrameName(),
          this->mSolarSystem->getObserver().getPosition(),
          this->mSolarSystem->getObserver().getRotation()};

      mGuiManager->addBookmark(bookmark);

      Settings::Checkpoint checkpoint;
      checkpoint.mScaling      = static_cast<float>(this->mSolarSystem->getObserver().getScale());
      checkpoint.mBookmarkName = bookmark.mName;
      mPluginSettings->mCheckpoints.push_back(checkpoint);

      logger().info("Recorded Checkpoint {}.", bookmark.mName);
    }

  } else {

    // Update the transform of the visible GUI areas according to the current reached index of
    // checkpoints
    // + a window of mCheckpointViews.size()
    if (mPluginSettings->mCheckpoints.size() > 0) {
      for (size_t i = 0; i < mCheckpointViews.size(); i++) {
        // slide through the stored checkpoints in the config
        size_t checkpointIdx = (mCurrentCheckpointIdx + i) % mPluginSettings->mCheckpoints.size();
        size_t viewIdx       = (mCurrentCheckpointIdx + i) % mCheckpointViews.size();

        std::optional<cs::core::Settings::Bookmark> b =
            getBookmarkByName(mPluginSettings->mCheckpoints[checkpointIdx].mBookmarkName);

        glm::dvec3 positionOffset(0, 0, 0);
        glm::dquat rotationOffset(1, 0, 0, 0);
        float      scale = mPluginSettings->mCheckpoints[checkpointIdx].mScaling;

        if (b->mLocation.has_value()) {
          if (b->mLocation->mPosition.has_value())
            positionOffset = b->mLocation->mPosition.value();
          if (b->mLocation->mRotation.has_value())
            rotationOffset = b->mLocation->mRotation.value();
        }

        auto object = getObjectForBookmark(*b);

        if (object) {
          auto transform =
              object->getObserverRelativeTransform(positionOffset, rotationOffset, scale);
          mCheckpointViews[viewIdx].mTransformNode->SetTransform(glm::value_ptr(transform), true);
        }
      }
    }

    // check if current checkpoint is normal checkpoint
    if (mCurrentCheckpointIdx < mPluginSettings->mCheckpoints.size() &&
        mPluginSettings->mCheckpoints[mCurrentCheckpointIdx].mType ==
            Plugin::Settings::Checkpoint::Type::eSimple) {
      // Fetch bookmark for position
      std::optional<cs::core::Settings::Bookmark> b =
          getBookmarkByName(mPluginSettings->mCheckpoints[mCurrentCheckpointIdx].mBookmarkName);

      glm::dvec3 positionOffset(0, 0, 0);
      if (b->mLocation.has_value()) {
        if (b->mLocation->mPosition.has_value())
          positionOffset = b->mLocation->mPosition.value();
      }

      auto object = getObjectForBookmark(*b);

      if (object) {

        glm::dvec3 vecToObserver = object->getObserverRelativePosition(positionOffset);

        if (glm::length(vecToObserver) < 1.0) {
          // go to next checkpoint
          logger().info("{}: Passed Checkpoint",
              mPluginSettings->mCheckpoints[mCurrentCheckpointIdx].mBookmarkName);
          nextCheckpoint();
        }
      }
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void Plugin::showCheckpoint(std::size_t index) {
  if (index >= mPluginSettings->mCheckpoints.size()) {
    return;
  }

  auto const& settings = mPluginSettings->mCheckpoints[index];

  // Fetch checkpoint at Index
  auto& view = mCheckpointViews[(index) % mCheckpointViews.size()];

  // Fetch bookmark for position
  std::optional<cs::core::Settings::Bookmark> bookmark = getBookmarkByName(settings.mBookmarkName);
  if (bookmark.has_value()) {
    cs::core::Settings::Bookmark b = bookmark.value();

    // Set webview according to type
    switch (settings.mType) {
    case Settings::Checkpoint::Type::eSimple: {
      view.mGuiItem->callJavascript("reset");
      break;
    }
    case Settings::Checkpoint::Type::eRequestFMS: {
      view.mGuiItem->callJavascript("setFMS");
      break;
    }
    case Settings::Checkpoint::Type::eRequestCOG: {
      view.mGuiItem->callJavascript("setCOG");
      break;
    }
    case Settings::Checkpoint::Type::eMessage: {
      view.mGuiItem->callJavascript("setMSG", settings.mData.value_or(""));
      break;
    }
    case Settings::Checkpoint::Type::eSwitchScenario: {
      std::string html = "";
      for (Plugin::Settings::Scenario& scenario : mPluginSettings->mOtherScenarios) {
        html += "<input class=\"btn\" type=\"button\" value=\"" + scenario.mName +
                "\" onclick=\"window.callNative('loadScenario', '" + scenario.mPath + "')\">\n";
      }
      view.mGuiItem->callJavascript("setCHS", html);
      break;
    }
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void Plugin::teleportToCurrent() {
  size_t index = std::max(0, static_cast<int>(mCurrentCheckpointIdx) - 1);

  if (index >= mPluginSettings->mCheckpoints.size()) {
    return;
  }

  auto const& settings = mPluginSettings->mCheckpoints[index];
  auto        bookmark = getBookmarkByName(settings.mBookmarkName);

  if (bookmark.has_value()) {
    cs::core::Settings::Bookmark b = bookmark.value();

    if (b.mLocation) {
      auto loc = b.mLocation.value();

      if (loc.mRotation.has_value() && loc.mPosition.has_value()) {
        mSolarSystem->flyObserverTo(
            loc.mCenter, loc.mFrame, loc.mPosition.value(), loc.mRotation.value(), 0.0);
      } else if (loc.mPosition.has_value()) {
        mSolarSystem->flyObserverTo(loc.mCenter, loc.mFrame, loc.mPosition.value(), 0.0);
      } else {
        mSolarSystem->flyObserverTo(loc.mCenter, loc.mFrame, 0.0);
      }
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void Plugin::nextCheckpoint() {
  if (mPluginSettings->mCheckpoints.size() == 0) {
    return;
  }

  // Advance the current checkpoint index.
  mCurrentCheckpointIdx =
      std::min(mCurrentCheckpointIdx + 1, mPluginSettings->mCheckpoints.size() - 1);

  // Setup the checkpoint which becomes visible next.
  std::size_t newlyVisibleIdx = mCurrentCheckpointIdx + mCheckpointViews.size() - 1;
  if (newlyVisibleIdx < mPluginSettings->mCheckpoints.size()) {
    showCheckpoint(newlyVisibleIdx);
  }

  updateCheckpointVisibility();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void Plugin::previousCheckpoint() {
  if (mPluginSettings->mCheckpoints.size() == 0) {
    return;
  }

  // Advance the current checkpoint index.
  mCurrentCheckpointIdx = std::max(static_cast<int>(mCurrentCheckpointIdx) - 1, 0);

  // Setup the checkpoint which becomes visible next.
  std::size_t newlyVisibleIdx = mCurrentCheckpointIdx;
  if (newlyVisibleIdx < mPluginSettings->mCheckpoints.size()) {
    showCheckpoint(newlyVisibleIdx);
  }

  updateCheckpointVisibility();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void Plugin::updateCheckpointVisibility() {
  // Set css classes of all visible checkpoints. The item at i==0 is the current checkpoint, and
  // i==mCheckpointViews.size()-1 is the most distant checkpoint.
  for (std::size_t i = 0; i < mCheckpointViews.size(); i++) {
    auto index = (mCurrentCheckpointIdx + i) % mCheckpointViews.size();
    if (mCurrentCheckpointIdx + i < mPluginSettings->mCheckpoints.size()) {
      mCheckpointViews[index].mGuiItem->callJavascript(
          "setBodyClass", "checkpoint" + std::to_string(i));
    } else {
      mCheckpointViews[index].mGuiItem->callJavascript("setBodyClass", "hidden");
    }

    // Make only current checkpoint interactive.
    mCheckpointViews[index].mGuiItem->setIsInteractive(i == 0);

    // Ensure that the checkpoints are drawn back-to-front.
    std::size_t sortKey = static_cast<std::size_t>(cs::utils::DrawOrder::eTransparentItems) +
                          mCheckpointViews.size() - i;
    VistaOpenSGMaterialTools::SetSortKeyOnSubtree(
        mCheckpointViews[index].mGuiNode.get(), static_cast<int>(sortKey));
  }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

std::optional<cs::core::Settings::Bookmark> Plugin::getBookmarkByName(std::string name) {
  cs::core::Settings::Bookmark bookmark;
  for (auto it = mGuiManager->getBookmarks().begin(); it != mGuiManager->getBookmarks().end();
       ++it) {
    if (it->second.mName == name) {
      return it->second;
    }
  }
  logger().error("No bookmark with the name \"" + name + "\" could be found!");
  return std::nullopt;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

std::shared_ptr<const cs::scene::CelestialObject> Plugin::getObjectForBookmark(
    cs::core::Settings::Bookmark const& bookmark) {
  for (auto const& [name, object] : mAllSettings->mObjects) {
    if (object->getCenterName() == bookmark.mLocation->mCenter &&
        object->getFrameName() == bookmark.mLocation->mFrame) {
      return object;
    }
  }

  return nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace csp::userstudy
