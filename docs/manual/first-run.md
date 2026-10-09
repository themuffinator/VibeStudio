# First-run setup

The first-run setup is an eight-step checklist that tailors VibeStudio to you:
language and accessibility, your editor profile, projects, game installations,
compilers, AI and the command line. Every step is optional, and you can change
every choice later.

> [!NOTE]
> **Status: Partial.** The checklist tracks your progress and opens the right
> settings for each step. Some planned parts of the flow are not built yet:
> role presets, guided project creation, a toolchain check inside setup, and
> command-line path setup.

On macOS, press Cmd wherever this page says Ctrl.

## Open the checklist

VibeStudio does not open the setup by itself: it starts on the **Workspace**
page with its defaults. To begin:

1. Open **Settings**: select it at the foot of the left rail, or press
   <kbd>Ctrl</kbd>+<kbd>0</kbd>.
2. Select **Getting Started**, the first category.

The panel shows the setup status (**Not Started**, **In Progress**,
**Skipped For Now** or **Complete**), the current step, a progress bar, and the
eight steps, each marked done, current or pending. Below the steps it lists
reminders such as "No game installation profile has been added yet."

## Work through the steps

1. Select **Start**. The first step, **Welcome and Access**, becomes the current
   step.
2. Select the step's settings button. Its label says where it goes, for
   example **Open Accessibility Settings**.
3. Make your choices. Settings apply immediately and are saved automatically.
4. Go back to **Settings** > **Getting Started** and select **Next**.
5. Repeat for each step. **Next** on the last step marks setup complete, and
   **Finish** does so at any point after you start.

| Step | What you choose | Settings button |
| --- | --- | --- |
| Welcome and Access | Language, region formats, theme, text size, colour vision, focus outline, motion, alerts and speech | **Open Accessibility Settings** |
| Workspace and Editor Profile | The editor profile the Levels page follows, and the 3D renderer | **Choose Editor Profile** |
| Projects and Packages | A project folder and its manifest, or nothing for now | **Open Workspace** |
| Game Installations | Your games, detected from Steam and GOG or added by hand | **Open Workspace** |
| Toolchains | Where your compiler programs are | **Open Build Toolchains** |
| AI and Automation | Whether to stay in AI-free mode | **Open AI Settings** |
| CLI Integration | Nothing to set; the command line is part of the same program | None |
| Review and Finish | Check the reminders, then finish | None |

## Language, appearance and accessibility

Language and appearance are on **Settings** > **Appearance and Language**:

- **Language**: the default, **System language**, follows your operating
  system whenever VibeStudio has that language. Choosing another offers
  **Restart Now**, and the new language appears after the restart. 47 languages
  are listed, but none is fully translated yet, so most of the interface stays
  in English.
- **Region formats**: how numbers, dates and sizes are written, chosen apart
  from the language, with a live sample.
- **Theme**: **System**, **Dark** (the default), **Light**,
  **High Contrast Dark** or **High Contrast Light**.
- **Text scale** from 100% to 200%, **Density**, **Typeface**, and
  **Wider letter and word spacing**.
- **Navigation**: how the left rail uses its width.

**Open Accessibility Settings** opens **Settings** > **Accessibility**:

- **Vision**: **Colour vision** for status colours (standard, red-green safe,
  blue-yellow safe or monochrome), **Reduce colour saturation**,
  **Thick focus outline** and **Thick text cursor**.
- **Motion and Timing**: **Reduce motion**,
  **Stop the text cursor blinking**, and how long status messages stay.
- **Alerts and Screen Readers**: announcements to your screen reader, a
  taskbar flash, and sound cues when long tasks finish.
- **Text to Speech**: **Read status changes aloud** with your computer's own
  voices, chosen events, voice, rate, pitch and volume. **Say Test Phrase**
  checks that it works.

See [Accessibility and languages](accessibility.md) for every option.

## Editor profile

**Choose Editor Profile** opens **Settings** > **Appearance and Language** at
**Editor profile**. Pick the editor whose keys, mouse and camera you already
know: the 24 profiles include TrenchBroom, NetRadiant Custom, GtkRadiant 1.4,
1.5 and 1.6, QeRadiant, Q3Radiant, DoomEdit, BSP, QuArK, Hammer and Ultimate Doom Builder.
The Levels **Controls** menu also offers **Browse Editor Profiles…**, with search
and previews of each profile’s defaults and differences.
**VibeStudio Default** is selected to
start with. **Customize Gestures…** adjusts the chosen profile. See
[Editor profiles](editor-profiles.md) for the supported controls and remaining
differences; choosing a profile does not add its original editor's file formats.

The same page has **3D Rendering**. Its **Renderer** decides what draws the
Levels camera, models, the modeller and material previews:

- **Automatic** (the default) uses Vulkan where it works and OpenGL otherwise.
  On macOS it tries OpenGL first.
- **OpenGL** or **Vulkan** uses only that one.

**Status** shows what each renderer found on your computer and which one is in
use. **Check Renderers** starts both again and draws a test image on each; use
it after updating a graphics driver. See
[3D views stay empty](troubleshooting.md#3d-views-stay-empty) if neither works.

## Projects and game installations

**Open Workspace** opens the **Workspace** page.

- **Open Project** (<kbd>Ctrl</kbd>+<kbd>O</kbd>) chooses a project folder.
  **Initialize Manifest** writes the project's `.vibestudio/project.json`. See
  [Projects and game installations](projects.md).
- **Detect Installs** looks for games in your Steam and GOG libraries. Found
  games appear under **Game Installations**; select one and choose
  **Import Detected** to keep it.
- **Add** creates a profile from a game folder you choose. **Use** makes the
  selected profile the default, and **Remove** forgets a profile without
  touching its files.

Detection only reads, and every new profile is read-only: VibeStudio copies a
test map into a game only after you allow it. See
[Build and launch](build-and-launch.md).

## Compilers

**Open Build Toolchains** opens the **Build** page. Its **Toolchain** tab lists
each compiler tool (VibeMap2 for Quake and Quake II, VibeMap3 for Quake III,
ZDBSP and ZokumBSP for Doom), whether it was found, and where its path comes
from. VibeStudio also looks on your `PATH`. VibeMap2 and VibeMap3 are
VibeStudio's own compilers; stock ericw-tools and q3map2 programs can still be
used, but you choose them with **Locate…**.

1. Select a tool whose status is **Not found**.
2. Choose **Locate…** and pick its program.
3. After you install a tool somewhere else, choose **Rescan**.

The compiler item in the status bar shows how many tools were found and opens
this tab. See [Build and launch](build-and-launch.md).

## AI and automation

**Open AI Settings** opens **Settings** > **AI and Automation**. **AI-free mode**
is on by default: no AI feature runs and nothing is sent anywhere. To use the
Assistant or AI-assisted generation, clear **AI-free mode**, choose a connector
and model, and select **Allow cloud AI connectors** if the model is not on your
computer. See [AI assistant](ai.md).

## Command line and finishing

**CLI Integration** has no settings: the command line is the same program with
`--cli` added, as [Run the command line](install.md#run-the-command-line)
shows. **Review and Finish** is the moment to read the reminders under the
steps, then select **Finish**.

## Skip now, finish later

- **Skip** sets setup to **Skipped For Now**. Nothing else changes.
- **Resume** continues from the step you left.
- **Finish** marks setup complete. After that, **Review** reopens the last
  step.
- **Reset** clears setup progress only. Your settings stay as they are.

## Change your choices later

Everything setup touches is an ordinary setting:

- **Settings** (<kbd>Ctrl</kbd>+<kbd>0</kbd>) has **Getting Started**,
  **Appearance and Language**, **Accessibility**, **AI and Automation** and
  **Extensions**. Type in **Search settings** to find a setting, and press
  <kbd>Enter</kbd> to move to the first match.
- **Tools** > **Preferences** (<kbd>Ctrl</kbd>+<kbd>,</kbd>) opens
  **Appearance and Language**, and **Tools** > **Accessibility Settings** opens
  **Accessibility**.
- The Command Palette (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>P</kbd>) runs
  commands such as **Larger Text**, **Toggle High Contrast** and
  **Detect Game Installations** from any page.

## Set up from the command line

The command line reads and changes the same settings, which helps when you
prepare several computers or a test profile. Setup progress and preferences
use options rather than command families:

```sh
vibestudio --cli --setup-report
vibestudio --cli --setup-step game-installations
vibestudio --cli --set-theme high-contrast-dark
vibestudio --cli --set-text-scale 150
vibestudio --cli editor select trenchbroom
vibestudio --cli render set vulkan
vibestudio --cli install detect --json
vibestudio --cli project init ./mymod
vibestudio --cli compiler set-path vibemap2-bsp --executable /opt/vibemap2/vibemap2-bsp
```

Setup progress also accepts `--setup-start`, `--setup-next`, `--setup-skip`,
`--setup-complete` and `--setup-reset`. The step IDs are `welcome-access`,
`workspace-profile`, `projects-packages`, `game-installations`, `toolchains`,
`ai-automation`, `cli-integration` and `review-finish`. Run
`vibestudio --cli --help` for every `--set-` option, and add
`--settings-file <path>` to change a separate settings file instead of your
own. See [Command line](cli.md).

## Learn more

- [Initial setup flow](../INITIAL_SETUP.md): the full design for setup,
  including the parts not built yet.
- [Accessibility and localisation](../ACCESSIBILITY_LOCALIZATION.md): the
  accessibility and language design in depth.
