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

Initial connector targets:

| Provider | Planned role |
| --- | --- |
| [OpenAI](https://platform.openai.com/docs/quickstart) | General reasoning, coding help, tool-calling workflows, project summarization, compiler-log explanation, multimodal assistance. |
| [Claude](https://platform.claude.com/docs/en/home) | Long-context reasoning, code and document review, agentic planning, script and shader assistance. |
| [Gemini](https://ai.google.dev/api) | Multimodal reasoning, large-context project understanding, alternative model routing. |
| [ElevenLabs](https://elevenlabs.io/docs/overview/intro) | Voice, narration, speech-to-text, sound effects, audio ideation, and audio pipeline experiments. |
| [Meshy](https://docs.meshy.ai/en) | Prompt/image-to-3D experiments, AI texturing, placeholder model generation, and concept-to-asset workflows. |
| Local/offline models | Privacy-preserving assistance where users provide compatible local runtimes or command-line tools. |
| Custom HTTP/MCP-style connectors | Future extension path for studios, source ports, asset services, and community AI tools. |

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
- [ ] Explain compiler errors and suggest likely fixes.
- [ ] Generate q3map2, ericw-tools, ZDBSP, or ZokumBSP command presets from natural language.
- [ ] Draft project manifests from an existing folder.
- [ ] Suggest missing asset dependencies from package/project scans.
- [ ] Generate batch conversion recipes.
- [ ] Draft shader-script scaffolds from a prompt.
- [ ] Create entity definition snippets or documentation from selected assets.
- [ ] Summarize package contents and potential release issues.
- [ ] Generate CLI commands for a requested workflow.
- [ ] Generate a supervised "fix and retry" plan for compiler failures.
- [ ] Draft placeholder textures, sprites, sounds, voices, or models through
  connector-specific providers when configured.
- [ ] Convert user intent into a batch operation plan with cost, time, affected
  files, and rollback/staging notes.
- [ ] Compare provider outputs for the same prompt where multiple connectors are configured.

## Safety And UX
- [ ] Require explicit opt-in and user-provided API configuration.
- [ ] Provide global AI-free mode and project-level AI disablement.
- [ ] Let users choose preferred providers per capability.
- [ ] Show provider, model, estimated cost/usage where available, context sent,
  and generated artifacts for each AI task.
- [ ] Provide a clear AI activity log.
- [ ] Show source context sent to the provider before first use of a project.
- [ ] Redact API keys and known secrets from logs.
- [ ] Prefer staged changes and diffs over direct writes.
- [ ] Let users copy prompts, responses, generated commands, and manifests.
- [ ] Allow cancellation and retry for long-running agentic workflows.
- [ ] Mark generated assets and text until accepted by the user.
- [ ] Keep cloud-dependent features visually distinct from local deterministic actions.

## Architecture Direction
- [ ] Add an AI connector abstraction with provider capability flags.
- [ ] Implement OpenAI as the first general-purpose connector.
- [ ] Add connector stubs/design notes for Claude, Gemini, ElevenLabs, Meshy, local/offline models, and custom HTTP/MCP-style connectors.
- [ ] Use model-agnostic configuration rather than hard-coded model assumptions.
- [ ] Add per-capability provider routing: reasoning, code, vision, image,
  audio, voice, 3D, embeddings, and local/offline execution.
- [ ] Add provider health, quota/cost, authentication, and rate-limit status where available.
- [ ] Expose safe VibeStudio tools for project scanning, package metadata, compiler runs, text edits, and staged writes.
- [ ] Capture AI request/response metadata in task logs without storing secrets.
- [ ] Support CLI access for AI workflows where safe and useful.
- [ ] Store AI workflow manifests for reproducibility: provider, model, prompt
  template, selected context, tool calls, generated artifacts, approvals, and validation results.

## MVP Boundary
AI implementation may remain experimental for the first MVP release, but the
architecture should already assume a connector layer, AI-free mode, reviewable
agentic workflows, and provider-neutral task records. A good first public
experiment is an opt-in assistant that explains compiler logs and proposes a
reproducible next command.
