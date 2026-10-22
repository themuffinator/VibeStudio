# AI assistant (optional)

VibeStudio can ask a text, image or sound model for help: questions about your
map, code or build, generated levels, textures and sounds, and proposed map
edits. All of it is optional, and nothing is sent anywhere until you turn it
on.

> [!NOTE]
> **Status: Partial.** The Assistant, the generators and the OpenAI, Claude,
> Gemini, ElevenLabs, local and custom connectors are implemented and tested
> offline against each provider's request and answer formats, but they have
> not been proven by users in real projects. The Claude Code, Codex and Custom
> Command connectors are tested against a stand-in program, not the real
> tools. The Local GGUF connector has a test that runs a small model, but only
> in builds with ggml when you name a model, so CI does not run it. Meshy,
> voice features and agentic workflows are Planned.

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

## Choose connectors and models

Everything below is set in **Settings** > **AI and Automation**. Its first
group holds three switches:

- **AI-free mode** turns every AI feature off.
- **Allow cloud AI connectors** lets AI features use connectors that send
  requests off your computer: providers with your API key, Claude Code and
  Codex with your own login, and Custom Command. A project's first request to
  each of them waits for your approval (see
  [Preview and approve requests](#preview-and-approve-requests)).
- **Allow agentic workflows** is Planned and does nothing yet.

**Text Model** sets up the connector that the Assistant, **Generate Level**,
**Edit Map with AI** and `ai ask` use, one connector at a time:

| Field | What it holds |
| --- | --- |
| **Connector** | The connector whose settings are shown below. |
| **Model** | The model's name as the provider or runtime lists it. For **Local GGUF Model (ggml)**, the `.gguf` file, which **Browse** picks. |
| **Endpoint** | The provider's or runtime's address; empty uses the provider's own. For Claude Code, Codex and Custom Command it reads **Program**: the tool's program, which **Browse** picks for Claude Code and Codex, or Custom Command's command line. The GGUF connector has none. |
| **Key** | Where the API key comes from. For Claude Code, Codex and Custom Command it reads **Sign-in**: the tool keeps its own login. |
| **Status** | What text features use now, and what stops them. |

- **Use This Connector** makes the connector shown the one text features ask.
  It becomes **Reasoning**, or **Local** while cloud connectors are off, so a
  cloud **Reasoning** connector stays chosen for when you allow them again.
  The button is hidden while text features already use that connector, and
  greyed, with the reason in its tooltip, when the settings rule it out.
- **Test Connection** asks the connector shown to reply OK and sends nothing
  from your project.
- **Ask Again Before Sending** forgets every approval.

**Preferred Connectors** chooses a connector for each kind of work:

| Row | What uses it |
| --- | --- |
| **Reasoning** | Every text feature. |
| **Image** | **Generate Texture** and `ai image`, with its model under **Image Model**. Only connectors that can draw pictures are listed. |
| **Audio** | **Generate Sound**, with its model under **Sound Model**. Only connectors that make sounds are listed. |
| **Local** | Text features while **Allow cloud AI connectors** is off. Lists **Local/Offline Runtime** and **Local GGUF Model (ggml)**. |
| **Coding (planned)**, **Vision (planned)**, **Voice (planned)**, **3D (planned)**, **Embeddings (planned)** | Nothing yet. |

An entry the settings rule out stays listed, greyed, with the reason after its
name and in its tooltip: **(needs cloud connectors)** for a connector that
leaves your computer while cloud connectors are off, and
**(not in this build)** for the GGUF connector in a build without ggml.

## Use a local model

A local runtime keeps every request on your computer and needs no key.

1. Start your runtime, such as Ollama, LM Studio or llama.cpp's server, and
   make sure it has a model.
2. Open **Settings** > **AI and Automation** and untick **AI-free mode**.
3. Under **Text Model**, pick **Local/Offline Runtime** in **Connector**, and
   type the **Model** name exactly as your runtime lists it (for Ollama, as
   `ollama list` shows it).
4. Leave **Endpoint** empty for Ollama's `http://localhost:11434/v1`, or enter
   your runtime's own address.
5. Choose **Use This Connector**.
6. Choose **Test Connection**. It asks the model to reply OK and sends nothing
   from your project.

For pictures, set **Preferred Connectors** > **Image** to
**Local/Offline Runtime**. Under **Image Model**, its **Endpoint** defaults to a
Stable Diffusion web UI, such as AUTOMATIC1111 or Forge, at
`http://127.0.0.1:7860`.

## Use your Claude or ChatGPT login

If you have a Claude or ChatGPT plan, VibeStudio can ask its models through the
tools you already sign in to, with no API key: **Claude Code** for Claude (for
example Opus 5.5), and **Codex** for OpenAI's models. Every text feature works
this way: the Assistant, both level generators, **Edit Map with AI**, and
**Test Connection**.

1. Install the tool and sign in once in a terminal: `claude auth login` for
   Claude Code, or `codex login` for Codex.
2. Open **Settings** > **AI and Automation**, untick **AI-free mode** and tick
   **Allow cloud AI connectors**. Your login still sends requests to Anthropic
   or OpenAI, so they count as cloud requests.
3. Under **Text Model**, pick **Claude Code** or **Codex** in **Connector**.
   **Model** is `claude-opus-5-5` for Claude Code unless you name another (or
   an alias such as `sonnet`); Codex uses its own default unless you name one.
   **Program** shows where the tool was found; choose **Browse** to use
   another copy.
4. Choose **Use This Connector**.
5. Choose **Test Connection**. It first checks that the tool is signed in and
   says how, then asks it to reply OK. If the tool is signed in with an API
   key, it warns you that requests are billed to that key, not to your plan.

VibeStudio runs the tool in a new, empty folder, so it can only answer:

- **Claude Code** runs in print mode with `--safe-mode`: no built-in tools,
  MCP servers, hooks, plugins, `CLAUDE.md` files, memory or saved session.
- **Codex** runs `codex exec` with `--ignore-user-config`, so your
  `config.toml`, with its MCP servers, hooks and plugins, is not loaded; your
  login still is. Its shell and other tools are switched off, it works in a
  read-only sandbox, and it runs with `--ephemeral` and `--ignore-rules`.
- Environment variables that would send requests elsewhere, or bill them to
  an API key, are not passed on. Claude Code gets no `ANTHROPIC_API_KEY`,
  `ANTHROPIC_AUTH_TOKEN`, `ANTHROPIC_BASE_URL`, `CLAUDE_CODE_USE_BEDROCK`,
  `CLAUDE_CODE_USE_VERTEX` or `CLAUDE_CODE_USE_FOUNDRY`; Codex gets no
  `CODEX_API_KEY`, `OPENAI_API_KEY` or `OPENAI_BASE_URL`. To use an API key,
  use the **Claude** or **OpenAI** connector; a **Custom Command** can run a
  tool any other way.
- Cancelling a request, or its timing out, ends the tool and everything it
  started, then removes the run folder.

VibeStudio never reads the tools' credentials. The first request from a
project shows exactly what will run, command and all, in the usual
**Send to …?** dialog. If a tool is too old for an option VibeStudio passes,
the request fails with "… is older than VibeStudio needs" and the command
that updates it.

**Custom Command** runs any other program that answers a prompt, such as
another vendor's command-line tool. VibeStudio cannot tell where the program
sends your prompt, so it counts as a cloud connector even when it runs on your
computer: it needs **Allow cloud AI connectors**, and the first request from a
project asks you first. Enter its command line as the **Program**, with
`{model}`, `{prompt_file}`, `{system_file}`, `{schema_file}` and
`{answer_file}` where the program expects them. **Model** fills `{model}`;
with no model set, `{model}` and the option before it, such as `-m {model}`,
are left out, so the program uses its own default. Without `{prompt_file}`,
the prompt goes to the program's standard input; without `{answer_file}`, its
standard output is the answer. For example, `gemini -m {model}` runs Gemini
CLI with the prompt on standard input. On Windows, name an `.exe`: tools that
npm installed as `.cmd` files work, other batch files do not.

These connectors answer text only. For pictures, set
**Preferred Connectors** > **Image** and its model under **Image Model**: OpenAI,
Gemini, a local runtime or a custom endpoint. For sounds, set **Audio** and its
model under **Sound Model**: ElevenLabs or a custom endpoint. Each generator
also has its own path with no AI.

## Run a GGUF model inside VibeStudio

> [!NOTE]
> **Status: Partial.** Only builds you make yourself with `-Dggml=enabled` can
> run GGUF models; the released packages are built without it.
> **Local GGUF Model (ggml)** still appears in every build's connector lists.
> In a build without ggml, **Preferred Connectors** greys it with
> **(not in this build)**, and its **Key** line and **Test Connection** say how
> to get the runtime.

A build made with `-Dggml=enabled` (see
[Building from source](building-from-source.md#build-with-ggml)) runs a GGUF
language model on the CPU, with no server and nothing leaving your computer.
In other builds, run llama.cpp's own server and use **Local/Offline Runtime**
(see [Use a local model](#use-a-local-model)).

1. Download a GGUF chat model. Small models are quick on a CPU but plan less
   well.
2. Untick **AI-free mode**. Under **Text Model**, pick
   **Local GGUF Model (ggml)** in **Connector**, then choose **Browse** next to
   **Model** and pick the `.gguf` file. There is no **Endpoint**.
3. Choose **Use This Connector**. With cloud connectors off it becomes
   **Local**, and a cloud **Reasoning** connector stays chosen for when you
   allow them again.
4. Choose **Test Connection**.

The model stays loaded between requests, and its file's SHA-256 is worked out
once. Answers are deterministic: the same question gives the same answer. The
context window is the model's own, up to 32,768 tokens, and the same for every
request. An answer stops at the output limit the feature asks for, at most
32,768 tokens, and says so only when it was actually cut. A request waiting
for another can be cancelled at once, and so can loading a model. When a
feature asks for a structured answer, such as a level plan, the model can only
write text that fits it. Text in your messages that spells out the model's own
control tokens is removed before the model reads it. Quake-MapGen plans with
its own grammar and records the model file's SHA-256 in its report.

## Use a cloud provider

1. Put your provider's API key in an environment variable for your user
   account (see the table below), then restart VibeStudio so it can read it.
2. Open **Settings** > **AI and Automation**, untick **AI-free mode** and tick
   **Allow cloud AI connectors**.
3. Under **Text Model**, pick the provider in **Connector** and type the
   **Model** name your provider uses. Leave **Endpoint** empty to use the
   provider's own address. The **Key** line says whether the variable was found.
4. Choose **Use This Connector**. For generated pictures or sounds, set
   **Preferred Connectors** > **Image** or **Audio** too.
5. Choose **Test Connection**.

The ID is what the command line takes, as in `--provider claude-code` or
`--set-ai-reasoning local-gguf`.

| Connector | ID | What it does | Key variable | Status |
| --- | --- | --- | --- | --- |
| **OpenAI** | `openai` | Text questions and pictures | `OPENAI_API_KEY` | Available |
| **Claude** | `claude` | Text questions | `ANTHROPIC_API_KEY` | Available |
| **Gemini** | `gemini` | Text questions and pictures | `GEMINI_API_KEY` | Available |
| **ElevenLabs** | `elevenlabs` | Sound effects | `ELEVENLABS_API_KEY` | Available |
| **Custom HTTP Connector** | `custom-http` | Any OpenAI-compatible text or picture endpoint, such as a team proxy, and sound endpoints that take ElevenLabs' request | None | Available |
| **Local/Offline Runtime** | `local-offline` | Text and pictures from programs on your computer | None | Available |
| **Claude Code** | `claude-code` | Text questions, through your Claude plan | None: the tool's own login | Partial |
| **Codex** | `codex` | Text questions, through your ChatGPT plan | None: the tool's own login | Partial |
| **Custom Command** | `custom-command` | Text questions from any program you name; needs cloud connectors allowed | None | Partial |
| **Local GGUF Model (ggml)** | `local-gguf` | Text questions from a `.gguf` file, only in builds made with `-Dggml=enabled`, not in the released packages | None | Partial |
| **Meshy** | `meshy` | Listed for 3D and pictures; sends nothing yet | `MESHY_API_KEY` | Planned |

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
readable by default, or the raw request body. For Claude Code, Codex and
Custom Command it shows the command that would run, with the files and input
it would get; for the GGUF connector, what the model would be given.

The first time a project sends to an endpoint off your computer, VibeStudio
shows the request in a **Send to …?** dialog, where **Cancel** is the default
and **Send** sends it. The dialog says where the request goes: to the
provider, with your API key used only to sign in; to Anthropic or OpenAI
through Claude Code or Codex, which sign in with your own login in an empty
folder with their tools turned off; or to your custom command, which sends it
wherever it is set up to. The box below the request,
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
| **Generate Level…** | **Generate** on **Levels** | Rules on your computer plan the rooms, with either generator | The text model plans the rooms, and the plan is checked and repaired |
| **Edit Map with AI…** | **Edit with AI** on **Levels** | **Load Proposal…** replays a saved proposal | The text model proposes edits to the open Quake-family map |
| **Generate Texture…** | **Generate** on **Textures** | Starts from a picture of your own | The image model draws variants |
| **Generate Sound…** | **Generate** on **Audio** | The built-in synthesizer makes the sound | The sound model makes it |

Nothing is written or applied until you choose it:

- **Generate Level** shows a layout preview, the plan and its notes.
  **Open in Levels** opens the result as a new, unsaved map; **Save As…** and
  **Save Plan…** write files. With **Generator: Quake-MapGen**, **Details**
  adds **Analysis** (navigation, visibility, lighting and travel findings) and
  **Report**, and **Save Report…** replaces **Save Plan…**.
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

When you choose a generator's AI path (always, in **Edit Map with AI**), two
buttons appear:

- **Preview Request…** shows what would go to the model and sends nothing:
  for a provider, the request without your API key; for Claude Code, Codex
  and Custom Command, the command that would run, with the files and input it
  would get; for the GGUF connector, what the model on your computer would be
  given. If the connection is blocked, it says why.
- **AI Settings…** opens **Settings** > **AI and Automation**. Open generator
  windows follow any change to AI settings at once. When the text model is
  ready, **Generate Level** names it in its choice:
  **The text model:** followed by the connector and model.

Like the Assistant, each generator asks before its first request to an
endpoint off your computer. Saved textures and sounds record what made them in
the project's `.vibestudio/generated` folder.

## When a request cannot be sent

The Assistant, the generators, **Status** and **Test Connection** say what is
missing:

| Message | What to do |
| --- | --- |
| AI-free mode is on. | Untick **AI-free mode** in **Settings** > **AI and Automation**. |
| No connector is chosen. | Pick a **Reasoning** or **Local** connector, or choose **Use This Connector**. |
| … has no endpoint. | Enter the **Endpoint**; a custom connector has no default. |
| … sends to …, off this machine, and cloud connectors are not allowed. | Tick **Allow cloud AI connectors**, or use a local runtime. Claude Code, Codex and Custom Command always need it. |
| No model is set for … | Type the **Model** name. |
| … needs an API key in the … environment variable, which is not set. | Set the variable, then restart VibeStudio. |
| … was not found (looked in: …). | Install Claude Code or Codex, or choose **Browse** next to **Program** and pick the program. |
| … is not signed in. | Run `claude auth login` or `codex login` in a terminal. |
| … is older than VibeStudio needs … | Update the tool: `claude update` for Claude Code, `npm install -g @openai/codex@latest` for Codex. |
| No command is set for Custom Command. | Enter the command line as the **Program**. |
| … is a batch file VibeStudio cannot run safely. | Point **Program** at the `.exe`, or at `node` and the tool's script. |
| This build of VibeStudio has no ggml runtime. | Use a build made with `-Dggml=enabled`, or run llama.cpp's server and use **Local/Offline Runtime**. |
| No GGUF model file is set. | Choose **Browse** next to **Model** and pick a `.gguf` file. The same goes for "… does not exist" and "… is not a .gguf model file". |
| This project turns AI off in its manifest. | Run `project init` with `--project-ai-free off`, as shown under [AI-free mode](#ai-free-mode). |

## Privacy at a glance

- AI is off until you untick **AI-free mode**, and a project can keep it off.
- Cloud providers, Claude Code, Codex and Custom Command also need
  **Allow cloud AI connectors**.
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
`ai status` prints `Text:`, `Images:` and `Sounds:` lines that say what each
feature would use now, and a `Connectors` list that gives each one's
`Status`: Available or Planned. `ai test-connection` checks that Claude Code
or Codex is signed in before it asks, as **Test Connection** does.

`--provider` takes a connector ID from the table under
[Use a cloud provider](#use-a-cloud-provider). Text commands also take
`claude-code`, `codex`, `custom-command` and `local-gguf`; picture and sound
commands do not. For `claude-code`, `codex` and `custom-command`,
`--endpoint` is the program or command line; for `local-gguf`, `--model` is
the `.gguf` file:

```sh
vibestudio --cli ai test-connection --provider claude-code --json
vibestudio --cli ai ask --provider claude-code --model sonnet --prompt "why does qbsp report a leak?" --context-file build/qbsp.log --yes
vibestudio --cli map generate --generator quake-mapgen --prompt "Quake 2 deathmatch, 8 rooms" --planner ai --output maps/qmg.map --report maps/qmg.json --yes
```

<details>
<summary>All AI and generator commands</summary>

| Task | Command |
| --- | --- |
| Show AI settings, keys found and connectors | `ai status`, `ai connectors` |
| Check the text model answers | `ai test-connection` |
| Ask a question, with files as context | `ai ask` |
| Draw or edit pictures | `ai image` |
| Generate or plan a level | `map generate`, `map plan`; `--generator quake-mapgen` for Quake-MapGen, `--ranker ggml` in ggml builds |
| Propose and apply map edits | `map ai-edit` |
| Generate a texture, or its companion maps | `texture generate`, `texture derive` |
| Generate a sound effect | `asset audio-generate` |

</details>

Settings have matching options, such as `--set-ai-free off`,
`--set-ai-cloud on`, `--set-ai-reasoning claude-code`,
`--set-ai-model claude-code=sonnet`, `--set-ai-model local-gguf=<path to .gguf>`
and `--set-ai-endpoint codex=<path to codex>`. `--set-ai-local` takes only the
connectors that run on your computer, `local-offline` and `local-gguf`. After
saving, the command prints the same `Text:`, `Images:` and `Sounds:` lines as
`ai status`. The other `ai` commands, such as
`ai explain-log`, are early rule-based helpers that run on your computer and
send nothing. See [Command line](cli.md).

## Learn more

- [AI automation](../AI_AUTOMATION.md): connectors, generators and their design rules.
- [First-run setup](first-run.md): the AI step of the setup checklist.
