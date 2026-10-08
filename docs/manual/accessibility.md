# Accessibility and languages

VibeStudio lets you change contrast, text size, colours, motion, focus
visibility and speech, and use the whole studio from the keyboard. This page
also covers interface languages, right-to-left layouts and region formats.

> [!NOTE]
> **Status: Partial.** Every option on this page is implemented and covered by
> automated tests, but testing with real screen readers, physical keyboards and
> each operating system is still to come. The interface is prepared for 47
> languages, but no translation is finished yet, so menus and messages appear
> in English.

## Open the settings

Choose **Settings** on the rail, or press <kbd>Ctrl</kbd>+<kbd>0</kbd>
(<kbd>Cmd</kbd> on macOS). Two categories hold these options:

- **Appearance and Language**: theme, text scale, density, typeface, spacing,
  language and region formats. <kbd>Ctrl</kbd>+<kbd>,</kbd> opens it directly.
- **Accessibility**: colour vision, focus and cursor, motion, timing, alerts,
  screen readers and text-to-speech. **Tools** > **Accessibility Settings**
  opens it directly.

Every option takes effect at once, except the language, and is saved with your
other preferences. The **Search settings** field on the Settings page
(<kbd>Ctrl</kbd>+<kbd>F</kbd>) finds an option by name, and <kbd>Enter</kbd>
moves to it. The first step of [first-run setup](first-run.md) leads to the
same options.

## Theme and contrast

**Theme** offers **Dark** (the default), **System**, **Light**,
**High Contrast Dark** and **High Contrast Light**. **System** follows your
desktop's light or dark colours and, in builds made with Qt 6.10 or later, its
high-contrast mode. **View** > **Toggle High Contrast** switches between a
high-contrast theme and the standard theme of the same lightness.

Status never relies on colour alone: every state also has its own symbol and
words, such as **AI-free** in the status bar.

## Text size, density and typeface

- **Text scale** goes from 100% to 200% in 25% steps and scales text, icons and
  controls. **View** > **Larger Text**, **Smaller Text** and
  **Reset Text Size** change it in steps.
- **Density**: **Comfortable** gives larger click targets, **Standard** is the
  default and **Compact** fits more on screen.
- **Typeface**: **System typeface**, or any font installed on your computer,
  such as Atkinson Hyperlegible, Lexend or OpenDyslexic. Code keeps its
  fixed-width font; zoom the code editor with
  <kbd>Ctrl</kbd>+<kbd>=</kbd> and <kbd>Ctrl</kbd>+<kbd>-</kbd>.
- **Wider letter and word spacing** adds the extra space between letters and
  words that WCAG 2.2 asks layouts to cope with; many readers with dyslexia
  find it easier to follow.
- **Navigation** chooses how the rail on the left uses its width:
  **Collapse to icons automatically**, **Always show labels** or
  **Icons only**.

## Colour and focus

On **Settings** > **Accessibility**, under **Vision**:

- **Colour vision** recolours status colours so the states you can tell apart
  are the ones that differ: **Standard colours**,
  **Red-green safe (protanopia, deuteranopia)**,
  **Blue-yellow safe (tritanopia)** or **Monochrome (no colour)**. Text and
  contrast stay as they are.
- **Reduce colour saturation** softens accent, selection and status colours
  towards grey without lowering contrast.
- **Thick focus outline** draws the keyboard focus outline three pixels wide
  around every control.
- **Thick text cursor** draws a three-pixel text cursor, growing with the text
  scale.

## Motion and timing

Under **Motion and Timing**:

- **Reduce motion** replaces spinners and animated progress bars with plain
  status text, opens the rail without sliding, switches pages without fading,
  and stops model animation playback.
- **Stop the text cursor blinking** keeps the cursor steady everywhere.
- **Status messages stay** sets how long status bar messages remain:
  **Standard**, **Three times longer** or **Until the next message**.

## Alerts and screen readers

Under **Alerts and Screen Readers**:

- **Announce results to screen readers** (on by default) asks your screen
  reader, such as Narrator, NVDA, JAWS, VoiceOver or Orca, to announce when a
  long task finishes or fails, and what the status bar reports, without moving
  focus.
- **Flash the taskbar when background work finishes** (on by default) flashes
  VibeStudio's taskbar or dock button when a long task ends while another
  window is in front.
- **Play sound cues for task results** (off by default) plays a rising tone
  when a task finishes, a level tone for a warning or cancellation, and a
  falling tone for a failure. Set **Cue volume** and try them with
  **Play Cues**.

Standard controls carry accessible names, and the map and model viewports,
charts and image previews describe their contents in text so a screen reader
can read them. Some custom controls do not report everything yet.

## Read aloud with text-to-speech

VibeStudio speaks with your computer's own voices; nothing goes to a cloud
voice service.

1. Under **Text to Speech**, tick **Read status changes aloud**.
2. Under **Read aloud**, choose what is spoken: **Long tasks that finish**,
   **Failures, warnings, and cancellations** (both on by default) and
   **Status bar messages**.
3. Choose a **Voice**, **Rate**, **Pitch** and **Volume**, then
   **Say Test Phrase** to hear them.

**Tools** > **Read Aloud** (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>U</kbd>)
reads the selected text, the current line of an editor, the focused list row or
control, or else the latest status message, even when
**Read status changes aloud** is off. Press it again, or choose
**Stop Reading Aloud**, to stop. Password and key fields are never read.

| System | Speech engine | Notes |
| --- | --- | --- |
| Windows | Windows Speech API | Rate, pitch and volume all apply. |
| macOS | The system's `say` command | Pitch and volume follow the system voice settings. |
| Linux | Speech Dispatcher (`spd-say`), or eSpeak NG or eSpeak | Install one of them to hear speech. |

If no engine can be found, the **Text to Speech** section says why, and every
spoken message still appears on screen. Only the Windows engine has been
checked with real voices so far. To choose an engine, or to silence speech for
one run, set the `VIBESTUDIO_SPEECH_ENGINE` environment variable to `sapi`,
`say`, `spd-say`, `espeak-ng`, `espeak` or `none`.

## Work from the keyboard

Every command in the menus can be run from the keyboard:

- The command palette (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>P</kbd> or
  <kbd>Ctrl</kbd>+<kbd>K</kbd>) runs any command by name, such as
  **Toggle High Contrast** or **Larger Text**.
- <kbd>Ctrl</kbd>+<kbd>1</kbd> to <kbd>Ctrl</kbd>+<kbd>0</kbd> switch pages,
  <kbd>Ctrl</kbd>+<kbd>F</kbd> jumps to the page's search or filter, and
  <kbd>Alt</kbd>+<kbd>Left</kbd> and <kbd>Alt</kbd>+<kbd>Right</kbd> go back
  and forward.
- The rail shows its labels while the keyboard is in it, and access keys are
  always underlined in menus.
- In the code editor <kbd>Tab</kbd> indents, so <kbd>Ctrl</kbd>+<kbd>Tab</kbd>
  and <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Tab</kbd> move focus out of it.

**Help** > **Keyboard Shortcuts**, or **Keyboard Shortcuts…** on the
Accessibility settings, lists every command with its keys and the page they
work on. Select a command and choose **Change Keys…** to give it keys of your
own, or **Reset** to restore the default. Your keys take priority over the
level editor's profile keys (see [Editor profiles](editor-profiles.md)).

Some controls inside pages are not yet reachable as commands, tab order has not
been checked everywhere, and a guided keyboard-only setup is still planned.
[A tour of the studio](tour.md) lists the main shortcuts.

## Change the interface language

1. Open **Settings** > **Appearance and Language** and find
   **Language and Region**.
2. Choose a **Language**. The default, **System language** followed by your
   system's language name, follows your operating system whenever VibeStudio
   has that language.
3. Choose **Restart Now** in the notice that appears. The new language shows
   after the restart.

> [!IMPORTANT]
> The translation catalogues list every interface string in all 47 languages,
> but none has been translated and reviewed yet, so menus and messages stay in
> English whichever language you choose. Layout direction and region formats
> already follow your choice.

<details>
<summary>The 47 interface languages</summary>

English, Chinese (Simplified), Chinese (Traditional), Hindi, Spanish (Spain),
Spanish (Latin America), Arabic, French, Bengali, Portuguese (Brazil),
Portuguese (Portugal), Russian, Indonesian, Urdu, German, Japanese, Nigerian
Pidgin, Marathi, Vietnamese, Telugu, Hausa, Turkish, Punjabi, Swahili,
Filipino, Tamil, Persian, Korean, Thai, Malay, Italian, Gujarati, Amharic,
Kannada, Polish, Ukrainian, Romanian, Dutch, Greek, Hungarian, Czech, Swedish,
Bulgarian, Hebrew, Danish, Finnish and Norwegian Bokmål.

</details>

A pseudo-localisation catalogue, which accents and lengthens every string, is
also maintained so testers can check that layouts survive longer text; it is
not offered as a language.

## Right-to-left languages

Arabic, Urdu, Persian and Hebrew are right-to-left. After you restart into one
of them, the whole window mirrors, panels included. **Run Build Pipeline** and
**Launch Game** keep the same spacing at their leading edge in either direction.
The mirrored layout has been checked by automated tests, not yet by native readers.

## Region formats

**Region formats**, also under **Language and Region**, sets how numbers,
dates, times and sizes are written, separately from the language:
**System regional settings** (the default), **Match the interface language**,
or a specific region. A sample shows the result as you choose.

## Command-line equivalents

```sh
vibestudio --cli accessibility report
vibestudio --cli accessibility speak --test
vibestudio --cli localization report --locale ar --json
```

| Command | What it does |
| --- | --- |
| `accessibility report` | Prints every accessibility and language preference, the speech engine and its voice count. |
| `accessibility voices` | Lists the speech engine's voices. |
| `accessibility speak` | Speaks text, or the test phrase with `--test`. |
| `localization targets` | Lists the 47 languages, marking right-to-left ones. |
| `localization report` | Shows right-to-left coverage, formatting samples and translation catalogue status. |

Preferences can also be set with options such as `--set-theme`,
`--set-text-scale`, `--set-color-vision`, `--set-reduced-motion`, `--set-tts`,
`--set-locale` and `--set-region`; `--preferences-report` prints them. See
[Command line](cli.md) for the full list.

## Learn more

- [Accessibility and localisation design](../ACCESSIBILITY_LOCALIZATION.md): every option, the language list and test coverage.
- [Initial setup](../INITIAL_SETUP.md): the first-run steps for language and accessibility.
