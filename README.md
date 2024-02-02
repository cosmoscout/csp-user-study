<!-- 
SPDX-FileCopyrightText: German Aerospace Center (DLR) <cosmoscout@dlr.de>
SPDX-License-Identifier: CC-BY-4.0
 -->

# User Studies Plugin for CosmoScout VR

This CosmoScout VR plugin draws a series of checkpoints at predefined locations in space.
The user can navigate from checkpoint to checkpoint and fulfil various tasks at each checkpoint.
There are checkpoints for measuring the body sway of the user or checkpoints which ask the user for a cyber-sickness rating on the fast-motion sickness scale.
The plugin has been used for the following paper:

> S. Schneegans, M. Zeumer, J. Gilg, and A. Gerndt, "CosmoScout VR: A Modular 3D Solar System Based on SPICE", 2022 IEEE Aerospace Conference (AERO), 2022. https://doi.org/10.1109/AERO53065.2022.9843488

## Installation

To add this plugin to CosmoScout VR, clone the repository recursively to the `plugins` directory:

```bash
cd cosmoscout-vr/plugins
git clone https://github.com/cosmoscout/csp-user-study.git
```

CosmoScout VR will pick this up automatically, a simple `./make.sh` or `.\make.bat` as usual will build Cosmocout VR together with the plugin.

## Configuration

This plugin can be enabled with the following configuration in your `settings.json`.
You can leave the `checkpoints` and `otherScenarios` arrays empty at first, they can be filled using the recording functionality of the plugin.

```js
{
  ...
  "plugins": {
    ...
    "csp-user-study": {
      "checkpoints": [             // List of checkpoints in each scenario
        {
          "type": <string>,        // Type of a stage (enum) see below for stage types
          "bookmark": <string>,    // Name of the bookmark used for stage position
          "scale": <float>,        // Scaling factor for the size of the web view
          "data": <string>         // For now, this is only used for the message of eMessage checkpoints
        },
        ...
      ],
      "otherScenarios": [          // List of other scenario configs related to the current scenario
        {
          "name": <string>,        // Name of the scenario
          "path": <string>         // Path to the config (e.g. "../share/scenes/scenario_name.json")
        },
        ...
      ]
     }
  }
}
```

### Stage Types

| Type             | Description |
|:-----------------|:------------|
| `simple`         | Draws a simple circular checkpoint which disappears when the user has moves through. |
| `requestFMS`     | Draws a checkpoint which requests a rating on the fast-motion sickness scale. |
| `requestCOG`     | Draws a checkpoint which attempts to measure the users body sway. |
| `message`        | Draws a checkpoint displaying the message provided in the `data` field. |
| `switchScenario` | Draws a checkpoint displaying the list of `otherScenarios` allowing the user to switch to a different scenario. |

## Scenario Recording

The plugin allows automatic placement of checkpoints along a given path.
For this, you should open the plugin's tab in the advanced settings in the sidebar of CosmoScout VR.
Just click the **Start New Recording** button.
The plugin will now create checkpoints every few seconds.
So you should navigate slowly along the path to record.
Once ready, you can stop the recording again.
If you now click the **Save Scenario** button, the current scene will be saved to to a JSON file in CosmoScout's `bin` directory.
You can edit this file and change the type of the recorded checkpoints in the configuration section of `csp-user-study`.