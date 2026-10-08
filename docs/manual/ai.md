# AI assistant (optional)

VibeStudio can ask a text, image or sound model for help: questions about your
map, code or build, generated levels, textures and sounds, and proposed map
edits. All of it is optional, and nothing is sent anywhere until you turn it
on.

> [!NOTE]
> **Status: Partial.** The Assistant, the generators and the OpenAI, Claude,
> Gemini, local and custom connectors are implemented and tested offline
> against each provider's request and answer formats, but they have not been
> proven by users in real projects. Meshy and voice features are design stubs,
> and agentic workflows are future work.

## AI-free mode

AI-free mode is on when you install VibeStudio. While it is on, no request goes
to any model, local or cloud, and the status bar shows **AI-free**. Editing,
building, packaging, validation, launching and the command line never need
AI, and every generator has a local path that works without it.

A project can also switch AI off for itself, whatever the studio setting:

```sh
vibestudio --cli project init ./mymod --project-ai-free on
```

A project can only make AI stricter; it never turns AI on. Run the same command
with `off` to remove the restriction.

## Use a local model

A local runtime keeps every request on your computer and needs no key.

1. Start your runtime, such as Ollama, LM Studio or llama.cpp's server, and
   make sure it has a model.
2. Open **Settings** > **AI and Automation** and untick **AI-free mode**.
3. Under **Preferred Connectors**, set **Reasoning** to
   **Local/Offline Runtime**.
4. Under **Assistant Model**, pick **Local/Offline Runtime** in **Connector**
   to show its settings, and type the **Model** name exactly as your runtime
   lists it (for Ollama, as `ollama list` shows it).
5. Leave **Endpoint** empty for Ollama's `http://localhost:11434/v1`, or enter
   your runtime's own address.
6. Choose **Test Connection**. It asks the model to reply OK and sends nothing
   from your project.

For pictures, set **Preferred Connectors** > **Image** to
**Local/Offline Runtime**. Under **Image Model**, its **Endpoint** defaults to a
Stable Diffusion web UI, such as AUTOMATIC1111 or Forge, at
`http://127.0.0.1:7860`.

## Use a cloud provider

1. Put your provider's API key in an environment variable for your user
   account (see the table below), then restart VibeStudio so it can read it.
2. Open **Settings** > **AI and Automation**, untick **AI-free mode** and tick
   **Allow cloud AI connectors**.
3. Under **Preferred Connectors**, set **Reasoning** to the provider. Set
   **Image** or **Audio** too if you want generated pictures or sounds.
4. Under **Assistant Model**, pick the same provider in **Connector** and type
   the **Model** name your provider uses. Leave **Endpoint** empty to use the
   provider's own address. The **Key** line says whether the variable was found.
5. Choose **Test Connection**.

| Connector | What it does | Key variable |
| --- | --- | --- |
| **OpenAI** | Text questions and pictures | `OPENAI_API_KEY` |
| **Claude** | Text questions | `ANTHROPIC_API_KEY` |
| **Gemini** | Text questions and pictures | `GEMINI_API_KEY` |
| **ElevenLabs** | Sound effects | `ELEVENLABS_API_KEY` |
| **Custom HTTP Connector** | Any OpenAI-compatible text endpoint, such as a team proxy, and sound endpoints that take ElevenLabs' request | None |
| **Local/Offline Runtime** | Text and pictures from programs on your computer | None |

**Meshy** appears in some connector lists, but it is not usable yet.
VibeStudio reads keys only from these environment variables. It never asks you
to type a key into the studio and never stores one in its settings, projects or
logs. Do not paste keys into a question, a map, a script or a context file.

## Ask the Assistant

Open the **Assistant** panel from **View** > **Assistant Panel** or the
command palette (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>P</kbd>; press
<kbd>Cmd</kbd> instead of <kbd>Ctrl</kbd> on macOS). These open it with a
question already proposed:

- **Explain** on the **Build** page, or **Explain with Assistant** on a build
  problem, asks about the last build's problems.
- **Ask Assistant About Selection**, or **Ask Assistant About This File** when
  nothing is selected, asks about the code you right-click in the **Code**
  editor.

Then:

1. Under **Send with the question**, tick what the model should see.
2. Type your question and choose **Send** (<kbd>Ctrl</kbd>+<kbd>Enter</kbd>).
3. Read the answer, with its token counts and the time it took. **Copy** copies
   it, and **New** starts a fresh conversation.

While the model works, the panel counts the seconds and the **Activity Center**
shows the request; **Cancel** or <kbd>Esc</kbd> stops it. Links in answers are
shown and can be copied, but never open by themselves. In AI-free mode, the
panel opens and explains which setting to change.

## Choose what is sent

Only your question and the items you tick are sent, along with VibeStudio's
own instructions to the model. The page you are on decides what starts ticked:

| Item | What it contains | Ticked at first on |
| --- | --- | --- |
| Selected code, or the open file | The selection, or the file's text | **Code** |
| The map | The open map's summary and health report | **Levels** |
| Selected map objects | The properties of the selection | **Levels** |
| Build log | The last build's problems and the last lines of each tool's output | **Build** |
| The project | The project manifest | Never |

Point at an item to preview its first lines. Before anything leaves, paths
inside your project become `<project>`, paths in your home folder become `~`,
and text shaped like a common provider key becomes `***`. Long items are cut to
their first lines, and the request says so. The key check looks for known
patterns only, so review what you tick.

## Preview and approve requests

**Preview** shows the exact request, without your key, and sends nothing:
readable by default, or the raw request body.

The first time a project sends to an endpoint off your computer, VibeStudio
shows the request in a **Send to …?** dialog, where **Cancel** is the default
and **Send** sends it. The box below the request,
**Don't ask again for this project and** the host's name, is ticked; untick it
to be asked every time. **Ask Again Before Sending** in **Settings** >
**AI and Automation** forgets every approval. Endpoints on your own computer
(`localhost`, loopback addresses and `.localhost` names) never ask, because
nothing leaves it.

## Generate levels, textures and sounds

Each generator has a local way to work with no AI, and an AI way that uses the
connectors above:

| Command | Opens from | Without AI | With AI |
| --- | --- | --- | --- |
| **Generate Level…** | **Generate** on **Levels** | Rules on your computer plan the rooms | The text model plans the rooms, and the plan is checked and repaired |
| **Edit Map with AI…** | **Edit with AI** on **Levels** | **Load Proposal…** replays a saved proposal | The text model proposes edits to the open Quake-family map |
| **Generate Texture…** | **Generate** on **Textures** | Starts from a picture of your own | The image model draws variants |
| **Generate Sound…** | **Generate** on **Audio** | The built-in synthesizer makes the sound | The sound model makes it |

Nothing is written or applied until you choose it:

- **Generate Level** shows a layout preview, the plan and its notes.
  **Open in Levels** opens the result as a new, unsaved map; **Save As…** and
  **Save Plan…** write files.
- **Edit with AI** asks you **What should change?**, then **Ask for Edits**
  lists each proposed edit as **Ready** or **Blocked**, with the reason.
  Untick any you do not want, then choose **Apply Checked**: each edit is an
  ordinary step you can undo. **Save Proposal…** keeps the list. Doom maps are
  not supported.
- **Generate Texture** shows its variants tiled 2 × 2 so seams show.
  **Save to Project** writes the chosen one where the game reads it,
  **Save and Apply to Map Selection** also puts it on the selected faces, and
  **Open in Texture Editor** continues by hand.
- **Generate Sound** lets you play each variant. **Save to Project**,
  **Save and Place in Map** (a `target_speaker` in a Quake II or Quake III map)
  and **Open in Audio Editor** take it further.

Every generator has **Preview Request…** and asks before its first request to an
endpoint off your computer, like the Assistant. Saved textures and sounds
record what made them in the project's `.vibestudio/generated` folder.

## When a request cannot be sent

The Assistant and **Test Connection** say what is missing:

| Message | What to do |
| --- | --- |
| AI-free mode is on. | Untick **AI-free mode** in **Settings** > **AI and Automation**. |
| No connector is chosen. | Pick a **Reasoning** or **Local** connector. |
| … has no endpoint. | Enter the **Endpoint**; a custom connector has no default. |
| … sends to …, off this machine, and cloud connectors are not allowed. | Tick **Allow cloud AI connectors**, or use a local runtime. |
| No model is set for … | Type the **Model** name. |
| … needs an API key in the … environment variable, which is not set. | Set the variable, then restart VibeStudio. |
| This project turns AI off in its manifest. | Run `project init` with `--project-ai-free off`, as shown under [AI-free mode](#ai-free-mode). |

## Privacy at a glance

- AI is off until you untick **AI-free mode**, and a project can keep it off.
- Cloud providers also need **Allow cloud AI connectors**.
- Only your question and the items you tick are sent, with project and home
  folder paths shortened and key-shaped text removed.
- **Preview** and **Preview Request…** show the exact request without sending
  it.
- The first request from each project to each outside host needs your
  approval, and **Ask Again Before Sending** resets every approval.
- Keys come only from environment variables and are never stored or shown.
- Generated work is reviewed before anything is written or applied.

## Command-line equivalents

```sh
vibestudio --cli ai status --json
vibestudio --cli ai test-connection --json
vibestudio --cli ai ask --prompt "why does qbsp report a leak?" --context-file build/qbsp.log --dry-run
```

`--dry-run` prints the request without your key and sends nothing. A command
that would send to an endpoint off your computer stops until you add `--yes`.

<details>
<summary>All AI and generator commands</summary>

| Task | Command |
| --- | --- |
| Show AI settings, keys found and connectors | `ai status`, `ai connectors` |
| Check the text model answers | `ai test-connection` |
| Ask a question, with files as context | `ai ask` |
| Draw or edit pictures | `ai image` |
| Generate or plan a level | `map generate`, `map plan` |
| Propose and apply map edits | `map ai-edit` |
| Generate a texture, or its companion maps | `texture generate`, `texture derive` |
| Generate a sound effect | `asset audio-generate` |

</details>

Settings have matching options, such as `--set-ai-free off`,
`--set-ai-cloud on`, `--set-ai-reasoning <connector>` and
`--set-ai-model <connector>=<model>`. The other `ai` commands, such as
`ai explain-log`, are early rule-based helpers that run on your computer and
send nothing. See [Command line](cli.md).

## Learn more

- [AI automation](../AI_AUTOMATION.md): connectors, generators and their design rules.
- [First-run setup](first-run.md): the AI step of the setup checklist.
