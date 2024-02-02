////////////////////////////////////////////////////////////////////////////////////////////////////
//                               This file is part of CosmoScout VR                               //
////////////////////////////////////////////////////////////////////////////////////////////////////

// SPDX-FileCopyrightText: German Aerospace Center (DLR) <cosmoscout@dlr.de>
// SPDX-License-Identifier: MIT

#ifndef CSP_USER_STUDY_PLUGIN_HPP
#define CSP_USER_STUDY_PLUGIN_HPP

#include "../../../src/cs-core/PluginBase.hpp"
#include "../../../src/cs-core/Settings.hpp"
#include "../../../src/cs-utils/Property.hpp"
#include <vector>

class VistaOpenGLNode;
class VistaTransformNode;

namespace cs::gui {
class WorldSpaceGuiArea;
class GuiItem;
} // namespace cs::gui

namespace csp::userstudy {

/// This plugin creates configurable navigation scenarios for a user study. It uses web views to
/// mark checkpoints with different tasks. There are different types of checkpoints: Simple
/// checkpoints are just rings which the user needs to fly through. Other checkpoints types display
/// messages or request user input.
/// The plugin is configurable via the application config file. See README.md for details.
class Plugin : public cs::core::PluginBase {
 public:
  struct Settings {

    /// The settings for a scenario
    struct Scenario {

      /// The name of the scenario
      std::string mName;

      /// The path to the scenario config
      std::string mPath;
    };

    /// List of configs containing related scenarios.
    std::vector<Scenario> mOtherScenarios;

    /// The settings for a stage of the scenario
    struct Checkpoint {
      enum class Type { eSimple, eRequestFMS, eRequestCOG, eMessage, eSwitchScenario };

      /// The type of the stage
      Type mType{Type::eSimple};

      /// The related bookmark for the position & orientation
      std::string mBookmarkName;

      /// The scaling factor for the stage mark
      float mScaling;

      /// For now, this is only used for the message of eMessage checkpoints.
      std::optional<std::string> mData;
    };

    /// List of stages making up the scenario
    std::vector<Checkpoint> mCheckpoints;

    /// The checkpoint recording interval in seconds.
    cs::utils::DefaultProperty<uint32_t> pRecordingInterval{5};
  };

  void init() override;
  void deInit() override;
  void update() override;

 private:
  void onLoad();
  void unload();

  // This updates a CheckpointView according the data for the checkpoint at the given index.
  void prepareCheckpoint(std::size_t index);

  // This updates the opacity of all currently visible CheckpointViews. The view which shows the
  // checkpoint at mCurrentCheckpointIdx will be fully visible, all others will be slightly
  // transparent.
  void updateCheckpointVisibility();

  // This teleports the observer to checkpoint location which has been visited last by the user.
  // Usually, this should make the currently active checkpoint visible on screen.
  void teleportToCurrent();

  // This advances mCurrentCheckpointIdx by one and makes the now obsolete CheckpointView show a new
  // checkpoint which is farthest in the future.
  void nextCheckpoint();

  // This reduces mCurrentCheckpointIdx by one and makes the now obsolete CheckpointView show the
  // checkpoint at mCurrentCheckpointIdx.
  void previousCheckpoint();

  // Retrieves the bookmark with the given name from the GuiManager. This may return std::nullopt if
  // no bookmark with the given name exists.
  std::optional<cs::core::Settings::Bookmark> getBookmarkByName(std::string const& name) const;

  // Retrieves a CelestialObject from the SolarSystem which has the same SPICE center and frame name
  // as the given bookmark. This can then be used to compute the observer-relative position of the
  // bookmark. If no CelestialObject with this center and frame name exists, this will return
  // std::nullptr.
  std::shared_ptr<const cs::scene::CelestialObject> getObjectForBookmark(
      cs::core::Settings::Bookmark const& bookmark) const;

  std::shared_ptr<Settings> mPluginSettings = std::make_shared<Settings>();

  // There is a fixed number of checkpoints visible at any given time (currently three). The objects
  // below are used to draw a checkpoint. When the user passes through a checkpoint, its
  // CheckpointView will be re-used for the next checkpoint which becomes visible.
  struct CheckpointView {
    std::unique_ptr<cs::gui::WorldSpaceGuiArea> mGuiArea;
    std::unique_ptr<cs::gui::GuiItem>           mGuiItem;
    std::unique_ptr<VistaOpenGLNode>            mGuiNode;
    std::unique_ptr<VistaTransformNode>         mTransformNode;
  };

  // These will show the next three checkpoints. The current checkpoint will be at index
  // i = mCurrentCheckpointIdx % mCheckpointViews.size().
  std::array<CheckpointView, 3> mCheckpointViews;
  std::size_t                   mCurrentCheckpointIdx = 0;
  cs::utils::Property<uint32_t> mCurrentFMS           = 0;

  // This is set to true during checkpoint recording.
  bool                                  mEnableRecording      = false;
  bool                                  mEnableCOGMeasurement = false;
  std::chrono::steady_clock::time_point mLastRecordTime;

  int mOnLoadConnection = -1;
  int mOnSaveConnection = -1;
};
} // namespace csp::userstudy

#endif // CSP_USER_STUDY_PLUGIN_HPP
