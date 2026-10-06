# AI Automation And Connectors

VibeStudio should fully embrace the AI era through optional, transparent,
provider-agnostic, prompt-based, generative, and agentic workflows. The goal is
to help creators move faster while keeping project ownership, review,
attribution, privacy, and reproducibility intact.

AI is part of the core product design, but it must not become a hard dependency.
Users who prefer an AI-free workflow should still get a complete local studio.

Primary provider references:
- [OpenAI API quickstart](https://platform.openai.com/docs/quickstart)
- [OpenAI Responses API reference](https://platform.openai.com/docs/api-reference/responses)
- [OpenAI tools guide](https://developers.openai.com/api/docs/guides/tools)
- [Claude API documentation](https://platform.claude.com/docs/en/home)
- [Gemini API reference](https://ai.google.dev/api)
- [ElevenLabs documentation](https://elevenlabs.io/docs/overview/intro)
- [Meshy API documentation](https://docs.meshy.ai/en)

## Philosophy
- AI assistance is optional and must never be required for core workflows.
- AI acceleration should be treated as a first-class workflow layer, not a side
  panel bolted on after the fact.
- Generative AI should help create drafts, assets, explanations, commands,
  scripts, shaders, documentation, and ideas quickly.
- Agentic AI should be able to coordinate multi-step workflows through explicit
  VibeStudio tools while remaining observable, cancelable, and reviewable.
- AI-free mode is a first-class mode. Manual and deterministic workflows must
  remain complete, documented, and efficient.
- Provider choice belongs to the user. VibeStudio should support a connector
  model rather than assuming one vendor, one model, or one capability shape.
- Prompted actions should produce plans, diffs, command manifests, or staged changes before applying them.
- AI should call explicit VibeStudio tools rather than editing hidden state directly.
- Local paths, secrets, private project metadata, and proprietary game data need consent-aware handling.
- Outputs must be auditable and reproducible where practical.

## Connector Model

VibeStudio should expose a provider-neutral connector layer with capability
flags instead of one-off integrations. Connectors may be built in, bundled,
community-provided, or project-local where licensing and security allow.

The MVP connector and workflow scaffold is active in `src/core/ai_connectors.*`
and `src/core/ai_workflows.*`. It registers provider-neutral capability
descriptors, connector descriptors, configurable model descriptors,
credential-source redaction, safe AI-callable tools, workflow manifests, and
opt-in settings used by the Qt preferences surface, inspector, CLI, settings
storage, and smoke tests. Text questions reach a provider through
`src/core/ai_transport.*`, which speaks three wire formats: OpenAI-compatible
Chat Completions (OpenAI itself, and local runtimes and proxies such as Ollama,
LM Studio, llama.cpp's server, and vLLM), Anthropic's Messages API, and Gemini's
generateContent. Building each request and reading each answer are plain
functions, tested against every provider's shapes; the client sends one request
at a time over Qt Network, with a total timeout, cancellation, and every
failure named (credential, rate limit, refusal, network, timeout, provider
error). Requests can ask for one JSON object matching a JSON Schema instead of
prose: OpenAI-compatible endpoints get `response_format` with a strict
`json_schema`, Claude gets `output_config.format`, and Gemini gets
`responseMimeType` with its OpenAPI-style `responseSchema`; bounds the
providers do not accept are checked locally after the answer
(`aiJsonSchemaProblems`), and the JSON is found in fenced or chatty answers too
(`extractAiJsonObject`). Pictures go through `src/core/ai_image_transport.*`:
OpenAI's Images API (generations, and edits as multipart forms), Gemini's
generateContent with image output, and the Stable Diffusion web UI API of
AUTOMATIC1111, Forge and SD.Next (txt2img and img2img, with native tiling).
The image client asks again where a provider draws one picture per call,
fetches pictures answered as links without the key, and shares the text
client's timeout, cancellation, and named failures. The proposal experiments
below remain deterministic, reviewable, and do not write files.
Milestone 8 adds Advanced Studio proposal helpers for shader scaffolds, entity
definition snippets, package validation plans, batch conversion recipes, and a
proposal review surface that exposes summary, context, staged outputs, tool
calls, prompts, and response previews.

Initial connector targets:

| Provider | Planned role |
| --- | --- |
| [OpenAI](https://platform.openai.com/docs/quickstart) | Text questions through [Chat Completions](https://platform.openai.com/docs/api-reference/chat) with `OPENAI_API_KEY`, including strict structured output; textures and concept art through the [Images API](https://platform.openai.com/docs/api-reference/images), `gpt-image-1.5` suggested and editable. |
| [Claude](https://platform.claude.com/docs/en/home) | Text questions through the [Messages API](https://platform.claude.com/docs/en/api/messages) with `ANTHROPIC_API_KEY`; `claude-opus-5-5` is suggested and editable. On Anthropic's own endpoint, models that support it opt into the server-side refusal fallback. |
| [Gemini](https://ai.google.dev/api) | Text questions through [generateContent](https://ai.google.dev/api/generate-content) with `GEMINI_API_KEY`, including JSON with a response schema; pictures through the same call asked for [image output](https://ai.google.dev/gemini-api/docs/image-generation), with the image model named in Settings. |
| [ElevenLabs](https://elevenlabs.io/docs/overview/intro) | Sound effects through the [Sound Effects API](https://elevenlabs.io/docs/api-reference/text-to-sound-effects/convert) with `ELEVENLABS_API_KEY`, `eleven_text_to_sound_v2` by default; voice, narration and speech-to-text remain planned. |
| [Meshy](https://docs.meshy.ai/en) | Prompt/image-to-3D experiments, AI texturing, placeholder model generation, and concept-to-asset workflows. |
| Local/offline models | Text questions to an OpenAI-compatible server on this machine: Ollama at `http://localhost:11434/v1` by default, or LM Studio and llama.cpp's server at their own addresses. Pictures from a [Stable Diffusion web UI](https://github.com/AUTOMATIC1111/stable-diffusion-webui/wiki/API) (AUTOMATIC1111 or Forge) at `http://127.0.0.1:7860`, or an OpenAI-compatible image server at an address ending in `/v1`. No key and no cloud opt-in needed. |
| Custom HTTP/MCP-style connectors | Text questions to any OpenAI-compatible endpoint you set, such as a team proxy, and sound effects from an endpoint that takes ElevenLabs' sound-generation request; MCP-style tools are a future extension path. |

Connector capabilities should be declared explicitly:
- Text generation and reasoning.
- Tool calling or structured action proposals.
- Vision or multimodal input.
- Embeddings/search.
- Image generation or editing.
- Audio generation, transcription, voice, or sound effects.
- 3D asset generation, texturing, remesh, rigging, or animation.
- Local/offline execution.
- Streaming output.
- Cost/usage reporting where providers expose it.
- Data retention and privacy notes.

## Assistant

The Assistant panel (View > Assistant Panel, the AI status chip, **Explain** on
the Build page, or **Ask Assistant About Selection** in the code editor) asks
the text model a question about the open work:

- **Who answers.** The Reasoning connector from Settings > AI and Automation,
  else the Local one. With cloud connectors off, a local connector wins. Each
  text connector's model, endpoint, and key source are set under Assistant
  Model, where **Test Connection** asks the model to reply OK.
- **What goes.** Only what is ticked under *Send with the question*: the
  selected code or the open file, the map summary, the selected map objects,
  the last build's problems and the end of each tool's output, or the project
  manifest. The page in front of the user decides what starts ticked. Paths in
  the project and home folders become `<project>` and `~`, and anything shaped
  like a provider key becomes `***`. Long items are cut to their first lines,
  and the request says so.
- **Before it goes.** **Preview** shows the exact request without the key,
  and sends nothing: readable by default (the settings one per line, then
  each message's text as it is written), or the raw JSON body with one tick. The first question from a project to an endpoint off this
  machine shows that request in a dialog and asks, and the answer is kept per
  project and destination until Settings' *Ask Again Before Sending* forgets
  it. A local endpoint (localhost, loopback, or `.localhost`) never leaves the
  machine, so it does not ask.
- **While it runs.** The panel counts the seconds, Cancel or Escape stops it,
  and the Activity panel shows it as a running, cancellable task that ends
  answered, failed, or cancelled.
- **The answer.** Markdown in the conversation, with the token counts and the
  time taken. Links are shown and copied, never opened on their own. A
  question that went unanswered stays in view, marked, and never goes to the
  model again, so the conversation's turns always alternate.

AI-free mode, the default, keeps the Assistant closed: it opens, says why it
cannot send, and names the setting. `vibestudio --cli ai ask` and
`ai test-connection` follow the same rules (see docs/CLI_STRATEGY.md).

## Generating Levels

**Generate Level** (the Levels header, the command palette, or
`vibestudio --cli map generate`) turns a description into a sealed, playable
level for Quake, Quake II, Quake III, or Doom. It follows the pipeline of
[Quake-MapGen](https://github.com/themuffinator/Quake-MapGen) (credited in
docs/CREDITS.md): plan without coordinates, lay out on an integer grid, build
from the layout, validate, then write.

1. **Spec.** The description is read for the game, mode, theme, room count,
   verticality, liquid, monsters, players, title and seed ("Quake 2
   deathmatch, 8 rooms, slime, seed 42"); the dialog's choices and the CLI's
   options set them outright. Without a seed, the words make one, so the same
   description gives the same level.
2. **Plan.** A plan names rooms (role, size, height, floor level 0-3, shape,
   liquid, lighting), the links between them, and what each holds (starts,
   weapons, ammo, health, armor, powerups, monsters, the exit), in the game's
   own item names. The rules planner makes one on this machine: a main route
   with loops back, an arena, a weapon progression matched to tougher
   monsters further in, deathmatch starts spread over every room. Or the text
   model makes one: it is sent the plan's JSON Schema (structured output
   where the provider has it), its answer is checked against the schema and
   the game's items, and one correction round shows it the problems before the
   rules take over. Every plan, the model's included, is repaired the same
   way: unique ids, known values, real rooms, one connected level, a start, an
   exit far from it, enough deathmatch starts; each repair is reported.
3. **Layout.** Rooms are placed breadth first beside the room they link to,
   keeping the level compact and square, five cells apart so later links fit;
   each link is a straight corridor whose stairs climb 16 units a step in its
   middle. Loops route around everything already placed (A*, turns cost more
   so stairs find straight runs), or open as a doorway between neighbours on
   one floor. Rooms get pillars, liquid pits with walkways, or stepped daises.
4. **Build.** Every open cell has a floor slab and a ceiling slab, and every
   closed cell beside one is a full column, so no cell can leak; neighbouring
   cells with the same slabs merge into one brush. Quake III's hidden faces
   take `common/caulk`. Lights, starts, items, monsters and the exit go where
   there is room, away from each other and from doorways. Doom levels trace
   the same cells into sectors, linedefs and sidedefs, with W1 exit lines
   around an exit pad, in a PWAD holding `MAP01`.
5. **Check.** The start must reach every room on foot (or swimming), and the
   map is read back with the editor's own parser; parser errors fail the
   generation rather than write a broken map.

The result is reviewed before it goes anywhere: a top-down layout preview
(north up, floors lighter where higher, a text legend that does not rely on
colour), the plan, the notes and repairs, and the map text. **Open in Levels**
opens it as a new, unsaved map; Save As and Save Plan write files. Textures
are the games' stock names unless the project's own fit (matched by role from
the open package and map) or are named outright.

## Editing Maps with AI

**Edit with AI** (the Levels header, the command palette, or
`vibestudio --cli map ai-edit`) changes the open Quake, Quake II, or Quake
III map from an instruction ("add a light above each player start",
"retexture the selected brushes with a metal texture"). The text model
never writes map text. It is sent a summary of the map (each entity by its
`entity:N` selector with its class, origin and keys, each brush with its
owner, bounds and textures, the textures in use, and the selection, with
home and project folders shortened) and answers in a fixed set of actions
against a JSON Schema: add a point entity, set or remove a key, add a box
brush, retexture brushes, move objects, delete objects.

Every action is checked against the map before it is shown: its targets
exist and are the right kind (keys belong to entities, a point entity has
no faces), numbers are finite and in range, a box has volume, names and
values hold no quotes or line breaks, and the worldspawn is never deleted.
The review lists each action in words as **Ready** or **Blocked** with why;
ready ones start checked and the reviewer unchecks any (Space on the
row). **Apply Checked** runs them through the editor's own operations, each
its own undo step, deletes last, and checks them against the map again
first if it changed while the model answered; a proposal applies once, and
each row then says Applied, Failed, Blocked, or Left out. **Save Proposal**
keeps the actions and choices as JSON; **Load Proposal** (or `map ai-edit
--proposal`) reviews and applies one with no model at all, so a proposal can
come from a script, another tool, or a person. Doom maps are refused: their
sectors and things have their own tools.

## Generating Textures

**Generate Texture** (the Textures header, the command palette, or
`vibestudio --cli texture generate`) makes game-ready textures. The image
model draws variants from the description (the prompt adds the surface, the
game's look, and the rules a usable texture needs: straight on, evenly lit,
edge to edge, tileable, no text), or a picture of your own is used with no AI.
Then, deterministically and the same for both, each variant is cropped to the
texture's shape, blended so its edges tile (two separable cross-fades, the seam
measured before and after), resampled with wrap-around area filtering,
converted through the texture exporter to the game's format and palette with
its fullbright and size rules, and optionally given companion maps derived
from it: `_norm` (normal with height in alpha), `_gloss` and `_glow` for
DarkPlaces, FTE and QuakeSpasm-family ports, or `_n`, `_s` and an additive
glow stage for ioquake3's OpenGL2 renderer. The idea of deriving these layers
from one texture follows [TexAI](https://github.com/themuffinator/TexAI)
(credited in docs/CREDITS.md). Variants are compared tiled 2x2, so a seam
shows as a cross. **Save to Project** writes the chosen one where its game
reads it: a lump in a WAD2 (Quake), a `.wal` (Quake II), a `.tga` and, for
glows and liquids, a shader in `scripts/vibestudio_generated.shader` (Quake
III), a flat or patch in a PWAD (Doom, Heretic, Hexen), or a PNG. A record of
the spec, provider, model, prompts and digest goes to
`.vibestudio/generated/textures/<name>.json`. **Save and Apply to Map
Selection** also puts it on the selected faces, and **Open in Texture Editor**
continues by hand. `texture derive` makes the companion maps for any picture.

The generators and Edit with AI show their requests before sending
(**Preview Request**), ask before the first request from a project to an
endpoint off this machine, run as Activity tasks, and can be cancelled while a
provider is working.

## Generating Sounds

**Generate Sound** (the Audio header, the command palette, or
`vibestudio --cli asset audio-generate`) makes sound effects for Doom, Quake,
Quake II, or Quake III from a description. The built-in synthesizer makes them
on this machine with no AI: fifteen kinds of sound (gunshot, energy weapon,
explosion, pickup, power-up, jump, pain, death, door or lift, switch,
teleport, footstep, impact, alarm, ambience), read from the description or
chosen, shaped by its words ("heavy", "metal", "distant"), and the same for
the same description and seed. Its kinds and its voice model (a waveform, a
pitch slide, vibrato, an envelope with punch, filters) follow DrPetter's
[sfxr](https://drpetter.se/project_sfxr.html) (credited in
docs/CREDITS.md). Or the sound model makes them: ElevenLabs' sound effects,
or a custom endpoint taking the same request, sent the description with the
game's sound, the kind, and whether it loops.

Either way each variant is made mono, trimmed of silence, faded, normalized
to -1 dBFS in the game's own format, and, for ambience and alarms or when
asked, joined end to start into a seamless loop that Quake-family games are
told to repeat. Variants are listed with their waveforms and played before
one is kept. **Save to Project** writes it where the game reads it: a DMX
lump in `wads/vibestudio_sounds.wad` for Doom, an 11 kHz 8-bit WAV under
`sound/` for Quake, a 22 kHz 16-bit WAV for Quake II and III, with a record
of what made it in `.vibestudio/generated/sounds/<name>.json`. **Save and
Place in Map** also adds a `target_speaker` to an open Quake II or III map,
looping or named for a trigger, and **Open in Audio Editor** continues by
hand. Sound requests follow the same preview, consent, Activity and
cancellation rules as the other generators.

## Agentic Workflow Model

Agentic workflows should be built around supervised loops:

1. Gather context through explicit project/package/compiler/search tools.
2. Produce a plan with expected files, commands, assets, and risks.
3. Ask for approval before writes, external requests with sensitive context, or
   compiler/package mutations.
4. Apply changes only through staged VibeStudio services.
5. Run validation or compiler tasks through the shared task runner.
6. Summarize what changed, what was generated, what failed, and what remains.

The activity center should show AI work as normal work: queued, thinking,
calling tools, waiting for review, applying staged changes, running validation,
completed, failed, or cancelled.

## Initial Experiments
- [x] Explain compiler errors and suggest likely fixes.
- [x] Generate q3map2, ericw-tools, ZDBSP, or ZokumBSP command presets from natural language.
- [x] Draft project manifests from an existing folder.
- [x] Suggest missing asset dependencies from package/project scans.
- [x] Generate batch conversion recipes.
- [x] Draft shader-script scaffolds from a prompt.
- [x] Create entity definition snippets or documentation from selected assets.
- [x] Summarize package contents and potential release issues.
- [x] Generate CLI commands for a requested workflow.
- [x] Generate a supervised "fix and retry" plan for compiler failures.
- [x] Draft placeholder textures, sprites, sounds, voices, or models through
  connector-specific providers when configured.
- [ ] Convert user intent into a batch operation plan with cost, time, affected
  files, and rollback/staging notes.
- [x] Generate a sealed, playable level from a description, planned by rules or
  by the text model as schema-checked JSON (Quake, Quake II, Quake III, Doom).
- [x] Generate seamless, palette-correct game textures with the image model,
  or from a picture, with source-port companion maps.
- [x] Edit the open map from an instruction: the text model proposes
  schema-checked actions, each validated against the map, reviewed, and
  applied as an undoable editor edit.
- [x] Generate game-ready sound effects with the synthesizer (no AI) or the
  sound model, delivered in each game's format and placed in the map.
- [x] Compare provider outputs for the same prompt where multiple connectors are configured.

## Safety And UX
- [x] Require explicit opt-in before cloud or agentic AI settings are enabled.
- [x] Provide global AI-free mode.
- [x] Provide project-level AI disablement: a project whose manifest sets
  AI-free mode (`project init <folder> --project-ai-free on`) stops every text,
  image, and sound request while it is open, in the GUI and the CLI, and says
  why. A project can only turn AI off: one that asks for AI does not get it
  while the studio is AI-free, and the override is never saved into the
  studio's own settings.
- [x] Let users choose preferred providers per capability in the settings model.
- [x] Show provider, model, estimated cost/usage where available, context sent,
  and generated artifacts for each AI task.
- [x] Provide a clear AI activity log.
- [x] Show source context sent to the provider before first use of a project.
- [x] Redact API keys and known secrets from logs.
- [x] Prefer staged changes and diffs over direct writes.
- [x] Let users copy prompts, responses, generated commands, and manifests.
- [x] Allow cancellation and retry for long-running agentic workflows.
- [x] Mark generated assets and text until accepted by the user.
- [x] Keep cloud-dependent features visually distinct from local deterministic actions.

## Architecture Direction
- [x] Add an AI connector abstraction with provider capability flags.
- [x] Implement OpenAI as the first general-purpose connector.
- [x] Add connector stubs/design notes for Claude, Gemini, ElevenLabs, Meshy, local/offline models, and custom HTTP/MCP-style connectors.
- [x] Use model-agnostic configuration rather than hard-coded model assumptions.
- [x] Add per-capability provider routing: reasoning, code, vision, image,
  audio, voice, 3D, embeddings, and local/offline execution.
- [x] Add provider health, quota/cost, authentication, and rate-limit status where available.
- [x] Expose safe VibeStudio tools for project scanning, package metadata, compiler runs, text edits, and staged writes.
- [x] Capture AI request/response metadata in task logs without storing secrets.
- [x] Support CLI access for AI status, connector/model/tool inspection, and first experiments.
- [x] Send text questions through a provider-neutral transport (OpenAI-compatible, Anthropic Messages, Gemini generateContent) that the GUI and the CLI share, testable without a network.
- [x] Ask for structured JSON output in each provider's own form, and check answers against the schema locally.
- [x] Generate and edit pictures through a provider-neutral image transport (OpenAI Images, Gemini image output, the Stable Diffusion web UI), with the same AI-free, cloud opt-in, consent, and preview rules as text.
- [x] Generate sound effects through a provider-neutral sound transport (ElevenLabs' Sound Effects API, or a custom endpoint taking the same request), under the same rules.
- [x] Store AI workflow manifests for reproducibility: provider, model, prompt
  template, selected context, tool calls, generated artifacts, approvals, and validation results.

## MVP Boundary
AI implementation may remain experimental for the first MVP release, but the
architecture should already assume a connector layer, AI-free mode, reviewable
agentic workflows, and provider-neutral task records. A good first public
experiment is an opt-in assistant that explains compiler logs and proposes a
reproducible next command.
