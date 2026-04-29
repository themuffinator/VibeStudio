# Efficiency Philosophy

VibeStudio exists to make idTech1, idTech2, and idTech3 development faster,
clearer, and easier than stitching together many separate tools. Efficiency is
not only performance. It is the total time, attention, and uncertainty removed
from the creative loop.

The product should eventually let a creator move from idea to in-game test with
as few repeated manual steps as practical, while keeping control, attribution,
and reproducibility intact.

## Efficiency Goals

- Reduce setup time by detecting game installations, source ports, packages,
  compilers, palettes, project folders, and output conventions.
- Reduce context switching by keeping package browsing, editing, compiling,
  diagnostics, staging, and launch/testing inside one shared project graph.
- Reduce repeated work through presets, profiles, templates, batch operations,
  command manifests, reusable compiler pipelines, and CLI automation.
- Reduce uncertainty through visible task state, output paths, structured logs,
  validation summaries, dependency graphs, and detail-on-demand diagnostics.
- Reduce waiting pain by keeping heavy work asynchronous, cancelable, cached,
  incremental, and reported through the activity center.
- Reduce expert-only friction by offering clean default workflows while keeping
  raw logs, metadata, manifests, and format internals available.
- Reduce creative blank-page time through optional generative and agentic AI
  workflows that produce reviewable proposals, not hidden mutations.

## Modern Acceleration Techniques

VibeStudio should combine deterministic tooling with modern automation:

- Generative AI for draft content, explanations, shader scaffolds, entity
  snippets, texture/model/audio ideation, documentation, and command recipes.
- Agentic AI for multi-step workflows such as "inspect this compiler failure,
  propose fixes, update a staged script, rerun validation, and summarize what
  changed" under explicit user control.
- Provider connectors for AI services instead of a single-provider design.
- CLI automation for repeatable project validation, package checks, compiler
  runs, batch conversion, and release preparation.
- Data-driven editor profiles and compiler profiles to avoid forcing users to
  relearn familiar workflows.
- Incremental indexing and caching for packages, assets, previews, diagnostics,
  and dependency data.
- Graphical workflow surfaces that show what changed, what is blocked, and what
  is ready to test.

## AI-Free Mode

AI acceleration is part of the core product design, but AI use must remain a
choice. Users who prefer no AI, cannot use cloud services, or work on private
projects must still get a complete local studio.

AI-free mode requirements:

- Core editing, packaging, compiling, validation, launch/testing, and CLI
  workflows work without any AI provider configured.
- No project content is sent to an AI provider unless the user opts in.
- Projects can disable AI even when the application has global AI settings.
- AI-generated files, assets, commands, and text must be marked as generated or
  proposed until accepted.
- Manual equivalents must exist for important AI-assisted workflows.

## Efficiency Metrics

Track these alongside the roadmap:

- Time from first launch to usable project workspace.
- Time from changed map/script/package asset to a compiler run.
- Time from compiler failure to an actionable diagnostic summary.
- Time from generated/proposed AI action to reviewed/staged application.
- Number of duplicate file/path selections needed after project setup.
- Number of clicks or commands for common package, compile, and launch loops.
- Startup time, package open time, indexing time, preview latency, and compiler
  task scheduling overhead.
- Percentage of long-running workflows with visible progress, cancellation, and
  output locations.
- Percentage of GUI workflows with CLI equivalents.
- Percentage of AI workflows with manual/local alternatives.

## Design Rule

Whenever a workflow is added, ask: how does this make development quicker,
easier, more reliable, or more understandable? If the answer is not clear, the
workflow needs a smaller slice, better feedback, a CLI path, a reusable preset,
or a stronger reason to exist.
