#include "cli/cli.h"
#include "cli/audio_session.h"
#include "cli/audio_take.h"
#include "cli/audio_recording.h"
#include "cli/workspace.h"
#include "cli/package_copy_limits.h"
#include "cli/package_copy_sessions.h"
#include "core/level_placement.h"
#include "cli/level_bookmarks.h"
#include "cli/level_gestures.h"
#include "cli/level_build_workspace.h"
#include "cli/level_build_artifacts.h"
#include "cli/level_build_deployment.h"
#include "cli/level_scene.h"
#include "cli/level_material_paint.h"
#include "cli/level_surface_clipboard.h"
#include "cli/ai_generation.h"
#include "core/ai_audio_transport.h"
#include "core/ai_image_transport.h"
#include "cli/model_mdl.h"
#include "cli/model_assembly.h"
#include "cli/model_collision.h"
#include "cli/model_transform_options.h"
#include "cli/model_skin_bindings.h"
#include "cli/model_surfaces.h"
#include "cli/model_material_slots.h"
#include "cli/model_appearance.h"
#include "cli/model_intersections.h"
#include "cli/model_import_repair.h"
#include "core/model_material_slots.h"

#include "app/studio_runtime.h"
#include "app/ui_primitives.h"
#include "core/advanced_studio.h"
#include "core/code_files.h"
#include "core/language_server.h"
#include "core/language_references.h"
#include "core/ai_connectors.h"
#include "core/ai_transport.h"
#include "core/ai_workflows.h"
#include "core/asset_tools.h"
#include "core/texture_document.h"
#include "core/texture_project.h"
#include "core/texture_export.h"
#include "core/texture_recovery.h"
#include "core/audio_clip.h"
#include "core/audio_export.h"
#include "core/audio_delivery.h"
#include "core/audio_level.h"
#include "core/audio_analysis.h"
#include "core/audio_resample.h"
#include "core/audio_project.h"
#include "core/audio_recovery_store.h"
#include "core/bsp_inspect.h"
#include "core/build_pipeline.h"
#include "core/compiler_profiles.h"
#include "core/compiler_registry.h"
#include "core/compiler_runner.h"
#include "core/editor_profiles.h"
#include "core/entity_definitions.h"
#include "core/idtech_image.h"
#include "core/level_map.h"
#include "core/level_doom_nodes.h"
#include "core/level_udmf.h"
#include "core/level_navigation.h"
#include "core/level_patch.h"
#include "core/level_patch_stitch.h"
#include "core/level_patch_cap.h"
#include "core/level_brush.h"
#include "core/level_document.h"
#include "core/level_dependencies.h"
#include "core/level_materials.h"
#include "core/level_surface.h"
#include "core/level_primitive.h"
#include "core/level_prefab.h"
#include "core/level_merge.h"
#include "core/localization.h"
#include "core/map_assets.h"
#include "core/map_render.h"
#include "core/model_mesh.h"
#include "core/model_design.h"
#include "core/model_document.h"
#include "core/model_animation.h"
#include "core/model_tags.h"
#include "core/model_topology_health.h"
#include "core/model_file_io.h"
#include "core/model_frame_export.h"
#include "core/model_recovery.h"
#include "core/operation_state.h"
#include "core/deflate.h"
#include "core/package_archive.h"
#include "core/package_compare.h"
#include "core/package_preview.h"
#include "core/package_selection.h"
#include "core/package_wad_groups.h"
#include "core/package_staging.h"
#include "core/package_draft.h"
#include "core/package_draft_storage.h"
#include "core/package_recovery.h"
#include "core/package_import_store.h"
#include "core/package_validation.h"
#include "core/package_publication.h"
#include "core/project_manifest.h"
#include "core/studio_semantics.h"
#include "core/studio_manifest.h"
#include "core/studio_settings.h"
#include "core/text_document.h"
#include "core/text_recovery.h"
#include "vibestudio_config.h"

#include <QCoreApplication>
#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QSysInfo>
#include <QStringDecoder>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <streambuf>
#include <string>

namespace vibestudio::cli {

namespace {

enum class CliExitCode {
	Success = 0,
	Failure = 1,
	Usage = 2,
	NotFound = 3,
	ValidationFailed = 4,
	Unavailable = 5,
};

enum class CliOutputFormat {
	Text,
	Json,
};

struct NullOutputBuffer final : std::streambuf {
	int overflow(int character) override { return character; }
};

QStringList& currentCliArgs()
{
	static QStringList args;
	return args;
}

bool quietOutput()
{
	return currentCliArgs().contains(QStringLiteral("--quiet")) && !currentCliArgs().contains(QStringLiteral("--json"));
}

bool verboseOutput()
{
	return currentCliArgs().contains(QStringLiteral("--verbose")) && !quietOutput() && !currentCliArgs().contains(QStringLiteral("--json"));
}

struct CliExitCodeDescriptor {
	CliExitCode code = CliExitCode::Success;
	QString id;
	QString label;
	QString description;
};

struct CliCommandDescriptor {
	QString family;
	QString command;
	QString summary;
	QStringList examples;
	bool json = true;
	bool quiet = true;
	bool dryRun = false;
	bool watch = false;
};

std::string text(const QString& value)
{
	const QByteArray bytes = value.toUtf8();
	return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

int exitCodeValue(CliExitCode code)
{
	return static_cast<int>(code);
}

QVector<CliExitCodeDescriptor> cliExitCodeDescriptors()
{
	return {
		{CliExitCode::Success, QStringLiteral("success"), QStringLiteral("Success"), QStringLiteral("The command completed successfully.")},
		{CliExitCode::Failure, QStringLiteral("failure"), QStringLiteral("Operation failure"), QStringLiteral("The command was understood but the operation failed.")},
		{CliExitCode::Usage, QStringLiteral("usage-error"), QStringLiteral("Usage error"), QStringLiteral("Arguments were missing, malformed, or incompatible.")},
		{CliExitCode::NotFound, QStringLiteral("not-found"), QStringLiteral("Not found"), QStringLiteral("A requested project, package, entry, installation, or tool was not found.")},
		{CliExitCode::ValidationFailed, QStringLiteral("validation-failed"), QStringLiteral("Validation failed"), QStringLiteral("Validation completed and found blocking problems.")},
		{CliExitCode::Unavailable, QStringLiteral("unavailable"), QStringLiteral("Unavailable"), QStringLiteral("The workflow is recognized but no capable implementation or tool is available yet.")},
	};
}

CliExitCodeDescriptor cliExitCodeDescriptor(CliExitCode code)
{
	for (const CliExitCodeDescriptor& descriptor : cliExitCodeDescriptors()) {
		if (descriptor.code == code) {
			return descriptor;
		}
	}
	return {code, QStringLiteral("unknown"), QStringLiteral("Unknown"), QStringLiteral("Unknown exit code.")};
}

QVector<CliCommandDescriptor> cliCommandDescriptors()
{
	return {
		{QStringLiteral("cli"), QStringLiteral("exit-codes"), QStringLiteral("Print stable exit-code identifiers."), {QStringLiteral("vibestudio --cli cli exit-codes --json")}},
		{QStringLiteral("cli"), QStringLiteral("commands"), QStringLiteral("Print the registered command surface used for help and docs validation."), {QStringLiteral("vibestudio --cli cli commands --json")}},
		{QStringLiteral("ui"), QStringLiteral("semantics"), QStringLiteral("Print status chip, shortcut registry, and command palette metadata."), {QStringLiteral("vibestudio --cli ui semantics --json")}},
		{QStringLiteral("workspace"), QStringLiteral("create"), QStringLiteral("Save portable project/module references to a .vibeworkspace file. Supports --project, --package, --map, --map-name, repeated --code, --current-code, --active-module, --from-session and --overwrite."), {QStringLiteral("vibestudio --cli workspace create ./work.vibeworkspace --project ./game --active-module textures --dry-run --json")}, true, true, true},
		{QStringLiteral("workspace"), QStringLiteral("inspect"), QStringLiteral("Validate a workspace and list missing references without opening or executing its contents."), {QStringLiteral("vibestudio --cli workspace inspect ./work.vibeworkspace --json")}},
		{QStringLiteral("asset"), QStringLiteral("formats"), QStringLiteral("List shared format capabilities, runtime codec availability and export profiles, optionally filtered by --module."), {QStringLiteral("vibestudio --cli asset formats --module textures --json")}},
		{QStringLiteral("asset"), QStringLiteral("route"), QStringLiteral("Resolve a filename to its owning studio module and format capabilities; content validation occurs when opened."), {QStringLiteral("vibestudio --cli asset route textures/wall.dds --json")}},
		{QStringLiteral("project"), QStringLiteral("init"), QStringLiteral("Create or refresh a project manifest."), {QStringLiteral("vibestudio --cli project init C:\\Games\\Quake\\mymod"), QStringLiteral("vibestudio --cli project init ./mymod --project-ai-free on")}},
		{QStringLiteral("project"), QStringLiteral("files"), QStringLiteral("List source files, media, maps, packages and studio documents using the Go to File catalog."), {QStringLiteral("vibestudio --cli project files ./mymod --where \"kind=image\" --json")}},
		{QStringLiteral("project"), QStringLiteral("info"), QStringLiteral("Print project manifest and health summary."), {QStringLiteral("vibestudio --cli project info ./mymod --json")}},
		{QStringLiteral("project"), QStringLiteral("validate"), QStringLiteral("Validate project health and return validation-failed for blocking issues."), {QStringLiteral("vibestudio --cli project validate ./mymod")}},
		{QStringLiteral("package"), QStringLiteral("info"), QStringLiteral("Print package/archive summary (250,000-record, 128-component and 64 MiB metadata/fingerprint indexing limits)."), {QStringLiteral("vibestudio --cli package info ./id1/pak0.pak")}},
		{QStringLiteral("package"), QStringLiteral("list"), QStringLiteral("List package entries, or with --where only those a query matches, as the Packages filter reads it (ext=wav size>1mb)."), {QStringLiteral("vibestudio --cli package list ./baseq3/pak0.pk3 --json"), QStringLiteral("vibestudio --cli package list ./baseq3/pak0.pk3 --where \"ext=tga size>256kb\"")}},
		{QStringLiteral("package"), QStringLiteral("preview"), QStringLiteral("Preview a package path or an exact --entry-index from list --json, including repeated names. JSON includes exact byte counts and bounded audio metadata."), {QStringLiteral("vibestudio --cli package preview ./pak0.pak maps/start.bsp"), QStringLiteral("vibestudio --cli package preview ./maps.wad --entry-index 3 --json")}},
		{QStringLiteral("package"), QStringLiteral("extract"), QStringLiteral("Stream planned package entries to a folder with dry-run preflight. Repeat --entry-index with paired --as relative paths to separate occurrences. Conflicting outputs and input destinations are refused; existing files require --overwrite. Empty selectors and unsupported options are errors."), {QStringLiteral("vibestudio --cli package extract ./pak0.pak --output ./out --entry maps/start.bsp --dry-run")}, true, true, true},
		{QStringLiteral("package"), QStringLiteral("validate"), QStringLiteral("Stream and verify every package payload, size, and checksum."), {QStringLiteral("vibestudio --cli package validate ./pak0.pak --json")}},
		{QStringLiteral("package"), QStringLiteral("recover"), QStringLiteral("Verify an interrupted save journal; --finish completes the backup and cleanup only when the saved output is verified. Optional --expected-sha256 rejects a changed journal; an external backup requires --backup."), {QStringLiteral("vibestudio --cli package recover ./build/.vibestudio-package-ABC123.payload.json --json")}},
		{QStringLiteral("package"), QStringLiteral("interrupted-saves"), QStringLiteral("Discover bounded interrupted-save journal metadata in the current folder or repeated --directory folders. Payloads are verified separately with package recover."), {QStringLiteral("vibestudio --cli package interrupted-saves --directory ./build --json")}},
		{QStringLiteral("package"), QStringLiteral("recoveries"), QStringLiteral("List bounded local package recovery metadata, storage usage, limits and review checksums; --directory selects the store. Payloads and history are verified during restore."), {QStringLiteral("vibestudio --cli package recoveries --json")}},
		{QStringLiteral("package"), QStringLiteral("draft-recover"), QStringLiteral("Verify a selected recovery ID and copy all content and edit history to a new --output .vibepackage directory. Requires --expected-sha256 from the inventory; --dry-run performs no writes."), {QStringLiteral("vibestudio --cli package draft-recover <id> --expected-sha256 <sha256> --output ./recovered.vibepackage --dry-run --json")}, true, true, true},
		{QStringLiteral("package"), QStringLiteral("recovery-discard"), QStringLiteral("Discard a selected recovery ID with --expected-sha256 or --expected-storage-sha256 from the inventory. The storage checksum also supports incomplete copies. Previews by default; --write removes the reviewed copy after session and path checks."), {QStringLiteral("vibestudio --cli package recovery-discard <id> --expected-sha256 <sha256> --write --json")}, true, true, true},
		{QStringLiteral("package"), QStringLiteral("draft-storage"), QStringLiteral("Verify a saved draft and review retained/unused storage, configured limits and the storage checksum without writing files."), {QStringLiteral("vibestudio --cli package draft-storage ./work.vibepackage --json")}},
		{QStringLiteral("package"), QStringLiteral("draft-compact"), QStringLiteral("Review reclamation of unused draft objects by --expected-storage-sha256. --write proves no document or worker reader remains, then removes only reviewed unused files; default dry run creates no locks."), {QStringLiteral("vibestudio --cli package draft-compact ./work.vibepackage --expected-storage-sha256 <sha256> --write --json")}, true, true, true},
		{QStringLiteral("package"), QStringLiteral("copy-sessions"), QCoreApplication::translate("PackageCopySessionsCli", "Inspect retained temporary copy sessions, actual payload usage, native ownership availability and storage review checksums. --directory selects the store; inspection creates no files."), {QStringLiteral("vibestudio --cli package copy-sessions --json")}},
		{QStringLiteral("package"), QStringLiteral("copy-discard"), QCoreApplication::translate("PackageCopySessionsCli", "Review discard of a copy session by UUID and --expected-storage-sha256 from copy-sessions. --directory selects the store. Default dry run; --write removes reviewed files. Both modes exclude live session owners."), {QStringLiteral("vibestudio --cli package copy-discard <id> --expected-storage-sha256 <sha256> --write --json")}, true, true, true},
		{QStringLiteral("package"), QStringLiteral("copy-store-limits"), QCoreApplication::translate("PackageCopyStoreLimitsCli", "Inspect or propose shared temporary-copy limits with --max-mib, --max-files, --max-entries and --max-batches. --directory selects the physical store; --write saves its policy. Optional --expected-policy-sha256 binds an earlier review. Existing copies are preserved."), {QStringLiteral("vibestudio --cli package copy-store-limits --json")}},
		{QStringLiteral("package"), QStringLiteral("copy-limits"), QCoreApplication::translate("PackageCopyLimitsCli", "Inspect or propose per-window temporary-copy limits with --max-mib, --max-files, --max-entries and --max-batches. Default read-only/dry run; --write saves limits. Live reservations are available in the GUI."), {QStringLiteral("vibestudio --cli package copy-limits --json")}},
		{QStringLiteral("package"), QStringLiteral("working-imports"), QStringLiteral("Review bounded temporary working sessions, payload reservations, limits, leases and storage checksums; --directory selects the store. Inspection creates no files."), {QStringLiteral("vibestudio --cli package working-imports --json")}},
		{QStringLiteral("package"), QStringLiteral("working-unlock"), QStringLiteral("Release an unchanged, reviewed working-import lock only after native owner exclusion. Requires <relative-lock-path> --expected-lock-sha256 <sha256>; --directory selects the store. Default dry run; --write releases the lock file."), {QStringLiteral("vibestudio --cli package working-unlock .store.lock --expected-lock-sha256 <sha256> --write --json")}},
		{QStringLiteral("package"), QStringLiteral("working-discard"), QStringLiteral("Review discard of a working session by ID and --expected-storage-sha256. --write proves its lease is idle and removes only reviewed files; the default dry run creates no locks and cannot prove lease liveness."), {QStringLiteral("vibestudio --cli package working-discard <id> --expected-storage-sha256 <sha256> --write --json")}, true, true, true},
		{QStringLiteral("package"), QStringLiteral("draft-save"), QStringLiteral("Save source content, staged edits, and undo/redo history in a portable .vibepackage directory. --dry-run verifies all history inputs and destination constraints without writing."), {QStringLiteral("vibestudio --cli package draft-save ./mod.pk3 ./work.vibepackage --add-file ./note.txt --as docs/note.txt --json")}, true, true, true},
		{QStringLiteral("package"), QStringLiteral("draft-info"), QStringLiteral("Verify and inspect a saved package draft without modifying it."), {QStringLiteral("vibestudio --cli package draft-info ./work.vibepackage --json")}},
		{QStringLiteral("package"), QStringLiteral("draft-undo"), QStringLiteral("Undo one edit group and atomically save the updated package draft."), {QStringLiteral("vibestudio --cli package draft-undo ./work.vibepackage --json")}},
		{QStringLiteral("package"), QStringLiteral("draft-redo"), QStringLiteral("Redo one edit group and atomically save the updated package draft."), {QStringLiteral("vibestudio --cli package draft-redo ./work.vibepackage --json")}},
		{QStringLiteral("package"), QStringLiteral("groups"), QStringLiteral("Inspect Doom WAD map, GL, namespace and texture-table groups with stable IDs and a review fingerprint for atomic staged group edits."), {QStringLiteral("vibestudio --cli package groups ./maps.wad --json")}},
		{QStringLiteral("package"), QStringLiteral("stage"), QStringLiteral("Preview staged package add, replace, rename, and delete operations."), {QStringLiteral("vibestudio --cli package stage ./pak0.pak --add-file ./autoexec.cfg --as scripts/autoexec.cfg --json")}, true, true, true},
		{QStringLiteral("package"), QStringLiteral("create"), QStringLiteral("Create an empty or staged PAK, ZIP, PK3, WAD, or .vibepackage document without a source. Use --format for draft format and --wad-magic for WAD variants. Dry runs verify content without writing."), {QStringLiteral("vibestudio --cli package create ./new.pk3 --mkdir maps --add-file ./start.bsp --as maps/start.bsp"), QStringLiteral("vibestudio --cli package create ./new.vibepackage --format wad --wad-magic WAD3 --dry-run")}, true, true, true},
		{QStringLiteral("package"), QStringLiteral("save-as"), QStringLiteral("Write a staged package to a new PAK, ZIP, PK3, or tested PWAD path."), {QStringLiteral("vibestudio --cli package save-as ./pak0.pak ./rebuilt.pk3 --format pk3 --manifest ./rebuilt.manifest.json")}, true, true, true},
		{QStringLiteral("package"), QStringLiteral("manifest"), QStringLiteral("Export a package staging manifest with retained-content and metadata usage and limits, without writing a package."), {QStringLiteral("vibestudio --cli package manifest ./pak0.pak --output ./stage.manifest.json")}, true, true, true},
		{QStringLiteral("package"), QStringLiteral("compare"), QStringLiteral("Compare two packages, or review one package with --staged and add/replace/rename/delete options. Unchecked content prevents a match unless --metadata-only is requested. Opening fingerprints complete source files in either mode."), {QStringLiteral("vibestudio --cli package compare ./release-1.pk3 ./release-2.pk3 --json"), QStringLiteral("vibestudio --cli package compare ./mod.pk3 --staged --delete scripts/old.cfg --json")}},
		{QStringLiteral("package"), QStringLiteral("subset"), QStringLiteral("Export exact occurrences and required WAD groups or a map's resolved assets from archives or drafts. Stage edits first; output must be separate."), {QStringLiteral("vibestudio --cli package subset ./assets ./subset.pk3 --prefix textures/arena --dry-run --json"), QStringLiteral("vibestudio --cli package subset ./assets ./level-assets.pk3 --map-input ./maps/arena.map --engine idTech3"), QStringLiteral("vibestudio --cli package subset ./maps.wad ./selected.wad --entry-index 17 --dry-run --json")}, true, true, true},
		{QStringLiteral("asset"), QStringLiteral("inspect"), QStringLiteral("Inspect package entry asset metadata, including image, model, audio, and script details."), {QStringLiteral("vibestudio --cli asset inspect ./pak0.pk3 textures/base/wall.png --json")}},
		{QStringLiteral("asset"), QStringLiteral("convert"), QStringLiteral("Batch-convert package image entries with crop, resize, palette, and dry-run previews."), {QStringLiteral("vibestudio --cli asset convert ./pak0.pk3 --entry textures/base/wall.png --output ./converted --format png --resize 128x128 --dry-run")}, true, true, true},
		{QStringLiteral("asset"), QStringLiteral("audio-wav"), QStringLiteral("Export readable WAV, DMX, MP3, FLAC and Vorbis package sounds to a separate WAV file."), {QStringLiteral("vibestudio --cli asset audio-wav ./pak0.pk3 sound/items/pickup.wav --output ./pickup.wav --dry-run")}, true, true, true},
		{QStringLiteral("asset"), QStringLiteral("audio-generate"), QStringLiteral("Make a game-ready sound effect from a description: the built-in synthesizer (no AI, the same sound for the same description and seed), or --source ai for your sound model (ElevenLabs or a custom endpoint); delivered as a Doom DMX lump, a Quake 11 kHz WAV, or a Quake II/III 22 kHz WAV, with --loop on for seamless loops."), {QStringLiteral("vibestudio --cli asset audio-generate --prompt \"heavy metal door slam\" --game quake --output ./mymod --preview ./door.wav"), QStringLiteral("vibestudio --cli asset audio-generate --prompt \"distant reactor hum\" --game quake2 --loop on --source ai --dry-run")}, true, true, true},
		{QStringLiteral("asset"), QStringLiteral("audio-edit"), QStringLiteral("Edit samples: trim/delete, fades, gain/normalize, mono/stereo, invert/remove-dc, paste/mix with --paste-input, insert-silence with --frames, or resample with --sample-rate; output .vsaudio or WAV with --wav-format and --dither."), {QStringLiteral("vibestudio --cli asset audio-edit ./sound.wav --operation normalize --db -1 --output ./normalized.vsaudio --dry-run"), QStringLiteral("vibestudio --cli asset audio-edit ./pak0.pk3 --entry sound/items/pickup.wav --operation trim --start-frame 0 --end-frame 22050 --output ./trimmed.wav")}, true, true, true},
		{QStringLiteral("asset"), QStringLiteral("audio-export"), QStringLiteral("Deliver a separate WAV or Doom DMX sound with --preset wav|doom|quake|quake2|quake3, optional final dither, source protection, and reproducible dry runs."), {QStringLiteral("vibestudio --cli asset audio-export ./sound.vsaudio --preset doom --output ./DSWIND.dmx --dry-run --json"), QStringLiteral("vibestudio --cli asset audio-export ./sound.wav --preset quake3 --output ./sound/game/wind.wav")}, true, true, true},
		{QStringLiteral("asset"), QStringLiteral("audio-analyze"), QStringLiteral("Measure read-only sample/true peak, integrated loudness, RMS, DC, and full-scale events in supported audio; optional --entry, --start-frame/--end-frame, --channel-map for surround roles, or --no-loudness."), {QStringLiteral("vibestudio --cli asset audio-analyze ./sound.vsaudio --json"), QStringLiteral("vibestudio --cli asset audio-analyze ./sounds.wad --entry DSWIND --start-frame 0 --end-frame 11025 --json")}},
		{QStringLiteral("asset"), QStringLiteral("audio-new"), QStringLiteral("Create an empty or silent audio document with --sample-rate, --channels, --frames, and --output. Empty documents require .vsaudio output."), {QStringLiteral("vibestudio --cli asset audio-new --sample-rate 44100 --channels 1 --frames 0 --output ./sound.vsaudio --dry-run")}, true, true, true},
		{QStringLiteral("asset"), QStringLiteral("audio-take"), QStringLiteral("Inspect checksummed .vstake recordings or export a reviewed frame/channel range to .vsaudio or float32 WAV. Export requires --expected-prefix-sha256, --start-frame, --end-frame, --channels (one-based), --output; optional --format project|wav, --allow-incomplete, --dry-run and --overwrite. Never opens an input device."), {QStringLiteral("vibestudio --cli asset audio-take inspect ./voice.vstake --json")}, true, true, true},
		{QStringLiteral("asset"), QStringLiteral("audio-recording"), QStringLiteral("Inspect a .vsrecord folder and independently verify its plan, receipt, timing and per-arm take prefixes, including complete passes and partial-pass frames. Import selected takes into an existing session with import SESSION --review PLAN.json --output SESSION.vssession. VibeStudioRecordingImport pins reviewed plan/receipt/prefix hashes, ranges, channels, placement and target tracks. Version 1 selects the first pass; version 2 requires one-based loopPass on every selection and permits distinct arm/pass pairs with pass-local ranges. Version 3 requires comp:true and crossfadeFrames, permits repeated pairs and plans non-overlapping sections with optional complementary linear crossfades after adjacent cuts; outgoing handles must fit the recorded pass and all handles count toward session budgets. Missing or interrupted receipts require allowInterrupted in the plan. Import supports --expected-session-sha256, --dry-run and --overwrite. Preview SESSION --review PLAN.json --output PREVIEW.wav renders the same clip span to stereo float32 WAV; --isolated removes backing while retaining mixer/effects/mute/solo. Preview shares hash and output guards without adopting session edits, listening volume or device clipping. Save-review SESSION --review PLAN.json --output REVIEW.json rechecks and saves editable review choices with paths relative to the output; --dry-run and --overwrite apply, and updating the loaded review is guarded by its read identity. Saved reviews reopen in Record Tracks and feed the same import/preview commands. Never opens audio devices."), {QStringLiteral("vibestudio --cli asset audio-recording inspect ./voice.vsrecord --json")}, true, true, true},
		{QStringLiteral("asset"), QStringLiteral("audio-session"), QStringLiteral("Create, inspect, import, edit, automate and mix down non-destructive .vssession sessions. Supports track/clip mixing, fades, resampling and exact frames. Edit operations add-bus, routing, send and remove-send manage stereo buses, pre/post sends and polarity; routing uses --output-bus ID|master|none, sends use --target-bus ID|master with --db, --pan, --pre-fader and --enabled. Effects use --track ID|master and --operation add|set|move|remove|clear|tail|preset|save-preset, with --type, --effect, --parameters key=value,..., --enabled, zero-based --index and global --tail-seconds (0..60). The presets command lists factory IDs, processor parameters and automatable eligibility at optional --sample-rate. The lookahead-limiter has fixed lookaheadMs (0..20); changes rebuild route compensation, and lookaheadMs cannot be automated. Inspect, transport and delivery JSON report processing latency separately from hardware latency. Operation preset replaces a chain using exactly one --preset ID or --preset-file FILE.vsfx; save-preset writes a chain with --name NAME to --output FILE.vsfx. Effect-automation uses --track ID|master --effect ID --parameter KEY with --points frame:value[:linear|step|smooth],...|none and/or --enabled true|false. Track automation also accepts the optional curve field. Curves live in sessions; presets contain static parameters and replacing a chain clears its lanes. Stems uses repeated --stem ID|master selectors (id: escapes literal IDs), --output DIR, optional --name PREFIX, --tap pre|post, --respect-solo true|false, frame bounds and --wav-format. Mixdown and stems accept --dither none|tpdf and unsigned --dither-seed. Stems preflight every destination, commit WAVs individually and record a version-1 delivery.stems.json manifest; failure retains completed-file results. Normal --output, --dry-run and --overwrite guards apply. Recover a .vssession-recovery with --expected-sha256 and a separate --output. Transport diagnostics use --frames, optional --block-frames and --loop true|false without opening audio devices. Session loops retain effects, routing delays and modulation on a continuous processing clock; sources and latency-corrected automation repeat, and the cursor wraps. Compensation primes once, using wrapped future context. Stop, seek and loop-policy changes reset processing; pause and meter reset preserve it. Meters analyzes a nonempty session range with optional --start-frame, --end-frame and --block-frames (1..65536). It reports pre/post track, bus and master sample peaks, RMS, held maxima, over-range counts and stereo correlation without devices or edits; --output, --overwrite and --dry-run do not apply. Tempo-map edits --tempo BPM, --meter N/D, --tempo-points tick:bpm,...|none and --meter-points bar:N/D,...|none; new accepts these too. Musical timing uses 960 ticks per quarter note and stepped 20..400 quarter-note BPM. Meter changes start at bars. Position is read-only: choose --at-frame, --at-tick or --at-position bar.beat.tick, with optional --snap bar|beat|half-beat|quarter-beat|beat-triplet. Audio and automation retain sample positions. Arrange uses --operation move|duplicate|remove|split|group|ungroup|gain|fades|mute with repeated --select-clip ID selectors. Group links default on; --linked-groups false edits only explicit clips. Move/duplicate require signed --offset-frames; split uses --at-frame; group uses --name; gain uses --db; fades uses --fade-in and --fade-out; mute uses --mute true|false. Splits preserve inherited fade envelopes, duplicate groups get independent identities, and track automation retains authored frames. Single region edits support --reset-fades true|false. Range uses --operation clear|ripple-delete|insert-silence|repeat with --start-frame, --end-frame (exclusive) and exactly one --all-tracks or repeated --select-track ID scope. Group links never expand range scope. Insert adds the range length at its start; repeat inserts a copy after its end. Time edits follow track/effect automation by default; --follow-automation false keeps authored frames. Master automation follows by default only for all-track time edits; --master-automation false preserves it. Clear only removes clip portions and does not accept automation options. Cut automation retains original curve domains. Tempo and meter maps remain unchanged. Media uses --operation inspect|rename|relink|replace|remove|prune. Inspect is read-only and reports embedded usage plus external file availability. Rename uses --source ID --name NAME. Relink/replace use --source ID --input FILE with optional --expected-sha256 from a prior dry-run; relink requires identical decoded samples, while replace retargets all clips to a new source identity and optionally --resample converts to the session rate. Replacement preserves channel count and must cover all referenced frames. Remove accepts repeated --source ID for unused sources; prune removes every unused embedded source. These operations never delete or modify source files. Saves write version 7 and read versions 1..6."), {QStringLiteral("vibestudio --cli asset audio-session new --sample-rate 48000 --output ./music.vssession --dry-run"), QStringLiteral("vibestudio --cli asset audio-session mixdown ./music.vssession --output ./music.wav --wav-format float32")}, true, true, true},
		{QStringLiteral("asset"), QStringLiteral("audio-recoveries"), QStringLiteral("Verify waveform and session recovery copies; --directory selects the folder. Reviewed --discard <id> requires --expected-sha256; --kind session selects a session (default waveform). --write commits, otherwise discard is a dry run."), {QStringLiteral("vibestudio --cli asset audio-recoveries --json")}, true, true, true},
		{QStringLiteral("asset"), QStringLiteral("audio-markers"), QStringLiteral("Inspect cues and a forward loop, or replace them with --markers <JSON file> and --output <separate .vsaudio|.wav>. Supports --entry, --dry-run, --overwrite, and WAV precision/dither options."), {QStringLiteral("vibestudio --cli asset audio-markers ./sound.vsaudio --json"), QStringLiteral("vibestudio --cli asset audio-markers ./sound.wav --markers ./markers.json --output ./marked.vsaudio --dry-run")}, true, true, true},
		{QStringLiteral("asset"), QStringLiteral("audio-project"), QStringLiteral("Inspect audio samples or a lossless project/recovery copy; use --output to import, copy, recover, or export to a separate .vsaudio or .wav file."), {QStringLiteral("vibestudio --cli asset audio-project ./sound.vsaudio --json"), QStringLiteral("vibestudio --cli asset audio-project ./sound.wav --output ./sound.vsaudio --dry-run")}, true, true, true},
		{QStringLiteral("asset"), QStringLiteral("find"), QStringLiteral("Search saved project text with --whole-word, --case-sensitive, --include and --exclude filters."), {QStringLiteral("vibestudio --cli asset find ./mymod --find \"seta\" --json")}},
		{QStringLiteral("asset"), QStringLiteral("replace"), QStringLiteral("Preview exact project text replacements; --delete-matches removes text, --write applies after source-hash checks."), {QStringLiteral("vibestudio --cli asset replace ./mymod --find \"devmap\" --replace \"map\" --dry-run")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("paint-material"), QStringLiteral("Paint exact map surfaces with repeatable --target face:brushId:faceNumber (one-based), patch:id, side:id:upper|lower|middle or sector:id:floor|ceiling. Requires --texture and --output; WADs require --map-name. Supports --dry-run and --overwrite."), {QStringLiteral("vibestudio --cli map paint-material ./maps/start.map --target face:0:1 --texture base/metal --output ./maps/painted.map --dry-run --json")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("sample-material"), QStringLiteral("Read one map surface material using --target; WADs require --map-name. Uses the paint-material selector syntax without changing any files."), {QStringLiteral("vibestudio --cli map sample-material ./maps/start.map --target face:0:1 --json")}},
		{QStringLiteral("map"), QStringLiteral("copy-surface"), QStringLiteral("Copy one brush face's material, mapping and flags to a versioned surface clipboard JSON file. Requires --target face:brushId:faceNumber (one-based) and --output. Supports --dry-run and --overwrite."), {QStringLiteral("vibestudio --cli map copy-surface ./maps/start.map --target face:0:1 --output ./surface.json --json")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("paste-surface"), QStringLiteral("Paste --clipboard surface.json onto repeatable --target face:brushId:faceNumber, brush:brushId or patch:patchId targets. Requires --output. --mode parameters copies matching-format values; radiant-values retains Valve axes; radiant-project copies classic/Valve values and projects matrix/patch UVs; project preserves world UVs; seamless wraps the shared plane edge. Parameter modes and radiant-project support patch targets and repeatable --object brush:id|patch:id|entity:id selection. Native projection reports edgeOnFaces. --mapping-only keeps materials/flags. Supply source --texture-size W,H and repeatable --material-size material=W,H when dimensions are needed. Project/seamless accept explicit --allow-valve220. --stroke replays up to 4096 ordered face/patch hits, applies selection only on the first hit and advances the seamless source; later source sizes use --material-size. JSON adds strokeHits, sourceAdvanced and finalSource. Supports --dry-run and --overwrite."), {QStringLiteral("vibestudio --cli map paste-surface ./maps/start.map --clipboard ./surface.json --target brush:0 --output ./maps/pasted.map --dry-run --json")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("new"), QStringLiteral("Create an empty map or starter room for Quake, Quake II, Quake III, Doom, or Hexen."), {QStringLiteral("vibestudio --cli map new --game quake3 --preset room --output ./maps/arena.map --dry-run --json")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("generate"), QStringLiteral("Generate a sealed, playable level from a prompt for Quake, Quake II, Quake III, or Doom: rules plan it, or --planner ai asks your text model; --preview draws the layout."), {QStringLiteral("vibestudio --cli map generate --prompt \"gothic castle with lava, 8 rooms, seed 42\" --game quake --output ./maps/generated.map --preview ./maps/generated.png"), QStringLiteral("vibestudio --cli map generate --prompt \"quake 3 duel arena\" --planner ai --dry-run")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("plan"), QStringLiteral("Make a level's room-and-connection plan without building it, by rules or by your text model, to review or edit before map generate --plan."), {QStringLiteral("vibestudio --cli map plan --prompt \"doom tech base, 6 rooms\" --output ./plan.json"), QStringLiteral("vibestudio --cli map plan --prompt \"deathmatch for 8 players\" --planner ai --provider local-offline --json")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("ai-edit"), QStringLiteral("Ask your text model for edits to a Quake-family .map in checked actions (add entity, set or remove a key, add a box, retexture, move, delete); review them, then --output writes the map with the ready ones. --proposal reviews a saved proposal with no model."), {QStringLiteral("vibestudio --cli map ai-edit ./maps/start.map --prompt \"add a light above each player start\" --save-proposal ./edits.json"), QStringLiteral("vibestudio --cli map ai-edit ./maps/start.map --proposal ./edits.json --only 1,2 --output ./maps/start-lit.map")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("recover"), QStringLiteral("Verify a recovery checkpoint and save its map to an explicit output."), {QStringLiteral("vibestudio --cli map recover ./recovery/document.vsrecovery --output ./maps/recovered.map --dry-run --json")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("recoveries"), QStringLiteral("List recovery checkpoints and their bounded metadata."), {QStringLiteral("vibestudio --cli map recoveries ./recovery --json")}},
		{QStringLiteral("map"), QStringLiteral("export-prefab"), QStringLiteral("Capture selected brushes, patches and entities as a versioned .vprefab with complete ownership and an anchor."), {QStringLiteral("vibestudio --cli map export-prefab ./maps/arena.map --object brush:0 --name Door --output ./prefabs/door.vprefab --dry-run --json")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("inspect-prefab"), QStringLiteral("Inspect a prefab file or package --entry, including dependency review with --package."), {QStringLiteral("vibestudio --cli map inspect-prefab ./prefabs/door.vprefab --package ./assets --json")}},
		{QStringLiteral("map"), QStringLiteral("insert-prefab"), QStringLiteral("Place a prefab at --position x,y,z with optional --rotation x,y,z and --texture-lock on|off; remap internal target links uniquely."), {QStringLiteral("vibestudio --cli map insert-prefab ./maps/arena.map --prefab ./prefabs/door.vprefab --position 128,0,0 --rotation 0,0,90 --output ./maps/arena-door.map --json")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("inspect-udmf"), QStringLiteral("Inspect lossless UDMF globals, object selectors, property literals and source lines."), {QStringLiteral("vibestudio --cli map inspect-udmf ./maps/arena.wad --map-name MAP01 --json")}},
		{QStringLiteral("map"), QStringLiteral("edit-udmf"), QStringLiteral("Edit UDMF properties as one transaction with --object global|type:index, repeatable --set key=literal and --remove key. Requires --output; supports --dry-run and --overwrite."), {QStringLiteral("vibestudio --cli map edit-udmf ./maps/arena.wad --map-name MAP01 --object vertex:0 --set x=16.5 --output ./maps/edited.wad --json")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("inspect"), QStringLiteral("Inspect Doom WAD maps and Quake-family .map files with entities, textures, statistics, validation, and preview lines."), {QStringLiteral("vibestudio --cli map inspect ./maps/start.map --select entity:0 --json")}},
		{QStringLiteral("map"), QStringLiteral("edit"), QStringLiteral("Edit map entity key/value pairs, a brush face's texture and alignment, or a Doom sector's, linedef's, or sidedef's fields, and write the result to a non-destructive save-as path. With --where, every entity or Doom thing a query matches takes each --set, as one undo step per key."), {QStringLiteral("vibestudio --cli map edit ./maps/start.map --entity 1 --set targetname=lift --output ./maps/start-edited.map"), QStringLiteral("vibestudio --cli map edit ./maps/e1.wad --map E1M1 --select linedef:12 --set special=1 --set front.middle=DOOR1 --output ./maps/e1-door.wad"), QStringLiteral("vibestudio --cli map edit ./maps/start.map --where class=light --set light=300 --output ./maps/start-bright.map")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("move"), QStringLiteral("Move a map object with --texture-lock on|off (default on for Quake brushes), then save to a new path."), {QStringLiteral("vibestudio --cli map move ./maps/start.map --object entity:1 --delta 16,0,0 --output ./maps/start-moved.map")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("add-brush"), QStringLiteral("Add a box, wedge, cylinder, cone or sphere brush in the map's face format; --axis, --sides and --bands control radial detail."), {QStringLiteral("vibestudio --cli map add-brush ./maps/start.map --shape cylinder --axis z --sides 12 --mins 0,0,0 --maxs 128,128,256 --texture base/metal --output ./maps/start-column.map")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("brush-components"), QStringLiteral("Inspect a brush's solved vertex, edge and face IDs."), {QStringLiteral("vibestudio --cli map brush-components ./maps/start.map --brush 0 --json")}, true, true, false},
		{QStringLiteral("map"), QStringLiteral("move-components"), QStringLiteral("Move or snap brush vertices, edges or faces and rebuild their convex hull."), {QStringLiteral("vibestudio --cli map move-components ./maps/start.map --brush 0 --kind vertex --component 0 --delta 0,0,16 --output ./maps/reshaped.map")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("add-patch"), QStringLiteral("Create a plane, cylinder or cone patch in a Quake III map."), {QStringLiteral("vibestudio --cli map add-patch ./maps/start.map --shape cylinder --size 128,128,256 --texture textures/base/metal --output ./maps/curves.map")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("edit-patch"), QStringLiteral("Edit patch control points, UV coordinates, material, facing or control-grid subdivision."), {QStringLiteral("vibestudio --cli map edit-patch ./maps/curves.map --patch 0 --point 1,1 --delta 0,0,32 --output ./maps/curves-edited.map")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("stitch-patches"), QStringLiteral("Join two patch boundaries with exact refinement, optional UV matching and tangent continuity."), {QStringLiteral("vibestudio --cli map stitch-patches ./maps/curves.map --first 0:last-column --second 1:first-column --max-gap 8 --output ./maps/joined.map --json")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("cap-patch"), QStringLiteral("Cap curved patch boundaries with planar surfaces, optional boundary UVs and one atomic map edit."), {QStringLiteral("vibestudio --cli map cap-patch ./maps/curves.map --patch 0 --boundary first-row --boundary last-row --output ./maps/capped.map --json")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("add-thing"), QStringLiteral("Add a thing to a Doom or Hexen map by DoomEd number and save to a new WAD."), {QStringLiteral("vibestudio --cli map add-thing ./maps/e1.wad --map E1M1 --type 3004 --origin 64,96 --angle 90 --output ./maps/e1-more.wad")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("add-entity"), QStringLiteral("Add a point entity to a Quake-family .map and save to a new path."), {QStringLiteral("vibestudio --cli map add-entity ./maps/start.map --class light --origin 64,0,96 --set light=300 --output ./maps/start-lit.map")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("place-sound"), QStringLiteral("Validate a package WAV and place a Quake II/III speaker in a separate map output. Supports --game quake2|quake3, --mode loop-on|loop-off|triggered and --targetname; inactive sounds require a target name."), {QStringLiteral("vibestudio --cli map place-sound ./maps/start.map --package ./assets --sound sound/ambient/hum.wav --game quake3 --origin 64,0,96 --output ./maps/start-sound.map --dry-run --json")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("delete"), QStringLiteral("Delete entities, brushes, or patches from a Quake-family .map, or things, vertices, linedefs, or sectors from a Doom or Hexen map, and save to a new path."), {QStringLiteral("vibestudio --cli map delete ./maps/start.map --object entity:3 --object brush:12 --output ./maps/start-trimmed.map")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("replace-texture"), QStringLiteral("Replace every use of one texture with another across a map, or only chosen objects, and save to a new path."), {QStringLiteral("vibestudio --cli map replace-texture ./maps/start.map --from base/wall --to base/metal --output ./maps/start-metal.map")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("rotate"), QStringLiteral("Rotate map objects by --degrees or --turns with --texture-lock on|off (default on), optional --pivot and explicit --allow-valve220 conversion."), {QStringLiteral("vibestudio --cli map rotate ./maps/start.map --object brush:0 --axis z --degrees 30 --texture-lock on --allow-valve220 --output ./maps/start-turned.map")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("align-textures"), QStringLiteral("Align brush surfaces with exactly one of --fit U,V, --shift U,V, --scale U,V, --degrees N, or --align U,V. Select --object brush:id or --face brushId:faceNumber (one-based), repeatable. Resolve sizes from --package or an explicit --texture-size W,H."), {QStringLiteral("vibestudio --cli map align-textures ./maps/start.map --object brush:0 --fit 1,1 --package ./assets --output ./maps/start-fitted.map"), QStringLiteral("vibestudio --cli map align-textures ./maps/start.map --face 0:1 --align center,minimum --texture-size 128,64 --output ./maps/start-aligned.map")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("flip"), QStringLiteral("Mirror map objects; --connected expands Doom/Hexen/UDMF geometry. UDMF retains fractional coordinates. Quake brush textures lock by default; --texture-lock off retains source parameters and --allow-valve220 permits conversion."), {QStringLiteral("vibestudio --cli map flip ./maps/start.map --object entity:3 --axis x --output ./maps/start-flipped.map"), QStringLiteral("vibestudio --cli map flip ./maps/arena.wad --object linedef:0 --connected --axis x --output ./maps/arena-mirrored.wad")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("apply-texture"), QStringLiteral("Put a texture on every face of the given brushes and patches of a Quake-family .map, and save to a new path."), {QStringLiteral("vibestudio --cli map apply-texture ./maps/start.map --object brush:12 --object entity:3 --texture base_wall/metal --output ./maps/start-metal.map")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("split-linedef"), QStringLiteral("Split linedefs of a Doom or Hexen map at their middles, and save to a new WAD."), {QStringLiteral("vibestudio --cli map split-linedef ./maps/e1.wad --map E1M1 --object linedef:12 --output ./maps/e1-split.wad")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("draw-sector"), QStringLiteral("Draw a sector on a Doom or Hexen map from its corners, joining and splitting the lines it meets, and save to a new WAD."), {QStringLiteral("vibestudio --cli map draw-sector ./maps/e1.wad --map E1M1 --points \"0,0 0,256 256,256 256,0\" --output ./maps/e1-room.wad")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("connect"), QStringLiteral("Make entities of a Quake-family .map target the last one given, naming it if it has no targetname, and save to a new path."), {QStringLiteral("vibestudio --cli map connect ./maps/start.map --object entity:4 --object entity:7 --output ./maps/start-linked.map")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("shift-sectors"), QStringLiteral("Raise or lower Doom sectors' floors, ceilings, or light by an amount, and save to a new WAD."), {QStringLiteral("vibestudio --cli map shift-sectors ./maps/e1.wad --map E1M1 --object sector:4 --field floor --by 8 --output ./maps/e1-raised.wad")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("gradient-sectors"), QStringLiteral("Spread Doom sectors' floors, ceilings, or light evenly from the first given to the last, and save to a new WAD."), {QStringLiteral("vibestudio --cli map gradient-sectors ./maps/e1.wad --map E1M1 --object sector:1 --object sector:2 --object sector:3 --field light --output ./maps/e1-lit.wad")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("make-door"), QStringLiteral("Make sectors of a Doom or Hexen map into doors, Doom Builder style, and save to a new WAD."), {QStringLiteral("vibestudio --cli map make-door ./maps/e1.wad --map E1M1 --object sector:7 --door-texture BIGDOOR2 --output ./maps/e1-door.wad")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("join-sectors"), QStringLiteral("Join sectors of a Doom or Hexen map into the last one given, and save to a new WAD."), {QStringLiteral("vibestudio --cli map join-sectors ./maps/e1.wad --map E1M1 --object sector:4 --object sector:2 --output ./maps/e1-joined.wad")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("merge-sectors"), QStringLiteral("Join sectors of a Doom or Hexen map into the last one given and take away the lines between them, and save to a new WAD."), {QStringLiteral("vibestudio --cli map merge-sectors ./maps/e1.wad --map E1M1 --object sector:4 --object sector:2 --output ./maps/e1-merged.wad")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("merge-vertices"), QStringLiteral("Join vertices of a Doom or Hexen map into the last one given, stitching lines left over each other, and save to a new WAD."), {QStringLiteral("vibestudio --cli map merge-vertices ./maps/e1.wad --map E1M1 --object vertex:12 --object vertex:40 --output ./maps/e1-joined.wad")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("flip-linedef"), QStringLiteral("Turn linedefs of a Doom or Hexen map around, sides and all, and save to a new WAD."), {QStringLiteral("vibestudio --cli map flip-linedef ./maps/e1.wad --map E1M1 --object linedef:12 --output ./maps/e1-flipped.wad")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("carve"), QStringLiteral("Carve the given brushes of a Quake-family .map out of every brush they overlap, and save to a new path."), {QStringLiteral("vibestudio --cli map carve ./maps/start.map --object brush:12 --output ./maps/start-doorway.map")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("hollow"), QStringLiteral("Turn brushes of a Quake-family .map into walls of a thickness, one per face, and save to a new path."), {QStringLiteral("vibestudio --cli map hollow ./maps/start.map --object brush:12 --thickness 8 --output ./maps/start-room.map")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("merge-brushes"), QStringLiteral("Merge an exact convex union of brushes, with explicit choices for conflicting surfaces, and save to a new path."), {QStringLiteral("vibestudio --cli map merge-brushes ./maps/start.map --object brush:0 --object brush:1 --output ./maps/merged.map --dry-run --json")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("clip"), QStringLiteral("Cut brushes of a Quake-family .map with a plane, keeping one side or both, and save to a new path."), {QStringLiteral("vibestudio --cli map clip ./maps/start.map --object brush:12 --axis x --at 64 --keep below --output ./maps/start-clipped.map"), QStringLiteral("vibestudio --cli map clip ./maps/start.map --object entity:3 --points \"0,0,0 64,64,0 0,0,64\" --keep both --output ./maps/start-split.map")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("resize"), QStringLiteral("Resize map objects to new bounds or a size; --texture-lock on stretches brush textures (default off). --allow-valve220 permits required conversion."), {QStringLiteral("vibestudio --cli map resize ./maps/start.map --object brush:12 --size 128,64,32 --output ./maps/start-wider.map"), QStringLiteral("vibestudio --cli map resize ./maps/start.map --object entity:3 --mins 0,0,0 --maxs 256,128,64 --output ./maps/start-resized.map")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("snap"), QStringLiteral("Snap objects independently, preserving owner assemblies and shared Doom vertices. Brush textures lock by default; --texture-lock off keeps source parameters."), {QStringLiteral("vibestudio --cli map snap ./maps/start.map --object entity:3 --object brush:12 --grid 16 --output ./maps/start-snapped.map")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("duplicate"), QStringLiteral("Copy Quake-family objects or Doom/Hexen things with an optional delta. Brush textures lock by default; --texture-lock off keeps source parameters."), {QStringLiteral("vibestudio --cli map duplicate ./maps/start.map --object entity:3 --object brush:12 --delta 64,0,0 --output ./maps/start-more.map")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("paste"), QStringLiteral("Insert UTF-8 map text from --from with an optional --delta. Brush textures lock by default; --texture-lock off keeps source parameters. Input limit: 8 MiB / 4 million UTF-16 characters."), {QStringLiteral("vibestudio --cli map paste ./maps/start.map --from ./assembly.map --delta 128,0,0 --output ./maps/placed.map --dry-run --json")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("compile-plan"), QStringLiteral("Build a compiler command plan from the inspected map and selected compiler profile."), {QStringLiteral("vibestudio --cli map compile-plan ./maps/start.map --profile ericw-qbsp --json")}, true, true, true},
		{QStringLiteral("shader"), QStringLiteral("inspect"), QStringLiteral("Parse idTech3 shader scripts into an editable graph model and validate texture references."), {QStringLiteral("vibestudio --cli shader inspect ./scripts/common.shader --package ./baseq3 --json")}},
		{QStringLiteral("shader"), QStringLiteral("set-stage"), QStringLiteral("Edit a shader stage directive and write a round-tripped shader script to a save-as path."), {QStringLiteral("vibestudio --cli shader set-stage ./scripts/common.shader --shader textures/base/wall --stage 1 --directive blendFunc --value \"GL_ONE GL_ONE\" --output ./scripts/common-edited.shader")}, true, true, true},
		{QStringLiteral("sprite"), QStringLiteral("plan"), QStringLiteral("Create Doom or Quake sprite frame, palette, sequencing, and package staging plans."), {QStringLiteral("vibestudio --cli sprite plan --engine doom --name TROO --frames 2 --rotations 8 --palette doom --json")}, true, true, true},
		{QStringLiteral("code"), QStringLiteral("language-server"), QStringLiteral("Run a local stdio server for diagnostics, --line/--column definitions, --hover, --signature-help, --completion (with optional --resolve-completion <index>) or --references. Preview --rename <name>, or list --code-actions and preview --action-index <n>; their --write requires --expected-plan-sha256. Preview --format-document or --format-range with --end-line/--end-column, --tab-size and --insert-spaces; formatting --write requires --expected-sha256. Code actions accept optional range endpoints. --server is absolute."), {QStringLiteral("vibestudio --cli code language-server ./main.cpp --server /absolute/path/clangd --root . --language cpp --json")}},
		{QStringLiteral("code"), QStringLiteral("index"), QStringLiteral("Index a project source tree with language hooks, diagnostics, symbols, build tasks, and launch profiles."), {QStringLiteral("vibestudio --cli code index ./mymod --find monster --json")}},
		{QStringLiteral("code"), QStringLiteral("files"), QStringLiteral("List project source files and cached metadata using the same bounded catalog and property queries as the Code Files panel."), {QStringLiteral("vibestudio --cli code files ./mymod --where \"ext=qc size>1kb\" --json")}},
		{QStringLiteral("code"), QStringLiteral("text-info"), QStringLiteral("Inspect a text file's encoding, BOM, line endings, editability, and source SHA-256."), {QStringLiteral("vibestudio --cli code text-info ./scripts/game.qc --json")}},
		{QStringLiteral("code"), QStringLiteral("text-save"), QStringLiteral("Preview a format-preserving save from --input; --write requires --expected-sha256 from text-info."), {QStringLiteral("vibestudio --cli code text-save ./scripts/game.qc --input ./edited.qc --dry-run --json")}, true, true, true},
		{QStringLiteral("code"), QStringLiteral("text-create"), QStringLiteral("Preview creating an empty text file or importing --input; --write commits and existing destinations require --expected-sha256."), {QStringLiteral("vibestudio --cli code text-create ./scripts/new.qc --dry-run --json")}, true, true, true},
		{QStringLiteral("code"), QStringLiteral("recoveries"), QStringLiteral("Inspect local text recovery copies; --directory selects a recovery folder."), {QStringLiteral("vibestudio --cli code recoveries --json")}},
		{QStringLiteral("code"), QStringLiteral("text-recover"), QStringLiteral("Preview exporting a verified recovery copy to --output; --write commits and existing destinations require --expected-sha256."), {QStringLiteral("vibestudio --cli code text-recover ./copy.vstextrecovery --output ./restored.qc --dry-run --json")}, true, true, true},
		{QStringLiteral("code"), QStringLiteral("text-save-as"), QStringLiteral("Preview a format-preserving copy to --output, optionally using edited --input; writing over an existing destination requires its --expected-sha256."), {QStringLiteral("vibestudio --cli code text-save-as ./scripts/game.qc --output ./scripts/copy.qc --dry-run --json")}, true, true, true},
		{QStringLiteral("localization"), QStringLiteral("report"), QStringLiteral("Print localization targets, pseudo-localization, RTL smoke coverage, locale formatting, and catalog status."), {QStringLiteral("vibestudio --cli localization report --locale ar --json")}},
		{QStringLiteral("localization"), QStringLiteral("targets"), QStringLiteral("List the documented localization target set."), {QStringLiteral("vibestudio --cli localization targets")}},
		{QStringLiteral("diagnostics"), QStringLiteral("bundle"), QStringLiteral("Export a redacted diagnostic bundle for support, QA, and reproducibility."), {QStringLiteral("vibestudio --cli diagnostics bundle --output ./diagnostics")}, true},
		{QStringLiteral("diagnostics"), QStringLiteral("crashes"), QStringLiteral("List the crash reports kept on this machine, newest first, with each one's time, reason, and report path."), {QStringLiteral("vibestudio --cli diagnostics crashes --json")}},
		{QStringLiteral("extension"), QStringLiteral("discover"), QStringLiteral("Discover VibeStudio extension manifests and report trust and sandbox metadata."), {QStringLiteral("vibestudio --cli extension discover ./extensions --json")}},
		{QStringLiteral("extension"), QStringLiteral("inspect"), QStringLiteral("Inspect a VibeStudio extension manifest."), {QStringLiteral("vibestudio --cli extension inspect ./extensions/tool/vibestudio.extension.json")}},
		{QStringLiteral("extension"), QStringLiteral("run"), QStringLiteral("Build or execute an approved extension command plan with generated-file staging."), {QStringLiteral("vibestudio --cli extension run ./extensions/tool/vibestudio.extension.json build --dry-run --json")}, true, true, true},
		{QStringLiteral("compiler"), QStringLiteral("list"), QStringLiteral("Print compiler registry and executable discovery."), {QStringLiteral("vibestudio --cli compiler list --json")}},
		{QStringLiteral("compiler"), QStringLiteral("profiles"), QStringLiteral("Print compiler wrapper profiles."), {QStringLiteral("vibestudio --cli compiler profiles")}},
		{QStringLiteral("compiler"), QStringLiteral("plan"), QStringLiteral("Build a reviewable compiler command plan."), {QStringLiteral("vibestudio --cli compiler plan ericw-qbsp --input ./maps/start.map --dry-run")}, true, true, true},
		{QStringLiteral("compiler"), QStringLiteral("manifest"), QStringLiteral("Print or write a compiler command manifest."), {QStringLiteral("vibestudio --cli compiler manifest ericw-qbsp --input ./maps/start.map --manifest ./build/start.compiler.json")}, true, true, true},
		{QStringLiteral("compiler"), QStringLiteral("run"), QStringLiteral("Execute a compiler command with logs, diagnostics, task state, and manifest capture."), {QStringLiteral("vibestudio --cli compiler run ericw-qbsp --input ./maps/start.map --watch --manifest ./build/start.run.json")}, true, true, true, true},
		{QStringLiteral("compiler"), QStringLiteral("rerun"), QStringLiteral("Re-run a saved compiler command manifest."), {QStringLiteral("vibestudio --cli compiler rerun ./build/start.run.json --watch")}, true, true, false, true},
		{QStringLiteral("compiler"), QStringLiteral("copy-command"), QStringLiteral("Print shell-ready command line from a manifest or profile."), {QStringLiteral("vibestudio --cli compiler copy-command ericw-qbsp --input ./maps/start.map")}},
		{QStringLiteral("ai"), QStringLiteral("status"), QStringLiteral("Print AI preferences, credentials, models, tools, and connector metadata."), {QStringLiteral("vibestudio --cli ai status --json")}},
		{QStringLiteral("ai"), QStringLiteral("tools"), QStringLiteral("Print AI-callable VibeStudio tool descriptors."), {QStringLiteral("vibestudio --cli ai tools")}},
		{QStringLiteral("ai"), QStringLiteral("explain-log"), QStringLiteral("Explain a compiler log as a reviewable, no-write AI workflow."), {QStringLiteral("vibestudio --cli ai explain-log --log ./build/qbsp.log --json")}},
		{QStringLiteral("ai"), QStringLiteral("propose-command"), QStringLiteral("Propose a reviewable compiler command from natural language."), {QStringLiteral("vibestudio --cli ai propose-command --prompt \"build quake map maps/start.map\"")}},
		{QStringLiteral("ai"), QStringLiteral("propose-manifest"), QStringLiteral("Draft a project manifest without writing files."), {QStringLiteral("vibestudio --cli ai propose-manifest ./mymod --name \"My Mod\"")}},
		{QStringLiteral("ai"), QStringLiteral("package-deps"), QStringLiteral("Suggest missing package dependencies from metadata."), {QStringLiteral("vibestudio --cli ai package-deps ./release.pk3")}},
		{QStringLiteral("ai"), QStringLiteral("cli-command"), QStringLiteral("Generate a safe CLI command proposal."), {QStringLiteral("vibestudio --cli ai cli-command --prompt \"validate pak0.pak\"")}},
		{QStringLiteral("ai"), QStringLiteral("fix-plan"), QStringLiteral("Generate a supervised fix-and-retry plan from compiler output."), {QStringLiteral("vibestudio --cli ai fix-plan --log ./build/qbsp.log --command \"vibestudio --cli compiler run ericw-qbsp --input maps/start.map\"")}},
		{QStringLiteral("ai"), QStringLiteral("asset-request"), QStringLiteral("Stage an ElevenLabs/Meshy/OpenAI asset generation request before import."), {QStringLiteral("vibestudio --cli ai asset-request --provider meshy --kind texture --prompt \"rusty sci-fi panel\"")}, true, true, true},
		{QStringLiteral("ai"), QStringLiteral("compare"), QStringLiteral("Prepare side-by-side provider output comparison metadata."), {QStringLiteral("vibestudio --cli ai compare --provider-a openai --provider-b claude --prompt \"explain this build failure\"")}},
		{QStringLiteral("ai"), QStringLiteral("shader-scaffold"), QStringLiteral("Generate a staged prompt-to-shader scaffold proposal."), {QStringLiteral("vibestudio --cli ai shader-scaffold --prompt \"glowing gothic wall\" --json")}, true, true, true},
		{QStringLiteral("ai"), QStringLiteral("entity-snippet"), QStringLiteral("Generate a staged prompt-to-entity-definition snippet."), {QStringLiteral("vibestudio --cli ai entity-snippet --prompt \"trigger starts lift\" --json")}, true, true, true},
		{QStringLiteral("ai"), QStringLiteral("package-plan"), QStringLiteral("Generate a staged prompt-to-package-validation plan."), {QStringLiteral("vibestudio --cli ai package-plan --prompt \"release check\" --package ./release.pk3 --json")}, true, true, true},
		{QStringLiteral("ai"), QStringLiteral("batch-recipe"), QStringLiteral("Generate a staged prompt-to-batch-conversion recipe."), {QStringLiteral("vibestudio --cli ai batch-recipe --prompt \"convert doom sprites\" --json")}, true, true, true},
		{QStringLiteral("ai"), QStringLiteral("review"), QStringLiteral("Render the AI proposal review surface for a generated workflow."), {QStringLiteral("vibestudio --cli ai review --prompt \"glowing shader\" --kind shader --json")}, true, true, true},
		{QStringLiteral("ai"), QStringLiteral("ask"), QStringLiteral("Ask the configured text model a question, with files as context. --dry-run shows the request; an off-machine send needs --yes."), {QStringLiteral("vibestudio --cli ai ask --prompt \"why does qbsp report a leak?\" --context-file build/qbsp.log --dry-run"), QStringLiteral("vibestudio --cli ai ask --provider local-offline --model llama3.2 --prompt \"explain this script\" --context-file progs/sentry.qc")}, true, true, true},
		{QStringLiteral("ai"), QStringLiteral("test-connection"), QStringLiteral("Ask the configured text model to reply OK, and report who answered and how fast."), {QStringLiteral("vibestudio --cli ai test-connection --json"), QStringLiteral("vibestudio --cli ai test-connection --provider claude")}},
		{QStringLiteral("ai"), QStringLiteral("image"), QStringLiteral("Draw pictures with the configured image model (OpenAI, Gemini, or a local Stable Diffusion web UI); --source edits a picture. --dry-run shows the request; an off-machine send needs --yes."), {QStringLiteral("vibestudio --cli ai image --prompt \"concept art of a slipgate chamber\" --output ./concepts/ --count 2 --dry-run"), QStringLiteral("vibestudio --cli ai image --provider local-offline --prompt \"stone gargoyle\" --tileable --output ./gargoyle.png")}, true, true, true},
		{QStringLiteral("about"), QStringLiteral("show"), QStringLiteral("Print version, platform, imported compiler, and credits metadata."), {QStringLiteral("vibestudio --cli about --json")}},
		{QStringLiteral("install"), QStringLiteral("list"), QStringLiteral("List saved manual game installation profiles."), {QStringLiteral("vibestudio --cli install list --json")}},
		{QStringLiteral("install"), QStringLiteral("detect"), QStringLiteral("Detect Steam and GOG installation candidates read-only."), {QStringLiteral("vibestudio --cli install detect --root \"D:/SteamLibrary\" --json")}},
		{QStringLiteral("install"), QStringLiteral("add"), QStringLiteral("Add or update a manual game installation profile."), {QStringLiteral("vibestudio --cli install add \"C:/Games/Quake\" --install-game quake --install-name \"Quake\" --dry-run")}, true, true, true},
		{QStringLiteral("install"), QStringLiteral("select"), QStringLiteral("Mark a saved installation profile as the selected one."), {QStringLiteral("vibestudio --cli install select quake-games-quake")}},
		{QStringLiteral("install"), QStringLiteral("validate"), QStringLiteral("Validate a saved installation profile read-only."), {QStringLiteral("vibestudio --cli install validate quake-games-quake --json")}},
		{QStringLiteral("install"), QStringLiteral("remove"), QStringLiteral("Remove a saved installation profile without touching game files."), {QStringLiteral("vibestudio --cli install remove quake-games-quake --dry-run")}, true, true, true},
		{QStringLiteral("editor"), QStringLiteral("view-links"), QStringLiteral("Inspect or set linked plan centres, plan zoom and camera-follow defaults with --centers, --zoom and --follow-camera on|off."), {QStringLiteral("vibestudio --cli editor view-links --json"), QStringLiteral("vibestudio --cli editor view-links --centers on --zoom on --follow-camera off")}},
		{QStringLiteral("editor"), QStringLiteral("bookmarks"), QStringLiteral("List, import, export, rename or remove named level views. WADs require --map-name; import replacement requires --replace and export replacement --overwrite."), {QStringLiteral("vibestudio --cli editor bookmarks list arena.map --json"), QStringLiteral("vibestudio --cli editor bookmarks import arena.map --input arena.vviews"), QStringLiteral("vibestudio --cli editor bookmarks export doom.wad --map-name MAP01 --output map01.vviews")}},
		{QStringLiteral("editor"), QStringLiteral("scene"), QStringLiteral("List, create, rename, move, assign, change visibility, lock, remove or reset map layers and groups. Use lock --id <UUID> --locked true|false. Changes require --output; use --dry-run to validate. WADs require --map-name; node IDs are UUIDs or default."), {QStringLiteral("vibestudio --cli editor scene list arena.map --json"), QStringLiteral("vibestudio --cli editor scene create arena.map --kind layer --name Architecture --output organized.map --dry-run")}, true, true, true},
		{QStringLiteral("editor"), QStringLiteral("profiles"), QStringLiteral("List editor interaction profiles and their bindings."), {QStringLiteral("vibestudio --cli editor profiles --json")}},
		{QStringLiteral("editor"), QStringLiteral("current"), QStringLiteral("Print the selected editor interaction profile."), {QStringLiteral("vibestudio --cli editor current")}},
		{QStringLiteral("editor"), QStringLiteral("select"), QStringLiteral("Select an editor interaction profile."), {QStringLiteral("vibestudio --cli editor select trenchbroom")}},
		{QStringLiteral("editor"), QStringLiteral("gestures"), QStringLiteral("Inspect, customize, reset, import or export per-profile pointer gestures and camera keys with validation."), {QStringLiteral("vibestudio --cli editor gestures hammer --json"), QStringLiteral("vibestudio --cli editor gestures hammer --set plan.panButtons=right+middle --dry-run --json")}},
		{QStringLiteral("editor"), QStringLiteral("layout"), QStringLiteral("Read or choose the level view layout: profile, single-2d, single-3d, camera-and-plan or four-views."), {QStringLiteral("vibestudio --cli editor layout four-views --json"), QStringLiteral("vibestudio --cli editor layout profile")}},
		{QStringLiteral("editor"), QStringLiteral("controls"), QStringLiteral("Print how the Levels views answer the mouse and keys under an editor profile: layout, 2D view, 3D camera, and keys."), {QStringLiteral("vibestudio --cli editor controls trenchbroom"), QStringLiteral("vibestudio --cli editor controls netradiant-custom --json")}},
		{QStringLiteral("editor"), QStringLiteral("keys"), QStringLiteral("List the keys the user gave commands in place of their defaults; --reset puts every default back."), {QStringLiteral("vibestudio --cli editor keys --json"), QStringLiteral("vibestudio --cli editor keys --reset")}},
		{QStringLiteral("compiler"), QStringLiteral("set-path"), QStringLiteral("Store a user compiler executable override."), {QStringLiteral("vibestudio --cli compiler set-path ericw-qbsp --executable /opt/ericw-tools/bin/qbsp")}},
		{QStringLiteral("compiler"), QStringLiteral("clear-path"), QStringLiteral("Remove a user compiler executable override."), {QStringLiteral("vibestudio --cli compiler clear-path ericw-qbsp")}},
		{QStringLiteral("ai"), QStringLiteral("connectors"), QStringLiteral("List provider-neutral AI connector descriptors and capabilities."), {QStringLiteral("vibestudio --cli ai connectors --json")}},
		{QStringLiteral("map"), QStringLiteral("render"), QStringLiteral("Render a deterministic SVG picture of a Doom or Quake-family map, optionally with a compiler leak trail."), {QStringLiteral("vibestudio --cli map render ./maps/start.map --output ./docs/start.svg --projection top --overwrite"), QStringLiteral("vibestudio --cli map render ./maps/start.map --leak ./maps/start.pts --output ./start-leak.svg"), QStringLiteral("vibestudio --cli map render ./maps/start.map --links --labels --output ./start-logic.svg")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("textures"), QStringLiteral("Check map textures against an archive, saved package draft, or up to 64 asset roots with shared indexing limits; missing references or incomplete audits return exit 4. Or list the objects that use one texture."), {QStringLiteral("vibestudio --cli map textures ./maps/start.map --package ./id1/pak0.pak --json"), QStringLiteral("vibestudio --cli map textures ./maps/start.map --uses base/wall")}},
		{QStringLiteral("map"), QStringLiteral("dependencies"), QStringLiteral("Resolve explicit level textures, shader images, models, and sounds from an archive, folder or .vibepackage draft, with map object attribution."), {QStringLiteral("vibestudio --cli map dependencies ./maps/arena.map --package ./assets --engine idTech3 --json")}},
		{QStringLiteral("map"), QStringLiteral("materials"), QStringLiteral("Resolve camera materials and placed models, including MD3 misc_model compiler skins, remaps, frame diagnostics, omitted surfaces and exact skin hashes; accepts staged .vibepackage inputs. Doom composites retain exact source inputs; --geometry also checks the camera mesh."), {QStringLiteral("vibestudio --cli map materials ./maps/arena.map --package ./assets --engine idTech3 --json"), QStringLiteral("vibestudio --cli map materials ./room.wad --map-name MAP01 --package ./resources.wad --geometry --json")}},
		{QStringLiteral("map"), QStringLiteral("find"), QStringLiteral("List the map objects a query matches, as the Levels Objects filter reads it: key=value, key:text, key!=value, key<n, key>n, and plain words, all of which must hold."), {QStringLiteral("vibestudio --cli map find ./maps/start.map --where \"class=light light>200\""), QStringLiteral("vibestudio --cli map find ./maps/doom.wad --map MAP01 --where tag=3 --json")}},
		{QStringLiteral("entity"), QStringLiteral("definitions"), QStringLiteral("Load Radiant .def, Valve .fgd, and Quake III .ent catalogues and list the entity classes they declare."), {QStringLiteral("vibestudio --cli entity definitions ./defs --json")}},
		{QStringLiteral("entity"), QStringLiteral("validate"), QStringLiteral("Check a map's entities against an entity definition catalogue, including target and targetname references."), {QStringLiteral("vibestudio --cli entity validate ./maps/start.map --definitions ./defs/quake.def --json")}},
		{QStringLiteral("model"), QStringLiteral("inspect"), QStringLiteral("Decode MDL, MD2, MD3, or polygonal OBJ from a loose file, archive, folder, or portable draft; report geometry, frames, tags, and skins."), {QStringLiteral("vibestudio --cli model inspect ./id1/pak0.pak progs/player.mdl --json")}},
		{QStringLiteral("model"), QStringLiteral("export"), QStringLiteral("Export one OBJ geometry frame with guarded writes and source/package protection. Supports portable drafts, dry-run, stdout, and omission notes."), {QStringLiteral("vibestudio --cli model export ./id1/pak0.pak progs/player.mdl --frame 0 --output ./out/player.obj --dry-run")}, true, true, true},
		{QStringLiteral("model"), QStringLiteral("materials"), QCoreApplication::translate("ModelMaterialSlotsCli", "Inspect model preview appearances using an archive, folder or portable draft with bounded image and shader lookup. Requires --package. Pair --surface N with --material-slot N for an external surface, use --skin N and --member N for an embedded MDL skin member, or use --entry PATH and/or --entry-index N for exact package .skin bindings. Appearance modes are separate. JSON includes selected surfaces and verified skin inputs. This read-only command does not change authored bindings."), {QStringLiteral("vibestudio --cli model materials ./models/prop.mesh.json --package ./assets --surface 0 --material-slot 1 --json")}},
		{QStringLiteral("model"), QStringLiteral("slots"), QCoreApplication::translate("ModelMaterialSlotsCli", "List ordered external material slots, optionally with --surface N. Edits require --surface N and --output .mesh.json. Use --operation set|insert with --slot N --material PATH, remove with --slot N, move with --slot N --to N (final index), replace with repeated --material PATH, or clear. Slot zero is primary. Insert permits the slot count to append. --dry-run validates; --overwrite permits replacing an existing output."), {QStringLiteral("vibestudio --cli model slots ./models/prop.mesh.json --json"), QStringLiteral("vibestudio --cli model slots ./models/prop.mesh.json --surface 0 --operation move --slot 1 --to 0 --output ./models/variant.mesh.json --dry-run --json")}, true, true, true},
		{QStringLiteral("model"), QStringLiteral("surfaces"), QCoreApplication::translate("ModelSurfacesCli", "List or edit model surfaces across every animation pose. Use --operation rename|separate|move|duplicate|delete|join, --surface N for a source, --name for a new name, --faces all or comma-separated indices for separate/move, and --surfaces indices with --target-surface N for join. Move also requires --target-surface. Different material slots require --adopt-target-materials. Edits require --output .mesh.json; --dry-run validates and --overwrite permits replacing an existing output."), {QStringLiteral("vibestudio --cli model surfaces ./models/prop.mesh.json --json"), QStringLiteral("vibestudio --cli model surfaces ./models/prop.mesh.json --operation separate --surface 0 --faces 0,1 --name panel --output ./models/parts.mesh.json --dry-run --json")}, true, true, true},
		{QStringLiteral("model"), QStringLiteral("skin"), QCoreApplication::translate("ModelSkinBindingsCli", "Apply Quake III .skin assignments to every surface's primary material in an editable model. Choose --file or --package with --entry/--entry-index; package drafts are supported. Requires --output .mesh.json; --dry-run validates without writing and existing outputs require --overwrite. Unused bindings and tag markers are reported."), {QStringLiteral("vibestudio --cli model skin ./models/prop.mesh.json --file ./models/prop.skin --output ./models/skinned.mesh.json --dry-run --json")}, true, true, true},
		{QStringLiteral("model"), QStringLiteral("topology"), QStringLiteral("Inspect indexed edges, duplicates, unused vertices, disconnected fans, winding, and boundaries on one model surface."), {QStringLiteral("vibestudio --cli model topology ./models/prop.mesh.json --surface 0 --json")}},
		{QStringLiteral("model"), QStringLiteral("repair-import"), QCoreApplication::translate("ModelImportRepairCli", "Review a repair for .mesh.json, MDL, MD2 or MD3. Remove unusable faces across all poses, rebuild unusable normals and remove orphaned seams while retaining other data. Without --output this is read-only. --output requires a new .mesh.json; --dry-run validates without writing. Existing files and changed inputs are protected."), {QStringLiteral("vibestudio --cli model repair-import ./models/damaged.mesh.json --json"), QStringLiteral("vibestudio --cli model repair-import ./models/damaged.md3 --output ./models/repaired.mesh.json --dry-run --json")}, true, true, true},
		{QStringLiteral("model"), QStringLiteral("intersections"), QCoreApplication::translate("ModelIntersectionsCli", "Inspect geometric face crossings and coplanar overlaps across surfaces and stored poses. Optional --frame all|N and --surface all|N default to all. A surface filter retains contacts with other surfaces. Shared boundaries and isolated point contacts are allowed; limits fail without a partial report. This read-only command does not repair geometry or inspect motion between poses."), {QStringLiteral("vibestudio --cli model intersections ./models/prop.mesh.json --frame all --json")}},
		{QStringLiteral("model"), QStringLiteral("uv"), QStringLiteral("Inspect UV islands, bounds, component indices, and marked seams for one model surface."), {QStringLiteral("vibestudio --cli model uv ./models/prop.mesh.json --surface 0 --json")}},
		{QStringLiteral("model"), QStringLiteral("tags"), QStringLiteral("Inspect named attachment origins and orientation axes by frame; supports --frame all|N and --tag name."), {QStringLiteral("vibestudio --cli model tags ./models/prop.mesh.json --frame all --json")}},
		{QStringLiteral("model"), QStringLiteral("animations"), QStringLiteral("List saved animation clips by index with inclusive frame ranges; optionally select --clip N."), {QStringLiteral("vibestudio --cli model animations ./models/prop.mesh.json --json")}},
		{QStringLiteral("model"), QStringLiteral("collision"), QCoreApplication::translate("ModelCollisionCli", "Author static or animated collision boxes with --operation inspect|add|fit|fit-animated|animate|freeze|update|transform|duplicate|delete|export-map|place. Use --name, --box, --centre X,Y,Z, --size X,Y,Z and --rotation X,Y,Z. Transform uses --box with --offset/--rotate X,Y,Z in --transform-space world|selection|custom (default world), custom --axis-rotation X,Y,Z, and intrinsic local --scale X,Y,Z, --pivot-mode selection|origin|custom, custom --pivot X,Y,Z and --snap-grid/--snap-angle/--snap-scale. The default pivot is selection centre. Static fitting accepts --frame all|N; fit-animated fits every pose. Both accept optional --surface with --vertices, --faces or --edges. Animate copies a static --box into every frame; freeze keeps --frame N. Animated update/transform require explicit --frame all|N; omitted update fields retain their per-pose values. Inspect accepts --frame N. Export/placement of animated boxes require --frame N and produce static brushes. Export/placement require --target quake|quake2|quake3; Quake III also requires --material for a project clip shader. Use --origin X,Y,Z and --map for placement into a separate output map. All mutations require --output; --dry-run validates without writing. Existing derivatives require --overwrite."), {QStringLiteral("vibestudio --cli model collision ./models/prop.mesh.json --operation fit --name body --output ./models/collision.mesh.json --dry-run --json"), QStringLiteral("vibestudio --cli model collision ./models/collision.mesh.json --operation export-map --target quake3 --material common/clip --output ./maps/prop-clip.map --json")}, true, true, true},
		{QStringLiteral("model"), QStringLiteral("assembly"), QCoreApplication::translate("ModelAssemblyCli", "Inspect or edit a linked .assembly.json recipe, independently sample each part, or bake composed poses. Animation baking uses --operation bake-animation with --frames, --sample-fps, optional --time and --clip-name; output supports .mesh.json, .md2 and .md3 with normal format limits. Sampling is start + index / FPS; editable sources retain clip timing. Supports --operation inspect|add|update|remove|bake|bake-animation, --new, --part, --model, --kind file|package, --parent, --tag, --translation X,Y,Z, --rotation X,Y,Z, --scale, --first-frame, --last-frame, --fps, --phase, --loop on|off, --interpolate on|off, --rename-to, --time, --package and --palette. Mutations require --output. Part add/update accepts --skin PATH, --skin-kind file|package (default file), and package-only --skin-entry-index N. An omitted index requires a unique package path. Omitted skin options preserve the link; update --clear-skin removes it. Linked skins use assembly schema 3, drive preview/bake materials, and retain original model files. JSON includes verified skin inputs and material assignments. Quake III animation.cfg uses animation-set with --config, --lower-part, --upper-part and optional --lower-animation/--upper-animation native slot names. Existing bindings retain omitted choices. animation-export writes .cfg; animation-clear removes the binding. All require --output and support --dry-run. Native clip selections drive inspection and bakes with lower frame offsets, reverse and loop tails. Player publication: player-review and player-export require --player-name ID, --head-part ID, --icon PATH and --package PATH. Optional --skin-name defaults to default; --icon-kind file|package defaults to file; --icon-entry-index selects an exact package image. Review captures separate lower/upper/head MD3 models, native skins, animation.cfg, a TGA icon and shader/image dependencies. Export requires --output .pk3 and supports --overwrite and --dry-run. It uses protected atomic publication and verifies deterministic bytes. Source models and assembly stay unchanged. Recovery operations: recoveries lists verified copies; recover and discard require --recovery UUID and --sha256 DIGEST. All three accept --directory. Recover requires --output and protects the original source; discard refuses active editors. Both support --dry-run."), {QStringLiteral("vibestudio --cli model assembly --new --part root --model ./models/body.md3 --output ./models/player.assembly.json --dry-run --json"), QStringLiteral("vibestudio --cli model assembly ./models/player.assembly.json --operation bake --time 0.5 --output ./out/player.obj --dry-run --json"), QStringLiteral("vibestudio --cli model assembly --operation recoveries --json")}, true, true, true},
		{QStringLiteral("model"), QStringLiteral("mdl"), QStringLiteral("Inspect or edit native MDL indexed skins, frame groups, timing, palette and header settings. Skin sources use --image or --package with one --entry or --entry-index, including saved drafts; --palette selects package palette paths. Use --time <seconds> with --native-frame, --skin, --timing stored|glquake and --sync-phase for read-only playback sampling."), {QStringLiteral("vibestudio --cli model mdl ./models/prop.mesh.json --json"), QStringLiteral("vibestudio --cli model mdl ./models/prop.mesh.json --operation add-skin --image ./skins/prop.pcx --output ./out/prop.mesh.json --dry-run --json"), QStringLiteral("vibestudio --cli model mdl ./models/prop.mesh.json --operation add-skin --package ./textures.vibepackage --entry textures/prop.png --output ./out/prop.mesh.json --dry-run --json")}, true, true, true},
		{QStringLiteral("model"), QStringLiteral("recoveries"), QStringLiteral("List local mesh recovery headers; --directory selects a recovery folder."), {QStringLiteral("vibestudio --cli model recoveries --json")}},
		{QStringLiteral("model"), QStringLiteral("recover"), QStringLiteral("Verify a mesh recovery copy and write a new editable source; existing files are protected."), {QStringLiteral("vibestudio --cli model recover ./copy.vsmeshrecovery --output ./out/recovered.mesh.json --dry-run --json")}, true, true, true},
		{QStringLiteral("model"), QStringLiteral("build"), QStringLiteral("Build MDL, MD2, MD3 or an OBJ frame from an editable mesh; primitive designs support MD2, MD3 and OBJ."), {QStringLiteral("vibestudio --cli model build ./samples/models/pillar.model.json --output ./out/pillar.md3 --dry-run --json")}, true, true, true},
		{QStringLiteral("model"), QStringLiteral("import"), QStringLiteral("Import polygonal OBJ, MDL, MD2, MD3, or bake a primitive design into an editable mesh source. OBJ retains UV/normal seams and direct material paths; material libraries and non-polygon data require conversion in the source modeller."), {QStringLiteral("vibestudio --cli model import ./samples/models/pillar.model.json --output ./out/pillar.mesh.json --dry-run --json")}, true, true, true},
		{QStringLiteral("model"), QStringLiteral("edit"), QCoreApplication::translate("ModelSurfaceSelection", "Apply a validated mesh, UV, material, animation-frame, or attachment-tag edit. Transforms support --transform-space world|selection|custom (default world), custom --axis-rotation X,Y,Z and selection --axes-frame N. Axes also apply to face extrusion and duplication. One reference pose fixes selection axes across affected frames; default is the edited frame or pivot reference pose. Pivots remain in model coordinates. Transforms support snapping, explicit pivots, and --surfaces all or unique comma-separated indices for whole surfaces sharing one pivot. Do not mix --surfaces with component selectors. Boundary operations fill-boundary-loops and bridge-boundary-loops require --edges and cover all frames; --source-frame chooses a reference pose (default 0). Bridging joins exactly two loops, supports unequal counts and accepts --bridge-twist -1023 to 1023 (default 0). UV atlas operations accept --uv-atlas-size N or WIDTHxHEIGHT and --uv-padding pixels. uv-pack-around packs complete islands around fixed unselected and shared-material regions in the 0–1 tile; --uv-pack-scale fit|preserve chooses uniform fit (default) or unchanged UV scale. Clip timing: set-clip-fps requires --clip and --clip-fps; add-clip optionally accepts --clip-fps. Zero leaves timing unspecified, otherwise use 0.001–1000 FPS."), {QStringLiteral("vibestudio --cli model edit ./out/pillar.mesh.json --operation transform --faces all --offset 0,0,8 --snap-grid 1 --output ./out/raised.mesh.json --dry-run --json")}, true, true, true},
		{QStringLiteral("map"), QStringLiteral("place-model"), QStringLiteral("Place a package MD3 in a Quake III map as a misc_model entity."), {QStringLiteral("vibestudio --cli map place-model ./maps/arena.map --engine idTech3 --package ./assets --entry models/props/pillar.md3 --origin 64,0,0 --output ./out/arena.map")}, true, true, true},
		{QStringLiteral("bsp"), QStringLiteral("inspect"), QStringLiteral("Inspect a compiled BSP plus any leak and portal files beside it."), {QStringLiteral("vibestudio --cli bsp inspect ./out/start.bsp --json")}},
		{QStringLiteral("build"), QStringLiteral("list"), QStringLiteral("List chained build pipelines and their stages."), {QStringLiteral("vibestudio --cli build list --json")}},
		{QStringLiteral("build"), QStringLiteral("artifacts"), QStringLiteral("Verify the captured inputs and last successful compiler output inventory."), {QStringLiteral("vibestudio --cli build artifacts ./builds/arena --json")}},
		{QStringLiteral("build"), QStringLiteral("deploy-plan"), QStringLiteral("Review a verified prepared build, installation, package destination and windowed launch command."), {QStringLiteral("vibestudio --cli build deploy-plan ./builds/arena --installation quake3 --mod studio --json")}},
		{QStringLiteral("build"), QStringLiteral("deploy-prepared"), QStringLiteral("Deploy a verified complete PAK or PK3 into an installation; optionally launch after publication."), {QStringLiteral("vibestudio --cli build deploy-prepared ./builds/arena --installation quake3 --mod studio --dry-run --json")}, true, true, true},
		{QStringLiteral("build"), QStringLiteral("publish-prepared"), QStringLiteral("Publish captured assets, BSP and engine runtime lighting as a verified PAK or PK3; optionally include build sources."), {QStringLiteral("vibestudio --cli build publish-prepared ./builds/arena --output ./arena.pk3 --dry-run --json")}, true, true, true},
		{QStringLiteral("build"), QStringLiteral("prepare"), QStringLiteral("Capture a Quake, Quake II or Quake III map and package draft as a new compiler workspace with verified input hashes."), {QStringLiteral("vibestudio --cli build prepare ./maps/arena.map --package ./assets.vibepackage --output ./builds/arena --json")}, true, true, true},
		{QStringLiteral("build"), QStringLiteral("run-prepared"), QStringLiteral("Verify a prepared workspace and run its Quake III compiler pipeline with captured assets."), {QStringLiteral("vibestudio --cli build run-prepared ./builds/arena --pipeline quake3-full --dry-run --json")}, true, true, true},
		{QStringLiteral("build"), QStringLiteral("plan"), QStringLiteral("Plan a chained build pipeline without running anything."), {QStringLiteral("vibestudio --cli build plan quake-full --input ./maps/start.map --json")}, true, true, true},
		{QStringLiteral("build"), QStringLiteral("run"), QStringLiteral("Run a chained build pipeline stage by stage with logs, diagnostics, and manifests."), {QStringLiteral("vibestudio --cli build run quake-full --input ./maps/start.map --watch")}, true, true, true, true},
		{QStringLiteral("launch"), QStringLiteral("plan"), QStringLiteral("Build a reviewable game launch command line without starting anything; --deploy also shows where the built map would be copied."), {QStringLiteral("vibestudio --cli launch plan --map start --json"), QStringLiteral("vibestudio --cli launch plan --bsp maps/start.bsp --deploy")}, true, true, true},
		{QStringLiteral("launch"), QStringLiteral("run"), QStringLiteral("Start the configured game installation with the planned command line; --deploy first copies the built map into the game folder."), {QStringLiteral("vibestudio --cli launch run --map start"), QStringLiteral("vibestudio --cli launch run --bsp maps/start.bsp --deploy --allow-test-maps")}, true, true, false},
		{QStringLiteral("texture"), QStringLiteral("decode"), QStringLiteral("Decode an idTech texture, flat, sprite, or image entry and optionally write a PNG."), {QStringLiteral("vibestudio --cli texture decode ./id1/pak0.pak progs/player.mdl --output ./out/player.png --dry-run")}, true, true, true},
		{QStringLiteral("texture"), QStringLiteral("palette"), QStringLiteral("Resolve the palette used to decode indexed art and report where it came from."), {QStringLiteral("vibestudio --cli texture palette ./id1/pak0.pak --palette quake --json")}},
		{QStringLiteral("texture"), QStringLiteral("create"), QStringLiteral("Create a bounded PNG or layered texture project with optional ordered operations."), {QStringLiteral("vibestudio --cli texture create --size 64x64 --color transparent --output ./texture.vtexture --dry-run --json")}, true, true, true},
		{QStringLiteral("texture"), QStringLiteral("edit"), QStringLiteral("Apply an ordered recipe to an image, layered project, or package texture; write PNG or .vtexture."), {QStringLiteral("vibestudio --cli texture edit ./texture.vtexture --operations ./edits.json --output ./edited.vtexture --dry-run --json")}, true, true, true},
		{QStringLiteral("texture"), QStringLiteral("inspect"), QStringLiteral("Validate a native texture project and report layers, metadata, dimensions, and source fingerprint."), {QStringLiteral("vibestudio --cli texture inspect ./texture.vtexture --json")}},
		{QStringLiteral("texture"), QStringLiteral("profiles"), QStringLiteral("List texture output profiles and the versioned export option defaults."), {QStringLiteral("vibestudio --cli texture profiles --json")}},
		{QStringLiteral("texture"), QStringLiteral("export"), QStringLiteral("Export an image, layered project, or package texture using an explicit game profile and guarded publication."), {QStringLiteral("vibestudio --cli texture export ./texture.vtexture --profile quake2-wal --export-options ./wal.json --output ./texture.wal --dry-run --json")}, true, true, true},
		{QStringLiteral("texture"), QStringLiteral("stage"), QStringLiteral("Encode a texture into an undoable package draft, preserving native WAD namespaces and lump types."), {QStringLiteral("vibestudio --cli texture stage ./flat.vtexture --profile doom-flat --target-package ./mod.wad --target-entry NEWFLAT --output ./mod.vibepackage --dry-run --json")}, true, true, true},
		{QStringLiteral("texture"), QStringLiteral("validate"), QStringLiteral("Encode and validate a texture profile without writing, reporting alpha, palette, mipmaps and engine limits."), {QStringLiteral("vibestudio --cli texture validate ./texture.vtexture --profile tga --json")}},
		{QStringLiteral("texture"), QStringLiteral("recoveries"), QStringLiteral("Inspect bounded local texture checkpoints without following their recorded source paths."), {QStringLiteral("vibestudio --cli texture recoveries ./texture-recovery --json")}},
		{QStringLiteral("texture"), QStringLiteral("recover"), QStringLiteral("Restore a texture checkpoint to an explicitly chosen new native project file."), {QStringLiteral("vibestudio --cli texture recover ./draft.vtrecovery --output ./recovered.vtexture --dry-run --json")}, true, true, true},
		{QStringLiteral("texture"), QStringLiteral("generate"), QStringLiteral("Generate a seamless game texture with your image model, or from --from-image without AI, and write it as the game reads it: WAD2 miptex, WAL, TGA and shader, or Doom flat/patch, with source-port companion maps."), {QStringLiteral("vibestudio --cli texture generate --prompt \"rusted riveted metal plate\" --game quake --palette-root ./id1 --companions --dry-run"), QStringLiteral("vibestudio --cli texture generate --from-image ./photo.png --game doom --surface floor --palette-root ./doom2 --wad ./textures.wad")}, true, true, true},
		{QStringLiteral("texture"), QStringLiteral("derive"), QStringLiteral("Derive the normal, gloss, and glow companion maps source ports read beside a texture, without AI."), {QStringLiteral("vibestudio --cli texture derive --input ./textures/metal1_1.png --game quake --json")}, true, true, true},
		{QStringLiteral("credits"), QStringLiteral("validate"), QStringLiteral("Validate README/docs credits, compiler pins, .gitmodules, and checked-out submodule revisions."), {QStringLiteral("vibestudio --cli credits validate --json")}},
	};
}

CliOutputFormat outputFormat(const QStringList& args)
{
	return args.contains(QStringLiteral("--json")) ? CliOutputFormat::Json : CliOutputFormat::Text;
}

QString nativePath(const QString& path)
{
	return QDir::toNativeSeparators(path);
}

QString settingsStatusText(QSettings::Status status)
{
	switch (status) {
	case QSettings::NoError:
		return QStringLiteral("ready");
	case QSettings::AccessError:
		return QStringLiteral("access-error");
	case QSettings::FormatError:
		return QStringLiteral("format-error");
	}
	return QStringLiteral("unknown");
}

// Options accept both `--option value` and `--option=value`. `commandTokens()`
// removes both spellings from the positional stream, so every accessor has to
// understand both or the inline form would be silently dropped.
bool optionMatchesInline(const QString& token, const QString& option, QString* value)
{
	if (!token.startsWith(option + QLatin1Char('='))) {
		return false;
	}
	if (value) {
		*value = token.mid(option.size() + 1);
	}
	return true;
}

QString optionValue(const QStringList& args, const QString& option)
{
	for (int index = 0; index < args.size(); ++index) {
		QString inlineValue;
		if (optionMatchesInline(args.at(index), option, &inlineValue)) {
			return inlineValue;
		}
		if (args.at(index) == option) {
			return index + 1 < args.size() ? args.at(index + 1) : QString();
		}
	}
	return {};
}

QStringList optionValues(const QStringList& args, const QString& option)
{
	QStringList values;
	for (int index = 0; index < args.size(); ++index) {
		QString inlineValue;
		if (optionMatchesInline(args.at(index), option, &inlineValue)) {
			values.push_back(inlineValue);
			continue;
		}
		if (args.at(index) == option && index + 1 < args.size()) {
			values.push_back(args.at(index + 1));
			++index;
		}
	}
	return values;
}

QString normalizedOptionId(const QString& id)
{
	return id.trimmed().toLower().replace('_', '-');
}

bool hasOption(const QStringList& args, const QString& option)
{
	if (args.contains(option)) {
		return true;
	}
	const QString inlinePrefix = option + QLatin1Char('=');
	for (const QString& token : args) {
		if (token.startsWith(inlinePrefix)) {
			return true;
		}
	}
	return false;
}

QStringList commandTokens(const QStringList& args)
{
	// Options are recognized from a table rather than an inline chain so that an
	// option added to a handler cannot silently leak itself, and its value, into
	// the positional stream. That leak is what made
	// `project init --project-ai-free on ./mymod` parse "--project-ai-free" as
	// the project path.
	static const QSet<QString> booleanFlags = {
			QStringLiteral("--invert"),
			QStringLiteral("--cli"),
			QStringLiteral("--json"),
			QStringLiteral("--quiet"),
			QStringLiteral("--verbose"),
			QStringLiteral("--dry-run"),
			QStringLiteral("--write"),
			QStringLiteral("--overwrite"),
			QStringLiteral("--allow-generated-palette"),
			QStringLiteral("--match-tangents"),
			QStringLiteral("--replace-texture"),
			QStringLiteral("--case-sensitive"),
			QStringLiteral("--whole-word"),
			QStringLiteral("--delete-matches"),
			QStringLiteral("--watch"),
			QStringLiteral("--task-state"),
			QStringLiteral("--execute"),
			QStringLiteral("--from-session"),
			QStringLiteral("--allow-execution"),
			QStringLiteral("--no-stage"),
			QStringLiteral("--extract-all"),
			QStringLiteral("--register-output"),
			QStringLiteral("--register-outputs"),
			QStringLiteral("--stream"),
			QStringLiteral("--no-color"),
			QStringLiteral("--no-grid"),
			QStringLiteral("--no-decode"),
			QStringLiteral("--no-recursive"),
			QStringLiteral("--metadata-only"),
			QStringLiteral("--staged"),
			QStringLiteral("--finish"),
			QStringLiteral("--strict"),
			QStringLiteral("--include-directories"),
			QStringLiteral("--in-place"),
			QStringLiteral("--deploy"),
			QStringLiteral("--allow-test-maps"),
			QStringLiteral("--allow-valve220"),
			QStringLiteral("--labels"),
			QStringLiteral("--links"),
			QStringLiteral("--high-contrast"),
			QStringLiteral("--self-test"),
			QStringLiteral("--yes"),
			QStringLiteral("--companions"),
			QStringLiteral("--fullbrights"),
			QStringLiteral("--no-seamless"),
			QStringLiteral("--transparent"),
			QStringLiteral("--tileable"),
	};

	static const QSet<QString> valueFlags = {
			// Generative commands (cli/ai_generation.cpp).
			QStringLiteral("--mode"), QStringLiteral("--theme"), QStringLiteral("--rooms"), QStringLiteral("--seed"), QStringLiteral("--verticality"),
			QStringLiteral("--players"), QStringLiteral("--monsters"), QStringLiteral("--liquid"), QStringLiteral("--title"), QStringLiteral("--wad"),
			QStringLiteral("--textures-from"), QStringLiteral("--planner"), QStringLiteral("--plan"), QStringLiteral("--revisions"), QStringLiteral("--save-plan"),
			QStringLiteral("--preview"), QStringLiteral("--report"), QStringLiteral("--style"), QStringLiteral("--count"), QStringLiteral("--quality"),
			QStringLiteral("--from-image"), QStringLiteral("--source"), QStringLiteral("--folder"), QStringLiteral("--seam-blend"),
			QStringLiteral("--normal-strength"), QStringLiteral("--glow-threshold"), QStringLiteral("--negative"), QStringLiteral("--steps"), QStringLiteral("--strength"),
			QStringLiteral("--proposal"), QStringLiteral("--save-proposal"), QStringLiteral("--only"),
			QStringLiteral("--loop"), QStringLiteral("--influence"), QStringLiteral("--variants"),
			QStringLiteral("--project"), QStringLiteral("--package"), QStringLiteral("--map"), QStringLiteral("--code"),
			QStringLiteral("--current-code"), QStringLiteral("--active-module"), QStringLiteral("--module"),
			QStringLiteral("--prefab"),
			QStringLiteral("--description"),
			QStringLiteral("--anchor"),
			QStringLiteral("--position"),
			QStringLiteral("--rotation"),
			QStringLiteral("--target-prefix"),
			QStringLiteral("--boundary"),
			QStringLiteral("--center"),
			QStringLiteral("--units-per-tile"),
			QStringLiteral("--first"),
			QStringLiteral("--second"),
			QStringLiteral("--max-gap"),
			QStringLiteral("--direction"),
			QStringLiteral("--target"),
			QStringLiteral("--surface"),
			QStringLiteral("--faces"),
			QStringLiteral("--vertices"),
			QStringLiteral("--edges"),
			QStringLiteral("--weld-distance"),
			QStringLiteral("--bridge-twist"),
			QStringLiteral("--snap-grid"),
			QStringLiteral("--snap-angle"),
			QStringLiteral("--snap-scale"),
			QStringLiteral("--pivot-mode"),
			QStringLiteral("--pivot-frame"),
			QStringLiteral("--transform-space"), QStringLiteral("--axis-rotation"), QStringLiteral("--axes-frame"),
			QStringLiteral("--tag"),
			QStringLiteral("--tag-origin"),
			QStringLiteral("--offset"),
			QStringLiteral("--rotate"),
			QStringLiteral("--scale"),
			QStringLiteral("--pivot"),
			QStringLiteral("--degrees"),
			QStringLiteral("--texture-lock"),
			QStringLiteral("--uv-scale"),
			QStringLiteral("--uv-offset"),
			QStringLiteral("--uv-rotation"),
			QStringLiteral("--uv-pivot"),
			QStringLiteral("--uv-pivot-mode"),
			QStringLiteral("--uv-grid"),
			QStringLiteral("--uv-atlas-size"),
			QStringLiteral("--uv-padding"),
			QStringLiteral("--uv-pack-scale"),
			QStringLiteral("--skin-size"),
			QStringLiteral("--expected-sha256"),
			QStringLiteral("--expected-plan-sha256"),
			QStringLiteral("--action-index"),
			QStringLiteral("--resolve-completion"),
			QStringLiteral("--rename"),
			QStringLiteral("--directory"),
			QStringLiteral("--discard"),
			QStringLiteral("--color"),
			QStringLiteral("--operations"),
			QStringLiteral("--operation"),
			QStringLiteral("--start-frame"),
			QStringLiteral("--sample-rate"),
			QStringLiteral("--wav-format"),
			QStringLiteral("--dither"),
			QStringLiteral("--dither-seed"),
			QStringLiteral("--markers"),
			QStringLiteral("--channels"),
			QStringLiteral("--channel-map"),
			QStringLiteral("--paste-input"),
			QStringLiteral("--end-frame"),
			QStringLiteral("--db"),
			QStringLiteral("--settings-file"),
			QStringLiteral("--installation"),
			QStringLiteral("--project-installation"),
			QStringLiteral("--project-root"),
			QStringLiteral("--editor-profile"),
			QStringLiteral("--set-editor-profile"),
			QStringLiteral("--project-editor-profile"),
			QStringLiteral("--project-palette"),
			QStringLiteral("--project-compiler-profile"),
			QStringLiteral("--project-compiler-search-paths"),
			QStringLiteral("--project-compiler-tool"),
			QStringLiteral("--project-compiler-executable"),
			QStringLiteral("--project-ai-free"),
			QStringLiteral("--ai-free"),
			QStringLiteral("--detect-install-root"),
			QStringLiteral("--set-ai-free"),
			QStringLiteral("--set-ai-cloud"),
			QStringLiteral("--set-ai-agentic"),
			QStringLiteral("--set-ai-reasoning"),
			QStringLiteral("--set-ai-text-model"),
			QStringLiteral("--set-ai-local"),
			QStringLiteral("--set-ai-model"),
			QStringLiteral("--set-ai-endpoint"),
			QStringLiteral("--set-ai-image"),
			QStringLiteral("--set-ai-image-model"),
			QStringLiteral("--set-ai-image-endpoint"),
			QStringLiteral("--set-ai-audio"),
			QStringLiteral("--set-ai-audio-model"),
			QStringLiteral("--set-ai-audio-endpoint"),
			QStringLiteral("--endpoint"),
			QStringLiteral("--context-file"),
			QStringLiteral("--max-tokens"),
			QStringLiteral("--provider"),
			QStringLiteral("--provider-a"),
			QStringLiteral("--provider-b"),
			QStringLiteral("--model"),
			QStringLiteral("--model-a"),
			QStringLiteral("--model-b"),
			QStringLiteral("--prompt"),
			QStringLiteral("--text"),
			QStringLiteral("--log"),
			QStringLiteral("--command"),
			QStringLiteral("--kind"),
			QStringLiteral("--name"),
			QStringLiteral("--sprite-name"),
			QStringLiteral("--manifest"),
			QStringLiteral("--write-manifest"),
			QStringLiteral("--workspace-root"),
			QStringLiteral("--working-directory"),
			QStringLiteral("--game"), QStringLiteral("--preset"), QStringLiteral("--floor-texture"), QStringLiteral("--ceiling-texture"), QStringLiteral("--input"),
			QStringLiteral("--output"),
			QStringLiteral("--format"),
			QStringLiteral("--crop"),
			QStringLiteral("--resize"),
			QStringLiteral("--palette"),
			QStringLiteral("--palette-file"),
			QStringLiteral("--palette-root"),
			QStringLiteral("--export-options"),
			QStringLiteral("--target-package"), QStringLiteral("--target-entry"),
			QStringLiteral("--find"),
			QStringLiteral("--symbol"),
			QStringLiteral("--replace"),
			QStringLiteral("--extensions"),
			QStringLiteral("--include"),
			QStringLiteral("--exclude"),
			QStringLiteral("--add-file"),
			QStringLiteral("--import-file"),
			QStringLiteral("--as"),
			QStringLiteral("--replace-file"),
			QStringLiteral("--replace-entry"),
			QStringLiteral("--replace-ordinal"),
			QStringLiteral("--rename-ordinal"),
			QStringLiteral("--delete-ordinal"),
			QStringLiteral("--entry-index"),
			QStringLiteral("--mkdir"), QStringLiteral("--rename-folder"), QStringLiteral("--folder-to"), QStringLiteral("--delete-folder"),
			QStringLiteral("--rename-group"), QStringLiteral("--group-to"), QStringLiteral("--delete-group"), QStringLiteral("--groups-fingerprint"),
			QStringLiteral("--wad-magic"),
			QStringLiteral("--entry"),
			QStringLiteral("--prefix"),
			QStringLiteral("--map-input"),
			QStringLiteral("--entries"),
			QStringLiteral("--extract-entry"),
			QStringLiteral("--asset-entry"),
			QStringLiteral("--rename"),
			QStringLiteral("--to"),
			QStringLiteral("--delete"),
			QStringLiteral("--remove-entry"),
			QStringLiteral("--resolve"),
			QStringLiteral("--map"),
			QStringLiteral("--map-name"),
			QStringLiteral("--engine"),
			QStringLiteral("--engine-hint"),
			QStringLiteral("--shader"),
			QStringLiteral("--stage"),
			QStringLiteral("--directive"),
			QStringLiteral("--value"),
			QStringLiteral("--frames"),
			QStringLiteral("--rotations"),
			QStringLiteral("--package"),
			QStringLiteral("--mounted-package"),
			QStringLiteral("--package-root"),
			QStringLiteral("--source-frame"),
			QStringLiteral("--target-surface"),
			QStringLiteral("--material-slot"),
			QStringLiteral("--slot"),
			QStringLiteral("--surfaces"),
			QStringLiteral("--clip"),
			QStringLiteral("--first-frame"),
			QStringLiteral("--last-frame"),
			QStringLiteral("--insert-count"),
			QStringLiteral("--clip-fps"),
			QStringLiteral("--sample-fps"),
			QStringLiteral("--clip-name"),
			QStringLiteral("--member"),
			QStringLiteral("--sync"),
			QStringLiteral("--eye"),
			QStringLiteral("--mdl-size"),
			QStringLiteral("--native-frame"),
			QStringLiteral("--time"),
			QStringLiteral("--timing"),
			QStringLiteral("--sync-phase"),
			QStringLiteral("--skin"),
			QStringLiteral("--image"),
			QStringLiteral("--flags"),
			QStringLiteral("--duration"),
			QStringLiteral("--extension-root"),
			QStringLiteral("--root"),
			QStringLiteral("--select"),
			QStringLiteral("--entity"),
			QStringLiteral("--set"),
			QStringLiteral("--property"),
			QStringLiteral("--object"),
			QStringLiteral("--delta"),
			QStringLiteral("--origin"),
			QStringLiteral("--type"),
			QStringLiteral("--angle"),
			QStringLiteral("--from"),
			QStringLiteral("--axis"),
			QStringLiteral("--mins"),
			QStringLiteral("--patch"),
			QStringLiteral("--point"),
			QStringLiteral("--shape"),
			QStringLiteral("--sides"),
			QStringLiteral("--bands"),
			QStringLiteral("--plane"),
			QStringLiteral("--columns"),
			QStringLiteral("--rows"),
			QStringLiteral("--subdivide"),
			QStringLiteral("--uv"),
			QStringLiteral("--maxs"),
			QStringLiteral("--texture"),
			QStringLiteral("--turns"),
			QStringLiteral("--size"),
			QStringLiteral("--at"),
			QStringLiteral("--points"),
			QStringLiteral("--keep"),
			QStringLiteral("--thickness"),
			QStringLiteral("--field"),
			QStringLiteral("--by"),
			QStringLiteral("--door-texture"),
			QStringLiteral("--uses"),
			QStringLiteral("--where"),
			QStringLiteral("--track-texture"),
			QStringLiteral("--ceiling-flat"),
			QStringLiteral("--profile"),
			QStringLiteral("--compiler-profile"),
			QStringLiteral("--extra-args"),
			QStringLiteral("--compiler-search-paths"),
			QStringLiteral("--timeout-ms"),
			QStringLiteral("--server"),
			QStringLiteral("--server-args"),
			QStringLiteral("--language"),
			QStringLiteral("--line"),
			QStringLiteral("--column"),
			QStringLiteral("--executable"),
			QStringLiteral("--locale"),
			QStringLiteral("--catalog-root"),
			QStringLiteral("--install-game"),
			QStringLiteral("--install-engine"),
			QStringLiteral("--install-name"),
			QStringLiteral("--install-executable"),
			QStringLiteral("--install-base-packages"),
			QStringLiteral("--install-mod-packages"),
			QStringLiteral("--install-palette"),
			QStringLiteral("--install-compiler-profile"),
			QStringLiteral("--install-read-only"),
			QStringLiteral("--install-hidden"),
			QStringLiteral("--pipeline"),
			QStringLiteral("--stage-args"),
			QStringLiteral("--max-bytes"),
			QStringLiteral("--expected-output-sha256"),
			QStringLiteral("--expected-package-sha256"),
			QStringLiteral("--expected-deployment-sha256"),
			QStringLiteral("--pak-slot"),
			QStringLiteral("--tool"),
			QStringLiteral("--disable-stage"),
			QStringLiteral("--launch-profile"),
			QStringLiteral("--mod"),
			QStringLiteral("--basedir"),
			QStringLiteral("--bsp"),
			QStringLiteral("--projection"),
			QStringLiteral("--grid"),
			QStringLiteral("--width"),
			QStringLiteral("--height"),
			QStringLiteral("--margin"),
			QStringLiteral("--mip"),
			QStringLiteral("--frame"),
			QStringLiteral("--compression"),
			QStringLiteral("--buckets"),
			QStringLiteral("--highlight"),
			QStringLiteral("--leak"),
			QStringLiteral("--search-paths"),
			QStringLiteral("--definitions"),
			QStringLiteral("--definition"),
			QStringLiteral("--definition-paths"),
			QStringLiteral("--path"),
			QStringLiteral("--paths"),
			QStringLiteral("--class"),
			QStringLiteral("--file"),
			QStringLiteral("--material"),
			QStringLiteral("--against"),
			QStringLiteral("--max-entry-bytes"),
			QStringLiteral("--max-mib"), QStringLiteral("--max-files"), QStringLiteral("--max-entries"), QStringLiteral("--max-batches"), QStringLiteral("--expected-policy-sha256"),
			QStringLiteral("--backup"),
	};

	QStringList tokens;
	for (int i = 1; i < args.size(); ++i) {
		const QString token = args.at(i);
		if (booleanFlags.contains(token)) {
			continue;
		}
		if (valueFlags.contains(token)) {
			++i;
			continue;
		}
		// "--option=value" carries its value inline, so nothing is consumed.
		const qsizetype equals = token.indexOf(QLatin1Char('='));
		if (token.startsWith(QStringLiteral("--")) && equals > 2
			&& (booleanFlags.contains(token.left(equals)) || valueFlags.contains(token.left(equals)))) {
			continue;
		}
		tokens.push_back(token);
	}
	return tokens;
}

bool boolOptionValue(const QString& value, bool* parsed)
{
	const QString normalized = normalizedOptionId(value);
	if (normalized == QStringLiteral("1") || normalized == QStringLiteral("true") || normalized == QStringLiteral("yes") || normalized == QStringLiteral("on") || normalized == QStringLiteral("enabled")) {
		*parsed = true;
		return true;
	}
	if (normalized == QStringLiteral("0") || normalized == QStringLiteral("false") || normalized == QStringLiteral("no") || normalized == QStringLiteral("off") || normalized == QStringLiteral("disabled")) {
		*parsed = false;
		return true;
	}
	return false;
}

bool localeOptionIsSupported(const QString& value)
{
	const QString requested = value.trimmed().replace('_', '-');
	if (requested.isEmpty()) {
		return false;
	}
	const QString normalized = normalizedLocaleName(requested);
	return normalized != QStringLiteral("en") || requested.startsWith(QStringLiteral("en"), Qt::CaseInsensitive);
}

void printHelp()
{
	std::cout << "VibeStudio " << text(versionString()) << "\n";
	std::cout << "Usage: vibestudio --cli [options]\n\n";
	std::cout << "       vibestudio --cli [--json] <family> <command> [arguments]\n\n";
	std::cout << "Options:\n";
	std::cout << "  --version           Print the application version.\n";
	std::cout << "  --help              Print this help text.\n";
	std::cout << "  --about             Print version, repository, credits, and license pointers.\n";
	std::cout << "  --credits           Print credits and license pointers.\n";
	std::cout << "  --json              Emit machine-readable JSON for supported commands.\n";
	std::cout << "  --quiet             Suppress successful human-readable narration. Errors still go to stderr.\n";
	std::cout << "  --verbose           Print timing and extra diagnostics for supported text commands.\n";
	std::cout << "  --exit-codes        Print stable CLI exit-code identifiers.\n";
	std::cout << "  --settings-file <path>\n";
	std::cout << "                      Use an INI settings file instead of the user's own preference store.\n";
	std::cout << "                      Resolved before any settings access, so scripts and CI never touch real preferences.\n";
	std::cout << "  --self-test         GUI mode only: build every work surface, repaint it, and exit. Used by CI.\n";
	std::cout << "  --studio-report     Print planned studio modules.\n";
	std::cout << "  --compiler-report   Print imported compiler integrations.\n";
	std::cout << "  --compiler-registry Print compiler tool registry and executable discovery.\n";
	std::cout << "  --platform-report   Print platform and Qt runtime details.\n";
	std::cout << "  --project-init <path>\n";
	std::cout << "                      Create or refresh .vibestudio/project.json for a project folder.\n";
	std::cout << "  --project-info <path>\n";
	std::cout << "                      Print project manifest and health summary.\n";
	std::cout << "  --project-validate <path>\n";
	std::cout << "                      Validate project manifest, folders, install linkage, and health checks.\n";
	std::cout << "  --project-installation <id>\n";
	std::cout << "                      Optional selected installation id for --project-init.\n";
	std::cout << "  --project-editor-profile <id>\n";
	std::cout << "                      Optional project-local editor profile override for --project-init.\n";
	std::cout << "  --project-palette <id>\n";
	std::cout << "                      Optional project-local palette override for --project-init.\n";
	std::cout << "  --project-compiler-profile <id>\n";
	std::cout << "                      Optional project-local compiler profile override for --project-init.\n";
	std::cout << "  --project-compiler-search-paths <paths>\n";
	std::cout << "                      Semicolon-separated project-local compiler executable search paths.\n";
	std::cout << "  --project-compiler-tool <id> --project-compiler-executable <path>\n";
	std::cout << "                      Optional project-local compiler executable override for --project-init.\n";
	std::cout << "  --project-ai-free <on|off>\n";
	std::cout << "                      Optional project-local AI-free mode override for --project-init.\n";
	std::cout << "  --operation-states  Print reusable operation state identifiers.\n";
	std::cout << "  --ui-primitives     Print reusable UI primitive identifiers.\n";
	std::cout << "  --ui-semantics      Print status chip, shortcut, and command palette metadata.\n";
	std::cout << "  --package-formats   Print package/archive interface descriptors.\n";
	std::cout << "  --check-package-path <path>\n";
	std::cout << "                      Normalize and validate a package virtual path.\n";
	std::cout << "  --info <path>       Print read-only package summary for a folder, PAK, WAD, ZIP, or PK3.\n";
	std::cout << "  --list <path>       List entries in a folder, PAK, WAD, ZIP, or PK3 package.\n";
	std::cout << "  --preview-package <path>\n";
	std::cout << "                      Open a package and preview --preview-entry text, image, or binary metadata.\n";
	std::cout << "  --preview-entry <virtual-path>\n";
	std::cout << "                      Virtual package entry path for --preview-package.\n";
	std::cout << "  --extract <path>    Extract a package to --output. Defaults to all entries unless --extract-entry is passed.\n";
	std::cout << "  --extract-entry <virtual-path>\n";
	std::cout << "                      Virtual package entry to extract. Repeat for multiple entries.\n";
	std::cout << "  --entry-index <n>   Exact zero-based list --json row for package preview, extraction, subset or MDL skin import.\n";
	std::cout << "                      Repeat for extraction; paired --as paths separate repeated names.\n";
	std::cout << "  --replace-ordinal <n> --replace-file <path>\n";
	std::cout << "  --rename-ordinal <n> --to <virtual-path>\n";
	std::cout << "  --delete-ordinal <n>\n";
	std::cout << "                      Edit one zero-based sourceOrdinal from list --json; repeat for more.\n";
	std::cout << "  --mkdir <folder>    Stage an empty folder (ZIP/PK3 preserve empty folders).\n";
	std::cout << "  --rename-folder <folder> --folder-to <unused-path>\n";
	std::cout << "  --delete-folder <folder>\n";
	std::cout << "  --rename-group <id> --group-to <map-label> | --delete-group <id>\n";
	std::cout << "  --groups-fingerprint <fingerprint-from-package-groups>\n";
	std::cout << "                      Edit a complete subtree in one undo step; WAD remains flat.\n";
	std::cout << "  --wad-magic <id>    WAD variant for package create: PWAD, IWAD, WAD2, WAD3.\n";
	std::cout << "  --extract-all       Extract every readable package entry.\n";
	std::cout << "  --validate-package <path>\n";
	std::cout << "                      Validate a package can be opened without loader warnings.\n";
	std::cout << "  --output <path>     Output folder for package extraction or output path for compiler planning.\n";
	std::cout << "  build prepare <map> --package <assets-or-draft> --output <new-directory> [--target quake|quake2|quake3]\n";
	std::cout << "                      Capture Quake III inputs; --name sets the map name, --max-bytes caps the snapshot (default 4 GiB).\n";
	std::cout << "  build run-prepared <directory> [--pipeline quake3-full|quake3-bsp-only]\n";
	std::cout << "                      Verify and build; --tool q3map2=<path>, --timeout-ms, --stage-args stage=arguments, --disable-stage.\n";
	std::cout << "                      Both commands support --dry-run; existing prepared directories are never overwritten.\n";
	std::cout << "  build artifacts <directory>\n";
	std::cout << "                      Verify the last successful build's inputs, BSP, generated shaders and external lightmaps.\n";
	std::cout << "  build publish-prepared <directory> --output <file.pak|file.pk3>\n";
	std::cout << "                      Publish verified runtime files; --include-source, --compression, --overwrite, --dry-run.\n";
	std::cout << "                      --expected-output-sha256 <receipt-hash> requires the same reviewed build.\n";
	std::cout << "  build deploy-plan <directory> [--installation <id>] [--mod <folder>]\n";
	std::cout << "                      Review complete PAK/PK3 deployment and the windowed launch command; writes nothing.\n";
	std::cout << "                      --pak-slot <n> chooses a Quake/Quake II numbered PAK; otherwise reuse this map's slot or select a free slot.\n";
	std::cout << "  build deploy-prepared <directory> [--installation <id>] [--mod <folder>]\n";
	std::cout << "                      --allow-test-assets permits this write to a read-only installation; --launch starts it afterward.\n";
	std::cout << "                      Supports --dry-run, --overwrite, --include-source, --compression and --expected-output-sha256.\n";
	std::cout << "                      --expected-package-sha256 <hash|missing> binds the reviewed destination.\n";
	std::cout << "                      --expected-deployment-sha256 <hash> binds the complete deployment review; --pak-slot also applies.\n";
	std::cout << "  --dry-run           Report package extraction/save-as writes or compiler command plans without touching the file system.\n";
	std::cout << "  --watch             Stream compiler task log entries while a long-running command is active.\n";
	std::cout << "  --task-state        Include machine-readable task state objects in JSON output where supported.\n";
	std::cout << "  --overwrite         Allow package extraction or save-as to replace existing output files.\n";
	std::cout << "  --in-place          Replace an existing package only after the new archive is written and verified.\n";
	std::cout << "  --compression <id>  DEFLATE level for ZIP and PK3 output: store, fast, default, or best.\n";
	std::cout << "  --prefix <folder>   Repeatable subset folder selector (with --entry-index, --entry and --where).\n";
	std::cout << "  --map-input <map>   Select resolved level assets for package subset; excludes the map/BSP.\n";
	std::cout << "  --backup <path>     Where --in-place moves the replaced file. Default <destination>.bak.\n";
	std::cout << "  --settings-report   Print settings storage and recent projects.\n";
	std::cout << "  --setup-report      Print first-run setup status and summary.\n";
	std::cout << "  --setup-start       Start or resume first-run setup.\n";
	std::cout << "  --setup-step <id>   Resume setup at a specific step.\n";
	std::cout << "  --setup-next        Advance setup to the next step.\n";
	std::cout << "  --setup-skip        Skip setup for now without completing it.\n";
	std::cout << "  --setup-complete    Mark setup complete.\n";
	std::cout << "  --setup-reset       Reset setup progress.\n";
	std::cout << "  --preferences-report\n";
	std::cout << "                      Print accessibility and language preferences.\n";
	std::cout << "  --localization-report\n";
	std::cout << "                      Print localization target, pseudo-localization, RTL, formatting, and catalog status.\n";
	std::cout << "  --set-locale <code> Set the preferred UI locale. Supported: " << text(supportedLocaleNames().join(", ")) << "\n";
	std::cout << "  --set-theme <id>    Set theme: " << text(themeIds().join(", ")) << "\n";
	std::cout << "  --set-text-scale <percent>\n";
	std::cout << "                      Set text scale from 100 to 200.\n";
	std::cout << "  --set-density <id>  Set density: " << text(densityIds().join(", ")) << "\n";
	std::cout << "  --editor-profiles   Print routed editor interaction profiles.\n";
	std::cout << "  --set-editor-profile <id>\n";
	std::cout << "                      Select editor profile: " << text(editorProfileIds().join(", ")) << "\n";
	std::cout << "  --ai-status         Print AI-free mode, opt-in settings, and connector stubs.\n";
	std::cout << "  --set-ai-text-model <id>\n";
	std::cout << "                      Preferred text model: " << text(aiModelIds().join(", ")) << "\n";
	std::cout << "  --set-ai-free <on|off>\n";
	std::cout << "                      Enable or disable AI-free mode. Default is on.\n";
	std::cout << "  --set-ai-cloud <on|off>\n";
	std::cout << "                      Opt in or out of experimental cloud AI connector settings.\n";
	std::cout << "  --set-ai-agentic <on|off>\n";
	std::cout << "                      Opt in or out of future supervised agentic workflows.\n";
	std::cout << "  --set-ai-reasoning <id>\n";
	std::cout << "                      Preferred reasoning connector: " << text(aiConnectorIds().join(", ")) << "\n";
	std::cout << "  --set-ai-local <id> Preferred local connector, used when cloud connectors are off.\n";
	std::cout << "  --set-ai-model <connector>=<model>\n";
	std::cout << "                      The model a text connector asks, as its provider names it; empty clears it.\n";
	std::cout << "  --set-ai-endpoint <connector>=<url>\n";
	std::cout << "                      A text connector's base URL, such as http://localhost:11434/v1; empty restores the default.\n";
	std::cout << "  --set-ai-image <id> Image connector for the Texture Generator and ai image.\n";
	std::cout << "  --set-ai-image-model <connector>=<model>\n";
	std::cout << "  --set-ai-image-endpoint <connector>=<url>\n";
	std::cout << "                      An image connector's model and base URL, such as http://127.0.0.1:7860.\n";
	std::cout << "  --set-ai-audio <id> Sound connector for the Sound Generator: elevenlabs or custom-http.\n";
	std::cout << "  --set-ai-audio-model <connector>=<model>\n";
	std::cout << "  --set-ai-audio-endpoint <connector>=<url>\n";
	std::cout << "                      A sound connector's model and base URL; empty restores the default.\n";
	std::cout << "  --set-reduced-motion <on|off>\n";
	std::cout << "                      Store the reduced-motion preference.\n";
	std::cout << "  --set-tts <on|off>  Store the OS-backed text-to-speech preference.\n";
	std::cout << "  --installations-report\n";
	std::cout << "                      Print saved manual game installation profiles.\n";
	std::cout << "  --detect-installations\n";
	std::cout << "                      Detect Steam and GOG installation candidates without saving them.\n";
	std::cout << "  --detect-install-root <path>\n";
	std::cout << "                      Extra Steam/GOG library or game root for --detect-installations.\n";
	std::cout << "  --add-installation <root>\n";
	std::cout << "                      Add or update a manual game installation profile.\n";
	std::cout << "  --install-game <key>\n";
	std::cout << "                      Game key for --add-installation. Known: " << text(knownGameKeys().join(", ")) << "\n";
	std::cout << "  --install-engine <id>\n";
	std::cout << "                      Engine family for --add-installation. Known: " << text(gameEngineFamilyIds().join(", ")) << "\n";
	std::cout << "  --install-name <name>\n";
	std::cout << "                      Display name for --add-installation.\n";
	std::cout << "  --install-executable <path>\n";
	std::cout << "                      Optional executable path for --add-installation.\n";
	std::cout << "  --install-base-packages <paths>\n";
	std::cout << "                      Semicolon-separated base package paths for --add-installation.\n";
	std::cout << "  --install-mod-packages <paths>\n";
	std::cout << "                      Semicolon-separated mod/package paths for --add-installation.\n";
	std::cout << "  --install-read-only <on|off>\n";
	std::cout << "                      Store whether the installation should be treated read-only.\n";
	std::cout << "  --install-hidden <on|off>\n";
	std::cout << "                      Store whether the installation profile is hidden from default views.\n";
	std::cout << "  --select-installation <id>\n";
	std::cout << "                      Mark an installation profile as selected.\n";
	std::cout << "  --validate-installation <id>\n";
	std::cout << "                      Validate a saved installation profile read-only.\n";
	std::cout << "  --remove-installation <id>\n";
	std::cout << "                      Remove a saved installation profile without touching files.\n";
	std::cout << "  --recent-projects   Print recent projects only.\n";
	std::cout << "  --add-recent-project <path>\n";
	std::cout << "                      Remember a project folder in persistent settings.\n";
	std::cout << "  --remove-recent-project <path>\n";
	std::cout << "                      Forget a project folder without touching files.\n";
	std::cout << "  --clear-recent-projects\n";
	std::cout << "                      Clear remembered project folders without touching files.\n";
	// The subcommand list is generated from the same registry that backs
	// `cli commands` and scripts/validate_cli_docs.py, so the help text cannot
	// drift away from what is actually routed. Adding a command to the registry
	// is enough to document it here.
	std::cout << "\nSubcommands:\n";
	int longestName = 0;
	for (const CliCommandDescriptor& descriptor : cliCommandDescriptors()) {
		longestName = std::max<int>(longestName, static_cast<int>(descriptor.family.size() + descriptor.command.size() + 1));
	}
	QString currentFamily;
	for (const CliCommandDescriptor& descriptor : cliCommandDescriptors()) {
		if (descriptor.family != currentFamily) {
			currentFamily = descriptor.family;
			std::cout << "\n  " << text(currentFamily) << "\n";
		}
		const QString name = QStringLiteral("%1 %2").arg(descriptor.family, descriptor.command);
		QStringList modes;
		if (descriptor.json) {
			modes.push_back(QStringLiteral("--json"));
		}
		if (descriptor.dryRun) {
			modes.push_back(QStringLiteral("--dry-run"));
		}
		if (descriptor.watch) {
			modes.push_back(QStringLiteral("--watch"));
		}
		std::cout << "    " << text(name.leftJustified(longestName + 2, QLatin1Char(' '))) << text(descriptor.summary);
		if (!modes.isEmpty()) {
			std::cout << " [" << text(modes.join(QLatin1Char(' '))) << "]";
		}
		std::cout << "\n";
		if (!descriptor.examples.isEmpty()) {
			std::cout << "    " << text(QString(longestName + 2, QLatin1Char(' '))) << "e.g. "
				<< text(descriptor.examples.first()) << "\n";
		}
	}
	std::cout << "\nExamples:\n";
	std::cout << "  PowerShell: vibestudio --cli package validate \"C:\\Games\\Quake\\id1\\pak0.pak\" --json\n";
	std::cout << "  POSIX:      vibestudio --cli compiler plan ericw-qbsp --input './maps/start.map' --dry-run\n";
}

void printStudioReport()
{
	std::cout << "VibeStudio studio modules\n";
	for (const StudioModule& module : plannedModules()) {
		std::cout << "- " << text(module.name) << " [" << text(module.maturity) << "]\n";
		std::cout << "  Category: " << text(module.category) << "\n";
		std::cout << "  Engines: " << text(module.engines.join(", ")) << "\n";
		std::cout << "  " << text(module.description) << "\n";
	}
}

void printCompilerReport()
{
	std::cout << "VibeStudio compiler integrations\n";
	for (const CompilerIntegration& compiler : compilerIntegrations()) {
		std::cout << "- " << text(compiler.displayName) << "\n";
		std::cout << "  Engines: " << text(compiler.engines) << "\n";
		std::cout << "  Role: " << text(compiler.role) << "\n";
		std::cout << "  Source: " << text(compiler.sourcePath) << "\n";
		std::cout << "  Upstream: " << text(compiler.upstreamUrl) << "\n";
		std::cout << "  Revision: " << text(compiler.pinnedRevision) << "\n";
		std::cout << "  License: " << text(compiler.license) << "\n";
	}
}

void printPlatformReport()
{
	std::cout << "VibeStudio platform report\n";
	std::cout << "Version: " << text(versionString()) << "\n";
	std::cout << "GitHub repo: " << VIBESTUDIO_GITHUB_REPO << "\n";
	std::cout << "Update channel: " << VIBESTUDIO_UPDATE_CHANNEL << "\n";
	std::cout << "Qt runtime: " << qVersion() << "\n";
	std::cout << "Kernel: " << text(QSysInfo::kernelType()) << " " << text(QSysInfo::kernelVersion()) << "\n";
	std::cout << "CPU architecture: " << text(QSysInfo::currentCpuArchitecture()) << "\n";
	std::cout << "Product: " << text(QSysInfo::prettyProductName()) << "\n";
}

void printProjectHealth(const ProjectHealthSummary& health)
{
	std::cout << "Project health\n";
	std::cout << "State: " << text(operationStateId(health.overallState())) << "\n";
	std::cout << "Ready: " << health.readyCount << "\n";
	std::cout << "Warnings: " << health.warningCount << "\n";
	std::cout << "Failures: " << health.failedCount << "\n";
	for (const ProjectHealthCheck& check : health.checks) {
		std::cout << "- " << text(check.title) << "\n";
		std::cout << "  State: " << text(operationStateId(check.state)) << "\n";
		std::cout << "  Detail: " << text(check.detail) << "\n";
	}
}

void printProjectInfo(const ProjectManifest& manifest)
{
	std::cout << "Project manifest\n";
	std::cout << text(projectManifestToText(manifest)) << "\n";
	printProjectHealth(buildProjectHealthSummary(manifest));
}

void printOperationStates()
{
	std::cout << "Operation states\n";
	for (const QString& id : operationStateIds()) {
		const OperationState state = operationStateFromId(id);
		std::cout << "- " << text(id) << "\n";
		std::cout << "  Label: " << text(operationStateDisplayName(state)) << "\n";
		std::cout << "  Terminal: " << (operationStateIsTerminal(state) ? "yes" : "no") << "\n";
		std::cout << "  Cancellable: " << (operationStateAllowsCancellation(state) ? "yes" : "no") << "\n";
	}
}

void printUiPrimitives()
{
	std::cout << "UI primitives\n";
	for (const UiPrimitiveDescriptor& primitive : uiPrimitiveDescriptors()) {
		std::cout << "- " << text(primitive.id) << "\n";
		std::cout << "  Title: " << text(primitive.title) << "\n";
		std::cout << "  Description: " << text(primitive.description) << "\n";
		std::cout << "  Use cases: " << text(primitive.useCases.join(", ")) << "\n";
	}
}

void printUiSemantics()
{
	std::cout << text(statusChipSummaryText()) << "\n\n";
	std::cout << text(shortcutRegistrySummaryText()) << "\n\n";
	std::cout << text(commandPaletteSummaryText()) << "\n";
}

void printPackageFormats()
{
	std::cout << "Package archive interfaces\n";
	for (const PackageArchiveFormatDescriptor& descriptor : packageArchiveFormatDescriptors()) {
		std::cout << "- " << text(descriptor.id) << "\n";
		std::cout << "  Label: " << text(descriptor.displayName) << "\n";
		std::cout << "  Extensions: " << text(descriptor.extensions.isEmpty() ? QStringLiteral("(folder)") : descriptor.extensions.join(", ")) << "\n";
		std::cout << "  Capabilities: " << text(descriptor.capabilities.join(", ")) << "\n";
		std::cout << "  " << text(descriptor.description) << "\n";
	}
}

void printPackagePathCheck(const QString& path)
{
	const PackageVirtualPath normalized = normalizePackageVirtualPath(path);
	std::cout << "Package path check\n";
	std::cout << "Original: " << text(path) << "\n";
	std::cout << "Normalized: " << text(normalized.normalizedPath.isEmpty() ? QStringLiteral("(empty)") : normalized.normalizedPath) << "\n";
	std::cout << "Safe: " << (normalized.isSafe() ? "yes" : "no") << "\n";
	std::cout << "Issue: " << text(packagePathIssueId(normalized.issue)) << " (" << text(packagePathIssueDisplayName(normalized.issue)) << ")\n";
	if (normalized.isSafe()) {
		std::cout << "File name: " << text(packageVirtualPathFileName(normalized.normalizedPath)) << "\n";
		std::cout << "Parent: " << text(packageVirtualPathParent(normalized.normalizedPath).isEmpty() ? QStringLiteral("/") : packageVirtualPathParent(normalized.normalizedPath)) << "\n";
		std::cout << "Nested archive candidate: " << (packageEntryLooksNestedArchive(normalized.normalizedPath) ? "yes" : "no") << "\n";
	}
}

QString sizeText(quint64 bytes)
{
	if (bytes > (quint64(1) << 53)) { return QStringLiteral("%1 B").arg(bytes); }
	if (bytes >= 1024ull * 1024ull * 1024ull) {
		return QStringLiteral("%1 GiB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
	}
	if (bytes >= 1024ull * 1024ull) {
		return QStringLiteral("%1 MiB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 2);
	}
	if (bytes >= 1024ull) {
		return QStringLiteral("%1 KiB").arg(static_cast<double>(bytes) / 1024.0, 0, 'f', 2);
	}
	return QStringLiteral("%1 B").arg(bytes);
}

void printJson(const QJsonObject& object)
{
	const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
	std::cout << std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
	if (!bytes.endsWith('\n')) {
		std::cout << "\n";
	}
}

QJsonArray stringArrayJson(const QStringList& values)
{
	QJsonArray array;
	for (const QString& value : values) {
		array.append(value);
	}
	return array;
}

QJsonObject exitCodeJson(CliExitCode code)
{
	const CliExitCodeDescriptor descriptor = cliExitCodeDescriptor(code);
	QJsonObject object;
	object.insert(QStringLiteral("code"), exitCodeValue(code));
	object.insert(QStringLiteral("id"), descriptor.id);
	object.insert(QStringLiteral("label"), descriptor.label);
	object.insert(QStringLiteral("description"), descriptor.description);
	return object;
}

QJsonArray stringArrayJson(const QStringList& values);

QJsonArray exitCodesJson()
{
	QJsonArray array;
	for (const CliExitCodeDescriptor& descriptor : cliExitCodeDescriptors()) {
		array.append(exitCodeJson(descriptor.code));
	}
	return array;
}

QJsonObject cliCommandDescriptorJson(const CliCommandDescriptor& descriptor)
{
	QJsonObject object;
	object.insert(QStringLiteral("family"), descriptor.family);
	object.insert(QStringLiteral("command"), descriptor.command);
	object.insert(QStringLiteral("name"), QStringLiteral("%1 %2").arg(descriptor.family, descriptor.command));
	object.insert(QStringLiteral("summary"), descriptor.summary);
	object.insert(QStringLiteral("examples"), stringArrayJson(descriptor.examples));
	object.insert(QStringLiteral("json"), descriptor.json);
	object.insert(QStringLiteral("quiet"), descriptor.quiet);
	object.insert(QStringLiteral("dryRun"), descriptor.dryRun);
	object.insert(QStringLiteral("watch"), descriptor.watch);
	return object;
}

QJsonArray cliCommandsJson()
{
	QJsonArray array;
	for (const CliCommandDescriptor& descriptor : cliCommandDescriptors()) {
		array.append(cliCommandDescriptorJson(descriptor));
	}
	return array;
}

QJsonObject cliResultJson(const QString& commandName, CliExitCode code = CliExitCode::Success)
{
	QJsonObject object;
	object.insert(QStringLiteral("command"), commandName);
	object.insert(QStringLiteral("ok"), code == CliExitCode::Success);
	object.insert(QStringLiteral("status"), code == CliExitCode::Success ? QStringLiteral("success") : QStringLiteral("error"));
	object.insert(QStringLiteral("exitCode"), exitCodeJson(code));
	return object;
}

int printCliError(const QString& commandName, CliExitCode code, const QString& message, CliOutputFormat format)
{
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("message"), message);
		printJson(object);
	} else {
		std::cerr << text(message) << "\n";
	}
	return exitCodeValue(code);
}

QJsonObject operationStateJson(OperationState state)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), operationStateId(state));
	object.insert(QStringLiteral("label"), operationStateDisplayName(state));
	object.insert(QStringLiteral("terminal"), operationStateIsTerminal(state));
	object.insert(QStringLiteral("cancellable"), operationStateAllowsCancellation(state));
	return object;
}

QJsonArray operationStatesJson()
{
	QJsonArray array;
	for (const QString& id : operationStateIds()) {
		array.append(operationStateJson(operationStateFromId(id)));
	}
	return array;
}

QJsonObject statusChipJson(const StatusChipDescriptor& descriptor)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), descriptor.id);
	object.insert(QStringLiteral("domain"), descriptor.domain);
	object.insert(QStringLiteral("label"), descriptor.label);
	object.insert(QStringLiteral("description"), descriptor.description);
	object.insert(QStringLiteral("state"), operationStateId(descriptor.state));
	object.insert(QStringLiteral("iconName"), descriptor.iconName);
	object.insert(QStringLiteral("colorToken"), descriptor.colorToken);
	object.insert(QStringLiteral("nonColorCue"), descriptor.nonColorCue);
	object.insert(QStringLiteral("nextAction"), descriptor.nextAction);
	return object;
}

QJsonArray statusChipsJson()
{
	QJsonArray array;
	for (const StatusChipDescriptor& descriptor : statusChipDescriptors()) {
		array.append(statusChipJson(descriptor));
	}
	return array;
}

QJsonObject shortcutJson(const ShortcutDescriptor& descriptor)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), descriptor.id);
	object.insert(QStringLiteral("commandId"), descriptor.commandId);
	object.insert(QStringLiteral("label"), descriptor.label);
	object.insert(QStringLiteral("context"), descriptor.context);
	object.insert(QStringLiteral("defaultSequence"), descriptor.defaultSequence);
	object.insert(QStringLiteral("alternateSequences"), stringArrayJson(descriptor.alternateSequences));
	object.insert(QStringLiteral("description"), descriptor.description);
	object.insert(QStringLiteral("userRemappable"), descriptor.userRemappable);
	return object;
}

QJsonArray shortcutsJson()
{
	QJsonArray array;
	for (const ShortcutDescriptor& descriptor : shortcutDescriptors()) {
		array.append(shortcutJson(descriptor));
	}
	return array;
}

QJsonObject commandPaletteEntryJson(const CommandPaletteEntry& entry)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), entry.id);
	object.insert(QStringLiteral("commandId"), entry.commandId);
	object.insert(QStringLiteral("label"), entry.label);
	object.insert(QStringLiteral("category"), entry.category);
	object.insert(QStringLiteral("summary"), entry.summary);
	object.insert(QStringLiteral("defaultShortcut"), entry.defaultShortcut);
	object.insert(QStringLiteral("requiresProject"), entry.requiresProject);
	object.insert(QStringLiteral("destructive"), entry.destructive);
	object.insert(QStringLiteral("stagedOrDryRun"), entry.stagedOrDryRun);
	return object;
}

QJsonArray commandPaletteEntriesJson()
{
	QJsonArray array;
	for (const CommandPaletteEntry& entry : commandPaletteEntries()) {
		array.append(commandPaletteEntryJson(entry));
	}
	return array;
}

QJsonObject uiSemanticsJson()
{
	QStringList conflicts;
	const bool hasShortcutConflicts = shortcutRegistryHasConflicts(&conflicts);
	QJsonObject object;
	object.insert(QStringLiteral("statusChips"), statusChipsJson());
	object.insert(QStringLiteral("statusDomains"), stringArrayJson(statusChipDomains()));
	object.insert(QStringLiteral("shortcuts"), shortcutsJson());
	object.insert(QStringLiteral("shortcutConflictCount"), conflicts.size());
	object.insert(QStringLiteral("shortcutConflicts"), stringArrayJson(conflicts));
	object.insert(QStringLiteral("shortcutRegistryOk"), !hasShortcutConflicts);
	object.insert(QStringLiteral("commandPalette"), commandPaletteEntriesJson());
	return object;
}

QJsonObject aboutDocumentJson(const AboutDocument& document)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), document.id);
	object.insert(QStringLiteral("title"), document.title);
	object.insert(QStringLiteral("path"), document.path);
	object.insert(QStringLiteral("description"), document.description);
	return object;
}

QJsonArray aboutDocumentsJson()
{
	QJsonArray documents;
	for (const AboutDocument& document : aboutDocuments()) {
		documents.append(aboutDocumentJson(document));
	}
	return documents;
}

QJsonObject aboutJson()
{
	QJsonObject object;
	object.insert(QStringLiteral("name"), QStringLiteral("VibeStudio"));
	object.insert(QStringLiteral("version"), versionString());
	object.insert(QStringLiteral("repository"), githubRepository());
	object.insert(QStringLiteral("updateChannel"), updateChannel());
	object.insert(QStringLiteral("licenseSummary"), projectLicenseSummary());
	object.insert(QStringLiteral("documents"), aboutDocumentsJson());
	return object;
}

QJsonObject packageWarningJson(const PackageLoadWarning& warning)
{
	QJsonObject object;
	object.insert(QStringLiteral("virtualPath"), warning.virtualPath);
	object.insert(QStringLiteral("message"), warning.message);
	return object;
}

QJsonArray packageWarningsJson(const QVector<PackageLoadWarning>& warnings)
{
	QJsonArray array;
	for (const PackageLoadWarning& warning : warnings) {
		array.append(packageWarningJson(warning));
	}
	return array;
}

QJsonObject packageSummaryJson(const PackageArchiveSummary& summary)
{
	QJsonObject object;
	object.insert(QStringLiteral("sourcePath"), summary.sourcePath);
	object.insert(QStringLiteral("format"), packageArchiveFormatId(summary.format));
	object.insert(QStringLiteral("formatLabel"), packageArchiveFormatDisplayName(summary.format));
	object.insert(QStringLiteral("entryCount"), summary.entryCount);
	object.insert(QStringLiteral("fileCount"), summary.fileCount);
	object.insert(QStringLiteral("directoryCount"), summary.directoryCount);
	object.insert(QStringLiteral("nestedArchiveCount"), summary.nestedArchiveCount);
	object.insert(QStringLiteral("totalSizeBytes"), summary.totalSizeOverflow ? QJsonValue() : QJsonValue(static_cast<double>(summary.totalSizeBytes)));
	object.insert(QStringLiteral("totalSizeBytesExact"), summary.totalSizeOverflow ? QJsonValue() : QJsonValue(QString::number(summary.totalSizeBytes)));
	object.insert(QStringLiteral("totalSizeOverflow"), summary.totalSizeOverflow);
	object.insert(QStringLiteral("warningCount"), summary.warningCount);
	return object;
}

QJsonObject packageEntryJson(const PackageEntry& entry)
{
	QJsonObject object;
	object.insert(QStringLiteral("virtualPath"), entry.virtualPath);
	object.insert(QStringLiteral("kind"), packageEntryKindId(entry.kind));
	object.insert(QStringLiteral("sizeBytes"), static_cast<double>(entry.sizeBytes));
	object.insert(QStringLiteral("compressedSizeBytes"), static_cast<double>(entry.compressedSizeBytes));
	object.insert(QStringLiteral("dataOffset"), static_cast<double>(entry.dataOffset));
	object.insert(QStringLiteral("sourceOrdinal"), entry.sourceOrdinal);
	if (entry.modifiedUtc.isValid()) {
		object.insert(QStringLiteral("modifiedUtc"), entry.modifiedUtc.toUTC().toString(Qt::ISODate));
	}
	object.insert(QStringLiteral("typeHint"), entry.typeHint);
	object.insert(QStringLiteral("storageMethod"), entry.storageMethod);
	object.insert(QStringLiteral("sourceArchiveId"), entry.sourceArchiveId);
	object.insert(QStringLiteral("nestedArchiveCandidate"), entry.nestedArchiveCandidate);
	object.insert(QStringLiteral("readable"), entry.readable);
	object.insert(QStringLiteral("note"), entry.note);
	return object;
}

QJsonObject packageInfoJson(const PackageArchive& archive)
{
	QJsonObject object;
	object.insert(QStringLiteral("summary"), packageSummaryJson(archive.summary()));
	object.insert(QStringLiteral("warnings"), packageWarningsJson(archive.warnings()));
	return object;
}

QJsonObject packageTimingJson(qint64 packageOpenMs, qint64 previewMs = -1)
{
	QJsonObject object;
	const qint64 normalizedPackageOpenMs = std::max<qint64>(0, packageOpenMs);
	object.insert(QStringLiteral("packageOpenMs"), static_cast<double>(normalizedPackageOpenMs));
	if (previewMs >= 0) {
		object.insert(QStringLiteral("previewMs"), static_cast<double>(previewMs));
		object.insert(QStringLiteral("totalMs"), static_cast<double>(normalizedPackageOpenMs + previewMs));
	} else {
		object.insert(QStringLiteral("totalMs"), static_cast<double>(normalizedPackageOpenMs));
	}
	return object;
}

QJsonObject packageListJson(const PackageArchive& archive)
{
	QJsonObject object = packageInfoJson(archive);
	QJsonArray entries;
	qsizetype index = 0;
	for (const PackageEntry& entry : archive.entries()) {
		QJsonObject item = packageEntryJson(entry);
		item.insert(QStringLiteral("entryIndex"), index++);
		entries.append(item);
	}
	object.insert(QStringLiteral("entries"), entries);
	return object;
}

QJsonObject packagePreviewJson(const PackagePreview& preview)
{
	QJsonObject object;
	object.insert(QStringLiteral("virtualPath"), preview.virtualPath);
	object.insert(QStringLiteral("kind"), packagePreviewKindId(preview.kind));
	object.insert(QStringLiteral("kindLabel"), packagePreviewKindDisplayName(preview.kind));
	object.insert(QStringLiteral("assetKind"), preview.assetKindId);
	object.insert(QStringLiteral("title"), preview.title);
	object.insert(QStringLiteral("summary"), preview.summary);
	object.insert(QStringLiteral("body"), preview.body);
	object.insert(QStringLiteral("details"), stringArrayJson(preview.detailLines));
	object.insert(QStringLiteral("raw"), stringArrayJson(preview.rawLines));
	object.insert(QStringLiteral("assetDetails"), stringArrayJson(preview.assetDetailLines));
	object.insert(QStringLiteral("assetRaw"), stringArrayJson(preview.assetRawLines));
	object.insert(QStringLiteral("truncated"), preview.truncated);
	object.insert(QStringLiteral("bytesRead"), static_cast<double>(preview.bytesRead));
	object.insert(QStringLiteral("totalBytes"), static_cast<double>(preview.totalBytes));
	object.insert(QStringLiteral("bytesReadExact"), QString::number(preview.bytesRead));
	object.insert(QStringLiteral("totalBytesExact"), preview.totalBytesKnown ? QJsonValue(QString::number(preview.totalBytes)) : QJsonValue());
	object.insert(QStringLiteral("totalBytesKnown"), preview.totalBytesKnown);
	object.insert(QStringLiteral("error"), preview.error);
	object.insert(QStringLiteral("imageFormat"), preview.imageFormat);
	object.insert(QStringLiteral("imageDepth"), preview.imageDepth);
	object.insert(QStringLiteral("imageColorCount"), preview.imageColorCount);
	object.insert(QStringLiteral("imagePaletteAware"), preview.imagePaletteAware);
	object.insert(QStringLiteral("imagePalette"), stringArrayJson(preview.imagePaletteLines));
	if (preview.imageSize.isValid()) {
		QJsonObject size;
		size.insert(QStringLiteral("width"), preview.imageSize.width());
		size.insert(QStringLiteral("height"), preview.imageSize.height());
		object.insert(QStringLiteral("imageSize"), size);
	}
	object.insert(QStringLiteral("modelFormat"), preview.modelFormat);
	object.insert(QStringLiteral("modelViewport"), stringArrayJson(preview.modelViewportLines));
	object.insert(QStringLiteral("modelMaterials"), stringArrayJson(preview.modelMaterialLines));
	object.insert(QStringLiteral("modelAnimations"), stringArrayJson(preview.modelAnimationLines));
	object.insert(QStringLiteral("audioFormat"), preview.audioFormat);
	object.insert(QStringLiteral("audioCodec"), preview.audioCodec);
	object.insert(QStringLiteral("audioChannels"), preview.audioChannels);
	object.insert(QStringLiteral("audioSampleRate"), preview.audioSampleRate);
	object.insert(QStringLiteral("audioBitsPerSample"), preview.audioBitsPerSample);
	object.insert(QStringLiteral("audioDurationMs"), static_cast<double>(preview.audioDurationMs));
	object.insert(QStringLiteral("audioDurationMsExact"), QString::number(preview.audioDurationMs));
	object.insert(QStringLiteral("audioPlaybackCandidate"), preview.audioPlaybackCandidate);
	object.insert(QStringLiteral("audioWaveform"), stringArrayJson(preview.audioWaveformLines));
	object.insert(QStringLiteral("textLanguageId"), preview.textLanguageId);
	object.insert(QStringLiteral("textLanguageName"), preview.textLanguageName);
	object.insert(QStringLiteral("textHighlights"), stringArrayJson(preview.textHighlightLines));
	object.insert(QStringLiteral("textDiagnostics"), stringArrayJson(preview.textDiagnosticLines));
	object.insert(QStringLiteral("textSaveState"), preview.textSaveState);
	return object;
}

QJsonObject packageExtractionEntryJson(const PackageExtractionEntryResult& result)
{
	QJsonObject object;
	object.insert(QStringLiteral("virtualPath"), result.virtualPath);
	object.insert(QStringLiteral("entryIndex"), static_cast<double>(result.entryIndex));
	object.insert(QStringLiteral("sourceOrdinal"), static_cast<double>(result.sourceOrdinal));
	object.insert(QStringLiteral("outputPath"), result.outputPath);
	object.insert(QStringLiteral("kind"), packageEntryKindId(result.kind));
	object.insert(QStringLiteral("bytes"), static_cast<double>(result.bytes));
	object.insert(QStringLiteral("dryRun"), result.dryRun);
	object.insert(QStringLiteral("written"), result.written);
	object.insert(QStringLiteral("skipped"), result.skipped);
	object.insert(QStringLiteral("message"), result.message);
	object.insert(QStringLiteral("error"), result.error);
	return object;
}

QJsonObject packageExtractionReportJson(const PackageExtractionReport& report)
{
	QJsonObject object;
	object.insert(QStringLiteral("sourcePath"), report.sourcePath);
	object.insert(QStringLiteral("targetDirectory"), report.targetDirectory);
	object.insert(QStringLiteral("extractAll"), report.extractAll);
	object.insert(QStringLiteral("dryRun"), report.dryRun);
	object.insert(QStringLiteral("overwriteExisting"), report.overwriteExisting);
	object.insert(QStringLiteral("cancelled"), report.cancelled);
	object.insert(QStringLiteral("succeeded"), report.succeeded());
	object.insert(QStringLiteral("requestedCount"), report.requestedCount);
	object.insert(QStringLiteral("processedCount"), report.processedCount);
	object.insert(QStringLiteral("writtenCount"), report.writtenCount);
	object.insert(QStringLiteral("directoryCount"), report.directoryCount);
	object.insert(QStringLiteral("skippedCount"), report.skippedCount);
	object.insert(QStringLiteral("errorCount"), report.errorCount);
	object.insert(QStringLiteral("totalBytes"), static_cast<double>(report.totalBytes));
	object.insert(QStringLiteral("bytesRead"), static_cast<double>(report.bytesRead));
	object.insert(QStringLiteral("warnings"), stringArrayJson(report.warnings));
	QJsonArray entries;
	for (const PackageExtractionEntryResult& result : report.entries) {
		entries.append(packageExtractionEntryJson(result));
	}
	object.insert(QStringLiteral("entries"), entries);
	return object;
}

QJsonObject assetImageConversionEntryJson(const AssetImageConversionEntryResult& result)
{
	QJsonObject object;
	object.insert(QStringLiteral("virtualPath"), result.virtualPath);
	object.insert(QStringLiteral("outputPath"), result.outputPath);
	object.insert(QStringLiteral("outputFormat"), result.outputFormat);
	if (result.beforeSize.isValid()) {
		QJsonObject size;
		size.insert(QStringLiteral("width"), result.beforeSize.width());
		size.insert(QStringLiteral("height"), result.beforeSize.height());
		object.insert(QStringLiteral("beforeSize"), size);
	}
	if (result.afterSize.isValid()) {
		QJsonObject size;
		size.insert(QStringLiteral("width"), result.afterSize.width());
		size.insert(QStringLiteral("height"), result.afterSize.height());
		object.insert(QStringLiteral("afterSize"), size);
	}
	object.insert(QStringLiteral("inputBytes"), static_cast<double>(result.inputBytes));
	object.insert(QStringLiteral("outputBytes"), static_cast<double>(result.outputBytes));
	object.insert(QStringLiteral("dryRun"), result.dryRun);
	object.insert(QStringLiteral("written"), result.written);
	object.insert(QStringLiteral("message"), result.message);
	object.insert(QStringLiteral("error"), result.error);
	object.insert(QStringLiteral("preview"), stringArrayJson(result.previewLines));
	return object;
}

QJsonObject assetImageConversionReportJson(const AssetImageConversionReport& report)
{
	QJsonObject object;
	object.insert(QStringLiteral("sourcePath"), report.sourcePath);
	object.insert(QStringLiteral("outputDirectory"), report.outputDirectory);
	object.insert(QStringLiteral("requestedCount"), report.requestedCount);
	object.insert(QStringLiteral("processedCount"), report.processedCount);
	object.insert(QStringLiteral("writtenCount"), report.writtenCount);
	object.insert(QStringLiteral("errorCount"), report.errorCount);
	object.insert(QStringLiteral("totalInputBytes"), static_cast<double>(report.totalInputBytes));
	object.insert(QStringLiteral("totalOutputBytes"), static_cast<double>(report.totalOutputBytes));
	object.insert(QStringLiteral("dryRun"), report.dryRun);
	object.insert(QStringLiteral("succeeded"), report.succeeded());
	object.insert(QStringLiteral("warnings"), stringArrayJson(report.warnings));
	QJsonArray entries;
	for (const AssetImageConversionEntryResult& result : report.entries) {
		entries.append(assetImageConversionEntryJson(result));
	}
	object.insert(QStringLiteral("entries"), entries);
	return object;
}

QJsonObject assetAudioExportReportJson(const AssetAudioExportReport& report)
{
	QJsonObject object;
	object.insert(QStringLiteral("sourcePath"), report.sourcePath);
	object.insert(QStringLiteral("virtualPath"), report.virtualPath);
	object.insert(QStringLiteral("outputPath"), report.outputPath);
	object.insert(QStringLiteral("bytes"), static_cast<double>(report.bytes));
	object.insert(QStringLiteral("dryRun"), report.dryRun);
	object.insert(QStringLiteral("written"), report.written);
	object.insert(QStringLiteral("cancelled"), report.cancelled);
	object.insert(QStringLiteral("sourceFormat"), report.sourceFormat);
	object.insert(QStringLiteral("converted"), report.converted);
	object.insert(QStringLiteral("details"), QJsonArray::fromStringList(report.detailLines));
	object.insert(QStringLiteral("message"), report.message);
	object.insert(QStringLiteral("error"), report.error);
	object.insert(QStringLiteral("succeeded"), report.succeeded());
	return object;
}

QJsonObject assetTextMatchJson(const AssetTextMatch& match)
{
	QJsonObject object;
	object.insert(QStringLiteral("filePath"), match.filePath);
	object.insert(QStringLiteral("line"), match.line);
	object.insert(QStringLiteral("column"), match.column);
	object.insert(QStringLiteral("source"), match.bufferId.isEmpty() ? QStringLiteral("disk") : QStringLiteral("buffer"));
	object.insert(QStringLiteral("encoding"), match.encoding);
	object.insert(QStringLiteral("textSha256"), QString::fromLatin1(match.textSha256.toHex()));
	if (!match.bufferId.isEmpty()) {
		object.insert(QStringLiteral("bufferId"), match.bufferId);
		object.insert(QStringLiteral("bufferRevision"), match.bufferRevision);
	}
	object.insert(QStringLiteral("lineText"), match.lineText.left(2000));
	object.insert(QStringLiteral("replacementLine"), match.replacementLine.left(2000));
	object.insert(QStringLiteral("previewTruncated"), match.lineText.size() > 2000 || match.replacementLine.size() > 2000);
	return object;
}

QJsonObject assetTextSearchReportJson(const AssetTextSearchReport& report)
{
	QJsonObject object;
	object.insert(QStringLiteral("rootPath"), report.rootPath);
	object.insert(QStringLiteral("findText"), report.findText);
	object.insert(QStringLiteral("replaceText"), report.replaceText);
	object.insert(QStringLiteral("replace"), report.replace);
	object.insert(QStringLiteral("dryRun"), report.dryRun);
	object.insert(QStringLiteral("filesScanned"), report.filesScanned);
	object.insert(QStringLiteral("buffersScanned"), report.buffersScanned);
	object.insert(QStringLiteral("filesSkipped"), report.filesSkipped);
	object.insert(QStringLiteral("complete"), report.complete);
	object.insert(QStringLiteral("cancelled"), report.cancelled);
	object.insert(QStringLiteral("canApply"), report.canApply());
	object.insert(QStringLiteral("replacementsApplied"), report.replacementsApplied);
	object.insert(QStringLiteral("writtenFiles"), stringArrayJson(report.writtenFiles));
	object.insert(QStringLiteral("editedBuffers"), stringArrayJson(report.editedBuffers));
	object.insert(QStringLiteral("bufferEditsPending"), report.bufferEditsPending);
	object.insert(QStringLiteral("filesWithMatches"), report.filesWithMatches);
	object.insert(QStringLiteral("matchCount"), report.matchCount);
	object.insert(QStringLiteral("replacementCount"), report.replacementCount);
	object.insert(QStringLiteral("saveState"), report.saveState);
	object.insert(QStringLiteral("warnings"), stringArrayJson(report.warnings));
	object.insert(QStringLiteral("succeeded"), report.succeeded());
	QJsonArray matches;
	for (const AssetTextMatch& match : report.matches) {
		matches.append(assetTextMatchJson(match));
	}
	object.insert(QStringLiteral("matches"), matches);
	if (report.hasLanguageEdits()) {
		object.insert(QStringLiteral("semanticRename"), report.semanticRename); object.insert(QStringLiteral("provider"), report.referenceProvider);
		object.insert(QStringLiteral("codeActionTitle"), report.codeActionTitle);
		object.insert(QStringLiteral("planSha256"), QString::fromLatin1(languageWorkspaceEditPlanHash(report).toHex()));
		QJsonArray changes;
		for (const auto& change : report.changes) {
			QJsonArray edits;
			for (const auto& edit : change.edits) { edits << QJsonObject {{QStringLiteral("offset"), qint64(edit.offset)}, {QStringLiteral("length"), qint64(edit.length)}, {QStringLiteral("text"), edit.replacement}}; }
			changes << QJsonObject {{QStringLiteral("filePath"), change.filePath}, {QStringLiteral("originalSha256"), QString::fromLatin1(change.originalSha256.toHex())},
				{QStringLiteral("textSha256"), QString::fromLatin1(change.textSha256.toHex())}, {QStringLiteral("edits"), edits}};
		}
		object.insert(QStringLiteral("changes"), changes);
	}
	return object;
}

QJsonObject projectManifestJson(const ProjectManifest& manifest)
{
	QJsonObject object;
	object.insert(QStringLiteral("schemaVersion"), manifest.schemaVersion);
	object.insert(QStringLiteral("projectId"), manifest.projectId);
	object.insert(QStringLiteral("displayName"), manifest.displayName);
	object.insert(QStringLiteral("rootPath"), manifest.rootPath);
	object.insert(QStringLiteral("manifestPath"), projectManifestPath(manifest.rootPath));
	object.insert(QStringLiteral("sourceFolders"), stringArrayJson(manifest.sourceFolders));
	object.insert(QStringLiteral("packageFolders"), stringArrayJson(manifest.packageFolders));
	object.insert(QStringLiteral("outputFolder"), manifest.outputFolder);
	object.insert(QStringLiteral("tempFolder"), manifest.tempFolder);
	object.insert(QStringLiteral("selectedInstallationId"), manifest.selectedInstallationId);
	object.insert(QStringLiteral("compilerSearchPaths"), stringArrayJson(manifest.compilerSearchPaths));
	QJsonArray compilerOverrides;
	for (const CompilerToolPathOverride& override : manifest.compilerToolOverrides) {
		QJsonObject overrideObject;
		overrideObject.insert(QStringLiteral("toolId"), override.toolId);
		overrideObject.insert(QStringLiteral("executablePath"), override.executablePath);
		compilerOverrides.append(overrideObject);
	}
	object.insert(QStringLiteral("compilerToolOverrides"), compilerOverrides);
	object.insert(QStringLiteral("registeredOutputPaths"), stringArrayJson(manifest.registeredOutputPaths));
	QJsonObject overrides;
	overrides.insert(QStringLiteral("selectedInstallationId"), manifest.settingsOverrides.selectedInstallationId);
	overrides.insert(QStringLiteral("editorProfileId"), manifest.settingsOverrides.editorProfileId);
	overrides.insert(QStringLiteral("paletteId"), manifest.settingsOverrides.paletteId);
	overrides.insert(QStringLiteral("compilerProfileId"), manifest.settingsOverrides.compilerProfileId);
	overrides.insert(QStringLiteral("aiFreeModeSet"), manifest.settingsOverrides.aiFreeModeSet);
	if (manifest.settingsOverrides.aiFreeModeSet) {
		overrides.insert(QStringLiteral("aiFreeMode"), manifest.settingsOverrides.aiFreeMode);
	}
	object.insert(QStringLiteral("settingsOverrides"), overrides);
	if (manifest.createdUtc.isValid()) {
		object.insert(QStringLiteral("createdUtc"), manifest.createdUtc.toUTC().toString(Qt::ISODate));
	}
	if (manifest.updatedUtc.isValid()) {
		object.insert(QStringLiteral("updatedUtc"), manifest.updatedUtc.toUTC().toString(Qt::ISODate));
	}
	return object;
}

QJsonObject projectHealthCheckJson(const ProjectHealthCheck& check)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), check.id);
	object.insert(QStringLiteral("title"), check.title);
	object.insert(QStringLiteral("detail"), check.detail);
	object.insert(QStringLiteral("state"), operationStateJson(check.state));
	return object;
}

QJsonObject projectHealthJson(const ProjectHealthSummary& health)
{
	QJsonObject object;
	object.insert(QStringLiteral("title"), health.title);
	object.insert(QStringLiteral("detail"), health.detail);
	object.insert(QStringLiteral("state"), operationStateJson(health.overallState()));
	object.insert(QStringLiteral("readyCount"), health.readyCount);
	object.insert(QStringLiteral("warningCount"), health.warningCount);
	object.insert(QStringLiteral("failedCount"), health.failedCount);
	QJsonArray checks;
	for (const ProjectHealthCheck& check : health.checks) {
		checks.append(projectHealthCheckJson(check));
	}
	object.insert(QStringLiteral("checks"), checks);
	return object;
}

QJsonObject gameInstallationValidationJson(const GameInstallationValidation& validation)
{
	QJsonObject object;
	object.insert(QStringLiteral("usable"), validation.isUsable());
	object.insert(QStringLiteral("rootExists"), validation.rootExists);
	object.insert(QStringLiteral("rootIsDirectory"), validation.rootIsDirectory);
	object.insert(QStringLiteral("executableExists"), validation.executableExists);
	object.insert(QStringLiteral("executableIsFile"), validation.executableIsFile);
	object.insert(QStringLiteral("warnings"), stringArrayJson(validation.warnings));
	object.insert(QStringLiteral("errors"), stringArrayJson(validation.errors));
	return object;
}

QJsonObject gameInstallationProfileJson(const GameInstallationProfile& profile, const QString& selectedId)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), profile.id);
	object.insert(QStringLiteral("gameKey"), profile.gameKey);
	object.insert(QStringLiteral("engineFamily"), gameEngineFamilyId(profile.engineFamily));
	object.insert(QStringLiteral("engineFamilyLabel"), gameEngineFamilyDisplayName(profile.engineFamily));
	object.insert(QStringLiteral("displayName"), profile.displayName);
	object.insert(QStringLiteral("rootPath"), profile.rootPath);
	object.insert(QStringLiteral("executablePath"), profile.executablePath);
	object.insert(QStringLiteral("basePackagePaths"), stringArrayJson(profile.basePackagePaths));
	object.insert(QStringLiteral("modPackagePaths"), stringArrayJson(profile.modPackagePaths));
	object.insert(QStringLiteral("paletteId"), profile.paletteId);
	object.insert(QStringLiteral("compilerProfileId"), profile.compilerProfileId);
	object.insert(QStringLiteral("readOnly"), profile.readOnly);
	object.insert(QStringLiteral("active"), profile.active);
	object.insert(QStringLiteral("hidden"), profile.hidden);
	object.insert(QStringLiteral("manual"), profile.manual);
	object.insert(QStringLiteral("selected"), sameGameInstallationId(profile.id, selectedId));
	if (profile.createdUtc.isValid()) {
		object.insert(QStringLiteral("createdUtc"), profile.createdUtc.toUTC().toString(Qt::ISODate));
	}
	if (profile.updatedUtc.isValid()) {
		object.insert(QStringLiteral("updatedUtc"), profile.updatedUtc.toUTC().toString(Qt::ISODate));
	}
	object.insert(QStringLiteral("validation"), gameInstallationValidationJson(validateGameInstallationProfile(profile)));
	return object;
}

QJsonObject gameInstallationDetectionCandidateJson(const GameInstallationDetectionCandidate& candidate)
{
	QJsonObject object;
	object.insert(QStringLiteral("sourceId"), candidate.sourceId);
	object.insert(QStringLiteral("sourceName"), candidate.sourceName);
	object.insert(QStringLiteral("confidencePercent"), candidate.confidencePercent);
	object.insert(QStringLiteral("matchedPaths"), stringArrayJson(candidate.matchedPaths));
	object.insert(QStringLiteral("warnings"), stringArrayJson(candidate.warnings));
	object.insert(QStringLiteral("profile"), gameInstallationProfileJson(candidate.profile, QString()));
	return object;
}

QJsonObject gameInstallationDetectionJson(const QVector<GameInstallationDetectionCandidate>& candidates)
{
	QJsonObject object;
	object.insert(QStringLiteral("candidateCount"), candidates.size());
	QJsonArray array;
	for (const GameInstallationDetectionCandidate& candidate : candidates) {
		array.append(gameInstallationDetectionCandidateJson(candidate));
	}
	object.insert(QStringLiteral("candidates"), array);
	return object;
}

QJsonObject gameInstallationsJson(const StudioSettings& settings)
{
	QJsonObject object;
	const QString selectedId = settings.selectedGameInstallationId();
	object.insert(QStringLiteral("selectedInstallationId"), selectedId);
	QJsonArray profiles;
	for (const GameInstallationProfile& profile : settings.gameInstallations()) {
		profiles.append(gameInstallationProfileJson(profile, selectedId));
	}
	object.insert(QStringLiteral("profiles"), profiles);
	return object;
}

QJsonObject compilerToolDiscoveryJson(const CompilerToolDiscovery& discovery)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), discovery.descriptor.id);
	object.insert(QStringLiteral("integrationId"), discovery.descriptor.integrationId);
	object.insert(QStringLiteral("displayName"), discovery.descriptor.displayName);
	object.insert(QStringLiteral("engineFamily"), discovery.descriptor.engineFamily);
	object.insert(QStringLiteral("role"), discovery.descriptor.role);
	object.insert(QStringLiteral("declaredSourcePath"), discovery.descriptor.sourcePath);
	object.insert(QStringLiteral("executableNames"), stringArrayJson(discovery.descriptor.executableNames));
	object.insert(QStringLiteral("candidateRelativePaths"), stringArrayJson(discovery.descriptor.candidateRelativePaths));
	object.insert(QStringLiteral("versionProbeArguments"), stringArrayJson(discovery.descriptor.versionProbeArguments));
	object.insert(QStringLiteral("sourceAvailable"), discovery.sourceAvailable);
	object.insert(QStringLiteral("executableAvailable"), discovery.executableAvailable);
	object.insert(QStringLiteral("executablePathOverridden"), discovery.executablePathOverridden);
	object.insert(QStringLiteral("versionProbeAttempted"), discovery.versionProbeAttempted);
	object.insert(QStringLiteral("versionAvailable"), discovery.versionAvailable);
	object.insert(QStringLiteral("sourcePath"), discovery.sourcePath);
	object.insert(QStringLiteral("executablePath"), discovery.executablePath);
	object.insert(QStringLiteral("versionText"), discovery.versionText);
	object.insert(QStringLiteral("versionProbeCommandLine"), discovery.versionProbeCommandLine);
	object.insert(QStringLiteral("capabilityFlags"), stringArrayJson(discovery.capabilityFlags));
	object.insert(QStringLiteral("warnings"), stringArrayJson(discovery.warnings));
	object.insert(QStringLiteral("state"), operationStateJson(discovery.state()));
	return object;
}

QJsonObject compilerRegistryJson(const CompilerRegistrySummary& summary)
{
	QJsonObject object;
	object.insert(QStringLiteral("state"), operationStateJson(summary.overallState()));
	object.insert(QStringLiteral("sourceAvailableCount"), summary.sourceAvailableCount);
	object.insert(QStringLiteral("executableAvailableCount"), summary.executableAvailableCount);
	object.insert(QStringLiteral("warningCount"), summary.warningCount);
	QJsonArray tools;
	for (const CompilerToolDiscovery& discovery : summary.tools) {
		tools.append(compilerToolDiscoveryJson(discovery));
	}
	object.insert(QStringLiteral("tools"), tools);
	return object;
}

QJsonObject compilerProfileJson(const CompilerProfileDescriptor& profile)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), profile.id);
	object.insert(QStringLiteral("toolId"), profile.toolId);
	object.insert(QStringLiteral("displayName"), profile.displayName);
	object.insert(QStringLiteral("engineFamily"), profile.engineFamily);
	object.insert(QStringLiteral("stageId"), profile.stageId);
	object.insert(QStringLiteral("inputDescription"), profile.inputDescription);
	object.insert(QStringLiteral("inputExtensions"), stringArrayJson(profile.inputExtensions));
	object.insert(QStringLiteral("defaultOutputExtension"), profile.defaultOutputExtension);
	object.insert(QStringLiteral("description"), profile.description);
	object.insert(QStringLiteral("defaultArguments"), stringArrayJson(profile.defaultArguments));
	object.insert(QStringLiteral("inputRequired"), profile.inputRequired);
	object.insert(QStringLiteral("outputPathArgumentSupported"), profile.outputPathArgumentSupported);
	return object;
}

QJsonArray compilerProfilesJson()
{
	QJsonArray profiles;
	for (const CompilerProfileDescriptor& profile : compilerProfileDescriptors()) {
		profiles.append(compilerProfileJson(profile));
	}
	return profiles;
}

QJsonObject editorProfileBindingJson(const EditorProfileBinding& binding)
{
	QJsonObject object;
	object.insert(QStringLiteral("actionId"), binding.actionId);
	object.insert(QStringLiteral("displayName"), binding.displayName);
	object.insert(QStringLiteral("shortcut"), binding.shortcut);
	object.insert(QStringLiteral("mouseGesture"), binding.mouseGesture);
	object.insert(QStringLiteral("context"), binding.context);
	object.insert(QStringLiteral("commandId"), binding.commandId);
	object.insert(QStringLiteral("surfaceId"), binding.surfaceId);
	object.insert(QStringLiteral("implemented"), binding.implemented);
	return object;
}

// How the Levels views answer the mouse and keys under a profile: the facts
// a script needs, and the same reference rows the Controls window shows.
QJsonObject levelEditorControlsJson(const LevelEditorControls& controls)
{
	QJsonObject object;
	object.insert(QStringLiteral("layout"), levelViewLayoutId(controls.layout));
	object.insert(QStringLiteral("defaultGridUnits"), controls.defaultGridUnits);
	object.insert(QStringLiteral("perspectiveCamera"), controls.camera.perspective);
	object.insert(QStringLiteral("fieldOfViewDegrees"), controls.camera.fieldOfViewDegrees);
	object.insert(QStringLiteral("emptyDrag"), planEmptyDragId(controls.plan.emptyDrag));
	object.insert(QStringLiteral("emptyDragWithSelection"), planEmptyDragId(controls.plan.emptyDragWithSelection));
	object.insert(QStringLiteral("cameraWheel"), cameraWheelId(controls.camera.wheel));
	object.insert(QStringLiteral("flyKeys"), stringArrayJson(controls.camera.flyKeys.all()));
	object.insert(QStringLiteral("flyNeedsMouseLook"), controls.camera.flyNeedsLook);
	object.insert(QStringLiteral("mouseLookToggleKey"), controls.camera.lookToggleKey);
	object.insert(QStringLiteral("mouseLookHoldKey"), controls.camera.lookHoldKey);
	object.insert(QStringLiteral("planPanHoldKey"), controls.plan.panHoldKey);
	QJsonArray rows;
	for (const LevelEditorControlRow& row : levelEditorControlRows(controls)) {
		QJsonObject entry;
		entry.insert(QStringLiteral("section"), row.section);
		entry.insert(QStringLiteral("view"), row.view);
		entry.insert(QStringLiteral("action"), row.action);
		entry.insert(QStringLiteral("gesture"), row.gesture);
		rows.append(entry);
	}
	object.insert(QStringLiteral("rows"), rows);
	object.insert(QStringLiteral("problems"), stringArrayJson(levelEditorControlProblems(controls)));
	return object;
}

QJsonObject editorProfileJson(const EditorProfileDescriptor& profile, const QString& selectedId = QString())
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), profile.id);
	object.insert(QStringLiteral("displayName"), profile.displayName);
	object.insert(QStringLiteral("shortName"), profile.shortName);
	object.insert(QStringLiteral("lineage"), profile.lineage);
	object.insert(QStringLiteral("aliases"), stringArrayJson(profile.aliases));
	object.insert(QStringLiteral("adaptations"), stringArrayJson(profile.adaptations));
	object.insert(QStringLiteral("referenceUrl"), profile.referenceUrl);
	object.insert(QStringLiteral("description"), profile.description);
	object.insert(QStringLiteral("layoutPresetId"), profile.layoutPresetId);
	object.insert(QStringLiteral("cameraPresetId"), profile.cameraPresetId);
	object.insert(QStringLiteral("selectionPresetId"), profile.selectionPresetId);
	object.insert(QStringLiteral("gridPresetId"), profile.gridPresetId);
	object.insert(QStringLiteral("terminologyPresetId"), profile.terminologyPresetId);
	object.insert(QStringLiteral("supportedEngineFamilies"), stringArrayJson(profile.supportedEngineFamilies));
	object.insert(QStringLiteral("defaultPanels"), stringArrayJson(profile.defaultPanels));
	object.insert(QStringLiteral("workflowNotes"), stringArrayJson(profile.workflowNotes));
	object.insert(QStringLiteral("keybindingNotes"), stringArrayJson(profile.keybindingNotes));
	object.insert(QStringLiteral("mouseBindingNotes"), stringArrayJson(profile.mouseBindingNotes));
	object.insert(QStringLiteral("placeholder"), profile.placeholder);
	object.insert(QStringLiteral("unresolvedPresets"), stringArrayJson(profile.unresolvedPresets));
	object.insert(QStringLiteral("controls"), levelEditorControlsJson(profile.controls));
	object.insert(QStringLiteral("selected"), !selectedId.isEmpty() && profile.id == selectedId);
	QJsonArray bindings;
	for (const EditorProfileBinding& binding : profile.bindings) {
		bindings.append(editorProfileBindingJson(binding));
	}
	object.insert(QStringLiteral("bindings"), bindings);
	return object;
}

QJsonArray editorProfilesJson(const QString& selectedId = QString())
{
	QJsonArray profiles;
	for (const EditorProfileDescriptor& profile : editorProfileDescriptors()) {
		profiles.append(editorProfileJson(profile, selectedId));
	}
	return profiles;
}

QJsonObject localizationTargetJson(const LocalizationTarget& target)
{
	QJsonObject object;
	object.insert(QStringLiteral("localeName"), target.localeName);
	object.insert(QStringLiteral("englishName"), target.englishName);
	object.insert(QStringLiteral("nativeName"), target.nativeName);
	object.insert(QStringLiteral("rightToLeft"), target.rightToLeft);
	return object;
}

QJsonArray localizationTargetsJson()
{
	QJsonArray array;
	for (const LocalizationTarget& target : localizationTargets()) {
		array.append(localizationTargetJson(target));
	}
	return array;
}

QJsonObject localeFormattingSampleJson(const LocaleFormattingSample& sample)
{
	QJsonObject object;
	object.insert(QStringLiteral("localeName"), sample.localeName);
	object.insert(QStringLiteral("decimalNumber"), sample.decimalNumber);
	object.insert(QStringLiteral("integerNumber"), sample.integerNumber);
	object.insert(QStringLiteral("date"), sample.date);
	object.insert(QStringLiteral("time"), sample.time);
	object.insert(QStringLiteral("dateTime"), sample.dateTime);
	object.insert(QStringLiteral("size"), sample.size);
	object.insert(QStringLiteral("duration"), sample.duration);
	object.insert(QStringLiteral("sortedLabels"), stringArrayJson(sample.sortedLabels));
	return object;
}

QJsonObject pluralizationSmokeSampleJson(const PluralizationSmokeSample& sample)
{
	QJsonObject object;
	object.insert(QStringLiteral("localeName"), sample.localeName);
	object.insert(QStringLiteral("count"), sample.count);
	object.insert(QStringLiteral("localizedNumber"), sample.localizedNumber);
	object.insert(QStringLiteral("text"), sample.text);
	object.insert(QStringLiteral("singular"), sample.singular);
	object.insert(QStringLiteral("localizedNumberVisible"), sample.localizedNumberVisible);
	return object;
}

QJsonObject translationExpansionLayoutCheckJson(const TranslationExpansionLayoutCheck& check)
{
	QJsonObject object;
	object.insert(QStringLiteral("surfaceId"), check.surfaceId);
	object.insert(QStringLiteral("label"), check.label);
	object.insert(QStringLiteral("sourceText"), check.sourceText);
	object.insert(QStringLiteral("expandedText"), check.expandedText);
	object.insert(QStringLiteral("sourceLength"), check.sourceLength);
	object.insert(QStringLiteral("expandedLength"), check.expandedLength);
	object.insert(QStringLiteral("maxRecommendedCharacters"), check.maxRecommendedCharacters);
	object.insert(QStringLiteral("expansionRatio"), check.expansionRatio);
	object.insert(QStringLiteral("passed"), check.passed);
	object.insert(QStringLiteral("recommendation"), check.recommendation);
	return object;
}

QJsonObject translationCatalogStatusJson(const TranslationCatalogStatus& status)
{
	QJsonObject object;
	object.insert(QStringLiteral("localeName"), status.localeName);
	object.insert(QStringLiteral("fileName"), status.fileName);
	object.insert(QStringLiteral("path"), status.path);
	object.insert(QStringLiteral("present"), status.present);
	object.insert(QStringLiteral("stale"), status.stale);
	object.insert(QStringLiteral("messageCount"), status.messageCount);
	object.insert(QStringLiteral("translatedCount"), status.translatedCount);
	object.insert(QStringLiteral("unfinishedCount"), status.unfinishedCount);
	object.insert(QStringLiteral("obsoleteCount"), status.obsoleteCount);
	object.insert(QStringLiteral("vanishedCount"), status.vanishedCount);
	object.insert(QStringLiteral("status"), status.status);
	object.insert(QStringLiteral("issues"), stringArrayJson(status.issues));
	return object;
}

QJsonObject localizationSmokeReportJson(const LocalizationSmokeReport& report)
{
	QJsonObject object;
	object.insert(QStringLiteral("localeName"), report.localeName);
	object.insert(QStringLiteral("targetCount"), report.targets.size());
	object.insert(QStringLiteral("targets"), localizationTargetsJson());
	object.insert(QStringLiteral("pseudoSample"), report.pseudoSample);
	object.insert(QStringLiteral("expansionSample"), report.expansionSample);
	object.insert(QStringLiteral("expansionSourceLength"), report.expansionSourceLength);
	object.insert(QStringLiteral("expansionSampleLength"), report.expansionSampleLength);
	object.insert(QStringLiteral("expansionRatio"), report.expansionRatio);
	object.insert(QStringLiteral("expansionSmokeOk"), report.expansionSmokeOk);
	object.insert(QStringLiteral("expansionLayoutSmokeOk"), report.expansionLayoutSmokeOk);
	object.insert(QStringLiteral("pluralizationSmokeOk"), report.pluralizationSmokeOk);
	object.insert(QStringLiteral("rightToLeftLocales"), stringArrayJson(report.rightToLeftLocales));
	object.insert(QStringLiteral("formatting"), localeFormattingSampleJson(report.formatting));
	QJsonArray pluralization;
	for (const PluralizationSmokeSample& sample : report.pluralization) {
		pluralization.append(pluralizationSmokeSampleJson(sample));
	}
	object.insert(QStringLiteral("pluralization"), pluralization);
	QJsonArray layoutChecks;
	for (const TranslationExpansionLayoutCheck& check : report.layoutChecks) {
		layoutChecks.append(translationExpansionLayoutCheckJson(check));
	}
	object.insert(QStringLiteral("layoutChecks"), layoutChecks);
	QJsonArray catalogs;
	for (const TranslationCatalogStatus& status : report.catalogs) {
		catalogs.append(translationCatalogStatusJson(status));
	}
	object.insert(QStringLiteral("catalogs"), catalogs);
	object.insert(QStringLiteral("catalogCount"), report.catalogCount);
	object.insert(QStringLiteral("staleCatalogCount"), report.staleCatalogCount);
	object.insert(QStringLiteral("untranslatedMessageCount"), report.untranslatedMessageCount);
	object.insert(QStringLiteral("obsoleteMessageCount"), report.obsoleteMessageCount);
	object.insert(QStringLiteral("warnings"), stringArrayJson(report.warnings));
	object.insert(QStringLiteral("ok"), report.ok);
	return object;
}

QJsonObject studioModuleJson(const StudioModule& module)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), module.id);
	object.insert(QStringLiteral("name"), module.name);
	object.insert(QStringLiteral("category"), module.category);
	object.insert(QStringLiteral("maturity"), module.maturity);
	object.insert(QStringLiteral("description"), module.description);
	object.insert(QStringLiteral("engines"), stringArrayJson(module.engines));
	return object;
}

QJsonArray studioModulesJson()
{
	QJsonArray array;
	for (const StudioModule& module : plannedModules()) {
		array.append(studioModuleJson(module));
	}
	return array;
}

QJsonObject diagnosticBundleJson()
{
	QJsonObject object;
	object.insert(QStringLiteral("schemaVersion"), 1);
	object.insert(QStringLiteral("generatedUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
	object.insert(QStringLiteral("version"), versionString());
	object.insert(QStringLiteral("qtVersion"), QString::fromUtf8(qVersion()));
	object.insert(QStringLiteral("buildAbi"), QSysInfo::buildAbi());
	object.insert(QStringLiteral("kernelType"), QSysInfo::kernelType());
	object.insert(QStringLiteral("kernelVersion"), QSysInfo::kernelVersion());
	object.insert(QStringLiteral("productType"), QSysInfo::productType());
	object.insert(QStringLiteral("productVersion"), QSysInfo::productVersion());
	object.insert(QStringLiteral("commandCount"), cliCommandDescriptors().size());
	object.insert(QStringLiteral("commands"), cliCommandsJson());
	object.insert(QStringLiteral("modules"), studioModulesJson());
	object.insert(QStringLiteral("operationStates"), operationStatesJson());
	object.insert(QStringLiteral("uiSemantics"), uiSemanticsJson());
	object.insert(QStringLiteral("localization"), localizationSmokeReportJson(buildLocalizationSmokeReport(QStringLiteral("en"))));
	object.insert(QStringLiteral("redaction"), QStringLiteral("No secrets, API keys, environment values, home-directory contents, or project file payloads are included."));
	return object;
}

QJsonObject aiCapabilityJson(const AiCapabilityDescriptor& capability)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), capability.id);
	object.insert(QStringLiteral("displayName"), capability.displayName);
	object.insert(QStringLiteral("description"), capability.description);
	return object;
}

QJsonArray aiCapabilitiesJson()
{
	QJsonArray capabilities;
	for (const AiCapabilityDescriptor& capability : aiCapabilityDescriptors()) {
		capabilities.append(aiCapabilityJson(capability));
	}
	return capabilities;
}

QJsonObject aiConnectorJson(const AiConnectorDescriptor& connector)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), connector.id);
	object.insert(QStringLiteral("displayName"), connector.displayName);
	object.insert(QStringLiteral("providerFamily"), connector.providerFamily);
	object.insert(QStringLiteral("endpointKind"), connector.endpointKind);
	object.insert(QStringLiteral("authRequirement"), connector.authRequirement);
	object.insert(QStringLiteral("privacyNote"), connector.privacyNote);
	object.insert(QStringLiteral("capabilities"), stringArrayJson(connector.capabilities));
	object.insert(QStringLiteral("configurationNotes"), stringArrayJson(connector.configurationNotes));
	object.insert(QStringLiteral("cloudBased"), connector.cloudBased);
	object.insert(QStringLiteral("implemented"), connector.implemented);
	return object;
}

QJsonArray aiConnectorsJson()
{
	QJsonArray connectors;
	for (const AiConnectorDescriptor& connector : aiConnectorDescriptors()) {
		connectors.append(aiConnectorJson(connector));
	}
	return connectors;
}

QJsonObject aiModelJson(const AiModelDescriptor& model)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), model.id);
	object.insert(QStringLiteral("connectorId"), model.connectorId);
	object.insert(QStringLiteral("displayName"), model.displayName);
	object.insert(QStringLiteral("description"), model.description);
	object.insert(QStringLiteral("capabilities"), stringArrayJson(model.capabilities));
	object.insert(QStringLiteral("defaultForConnector"), model.defaultForConnector);
	return object;
}

QJsonArray aiModelsJson()
{
	QJsonArray models;
	for (const AiModelDescriptor& model : aiModelDescriptors()) {
		models.append(aiModelJson(model));
	}
	return models;
}

QJsonObject aiCredentialStatusJson(const AiCredentialStatus& status)
{
	QJsonObject object;
	object.insert(QStringLiteral("connectorId"), status.connectorId);
	object.insert(QStringLiteral("configured"), status.configured);
	object.insert(QStringLiteral("source"), status.source);
	object.insert(QStringLiteral("lookupKey"), status.lookupKey);
	object.insert(QStringLiteral("redactedValue"), status.redactedValue);
	object.insert(QStringLiteral("warnings"), stringArrayJson(status.warnings));
	return object;
}

QJsonArray aiCredentialStatusesJson(const AiAutomationPreferences& preferences)
{
	QJsonArray statuses;
	for (const AiCredentialStatus& status : aiCredentialStatuses(preferences)) {
		statuses.append(aiCredentialStatusJson(status));
	}
	return statuses;
}

QJsonArray aiToolsJson()
{
	QJsonArray tools;
	for (const AiToolDescriptor& tool : aiToolDescriptors()) {
		tools.append(aiToolDescriptorJson(tool));
	}
	return tools;
}

QJsonObject aiPreferencesJson(const AiAutomationPreferences& preferences)
{
	const AiAutomationPreferences normalized = normalizedAiAutomationPreferences(preferences);
	QJsonObject object;
	object.insert(QStringLiteral("aiFreeMode"), normalized.aiFreeMode);
	object.insert(QStringLiteral("projectAiFree"), normalized.projectAiFree);
	object.insert(QStringLiteral("cloudConnectorsEnabled"), normalized.cloudConnectorsEnabled);
	object.insert(QStringLiteral("agenticWorkflowsEnabled"), normalized.agenticWorkflowsEnabled);
	object.insert(QStringLiteral("preferredReasoningConnectorId"), normalized.preferredReasoningConnectorId);
	object.insert(QStringLiteral("preferredCodingConnectorId"), normalized.preferredCodingConnectorId);
	object.insert(QStringLiteral("preferredVisionConnectorId"), normalized.preferredVisionConnectorId);
	object.insert(QStringLiteral("preferredImageConnectorId"), normalized.preferredImageConnectorId);
	object.insert(QStringLiteral("preferredAudioConnectorId"), normalized.preferredAudioConnectorId);
	object.insert(QStringLiteral("preferredVoiceConnectorId"), normalized.preferredVoiceConnectorId);
	object.insert(QStringLiteral("preferredThreeDConnectorId"), normalized.preferredThreeDConnectorId);
	object.insert(QStringLiteral("preferredEmbeddingsConnectorId"), normalized.preferredEmbeddingsConnectorId);
	object.insert(QStringLiteral("preferredLocalConnectorId"), normalized.preferredLocalConnectorId);
	object.insert(QStringLiteral("preferredTextModelId"), normalized.preferredTextModelId);
	object.insert(QStringLiteral("preferredCodingModelId"), normalized.preferredCodingModelId);
	object.insert(QStringLiteral("preferredVisionModelId"), normalized.preferredVisionModelId);
	object.insert(QStringLiteral("preferredImageModelId"), normalized.preferredImageModelId);
	object.insert(QStringLiteral("preferredAudioModelId"), normalized.preferredAudioModelId);
	object.insert(QStringLiteral("preferredVoiceModelId"), normalized.preferredVoiceModelId);
	object.insert(QStringLiteral("preferredThreeDModelId"), normalized.preferredThreeDModelId);
	object.insert(QStringLiteral("preferredEmbeddingsModelId"), normalized.preferredEmbeddingsModelId);
	object.insert(QStringLiteral("openAiCredentialEnvironmentVariable"), normalized.openAiCredentialEnvironmentVariable);
	object.insert(QStringLiteral("elevenLabsCredentialEnvironmentVariable"), normalized.elevenLabsCredentialEnvironmentVariable);
	object.insert(QStringLiteral("meshyCredentialEnvironmentVariable"), normalized.meshyCredentialEnvironmentVariable);
	object.insert(QStringLiteral("customHttpCredentialEnvironmentVariable"), normalized.customHttpCredentialEnvironmentVariable);
	return object;
}

QJsonObject aiWorkflowResultJson(const AiWorkflowResult& result)
{
	QJsonObject object;
	object.insert(QStringLiteral("title"), result.title);
	object.insert(QStringLiteral("summary"), result.summary);
	object.insert(QStringLiteral("reviewableText"), result.reviewableText);
	object.insert(QStringLiteral("commandLine"), result.commandLine);
	object.insert(QStringLiteral("nextActions"), stringArrayJson(result.nextActions));
	object.insert(QStringLiteral("diagnostics"), stringArrayJson(result.diagnostics));
	object.insert(QStringLiteral("manifest"), aiWorkflowManifestJson(result.manifest));
	return object;
}

QJsonObject taskStateJson(const QString& taskId, OperationState state, const QString& summary, qint64 durationMs, bool cancellable)
{
	QJsonObject object;
	object.insert(QStringLiteral("taskId"), taskId);
	object.insert(QStringLiteral("state"), operationStateJson(state));
	object.insert(QStringLiteral("summary"), summary);
	object.insert(QStringLiteral("durationMs"), QString::number(durationMs));
	object.insert(QStringLiteral("cancellable"), cancellable);
	object.insert(QStringLiteral("updatedUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
	return object;
}

QJsonObject compilerCommandPlanJson(const CompilerCommandPlan& plan)
{
	QJsonObject object;
	object.insert(QStringLiteral("profileFound"), plan.profileFound);
	object.insert(QStringLiteral("toolFound"), plan.toolFound);
	object.insert(QStringLiteral("executableAvailable"), plan.executableAvailable);
	object.insert(QStringLiteral("runnable"), plan.isRunnable());
	object.insert(QStringLiteral("state"), operationStateJson(plan.state()));
	if (plan.profileFound) {
		object.insert(QStringLiteral("profile"), compilerProfileJson(plan.profile));
	}
	if (plan.toolFound) {
		object.insert(QStringLiteral("tool"), compilerToolDiscoveryJson(plan.tool));
	}
	object.insert(QStringLiteral("program"), plan.program);
	object.insert(QStringLiteral("arguments"), stringArrayJson(plan.arguments));
	object.insert(QStringLiteral("workingDirectory"), plan.workingDirectory);
	object.insert(QStringLiteral("inputPath"), plan.inputPath);
	object.insert(QStringLiteral("expectedOutputPath"), plan.expectedOutputPath);
	object.insert(QStringLiteral("commandLine"), plan.commandLine);
	object.insert(QStringLiteral("knownIssueWarnings"), stringArrayJson(plan.knownIssueWarnings));
	object.insert(QStringLiteral("preflightWarnings"), stringArrayJson(plan.preflightWarnings));
	object.insert(QStringLiteral("warnings"), stringArrayJson(plan.warnings));
	object.insert(QStringLiteral("errors"), stringArrayJson(plan.errors));
	return object;
}

QStringList optionPathList(const QStringList& args, const QString& option)
{
	const QString value = optionValue(args, option);
	if (value.trimmed().isEmpty()) {
		return {};
	}
	return value.split(';', Qt::SkipEmptyParts);
}

QJsonObject compilerDiagnosticJson(const CompilerDiagnostic& diagnostic)
{
	QJsonObject object;
	object.insert(QStringLiteral("level"), diagnostic.level);
	object.insert(QStringLiteral("message"), diagnostic.message);
	object.insert(QStringLiteral("filePath"), diagnostic.filePath);
	object.insert(QStringLiteral("line"), diagnostic.line);
	object.insert(QStringLiteral("column"), diagnostic.column);
	object.insert(QStringLiteral("rawLine"), diagnostic.rawLine);
	return object;
}

QJsonArray compilerDiagnosticsJson(const QVector<CompilerDiagnostic>& diagnostics)
{
	QJsonArray array;
	for (const CompilerDiagnostic& diagnostic : diagnostics) {
		array.append(compilerDiagnosticJson(diagnostic));
	}
	return array;
}

QJsonObject compilerRunResultJson(const CompilerRunResult& result)
{
	QJsonObject object;
	object.insert(QStringLiteral("state"), operationStateJson(result.state));
	object.insert(QStringLiteral("started"), result.started);
	object.insert(QStringLiteral("timedOut"), result.timedOut);
	object.insert(QStringLiteral("cancelled"), result.cancelled);
	object.insert(QStringLiteral("exitCode"), result.exitCode);
	object.insert(QStringLiteral("durationMs"), QString::number(result.durationMs));
	object.insert(QStringLiteral("stdout"), result.stdoutText);
	object.insert(QStringLiteral("stderr"), result.stderrText);
	object.insert(QStringLiteral("diagnostics"), compilerDiagnosticsJson(result.diagnostics));
	object.insert(QStringLiteral("registeredOutputPaths"), stringArrayJson(result.registeredOutputPaths));
	object.insert(QStringLiteral("manifestPath"), result.manifestPath);
	object.insert(QStringLiteral("error"), result.error);
	object.insert(QStringLiteral("manifest"), compilerCommandManifestJson(result.manifest));
	return object;
}

QJsonObject levelMapVec3Json(const LevelMapVec3& value)
{
	QJsonObject object;
	object.insert(QStringLiteral("valid"), value.valid);
	object.insert(QStringLiteral("x"), value.x);
	object.insert(QStringLiteral("y"), value.y);
	object.insert(QStringLiteral("z"), value.z);
	return object;
}

QJsonObject levelMapIssueJson(const LevelMapIssue& issue)
{
	QJsonObject object;
	object.insert(QStringLiteral("severity"), levelMapIssueSeverityId(issue.severity));
	object.insert(QStringLiteral("code"), issue.code);
	object.insert(QStringLiteral("message"), issue.message);
	object.insert(QStringLiteral("objectId"), issue.objectId);
	object.insert(QStringLiteral("line"), issue.line);
	return object;
}

QJsonArray levelMapIssuesJson(const QVector<LevelMapIssue>& issues)
{
	QJsonArray array;
	for (const LevelMapIssue& issue : issues) {
		array.append(levelMapIssueJson(issue));
	}
	return array;
}

QJsonObject levelMapPropertyJson(const LevelMapProperty& property)
{
	QJsonObject object;
	object.insert(QStringLiteral("key"), property.key);
	object.insert(QStringLiteral("value"), property.value);
	object.insert(QStringLiteral("line"), property.line);
	return object;
}

QJsonArray levelMapPropertiesJson(const QVector<LevelMapProperty>& properties)
{
	QJsonArray array;
	for (const LevelMapProperty& property : properties) {
		array.append(levelMapPropertyJson(property));
	}
	return array;
}

QJsonObject levelMapEntityJson(const LevelMapEntity& entity)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), entity.id);
	object.insert(QStringLiteral("className"), entity.className);
	object.insert(QStringLiteral("origin"), levelMapVec3Json(entity.origin));
	object.insert(QStringLiteral("startLine"), entity.startLine);
	object.insert(QStringLiteral("endLine"), entity.endLine);
	object.insert(QStringLiteral("selected"), entity.selected);
	object.insert(QStringLiteral("properties"), levelMapPropertiesJson(entity.properties));
	return object;
}

QJsonArray levelMapEntitiesJson(const QVector<LevelMapEntity>& entities)
{
	QJsonArray array;
	for (const LevelMapEntity& entity : entities) {
		array.append(levelMapEntityJson(entity));
	}
	return array;
}

QJsonObject levelMapBrushJson(const LevelMapBrush& brush)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), brush.id);
	object.insert(QStringLiteral("entityId"), brush.entityId);
	object.insert(QStringLiteral("faceCount"), brush.faceCount);
	object.insert(QStringLiteral("textureNames"), stringArrayJson(brush.textureNames));
	object.insert(QStringLiteral("mins"), levelMapVec3Json(brush.mins));
	object.insert(QStringLiteral("maxs"), levelMapVec3Json(brush.maxs));
	object.insert(QStringLiteral("selected"), brush.selected);
	// Each face as `map edit` sets it: faceN.texture, .shiftx, .shifty,
	// .rotation, .scalex, .scaley, N counted from 1.
	QJsonArray faces;
	for (const LevelMapBrushFace& face : brush.faces) {
		QJsonObject entry;
		entry.insert(QStringLiteral("face"), face.id + 1);
		entry.insert(QStringLiteral("texture"), face.textureName);
		entry.insert(QStringLiteral("shiftx"), face.explicitTextureAxes ? face.uOffset : face.shiftX);
		entry.insert(QStringLiteral("shifty"), face.explicitTextureAxes ? face.vOffset : face.shiftY);
		entry.insert(QStringLiteral("rotation"), face.rotation);
		entry.insert(QStringLiteral("scalex"), face.scaleX);
		entry.insert(QStringLiteral("scaley"), face.scaleY);
		entry.insert(QStringLiteral("valve220"), face.explicitTextureAxes);
		entry.insert(QStringLiteral("brushPrimitive"), face.explicitTextureMatrix);
		if (face.explicitTextureMatrix) {
			QJsonArray matrix;
			for (double value : face.textureMatrix) { matrix.append(value); }
			entry.insert(QStringLiteral("matrix"), matrix);
		}
		entry.insert(QStringLiteral("line"), face.line);
		faces.append(entry);
	}
	object.insert(QStringLiteral("faces"), faces);
	return object;
}

QJsonArray levelMapBrushesJson(const QVector<LevelMapBrush>& brushes)
{
	QJsonArray array;
	for (const LevelMapBrush& brush : brushes) {
		array.append(levelMapBrushJson(brush));
	}
	return array;
}

QJsonObject levelMapDoomVertexJson(const LevelMapDoomVertex& vertex)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), vertex.id);
	object.insert(QStringLiteral("x"), vertex.x);
	object.insert(QStringLiteral("y"), vertex.y);
	object.insert(QStringLiteral("selected"), vertex.selected);
	return object;
}

QJsonArray levelMapDoomVerticesJson(const QVector<LevelMapDoomVertex>& vertices)
{
	QJsonArray array;
	for (const LevelMapDoomVertex& vertex : vertices) {
		array.append(levelMapDoomVertexJson(vertex));
	}
	return array;
}

QJsonArray hexenArgsJson(const std::array<int, 5>& args)
{
	QJsonArray array;
	for (const int arg : args) {
		array.append(arg);
	}
	return array;
}

// A Hexen map's lines and things carry five special arguments, and its things
// a thing id, a height, and a special, which the JSON carries too.
QJsonObject levelMapDoomLinedefJson(const LevelMapDoomLinedef& linedef, bool hexen)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), linedef.id);
	object.insert(QStringLiteral("startVertex"), linedef.startVertex);
	object.insert(QStringLiteral("endVertex"), linedef.endVertex);
	object.insert(QStringLiteral("flags"), linedef.flags);
	object.insert(QStringLiteral("special"), linedef.special);
	object.insert(QStringLiteral("tag"), linedef.tag);
	object.insert(QStringLiteral("frontSidedef"), linedef.frontSidedef);
	object.insert(QStringLiteral("backSidedef"), linedef.backSidedef);
	object.insert(QStringLiteral("selected"), linedef.selected);
	if (hexen) {
		object.insert(QStringLiteral("args"), hexenArgsJson(linedef.args));
	}
	return object;
}

QJsonArray levelMapDoomLinedefsJson(const QVector<LevelMapDoomLinedef>& linedefs, bool hexen)
{
	QJsonArray array;
	for (const LevelMapDoomLinedef& linedef : linedefs) {
		array.append(levelMapDoomLinedefJson(linedef, hexen));
	}
	return array;
}

QJsonObject levelMapDoomThingJson(const LevelMapDoomThing& thing, bool hexen)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), thing.id);
	object.insert(QStringLiteral("x"), thing.x);
	object.insert(QStringLiteral("y"), thing.y);
	object.insert(QStringLiteral("angle"), thing.angle);
	object.insert(QStringLiteral("type"), thing.type);
	object.insert(QStringLiteral("flags"), thing.flags);
	object.insert(QStringLiteral("selected"), thing.selected);
	if (hexen) {
		object.insert(QStringLiteral("z"), thing.z);
		object.insert(QStringLiteral("tid"), thing.tid);
		object.insert(QStringLiteral("special"), thing.special);
		object.insert(QStringLiteral("args"), hexenArgsJson(thing.args));
	}
	return object;
}

QJsonArray levelMapDoomThingsJson(const QVector<LevelMapDoomThing>& things, bool hexen)
{
	QJsonArray array;
	for (const LevelMapDoomThing& thing : things) {
		array.append(levelMapDoomThingJson(thing, hexen));
	}
	return array;
}

QJsonArray levelMapPatchesJson(const QVector<LevelMapPatch>& patches)
{
	QJsonArray array;
	for (const LevelMapPatch& patch : patches) {
		QJsonObject object;
		object.insert(QStringLiteral("id"), patch.id);
		object.insert(QStringLiteral("entityId"), patch.entityId);
		object.insert(QStringLiteral("textureName"), patch.textureName);
		object.insert(QStringLiteral("width"), patch.width);
		object.insert(QStringLiteral("height"), patch.height);
		object.insert(QStringLiteral("fixedSubdivisions"), patch.fixedSubdivisions);
		object.insert(QStringLiteral("subdivisionsX"), patch.subdivisionsX);
		object.insert(QStringLiteral("subdivisionsY"), patch.subdivisionsY);
		QJsonArray points;
		for (int index = 0; index < patch.controlPoints.size(); ++index) {
			QJsonObject point = levelMapVec3Json(patch.controlPoints.at(index));
			point.insert(QStringLiteral("row"), patch.width > 0 ? index / patch.width : -1);
			point.insert(QStringLiteral("column"), patch.width > 0 ? index % patch.width : -1);
			point.insert(QStringLiteral("u"), patch.controlU.value(index));
			point.insert(QStringLiteral("v"), patch.controlV.value(index));
			points.append(point);
		}
		object.insert(QStringLiteral("controlPoints"), points);
		object.insert(QStringLiteral("mins"), levelMapVec3Json(patch.mins));
		object.insert(QStringLiteral("maxs"), levelMapVec3Json(patch.maxs));
		object.insert(QStringLiteral("startLine"), patch.startLine);
		object.insert(QStringLiteral("endLine"), patch.endLine);
		object.insert(QStringLiteral("selected"), patch.selected);
		array.append(object);
	}
	return array;
}

QJsonArray levelMapDoomSidedefsJson(const QVector<LevelMapDoomSidedef>& sidedefs)
{
	QJsonArray array;
	for (const LevelMapDoomSidedef& sidedef : sidedefs) {
		QJsonObject object;
		object.insert(QStringLiteral("id"), sidedef.id);
		object.insert(QStringLiteral("sector"), sidedef.sector);
		object.insert(QStringLiteral("offsetX"), sidedef.offsetX);
		object.insert(QStringLiteral("offsetY"), sidedef.offsetY);
		object.insert(QStringLiteral("upperTexture"), sidedef.upperTexture);
		object.insert(QStringLiteral("lowerTexture"), sidedef.lowerTexture);
		object.insert(QStringLiteral("middleTexture"), sidedef.middleTexture);
		array.append(object);
	}
	return array;
}

QJsonArray levelMapDoomSectorsJson(const QVector<LevelMapDoomSector>& sectors)
{
	QJsonArray array;
	for (const LevelMapDoomSector& sector : sectors) {
		QJsonObject object;
		object.insert(QStringLiteral("id"), sector.id);
		object.insert(QStringLiteral("floorHeight"), sector.floorHeight);
		object.insert(QStringLiteral("ceilingHeight"), sector.ceilingHeight);
		object.insert(QStringLiteral("floorTexture"), sector.floorTexture);
		object.insert(QStringLiteral("ceilingTexture"), sector.ceilingTexture);
		object.insert(QStringLiteral("lightLevel"), sector.lightLevel);
		object.insert(QStringLiteral("special"), sector.special);
		object.insert(QStringLiteral("tag"), sector.tag);
		object.insert(QStringLiteral("selected"), sector.selected);
		array.append(object);
	}
	return array;
}

QJsonArray levelMapTargetLinksJson(const QVector<LevelMapTargetLink>& links)
{
	QJsonArray array;
	for (const LevelMapTargetLink& link : links) {
		QJsonObject object;
		object.insert(QStringLiteral("sourceEntityId"), link.sourceEntityId);
		object.insert(QStringLiteral("targetEntityId"), link.targetEntityId);
		object.insert(QStringLiteral("key"), link.key);
		object.insert(QStringLiteral("name"), link.name);
		object.insert(QStringLiteral("from"), levelMapVec3Json(link.from));
		object.insert(QStringLiteral("to"), levelMapVec3Json(link.to));
		array.append(object);
	}
	return array;
}

QJsonObject levelMapStatisticsJson(const LevelMapStatistics& stats)
{
	QJsonObject object;
	object.insert(QStringLiteral("entityCount"), stats.entityCount);
	object.insert(QStringLiteral("brushCount"), stats.brushCount);
	object.insert(QStringLiteral("brushFaceCount"), stats.brushFaceCount);
	object.insert(QStringLiteral("doomThingCount"), stats.doomThingCount);
	object.insert(QStringLiteral("doomVertexCount"), stats.doomVertexCount);
	object.insert(QStringLiteral("doomLinedefCount"), stats.doomLinedefCount);
	object.insert(QStringLiteral("doomSidedefCount"), stats.doomSidedefCount);
	object.insert(QStringLiteral("doomSectorCount"), stats.doomSectorCount);
	object.insert(QStringLiteral("textureReferenceCount"), stats.textureReferenceCount);
	object.insert(QStringLiteral("uniqueTextureCount"), stats.uniqueTextureCount);
	object.insert(QStringLiteral("issueCount"), stats.issueCount);
	object.insert(QStringLiteral("warningCount"), stats.warningCount);
	object.insert(QStringLiteral("errorCount"), stats.errorCount);
	object.insert(QStringLiteral("mins"), levelMapVec3Json(stats.mins));
	object.insert(QStringLiteral("maxs"), levelMapVec3Json(stats.maxs));
	return object;
}

QJsonObject levelMapDocumentJson(const LevelMapDocument& document)
{
	QJsonObject object;
	if (document.doomUdmf) { object.insert(QStringLiteral("udmfNamespace"), document.doomUdmf->nameSpace); }
	if (document.format == LevelMapFormat::DoomWad) { object.insert(QStringLiteral("nodeBuild"), levelDoomNodeReportJson(inspectLevelDoomNodes(document))); }
	object.insert(QStringLiteral("sourcePath"), document.sourcePath);
	object.insert(QStringLiteral("outputPath"), document.outputPath);
	object.insert(QStringLiteral("mapName"), document.mapName);
	object.insert(QStringLiteral("engineFamily"), document.engineFamily);
	object.insert(QStringLiteral("format"), levelMapFormatId(document.format));
	object.insert(QStringLiteral("formatLabel"), levelMapFormatDisplayName(document.format));
	object.insert(QStringLiteral("editState"), document.editState);
	object.insert(QStringLiteral("selectionKind"), levelMapSelectionKindId(document.selectionKind));
	object.insert(QStringLiteral("selectedObjectId"), document.selectedObjectId);
	object.insert(QStringLiteral("statistics"), levelMapStatisticsJson(levelMapStatistics(document)));
	object.insert(QStringLiteral("statisticsLines"), stringArrayJson(levelMapStatisticsLines(document)));
	object.insert(QStringLiteral("entityLines"), stringArrayJson(levelMapEntityLines(document)));
	object.insert(QStringLiteral("textureLines"), stringArrayJson(levelMapTextureLines(document)));
	object.insert(QStringLiteral("validationLines"), stringArrayJson(levelMapValidationLines(document)));
	object.insert(QStringLiteral("viewLines"), stringArrayJson(levelMapViewLines(document)));
	object.insert(QStringLiteral("selectionLines"), stringArrayJson(levelMapSelectionLines(document)));
	object.insert(QStringLiteral("propertyLines"), stringArrayJson(levelMapPropertyLines(document)));
	object.insert(QStringLiteral("undoLines"), stringArrayJson(levelMapUndoLines(document)));
	object.insert(QStringLiteral("textureReferences"), stringArrayJson(document.textureReferences));
	object.insert(QStringLiteral("issues"), levelMapIssuesJson(document.issues));
	object.insert(QStringLiteral("entities"), levelMapEntitiesJson(document.entities));
	object.insert(QStringLiteral("brushes"), levelMapBrushesJson(document.brushes));
	object.insert(QStringLiteral("patches"), levelMapPatchesJson(document.patches));
	object.insert(QStringLiteral("targetLinks"), levelMapTargetLinksJson(levelMapTargetLinks(document)));
	object.insert(QStringLiteral("doomVertices"), levelMapDoomVerticesJson(document.doomVertices));
	object.insert(QStringLiteral("doomLinedefs"), levelMapDoomLinedefsJson(document.doomLinedefs, document.doomFormat == LevelMapDoomFormat::Hexen));
	object.insert(QStringLiteral("doomSidedefs"), levelMapDoomSidedefsJson(document.doomSidedefs));
	object.insert(QStringLiteral("doomSectors"), levelMapDoomSectorsJson(document.doomSectors));
	object.insert(QStringLiteral("doomThings"), levelMapDoomThingsJson(document.doomThings, document.doomFormat == LevelMapDoomFormat::Hexen));
	return object;
}

QJsonObject levelMapSaveReportJson(const LevelMapSaveReport& report)
{
	QJsonObject object;
	object.insert(QStringLiteral("sourcePath"), report.sourcePath);
	object.insert(QStringLiteral("outputPath"), report.outputPath);
	object.insert(QStringLiteral("backupPath"), report.backupPath);
	object.insert(QStringLiteral("sha256"), QString::fromLatin1(report.contentHash.toHex()));
	object.insert(QStringLiteral("mapName"), report.mapName);
	object.insert(QStringLiteral("format"), levelMapFormatId(report.format));
	object.insert(QStringLiteral("dryRun"), report.dryRun);
	object.insert(QStringLiteral("written"), report.written);
	object.insert(QStringLiteral("editState"), report.editState);
	object.insert(QStringLiteral("summaryLines"), stringArrayJson(report.summaryLines));
	object.insert(QStringLiteral("staleLumps"), stringArrayJson(report.staleLumps));
	object.insert(QStringLiteral("warnings"), stringArrayJson(report.warnings));
	object.insert(QStringLiteral("errors"), stringArrayJson(report.errors));
	object.insert(QStringLiteral("succeeded"), report.succeeded());
	return object;
}

QJsonObject advancedStudioIssueJson(const AdvancedStudioIssue& issue)
{
	QJsonObject object;
	object.insert(QStringLiteral("severity"), issue.severity);
	object.insert(QStringLiteral("code"), issue.code);
	object.insert(QStringLiteral("message"), issue.message);
	object.insert(QStringLiteral("objectId"), issue.objectId);
	object.insert(QStringLiteral("line"), issue.line);
	return object;
}

QJsonArray advancedStudioIssuesJson(const QVector<AdvancedStudioIssue>& issues)
{
	QJsonArray array;
	for (const AdvancedStudioIssue& issue : issues) {
		array.append(advancedStudioIssueJson(issue));
	}
	return array;
}

QJsonObject shaderStageJson(const ShaderStage& stage)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), stage.id);
	object.insert(QStringLiteral("shaderName"), stage.shaderName);
	object.insert(QStringLiteral("startLine"), stage.startLine);
	object.insert(QStringLiteral("endLine"), stage.endLine);
	object.insert(QStringLiteral("mapDirective"), stage.mapDirective);
	object.insert(QStringLiteral("blendFunc"), stage.blendFunc);
	object.insert(QStringLiteral("rgbGen"), stage.rgbGen);
	object.insert(QStringLiteral("alphaGen"), stage.alphaGen);
	object.insert(QStringLiteral("tcMod"), stage.tcMod);
	object.insert(QStringLiteral("directives"), stringArrayJson(stage.directives));
	object.insert(QStringLiteral("textureReferences"), stringArrayJson(stage.textureReferences));
	object.insert(QStringLiteral("rawText"), stage.rawText);
	object.insert(QStringLiteral("selected"), stage.selected);
	return object;
}

QJsonArray shaderStagesJson(const QVector<ShaderStage>& stages)
{
	QJsonArray array;
	for (const ShaderStage& stage : stages) {
		array.append(shaderStageJson(stage));
	}
	return array;
}

QJsonObject shaderDefinitionJson(const ShaderDefinition& shader)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), shader.id);
	object.insert(QStringLiteral("name"), shader.name);
	object.insert(QStringLiteral("startLine"), shader.startLine);
	object.insert(QStringLiteral("endLine"), shader.endLine);
	object.insert(QStringLiteral("directives"), stringArrayJson(shader.directives));
	object.insert(QStringLiteral("textureReferences"), stringArrayJson(shader.textureReferences));
	object.insert(QStringLiteral("rawText"), shader.rawText);
	object.insert(QStringLiteral("selected"), shader.selected);
	object.insert(QStringLiteral("stages"), shaderStagesJson(shader.stages));
	return object;
}

QJsonArray shaderDefinitionsJson(const QVector<ShaderDefinition>& shaders)
{
	QJsonArray array;
	for (const ShaderDefinition& shader : shaders) {
		array.append(shaderDefinitionJson(shader));
	}
	return array;
}

QJsonObject shaderReferenceValidationJson(const ShaderReferenceValidation& validation)
{
	QJsonObject object;
	object.insert(QStringLiteral("textureReference"), validation.textureReference);
	object.insert(QStringLiteral("candidatePaths"), stringArrayJson(validation.candidatePaths));
	object.insert(QStringLiteral("found"), validation.found);
	object.insert(QStringLiteral("foundInPackage"), validation.foundInPackage);
	return object;
}

QJsonArray shaderReferenceValidationsJson(const QVector<ShaderReferenceValidation>& validation)
{
	QJsonArray array;
	for (const ShaderReferenceValidation& item : validation) {
		array.append(shaderReferenceValidationJson(item));
	}
	return array;
}

QJsonObject advancedShaderDocumentJson(const ShaderDocument& document, const QVector<ShaderReferenceValidation>& validation = {}, const QStringList& validationWarnings = {})
{
	QJsonObject object;
	object.insert(QStringLiteral("sourcePath"), document.sourcePath);
	object.insert(QStringLiteral("editState"), document.editState);
	object.insert(QStringLiteral("shaderCount"), document.shaders.size());
	object.insert(QStringLiteral("shaders"), shaderDefinitionsJson(document.shaders));
	object.insert(QStringLiteral("issues"), advancedStudioIssuesJson(document.issues));
	object.insert(QStringLiteral("graphLines"), stringArrayJson(shaderGraphLines(document)));
	object.insert(QStringLiteral("previewLines"), stringArrayJson(shaderStagePreviewLines(document)));
	object.insert(QStringLiteral("referenceValidation"), shaderReferenceValidationsJson(validation));
	object.insert(QStringLiteral("referenceValidationLines"), stringArrayJson(shaderReferenceValidationLines(validation, validationWarnings)));
	object.insert(QStringLiteral("referenceValidationWarnings"), stringArrayJson(validationWarnings));
	return object;
}

QJsonObject shaderSaveReportJson(const ShaderSaveReport& report)
{
	QJsonObject object;
	object.insert(QStringLiteral("sourcePath"), report.sourcePath);
	object.insert(QStringLiteral("outputPath"), report.outputPath);
	object.insert(QStringLiteral("dryRun"), report.dryRun);
	object.insert(QStringLiteral("written"), report.written);
	object.insert(QStringLiteral("editState"), report.editState);
	object.insert(QStringLiteral("summaryLines"), stringArrayJson(report.summaryLines));
	object.insert(QStringLiteral("warnings"), stringArrayJson(report.warnings));
	object.insert(QStringLiteral("errors"), stringArrayJson(report.errors));
	object.insert(QStringLiteral("succeeded"), report.succeeded());
	return object;
}

QJsonObject spriteFramePlanJson(const SpriteFramePlan& frame)
{
	QJsonObject object;
	object.insert(QStringLiteral("index"), frame.index);
	object.insert(QStringLiteral("frameId"), frame.frameId);
	object.insert(QStringLiteral("rotationId"), frame.rotationId);
	object.insert(QStringLiteral("lumpName"), frame.lumpName);
	object.insert(QStringLiteral("sourcePath"), frame.sourcePath);
	object.insert(QStringLiteral("virtualPath"), frame.virtualPath);
	object.insert(QStringLiteral("paletteAction"), frame.paletteAction);
	return object;
}

QJsonObject spriteWorkflowPlanJson(const SpriteWorkflowPlan& plan)
{
	QJsonObject object;
	object.insert(QStringLiteral("engineFamily"), plan.engineFamily);
	object.insert(QStringLiteral("spriteName"), plan.spriteName);
	object.insert(QStringLiteral("paletteId"), plan.paletteId);
	object.insert(QStringLiteral("outputPackageRoot"), plan.outputPackageRoot);
	object.insert(QStringLiteral("state"), operationStateJson(plan.state));
	object.insert(QStringLiteral("palettePreviewLines"), stringArrayJson(plan.palettePreviewLines));
	object.insert(QStringLiteral("sequenceLines"), stringArrayJson(plan.sequenceLines));
	object.insert(QStringLiteral("stagingLines"), stringArrayJson(plan.stagingLines));
	object.insert(QStringLiteral("warnings"), stringArrayJson(plan.warnings));
	QJsonArray frames;
	for (const SpriteFramePlan& frame : plan.frames) {
		frames.append(spriteFramePlanJson(frame));
	}
	object.insert(QStringLiteral("frames"), frames);
	return object;
}

QJsonObject codeSourceFileJson(const CodeSourceFile& file)
{
	QJsonObject object;
	object.insert(QStringLiteral("filePath"), file.filePath);
	object.insert(QStringLiteral("relativePath"), file.relativePath);
	object.insert(QStringLiteral("languageId"), file.languageId);
	object.insert(QStringLiteral("bytes"), QString::number(file.bytes));
	object.insert(QStringLiteral("lineCount"), file.lineCount);
	object.insert(QStringLiteral("fromBuffer"), file.fromBuffer);
	return object;
}

QJsonObject codeSymbolJson(const CodeSymbol& symbol)
{
	QJsonObject object;
	object.insert(QStringLiteral("name"), symbol.name);
	object.insert(QStringLiteral("kind"), symbol.kind);
	object.insert(QStringLiteral("filePath"), symbol.filePath);
	object.insert(QStringLiteral("relativePath"), symbol.relativePath);
	object.insert(QStringLiteral("line"), symbol.line);
	object.insert(QStringLiteral("column"), symbol.column);
	return object;
}

QJsonObject languageServiceHookJson(const LanguageServiceHook& hook)
{
	QJsonObject object;
	object.insert(QStringLiteral("languageId"), hook.languageId);
	object.insert(QStringLiteral("displayName"), hook.displayName);
	object.insert(QStringLiteral("extensions"), stringArrayJson(hook.extensions));
	object.insert(QStringLiteral("capabilities"), stringArrayJson(hook.capabilities));
	object.insert(QStringLiteral("serverCommand"), hook.serverCommand);
	object.insert(QStringLiteral("available"), hook.available);
	object.insert(QStringLiteral("status"), hook.status);
	return object;
}

QJsonObject codeDiagnosticJson(const CodeDiagnostic& diagnostic)
{
	QJsonObject object;
	object.insert(QStringLiteral("severity"), diagnostic.severity);
	object.insert(QStringLiteral("message"), diagnostic.message);
	object.insert(QStringLiteral("filePath"), diagnostic.filePath);
	object.insert(QStringLiteral("relativePath"), diagnostic.relativePath);
	object.insert(QStringLiteral("line"), diagnostic.line);
	object.insert(QStringLiteral("column"), diagnostic.column);
	return object;
}

QJsonObject codeWorkspaceIndexJson(const CodeWorkspaceIndex& index)
{
	QJsonObject object;
	object.insert(QStringLiteral("complete"), index.complete);
	object.insert(QStringLiteral("cancelled"), index.cancelled);
	object.insert(QStringLiteral("filesSkipped"), index.filesSkipped);
	object.insert(QStringLiteral("entriesVisited"), index.entriesVisited);
	object.insert(QStringLiteral("bytesRead"), index.bytesRead);
	object.insert(QStringLiteral("rootPath"), index.rootPath);
	object.insert(QStringLiteral("state"), operationStateJson(index.state));
	object.insert(QStringLiteral("treeLines"), stringArrayJson(index.treeLines));
	object.insert(QStringLiteral("buildTaskLines"), stringArrayJson(index.buildTaskLines));
	object.insert(QStringLiteral("launchProfileLines"), stringArrayJson(index.launchProfileLines));
	object.insert(QStringLiteral("warnings"), stringArrayJson(index.warnings));
	QJsonArray files;
	for (const CodeSourceFile& file : index.files) {
		files.append(codeSourceFileJson(file));
	}
	object.insert(QStringLiteral("files"), files);
	QJsonArray symbols;
	for (const CodeSymbol& symbol : index.symbols) {
		symbols.append(codeSymbolJson(symbol));
	}
	object.insert(QStringLiteral("symbols"), symbols);
	QJsonArray hooks;
	for (const LanguageServiceHook& hook : index.languageHooks) {
		hooks.append(languageServiceHookJson(hook));
	}
	object.insert(QStringLiteral("languageHooks"), hooks);
	QJsonArray diagnostics;
	for (const CodeDiagnostic& diagnostic : index.diagnostics) {
		diagnostics.append(codeDiagnosticJson(diagnostic));
	}
	object.insert(QStringLiteral("diagnostics"), diagnostics);
	return object;
}

QJsonObject extensionGeneratedFileJson(const ExtensionGeneratedFile& file)
{
	QJsonObject object;
	object.insert(QStringLiteral("virtualPath"), file.virtualPath);
	object.insert(QStringLiteral("sourceDescription"), file.sourceDescription);
	object.insert(QStringLiteral("summary"), file.summary);
	object.insert(QStringLiteral("staged"), file.staged);
	return object;
}

QJsonArray extensionGeneratedFilesJson(const QVector<ExtensionGeneratedFile>& files)
{
	QJsonArray array;
	for (const ExtensionGeneratedFile& file : files) {
		array.append(extensionGeneratedFileJson(file));
	}
	return array;
}

QJsonObject extensionCommandDescriptorJson(const ExtensionCommandDescriptor& command)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), command.id);
	object.insert(QStringLiteral("displayName"), command.displayName);
	object.insert(QStringLiteral("description"), command.description);
	object.insert(QStringLiteral("program"), command.program);
	object.insert(QStringLiteral("arguments"), stringArrayJson(command.arguments));
	object.insert(QStringLiteral("workingDirectory"), command.workingDirectory);
	object.insert(QStringLiteral("capabilities"), stringArrayJson(command.capabilities));
	object.insert(QStringLiteral("generatedFiles"), extensionGeneratedFilesJson(command.generatedFiles));
	object.insert(QStringLiteral("requiresApproval"), command.requiresApproval);
	return object;
}

QJsonObject extensionManifestJson(const ExtensionManifest& manifest)
{
	QJsonObject object;
	object.insert(QStringLiteral("schemaVersion"), manifest.schemaVersion);
	object.insert(QStringLiteral("manifestPath"), manifest.manifestPath);
	object.insert(QStringLiteral("rootPath"), manifest.rootPath);
	object.insert(QStringLiteral("id"), manifest.id);
	object.insert(QStringLiteral("displayName"), manifest.displayName);
	object.insert(QStringLiteral("version"), manifest.version);
	object.insert(QStringLiteral("description"), manifest.description);
	object.insert(QStringLiteral("trustLevel"), manifest.trustLevel);
	object.insert(QStringLiteral("sandboxModel"), manifest.sandboxModel);
	object.insert(QStringLiteral("capabilities"), stringArrayJson(manifest.capabilities));
	object.insert(QStringLiteral("warnings"), stringArrayJson(manifest.warnings));
	QJsonArray commands;
	for (const ExtensionCommandDescriptor& command : manifest.commands) {
		commands.append(extensionCommandDescriptorJson(command));
	}
	object.insert(QStringLiteral("commands"), commands);
	return object;
}

QJsonObject extensionDiscoveryJson(const ExtensionDiscoveryResult& result)
{
	QJsonObject object;
	object.insert(QStringLiteral("searchRoots"), stringArrayJson(result.searchRoots));
	object.insert(QStringLiteral("state"), operationStateJson(result.state));
	object.insert(QStringLiteral("warnings"), stringArrayJson(result.warnings));
	object.insert(QStringLiteral("trustModel"), stringArrayJson(extensionTrustModelLines()));
	QJsonArray manifests;
	for (const ExtensionManifest& manifest : result.manifests) {
		manifests.append(extensionManifestJson(manifest));
	}
	object.insert(QStringLiteral("manifests"), manifests);
	return object;
}

QJsonObject extensionCommandPlanJson(const ExtensionCommandPlan& plan)
{
	QJsonObject object;
	object.insert(QStringLiteral("manifest"), extensionManifestJson(plan.manifest));
	object.insert(QStringLiteral("command"), extensionCommandDescriptorJson(plan.command));
	object.insert(QStringLiteral("program"), plan.program);
	object.insert(QStringLiteral("arguments"), stringArrayJson(plan.arguments));
	object.insert(QStringLiteral("workingDirectory"), plan.workingDirectory);
	object.insert(QStringLiteral("dryRun"), plan.dryRun);
	object.insert(QStringLiteral("executionAllowed"), plan.executionAllowed);
	object.insert(QStringLiteral("state"), operationStateJson(plan.state));
	object.insert(QStringLiteral("warnings"), stringArrayJson(plan.warnings));
	object.insert(QStringLiteral("stagingLines"), stringArrayJson(plan.stagingLines));
	return object;
}

QJsonObject extensionCommandResultJson(const ExtensionCommandResult& result)
{
	QJsonObject object;
	object.insert(QStringLiteral("plan"), extensionCommandPlanJson(result.plan));
	object.insert(QStringLiteral("started"), result.started);
	object.insert(QStringLiteral("dryRun"), result.dryRun);
	object.insert(QStringLiteral("exitCode"), result.exitCode);
	object.insert(QStringLiteral("stdout"), result.stdoutText);
	object.insert(QStringLiteral("stderr"), result.stderrText);
	object.insert(QStringLiteral("error"), result.error);
	object.insert(QStringLiteral("state"), operationStateJson(result.state));
	if (result.finishedUtc.isValid()) {
		object.insert(QStringLiteral("finishedUtc"), result.finishedUtc.toUTC().toString(Qt::ISODate));
	}
	return object;
}

QJsonArray compilerToolPathOverridesJson(const QVector<CompilerToolPathOverride>& overrides)
{
	QJsonArray array;
	for (const CompilerToolPathOverride& override : overrides) {
		QJsonObject object;
		object.insert(QStringLiteral("toolId"), override.toolId);
		object.insert(QStringLiteral("executablePath"), override.executablePath);
		array.append(object);
	}
	return array;
}

CompilerRegistryOptions compilerRegistryOptionsFromArgs(const QStringList& args)
{
	StudioSettings settings;
	CompilerRegistryOptions options;
	options.workspaceRootPath = optionValue(args, QStringLiteral("--workspace-root"));
	options.extraSearchPaths = optionPathList(args, QStringLiteral("--compiler-search-paths"));
	options.executableOverrides = settings.compilerToolPathOverrides();
	if (!options.workspaceRootPath.trimmed().isEmpty()) {
		ProjectManifest manifest;
		if (loadProjectManifest(options.workspaceRootPath, &manifest)) {
			options.extraSearchPaths = effectiveProjectCompilerSearchPaths(manifest, options.extraSearchPaths);
			options.executableOverrides = effectiveProjectCompilerToolOverrides(manifest, options.executableOverrides);
		}
	}
	return options;
}

void applyCompilerRequestContext(CompilerCommandRequest* request, const QStringList& args)
{
	if (!request) {
		return;
	}
	CompilerRegistryOptions options = compilerRegistryOptionsFromArgs(args);
	if (!request->workspaceRootPath.trimmed().isEmpty()) {
		options.workspaceRootPath = request->workspaceRootPath;
		ProjectManifest manifest;
		if (loadProjectManifest(options.workspaceRootPath, &manifest)) {
			options.extraSearchPaths = effectiveProjectCompilerSearchPaths(manifest, options.extraSearchPaths);
			options.executableOverrides = effectiveProjectCompilerToolOverrides(manifest, options.executableOverrides);
		}
	}
	request->extraSearchPaths = options.extraSearchPaths;
	request->executableOverrides = options.executableOverrides;
}

bool loadPackageForCliQuiet(const QString& path, PackageArchive* archive, QString* error, qint64* durationMs = nullptr)
{
	QString localError;
	QElapsedTimer timer;
	timer.start();
	bool loaded = false;
	if (archive && path.endsWith(QStringLiteral(".vibepackage"), Qt::CaseInsensitive)) {
		PackageStagingModel plan;
		if (PackageDraft::load(path, &plan, &localError)) {
			*archive = packagePlannedArchive(plan, &localError);
			loaded = archive->isOpen();
		}
	} else if (archive) {
		loaded = archive->load(path, &localError);
	}
	if (!loaded) {
		if (durationMs) {
			*durationMs = timer.elapsed();
		}
		if (error) {
			*error = localError.isEmpty() ? QStringLiteral("unknown error") : localError;
		}
		return false;
	}
	if (durationMs) {
		*durationMs = timer.elapsed();
	}
	return true;
}

void printPackageWarnings(const PackageArchive& archive)
{
	const QVector<PackageLoadWarning> warnings = archive.warnings();
	if (warnings.isEmpty()) {
		return;
	}
	std::cout << "Warnings\n";
	for (const PackageLoadWarning& warning : warnings) {
		std::cout << "- " << text(warning.virtualPath.isEmpty() ? QStringLiteral("(package)") : warning.virtualPath) << ": " << text(warning.message) << "\n";
	}
}

void printPackageInfo(const PackageArchive& archive)
{
	const PackageArchiveSummary summary = archive.summary();
	std::cout << "Package info\n";
	std::cout << "Path: " << text(nativePath(summary.sourcePath)) << "\n";
	std::cout << "Format: " << text(packageArchiveFormatId(summary.format)) << " (" << text(packageArchiveFormatDisplayName(summary.format)) << ")\n";
	std::cout << "Entries: " << summary.entryCount << "\n";
	std::cout << "Files: " << summary.fileCount << "\n";
	std::cout << "Directories: " << summary.directoryCount << "\n";
	std::cout << "Nested archive candidates: " << summary.nestedArchiveCount << "\n";
	std::cout << "Total file bytes: " << (summary.totalSizeOverflow ? "exceeds the supported range" : text(sizeText(summary.totalSizeBytes))) << "\n";
	std::cout << "Warnings: " << summary.warningCount << "\n";
	printPackageWarnings(archive);
}

void printPackageList(const PackageArchive& archive)
{
	printPackageInfo(archive);
	std::cout << "Entries\n";
	for (const PackageEntry& entry : archive.entries()) {
		std::cout << "- " << text(entry.virtualPath) << "\n";
		std::cout << "  Kind: " << text(packageEntryKindId(entry.kind)) << "\n";
		std::cout << "  Size: " << text(sizeText(entry.sizeBytes)) << "\n";
		std::cout << "  Type: " << text(entry.typeHint) << "\n";
		std::cout << "  Storage: " << text(entry.storageMethod.isEmpty() ? QStringLiteral("unknown") : entry.storageMethod) << "\n";
		std::cout << "  Readable: " << (entry.readable ? "yes" : "no") << "\n";
		if (entry.nestedArchiveCandidate) {
			std::cout << "  Nested archive candidate: yes\n";
		}
		if (!entry.note.isEmpty()) {
			std::cout << "  Note: " << text(entry.note) << "\n";
		}
	}
}

bool printPackagePreview(const PackageArchive& archive, const QString& virtualPath, const PackagePreview* prebuiltPreview = nullptr)
{
	const PackagePreview builtPreview = prebuiltPreview ? PackagePreview() : buildPackageEntryPreview(archive, virtualPath);
	const PackagePreview& preview = prebuiltPreview ? *prebuiltPreview : builtPreview;
	std::cout << "Package preview\n";
	std::cout << "Entry: " << text(preview.virtualPath.isEmpty() ? virtualPath : preview.virtualPath) << "\n";
	std::cout << "Kind: " << text(packagePreviewKindId(preview.kind)) << " (" << text(packagePreviewKindDisplayName(preview.kind)) << ")\n";
	if (!preview.summary.isEmpty()) {
		std::cout << "Summary: " << text(preview.summary) << "\n";
	}
	if (preview.totalBytesKnown || preview.bytesRead > 0) {
		std::cout << "Bytes sampled: " << text(sizeText(static_cast<quint64>(std::max<qint64>(0, preview.bytesRead)))) << " of " << text(sizeText(preview.totalBytes)) << "\n";
	}
	std::cout << "Truncated: " << (preview.truncated ? "yes" : "no") << "\n";
	if (!preview.error.isEmpty()) {
		std::cout << "Error: " << text(preview.error) << "\n";
	}
	if (!preview.imageFormat.isEmpty() || preview.imageSize.isValid()) {
		std::cout << "Image format: " << text(preview.imageFormat.isEmpty() ? QStringLiteral("unknown") : preview.imageFormat) << "\n";
		std::cout << "Image size: " << text(preview.imageSize.isValid() ? QStringLiteral("%1 x %2").arg(preview.imageSize.width()).arg(preview.imageSize.height()) : QStringLiteral("unknown")) << "\n";
		std::cout << "Image depth: " << preview.imageDepth << "\n";
		std::cout << "Palette-aware: " << (preview.imagePaletteAware ? "yes" : "no") << "\n";
	}
	if (!preview.modelFormat.isEmpty() || !preview.modelViewportLines.isEmpty()) {
		std::cout << "Model format: " << text(preview.modelFormat.isEmpty() ? QStringLiteral("unknown") : preview.modelFormat) << "\n";
		if (!preview.modelViewportLines.isEmpty()) {
			std::cout << "Model viewport\n";
			for (const QString& line : preview.modelViewportLines) {
				std::cout << "- " << text(line) << "\n";
			}
		}
	}
	if (!preview.audioFormat.isEmpty() || !preview.audioWaveformLines.isEmpty()) {
		std::cout << "Audio format: " << text(preview.audioFormat.isEmpty() ? QStringLiteral("unknown") : preview.audioFormat) << "\n";
		if (!preview.audioWaveformLines.isEmpty()) {
			std::cout << "Waveform\n";
			for (const QString& line : preview.audioWaveformLines) {
				std::cout << "- " << text(line) << "\n";
			}
		}
	}
	if (!preview.textLanguageName.isEmpty()) {
		std::cout << "Text language: " << text(preview.textLanguageName) << "\n";
		std::cout << "Text save state: " << text(preview.textSaveState.isEmpty() ? QStringLiteral("clean") : preview.textSaveState) << "\n";
	}
	if (!preview.detailLines.isEmpty()) {
		std::cout << "Details\n";
		for (const QString& line : preview.detailLines) {
			std::cout << "- " << text(line) << "\n";
		}
	}
	if (!preview.body.isEmpty()) {
		std::cout << "Body\n";
		std::cout << text(preview.body) << "\n";
	}
	return preview.kind != PackagePreviewKind::Unavailable;
}

void printPackageExtractionReport(const PackageExtractionReport& report)
{
	std::cout << text(packageExtractionReportText(report)) << "\n";
}

QStringList packageExtractionEntriesFromArgs(const QStringList& args)
{
	QStringList entries = optionValues(args, QStringLiteral("--extract-entry"));
	entries += optionValues(args, QStringLiteral("--entry"));
	const QString packedEntries = optionValue(args, QStringLiteral("--entries"));
	if (!packedEntries.trimmed().isEmpty()) {
		entries += packedEntries.split(';', Qt::SkipEmptyParts);
	}

	QStringList normalized;
	for (const QString& entry : entries) {
		const QString trimmed = entry.trimmed();
		if (!trimmed.isEmpty() && !normalized.contains(trimmed)) {
			normalized.push_back(trimmed);
		}
	}
	return normalized;
}

PackageArchiveFormat packageWriteFormatFromArgs(const QStringList& args, const QString& outputPath)
{
	const QString formatId = optionValue(args, QStringLiteral("--format"));
	if (!formatId.trimmed().isEmpty()) {
		return packageArchiveFormatFromId(formatId);
	}
	return packageArchiveFormatFromFileName(outputPath);
}

QJsonObject packageStagingJson(const PackageStagingModel& staging)
{
	return QJsonDocument::fromJson(staging.manifestJson()).object();
}

QJsonObject packageWriteReportJson(const PackageWriteReport& report)
{
	QJsonObject object;
	object.insert(QStringLiteral("cancelled"), report.cancelled);
	object.insert(QStringLiteral("sourcePath"), report.sourcePath);
	object.insert(QStringLiteral("outputPath"), report.outputPath);
	object.insert(QStringLiteral("manifestPath"), report.manifestPath);
	object.insert(QStringLiteral("backupPath"), report.backupPath);
	object.insert(QStringLiteral("overwroteInPlace"), report.overwroteInPlace);
	object.insert(QStringLiteral("outputCommitted"), report.outputCommitted);
	object.insert(QStringLiteral("recoveryPaths"), stringArrayJson(report.recoveryPaths));
	object.insert(QStringLiteral("format"), packageArchiveFormatId(report.format));
	object.insert(QStringLiteral("entryCount"), report.entryCount);
	object.insert(QStringLiteral("bytesWritten"), static_cast<double>(report.bytesWritten));
	object.insert(QStringLiteral("sha256"), report.sha256);
	object.insert(QStringLiteral("deterministic"), report.deterministic);
	object.insert(QStringLiteral("dryRun"), report.dryRun);
	object.insert(QStringLiteral("wroteManifest"), report.wroteManifest);
	object.insert(QStringLiteral("warnings"), stringArrayJson(report.warnings));
	object.insert(QStringLiteral("blockedMessages"), stringArrayJson(report.blockedMessages));
	object.insert(QStringLiteral("succeeded"), report.succeeded());
	return object;
}

void printPackageStagingSummary(const PackageStagingModel& staging)
{
	const PackageStagingSummary summary = staging.summary();
	std::cout << "Package staging\n";
	std::cout << "Source: " << text(nativePath(summary.sourcePath)) << "\n";
	std::cout << "Format: " << text(packageArchiveFormatId(summary.sourceFormat)) << "\n";
	std::cout << "Base files: " << summary.baseFileCount << "\n";
	std::cout << "Staged files: " << summary.stagedFileCount << "\n";
	std::cout << "Operations: " << summary.operationCount << " (add " << summary.addedCount << ", replace " << summary.replacedCount << ", rename " << summary.renamedCount << ", delete " << summary.deletedCount << ")\n";
	const auto totalText = [&](quint64 bytes, bool overflow) {
		return !summary.totalsAvailable ? std::string("unavailable") : overflow ? std::string("exceeds the supported range") : text(QString::number(bytes));
	};
	std::cout << "Bytes before/after: " << totalText(summary.beforeBytes, summary.beforeSizeOverflow) << " -> " << totalText(summary.afterBytes, summary.afterSizeOverflow) << "\n";
	std::cout << "Save state: " << (summary.canSave ? "ready" : "blocked") << "\n";
	for (const PackageStageConflict& conflict : staging.conflicts()) {
		std::cout << "- " << (conflict.blocking ? "BLOCKED" : "notice") << " " << text(conflict.virtualPath) << ": " << text(conflict.message) << "\n";
	}
}

bool loadPackageStaging(const QString& packagePath, PackageStagingModel* staging, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!staging) {
		if (error) {
			*error = QStringLiteral("Missing staging model.");
		}
		return false;
	}
	if (packagePath.endsWith(QStringLiteral(".vibepackage"), Qt::CaseInsensitive)) { return PackageDraft::load(packagePath, staging, error); }
	PackageArchive archive;
	if (!loadPackageForCliQuiet(packagePath, &archive, error)) {
		return false;
	}
	return staging->loadBaseArchive(archive, error);
}

bool parsePackageIndex(const QString& value, int* index, QString* error)
{
	bool valid = false;
	const int parsed = value.toInt(&valid);
	if (!valid || parsed < 0 || value.trimmed() != value) {
		if (error) { *error = QStringLiteral("Package entry indexes and source ordinals require a nonnegative integer."); }
		return false;
	}
	*index = parsed;
	return true;
}

QSet<QString> packageStageValueOptions()
{
	return {QStringLiteral("--add-file"), QStringLiteral("--import-file"), QStringLiteral("--as"), QStringLiteral("--replace-file"),
		QStringLiteral("--replace-entry"), QStringLiteral("--entry"), QStringLiteral("--rename"), QStringLiteral("--to"), QStringLiteral("--delete"), QStringLiteral("--remove-entry"), QStringLiteral("--resolve"),
		QStringLiteral("--replace-ordinal"), QStringLiteral("--rename-ordinal"), QStringLiteral("--delete-ordinal"),
		QStringLiteral("--mkdir"), QStringLiteral("--rename-folder"), QStringLiteral("--folder-to"), QStringLiteral("--delete-folder"),
		QStringLiteral("--rename-group"), QStringLiteral("--group-to"), QStringLiteral("--delete-group"), QStringLiteral("--groups-fingerprint")};
}

bool applyPackageStageArgsImpl(PackageStagingModel* staging, const QStringList& args, QString* error, PackageFileImportMode mode, const PackageReadControl& control)
{
	if (error) { error->clear(); }
	if (!staging) { if (error) { *error = QStringLiteral("Missing staging model."); } return false; }
	const auto fail = [error](const QString& message) { if (error) { *error = message; } return false; };
	const auto values = packageStageValueOptions();
	for (qsizetype position = 1; position < args.size(); ++position) {
		const QString argument = args.at(position);
		const auto equals = argument.indexOf(QLatin1Char('='));
		const QString name = equals < 0 ? argument : argument.left(equals);
		if (!values.contains(name)) { continue; }
		if (equals < 0 && (position + 1 >= args.size() || args.at(position + 1).startsWith('-'))) {
			return fail(QStringLiteral("Missing value for %1; use %1=value for a value beginning with a dash.").arg(name));
		}
		const QString value = equals < 0 ? args.at(++position) : argument.mid(equals + 1);
		if (value.trimmed().isEmpty()) { return fail(QStringLiteral("Empty value for %1.").arg(name)); }
		if (name.endsWith(QStringLiteral("-ordinal"))) { int ordinal = -1; if (!parsePackageIndex(value, &ordinal, error)) { return false; } }
	}
	const QSet<QString> groupOptions{QStringLiteral("--rename-group"), QStringLiteral("--group-to"), QStringLiteral("--delete-group"), QStringLiteral("--groups-fingerprint")};
	bool groupEdit = false; for (const auto& name : groupOptions) { groupEdit |= hasOption(args, name); }
	if (groupEdit) {
		const auto tokens = commandTokens(args); const auto action = tokens.value(1);
		QSet<QString> flags{QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--quiet"), QStringLiteral("--verbose"), QStringLiteral("--dry-run")};
		QSet<QString> singleValues{QStringLiteral("--settings-file"), QStringLiteral("--locale"), QStringLiteral("--catalog-root")};
		int positions = 3;
		if (action == QStringLiteral("save-as")) {
			flags += QSet<QString>{QStringLiteral("--overwrite"), QStringLiteral("--in-place")};
			singleValues += QSet<QString>{QStringLiteral("--output"), QStringLiteral("--format"), QStringLiteral("--compression"), QStringLiteral("--backup"), QStringLiteral("--manifest"), QStringLiteral("--write-manifest")};
			positions = hasOption(args, QStringLiteral("--output")) ? 3 : 4;
		} else if (action == QStringLiteral("draft-save")) { positions = 4; flags.insert(QStringLiteral("--overwrite")); }
		else if (action == QStringLiteral("manifest")) { singleValues.insert(QStringLiteral("--output")); }
		else if (action == QStringLiteral("compare")) {
			flags += QSet<QString>{QStringLiteral("--staged"), QStringLiteral("--metadata-only"), QStringLiteral("--include-directories")}; singleValues.insert(QStringLiteral("--max-entry-bytes"));
		} else if (action != QStringLiteral("stage") && action != QStringLiteral("staging")) { return fail(QStringLiteral("Group edits require stage, draft-save, save-as, manifest or staged compare.")); }
		if (tokens.size() != positions) { return fail(QStringLiteral("Unexpected positional arguments for a package group edit.")); }
		QSet<QString> seen;
		for (qsizetype at = 1; at < args.size(); ++at) {
			const auto argument = args.at(at); if (!argument.startsWith('-')) { continue; }
			const auto equals = argument.indexOf('='); const auto name = equals < 0 ? argument : argument.left(equals);
			if (flags.contains(name)) { if (equals >= 0 || seen.contains(name)) { return fail(QStringLiteral("Invalid or repeated flag: %1").arg(name)); } }
			else if (singleValues.contains(name) || groupOptions.contains(name)) {
				if (singleValues.contains(name) && seen.contains(name)) { return fail(QStringLiteral("Repeated option: %1").arg(name)); }
				if (equals < 0 && (at + 1 >= args.size() || args.at(at + 1).startsWith('-'))) { return fail(QStringLiteral("Missing value: %1").arg(name)); }
				if ((equals < 0 ? args.at(++at) : argument.mid(equals + 1)).trimmed().isEmpty()) { return fail(QStringLiteral("Empty value: %1").arg(name)); }
			} else { return fail(QStringLiteral("Unsupported option for a WAD group edit: %1").arg(name)); }
			seen.insert(name);
		}
		for (const auto& name : values) { if (!groupOptions.contains(name) && hasOption(args, name)) { return fail(QStringLiteral("Group edits cannot be combined with individual entry, folder or conflict-policy options.")); } }
		const auto renames = optionValues(args, QStringLiteral("--rename-group")); const auto targets = optionValues(args, QStringLiteral("--group-to"));
		const auto deletes = optionValues(args, QStringLiteral("--delete-group")); const auto fingerprints = optionValues(args, QStringLiteral("--groups-fingerprint"));
		if (renames.size() != targets.size() || (renames.isEmpty() && deletes.isEmpty()) || fingerprints.size() != 1) {
			return fail(QStringLiteral("Pair each --rename-group with --group-to, or use --delete-group; supply one --groups-fingerprint from package groups."));
		}
		const auto inventory = inspectPackageWadGroups(*staging, control);
		if (!inventory.succeeded()) { return fail(inventory.error); }
		if (fingerprints.first() != inventory.fingerprint) { return fail(QStringLiteral("The package groups changed. Inspect package groups again.")); }
		QSet<qsizetype> selected;
		for (const auto& id : renames + deletes) {
			const auto group = std::find_if(inventory.groups.cbegin(), inventory.groups.cend(), [&](const auto& item) { return item.id == id; });
			if (group == inventory.groups.cend()) { return fail(QStringLiteral("Unknown WAD group: %1").arg(id)); }
			for (const auto& range : group->ranges) { for (qsizetype at = range.first; at <= range.last; ++at) {
				if (selected.contains(at)) { return fail(QStringLiteral("Choose non-overlapping WAD groups once per command.")); } selected.insert(at);
			} }
		}
		for (qsizetype at = 0; at < renames.size() + deletes.size(); ++at) {
			const bool remove = at >= renames.size();
			PackageWadGroupEditRequest request{remove ? deletes.at(at - renames.size()) : renames.at(at),
				inspectPackageWadGroups(*staging, control).fingerprint, remove ? QString() : targets.at(at), remove};
			if (!stagePackageWadGroupEdit(staging, request, nullptr, error, control)) { return false; }
		}
		return true;
	}
	const QString resolutionId = optionValue(args, QStringLiteral("--resolve"));
	if (!resolutionId.isEmpty() && !QStringList{QStringLiteral("block"), QStringLiteral("replace-existing"), QStringLiteral("skip")}.contains(resolutionId)) {
		return fail(QStringLiteral("--resolve accepts block, replace-existing, or skip."));
	}
	for (const auto& path : optionValues(args, QStringLiteral("--mkdir"))) { if (!staging->createDirectory(path, error)) { return false; } }
	const PackageStageConflictResolution resolution = packageStageConflictResolutionFromId(optionValue(args, QStringLiteral("--resolve")));
	const QStringList addFiles = optionValues(args, QStringLiteral("--add-file")) + optionValues(args, QStringLiteral("--import-file"));
	const QStringList addTargets = optionValues(args, QStringLiteral("--as"));
	if (addTargets.size() > addFiles.size()) { return fail(QStringLiteral("Each --as requires an imported file.")); }
	for (qsizetype index = 0; index < addFiles.size(); ++index) {
		const QString target = addTargets.value(index, QFileInfo(addFiles[index]).fileName());
		if (!staging->addFile(addFiles[index], target, error, resolution, control, mode)) { return false; }
	}

	const QStringList replaceFiles = optionValues(args, QStringLiteral("--replace-file"));
	const QStringList replaceOrdinals = optionValues(args, QStringLiteral("--replace-ordinal"));
	QStringList replaceTargets = optionValues(args, QStringLiteral("--replace-entry"));
	if (replaceTargets.isEmpty() && !replaceFiles.isEmpty()) { replaceTargets = optionValues(args, QStringLiteral("--entry")); }
	if ((!replaceOrdinals.isEmpty() && !replaceTargets.isEmpty())
		|| replaceFiles.size() != (replaceOrdinals.isEmpty() ? replaceTargets.size() : replaceOrdinals.size())) {
		return fail(QStringLiteral("Each --replace-file requires a matching --replace-entry or --replace-ordinal; use one selector kind per command."));
	}
	for (qsizetype index = 0; index < replaceFiles.size(); ++index) {
		int ordinal = -1;
		if (!replaceOrdinals.isEmpty() && !parsePackageIndex(replaceOrdinals.at(index), &ordinal, error)) { return false; }
		if (!(ordinal >= 0 ? staging->replaceOccurrence(ordinal, replaceFiles[index], error, control, mode)
			: staging->replaceFile(replaceTargets[index], replaceFiles[index], error, control, mode))) { return false; }
	}

	const QStringList renameSources = optionValues(args, QStringLiteral("--rename"));
	const QStringList renameOrdinals = optionValues(args, QStringLiteral("--rename-ordinal"));
	const QStringList renameTargets = optionValues(args, QStringLiteral("--to"));
	if ((!renameSources.isEmpty() && !renameOrdinals.isEmpty())
		|| (renameOrdinals.isEmpty() ? renameSources.size() : renameOrdinals.size()) != renameTargets.size()) {
		return fail(QStringLiteral("Each --rename or --rename-ordinal requires a matching --to target; use one selector kind per command."));
	}
	for (qsizetype index = 0; index < renameTargets.size(); ++index) {
		int ordinal = -1;
		if (!renameOrdinals.isEmpty() && !parsePackageIndex(renameOrdinals.at(index), &ordinal, error)) { return false; }
		if (!(ordinal >= 0 ? staging->renameOccurrence(ordinal, renameTargets[index], error, resolution)
			: staging->renameEntry(renameSources[index], renameTargets[index], error, resolution))) { return false; }
	}

	const QStringList folderSources = optionValues(args, QStringLiteral("--rename-folder"));
	const QStringList folderTargets = optionValues(args, QStringLiteral("--folder-to"));
	if (folderSources.size() != folderTargets.size()) { return fail(QStringLiteral("Each --rename-folder requires a matching --folder-to.")); }
	for (qsizetype index = 0; index < folderSources.size(); ++index) {
		if (!staging->renameDirectory(folderSources.at(index), folderTargets.at(index), error)) { return false; }
	}

	const QStringList deleteTargets = optionValues(args, QStringLiteral("--delete")) + optionValues(args, QStringLiteral("--remove-entry"));
	for (const QString& target : deleteTargets) {
		if (!staging->deleteEntry(target, error, resolution)) { return false; }
	}
	for (const QString& value : optionValues(args, QStringLiteral("--delete-ordinal"))) {
		int ordinal = -1;
		if (!parsePackageIndex(value, &ordinal, error) || !staging->deleteOccurrence(ordinal, error, resolution)) { return false; }
	}
	for (const auto& path : optionValues(args, QStringLiteral("--delete-folder"))) { if (!staging->deleteDirectory(path, error)) { return false; } }
	return true;
}

bool applyPackageStageArgs(PackageStagingModel* staging, const QStringList& args, QString* error, bool retainImports = true)
{
	if (!staging) { return false; }
	auto candidate = *staging;
	if (!candidate.beginOperationGroup(QCoreApplication::translate("VibeStudioPackageStaging", "Stage command"), error)) { return false; }
	const auto mode = retainImports && !hasOption(args, QStringLiteral("--dry-run")) ? PackageFileImportMode::Snapshot : PackageFileImportMode::VerifyOnly;
	PackageReadControl control;
	if (mode == PackageFileImportMode::Snapshot) {
		const StudioSettings settings(StudioSettings::AccessMode::ReadOnly); auto options = std::make_shared<PackageImportOptions>();
		options->maximumBytes = static_cast<qint64>(settings.packageImportMaximumMiB()) * 1024 * 1024;
		options->maximumFiles = settings.packageImportMaximumFiles(); control.importOptions = options;
	}
	if (!applyPackageStageArgsImpl(&candidate, args, error, mode, control)) { return false; }
	if (!candidate.endOperationGroup(true, error, control)) { return false; }
	*staging = std::move(candidate);
	return true;
}

void printInstallationValidation(const GameInstallationProfile& profile)
{
	const GameInstallationValidation validation = validateGameInstallationProfile(profile);
	std::cout << "Validation: " << (validation.isUsable() ? "usable" : "blocked") << "\n";
	if (!validation.errors.isEmpty()) {
		std::cout << "Errors\n";
		for (const QString& error : validation.errors) {
			std::cout << "- " << text(error) << "\n";
		}
	}
	if (!validation.warnings.isEmpty()) {
		std::cout << "Warnings\n";
		for (const QString& warning : validation.warnings) {
			std::cout << "- " << text(warning) << "\n";
		}
	}
}

void printGameInstallationProfile(const GameInstallationProfile& profile, const QString& selectedId)
{
	std::cout << "- " << text(profile.id) << "\n";
	std::cout << "  Name: " << text(profile.displayName) << "\n";
	std::cout << "  Game: " << text(profile.gameKey) << "\n";
	std::cout << "  Engine: " << text(gameEngineFamilyId(profile.engineFamily)) << " (" << text(gameEngineFamilyDisplayName(profile.engineFamily)) << ")\n";
	std::cout << "  Root: " << text(nativePath(profile.rootPath)) << "\n";
	std::cout << "  Executable: " << text(profile.executablePath.isEmpty() ? QStringLiteral("(not set)") : nativePath(profile.executablePath)) << "\n";
	std::cout << "  Base package paths: " << text(profile.basePackagePaths.isEmpty() ? QStringLiteral("(none)") : profile.basePackagePaths.join(QStringLiteral("; "))) << "\n";
	std::cout << "  Mod package paths: " << text(profile.modPackagePaths.isEmpty() ? QStringLiteral("(none)") : profile.modPackagePaths.join(QStringLiteral("; "))) << "\n";
	std::cout << "  Palette: " << text(profile.paletteId.isEmpty() ? QStringLiteral("(generic)") : profile.paletteId) << "\n";
	std::cout << "  Compiler profile: " << text(profile.compilerProfileId.isEmpty() ? QStringLiteral("(generic)") : profile.compilerProfileId) << "\n";
	std::cout << "  Read-only: " << (profile.readOnly ? "yes" : "no") << "\n";
	std::cout << "  Hidden: " << (profile.hidden ? "yes" : "no") << "\n";
	std::cout << "  Manual: " << (profile.manual ? "yes" : "no") << "\n";
	std::cout << "  Selected: " << (sameGameInstallationId(profile.id, selectedId) ? "yes" : "no") << "\n";
}

void printGameInstallations(const StudioSettings& settings)
{
	const QVector<GameInstallationProfile> profiles = settings.gameInstallations();
	if (profiles.isEmpty()) {
		std::cout << "Game installations: none\n";
		return;
	}

	std::cout << "Game installations\n";
	std::cout << "Selected: " << text(settings.selectedGameInstallationId().isEmpty() ? QStringLiteral("(none)") : settings.selectedGameInstallationId()) << "\n";
	for (const GameInstallationProfile& profile : profiles) {
		printGameInstallationProfile(profile, settings.selectedGameInstallationId());
		printInstallationValidation(profile);
	}
}

void printInstallationDetectionCandidates(const QVector<GameInstallationDetectionCandidate>& candidates)
{
	if (candidates.isEmpty()) {
		std::cout << "Detected game installations: none\n";
		std::cout << "No profiles were saved. Add an installation manually or pass additional --root paths.\n";
		return;
	}

	std::cout << "Detected game installations\n";
	std::cout << "Candidates: " << candidates.size() << "\n";
	for (const GameInstallationDetectionCandidate& candidate : candidates) {
		std::cout << "- " << text(candidate.profile.displayName) << "\n";
		std::cout << "  Source: " << text(candidate.sourceName) << " [" << text(candidate.sourceId) << "]\n";
		std::cout << "  Confidence: " << candidate.confidencePercent << "%\n";
		printGameInstallationProfile(candidate.profile, QString());
		if (!candidate.matchedPaths.isEmpty()) {
			std::cout << "  Matched paths: " << text(candidate.matchedPaths.join(QStringLiteral("; "))) << "\n";
		}
		if (!candidate.warnings.isEmpty()) {
			std::cout << "  Candidate warnings\n";
			for (const QString& warning : candidate.warnings) {
				std::cout << "  - " << text(warning) << "\n";
			}
		}
	}
	std::cout << "No profiles were saved. Re-run --add-installation with the chosen root to confirm.\n";
}

const GameInstallationProfile* findInstallationById(const QVector<GameInstallationProfile>& profiles, const QString& id)
{
	for (const GameInstallationProfile& profile : profiles) {
		if (sameGameInstallationId(profile.id, id)) {
			return &profile;
		}
	}
	return nullptr;
}

void printRecentProjects(const StudioSettings& settings)
{
	const QVector<RecentProject> projects = settings.recentProjects();
	if (projects.isEmpty()) {
		std::cout << "Recent projects: none\n";
		return;
	}

	std::cout << "Recent projects\n";
	for (const RecentProject& project : projects) {
		std::cout << "- " << text(project.displayName) << "\n";
		std::cout << "  Path: " << text(nativePath(project.path)) << "\n";
		std::cout << "  State: " << (project.exists ? "ready" : "missing") << "\n";
		std::cout << "  Last opened UTC: " << text(project.lastOpenedUtc.toUTC().toString(Qt::ISODate)) << "\n";
	}
}

void printPreferences(const AccessibilityPreferences& preferences, const QString& editorProfileId = defaultEditorProfileId())
{
	std::cout << "Accessibility and language preferences\n";
	std::cout << "Locale: " << text(preferences.localeName) << "\n";
	std::cout << "Theme: " << text(themeId(preferences.theme)) << " (" << text(themeDisplayName(preferences.theme)) << ")\n";
	std::cout << "Text scale: " << preferences.textScalePercent << "%\n";
	std::cout << "Density: " << text(densityId(preferences.density)) << " (" << text(densityDisplayName(preferences.density)) << ")\n";
	std::cout << "Editor profile: " << text(editorProfileDisplayNameForId(editorProfileId)) << " [" << text(editorProfileForId(editorProfileId) ? normalizedEditorProfileId(editorProfileId) : defaultEditorProfileId()) << "]\n";
	std::cout << "Reduced motion: " << (preferences.reducedMotion ? "enabled" : "disabled") << "\n";
	std::cout << "Text to speech: " << (preferences.textToSpeechEnabled ? "enabled" : "disabled") << "\n";
}

void printSetupSummary(const SetupSummary& summary)
{
	std::cout << "First-run setup\n";
	std::cout << "Status: " << text(summary.status) << "\n";
	std::cout << "Current step: " << text(summary.currentStepName) << " [" << text(summary.currentStepId) << "]\n";
	std::cout << "Description: " << text(summary.currentStepDescription) << "\n";
	std::cout << "Next action: " << text(summary.nextAction) << "\n";

	if (!summary.completedItems.isEmpty()) {
		std::cout << "Completed items\n";
		for (const QString& item : summary.completedItems) {
			std::cout << "- " << text(item) << "\n";
		}
	}
	if (!summary.pendingItems.isEmpty()) {
		std::cout << "Pending items\n";
		for (const QString& item : summary.pendingItems) {
			std::cout << "- " << text(item) << "\n";
		}
	}
	if (!summary.warnings.isEmpty()) {
		std::cout << "Warnings\n";
		for (const QString& item : summary.warnings) {
			std::cout << "- " << text(item) << "\n";
		}
	}
}

void printSettingsReport()
{
	const StudioSettings settings;
	std::cout << "VibeStudio settings\n";
	std::cout << "Storage: " << text(nativePath(settings.storageLocation())) << "\n";
	std::cout << "Status: " << text(settingsStatusText(settings.status())) << "\n";
	std::cout << "Schema: " << settings.schemaVersion() << "\n";
	std::cout << "Selected mode index: " << settings.selectedMode() << "\n";
	printRecentProjects(settings);
	printGameInstallations(settings);
	printPreferences(settings.accessibilityPreferences(), settings.selectedEditorProfileId());
	std::cout << "AI automation\n";
	std::cout << text(aiAutomationPreferencesText(settings.aiAutomationPreferences())) << "\n";
	printSetupSummary(settings.setupSummary());
}

void printExitCodes(CliOutputFormat format, const QString& commandName)
{
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("exitCodes"), exitCodesJson());
		printJson(object);
		return;
	}

	std::cout << "VibeStudio CLI exit codes\n";
	for (const CliExitCodeDescriptor& descriptor : cliExitCodeDescriptors()) {
		std::cout << "- " << exitCodeValue(descriptor.code) << " " << text(descriptor.id) << "\n";
		std::cout << "  Label: " << text(descriptor.label) << "\n";
		std::cout << "  " << text(descriptor.description) << "\n";
	}
}

int runCliCommandsCommand(const QString& commandName, CliOutputFormat format)
{
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("commands"), cliCommandsJson());
		printJson(object);
		return exitCodeValue(CliExitCode::Success);
	}

	std::cout << "VibeStudio CLI commands\n";
	for (const CliCommandDescriptor& descriptor : cliCommandDescriptors()) {
		std::cout << "- " << text(descriptor.family) << " " << text(descriptor.command) << "\n";
		std::cout << "  " << text(descriptor.summary) << "\n";
		if (!descriptor.examples.isEmpty()) {
			std::cout << "  Example: " << text(descriptor.examples.first()) << "\n";
		}
	}
	return exitCodeValue(CliExitCode::Success);
}

int runUiSemanticsCommand(const QString& commandName, CliOutputFormat format)
{
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("uiSemantics"), uiSemanticsJson());
		printJson(object);
		return exitCodeValue(CliExitCode::Success);
	}

	printUiSemantics();
	return exitCodeValue(CliExitCode::Success);
}

struct CreditsValidationCheck {
	QString id;
	QString path;
	QString requiredText;
	QString observedText;
	bool passed = false;
	QString message;
};

struct CreditTokenRequirement {
	QString id;
	QString requiredText;
};

QString fileText(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		return {};
	}
	return QString::fromUtf8(file.readAll());
}

bool containsToken(const QString& haystack, const QString& needle)
{
	return haystack.contains(needle, Qt::CaseInsensitive);
}

QString markdownSectionText(const QString& markdown, const QString& heading)
{
	const qsizetype headingIndex = markdown.indexOf(heading, 0, Qt::CaseInsensitive);
	if (headingIndex < 0) {
		return {};
	}
	const qsizetype searchStart = headingIndex + heading.size();
	const qsizetype nextHeadingIndex = markdown.indexOf(QRegularExpression(QStringLiteral("\\n##\\s+")), searchStart);
	if (nextHeadingIndex < 0) {
		return markdown.mid(headingIndex);
	}
	return markdown.mid(headingIndex, nextHeadingIndex - headingIndex);
}

QString normalizedRepositoryUrl(QString url)
{
	url = url.trimmed();
	while (url.endsWith(QLatin1Char('/'))) {
		url.chop(1);
	}
	if (url.endsWith(QStringLiteral(".git"), Qt::CaseInsensitive)) {
		url.chop(4);
	}
	return url.toLower();
}

QString gitmodulesUrlForSourcePath(const QString& gitmodules, const QString& sourcePath)
{
	bool matchingSection = false;
	const QStringList lines = gitmodules.split(QLatin1Char('\n'));
	for (const QString& line : lines) {
		const QString trimmed = line.trimmed();
		if (trimmed.startsWith(QStringLiteral("[submodule"))) {
			matchingSection = false;
			continue;
		}
		if (trimmed.startsWith(QStringLiteral("path"))) {
			const qsizetype equalsIndex = trimmed.indexOf(QLatin1Char('='));
			matchingSection = equalsIndex >= 0 && trimmed.mid(equalsIndex + 1).trimmed() == sourcePath;
			continue;
		}
		if (matchingSection && trimmed.startsWith(QStringLiteral("url"))) {
			const qsizetype equalsIndex = trimmed.indexOf(QLatin1Char('='));
			if (equalsIndex >= 0) {
				return trimmed.mid(equalsIndex + 1).trimmed();
			}
		}
	}
	return {};
}

QString gitHeadRevision(const QString& sourcePath)
{
	QProcess process;
	process.setProgram(QStringLiteral("git"));
	process.setArguments({QStringLiteral("-C"), sourcePath, QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
	process.setProcessChannelMode(QProcess::MergedChannels);
	process.start();
	if (!process.waitForStarted(3000)) {
		return {};
	}
	if (!process.waitForFinished(5000)) {
		process.kill();
		process.waitForFinished(1000);
		return {};
	}
	if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
		return {};
	}
	return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

QVector<CreditTokenRequirement> requiredCreditTokens()
{
	return {
		{QStringLiteral("pakfu"), QStringLiteral("PakFu")},
		{QStringLiteral("ericw-tools"), QStringLiteral("ericw-tools")},
		{QStringLiteral("netradiant-custom"), QStringLiteral("NetRadiant Custom")},
		{QStringLiteral("zdbsp"), QStringLiteral("ZDBSP")},
		{QStringLiteral("zokumbsp"), QStringLiteral("ZokumBSP")},
		{QStringLiteral("gtkradiant"), QStringLiteral("GtkRadiant")},
		{QStringLiteral("trenchbroom"), QStringLiteral("TrenchBroom")},
		{QStringLiteral("quark"), QStringLiteral("QuArK")},
		{QStringLiteral("openai"), QStringLiteral("OpenAI")},
		{QStringLiteral("claude"), QStringLiteral("Claude")},
		{QStringLiteral("gemini"), QStringLiteral("Gemini")},
		{QStringLiteral("elevenlabs"), QStringLiteral("ElevenLabs")},
		{QStringLiteral("meshy"), QStringLiteral("Meshy")},
	};
}

void appendCreditsTextCheck(QVector<CreditsValidationCheck>& checks, const QString& id, const QString& path, const QString& requiredText, const QString& haystack, const QString& message)
{
	checks.append({id, path, requiredText, {}, containsToken(haystack, requiredText), message});
}

QJsonObject creditsValidationCheckJson(const CreditsValidationCheck& check)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), check.id);
	object.insert(QStringLiteral("path"), check.path);
	object.insert(QStringLiteral("requiredText"), check.requiredText);
	if (!check.observedText.isEmpty()) {
		object.insert(QStringLiteral("observedText"), check.observedText);
	}
	object.insert(QStringLiteral("passed"), check.passed);
	object.insert(QStringLiteral("message"), check.message);
	return object;
}

int runCreditsValidateCommand(const QString& commandName, CliOutputFormat format)
{
	const QString readmePath = QDir::current().absoluteFilePath(QStringLiteral("README.md"));
	const QString creditsPath = QDir::current().absoluteFilePath(QStringLiteral("docs/CREDITS.md"));
	const QString gitmodulesPath = QDir::current().absoluteFilePath(QStringLiteral(".gitmodules"));
	const QString readme = fileText(readmePath);
	const QString credits = fileText(creditsPath);
	const QString readmeCredits = markdownSectionText(readme, QStringLiteral("## Credits"));
	const QString gitmodules = fileText(gitmodulesPath);
	QVector<CreditsValidationCheck> checks = {
		{QStringLiteral("readme-exists"), readmePath, QStringLiteral("README.md"), {}, !readme.isEmpty(), readme.isEmpty() ? QStringLiteral("README.md could not be read.") : QStringLiteral("README.md is readable.")},
		{QStringLiteral("credits-exists"), creditsPath, QStringLiteral("docs/CREDITS.md"), {}, !credits.isEmpty(), credits.isEmpty() ? QStringLiteral("docs/CREDITS.md could not be read.") : QStringLiteral("docs/CREDITS.md is readable.")},
		{QStringLiteral("gitmodules-exists"), gitmodulesPath, QStringLiteral(".gitmodules"), {}, !gitmodules.isEmpty(), gitmodules.isEmpty() ? QStringLiteral(".gitmodules could not be read.") : QStringLiteral(".gitmodules is readable.")},
		{QStringLiteral("readme-credits-section"), readmePath, QStringLiteral("## Credits"), {}, !readmeCredits.isEmpty(), QStringLiteral("README Credits section should stay visible and auditable.")},
	};
	for (const CreditTokenRequirement& token : requiredCreditTokens()) {
		appendCreditsTextCheck(checks, QStringLiteral("readme-credit-token-%1").arg(token.id), readmePath, token.requiredText, readmeCredits, QStringLiteral("README Credits section should include %1 attribution.").arg(token.requiredText));
		appendCreditsTextCheck(checks, QStringLiteral("docs-credit-token-%1").arg(token.id), creditsPath, token.requiredText, credits, QStringLiteral("docs/CREDITS.md should include %1 attribution.").arg(token.requiredText));
	}
	for (const CompilerIntegration& compiler : compilerIntegrations()) {
		appendCreditsTextCheck(checks, QStringLiteral("compiler-readme-revision-%1").arg(compiler.id), readmePath, compiler.pinnedRevision, readme, QStringLiteral("README should include the pinned %1 revision.").arg(compiler.displayName));
		appendCreditsTextCheck(checks, QStringLiteral("compiler-docs-revision-%1").arg(compiler.id), creditsPath, compiler.pinnedRevision, credits, QStringLiteral("docs/CREDITS.md should include the pinned %1 revision.").arg(compiler.displayName));
		appendCreditsTextCheck(checks, QStringLiteral("compiler-gitmodules-path-%1").arg(compiler.id), gitmodulesPath, compiler.sourcePath, gitmodules, QStringLiteral(".gitmodules should include the %1 submodule path.").arg(compiler.displayName));

		const QString normalizedGitmoduleUrl = normalizedRepositoryUrl(gitmodulesUrlForSourcePath(gitmodules, compiler.sourcePath));
		checks.append({
			QStringLiteral("compiler-gitmodules-url-%1").arg(compiler.id),
			gitmodulesPath,
			normalizedRepositoryUrl(compiler.upstreamUrl),
			normalizedGitmoduleUrl,
			normalizedGitmoduleUrl == normalizedRepositoryUrl(compiler.upstreamUrl),
			QStringLiteral(".gitmodules should point %1 at the structured upstream URL.").arg(compiler.displayName),
		});

		const QString absoluteSourcePath = QDir::current().absoluteFilePath(compiler.sourcePath);
		const QString headRevision = gitHeadRevision(absoluteSourcePath);
		checks.append({
			QStringLiteral("compiler-submodule-revision-%1").arg(compiler.id),
			absoluteSourcePath,
			compiler.pinnedRevision,
			headRevision,
			headRevision.compare(compiler.pinnedRevision, Qt::CaseInsensitive) == 0,
			headRevision.isEmpty()
				? QStringLiteral("Unable to read checked-out submodule revision for %1.").arg(compiler.displayName)
				: QStringLiteral("Checked-out %1 revision should match the structured manifest pin.").arg(compiler.displayName),
		});
	}
	bool passed = true;
	int failedCount = 0;
	for (const CreditsValidationCheck& check : checks) {
		passed = passed && check.passed;
		if (!check.passed) {
			++failedCount;
		}
	}
	const CliExitCode code = passed ? CliExitCode::Success : CliExitCode::ValidationFailed;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		QJsonArray array;
		for (const CreditsValidationCheck& check : checks) {
			array.append(creditsValidationCheckJson(check));
		}
		object.insert(QStringLiteral("checks"), array);
		object.insert(QStringLiteral("passed"), passed);
		object.insert(QStringLiteral("checkCount"), checks.size());
		object.insert(QStringLiteral("failedCount"), failedCount);
		printJson(object);
		return exitCodeValue(code);
	}

	std::cout << "Credits validation\n";
	for (const CreditsValidationCheck& check : checks) {
		std::cout << "- " << (check.passed ? "ok" : "missing") << " " << text(check.id) << "\n";
		std::cout << "  " << text(check.message) << "\n";
		if (!check.observedText.isEmpty()) {
			std::cout << "  Observed: " << text(check.observedText) << "\n";
		}
	}
	std::cout << "Passed: " << (passed ? "yes" : "no") << " (" << (checks.size() - failedCount) << "/" << checks.size() << ")\n";
	return exitCodeValue(code);
}

int runAboutCommand(const QString& commandName, CliOutputFormat format)
{
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("about"), aboutJson());
		printJson(object);
		return exitCodeValue(CliExitCode::Success);
	}

	std::cout << text(aboutSurfaceText()) << "\n";
	return exitCodeValue(CliExitCode::Success);
}

int runEditorLayoutCommand(const QStringList& args, CliOutputFormat format)
{
	const QString command = QStringLiteral("editor layout");
	const auto usage = [&](const QString& message) { return printCliError(command, CliExitCode::Usage, message, format); };
	const QSet<QString> globals{QStringLiteral("--settings-file"), QStringLiteral("--locale"), QStringLiteral("--catalog-root")};
	const QSet<QString> flags{QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--quiet"), QStringLiteral("--verbose")};
	QSet<QString> seen;
	QStringList positional;
	for (qsizetype i = 1; i < args.size(); ++i) {
		const QString arg = args.at(i);
		if (!arg.startsWith('-')) { positional << arg; continue; }
		const auto equals = arg.indexOf('=');
		const auto key = equals < 0 ? arg : arg.left(equals);
		if (seen.contains(key)) { return usage(QStringLiteral("Repeated option: %1").arg(key)); }
		seen.insert(key);
		if (flags.contains(key) && equals < 0) { continue; }
		if (!globals.contains(key)) { return usage(QStringLiteral("Unexpected option: %1").arg(arg)); }
		QString value;
		if (equals >= 0) { value = arg.mid(equals + 1); }
		else if (i + 1 < args.size() && !args.at(i + 1).startsWith('-')) { value = args.at(++i); }
		if (value.trimmed().isEmpty()) { return usage(QStringLiteral("Missing value for %1.").arg(key)); }
	}
	if (positional.size() < 2 || positional.size() > 3) { return usage(QStringLiteral("editor layout accepts at most one layout identifier.")); }
	StudioSettings settings;
	if (positional.size() == 3) {
		const QString id = positional.last();
		if (id != QStringLiteral("profile") && !levelViewLayoutForId(id, nullptr)) {
			return usage(QStringLiteral("Expected profile, single-2d, single-3d, camera-and-plan or four-views."));
		}
		if (!settings.setLevelViewLayoutPreference(id)) {
			return printCliError(command, CliExitCode::Failure, QStringLiteral("The settings store is read-only."), format);
		}
		settings.sync();
		if (settings.status() != QSettings::NoError) {
			return printCliError(command, CliExitCode::Failure, settingsStatusText(settings.status()), format);
		}
	}
	EditorProfileDescriptor profile;
	editorProfileForId(settings.selectedEditorProfileId(), &profile);
	const QString preference = settings.levelViewLayoutPreference();
	LevelViewLayout effective = profile.controls.layout;
	levelViewLayoutForId(preference, &effective);
	if (format == CliOutputFormat::Json) {
		auto object = cliResultJson(command);
		object.insert(QStringLiteral("preference"), preference);
		object.insert(QStringLiteral("effectiveLayout"), levelViewLayoutId(effective));
		object.insert(QStringLiteral("profileId"), profile.id);
		printJson(object);
	} else {
		std::cout << "Layout preference: " << text(preference) << "\nEffective layout: " << text(levelViewLayoutId(effective)) << "\n";
	}
	return exitCodeValue(CliExitCode::Success);
}

int runEditorViewLinksCommand(const QStringList& args, CliOutputFormat format)
{
	const QString command = QStringLiteral("editor view-links");
	const auto usage = [&](const QString& message) { return printCliError(command, CliExitCode::Usage, message, format); };
	const QSet<QString> globals {"--settings-file", "--locale", "--catalog-root"};
	const QSet<QString> flags {"--cli", "--json", "--quiet", "--verbose"};
	const QSet<QString> options {"--centers", "--zoom", "--follow-camera"};
	QSet<QString> seen;
	QHash<QString, bool> values;
	QStringList positional;
	for (qsizetype i = 1; i < args.size(); ++i) {
		const auto arg = args[i];
		if (!arg.startsWith('-')) { positional << arg; continue; }
		const auto equal = arg.indexOf('=');
		const auto key = equal < 0 ? arg : arg.left(equal);
		if (seen.contains(key)) { return usage(QStringLiteral("Repeated option: %1").arg(key)); }
		seen.insert(key);
		if (flags.contains(key) && equal < 0) { continue; }
		if (!options.contains(key) && !globals.contains(key)) { return usage(QStringLiteral("Unexpected option: %1").arg(arg)); }
		QString value;
		if (equal >= 0) { value = arg.mid(equal + 1); }
		else if (i + 1 < args.size() && !args[i + 1].startsWith('-')) { value = args[++i]; }
		if (value.trimmed().isEmpty()) { return usage(QStringLiteral("Missing value for %1.").arg(key)); }
		if (options.contains(key)) {
			if (value != QStringLiteral("on") && value != QStringLiteral("off")) { return usage(QStringLiteral("%1 expects on or off.").arg(key)); }
			values.insert(key, value == QStringLiteral("on"));
		}
	}
	if (positional.size() != 2) { return usage(QStringLiteral("editor view-links accepts only --centers, --zoom and --follow-camera on|off.")); }
	StudioSettings settings;
	auto links = settings.levelViewLinks();
	if (values.contains(QStringLiteral("--centers"))) { links.centers = values.value(QStringLiteral("--centers")); }
	if (values.contains(QStringLiteral("--zoom"))) { links.zoom = values.value(QStringLiteral("--zoom")); }
	if (values.contains(QStringLiteral("--follow-camera"))) { links.followCamera = values.value(QStringLiteral("--follow-camera")); }
	if (!values.isEmpty()) {
		if (!settings.setLevelViewLinks(links)) { return printCliError(command, CliExitCode::Failure, QStringLiteral("The navigation settings could not be written."), format); }
		settings.sync();
		if (settings.status() != QSettings::NoError) { return printCliError(command, CliExitCode::Failure, settingsStatusText(settings.status()), format); }
	}
	if (format == CliOutputFormat::Json) {
		auto output = cliResultJson(command);
		output.insert(QStringLiteral("links"), QJsonObject {{"centers", links.centers}, {"zoom", links.zoom}, {"followCamera", links.followCamera}});
		printJson(output);
	} else {
		std::cout << "Plan centres: " << (links.centers ? "linked" : "independent") << "\nPlan zoom: " << (links.zoom ? "linked" : "independent")
			<< "\nFollow camera: " << (links.followCamera ? "on" : "off") << "\n";
	}
	return exitCodeValue(CliExitCode::Success);
}

int runEditorProfilesCommand(const QString& commandName, CliOutputFormat format)
{
	const StudioSettings settings;
	const QString selectedId = settings.selectedEditorProfileId();
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("selectedEditorProfileId"), selectedId);
		object.insert(QStringLiteral("profiles"), editorProfilesJson(selectedId));
		printJson(object);
		return exitCodeValue(CliExitCode::Success);
	}

	std::cout << "Editor profiles\n";
	std::cout << "Selected: " << text(editorProfileDisplayNameForId(selectedId)) << " [" << text(selectedId) << "]\n";
	for (const EditorProfileDescriptor& profile : editorProfileDescriptors()) {
		std::cout << "- " << text(profile.displayName) << " [" << text(profile.id) << "]" << (profile.id == selectedId ? " selected" : "") << "\n";
		std::cout << "  Lineage: " << text(profile.lineage) << "\n";
		std::cout << "  Layout: " << text(profile.layoutPresetId) << "\n";
		std::cout << "  Camera: " << text(profile.cameraPresetId) << "\n";
		std::cout << "  Selection: " << text(profile.selectionPresetId) << "\n";
		std::cout << "  Grid: " << text(profile.gridPresetId) << "\n";
		std::cout << "  Terminology: " << text(profile.terminologyPresetId) << "\n";
		std::cout << "  Engines: " << text(profile.supportedEngineFamilies.join(", ")) << "\n";
		std::cout << "  Panels: " << text(profile.defaultPanels.join(", ")) << "\n";
		std::cout << "  Routed commands: " << profile.bindings.size() << "\n";
		std::cout << "  " << text(profile.description) << "\n";
	}
	return exitCodeValue(CliExitCode::Success);
}

int runEditorCurrentCommand(const QString& commandName, CliOutputFormat format)
{
	const StudioSettings settings;
	EditorProfileDescriptor profile;
	editorProfileForId(settings.selectedEditorProfileId(), &profile);
	QString gestureError;
	profile.controls = settings.effectiveLevelEditorControls(profile.id, &gestureError);
	if (!gestureError.isEmpty()) { return printCliError(commandName, CliExitCode::ValidationFailed, gestureError, format); }
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("selectedEditorProfileId"), profile.id);
		object.insert(QStringLiteral("profile"), editorProfileJson(profile, profile.id));
		object.insert(QStringLiteral("gestureOverrides"), settings.editorGestureOverrides(profile.id));
		printJson(object);
		return exitCodeValue(CliExitCode::Success);
	}

	std::cout << "Selected editor profile\n";
	std::cout << text(editorProfileSummaryText(profile)) << "\n";
	return exitCodeValue(CliExitCode::Success);
}

int runEditorControlsCommand(const QString& commandName, const QString& requestedId, CliOutputFormat format)
{
	const StudioSettings settings;
	const QString profileId = requestedId.trimmed().isEmpty() ? settings.selectedEditorProfileId() : requestedId;
	EditorProfileDescriptor profile;
	if (!editorProfileForId(profileId, &profile)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unknown editor profile '%1'. Expected one of: %2").arg(profileId, editorProfileIds().join(QStringLiteral(", "))), format);
	}
	QString gestureError;
	profile.controls = settings.effectiveLevelEditorControls(profile.id, &gestureError);
	if (!gestureError.isEmpty()) { return printCliError(commandName, CliExitCode::ValidationFailed, gestureError, format); }
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("profileId"), profile.id);
		object.insert(QStringLiteral("displayName"), profile.displayName);
		object.insert(QStringLiteral("placeholder"), profile.placeholder);
		object.insert(QStringLiteral("controls"), levelEditorControlsJson(profile.controls));
		object.insert(QStringLiteral("gestureOverrides"), settings.editorGestureOverrides(profile.id));
		printJson(object);
		return exitCodeValue(CliExitCode::Success);
	}

	std::cout << "Level editor controls: " << text(profile.displayName) << " [" << text(profile.id) << "]\n";
	if (profile.placeholder) {
		std::cout << "This profile changes keys only; the views keep the VibeStudio controls below.\n";
	}
	QString view;
	for (const LevelEditorControlRow& row : levelEditorControlRows(profile.controls)) {
		if (row.view != view) {
			view = row.view;
			std::cout << "\n" << text(view) << "\n";
		}
		std::cout << "  " << text(row.action) << ": " << text(row.gesture) << "\n";
	}
	return exitCodeValue(CliExitCode::Success);
}

int runEditorSelectCommand(const QString& commandName, const QString& profileId, CliOutputFormat format)
{
	EditorProfileDescriptor profile;
	if (!editorProfileForId(profileId, &profile)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unknown editor profile '%1'. Expected one of: %2").arg(profileId, editorProfileIds().join(QStringLiteral(", "))), format);
	}

	StudioSettings settings;
	settings.setSelectedEditorProfileId(profile.id);
	settings.sync();
	if (settings.status() != QSettings::NoError) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Failed to save editor profile: %1").arg(settingsStatusText(settings.status())), format);
	}

	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("selectedEditorProfileId"), settings.selectedEditorProfileId());
		object.insert(QStringLiteral("profile"), editorProfileJson(profile, settings.selectedEditorProfileId()));
		printJson(object);
	} else {
		std::cout << "Selected editor profile: " << text(profile.displayName) << " [" << text(profile.id) << "]\n";
	}
	return exitCodeValue(CliExitCode::Success);
}

// The keys a user gave commands in the Keyboard Shortcuts dialog live in the
// settings the studio shares with the CLI; --reset is the way back when a
// rebinding went wrong.
int runEditorKeysCommand(const QString& commandName, const QStringList& args, CliOutputFormat format)
{
	StudioSettings settings;
	QHash<QString, QStringList> shortcuts = settings.userShortcuts();
	const bool reset = hasOption(args, QStringLiteral("--reset"));
	const int cleared = reset ? static_cast<int>(shortcuts.size()) : 0;
	if (reset) {
		settings.setUserShortcuts({});
		settings.sync();
		if (settings.status() != QSettings::NoError) {
			return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Failed to save the keys: %1").arg(settingsStatusText(settings.status())), format);
		}
		shortcuts.clear();
	}
	QStringList commandIds = shortcuts.keys();
	commandIds.sort();
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		QJsonArray list;
		for (const QString& commandId : std::as_const(commandIds)) {
			QJsonObject entry;
			entry.insert(QStringLiteral("commandId"), commandId);
			entry.insert(QStringLiteral("keys"), QJsonArray::fromStringList(shortcuts.value(commandId)));
			entry.insert(QStringLiteral("unbound"), shortcuts.value(commandId).isEmpty());
			list.append(entry);
		}
		object.insert(QStringLiteral("userShortcuts"), list);
		object.insert(QStringLiteral("reset"), reset);
		object.insert(QStringLiteral("cleared"), cleared);
		printJson(object);
		return exitCodeValue(CliExitCode::Success);
	}
	if (reset) {
		std::cout << "Reset " << cleared << " command(s) to their default keys.\n";
	}
	std::cout << "User keys: " << commandIds.size() << "\n";
	for (const QString& commandId : std::as_const(commandIds)) {
		const QStringList keys = shortcuts.value(commandId);
		// "; " between keys, as Qt lists them, so a chord's ", " stays whole.
		std::cout << "- " << text(commandId) << ": " << (keys.isEmpty() ? std::string("(no keys)") : text(keys.join(QStringLiteral("; ")))) << "\n";
	}
	return exitCodeValue(CliExitCode::Success);
}

int runLocalizationReportCommand(const QString& commandName, const QStringList& args, CliOutputFormat format, bool targetsOnly)
{
	const QString localeName = hasOption(args, QStringLiteral("--locale")) ? optionValue(args, QStringLiteral("--locale")) : QStringLiteral("en");
	// Only pass an explicit root when the user gave one: an empty value lets
	// resolveTranslationCatalogRoot() try VIBESTUDIO_I18N_DIR, the application
	// directory, and the installed share directory before the relative fallback.
	const QString catalogRoot = optionValue(args, QStringLiteral("--catalog-root"));
	const LocalizationSmokeReport report = buildLocalizationSmokeReport(localeName, catalogRoot);
	const CliExitCode code = report.ok ? CliExitCode::Success : CliExitCode::ValidationFailed;

	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		if (targetsOnly) {
			object.insert(QStringLiteral("targets"), localizationTargetsJson());
			object.insert(QStringLiteral("targetCount"), localizationTargets().size());
		} else {
			object.insert(QStringLiteral("localization"), localizationSmokeReportJson(report));
		}
		printJson(object);
		return exitCodeValue(code);
	}

	if (targetsOnly) {
		std::cout << "Localization targets\n";
		for (const LocalizationTarget& target : localizationTargets()) {
			std::cout << "- " << text(target.localeName) << ": " << text(target.englishName) << " / " << text(target.nativeName) << (target.rightToLeft ? " [RTL]" : "") << "\n";
		}
	} else {
		std::cout << text(localizationSmokeReportText(report)) << "\n";
	}
	return exitCodeValue(code);
}

int runDiagnosticsBundleCommand(const QString& commandName, const QStringList& args, CliOutputFormat format)
{
	const QJsonObject bundle = diagnosticBundleJson();
	const QString outputPath = optionValue(args, QStringLiteral("--output"));
	QString writtenPath;
	if (!outputPath.trimmed().isEmpty()) {
		QDir outputDir(outputPath);
		if (!outputDir.exists() && !QDir().mkpath(outputDir.path())) {
			return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to create diagnostic bundle folder: %1").arg(nativePath(outputPath)), format);
		}
		writtenPath = outputDir.filePath(QStringLiteral("vibestudio-diagnostics.json"));
		QFile file(writtenPath);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
			return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to write diagnostic bundle: %1").arg(file.errorString()), format);
		}
		file.write(QJsonDocument(bundle).toJson(QJsonDocument::Indented));
	}

	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("bundle"), bundle);
		object.insert(QStringLiteral("writtenPath"), writtenPath);
		printJson(object);
	} else {
		std::cout << "Diagnostic bundle\n";
		std::cout << "Version: " << text(versionString()) << "\n";
		std::cout << "Commands: " << cliCommandDescriptors().size() << "\n";
		std::cout << "Modules: " << plannedModules().size() << "\n";
		std::cout << "Redaction: " << text(bundle.value(QStringLiteral("redaction")).toString()) << "\n";
		if (!writtenPath.isEmpty()) {
			std::cout << "Written: " << text(nativePath(QFileInfo(writtenPath).absoluteFilePath())) << "\n";
		}
	}
	return exitCodeValue(CliExitCode::Success);
}

// Reads the reports the studio keeps beside its session logs. Nothing here
// installs crash capture: listing reports never writes a session marker.
int runDiagnosticsCrashesCommand(const QString& commandName, CliOutputFormat format)
{
	const QVector<CrashReportInfo> reports = recentCrashReports(20);
	const QString directory = crashReportDirectory();
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("directory"), directory);
		QJsonArray list;
		for (const CrashReportInfo& report : reports) {
			list.append(crashReportJson(report));
		}
		object.insert(QStringLiteral("reports"), list);
		printJson(object);
		return exitCodeValue(CliExitCode::Success);
	}
	std::cout << "Crash reports: " << reports.size() << "\n";
	std::cout << "Folder: " << text(directory.isEmpty() ? QStringLiteral("(none)") : nativePath(directory)) << "\n";
	for (const CrashReportInfo& report : reports) {
		const QString when = report.crashedAt.isValid() ? report.crashedAt.toUTC().toString(Qt::ISODate) : QStringLiteral("unknown time");
		const QString reason = report.reasonDetail.isEmpty() ? report.reasonId : report.reasonDetail;
		std::cout << "- " << text(when) << "  " << text(report.version) << "  " << text(reason) << "\n";
		std::cout << "  " << text(nativePath(report.path)) << "\n";
	}
	return exitCodeValue(CliExitCode::Success);
}

// `ai ask` and `ai test-connection`: the Assistant's question, from a shell.
// The same rules as the GUI: AI-free mode and cloud opt-in are the
// settings'; a question with project context leaves the machine only with
// --yes, since a script has nobody to show the request to.

// The settings' text connection, with --provider, --model, and --endpoint
// laid over it for this run only.
AiTextConnection aiTextConnectionFromArgs(const QStringList& args, AiAutomationPreferences* preferencesOut = nullptr)
{
	AiAutomationPreferences preferences = StudioSettings().aiAutomationPreferences();
	const QString provider = optionValue(args, QStringLiteral("--provider")).trimmed();
	QString connectorId = normalizedAiId(provider);
	if (connectorId.isEmpty()) {
		connectorId = resolveAiTextConnection(preferences).connectorId;
	}
	const QString model = optionValue(args, QStringLiteral("--model")).trimmed();
	const QString endpoint = optionValue(args, QStringLiteral("--endpoint")).trimmed();
	if (!connectorId.isEmpty() && !model.isEmpty()) {
		preferences.connectorModels.insert(connectorId, model);
	}
	if (!connectorId.isEmpty() && !endpoint.isEmpty()) {
		preferences.connectorEndpoints.insert(connectorId, endpoint);
	}
	if (preferencesOut) {
		*preferencesOut = preferences;
	}
	return resolveAiTextConnection(preferences, provider);
}

QJsonObject aiTextConnectionJson(const AiTextConnection& connection)
{
	QJsonObject object;
	object.insert(QStringLiteral("connectorId"), connection.connectorId);
	object.insert(QStringLiteral("connector"), connection.displayName);
	object.insert(QStringLiteral("model"), connection.model);
	object.insert(QStringLiteral("endpoint"), connection.endpoint);
	object.insert(QStringLiteral("local"), connection.local);
	object.insert(QStringLiteral("credentialVariable"), connection.credentialVariable);
	object.insert(QStringLiteral("credentialFound"), connection.credentialFound);
	object.insert(QStringLiteral("state"), aiTextConnectionBlockId(connection.block));
	object.insert(QStringLiteral("message"), aiTextConnectionBlockText(connection));
	return object;
}

QJsonObject aiImageConnectionJson(const AiImageConnection& connection)
{
	QJsonObject object;
	object.insert(QStringLiteral("connectorId"), connection.connectorId);
	object.insert(QStringLiteral("connector"), connection.displayName);
	object.insert(QStringLiteral("model"), connection.model);
	object.insert(QStringLiteral("endpoint"), connection.endpoint);
	object.insert(QStringLiteral("api"), aiImageApiId(connection.api));
	object.insert(QStringLiteral("local"), connection.local);
	object.insert(QStringLiteral("credentialVariable"), connection.credentialVariable);
	object.insert(QStringLiteral("credentialFound"), connection.credentialFound);
	object.insert(QStringLiteral("state"), aiImageConnectionBlockId(connection.block));
	object.insert(QStringLiteral("message"), aiImageConnectionBlockText(connection));
	return object;
}

QJsonObject aiSoundConnectionJson(const AiSoundConnection& connection)
{
	QJsonObject object;
	object.insert(QStringLiteral("connectorId"), connection.connectorId);
	object.insert(QStringLiteral("connector"), connection.displayName);
	object.insert(QStringLiteral("model"), connection.model);
	object.insert(QStringLiteral("endpoint"), connection.endpoint);
	object.insert(QStringLiteral("local"), connection.local);
	object.insert(QStringLiteral("credentialVariable"), connection.credentialVariable);
	object.insert(QStringLiteral("credentialFound"), connection.credentialFound);
	object.insert(QStringLiteral("state"), aiSoundConnectionBlockId(connection.block));
	object.insert(QStringLiteral("message"), aiSoundConnectionBlockText(connection));
	return object;
}

QJsonObject aiChatResponseJson(const AiChatResponse& response)
{
	QJsonObject object;
	object.insert(QStringLiteral("ok"), response.ok);
	object.insert(QStringLiteral("text"), response.text);
	object.insert(QStringLiteral("finishReason"), response.finishReason);
	object.insert(QStringLiteral("truncated"), response.truncated);
	object.insert(QStringLiteral("model"), response.model);
	object.insert(QStringLiteral("httpStatus"), response.httpStatus);
	object.insert(QStringLiteral("elapsedMs"), static_cast<double>(response.elapsedMsecs));
	QJsonObject usage;
	usage.insert(QStringLiteral("inputTokens"), response.inputTokens);
	usage.insert(QStringLiteral("outputTokens"), response.outputTokens);
	object.insert(QStringLiteral("usage"), usage);
	if (!response.ok) {
		QJsonObject failure;
		failure.insert(QStringLiteral("id"), aiChatFailureId(response.failure));
		failure.insert(QStringLiteral("summary"), aiChatFailureText(response.failure));
		failure.insert(QStringLiteral("message"), response.errorMessage);
		object.insert(QStringLiteral("failure"), failure);
	}
	return object;
}

// Sends and waits: the CLI has an event loop only while this runs.
AiChatResponse sendAiChatAndWait(const AiChatRequest& request, const QString& apiKey, int timeoutMsecs, QString* error)
{
	AiChatClient client;
	client.setTimeoutMsecs(timeoutMsecs);
	AiChatResponse result;
	bool done = false;
	QEventLoop loop;
	if (!client.send(request, apiKey, [&result, &done, &loop](const AiChatResponse& response) {
			result = response;
			done = true;
			loop.quit();
		}, error)) {
		result.failure = AiChatFailure::NotConfigured;
		result.errorMessage = error ? *error : QString();
		return result;
	}
	if (!done) {
		loop.exec();
	}
	return result;
}

int runAiAskCommand(const QString& commandName, const QStringList& args, CliOutputFormat format)
{
	const QString question = optionValue(args, QStringLiteral("--prompt")).trimmed();
	if (question.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --prompt <question>.").arg(commandName), format);
	}
	const AiTextConnection connection = aiTextConnectionFromArgs(args);
	const QString project = StudioSettings().currentProjectPath();
	QVector<AiContextItem> context;
	for (const QString& path : optionValues(args, QStringLiteral("--context-file"))) {
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly)) {
			return printCliError(commandName, CliExitCode::NotFound, QStringLiteral("Cannot read the context file %1.").arg(QDir::toNativeSeparators(path)), format);
		}
		const QString raw = QString::fromUtf8(file.readAll());
		context.push_back(makeAiContextItem(QStringLiteral("file"), QFileInfo(path).fileName(),
			redactAiText(redactAiContextPaths(raw, project, QDir::homePath()))));
	}
	bool tokensOk = true;
	const QString tokensText = optionValue(args, QStringLiteral("--max-tokens"));
	const int maxTokens = tokensText.isEmpty() ? 16000 : tokensText.toInt(&tokensOk);
	if (!tokensOk || maxTokens < 16) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("--max-tokens needs a number of at least 16."), format);
	}
	AiChatRequest request;
	request.connectorId = connection.connectorId;
	request.model = connection.model;
	request.endpoint = connection.endpoint;
	request.system = studioAssistantSystemPrompt();
	request.maxOutputTokens = maxTokens;
	const QString contextText = aiContextPromptText(context);
	request.messages = {{QStringLiteral("user"), contextText.isEmpty() ? question : contextText + QStringLiteral("\n\n## Question\n") + question}};

	// The request as it would go, without the key: what --dry-run prints and
	// what an off-machine send without --yes stops at.
	AiHttpRequest preview;
	QString buildError;
	AiChatRequest previewRequest = request;
	if (previewRequest.connectorId.isEmpty()) {
		previewRequest.connectorId = QStringLiteral("local-offline");
	}
	if (previewRequest.model.isEmpty()) {
		previewRequest.model = QStringLiteral("(model)");
	}
	const bool previewBuilt = buildAiHttpRequest(previewRequest, QString(), &preview, &buildError);
	const QString requestText = previewBuilt ? describeAiHttpRequest(preview) : buildError;
	const bool dryRun = hasOption(args, QStringLiteral("--dry-run"));
	if (dryRun) {
		if (format == CliOutputFormat::Json) {
			QJsonObject object = cliResultJson(commandName);
			object.insert(QStringLiteral("dryRun"), true);
			object.insert(QStringLiteral("connection"), aiTextConnectionJson(connection));
			object.insert(QStringLiteral("request"), requestText);
			if (previewBuilt) {
				object.insert(QStringLiteral("requestBody"), QJsonDocument::fromJson(preview.body).object());
			}
			printJson(object);
		} else {
			std::cout << text(aiTextConnectionBlockText(connection)) << "\n\n" << text(requestText) << "\n";
			if (!connection.credentialVariable.isEmpty()) {
				std::cout << "\nThe " << text(connection.credentialVariable) << " value goes in the authentication header when this is sent.\n";
			}
		}
		return exitCodeValue(CliExitCode::Success);
	}
	if (!connection.ready()) {
		return printCliError(commandName, CliExitCode::Unavailable, aiTextConnectionBlockText(connection), format);
	}
	if (!connection.local && !hasOption(args, QStringLiteral("--yes"))) {
		const QString message = QStringLiteral("This question would go to %1 at %2, off this machine. Review it with --dry-run, then add --yes to send it.")
									.arg(connection.displayName, QUrl(connection.endpoint).host());
		return printCliError(commandName, CliExitCode::Unavailable, message, format);
	}
	bool timeoutOk = true;
	const QString timeoutText = optionValue(args, QStringLiteral("--timeout-ms"));
	const int timeout = timeoutText.isEmpty() ? 5 * 60 * 1000 : timeoutText.toInt(&timeoutOk);
	if (!timeoutOk || timeout < 1000) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("--timeout-ms needs a number of at least 1000."), format);
	}
	QString sendError;
	const AiChatResponse response = sendAiChatAndWait(request, aiTextConnectionApiKey(connection), timeout, &sendError);
	const CliExitCode code = response.ok ? CliExitCode::Success : CliExitCode::Failure;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("connection"), aiTextConnectionJson(connection));
		QJsonArray sentContext;
		for (const AiContextItem& item : context) {
			sentContext.append(QJsonObject {{QStringLiteral("label"), item.label}, {QStringLiteral("omitted"), item.omittedNote}});
		}
		object.insert(QStringLiteral("context"), sentContext);
		object.insert(QStringLiteral("response"), aiChatResponseJson(response));
		printJson(object);
		return exitCodeValue(code);
	}
	if (!response.ok) {
		return printCliError(commandName, code, QStringLiteral("%1 %2").arg(aiChatFailureText(response.failure), response.errorMessage).trimmed(), format);
	}
	std::cout << text(response.text.trimmed()) << "\n";
	if (response.truncated) {
		std::cerr << "The answer stopped at the output limit; raise --max-tokens for more.\n";
	}
	return exitCodeValue(code);
}

int runAiTestConnectionCommand(const QString& commandName, const QStringList& args, CliOutputFormat format)
{
	const AiTextConnection connection = aiTextConnectionFromArgs(args);
	if (!connection.ready()) {
		return printCliError(commandName, CliExitCode::Unavailable, aiTextConnectionBlockText(connection), format);
	}
	// A fixed question with nothing from the project in it.
	AiChatRequest request;
	request.connectorId = connection.connectorId;
	request.model = connection.model;
	request.endpoint = connection.endpoint;
	request.messages = {{QStringLiteral("user"), QStringLiteral("Reply with the single word OK.")}};
	request.maxOutputTokens = 1024;
	QString error;
	const AiChatResponse response = sendAiChatAndWait(request, aiTextConnectionApiKey(connection), 60000, &error);
	const CliExitCode code = response.ok ? CliExitCode::Success : CliExitCode::Failure;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("connection"), aiTextConnectionJson(connection));
		object.insert(QStringLiteral("response"), aiChatResponseJson(response));
		printJson(object);
		return exitCodeValue(code);
	}
	const QString where = connection.local ? QStringLiteral("on this machine") : QStringLiteral("at %1").arg(QUrl(connection.endpoint).host());
	const QString seconds = QString::number(response.elapsedMsecs / 1000.0, 'f', 1);
	if (!response.ok) {
		return printCliError(commandName, code, QStringLiteral("%1 (%2 %3) failed after %4 s: %5 %6")
			.arg(connection.displayName, connection.model, where, seconds, aiChatFailureText(response.failure), response.errorMessage).trimmed(), format);
	}
	std::cout << text(QStringLiteral("%1 (%2) answered %3 in %4 s: %5").arg(connection.displayName, response.model.isEmpty() ? connection.model : response.model, where, seconds,
		response.text.trimmed().left(80))) << "\n";
	return exitCodeValue(code);
}

int runAiStatusCommand(const QString& commandName, CliOutputFormat format)
{
	const StudioSettings settings;
	const AiAutomationPreferences preferences = settings.aiAutomationPreferences();
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("preferences"), aiPreferencesJson(preferences));
		object.insert(QStringLiteral("capabilities"), aiCapabilitiesJson());
		object.insert(QStringLiteral("connectors"), aiConnectorsJson());
		object.insert(QStringLiteral("models"), aiModelsJson());
		object.insert(QStringLiteral("credentials"), aiCredentialStatusesJson(preferences));
		object.insert(QStringLiteral("tools"), aiToolsJson());
		object.insert(QStringLiteral("textConnection"), aiTextConnectionJson(resolveAiTextConnection(preferences)));
		object.insert(QStringLiteral("imageConnection"), aiImageConnectionJson(resolveAiImageConnection(preferences)));
		object.insert(QStringLiteral("soundConnection"), aiSoundConnectionJson(resolveAiSoundConnection(preferences)));
		printJson(object);
		return exitCodeValue(CliExitCode::Success);
	}

	std::cout << "AI automation\n";
	std::cout << text(aiAutomationPreferencesText(preferences)) << "\n";
	std::cout << "Assistant: " << text(aiTextConnectionBlockText(resolveAiTextConnection(preferences))) << "\n";
	std::cout << "Images: " << text(aiImageConnectionBlockText(resolveAiImageConnection(preferences))) << "\n";
	std::cout << "Sounds: " << text(aiSoundConnectionBlockText(resolveAiSoundConnection(preferences))) << "\n";
	std::cout << "Connector stubs\n";
	for (const AiConnectorDescriptor& connector : aiConnectorDescriptors()) {
		std::cout << "- " << text(connector.displayName) << " [" << text(connector.id) << "]\n";
		std::cout << "  Provider: " << text(connector.providerFamily) << "\n";
		std::cout << "  Endpoint: " << text(connector.endpointKind) << "\n";
		std::cout << "  Location: " << (connector.cloudBased ? "cloud" : "local/offline") << "\n";
		std::cout << "  Implemented: " << (connector.implemented ? "yes" : "design stub") << "\n";
		std::cout << "  Capabilities: " << text(connector.capabilities.join(", ")) << "\n";
	}
	std::cout << "Credential status\n";
	for (const AiCredentialStatus& status : aiCredentialStatuses(preferences)) {
		std::cout << "- " << text(status.connectorId) << ": " << (status.configured ? "configured" : "missing") << " " << text(status.redactedValue) << "\n";
	}
	std::cout << "Models\n";
	for (const AiModelDescriptor& model : aiModelDescriptors()) {
		std::cout << "- " << text(model.displayName) << " [" << text(model.id) << "]\n";
		std::cout << "  Connector: " << text(model.connectorId) << "\n";
		std::cout << "  Capabilities: " << text(model.capabilities.join(", ")) << "\n";
	}
	return exitCodeValue(CliExitCode::Success);
}

int runAiConnectorsCommand(const QString& commandName, CliOutputFormat format)
{
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("capabilities"), aiCapabilitiesJson());
		object.insert(QStringLiteral("connectors"), aiConnectorsJson());
		object.insert(QStringLiteral("models"), aiModelsJson());
		printJson(object);
		return exitCodeValue(CliExitCode::Success);
	}

	std::cout << "AI connector design stubs\n";
	for (const AiConnectorDescriptor& connector : aiConnectorDescriptors()) {
		std::cout << text(aiConnectorSummaryText(connector)) << "\n\n";
	}
	return exitCodeValue(CliExitCode::Success);
}

int runAiToolsCommand(const QString& commandName, CliOutputFormat format)
{
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("tools"), aiToolsJson());
		printJson(object);
		return exitCodeValue(CliExitCode::Success);
	}

	std::cout << "AI-callable VibeStudio tools\n";
	for (const AiToolDescriptor& tool : aiToolDescriptors()) {
		std::cout << "- " << text(tool.displayName) << " [" << text(tool.id) << "]\n";
		std::cout << "  " << text(tool.description) << "\n";
		std::cout << "  Capabilities: " << text(tool.capabilities.join(", ")) << "\n";
		std::cout << "  Approval: " << (tool.requiresApproval ? "required" : "not required") << "\n";
		std::cout << "  Writes: " << (tool.writesFiles ? "staged only" : "none") << "\n";
	}
	return exitCodeValue(CliExitCode::Success);
}

QString aiLogTextFromArgs(const QStringList& args)
{
	if (hasOption(args, QStringLiteral("--text"))) {
		return optionValue(args, QStringLiteral("--text"));
	}
	const QString logPath = optionValue(args, QStringLiteral("--log"));
	if (logPath.trimmed().isEmpty()) {
		return {};
	}
	return fileText(logPath);
}

int finishAiWorkflowCommand(const QString& commandName, const AiWorkflowResult& result, const QStringList& args, CliOutputFormat format)
{
	AiWorkflowResult output = result;
	const QString manifestPath = optionValue(args, QStringLiteral("--manifest"));
	if (!manifestPath.trimmed().isEmpty()) {
		QString error;
		if (!saveAiWorkflowManifest(output.manifest, manifestPath, &error)) {
			return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Failed to save AI workflow manifest: %1").arg(error), format);
		}
		output.manifest.stagedOutputs.push_back({QStringLiteral("manifest-file"), QStringLiteral("json"), QStringLiteral("Saved workflow manifest"), QFileInfo(manifestPath).absoluteFilePath(), QStringLiteral("AI workflow manifest written for review."), {QFileInfo(manifestPath).absoluteFilePath()}, true});
	}

	const CliExitCode code = output.manifest.state == OperationState::Failed ? CliExitCode::Failure : CliExitCode::Success;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("workflow"), aiWorkflowResultJson(output));
		object.insert(QStringLiteral("taskState"), taskStateJson(output.manifest.manifestId, output.manifest.state, output.summary, 0, output.manifest.cancellable));
		printJson(object);
		return exitCodeValue(code);
	}

	std::cout << text(output.title) << "\n";
	std::cout << text(output.summary) << "\n";
	if (!output.reviewableText.trimmed().isEmpty()) {
		std::cout << text(output.reviewableText) << "\n";
	}
	std::cout << text(aiWorkflowManifestText(output.manifest)) << "\n";
	if (!manifestPath.trimmed().isEmpty()) {
		std::cout << "Manifest saved: " << text(nativePath(QFileInfo(manifestPath).absoluteFilePath())) << "\n";
	}
	return exitCodeValue(code);
}

int runAiExplainLogCommand(const QString& commandName, const QStringList& args, CliOutputFormat format)
{
	const QString logText = aiLogTextFromArgs(args);
	if (logText.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --log <path> or --text <compiler-output>.").arg(commandName), format);
	}
	const StudioSettings settings;
	return finishAiWorkflowCommand(
		commandName,
		explainCompilerLogAiExperiment(logText, settings.aiAutomationPreferences(), optionValue(args, QStringLiteral("--provider")), optionValue(args, QStringLiteral("--model"))),
		args,
		format);
}

int runAiProposeCommandCommand(const QString& commandName, const QStringList& args, CliOutputFormat format)
{
	const QString prompt = optionValue(args, QStringLiteral("--prompt"));
	if (prompt.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --prompt <text>.").arg(commandName), format);
	}
	const StudioSettings settings;
	return finishAiWorkflowCommand(
		commandName,
		proposeCompilerCommandAiExperiment(prompt, optionValue(args, QStringLiteral("--workspace-root")), settings.aiAutomationPreferences(), optionValue(args, QStringLiteral("--provider")), optionValue(args, QStringLiteral("--model"))),
		args,
		format);
}

int runAiProposeManifestCommand(const QString& commandName, const QString& projectRoot, const QStringList& args, CliOutputFormat format)
{
	const QString root = projectRoot.trimmed().isEmpty() ? optionValue(args, QStringLiteral("--project-root")) : projectRoot;
	if (root.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a project root path.").arg(commandName), format);
	}
	const StudioSettings settings;
	return finishAiWorkflowCommand(
		commandName,
		draftProjectManifestAiExperiment(root, optionValue(args, QStringLiteral("--name")), settings.aiAutomationPreferences(), optionValue(args, QStringLiteral("--provider")), optionValue(args, QStringLiteral("--model"))),
		args,
		format);
}

int runAiPackageDepsCommand(const QString& commandName, const QString& packagePath, const QStringList& args, CliOutputFormat format)
{
	const QString path = packagePath.trimmed().isEmpty() ? optionValue(args, QStringLiteral("--package")) : packagePath;
	if (path.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a package path.").arg(commandName), format);
	}
	const StudioSettings settings;
	return finishAiWorkflowCommand(
		commandName,
		suggestPackageDependenciesAiExperiment(path, settings.aiAutomationPreferences(), optionValue(args, QStringLiteral("--provider")), optionValue(args, QStringLiteral("--model"))),
		args,
		format);
}

int runAiCliCommandCommand(const QString& commandName, const QStringList& args, CliOutputFormat format)
{
	const QString prompt = optionValue(args, QStringLiteral("--prompt"));
	if (prompt.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --prompt <text>.").arg(commandName), format);
	}
	const StudioSettings settings;
	return finishAiWorkflowCommand(
		commandName,
		generateCliCommandAiExperiment(prompt, settings.aiAutomationPreferences(), optionValue(args, QStringLiteral("--provider")), optionValue(args, QStringLiteral("--model"))),
		args,
		format);
}

int runAiFixPlanCommand(const QString& commandName, const QStringList& args, CliOutputFormat format)
{
	const QString logText = aiLogTextFromArgs(args);
	if (logText.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --log <path> or --text <compiler-output>.").arg(commandName), format);
	}
	const StudioSettings settings;
	return finishAiWorkflowCommand(
		commandName,
		fixAndRetryPlanAiExperiment(logText, optionValue(args, QStringLiteral("--command")), settings.aiAutomationPreferences(), optionValue(args, QStringLiteral("--provider")), optionValue(args, QStringLiteral("--model"))),
		args,
		format);
}

int runAiAssetRequestCommand(const QString& commandName, const QStringList& args, CliOutputFormat format)
{
	const QString prompt = optionValue(args, QStringLiteral("--prompt"));
	if (prompt.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --prompt <text>.").arg(commandName), format);
	}
	const StudioSettings settings;
	return finishAiWorkflowCommand(
		commandName,
		stageAssetGenerationRequestAiExperiment(optionValue(args, QStringLiteral("--provider")), optionValue(args, QStringLiteral("--kind")), prompt, settings.aiAutomationPreferences(), optionValue(args, QStringLiteral("--model"))),
		args,
		format);
}

int runAiCompareCommand(const QString& commandName, const QStringList& args, CliOutputFormat format)
{
	const QString prompt = optionValue(args, QStringLiteral("--prompt"));
	if (prompt.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --prompt <text>.").arg(commandName), format);
	}
	const StudioSettings settings;
	return finishAiWorkflowCommand(
		commandName,
		compareProviderOutputsAiExperiment(prompt, optionValue(args, QStringLiteral("--provider-a")), optionValue(args, QStringLiteral("--provider-b")), settings.aiAutomationPreferences(), optionValue(args, QStringLiteral("--model-a")), optionValue(args, QStringLiteral("--model-b"))),
		args,
		format);
}

int runAiShaderScaffoldCommand(const QString& commandName, const QStringList& args, CliOutputFormat format)
{
	const QString prompt = optionValue(args, QStringLiteral("--prompt"));
	if (prompt.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --prompt <text>.").arg(commandName), format);
	}
	const StudioSettings settings;
	return finishAiWorkflowCommand(
		commandName,
		promptToShaderScaffoldAiExperiment(prompt, settings.aiAutomationPreferences(), optionValue(args, QStringLiteral("--provider")), optionValue(args, QStringLiteral("--model"))),
		args,
		format);
}

int runAiEntitySnippetCommand(const QString& commandName, const QStringList& args, CliOutputFormat format)
{
	const QString prompt = optionValue(args, QStringLiteral("--prompt"));
	if (prompt.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --prompt <text>.").arg(commandName), format);
	}
	const StudioSettings settings;
	return finishAiWorkflowCommand(
		commandName,
		promptToEntityDefinitionAiExperiment(prompt, settings.aiAutomationPreferences(), optionValue(args, QStringLiteral("--provider")), optionValue(args, QStringLiteral("--model"))),
		args,
		format);
}

int runAiPackagePlanCommand(const QString& commandName, const QStringList& args, CliOutputFormat format)
{
	const QString prompt = optionValue(args, QStringLiteral("--prompt"));
	if (prompt.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --prompt <text>.").arg(commandName), format);
	}
	const StudioSettings settings;
	return finishAiWorkflowCommand(
		commandName,
		promptToPackageValidationPlanAiExperiment(prompt, optionValue(args, QStringLiteral("--package")), settings.aiAutomationPreferences(), optionValue(args, QStringLiteral("--provider")), optionValue(args, QStringLiteral("--model"))),
		args,
		format);
}

int runAiBatchRecipeCommand(const QString& commandName, const QStringList& args, CliOutputFormat format)
{
	const QString prompt = optionValue(args, QStringLiteral("--prompt"));
	if (prompt.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --prompt <text>.").arg(commandName), format);
	}
	const StudioSettings settings;
	return finishAiWorkflowCommand(
		commandName,
		promptToBatchConversionRecipeAiExperiment(prompt, settings.aiAutomationPreferences(), optionValue(args, QStringLiteral("--provider")), optionValue(args, QStringLiteral("--model"))),
		args,
		format);
}

AiWorkflowResult aiReviewWorkflowFromArgs(const QStringList& args, const AiAutomationPreferences& preferences)
{
	const QString prompt = optionValue(args, QStringLiteral("--prompt"));
	const QString kind = normalizedOptionId(optionValue(args, QStringLiteral("--kind")));
	if (kind == QStringLiteral("entity") || kind == QStringLiteral("entity-snippet") || kind == QStringLiteral("entity-definition")) {
		return promptToEntityDefinitionAiExperiment(prompt, preferences, optionValue(args, QStringLiteral("--provider")), optionValue(args, QStringLiteral("--model")));
	}
	if (kind == QStringLiteral("package") || kind == QStringLiteral("package-plan") || kind == QStringLiteral("package-validation")) {
		return promptToPackageValidationPlanAiExperiment(prompt, optionValue(args, QStringLiteral("--package")), preferences, optionValue(args, QStringLiteral("--provider")), optionValue(args, QStringLiteral("--model")));
	}
	if (kind == QStringLiteral("batch") || kind == QStringLiteral("batch-recipe") || kind == QStringLiteral("conversion")) {
		return promptToBatchConversionRecipeAiExperiment(prompt, preferences, optionValue(args, QStringLiteral("--provider")), optionValue(args, QStringLiteral("--model")));
	}
	if (kind == QStringLiteral("cli") || kind == QStringLiteral("command")) {
		return generateCliCommandAiExperiment(prompt, preferences, optionValue(args, QStringLiteral("--provider")), optionValue(args, QStringLiteral("--model")));
	}
	return promptToShaderScaffoldAiExperiment(prompt, preferences, optionValue(args, QStringLiteral("--provider")), optionValue(args, QStringLiteral("--model")));
}

int runAiReviewCommand(const QString& commandName, const QStringList& args, CliOutputFormat format)
{
	const QString prompt = optionValue(args, QStringLiteral("--prompt"));
	if (prompt.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --prompt <text>.").arg(commandName), format);
	}
	const StudioSettings settings;
	AiWorkflowResult result = aiReviewWorkflowFromArgs(args, settings.aiAutomationPreferences());
	result.title = QStringLiteral("%1 %2").arg(result.title, QStringLiteral("Review"));
	result.reviewableText = aiProposalReviewSurfaceText(result);
	result.summary = QStringLiteral("%1 %2").arg(result.summary, QStringLiteral("Review surface rendered."));
	return finishAiWorkflowCommand(commandName, result, args, format);
}

int runProjectInitCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format)
{
	if (path.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a project folder path.").arg(commandName), format);
	}

	const QString selectedInstallationId = hasOption(args, QStringLiteral("--installation")) ? optionValue(args, QStringLiteral("--installation")) : optionValue(args, QStringLiteral("--project-installation"));
	const QString editorProfileId = hasOption(args, QStringLiteral("--editor-profile")) ? optionValue(args, QStringLiteral("--editor-profile")) : optionValue(args, QStringLiteral("--project-editor-profile"));
	const QString paletteId = hasOption(args, QStringLiteral("--palette")) ? optionValue(args, QStringLiteral("--palette")) : optionValue(args, QStringLiteral("--project-palette"));
	const QString compilerProfileId = hasOption(args, QStringLiteral("--compiler-profile")) ? optionValue(args, QStringLiteral("--compiler-profile")) : optionValue(args, QStringLiteral("--project-compiler-profile"));
	const QString projectCompilerSearchPaths = optionValue(args, QStringLiteral("--project-compiler-search-paths"));
	const QString projectCompilerToolId = optionValue(args, QStringLiteral("--project-compiler-tool"));
	const QString projectCompilerExecutable = optionValue(args, QStringLiteral("--project-compiler-executable"));
	const bool aiFreeOverrideRequested = hasOption(args, QStringLiteral("--ai-free")) || hasOption(args, QStringLiteral("--project-ai-free"));
	bool aiFreeMode = false;
	if (aiFreeOverrideRequested) {
		const QString value = hasOption(args, QStringLiteral("--ai-free")) ? optionValue(args, QStringLiteral("--ai-free")) : optionValue(args, QStringLiteral("--project-ai-free"));
		if (!boolOptionValue(value, &aiFreeMode)) {
			return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Project AI-free override requires on or off."), format);
		}
	}
	if (!editorProfileId.trimmed().isEmpty()) {
		EditorProfileDescriptor profile;
		if (!editorProfileForId(editorProfileId, &profile)) {
			return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unknown editor profile '%1'. Expected one of: %2").arg(editorProfileId, editorProfileIds().join(QStringLiteral(", "))), format);
		}
	}

	ProjectManifest manifest;
	QString error;
	if (!loadProjectManifest(path, &manifest, &error)) {
		manifest = defaultProjectManifest(path);
	}
	if (!selectedInstallationId.trimmed().isEmpty()) {
		manifest.selectedInstallationId = selectedInstallationId.trimmed();
		manifest.settingsOverrides.selectedInstallationId = selectedInstallationId.trimmed();
	}
	if (!editorProfileId.trimmed().isEmpty()) {
		manifest.settingsOverrides.editorProfileId = normalizedEditorProfileId(editorProfileId);
	}
	if (!paletteId.trimmed().isEmpty()) {
		manifest.settingsOverrides.paletteId = paletteId.trimmed();
	}
	if (!compilerProfileId.trimmed().isEmpty()) {
		manifest.settingsOverrides.compilerProfileId = compilerProfileId.trimmed();
	}
	if (!projectCompilerSearchPaths.trimmed().isEmpty()) {
		manifest.compilerSearchPaths = optionPathList(args, QStringLiteral("--project-compiler-search-paths"));
	}
	if (!projectCompilerToolId.trimmed().isEmpty() || !projectCompilerExecutable.trimmed().isEmpty()) {
		if (projectCompilerToolId.trimmed().isEmpty() || projectCompilerExecutable.trimmed().isEmpty()) {
			return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Project compiler executable override requires --project-compiler-tool and --project-compiler-executable."), format);
		}
		CompilerToolDescriptor descriptor;
		if (!compilerToolDescriptorForId(projectCompilerToolId, &descriptor)) {
			return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unknown compiler tool id: %1").arg(projectCompilerToolId), format);
		}
		const QString executablePath = QFileInfo(projectCompilerExecutable).isAbsolute() ? QFileInfo(projectCompilerExecutable).absoluteFilePath() : projectCompilerExecutable;
		bool replaced = false;
		for (CompilerToolPathOverride& override : manifest.compilerToolOverrides) {
			if (QString::compare(override.toolId, descriptor.id, Qt::CaseInsensitive) == 0) {
				override.executablePath = executablePath;
				replaced = true;
				break;
			}
		}
		if (!replaced) {
			manifest.compilerToolOverrides.push_back({descriptor.id, executablePath});
		}
	}
	if (aiFreeOverrideRequested) {
		manifest.settingsOverrides.aiFreeModeSet = true;
		manifest.settingsOverrides.aiFreeMode = aiFreeMode;
	}
	if (!saveProjectManifest(manifest, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Failed to save project manifest: %1").arg(error), format);
	}
	if (!loadProjectManifest(path, &manifest, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Failed to reload project manifest: %1").arg(error), format);
	}

	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("saved"), true);
		object.insert(QStringLiteral("manifest"), projectManifestJson(manifest));
		object.insert(QStringLiteral("health"), projectHealthJson(buildProjectHealthSummary(manifest)));
		printJson(object);
	} else {
		std::cout << "Project manifest saved\n";
		std::cout << "Path: " << text(nativePath(projectManifestPath(path))) << "\n";
		printProjectInfo(manifest);
	}
	return exitCodeValue(CliExitCode::Success);
}

int runProjectInfoCommand(const QString& commandName, const QString& path, CliOutputFormat format)
{
	if (path.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a project folder path.").arg(commandName), format);
	}

	ProjectManifest manifest;
	QString error;
	if (!loadProjectManifest(path, &manifest, &error)) {
		return printCliError(commandName, CliExitCode::NotFound, QStringLiteral("Unable to load project manifest: %1. Manifest path: %2").arg(error, nativePath(projectManifestPath(path))), format);
	}

	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("manifest"), projectManifestJson(manifest));
		object.insert(QStringLiteral("health"), projectHealthJson(buildProjectHealthSummary(manifest)));
		printJson(object);
	} else {
		printProjectInfo(manifest);
	}
	return exitCodeValue(CliExitCode::Success);
}

int runProjectValidateCommand(const QString& commandName, const QString& path, CliOutputFormat format)
{
	if (path.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a project folder path.").arg(commandName), format);
	}

	ProjectManifest manifest;
	QString error;
	if (!loadProjectManifest(path, &manifest, &error)) {
		return printCliError(commandName, CliExitCode::NotFound, QStringLiteral("Unable to load project manifest: %1. Manifest path: %2").arg(error, nativePath(projectManifestPath(path))), format);
	}

	const ProjectHealthSummary health = buildProjectHealthSummary(manifest);
	const CliExitCode code = health.failedCount > 0 ? CliExitCode::ValidationFailed : CliExitCode::Success;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("manifest"), projectManifestJson(manifest));
		object.insert(QStringLiteral("health"), projectHealthJson(health));
		printJson(object);
	} else {
		printProjectHealth(health);
	}
	return exitCodeValue(code);
}

int runPackageInfoCommand(const QString& commandName, const QString& path, CliOutputFormat format)
{
	if (path.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a folder, PAK, WAD, ZIP, or PK3 path.").arg(commandName), format);
	}

	PackageArchive archive;
	QString error;
	qint64 packageOpenMs = 0;
	if (!loadPackageForCliQuiet(path, &archive, &error, &packageOpenMs)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to open package: %1").arg(error), format);
	}

	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("package"), packageInfoJson(archive));
		object.insert(QStringLiteral("timing"), packageTimingJson(packageOpenMs));
		printJson(object);
	} else {
		printPackageInfo(archive);
		if (verboseOutput()) {
			std::cerr << "vibestudio: package opened in " << packageOpenMs << " ms\n";
		}
	}
	return exitCodeValue(CliExitCode::Success);
}

int runPackageListCommand(const QString& commandName, const QString& path, CliOutputFormat format, const QString& where = QString())
{
	if (path.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a folder, PAK, WAD, ZIP, or PK3 path.").arg(commandName), format);
	}

	PackageArchive archive;
	QString error;
	qint64 packageOpenMs = 0;
	if (!loadPackageForCliQuiet(path, &archive, &error, &packageOpenMs)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to open package: %1").arg(error), format);
	}

	// --where QUERY: only the entries the query matches, by path.
	if (!where.trimmed().isEmpty()) {
		const StudioQuery query = parseStudioQuery(where);
		QStringList matching;
		for (const PackageEntry& entry : archive.entries()) {
			const QString description = QStringLiteral("%1 %2 %3").arg(entry.virtualPath, entry.typeHint, entry.storageMethod);
			if (studioQueryMatches(query, packageEntryQueryProperties(entry), description)) {
				matching << entry.virtualPath;
			}
		}
		if (format == CliOutputFormat::Json) {
			QJsonObject object = cliResultJson(commandName);
			object.insert(QStringLiteral("query"), where.trimmed());
			object.insert(QStringLiteral("entries"), QJsonArray::fromStringList(matching));
			printJson(object);
		} else {
			std::cout << "Entries matching " << text(where.trimmed()) << ": " << matching.size() << "\n";
			for (const QString& entryPath : std::as_const(matching)) {
				std::cout << "- " << text(entryPath) << "\n";
			}
		}
		return exitCodeValue(CliExitCode::Success);
	}

	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("package"), packageListJson(archive));
		object.insert(QStringLiteral("timing"), packageTimingJson(packageOpenMs));
		printJson(object);
	} else {
		printPackageList(archive);
		if (verboseOutput()) {
			std::cerr << "vibestudio: package listed after open in " << packageOpenMs << " ms\n";
		}
	}
	return exitCodeValue(CliExitCode::Success);
}

int runPackagePreviewCommand(const QString& commandName, const QString& packagePath, const QString& entryPath, CliOutputFormat format, int entryIndex = -1)
{
	if (packagePath.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a folder, PAK, WAD, ZIP, or PK3 path.").arg(commandName), format);
	}
	if (entryPath.isEmpty() && entryIndex < 0) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a virtual package entry path.").arg(commandName), format);
	}

	PackageArchive archive;
	QString error;
	qint64 packageOpenMs = 0;
	if (!loadPackageForCliQuiet(packagePath, &archive, &error, &packageOpenMs)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to open package: %1").arg(error), format);
	}

	QElapsedTimer previewTimer;
	previewTimer.start();
	const PackagePreview preview = entryIndex >= 0 ? buildPackageEntryPreviewAt(archive, entryIndex) : buildPackageEntryPreview(archive, entryPath);
	const qint64 previewMs = previewTimer.elapsed();
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, preview.kind == PackagePreviewKind::Unavailable ? CliExitCode::NotFound : CliExitCode::Success);
		object.insert(QStringLiteral("packagePath"), packagePath);
		object.insert(QStringLiteral("preview"), packagePreviewJson(preview));
		if (entryIndex >= 0) { object.insert(QStringLiteral("entryIndex"), entryIndex); }
		object.insert(QStringLiteral("timing"), packageTimingJson(packageOpenMs, previewMs));
		printJson(object);
		return preview.kind == PackagePreviewKind::Unavailable ? exitCodeValue(CliExitCode::NotFound) : exitCodeValue(CliExitCode::Success);
	}

	const bool previewPrinted = printPackagePreview(archive, preview.virtualPath, &preview);
	if (verboseOutput()) {
		std::cerr << "vibestudio: package opened in " << packageOpenMs << " ms; preview built in " << previewMs << " ms\n";
	}
	return previewPrinted ? exitCodeValue(CliExitCode::Success) : exitCodeValue(CliExitCode::Failure);
}

int runPackageExtractCommand(const QString& commandName, const QString& packagePath, const QString& outputPath, const QStringList& entries, bool extractAll, bool dryRun, bool overwrite, const QStringList& args, CliOutputFormat format)
{
	const bool legacy = commandName == QStringLiteral("--extract");
	const qsizetype expectedTokens = legacy ? 0 : (hasOption(args, QStringLiteral("--output")) ? 3 : 4);
	const QSet<QString> flags {QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--quiet"), QStringLiteral("--verbose"),
		QStringLiteral("--dry-run"), QStringLiteral("--overwrite"), QStringLiteral("--extract-all")};
	QSet<QString> values {QStringLiteral("--output"), QStringLiteral("--extract-entry"), QStringLiteral("--entry"),
		QStringLiteral("--entries"), QStringLiteral("--entry-index"), QStringLiteral("--as"), QStringLiteral("--settings-file"), QStringLiteral("--locale"), QStringLiteral("--catalog-root")};
	if (legacy) { values.insert(QStringLiteral("--extract")); }
	QSet<QString> seen;
	QStringList positionals;
	bool selected = false;
	QStringList indexes;
	QStringList outputNames;
	// Like commandTokens, args includes QCoreApplication's executable at index 0.
	for (qsizetype index = 1; index < args.size(); ++index) {
		const QString argument = args[index];
		if (!argument.startsWith(QLatin1Char('-'))) { positionals << argument; continue; }
		const QString key = argument.section(QLatin1Char('='), 0, 0);
		const bool repeatedSelector = key == QStringLiteral("--entry") || key == QStringLiteral("--extract-entry")
			|| key == QStringLiteral("--entry-index") || key == QStringLiteral("--as");
		if ((!flags.contains(argument) && !values.contains(key)) || (seen.contains(key) && !repeatedSelector)) {
			return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unsupported or repeated extraction option: %1").arg(argument), format);
		}
		seen.insert(key);
		if (values.contains(key)) {
			const bool attachedValue = argument.contains(QLatin1Char('='));
			const QString value = attachedValue ? argument.mid(argument.indexOf(QLatin1Char('=')) + 1) : args.value(++index);
			if (value.trimmed().isEmpty() || (!attachedValue && value.startsWith(QLatin1Char('-')))) {
				return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a nonempty value.").arg(key), format);
			}
			selected = selected || (repeatedSelector && key != QStringLiteral("--as")) || key == QStringLiteral("--entries");
			if (key == QStringLiteral("--entry-index")) { indexes << value; }
			if (key == QStringLiteral("--as")) { outputNames << value; }
		}
	}
	if (positionals.size() != expectedTokens) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Extraction requires one package and one output directory."), format);
	}
	if ((selected && entries.isEmpty() && indexes.isEmpty()) || (extractAll && selected)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Choose nonempty entry selectors or --extract-all, not both."), format);
	}
	if (packagePath.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a folder, PAK, WAD, ZIP, or PK3 path.").arg(commandName), format);
	}
	if (outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --output <folder>.").arg(commandName), format);
	}

	if (!outputNames.isEmpty() && outputNames.size() != indexes.size()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Use one --as output path for every --entry-index, or omit all output overrides."), format);
	}
	QVector<PackageExtractionSelection> indexedSelection;
	QSet<int> selectedIndexes;
	for (qsizetype position = 0; position < indexes.size(); ++position) {
		int index = -1; QString indexError;
		if (!parsePackageIndex(indexes.at(position), &index, &indexError) || selectedIndexes.contains(index)) {
			return printCliError(commandName, CliExitCode::Usage, indexError.isEmpty() ? QStringLiteral("Do not repeat an extraction entry index.") : indexError, format);
		}
		selectedIndexes.insert(index);
		indexedSelection.append({index, outputNames.value(position)});
	}

	PackageArchive archive;
	QString error;
	if (!loadPackageForCliQuiet(packagePath, &archive, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to open package: %1").arg(error), format);
	}

	PackageExtractionRequest request;
	request.targetDirectory = outputPath;
	request.virtualPaths = entries;
	request.extractAll = extractAll || (entries.isEmpty() && indexes.isEmpty());
	request.entrySelections = indexedSelection;
	request.dryRun = dryRun;
	request.overwriteExisting = overwrite;
	const PackageExtractionReport report = extractPackageEntries(archive, request);
	const CliExitCode code = report.succeeded() ? CliExitCode::Success : CliExitCode::Failure;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("extraction"), packageExtractionReportJson(report));
		printJson(object);
	} else {
		printPackageExtractionReport(report);
	}
	return exitCodeValue(code);
}

int runPackagePublicationCommand(const QString& action, const QStringList& args, CliOutputFormat format)
{
	const QString command = QStringLiteral("package ") + action;
	const bool listing = action == QStringLiteral("interrupted-saves");
	const auto usage = [&](const QString& message) { return printCliError(command, CliExitCode::Usage, message, format); };
	const QSet<QString> globals{QStringLiteral("--settings-file"), QStringLiteral("--locale"), QStringLiteral("--catalog-root")};
	QMap<QString, QString> values; QSet<QString> flags; QStringList positionals, directories;
	for (qsizetype i = 1; i < args.size(); ++i) {
		const QString token = args.at(i);
		if (!token.startsWith('-')) { positionals.append(token); continue; }
		const qsizetype equals = token.indexOf('='); const QString key = equals < 0 ? token : token.left(equals);
		if (flags.contains(key) || values.contains(key)) { return usage(QStringLiteral("Repeated option: %1").arg(key)); }
		if (QStringList{QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--quiet"), QStringLiteral("--verbose")}.contains(key)
			|| (!listing && key == QStringLiteral("--finish"))) {
			if (equals >= 0) { return usage(QStringLiteral("This flag does not accept a value: %1").arg(key)); }
			flags.insert(key); continue;
		}
		if (!globals.contains(key) && !(listing && key == QStringLiteral("--directory"))
			&& !(!listing && (key == QStringLiteral("--backup") || key == QStringLiteral("--expected-sha256")))) {
			return usage(QStringLiteral("Unsupported interrupted-save option: %1").arg(key));
		}
		QString value;
		if (equals >= 0) { value = token.mid(equals + 1); }
		else {
			if (i + 1 >= args.size() || args.at(i + 1).startsWith('-')) { return usage(QStringLiteral("Missing value for %1.").arg(key)); }
			value = args.at(++i);
		}
		if (value.trimmed().isEmpty()) { return usage(QStringLiteral("Empty value for %1.").arg(key)); }
		if (key == QStringLiteral("--directory")) { directories << value; } else { values.insert(key, value); }
	}
	if (positionals.size() != (listing ? 2 : 3) || positionals.value(0) != QStringLiteral("package") || positionals.value(1) != action) {
		return usage(QStringLiteral("Use %1%2.").arg(command, listing ? QStringLiteral(" [--directory <folder>]") : QStringLiteral(" <journal> [--finish]")));
	}
	if (listing) {
		if (directories.size() > PackagePublicationDirectoryLimit) { return usage(QStringLiteral("Select at most %1 output folders.").arg(PackagePublicationDirectoryLimit)); }
		if (directories.isEmpty()) { directories << QDir::currentPath(); }
		const auto inventory = listPackagePublicationJournals(directories);
		const auto code = inventory.complete() ? CliExitCode::Success : CliExitCode::ValidationFailed;
		if (format == CliOutputFormat::Json) {
			auto object = cliResultJson(command, code); object.insert(QStringLiteral("inventory"), packagePublicationInventoryJson(inventory)); printJson(object);
		} else {
			std::cout << text(QStringLiteral("%1 interrupted save journals%2. Payloads have not been verified.\n")
				.arg(inventory.journals.size()).arg(inventory.complete() ? QString() : QStringLiteral(" (incomplete or needs review)")));
			for (const auto& journal : inventory.journals) {
				std::cout << text(journal.journalPath) << "\n  SHA-256: " << journal.journalSha256.toHex().constData() << "\n";
			}
			for (const auto& error : inventory.errors) { std::cout << text(error) << "\n"; }
		}
		return exitCodeValue(code);
	}
	if (values.contains(QStringLiteral("--backup")) && !flags.contains(QStringLiteral("--finish"))) { return usage(QStringLiteral("--backup requires --finish.")); }
	const QString expected = values.value(QStringLiteral("--expected-sha256"));
	if (!expected.isEmpty() && !QRegularExpression(QStringLiteral("^[0-9a-fA-F]{64}$")).match(expected).hasMatch()) {
		return usage(QStringLiteral("--expected-sha256 requires the 64 hexadecimal digits from the reviewed journal."));
	}
	const QString journalPath = positionals.at(2);
	if (!QFileInfo::exists(journalPath)) { return printCliError(command, CliExitCode::NotFound, QStringLiteral("Package recovery journal not found: %1").arg(journalPath), format); }
	const auto report = recoverPackagePublication(journalPath, flags.contains(QStringLiteral("--finish")), values.value(QStringLiteral("--backup")), {}, QByteArray::fromHex(expected.toLatin1()));
	const auto code = report.error.isEmpty() ? CliExitCode::Success : CliExitCode::ValidationFailed;
	if (format == CliOutputFormat::Json) {
		auto object = cliResultJson(command, code); object.insert(QStringLiteral("recovery"), packageRecoveryJson(report)); printJson(object);
	} else { std::cout << text(packageRecoveryText(report)) << "\n"; }
	return exitCodeValue(code);
}

int runPackageValidateCommand(const QString& commandName, const QString& packagePath, const QStringList& args, CliOutputFormat format)
{
	if (packagePath.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a folder, PAK, WAD, ZIP, or PK3 path.").arg(commandName), format);
	}
	if (!QFileInfo::exists(packagePath)) {
		return printCliError(commandName, CliExitCode::NotFound, QStringLiteral("Package path not found: %1").arg(nativePath(packagePath)), format);
	}

	PackageValidationRequest request;
	if (hasOption(args, QStringLiteral("--max-entry-bytes"))) {
		bool valid = false;
		request.maxEntryBytes = optionValue(args, QStringLiteral("--max-entry-bytes")).toULongLong(&valid);
		if (!valid || request.maxEntryBytes == 0) {
			return printCliError(commandName, CliExitCode::Usage, QStringLiteral("--max-entry-bytes requires a positive integer."), format);
		}
	}
	PackageArchive archive;
	QString error;
	if (!loadPackageForCliQuiet(packagePath, &archive, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to open package: %1").arg(error), format);
	}

	const auto report = validatePackage(archive, request);
	const CliExitCode code = report.valid() ? CliExitCode::Success : CliExitCode::ValidationFailed;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("package"), packageInfoJson(archive));
		object.insert(QStringLiteral("validation"), packageValidationJson(report));
		printJson(object);
	} else {
		std::cout << text(packageValidationText(report)) << "\n";
	}
	return exitCodeValue(code);
}

PackageReadControl packageDraftWriteControl()
{
	PackageReadControl control; control.draftLimits = std::make_shared<PackageDraftSaveLimits>(StudioSettings(StudioSettings::AccessMode::ReadOnly).packageDraftSaveLimits()); return control;
}

int runPackageDraftStorageCommand(const QString& action, const QStringList& args, CliOutputFormat format)
{
	const QString command = QStringLiteral("package ") + action; const bool reviewing = action == QStringLiteral("draft-storage");
	const auto usage = [&](const QString& error) { return printCliError(command, CliExitCode::Usage, error, format); };
	QMap<QString, QString> values; QSet<QString> flags; QStringList positionals;
	const QSet<QString> globals{QStringLiteral("--settings-file"), QStringLiteral("--locale"), QStringLiteral("--catalog-root")};
	for (qsizetype i = 1; i < args.size(); ++i) {
		const auto token = args.at(i); if (!token.startsWith('-')) { positionals << token; continue; }
		const auto equals = token.indexOf('='); const auto key = equals < 0 ? token : token.left(equals);
		if (values.contains(key) || flags.contains(key)) { return usage(QStringLiteral("Repeated option: %1").arg(key)); }
		const bool flag = QStringList{QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--quiet"), QStringLiteral("--verbose")}.contains(key)
			|| (!reviewing && (key == QStringLiteral("--write") || key == QStringLiteral("--dry-run")));
		if (flag) {
			if (equals >= 0) { return usage(QStringLiteral("This flag does not accept a value: %1").arg(key)); } flags.insert(key); continue;
		}
		if (!globals.contains(key) && !(key == QStringLiteral("--expected-storage-sha256") && !reviewing)) { return usage(QStringLiteral("Unsupported draft storage option: %1").arg(key)); }
		QString value;
		if (equals >= 0) { value = token.mid(equals + 1); }
		else {
			if (i + 1 >= args.size() || args.at(i + 1).startsWith('-')) { return usage(QStringLiteral("Missing value for %1.").arg(key)); }
			value = args.at(++i);
		}
		if (value.trimmed().isEmpty()) { return usage(QStringLiteral("Empty value for %1.").arg(key)); } values.insert(key, value);
	}
	if (positionals.size() != 3 || positionals.value(0) != QStringLiteral("package") || positionals.value(1) != action
		|| positionals.at(2).trimmed().isEmpty() || (flags.contains(QStringLiteral("--write")) && flags.contains(QStringLiteral("--dry-run")))) {
		return usage(QStringLiteral("Use %1 <draft.vibepackage>; --write and --dry-run are exclusive.").arg(command));
	}
	const QString path = positionals.at(2);
	if (reviewing) {
		const auto review = reviewPackageDraftStorage(path); const auto code = review.complete() ? CliExitCode::Success : CliExitCode::ValidationFailed;
		if (format == CliOutputFormat::Json) {
			auto result = cliResultJson(command, code); result.insert(QStringLiteral("storage"), packageDraftStorageReviewJson(review));
			const auto limits = StudioSettings(StudioSettings::AccessMode::ReadOnly).packageDraftSaveLimits();
			result.insert(QStringLiteral("limits"), QJsonObject{{QStringLiteral("maximumBytes"), limits.maximumBytes}, {QStringLiteral("maximumFiles"), limits.maximumFiles}}); printJson(result);
		} else {
			std::cout << text(QStringLiteral("Draft storage: %1 bytes in %2 files; %3 unused bytes in %4 files.\nStorage SHA-256: %5\n")
				.arg(review.storage.bytes).arg(review.storage.files.size()).arg(review.reclaimableBytes).arg(review.reclaimable.size()).arg(QString::fromLatin1(review.storage.fingerprint.toHex())));
			if (!review.error.isEmpty()) { std::cerr << text(review.error) << "\n"; }
		}
		return exitCodeValue(code);
	}
	const auto hash = values.value(QStringLiteral("--expected-storage-sha256")).toLower();
	if (hash.size() != 64 || QByteArray::fromHex(hash.toLatin1()).toHex() != hash.toLatin1()) { return usage(QStringLiteral("Supply --expected-storage-sha256 from package draft-storage.")); }
	const auto compacted = compactPackageDraftStorage(path, QByteArray::fromHex(hash.toLatin1()), !flags.contains(QStringLiteral("--write")));
	const auto code = compacted.succeeded ? CliExitCode::Success : CliExitCode::ValidationFailed;
	if (format == CliOutputFormat::Json) {
		auto result = cliResultJson(command, code); result.insert(QStringLiteral("path"), QFileInfo(path).absoluteFilePath());
		result.insert(QStringLiteral("dryRun"), compacted.dryRun); result.insert(QStringLiteral("readerExclusion"), compacted.readerExclusion);
		result.insert(QStringLiteral("reclaimedBytes"), compacted.reclaimedBytes); result.insert(QStringLiteral("reclaimedFiles"), compacted.reclaimedFiles);
		result.insert(QStringLiteral("error"), compacted.error); printJson(result);
	} else {
		std::cout << text(compacted.dryRun ? QStringLiteral("Reviewed potential reclamation: %1 bytes in %2 files. Reader exclusion is checked only with --write.\n").arg(compacted.reclaimedBytes).arg(compacted.reclaimedFiles)
			: QStringLiteral("Reclaimed %1 bytes in %2 files.\n").arg(compacted.reclaimedBytes).arg(compacted.reclaimedFiles));
		if (!compacted.error.isEmpty()) { std::cerr << text(compacted.error) << "\n"; }
	}
	return exitCodeValue(code);
}

int runPackageWorkingStoreCommand(const QString& action, const QStringList& args, CliOutputFormat format)
{
	const QString command = QStringLiteral("package ") + action;
	const bool listing = action == QStringLiteral("working-imports"), unlocking = action == QStringLiteral("working-unlock");
	const QString hashOption = unlocking ? QStringLiteral("--expected-lock-sha256") : QStringLiteral("--expected-storage-sha256");
	const auto usage = [&](const QString& message) { return printCliError(command, CliExitCode::Usage, message, format); };
	const QSet<QString> globals{QStringLiteral("--settings-file"), QStringLiteral("--locale"), QStringLiteral("--catalog-root")};
	QMap<QString, QString> values; QSet<QString> flags; QStringList positionals;
	for (qsizetype i = 1; i < args.size(); ++i) {
		const QString token = args.at(i);
		if (!token.startsWith('-')) { positionals << token; continue; }
		const auto equals = token.indexOf('='); const QString key = equals < 0 ? token : token.left(equals);
		if (flags.contains(key) || values.contains(key)) { return usage(QStringLiteral("Repeated option: %1").arg(key)); }
		const bool flag = QStringList{QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--quiet"), QStringLiteral("--verbose")}.contains(key)
			|| (!listing && (key == QStringLiteral("--write") || key == QStringLiteral("--dry-run")));
		if (flag) {
			if (equals >= 0) { return usage(QStringLiteral("This flag does not accept a value: %1").arg(key)); }
			flags.insert(key); continue;
		}
		if (!globals.contains(key) && key != QStringLiteral("--directory") && !(key == hashOption && !listing)) {
			return usage(QStringLiteral("Unsupported working import option: %1").arg(key));
		}
		QString value;
		if (equals >= 0) { value = token.mid(equals + 1); }
		else {
			if (i + 1 >= args.size() || args.at(i + 1).startsWith('-')) { return usage(QStringLiteral("Missing value for %1.").arg(key)); }
			value = args.at(++i);
		}
		if (value.trimmed().isEmpty()) { return usage(QStringLiteral("Empty value for %1.").arg(key)); } values.insert(key, value);
	}
	if (positionals.size() != (listing ? 2 : 3) || positionals.value(0) != QStringLiteral("package") || positionals.value(1) != action
		|| (flags.contains(QStringLiteral("--write")) && flags.contains(QStringLiteral("--dry-run")))) {
		return usage(QStringLiteral("Use %1%2; --write and --dry-run are exclusive.").arg(command, listing ? QString() : unlocking ? QStringLiteral(" <relative-lock-path>") : QStringLiteral(" <id>")));
	}
	const QString directory = values.value(QStringLiteral("--directory"), packageImportDirectory());
	if (listing) {
		const auto inventory = listPackageImports(directory); const auto code = inventory.complete() ? CliExitCode::Success : CliExitCode::ValidationFailed;
		if (format == CliOutputFormat::Json) {
			auto result = cliResultJson(command, code); result.insert(QStringLiteral("inventory"), packageImportInventoryJson(inventory));
			const StudioSettings settings(StudioSettings::AccessMode::ReadOnly);
			result.insert(QStringLiteral("limits"), QJsonObject{{QStringLiteral("maximumBytes"), static_cast<qint64>(settings.packageImportMaximumMiB()) * 1024 * 1024},
				{QStringLiteral("maximumFiles"), settings.packageImportMaximumFiles()}}); printJson(result);
		} else {
			for (const auto& info : inventory.sessions) {
				std::cout << text(QStringLiteral("%1: %2 payload bytes, %3 reserved bytes, %4 files%5\n  Storage SHA-256: %6\n")
					.arg(info.id).arg(info.bytes).arg(info.reservedBytes).arg(info.files)
					.arg(info.leasePresent ? QStringLiteral("; lease present") : QString(), QString::fromLatin1(info.fingerprint.toHex())));
				if (!info.error.isEmpty()) { std::cerr << text(info.error) << "\n"; }
				if (!info.storageError.isEmpty()) { std::cerr << text(info.storageError) << "\n"; }
			}
			for (const auto& lock : inventory.locks) {
				std::cout << text(QStringLiteral("Lock: %1\n  Lock SHA-256: %2\n").arg(lock.relativePath, QString::fromLatin1(lock.fingerprint.toHex())));
				if (!lock.error.isEmpty()) { std::cerr << text(lock.error) << "\n"; }
			}
			if (!inventory.error.isEmpty()) { std::cerr << text(inventory.error) << "\n"; }
		}
		return exitCodeValue(code);
	}
	const QString id = positionals.at(2), hash = values.value(hashOption).toLower();
	const QUuid uuid(id);
	if ((!unlocking && (uuid.isNull() || uuid.toString(QUuid::WithoutBraces) != id)) || hash.size() != 64 || QByteArray::fromHex(hash.toLatin1()).toHex() != hash.toLatin1()) {
		return usage(QStringLiteral("Supply a %1 and %2 from package working-imports.").arg(unlocking ? QStringLiteral("relative lock path") : QStringLiteral("working-session UUID"), hashOption));
	}
	const bool dryRun = !flags.contains(QStringLiteral("--write")); QString error;
	if (unlocking) {
		if (!releasePackageImportLock(directory, id, QByteArray::fromHex(hash.toLatin1()), dryRun, &error)) {
			return printCliError(command, CliExitCode::ValidationFailed, error, format);
		}
		if (format == CliOutputFormat::Json) {
			auto result = cliResultJson(command); result.insert(QStringLiteral("relativePath"), id); result.insert(QStringLiteral("lockSha256"), hash);
			result.insert(QStringLiteral("dryRun"), dryRun); result.insert(QStringLiteral("released"), !dryRun);
			result.insert(QStringLiteral("ownerExclusionChecked"), true); printJson(result);
		} else { std::cout << text(dryRun ? QStringLiteral("Lock review and owner exclusion verified; no lock was removed. A write checks both again.\n") : QStringLiteral("Reviewed working import lock released.\n")); }
		return 0;
	}
	if (!discardPackageImports(directory, id, QByteArray::fromHex(hash.toLatin1()), dryRun, &error)) { return printCliError(command, CliExitCode::ValidationFailed, error, format); }
	if (format == CliOutputFormat::Json) {
		auto result = cliResultJson(command); result.insert(QStringLiteral("id"), id); result.insert(QStringLiteral("storageSha256"), hash);
		result.insert(QStringLiteral("dryRun"), dryRun); result.insert(QStringLiteral("discarded"), !dryRun); result.insert(QStringLiteral("leaseChecked"), !dryRun); printJson(result);
	} else { std::cout << text(dryRun ? QStringLiteral("Storage review verified. Session liveness is checked only with --write.\n") : QStringLiteral("Working import session discarded.\n")); }
	return 0;
}

int runPackageRecoveryStoreCommand(const QString& action, const QStringList& args, CliOutputFormat format)
{
	const QString command = QStringLiteral("package ") + action;
	const bool listing = action == QStringLiteral("recoveries"), restoring = action == QStringLiteral("draft-recover");
	const auto usage = [&](const QString& message) { return printCliError(command, CliExitCode::Usage, message, format); };
	const QSet<QString> globals{QStringLiteral("--settings-file"), QStringLiteral("--locale"), QStringLiteral("--catalog-root")};
	QMap<QString, QString> values; QSet<QString> flags; QStringList positionals;
	for (qsizetype i = 1; i < args.size(); ++i) {
		const QString token = args.at(i);
		if (!token.startsWith('-')) { positionals.append(token); continue; }
		const qsizetype equals = token.indexOf('='); const QString key = equals < 0 ? token : token.left(equals);
		if (flags.contains(key) || values.contains(key)) { return usage(QStringLiteral("Repeated option: %1").arg(key)); }
		const bool flag = QStringList{QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--quiet"), QStringLiteral("--verbose")}.contains(key)
			|| (!listing && key == QStringLiteral("--dry-run")) || (!listing && !restoring && key == QStringLiteral("--write"));
		if (flag) {
			if (equals >= 0) { return usage(QStringLiteral("This flag does not accept a value: %1").arg(key)); }
			flags.insert(key); continue;
		}
		if (!globals.contains(key) && key != QStringLiteral("--directory") && !(!listing && key == QStringLiteral("--expected-sha256"))
			&& !(restoring && key == QStringLiteral("--output")) && !(!listing && !restoring && key == QStringLiteral("--expected-storage-sha256"))) { return usage(QStringLiteral("Unsupported recovery option: %1").arg(key)); }
		QString value;
		if (equals >= 0) { value = token.mid(equals + 1); }
		else {
			if (i + 1 >= args.size() || args.at(i + 1).startsWith('-')) { return usage(QStringLiteral("Missing value for %1.").arg(key)); }
			value = args.at(++i);
		}
		if (value.trimmed().isEmpty()) { return usage(QStringLiteral("Empty value for %1.").arg(key)); }
		values.insert(key, value);
	}
	if (positionals.size() != (listing ? 2 : 3) || positionals.value(0) != QStringLiteral("package") || positionals.value(1) != action
		|| (flags.contains(QStringLiteral("--dry-run")) && flags.contains(QStringLiteral("--write")))) {
		return usage(QStringLiteral("Use %1%2; --write and --dry-run are exclusive.").arg(command, listing ? QString() : QStringLiteral(" <id>")));
	}
	const QString directory = values.value(QStringLiteral("--directory"), packageRecoveryDirectory());
	if (listing) {
		const auto inventory = listPackageRecoveries(directory);
		bool valid = inventory.error.isEmpty() && !inventory.truncated && !inventory.cancelled && inventory.storageComplete;
		for (const auto& info : inventory.records) { valid = valid && info.readable(); }
		const auto code = valid ? CliExitCode::Success : CliExitCode::ValidationFailed;
		if (format == CliOutputFormat::Json) {
			auto result = cliResultJson(command, code); result.insert(QStringLiteral("directory"), QFileInfo(directory).absoluteFilePath());
			result.insert(QStringLiteral("inventory"), packageRecoveryInventoryJson(inventory));
			const StudioSettings settings(StudioSettings::AccessMode::ReadOnly);
			result.insert(QStringLiteral("limits"), QJsonObject{{QStringLiteral("maximumBytes"), static_cast<qint64>(settings.packageRecoveryMaximumMiB()) * 1024 * 1024},
				{QStringLiteral("maximumCopies"), settings.packageRecoveryMaximumCopies()}}); printJson(result);
		} else {
			for (const auto& info : inventory.records) {
				std::cout << text(QStringLiteral("%1  %2  %3\n  Manifest SHA-256: %4\n  Storage review SHA-256: %5\n  Stored bytes: %6\n").arg(info.id, info.title,
					info.readable() ? QStringLiteral("metadata checked") : info.error, QString::fromLatin1(info.manifestSha256.toHex()),
					QString::fromLatin1(info.storageSha256.toHex())).arg(info.storageBytes));
				if (!info.storageError.isEmpty()) { std::cerr << text(info.storageError) << "\n"; }
			}
			std::cout << text(QStringLiteral("%1 package recovery copies%2. Payloads are verified during restore.\n").arg(inventory.records.size()).arg(inventory.truncated ? QStringLiteral(" (scan limit reached)") : QString()));
			if (!inventory.error.isEmpty()) { std::cerr << text(inventory.error) << "\n"; }
		}
		return exitCodeValue(code);
	}
	const bool storageReview = values.contains(QStringLiteral("--expected-storage-sha256"));
	if (storageReview && values.contains(QStringLiteral("--expected-sha256"))) { return usage(QStringLiteral("Choose exactly one review checksum option.")); }
	const QString id = positionals.at(2), hash = values.value(storageReview ? QStringLiteral("--expected-storage-sha256") : QStringLiteral("--expected-sha256")).toLower();
	if (packageRecoveryPath(directory, id).isEmpty() || hash.size() != 64 || QByteArray::fromHex(hash.toLatin1()).toHex() != hash.toLatin1()
		|| (restoring && !values.contains(QStringLiteral("--output")))) {
		return usage(QStringLiteral("Supply a recovery UUID and --expected-sha256 from package recoveries. Discard also accepts --expected-storage-sha256. Restoring requires --output <new.vibepackage>."));
	}
	const bool dryRun = restoring ? flags.contains(QStringLiteral("--dry-run")) : !flags.contains(QStringLiteral("--write"));
	QString error;
	const bool succeeded = restoring ? restorePackageRecovery(directory, id, QByteArray::fromHex(hash.toLatin1()), values.value(QStringLiteral("--output")), nullptr, &error, packageDraftWriteControl(), dryRun)
		: storageReview ? discardPackageRecoveryStorage(directory, id, QByteArray::fromHex(hash.toLatin1()), dryRun, &error)
		: discardPackageRecovery(directory, id, QByteArray::fromHex(hash.toLatin1()), dryRun, &error);
	if (!succeeded) { return printCliError(command, CliExitCode::ValidationFailed, error, format); }
	if (format == CliOutputFormat::Json) {
		auto result = cliResultJson(command, CliExitCode::Success);
		result.insert(QStringLiteral("id"), id); result.insert(storageReview ? QStringLiteral("storageSha256") : QStringLiteral("manifestSha256"), hash); result.insert(QStringLiteral("dryRun"), dryRun);
		result.insert(restoring ? QStringLiteral("recovered") : QStringLiteral("discarded"), !dryRun);
		if (restoring) { result.insert(QStringLiteral("draftPath"), QFileInfo(values.value(QStringLiteral("--output"))).absoluteFilePath()); }
		printJson(result);
	} else { std::cout << text(QStringLiteral("%1 package recovery %2.\n").arg(dryRun ? QStringLiteral("Verified dry run for") : restoring ? QStringLiteral("Restored") : QStringLiteral("Discarded"), id)); }
	return 0;
}

int runPackageDraftCommand(const QString& command, const QStringList& tokens, const QStringList& args, CliOutputFormat format)
{
	const QString commandName = QStringLiteral("package ") + command;
	const bool saving = command == QStringLiteral("draft-save");
	const auto usage = [&](const QString& message) { return printCliError(commandName, CliExitCode::Usage, message, format); };
	if (tokens.size() != (saving ? 4 : 3)) { return usage(saving ? QStringLiteral("package draft-save requires a source and .vibepackage output directory.") : QStringLiteral("%1 requires one .vibepackage directory.").arg(commandName)); }
	const auto stageValues = packageStageValueOptions();
	const QSet<QString> globalValues{QStringLiteral("--settings-file"), QStringLiteral("--locale"), QStringLiteral("--catalog-root")};
	const QSet<QString> flags{QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--quiet"), QStringLiteral("--verbose")};
	QSet<QString> seen;
	for (qsizetype i = 1; i < args.size(); ++i) {
		const QString argument = args.at(i);
		if (!argument.startsWith('-')) { continue; }
		const auto equals = argument.indexOf('=');
		const QString name = equals < 0 ? argument : argument.left(equals);
		if (flags.contains(name) || (saving && (name == QStringLiteral("--overwrite") || name == QStringLiteral("--dry-run")))) {
			if (equals >= 0 || seen.contains(name)) { return usage(QStringLiteral("Invalid or repeated flag: %1").arg(name)); }
		} else if (globalValues.contains(name) || (saving && stageValues.contains(name))) {
			QString value;
			if (equals >= 0) { value = argument.mid(equals + 1); }
			else {
				if (i + 1 >= args.size() || args.at(i + 1).startsWith('-')) { return usage(QStringLiteral("Missing value for %1; use %1=value for a value beginning with a dash.").arg(name)); }
				value = args.at(++i);
			}
			if (value.trimmed().isEmpty() || ((globalValues.contains(name) || name == QStringLiteral("--resolve")) && seen.contains(name))) {
				return usage(QStringLiteral("Empty or repeated option: %1").arg(name));
			}
		} else { return usage(QStringLiteral("Unknown option for %1: %2").arg(commandName, name)); }
		seen.insert(name);
	}
	if (saving) {
		const auto adds = optionValues(args, QStringLiteral("--add-file")) + optionValues(args, QStringLiteral("--import-file"));
		if (optionValues(args, QStringLiteral("--as")).size() > adds.size()) { return usage(QStringLiteral("Each --as requires an imported file.")); }
		if (seen.contains(QStringLiteral("--entry")) && seen.contains(QStringLiteral("--replace-entry"))) { return usage(QStringLiteral("Use only --replace-entry for replacement targets.")); }
		if (seen.contains(QStringLiteral("--entry")) && !seen.contains(QStringLiteral("--replace-file"))) { return usage(QStringLiteral("--entry requires --replace-file.")); }
		const QString resolution = optionValue(args, QStringLiteral("--resolve"));
		if (!resolution.isEmpty() && !QStringList{QStringLiteral("block"), QStringLiteral("replace-existing"), QStringLiteral("skip")}.contains(resolution)) {
			return usage(QStringLiteral("--resolve accepts block, replace-existing, or skip."));
		}
	}
	PackageStagingModel plan;
	QString error;
	if (!(saving ? loadPackageStaging(tokens.at(2), &plan, &error) : PackageDraft::load(tokens.at(2), &plan, &error))) {
		return printCliError(commandName, CliExitCode::Failure, error, format);
	}
	if (saving && !applyPackageStageArgs(&plan, args, &error)) { return usage(error); }
	if ((command == QStringLiteral("draft-undo") && !plan.undo()) || (command == QStringLiteral("draft-redo") && !plan.redo())) {
		return printCliError(commandName, CliExitCode::ValidationFailed, QStringLiteral("No edit is available for %1.").arg(command), format);
	}
	const bool dryRun = saving && hasOption(args, QStringLiteral("--dry-run"));
	const bool writing = command != QStringLiteral("draft-info");
	if (writing && !PackageDraft::save(saving ? tokens.at(3) : tokens.at(2), &plan, saving ? hasOption(args, QStringLiteral("--overwrite")) : true, &error, packageDraftWriteControl(), dryRun)) {
		return printCliError(commandName, CliExitCode::Failure, error, format);
	}
	const auto summary = plan.summary();
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("draftPath"), plan.draftPath());
		object.insert(QStringLiteral("saved"), writing && !dryRun);
		object.insert(QStringLiteral("dryRun"), dryRun);
		object.insert(QStringLiteral("outputPath"), saving ? QFileInfo(tokens.at(3)).absoluteFilePath() : plan.draftPath());
		object.insert(QStringLiteral("revision"), QString::number(plan.revision()));
		object.insert(QStringLiteral("modified"), plan.isModified());
		object.insert(QStringLiteral("canUndo"), plan.canUndo()); object.insert(QStringLiteral("canRedo"), plan.canRedo());
		object.insert(QStringLiteral("undoLabel"), plan.undoLabel()); object.insert(QStringLiteral("redoLabel"), plan.redoLabel());
		object.insert(QStringLiteral("sourcePath"), summary.sourcePath); object.insert(QStringLiteral("sourceFormat"), packageArchiveFormatId(summary.sourceFormat));
		object.insert(QStringLiteral("operationCount"), summary.operationCount); object.insert(QStringLiteral("fileCount"), summary.stagedFileCount);
		object.insert(QStringLiteral("blockingCount"), summary.blockingCount); object.insert(QStringLiteral("canExport"), summary.canSave);
		printJson(object);
	} else {
		if (dryRun) { std::cout << "Dry run: draft contents and destination checked; no files written.\n"; }
		std::cout << "Package draft: " << text(nativePath(plan.draftPath())) << "\n";
		std::cout << "Undo: " << text(plan.undoLabel()) << "\nRedo: " << text(plan.redoLabel()) << "\n";
		printPackageStagingSummary(plan);
	}
	return exitCodeValue(CliExitCode::Success);
}


int runPackageCreateCommand(const QStringList& tokens, const QStringList& args, CliOutputFormat format)
{
	const QString commandName = QStringLiteral("package create");
	const auto usage = [&](const QString& message) { return printCliError(commandName, CliExitCode::Usage, message, format); };
	if (tokens.size() != 3 || tokens.at(2).trimmed().isEmpty()) { return usage(QStringLiteral("package create requires one archive or .vibepackage output path.")); }
	const auto stageValues = packageStageValueOptions();
	const QSet<QString> singleValues{QStringLiteral("--format"), QStringLiteral("--wad-magic"), QStringLiteral("--compression"),
		QStringLiteral("--settings-file"), QStringLiteral("--locale"), QStringLiteral("--catalog-root")};
	const QSet<QString> flags{QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--quiet"), QStringLiteral("--verbose"), QStringLiteral("--dry-run"), QStringLiteral("--overwrite")};
	QSet<QString> seen;
	for (qsizetype i = 1; i < args.size(); ++i) {
		const QString argument = args.at(i);
		if (!argument.startsWith('-')) { continue; }
		const auto equals = argument.indexOf('=');
		const QString name = equals < 0 ? argument : argument.left(equals);
		if (flags.contains(name)) {
			if (equals >= 0 || seen.contains(name)) { return usage(QStringLiteral("Invalid or repeated flag: %1").arg(name)); }
		} else if (singleValues.contains(name) || stageValues.contains(name)) {
			if (equals < 0 && (i + 1 >= args.size() || args.at(i + 1).startsWith('-'))) { return usage(QStringLiteral("Missing value for %1; use %1=value for a value beginning with a dash.").arg(name)); }
			const QString value = equals < 0 ? args.at(++i) : argument.mid(equals + 1);
			if (value.trimmed().isEmpty() || ((singleValues.contains(name) || name == QStringLiteral("--resolve")) && seen.contains(name))) { return usage(QStringLiteral("Empty or repeated option: %1").arg(name)); }
		} else { return usage(QStringLiteral("Unknown option for package create: %1").arg(name)); }
		seen.insert(name);
	}
	if (seen.contains(QStringLiteral("--entry")) && (!seen.contains(QStringLiteral("--replace-file")) || seen.contains(QStringLiteral("--replace-entry")))) {
		return usage(QStringLiteral("Use --entry only as the target of --replace-file, without --replace-entry."));
	}
	const QString output = tokens.at(2);
	const bool draft = output.endsWith(QStringLiteral(".vibepackage"), Qt::CaseInsensitive);
	const auto archiveFormat = draft && !seen.contains(QStringLiteral("--format")) ? PackageArchiveFormat::Pk3 : packageWriteFormatFromArgs(args, output);
	const QString magic = optionValue(args, QStringLiteral("--wad-magic"));
	PackageStagingModel plan; QString error;
	if (!plan.createEmpty(archiveFormat, magic, &error) || !applyPackageStageArgs(&plan, args, &error)) { return usage(error); }
	const bool dryRun = hasOption(args, QStringLiteral("--dry-run"));
	const bool overwrite = hasOption(args, QStringLiteral("--overwrite"));
	DeflateLevel compression = DeflateLevel::Default;
	if (seen.contains(QStringLiteral("--compression")) && (draft || !deflateLevelFromId(optionValue(args, QStringLiteral("--compression")), &compression))) {
		return usage(QStringLiteral("--compression accepts store, fast, default, or best for archive output."));
	}
	if (draft) {
		if (!PackageDraft::save(output, &plan, overwrite, &error, packageDraftWriteControl(), dryRun)) { return printCliError(commandName, CliExitCode::Failure, error, format); }
		if (format == CliOutputFormat::Json) {
			QJsonObject object = cliResultJson(commandName);
			object.insert(QStringLiteral("outputPath"), QFileInfo(output).absoluteFilePath());
			object.insert(QStringLiteral("draftPath"), plan.draftPath());
			object.insert(QStringLiteral("written"), !dryRun); object.insert(QStringLiteral("dryRun"), dryRun);
			object.insert(QStringLiteral("staging"), packageStagingJson(plan)); printJson(object);
		} else {
			std::cout << (dryRun ? "Draft checked: " : "Draft created: ") << text(nativePath(QFileInfo(output).absoluteFilePath())) << "\n";
			printPackageStagingSummary(plan);
		}
		return exitCodeValue(CliExitCode::Success);
	}
	PackageWriteRequest request; request.destinationPath = output; request.format = archiveFormat;
	request.allowOverwrite = overwrite; request.dryRun = dryRun; request.compression = compression;
	if (!request.dryRun) { StudioSettings().rememberPackagePublicationDirectory(QFileInfo(request.destinationPath).absolutePath()); }
	const auto report = plan.writeArchive(request);
	const auto code = report.succeeded() ? CliExitCode::Success : CliExitCode::ValidationFailed;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("write"), packageWriteReportJson(report));
		object.insert(QStringLiteral("staging"), packageStagingJson(plan)); printJson(object);
	} else { std::cout << text(packageWriteReportText(report)) << "\n"; }
	return exitCodeValue(code);
}

int runPackageGroupsCommand(const QStringList& tokens, const QStringList& args, CliOutputFormat format)
{
	const QString command = QStringLiteral("package groups");
	const auto usage = [&](const QString& error) { return printCliError(command, CliExitCode::Usage, error, format); };
	if (tokens.size() != 3 || tokens.at(2).trimmed().isEmpty()) { return usage(QStringLiteral("package groups requires one WAD or .vibepackage source.")); }
	const QSet<QString> flags{QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--quiet"), QStringLiteral("--verbose")};
	const QSet<QString> values{QStringLiteral("--settings-file"), QStringLiteral("--locale"), QStringLiteral("--catalog-root")}; QSet<QString> seen;
	for (qsizetype at = 1; at < args.size(); ++at) {
		const auto argument = args.at(at); if (!argument.startsWith('-')) { continue; }
		const auto equals = argument.indexOf('='); const auto name = equals < 0 ? argument : argument.left(equals);
		if (seen.contains(name)) { return usage(QStringLiteral("Repeated option: %1").arg(name)); } seen.insert(name);
		if (flags.contains(name)) { if (equals >= 0) { return usage(QStringLiteral("Unexpected flag value: %1").arg(name)); } }
		else if (values.contains(name)) {
			if (equals < 0 && (at + 1 >= args.size() || args.at(at + 1).startsWith('-'))) { return usage(QStringLiteral("Missing value: %1").arg(name)); }
			if ((equals < 0 ? args.at(++at) : argument.mid(equals + 1)).trimmed().isEmpty()) { return usage(QStringLiteral("Empty value: %1").arg(name)); }
		} else { return usage(QStringLiteral("Unsupported package groups option: %1").arg(name)); }
	}
	PackageStagingModel model; QString error;
	if (!loadPackageStaging(tokens.at(2), &model, &error)) { return printCliError(command, CliExitCode::Failure, error, format); }
	const auto inventory = inspectPackageWadGroups(model);
	if (!inventory.succeeded()) { return printCliError(command, CliExitCode::ValidationFailed, inventory.error, format); }
	if (format == CliOutputFormat::Json) { auto result = cliResultJson(command); result.insert(QStringLiteral("inventory"), packageWadGroupsJson(inventory)); printJson(result); }
	else {
		std::cout << "Review fingerprint: " << text(inventory.fingerprint) << '\n';
		for (const auto& group : inventory.groups) { std::cout << text(group.id) << "  " << text(group.name) << "  " << group.memberCount << " entries" << (group.error.isEmpty() ? "" : "  BLOCKED: ") << text(group.error) << '\n'; }
	}
	return 0;
}

int runPackageStageCommand(const QString& commandName, const QString& packagePath, const QStringList& args, CliOutputFormat format)
{
	if (packagePath.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a folder, PAK, WAD, ZIP, or PK3 path.").arg(commandName), format);
	}

	PackageStagingModel staging;
	QString error;
	if (!loadPackageStaging(packagePath, &staging, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to stage package: %1").arg(error), format);
	}
	if (!applyPackageStageArgs(&staging, args, &error, false)) {
		return printCliError(commandName, CliExitCode::Usage, error, format);
	}

	const CliExitCode code = staging.summary().canSave ? CliExitCode::Success : CliExitCode::ValidationFailed;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("staging"), packageStagingJson(staging));
		printJson(object);
	} else {
		printPackageStagingSummary(staging);
	}
	return exitCodeValue(code);
}

int runPackageManifestCommand(const QString& commandName, const QString& packagePath, const QString& outputPath, const QStringList& args, CliOutputFormat format)
{
	if (packagePath.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a folder, PAK, WAD, ZIP, or PK3 path.").arg(commandName), format);
	}
	if (outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --output <manifest.json>.").arg(commandName), format);
	}

	PackageStagingModel staging;
	QString error;
	if (!loadPackageStaging(packagePath, &staging, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to stage package: %1").arg(error), format);
	}
	if (!applyPackageStageArgs(&staging, args, &error, false)) {
		return printCliError(commandName, CliExitCode::Usage, error, format);
	}
	if (!staging.exportManifest(outputPath, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to export package manifest: %1").arg(error), format);
	}

	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("manifestPath"), QFileInfo(outputPath).absoluteFilePath());
		object.insert(QStringLiteral("staging"), packageStagingJson(staging));
		printJson(object);
	} else {
		std::cout << "Package staging manifest exported: " << text(nativePath(QFileInfo(outputPath).absoluteFilePath())) << "\n";
		printPackageStagingSummary(staging);
	}
	return exitCodeValue(CliExitCode::Success);
}

int runPackageSaveAsCommand(const QString& commandName, const QString& packagePath, const QString& outputPath, const QStringList& args, CliOutputFormat format, const PackageStagingModel* prepared = nullptr, const PackageSubsetReview* subsetReview = nullptr)
{
	if (packagePath.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a source package path.").arg(commandName), format);
	}
	if (outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a save-as output path.").arg(commandName), format);
	}

	PackageStagingModel staging;
	QString error;
	if (prepared) { staging = *prepared; }
	const bool loaded = prepared || loadPackageStaging(packagePath, &staging, &error);
	if (!loaded) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to stage package: %1").arg(error), format);
	}
	if (!prepared && !applyPackageStageArgs(&staging, args, &error)) {
		return printCliError(commandName, CliExitCode::Usage, error, format);
	}

	PackageWriteRequest request;
	request.destinationPath = outputPath;
	request.format = packageWriteFormatFromArgs(args, outputPath);
	request.allowOverwrite = hasOption(args, QStringLiteral("--overwrite"));
	// The encoder measures every RFC 1951 block type per block whatever the
	// level, so the levels trade search effort for ratio, never correctness.
	// An unrecognised name is a usage error rather than a silent default,
	// because "I asked for best and got default" is not something a build
	// script can notice.
	const QString compressionId = optionValue(args, QStringLiteral("--compression")).trimmed();
	if (!compressionId.isEmpty()) {
		DeflateLevel level = DeflateLevel::Default;
		if (!deflateLevelFromId(compressionId, &level)) {
			return printCliError(commandName, CliExitCode::Usage,
				QStringLiteral("Unknown compression level: %1. Use store, fast, default, or best.").arg(compressionId), format);
		}
		request.compression = level;
	}
	// --in-place is the safe form of --overwrite: the writer builds the archive
	// beside the target, verifies it, and only then moves the original aside to
	// --backup (default <destination>.bak) and renames the new file into place.
	request.allowInPlaceOverwrite = hasOption(args, QStringLiteral("--in-place"));
	request.backupPath = optionValue(args, QStringLiteral("--backup"));
	request.writeManifest = hasOption(args, QStringLiteral("--manifest")) || hasOption(args, QStringLiteral("--write-manifest"));
	request.dryRun = hasOption(args, QStringLiteral("--dry-run"));
	request.manifestPath = optionValue(args, QStringLiteral("--manifest"));
	if (!request.dryRun) { StudioSettings().rememberPackagePublicationDirectory(QFileInfo(request.destinationPath).absolutePath()); }
	// Capture entry hashes while the source snapshot still exists. In-place
	// publication invalidates that reader's original offsets and identity.
	const QJsonObject preparedStaging = format == CliOutputFormat::Json ? packageStagingJson(staging) : QJsonObject();
	const PackageWriteReport report = staging.writeArchive(request);
	const CliExitCode code = report.succeeded() ? CliExitCode::Success : CliExitCode::ValidationFailed;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("write"), packageWriteReportJson(report));
		object.insert(QStringLiteral("staging"), preparedStaging);
		if (subsetReview) { object.insert(QStringLiteral("subset"), packageSubsetReviewJson(*subsetReview)); }
		printJson(object);
	} else {
		if (subsetReview) {
			for (const auto& member : subsetReview->members) {
				std::cout << "[" << member.entryIndex << "] " << text(member.virtualPath) << " — " << text(member.selected ? QStringLiteral("selected") : member.reason) << "\n";
			}
			for (const auto& warning : subsetReview->warnings) { std::cout << text(warning) << "\n"; }
		}
		std::cout << text(packageWriteReportText(report)) << "\n";
	}
	return exitCodeValue(code);
}

QStringList semicolonOrCommaList(const QString& value)
{
	QStringList result;
	for (const QString& chunk : value.split(QRegularExpression(QStringLiteral("[;,]")), Qt::SkipEmptyParts)) {
		const QString trimmed = chunk.trimmed();
		if (!trimmed.isEmpty()) {
			result.push_back(trimmed);
		}
	}
	return result;
}

QStringList assetEntryArgs(const QStringList& args)
{
	QStringList entries = optionValues(args, QStringLiteral("--entry"));
	entries += optionValues(args, QStringLiteral("--asset-entry"));
	entries += semicolonOrCommaList(optionValue(args, QStringLiteral("--entries")));

	QStringList normalized;
	for (const QString& entry : entries) {
		const QString trimmed = entry.trimmed();
		if (!trimmed.isEmpty() && !normalized.contains(trimmed)) {
			normalized.push_back(trimmed);
		}
	}
	return normalized;
}

QStringList assetTextExtensionsFromArgs(const QStringList& args)
{
	QStringList extensions;
	for (QString extension : semicolonOrCommaList(optionValue(args, QStringLiteral("--extensions")))) {
		extension = extension.trimmed().toLower();
		if (extension.startsWith(QLatin1Char('.'))) {
			extension.remove(0, 1);
		}
		if (!extension.isEmpty() && !extensions.contains(extension)) {
			extensions.push_back(extension);
		}
	}
	return extensions;
}

QRect cropRectFromOption(const QString& value, bool* ok)
{
	if (ok) {
		*ok = true;
	}
	if (value.trimmed().isEmpty()) {
		return {};
	}
	const QStringList parts = value.split(',', Qt::SkipEmptyParts);
	if (parts.size() != 4) {
		if (ok) {
			*ok = false;
		}
		return {};
	}
	bool parsed = true;
	const int x = parts.value(0).trimmed().toInt(&parsed);
	if (!parsed) {
		if (ok) {
			*ok = false;
		}
		return {};
	}
	const int y = parts.value(1).trimmed().toInt(&parsed);
	if (!parsed) {
		if (ok) {
			*ok = false;
		}
		return {};
	}
	const int width = parts.value(2).trimmed().toInt(&parsed);
	if (!parsed) {
		if (ok) {
			*ok = false;
		}
		return {};
	}
	const int height = parts.value(3).trimmed().toInt(&parsed);
	if (!parsed || width <= 0 || height <= 0) {
		if (ok) {
			*ok = false;
		}
		return {};
	}
	return QRect(x, y, width, height);
}

QSize resizeSizeFromOption(QString value, bool* ok)
{
	if (ok) {
		*ok = true;
	}
	value = value.trimmed().toLower();
	if (value.isEmpty()) {
		return {};
	}
	value.replace(QLatin1Char('x'), QLatin1Char(','));
	const QStringList parts = value.split(',', Qt::SkipEmptyParts);
	if (parts.size() != 2) {
		if (ok) {
			*ok = false;
		}
		return {};
	}
	bool widthOk = false;
	bool heightOk = false;
	const int width = parts.value(0).trimmed().toInt(&widthOk);
	const int height = parts.value(1).trimmed().toInt(&heightOk);
	if (!widthOk || !heightOk || width <= 0 || height <= 0) {
		if (ok) {
			*ok = false;
		}
		return {};
	}
	return QSize(width, height);
}

int runAssetInspectCommand(const QString& commandName, const QString& packagePath, const QString& entryPath, CliOutputFormat format)
{
	return runPackagePreviewCommand(commandName, packagePath, entryPath, format);
}

int runAssetConvertCommand(const QString& commandName, const QString& packagePath, const QString& outputPath, const QStringList& args, CliOutputFormat format)
{
	if (packagePath.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a folder, PAK, WAD, ZIP, or PK3 path.").arg(commandName), format);
	}
	if (outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --output <folder>.").arg(commandName), format);
	}

	bool cropOk = true;
	bool resizeOk = true;
	const QRect cropRect = cropRectFromOption(optionValue(args, QStringLiteral("--crop")), &cropOk);
	const QSize resizeSize = resizeSizeFromOption(optionValue(args, QStringLiteral("--resize")), &resizeOk);
	if (!cropOk) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("--crop must be x,y,w,h with positive width and height."), format);
	}
	if (!resizeOk) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("--resize must be WxH with positive dimensions."), format);
	}
	const QString paletteMode = optionValue(args, QStringLiteral("--palette")).trimmed().toLower();
	if (!paletteMode.isEmpty() && paletteMode != QStringLiteral("grayscale") && paletteMode != QStringLiteral("indexed")) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("--palette must be grayscale or indexed."), format);
	}

	PackageArchive archive;
	QString error;
	if (!loadPackageForCliQuiet(packagePath, &archive, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to open package: %1").arg(error), format);
	}

	AssetImageConversionRequest request;
	request.outputDirectory = outputPath;
	request.virtualPaths = assetEntryArgs(args);
	request.outputFormat = optionValue(args, QStringLiteral("--format")).trimmed().isEmpty() ? QStringLiteral("png") : optionValue(args, QStringLiteral("--format")).trimmed();
	request.cropRect = cropRect;
	request.resizeSize = resizeSize;
	request.paletteMode = paletteMode;
	request.dryRun = hasOption(args, QStringLiteral("--dry-run"));
	request.overwriteExisting = hasOption(args, QStringLiteral("--overwrite"));
	const AssetImageConversionReport report = convertPackageImages(archive, request);
	const CliExitCode code = report.succeeded() ? CliExitCode::Success : CliExitCode::ValidationFailed;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("imageConversion"), assetImageConversionReportJson(report));
		printJson(object);
	} else {
		std::cout << text(assetImageConversionReportText(report)) << "\n";
	}
	return exitCodeValue(code);
}

bool audioWavOptionsForCli(const QStringList& args, const QString& output, AudioWavOptions* options, QString* error, bool requireWavOutput = true)
{
	const bool supplied = hasOption(args, QStringLiteral("--wav-format")) || hasOption(args, QStringLiteral("--dither")) || hasOption(args, QStringLiteral("--dither-seed"));
	if (supplied && requireWavOutput && QFileInfo(output).suffix().compare(QStringLiteral("wav"), Qt::CaseInsensitive) != 0) {
		*error = QStringLiteral("WAV precision and dither options require --output <file.wav>."); return false;
	}
	if (hasOption(args, QStringLiteral("--wav-format")) && !parseAudioWavFormat(optionValue(args, QStringLiteral("--wav-format")), &options->format)) {
		*error = QStringLiteral("--wav-format must be pcm8, pcm16, pcm24, pcm32, or float32."); return false;
	}
	if (hasOption(args, QStringLiteral("--dither"))) {
		const QString mode = optionValue(args, QStringLiteral("--dither"));
		if (mode != QStringLiteral("none") && mode != QStringLiteral("tpdf")) { *error = QStringLiteral("--dither must be none or tpdf."); return false; }
		options->dither = mode == QStringLiteral("tpdf");
	}
	if (options->dither && options->format == AudioWavFormat::Float32) { *error = QStringLiteral("Float WAV retains samples directly; dither applies only to integer PCM."); return false; }
	if (hasOption(args, QStringLiteral("--dither-seed"))) {
		bool valid = false;
		const QString seed = optionValue(args, QStringLiteral("--dither-seed"));
		options->ditherSeed = seed.toULongLong(&valid);
		if (!valid || seed.startsWith(QLatin1Char('-')) || !options->dither) { *error = QStringLiteral("--dither-seed requires an unsigned 64-bit integer and --dither tpdf."); return false; }
	}
	return true;
}

void audioWavReportFields(QJsonObject& report, const AudioWavOptions& options)
{
	report.insert(QStringLiteral("wavFormat"), audioWavFormatId(options.format));
	report.insert(QStringLiteral("bitsPerSample"), audioWavBits(options.format));
	report.insert(QStringLiteral("dither"), options.dither ? QStringLiteral("tpdf") : QStringLiteral("none"));
	// JSON numbers cannot represent every 64-bit seed exactly.
	if (options.dither) { report.insert(QStringLiteral("ditherSeed"), QString::number(options.ditherSeed)); }
}

int runAssetAudioRecoveriesCommand(const QStringList& args, CliOutputFormat format)
{
	const QString command = QStringLiteral("asset audio-recoveries");
	QMap<QString, QString> values;
	QSet<QString> flags;
	QStringList positionals;
	for (int i = 1; i < args.size(); ++i) {
		const QString token = args[i];
		if (!token.startsWith(QStringLiteral("--"))) { positionals.append(token); continue; }
		const qsizetype equal = token.indexOf(QLatin1Char('='));
		const QString key = equal < 0 ? token : token.left(equal);
		if (flags.contains(key) || values.contains(key)) { return printCliError(command, CliExitCode::Usage, QStringLiteral("Repeated option: %1").arg(key), format); }
		if (QStringList{QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--dry-run"), QStringLiteral("--write")}.contains(key) && equal < 0) { flags.insert(key); continue; }
		if (!QStringList{QStringLiteral("--directory"), QStringLiteral("--discard"), QStringLiteral("--expected-sha256"), QStringLiteral("--kind"), QStringLiteral("--settings-file"), QStringLiteral("--locale"), QStringLiteral("--catalog-root")}.contains(key)) { return printCliError(command, CliExitCode::Usage, QStringLiteral("Unsupported recovery option: %1").arg(token), format); }
		const QString value = equal < 0 ? (i + 1 < args.size() ? args[++i] : QString()) : token.mid(equal + 1);
		if (value.isEmpty() || value.startsWith(QStringLiteral("--"))) { return printCliError(command, CliExitCode::Usage, QStringLiteral("%1 requires a value.").arg(key), format); }
		values.insert(key, value);
	}
	if (positionals != QStringList{QStringLiteral("asset"), QStringLiteral("audio-recoveries")} || (flags.contains(QStringLiteral("--write")) && flags.contains(QStringLiteral("--dry-run")))) { return printCliError(command, CliExitCode::Usage, QStringLiteral("Use asset audio-recoveries with --directory and optional reviewed discard; --write and --dry-run are exclusive."), format); }
	const QString directory = values.value(QStringLiteral("--directory"), audioRecoveryDirectory());
	const QString id = values.value(QStringLiteral("--discard"));
	const QString kindId = values.value(QStringLiteral("--kind"), QStringLiteral("waveform"));
	if (kindId != QStringLiteral("waveform") && kindId != QStringLiteral("session")) { return printCliError(command, CliExitCode::Usage, QStringLiteral("Recovery kind must be waveform or session."), format); }
	const auto kind = kindId == QStringLiteral("session") ? AudioRecoveryKind::Session : AudioRecoveryKind::Waveform;
	const bool write = flags.contains(QStringLiteral("--write"));
	if (id.isEmpty() && (write || values.contains(QStringLiteral("--expected-sha256")) || values.contains(QStringLiteral("--kind")))) { return printCliError(command, CliExitCode::Usage, QStringLiteral("--write, --kind and --expected-sha256 require --discard <id>."), format); }
	if (!id.isEmpty()) {
		const QString hash = values.value(QStringLiteral("--expected-sha256"));
		if (!QRegularExpression(QStringLiteral("^[0-9a-fA-F]{64}$")).match(hash).hasMatch()) { return printCliError(command, CliExitCode::Usage, QStringLiteral("Discard requires --expected-sha256 from the reviewed inventory."), format); }
		QString error;
		if (!discardAudioRecovery(directory, id, QByteArray::fromHex(hash.toLatin1()), !write, &error, kind)) { return printCliError(command, CliExitCode::ValidationFailed, error, format); }
	}
	const auto inventory = listAudioRecoveries(directory);
	bool valid = inventory.error.isEmpty() && !inventory.truncated && !inventory.cancelled;
	for (const auto& item : inventory.records) { valid = valid && item.verified(); }
	const auto code = valid ? CliExitCode::Success : CliExitCode::ValidationFailed;
	if (format == CliOutputFormat::Json) {
		auto report = audioRecoveryInventoryJson(inventory);
		report.insert(QStringLiteral("directory"), QDir(directory).absolutePath());
		report.insert(QStringLiteral("discardId"), id);
		report.insert(QStringLiteral("discardKind"), id.isEmpty() ? QString() : kindId);
		report.insert(QStringLiteral("discarded"), !id.isEmpty() && write);
		report.insert(QStringLiteral("dryRun"), !write);
		auto object = cliResultJson(command, code); object.insert(QStringLiteral("audioRecoveries"), report); printJson(object);
	} else {
		std::cout << "Audio recovery folder: " << text(directory) << '\n';
		for (const auto& item : inventory.records) { std::cout << text(item.id) << " | " << text(audioRecoveryKindId(item.kind)) << " | " << text(item.verified() ? item.sourceName : item.error) << " | " << item.sha256.toHex().constData() << '\n'; }
		if (!id.isEmpty()) { std::cout << (write ? "Discarded " : "Would discard ") << text(id) << '\n'; }
		if (!inventory.error.isEmpty()) { std::cout << text(inventory.error) << '\n'; }
		if (inventory.truncated) { std::cout << "Scan limit reached; additional files were not inspected.\n"; }
	}
	return exitCodeValue(code);
}

int runAssetAudioMarkersCommand(const QString& input, const QStringList& args, CliOutputFormat format)
{
	const QString command = QStringLiteral("asset audio-markers");
	QMap<QString, QString> values;
	QSet<QString> flags;
	QStringList positionals;
	for (int i = 1; i < args.size(); ++i) {
		const QString token = args[i];
		if (!token.startsWith(QStringLiteral("--"))) { positionals.append(token); continue; }
		const qsizetype equal = token.indexOf(QLatin1Char('='));
		const QString key = equal < 0 ? token : token.left(equal);
		if (flags.contains(key) || values.contains(key)) { return printCliError(command, CliExitCode::Usage, QStringLiteral("Repeated option: %1").arg(key), format); }
		if (QStringList{QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--dry-run"), QStringLiteral("--overwrite")}.contains(key) && equal < 0) { flags.insert(key); continue; }
		if (!QStringList{QStringLiteral("--markers"), QStringLiteral("--output"), QStringLiteral("--entry"), QStringLiteral("--wav-format"), QStringLiteral("--dither"), QStringLiteral("--dither-seed"), QStringLiteral("--settings-file"), QStringLiteral("--locale"), QStringLiteral("--catalog-root")}.contains(key)) {
			return printCliError(command, CliExitCode::Usage, QStringLiteral("Unsupported marker option: %1").arg(token), format);
		}
		const QString value = equal < 0 ? (i + 1 < args.size() ? args[++i] : QString()) : token.mid(equal + 1);
		if (value.isEmpty() || value.startsWith(QStringLiteral("--"))) { return printCliError(command, CliExitCode::Usage, QStringLiteral("%1 requires a value.").arg(key), format); }
		values.insert(key, value);
	}
	const QString output = values.value(QStringLiteral("--output"));
	const QString manifest = values.value(QStringLiteral("--markers"));
	if (input.isEmpty() || positionals != QStringList{QStringLiteral("asset"), QStringLiteral("audio-markers"), input} || output.isEmpty() != manifest.isEmpty() ||
	    (output.isEmpty() && flags.contains(QStringLiteral("--overwrite")))) {
		return printCliError(command, CliExitCode::Usage, QStringLiteral("Inspect one sound/project (or package with --entry). To change markers, pair --markers <JSON file> with --output <separate .vsaudio|.wav>. The JSON contains cues [{id, frame, name}] and loop null or {first, end}; end is exclusive."), format);
	}
	QString error;
	AudioWavOptions wavOptions;
	if (!audioWavOptionsForCli(args, output, &wavOptions, &error)) { return printCliError(command, CliExitCode::Usage, error, format); }
	QString source = input, protectedPath = input;
	QByteArray bytes;
	if (values.contains(QStringLiteral("--entry"))) {
		source = values.value(QStringLiteral("--entry"));
		PackageArchive archive;
		if (!loadPackageForCliQuiet(input, &archive, &error) || !archive.readEntryBytes(source, &bytes, &error, AudioInputByteLimit + 1)) { return printCliError(command, CliExitCode::Failure, error, format); }
		if (archive.format() == PackageArchiveFormat::Folder) { protectedPath = QDir(input).filePath(source); }
	} else {
		QFile file(input);
		if (!file.open(QIODevice::ReadOnly)) { return printCliError(command, CliExitCode::Failure, file.errorString(), format); }
		bytes = file.read(AudioInputByteLimit + 1);
		if (file.error() != QFileDevice::NoError) { return printCliError(command, CliExitCode::Failure, file.errorString(), format); }
	}
	AudioProject project;
	const bool nativeInput = QFileInfo(source).suffix().compare(QStringLiteral("vsaudio"), Qt::CaseInsensitive) == 0;
	if (nativeInput) {
		if (!decodeAudioProject(bytes, &project, &error)) { return printCliError(command, CliExitCode::ValidationFailed, error, format); }
	} else {
		const auto decoded = decodeAudioClip(source, bytes);
		if (!decoded.succeeded()) { return printCliError(command, CliExitCode::ValidationFailed, decoded.error, format); }
		project = {decoded.clip, 0, decoded.clip.frameCount(), source, QFileInfo(protectedPath).absoluteFilePath(), {}};
		if (!decoded.warnings.isEmpty()) { project.metadata.insert(QStringLiteral("importWarnings"), QJsonArray::fromStringList(decoded.warnings)); }
	}
	const auto before = project.clip.markers;
	const bool dryRun = flags.contains(QStringLiteral("--dry-run"));
	const bool nativeOutput = QFileInfo(output).suffix().compare(QStringLiteral("vsaudio"), Qt::CaseInsensitive) == 0;
	if (!manifest.isEmpty()) {
		QFile file(manifest);
		if (!file.open(QIODevice::ReadOnly)) { return printCliError(command, CliExitCode::Failure, file.errorString(), format); }
		const auto markerBytes = file.read(AudioMarkerByteLimit + 1);
		const auto json = QJsonDocument::fromJson(markerBytes);
		if (file.error() != QFileDevice::NoError || markerBytes.size() > AudioMarkerByteLimit || !json.isObject()) { return printCliError(command, CliExitCode::ValidationFailed, QStringLiteral("Markers require a readable JSON object within 64 KiB."), format); }
		if (!parseAudioMarkersJson(json.object(), project.clip.frameCount(), &project.clip.markers, &error)) { return printCliError(command, CliExitCode::ValidationFailed, error, format); }
		for (const auto& path : {input, protectedPath, project.sourcePath, manifest}) {
			if (!path.isEmpty() && audioPathsReferToSameFile(output, path)) { return printCliError(command, CliExitCode::ValidationFailed, QStringLiteral("Use a separate output to preserve the input, source, and marker manifest."), format); }
		}
		project.metadata.remove(QStringLiteral("recoveryWrittenUtc"));
		project.metadata.remove(QStringLiteral("recoveryOwnerPid"));
		if (nativeOutput) {
			const auto saved = writeAudioProject(project, {output, flags.contains(QStringLiteral("--overwrite")), dryRun, {}, protectedPath});
			if (!saved.succeeded) { return printCliError(command, CliExitCode::ValidationFailed, saved.error, format); }
		} else {
			const auto encoded = encodeAudioWav(project.clip, wavOptions, &error);
			if (encoded.isEmpty() || !writeAudioExportBytes(encoded, output, {QStringLiteral("wav")}, flags.contains(QStringLiteral("--overwrite")), {input, protectedPath, project.sourcePath, manifest}, &error, dryRun)) {
				return printCliError(command, CliExitCode::ValidationFailed, error, format);
			}
		}
	}
	QJsonObject report{{QStringLiteral("input"), QFileInfo(input).absoluteFilePath()}, {QStringLiteral("source"), source},
		{QStringLiteral("frames"), project.clip.frameCount()}, {QStringLiteral("channels"), project.clip.channels}, {QStringLiteral("sampleRate"), project.clip.sampleRate},
		{QStringLiteral("before"), audioMarkersJson(before)}, {QStringLiteral("markers"), audioMarkersJson(project.clip.markers)},
		{QStringLiteral("output"), output.isEmpty() ? QString() : QFileInfo(output).absoluteFilePath()},
		{QStringLiteral("dryRun"), dryRun}, {QStringLiteral("written"), !output.isEmpty() && !dryRun}};
	report.insert(QStringLiteral("importWarnings"), project.metadata.value(QStringLiteral("importWarnings")).toArray());
	if (!output.isEmpty()) {
		report.insert(QStringLiteral("losslessOutput"), nativeOutput || wavOptions.format == AudioWavFormat::Float32);
		report.insert(QStringLiteral("metadataPreserved"), nativeInput && nativeOutput);
		if (!nativeOutput) { audioWavReportFields(report, wavOptions); }
	}
	if (format == CliOutputFormat::Json) {
		auto object = cliResultJson(command, CliExitCode::Success); object.insert(QStringLiteral("audioMarkers"), report); printJson(object);
	} else {
		std::cout << text(QString::fromUtf8(QJsonDocument(audioMarkersJson(project.clip.markers)).toJson(QJsonDocument::Indented)));
		for (const auto& warning : project.metadata.value(QStringLiteral("importWarnings")).toArray()) { std::cout << text(warning.toString()) << "\n"; }
		if (!output.isEmpty()) { std::cout << (dryRun ? "Would write " : "Wrote ") << text(output) << ".\n"; }
	}
	return exitCodeValue(CliExitCode::Success);
}

int runAssetAudioProjectCommand(const QString& input, const QStringList& args, CliOutputFormat format)
{
	const QString command = QStringLiteral("asset audio-project");
	if (input.isEmpty()) { return printCliError(command, CliExitCode::Usage, QStringLiteral("Requires an audio project, recovery copy, WAV, MP3, FLAC, Ogg Vorbis or digital DMX file."), format); }
	AudioProject project;
	QString error;
	if (QFileInfo(input).suffix().compare(QStringLiteral("vsaudio"), Qt::CaseInsensitive) == 0) {
		if (!readAudioProject(input, &project, nullptr, &error)) { return printCliError(command, CliExitCode::ValidationFailed, error, format); }
	} else {
		QFile file(input);
		if (!file.open(QIODevice::ReadOnly)) { return printCliError(command, CliExitCode::Failure, file.errorString(), format); }
		const auto decoded = decodeAudioClip(input, file.read(AudioInputByteLimit + 1));
		if (file.error() != QFileDevice::NoError) { return printCliError(command, CliExitCode::Failure, file.errorString(), format); }
		if (!decoded.succeeded()) { return printCliError(command, CliExitCode::ValidationFailed, decoded.error, format); }
		project = {decoded.clip, 0, decoded.clip.frameCount(), QFileInfo(input).fileName(), QFileInfo(input).absoluteFilePath(), {}};
		if (!decoded.warnings.isEmpty()) { project.metadata.insert(QStringLiteral("importWarnings"), QJsonArray::fromStringList(decoded.warnings)); }
	}
	const QString output = optionValue(args, QStringLiteral("--output"));
	const bool dryRun = hasOption(args, QStringLiteral("--dry-run"));
	const bool recovery = project.metadata.contains(QStringLiteral("recoveryWrittenUtc"));
	const bool native = QFileInfo(output).suffix().compare(QStringLiteral("vsaudio"), Qt::CaseInsensitive) == 0;
	AudioWavOptions wavOptions;
	if (!audioWavOptionsForCli(args, output, &wavOptions, &error)) { return printCliError(command, CliExitCode::Usage, error, format); }
	if (hasOption(args, QStringLiteral("--output")) && output.isEmpty()) {
		return printCliError(command, CliExitCode::Usage, QStringLiteral("--output requires a separate .vsaudio or .wav file."), format);
	}
	if (!output.isEmpty()) {
		if (!project.sourcePath.isEmpty() && audioPathsReferToSameFile(output, project.sourcePath)) {
			return printCliError(command, CliExitCode::ValidationFailed, QStringLiteral("Export to a separate path to preserve the source."), format);
		}
		project.metadata.remove(QStringLiteral("recoveryWrittenUtc"));
		project.metadata.remove(QStringLiteral("recoveryOwnerPid"));
		if (native) {
			const auto report = writeAudioProject(project, {output, hasOption(args, QStringLiteral("--overwrite")), dryRun, {}, input});
			if (!report.succeeded) { return printCliError(command, CliExitCode::ValidationFailed, report.error, format); }
		} else {
			// Protect both the inspected project/recovery and its original source.
			if (!saveAudioWav(project.clip, wavOptions, output, hasOption(args, QStringLiteral("--overwrite")), input, &error, dryRun)) {
				return printCliError(command, CliExitCode::ValidationFailed, error.isEmpty() ? QStringLiteral("Export to a separate path to preserve the source.") : error, format);
			}
		}
	}
	QJsonObject report{{QStringLiteral("input"), QFileInfo(input).absoluteFilePath()}, {QStringLiteral("sourceName"), project.sourceName},
		{QStringLiteral("sourcePath"), project.sourcePath}, {QStringLiteral("frames"), project.clip.frameCount()},
		{QStringLiteral("channels"), project.clip.channels}, {QStringLiteral("sampleRate"), project.clip.sampleRate},
		{QStringLiteral("selectionStart"), project.firstFrame}, {QStringLiteral("selectionEnd"), project.endFrame},
		{QStringLiteral("recovery"), recovery}, {QStringLiteral("dryRun"), dryRun}, {QStringLiteral("written"), !output.isEmpty() && !dryRun},
		{QStringLiteral("output"), output.isEmpty() ? QString() : QFileInfo(output).absoluteFilePath()},
		{QStringLiteral("losslessOutput"), native}, {QStringLiteral("metadata"), project.metadata}};
	report.insert(QStringLiteral("markers"), audioMarkersJson(project.clip.markers));
	report.insert(QStringLiteral("importWarnings"), project.metadata.value(QStringLiteral("importWarnings")).toArray());
	if (!output.isEmpty() && !native) {
		audioWavReportFields(report, wavOptions);
		report.insert(QStringLiteral("losslessOutput"), wavOptions.format == AudioWavFormat::Float32);
		report.insert(QStringLiteral("metadataPreserved"), false);
	}
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(command, CliExitCode::Success); object.insert(QStringLiteral("audioProject"), report); printJson(object);
	} else {
		std::cout << project.clip.frameCount() << " frames, " << project.clip.channels << " channels, " << project.clip.sampleRate << " Hz.\n";
		for (const auto& warning : project.metadata.value(QStringLiteral("importWarnings")).toArray()) { std::cout << text(warning.toString()) << "\n"; }
		if (recovery) { std::cout << "Local recovery copy; source path is provenance only.\n"; }
		if (!output.isEmpty()) { std::cout << (dryRun ? "Would write " : "Wrote ") << text(output) << (native ? " (lossless audio project)" : " (" + text(audioWavFormatId(wavOptions.format)) + " WAV)") << ".\n"; }
	}
	return exitCodeValue(CliExitCode::Success);
}

int runAssetAudioExportCommand(const QString& input, const QStringList& args, CliOutputFormat format)
{
	const QString command = QStringLiteral("asset audio-export");
	const QString output = optionValue(args, QStringLiteral("--output"));
	AudioDeliveryOptions options;
	QString error;
	if (input.isEmpty() || output.isEmpty()) {
		return printCliError(command, CliExitCode::Usage, QStringLiteral("Requires an input sound/project and --output <file.wav|file.dmx|file.lmp>; optional --entry reads a package sound. --preset is wav, doom, quake, quake2, or quake3. WAV preserves the document rate/channels and accepts --wav-format; game presets convert a delivery copy to mono at their fixed rate/precision. --dither none|tpdf and --dither-seed apply at final integer quantization."), format);
	}
	if ((hasOption(args, QStringLiteral("--preset")) && !parseAudioDeliveryPreset(optionValue(args, QStringLiteral("--preset")), &options.preset)) ||
	    (options.preset != AudioDeliveryPreset::Wav && hasOption(args, QStringLiteral("--wav-format")))) {
		return printCliError(command, CliExitCode::Usage, QStringLiteral("Choose --preset wav|doom|quake|quake2|quake3. Only the wav preset accepts --wav-format."), format);
	}
	for (const QString& flag : {QStringLiteral("--sample-rate"), QStringLiteral("--channels"), QStringLiteral("--operation"), QStringLiteral("--frames"), QStringLiteral("--start-frame"), QStringLiteral("--end-frame")}) {
		if (hasOption(args, flag)) { return printCliError(command, CliExitCode::Usage, QStringLiteral("%1 changes the editable document; use audio-edit or audio-new before delivery.").arg(flag), format); }
	}
	if (!audioWavOptionsForCli(args, output, &options.wav, &error, false)) { return printCliError(command, CliExitCode::Usage, error, format); }
	QString sourceName = input, protectedPath = input;
	QByteArray bytes;
	if (hasOption(args, QStringLiteral("--entry"))) {
		sourceName = optionValue(args, QStringLiteral("--entry"));
		PackageArchive archive;
		if (sourceName.isEmpty() || !loadPackageForCliQuiet(input, &archive, &error) || !archive.readEntryBytes(sourceName, &bytes, &error, AudioInputByteLimit + 1)) {
			return printCliError(command, CliExitCode::Failure, error.isEmpty() ? QStringLiteral("A readable --entry is required.") : error, format);
		}
		if (archive.format() == PackageArchiveFormat::Folder) { protectedPath = QDir(input).filePath(sourceName); }
	} else {
		QFile file(input);
		if (!file.open(QIODevice::ReadOnly)) { return printCliError(command, CliExitCode::Failure, file.errorString(), format); }
		bytes = file.read(AudioInputByteLimit + 1);
		if (file.error() != QFileDevice::NoError) { return printCliError(command, CliExitCode::Failure, file.errorString(), format); }
	}
	AudioProject document;
	if (QFileInfo(sourceName).suffix().compare(QStringLiteral("vsaudio"), Qt::CaseInsensitive) == 0) {
		if (!decodeAudioProject(bytes, &document, &error)) { return printCliError(command, CliExitCode::ValidationFailed, error, format); }
	} else {
		const auto decoded = decodeAudioClip(sourceName, bytes);
		if (!decoded.succeeded()) { return printCliError(command, CliExitCode::ValidationFailed, decoded.error, format); }
		document.clip = decoded.clip;
		if (!decoded.warnings.isEmpty()) { document.metadata.insert(QStringLiteral("importWarnings"), QJsonArray::fromStringList(decoded.warnings)); }
	}
	const auto delivery = renderAudioDelivery(document.clip, options);
	if (!delivery.succeeded()) { return printCliError(command, CliExitCode::ValidationFailed, delivery.error, format); }
	const bool dryRun = hasOption(args, QStringLiteral("--dry-run"));
	if (!writeAudioDelivery(delivery, output, hasOption(args, QStringLiteral("--overwrite")), {input, protectedPath, document.sourcePath}, &error, dryRun)) {
		return printCliError(command, CliExitCode::Failure, error, format);
	}
	if (format == CliOutputFormat::Json) {
		QJsonObject report{{QStringLiteral("input"), QFileInfo(input).absoluteFilePath()}, {QStringLiteral("source"), sourceName},
			{QStringLiteral("output"), QFileInfo(output).absoluteFilePath()}, {QStringLiteral("preset"), audioDeliveryPresetId(options.preset)},
			{QStringLiteral("container"), delivery.plan.dmx ? QStringLiteral("dmx") : QStringLiteral("wav")},
			{QStringLiteral("inputFrames"), document.clip.frameCount()}, {QStringLiteral("inputSampleRate"), document.clip.sampleRate},
			{QStringLiteral("inputChannels"), document.clip.channels}, {QStringLiteral("frames"), delivery.plan.frames},
			{QStringLiteral("sampleRate"), delivery.plan.sampleRate}, {QStringLiteral("channels"), delivery.plan.channels},
			{QStringLiteral("bytes"), delivery.bytes.size()}, {QStringLiteral("dryRun"), dryRun}, {QStringLiteral("written"), !dryRun},
			{QStringLiteral("peak"), delivery.peak}, {QStringLiteral("samplesAboveFullScale"), delivery.samplesAboveFullScale},
			{QStringLiteral("metadataPreserved"), false}};
		report.insert(QStringLiteral("importWarnings"), document.metadata.value(QStringLiteral("importWarnings")).toArray());
		report.insert(QStringLiteral("inputMarkers"), audioMarkersJson(document.clip.markers));
		report.insert(QStringLiteral("outputCues"), delivery.plan.outputCues);
		report.insert(QStringLiteral("outputLoop"), delivery.plan.outputLoop);
		report.insert(QStringLiteral("markerSummary"), delivery.plan.markerSummary);
		audioWavReportFields(report, delivery.plan.wav);
		if (delivery.plan.dmx) { report.remove(QStringLiteral("wavFormat")); report.insert(QStringLiteral("sampleFormat"), QStringLiteral("pcm8")); }
		auto result = cliResultJson(command, CliExitCode::Success); result.insert(QStringLiteral("audioExport"), report); printJson(result);
	} else {
		std::cout << (dryRun ? "Would export " : "Exported ") << text(output) << ": " << text(audioDeliveryDescription(delivery.plan)) << ".\n";
		for (const auto& warning : document.metadata.value(QStringLiteral("importWarnings")).toArray()) { std::cout << text(warning.toString()) << "\n"; }
		if (delivery.plan.wav.format != AudioWavFormat::Float32 && delivery.samplesAboveFullScale > 0) {
			std::cout << "Warning: " << delivery.samplesAboveFullScale << " converted samples exceed full scale; reduce gain before integer delivery.\n";
		}
	}
	return exitCodeValue(CliExitCode::Success);
}

int runAssetAudioAnalyzeCommand(const QString& input, const QStringList& args, CliOutputFormat format)
{
	const QString command = QStringLiteral("asset audio-analyze");
	if (input.isEmpty()) {
		return printCliError(command, CliExitCode::Usage, QStringLiteral("Requires a sound/project or a package with --entry. Optional --start-frame and --end-frame select an exact, end-exclusive range; the default is the whole sound. Surround loudness requires --channel-map with source-order roles (L,R,C,LFE,Ls,Rs,Lb,Rb,Cb,X). Use --no-loudness for sample/true-peak statistics only. Analysis never writes files."), format);
	}
	for (const QString& flag : {QStringLiteral("--output"), QStringLiteral("--overwrite"), QStringLiteral("--operation"), QStringLiteral("--sample-rate"), QStringLiteral("--channels"), QStringLiteral("--frames"), QStringLiteral("--preset"), QStringLiteral("--wav-format"), QStringLiteral("--dither"), QStringLiteral("--dither-seed")}) {
		if (hasOption(args, flag)) { return printCliError(command, CliExitCode::Usage, QStringLiteral("%1 is not an analysis option; analysis is read-only.").arg(flag), format); }
	}
	qint64 first = 0, end = -1;
	for (const QString& flag : {QStringLiteral("--start-frame"), QStringLiteral("--end-frame")}) {
		if (!hasOption(args, flag)) { continue; }
		bool valid = false;
		const qint64 value = optionValue(args, flag).toLongLong(&valid);
		if (!valid || value < 0 || value > AudioSampleLimit) {
			return printCliError(command, CliExitCode::Usage, QStringLiteral("%1 requires a non-negative frame number within the editor's sample limit.").arg(flag), format);
		}
		if (flag == QStringLiteral("--start-frame")) { first = value; } else { end = value; }
	}
	QString source = input, error;
	QByteArray bytes;
	if (hasOption(args, QStringLiteral("--entry"))) {
		source = optionValue(args, QStringLiteral("--entry"));
		PackageArchive archive;
		if (source.isEmpty() || !loadPackageForCliQuiet(input, &archive, &error) || !archive.readEntryBytes(source, &bytes, &error, AudioInputByteLimit + 1)) {
			return printCliError(command, CliExitCode::Failure, error.isEmpty() ? QStringLiteral("A readable --entry is required.") : error, format);
		}
	} else {
		QFile file(input);
		if (!file.open(QIODevice::ReadOnly)) { return printCliError(command, CliExitCode::Failure, file.errorString(), format); }
		bytes = file.read(AudioInputByteLimit + 1);
		if (file.error() != QFileDevice::NoError) { return printCliError(command, CliExitCode::Failure, file.errorString(), format); }
	}
	AudioClip clip;
	QStringList importWarnings;
	if (QFileInfo(source).suffix().compare(QStringLiteral("vsaudio"), Qt::CaseInsensitive) == 0) {
		AudioProject project;
		if (!decodeAudioProject(bytes, &project, &error)) { return printCliError(command, CliExitCode::ValidationFailed, error, format); }
		clip = project.clip;
		for (const auto& warning : project.metadata.value(QStringLiteral("importWarnings")).toArray()) { importWarnings << warning.toString(); }
	} else {
		const auto decoded = decodeAudioClip(source, bytes);
		if (!decoded.succeeded()) { return printCliError(command, CliExitCode::ValidationFailed, decoded.error, format); }
		clip = decoded.clip;
		importWarnings = decoded.warnings;
	}
	AudioAnalysisOptions options;
	options.measureLoudness = !hasOption(args, QStringLiteral("--no-loudness"));
	if (!options.measureLoudness && hasOption(args, QStringLiteral("--channel-map"))) {
		return printCliError(command, CliExitCode::Usage, QStringLiteral("Choose --channel-map or --no-loudness, not both."), format);
	}
	if (hasOption(args, QStringLiteral("--channel-map"))) {
		for (const QString& token : optionValue(args, QStringLiteral("--channel-map")).split(QLatin1Char(','))) {
			AudioChannelRole role;
			if (!parseAudioChannelRole(token, &role)) {
				return printCliError(command, CliExitCode::Usage, QStringLiteral("--channel-map takes source-order roles: L,R,C,LFE,Ls,Rs,Lb,Rb,Cb,X. X excludes a channel from loudness only."), format);
			}
			options.channelMap << role;
		}
	}
	const auto result = analyzeAudioClip(clip, first, end, {}, options);
	if (!result.succeeded()) { return printCliError(command, CliExitCode::ValidationFailed, result.error, format); }
	if (format == CliOutputFormat::Json) {
		auto report = audioAnalysisJson(result.analysis);
		report.insert(QStringLiteral("input"), QFileInfo(input).absoluteFilePath());
		report.insert(QStringLiteral("source"), source);
		report.insert(QStringLiteral("importWarnings"), QJsonArray::fromStringList(importWarnings));
		auto object = cliResultJson(command, CliExitCode::Success);
		object.insert(QStringLiteral("audioAnalysis"), report);
		printJson(object);
	} else {
		std::cout << "Frames " << result.analysis.firstFrame << ".." << result.analysis.endFrame << " (end exclusive), " << clip.sampleRate << " Hz.\n";
		for (qsizetype channel = 0; channel < result.analysis.channels.size(); ++channel) {
			const auto& stats = result.analysis.channels[channel];
			const auto db = [](double level) { return level > 0 ? QString::number(20 * std::log10(level), 'f', 2) : QStringLiteral("-inf"); };
			std::cout << "Channel " << channel + 1 << ": peak " << text(db(stats.peak)) << " dBFS, RMS " << text(db(stats.rms)) << " dBFS, DC " << stats.dc * 100 << "%, " << stats.samplesAboveFullScale << " samples above full scale.\n";
		}
		for (const auto& warning : importWarnings) { std::cout << text(warning) << "\n"; }
		const auto& analysis = result.analysis;
		if (analysis.truePeak) {
			std::cout << "True peak: " << (*analysis.truePeak > 0 ? text(QString::number(20 * std::log10(*analysis.truePeak), 'f', 2)) : "-inf") << " dBTP.\n";
		} else { std::cout << text(analysis.truePeakMessage) << '\n'; }
		if (analysis.integratedLufs) {
			std::cout << "Integrated loudness: " << *analysis.integratedLufs << " LUFS.\n";
		} else { std::cout << text(analysis.loudnessMessage) << '\n'; }
		std::cout << "Sample RMS includes DC; loudness uses reviewed speaker roles and BS.1770 gating. Source unchanged.\n";
	}
	return exitCodeValue(CliExitCode::Success);
}

int runAssetAudioNewCommand(const QStringList& args, CliOutputFormat format)
{
	const QString command = QStringLiteral("asset audio-new");
	const QString output = optionValue(args, QStringLiteral("--output"));
	if (output.isEmpty()) { return printCliError(command, CliExitCode::Usage, QStringLiteral("Requires --output <file.vsaudio|file.wav>."), format); }
	qint64 rate = 44100, channels = 1, frames = 0;
	for (const QString& flag : {QStringLiteral("--sample-rate"), QStringLiteral("--channels"), QStringLiteral("--frames")}) {
		if (!hasOption(args, flag)) { continue; }
		bool valid = false; const qint64 value = optionValue(args, flag).toLongLong(&valid);
		const qint64 maximum = flag == QStringLiteral("--sample-rate") ? 384000 : flag == QStringLiteral("--channels") ? 8 : AudioSampleLimit;
		const int minimum = flag == QStringLiteral("--frames") ? 0 : 1;
		if (!valid || value < minimum || value > maximum) { return printCliError(command, CliExitCode::Usage, QStringLiteral("Invalid %1 value.").arg(flag), format); }
		if (flag == QStringLiteral("--sample-rate")) { rate = value; } else if (flag == QStringLiteral("--channels")) { channels = value; } else { frames = value; }
	}
	const auto created = createAudioClip(static_cast<int>(rate), static_cast<int>(channels), frames);
	if (!created.succeeded()) { return printCliError(command, CliExitCode::ValidationFailed, created.error, format); }
	const bool dryRun = hasOption(args, QStringLiteral("--dry-run"));
	QString error;
	AudioWavOptions wavOptions;
	if (!audioWavOptionsForCli(args, output, &wavOptions, &error)) { return printCliError(command, CliExitCode::Usage, error, format); }
	if (QFileInfo(output).suffix().compare(QStringLiteral("vsaudio"), Qt::CaseInsensitive) == 0) {
		const AudioProject project{created.clip, 0, frames, QFileInfo(output).completeBaseName(), {}, {}};
		const auto saved = writeAudioProject(project, {output, hasOption(args, QStringLiteral("--overwrite")), dryRun, {}, {}});
		error = saved.error;
	} else { saveAudioWav(created.clip, wavOptions, output, hasOption(args, QStringLiteral("--overwrite")), {}, &error, dryRun); }
	if (!error.isEmpty()) { return printCliError(command, CliExitCode::ValidationFailed, error, format); }
	if (format == CliOutputFormat::Json) {
		auto result = cliResultJson(command, CliExitCode::Success);
		result.insert(QStringLiteral("audioNew"), QJsonObject{{QStringLiteral("sampleRate"), rate}, {QStringLiteral("channels"), channels},
			{QStringLiteral("frames"), frames}, {QStringLiteral("output"), QFileInfo(output).absoluteFilePath()},
			{QStringLiteral("dryRun"), dryRun}, {QStringLiteral("written"), !dryRun}});
		printJson(result);
	} else { std::cout << (dryRun ? "Would create " : "Created ") << text(output) << ": " << frames << " frames, " << channels << " channels, " << rate << " Hz.\n"; }
	return exitCodeValue(CliExitCode::Success);
}

int runAssetAudioEditCommand(const QString& input, const QStringList& args, CliOutputFormat format)
{
	const QString command = QStringLiteral("asset audio-edit");
	const QString output = optionValue(args, QStringLiteral("--output"));
	AudioWavOptions wavOptions;
	QString optionsError;
	if (!audioWavOptionsForCli(args, output, &wavOptions, &optionsError)) { return printCliError(command, CliExitCode::Usage, optionsError, format); }
	AudioEdit edit;
	edit.operation = optionValue(args, QStringLiteral("--operation"));
	if (input.isEmpty() || output.isEmpty() || edit.operation.isEmpty()) {
		return printCliError(command, CliExitCode::Usage, QStringLiteral("Requires an input path, --operation <trim|delete|silence|reverse|fade-in|fade-out|gain|normalize|mono|stereo|invert|remove-dc|paste|mix|insert-silence|resample>, and --output <file.wav|file.vsaudio>. Use --entry for a package sound, --start-frame/--end-frame for an exclusive frame range, --db for gain/normalization, --paste-input for paste/mix, --frames for silence insertion, and --sample-rate for whole-sound resampling. WAV output accepts --wav-format pcm8|pcm16|pcm24|pcm32|float32, --dither none|tpdf, and an optional --dither-seed."), format);
	}
	for (const QString& option : {QStringLiteral("--start-frame"), QStringLiteral("--end-frame")}) {
		if (!hasOption(args, option)) { continue; }
		bool valid = false;
		const qint64 frame = optionValue(args, option).toLongLong(&valid);
		if (!valid || frame < 0) { return printCliError(command, CliExitCode::Usage, QStringLiteral("%1 requires a non-negative frame number.").arg(option), format); }
		if (option == QStringLiteral("--start-frame")) { edit.firstFrame = frame; } else { edit.endFrame = frame; }
	}
	if (hasOption(args, QStringLiteral("--db"))) {
		bool valid = false; edit.decibels = optionValue(args, QStringLiteral("--db")).toDouble(&valid);
		if (!valid || (edit.operation != QStringLiteral("gain") && edit.operation != QStringLiteral("normalize"))) {
			return printCliError(command, CliExitCode::Usage, QStringLiteral("--db requires a numeric value and a gain or normalize operation."), format);
		}
	} else if (edit.operation == QStringLiteral("normalize")) { edit.decibels = -1.0; }
	const bool resample = edit.operation == QStringLiteral("resample");
	int targetRate = 0;
	if (resample != hasOption(args, QStringLiteral("--sample-rate"))) {
		return printCliError(command, CliExitCode::Usage, QStringLiteral("resample requires --sample-rate; other edits do not accept it."), format);
	}
	if (resample) {
		bool valid = false; targetRate = optionValue(args, QStringLiteral("--sample-rate")).toInt(&valid);
		if (!valid || targetRate < 1 || targetRate > 384000 || hasOption(args, QStringLiteral("--start-frame")) || hasOption(args, QStringLiteral("--end-frame"))) {
			return printCliError(command, CliExitCode::Usage, QStringLiteral("Resampling applies to the whole sound and requires --sample-rate between 1 and 384000; frame range options are not accepted."), format);
		}
	}
	QString error, sourceName = input, protectedPath = input;
	QByteArray bytes;
	if (hasOption(args, QStringLiteral("--entry"))) {
		sourceName = optionValue(args, QStringLiteral("--entry"));
		PackageArchive archive;
		if (sourceName.isEmpty() || !loadPackageForCliQuiet(input, &archive, &error) || !archive.readEntryBytes(sourceName, &bytes, &error, AudioInputByteLimit + 1)) {
			return printCliError(command, CliExitCode::Failure, error.isEmpty() ? QStringLiteral("A readable --entry is required.") : error, format);
		}
		if (archive.format() == PackageArchiveFormat::Folder) { protectedPath = QDir(input).filePath(sourceName); }
	} else {
		QFile file(input);
		if (!file.open(QIODevice::ReadOnly)) { return printCliError(command, CliExitCode::Failure, file.errorString(), format); }
		bytes = file.read(AudioInputByteLimit + 1);
		if (file.error() != QFileDevice::NoError) { return printCliError(command, CliExitCode::Failure, file.errorString(), format); }
	}
	AudioProject document;
	AudioClipResult source;
	const bool nativeInput = QFileInfo(sourceName).suffix().compare(QStringLiteral("vsaudio"), Qt::CaseInsensitive) == 0;
	if (nativeInput) {
		if (!decodeAudioProject(bytes, &document, &error)) { return printCliError(command, CliExitCode::ValidationFailed, error, format); }
		source.clip = document.clip;
	} else {
		source = decodeAudioClip(sourceName, bytes);
		document.sourceName = sourceName; document.sourcePath = QFileInfo(protectedPath).absoluteFilePath();
	}
	if (!source.succeeded()) { return printCliError(command, CliExitCode::ValidationFailed, source.error, format); }
	const bool paste = edit.operation == QStringLiteral("paste"), mix = edit.operation == QStringLiteral("mix");
	const bool silenceInsert = edit.operation == QStringLiteral("insert-silence");
	const QString pastePath = optionValue(args, QStringLiteral("--paste-input"));
	if (hasOption(args, QStringLiteral("--paste-input")) != (paste || mix) ||
		hasOption(args, QStringLiteral("--frames")) != silenceInsert ||
		((mix || silenceInsert) && hasOption(args, QStringLiteral("--end-frame")))) {
		return printCliError(command, CliExitCode::Usage, QStringLiteral("paste/mix require --paste-input; insert-silence requires --frames. Only paste replaces an --end-frame range."), format);
	}
	AudioClipResult result;
	qint64 insertedFrames = 0;
	if (paste || mix) {
		AudioClipResult fragment;
		if (QFileInfo(pastePath).suffix().compare(QStringLiteral("vsaudio"), Qt::CaseInsensitive) == 0) {
			AudioProject insertion;
			if (!readAudioProject(pastePath, &insertion, nullptr, &error)) { return printCliError(command, CliExitCode::ValidationFailed, error, format); }
			fragment.clip = insertion.clip;
		} else {
			QFile file(pastePath);
			if (!file.open(QIODevice::ReadOnly)) { return printCliError(command, CliExitCode::Failure, file.errorString(), format); }
			fragment = decodeAudioClip(pastePath, file.read(AudioInputByteLimit + 1));
			if (file.error() != QFileDevice::NoError) { return printCliError(command, CliExitCode::Failure, file.errorString(), format); }
		}
		if (!fragment.succeeded() || fragment.clip.frameCount() == 0) { return printCliError(command, CliExitCode::ValidationFailed, fragment.error.isEmpty() ? QStringLiteral("Pasted audio must contain samples.") : fragment.error, format); }
		if (audioPathsReferToSameFile(output, pastePath)) { return printCliError(command, CliExitCode::Failure, QStringLiteral("The pasted source is protected; choose another output."), format); }
		source.warnings += fragment.warnings; source.warnings.removeDuplicates();
		insertedFrames = fragment.clip.frameCount();
		result = mix ? mixAudioClip(source.clip, edit.firstFrame, fragment.clip)
			: replaceAudioRange(source.clip, edit.firstFrame, edit.endFrame == -1 ? source.clip.frameCount() : edit.endFrame, fragment.clip);
	} else if (silenceInsert) {
		bool valid = false; insertedFrames = optionValue(args, QStringLiteral("--frames")).toLongLong(&valid);
		if (!valid || insertedFrames < 0) { return printCliError(command, CliExitCode::Usage, QStringLiteral("--frames requires a non-negative integer."), format); }
		result = insertAudioSilence(source.clip, edit.firstFrame, insertedFrames);
	} else if (resample) { result = resampleAudioClip(source.clip, targetRate); }
	else { result = applyAudioEdit(source.clip, edit); }
	if (!result.succeeded()) { return printCliError(command, CliExitCode::ValidationFailed, result.error, format); }
	double peak = 0;
	qint64 clippedSamples = 0;
	for (float sample : result.clip.samples) {
		peak = std::max(peak, std::abs(static_cast<double>(sample)));
		if (sample < -1.0f || sample > 1.0f) { ++clippedSamples; }
	}
	const bool dryRun = hasOption(args, QStringLiteral("--dry-run"));
	const bool nativeOutput = QFileInfo(output).suffix().compare(QStringLiteral("vsaudio"), Qt::CaseInsensitive) == 0;
	if ((!document.sourcePath.isEmpty() && audioPathsReferToSameFile(output, document.sourcePath)) || audioPathsReferToSameFile(output, protectedPath)) {
		return printCliError(command, CliExitCode::Failure, QStringLiteral("Export to a separate path to preserve the source sound or project."), format);
	}
	if (!source.warnings.isEmpty()) { document.metadata.insert(QStringLiteral("importWarnings"), QJsonArray::fromStringList(source.warnings)); }
	if (nativeOutput) {
		const qint64 previousFirst = nativeInput ? document.firstFrame : 0;
		const qint64 previousEnd = nativeInput ? document.endFrame : source.clip.frameCount();
		document.clip = result.clip;
		document.firstFrame = edit.operation == QStringLiteral("trim") ? 0 : std::min(edit.firstFrame, result.clip.frameCount());
		document.endFrame = edit.operation == QStringLiteral("trim") ? result.clip.frameCount()
			: edit.operation == QStringLiteral("delete") ? document.firstFrame : std::min(edit.endFrame == -1 ? source.clip.frameCount() : edit.endFrame, result.clip.frameCount());
		if (paste || mix || silenceInsert) { document.endFrame = document.firstFrame + insertedFrames; }
		if (resample) {
			const auto mapped = [&](qint64 frame) { return frame == source.clip.frameCount() ? result.clip.frameCount()
				: std::min(result.clip.frameCount(), audioFrameAtSampleRate(frame, source.clip.sampleRate, targetRate)); };
			document.firstFrame = mapped(previousFirst); document.endFrame = mapped(previousEnd);
		}
		document.metadata.remove(QStringLiteral("recoveryWrittenUtc")); document.metadata.remove(QStringLiteral("recoveryOwnerPid"));
		const auto saved = writeAudioProject(document, {output, hasOption(args, QStringLiteral("--overwrite")), dryRun, {}, protectedPath});
		if (!saved.succeeded) { return printCliError(command, CliExitCode::Failure, saved.error, format); }
	} else if (!saveAudioWav(result.clip, wavOptions, output, hasOption(args, QStringLiteral("--overwrite")),
		document.sourcePath.isEmpty() ? protectedPath : document.sourcePath, &error, dryRun)) {
		return printCliError(command, CliExitCode::Failure, error, format);
	}
	if (format == CliOutputFormat::Json) {
		QJsonObject report;
		report.insert(QStringLiteral("input"), QFileInfo(input).absoluteFilePath()); report.insert(QStringLiteral("source"), sourceName);
		report.insert(QStringLiteral("output"), QFileInfo(output).absoluteFilePath()); report.insert(QStringLiteral("operation"), edit.operation);
		report.insert(QStringLiteral("startFrame"), edit.firstFrame); report.insert(QStringLiteral("endFrame"), edit.endFrame == -1 ? source.clip.frameCount() : edit.endFrame);
		report.insert(QStringLiteral("inputFrames"), source.clip.frameCount()); report.insert(QStringLiteral("outputFrames"), result.clip.frameCount());
		report.insert(QStringLiteral("insertedFrames"), insertedFrames);
		report.insert(QStringLiteral("inputSampleRate"), source.clip.sampleRate);
		report.insert(QStringLiteral("channels"), result.clip.channels); report.insert(QStringLiteral("sampleRate"), result.clip.sampleRate);
		report.insert(QStringLiteral("bitsPerSample"), 32); report.insert(QStringLiteral("dryRun"), dryRun); report.insert(QStringLiteral("written"), !dryRun);
		if (!nativeOutput) { audioWavReportFields(report, wavOptions); }
		report.insert(QStringLiteral("losslessOutput"), nativeOutput || wavOptions.format == AudioWavFormat::Float32);
		report.insert(QStringLiteral("metadataPreserved"), nativeInput && nativeOutput);
		report.insert(QStringLiteral("inputMarkers"), audioMarkersJson(source.clip.markers));
		report.insert(QStringLiteral("markers"), audioMarkersJson(result.clip.markers));
		report.insert(QStringLiteral("importWarnings"), document.metadata.value(QStringLiteral("importWarnings")).toArray());
		report.insert(QStringLiteral("peak"), peak);
		report.insert(QStringLiteral("peakDbfs"), peak > 0 ? QJsonValue(20.0 * std::log10(peak)) : QJsonValue(QJsonValue::Null));
		report.insert(QStringLiteral("samplesAboveFullScale"), clippedSamples);
		report.insert(QStringLiteral("clippedSamples"), nativeOutput || wavOptions.format == AudioWavFormat::Float32 ? qint64(0) : clippedSamples);
		QJsonObject object = cliResultJson(command, CliExitCode::Success); object.insert(QStringLiteral("audioEdit"), report); printJson(object);
	} else {
		std::cout << (dryRun ? "Would export " : "Exported ") << text(output) << ": " << result.clip.frameCount() << " frames, "
			<< result.clip.channels << " channels, " << result.clip.sampleRate << " Hz, "
			<< (nativeOutput ? "lossless audio project" : text(audioWavFormatId(wavOptions.format)) + " WAV; supported cues and loop retained, other source tags omitted") << ".\n";
		for (const auto& warning : document.metadata.value(QStringLiteral("importWarnings")).toArray()) { std::cout << text(warning.toString()) << "\n"; }
		if (!nativeOutput && wavOptions.format != AudioWavFormat::Float32 && clippedSamples > 0) { std::cout << "Warning: " << clippedSamples << " samples clip at integer export. Normalize or reduce gain to retain the signal shape.\n"; }
	}
	return exitCodeValue(CliExitCode::Success);
}

int runAssetAudioWavCommand(const QString& commandName, const QString& packagePath, const QString& entryPath, const QString& outputPath, const QStringList& args, CliOutputFormat format)
{
	if (packagePath.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a folder, PAK, WAD, ZIP, or PK3 path.").arg(commandName), format);
	}
	if (entryPath.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a virtual package audio entry path.").arg(commandName), format);
	}
	if (outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --output <file.wav>.").arg(commandName), format);
	}

	PackageArchive archive;
	QString error;
	if (!loadPackageForCliQuiet(packagePath, &archive, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to open package: %1").arg(error), format);
	}
	const AssetAudioExportReport report = exportPackageAudioToWav(archive, entryPath, outputPath, hasOption(args, QStringLiteral("--dry-run")), hasOption(args, QStringLiteral("--overwrite")));
	const CliExitCode code = report.succeeded() ? CliExitCode::Success : CliExitCode::ValidationFailed;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("audioExport"), assetAudioExportReportJson(report));
		printJson(object);
	} else {
		std::cout << text(assetAudioExportReportText(report)) << "\n";
	}
	return exitCodeValue(code);
}

int runAssetTextCommand(const QString& commandName, const QString& projectRoot, const QString& positionalFind, const QString& positionalReplace, bool replace, const QStringList& args, CliOutputFormat format)
{
	if (projectRoot.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a project root path.").arg(commandName), format);
	}
	const QString findText = hasOption(args, QStringLiteral("--find")) ? optionValue(args, QStringLiteral("--find")) : positionalFind;
	const QString replaceText = hasOption(args, QStringLiteral("--replace")) ? optionValue(args, QStringLiteral("--replace")) : positionalReplace;
	if (findText.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --find <text>.").arg(commandName), format);
	}
	const bool deleteMatches = hasOption(args, QStringLiteral("--delete-matches"));
	if (deleteMatches && (!replace || hasOption(args, QStringLiteral("--replace")) || !positionalReplace.isEmpty())) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("--delete-matches is only valid for asset replace and cannot be combined with replacement text."), format);
	}
	if (replace && replaceText.isEmpty() && !deleteMatches) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --replace <text>, or --delete-matches to remove matches.").arg(commandName), format);
	}

	AssetTextSearchRequest request;
	request.rootPath = projectRoot;
	request.findText = findText;
	request.replaceText = replaceText;
	request.extensions = assetTextExtensionsFromArgs(args);
	request.includeGlobs = semicolonOrCommaList(optionValue(args, QStringLiteral("--include")));
	request.excludeGlobs = semicolonOrCommaList(optionValue(args, QStringLiteral("--exclude")));
	request.wholeWords = hasOption(args, QStringLiteral("--whole-word"));
	request.replace = replace;
	request.dryRun = replace ? !hasOption(args, QStringLiteral("--write")) || hasOption(args, QStringLiteral("--dry-run")) : true;
	request.caseSensitive = hasOption(args, QStringLiteral("--case-sensitive"));
	const AssetTextSearchReport report = findReplaceProjectText(request);
	const CliExitCode code = report.succeeded() ? CliExitCode::Success : CliExitCode::ValidationFailed;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("textSearch"), assetTextSearchReportJson(report));
		printJson(object);
	} else {
		std::cout << text(assetTextSearchReportText(report)) << "\n";
	}
	return exitCodeValue(code);
}

LevelMapLoadRequest levelMapLoadRequestFromArgs(const QString& path, const QStringList& args)
{
	LevelMapLoadRequest request;
	request.path = hasOption(args, QStringLiteral("--input")) ? optionValue(args, QStringLiteral("--input")) : path;
	request.mapName = hasOption(args, QStringLiteral("--map-name")) ? optionValue(args, QStringLiteral("--map-name")) : optionValue(args, QStringLiteral("--map"));
	request.engineHint = hasOption(args, QStringLiteral("--engine-hint")) ? optionValue(args, QStringLiteral("--engine-hint")) : optionValue(args, QStringLiteral("--engine"));
	return request;
}

CliExitCode levelMapLoadFailureCode(const LevelMapLoadRequest& request)
{
	return QFileInfo::exists(request.path) ? CliExitCode::Failure : CliExitCode::NotFound;
}

bool parseLevelMapSelector(const QString& selector, QString* objectKind, int* objectId)
{
	const QStringList parts = selector.trimmed().split(':', Qt::SkipEmptyParts);
	if (parts.size() != 2) {
		return false;
	}
	bool ok = false;
	const int parsedId = parts.value(1).toInt(&ok);
	if (!ok || parsedId < 0) {
		return false;
	}
	if (objectKind) {
		*objectKind = normalizedOptionId(parts.value(0));
	}
	if (objectId) {
		*objectId = parsedId;
	}
	return true;
}

bool parseLevelMapEntityId(const QString& value, int* entityId)
{
	QString kind;
	int id = -1;
	if (parseLevelMapSelector(value, &kind, &id)) {
		// A Doom thing is edited through the entity that mirrors it, which
		// shares its id.
		if (kind != QStringLiteral("entity") && kind != QStringLiteral("thing")) {
			return false;
		}
		if (entityId) {
			*entityId = id;
		}
		return true;
	}
	bool ok = false;
	id = value.trimmed().toInt(&ok);
	if (!ok || id < 0) {
		return false;
	}
	if (entityId) {
		*entityId = id;
	}
	return true;
}

bool parseLevelMapDelta(const QString& value, double* dx, double* dy, double* dz)
{
	const QStringList parts = value.split(QRegularExpression(QStringLiteral(R"([,\s]+)")), Qt::SkipEmptyParts);
	if (parts.size() < 2 || parts.size() > 3) {
		return false;
	}
	bool okX = false;
	bool okY = false;
	bool okZ = true;
	const double parsedX = parts.value(0).toDouble(&okX);
	const double parsedY = parts.value(1).toDouble(&okY);
	const double parsedZ = parts.size() >= 3 ? parts.value(2).toDouble(&okZ) : 0.0;
	if (!okX || !okY || !okZ) {
		return false;
	}
	if (dx) {
		*dx = parsedX;
	}
	if (dy) {
		*dy = parsedY;
	}
	if (dz) {
		*dz = parsedZ;
	}
	return true;
}

QStringList levelMapSetArguments(const QStringList& args)
{
	QStringList sets = optionValues(args, QStringLiteral("--set"));
	sets += optionValues(args, QStringLiteral("--property"));
	return sets;
}

bool splitLevelMapPropertyAssignment(const QString& assignment, QString* key, QString* value)
{
	const int equals = assignment.indexOf('=');
	if (equals <= 0) {
		return false;
	}
	const QString parsedKey = assignment.left(equals).trimmed();
	const QString parsedValue = assignment.mid(equals + 1);
	if (parsedKey.isEmpty()) {
		return false;
	}
	if (key) {
		*key = parsedKey;
	}
	if (value) {
		*value = parsedValue;
	}
	return true;
}

int runMapUdmfCommand(const QString& action, const QString& path, const QStringList& args, CliOutputFormat format)
{
	const QString command = QStringLiteral("map ") + action;
	const auto request = levelMapLoadRequestFromArgs(path, args);
	const bool editing = action == QStringLiteral("edit-udmf");
	const auto output = optionValue(args, QStringLiteral("--output"));
	if (path.isEmpty() || (editing && output.trimmed().isEmpty())) {
		return printCliError(command, CliExitCode::Usage, QStringLiteral("Provide a UDMF WAD path and --output when editing."), format);
	}
	LevelMapDocument document; QString error;
	if (!loadLevelMap(request, &document, &error)) { return printCliError(command, levelMapLoadFailureCode(request), error, format); }
	if (!document.doomUdmf) { return printCliError(command, CliExitCode::Usage, QStringLiteral("The selected map is not UDMF."), format); }
	auto result = cliResultJson(command);
	if (editing) {
		const auto selector = optionValue(args, QStringLiteral("--object"));
		QVector<LevelUdmfPropertyEdit> edits;
		for (const auto& assignment : optionValues(args, QStringLiteral("--set"))) {
			QString key, literal;
			if (!splitLevelMapPropertyAssignment(assignment, &key, &literal)) {
				return printCliError(command, CliExitCode::Usage, QStringLiteral("Each --set requires key=literal."), format);
			}
			edits << LevelUdmfPropertyEdit{selector, key, literal, false};
		}
		for (const auto& key : optionValues(args, QStringLiteral("--remove"))) { edits << LevelUdmfPropertyEdit{selector, key, {}, true}; }
		if (edits.isEmpty() || selector.isEmpty()) {
			return printCliError(command, CliExitCode::Usage, QStringLiteral("Provide --object global|type:index and at least one --set or --remove."), format);
		}
		if (!editLevelMapUdmfProperties(&document, edits, &error)) { return printCliError(command, CliExitCode::ValidationFailed, error, format); }
		const auto save = saveLevelMapAs(document, output, hasOption(args, QStringLiteral("--dry-run")), hasOption(args, QStringLiteral("--overwrite")));
		const auto code = save.succeeded() ? CliExitCode::Success : CliExitCode::ValidationFailed;
		result = cliResultJson(command, code);
		result.insert(QStringLiteral("save"), levelMapSaveReportJson(save));
		result.insert(QStringLiteral("map"), levelMapDocumentJson(document));
		if (format == CliOutputFormat::Json) { printJson(result); } else { std::cout << text(levelMapSaveReportText(save)) << '\n'; }
		return exitCodeValue(code);
	}
	result.insert(QStringLiteral("udmf"), levelUdmfDocumentJson(*document.doomUdmf));
	if (format == CliOutputFormat::Json) { printJson(result); }
	else {
		std::cout << "namespace = " << text(document.doomUdmf->nameSpace) << '\n';
		const auto fields = [](const auto& props) { for (const auto& p : props) { std::cout << "  " << text(p.name) << " = " << text(p.literal) << '\n'; } };
		std::cout << "global\n"; fields(document.doomUdmf->globals);
		for (const auto& block : document.doomUdmf->blocks) { std::cout << text(block.selector()) << '\n'; fields(block.properties); }
	}
	return 0;
}

int runMapInspectCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format)
{
	const LevelMapLoadRequest request = levelMapLoadRequestFromArgs(path, args);
	if (request.path.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a Doom WAD or Quake-family .map path.").arg(commandName), format);
	}
	LevelMapDocument document;
	QString error;
	if (!loadLevelMap(request, &document, &error)) {
		return printCliError(commandName, levelMapLoadFailureCode(request), QStringLiteral("Unable to load map: %1").arg(error), format);
	}
	const QString selector = optionValue(args, QStringLiteral("--select"));
	if (!selector.trimmed().isEmpty() && !selectLevelMapObject(&document, selector, &error)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unable to select map object: %1").arg(error), format);
	}
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("map"), levelMapDocumentJson(document));
		printJson(object);
	} else {
		std::cout << text(levelMapReportText(document)) << "\n";
	}
	return exitCodeValue(CliExitCode::Success);
}

int runMapEditWhereCommand(const QString& commandName, const LevelMapLoadRequest& request, const QStringList& args, CliOutputFormat format)
{
	const QString where = optionValue(args, QStringLiteral("--where")).trimmed();
	if (where.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1: --where takes a query, such as class=light.").arg(commandName), format);
	}
	const QStringList assignments = levelMapSetArguments(args);
	if (assignments.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires at least one --set key=value assignment.").arg(commandName), format);
	}
	const QString outputPath = optionValue(args, QStringLiteral("--output"));
	if (outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --output <save-as path>.").arg(commandName), format);
	}
	LevelMapDocument document;
	QString error;
	if (!loadLevelMap(request, &document, &error)) {
		return printCliError(commandName, levelMapLoadFailureCode(request), QStringLiteral("Unable to load map: %1").arg(error), format);
	}
	// Entities on a Quake-family map, things (through their mirrors) on a
	// Doom map; brushes, lines, and sectors have no keys to set.
	const QString prefix = document.format == LevelMapFormat::DoomWad ? QStringLiteral("thing:") : QStringLiteral("entity:");
	QVector<int> ids;
	QStringList edited;
	for (const QString& selector : levelMapObjectsMatchingQuery(document, where)) {
		if (selector.startsWith(prefix)) {
			ids << selector.mid(prefix.size()).toInt();
			edited << selector;
		}
	}
	if (ids.isEmpty()) {
		return printCliError(commandName, CliExitCode::Failure,
			QStringLiteral("%1: no %2 matches --where %3.").arg(commandName, document.format == LevelMapFormat::DoomWad ? QStringLiteral("thing") : QStringLiteral("entity"), where), format);
	}
	QStringList keys;
	for (const QString& assignment : assignments) {
		QString key;
		QString value;
		if (!splitLevelMapPropertyAssignment(assignment, &key, &value)) {
			return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Map property assignment must be formatted as key=value: %1").arg(assignment), format);
		}
		if (!setLevelMapEntitiesProperty(&document, ids, key, {value}, &error)) {
			return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to set %1: %2").arg(key, error), format);
		}
		keys << key;
	}
	const LevelMapSaveReport report = saveLevelMapAs(document, outputPath, hasOption(args, QStringLiteral("--dry-run")), hasOption(args, QStringLiteral("--overwrite")));
	const CliExitCode code = report.succeeded() ? CliExitCode::Success : CliExitCode::ValidationFailed;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("where"), where);
		object.insert(QStringLiteral("edited"), QJsonArray::fromStringList(edited));
		object.insert(QStringLiteral("keys"), QJsonArray::fromStringList(keys));
		object.insert(QStringLiteral("save"), levelMapSaveReportJson(report));
		printJson(object);
	} else {
		std::cout << text(QStringLiteral("Set %1 on %2: %3").arg(keys.join(QStringLiteral(", "))).arg(edited.size() == 1 ? QStringLiteral("1 object") : QStringLiteral("%1 objects").arg(edited.size())).arg(edited.join(QStringLiteral(", ")))) << "\n";
		std::cout << text(levelMapSaveReportText(report)) << "\n";
	}
	return exitCodeValue(code);
}

int runMapEditCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format)
{
	const LevelMapLoadRequest request = levelMapLoadRequestFromArgs(path, args);
	if (request.path.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a Doom WAD or Quake-family .map path.").arg(commandName), format);
	}
	// --where QUERY: every entity, or Doom thing, the query keeps takes each
	// --set, as the Levels inspector sets a key on several selected entities.
	if (hasOption(args, QStringLiteral("--where"))) {
		return runMapEditWhereCommand(commandName, request, args, format);
	}
	// An entity's keys by --entity N or --select entity:N; on a Doom map a
	// sector's, linedef's, or sidedef's fields by --select kind:N.
	const QString entityValue = hasOption(args, QStringLiteral("--entity")) ? optionValue(args, QStringLiteral("--entity")) : optionValue(args, QStringLiteral("--select"));
	QString recordKind = QStringLiteral("entity");
	int entityId = -1;
	for (const QString& kind : {QStringLiteral("sector"), QStringLiteral("linedef"), QStringLiteral("sidedef"), QStringLiteral("brush")}) {
		const QString prefix = kind + QLatin1Char(':');
		if (entityValue.trimmed().startsWith(prefix, Qt::CaseInsensitive)) {
			bool ok = false;
			entityId = entityValue.trimmed().mid(prefix.size()).toInt(&ok);
			if (!ok || entityId < 0) {
				return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1: --select %2:<id> takes a whole number.").arg(commandName, kind), format);
			}
			recordKind = kind;
		}
	}
	if (recordKind == QStringLiteral("entity") && !parseLevelMapEntityId(entityValue, &entityId)) {
		return printCliError(commandName, CliExitCode::Usage,
			QStringLiteral("%1 requires --entity <id>, or --select entity:<id>, thing:<id>, brush:<id>, sector:<id>, linedef:<id>, or sidedef:<id>.").arg(commandName), format);
	}
	const QStringList assignments = levelMapSetArguments(args);
	if (assignments.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires at least one --set key=value assignment.").arg(commandName), format);
	}
	const QString outputPath = optionValue(args, QStringLiteral("--output"));
	if (outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --output <save-as path>.").arg(commandName), format);
	}

	LevelMapDocument document;
	QString error;
	if (!loadLevelMap(request, &document, &error)) {
		return printCliError(commandName, levelMapLoadFailureCode(request), QStringLiteral("Unable to load map: %1").arg(error), format);
	}
	// thing:N names a Doom thing; on a Quake-family map it would quietly be
	// entity N instead.
	if (entityValue.trimmed().startsWith(QStringLiteral("thing:"), Qt::CaseInsensitive) && document.format != LevelMapFormat::DoomWad) {
		return printCliError(commandName, CliExitCode::Usage,
			QStringLiteral("%1: thing:<id> names a Doom thing; use entity:<id> on a Quake-family map.").arg(commandName), format);
	}
	if (recordKind == QStringLiteral("entity") && !selectLevelMapObject(&document, QStringLiteral("entity:%1").arg(entityId), &error)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unable to select entity: %1").arg(error), format);
	}
	for (const QString& assignment : assignments) {
		QString key;
		QString value;
		if (!splitLevelMapPropertyAssignment(assignment, &key, &value)) {
			return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Map property assignment must be formatted as key=value: %1").arg(assignment), format);
		}
		bool edited = false;
		if (recordKind == QStringLiteral("sector")) {
			edited = setLevelMapSectorProperty(&document, entityId, key, value, &error);
		} else if (recordKind == QStringLiteral("linedef")) {
			edited = setLevelMapLinedefProperty(&document, entityId, key, value, &error);
		} else if (recordKind == QStringLiteral("sidedef")) {
			edited = setLevelMapSidedefProperty(&document, entityId, key, value, &error);
		} else if (recordKind == QStringLiteral("brush")) {
			// faceN.field, faces counted from 1 as the Inspector counts them.
			const qsizetype dot = key.indexOf(QLatin1Char('.'));
			bool numbered = false;
			const int face = dot > 4 && key.startsWith(QStringLiteral("face"), Qt::CaseInsensitive) ? key.mid(4, dot - 4).toInt(&numbered) : 0;
			if (!numbered || face < 1) {
				return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1: a brush field is faceN.field, such as face2.shiftx=16.").arg(commandName), format);
			}
			edited = setLevelMapBrushFaceProperty(&document, entityId, face - 1, key.mid(dot + 1), value, &error);
		} else {
			edited = setLevelMapEntityProperty(&document, entityId, key, value, &error);
		}
		if (!edited) {
			return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to edit %1 property: %2").arg(recordKind, error), format);
		}
	}
	// The record edited is the one the report describes.
	if (recordKind != QStringLiteral("entity") && recordKind != QStringLiteral("sidedef")) {
		QString selectError;
		selectLevelMapObject(&document, QStringLiteral("%1:%2").arg(recordKind).arg(entityId), &selectError);
	}
	const LevelMapSaveReport report = saveLevelMapAs(document, outputPath, hasOption(args, QStringLiteral("--dry-run")), hasOption(args, QStringLiteral("--overwrite")));
	const CliExitCode code = report.succeeded() ? CliExitCode::Success : CliExitCode::ValidationFailed;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("map"), levelMapDocumentJson(document));
		object.insert(QStringLiteral("save"), levelMapSaveReportJson(report));
		printJson(object);
	} else {
		std::cout << text(levelMapSaveReportText(report)) << "\n";
		std::cout << text(levelMapReportText(document)) << "\n";
	}
	return exitCodeValue(code);
}

bool mapTextureLockFromArgs(const QStringList& args, bool enabledByDefault, LevelMapTextureLockOptions* textures, QString* error)
{
	textures->enabled = enabledByDefault;
	textures->allowValve220 = hasOption(args, QStringLiteral("--allow-valve220"));
	if (hasOption(args, QStringLiteral("--texture-lock"))) {
		const auto values = optionValues(args, QStringLiteral("--texture-lock"));
		const auto value = optionValue(args, QStringLiteral("--texture-lock")).toLower();
		if (values.size() != 1 || (value != QStringLiteral("on") && value != QStringLiteral("off"))) {
			*error = QStringLiteral("--texture-lock takes on or off exactly once.");
			return false;
		}
		textures->enabled = value == QStringLiteral("on");
	}
	return true;
}

int runMapMoveCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format)
{
	LevelMapTextureLockOptions textures;
	QString textureError;
	if (!mapTextureLockFromArgs(args, true, &textures, &textureError)) {
		return printCliError(commandName, CliExitCode::Usage, textureError, format);
	}
	const LevelMapLoadRequest request = levelMapLoadRequestFromArgs(path, args);
	if (request.path.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a Doom WAD or Quake-family .map path.").arg(commandName), format);
	}
	const QString selector = hasOption(args, QStringLiteral("--object")) ? optionValue(args, QStringLiteral("--object")) : optionValue(args, QStringLiteral("--select"));
	QString objectKind;
	int objectId = -1;
	if (!parseLevelMapSelector(selector, &objectKind, &objectId)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --object kind:id.").arg(commandName), format);
	}
	double dx = 0.0;
	double dy = 0.0;
	double dz = 0.0;
	if (!parseLevelMapDelta(optionValue(args, QStringLiteral("--delta")), &dx, &dy, &dz)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --delta x,y,z.").arg(commandName), format);
	}
	const QString outputPath = optionValue(args, QStringLiteral("--output"));
	if (outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --output <save-as path>.").arg(commandName), format);
	}

	LevelMapDocument document;
	QString error;
	if (!loadLevelMap(request, &document, &error)) {
		return printCliError(commandName, levelMapLoadFailureCode(request), QStringLiteral("Unable to load map: %1").arg(error), format);
	}
	if (!moveLevelMapObject(&document, objectKind, objectId, dx, dy, dz, textures, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to move map object: %1").arg(error), format);
	}
	const LevelMapSaveReport report = saveLevelMapAs(document, outputPath, hasOption(args, QStringLiteral("--dry-run")), hasOption(args, QStringLiteral("--overwrite")));
	const CliExitCode code = report.succeeded() ? CliExitCode::Success : CliExitCode::ValidationFailed;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
	object.insert(QStringLiteral("textureLockPolicy"), textures.enabled ? QStringLiteral("locked") : QStringLiteral("source-parameters"));
	object.insert(QStringLiteral("allowValve220"), textures.allowValve220);
		object.insert(QStringLiteral("map"), levelMapDocumentJson(document));
		object.insert(QStringLiteral("save"), levelMapSaveReportJson(report));
		printJson(object);
	} else {
		std::cout << text(levelMapSaveReportText(report)) << "\n";
		std::cout << text(levelMapReportText(document)) << "\n";
	}
	return exitCodeValue(code);
}

int printLevelMapSaveResult(const QString& commandName, const LevelMapDocument& document, const LevelMapSaveReport& report,
	CliOutputFormat format, const QJsonObject& extra = {})
{
	const CliExitCode code = report.succeeded() ? CliExitCode::Success : CliExitCode::ValidationFailed;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		for (auto it = extra.constBegin(); it != extra.constEnd(); ++it) {
			object.insert(it.key(), it.value());
		}
		object.insert(QStringLiteral("map"), levelMapDocumentJson(document));
		object.insert(QStringLiteral("save"), levelMapSaveReportJson(report));
		printJson(object);
	} else {
		std::cout << text(levelMapSaveReportText(report)) << "\n";
		std::cout << text(levelMapReportText(document)) << "\n";
	}
	return exitCodeValue(code);
}

int runMapNewCommand(const QStringList& args, CliOutputFormat format)
{
	const QString command = QStringLiteral("map new");
	const QString output = optionValue(args, QStringLiteral("--output"));
	const QString preset = hasOption(args, QStringLiteral("--preset")) ? optionValue(args, QStringLiteral("--preset")) : QStringLiteral("room");
	if (output.isEmpty() || (preset != QStringLiteral("room") && preset != QStringLiteral("empty"))) {
		return printCliError(command, CliExitCode::Usage, QStringLiteral("map new requires --output <path> and accepts --preset room|empty."), format);
	}
	LevelMapCreateRequest request;
	if (hasOption(args, QStringLiteral("--game"))) { request.game = optionValue(args, QStringLiteral("--game")); }
	request.name = QFileInfo(output).completeBaseName(); request.starterRoom = preset == QStringLiteral("room");
	if (hasOption(args, QStringLiteral("--map"))) { request.mapName = optionValue(args, QStringLiteral("--map")).toUpper(); }
	request.wallTexture = optionValue(args, QStringLiteral("--texture"));
	request.floorTexture = optionValue(args, QStringLiteral("--floor-texture"));
	request.ceilingTexture = optionValue(args, QStringLiteral("--ceiling-texture"));
	LevelMapDocument document; QString error;
	if (!createLevelMap(request, &document, &error)) { return printCliError(command, CliExitCode::ValidationFailed, error, format); }
	const auto report = saveLevelMapAs(document, output, hasOption(args, QStringLiteral("--dry-run")), hasOption(args, QStringLiteral("--overwrite")));
	return printLevelMapSaveResult(command, document, report, format);
}

int runMapRecoverCommand(const QString& path, const QStringList& args, CliOutputFormat format)
{
	const QString command = QStringLiteral("map recover"), output = optionValue(args, QStringLiteral("--output"));
	if (path.isEmpty() || output.isEmpty()) {
		return printCliError(command, CliExitCode::Usage, QStringLiteral("map recover requires a .vsrecovery checkpoint and --output <map path>."), format);
	}
	LevelMapDocument document; QString error;
	if (!restoreLevelMapRecovery(path, &document, &error)) { return printCliError(command, CliExitCode::ValidationFailed, error, format); }
	const auto report = saveLevelMapAs(document, output, hasOption(args, QStringLiteral("--dry-run")), hasOption(args, QStringLiteral("--overwrite")));
	return printLevelMapSaveResult(command, document, report, format);
}

int runMapRecoveriesCommand(const QString& directory, CliOutputFormat format)
{
	const QString command = QStringLiteral("map recoveries");
	if (directory.isEmpty() || !QFileInfo(directory).isDir()) {
		return printCliError(command, CliExitCode::Usage, QStringLiteral("map recoveries requires an existing recovery directory."), format);
	}
	QJsonArray records;
	for (const auto& record : listLevelMapRecoveries(directory)) {
		records.append(QJsonObject {{QStringLiteral("path"),record.path}, {QStringLiteral("sourcePath"),record.sourcePath},
			{QStringLiteral("mapName"),record.mapName}, {QStringLiteral("format"),record.format},
			{QStringLiteral("writtenUtc"),record.writtenUtc.toString(Qt::ISODateWithMs)}, {QStringLiteral("bytes"),record.payloadBytes},
			{QStringLiteral("validMetadata"),record.isValid()}, {QStringLiteral("error"),record.error}});
		if (format != CliOutputFormat::Json) {
			std::cout << text(record.path) << "\t" << text(record.mapName) << "\t" << text(record.writtenUtc.toString(Qt::ISODateWithMs)) << "\t" << text(record.error) << "\n";
		}
	}
	if (format == CliOutputFormat::Json) { auto result = cliResultJson(command); result.insert(QStringLiteral("recoveries"),records); printJson(result); }
	return exitCodeValue(CliExitCode::Success);
}

int runMapPlaceSoundCommand(const QString& path, const QStringList& args, CliOutputFormat format)
{
	const QString command = QStringLiteral("map place-sound");
	const auto usage = [&](const QString& error) { return printCliError(command, CliExitCode::Usage, error, format); };
	QMap<QString, QString> values;
	QSet<QString> flags;
	QStringList positional;
	for (int index = 1; index < args.size(); ++index) {
		const QString token = args[index];
		if (!token.startsWith(QStringLiteral("--"))) { positional.append(token); continue; }
		const qsizetype equal = token.indexOf(QLatin1Char('='));
		const QString key = equal < 0 ? token : token.left(equal);
		if (flags.contains(key) || values.contains(key)) { return usage(QStringLiteral("Repeated option: %1").arg(key)); }
		if (QStringList{QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--dry-run"), QStringLiteral("--overwrite")}.contains(key) && equal < 0) {
			flags.insert(key); continue;
		}
		if (!QStringList{QStringLiteral("--package"), QStringLiteral("--sound"), QStringLiteral("--game"), QStringLiteral("--origin"),
		                 QStringLiteral("--mode"), QStringLiteral("--targetname"), QStringLiteral("--output"),
		                 QStringLiteral("--settings-file"), QStringLiteral("--locale"), QStringLiteral("--catalog-root")}.contains(key)) {
			return usage(QStringLiteral("Unsupported sound placement option: %1").arg(token));
		}
		const QString value = equal < 0 ? (index + 1 < args.size() ? args[++index] : QString()) : token.mid(equal + 1);
		if (value.isEmpty() || value.startsWith(QStringLiteral("--"))) { return usage(QStringLiteral("%1 requires a value.").arg(key)); }
		values.insert(key, value);
	}
	const QString output = values.value(QStringLiteral("--output")), packagePath = values.value(QStringLiteral("--package"));
	if (path.isEmpty() || positional.size() != 3 || positional.value(2) != path || packagePath.isEmpty() ||
	    !output.endsWith(QStringLiteral(".map"), Qt::CaseInsensitive) || values.value(QStringLiteral("--sound")).isEmpty()) {
		return usage(QStringLiteral("map place-sound requires one map, --package <archive, folder or .vibepackage>, --sound sound/name.wav, --origin x,y,z and --output <separate .map>."));
	}
	LevelSoundRequest placement;
	placement.game = values.value(QStringLiteral("--game"));
	placement.virtualPath = values.value(QStringLiteral("--sound"));
	placement.mode = values.value(QStringLiteral("--mode"), QStringLiteral("loop-on"));
	placement.targetName = values.value(QStringLiteral("--targetname"));
	placement.origin.valid = parseLevelMapDelta(values.value(QStringLiteral("--origin")), &placement.origin.x, &placement.origin.y, &placement.origin.z);
	if (!placement.origin.valid) { return usage(QStringLiteral("--origin requires three finite coordinates: x,y,z.")); }
	LevelMapDocument document;
	QString error;
	const auto load = levelMapLoadRequestFromArgs(path, args);
	if (!loadLevelMap(load, &document, &error)) { return printCliError(command, levelMapLoadFailureCode(load), error, format); }
	const auto plan = planLevelSound(levelSoundTarget(document), placement);
	if (!plan.valid()) { return printCliError(command, CliExitCode::ValidationFailed, plan.error, format); }
	PackageArchive archive;
	if (!loadPackageForCliQuiet(packagePath, &archive, &error)) { return printCliError(command, CliExitCode::Failure, error, format); }
	if (audioPathsReferToSameFile(output, path) || audioPathsReferToSameFile(output, packagePath) || archive.protectsInputPath(output)) {
		return printCliError(command, CliExitCode::ValidationFailed, QStringLiteral("Choose a separate map output; map and package inputs are protected."), format);
	}
	int matches = 0;
	bool exact = false;
	quint64 bytes = 0;
	for (const auto& entry : archive.entries()) {
		if (entry.kind == PackageEntryKind::File && entry.virtualPath.compare(placement.virtualPath, Qt::CaseInsensitive) == 0) {
			++matches; exact |= entry.virtualPath == placement.virtualPath; bytes = entry.sizeBytes;
		}
	}
	if (matches != 1 || !exact || bytes > AudioInputByteLimit) {
		return printCliError(command, CliExitCode::ValidationFailed, QStringLiteral("The sound must resolve to one exact-case package file within the audio size limit."), format);
	}
	QByteArray wav;
	if (!archive.readEntryBytes(placement.virtualPath, &wav, &error, AudioInputByteLimit + 1)) {
		return printCliError(command, CliExitCode::Failure, error, format);
	}
	int entity = -1;
	if (!placeLevelSound(&document, placement, wav, &entity, &error)) { return printCliError(command, CliExitCode::ValidationFailed, error, format); }
	const auto report = saveLevelMapAs(document, output, flags.contains(QStringLiteral("--dry-run")), flags.contains(QStringLiteral("--overwrite")));
	QJsonObject properties;
	for (const auto& property : plan.properties) { properties.insert(property.key, property.value); }
	QJsonObject sound{{QStringLiteral("game"), plan.game}, {QStringLiteral("className"), plan.className},
	                  {QStringLiteral("packagePath"), packagePath}, {QStringLiteral("virtualPath"), placement.virtualPath},
	                  {QStringLiteral("reference"), plan.soundReference}, {QStringLiteral("mode"), placement.mode},
	                  {QStringLiteral("origin"), QJsonArray{placement.origin.x, placement.origin.y, placement.origin.z}},
	                  {QStringLiteral("properties"), properties}, {QStringLiteral("bytes"), wav.size()}};
	if (format != CliOutputFormat::Json) { std::cout << text(QStringLiteral("Sound %1: %2, entity:%3").arg(plan.game, placement.virtualPath).arg(entity)) << "\n"; }
	return printLevelMapSaveResult(command, document, report, format,
	                              {{QStringLiteral("entityId"), entity}, {QStringLiteral("selector"), QStringLiteral("entity:%1").arg(entity)}, {QStringLiteral("sound"), sound}});
}

int runMapAddEntityCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format)
{
	const LevelMapLoadRequest request = levelMapLoadRequestFromArgs(path, args);
	if (request.path.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a Quake-family .map path.").arg(commandName), format);
	}
	const QString className = optionValue(args, QStringLiteral("--class")).trimmed();
	if (className.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --class <classname>.").arg(commandName), format);
	}
	double x = 0.0;
	double y = 0.0;
	double z = 0.0;
	if (!parseLevelMapDelta(optionValue(args, QStringLiteral("--origin")), &x, &y, &z)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --origin x,y,z.").arg(commandName), format);
	}
	QVector<LevelMapProperty> properties;
	for (const QString& assignment : levelMapSetArguments(args)) {
		QString key;
		QString value;
		if (!splitLevelMapPropertyAssignment(assignment, &key, &value)) {
			return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Map property assignment must be formatted as key=value: %1").arg(assignment), format);
		}
		properties.push_back({key, value, 0});
	}
	const QString outputPath = optionValue(args, QStringLiteral("--output"));
	if (outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --output <save-as path>.").arg(commandName), format);
	}

	LevelMapDocument document;
	QString error;
	if (!loadLevelMap(request, &document, &error)) {
		return printCliError(commandName, levelMapLoadFailureCode(request), QStringLiteral("Unable to load map: %1").arg(error), format);
	}
	int entityId = -1;
	if (!addLevelMapEntity(&document, className, {x, y, z, true}, properties, &entityId, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to add entity: %1").arg(error), format);
	}
	const LevelMapSaveReport report = saveLevelMapAs(document, outputPath, hasOption(args, QStringLiteral("--dry-run")), hasOption(args, QStringLiteral("--overwrite")));
	if (format != CliOutputFormat::Json) {
		std::cout << text(QStringLiteral("Added entity:%1 (%2)").arg(entityId).arg(className)) << "\n";
	}
	QJsonObject extra;
	extra.insert(QStringLiteral("entityId"), entityId);
	extra.insert(QStringLiteral("selector"), QStringLiteral("entity:%1").arg(entityId));
	return printLevelMapSaveResult(commandName, document, report, format, extra);
}

// Reads repeatable --object (or --select) options, each of which may list
// several kind:id selectors. False names the first selector it cannot read.
bool levelMapObjectsFromArgs(const QStringList& args, QVector<LevelMapSelectionRef>* objects, QString* badSelector)
{
	for (const QString& value : optionValues(args, QStringLiteral("--object")) + optionValues(args, QStringLiteral("--select"))) {
		for (const QString& selector : value.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
			QString kind;
			int objectId = -1;
			const LevelMapSelectionKind selectionKind = parseLevelMapSelector(selector, &kind, &objectId)
				? levelMapSelectionKindFromId(kind)
				: LevelMapSelectionKind::None;
			if (selectionKind == LevelMapSelectionKind::None) {
				*badSelector = selector.trimmed();
				return false;
			}
			// Named twice, an object counts once, where it was named last, as
			// picking it again in the view moves it to the end.
			const LevelMapSelectionRef ref {selectionKind, objectId};
			objects->removeAll(ref);
			objects->push_back(ref);
		}
	}
	return true;
}

int runMapPrefabCommand(const QString& action, const QString& path, const QStringList& args, CliOutputFormat format)
{
	const QString command = QStringLiteral("map ") + action;
	const bool capture = action == QStringLiteral("export-prefab"), inspect = action == QStringLiteral("inspect-prefab");
	const auto usage = [&](const QString& message) { return printCliError(command, CliExitCode::Usage, message, format); };
	QSet<QString> values{QStringLiteral("--input"), QStringLiteral("--package"), QStringLiteral("--settings-file"), QStringLiteral("--locale"), QStringLiteral("--catalog-root")};
	QSet<QString> flags{QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--quiet"), QStringLiteral("--verbose")};
	if (!inspect) {
		values += QSet<QString>{QStringLiteral("--output"), QStringLiteral("--engine")};
		flags += QSet<QString>{QStringLiteral("--overwrite"), QStringLiteral("--dry-run")};
	}
	if (capture) { values += QSet<QString>{QStringLiteral("--object"), QStringLiteral("--name"), QStringLiteral("--description"), QStringLiteral("--anchor")}; }
	else {
		values.insert(QStringLiteral("--entry"));
		if (!inspect) { values += QSet<QString>{QStringLiteral("--prefab"), QStringLiteral("--position"), QStringLiteral("--rotation"), QStringLiteral("--target-prefix"), QStringLiteral("--texture-lock")}; }
	}
	QSet<QString> seen;
	QStringList positional;
	for (qsizetype i = 1; i < args.size(); ++i) {
		const auto argument = args[i];
		if (!argument.startsWith('-')) { positional << argument; continue; }
		const auto equals = argument.indexOf('=');
		const auto name = equals < 0 ? argument : argument.left(equals);
		if (seen.contains(name) && name != QStringLiteral("--object")) { return usage(QStringLiteral("Repeated option: %1").arg(name)); }
		if (flags.contains(name)) {
			if (equals >= 0) { return usage(QStringLiteral("Flag %1 does not take a value.").arg(name)); }
		} else if (values.contains(name)) {
			QString value;
			if (equals >= 0) { value = argument.mid(equals + 1); }
			else {
				if (i + 1 == args.size() || args[i + 1].startsWith(QStringLiteral("--"))) { return usage(QStringLiteral("Missing value for %1.").arg(name)); }
				value = args[++i];
			}
			if (value.trimmed().isEmpty() && name != QStringLiteral("--description") && name != QStringLiteral("--target-prefix")) { return usage(QStringLiteral("Empty value for %1.").arg(name)); }
		} else { return usage(QStringLiteral("Unknown option for %1: %2").arg(command, name)); }
		seen.insert(name);
	}
	if (path.isEmpty() || positional.size() != (seen.contains(QStringLiteral("--input")) ? 2 : 3)) { return usage(QStringLiteral("%1 requires exactly one input path, positional or --input.").arg(command)); }
	const auto output = optionValue(args, QStringLiteral("--output"));
	if (!inspect && output.trimmed().isEmpty()) { return usage(QStringLiteral("%1 requires --output <save-as path>.").arg(command)); }
	if (capture && QFileInfo(output).suffix().compare(QStringLiteral("vprefab"), Qt::CaseInsensitive) != 0) { return usage(QStringLiteral("Prefab outputs use the .vprefab extension.")); }
	LevelPrefabCreateRequest request;
	LevelPrefabPlacement placement;
	for (const auto &option : {QStringLiteral("--anchor"), QStringLiteral("--position"), QStringLiteral("--rotation")}) {
		if (!seen.contains(option)) { continue; }
		LevelMapVec3 *point = option == QStringLiteral("--anchor") ? &request.anchor : option == QStringLiteral("--position") ? &placement.position : &placement.rotation;
		point->valid = parseLevelMapDelta(optionValue(args, option), &point->x, &point->y, &point->z);
		if (!point->valid || !std::isfinite(point->x) || !std::isfinite(point->y) || !std::isfinite(point->z)) { return usage(QStringLiteral("%1 requires three finite coordinates: x,y,z.").arg(option)); }
	}
	if (!capture && !inspect && !seen.contains(QStringLiteral("--position"))) { return usage(QStringLiteral("Specify --position x,y,z for the placed anchor.")); }
	LevelMapTextureLockOptions textures;
	QString error;
	if (!mapTextureLockFromArgs(args,true,&textures,&error)) { return usage(error); }
	placement.textureLock = textures.enabled;
	placement.targetPrefix = optionValue(args,QStringLiteral("--target-prefix"));
	LevelPrefab prefab;
	LevelPrefabReport report;
	LevelMapDocument document, definition;
	PackageArchive assets;
	bool hasAssets = false;
	if (!inspect) {
		const auto load = levelMapLoadRequestFromArgs(path,args);
		if (!loadLevelMap(load,&document,&error)) { return printCliError(command,levelMapLoadFailureCode(load),error,format); }
	}
	if (capture) {
		QVector<LevelMapSelectionRef> selection;
		if (!levelMapObjectsFromArgs(args,&selection,&error) || selection.isEmpty() || !setLevelMapSelection(&document,selection,&error)) { return usage(QStringLiteral("Select existing objects with repeated --object kind:id. %1").arg(error)); }
		request.name = optionValue(args,QStringLiteral("--name"));
		request.description = optionValue(args,QStringLiteral("--description"));
		if (request.name.trimmed().isEmpty()) { return usage(QStringLiteral("Specify --name for the prefab.")); }
		if (!createLevelPrefab(document,request,&prefab,&report,&error)) { return printCliError(command,CliExitCode::ValidationFailed,error,format); }
	} else {
		const auto prefabPath = inspect ? path : optionValue(args,QStringLiteral("--prefab"));
		if (prefabPath.trimmed().isEmpty()) { return usage(QStringLiteral("Specify --prefab <file-or-package>.")); }
		if (seen.contains(QStringLiteral("--entry"))) {
			QByteArray bytes;
			if (!assets.load(prefabPath,&error) || !assets.readEntryBytes(optionValue(args,QStringLiteral("--entry")),&bytes,&error,levelPrefabByteLimit) || !parseLevelPrefab(bytes,&prefab,&error)) { return printCliError(command,CliExitCode::ValidationFailed,error,format); }
			hasAssets = true;
		} else if (!readLevelPrefab(prefabPath,&prefab,&error)) { return printCliError(command,QFileInfo::exists(prefabPath) ? CliExitCode::ValidationFailed : CliExitCode::NotFound,error,format); }
		if (!inspect && !insertLevelPrefab(&document,prefab,placement,&report,&error)) { return printCliError(command,CliExitCode::ValidationFailed,error,format); }
	}
	LevelPrefabReport inspected;
	if (!inspectLevelPrefab(prefab,&definition,&inspected,&error)) { return printCliError(command,CliExitCode::ValidationFailed,error,format); }
	if (inspect) { report = inspected; }
	if (seen.contains(QStringLiteral("--package"))) {
		if (!assets.load(optionValue(args,QStringLiteral("--package")),&error)) { return printCliError(command,CliExitCode::ValidationFailed,error,format); }
		hasAssets = true;
	}
	QJsonObject details = levelPrefabReportJson(report);
	details.insert(QStringLiteral("name"),prefab.name);
	details.insert(QStringLiteral("description"),prefab.description);
	details.insert(QStringLiteral("engineFamily"),prefab.engineFamily);
	details.insert(QStringLiteral("anchor"),QJsonArray{prefab.anchor.x,prefab.anchor.y,prefab.anchor.z});
	QString dependencyText;
	if (hasAssets) {
		const auto dependencies = inspectLevelDependencies(definition,assets);
		details.insert(QStringLiteral("dependencies"),levelDependencyReportJson(dependencies));
		dependencyText = levelDependencyReportText(dependencies);
	}
	if (!capture && !inspect) {
		details.insert(QStringLiteral("position"),QJsonArray{placement.position.x,placement.position.y,placement.position.z});
		details.insert(QStringLiteral("rotation"),QJsonArray{placement.rotation.x,placement.rotation.y,placement.rotation.z});
		details.insert(QStringLiteral("textureLock"),placement.textureLock);
		const auto saved = saveLevelMapAs(document,output,hasOption(args,QStringLiteral("--dry-run")),hasOption(args,QStringLiteral("--overwrite")));
		if (format != CliOutputFormat::Json) { std::cout << text(report.warnings.join('\n')) << '\n' << text(dependencyText); }
		return printLevelMapSaveResult(command,document,saved,format,{{QStringLiteral("prefab"),details}});
	}
	LevelPrefabWriteReport written;
	if (capture && !writeLevelPrefab(prefab,output,hasOption(args,QStringLiteral("--overwrite")),hasOption(args,QStringLiteral("--dry-run")),&written,&error)) { return printCliError(command,CliExitCode::ValidationFailed,error,format); }
	if (format == CliOutputFormat::Json) {
		auto result = cliResultJson(command);
		result.insert(QStringLiteral("prefab"),details);
		if (capture) { result.insert(QStringLiteral("save"),QJsonObject{{QStringLiteral("path"),written.path},{QStringLiteral("written"),written.committed},{QStringLiteral("dryRun"),written.dryRun},{QStringLiteral("backupPath"),written.backupPath},{QStringLiteral("warnings"),QJsonArray::fromStringList(written.warnings)},{QStringLiteral("recoveryPaths"),QJsonArray::fromStringList(written.recoveryPaths)}}); }
		printJson(result);
	} else {
		std::cout << text(QStringLiteral("%1: %2 brushes, %3 patches, %4 entities\n").arg(prefab.name).arg(report.statistics.brushCount).arg(report.statistics.patchCount).arg(report.statistics.entityCount-1));
		std::cout << text(report.warnings.join('\n')) << '\n' << text(dependencyText);
		if (capture) {
			std::cout << text(written.dryRun ? QStringLiteral("Dry run: ") : QStringLiteral("Saved: ")) << text(written.path) << '\n';
			if (!written.backupPath.isEmpty()) { std::cout << "Backup: " << text(written.backupPath) << '\n'; }
			std::cout << text(written.warnings.join('\n')) << '\n' << text(written.recoveryPaths.join('\n')) << '\n';
		}
	}
	return exitCodeValue(CliExitCode::Success);
}

int runMapAddBrushCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format)
{
	LevelBrushPrimitiveRequest primitive;
	if (hasOption(args, QStringLiteral("--shape"))) { primitive.shape = optionValue(args, QStringLiteral("--shape")); }
	if (hasOption(args, QStringLiteral("--axis"))) {
		primitive.axis = QStringList{QStringLiteral("x"),QStringLiteral("y"),QStringLiteral("z")}
			.indexOf(optionValue(args, QStringLiteral("--axis")).toLower());
		if (primitive.axis < 0) { return printCliError(commandName, CliExitCode::Usage, QStringLiteral("--axis takes x, y or z."), format); }
	}
	for (const auto& option : {QStringLiteral("--sides"), QStringLiteral("--bands")}) {
		if (!hasOption(args, option)) { continue; }
		bool valid = false;
		const int value = optionValue(args, option).toInt(&valid);
		if (!valid) { return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 takes a whole number.").arg(option), format); }
		if (option == QStringLiteral("--sides")) { primitive.sides = value; } else { primitive.bands = value; }
	}
	if ((hasOption(args, QStringLiteral("--bands")) && primitive.shape != QStringLiteral("sphere"))
		|| (hasOption(args, QStringLiteral("--sides")) && (primitive.shape == QStringLiteral("box") || primitive.shape == QStringLiteral("wedge")))) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("--bands applies to spheres; --sides applies to cylinders, cones and spheres."), format);
	}
	const LevelMapLoadRequest request = levelMapLoadRequestFromArgs(path, args);
	if (request.path.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a Quake-family .map path.").arg(commandName), format);
	}
	LevelMapVec3 mins {0.0, 0.0, 0.0, true};
	LevelMapVec3 maxs {0.0, 0.0, 0.0, true};
	if (!parseLevelMapDelta(optionValue(args, QStringLiteral("--mins")), &mins.x, &mins.y, &mins.z)
		|| !parseLevelMapDelta(optionValue(args, QStringLiteral("--maxs")), &maxs.x, &maxs.y, &maxs.z)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --mins x,y,z and --maxs x,y,z.").arg(commandName), format);
	}
	const QString texture = optionValue(args, QStringLiteral("--texture")).trimmed();
	if (texture.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --texture <name>.").arg(commandName), format);
	}
	const QString outputPath = optionValue(args, QStringLiteral("--output"));
	if (outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --output <save-as path>.").arg(commandName), format);
	}

	LevelMapDocument document;
	QString error;
	if (!loadLevelMap(request, &document, &error)) {
		return printCliError(commandName, levelMapLoadFailureCode(request), QStringLiteral("Unable to load map: %1").arg(error), format);
	}
	int brushId = -1;
	primitive.mins = mins;
	primitive.maxs = maxs;
	primitive.texture = texture;
	if (!addLevelMapBrushPrimitive(&document, primitive, &brushId, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to add brush: %1").arg(error), format);
	}
	const LevelMapSaveReport report = saveLevelMapAs(document, outputPath, hasOption(args, QStringLiteral("--dry-run")), hasOption(args, QStringLiteral("--overwrite")));
	if (format != CliOutputFormat::Json) {
		std::cout << text(QStringLiteral("Added brush:%1").arg(brushId)) << "\n";
	}
	QJsonObject extra;
	extra.insert(QStringLiteral("brushId"), brushId);
	extra.insert(QStringLiteral("selector"), QStringLiteral("brush:%1").arg(brushId));
	extra.insert(QStringLiteral("shape"), primitive.shape);
	extra.insert(QStringLiteral("faceCount"), document.brushes.last().faceCount);
	extra.insert(QStringLiteral("faceDialect"), document.brushes.last().primitiveKind);
	return printLevelMapSaveResult(commandName, document, report, format, extra);
}

int runMapBrushComponentsCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format, bool editing)
{
	const auto usage = [&](const QString& error) { return printCliError(commandName,CliExitCode::Usage,error,format); };
	bool idOk = false; const int id = optionValue(args,QStringLiteral("--brush")).toInt(&idOk);
	if (path.isEmpty() || !idOk || id < 0) { return usage(QStringLiteral("A map path and --brush <non-negative ID> are required.")); }
	LevelMapDocument document; QString error; const auto request = levelMapLoadRequestFromArgs(path,args);
	if (!loadLevelMap(request,&document,&error)) { return printCliError(commandName,levelMapLoadFailureCode(request),error,format); }
	const auto found = std::find_if(document.brushes.cbegin(),document.brushes.cend(),[&](const auto& b) { return b.id == id; });
	if (found == document.brushes.cend()) { return usage(QStringLiteral("The brush does not exist.")); }
	LevelMapBrush brush = *found; LevelBrushTopology topology;
	if (!levelBrushTopology(brush,&topology,&error)) { return printCliError(commandName,CliExitCode::Failure,error,format); }
	if (editing) {
		const QString output = optionValue(args,QStringLiteral("--output"));
		if (output.isEmpty()) { return usage(QStringLiteral("--output is required.")); }
		const auto kindName = optionValue(args,QStringLiteral("--kind"));
		LevelBrushComponent kind;
		if (kindName == QStringLiteral("vertex")) { kind = LevelBrushComponent::Vertex; }
		else if (kindName == QStringLiteral("edge")) { kind = LevelBrushComponent::Edge; }
		else if (kindName == QStringLiteral("face")) { kind = LevelBrushComponent::Face; }
		else { return usage(QStringLiteral("--kind requires vertex, edge or face.")); }
		QVector<int> components;
		for (const auto& textId : optionValues(args,QStringLiteral("--component"))) {
			bool ok = false; const int component = textId.toInt(&ok);
			if (!ok || component < 0) { return usage(QStringLiteral("--component requires a non-negative component ID.")); }
			if (!components.contains(component)) { components << component; }
		}
		LevelMapVec3 delta{0,0,0,true}; double grid = 0;
		if (!hasOption(args,QStringLiteral("--delta")) && !hasOption(args,QStringLiteral("--grid"))) { return usage(QStringLiteral("Specify --delta x,y,z or --grid size.")); }
		if (hasOption(args,QStringLiteral("--delta")) && !parseLevelMapDelta(optionValue(args,QStringLiteral("--delta")),&delta.x,&delta.y,&delta.z)) {
			return usage(QStringLiteral("--delta requires x,y,z."));
		}
		if (hasOption(args,QStringLiteral("--grid"))) {
			bool ok = false; grid = optionValue(args,QStringLiteral("--grid")).toDouble(&ok);
			if (!ok) { return usage(QStringLiteral("--grid requires a finite non-negative size.")); }
		}
		LevelBrushEditReport change;
		if (!moveLevelBrushComponents(&brush,kind,components,delta,grid,hasOption(args,QStringLiteral("--allow-collapse")),&change,&error)) { return usage(error); }
		if (!replaceLevelMapBrushGeometry(&document,id,brush,&error)) { return printCliError(commandName,CliExitCode::Failure,error,format); }
		const auto saved = saveLevelMapAs(document,output,hasOption(args,QStringLiteral("--dry-run")),hasOption(args,QStringLiteral("--overwrite")));
		QJsonObject extra{{QStringLiteral("brushId"),id},{QStringLiteral("verticesBefore"),change.verticesBefore},{QStringLiteral("verticesAfter"),change.verticesAfter},
			{QStringLiteral("facesBefore"),change.facesBefore},{QStringLiteral("facesAfter"),change.facesAfter},{QStringLiteral("collapsedVertices"),change.collapsedVertices},{QStringLiteral("changed"),change.changed}};
		if (format != CliOutputFormat::Json) { std::cout << "Brush " << id << ": " << change.verticesBefore << " -> " << change.verticesAfter << " vertices; " << change.facesBefore << " -> " << change.facesAfter << " faces; " << change.collapsedVertices << " collapsed vertices.\n"; }
		return printLevelMapSaveResult(commandName,document,saved,format,extra);
	}
	QJsonArray vertices,edges,faces;
	for (int i = 0; i < topology.vertices.size(); ++i) { auto v = levelMapVec3Json(topology.vertices[i]); v.insert(QStringLiteral("id"),i); vertices.append(v); }
	const auto idsJson = [](const auto& ids) { QJsonArray values; for (int i : ids) { values.append(i); } return values; };
	for (int i = 0; i < topology.edges.size(); ++i) { edges.append(QJsonObject{{QStringLiteral("id"),i},{QStringLiteral("vertices"),idsJson(topology.edges[i])}}); }
	for (int i = 0; i < topology.faces.size(); ++i) {
		faces.append(QJsonObject{{QStringLiteral("id"),i},{QStringLiteral("vertices"),idsJson(topology.faces[i])},{QStringLiteral("texture"),brush.faces[i].textureName}});
	}
	const QJsonObject data{{QStringLiteral("brushId"),id},{QStringLiteral("vertices"),vertices},{QStringLiteral("edges"),edges},{QStringLiteral("faces"),faces}};
	// This dedicated inspection avoids solving every brush for ordinary map inspect.
	if (format == CliOutputFormat::Json) { auto result = cliResultJson(commandName); result.insert(QStringLiteral("brush"),data); printJson(result); }
	else {
		for (int i = 0; i < topology.vertices.size(); ++i) { const auto p = topology.vertices[i]; std::cout << "vertex " << i << ": " << p.x << ", " << p.y << ", " << p.z << '\n'; }
		for (int i = 0; i < topology.edges.size(); ++i) { std::cout << "edge " << i << ": " << topology.edges[i][0] << ", " << topology.edges[i][1] << '\n'; }
		for (int i = 0; i < topology.faces.size(); ++i) { std::cout << "face " << i << " (" << text(brush.faces[i].textureName) << "):"; for (int v : topology.faces[i]) { std::cout << ' ' << v; } std::cout << '\n'; }
	}
	return 0;
}

int runMapPatchCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format, bool creating)
{
	const auto usage = [&](const QString& error) { return printCliError(commandName, CliExitCode::Usage, error, format); };
	const QString output = optionValue(args, QStringLiteral("--output"));
	if (path.isEmpty() || output.isEmpty()) { return usage(QStringLiteral("A map path and --output are required.")); }
	QString error;
	LevelMapDocument document;
	const auto request = levelMapLoadRequestFromArgs(path, args);
	if (!loadLevelMap(request, &document, &error)) { return printCliError(commandName, levelMapLoadFailureCode(request), error, format); }
	LevelMapPatch patch;
	int patchId = -1;
	if (creating) {
		LevelPatchCreateRequest create;
		if (hasOption(args, QStringLiteral("--shape"))) { create.shape = optionValue(args, QStringLiteral("--shape")); }
		if (hasOption(args, QStringLiteral("--plane"))) { create.plane = optionValue(args, QStringLiteral("--plane")); }
		create.texture = optionValue(args, QStringLiteral("--texture"));
		for (const auto& pair : {qMakePair(QStringLiteral("--size"), &create.size), qMakePair(QStringLiteral("--origin"), &create.center)}) {
			if (hasOption(args, pair.first) && !parseLevelMapDelta(optionValue(args, pair.first), &pair.second->x, &pair.second->y, &pair.second->z)) {
				return usage(QStringLiteral("%1 requires x,y,z coordinates.").arg(pair.first));
			}
		}
		for (const auto& pair : {qMakePair(QStringLiteral("--columns"), &create.columns), qMakePair(QStringLiteral("--rows"), &create.rows)}) {
			if (hasOption(args, pair.first)) {
				bool ok = false; *pair.second = optionValue(args, pair.first).toInt(&ok);
				if (!ok) { return usage(QStringLiteral("Grid dimensions must be integers.")); }
			}
		}
		if (!createLevelPatch(create, &patch, &error)) { return usage(error); }
		if (!addLevelMapPatch(&document, patch, &patchId, &error)) { return printCliError(commandName, CliExitCode::Failure, error, format); }
	} else {
		bool idOk = false; patchId = optionValue(args, QStringLiteral("--patch")).toInt(&idOk);
		if (!idOk || patchId < 0) { return usage(QStringLiteral("--patch requires a non-negative patch ID.")); }
		bool found = false;
		for (const auto& candidate : document.patches) { if (candidate.id == patchId) { patch = candidate; found = true; break; } }
		if (!found) { return usage(QStringLiteral("The requested patch does not exist.")); }
		QVector<int> points;
		for (const auto& point : optionValues(args, QStringLiteral("--point"))) {
			const auto parts = point.split(QLatin1Char(',')); bool rowOk = false, colOk = false;
			const int row = parts.value(0).toInt(&rowOk), col = parts.value(1).toInt(&colOk);
			if (parts.size() != 2 || !rowOk || !colOk || row < 0 || row >= patch.height || col < 0 || col >= patch.width) {
				return usage(QStringLiteral("--point requires an existing zero-based row,column pair."));
			}
			if (!points.contains(row * patch.width + col)) { points << row * patch.width + col; }
		}
		bool changed = false;
		if (hasOption(args, QStringLiteral("--delta")) || hasOption(args, QStringLiteral("--grid"))) {
			LevelMapVec3 delta {0,0,0,true}; double grid = 0;
			if (hasOption(args, QStringLiteral("--delta")) && !parseLevelMapDelta(optionValue(args, QStringLiteral("--delta")), &delta.x, &delta.y, &delta.z)) {
				return usage(QStringLiteral("--delta requires x,y,z."));
			}
			if (hasOption(args, QStringLiteral("--grid"))) {
				bool ok = false; grid = optionValue(args, QStringLiteral("--grid")).toDouble(&ok);
				if (!ok) { return usage(QStringLiteral("--grid requires a number.")); }
			}
			if (!moveLevelPatchPoints(&patch, points, delta, grid, &error)) { return usage(error); } changed = true;
		}
		if (hasOption(args, QStringLiteral("--uv"))) {
			const auto uv = optionValue(args, QStringLiteral("--uv")).split(QLatin1Char(',')); bool uOk = false, vOk = false;
			const double u = uv.value(0).toDouble(&uOk), v = uv.value(1).toDouble(&vOk);
			if (uv.size() != 2 || !uOk || !vOk || points.isEmpty()) { return usage(QStringLiteral("--uv requires u,v and at least one --point.")); }
			for (int i : points) { patch.controlU[i] = u; patch.controlV[i] = v; } changed = true;
		}
		if (hasOption(args, QStringLiteral("--texture"))) {
			patch.textureName = levelPatchMaterialToken(optionValue(args, QStringLiteral("--texture")), patch.fixedSubdivisions);
			changed = true;
		}
		if (hasOption(args, QStringLiteral("--subdivide"))) {
			const auto axis = optionValue(args, QStringLiteral("--subdivide"));
			if (axis != QStringLiteral("rows") && axis != QStringLiteral("columns")) { return usage(QStringLiteral("--subdivide accepts rows or columns.")); }
			if (!subdivideLevelPatch(&patch, axis == QStringLiteral("columns"), &error)) { return usage(error); } changed = true;
		}
		if (hasOption(args, QStringLiteral("--invert"))) { if (!invertLevelPatch(&patch, &error)) { return usage(error); } changed = true; }
		if (!changed) { return usage(QStringLiteral("Specify --delta, --grid, --uv, --texture, --subdivide or --invert.")); }
		if (!replaceLevelMapPatch(&document, patchId, patch, &error)) { return printCliError(commandName, CliExitCode::Failure, error, format); }
	}
	const auto report = saveLevelMapAs(document, output, hasOption(args, QStringLiteral("--dry-run")), hasOption(args, QStringLiteral("--overwrite")));
	QJsonObject extra; extra.insert(QStringLiteral("patchId"), patchId); extra.insert(QStringLiteral("selector"), QStringLiteral("patch:%1").arg(patchId));
	return printLevelMapSaveResult(commandName, document, report, format, extra);
}

int runMapCapPatchCommand(const QString& path, const QStringList& args, CliOutputFormat format)
{
	const QString command=QStringLiteral("map cap-patch");
	const auto usage=[&](const QString& message){return printCliError(command,CliExitCode::Usage,message,format);};
	for (const auto& option : {QStringLiteral("--patch"),QStringLiteral("--output"),QStringLiteral("--texture"),QStringLiteral("--uv"),QStringLiteral("--units-per-tile"),QStringLiteral("--center"),QStringLiteral("--boundary")}) {
		const auto count=std::count_if(args.cbegin(),args.cend(),[&](const auto& token){return token==option || token.startsWith(option+QLatin1Char('='));});
		const auto values=optionValues(args,option);
		if (count!=values.size() || (count>1 && option!=QStringLiteral("--boundary")) ||
			std::any_of(values.cbegin(),values.cend(),[](const auto& value){return value.isEmpty() || value.startsWith(QStringLiteral("--"));})) {
			return usage(QStringLiteral("%1 requires a value and may be repeated only for --boundary.").arg(option));
		}
	}
	const auto output=optionValue(args,QStringLiteral("--output"));
	bool valid=false; const int id=optionValue(args,QStringLiteral("--patch")).toInt(&valid);
	if (path.isEmpty() || output.isEmpty() || !valid || id<0) { return usage(QStringLiteral("A map path, non-negative --patch ID, and --output are required.")); }
	LevelPatchCapRequest request;
	request.boundaries.clear();
	for (const auto& token : optionValues(args,QStringLiteral("--boundary"))) {
		LevelPatchBoundary edge;
		if (!parseLevelPatchBoundary(token,&edge)) { return usage(QStringLiteral("--boundary accepts first-row, last-row, first-column or last-column.")); }
		request.boundaries << edge;
	}
	if (request.boundaries.isEmpty()) { return usage(QStringLiteral("At least one --boundary is required.")); }
	request.texture=optionValue(args,QStringLiteral("--texture")); request.invert=hasOption(args,QStringLiteral("--invert"));
	if (hasOption(args,QStringLiteral("--uv"))) {
		const auto uv=optionValue(args,QStringLiteral("--uv"));
		if (uv!=QStringLiteral("planar") && uv!=QStringLiteral("boundary")) { return usage(QStringLiteral("--uv accepts planar or boundary.")); }
		request.uv=uv==QStringLiteral("planar") ? LevelPatchCapUv::Planar : LevelPatchCapUv::Boundary;
	}
	if (hasOption(args,QStringLiteral("--units-per-tile"))) {
		request.unitsPerTile=optionValue(args,QStringLiteral("--units-per-tile")).toDouble(&valid);
		if (!valid || !std::isfinite(request.unitsPerTile) || request.unitsPerTile<=0) { return usage(QStringLiteral("--units-per-tile requires a finite positive number.")); }
	}
	if (hasOption(args,QStringLiteral("--center"))) {
		const auto xyz=optionValue(args,QStringLiteral("--center")).split(QLatin1Char(','));
		if (xyz.size()!=3) { return usage(QStringLiteral("--center requires X,Y,Z.")); }
		double* fields[3]{&request.center.x,&request.center.y,&request.center.z};
		for (int i=0;i<3;++i) { *fields[i]=xyz[i].toDouble(&valid); if (!valid || !std::isfinite(*fields[i])) { return usage(QStringLiteral("--center requires three finite numbers.")); } }
		request.customCenter=true;
	}
	LevelMapDocument document; QString error; const auto load=levelMapLoadRequestFromArgs(path,args);
	if (!loadLevelMap(load,&document,&error)) { return printCliError(command,levelMapLoadFailureCode(load),error,format); }
	LevelPatchCapResult result;
	if (!capLevelMapPatch(&document,id,request,&result,&error)) { return printCliError(command,CliExitCode::ValidationFailed,error,format); }
	if (format!=CliOutputFormat::Json) {
		std::cout << text(QCoreApplication::translate("LevelPatchCap","Added %1 patch caps.").arg(result.caps.size())) << '\n';
		for (const auto& warning : result.warnings) { std::cout << text(warning) << '\n'; }
	}
	return printLevelMapSaveResult(command,document,saveLevelMapAs(document,output,hasOption(args,QStringLiteral("--dry-run")),hasOption(args,QStringLiteral("--overwrite"))),format,
		{{QStringLiteral("cap"),levelPatchCapReportJson(result)}});
}

int runMapStitchPatchesCommand(const QString& path, const QStringList& args, CliOutputFormat format)
{
	const QString command=QStringLiteral("map stitch-patches");
	const auto usage=[&](const QString& message) { return printCliError(command,CliExitCode::Usage,message,format); };
	for (const QString& option : {QStringLiteral("--first"),QStringLiteral("--second"),QStringLiteral("--max-gap"),QStringLiteral("--direction"),QStringLiteral("--target"),QStringLiteral("--uv"),QStringLiteral("--output")}) {
		const auto occurrences=std::count_if(args.cbegin(),args.cend(),[&](const QString& token) { return token==option || token.startsWith(option+QLatin1Char('=')); });
		if (hasOption(args,option) && (occurrences!=1 || optionValue(args,option).isEmpty() || optionValue(args,option).startsWith(QStringLiteral("--")))) {
			return usage(QStringLiteral("%1 requires exactly one value.").arg(option));
		}
	}
	const auto output=optionValue(args,QStringLiteral("--output"));
	if (path.isEmpty() || output.isEmpty()) { return usage(QStringLiteral("A map path and --output are required.")); }
	LevelPatchStitchRequest stitch; int first=-1,second=-1;
	const auto boundary=[&](const QString& option,int* id,LevelPatchBoundary* edge) {
		const auto parts=optionValue(args,option).split(QLatin1Char(':')); bool valid=false;
		*id=parts.value(0).toInt(&valid);
		return parts.size()==2 && valid && *id>=0 && parseLevelPatchBoundary(parts[1],edge);
	};
	if (!boundary(QStringLiteral("--first"),&first,&stitch.first) || !boundary(QStringLiteral("--second"),&second,&stitch.second)) {
		return usage(QStringLiteral("--first and --second require patchId:first-row|last-row|first-column|last-column."));
	}
	if (hasOption(args,QStringLiteral("--max-gap"))) {
		bool valid=false; stitch.maxDistance=optionValue(args,QStringLiteral("--max-gap")).toDouble(&valid);
		if (!valid || !std::isfinite(stitch.maxDistance) || stitch.maxDistance<0 || stitch.maxDistance>1048576) { return usage(QStringLiteral("--max-gap requires a finite number from 0 to 1048576.")); }
	}
	for (const auto& item : {qMakePair(QStringLiteral("--direction"),QStringList{QStringLiteral("auto"),QStringLiteral("forward"),QStringLiteral("reversed")}),
		qMakePair(QStringLiteral("--target"),QStringList{QStringLiteral("first"),QStringLiteral("second"),QStringLiteral("average")}),
		qMakePair(QStringLiteral("--uv"),QStringList{QStringLiteral("preserve"),QStringLiteral("first"),QStringLiteral("second"),QStringLiteral("average")})}) {
		if (!hasOption(args,item.first)) { continue; }
		const int index=static_cast<int>(item.second.indexOf(optionValue(args,item.first)));
		if (index<0) { return usage(QStringLiteral("%1 accepts %2.").arg(item.first,item.second.join(QStringLiteral(", ")))); }
		if (item.first==QStringLiteral("--direction")) { stitch.order=static_cast<LevelPatchStitchOrder>(index); }
		else if (item.first==QStringLiteral("--target")) { stitch.target=static_cast<LevelPatchStitchTarget>(index); }
		else { stitch.uv=static_cast<LevelPatchStitchUv>(index); }
	}
	stitch.matchTangents=hasOption(args,QStringLiteral("--match-tangents"));
	LevelMapDocument document; QString error; const auto load=levelMapLoadRequestFromArgs(path,args);
	if (!loadLevelMap(load,&document,&error)) { return printCliError(command,levelMapLoadFailureCode(load),error,format); }
	LevelPatchStitchResult result;
	if (!stitchLevelMapPatches(&document,first,second,stitch,&result,&error)) { return printCliError(command,CliExitCode::ValidationFailed,error,format); }
	if (format != CliOutputFormat::Json) {
		std::cout << text(QCoreApplication::translate("LevelPatchStitch", "%1 paired controls; maximum gap %2, movement %3.")
			.arg(result.boundaryPoints).arg(result.maxGap,0,'g',6).arg(result.maxMovement,0,'g',6)) << '\n';
		for (const auto& warning : result.warnings) { std::cout << text(warning) << '\n'; }
	}
	const auto report=saveLevelMapAs(document,output,hasOption(args,QStringLiteral("--dry-run")),hasOption(args,QStringLiteral("--overwrite")));
	return printLevelMapSaveResult(command,document,report,format,{{QStringLiteral("stitch"),levelPatchStitchReportJson(result)}});
}

int runMapAddThingCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format)
{
	const LevelMapLoadRequest request = levelMapLoadRequestFromArgs(path, args);
	if (request.path.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a Doom or Hexen WAD path.").arg(commandName), format);
	}
	bool typeOk = false;
	const int type = optionValue(args, QStringLiteral("--type")).trimmed().toInt(&typeOk);
	if (!typeOk) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --type <DoomEd number>.").arg(commandName), format);
	}
	double x = 0.0;
	double y = 0.0;
	double z = 0.0;
	if (!parseLevelMapDelta(optionValue(args, QStringLiteral("--origin")), &x, &y, &z)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --origin x,y.").arg(commandName), format);
	}
	int angle = 0;
	if (hasOption(args, QStringLiteral("--angle"))) {
		bool angleOk = false;
		angle = optionValue(args, QStringLiteral("--angle")).trimmed().toInt(&angleOk);
		if (!angleOk) {
			return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1: --angle takes whole degrees.").arg(commandName), format);
		}
	}
	const QString outputPath = optionValue(args, QStringLiteral("--output"));
	if (outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --output <save-as path>.").arg(commandName), format);
	}

	LevelMapDocument document;
	QString error;
	if (!loadLevelMap(request, &document, &error)) {
		return printCliError(commandName, levelMapLoadFailureCode(request), QStringLiteral("Unable to load map: %1").arg(error), format);
	}
	int thingId = -1;
	if (!addLevelMapDoomThing(&document, type, x, y, angle, &thingId, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to add thing: %1").arg(error), format);
	}
	const LevelMapSaveReport report = saveLevelMapAs(document, outputPath, hasOption(args, QStringLiteral("--dry-run")), hasOption(args, QStringLiteral("--overwrite")));
	if (format != CliOutputFormat::Json) {
		std::cout << text(QStringLiteral("Added thing:%1 (type %2)").arg(thingId).arg(type)) << "\n";
	}
	QJsonObject extra;
	extra.insert(QStringLiteral("thingId"), thingId);
	extra.insert(QStringLiteral("selector"), QStringLiteral("thing:%1").arg(thingId));
	return printLevelMapSaveResult(commandName, document, report, format, extra);
}

int runMapDeleteCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format)
{
	const LevelMapLoadRequest request = levelMapLoadRequestFromArgs(path, args);
	if (request.path.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a Quake-family .map or Doom WAD path.").arg(commandName), format);
	}
	QVector<LevelMapSelectionRef> objects;
	QString badSelector;
	if (!levelMapObjectsFromArgs(args, &objects, &badSelector)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unknown map object selector: %1").arg(badSelector), format);
	}
	if (objects.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --object kind:id, repeatable.").arg(commandName), format);
	}
	const QString outputPath = optionValue(args, QStringLiteral("--output"));
	if (outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --output <save-as path>.").arg(commandName), format);
	}

	LevelMapDocument document;
	QString error;
	if (!loadLevelMap(request, &document, &error)) {
		return printCliError(commandName, levelMapLoadFailureCode(request), QStringLiteral("Unable to load map: %1").arg(error), format);
	}
	if (!deleteLevelMapObjects(&document, objects, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to delete map objects: %1").arg(error), format);
	}
	const LevelMapSaveReport report = saveLevelMapAs(document, outputPath, hasOption(args, QStringLiteral("--dry-run")), hasOption(args, QStringLiteral("--overwrite")));
	QJsonArray deleted;
	for (const LevelMapSelectionRef& ref : objects) {
		deleted.append(levelMapSelectionRefId(ref));
	}
	QJsonObject extra;
	extra.insert(QStringLiteral("deleted"), deleted);
	return printLevelMapSaveResult(commandName, document, report, format, extra);
}

int runMapReplaceTextureCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format)
{
	const LevelMapLoadRequest request = levelMapLoadRequestFromArgs(path, args);
	if (request.path.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a Doom WAD or Quake-family .map path.").arg(commandName), format);
	}
	const QString from = optionValue(args, QStringLiteral("--from")).trimmed();
	const QString to = optionValue(args, QStringLiteral("--to")).trimmed();
	if (from.isEmpty() || to.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --from <texture> and --to <texture>.").arg(commandName), format);
	}
	// --object narrows the replacement to those objects.
	QVector<LevelMapSelectionRef> objects;
	QString badSelector;
	if (!levelMapObjectsFromArgs(args, &objects, &badSelector)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unknown map object selector: %1").arg(badSelector), format);
	}
	const QString outputPath = optionValue(args, QStringLiteral("--output"));
	if (outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --output <save-as path>.").arg(commandName), format);
	}

	LevelMapDocument document;
	QString error;
	if (!loadLevelMap(request, &document, &error)) {
		return printCliError(commandName, levelMapLoadFailureCode(request), QStringLiteral("Unable to load map: %1").arg(error), format);
	}
	if (!objects.isEmpty() && !setLevelMapSelection(&document, objects, &error)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unable to select map objects: %1").arg(error), format);
	}
	int replaced = 0;
	if (!replaceLevelMapTexture(&document, from, to, !objects.isEmpty(), &replaced, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to replace texture: %1").arg(error), format);
	}
	const LevelMapSaveReport report = saveLevelMapAs(document, outputPath, hasOption(args, QStringLiteral("--dry-run")), hasOption(args, QStringLiteral("--overwrite")));
	if (format != CliOutputFormat::Json) {
		std::cout << text(QStringLiteral("Replaced %1 with %2: %3 uses").arg(from, to).arg(replaced)) << "\n";
	}
	QJsonObject extra;
	extra.insert(QStringLiteral("from"), from);
	extra.insert(QStringLiteral("to"), to);
	extra.insert(QStringLiteral("replaced"), replaced);
	return printLevelMapSaveResult(commandName, document, report, format, extra);
}

int runMapRotateCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format)
{
	const LevelMapLoadRequest request = levelMapLoadRequestFromArgs(path, args);
	if (request.path.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a Doom WAD or Quake-family .map path.").arg(commandName), format);
	}
	QVector<LevelMapSelectionRef> objects;
	QString badSelector;
	if (!levelMapObjectsFromArgs(args, &objects, &badSelector)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unknown map object selector: %1").arg(badSelector), format);
	}
	if (objects.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --object kind:id, repeatable.").arg(commandName), format);
	}
	const QString axisText = hasOption(args, QStringLiteral("--axis")) ? optionValue(args, QStringLiteral("--axis")).trimmed().toLower() : QStringLiteral("z");
	const int axis = QStringList {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z")}.indexOf(axisText);
	if (axis < 0) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1: --axis takes x, y, or z.").arg(commandName), format);
	}
	bool turnsOk = true;
	const int turns = hasOption(args, QStringLiteral("--turns")) ? optionValue(args, QStringLiteral("--turns")).trimmed().toInt(&turnsOk) : 1;
	if (!turnsOk) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1: --turns takes a whole number of quarter turns.").arg(commandName), format);
	}
	LevelMapTextureLockOptions textures;
	QString textureError;
	if (!mapTextureLockFromArgs(args, true, &textures, &textureError)) {
		return printCliError(commandName, CliExitCode::Usage, textureError, format);
	}
	LevelMapRotationRequest rotation;
	rotation.axis = axis;
	rotation.degrees = 90.0 * (turns % 4);
	if (hasOption(args, QStringLiteral("--degrees"))) {
		bool valid = false;
		rotation.degrees = optionValue(args, QStringLiteral("--degrees")).toDouble(&valid);
		if (!valid || !std::isfinite(rotation.degrees) || hasOption(args, QStringLiteral("--turns"))) {
			return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Use a finite --degrees value or --turns, not both."), format);
		}
	}
	if (hasOption(args, QStringLiteral("--pivot"))) {
		const auto value = optionValue(args, QStringLiteral("--pivot"));
		rotation.pivot.valid = true;
		if (value.split(QRegularExpression(QStringLiteral(R"([,\s]+)")), Qt::SkipEmptyParts).size() != 3
			|| !parseLevelMapDelta(value, &rotation.pivot.x, &rotation.pivot.y, &rotation.pivot.z)) {
			return printCliError(commandName, CliExitCode::Usage, QStringLiteral("--pivot takes x,y,z coordinates."), format);
		}
	}
	rotation.textureLock = textures.enabled;
	rotation.allowValve220 = textures.allowValve220;
	const QString outputPath = optionValue(args, QStringLiteral("--output"));
	if (outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --output <save-as path>.").arg(commandName), format);
	}

	LevelMapDocument document;
	QString error;
	if (!loadLevelMap(request, &document, &error)) {
		return printCliError(commandName, levelMapLoadFailureCode(request), QStringLiteral("Unable to load map: %1").arg(error), format);
	}
	if (!setLevelMapSelection(&document, objects, &error)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unable to select map objects: %1").arg(error), format);
	}
	if (!rotateLevelMapSelection(&document, rotation, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to turn map objects: %1").arg(error), format);
	}
	const LevelMapSaveReport report = saveLevelMapAs(document, outputPath, hasOption(args, QStringLiteral("--dry-run")), hasOption(args, QStringLiteral("--overwrite")));
	QJsonObject extra;
	extra.insert(QStringLiteral("axis"), axisText);
	extra.insert(QStringLiteral("turns"), turns);
	extra.insert(QStringLiteral("degrees"), rotation.degrees);
	extra.insert(QStringLiteral("textureLockPolicy"), rotation.textureLock ? QStringLiteral("locked") : QStringLiteral("source-parameters"));
	extra.insert(QStringLiteral("allowValve220"), rotation.allowValve220);
	if (rotation.pivot.valid) { extra.insert(QStringLiteral("pivot"), levelMapVec3Json(rotation.pivot)); }
	return printLevelMapSaveResult(commandName, document, report, format, extra);
}

int runMapFlipCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format)
{
	LevelMapTextureLockOptions textures;
	QString textureError;
	if (!mapTextureLockFromArgs(args, true, &textures, &textureError)) {
		return printCliError(commandName, CliExitCode::Usage, textureError, format);
	}
	const LevelMapLoadRequest request = levelMapLoadRequestFromArgs(path, args);
	if (request.path.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a Doom WAD or Quake-family .map path.").arg(commandName), format);
	}
	QVector<LevelMapSelectionRef> objects;
	QString badSelector;
	if (!levelMapObjectsFromArgs(args, &objects, &badSelector)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unknown map object selector: %1").arg(badSelector), format);
	}
	if (objects.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --object kind:id, repeatable.").arg(commandName), format);
	}
	const QString axisText = optionValue(args, QStringLiteral("--axis")).trimmed().toLower();
	const int axis = QStringList {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z")}.indexOf(axisText);
	if (axis < 0) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --axis x, y, or z.").arg(commandName), format);
	}
	const QString outputPath = optionValue(args, QStringLiteral("--output"));
	if (outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --output <save-as path>.").arg(commandName), format);
	}

	LevelMapDocument document;
	QString error;
	if (!loadLevelMap(request, &document, &error)) {
		return printCliError(commandName, levelMapLoadFailureCode(request), QStringLiteral("Unable to load map: %1").arg(error), format);
	}
	if (!setLevelMapSelection(&document, objects, &error)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unable to select map objects: %1").arg(error), format);
	}
	LevelPlacementRequest placement;
	placement.operation = LevelPlacementOperation::Mirror;
	placement.axis = axis;
	placement.textures = textures;
	placement.connectedGeometry = hasOption(args, QStringLiteral("--connected"));
	auto prepared = prepareLevelPlacement(document, placement);
	if (!prepared.succeeded) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to flip map objects: %1").arg(prepared.error), format);
	}
	document = std::move(prepared.document);
	const LevelMapSaveReport report = saveLevelMapAs(document, outputPath, hasOption(args, QStringLiteral("--dry-run")), hasOption(args, QStringLiteral("--overwrite")));
	QJsonObject extra;
	extra.insert(QStringLiteral("textureLockPolicy"), document.format == LevelMapFormat::DoomWad ? QStringLiteral("native-offsets") :
		textures.enabled ? QStringLiteral("locked") : QStringLiteral("source-parameters"));
	extra.insert(QStringLiteral("allowValve220"), textures.allowValve220);
	extra.insert(QStringLiteral("axis"), axisText);
	extra.insert(QStringLiteral("connectedGeometry"), placement.connectedGeometry);
	return printLevelMapSaveResult(commandName, document, report, format, extra);
}

int runMapSurfaceCommand(const QString& path, const QStringList& args, CliOutputFormat format)
{
	const QString command = QStringLiteral("map align-textures");
	const auto usage = [&](const QString& message) { return printCliError(command, CliExitCode::Usage, message, format); };
	const QString output = optionValue(args, QStringLiteral("--output"));
	if (path.trimmed().isEmpty() || output.trimmed().isEmpty()) {
		return usage(QStringLiteral("map align-textures requires a map path and --output <save-as path>."));
	}
	const QStringList operations {QStringLiteral("--shift"), QStringLiteral("--scale"), QStringLiteral("--degrees"), QStringLiteral("--fit"), QStringLiteral("--align")};
	int operation = -1;
	for (int i = 0; i < operations.size(); ++i) {
		if (!hasOption(args, operations[i])) { continue; }
		if (operation >= 0 || optionValues(args, operations[i]).size() != 1) {
			return usage(QStringLiteral("Choose exactly one surface operation: --shift, --scale, --degrees, --fit or --align."));
		}
		operation = i;
	}
	if (operation < 0) { return usage(QStringLiteral("Choose --shift U,V, --scale U,V, --degrees N, --fit U,V or --align U,V.")); }
	LevelSurfaceRequest surface;
	surface.operation = static_cast<LevelSurfaceOperation>(operation);
	const auto pair = [](const QString& value, double* x, double* y) {
		const auto values = value.trimmed().split(QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts);
		bool a = false, b = false;
		if (values.size() != 2) { return false; }
		*x = values[0].toDouble(&a); *y = values[1].toDouble(&b);
		return a && b && std::isfinite(*x) && std::isfinite(*y);
	};
	if (surface.operation == LevelSurfaceOperation::Rotate) {
		bool ok = false;
		surface.degrees = optionValue(args, operations[operation]).toDouble(&ok);
		if (!ok || !std::isfinite(surface.degrees)) { return usage(QStringLiteral("--degrees needs a finite angle.")); }
	} else if (surface.operation == LevelSurfaceOperation::Align) {
		const auto values = optionValue(args, operations[operation]).toLower().split(',', Qt::KeepEmptyParts);
		const QStringList modes {QStringLiteral("keep"), QStringLiteral("minimum"), QStringLiteral("center"), QStringLiteral("maximum")};
		if (values.size() != 2 || !modes.contains(values[0].trimmed()) || !modes.contains(values[1].trimmed())) {
			return usage(QStringLiteral("--align takes U,V modes: keep, minimum, center, maximum."));
		}
		surface.alignU = static_cast<LevelSurfaceAlignment>(modes.indexOf(values[0].trimmed()));
		surface.alignV = static_cast<LevelSurfaceAlignment>(modes.indexOf(values[1].trimmed()));
	} else if (!pair(optionValue(args, operations[operation]), &surface.x, &surface.y)) {
		return usage(QStringLiteral("%1 needs two finite U,V values.").arg(operations[operation]));
	}
	QSize explicitSize;
	if (hasOption(args, QStringLiteral("--texture-size"))) {
		double width = 0, height = 0;
		if (!pair(optionValue(args, QStringLiteral("--texture-size")), &width, &height) || width < 1 || height < 1 || width > 16384 || height > 16384 || std::floor(width) != width || std::floor(height) != height) {
			return usage(QStringLiteral("--texture-size takes whole width,height values from 1 through 16384."));
		}
		explicitSize = QSize(static_cast<int>(width), static_cast<int>(height));
	}
	QVector<LevelMapSelectionRef> objects;
	QString error;
	if (!levelMapObjectsFromArgs(args, &objects, &error)) { return usage(QStringLiteral("Unknown object selector: %1").arg(error)); }
	for (const auto& object : objects) {
		if (object.kind != LevelMapSelectionKind::QuakeBrush && object.kind != LevelMapSelectionKind::Entity) {
			return usage(QStringLiteral("Surface alignment selects brush faces or brushes owned by entities. Use edit-patch for patch UVs."));
		}
	}
	LevelMapDocument document;
	const auto load = levelMapLoadRequestFromArgs(path, args);
	if (!loadLevelMap(load, &document, &error)) { return printCliError(command, levelMapLoadFailureCode(load), error, format); }
	if (!setLevelMapSelection(&document, objects, &error)) { return usage(error); }
	auto faces = levelMapSelectedSurfaces(document);
	for (const auto& token : optionValues(args, QStringLiteral("--face"))) {
		const auto parts = token.split(':');
		bool brushOk = false, faceOk = false;
		const int brush = parts.value(0).toInt(&brushOk), face = parts.value(1).toInt(&faceOk);
		if (parts.size() != 2 || !brushOk || !faceOk || brush < 0 || face < 1) {
			return usage(QStringLiteral("--face takes brushId:faceNumber; face numbers start at one."));
		}
		const LevelSurfaceFace ref{brush, face - 1};
		if (!faces.contains(ref)) { faces.append(ref); }
	}
	if (faces.isEmpty()) { return usage(QStringLiteral("Select at least one brush face with --object or --face.")); }
	QSet<int> brushIds;
	for (const auto& face : faces) { brushIds.insert(face.brushId); }
	auto assetsDocument = document;
	assetsDocument.brushes.removeIf([&](const auto& brush) { return !brushIds.contains(brush.id); });
	assetsDocument.entities.clear(); assetsDocument.patches.clear();
	LevelPreviewAssets assets;
	const auto packagePath = optionValue(args, QStringLiteral("--package"));
	if (hasOption(args, QStringLiteral("--package"))) {
		PackageArchive archive;
		if (packagePath.isEmpty() || !archive.load(packagePath, &error)) { return printCliError(command, CliExitCode::NotFound, error.isEmpty() ? QStringLiteral("Missing package path.") : error, format); }
		LevelPreviewAssetOptions options;
		options.paletteId = optionValue(args, QStringLiteral("--palette"));
		assets = resolveLevelPreviewAssets(assetsDocument, archive, options);
	}
	auto sizes = levelPreviewTextureSizes(assets);
	if (explicitSize.isValid()) {
		for (const auto& brush : assetsDocument.brushes) {
			for (const auto& face : brush.faces) { sizes.insert(face.textureName.trimmed().replace('\\', '/').toCaseFolded(), explicitSize); }
		}
	}
	LevelSurfaceEditPlan plan;
	if (!prepareLevelSurfaceEdit(document, faces, surface, sizes, &plan, &error) || !commitLevelSurfaceEdit(&document, plan, &error)) {
		return printCliError(command, CliExitCode::ValidationFailed, error, format);
	}
	const auto saved = saveLevelMapAs(document, output, hasOption(args, QStringLiteral("--dry-run")), hasOption(args, QStringLiteral("--overwrite")));
	QJsonObject extra {{QStringLiteral("operation"), operations[operation].mid(2)}, {QStringLiteral("facesChanged"), plan.faceCount()},
		{QStringLiteral("brushesChanged"), plan.brushCount()}, {QStringLiteral("explicitTextureSize"), explicitSize.isValid()}};
	if (!packagePath.isEmpty()) { extra.insert(QStringLiteral("materials"), levelPreviewAssetsJson(assets)); }
	return printLevelMapSaveResult(command, document, saved, format, extra);
}

int runMapApplyTextureCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format)
{
	const LevelMapLoadRequest request = levelMapLoadRequestFromArgs(path, args);
	if (request.path.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a Quake-family .map path.").arg(commandName), format);
	}
	QVector<LevelMapSelectionRef> objects;
	QString badSelector;
	if (!levelMapObjectsFromArgs(args, &objects, &badSelector)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unknown map object selector: %1").arg(badSelector), format);
	}
	if (objects.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --object kind:id, repeatable.").arg(commandName), format);
	}
	const QString texture = optionValue(args, QStringLiteral("--texture")).trimmed();
	if (texture.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --texture <name>.").arg(commandName), format);
	}
	const QString outputPath = optionValue(args, QStringLiteral("--output"));
	if (outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --output <save-as path>.").arg(commandName), format);
	}

	LevelMapDocument document;
	QString error;
	if (!loadLevelMap(request, &document, &error)) {
		return printCliError(commandName, levelMapLoadFailureCode(request), QStringLiteral("Unable to load map: %1").arg(error), format);
	}
	if (!setLevelMapSelection(&document, objects, &error)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unable to select map objects: %1").arg(error), format);
	}
	int applied = 0;
	if (!applyLevelMapTexture(&document, texture, &applied, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to apply the texture: %1").arg(error), format);
	}
	const LevelMapSaveReport report = saveLevelMapAs(document, outputPath, hasOption(args, QStringLiteral("--dry-run")), hasOption(args, QStringLiteral("--overwrite")));
	if (format != CliOutputFormat::Json) {
		std::cout << text(QStringLiteral("Applied %1 to %2 faces and patches").arg(texture).arg(applied)) << "\n";
	}
	QJsonObject extra;
	extra.insert(QStringLiteral("texture"), texture);
	extra.insert(QStringLiteral("applied"), applied);
	return printLevelMapSaveResult(commandName, document, report, format, extra);
}

int runMapLinedefEditCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format, bool split)
{
	const LevelMapLoadRequest request = levelMapLoadRequestFromArgs(path, args);
	if (request.path.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a Doom or Hexen WAD path.").arg(commandName), format);
	}
	QVector<LevelMapSelectionRef> objects;
	QString badSelector;
	if (!levelMapObjectsFromArgs(args, &objects, &badSelector)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unknown map object selector: %1").arg(badSelector), format);
	}
	if (objects.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --object linedef:N, repeatable.").arg(commandName), format);
	}
	const QString outputPath = optionValue(args, QStringLiteral("--output"));
	if (outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --output <save-as path>.").arg(commandName), format);
	}

	LevelMapDocument document;
	QString error;
	if (!loadLevelMap(request, &document, &error)) {
		return printCliError(commandName, levelMapLoadFailureCode(request), QStringLiteral("Unable to load map: %1").arg(error), format);
	}
	if (!setLevelMapSelection(&document, objects, &error)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unable to select map objects: %1").arg(error), format);
	}
	int count = 0;
	if (!(split ? splitLevelMapLinedefs(&document, &count, &error) : flipLevelMapLinedefs(&document, &count, &error))) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to %1 linedefs: %2").arg(split ? QStringLiteral("split") : QStringLiteral("flip"), error), format);
	}
	const LevelMapSaveReport report = saveLevelMapAs(document, outputPath, hasOption(args, QStringLiteral("--dry-run")), hasOption(args, QStringLiteral("--overwrite")));
	QJsonArray linedefs;
	for (const LevelMapSelectionRef& ref : document.selection) {
		linedefs.append(levelMapSelectionRefId(ref));
	}
	if (format != CliOutputFormat::Json) {
		QStringList names;
		for (const QJsonValue& linedef : linedefs) {
			names << linedef.toString();
		}
		const QString noun = count == 1 ? QStringLiteral("linedef") : QStringLiteral("linedefs");
		std::cout << text(split ? QStringLiteral("Split %1 %2 into %3").arg(count).arg(noun, names.join(QStringLiteral(", ")))
					: QStringLiteral("Flipped %1 %2").arg(count).arg(noun))
			  << "\n";
	}
	QJsonObject extra;
	extra.insert(split ? QStringLiteral("split") : QStringLiteral("flipped"), count);
	extra.insert(QStringLiteral("linedefs"), linedefs);
	return printLevelMapSaveResult(commandName, document, report, format, extra);
}

int runMapDrawSectorCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format)
{
	const LevelMapLoadRequest request = levelMapLoadRequestFromArgs(path, args);
	if (request.path.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a Doom or Hexen WAD path.").arg(commandName), format);
	}
	// --points "x,y x,y x,y": the corners, in order around the sector.
	QVector<LevelMapVec3> corners;
	for (const QString& point : optionValue(args, QStringLiteral("--points")).split(QRegularExpression(QStringLiteral(R"([\s;]+)")), Qt::SkipEmptyParts)) {
		const QStringList parts = point.split(QLatin1Char(','));
		bool okX = false;
		bool okY = false;
		const double x = parts.value(0).toDouble(&okX);
		const double y = parts.value(1).toDouble(&okY);
		if (parts.size() != 2 || !okX || !okY) {
			return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1: --points takes the corners in order, \"x,y x,y x,y\".").arg(commandName), format);
		}
		corners.push_back({x, y, 0.0, true});
	}
	if (corners.size() < 3) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --points with three corners or more, \"x,y x,y x,y\".").arg(commandName), format);
	}
	const QString outputPath = optionValue(args, QStringLiteral("--output"));
	if (outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --output <save-as path>.").arg(commandName), format);
	}

	LevelMapDocument document;
	QString error;
	if (!loadLevelMap(request, &document, &error)) {
		return printCliError(commandName, levelMapLoadFailureCode(request), QStringLiteral("Unable to load map: %1").arg(error), format);
	}
	int sector = -1;
	int added = 0;
	if (!drawLevelMapDoomSector(&document, corners, &sector, &error, &added)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to draw the sector: %1").arg(error), format);
	}
	const LevelMapSaveReport report = saveLevelMapAs(document, outputPath, hasOption(args, QStringLiteral("--dry-run")), hasOption(args, QStringLiteral("--overwrite")));
	if (format != CliOutputFormat::Json) {
		std::cout << text(QStringLiteral("Drew sector:%1 with %2 new %3").arg(sector).arg(added).arg(added == 1 ? QStringLiteral("linedef") : QStringLiteral("linedefs")))
			  << "\n";
	}
	QJsonObject extra;
	extra.insert(QStringLiteral("sector"), QStringLiteral("sector:%1").arg(sector));
	extra.insert(QStringLiteral("newLinedefs"), added);
	return printLevelMapSaveResult(commandName, document, report, format, extra);
}

int runMapMergeVerticesCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format)
{
	const LevelMapLoadRequest request = levelMapLoadRequestFromArgs(path, args);
	if (request.path.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a Doom or Hexen WAD path.").arg(commandName), format);
	}
	QVector<LevelMapSelectionRef> objects;
	QString badSelector;
	if (!levelMapObjectsFromArgs(args, &objects, &badSelector)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unknown map object selector: %1").arg(badSelector), format);
	}
	if (objects.size() < 2) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires two or more --object vertex:N; the last is the one kept.").arg(commandName), format);
	}
	const QString outputPath = optionValue(args, QStringLiteral("--output"));
	if (outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --output <save-as path>.").arg(commandName), format);
	}

	LevelMapDocument document;
	QString error;
	if (!loadLevelMap(request, &document, &error)) {
		return printCliError(commandName, levelMapLoadFailureCode(request), QStringLiteral("Unable to load map: %1").arg(error), format);
	}
	if (!setLevelMapSelection(&document, objects, &error)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unable to select map objects: %1").arg(error), format);
	}
	int merged = 0;
	if (!mergeLevelMapVertices(&document, &merged, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to merge vertices: %1").arg(error), format);
	}
	const QString kept = document.selection.isEmpty() ? QString() : levelMapSelectionRefId(document.selection.last());
	const LevelMapSaveReport report = saveLevelMapAs(document, outputPath, hasOption(args, QStringLiteral("--dry-run")), hasOption(args, QStringLiteral("--overwrite")));
	if (format != CliOutputFormat::Json) {
		std::cout << text(QStringLiteral("Merged %1 %2 into %3").arg(merged).arg(merged == 1 ? QStringLiteral("vertex") : QStringLiteral("vertices"), kept)) << "\n";
	}
	QJsonObject extra;
	extra.insert(QStringLiteral("merged"), merged);
	extra.insert(QStringLiteral("vertex"), kept);
	return printLevelMapSaveResult(commandName, document, report, format, extra);
}

int runMapConnectCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format)
{
	const LevelMapLoadRequest request = levelMapLoadRequestFromArgs(path, args);
	if (request.path.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a Quake-family .map path.").arg(commandName), format);
	}
	QVector<LevelMapSelectionRef> objects;
	QString badSelector;
	if (!levelMapObjectsFromArgs(args, &objects, &badSelector)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unknown map object selector: %1").arg(badSelector), format);
	}
	if (objects.size() < 2) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires two or more --object entity:N; the last is the target.").arg(commandName), format);
	}
	const QString outputPath = optionValue(args, QStringLiteral("--output"));
	if (outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --output <save-as path>.").arg(commandName), format);
	}

	LevelMapDocument document;
	QString error;
	if (!loadLevelMap(request, &document, &error)) {
		return printCliError(commandName, levelMapLoadFailureCode(request), QStringLiteral("Unable to load map: %1").arg(error), format);
	}
	if (!setLevelMapSelection(&document, objects, &error)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unable to select map objects: %1").arg(error), format);
	}
	QString name;
	if (!connectLevelMapEntities(&document, &name, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to connect entities: %1").arg(error), format);
	}
	const LevelMapSaveReport report = saveLevelMapAs(document, outputPath, hasOption(args, QStringLiteral("--dry-run")), hasOption(args, QStringLiteral("--overwrite")));
	if (format != CliOutputFormat::Json) {
		std::cout << text(QStringLiteral("Connected %1 to %2 as %3").arg(objects.size() - 1).arg(levelMapSelectionRefId(objects.last()), name)) << "\n";
	}
	QJsonObject extra;
	extra.insert(QStringLiteral("targetname"), name);
	return printLevelMapSaveResult(commandName, document, report, format, extra);
}

// map shift-sectors and map gradient-sectors: the quick sector actions.
int runMapSectorFieldCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format, bool gradient)
{
	const LevelMapLoadRequest request = levelMapLoadRequestFromArgs(path, args);
	if (request.path.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a Doom or Hexen WAD path.").arg(commandName), format);
	}
	QVector<LevelMapSelectionRef> objects;
	QString badSelector;
	if (!levelMapObjectsFromArgs(args, &objects, &badSelector)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unknown map object selector: %1").arg(badSelector), format);
	}
	LevelMapSectorField field = LevelMapSectorField::Floor;
	if (!levelMapSectorFieldFromId(optionValue(args, QStringLiteral("--field")), &field)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --field floor, ceiling, or light.").arg(commandName), format);
	}
	bool numeric = gradient;
	const int by = gradient ? 0 : optionValue(args, QStringLiteral("--by")).trimmed().toInt(&numeric);
	if (!numeric) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --by <whole number>, negative to lower.").arg(commandName), format);
	}
	const QString outputPath = optionValue(args, QStringLiteral("--output"));
	if (outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --output <save-as path>.").arg(commandName), format);
	}
	LevelMapDocument document;
	QString error;
	if (!loadLevelMap(request, &document, &error)) {
		return printCliError(commandName, levelMapLoadFailureCode(request), QStringLiteral("Unable to load map: %1").arg(error), format);
	}
	if (!setLevelMapSelection(&document, objects, &error)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unable to select map objects: %1").arg(error), format);
	}
	int changed = 0;
	const bool done = gradient ? gradientLevelMapSectors(&document, field, &changed, &error) : shiftLevelMapSectors(&document, field, by, &changed, &error);
	if (!done) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to change the sectors: %1").arg(error), format);
	}
	const LevelMapSaveReport report = saveLevelMapAs(document, outputPath, hasOption(args, QStringLiteral("--dry-run")), hasOption(args, QStringLiteral("--overwrite")));
	if (format != CliOutputFormat::Json) {
		std::cout << text(document.undoStack.last().description) << "\n";
	}
	QJsonObject extra;
	extra.insert(QStringLiteral("changed"), changed);
	extra.insert(QStringLiteral("field"), levelMapSectorFieldId(field));
	return printLevelMapSaveResult(commandName, document, report, format, extra);
}

int runMapMakeDoorCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format)
{
	const LevelMapLoadRequest request = levelMapLoadRequestFromArgs(path, args);
	if (request.path.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a Doom or Hexen WAD path.").arg(commandName), format);
	}
	QVector<LevelMapSelectionRef> objects;
	QString badSelector;
	if (!levelMapObjectsFromArgs(args, &objects, &badSelector)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unknown map object selector: %1").arg(badSelector), format);
	}
	if (objects.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires one or more --object sector:N.").arg(commandName), format);
	}
	const QString outputPath = optionValue(args, QStringLiteral("--output"));
	if (outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --output <save-as path>.").arg(commandName), format);
	}
	LevelMapDoorOptions options;
	if (hasOption(args, QStringLiteral("--door-texture"))) {
		options.doorTexture = optionValue(args, QStringLiteral("--door-texture"));
	}
	if (hasOption(args, QStringLiteral("--track-texture"))) {
		options.trackTexture = optionValue(args, QStringLiteral("--track-texture"));
	}
	options.ceilingFlat = optionValue(args, QStringLiteral("--ceiling-flat"));
	options.resetOffsets = !hasOption(args, QStringLiteral("--keep-offsets"));

	LevelMapDocument document;
	QString error;
	if (!loadLevelMap(request, &document, &error)) {
		return printCliError(commandName, levelMapLoadFailureCode(request), QStringLiteral("Unable to load map: %1").arg(error), format);
	}
	if (!setLevelMapSelection(&document, objects, &error)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unable to select map objects: %1").arg(error), format);
	}
	int doors = 0;
	if (!makeLevelMapDoors(&document, options, &doors, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to make doors: %1").arg(error), format);
	}
	const LevelMapSaveReport report = saveLevelMapAs(document, outputPath, hasOption(args, QStringLiteral("--dry-run")), hasOption(args, QStringLiteral("--overwrite")));
	if (format != CliOutputFormat::Json) {
		std::cout << text(QStringLiteral("Made %1 %2").arg(doors).arg(doors == 1 ? QStringLiteral("door") : QStringLiteral("doors"))) << "\n";
	}
	QJsonObject extra;
	extra.insert(QStringLiteral("doors"), doors);
	return printLevelMapSaveResult(commandName, document, report, format, extra);
}

int runMapJoinSectorsCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format, bool merge)
{
	const LevelMapLoadRequest request = levelMapLoadRequestFromArgs(path, args);
	if (request.path.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a Doom or Hexen WAD path.").arg(commandName), format);
	}
	QVector<LevelMapSelectionRef> objects;
	QString badSelector;
	if (!levelMapObjectsFromArgs(args, &objects, &badSelector)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unknown map object selector: %1").arg(badSelector), format);
	}
	if (objects.size() < 2) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires two or more --object sector:N; the last is the one kept.").arg(commandName), format);
	}
	const QString outputPath = optionValue(args, QStringLiteral("--output"));
	if (outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --output <save-as path>.").arg(commandName), format);
	}

	LevelMapDocument document;
	QString error;
	if (!loadLevelMap(request, &document, &error)) {
		return printCliError(commandName, levelMapLoadFailureCode(request), QStringLiteral("Unable to load map: %1").arg(error), format);
	}
	if (!setLevelMapSelection(&document, objects, &error)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unable to select map objects: %1").arg(error), format);
	}
	const int linedefs = static_cast<int>(document.doomLinedefs.size());
	int joined = 0;
	if (!joinLevelMapSectors(&document, merge, &joined, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to %1 sectors: %2").arg(merge ? QStringLiteral("merge") : QStringLiteral("join"), error), format);
	}
	const QString kept = document.selection.isEmpty() ? QString() : levelMapSelectionRefId(document.selection.last());
	const int removed = linedefs - static_cast<int>(document.doomLinedefs.size());
	const LevelMapSaveReport report = saveLevelMapAs(document, outputPath, hasOption(args, QStringLiteral("--dry-run")), hasOption(args, QStringLiteral("--overwrite")));
	if (format != CliOutputFormat::Json) {
		std::cout << text(QStringLiteral("%1 %2 sectors into %3").arg(merge ? QStringLiteral("Merged") : QStringLiteral("Joined")).arg(joined + 1).arg(kept));
		if (merge) {
			std::cout << text(QStringLiteral(", %1 %2 between them removed").arg(removed).arg(removed == 1 ? QStringLiteral("linedef") : QStringLiteral("linedefs")));
		}
		std::cout << "\n";
	}
	QJsonObject extra;
	extra.insert(QStringLiteral("joined"), joined + 1);
	extra.insert(QStringLiteral("sector"), kept);
	extra.insert(QStringLiteral("removedLinedefs"), removed);
	return printLevelMapSaveResult(commandName, document, report, format, extra);
}

int runMapCarveCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format)
{
	const LevelMapLoadRequest request = levelMapLoadRequestFromArgs(path, args);
	if (request.path.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a Quake-family .map path.").arg(commandName), format);
	}
	QVector<LevelMapSelectionRef> objects;
	QString badSelector;
	if (!levelMapObjectsFromArgs(args, &objects, &badSelector)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unknown map object selector: %1").arg(badSelector), format);
	}
	if (objects.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --object kind:id for the carving brushes, repeatable.").arg(commandName), format);
	}
	const QString outputPath = optionValue(args, QStringLiteral("--output"));
	if (outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --output <save-as path>.").arg(commandName), format);
	}

	LevelMapDocument document;
	QString error;
	if (!loadLevelMap(request, &document, &error)) {
		return printCliError(commandName, levelMapLoadFailureCode(request), QStringLiteral("Unable to load map: %1").arg(error), format);
	}
	if (!setLevelMapSelection(&document, objects, &error)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unable to select map objects: %1").arg(error), format);
	}
	int carved = 0;
	QStringList skipped;
	if (!carveLevelMapSelection(&document, &carved, &error, &skipped)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to carve: %1").arg(error), format);
	}
	const LevelMapSaveReport report = saveLevelMapAs(document, outputPath, hasOption(args, QStringLiteral("--dry-run")), hasOption(args, QStringLiteral("--overwrite")));
	QStringList with;
	for (const LevelMapSelectionRef& ref : objects) {
		with << levelMapSelectionRefId(ref);
	}
	if (format != CliOutputFormat::Json) {
		std::cout << text((carved == 1 ? QStringLiteral("Carved 1 brush with %2") : QStringLiteral("Carved %1 brushes with %2").arg(carved))
					  .arg(with.join(QStringLiteral(", "))))
			  << "\n";
		for (const QString& reason : skipped) {
			std::cout << text(QStringLiteral("Left as it was: %1").arg(reason)) << "\n";
		}
	}
	QJsonObject extra;
	extra.insert(QStringLiteral("carved"), carved);
	extra.insert(QStringLiteral("skipped"), QJsonArray::fromStringList(skipped));
	return printLevelMapSaveResult(commandName, document, report, format, extra);
}

int runMapMergeBrushesCommand(const QString& path, const QStringList& args, CliOutputFormat format)
{
	const QString command = QStringLiteral("map merge-brushes");
	const auto usage = [&](const QString& message) { return printCliError(command, CliExitCode::Usage, message, format); };
	for (int i = 0; i < args.size(); ++i) {
		if ((args[i] == QStringLiteral("--face-source") || args[i] == QStringLiteral("--object") || args[i] == QStringLiteral("--output"))
			&& (i + 1 == args.size() || args[i + 1].startsWith(QStringLiteral("--")))) {
			return usage(QStringLiteral("%1 requires a value.").arg(args[i]));
		}
	}
	const QString output = optionValue(args, QStringLiteral("--output"));
	if (path.trimmed().isEmpty() || output.trimmed().isEmpty()) {
		return usage(QStringLiteral("map merge-brushes requires a map path, repeated --object selectors and --output <save-as path>."));
	}
	QVector<LevelMapSelectionRef> objects;
	QString error;
	if (!levelMapObjectsFromArgs(args, &objects, &error) || objects.isEmpty()) { return usage(QStringLiteral("Select brushes with repeated --object brush:id or --object entity:id.")); }
	LevelBrushMergeRequest merge;
	static const QRegularExpression sourcePattern(QStringLiteral(R"(^(\d+)=(\d+):(\d+)$)"));
	const auto choices = optionValues(args, QStringLiteral("--face-source"));
	if (hasOption(args, QStringLiteral("--face-source")) && choices.isEmpty()) { return usage(QStringLiteral("--face-source requires outputFace=brushId:sourceFace, all zero-based.")); }
	for (const auto& choice : choices) {
		const auto match = sourcePattern.match(choice);
		bool a = false, b = false, c = false;
		const int face = match.captured(1).toInt(&a), brush = match.captured(2).toInt(&b), source = match.captured(3).toInt(&c);
		if (!match.hasMatch() || !a || !b || !c || merge.faceSources.contains(face)) {
			return usage(QStringLiteral("Each --face-source must be outputFace=brushId:sourceFace with a unique output face; all IDs are zero-based."));
		}
		merge.faceSources.insert(face, {brush, source});
	}
	const auto load = levelMapLoadRequestFromArgs(path, args);
	LevelMapDocument document;
	if (!loadLevelMap(load, &document, &error)) { return printCliError(command, levelMapLoadFailureCode(load), error, format); }
	if (!setLevelMapSelection(&document, objects, &error)) { return usage(error); }
	LevelBrushMergePlan plan;
	if (!prepareLevelBrushMerge(document, merge, &plan, &error)) { return printCliError(command, CliExitCode::ValidationFailed, error, format); }
	if (!plan.ready()) {
		const auto message = QStringLiteral("Choose a source for each conflicting surface with --face-source outputFace=brushId:sourceFace. No output was written.");
		if (format == CliOutputFormat::Json) {
			auto result = cliResultJson(command, CliExitCode::ValidationFailed);
			result.insert(QStringLiteral("message"), message);
			result.insert(QStringLiteral("merge"), levelBrushMergeReportJson(plan));
			printJson(result);
		} else {
			std::cerr << text(message) << '\n';
			for (int f = 0; f < plan.geometry().faces.size(); ++f) {
				const auto& face = plan.geometry().faces[f];
				if (face.resolved) { continue; }
				QStringList options;
				for (const auto& source : face.sources) { options << QStringLiteral("%1=%2:%3").arg(f).arg(source.brushId).arg(source.faceIndex); }
				std::cerr << text(QStringLiteral("Face %1: %2").arg(f).arg(options.join(QStringLiteral(" or ")))) << '\n';
			}
		}
		return exitCodeValue(CliExitCode::ValidationFailed);
	}
	if (!commitLevelBrushMerge(&document, plan, &error)) { return printCliError(command, CliExitCode::Failure, error, format); }
	const auto saved = saveLevelMapAs(document, output, hasOption(args, QStringLiteral("--dry-run")), hasOption(args, QStringLiteral("--overwrite")));
	return printLevelMapSaveResult(command, document, saved, format, {{QStringLiteral("merge"), levelBrushMergeReportJson(plan)}});
}

int runMapHollowCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format)
{
	const LevelMapLoadRequest request = levelMapLoadRequestFromArgs(path, args);
	if (request.path.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a Quake-family .map path.").arg(commandName), format);
	}
	QVector<LevelMapSelectionRef> objects;
	QString badSelector;
	if (!levelMapObjectsFromArgs(args, &objects, &badSelector)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unknown map object selector: %1").arg(badSelector), format);
	}
	if (objects.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --object kind:id, repeatable.").arg(commandName), format);
	}
	bool thicknessOk = false;
	const double thickness = optionValue(args, QStringLiteral("--thickness")).trimmed().toDouble(&thicknessOk);
	if (!thicknessOk || thickness <= 0.0) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --thickness <units> above zero.").arg(commandName), format);
	}
	const QString outputPath = optionValue(args, QStringLiteral("--output"));
	if (outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --output <save-as path>.").arg(commandName), format);
	}

	LevelMapDocument document;
	QString error;
	if (!loadLevelMap(request, &document, &error)) {
		return printCliError(commandName, levelMapLoadFailureCode(request), QStringLiteral("Unable to load map: %1").arg(error), format);
	}
	if (!setLevelMapSelection(&document, objects, &error)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unable to select map objects: %1").arg(error), format);
	}
	int hollowed = 0;
	QStringList skipped;
	if (!hollowLevelMapSelection(&document, thickness, &hollowed, &error, &skipped)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to hollow: %1").arg(error), format);
	}
	const LevelMapSaveReport report = saveLevelMapAs(document, outputPath, hasOption(args, QStringLiteral("--dry-run")), hasOption(args, QStringLiteral("--overwrite")));
	QJsonArray walls;
	for (const LevelMapSelectionRef& ref : document.selection) {
		walls.append(levelMapSelectionRefId(ref));
	}
	if (format != CliOutputFormat::Json) {
		std::cout << text((hollowed == 1 ? QStringLiteral("Hollowed 1 brush into %2 walls") : QStringLiteral("Hollowed %1 brushes into %2 walls").arg(hollowed))
					  .arg(walls.size()))
			  << "\n";
		for (const QString& reason : skipped) {
			std::cout << text(QStringLiteral("Left as it was: %1").arg(reason)) << "\n";
		}
	}
	QJsonObject extra;
	extra.insert(QStringLiteral("hollowed"), hollowed);
	extra.insert(QStringLiteral("skipped"), QJsonArray::fromStringList(skipped));
	extra.insert(QStringLiteral("thickness"), thickness);
	extra.insert(QStringLiteral("walls"), walls);
	return printLevelMapSaveResult(commandName, document, report, format, extra);
}

int runMapClipCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format)
{
	const LevelMapLoadRequest request = levelMapLoadRequestFromArgs(path, args);
	if (request.path.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a Quake-family .map path.").arg(commandName), format);
	}
	QVector<LevelMapSelectionRef> objects;
	QString badSelector;
	if (!levelMapObjectsFromArgs(args, &objects, &badSelector)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unknown map object selector: %1").arg(badSelector), format);
	}
	if (objects.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --object kind:id, repeatable.").arg(commandName), format);
	}
	// The plane: --axis with --at for x, y, or z = at, whose front is up the
	// axis, or --points for any plane, whose front is (b - a) x (c - a).
	LevelMapVec3 a {0.0, 0.0, 0.0, true};
	LevelMapVec3 b {0.0, 0.0, 0.0, true};
	LevelMapVec3 c {0.0, 0.0, 0.0, true};
	if (hasOption(args, QStringLiteral("--points"))) {
		const QStringList points = optionValue(args, QStringLiteral("--points")).split(QRegularExpression(QStringLiteral(R"([\s;]+)")), Qt::SkipEmptyParts);
		LevelMapVec3* targets[3] {&a, &b, &c};
		bool parsed = points.size() == 3;
		for (int index = 0; parsed && index < 3; ++index) {
			parsed = parseLevelMapDelta(points.at(index), &targets[index]->x, &targets[index]->y, &targets[index]->z)
				&& points.at(index).split(QLatin1Char(','), Qt::SkipEmptyParts).size() == 3;
		}
		if (!parsed) {
			return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1: --points takes three points, \"x,y,z x,y,z x,y,z\".").arg(commandName), format);
		}
	} else {
		const int axis = QStringList {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z")}.indexOf(optionValue(args, QStringLiteral("--axis")).trimmed().toLower());
		bool atOk = false;
		const double at = optionValue(args, QStringLiteral("--at")).trimmed().toDouble(&atOk);
		if (axis < 0 || !atOk) {
			return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --axis x|y|z with --at <units>, or --points.").arg(commandName), format);
		}
		double* first[3] {&a.x, &a.y, &a.z};
		*first[axis] = at;
		b = a;
		c = a;
		// Two steps along the other axes, in the order that puts the normal up
		// this one: y then z for x, z then x for y, x then y for z.
		const int next = (axis + 1) % 3;
		const int after = (axis + 2) % 3;
		double* bAxes[3] {&b.x, &b.y, &b.z};
		double* cAxes[3] {&c.x, &c.y, &c.z};
		*bAxes[next] += 1.0;
		*cAxes[after] += 1.0;
	}
	const QString keepText = hasOption(args, QStringLiteral("--keep")) ? optionValue(args, QStringLiteral("--keep")).trimmed().toLower() : QStringLiteral("back");
	LevelMapClipKeep keep = LevelMapClipKeep::Back;
	if (keepText == QStringLiteral("front") || keepText == QStringLiteral("above")) {
		keep = LevelMapClipKeep::Front;
	} else if (keepText == QStringLiteral("both")) {
		keep = LevelMapClipKeep::Both;
	} else if (keepText != QStringLiteral("back") && keepText != QStringLiteral("below")) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1: --keep takes back, front, or both (below and above with --axis).").arg(commandName), format);
	}
	const QString outputPath = optionValue(args, QStringLiteral("--output"));
	if (outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --output <save-as path>.").arg(commandName), format);
	}

	LevelMapDocument document;
	QString error;
	if (!loadLevelMap(request, &document, &error)) {
		return printCliError(commandName, levelMapLoadFailureCode(request), QStringLiteral("Unable to load map: %1").arg(error), format);
	}
	if (!setLevelMapSelection(&document, objects, &error)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unable to select map objects: %1").arg(error), format);
	}
	int clipped = 0;
	QStringList skipped;
	if (!clipLevelMapSelection(&document, a, b, c, keep, &clipped, &error, &skipped)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to clip: %1").arg(error), format);
	}
	const LevelMapSaveReport report = saveLevelMapAs(document, outputPath, hasOption(args, QStringLiteral("--dry-run")), hasOption(args, QStringLiteral("--overwrite")));
	QJsonArray pieces;
	for (const LevelMapSelectionRef& ref : document.selection) {
		pieces.append(levelMapSelectionRefId(ref));
	}
	if (format != CliOutputFormat::Json) {
		QStringList names;
		for (const QJsonValue& piece : pieces) {
			names << piece.toString();
		}
		std::cout << text((clipped == 1 ? QStringLiteral("Clipped 1 brush into %2") : QStringLiteral("Clipped %1 brushes into %2").arg(clipped)).arg(names.join(QStringLiteral(", ")))) << "\n";
		for (const QString& reason : skipped) {
			std::cout << text(QStringLiteral("Left as it was: %1").arg(reason)) << "\n";
		}
	}
	QJsonObject extra;
	extra.insert(QStringLiteral("clipped"), clipped);
	extra.insert(QStringLiteral("skipped"), QJsonArray::fromStringList(skipped));
	extra.insert(QStringLiteral("pieces"), pieces);
	extra.insert(QStringLiteral("keep"), keep == LevelMapClipKeep::Both ? QStringLiteral("both") : keep == LevelMapClipKeep::Front ? QStringLiteral("front") : QStringLiteral("back"));
	return printLevelMapSaveResult(commandName, document, report, format, extra);
}

int runMapResizeCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format)
{
	LevelMapTextureLockOptions textures;
	QString textureError;
	if (!mapTextureLockFromArgs(args, false, &textures, &textureError)) {
		return printCliError(commandName, CliExitCode::Usage, textureError, format);
	}
	const LevelMapLoadRequest request = levelMapLoadRequestFromArgs(path, args);
	if (request.path.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a Doom WAD or Quake-family .map path.").arg(commandName), format);
	}
	QVector<LevelMapSelectionRef> objects;
	QString badSelector;
	if (!levelMapObjectsFromArgs(args, &objects, &badSelector)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unknown map object selector: %1").arg(badSelector), format);
	}
	if (objects.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --object kind:id, repeatable.").arg(commandName), format);
	}
	const bool bySize = hasOption(args, QStringLiteral("--size"));
	const bool byBounds = hasOption(args, QStringLiteral("--mins")) || hasOption(args, QStringLiteral("--maxs"));
	if (bySize == byBounds) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires either --size x,y,z or --mins x,y,z with --maxs x,y,z.").arg(commandName), format);
	}
	const QString outputPath = optionValue(args, QStringLiteral("--output"));
	if (outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --output <save-as path>.").arg(commandName), format);
	}

	LevelMapDocument document;
	QString error;
	if (!loadLevelMap(request, &document, &error)) {
		return printCliError(commandName, levelMapLoadFailureCode(request), QStringLiteral("Unable to load map: %1").arg(error), format);
	}
	if (!setLevelMapSelection(&document, objects, &error)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unable to select map objects: %1").arg(error), format);
	}
	LevelMapVec3 oldMins;
	LevelMapVec3 oldMaxs;
	if (!levelMapSelectionBounds(document, &oldMins, &oldMaxs)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to resize map objects: nothing selected has a size."), format);
	}
	// Two numbers leave z as it is, which suits Doom maps.
	const auto readVector = [&args](const QString& option, const LevelMapVec3& current, LevelMapVec3* result) {
		const QString value = optionValue(args, option);
		*result = current;
		if (!parseLevelMapDelta(value, &result->x, &result->y, &result->z)) {
			return false;
		}
		if (value.split(QRegularExpression(QStringLiteral(R"([,\s]+)")), Qt::SkipEmptyParts).size() == 2) {
			result->z = current.z;
		}
		result->valid = true;
		return true;
	};
	LevelMapVec3 mins = oldMins;
	LevelMapVec3 maxs = oldMaxs;
	if (bySize) {
		LevelMapVec3 size {oldMaxs.x - oldMins.x, oldMaxs.y - oldMins.y, oldMaxs.z - oldMins.z, true};
		if (!readVector(QStringLiteral("--size"), size, &size)) {
			return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1: --size takes x,y,z in map units.").arg(commandName), format);
		}
		maxs = {oldMins.x + size.x, oldMins.y + size.y, oldMins.z + size.z, true};
	} else if (!readVector(QStringLiteral("--mins"), oldMins, &mins) || !readVector(QStringLiteral("--maxs"), oldMaxs, &maxs)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --mins x,y,z and --maxs x,y,z.").arg(commandName), format);
	}
	if (!resizeLevelMapSelection(&document, mins, maxs, textures, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to resize map objects: %1").arg(error), format);
	}
	const LevelMapSaveReport report = saveLevelMapAs(document, outputPath, hasOption(args, QStringLiteral("--dry-run")), hasOption(args, QStringLiteral("--overwrite")));
	if (format != CliOutputFormat::Json) {
		const auto vectorText = [](const LevelMapVec3& value) {
			return QStringLiteral("%1 %2 %3").arg(value.x, 0, 'g', 10).arg(value.y, 0, 'g', 10).arg(value.z, 0, 'g', 10);
		};
		std::cout << text(QStringLiteral("Resized: %1 .. %2 to %3 .. %4").arg(vectorText(oldMins), vectorText(oldMaxs), vectorText(mins), vectorText(maxs)))
			  << "\n";
	}
	QJsonObject extra;
	extra.insert(QStringLiteral("textureLockPolicy"), textures.enabled ? QStringLiteral("locked") : QStringLiteral("source-parameters"));
	extra.insert(QStringLiteral("allowValve220"), textures.allowValve220);
	extra.insert(QStringLiteral("fromMins"), levelMapVec3Json(oldMins));
	extra.insert(QStringLiteral("fromMaxs"), levelMapVec3Json(oldMaxs));
	extra.insert(QStringLiteral("mins"), levelMapVec3Json(mins));
	extra.insert(QStringLiteral("maxs"), levelMapVec3Json(maxs));
	return printLevelMapSaveResult(commandName, document, report, format, extra);
}

int runMapSnapCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format)
{
	LevelMapTextureLockOptions textures;
	QString textureError;
	if (!mapTextureLockFromArgs(args, true, &textures, &textureError)) {
		return printCliError(commandName, CliExitCode::Usage, textureError, format);
	}
	const LevelMapLoadRequest request = levelMapLoadRequestFromArgs(path, args);
	if (request.path.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a Doom WAD or Quake-family .map path.").arg(commandName), format);
	}
	QVector<LevelMapSelectionRef> objects;
	QString badSelector;
	if (!levelMapObjectsFromArgs(args, &objects, &badSelector)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unknown map object selector: %1").arg(badSelector), format);
	}
	if (objects.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --object kind:id, repeatable.").arg(commandName), format);
	}
	bool gridOk = false;
	const double grid = hasOption(args, QStringLiteral("--grid")) ? optionValue(args, QStringLiteral("--grid")).toDouble(&gridOk) : 16.0;
	if (hasOption(args, QStringLiteral("--grid")) && (!gridOk || !std::isfinite(grid) || grid <= 0.0)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1: --grid takes a positive size in map units.").arg(commandName), format);
	}
	const QString outputPath = optionValue(args, QStringLiteral("--output"));
	if (outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --output <save-as path>.").arg(commandName), format);
	}

	LevelMapDocument document;
	QString error;
	if (!loadLevelMap(request, &document, &error)) {
		return printCliError(commandName, levelMapLoadFailureCode(request), QStringLiteral("Unable to load map: %1").arg(error), format);
	}
	if (!setLevelMapSelection(&document, objects, &error)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unable to select map objects: %1").arg(error), format);
	}
	LevelPlacementRequest placement;
	placement.operation = LevelPlacementOperation::Snap; placement.grid = grid; placement.textures = textures;
	auto prepared = prepareLevelPlacement(document, placement);
	if (!prepared.succeeded) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to snap map objects: %1").arg(prepared.error), format);
	}
	document = std::move(prepared.document);
	const LevelMapSaveReport report = saveLevelMapAs(document, outputPath, hasOption(args, QStringLiteral("--dry-run")), hasOption(args, QStringLiteral("--overwrite")));
	QJsonArray moved;
	for (const LevelMapMoveStep& step : document.undoStack.last().moveSteps) {
		moved.append(QStringLiteral("%1:%2").arg(step.objectKind).arg(step.objectId));
	}
	QJsonObject extra;
	extra.insert(QStringLiteral("snapped"), moved);
	extra.insert(QStringLiteral("grid"), grid);
	extra.insert(QStringLiteral("textureLockPolicy"), textures.enabled ? QStringLiteral("locked") : QStringLiteral("source-parameters"));
	return printLevelMapSaveResult(commandName, document, report, format, extra);
}

int runMapDuplicateCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format)
{
	LevelMapTextureLockOptions textures;
	QString textureError;
	if (!mapTextureLockFromArgs(args, true, &textures, &textureError)) {
		return printCliError(commandName, CliExitCode::Usage, textureError, format);
	}
	const LevelMapLoadRequest request = levelMapLoadRequestFromArgs(path, args);
	if (request.path.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a Quake-family .map path.").arg(commandName), format);
	}
	QVector<LevelMapSelectionRef> objects;
	QString badSelector;
	if (!levelMapObjectsFromArgs(args, &objects, &badSelector)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unknown map object selector: %1").arg(badSelector), format);
	}
	if (objects.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --object kind:id, repeatable.").arg(commandName), format);
	}
	// Without --delta the copies sit exactly on the originals.
	double dx = 0.0;
	double dy = 0.0;
	double dz = 0.0;
	if (hasOption(args, QStringLiteral("--delta")) && !parseLevelMapDelta(optionValue(args, QStringLiteral("--delta")), &dx, &dy, &dz)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --delta x,y,z.").arg(commandName), format);
	}
	const QString outputPath = optionValue(args, QStringLiteral("--output"));
	if (outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --output <save-as path>.").arg(commandName), format);
	}

	LevelMapDocument document;
	QString error;
	if (!loadLevelMap(request, &document, &error)) {
		return printCliError(commandName, levelMapLoadFailureCode(request), QStringLiteral("Unable to load map: %1").arg(error), format);
	}
	document.selection = objects;
	LevelPlacementRequest placement;
	placement.offset = {dx, dy, dz, true}; placement.textures = textures;
	auto prepared = prepareLevelPlacement(document, placement);
	if (!prepared.succeeded) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to duplicate map objects: %1").arg(prepared.error), format);
	}
	document = std::move(prepared.document);
	const LevelMapSaveReport report = saveLevelMapAs(document, outputPath, hasOption(args, QStringLiteral("--dry-run")), hasOption(args, QStringLiteral("--overwrite")));
	QJsonArray copies;
	for (const LevelMapSelectionRef& ref : document.selection) {
		copies.append(levelMapSelectionRefId(ref));
	}
	if (format != CliOutputFormat::Json) {
		QStringList names;
		for (const QJsonValue& copy : copies) {
			names << copy.toString();
		}
		std::cout << text(QStringLiteral("Copies: %1").arg(names.join(QStringLiteral(", ")))) << "\n";
	}
	QJsonObject extra;
	extra.insert(QStringLiteral("copies"), copies);
	extra.insert(QStringLiteral("delta"), levelMapVec3Json({dx, dy, dz, true}));
	extra.insert(QStringLiteral("textureLockPolicy"), textures.enabled ? QStringLiteral("locked") : QStringLiteral("source-parameters"));
	return printLevelMapSaveResult(commandName, document, report, format, extra);
}

int runMapPasteCommand(const QString& path, const QStringList& args, CliOutputFormat format)
{
	const QString commandName = QStringLiteral("map paste");
	const QSet<QString> values{QStringLiteral("--from"), QStringLiteral("--delta"), QStringLiteral("--output"),
		QStringLiteral("--texture-lock"), QStringLiteral("--engine"), QStringLiteral("--engine-hint"), QStringLiteral("--settings-file"),
		QStringLiteral("--locale"), QStringLiteral("--catalog-root")};
	const QSet<QString> flags{QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--dry-run"),
		QStringLiteral("--overwrite"), QStringLiteral("--quiet"), QStringLiteral("--verbose")};
	QSet<QString> seen;
	QStringList positional;
	for (int i = 1; i < args.size(); ++i) {
		const auto arg = args[i];
		if (!arg.startsWith(QLatin1Char('-'))) { positional << arg; continue; }
		const auto equal = arg.indexOf('=');
		const auto key = equal < 0 ? arg : arg.left(equal);
		if ((!values.contains(key) && !flags.contains(key)) || seen.contains(key)) {
			return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unknown or repeated option: %1").arg(key), format);
		}
		seen.insert(key);
		if ((flags.contains(key) && equal >= 0) || (values.contains(key) &&
			(equal >= 0 ? arg.mid(equal+1).isEmpty() : (++i >= args.size() || args[i].startsWith(QStringLiteral("--")))))) {
			return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Missing value for %1.").arg(key), format);
		}
	}
	LevelMapTextureLockOptions textures;
	QString error;
	if (!mapTextureLockFromArgs(args, true, &textures, &error)) { return printCliError(commandName, CliExitCode::Usage, error, format); }
	const auto input = optionValue(args, QStringLiteral("--from")), output = optionValue(args, QStringLiteral("--output"));
	LevelMapVec3 delta{0, 0, 0, true};
	if (positional.size() != 3 || path.isEmpty() || input.isEmpty() || output.isEmpty() || (hasOption(args, QStringLiteral("--delta")) &&
		!parseLevelMapDelta(optionValue(args, QStringLiteral("--delta")), &delta.x, &delta.y, &delta.z))) {
		return printCliError(commandName, CliExitCode::Usage,
			QStringLiteral("map paste <map> requires --from <map-text file>, --output <path>, and an optional --delta x,y,z."), format);
	}
	QFile file(input);
	if (!file.open(QIODevice::ReadOnly)) { return printCliError(commandName, CliExitCode::NotFound, file.errorString(), format); }
	const auto bytes = file.read(kLevelMapClipboardMaxCharacters * 2 + 1);
	if (bytes.size() > kLevelMapClipboardMaxCharacters * 2 || file.error() != QFile::NoError) {
		return printCliError(commandName, CliExitCode::ValidationFailed, QStringLiteral("The map-text input could not be read within the 8 MiB limit."), format);
	}
	QStringDecoder decoder(QStringDecoder::Utf8);
	const QString textInput = decoder(bytes);
	if (decoder.hasError()) {
		return printCliError(commandName, CliExitCode::ValidationFailed, QStringLiteral("The map-text input must be valid UTF-8."), format);
	}
	LevelMapDocument document;
	const auto request = levelMapLoadRequestFromArgs(path, args);
	if (!loadLevelMap(request, &document, &error)) { return printCliError(commandName, levelMapLoadFailureCode(request), error, format); }
	LevelPlacementRequest placement;
	placement.operation = LevelPlacementOperation::Paste; placement.text = textInput; placement.offset = delta; placement.textures = textures;
	auto prepared = prepareLevelPlacement(document, placement);
	if (!prepared.succeeded) {
		return printCliError(commandName, CliExitCode::Failure, prepared.error, format);
	}
	document = std::move(prepared.document);
	const auto report = saveLevelMapAs(document, output, hasOption(args, QStringLiteral("--dry-run")), hasOption(args, QStringLiteral("--overwrite")));
	QJsonArray added;
	for (const auto& ref : document.selection) { added.append(levelMapSelectionRefId(ref)); }
	return printLevelMapSaveResult(commandName, document, report, format,
		{{QStringLiteral("pasted"), added}, {QStringLiteral("delta"), levelMapVec3Json(delta)},
		 {QStringLiteral("textureLockPolicy"), textures.enabled ? QStringLiteral("locked") : QStringLiteral("source-parameters")}});
}

int runMapCompilePlanCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format)
{
	const LevelMapLoadRequest request = levelMapLoadRequestFromArgs(path, args);
	if (request.path.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a Doom WAD or Quake-family .map path.").arg(commandName), format);
	}
	LevelMapDocument document;
	QString error;
	if (!loadLevelMap(request, &document, &error)) {
		return printCliError(commandName, levelMapLoadFailureCode(request), QStringLiteral("Unable to load map: %1").arg(error), format);
	}
	const QString profileId = hasOption(args, QStringLiteral("--compiler-profile")) ? optionValue(args, QStringLiteral("--compiler-profile")) : optionValue(args, QStringLiteral("--profile"));
	CompilerCommandRequest compilerRequest = compilerRequestForLevelMap(document, profileId, optionValue(args, QStringLiteral("--output")));
	compilerRequest.workingDirectory = optionValue(args, QStringLiteral("--working-directory")).trimmed().isEmpty() ? compilerRequest.workingDirectory : optionValue(args, QStringLiteral("--working-directory"));
	compilerRequest.workspaceRootPath = optionValue(args, QStringLiteral("--workspace-root"));
	const QString extraArgs = optionValue(args, QStringLiteral("--extra-args"));
	if (!extraArgs.trimmed().isEmpty()) {
		compilerRequest.extraArguments += QProcess::splitCommand(extraArgs);
	}
	applyCompilerRequestContext(&compilerRequest, args);
	const CompilerCommandPlan plan = buildCompilerCommandPlan(compilerRequest);
	const CliExitCode code = plan.errors.isEmpty() ? CliExitCode::Success : (!plan.profileFound ? CliExitCode::Usage : CliExitCode::NotFound);
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("map"), levelMapDocumentJson(document));
		object.insert(QStringLiteral("plan"), compilerCommandPlanJson(plan));
		printJson(object);
	} else {
		std::cout << text(levelMapReportText(document)) << "\n";
		std::cout << text(compilerCommandPlanText(plan)) << "\n";
	}
	return exitCodeValue(code);
}

QStringList shaderPackagePathsFromArgs(const QStringList& args)
{
	QStringList paths = optionValues(args, QStringLiteral("--package"));
	paths += optionValues(args, QStringLiteral("--mounted-package"));
	QStringList normalized;
	for (const QString& path : paths) {
		const QString trimmed = path.trimmed();
		if (!trimmed.isEmpty() && !normalized.contains(trimmed)) {
			normalized.push_back(trimmed);
		}
	}
	return normalized;
}

int runShaderInspectCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format)
{
	const QString shaderPath = path.trimmed().isEmpty() ? optionValue(args, QStringLiteral("--input")) : path;
	if (shaderPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a shader script path.").arg(commandName), format);
	}
	ShaderDocument document;
	QString error;
	if (!loadShaderScript(shaderPath, &document, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to load shader script: %1").arg(error), format);
	}
	QStringList validationWarnings;
	const QVector<ShaderReferenceValidation> validation = validateShaderReferences(document, shaderPackagePathsFromArgs(args), &validationWarnings);
	const bool missingReference = std::any_of(validation.cbegin(), validation.cend(), [](const ShaderReferenceValidation& item) {
		return !item.found;
	});
	const CliExitCode code = document.shaders.isEmpty() ? CliExitCode::ValidationFailed : (missingReference && !validation.isEmpty() ? CliExitCode::ValidationFailed : CliExitCode::Success);
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("shader"), advancedShaderDocumentJson(document, validation, validationWarnings));
		printJson(object);
	} else {
		std::cout << text(shaderDocumentReportText(document, validation, validationWarnings)) << "\n";
	}
	return exitCodeValue(code);
}

int runShaderSetStageCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format)
{
	const QString shaderPath = path.trimmed().isEmpty() ? optionValue(args, QStringLiteral("--input")) : path;
	if (shaderPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a shader script path.").arg(commandName), format);
	}
	const QString shaderName = optionValue(args, QStringLiteral("--shader"));
	const QString directive = optionValue(args, QStringLiteral("--directive"));
	const QString value = optionValue(args, QStringLiteral("--value"));
	const QString outputPath = optionValue(args, QStringLiteral("--output"));
	bool stageOk = false;
	const int stageIndex = optionValue(args, QStringLiteral("--stage")).toInt(&stageOk);
	if (shaderName.trimmed().isEmpty() || !stageOk || directive.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --shader <name> --stage <index> --directive <name> --value <text>.").arg(commandName), format);
	}
	if (outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --output <save-as path>.").arg(commandName), format);
	}

	ShaderDocument document;
	QString error;
	if (!loadShaderScript(shaderPath, &document, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to load shader script: %1").arg(error), format);
	}
	if (!setShaderStageDirective(&document, shaderName, stageIndex, directive, value, &error)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unable to edit shader stage: %1").arg(error), format);
	}
	const ShaderSaveReport report = saveShaderScriptAs(document, outputPath, hasOption(args, QStringLiteral("--dry-run")), hasOption(args, QStringLiteral("--overwrite")));
	QStringList validationWarnings;
	const QVector<ShaderReferenceValidation> validation = validateShaderReferences(document, shaderPackagePathsFromArgs(args), &validationWarnings);
	const CliExitCode code = report.succeeded() ? CliExitCode::Success : CliExitCode::ValidationFailed;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("shader"), advancedShaderDocumentJson(document, validation, validationWarnings));
		object.insert(QStringLiteral("save"), shaderSaveReportJson(report));
		printJson(object);
	} else {
		std::cout << text(shaderSaveReportText(report)) << "\n";
		std::cout << text(shaderDocumentReportText(document, validation, validationWarnings)) << "\n";
	}
	return exitCodeValue(code);
}

int runSpritePlanCommand(const QString& commandName, const QStringList& args, CliOutputFormat format)
{
	SpriteWorkflowRequest request;
	request.engineFamily = optionValue(args, QStringLiteral("--engine")).trimmed().isEmpty() ? QStringLiteral("doom") : optionValue(args, QStringLiteral("--engine"));
	request.spriteName = hasOption(args, QStringLiteral("--sprite-name")) ? optionValue(args, QStringLiteral("--sprite-name")) : optionValue(args, QStringLiteral("--name"));
	if (request.spriteName.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --name <sprite-name>.").arg(commandName), format);
	}
	bool framesOk = true;
	bool rotationsOk = true;
	if (hasOption(args, QStringLiteral("--frames"))) {
		request.frameCount = optionValue(args, QStringLiteral("--frames")).toInt(&framesOk);
	}
	if (hasOption(args, QStringLiteral("--rotations"))) {
		request.rotations = optionValue(args, QStringLiteral("--rotations")).toInt(&rotationsOk);
	}
	if (!framesOk || request.frameCount <= 0) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("--frames must be a positive integer."), format);
	}
	if (!rotationsOk || request.rotations < 0) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("--rotations must be 0 through 8 for Doom workflows."), format);
	}
	request.paletteId = optionValue(args, QStringLiteral("--palette")).trimmed().isEmpty() ? QStringLiteral("generic") : optionValue(args, QStringLiteral("--palette"));
	request.outputPackageRoot = optionValue(args, QStringLiteral("--package-root")).trimmed().isEmpty() ? QStringLiteral("sprites") : optionValue(args, QStringLiteral("--package-root"));
	request.sourceFramePaths = optionValues(args, QStringLiteral("--source-frame"));
	request.stageForPackage = !hasOption(args, QStringLiteral("--no-stage"));
	const SpriteWorkflowPlan plan = buildSpriteWorkflowPlan(request);
	const CliExitCode code = plan.state == OperationState::Failed ? CliExitCode::Failure : CliExitCode::Success;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("sprite"), spriteWorkflowPlanJson(plan));
		printJson(object);
	} else {
		std::cout << text(spriteWorkflowPlanText(plan)) << "\n";
	}
	return exitCodeValue(code);
}

int runCodeTextCommand(const QString& action, const QString& path, const QStringList& args, CliOutputFormat format)
{
	const QString commandName = QStringLiteral("code %1").arg(action);
	const bool saving = action == QStringLiteral("text-save");
	const bool dryRun = !hasOption(args, QStringLiteral("--write")) || hasOption(args, QStringLiteral("--dry-run"));
	if (path.isEmpty()) { return printCliError(commandName, CliExitCode::Usage, QStringLiteral("A text file path is required."), format); }
	const QString inputPath = optionValue(args, QStringLiteral("--input"));
	const QString expected = optionValue(args, QStringLiteral("--expected-sha256"));
	static const QRegularExpression sha256Pattern(QStringLiteral("^[0-9a-fA-F]{64}$"));
	if (saving && (inputPath.isEmpty() || (!dryRun && expected.isEmpty()) || (!expected.isEmpty() && !sha256Pattern.match(expected).hasMatch()))) {
		return printCliError(commandName, CliExitCode::Usage,
			QStringLiteral("text-save requires --input <edited-text-file>; --write also requires --expected-sha256 <64 hex digits> from text-info."), format);
	}
	const TextFileDocument document = readTextFile(path);
	CliExitCode code = document.editable() ? CliExitCode::Success
		: !QFileInfo::exists(path) ? CliExitCode::NotFound : CliExitCode::ValidationFailed;
	QJsonObject details {
		{QStringLiteral("path"), document.path},
		{QStringLiteral("encoding"), document.readable ? textEncodingName(document.encoding) : QString()},
		{QStringLiteral("byteOrderMark"), document.byteOrderMark},
		{QStringLiteral("sha256"), document.truncated || !document.readable ? QString() : QString::fromLatin1(document.sha256.toHex())},
		{QStringLiteral("byteCount"), document.originalBytes.size()},
		{QStringLiteral("editable"), document.editable()},
		{QStringLiteral("truncated"), document.truncated},
		{QStringLiteral("preferredLineEnding"), textLineEndingName(document.preferredLineEnding)},
		{QStringLiteral("finalNewline"), document.text.endsWith(QLatin1Char('\n'))},
		{QStringLiteral("error"), document.error},
	};
	QJsonObject endings;
	for (const auto ending : document.lineEndings) {
		const QString name = textLineEndingName(ending);
		endings.insert(name, endings.value(name).toInt() + 1);
	}
	details.insert(QStringLiteral("lineEndings"), endings);
	TextFileSaveResult saved;
	if (saving && document.editable()) {
		const TextFileDocument input = readTextFile(inputPath);
		if (!input.editable()) {
			return printCliError(commandName, CliExitCode::ValidationFailed, QStringLiteral("Input text could not be loaded: %1").arg(input.error), format);
		}
		saved = saveTextFile(document, input.text, dryRun, QByteArray::fromHex(expected.toLatin1()));
		code = saved.succeeded ? CliExitCode::Success : CliExitCode::ValidationFailed;
		details.insert(QStringLiteral("save"), QJsonObject {
			{QStringLiteral("dryRun"), saved.dryRun},
			{QStringLiteral("succeeded"), saved.succeeded},
			{QStringLiteral("changed"), saved.changed},
			{QStringLiteral("conflict"), saved.conflict},
			{QStringLiteral("bytesWritten"), saved.bytesWritten},
			{QStringLiteral("outputSha256"), QString::fromLatin1(saved.outputSha256.toHex())},
			{QStringLiteral("error"), saved.error},
		});
	}
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("textDocument"), details);
		printJson(object);
	} else {
		std::cout << "Text document: " << text(document.path) << "\n"
			<< "Format: " << text(textFileFormatLabel(document)) << "\n"
			<< "Editable: " << (document.editable() ? "yes" : "no") << "\n"
			<< "SHA-256: " << text(details.value(QStringLiteral("sha256")).toString()) << "\n";
		if (!document.error.isEmpty()) { std::cout << text(document.error) << "\n"; }
		if (saving && document.editable()) {
			std::cout << "Save: " << (saved.succeeded ? (dryRun ? "preview" : saved.changed ? "written" : "unchanged") : "blocked") << "\n";
			if (!saved.error.isEmpty()) { std::cout << text(saved.error) << "\n"; }
		}
	}
	return exitCodeValue(code);
}

int runCodeTextWriteCommand(const QString& action, const QString& path, const QStringList& args, CliOutputFormat format)
{
	const QString commandName = QStringLiteral("code %1").arg(action);
	const bool creating = action == QStringLiteral("text-create");
	const bool recovering = action == QStringLiteral("text-recover");
	const bool dryRun = !hasOption(args, QStringLiteral("--write")) || hasOption(args, QStringLiteral("--dry-run"));
	const QString output = creating ? path : optionValue(args, QStringLiteral("--output"));
	const QString input = optionValue(args, QStringLiteral("--input"));
	const QString expected = optionValue(args, QStringLiteral("--expected-sha256"));
	static const QRegularExpression hashPattern(QStringLiteral("^[0-9a-fA-F]{64}$"));
	if (path.isEmpty() || output.isEmpty() || (recovering && !input.isEmpty()) || (!expected.isEmpty() && !hashPattern.match(expected).hasMatch())) {
		return printCliError(commandName, CliExitCode::Usage,
			QStringLiteral("Supply a file path, --output for text-save-as or text-recover, and a valid --expected-sha256 when replacing a destination. Recovery does not accept --input."), format);
	}
	TextFileDocument source;
	if (recovering) {
		const auto record = inspectTextRecovery(path, &source);
		if (!record.isValid()) { return printCliError(commandName, CliExitCode::ValidationFailed, record.error, format); }
	} else {
		source = creating ? (input.isEmpty() ? decodeTextFile({}) : readTextFile(input)) : readTextFile(path);
	}
	const auto edited = input.isEmpty() ? source : readTextFile(input);
	if (!source.editable() || !edited.editable()) {
		return printCliError(commandName, CliExitCode::ValidationFailed, source.editable() ? edited.error : source.error, format);
	}
	const auto target = inspectTextWriteTarget(output);
	if (target.existed && !dryRun && expected.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage,
			QStringLiteral("Replacing an existing destination requires --expected-sha256 from the preview's destinationSha256 or text-info."), format);
	}
	TextFileSaveResult saved;
	if (!expected.isEmpty() && (!target.existed || target.sha256 != QByteArray::fromHex(expected.toLatin1()))) {
		saved.dryRun = dryRun;
		saved.conflict = true;
		saved.error = QStringLiteral("The destination does not match --expected-sha256. Nothing was written.");
	} else {
		saved = saveTextFileAs(source, edited.text, target, dryRun);
	}
	const auto code = saved.succeeded ? CliExitCode::Success : CliExitCode::ValidationFailed;
	const QJsonObject details {
		{QStringLiteral("path"), target.path},
		{QStringLiteral("destinationExists"), target.existed},
		{QStringLiteral("destinationSha256"), QString::fromLatin1(target.sha256.toHex())},
		{QStringLiteral("format"), textFileFormatLabel(source)},
		{QStringLiteral("save"), QJsonObject {
			{QStringLiteral("dryRun"), saved.dryRun}, {QStringLiteral("succeeded"), saved.succeeded},
			{QStringLiteral("changed"), saved.changed}, {QStringLiteral("conflict"), saved.conflict},
			{QStringLiteral("bytesWritten"), saved.bytesWritten},
			{QStringLiteral("outputSha256"), QString::fromLatin1(saved.outputSha256.toHex())},
			{QStringLiteral("error"), saved.error}}},
	};
	if (format == CliOutputFormat::Json) {
		auto result = cliResultJson(commandName, code);
		result.insert(QStringLiteral("textDocument"), details);
		printJson(result);
	} else {
		std::cout << "Destination: " << text(target.path) << "\n"
			<< "Destination SHA-256: " << target.sha256.toHex().constData() << "\n"
			<< "Save: " << (saved.succeeded ? (dryRun ? "preview" : "saved") : "blocked") << "\n";
		if (!saved.error.isEmpty()) { std::cout << text(saved.error) << "\n"; }
	}
	return exitCodeValue(code);
}

int runCodeRecoveriesCommand(const QStringList& args, CliOutputFormat format)
{
	const QString directory = optionValue(args, QStringLiteral("--directory")).isEmpty()
		? textRecoveryDirectory() : optionValue(args, QStringLiteral("--directory"));
	const auto scan = listTextRecoveries(directory);
	QJsonArray records;
	bool valid = scan.error.isEmpty() && !scan.limited;
	for (const auto& record : scan.records) {
		valid = valid && record.isValid();
		records.append(QJsonObject {
			{QStringLiteral("path"), record.path}, {QStringLiteral("title"), record.title},
			{QStringLiteral("sourcePath"), record.sourcePath}, {QStringLiteral("valid"), record.isValid()},
			{QStringLiteral("writtenUtc"), record.writtenUtc.toString(Qt::ISODateWithMs)},
			{QStringLiteral("payloadBytes"), record.payloadBytes}, {QStringLiteral("error"), record.error},
		});
	}
	const auto code = valid ? CliExitCode::Success : CliExitCode::ValidationFailed;
	if (format == CliOutputFormat::Json) {
		auto object = cliResultJson(QStringLiteral("code recoveries"), code);
		object.insert(QStringLiteral("directory"), directory);
		object.insert(QStringLiteral("recoveries"), records);
		object.insert(QStringLiteral("limited"), scan.limited);
		object.insert(QStringLiteral("error"), scan.error);
		printJson(object);
	} else {
		std::cout << "Text recovery folder: " << text(directory) << "\n";
		for (const auto& record : scan.records) { std::cout << text(record.path) << " | " << text(record.isValid() ? record.title : record.error) << "\n"; }
		if (!scan.error.isEmpty()) { std::cout << text(scan.error) << "\n"; }
		if (scan.limited) { std::cout << "Scan limit reached; more copies may exist.\n"; }
	}
	return exitCodeValue(code);
}

int runCodeFilesCommand(const QString& rootPath, const QStringList& args, CliOutputFormat format, bool includeAssets = false)
{
	const QString commandName = includeAssets ? QStringLiteral("project files") : QStringLiteral("code files");
	CodeFilesRequest request;
	request.includeAssets = includeAssets;
	request.maxFiles = includeAssets ? 20000 : 4000;
	request.rootPath = rootPath.trimmed().isEmpty() ? optionValue(args, QStringLiteral("--workspace-root")) : rootPath;
	if (request.rootPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a project root path.").arg(commandName), format);
	}
	if (hasOption(args, QStringLiteral("--max-files"))) {
		bool valid = false;
		request.maxFiles = optionValue(args, QStringLiteral("--max-files")).toInt(&valid);
		if (!valid || request.maxFiles < 1 || request.maxFiles > 20000) {
			return printCliError(commandName, CliExitCode::Usage, QStringLiteral("--max-files must be between 1 and 20000."), format);
		}
	}
	const QString where = optionValue(args, QStringLiteral("--where"));
	const int whereArgument = args.indexOf(QStringLiteral("--where"));
	if (hasOption(args, QStringLiteral("--where")) && (where.trimmed().isEmpty()
		|| (whereArgument >= 0 && where.startsWith(QStringLiteral("--"))))) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("--where requires a file query."), format);
	}
	const StudioQuery query = parseStudioQuery(where);
	const QStringList unknown = studioQueryUnknownKeys(query, {QStringLiteral("path"), QStringLiteral("name"), QStringLiteral("ext"), QStringLiteral("folder"), QStringLiteral("size"), QStringLiteral("language"), QStringLiteral("kind")});
	if (!unknown.isEmpty()) { return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unknown file field(s): %1").arg(unknown.join(QStringLiteral(", "))), format); }
	const CodeFilesResult result = listCodeFiles(request);
	const CliExitCode code = !result.error.isEmpty() ? CliExitCode::Failure : result.complete ? CliExitCode::Success : CliExitCode::ValidationFailed;
	QJsonArray files;
	for (const auto& file : result.files) {
		if (!studioQueryMatches(query, codeFileQueryProperties(file), file.relativePath)) { continue; }
		files.push_back(QJsonObject {{QStringLiteral("filePath"), file.filePath}, {QStringLiteral("relativePath"), file.relativePath},
			{QStringLiteral("language"), file.languageId}, {QStringLiteral("kind"), file.kind}, {QStringLiteral("sizeBytes"), file.sizeBytes}});
	}
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(includeAssets ? QStringLiteral("projectFiles") : QStringLiteral("codeFiles"), QJsonObject {{QStringLiteral("rootPath"), result.rootPath}, {QStringLiteral("files"), files},
			{QStringLiteral("complete"), result.complete}, {QStringLiteral("cancelled"), result.cancelled}, {QStringLiteral("entriesVisited"), result.entriesVisited},
			{QStringLiteral("filesScanned"), result.files.size()}, {QStringLiteral("directoriesExcluded"), result.directoriesExcluded}, {QStringLiteral("linksExcluded"), result.linksExcluded},
			{QStringLiteral("query"), where}, {QStringLiteral("warnings"), QJsonArray::fromStringList(result.warnings)}, {QStringLiteral("error"), result.error}});
		printJson(object);
	} else {
		std::cout << "Project files: " << text(result.rootPath) << "\n";
		std::cout << files.size() << " match(es), " << result.files.size() << " file(s) listed; " << (result.complete ? "complete" : "incomplete") << "\n";
		for (const auto& item : files) {
			const auto file = item.toObject();
			std::cout << text(file.value(QStringLiteral("relativePath")).toString()) << " [" << text(file.value(QStringLiteral("language")).toString())
				<< ", " << file.value(QStringLiteral("sizeBytes")).toInteger() << " bytes]\n";
		}
		for (const auto& warning : result.warnings) { std::cout << text(warning) << "\n"; }
		if (!result.error.isEmpty()) { std::cout << text(result.error) << "\n"; }
	}
	return exitCodeValue(code);
}

int runCodeLanguageServerCommand(const QString& path, const QStringList& args, CliOutputFormat format)
{
	const QString command = QStringLiteral("code language-server");
	const auto usage = [&](const QString& error) { return printCliError(command, CliExitCode::Usage, error, format); };
	if (path.isEmpty() || !hasOption(args, QStringLiteral("--server"))) { return usage(QStringLiteral("A source path and --server absolute executable are required.")); }
	LanguageServerConfig config;
	config.program = optionValue(args, QStringLiteral("--server"));
	config.rootPath = hasOption(args, QStringLiteral("--root")) ? QFileInfo(optionValue(args, QStringLiteral("--root"))).absoluteFilePath() : QFileInfo(path).absolutePath();
	if (!QDir::isAbsolutePath(config.program)) { return usage(QStringLiteral("--server must be an absolute executable path.")); }
	if (hasOption(args, QStringLiteral("--server-args"))) {
		const auto arguments = QJsonDocument::fromJson(optionValue(args, QStringLiteral("--server-args")).toUtf8());
		if (!arguments.isArray()) { return usage(QStringLiteral("--server-args must be a JSON array of argument strings.")); }
		for (const auto& value : arguments.array()) { if (!value.isString()) { return usage(QStringLiteral("Every server argument must be a string.")); } config.arguments << value.toString(); }
	}
	if (hasOption(args, QStringLiteral("--timeout-ms"))) {
		bool valid = false; config.timeoutMs = optionValue(args, QStringLiteral("--timeout-ms")).toInt(&valid);
		if (!valid || config.timeoutMs < 100 || config.timeoutMs > 120000) { return usage(QStringLiteral("--timeout-ms must be between 100 and 120000.")); }
	}
	const QString language = hasOption(args, QStringLiteral("--language")) ? optionValue(args, QStringLiteral("--language")) : QStringLiteral("cpp");
	if (!QRegularExpression(QStringLiteral("^[A-Za-z][A-Za-z0-9_+.-]{0,63}$")).match(language).hasMatch()) { return usage(QStringLiteral("--language must be a valid language identifier.")); }
	int line = 0, column = 1;
	if (hasOption(args, QStringLiteral("--line"))) {
		bool valid = false; line = optionValue(args, QStringLiteral("--line")).toInt(&valid);
		if (!valid || line < 1) { return usage(QStringLiteral("--line must be a positive one-based line.")); }
	}
	if (hasOption(args, QStringLiteral("--column"))) {
		bool valid = false; column = optionValue(args, QStringLiteral("--column")).toInt(&valid);
		if (!valid || column < 1 || line == 0) { return usage(QStringLiteral("--column requires --line and a positive one-based UTF-16 column.")); }
	}
	const auto source = readTextFile(path);
	if (!source.editable()) { return printCliError(command, CliExitCode::ValidationFailed, source.error, format); }
	const bool complete = hasOption(args, QStringLiteral("--completion"));
	int resolveCompletionIndex = 0;
	if (hasOption(args, QStringLiteral("--resolve-completion"))) {
		bool valid = false; resolveCompletionIndex = optionValue(args, QStringLiteral("--resolve-completion")).toInt(&valid);
		if (!complete || !valid || resolveCompletionIndex < 1 || resolveCompletionIndex > 500) { return usage(QStringLiteral("--resolve-completion requires --completion and an index from 1 to 500.")); }
	}
	const bool referenceQuery = hasOption(args, QStringLiteral("--references"));
	const bool hoverQuery = hasOption(args, QStringLiteral("--hover"));
	const bool signatureQuery = hasOption(args, QStringLiteral("--signature-help"));
	const bool renameQuery = hasOption(args, QStringLiteral("--rename"));
	const bool actionQuery = hasOption(args, QStringLiteral("--code-actions"));
	int actionIndex = 0;
	if (hasOption(args, QStringLiteral("--action-index"))) {
		bool valid = false; actionIndex = optionValue(args, QStringLiteral("--action-index")).toInt(&valid);
		if (!actionQuery || !valid || actionIndex < 1 || actionIndex > 200) { return usage(QStringLiteral("--action-index requires --code-actions and an index from 1 to 200.")); }
	}
	const QString newName = optionValue(args, QStringLiteral("--rename"));
	const QString expectedPlan = optionValue(args, QStringLiteral("--expected-plan-sha256"));
	const bool writeRename = renameQuery && hasOption(args, QStringLiteral("--write"));
	const bool writeAction = actionQuery && hasOption(args, QStringLiteral("--write"));
	if ((renameQuery && !validLanguageRenameName(newName)) || (!renameQuery && actionIndex == 0 && hasOption(args, QStringLiteral("--expected-plan-sha256")))
		|| (writeAction && actionIndex == 0)
		|| ((writeRename || writeAction) && (expectedPlan.isEmpty() || hasOption(args, QStringLiteral("--dry-run"))))
		|| (hasOption(args, QStringLiteral("--expected-plan-sha256")) && !QRegularExpression(QStringLiteral("^[0-9a-fA-F]{64}$")).match(expectedPlan).hasMatch())) {
		return usage(QStringLiteral("--rename requires a valid new name. Rename or selected code action --write requires --expected-plan-sha256 from the reviewed preview and cannot use --dry-run; code actions also require --action-index."));
	}
	const bool formatDocument = hasOption(args, QStringLiteral("--format-document"));
	const bool formatRange = hasOption(args, QStringLiteral("--format-range"));
	const bool formatQuery = formatDocument || formatRange;
	const bool writeFormatting = !renameQuery && !actionQuery && hasOption(args, QStringLiteral("--write"));
	const QString expected = optionValue(args, QStringLiteral("--expected-sha256"));
	LanguageFormattingOptions formatOptions;
	if (hasOption(args, QStringLiteral("--tab-size"))) {
		bool valid = false; formatOptions.tabSize = optionValue(args, QStringLiteral("--tab-size")).toInt(&valid);
		if (!valid || formatOptions.tabSize < 1 || formatOptions.tabSize > 16) { return usage(QStringLiteral("--tab-size must be between 1 and 16.")); }
	}
	formatOptions.insertSpaces = hasOption(args, QStringLiteral("--insert-spaces"));
	if ((!formatQuery && (writeFormatting || hasOption(args, QStringLiteral("--expected-sha256")) || hasOption(args, QStringLiteral("--tab-size")) || formatOptions.insertSpaces))
		|| (writeFormatting && (hasOption(args, QStringLiteral("--dry-run")) || expected.isEmpty()))
		|| (hasOption(args, QStringLiteral("--expected-sha256")) && !QRegularExpression(QStringLiteral("^[0-9a-fA-F]{64}$")).match(expected).hasMatch())) {
		return usage(QStringLiteral("Formatting options require --format-document or --format-range; --write requires --expected-sha256 <64 hex digits> and cannot be combined with --dry-run."));
	}
	if (formatDocument && line > 0) { return usage(QStringLiteral("Document formatting does not accept a source position. Use --format-range for a selection.")); }
	if (formatRange) {
		bool lineValid = false, columnValid = false;
		const int endLine = optionValue(args, QStringLiteral("--end-line")).toInt(&lineValid), endColumn = optionValue(args, QStringLiteral("--end-column")).toInt(&columnValid);
		formatOptions.rangeOffset = languageSourceOffset(source.text, line - 1, column - 1);
		const int end = lineValid && columnValid && endLine > 0 && endColumn > 0 ? languageSourceOffset(source.text, endLine - 1, endColumn - 1) : -1;
		formatOptions.rangeLength = end - formatOptions.rangeOffset;
		if (line == 0 || formatOptions.rangeOffset < 0 || !validLanguageFormattingOptions(source.text, formatOptions)) { return usage(QStringLiteral("--format-range requires a nonempty valid --line/--column to --end-line/--end-column range, using one-based UTF-16 positions.")); }
	} else if (!actionQuery && (hasOption(args, QStringLiteral("--end-line")) || hasOption(args, QStringLiteral("--end-column")))) { return usage(QStringLiteral("Range endpoints require --format-range or --code-actions.")); }
	int actionOffset = -1, actionLength = 0;
	if (actionQuery) {
		actionOffset = languageSourceOffset(source.text, line - 1, column - 1);
		if (hasOption(args, QStringLiteral("--end-line")) || hasOption(args, QStringLiteral("--end-column"))) {
			bool lineValid = false, columnValid = false;
			const int endLine = optionValue(args, QStringLiteral("--end-line")).toInt(&lineValid), endColumn = optionValue(args, QStringLiteral("--end-column")).toInt(&columnValid);
			const int end = lineValid && columnValid && endLine > 0 && endColumn > 0 ? languageSourceOffset(source.text, endLine - 1, endColumn - 1) : -1;
			actionLength = end - actionOffset;
		}
		if (line == 0 || !validLanguageCodeActionRange(source.text, actionOffset, actionLength)) { return usage(QStringLiteral("Code actions require a valid --line/--column caret or optional --end-line/--end-column selection, using one-based UTF-16 positions.")); }
	}
	if (!expected.isEmpty() && QByteArray::fromHex(expected.toLatin1()) != source.sha256) { return printCliError(command, CliExitCode::ValidationFailed, QStringLiteral("The saved source no longer matches --expected-sha256. Preview formatting again."), format); }
	const bool includeDeclaration = !hasOption(args, QStringLiteral("--exclude-declaration"));
	if (int(complete) + int(referenceQuery) + int(hoverQuery) + int(signatureQuery) + int(formatDocument) + int(formatRange) + int(renameQuery) + int(actionQuery) > 1) { return usage(QStringLiteral("Choose one completion, references, hover, signature help, formatting, rename or code actions query.")); }
	if (!includeDeclaration && !referenceQuery) { return usage(QStringLiteral("--exclude-declaration requires --references.")); }
	if ((complete || referenceQuery || hoverQuery || signatureQuery || renameQuery) && line == 0) { return usage(QStringLiteral("--completion, --references, --hover, --signature-help and --rename require --line and optional --column.")); }
	if (line > 0) {
		const auto lines = source.text.split(QLatin1Char('\n'));
		if (line > lines.size() || column > lines[line - 1].size() + 1 || languageSourceOffset(source.text, line - 1, column - 1) < 0) { return usage(QStringLiteral("The language service position is outside the source document or splits a Unicode character.")); }
	}
	LanguageServerClient client;
	QEventLoop loop;
	QTimer timeout; timeout.setSingleShot(true);
	QString error;
	LanguageDiagnostics diagnostics;
	QVector<LanguageLocation> definitions;
	LanguageCompletions completions;
	LanguageReferences references;
	LanguageHover hover;
	LanguageFormatting formatting;
	LanguageRename rename; QString renameSymbol; int renameSourceVersion = 0; bool renameReceived = false;
	LanguageCodeActions actions; LanguageCodeAction selectedAction; bool actionsReceived = false;
	bool formattingReceived = false;
	bool completionReceived = false;
	bool completionResolveReceived = false;
	bool referencesReceived = false;
	bool hoverReceived = false;
	bool signatureReceived = false; LanguageSignatureHelp signature;
	bool synchronized = false, definitionDone = line == 0 || complete || referenceQuery || hoverQuery || signatureQuery || formatQuery || renameQuery || actionQuery;
	const auto finish = [&]() { if (!error.isEmpty() || (actionQuery ? actionsReceived : renameQuery ? renameReceived : formatQuery ? formattingReceived : (diagnostics.received && definitionDone && (!complete || completionReceived) && (!resolveCompletionIndex || completionResolveReceived) && (!referenceQuery || referencesReceived) && (!hoverQuery || hoverReceived) && (!signatureQuery || signatureReceived)))) { loop.quit(); } };
	const auto requestRename = [&]() {
		if (client.rename(source.path, line - 1, column - 1, newName, [&](const LanguageRename& result) { rename = result; renameReceived = true; error = result.error; finish(); }) < 0) {
			error = QStringLiteral("The selected server does not provide rename."); loop.quit();
		}
	};
	client.diagnosticsChanged = [&](const LanguageDiagnostics& report) { if (report.received) { diagnostics = report; if (!report.error.isEmpty() && !actionQuery && !renameQuery && !formatQuery) { error = report.error; } finish(); } };
	client.changed = [&]() {
		if (client.state() == QStringLiteral("failed")) { error = client.error(); loop.quit(); }
		else if (client.ready() && !synchronized) {
			synchronized = true;
			error = client.synchronize({{source.path, language, source.text}});
			if (!error.isEmpty()) { loop.quit(); return; }
			if (actionQuery && client.codeActions(source.path, actionOffset, actionLength, [&](const LanguageCodeActions& result) {
				actions = result; error = result.error;
				if (error.isEmpty() && actionIndex > 0) {
					if (actionIndex > actions.items.size()) { error = QStringLiteral("The selected code action index is not in the returned list."); }
					else {
						selectedAction = actions.items[actionIndex - 1];
						if (!selectedAction.available()) { error = selectedAction.disabledReason; }
						else if (selectedAction.needsResolve) {
							if (client.resolveCodeAction(source.path, actions.version, selectedAction, [&](const LanguageCodeAction& resolved, const QString& why) {
								selectedAction = resolved; error = !why.isEmpty() ? why : resolved.disabledReason; actionsReceived = true; finish();
							}) >= 0) { return; }
							error = QStringLiteral("The selected server could not resolve this action.");
						}
					}
				}
				actionsReceived = true; finish();
			}) < 0) { error = QStringLiteral("The selected server does not provide code actions."); loop.quit(); }
			if (renameQuery) {
				renameSourceVersion = client.documentVersion(source.path);
				if (client.supportsPrepareRename()) {
					if (client.prepareRename(source.path, line - 1, column - 1, [&](const LanguageRenamePreparation& result) {
						error = result.error; renameSymbol = result.placeholder; if (!error.isEmpty()) { finish(); } else { requestRename(); }
					}) < 0) { error = QStringLiteral("The selected server could not prepare rename."); loop.quit(); }
				} else { requestRename(); }
			}
			if (complete && client.completion(source.path, line - 1, column - 1, 1, {}, [&](const LanguageCompletions& result) {
				completions = result; completionReceived = true; error = result.error;
				if (error.isEmpty() && resolveCompletionIndex > 0) {
					if (resolveCompletionIndex > completions.items.size()) { error = QStringLiteral("The completion index is not in the returned list."); }
					else if (client.resolveCompletion(source.path, completions.version, completions.items[resolveCompletionIndex - 1], [&](const LanguageCompletionItem& item, const QString& why) {
						completionResolveReceived = true; error = why; if (why.isEmpty()) { completions.items[resolveCompletionIndex - 1] = item; } finish();
					}) < 0) { error = QStringLiteral("The provider cannot resolve this completion."); }
				}
				finish();
			}) < 0) { error = QStringLiteral("The selected server does not provide completion."); loop.quit(); }
			if (referenceQuery && client.references(source.path, line - 1, column - 1, includeDeclaration, [&](const LanguageReferences& result) {
				references = result; referencesReceived = true; error = result.error; finish();
			}) < 0) { error = QStringLiteral("The selected server does not provide references."); loop.quit(); }
			if (hoverQuery && client.hover(source.path, line - 1, column - 1, [&](const LanguageHover& result) {
				hover = result; hoverReceived = true; error = result.error; finish();
			}) < 0) { error = QStringLiteral("The selected server does not provide Quick Info."); loop.quit(); }
			if (signatureQuery && client.signatureHelp(source.path, line - 1, column - 1, 1, {}, false, {}, [&](const LanguageSignatureHelp& result) {
				signature = result; signatureReceived = true; error = result.error; finish();
			}) < 0) { error = QStringLiteral("The selected server does not provide parameter hints."); loop.quit(); }
			if (formatQuery && client.formatting(source.path, formatOptions, [&](const LanguageFormatting& result) {
				formatting = result; formattingReceived = true; error = result.error; finish();
			}) < 0) { error = QStringLiteral("The selected server does not support the requested formatting operation."); loop.quit(); }
			if (line > 0 && !complete && !referenceQuery && !hoverQuery && !signatureQuery && !formatQuery && !renameQuery && !actionQuery && client.definition(source.path, line - 1, column - 1, [&](const auto& locations, const QString& why) {
				definitions = locations; error = why; definitionDone = true; finish();
			}) < 0) { error = QStringLiteral("The selected server does not provide definitions."); loop.quit(); }
		}
	};
	QObject::connect(&timeout, &QTimer::timeout, &loop, [&]() { error = QStringLiteral("Timed out waiting for complete language server results."); loop.quit(); });
	if (client.start(config)) { timeout.start(config.timeoutMs); loop.exec(); }
	else { error = client.error(); }
	timeout.stop();
	const QString server = client.serverName();
	const QStringList log = client.logLines();
	client.changed = {}; client.diagnosticsChanged = {}; client.stop();
	// Drain the shutdown handshake without extending a hung server indefinitely.
	QEventLoop shutdown; QTimer stopTimer; stopTimer.setSingleShot(true);
	client.changed = [&]() { if (client.state() == QStringLiteral("stopped") || client.state() == QStringLiteral("failed")) { shutdown.quit(); } };
	QObject::connect(&stopTimer, &QTimer::timeout, &shutdown, &QEventLoop::quit);
	if (client.state() == QStringLiteral("stopping")) { stopTimer.start(1700); shutdown.exec(); }
	client.changed = {};
	AssetTextSearchReport referenceReport;
	if (referenceQuery && referencesReceived) {
		LanguageReferenceRequest request; request.rootPath = client.rootPath(); request.provider = server; request.references = references;
		request.buffers << AssetTextBuffer {source.path, QStringLiteral("command-source"), references.version, source.text, textEncodingName(source.encoding), {}};
		referenceReport = prepareLanguageReferences(request);
	}
	const bool hasErrors = std::any_of(diagnostics.items.cbegin(), diagnostics.items.cend(), [](const auto& item) { return item.severity == 1; });
	AssetTextSearchReport renameReport; QByteArray renamePlanHash;
	if (renameQuery && renameReceived && error.isEmpty()) {
		if (readTextFile(source.path).sha256 != source.sha256) { error = QStringLiteral("The source changed while requesting rename. Preview again."); }
		else {
			LanguageRenameRequest request; request.rootPath = client.rootPath(); request.provider = server; request.symbol = renameSymbol;
			request.newName = newName; request.rename = rename; request.versions.insert(source.path, renameSourceVersion);
			renameReport = prepareLanguageRename(request); renamePlanHash = languageRenamePlanHash(renameReport);
			if (!renameReport.succeeded()) { error = renameReport.warnings.join(QLatin1Char('\n')); }
			else if (!expectedPlan.isEmpty() && QByteArray::fromHex(expectedPlan.toLatin1()) != renamePlanHash) { error = QStringLiteral("The rename plan differs from --expected-plan-sha256. Review the new preview before writing."); }
			else if (writeRename && renameReport.canApply()) { renameReport = applyProjectTextReplacements(renameReport); }
		}
	}
	QString formattedText; TextFileSaveResult formattingSave;
	AssetTextSearchReport actionReport; QByteArray actionPlanHash;
	if (actionQuery && actionsReceived && actionIndex > 0 && error.isEmpty()) {
		if (readTextFile(source.path).sha256 != source.sha256) { error = QStringLiteral("The source changed while requesting code actions. Preview again."); }
		else {
			LanguageWorkspaceEditRequest request; request.rootPath = client.rootPath(); request.provider = server; request.title = selectedAction.title; request.findText = selectedAction.title;
			request.workspaceEdit = selectedAction.wire.value(QStringLiteral("edit")); request.versions.insert(source.path, actions.version);
			actionReport = prepareLanguageWorkspaceEdit(request); actionPlanHash = languageWorkspaceEditPlanHash(actionReport);
			if (!actionReport.succeeded()) { error = actionReport.warnings.join(QLatin1Char('\n')); }
			else if (!expectedPlan.isEmpty() && QByteArray::fromHex(expectedPlan.toLatin1()) != actionPlanHash) { error = QStringLiteral("The code action plan differs from --expected-plan-sha256. Review the new preview before writing."); }
			else if (writeAction && actionReport.canApply()) { actionReport = applyProjectTextReplacements(actionReport); }
		}
	}
	if (formatQuery && formattingReceived && error.isEmpty() && previewLanguageFormatting(source.text, formatting, &formattedText, &error)) {
		formattingSave = saveTextFileEdits(source, formatting.edits, !writeFormatting, source.sha256);
		if (!formattingSave.succeeded) { error = formattingSave.error; }
	}
	const CliExitCode code = !error.isEmpty() || (actionQuery ? (!actionsReceived || (actionIndex > 0 ? !actionReport.succeeded() : (actions.limited || actions.skipped > 0))) : renameQuery ? (!renameReceived || !renameReport.succeeded()) : formatQuery ? (!formattingReceived || !formattingSave.succeeded) : (!diagnostics.received || diagnostics.limited || hasErrors))
		|| (complete && (!completionReceived || (resolveCompletionIndex > 0 && !completionResolveReceived) || completions.limited || completions.incomplete || completions.skipped > 0))
		|| (hoverQuery && (!hoverReceived || hover.limited || hover.skipped > 0))
		|| (signatureQuery && (!signatureReceived || signature.limited || signature.skipped > 0))
		|| (referenceQuery && (!referencesReceived || !referenceReport.succeeded())) ? CliExitCode::ValidationFailed : CliExitCode::Success;
	const auto locationJson = [](const LanguageLocation& at) { return QJsonObject {{QStringLiteral("filePath"), at.filePath}, {QStringLiteral("line"), at.line + 1}, {QStringLiteral("column"), at.character + 1},
		{QStringLiteral("endLine"), at.endLine + 1}, {QStringLiteral("endColumn"), at.endCharacter + 1}}; };
	QJsonArray items, targets;
	for (const auto& diagnostic : diagnostics.items) {
		auto item = locationJson(diagnostic.location); item.insert(QStringLiteral("severity"), diagnostic.severity); item.insert(QStringLiteral("message"), diagnostic.message);
		item.insert(QStringLiteral("code"), diagnostic.code); item.insert(QStringLiteral("source"), diagnostic.source); items << item;
	}
	for (const auto& target : definitions) { targets << locationJson(target); }
	QJsonArray suggestions;
	for (int index = 0; index < completions.items.size(); ++index) {
		const auto& item = completions.items[index];
		QJsonArray tabStops;
		for (const auto& stop : item.tabStops) {
			tabStops << QJsonObject {{QStringLiteral("number"), stop.number}, {QStringLiteral("offset"), stop.offset}, {QStringLiteral("length"), stop.length},
				{QStringLiteral("parent"), stop.parent}, {QStringLiteral("choices"), stringArrayJson(stop.choices)}};
		}
		QJsonArray edits;
		for (const auto& edit : item.edits) { edits << QJsonObject {{QStringLiteral("offset"), edit.offset}, {QStringLiteral("length"), edit.length}, {QStringLiteral("text"), edit.text}}; }
		suggestions << QJsonObject {{QStringLiteral("index"), index + 1}, {QStringLiteral("needsResolve"), item.needsResolve}, {QStringLiteral("label"), item.label}, {QStringLiteral("detail"), item.detail}, {QStringLiteral("documentation"), item.documentation},
			{QStringLiteral("filterText"), item.filterText}, {QStringLiteral("sortText"), item.sortText}, {QStringLiteral("kind"), item.kind}, {QStringLiteral("deprecated"), item.deprecated},
			{QStringLiteral("sourceSha256"), QString::fromLatin1(item.sourceSha256.toHex())}, {QStringLiteral("requestOffset"), item.requestOffset}, {QStringLiteral("caret"), item.caret},
			{QStringLiteral("snippet"), item.snippet}, {QStringLiteral("tabStops"), tabStops}, {QStringLiteral("edits"), edits}};
	}
	QJsonArray referenceItems;
	QJsonArray actionItems;
	for (int index = 0; index < actions.items.size(); ++index) {
		const auto& action = actions.items[index];
		actionItems << QJsonObject {{QStringLiteral("index"), index + 1}, {QStringLiteral("title"), action.title}, {QStringLiteral("kind"), action.kind},
			{QStringLiteral("preferred"), action.preferred}, {QStringLiteral("available"), action.available()}, {QStringLiteral("reason"), action.disabledReason}, {QStringLiteral("needsResolve"), action.needsResolve}};
	}
	QJsonArray formattingEdits;
	for (const auto& edit : formatting.edits) { formattingEdits << QJsonObject {{QStringLiteral("offset"), qint64(edit.offset)}, {QStringLiteral("length"), qint64(edit.length)}, {QStringLiteral("text"), edit.replacement}}; }
	QJsonArray hoverParts;
	for (const auto& part : hover.contents) { hoverParts << QJsonObject {{QStringLiteral("kind"), part.kind}, {QStringLiteral("value"), part.text}, {QStringLiteral("language"), part.language}}; }
	for (const auto& match : referenceReport.matches) {
		referenceItems << QJsonObject {{QStringLiteral("filePath"), match.filePath}, {QStringLiteral("line"), match.line}, {QStringLiteral("column"), match.column},
			{QStringLiteral("endLine"), match.endLine}, {QStringLiteral("endColumn"), match.endColumn}, {QStringLiteral("preview"), match.lineText}, {QStringLiteral("encoding"), match.encoding},
			{QStringLiteral("textSha256"), QString::fromLatin1(match.textSha256.toHex())}, {QStringLiteral("source"), match.bufferId.isEmpty() ? QStringLiteral("saved-file") : QStringLiteral("command-source")}};
	}
	if (format == CliOutputFormat::Json) {
		auto result = cliResultJson(command, code);
		auto renameJson = assetTextSearchReportJson(renameReport); if (!renamePlanHash.isEmpty()) { renameJson.insert(QStringLiteral("planSha256"), QString::fromLatin1(renamePlanHash.toHex())); }
		auto actionJson = assetTextSearchReportJson(actionReport); if (!actionPlanHash.isEmpty()) { actionJson.insert(QStringLiteral("planSha256"), QString::fromLatin1(actionPlanHash.toHex())); }
		auto signatureJson = languageSignatureContext(signature);
		signatureJson.insert(QStringLiteral("version"), signature.version); signatureJson.insert(QStringLiteral("requestOffset"), signature.requestOffset);
		signatureJson.insert(QStringLiteral("sourceSha256"), QString::fromLatin1(signature.sourceSha256.toHex()));
		signatureJson.insert(QStringLiteral("limited"), signature.limited); signatureJson.insert(QStringLiteral("skipped"), signature.skipped);
		result.insert(QStringLiteral("languageServer"), QJsonObject {{QStringLiteral("server"), server}, {QStringLiteral("rootPath"), client.rootPath()},
			{QStringLiteral("filePath"), source.path}, {QStringLiteral("diagnosticsReceived"), diagnostics.received}, {QStringLiteral("versioned"), diagnostics.versioned},
			{QStringLiteral("version"), diagnostics.version}, {QStringLiteral("limited"), diagnostics.limited}, {QStringLiteral("diagnostics"), items},
			{QStringLiteral("diagnosticOrigin"), diagnostics.origin}, {QStringLiteral("diagnosticsError"), diagnostics.error}, {QStringLiteral("diagnosticsSkipped"), diagnostics.skipped},
			{QStringLiteral("definitions"), targets}, {QStringLiteral("log"), QJsonArray::fromStringList(log)}, {QStringLiteral("error"), error},
			{QStringLiteral("renameReceived"), renameReceived}, {QStringLiteral("rename"), renameJson},
			{QStringLiteral("codeActionsReceived"), actionsReceived}, {QStringLiteral("codeActions"), QJsonObject {{QStringLiteral("items"), actionItems},
				{QStringLiteral("version"), actions.version}, {QStringLiteral("sourceSha256"), QString::fromLatin1(actions.sourceSha256.toHex())},
				{QStringLiteral("offset"), actions.offset}, {QStringLiteral("length"), actions.length}, {QStringLiteral("limited"), actions.limited}, {QStringLiteral("skipped"), actions.skipped},
				{QStringLiteral("selectedIndex"), actionIndex}, {QStringLiteral("preview"), actionJson}}},
			{QStringLiteral("formattingReceived"), formattingReceived}, {QStringLiteral("formatting"), QJsonObject {
				{QStringLiteral("version"), formatting.version}, {QStringLiteral("sourceSha256"), QString::fromLatin1(formatting.sourceSha256.toHex())},
				{QStringLiteral("sourceFileSha256"), QString::fromLatin1(source.sha256.toHex())}, {QStringLiteral("edits"), formattingEdits}, {QStringLiteral("text"), formattedText},
				{QStringLiteral("tabSize"), formatOptions.tabSize}, {QStringLiteral("insertSpaces"), formatOptions.insertSpaces}, {QStringLiteral("rangeOffset"), formatOptions.rangeOffset}, {QStringLiteral("rangeLength"), formatOptions.rangeLength},
				{QStringLiteral("dryRun"), !writeFormatting}, {QStringLiteral("succeeded"), formattingSave.succeeded}, {QStringLiteral("changed"), formattingSave.changed},
				{QStringLiteral("written"), writeFormatting && formattingSave.succeeded && formattingSave.changed}, {QStringLiteral("outputFileSha256"), QString::fromLatin1(formattingSave.outputSha256.toHex())}}},
			{QStringLiteral("signatureHelpReceived"), signatureReceived}, {QStringLiteral("signatureHelp"), signatureJson},
			{QStringLiteral("hoverReceived"), hoverReceived}, {QStringLiteral("hover"), QJsonObject {{QStringLiteral("version"), hover.version},
				{QStringLiteral("sourceSha256"), QString::fromLatin1(hover.sourceSha256.toHex())}, {QStringLiteral("requestOffset"), hover.requestOffset},
				{QStringLiteral("contents"), hoverParts}, {QStringLiteral("limited"), hover.limited}, {QStringLiteral("skipped"), hover.skipped},
				{QStringLiteral("range"), hover.hasRange ? QJsonValue(QJsonObject {{QStringLiteral("offset"), hover.offset}, {QStringLiteral("length"), hover.length}}) : QJsonValue(QJsonValue::Null)}}},
			{QStringLiteral("completionReceived"), completionReceived}, {QStringLiteral("completions"), QJsonObject {{QStringLiteral("version"), completions.version},
				{QStringLiteral("resolveReceived"), completionResolveReceived}, {QStringLiteral("resolvedIndex"), resolveCompletionIndex},
				{QStringLiteral("incomplete"), completions.incomplete}, {QStringLiteral("limited"), completions.limited}, {QStringLiteral("skipped"), completions.skipped}, {QStringLiteral("items"), suggestions}}},
			{QStringLiteral("referencesReceived"), referencesReceived}, {QStringLiteral("references"), QJsonObject {{QStringLiteral("includeDeclaration"), includeDeclaration},
				{QStringLiteral("version"), references.version}, {QStringLiteral("complete"), referencesReceived && referenceReport.succeeded()}, {QStringLiteral("limited"), references.limited},
				{QStringLiteral("skipped"), referenceReport.referenceLocationsSkipped}, {QStringLiteral("filesScanned"), referenceReport.filesScanned},
				{QStringLiteral("items"), referenceItems}, {QStringLiteral("warnings"), QJsonArray::fromStringList(referenceReport.warnings)}}}});
		printJson(result);
	} else {
		std::cout << text(server) << ": " << text(source.path) << "\n";
		for (const auto& item : diagnostics.items) { std::cout << item.location.line + 1 << ':' << item.location.character + 1 << ' ' << text(item.message) << "\n"; }
		for (const auto& at : definitions) { std::cout << text(at.filePath) << ':' << at.line + 1 << ':' << at.character + 1 << "\n"; }
		for (int index = 0; index < completions.items.size(); ++index) { const auto& item = completions.items[index]; std::cout << index + 1 << ": " << text(item.label) << " - " << text(item.detail) << (item.needsResolve ? " [resolve available]" : "") << "\n"; }
		if (complete) { std::cout << "Completions: " << completions.items.size() << "; incomplete=" << completions.incomplete << "; limited=" << completions.limited << "; skipped=" << completions.skipped << "\n"; }
		if (hoverQuery) {
			for (const auto& part : hover.contents) { std::cout << text(part.text) << "\n\n"; }
			std::cout << "Quick Info: " << hover.contents.size() << " part(s); limited=" << hover.limited << "; skipped=" << hover.skipped << "\n";
		}
		if (signatureQuery) {
			for (int index = 0; index < signature.signatures.size(); ++index) {
				const auto& item = signature.signatures[index];
				std::cout << (index == signature.activeSignature ? "* " : "  ") << index + 1 << ": " << text(item.label) << "\n" << text(item.documentation.text) << "\n";
				if (item.activeParameter >= 0 && item.activeParameter < item.parameters.size()) {
					const auto& parameter = item.parameters[item.activeParameter];
					std::cout << "Parameter " << item.activeParameter + 1 << ": " << text(parameter.label) << "\n" << text(parameter.documentation.text) << "\n";
				}
			}
			std::cout << "Parameter hints: " << signature.signatures.size() << " signature(s); limited=" << signature.limited << "; skipped=" << signature.skipped << "\n";
		}
		if (referenceQuery) {
			for (const auto& match : referenceReport.matches) { std::cout << text(match.filePath) << ':' << match.line << ':' << match.column << ' ' << text(match.lineText) << "\n"; }
			std::cout << "References: " << referenceReport.matchCount << "; complete=" << (referencesReceived && referenceReport.succeeded()) << "; skipped=" << referenceReport.referenceLocationsSkipped << "\n";
			for (const auto& warning : referenceReport.warnings) { std::cout << text(warning) << "\n"; }
		}
		if (formatQuery) {
			std::cout << "Formatting: " << formatting.edits.size() << " edit(s); " << (writeFormatting ? "write" : "preview") << "; changed=" << formattingSave.changed << "\n";
			std::cout << "Saved source SHA-256: " << text(QString::fromLatin1(source.sha256.toHex())) << "\n";
			if (error.isEmpty()) { std::cout << text(formattedText) << "\n"; }
		}
		if (renameQuery) {
			std::cout << text(assetTextSearchReportText(renameReport)) << "\nRename plan SHA-256: " << text(QString::fromLatin1(renamePlanHash.toHex())) << "\n";
		}
		if (actionQuery) {
			for (int index = 0; index < actions.items.size(); ++index) {
				const auto& action = actions.items[index]; std::cout << index + 1 << ": " << text(action.title) << " [" << text(action.kind) << "]";
				if (!action.available()) { std::cout << " - unavailable: " << text(action.disabledReason); } std::cout << "\n";
			}
			std::cout << "Code actions: " << actions.items.size() << "; limited=" << actions.limited << "; skipped=" << actions.skipped << "\n";
			if (actionIndex > 0) { std::cout << text(assetTextSearchReportText(actionReport)) << "\nCode action plan SHA-256: " << text(QString::fromLatin1(actionPlanHash.toHex())) << "\n"; }
		}
		if (!error.isEmpty()) { std::cout << text(error) << "\n"; }
		else if (diagnostics.received) { std::cout << diagnostics.items.size() << " diagnostic(s); " << (diagnostics.versioned ? "versioned" : "unversioned") << " report.\n"; }
		else { std::cout << "Diagnostics: no report received.\n"; }
	}
	return exitCodeValue(code);
}

int runCodeIndexCommand(const QString& commandName, const QString& rootPath, const QStringList& args, CliOutputFormat format)
{
	const QString root = rootPath.trimmed().isEmpty() ? optionValue(args, QStringLiteral("--workspace-root")) : rootPath;
	if (root.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a project source root path.").arg(commandName), format);
	}
	CodeWorkspaceIndexRequest request;
	request.rootPath = root;
	request.extensions = assetTextExtensionsFromArgs(args);
	request.symbolQuery = optionValue(args, QStringLiteral("--find"));
	if (hasOption(args, QStringLiteral("--symbol"))) {
		request.symbolQuery = optionValue(args, QStringLiteral("--symbol"));
	}
	if (hasOption(args, QStringLiteral("--max-files"))) {
		bool ok = false;
		request.maxFiles = optionValue(args, QStringLiteral("--max-files")).toInt(&ok);
		if (!ok || request.maxFiles < 1 || request.maxFiles > 20000) {
			return printCliError(commandName, CliExitCode::Usage, QStringLiteral("--max-files must be between 1 and 20000."), format);
		}
	}
	const CodeWorkspaceIndex index = indexCodeWorkspace(request);
	const CliExitCode code = index.state == OperationState::Failed ? CliExitCode::Failure : index.complete ? CliExitCode::Success : CliExitCode::ValidationFailed;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("code"), codeWorkspaceIndexJson(index));
		printJson(object);
	} else {
		std::cout << text(codeWorkspaceIndexText(index)) << "\n";
	}
	return exitCodeValue(code);
}

QStringList extensionRootsFromArgs(const QString& positionalRoot, const QStringList& args)
{
	QStringList roots;
	if (!positionalRoot.trimmed().isEmpty()) {
		roots << positionalRoot;
	}
	roots += optionValues(args, QStringLiteral("--extension-root"));
	roots += optionValues(args, QStringLiteral("--root"));
	if (roots.isEmpty()) {
		roots << QDir::currentPath();
	}
	return roots;
}

int runExtensionDiscoverCommand(const QString& commandName, const QString& rootPath, const QStringList& args, CliOutputFormat format)
{
	const ExtensionDiscoveryResult discovery = discoverExtensions(extensionRootsFromArgs(rootPath, args));
	const CliExitCode code = discovery.state == OperationState::Failed ? CliExitCode::Failure : CliExitCode::Success;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("extensions"), extensionDiscoveryJson(discovery));
		printJson(object);
	} else {
		std::cout << text(extensionDiscoveryText(discovery)) << "\n";
	}
	return exitCodeValue(code);
}

int runExtensionInspectCommand(const QString& commandName, const QString& manifestPath, CliOutputFormat format)
{
	if (manifestPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires an extension manifest path.").arg(commandName), format);
	}
	ExtensionManifest manifest;
	QString error;
	if (!loadExtensionManifest(manifestPath, &manifest, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to load extension manifest: %1").arg(error), format);
	}
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("extension"), extensionManifestJson(manifest));
		object.insert(QStringLiteral("trustModel"), stringArrayJson(extensionTrustModelLines()));
		printJson(object);
	} else {
		std::cout << text(extensionManifestText(manifest)) << "\n";
		std::cout << text(extensionTrustModelLines().join('\n')) << "\n";
	}
	return exitCodeValue(CliExitCode::Success);
}

int runExtensionRunCommand(const QString& commandName, const QString& manifestPath, const QString& commandId, const QStringList& args, CliOutputFormat format)
{
	if (manifestPath.trimmed().isEmpty() || commandId.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a manifest path and command id.").arg(commandName), format);
	}
	ExtensionManifest manifest;
	QString error;
	if (!loadExtensionManifest(manifestPath, &manifest, &error)) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Unable to load extension manifest: %1").arg(error), format);
	}
	const bool allowExecution = hasOption(args, QStringLiteral("--execute")) || hasOption(args, QStringLiteral("--allow-execution"));
	const bool dryRun = hasOption(args, QStringLiteral("--dry-run")) || !allowExecution;
	QStringList extraArguments;
	if (hasOption(args, QStringLiteral("--extra-args"))) {
		extraArguments = QProcess::splitCommand(optionValue(args, QStringLiteral("--extra-args")));
	}
	bool timeoutOk = false;
	const int requestedTimeout = optionValue(args, QStringLiteral("--timeout-ms")).toInt(&timeoutOk);
	const ExtensionCommandPlan plan = buildExtensionCommandPlan(manifest, commandId, extraArguments, dryRun, allowExecution);
	const ExtensionCommandResult result = runExtensionCommand(plan, timeoutOk && requestedTimeout > 0 ? requestedTimeout : 30000);
	const CliExitCode code = result.state == OperationState::Failed ? CliExitCode::Failure : CliExitCode::Success;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("extensionCommand"), extensionCommandResultJson(result));
		printJson(object);
	} else {
		std::cout << text(extensionCommandResultText(result)) << "\n";
	}
	return exitCodeValue(code);
}

int runInstallListCommand(const QString& commandName, CliOutputFormat format)
{
	const StudioSettings settings;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("installations"), gameInstallationsJson(settings));
		printJson(object);
	} else {
		printGameInstallations(settings);
	}
	return exitCodeValue(CliExitCode::Success);
}

int runInstallDetectCommand(const QString& commandName, const QStringList& roots, CliOutputFormat format)
{
	const QVector<GameInstallationDetectionCandidate> candidates = detectGameInstallations(roots, roots);
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("detection"), gameInstallationDetectionJson(candidates));
		printJson(object);
	} else {
		printInstallationDetectionCandidates(candidates);
	}
	return exitCodeValue(CliExitCode::Success);
}

int runCompilerListCommand(const QString& commandName, CliOutputFormat format)
{
	const CompilerRegistrySummary summary = discoverCompilerTools(compilerRegistryOptionsFromArgs(QCoreApplication::arguments()));
	const QVector<CompilerToolPathOverride> overrides = StudioSettings().compilerToolPathOverrides();
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("compilerRegistry"), compilerRegistryJson(summary));
		object.insert(QStringLiteral("userExecutableOverrides"), compilerToolPathOverridesJson(overrides));
		printJson(object);
	} else {
		std::cout << text(compilerRegistrySummaryText(summary)) << "\n";
		if (!overrides.isEmpty()) {
			std::cout << "User executable overrides\n";
			for (const CompilerToolPathOverride& override : overrides) {
				std::cout << "- " << text(override.toolId) << ": " << text(nativePath(override.executablePath)) << "\n";
			}
		}
	}
	return exitCodeValue(CliExitCode::Success);
}

int runCompilerProfilesCommand(const QString& commandName, CliOutputFormat format)
{
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("profiles"), compilerProfilesJson());
		printJson(object);
		return exitCodeValue(CliExitCode::Success);
	}

	std::cout << "Compiler wrapper profiles\n";
	for (const CompilerProfileDescriptor& profile : compilerProfileDescriptors()) {
		std::cout << "- " << text(profile.id) << "\n";
		std::cout << "  Tool: " << text(profile.toolId) << "\n";
		std::cout << "  Stage: " << text(profile.stageId) << "\n";
		std::cout << "  Engine: " << text(profile.engineFamily) << "\n";
		std::cout << "  Input: " << text(profile.inputDescription) << " (" << text(profile.inputExtensions.isEmpty() ? QStringLiteral("none") : profile.inputExtensions.join(", ")) << ")\n";
		std::cout << "  Input required: " << (profile.inputRequired ? "yes" : "no") << "\n";
		std::cout << "  Output extension: " << text(profile.defaultOutputExtension.isEmpty() ? QStringLiteral("(none)") : profile.defaultOutputExtension) << "\n";
		std::cout << "  " << text(profile.description) << "\n";
	}
	return exitCodeValue(CliExitCode::Success);
}

int runCompilerSetPathCommand(const QString& commandName, const QString& toolId, const QStringList& args, CliOutputFormat format)
{
	if (toolId.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a compiler tool id.").arg(commandName), format);
	}
	CompilerToolDescriptor descriptor;
	if (!compilerToolDescriptorForId(toolId, &descriptor)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unknown compiler tool id: %1").arg(toolId), format);
	}
	const QString executablePath = optionValue(args, QStringLiteral("--executable"));
	if (executablePath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --executable <path>.").arg(commandName), format);
	}
	if (!QFileInfo(executablePath).isFile()) {
		return printCliError(commandName, CliExitCode::NotFound, QStringLiteral("Compiler executable not found: %1").arg(nativePath(executablePath)), format);
	}

	StudioSettings settings;
	settings.upsertCompilerToolPathOverride({descriptor.id, QFileInfo(executablePath).absoluteFilePath()});
	settings.sync();
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("userExecutableOverrides"), compilerToolPathOverridesJson(settings.compilerToolPathOverrides()));
		printJson(object);
	} else {
		std::cout << "Compiler executable override saved\n";
		std::cout << text(descriptor.id) << ": " << text(nativePath(QFileInfo(executablePath).absoluteFilePath())) << "\n";
	}
	return exitCodeValue(CliExitCode::Success);
}

int runCompilerClearPathCommand(const QString& commandName, const QString& toolId, CliOutputFormat format)
{
	if (toolId.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a compiler tool id.").arg(commandName), format);
	}
	StudioSettings settings;
	settings.removeCompilerToolPathOverride(toolId);
	settings.sync();
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("userExecutableOverrides"), compilerToolPathOverridesJson(settings.compilerToolPathOverrides()));
		printJson(object);
	} else {
		std::cout << "Compiler executable override removed: " << text(toolId) << "\n";
	}
	return exitCodeValue(CliExitCode::Success);
}

CliExitCode compilerPlanExitCode(const CompilerCommandPlan& plan)
{
	if (plan.errors.isEmpty()) {
		return CliExitCode::Success;
	}
	if (!plan.profileFound) {
		return CliExitCode::Usage;
	}
	return CliExitCode::NotFound;
}

int runCompilerPlanCommand(const QString& commandName, const QString& profileId, const QString& positionalInputPath, const QStringList& args, CliOutputFormat format, bool manifestMode)
{
	CompilerCommandRequest request;
	request.profileId = profileId;
	request.inputPath = hasOption(args, QStringLiteral("--input")) ? optionValue(args, QStringLiteral("--input")) : positionalInputPath;
	request.outputPath = optionValue(args, QStringLiteral("--output"));
	request.workingDirectory = optionValue(args, QStringLiteral("--working-directory"));
	request.workspaceRootPath = optionValue(args, QStringLiteral("--workspace-root"));
	request.extraSearchPaths = optionPathList(args, QStringLiteral("--compiler-search-paths"));
	const QString extraArgs = optionValue(args, QStringLiteral("--extra-args"));
	if (!extraArgs.trimmed().isEmpty()) {
		request.extraArguments = QProcess::splitCommand(extraArgs);
	}
	applyCompilerRequestContext(&request, args);

	const CompilerCommandPlan plan = buildCompilerCommandPlan(request);
	const CompilerCommandManifest manifest = compilerCommandManifestFromPlan(plan);
	const QString manifestPath = optionValue(args, QStringLiteral("--manifest"));
	if (!manifestPath.trimmed().isEmpty()) {
		QString error;
		if (!saveCompilerCommandManifest(manifest, manifestPath, &error)) {
			return printCliError(commandName, CliExitCode::Failure, QStringLiteral("Failed to save compiler manifest: %1").arg(error), format);
		}
	}
	const CliExitCode code = compilerPlanExitCode(plan);
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		if (manifestMode) {
			object.insert(QStringLiteral("manifest"), compilerCommandManifestJson(manifest));
		} else {
			object.insert(QStringLiteral("plan"), compilerCommandPlanJson(plan));
			object.insert(QStringLiteral("manifest"), compilerCommandManifestJson(manifest));
		}
		if (!manifestPath.trimmed().isEmpty()) {
			object.insert(QStringLiteral("manifestPath"), QDir::cleanPath(QFileInfo(manifestPath).absoluteFilePath()));
		}
		printJson(object);
	} else {
		std::cout << text(manifestMode ? compilerCommandManifestText(manifest) : compilerCommandPlanText(plan)) << "\n";
		if (!manifestPath.trimmed().isEmpty()) {
			std::cout << "Manifest saved: " << text(nativePath(QFileInfo(manifestPath).absoluteFilePath())) << "\n";
		}
	}
	return exitCodeValue(code);
}

CliExitCode compilerRunExitCode(const CompilerRunResult& result)
{
	if (result.state == OperationState::Completed || result.state == OperationState::Warning) {
		return CliExitCode::Success;
	}
	if (!result.plan.profileFound) {
		return CliExitCode::Usage;
	}
	if (!result.plan.isRunnable() && !result.plan.errors.isEmpty()) {
		return CliExitCode::NotFound;
	}
	return CliExitCode::Failure;
}

int runCompilerRunCommand(const QString& commandName, const QString& profileId, const QString& positionalInputPath, const QStringList& args, CliOutputFormat format)
{
	CompilerRunRequest runRequest;
	runRequest.command.profileId = profileId;
	runRequest.command.inputPath = hasOption(args, QStringLiteral("--input")) ? optionValue(args, QStringLiteral("--input")) : positionalInputPath;
	runRequest.command.outputPath = optionValue(args, QStringLiteral("--output"));
	runRequest.command.workingDirectory = optionValue(args, QStringLiteral("--working-directory"));
	runRequest.command.workspaceRootPath = optionValue(args, QStringLiteral("--workspace-root"));
	runRequest.command.extraSearchPaths = optionPathList(args, QStringLiteral("--compiler-search-paths"));
	const QString extraArgs = optionValue(args, QStringLiteral("--extra-args"));
	if (!extraArgs.trimmed().isEmpty()) {
		runRequest.command.extraArguments = QProcess::splitCommand(extraArgs);
	}
	applyCompilerRequestContext(&runRequest.command, args);
	runRequest.manifestPath = optionValue(args, QStringLiteral("--manifest"));
	runRequest.dryRun = hasOption(args, QStringLiteral("--dry-run"));
	runRequest.registerOutputs = hasOption(args, QStringLiteral("--register-output")) || hasOption(args, QStringLiteral("--register-outputs"));
	bool timeoutOk = false;
	const int timeoutMs = optionValue(args, QStringLiteral("--timeout-ms")).toInt(&timeoutOk);
	if (timeoutOk && timeoutMs > 0) {
		runRequest.timeoutMs = timeoutMs;
	}

	CompilerRunCallbacks callbacks;
	if (hasOption(args, QStringLiteral("--watch")) && format == CliOutputFormat::Text && !quietOutput()) {
		callbacks.logEntry = [](const CompilerTaskLogEntry& entry) {
			std::cout << "[" << text(entry.timestampUtc.toUTC().toString(Qt::ISODate)) << "] "
				<< text(entry.level) << " " << text(entry.message) << "\n";
		};
	}
	CompilerRunResult result = runCompilerCommand(runRequest, callbacks);
	if (runRequest.registerOutputs && !runRequest.command.workspaceRootPath.trimmed().isEmpty() && !result.registeredOutputPaths.isEmpty()) {
		ProjectManifest manifest;
		QString error;
		if (loadProjectManifest(runRequest.command.workspaceRootPath, &manifest, &error)) {
			const int registered = registerProjectOutputPaths(&manifest, result.registeredOutputPaths);
			if (registered > 0 && saveProjectManifest(manifest, &error)) {
				result.manifest.registeredOutputPaths = result.registeredOutputPaths;
			}
		}
	}

	const CliExitCode code = compilerRunExitCode(result);
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("run"), compilerRunResultJson(result));
		if (hasOption(args, QStringLiteral("--task-state")) || hasOption(args, QStringLiteral("--watch"))) {
			object.insert(QStringLiteral("taskState"), taskStateJson(result.manifest.manifestId, result.state, result.error.isEmpty() ? compilerRunResultText(result).split('\n').value(0) : result.error, result.durationMs, false));
		}
		printJson(object);
	} else {
		std::cout << text(compilerRunResultText(result)) << "\n";
		if (!runRequest.manifestPath.trimmed().isEmpty()) {
			std::cout << "Manifest saved: " << text(nativePath(QFileInfo(runRequest.manifestPath).absoluteFilePath())) << "\n";
		}
	}
	return exitCodeValue(code);
}

int runCompilerRerunCommand(const QString& commandName, const QString& manifestInputPath, const QStringList& args, CliOutputFormat format)
{
	if (manifestInputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a manifest path.").arg(commandName), format);
	}
	CompilerCommandManifest manifest;
	QString error;
	if (!loadCompilerCommandManifest(manifestInputPath, &manifest, &error)) {
		return printCliError(commandName, CliExitCode::NotFound, QStringLiteral("Unable to load compiler manifest: %1").arg(error), format);
	}
	const QString outputManifestPath = optionValue(args, QStringLiteral("--manifest"));
	CompilerRunCallbacks callbacks;
	if (hasOption(args, QStringLiteral("--watch")) && format == CliOutputFormat::Text && !quietOutput()) {
		callbacks.logEntry = [](const CompilerTaskLogEntry& entry) {
			std::cout << "[" << text(entry.timestampUtc.toUTC().toString(Qt::ISODate)) << "] "
				<< text(entry.level) << " " << text(entry.message) << "\n";
		};
	}
	CompilerRunResult result = rerunCompilerCommandManifest(manifest, callbacks, outputManifestPath);
	const CliExitCode code = compilerRunExitCode(result);
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("run"), compilerRunResultJson(result));
		if (hasOption(args, QStringLiteral("--task-state")) || hasOption(args, QStringLiteral("--watch"))) {
			object.insert(QStringLiteral("taskState"), taskStateJson(result.manifest.manifestId, result.state, result.error.isEmpty() ? compilerRunResultText(result).split('\n').value(0) : result.error, result.durationMs, false));
		}
		printJson(object);
	} else {
		std::cout << text(compilerRunResultText(result)) << "\n";
		if (!outputManifestPath.trimmed().isEmpty()) {
			std::cout << "Manifest saved: " << text(nativePath(QFileInfo(outputManifestPath).absoluteFilePath())) << "\n";
		}
	}
	return exitCodeValue(code);
}

int runCompilerCopyCommandCommand(const QString& commandName, const QString& target, const QStringList& args, CliOutputFormat format)
{
	if (target.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a manifest path or compiler profile id.").arg(commandName), format);
	}

	QString commandLine;
	CompilerCommandManifest manifest;
	QString error;
	if (loadCompilerCommandManifest(target, &manifest, &error)) {
		commandLine = manifest.commandLine;
	} else {
		CompilerCommandRequest request;
		request.profileId = target;
		request.inputPath = optionValue(args, QStringLiteral("--input"));
		request.outputPath = optionValue(args, QStringLiteral("--output"));
		request.workingDirectory = optionValue(args, QStringLiteral("--working-directory"));
		request.workspaceRootPath = optionValue(args, QStringLiteral("--workspace-root"));
		const QString extraArgs = optionValue(args, QStringLiteral("--extra-args"));
		if (!extraArgs.trimmed().isEmpty()) {
			request.extraArguments = QProcess::splitCommand(extraArgs);
		}
		applyCompilerRequestContext(&request, args);
		commandLine = buildCompilerCommandPlan(request).commandLine;
	}
	if (commandLine.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Failure, QStringLiteral("No compiler command line could be produced."), format);
	}

	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("commandLine"), commandLine);
		printJson(object);
	} else {
		std::cout << text(commandLine) << "\n";
	}
	return exitCodeValue(CliExitCode::Success);
}

// ---------------------------------------------------------------------------
// map render
// ---------------------------------------------------------------------------

int runMapRenderCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format)
{
	const LevelMapLoadRequest request = levelMapLoadRequestFromArgs(path, args);
	if (request.path.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a Doom WAD or Quake-family .map path.").arg(commandName), format);
	}
	LevelMapDocument document;
	QString error;
	if (!loadLevelMap(request, &document, &error)) {
		return printCliError(commandName, levelMapLoadFailureCode(request), QStringLiteral("Unable to load map: %1").arg(error), format);
	}

	MapRenderOptions options;
	const QString projection = optionValue(args, QStringLiteral("--projection"));
	if (!projection.trimmed().isEmpty() && !mapRenderProjectionFromId(projection, &options.projection)) {
		return printCliError(commandName, CliExitCode::Usage,
			QStringLiteral("Unknown --projection value. Expected one of: %1.").arg(mapRenderProjectionIds().join(QStringLiteral(", "))), format);
	}
	bool ok = false;
	const int width = optionValue(args, QStringLiteral("--width")).toInt(&ok);
	if (ok && width > 0) {
		options.width = std::clamp(width, 64, 8192);
	}
	const int height = optionValue(args, QStringLiteral("--height")).toInt(&ok);
	if (ok && height > 0) {
		options.height = std::clamp(height, 64, 8192);
	}
	const int grid = optionValue(args, QStringLiteral("--grid")).toInt(&ok);
	if (ok && grid > 0) {
		options.gridSize = grid;
	}
	if (hasOption(args, QStringLiteral("--no-grid"))) {
		options.showGrid = false;
	}
	if (hasOption(args, QStringLiteral("--labels"))) {
		options.showLabels = true;
	}
	if (hasOption(args, QStringLiteral("--links"))) {
		options.showTargetLinks = true;
	}
	if (hasOption(args, QStringLiteral("--high-contrast"))) {
		options.highContrast = true;
	}
	const QString highlight = optionValue(args, QStringLiteral("--highlight"));
	if (!highlight.trimmed().isEmpty() && selectLevelMapObject(&document, highlight, &error)) {
		options.highlightKind = document.selectionKind;
		options.highlightObjectId = document.selectedObjectId;
	}
	// --leak draws a compiler point file (.pts or .lin) over the map, the trail
	// the Levels viewport shows after a leaking build.
	const QString leakPath = optionValue(args, QStringLiteral("--leak"));
	if (!leakPath.trimmed().isEmpty()) {
		const LeakPointFile leak = loadLeakPointFile(leakPath);
		if (!leak.valid) {
			return printCliError(commandName, CliExitCode::NotFound, leak.error, format);
		}
		for (qsizetype index = 0; index + 2 < leak.pointsXyz.size(); index += 3) {
			LevelMapVec3 point;
			point.x = leak.pointsXyz.at(index);
			point.y = leak.pointsXyz.at(index + 1);
			point.z = leak.pointsXyz.at(index + 2);
			point.valid = true;
			options.leakTrail.push_back(point);
		}
	}

	const QString outputPath = optionValue(args, QStringLiteral("--output"));
	const bool dryRun = hasOption(args, QStringLiteral("--dry-run"));
	if (outputPath.trimmed().isEmpty()) {
		MapRenderReport report;
		const QString svg = renderLevelMapSvg(document, options, &report);
		if (format == CliOutputFormat::Json) {
			QJsonObject object = cliResultJson(commandName);
			object.insert(QStringLiteral("map"), document.mapName);
			object.insert(QStringLiteral("projection"), mapRenderProjectionId(options.projection));
			object.insert(QStringLiteral("width"), report.width);
			object.insert(QStringLiteral("height"), report.height);
			object.insert(QStringLiteral("unitsPerPixel"), report.unitsPerPixel);
			object.insert(QStringLiteral("drawnLinedefs"), report.drawnLinedefCount);
			object.insert(QStringLiteral("drawnThings"), report.drawnThingCount);
			object.insert(QStringLiteral("drawnBrushes"), report.drawnBrushCount);
			object.insert(QStringLiteral("drawnPatches"), report.drawnPatchCount);
			object.insert(QStringLiteral("drawnEntities"), report.drawnEntityCount);
			object.insert(QStringLiteral("drawnSectors"), report.drawnSectorCount);
			object.insert(QStringLiteral("drawnLeakPoints"), report.drawnLeakPointCount);
			object.insert(QStringLiteral("drawnTargetLinks"), report.drawnTargetLinkCount);
			object.insert(QStringLiteral("svg"), svg);
			printJson(object);
		} else {
			std::cout << text(svg) << "\n";
		}
		return exitCodeValue(CliExitCode::Success);
	}

	const MapRenderReport report = writeLevelMapSvg(document, options, outputPath, dryRun, hasOption(args, QStringLiteral("--overwrite")));
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, report.succeeded() ? CliExitCode::Success : CliExitCode::Failure);
		object.insert(QStringLiteral("map"), document.mapName);
		object.insert(QStringLiteral("outputPath"), report.outputPath);
		object.insert(QStringLiteral("dryRun"), dryRun);
		object.insert(QStringLiteral("rendered"), report.rendered);
		object.insert(QStringLiteral("width"), report.width);
		object.insert(QStringLiteral("height"), report.height);
		object.insert(QStringLiteral("warnings"), QJsonArray::fromStringList(report.warnings));
		if (!report.error.isEmpty()) {
			object.insert(QStringLiteral("error"), report.error);
		}
		printJson(object);
	} else {
		std::cout << text(mapRenderReportText(report)) << "\n";
	}
	return exitCodeValue(report.succeeded() ? CliExitCode::Success : CliExitCode::Failure);
}

// ---------------------------------------------------------------------------
// bsp inspect
// ---------------------------------------------------------------------------

int runBspInspectCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format)
{
	Q_UNUSED(args);
	if (path.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a compiled .bsp path.").arg(commandName), format);
	}
	if (!QFileInfo::exists(path)) {
		return printCliError(commandName, CliExitCode::NotFound, QStringLiteral("Compiled map not found: %1").arg(QDir::toNativeSeparators(path)), format);
	}

	const CompiledMapArtifacts artifacts = inspectCompiledMapArtifacts(path);
	const bool valid = artifacts.bsp.valid;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, valid ? CliExitCode::Success : CliExitCode::ValidationFailed);
		object.insert(QStringLiteral("artifacts"), compiledMapArtifactsJson(artifacts));
		printJson(object);
	} else {
		std::cout << text(compiledMapArtifactsText(artifacts)) << "\n";
	}
	return exitCodeValue(valid ? CliExitCode::Success : CliExitCode::ValidationFailed);
}

// ---------------------------------------------------------------------------
// build pipelines
// ---------------------------------------------------------------------------

BuildPipelineRequest buildPipelineRequestFromArgs(const QString& pipelineId, const QStringList& args)
{
	BuildPipelineRequest request;
	request.pipelineId = pipelineId;
	request.inputPath = hasOption(args, QStringLiteral("--input")) ? optionValue(args, QStringLiteral("--input")) : QString();
	request.outputPath = optionValue(args, QStringLiteral("--output"));
	request.workingDirectory = optionValue(args, QStringLiteral("--working-directory"));
	request.workspaceRootPath = optionValue(args, QStringLiteral("--workspace-root"));
	request.manifestDirectory = optionValue(args, QStringLiteral("--manifest"));
	request.dryRun = hasOption(args, QStringLiteral("--dry-run"));
	request.registerOutputs = hasOption(args, QStringLiteral("--register-output")) || hasOption(args, QStringLiteral("--register-outputs"));

	for (int index = 0; index + 1 < args.size(); ++index) {
		if (args.at(index) == QStringLiteral("--disable-stage")) {
			request.disabledStageIds << args.at(index + 1);
		}
		if (args.at(index) == QStringLiteral("--stage-args")) {
			// "--stage-args <stageId>=<args>" keeps per-stage flags separable.
			const QString value = args.at(index + 1);
			const qsizetype equals = value.indexOf(QLatin1Char('='));
			if (equals > 0) {
				request.stageExtraArguments.insert(value.left(equals),
					value.mid(equals + 1).split(QLatin1Char(' '), Qt::SkipEmptyParts));
			}
		}
	}

	const QString searchPaths = optionValue(args, QStringLiteral("--compiler-search-paths"));
	if (!searchPaths.trimmed().isEmpty()) {
		request.extraSearchPaths = searchPaths.split(QLatin1Char(';'), Qt::SkipEmptyParts);
	}
	bool ok = false;
	const int timeout = optionValue(args, QStringLiteral("--timeout-ms")).toInt(&ok);
	if (ok && timeout > 0) {
		request.stageTimeoutMs = timeout;
	}

	if (!request.workspaceRootPath.trimmed().isEmpty()) {
		ProjectManifest manifest;
		if (loadProjectManifest(request.workspaceRootPath, &manifest)) {
			request.extraSearchPaths = effectiveProjectCompilerSearchPaths(manifest, request.extraSearchPaths);
			request.executableOverrides = effectiveProjectCompilerToolOverrides(manifest, request.executableOverrides);
		}
	}
	return request;
}

int runBuildListCommand(const QString& commandName, CliOutputFormat format)
{
	const QVector<BuildPipelineDescriptor> pipelines = buildPipelineDescriptors();
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		QJsonArray array;
		for (const BuildPipelineDescriptor& pipeline : pipelines) {
			QJsonObject entry;
			entry.insert(QStringLiteral("id"), pipeline.id);
			entry.insert(QStringLiteral("displayName"), pipeline.displayName);
			entry.insert(QStringLiteral("engineFamily"), pipeline.engineFamily);
			entry.insert(QStringLiteral("description"), pipeline.description);
			entry.insert(QStringLiteral("inputExtensions"), QJsonArray::fromStringList(pipeline.inputExtensions));
			QJsonArray stages;
			for (const BuildPipelineStage& stage : pipeline.stages) {
				QJsonObject stageObject;
				stageObject.insert(QStringLiteral("id"), stage.id);
				stageObject.insert(QStringLiteral("profileId"), stage.profileId);
				stageObject.insert(QStringLiteral("displayName"), stage.displayName);
				stageObject.insert(QStringLiteral("optional"), stage.optional);
				stageObject.insert(QStringLiteral("enabledByDefault"), stage.enabledByDefault);
				stageObject.insert(QStringLiteral("inputFromStageId"), stage.inputFromStageId);
				stages.append(stageObject);
			}
			entry.insert(QStringLiteral("stages"), stages);
			array.append(entry);
		}
		object.insert(QStringLiteral("pipelines"), array);
		printJson(object);
		return exitCodeValue(CliExitCode::Success);
	}

	std::cout << "Build pipelines\n";
	for (const BuildPipelineDescriptor& pipeline : pipelines) {
		std::cout << text(QStringLiteral("- %1 (%2, %3)\n").arg(pipeline.id, pipeline.displayName, pipeline.engineFamily));
		for (const BuildPipelineStage& stage : pipeline.stages) {
			std::cout << text(QStringLiteral("    %1 -> %2%3\n")
				.arg(stage.id, stage.profileId, stage.optional ? QStringLiteral(" [optional]") : QString()));
		}
	}
	return exitCodeValue(CliExitCode::Success);
}

int runBuildPlanCommand(const QString& commandName, const QString& pipelineId, const QStringList& args, CliOutputFormat format)
{
	if (pipelineId.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage,
			QStringLiteral("%1 requires a pipeline id. Known pipelines: %2.").arg(commandName, buildPipelineIds().join(QStringLiteral(", "))), format);
	}
	const BuildPipelineRequest request = buildPipelineRequestFromArgs(pipelineId, args);
	if (request.inputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --input <map path>.").arg(commandName), format);
	}
	const BuildPipelineResult result = planBuildPipeline(request);
	if (!result.errors.isEmpty() && result.stages.isEmpty()) {
		return printCliError(commandName, CliExitCode::NotFound, result.errors.join(QStringLiteral(" ")), format);
	}
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("pipeline"), buildPipelineResultJson(result));
		printJson(object);
	} else {
		std::cout << text(buildPipelineResultText(result)) << "\n";
	}
	return exitCodeValue(CliExitCode::Success);
}

int runBuildRunCommand(const QString& commandName, const QString& pipelineId, const QStringList& args, CliOutputFormat format)
{
	if (pipelineId.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage,
			QStringLiteral("%1 requires a pipeline id. Known pipelines: %2.").arg(commandName, buildPipelineIds().join(QStringLiteral(", "))), format);
	}
	BuildPipelineRequest request = buildPipelineRequestFromArgs(pipelineId, args);
	if (request.inputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires --input <map path>.").arg(commandName), format);
	}

	const bool watch = hasOption(args, QStringLiteral("--watch")) && format != CliOutputFormat::Json;
	BuildPipelineCallbacks callbacks;
	if (watch) {
		callbacks.logEntry = [](const CompilerTaskLogEntry& entry) {
			std::cout << text(QStringLiteral("[%1] %2\n").arg(entry.level, entry.message));
			std::cout.flush();
		};
		callbacks.stageStarted = [](int stageIndex, const BuildPipelineStage& stage) {
			std::cout << text(QStringLiteral("== stage %1: %2\n").arg(stageIndex + 1).arg(stage.displayName));
			std::cout.flush();
		};
		callbacks.stageFinished = [](int stageIndex, const BuildPipelineStageResult& result) {
			std::cout << text(QStringLiteral("== stage %1 finished: %2\n")
				.arg(stageIndex + 1).arg(operationStateId(result.state)));
			std::cout.flush();
		};
	}

	const BuildPipelineResult result = runBuildPipeline(request, callbacks);

	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, result.succeeded() ? CliExitCode::Success : CliExitCode::Failure);
		object.insert(QStringLiteral("pipeline"), buildPipelineResultJson(result));
		printJson(object);
	} else {
		std::cout << text(buildPipelineResultText(result)) << "\n";
	}
	if (result.cancelled) {
		return exitCodeValue(CliExitCode::Failure);
	}
	return exitCodeValue(result.succeeded() ? CliExitCode::Success : CliExitCode::Failure);
}

// ---------------------------------------------------------------------------
// launch
// ---------------------------------------------------------------------------

int runLaunchCommand(const QString& commandName, const QStringList& args, bool execute, CliOutputFormat format)
{
	StudioSettings settings;
	const QVector<GameInstallationProfile> installations = settings.gameInstallations();
	if (installations.isEmpty()) {
		return printCliError(commandName, CliExitCode::NotFound,
			QStringLiteral("No game installation profiles are saved. Add one with `install add` first."), format);
	}

	const QString requestedId = hasOption(args, QStringLiteral("--installation"))
		? optionValue(args, QStringLiteral("--installation"))
		: settings.selectedGameInstallationId();
	GameInstallationProfile installation = installations.first();
	bool found = requestedId.trimmed().isEmpty();
	for (const GameInstallationProfile& profile : installations) {
		if (sameGameInstallationId(profile.id, requestedId)) {
			installation = profile;
			found = true;
			break;
		}
	}
	if (!found) {
		return printCliError(commandName, CliExitCode::NotFound,
			QStringLiteral("Installation profile not found: %1").arg(requestedId), format);
	}

	GameLaunchRequest request;
	request.launchProfileId = optionValue(args, QStringLiteral("--launch-profile"));
	if (request.launchProfileId.trimmed().isEmpty()) {
		request.launchProfileId = defaultGameLaunchProfileId(installation.engineFamily);
	}
	request.executablePath = optionValue(args, QStringLiteral("--executable"));
	request.mapName = hasOption(args, QStringLiteral("--map")) ? optionValue(args, QStringLiteral("--map")) : optionValue(args, QStringLiteral("--map-name"));
	request.modDirectory = optionValue(args, QStringLiteral("--mod"));
	request.baseDirectory = hasOption(args, QStringLiteral("--basedir")) ? optionValue(args, QStringLiteral("--basedir")) : installation.rootPath;
	request.bspPath = optionValue(args, QStringLiteral("--bsp"));
	// A compiled map loads by its file name, so --bsp names the map when --map
	// does not; the Build page's launch does the same.
	if (request.mapName.trimmed().isEmpty() && QFileInfo(request.bspPath).suffix().compare(QStringLiteral("bsp"), Qt::CaseInsensitive) == 0) {
		request.mapName = QFileInfo(request.bspPath).completeBaseName();
	}
	request.workingDirectory = optionValue(args, QStringLiteral("--working-directory"));
	const QString extraArgs = optionValue(args, QStringLiteral("--extra-args"));
	if (!extraArgs.trimmed().isEmpty()) {
		request.extraArguments = extraArgs.split(QLatin1Char(' '), Qt::SkipEmptyParts);
	}
	request.dryRun = !execute;

	// --deploy copies the built map into the game folder the engine loads maps
	// from (Quake-family games); Doom-family ports read the PWAD in place.
	const bool deploy = hasOption(args, QStringLiteral("--deploy"));
	if (deploy && request.bspPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("--deploy needs --bsp <built map>."), format);
	}
	// Installations are read-only until the user allows test maps, the same
	// permission the GUI asks for once. --allow-test-maps grants it; only a run
	// records it on the profile, so planning never changes settings.
	const bool allowTestMaps = hasOption(args, QStringLiteral("--allow-test-maps"));
	GameInstallationProfile deployTarget = installation;
	if (allowTestMaps) {
		deployTarget.readOnly = false;
	}
	const GameMapDeployPlan deployPlan = deploy ? planGameMapDeploy(deployTarget, request.modDirectory, request.bspPath) : GameMapDeployPlan();
	const bool deployBlocked = deploy && deployPlan.required && !deployPlan.runnable();

	const GameLaunchPlan plan = buildGameLaunchPlan(request, installation);
	qint64 pid = 0;
	QString startError;
	QStringList deployed;
	bool started = false;
	if (execute && plan.runnable && !deployBlocked) {
		if (deploy && deployPlan.required && installation.readOnly) {
			installation.readOnly = false;
			settings.upsertGameInstallation(installation);
			settings.sync();
		}
		if (deploy && deployPlan.required && !deployGameMap(deployPlan, &deployed, &startError)) {
			startError = QStringLiteral("The map was not copied into the game: %1").arg(startError);
		} else {
			started = startGameLaunch(plan, &pid, &startError);
		}
	}

	const CliExitCode resultCode = (!plan.runnable || deployBlocked)
		? CliExitCode::Unavailable
		: ((execute && !started) ? CliExitCode::Failure : CliExitCode::Success);
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, resultCode);
		object.insert(QStringLiteral("installationId"), installation.id);
		object.insert(QStringLiteral("plan"), gameLaunchPlanJson(plan));
		if (deploy) {
			object.insert(QStringLiteral("deploy"), gameMapDeployPlanJson(deployPlan));
			object.insert(QStringLiteral("deployed"), stringArrayJson(deployed));
		}
		object.insert(QStringLiteral("executed"), started);
		if (started) {
			object.insert(QStringLiteral("processId"), pid);
		}
		if (!startError.isEmpty()) {
			object.insert(QStringLiteral("error"), startError);
		}
		printJson(object);
	} else {
		std::cout << text(gameLaunchPlanText(plan)) << "\n";
		if (deploy) {
			std::cout << text(gameMapDeployPlanText(deployPlan)) << "\n";
			if (deployPlan.required && !deployPlan.allowed) {
				std::cout << "Pass --allow-test-maps to allow copies into this installation; a run saves that choice on its profile.\n";
			}
			for (const QString& path : deployed) {
				std::cout << "Copied: " << text(QDir::toNativeSeparators(path)) << "\n";
			}
		}
		if (execute) {
			std::cout << text(started
				? QStringLiteral("Launched process %1.\n").arg(pid)
				: QStringLiteral("Launch was not started: %1\n").arg(!startError.isEmpty() ? startError
					: (deployBlocked ? QStringLiteral("the built map cannot be copied into the game") : QStringLiteral("plan is not runnable"))));
		}
	}

	return exitCodeValue(resultCode);
}

// ---------------------------------------------------------------------------
// texture decoding and palettes
// ---------------------------------------------------------------------------

int runTextureRecoveriesCommand(const QString& directory, CliOutputFormat format)
{
	const QString command = QStringLiteral("texture recoveries");
	if (directory.isEmpty()) { return printCliError(command, CliExitCode::Usage, QStringLiteral("texture recoveries requires a recovery directory."), format); }
	const auto list = listTextureRecoveries(directory);
	if (!list.error.isEmpty()) { return printCliError(command, CliExitCode::ValidationFailed, list.error, format); }
	QJsonArray records;
	for (const auto& record : list.records) {
		records.append(QJsonObject{{QStringLiteral("path"), record.path}, {QStringLiteral("id"), record.id}, {QStringLiteral("displayName"), record.displayName},
			{QStringLiteral("sourcePath"), record.sourcePath}, {QStringLiteral("sourceSha256"), QString::fromLatin1(record.sourceSha256.toHex())},
			{QStringLiteral("sha256"), QString::fromLatin1(record.recordSha256.toHex())}, {QStringLiteral("writtenUtc"), record.writtenUtc.toString(Qt::ISODateWithMs)},
			{QStringLiteral("revision"), QString::number(record.revision)}, {QStringLiteral("payloadBytes"), record.payloadBytes},
			{QStringLiteral("width"), record.size.width()}, {QStringLiteral("height"), record.size.height()}, {QStringLiteral("layerCount"), record.layerCount},
			{QStringLiteral("validEnvelope"), record.isValid()}, {QStringLiteral("error"), record.error}});
	}
	if (format == CliOutputFormat::Json) {
		auto object = cliResultJson(command, CliExitCode::Success); object.insert(QStringLiteral("directory"), QFileInfo(directory).absoluteFilePath());
		object.insert(QStringLiteral("checkpoints"), records); object.insert(QStringLiteral("truncated"), list.truncated); printJson(object);
	} else {
		for (const auto& record : list.records) { std::cout << text(QStringLiteral("%1: %2 (%3 x %4, layers: %5)\n").arg(record.path, record.isValid() ? record.displayName : record.error).arg(record.size.width()).arg(record.size.height()).arg(record.layerCount)); }
		std::cout << text(QStringLiteral("Checkpoints: %1%2\n").arg(records.size()).arg(list.truncated ? QStringLiteral(" (scan limit reached)") : QString()));
	}
	return 0;
}

int runTextureRecoverCommand(const QString& path, const QStringList& args, CliOutputFormat format)
{
	const QString command = QStringLiteral("texture recover"), output = optionValue(args, QStringLiteral("--output"));
	if (path.isEmpty() || output.isEmpty() || hasOption(args, QStringLiteral("--overwrite"))) {
		return printCliError(command, CliExitCode::Usage, QStringLiteral("texture recover requires a checkpoint and --output <new.vtexture>. Recovery cannot overwrite existing files."), format);
	}
	TextureDocument document; QJsonObject metadata; TextureRecoveryInfo info; QString error;
	if (!restoreTextureRecovery(path, &document, &metadata, &info, &error)) { return printCliError(command, CliExitCode::ValidationFailed, error, format); }
	TextureProjectSaveRequest request; request.path = output; request.dryRun = hasOption(args, QStringLiteral("--dry-run"));
	const auto saved = writeTextureProject(document, request, metadata);
	if (!saved.succeeded) { return printCliError(command, CliExitCode::Failure, saved.error, format); }
	if (format == CliOutputFormat::Json) {
		auto object = cliResultJson(command, CliExitCode::Success); object.insert(QStringLiteral("checkpoint"), info.path);
		object.insert(QStringLiteral("outputPath"), QFileInfo(output).absoluteFilePath()); object.insert(QStringLiteral("written"), saved.written);
		object.insert(QStringLiteral("dryRun"), request.dryRun); object.insert(QStringLiteral("layerCount"), document.layers().size());
		object.insert(QStringLiteral("width"), document.size().width()); object.insert(QStringLiteral("height"), document.size().height()); printJson(object);
	} else { std::cout << text(QStringLiteral("%1 recovered project: %2\n").arg(request.dryRun ? QStringLiteral("Would write") : QStringLiteral("Wrote"), QFileInfo(output).absoluteFilePath())); }
	return 0;
}

int runTextureInspectCommand(const QString& path, CliOutputFormat format)
{
	const QString command = QStringLiteral("texture inspect");
	if (path.isEmpty()) { return printCliError(command, CliExitCode::Usage, QStringLiteral("texture inspect requires a .vtexture project."), format); }
	TextureDocument document; TextureProjectIdentity identity; QJsonObject metadata; QString error;
	if (!readTextureProject(path, &document, &identity, &metadata, &error)) { return printCliError(command, CliExitCode::ValidationFailed, error, format); }
	QJsonArray layers;
	for (const auto& layer : document.layers()) {
		layers.append(QJsonObject{{QStringLiteral("id"), layer.id}, {QStringLiteral("name"), layer.name}, {QStringLiteral("visible"), layer.visible},
			{QStringLiteral("locked"), layer.locked}, {QStringLiteral("opacity"), layer.opacity}, {QStringLiteral("blend"), textureBlendModeId(layer.blend)},
			{QStringLiteral("indexed"), layer.pixels.format() == QImage::Format_Indexed8}});
	}
	if (format == CliOutputFormat::Json) {
		auto object = cliResultJson(command, CliExitCode::Success);
		object.insert(QStringLiteral("path"), identity.path); object.insert(QStringLiteral("sha256"), QString::fromLatin1(identity.sha256.toHex()));
		object.insert(QStringLiteral("width"), document.size().width()); object.insert(QStringLiteral("height"), document.size().height());
		object.insert(QStringLiteral("activeLayer"), document.activeLayerIndex()); object.insert(QStringLiteral("layers"), layers);
		object.insert(QStringLiteral("metadata"), metadata); printJson(object);
	} else {
		std::cout << text(QStringLiteral("%1: %2 x %3, %4 layers\n").arg(identity.path).arg(document.size().width()).arg(document.size().height()).arg(layers.size()));
		for (const auto& value : layers) { const auto layer = value.toObject(); std::cout << text(QStringLiteral("  %1: %2 (%3, %4%)\n").arg(layer.value(QStringLiteral("id")).toInt()).arg(layer.value(QStringLiteral("name")).toString(), layer.value(QStringLiteral("blend")).toString()).arg(layer.value(QStringLiteral("opacity")).toInt())); }
	}
	return 0;
}

int runTextureProfilesCommand(CliOutputFormat format)
{
	QJsonArray profiles;
	for (const auto& profile : textureExportProfiles()) {
		TextureExportOptions options; options.format = profile.format;
		profiles.append(QJsonObject{{QStringLiteral("id"), profile.id}, {QStringLiteral("name"), profile.name}, {QStringLiteral("suffix"), profile.suffix},
			{QStringLiteral("description"), profile.description}, {QStringLiteral("indexed"), profile.indexed}, {QStringLiteral("mipmapped"), profile.mipmapped}, {QStringLiteral("defaults"), textureExportOptionsJson(options)}});
		if (format != CliOutputFormat::Json) { std::cout << text(QStringLiteral("%1 (*.%2): %3\n").arg(profile.id, profile.suffix, profile.description)); }
	}
	if (format == CliOutputFormat::Json) { auto object = cliResultJson(QStringLiteral("texture profiles"), CliExitCode::Success); object.insert(QStringLiteral("profiles"), profiles); printJson(object); }
	return 0;
}

int runTextureExportCommand(const QString& command, const QString& input, const QStringList& args, bool validateOnly, CliOutputFormat format)
{
	const auto output = optionValue(args, QStringLiteral("--output"));
	const bool stagingOnly = command == QStringLiteral("texture stage");
	const auto targetPackage = optionValue(args, QStringLiteral("--target-package")), targetEntry = optionValue(args, QStringLiteral("--target-entry"));
	if (stagingOnly && (targetPackage.isEmpty() || targetEntry.isEmpty() || !output.endsWith(QStringLiteral(".vibepackage"), Qt::CaseInsensitive))) {
		return printCliError(command, CliExitCode::Usage, QStringLiteral("texture stage requires --target-package, --target-entry, and --output <draft.vibepackage>. --replace-texture permits entry replacement; --overwrite permits draft replacement."), format);
	}
	if (input.isEmpty() || (!validateOnly && output.isEmpty()) || (validateOnly && !output.isEmpty())) {
		return printCliError(command, CliExitCode::Usage, QStringLiteral("Supply an input image/project or --package and --entry. Export requires --output; validate accepts no output. Use --profile and optional --export-options <json>."), format);
	}
	QString error; QJsonObject metadata; TextureDocument document; TextureExportOptions options; IdTechImageDecodeResult decoded;
	const auto requestedProfile = optionValue(args, QStringLiteral("--profile"));
	const auto requestedPalette = optionValue(args, QStringLiteral("--palette"));
	if (!requestedPalette.isEmpty() && !idTechPaletteIds().contains(requestedPalette)) { return printCliError(command, CliExitCode::Usage, QStringLiteral("Unknown palette id: %1").arg(requestedPalette), format); }
	const auto paletteFile = optionValue(args, QStringLiteral("--palette-file"));
	const auto paletteRoot = optionValue(args, QStringLiteral("--palette-root"));
	if (!paletteFile.isEmpty() && !paletteRoot.isEmpty()) { return printCliError(command, CliExitCode::Usage, QStringLiteral("Choose --palette-file or --palette-root."), format); }
	const QString paletteId = !requestedPalette.isEmpty() ? requestedPalette :
		(requestedProfile.startsWith(QStringLiteral("doom-")) || QFileInfo(input).suffix().compare(QStringLiteral("lmp"), Qt::CaseInsensitive) == 0 ? QStringLiteral("doom") :
		(requestedProfile == QStringLiteral("quake2-wal") || QFileInfo(input).suffix().compare(QStringLiteral("wal"), Qt::CaseInsensitive) == 0 ? QStringLiteral("quake2") : QStringLiteral("quake")));
	IdTechPaletteResolution resolution; resolution.palette = generatedIdTechPalette(paletteId); resolution.requestedPaletteId = paletteId;
	PackageArchive archive; const auto package = optionValue(args, QStringLiteral("--package"));
	if (!package.isEmpty()) {
		if (!archive.load(package, &error)) { return printCliError(command, CliExitCode::NotFound, error, format); }
		resolution = resolveIdTechPalette(archive, paletteId);
	}
	if (!paletteRoot.isEmpty()) { resolution = resolveIdTechPaletteFromDirectory(paletteRoot, paletteId); }
	if (!paletteFile.isEmpty()) {
		QFile file(paletteFile);
		if (!QFileInfo(paletteFile).isFile() || !file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024) { return printCliError(command, CliExitCode::Usage, QStringLiteral("Cannot read the palette file, or it exceeds 4 MiB."), format); }
		const auto bytes = file.read(4 * 1024 * 1024 + 1); resolution = {};
		const bool pcx = QFileInfo(paletteFile).suffix().compare(QStringLiteral("pcx"), Qt::CaseInsensitive) == 0;
		if (file.error() != QFile::NoError || bytes.size() > 4 * 1024 * 1024 || !(pcx ? parsePcxPalette(bytes, paletteId, &resolution.palette, &error) : parseIdTechPaletteBytes(bytes, paletteId, &resolution.palette, &error))) {
			return printCliError(command, CliExitCode::ValidationFailed, error.isEmpty() ? QStringLiteral("Unable to read palette bytes.") : error, format);
		}
		resolution.requestedPaletteId = paletteId; resolution.sourceVirtualPath = QFileInfo(paletteFile).absoluteFilePath(); resolution.palette.sourceDescription = resolution.sourceVirtualPath;
	}
	if (archive.isOpen()) {
		QByteArray bytes;
		if (!archive.readEntryBytes(input, &bytes, &error, 64 * 1024 * 1024)) { return printCliError(command, CliExitCode::Failure, error, format); }
		QString decodePath = input;
		for (const auto& entry : archive.entries()) { if (entry.virtualPath.compare(input, Qt::CaseInsensitive) == 0) { decodePath = assetDetectionPath(input, entry.typeHint); break; } }
		if (requestedPalette.isEmpty() && paletteFile.isEmpty() && paletteRoot.isEmpty()) {
			const auto sourceId = defaultIdTechPaletteIdForImage(detectIdTechImageFormat(decodePath, bytes), bytes.size());
			if (!sourceId.isEmpty()) { resolution = resolveIdTechPalette(archive, sourceId); }
		}
		IdTechImageDecodeContext decodeContext;
		decodeContext.archive = &archive;
		decoded = decodeIdTechImage(decodePath, bytes, resolution.palette, decodeContext);
		if (!decoded.decoded || !document.reset(decoded.image, &error)) { return printCliError(command, CliExitCode::ValidationFailed, error.isEmpty() ? decoded.error : error, format); }
	} else if (QFileInfo(input).suffix().compare(QStringLiteral("vtexture"), Qt::CaseInsensitive) == 0) {
		if (!readTextureProject(input, &document, nullptr, &metadata, &error)) { return printCliError(command, CliExitCode::ValidationFailed, error, format); }
		if (requestedPalette.isEmpty() && paletteFile.isEmpty() && paletteRoot.isEmpty() && metadata.contains(QStringLiteral("palette"))) {
			if (!texturePaletteFromMetadata(metadata.value(QStringLiteral("palette")).toObject(), &resolution, &error)) { return printCliError(command, CliExitCode::ValidationFailed, error, format); }
		}
	} else if (!loadTextureFile(input, resolution.palette, &document, &error, &decoded)) { return printCliError(command, CliExitCode::ValidationFailed, error, format); }
	options = textureExportOptionsForImage(decoded);
	QJsonObject settings = metadata.contains(QStringLiteral("export")) ? metadata.value(QStringLiteral("export")).toObject() : textureExportOptionsJson(options);
	const auto settingsPath = optionValue(args, QStringLiteral("--export-options"));
	if (!settingsPath.isEmpty()) {
		QFile file(settingsPath);
		if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024) { return printCliError(command, CliExitCode::Usage, QStringLiteral("Export options must be a JSON object no larger than 64 KiB."), format); }
		QJsonParseError parseError; const auto json = QJsonDocument::fromJson(file.read(64 * 1024 + 1), &parseError);
		if (file.error() != QFile::NoError || parseError.error != QJsonParseError::NoError || !json.isObject()) { return printCliError(command, CliExitCode::Usage, QStringLiteral("Export options must be a JSON object."), format); }
		const auto overrides = json.object(); for (auto it = overrides.begin(); it != overrides.end(); ++it) { settings.insert(it.key(), it.value()); }
	}
	if (!requestedProfile.isEmpty()) { settings.insert(QStringLiteral("profile"), requestedProfile); }
	if (hasOption(args, QStringLiteral("--allow-generated-palette"))) { settings.insert(QStringLiteral("allowGeneratedPalette"), true); }
	if (!textureExportOptionsFromJson(settings, &options, &error)) { return printCliError(command, CliExitCode::Usage, error, format); }
	PackageStagingModel staging;
	if (stagingOnly) {
		if (QFileInfo(output).exists() && !hasOption(args, QStringLiteral("--overwrite"))) { return printCliError(command, CliExitCode::ValidationFailed, QStringLiteral("The draft output exists. Use --overwrite after reviewing it."), format); }
		if (targetPackage.endsWith(QStringLiteral(".vibepackage"), Qt::CaseInsensitive)) {
			if (!PackageDraft::load(targetPackage, &staging, &error)) { return printCliError(command, CliExitCode::ValidationFailed, error, format); }
		} else {
			PackageArchive target;
			if (!target.load(targetPackage, &error) || !staging.loadBaseArchive(target, &error)) { return printCliError(command, CliExitCode::ValidationFailed, error, format); }
		}
		if (paletteFile.isEmpty() && paletteRoot.isEmpty()) {
			const auto targetPaletteId = !requestedPalette.isEmpty() ? requestedPalette :
				(options.format == TextureExportFormat::DoomFlat || options.format == TextureExportFormat::DoomPatch ? QStringLiteral("doom") : options.format == TextureExportFormat::Quake2Wal ? QStringLiteral("quake2") : QStringLiteral("quake"));
			const auto targetPalette = resolveIdTechPalette(PackageStagingArchive(staging), targetPaletteId);
			if (targetPalette.fromPackage) { resolution = targetPalette; }
		}
	}
	TextureExportResult result; const bool dryRun = validateOnly || hasOption(args, QStringLiteral("--dry-run"));
	bool succeeded = (validateOnly || stagingOnly) ? (result = encodeTextureExport(document.image(), options, resolution), result.succeeded) :
		saveTextureExport(document.image(), options, resolution, output, hasOption(args, QStringLiteral("--overwrite")), dryRun, &result, &error);
	if (succeeded && stagingOnly) {
		succeeded = stageTextureExport(result, options, targetEntry, &staging, hasOption(args, QStringLiteral("--replace-texture")), &error);
		if (succeeded && !dryRun) { succeeded = PackageDraft::save(output, &staging, hasOption(args, QStringLiteral("--overwrite")), &error, packageDraftWriteControl()); }
	}
	if (!succeeded && error.isEmpty()) { error = result.error; }
	if (format == CliOutputFormat::Json) {
		auto object = cliResultJson(command, succeeded ? CliExitCode::Success : CliExitCode::ValidationFailed);
		if (!error.isEmpty()) { object.insert(QStringLiteral("message"), error); }
		object.insert(QStringLiteral("input"), input); object.insert(QStringLiteral("package"), package); object.insert(QStringLiteral("outputPath"), output);
		object.insert(QStringLiteral("written"), succeeded && !dryRun); object.insert(QStringLiteral("dryRun"), dryRun); object.insert(QStringLiteral("layerCount"), document.layers().size());
		if (stagingOnly) {
			object.insert(QStringLiteral("targetPackage"), targetPackage); object.insert(QStringLiteral("targetEntry"), targetEntry);
			object.insert(QStringLiteral("staging"), QJsonDocument::fromJson(staging.manifestJson()).object());
			object.insert(QStringLiteral("packageWritten"), false);
		}
		object.insert(QStringLiteral("export"), textureExportReportJson(options, resolution, result)); printJson(object);
	} else {
		if (!succeeded) { std::cerr << text(error) << "\n"; }
		else { std::cout << text(QStringLiteral("%1 %2: %3 x %4, %5 bytes, %6 mip levels\n").arg(validateOnly ? QStringLiteral("Validated") : (dryRun ? QStringLiteral("Would write") : QStringLiteral("Wrote")), validateOnly ? input : output).arg(document.size().width()).arg(document.size().height()).arg(result.bytes.size()).arg(result.mipLevels.size())); }
		for (const auto& warning : result.warnings) { std::cout << text(warning) << "\n"; }
	}
	return succeeded ? 0 : static_cast<int>(CliExitCode::ValidationFailed);
}

int runTextureEditCommand(const QString& commandName, const QString& input, bool create, const QStringList& args, CliOutputFormat format)
{
	const QString output = optionValue(args, QStringLiteral("--output"));
	const QString recipePath = optionValue(args, QStringLiteral("--operations"));
	if (output.isEmpty() || (!create && (input.isEmpty() || recipePath.isEmpty()))) {
		return printCliError(commandName, CliExitCode::Usage, create
			? QStringLiteral("texture create requires --size <width>x<height> and --output <file.png|file.vtexture>.")
			: QStringLiteral("texture edit requires an input image/project (or --package and --entry), --operations <recipe.json>, and --output <file.png|file.vtexture>."), format);
	}
	const QString paletteId = optionValue(args, QStringLiteral("--palette"));
	if (!paletteId.isEmpty() && !idTechPaletteIds().contains(paletteId)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unknown palette id: %1").arg(paletteId), format);
	}
	IdTechPaletteResolution resolution;
	resolution.palette = generatedIdTechPalette(paletteId.isEmpty() ? QStringLiteral("quake") : paletteId);
	PackageArchive archive;
	QString error;
	const auto package = optionValue(args, QStringLiteral("--package"));
	if (!package.isEmpty()) {
		if (!archive.load(package, &error)) { return printCliError(commandName, CliExitCode::NotFound, error, format); }
		resolution = resolveIdTechPalette(archive, paletteId.isEmpty() ? QStringLiteral("quake") : paletteId);
	}
	TextureDocument document; TextureProjectIdentity identity; QJsonObject metadata;
	if (create) {
		const auto sizeText = optionValue(args, QStringLiteral("--size"));
		const auto match = QRegularExpression(QStringLiteral("^([0-9]{1,5})[xX,]([0-9]{1,5})$")).match(sizeText);
		const QColor color(hasOption(args, QStringLiteral("--color")) ? optionValue(args, QStringLiteral("--color")) : QStringLiteral("transparent"));
		if (!match.hasMatch() || !document.create({match.captured(1).toInt(), match.captured(2).toInt()}, color, &error)) {
			return printCliError(commandName, CliExitCode::Usage, error.isEmpty() ? QStringLiteral("Use --size <width>x<height> and a valid --color name, #RRGGBB, or #AARRGGBB.") : error, format);
		}
	} else if (archive.isOpen()) {
		const auto decoded = decodeIdTechImageFromArchive(archive, input, paletteId, &resolution);
		if (!decoded.decoded || !document.reset(decoded.image, &error)) {
			return printCliError(commandName, CliExitCode::Failure, error.isEmpty() ? decoded.error : error, format);
		}
		metadata.insert(QStringLiteral("export"), textureExportOptionsJson(textureExportOptionsForImage(decoded)));
	} else if (QFileInfo(input).suffix().compare(QStringLiteral("vtexture"), Qt::CaseInsensitive) == 0) {
		if (!readTextureProject(input, &document, &identity, &metadata, &error)) { return printCliError(commandName, CliExitCode::ValidationFailed, error, format); }
		if (paletteId.isEmpty() && metadata.contains(QStringLiteral("palette")) && !texturePaletteFromMetadata(metadata.value(QStringLiteral("palette")).toObject(), &resolution, &error)) {
			return printCliError(commandName, CliExitCode::ValidationFailed, error, format);
		}
	} else {
		IdTechImageDecodeResult decoded;
		if (!loadTextureFile(input, resolution.palette, &document, &error, &decoded)) { return printCliError(commandName, CliExitCode::Failure, error, format); }
		metadata.insert(QStringLiteral("export"), textureExportOptionsJson(textureExportOptionsForImage(decoded)));
	}
	QJsonArray operations;
	if (!recipePath.isEmpty()) {
		QFile recipe(recipePath);
		if (!recipe.open(QIODevice::ReadOnly) || recipe.size() > 1024 * 1024) {
			return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unable to read the recipe, or it exceeds 1 MiB."), format);
		}
		QJsonParseError parseError;
		const auto recipeBytes = recipe.read(1024 * 1024 + 1);
		if (recipe.error() != QFileDevice::NoError || recipeBytes.size() > 1024 * 1024) {
			return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unable to read the recipe, or it exceeds 1 MiB."), format);
		}
		const auto json = QJsonDocument::fromJson(recipeBytes, &parseError);
		if (parseError.error != QJsonParseError::NoError || !json.isArray()) {
			return printCliError(commandName, CliExitCode::Usage, QStringLiteral("The recipe must be a JSON array of texture operations."), format);
		}
		operations = json.array();
		if (!applyTextureOperations(&document, operations, resolution.palette, &error)) { return printCliError(commandName, CliExitCode::ValidationFailed, error, format); }
	}
	const bool dryRun = hasOption(args, QStringLiteral("--dry-run"));
	const bool project = QFileInfo(output).suffix().compare(QStringLiteral("vtexture"), Qt::CaseInsensitive) == 0;
	QString backupPath;
	if (project) {
		TextureProjectSaveRequest request; request.path = output; request.overwrite = hasOption(args, QStringLiteral("--overwrite")); request.dryRun = dryRun;
		if (!identity.path.isEmpty() && (QFileInfo(output).absoluteFilePath() == identity.path || QFileInfo(output).canonicalFilePath() == identity.canonicalPath)) {
			request.expectedSha256 = identity.sha256; request.expectedCanonicalPath = identity.canonicalPath;
		}
		metadata.insert(QStringLiteral("palette"), texturePaletteMetadata(resolution));
		const auto saved = writeTextureProject(document, request, metadata);
		if (!saved.succeeded) { return printCliError(commandName, saved.conflict ? CliExitCode::ValidationFailed : CliExitCode::Failure, saved.error, format); }
		backupPath = saved.backupPath;
	} else if (!document.savePng(output, hasOption(args, QStringLiteral("--overwrite")), dryRun, &error)) { return printCliError(commandName, CliExitCode::Failure, error, format); }
	QStringList warnings = resolution.warnings;
	if (!project) { warnings << QStringLiteral("PNG output is the visible layer composite; editable layers are retained only in .vtexture projects."); }
	if (!create && identity.path.isEmpty()) { warnings << QStringLiteral("Image import reads one surface; mipmaps are regenerated and sprite frames are not retained. Supported names, flags and offsets are retained as native project export settings."); }
	if (resolution.palette.generated) { warnings << QStringLiteral("The selected palette is a generated stand-in, not an original game palette."); }
	if (format == CliOutputFormat::Json) {
		auto object = cliResultJson(commandName, CliExitCode::Success);
		object.insert(QStringLiteral("input"), input); object.insert(QStringLiteral("package"), package);
		object.insert(QStringLiteral("outputPath"), output); object.insert(QStringLiteral("width"), document.image().width()); object.insert(QStringLiteral("height"), document.image().height());
		object.insert(QStringLiteral("operationCount"), operations.size()); object.insert(QStringLiteral("written"), !dryRun); object.insert(QStringLiteral("dryRun"), dryRun);
		object.insert(QStringLiteral("layerCount"), document.layers().size()); object.insert(QStringLiteral("outputFormat"), project ? QStringLiteral("vtexture") : QStringLiteral("png"));
		object.insert(QStringLiteral("backupPath"), backupPath);
		object.insert(QStringLiteral("paletteId"), resolution.palette.id); object.insert(QStringLiteral("paletteGenerated"), resolution.palette.generated);
		object.insert(QStringLiteral("paletteSource"), resolution.sourceVirtualPath); object.insert(QStringLiteral("warnings"), QJsonArray::fromStringList(warnings)); printJson(object);
	} else {
		std::cout << text(QStringLiteral("%1 %2 (%3 x %4, %5 operations)").arg(dryRun ? QStringLiteral("Would write") : QStringLiteral("Wrote"), output).arg(document.image().width()).arg(document.image().height()).arg(operations.size())) << "\n";
		if (!backupPath.isEmpty()) { std::cout << text(QStringLiteral("%1: %2\n").arg(dryRun ? QStringLiteral("Planned backup") : QStringLiteral("Previous version"), backupPath)); }
		for (const auto& warning : warnings) { std::cout << text(warning) << "\n"; }
	}
	return 0;
}

int runTextureDecodeCommand(const QString& commandName, const QString& packagePath, const QString& entryPath, const QStringList& args, CliOutputFormat format)
{
	if (packagePath.trimmed().isEmpty() || entryPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage,
			QStringLiteral("%1 requires a package path and an entry virtual path.").arg(commandName), format);
	}
	PackageArchive archive;
	QString error;
	if (!archive.load(packagePath, &error)) {
		return printCliError(commandName, CliExitCode::NotFound, QStringLiteral("Unable to open package: %1").arg(error), format);
	}

	const QString paletteId = optionValue(args, QStringLiteral("--palette"));
	IdTechPaletteResolution resolution;
	const IdTechImageDecodeResult decoded = decodeIdTechImageFromArchive(archive, entryPath, paletteId, &resolution);
	if (!decoded.decoded) {
		return printCliError(commandName, CliExitCode::Unavailable,
			decoded.error.isEmpty() ? QStringLiteral("Entry could not be decoded as an image.") : decoded.error, format);
	}

	const QString outputPath = optionValue(args, QStringLiteral("--output"));
	const bool dryRun = hasOption(args, QStringLiteral("--dry-run"));
	bool written = false;
	QString writeError;
	if (!outputPath.trimmed().isEmpty()) {
		if (QFileInfo::exists(outputPath) && !hasOption(args, QStringLiteral("--overwrite"))) {
			writeError = QStringLiteral("Output already exists. Pass --overwrite to replace it.");
		} else if (!dryRun) {
			QDir().mkpath(QFileInfo(outputPath).absolutePath());
			written = decoded.image.save(outputPath, "PNG");
			if (!written) {
				writeError = QStringLiteral("The decoded image could not be written.");
			}
		}
	}

	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, writeError.isEmpty() ? CliExitCode::Success : CliExitCode::Failure);
		object.insert(QStringLiteral("package"), archive.sourcePath());
		object.insert(QStringLiteral("entry"), entryPath);
		object.insert(QStringLiteral("format"), decoded.formatId);
		object.insert(QStringLiteral("formatName"), decoded.formatName);
		object.insert(QStringLiteral("width"), decoded.width);
		object.insert(QStringLiteral("height"), decoded.height);
		object.insert(QStringLiteral("mipLevels"), static_cast<int>(decoded.mipLevels.size()));
		object.insert(QStringLiteral("frames"), static_cast<int>(decoded.frames.size()));
		object.insert(QStringLiteral("paletted"), decoded.paletted);
		object.insert(QStringLiteral("hasTransparency"), decoded.hasTransparency);
		object.insert(QStringLiteral("paletteId"), resolution.palette.id);
		object.insert(QStringLiteral("paletteFromPackage"), resolution.fromPackage);
		object.insert(QStringLiteral("paletteGenerated"), resolution.palette.generated);
		object.insert(QStringLiteral("paletteSource"), resolution.sourceVirtualPath);
		object.insert(QStringLiteral("outputPath"), outputPath);
		object.insert(QStringLiteral("written"), written);
		object.insert(QStringLiteral("dryRun"), dryRun);
		object.insert(QStringLiteral("warnings"), QJsonArray::fromStringList(decoded.warnings + resolution.warnings));
		if (!writeError.isEmpty()) {
			object.insert(QStringLiteral("error"), writeError);
		}
		printJson(object);
	} else {
		for (const QString& line : idTechImageSummaryLines(decoded)) {
			std::cout << text(line) << "\n";
		}
		for (const QString& line : idTechPaletteSummaryLines(resolution)) {
			std::cout << text(line) << "\n";
		}
		if (!outputPath.trimmed().isEmpty()) {
			std::cout << text(dryRun
				? QStringLiteral("Dry run: would write %1\n").arg(QDir::toNativeSeparators(outputPath))
				: (written ? QStringLiteral("Wrote %1\n").arg(QDir::toNativeSeparators(outputPath))
					: QStringLiteral("Not written: %1\n").arg(writeError)));
		}
	}
	return exitCodeValue(writeError.isEmpty() ? CliExitCode::Success : CliExitCode::Failure);
}

int runTexturePaletteCommand(const QString& commandName, const QString& packagePath, const QStringList& args, CliOutputFormat format)
{
	const QString paletteId = optionValue(args, QStringLiteral("--palette"));
	IdTechPaletteResolution resolution;
	if (packagePath.trimmed().isEmpty()) {
		resolution.requestedPaletteId = paletteId;
		resolution.palette = generatedIdTechPalette(paletteId);
	} else {
		PackageArchive archive;
		QString error;
		if (!archive.load(packagePath, &error)) {
			return printCliError(commandName, CliExitCode::NotFound, QStringLiteral("Unable to open package: %1").arg(error), format);
		}
		resolution = resolveIdTechPalette(archive, paletteId);
	}

	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("paletteId"), resolution.palette.id);
		object.insert(QStringLiteral("displayName"), resolution.palette.displayName);
		object.insert(QStringLiteral("fromPackage"), resolution.fromPackage);
		object.insert(QStringLiteral("generated"), resolution.palette.generated);
		object.insert(QStringLiteral("sourceVirtualPath"), resolution.sourceVirtualPath);
		object.insert(QStringLiteral("transparentIndex"), resolution.palette.transparentIndex);
		object.insert(QStringLiteral("fullbrightStartIndex"), resolution.palette.fullbrightStartIndex);
		object.insert(QStringLiteral("colorCount"), static_cast<int>(resolution.palette.colors.size()));
		object.insert(QStringLiteral("searchedPaths"), QJsonArray::fromStringList(resolution.searchedPaths));
		object.insert(QStringLiteral("warnings"), QJsonArray::fromStringList(resolution.warnings));
		QJsonArray known;
		for (const IdTechPaletteDescriptor& descriptor : idTechPaletteDescriptors()) {
			QJsonObject entry;
			entry.insert(QStringLiteral("id"), descriptor.id);
			entry.insert(QStringLiteral("displayName"), descriptor.displayName);
			entry.insert(QStringLiteral("engineFamily"), descriptor.engineFamily);
			known.append(entry);
		}
		object.insert(QStringLiteral("knownPalettes"), known);
		printJson(object);
	} else {
		for (const QString& line : idTechPaletteSummaryLines(resolution)) {
			std::cout << text(line) << "\n";
		}
	}
	return exitCodeValue(CliExitCode::Success);
}


// ---------------------------------------------------------------------------
// install add / select / validate / remove
// ---------------------------------------------------------------------------

QJsonObject gameInstallationProfileJsonForCli(const GameInstallationProfile& profile, const QString& selectedId)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), profile.id);
	object.insert(QStringLiteral("gameKey"), profile.gameKey);
	object.insert(QStringLiteral("displayName"), profile.displayName);
	object.insert(QStringLiteral("engineFamily"), gameEngineFamilyId(profile.engineFamily));
	object.insert(QStringLiteral("rootPath"), profile.rootPath);
	object.insert(QStringLiteral("executablePath"), profile.executablePath);
	object.insert(QStringLiteral("paletteId"), profile.paletteId);
	object.insert(QStringLiteral("compilerProfileId"), profile.compilerProfileId);
	object.insert(QStringLiteral("selected"), sameGameInstallationId(profile.id, selectedId));
	object.insert(QStringLiteral("readOnly"), profile.readOnly);
	object.insert(QStringLiteral("hidden"), profile.hidden);
	return object;
}

QJsonObject gameInstallationValidationJsonForCli(const GameInstallationValidation& validation)
{
	QJsonObject object;
	object.insert(QStringLiteral("usable"), validation.isUsable());
	object.insert(QStringLiteral("rootExists"), validation.rootExists);
	object.insert(QStringLiteral("rootIsDirectory"), validation.rootIsDirectory);
	object.insert(QStringLiteral("executableExists"), validation.executableExists);
	object.insert(QStringLiteral("executableIsFile"), validation.executableIsFile);
	object.insert(QStringLiteral("warnings"), QJsonArray::fromStringList(validation.warnings));
	object.insert(QStringLiteral("errors"), QJsonArray::fromStringList(validation.errors));
	return object;
}

int runInstallAddCommand(const QString& commandName, const QString& rootPath, const QStringList& args, CliOutputFormat format)
{
	if (rootPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires an installation root path.").arg(commandName), format);
	}

	GameInstallationProfile profile;
	profile.rootPath = rootPath;
	profile.gameKey = optionValue(args, QStringLiteral("--install-game"));
	profile.displayName = optionValue(args, QStringLiteral("--install-name"));
	profile.engineFamily = hasOption(args, QStringLiteral("--install-engine"))
		? gameEngineFamilyFromId(optionValue(args, QStringLiteral("--install-engine")))
		: GameEngineFamily::Unknown;
	profile.executablePath = optionValue(args, QStringLiteral("--install-executable"));
	profile.basePackagePaths = optionPathList(args, QStringLiteral("--install-base-packages"));
	profile.modPackagePaths = optionPathList(args, QStringLiteral("--install-mod-packages"));
	profile.paletteId = optionValue(args, QStringLiteral("--install-palette"));
	profile.compilerProfileId = optionValue(args, QStringLiteral("--install-compiler-profile"));
	if (hasOption(args, QStringLiteral("--install-hidden"))) {
		bool hidden = false;
		if (!boolOptionValue(optionValue(args, QStringLiteral("--install-hidden")), &hidden)) {
			return printCliError(commandName, CliExitCode::Usage, QStringLiteral("--install-hidden requires on or off."), format);
		}
		profile.hidden = hidden;
	}
	if (hasOption(args, QStringLiteral("--install-read-only"))) {
		bool readOnly = true;
		if (!boolOptionValue(optionValue(args, QStringLiteral("--install-read-only")), &readOnly)) {
			return printCliError(commandName, CliExitCode::Usage, QStringLiteral("--install-read-only requires on or off."), format);
		}
		profile.readOnly = readOnly;
	}
	profile = normalizedGameInstallationProfile(profile);

	if (hasOption(args, QStringLiteral("--dry-run"))) {
		const GameInstallationValidation validation = validateGameInstallationProfile(profile);
		if (format == CliOutputFormat::Json) {
			QJsonObject object = cliResultJson(commandName);
			object.insert(QStringLiteral("dryRun"), true);
			object.insert(QStringLiteral("saved"), false);
			object.insert(QStringLiteral("installation"), gameInstallationProfileJsonForCli(profile, QString()));
			object.insert(QStringLiteral("validation"), gameInstallationValidationJsonForCli(validation));
			printJson(object);
		} else {
			std::cout << "Dry run: would save game installation " << text(profile.id) << "\n";
			printGameInstallationProfile(profile, QString());
			printInstallationValidation(profile);
		}
		return exitCodeValue(CliExitCode::Success);
	}

	StudioSettings settings;
	settings.upsertGameInstallation(profile);
	settings.sync();
	if (settings.status() != QSettings::NoError) {
		return printCliError(commandName, CliExitCode::Failure,
			QStringLiteral("Failed to save installation profile: %1").arg(settingsStatusText(settings.status())), format);
	}

	const GameInstallationValidation validation = validateGameInstallationProfile(profile);
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("saved"), true);
		object.insert(QStringLiteral("installation"), gameInstallationProfileJsonForCli(profile, settings.selectedGameInstallationId()));
		object.insert(QStringLiteral("validation"), gameInstallationValidationJsonForCli(validation));
		printJson(object);
	} else {
		std::cout << "Saved game installation: " << text(profile.id) << "\n";
		printGameInstallationProfile(profile, settings.selectedGameInstallationId());
		printInstallationValidation(profile);
	}
	return exitCodeValue(CliExitCode::Success);
}

int runInstallSelectCommand(const QString& commandName, const QString& installationId, CliOutputFormat format)
{
	if (installationId.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires an installation id.").arg(commandName), format);
	}
	StudioSettings settings;
	bool found = false;
	GameInstallationProfile selected;
	for (const GameInstallationProfile& profile : settings.gameInstallations()) {
		if (sameGameInstallationId(profile.id, installationId)) {
			selected = profile;
			found = true;
			break;
		}
	}
	if (!found) {
		return printCliError(commandName, CliExitCode::NotFound,
			QStringLiteral("Installation profile not found: %1").arg(installationId), format);
	}
	settings.setSelectedGameInstallation(selected.id);
	settings.sync();
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("installation"), gameInstallationProfileJsonForCli(selected, settings.selectedGameInstallationId()));
		printJson(object);
	} else {
		std::cout << "Selected game installation: " << text(selected.id) << "\n";
		printGameInstallationProfile(selected, settings.selectedGameInstallationId());
	}
	return exitCodeValue(CliExitCode::Success);
}

int runInstallValidateCommand(const QString& commandName, const QString& installationId, CliOutputFormat format)
{
	StudioSettings settings;
	const QVector<GameInstallationProfile> profiles = settings.gameInstallations();
	const QString requested = installationId.trimmed().isEmpty() ? settings.selectedGameInstallationId() : installationId;
	if (requested.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage,
			QStringLiteral("%1 requires an installation id, or a selected installation profile.").arg(commandName), format);
	}
	for (const GameInstallationProfile& profile : profiles) {
		if (!sameGameInstallationId(profile.id, requested)) {
			continue;
		}
		const GameInstallationValidation validation = validateGameInstallationProfile(profile);
		const CliExitCode code = validation.isUsable() ? CliExitCode::Success : CliExitCode::ValidationFailed;
		if (format == CliOutputFormat::Json) {
			QJsonObject object = cliResultJson(commandName, code);
			object.insert(QStringLiteral("installation"), gameInstallationProfileJsonForCli(profile, settings.selectedGameInstallationId()));
			object.insert(QStringLiteral("validation"), gameInstallationValidationJsonForCli(validation));
			printJson(object);
		} else {
			printGameInstallationProfile(profile, settings.selectedGameInstallationId());
			printInstallationValidation(profile);
		}
		return exitCodeValue(code);
	}
	return printCliError(commandName, CliExitCode::NotFound,
		QStringLiteral("Installation profile not found: %1").arg(requested), format);
}

int runInstallRemoveCommand(const QString& commandName, const QString& installationId, const QStringList& args, CliOutputFormat format)
{
	if (installationId.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires an installation id.").arg(commandName), format);
	}
	StudioSettings settings;
	bool found = false;
	GameInstallationProfile target;
	for (const GameInstallationProfile& profile : settings.gameInstallations()) {
		if (sameGameInstallationId(profile.id, installationId)) {
			target = profile;
			found = true;
			break;
		}
	}
	if (!found) {
		return printCliError(commandName, CliExitCode::NotFound,
			QStringLiteral("Installation profile not found: %1").arg(installationId), format);
	}

	const bool dryRun = hasOption(args, QStringLiteral("--dry-run"));
	if (!dryRun) {
		// Removing a profile never touches the game files it points at.
		settings.removeGameInstallation(target.id);
		settings.sync();
	}
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("dryRun"), dryRun);
		object.insert(QStringLiteral("removed"), !dryRun);
		object.insert(QStringLiteral("installationId"), target.id);
		object.insert(QStringLiteral("rootPath"), target.rootPath);
		printJson(object);
	} else {
		std::cout << text(dryRun
			? QStringLiteral("Dry run: would remove installation profile %1. Game files are never touched.\n").arg(target.id)
			: QStringLiteral("Removed installation profile %1. Game files were not touched.\n").arg(target.id));
	}
	return exitCodeValue(CliExitCode::Success);
}


// ---------------------------------------------------------------------------
// map find
// ---------------------------------------------------------------------------

int runMapFindCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format)
{
	const LevelMapLoadRequest request = levelMapLoadRequestFromArgs(path, args);
	if (request.path.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage,
			QStringLiteral("%1 requires a Doom WAD or Quake-family .map path.").arg(commandName), format);
	}
	const QString where = optionValue(args, QStringLiteral("--where")).trimmed();
	if (where.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage,
			QStringLiteral("%1 requires --where <query>, such as class=light, tag=3, or \"texture:brick light>200\".").arg(commandName), format);
	}
	LevelMapDocument document;
	QString error;
	if (!loadLevelMap(request, &document, &error)) {
		return printCliError(commandName, levelMapLoadFailureCode(request), QStringLiteral("Unable to load map: %1").arg(error), format);
	}
	const QStringList selectors = levelMapObjectsMatchingQuery(document, where);
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("query"), where);
		object.insert(QStringLiteral("objects"), QJsonArray::fromStringList(selectors));
		printJson(object);
	} else {
		std::cout << "Objects matching " << text(where) << ": " << selectors.size() << "\n";
		for (const QString& selector : selectors) {
			std::cout << "- " << text(selector) << "\n";
		}
	}
	return exitCodeValue(CliExitCode::Success);
}

// ---------------------------------------------------------------------------
// Level dependencies and selective exports share the non-UI services.
int runMapDependenciesCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format)
{
	const QString packagePath = optionValue(args, QStringLiteral("--package"));
	if (path.trimmed().isEmpty() || packagePath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 requires a map path and --package <archive, folder or .vibepackage>.").arg(commandName), format);
	}
	LevelMapDocument document;
	PackageArchive archive;
	QString error;
	if (!loadLevelMap(levelMapLoadRequestFromArgs(path, args), &document, &error) ||
	    !loadPackageForCliQuiet(packagePath, &archive, &error)) {
		return printCliError(commandName, CliExitCode::Failure, error, format);
	}
	LevelDependencyReport report = inspectLevelDependencies(document, archive);
	// A planned archive may have no base path, or retain its original archive's
	// path. The CLI report must identify the package or draft the user reviewed.
	report.packagePath = QFileInfo(packagePath).absoluteFilePath();
	const CliExitCode code = report.complete && report.problemCount == 0 ? CliExitCode::Success : CliExitCode::ValidationFailed;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("dependencies"), levelDependencyReportJson(report));
		printJson(object);
	} else {
		std::cout << text(levelDependencyReportText(report)) << "\n";
	}
	return exitCodeValue(code);
}

int runPackageSubsetCommand(const QStringList& tokens, const QStringList& args, CliOutputFormat format)
{
	const QString commandName = QStringLiteral("package subset");
	const auto usage = [&](const QString& error) { return printCliError(commandName, CliExitCode::Usage, error, format); };
	const QSet<QString> flags{QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--quiet"), QStringLiteral("--verbose"),
		QStringLiteral("--dry-run"), QStringLiteral("--overwrite"), QStringLiteral("--write-manifest")};
	const QSet<QString> repeatable{QStringLiteral("--entry"), QStringLiteral("--asset-entry"), QStringLiteral("--entry-index"), QStringLiteral("--prefix")};
	const QSet<QString> values{QStringLiteral("--settings-file"), QStringLiteral("--locale"), QStringLiteral("--catalog-root"),
		QStringLiteral("--entries"), QStringLiteral("--where"), QStringLiteral("--map-input"), QStringLiteral("--output"),
		QStringLiteral("--format"), QStringLiteral("--compression"), QStringLiteral("--manifest"), QStringLiteral("--engine"),
		QStringLiteral("--engine-hint"), QStringLiteral("--map-name"), QStringLiteral("--map")};
	QSet<QString> seen;
	for (qsizetype at = 1; at < args.size(); ++at) {
		const QString argument = args.at(at); if (!argument.startsWith('-')) { continue; }
		const auto equals = argument.indexOf('='); const QString name = equals < 0 ? argument : argument.left(equals);
		if (flags.contains(name)) {
			if (equals >= 0 || seen.contains(name)) { return usage(QStringLiteral("Invalid or repeated flag: %1").arg(name)); }
		} else if (values.contains(name) || repeatable.contains(name)) {
			QString value;
			if (equals >= 0) { value = argument.mid(equals + 1); }
			else {
				if (at + 1 >= args.size() || args.at(at + 1).startsWith('-')) { return usage(QStringLiteral("Missing value for %1; use %1=value for a value beginning with a dash.").arg(name)); }
				value = args.at(++at);
			}
			if (value.trimmed().isEmpty() || (!repeatable.contains(name) && seen.contains(name))) { return usage(QStringLiteral("Empty or repeated option: %1").arg(name)); }
		} else { return usage(QStringLiteral("Unknown subset option: %1. Stage edits in a .vibepackage draft before exporting a subset.").arg(name)); }
		seen.insert(name);
	}
	if (tokens.size() != (seen.contains(QStringLiteral("--output")) ? 3 : 4)) {
		return usage(QStringLiteral("package subset requires one source and one separate output path. Supply the output positionally or with --output."));
	}
	const QString packagePath = tokens.at(2), outputPath = seen.contains(QStringLiteral("--output")) ? optionValue(args, QStringLiteral("--output")) : tokens.at(3);
	if (packagePath.trimmed().isEmpty() || outputPath.trimmed().isEmpty()) { return usage(QStringLiteral("Source and output paths cannot be empty.")); }
	if (seen.contains(QStringLiteral("--engine")) && seen.contains(QStringLiteral("--engine-hint"))) { return usage(QStringLiteral("Choose --engine or --engine-hint, not both.")); }
	if (seen.contains(QStringLiteral("--map")) && seen.contains(QStringLiteral("--map-name"))) { return usage(QStringLiteral("Choose --map-name or --map, not both.")); }
	if (seen.contains(QStringLiteral("--manifest")) && seen.contains(QStringLiteral("--write-manifest"))) { return usage(QStringLiteral("Choose --manifest <path> or --write-manifest, not both.")); }
	PackageSelectionRequest selection; selection.entries = assetEntryArgs(args);
	selection.prefixes = optionValues(args, QStringLiteral("--prefix")); selection.query = optionValue(args, QStringLiteral("--where"));
	for (const auto& value : optionValues(args, QStringLiteral("--entry-index"))) {
		bool valid = false; const auto index = value.toLongLong(&valid);
		if (!valid || index != static_cast<qlonglong>(static_cast<qsizetype>(index)) || !QRegularExpression(QStringLiteral("^[0-9]+$")).match(value).hasMatch()) { return usage(QStringLiteral("--entry-index requires a nonnegative integer from this package snapshot's list.")); }
		selection.entryIndexes << static_cast<qsizetype>(index);
	}
	const QString mapPath = optionValue(args, QStringLiteral("--map-input"));
	const bool selectors = !selection.entries.isEmpty() || !selection.prefixes.isEmpty() || !selection.query.isEmpty() || !selection.entryIndexes.isEmpty();
	if ((!mapPath.isEmpty() && selectors) || (mapPath.isEmpty() && !selectors)) {
		return usage(QStringLiteral("Select entry indexes, paths, prefixes and an optional query, or use --map-input exclusively."));
	}
	if (mapPath.isEmpty() && (seen.contains(QStringLiteral("--engine")) || seen.contains(QStringLiteral("--engine-hint"))
		|| seen.contains(QStringLiteral("--map")) || seen.contains(QStringLiteral("--map-name")))) { return usage(QStringLiteral("Map options require --map-input.")); }
	const auto outputFormat = packageWriteFormatFromArgs(args, outputPath);
	if (seen.contains(QStringLiteral("--format")) && outputFormat != PackageArchiveFormat::Pak && outputFormat != PackageArchiveFormat::Zip
		&& outputFormat != PackageArchiveFormat::Pk3 && outputFormat != PackageArchiveFormat::Wad) {
		return usage(QStringLiteral("Unknown output format. Use pak, zip, pk3, or wad."));
	}
	DeflateLevel level = DeflateLevel::Default;
	if (seen.contains(QStringLiteral("--compression")) && !deflateLevelFromId(optionValue(args, QStringLiteral("--compression")), &level)) {
		return usage(QStringLiteral("Unknown compression level. Use store, fast, default, or best."));
	}
	QString error; std::shared_ptr<const PackageArchiveReader> archive;
	if (packagePath.endsWith(QStringLiteral(".vibepackage"), Qt::CaseInsensitive)) {
		PackageStagingModel draft;
		if (!PackageDraft::load(packagePath, &draft, &error)) { return printCliError(commandName, CliExitCode::Failure, error, format); }
		archive = std::make_shared<PackageArchive>(packagePlannedArchive(draft, &error));
		if (!archive->isOpen()) { return printCliError(commandName, CliExitCode::Failure, error, format); }
	} else {
		auto source = std::make_shared<PackageArchive>();
		if (!loadPackageForCliQuiet(packagePath, source.get(), &error)) { return printCliError(commandName, CliExitCode::Failure, error, format); }
		archive = std::move(source);
	}
	if (!mapPath.isEmpty()) {
		LevelMapDocument document;
		if (!loadLevelMap(levelMapLoadRequestFromArgs(mapPath, args), &document, &error)) { return printCliError(commandName, CliExitCode::Failure, error, format); }
		const LevelDependencyReport report = inspectLevelDependencies(document, *archive);
		if (!report.canExport()) {
			if (format == CliOutputFormat::Json) {
				QJsonObject object = cliResultJson(commandName, CliExitCode::ValidationFailed);
				object.insert(QStringLiteral("dependencies"), levelDependencyReportJson(report));
				object.insert(QStringLiteral("error"), QStringLiteral("No exportable asset subset. Resolve dependency problems or incomplete scans first.")); printJson(object);
			} else { std::cout << text(levelDependencyReportText(report)) << "\nNo exportable asset subset.\n"; }
			return exitCodeValue(CliExitCode::ValidationFailed);
		}
		selection.entries = report.resolvedPaths;
	}
	const auto selected = selectPackageFiles(*archive, selection);
	if (!selected.succeeded()) { return usage(selected.errors.join(QLatin1Char('\n'))); }
	PackageStagingModel subset; PackageSubsetReview review;
	if (!subset.loadBaseArchiveSubsetAt(*archive, selected.entryIndexes, &error, &review)) { return printCliError(commandName, CliExitCode::ValidationFailed, error, format); }
	return runPackageSaveAsCommand(commandName, packagePath, outputPath, args, format, &subset, &review);
}

// map textures
// ---------------------------------------------------------------------------

int runMapMaterialsCommand(const QString& path, const QStringList& args, CliOutputFormat format)
{
	const QString commandName = QStringLiteral("map materials");
	const QString packagePath = optionValue(args, QStringLiteral("--package"));
	if (path.trimmed().isEmpty() || packagePath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("map materials requires a map path and --package <archive-or-folder>."), format);
	}
	LevelMapDocument document; PackageArchive archive; QString error;
	const auto request = levelMapLoadRequestFromArgs(path, args);
	if (!loadLevelMap(request, &document, &error)) { return printCliError(commandName, levelMapLoadFailureCode(request), error, format); }
	if (!loadPackageForCliQuiet(packagePath, &archive, &error)) { return printCliError(commandName, CliExitCode::NotFound, error, format); }
	LevelPreviewAssetOptions options;
	options.paletteId = optionValue(args, QStringLiteral("--palette"));
	const auto report = resolveLevelPreviewAssets(document, archive, options);
	const bool inspectGeometry = hasOption(args, QStringLiteral("--geometry"));
	LevelMapPreviewMesh preview;
	if (inspectGeometry) {
		LevelMapPreviewMeshOptions meshOptions; meshOptions.modelMeshes = report.models; meshOptions.textureSizes = levelPreviewTextureSizes(report);
		preview = buildLevelMapPreviewMesh(document, meshOptions);
	}
	const auto code = report.complete && report.problemCount() == 0 && !preview.truncated && !preview.cancelled && preview.warnings.isEmpty()
		? CliExitCode::Success : CliExitCode::ValidationFailed;
	if (format == CliOutputFormat::Json) {
		auto object = cliResultJson(commandName, code); object.insert(QStringLiteral("materials"), levelPreviewAssetsJson(report));
		if (inspectGeometry) {
			object.insert(QStringLiteral("geometry"), QJsonObject {{QStringLiteral("triangles"), preview.triangles}, {QStringLiteral("walls"), preview.walls},
				{QStringLiteral("floors"), preview.floors}, {QStringLiteral("ceilings"), preview.ceilings}, {QStringLiteral("brushFaces"), preview.brushFaces},
				{QStringLiteral("patches"), preview.patches}, {QStringLiteral("modelInstances"), preview.modelInstances},
				{QStringLiteral("truncated"), preview.truncated}, {QStringLiteral("cancelled"), preview.cancelled}, {QStringLiteral("warnings"), QJsonArray::fromStringList(preview.warnings)}});
		}
		printJson(object);
	} else {
		std::cout << text(levelPreviewAssetsText(report)) << "\n";
		if (inspectGeometry) {
			std::cout << text(QCoreApplication::translate("VibeStudioCLI", "Preview: %1 triangles, %2 walls, %3 floors, %4 ceilings.").arg(preview.triangles).arg(preview.walls).arg(preview.floors).arg(preview.ceilings)) << "\n";
			for (const auto& warning : preview.warnings) { std::cout << text(warning) << "\n"; }
		}
	}
	return exitCodeValue(code);
}

int runMapTexturesCommand(const QString& commandName, const QString& path, const QStringList& args, CliOutputFormat format)
{
	const LevelMapLoadRequest request = levelMapLoadRequestFromArgs(path, args);
	if (request.path.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage,
			QStringLiteral("%1 requires a Doom WAD or Quake-family .map path.").arg(commandName), format);
	}
	LevelMapDocument document;
	QString error;
	if (!loadLevelMap(request, &document, &error)) {
		return printCliError(commandName, levelMapLoadFailureCode(request), QStringLiteral("Unable to load map: %1").arg(error), format);
	}

	// --uses NAME: the objects that show one texture, as the Levels Textures
	// tab's Select Objects Using It finds them, by their selectors.
	if (hasOption(args, QStringLiteral("--uses"))) {
		const QString texture = optionValue(args, QStringLiteral("--uses")).trimmed();
		if (texture.isEmpty()) {
			return printCliError(commandName, CliExitCode::Usage, QStringLiteral("%1 --uses requires a texture name.").arg(commandName), format);
		}
		QStringList selectors;
		for (const LevelMapSelectionRef& ref : levelMapObjectsUsingTexture(document, texture)) {
			selectors << levelMapSelectionRefId(ref);
		}
		if (format == CliOutputFormat::Json) {
			QJsonObject object = cliResultJson(commandName);
			object.insert(QStringLiteral("texture"), texture);
			object.insert(QStringLiteral("objects"), QJsonArray::fromStringList(selectors));
			printJson(object);
		} else {
			std::cout << "Objects using " << text(texture) << ": " << selectors.size() << "\n";
			for (const QString& selector : std::as_const(selectors)) {
				std::cout << "- " << text(selector) << "\n";
			}
		}
		return exitCodeValue(CliExitCode::Success);
	}

	const bool decodeSizes = !hasOption(args, QStringLiteral("--no-decode"));
	MapTextureAudit audit;
	const QString packagePath = optionValue(args, QStringLiteral("--package"));
	if (!packagePath.trimmed().isEmpty()) {
		PackageArchive archive;
		QString packageError;
		if (!loadPackageForCliQuiet(packagePath, &archive, &packageError)) {
			return printCliError(commandName, CliExitCode::NotFound,
				QStringLiteral("Unable to open package: %1").arg(packageError), format);
		}
		audit = auditLevelMapTextures(document, archive, decodeSizes);
	} else {
		QStringList roots = optionValues(args, QStringLiteral("--root"));
		const QString searchPaths = optionValue(args, QStringLiteral("--search-paths"));
		if (!searchPaths.trimmed().isEmpty()) {
			roots += searchPaths.split(QLatin1Char(';'), Qt::SkipEmptyParts);
		}
		if (roots.isEmpty()) {
			// Default to the project's package folders when the map sits inside one.
			const QString projectRoot = optionValue(args, QStringLiteral("--project-root"));
			ProjectManifest manifest;
			if (!projectRoot.trimmed().isEmpty() && loadProjectManifest(projectRoot, &manifest)) {
				roots = manifest.packageFolders;
			}
		}
		if (roots.isEmpty()) {
			return printCliError(commandName, CliExitCode::Usage,
				QStringLiteral("%1 requires --package <archive>, one or more --root <folder>, or --project-root <path> with package folders.").arg(commandName), format);
		}
		audit = auditLevelMapTexturesInDirectories(document, roots, decodeSizes);
	}

	const CliExitCode code = !audit.sourceIndexComplete || !audit.complete || audit.cancelled || audit.missingCount > 0 ? CliExitCode::ValidationFailed : CliExitCode::Success;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("textures"), mapTextureAuditJson(audit));
		printJson(object);
	} else {
		std::cout << text(mapTextureAuditText(audit)) << "\n";
	}
	return exitCodeValue(code);
}

// ---------------------------------------------------------------------------
// entity definitions
// ---------------------------------------------------------------------------

QStringList entityDefinitionPathsFromArgs(const QStringList& args, const QStringList& tokens, int firstPositional)
{
	QStringList paths = optionValues(args, QStringLiteral("--definitions"));
	paths += optionValues(args, QStringLiteral("--definition"));
	paths += optionValues(args, QStringLiteral("--path"));
	paths += optionValues(args, QStringLiteral("--paths"));
	const QString list = optionValue(args, QStringLiteral("--definition-paths"));
	if (!list.trimmed().isEmpty()) {
		paths += list.split(QLatin1Char(';'), Qt::SkipEmptyParts);
	}
	for (int index = firstPositional; index < tokens.size(); ++index) {
		paths << tokens.at(index);
	}

	// With nothing named explicitly, fall back to the conventional per-project
	// locations so a configured project just works.
	if (paths.isEmpty()) {
		const QString projectRoot = optionValue(args, QStringLiteral("--project-root"));
		if (!projectRoot.trimmed().isEmpty()) {
			paths = entityDefinitionSearchPaths(projectRoot);
		}
	}
	return paths;
}

int runEntityDefinitionsCommand(const QString& commandName, const QStringList& tokens, const QStringList& args, CliOutputFormat format)
{
	const QStringList paths = entityDefinitionPathsFromArgs(args, tokens, 2);
	if (paths.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage,
			QStringLiteral("%1 requires one or more definition files or folders, or --project-root <path>.").arg(commandName), format);
	}

	const EntityDefinitionCatalogue catalogue = loadEntityDefinitions(paths, !hasOption(args, QStringLiteral("--no-recursive")));
	if (!catalogue.error.isEmpty()) {
		return printCliError(commandName, CliExitCode::NotFound, catalogue.error, format);
	}

	const QString filter = optionValue(args, QStringLiteral("--class"));
	if (!filter.trimmed().isEmpty()) {
		EntityClassDefinition definition;
		if (!catalogue.classForName(filter, &definition)) {
			return printCliError(commandName, CliExitCode::NotFound,
				QStringLiteral("Entity class not defined by the loaded catalogue: %1").arg(filter), format);
		}
		if (format == CliOutputFormat::Json) {
			QJsonObject object = cliResultJson(commandName);
			object.insert(QStringLiteral("catalogue"), entityDefinitionCatalogueJson(catalogue));
			object.insert(QStringLiteral("className"), definition.className);
			printJson(object);
		} else {
			for (const QString& line : entityClassSummaryLines(definition)) {
				std::cout << text(line) << "\n";
			}
		}
		return exitCodeValue(CliExitCode::Success);
	}

	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("catalogue"), entityDefinitionCatalogueJson(catalogue));
		printJson(object);
		return exitCodeValue(CliExitCode::Success);
	}

	std::cout << "Entity definitions\n";
	for (const QString& source : catalogue.sourcePaths) {
		std::cout << "- source: " << text(QDir::toNativeSeparators(source)) << "\n";
	}
	std::cout << text(QStringLiteral("Classes: %1 (%2 point, %3 brush, %4 base)\n")
		.arg(catalogue.classes.size())
		.arg(catalogue.pointClassCount)
		.arg(catalogue.brushClassCount)
		.arg(catalogue.baseClassCount));
	for (const EntityClassDefinition& definition : catalogue.classes) {
		if (definition.kind == EntityClassKind::Base) {
			continue;
		}
		std::cout << text(QStringLiteral("  %1 [%2] %3 key(s), %4 flag(s)\n")
			.arg(definition.className, entityClassKindId(definition.kind))
			.arg(definition.keys.size())
			.arg(definition.spawnflags.size()));
	}
	for (const QString& warning : catalogue.warnings) {
		std::cout << "! " << text(warning) << "\n";
	}
	return exitCodeValue(CliExitCode::Success);
}

int runEntityValidateCommand(const QString& commandName, const QString& mapPath, const QStringList& tokens, const QStringList& args, CliOutputFormat format)
{
	const LevelMapLoadRequest request = levelMapLoadRequestFromArgs(mapPath, args);
	if (request.path.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage,
			QStringLiteral("%1 requires a Doom WAD or Quake-family .map path.").arg(commandName), format);
	}
	LevelMapDocument document;
	QString error;
	if (!loadLevelMap(request, &document, &error)) {
		return printCliError(commandName, levelMapLoadFailureCode(request), QStringLiteral("Unable to load map: %1").arg(error), format);
	}

	const QStringList paths = entityDefinitionPathsFromArgs(args, tokens, 3);
	if (paths.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage,
			QStringLiteral("%1 requires --definitions <path> one or more times, or --project-root <path>.").arg(commandName), format);
	}
	const EntityDefinitionCatalogue catalogue = loadEntityDefinitions(paths, !hasOption(args, QStringLiteral("--no-recursive")));
	if (catalogue.isEmpty()) {
		return printCliError(commandName, CliExitCode::NotFound,
			catalogue.error.isEmpty() ? QStringLiteral("No entity definitions were loaded from the given paths.") : catalogue.error, format);
	}

	const EntityValidationReport report = validateLevelMapEntities(document, catalogue);
	// An unknown classname is a warning, not an error: a map may legitimately
	// use an entity a mod adds without shipping a definition for it. --strict
	// is for the build script that wants to refuse that anyway.
	const bool strict = hasOption(args, QStringLiteral("--strict"));
	const CliExitCode code = (report.errorCount > 0 || (strict && report.warningCount > 0))
		? CliExitCode::ValidationFailed
		: CliExitCode::Success;
	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("entities"), entityValidationReportJson(report));
		printJson(object);
	} else {
		std::cout << text(entityValidationText(report)) << "\n";
	}
	return exitCodeValue(code);
}

// ---------------------------------------------------------------------------
// model geometry
// ---------------------------------------------------------------------------

bool loadModelMeshForCli(const QString& commandName, const QString& packagePath, const QString& entryPath, const QStringList& args,
	CliOutputFormat format, ModelMesh* mesh, QString* resolvedSource, int* exitCode, PackageArchive* sourceArchive = nullptr)
{
	const QString paletteId = optionValue(args, QStringLiteral("--palette"));
	if (packagePath.trimmed().isEmpty()) {
		*exitCode = printCliError(commandName, CliExitCode::Usage,
			QStringLiteral("%1 requires a package path, or a model file path with --file.").arg(commandName), format);
		return false;
	}

	// A bare file path is as useful as a package entry for a modeller, so both
	// are accepted: a package plus an entry, or a single file on disk.
	if (entryPath.trimmed().isEmpty()) {
		if (!QFileInfo(packagePath).isFile()) {
			*exitCode = printCliError(commandName, CliExitCode::NotFound,
				QStringLiteral("Unable to read model file: %1").arg(QDir::toNativeSeparators(packagePath)), format);
			return false;
		}
		QByteArray bytes; QString error;
		if (!readModelFile(packagePath, &bytes, &error)) {
			*exitCode = printCliError(commandName, CliExitCode::Failure, error, format); return false;
		}
		const IdTechPalette palette = generatedIdTechPalette(paletteId);
		*mesh = decodeModelMesh(packagePath, bytes, &palette);
		*resolvedSource = QDir::toNativeSeparators(packagePath);
		return true;
	}

	PackageStagingModel staging;
	QString archiveError;
	if (!loadPackageStaging(packagePath, &staging, &archiveError)) {
		*exitCode = printCliError(commandName, CliExitCode::NotFound,
			QStringLiteral("Unable to open package: %1").arg(archiveError), format);
		return false;
	}
	const PackageArchive archive = packagePlannedArchive(staging, &archiveError);
	if (!archive.isOpen()) { *exitCode = printCliError(commandName, CliExitCode::Failure, archiveError, format); return false; }
	*mesh = decodeModelMeshFromArchive(archive, entryPath, paletteId);
	*resolvedSource = QStringLiteral("%1!%2").arg(QDir::toNativeSeparators(packagePath), entryPath);
	if (sourceArchive) { *sourceArchive = archive; }
	return true;
}

QJsonObject modelMeshJsonForCli(const ModelMesh& mesh)
{
	QJsonObject object;
	object.insert(QStringLiteral("format"), mesh.formatId);
	object.insert(QStringLiteral("formatName"), mesh.formatName);
	object.insert(QStringLiteral("version"), mesh.version);
	object.insert(QStringLiteral("geometryAvailable"), mesh.geometryAvailable);
	object.insert(QStringLiteral("md2SkinSize"), QJsonArray{mesh.md2SkinSize.width(), mesh.md2SkinSize.height()});
	object.insert(QStringLiteral("frames"), mesh.frameCount);
	object.insert(QStringLiteral("surfaces"), mesh.surfaceCount);
	object.insert(QStringLiteral("vertices"), mesh.vertexCount);
	object.insert(QStringLiteral("triangles"), mesh.triangleCount);
	object.insert(QStringLiteral("tags"), mesh.tagCount);
	object.insert(QStringLiteral("skins"), mesh.skinCount);
	object.insert(QStringLiteral("embeddedSkins"), static_cast<int>(mesh.embeddedSkins.size()));
	object.insert(QStringLiteral("boundingRadius"), static_cast<double>(mesh.boundingRadius()));
	object.insert(QStringLiteral("skinPaths"), QJsonArray::fromStringList(mesh.skinPaths));

	QJsonArray animations;
	for (const ModelAnimation& animation : mesh.animations) {
		QJsonObject entry;
		entry.insert(QStringLiteral("name"), animation.name);
		entry.insert(QStringLiteral("firstFrame"), animation.firstFrame);
		entry.insert(QStringLiteral("frameCount"), animation.frameCount);
		entry.insert(QStringLiteral("framesPerSecond"), animation.framesPerSecond);
		animations.append(entry);
	}
	object.insert(QStringLiteral("animations"), animations);

	QJsonArray surfaces;
	for (const ModelSurface& surface : mesh.surfaces) {
		QJsonObject entry;
		entry.insert(QStringLiteral("index"), surface.index);
		entry.insert(QStringLiteral("name"), surface.name);
		entry.insert(QStringLiteral("vertices"), surface.vertexCount);
		entry.insert(QStringLiteral("triangles"), static_cast<int>(surface.triangles.size()));
		entry.insert(QStringLiteral("uvSeams"), static_cast<int>(surface.uvSeams.size()));
		entry.insert(QStringLiteral("skinPaths"), QJsonArray::fromStringList(surface.skinPaths));
		surfaces.append(entry);
	}
	object.insert(QStringLiteral("surfaceList"), surfaces);
	object.insert(QStringLiteral("warnings"), QJsonArray::fromStringList(mesh.warnings));
	if (!mesh.error.isEmpty()) {
		object.insert(QStringLiteral("error"), mesh.error);
	}
	return object;
}

int runModelInspectCommand(const QString& commandName, const QString& packagePath, const QString& entryPath, const QStringList& args, CliOutputFormat format)
{
	ModelMesh mesh;
	QString source;
	int exitCode = exitCodeValue(CliExitCode::Failure);
	if (!loadModelMeshForCli(commandName, packagePath, entryPath, args, format, &mesh, &source, &exitCode)) {
		return exitCode;
	}
	if (!mesh.isValid()) {
		return printCliError(commandName, CliExitCode::Unavailable,
			mesh.error.isEmpty() ? QStringLiteral("Entry is not a recognized idTech model.") : mesh.error, format);
	}

	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName);
		object.insert(QStringLiteral("source"), source);
		object.insert(QStringLiteral("model"), modelMeshJsonForCli(mesh));
		printJson(object);
	} else {
		std::cout << text(modelMeshSummaryText(mesh)) << "\n";
	}
	return exitCodeValue(CliExitCode::Success);
}

int runModelRecoveriesCommand(const QStringList& args, CliOutputFormat format)
{
	const auto directory = hasOption(args, QStringLiteral("--directory")) ? optionValue(args, QStringLiteral("--directory")) : modelRecoveryDirectory();
	const QString command = QStringLiteral("model recoveries");
	if (directory.isEmpty()) { return printCliError(command, CliExitCode::Usage, QStringLiteral("--directory requires a folder path."), format); }
	const auto scan = listModelRecoveries(directory);
	if (!scan.error.isEmpty()) { return printCliError(command, CliExitCode::Failure, scan.error, format); }
	QJsonArray records;
	for (const auto& record : scan.records) {
		QJsonObject item{{QStringLiteral("path"), record.path}, {QStringLiteral("id"), record.id},
			{QStringLiteral("title"), record.title}, {QStringLiteral("sourcePath"), record.sourcePath},
			{QStringLiteral("sourceSha256"), QString::fromLatin1(record.sourceSha256.toHex())},
			{QStringLiteral("payloadSha256"), QString::fromLatin1(record.payloadSha256.toHex())},
			{QStringLiteral("payloadBytes"), record.payloadBytes}, {QStringLiteral("ownerProcessId"), record.ownerProcessId},
			{QStringLiteral("writtenUtc"), record.writtenUtc.toString(Qt::ISODateWithMs)},
			{QStringLiteral("headerValid"), record.isValid()}, {QStringLiteral("payloadVerified"), false}, {QStringLiteral("error"), record.error}};
		records.append(item);
	}
	if (format == CliOutputFormat::Json) {
		auto result = cliResultJson(command);
		result.insert(QStringLiteral("directory"), QFileInfo(directory).absoluteFilePath());
		result.insert(QStringLiteral("limited"), scan.limited);
		result.insert(QStringLiteral("records"), records);
		printJson(result);
	} else {
		for (const auto& record : scan.records) {
			std::cout << text(QStringLiteral("%1 · %2 · %3\n").arg(record.path, record.writtenUtc.toLocalTime().toString(Qt::ISODate),
				record.isValid() ? QStringLiteral("header valid; payload verified on restore") : record.error));
		}
		std::cout << text(QStringLiteral("%1 recovery copies%2\n").arg(records.size()).arg(scan.limited ? QStringLiteral(" (scan limit reached)") : QString()));
	}
	return exitCodeValue(CliExitCode::Success);
}

int runModelRecoverCommand(const QString& path, const QStringList& args, CliOutputFormat format)
{
	const QString command = QStringLiteral("model recover"), output = optionValue(args, QStringLiteral("--output"));
	if (path.isEmpty() || !output.endsWith(QStringLiteral(".mesh.json"), Qt::CaseInsensitive) || hasOption(args, QStringLiteral("--overwrite"))) {
		return printCliError(command, CliExitCode::Usage, QStringLiteral("model recover requires a recovery path and --output <new.mesh.json>. Existing files cannot be overwritten."), format);
	}
	ModelRecoverySnapshot snapshot;
	const auto record = inspectModelRecovery(path, &snapshot);
	if (!record.isValid()) { return printCliError(command, CliExitCode::ValidationFailed, record.error, format); }
	const QFileInfo target(output), source(record.sourcePath);
	if (target.exists() || target.isSymLink() || target.isJunction() || (!record.sourcePath.isEmpty() && target.absoluteFilePath().compare(source.absoluteFilePath(), Qt::CaseInsensitive) == 0)) {
		return printCliError(command, CliExitCode::Failure, QStringLiteral("Choose a new output path. The original source, recovery copy, and existing files are protected."), format);
	}
	QString error;
	ModelDocument document;
	if (!document.restoreDraft(snapshot.mesh, snapshot.selection, &error)) { return printCliError(command, CliExitCode::ValidationFailed, error, format); }
	const bool dryRun = hasOption(args, QStringLiteral("--dry-run"));
	if (!dryRun && !document.save(output, false, &error)) { return printCliError(command, CliExitCode::Failure, error, format); }
	if (format == CliOutputFormat::Json) {
		auto result = cliResultJson(command);
		result.insert(QStringLiteral("recoveryPath"), record.path);
		result.insert(QStringLiteral("sourcePath"), record.sourcePath);
		result.insert(QStringLiteral("outputPath"), target.absoluteFilePath());
		result.insert(QStringLiteral("payloadVerified"), true);
		result.insert(QStringLiteral("dryRun"), dryRun);
		result.insert(QStringLiteral("written"), !dryRun);
		result.insert(QStringLiteral("model"), modelMeshJsonForCli(document.mesh()));
		printJson(result);
	} else {
		std::cout << text(QStringLiteral("%1 %2 from verified recovery. Original source and recovery copy retained.\n")
			.arg(dryRun ? QStringLiteral("Would write") : QStringLiteral("Wrote"), target.absoluteFilePath()));
	}
	return exitCodeValue(CliExitCode::Success);
}

int runModelMaterialsCommand(const QString& path, const QStringList& args, CliOutputFormat format)
{
	Q_UNUSED(path);
	const auto result = runModelAppearance(args);
	const auto command = QStringLiteral("model materials");
	const auto code = static_cast<CliExitCode>(result.exitCode);
	if (!result.error.isEmpty()) { return printCliError(command, code, result.error, format); }
	if (format == CliOutputFormat::Json) {
		auto output = cliResultJson(command, code);
		for (auto it = result.data.constBegin(); it != result.data.constEnd(); ++it) { output.insert(it.key(), it.value()); }
		printJson(output);
	} else { std::cout << text(result.text) << "\n"; }
	return result.exitCode;
}

int runModelTagsCommand(const QString& path, const QStringList& args, CliOutputFormat format)
{
	const QString command = QStringLiteral("model tags");
	if (path.trimmed().isEmpty()) { return printCliError(command, CliExitCode::Usage, QStringLiteral("model tags requires a model source."), format); }
	QByteArray bytes; QString error; ModelMesh mesh;
	if (!readModelFile(path, &bytes, &error)) { return printCliError(command, CliExitCode::Failure, error, format); }
	if (!importEditableModel(path, bytes, &mesh, &error)) { return printCliError(command, CliExitCode::ValidationFailed, error, format); }
	int frame = -1; bool valid = true;
	if (hasOption(args, QStringLiteral("--frame")) && optionValue(args, QStringLiteral("--frame")) != QStringLiteral("all")) {
		frame = optionValue(args, QStringLiteral("--frame")).toInt(&valid);
		if (!valid || frame < 0 || frame >= mesh.frames.size()) { return printCliError(command, CliExitCode::Usage, QStringLiteral("--frame requires all or an existing zero-based frame index."), format); }
	}
	const auto name = optionValue(args, QStringLiteral("--tag"));
	if (hasOption(args, QStringLiteral("--tag")) && !findModelTag(mesh, name, 0)) {
		return printCliError(command, CliExitCode::Usage, QStringLiteral("--tag requires an existing attachment name."), format);
	}
	QJsonArray tags;
	for (const auto& tag : mesh.tags) {
		if ((frame >= 0 && frame != tag.frameIndex) || (!name.isEmpty() && tag.name != name)) { continue; }
		QJsonArray axes; QStringList axisText;
		for (float component : tag.axis) { axes.append(component); axisText << QString::number(component, 'g', 7); }
		tags.append(QJsonObject{{QStringLiteral("name"),tag.name},{QStringLiteral("frame"),tag.frameIndex},
			{QStringLiteral("origin"),QJsonArray{tag.origin.x,tag.origin.y,tag.origin.z}},{QStringLiteral("axis"),axes}});
		if (format != CliOutputFormat::Json) {
			std::cout << text(QStringLiteral("%1 frame %2: origin %3,%4,%5; local axes %6\n").arg(tag.name).arg(tag.frameIndex)
				.arg(tag.origin.x).arg(tag.origin.y).arg(tag.origin.z).arg(axisText.join(QLatin1Char(','))));
		}
	}
	if (format == CliOutputFormat::Json) {
		auto result = cliResultJson(command);
		result.insert(QStringLiteral("source"), QFileInfo(path).absoluteFilePath());
		result.insert(QStringLiteral("tagCount"), mesh.tagCount); result.insert(QStringLiteral("frames"), mesh.frameCount);
		result.insert(QStringLiteral("tags"), tags); printJson(result);
	} else if (tags.isEmpty()) { std::cout << "No attachment tags.\n"; }
	return exitCodeValue(CliExitCode::Success);
}

int runModelAnimationsCommand(const QString& path, const QStringList& args, CliOutputFormat format)
{
	const QString command = QStringLiteral("model animations");
	if (path.trimmed().isEmpty()) { return printCliError(command, CliExitCode::Usage, QStringLiteral("model animations requires a model source."), format); }
	QByteArray bytes; QString error; ModelMesh mesh;
	if (!readModelFile(path, &bytes, &error)) { return printCliError(command, CliExitCode::Failure, error, format); }
	if (!importEditableModel(path, bytes, &mesh, &error)) { return printCliError(command, CliExitCode::ValidationFailed, error, format); }
	bool valid = true;
	const int selected = hasOption(args, QStringLiteral("--clip")) ? optionValue(args, QStringLiteral("--clip")).toInt(&valid) : -1;
	if (!valid || (hasOption(args, QStringLiteral("--clip")) && (selected < 0 || selected >= mesh.animations.size()))) {
		return printCliError(command, CliExitCode::Usage, QStringLiteral("--clip requires an existing zero-based animation index."), format);
	}
	QJsonArray clips;
	for (int i = 0; i < mesh.animations.size(); ++i) {
		if (selected >= 0 && selected != i) { continue; }
		const auto& clip = mesh.animations[i];
		clips.append(QJsonObject{{QStringLiteral("index"),i},{QStringLiteral("name"),clip.name},{QStringLiteral("firstFrame"),clip.firstFrame},
			{QStringLiteral("lastFrame"),clip.firstFrame + clip.frameCount - 1},{QStringLiteral("frameCount"),clip.frameCount},{QStringLiteral("framesPerSecond"),clip.framesPerSecond}});
		if (format != CliOutputFormat::Json) {
			std::cout << text(QStringLiteral("Clip %1: %2; frames %3–%4 (%5 poses)\n").arg(i).arg(clip.name).arg(clip.firstFrame).arg(clip.firstFrame + clip.frameCount - 1).arg(clip.frameCount));
			if (clip.framesPerSecond > 0) { std::cout << text(QCoreApplication::translate("ModelAnimationCli", "  Saved FPS: %1; configure native game timing separately.\n").arg(clip.framesPerSecond, 0, 'g', 12)); }
		}
	}
	if (format == CliOutputFormat::Json) {
		auto result = cliResultJson(command);
		result.insert(QStringLiteral("source"), QFileInfo(path).absoluteFilePath()); result.insert(QStringLiteral("frameCount"),mesh.frameCount);
		result.insert(QStringLiteral("clips"),clips); printJson(result);
	} else if (clips.isEmpty()) { std::cout << "No animation clips; all model frames remain available.\n"; }
	return exitCodeValue(CliExitCode::Success);
}

int runModelTopologyCommand(const QString& path, const QStringList& args, CliOutputFormat format, bool uv = false)
{
	const QString command = uv ? QStringLiteral("model uv") : QStringLiteral("model topology");
	if (path.trimmed().isEmpty()) { return printCliError(command, CliExitCode::Usage, QStringLiteral("%1 requires a model source.").arg(command), format); }
	QByteArray bytes; QString error; ModelMesh mesh;
	if (!readModelFile(path, &bytes, &error)) { return printCliError(command, CliExitCode::Failure, error, format); }
	if (!importEditableModel(path, bytes, &mesh, &error)) { return printCliError(command, CliExitCode::ValidationFailed, error, format); }
	bool valid = true;
	const int surfaceIndex = hasOption(args, QStringLiteral("--surface")) ? optionValue(args, QStringLiteral("--surface")).toInt(&valid) : 0;
	if (!valid || surfaceIndex < 0 || surfaceIndex >= mesh.surfaces.size()) {
		return printCliError(command, CliExitCode::Usage, QStringLiteral("--surface requires an existing zero-based surface index."), format);
	}
	const auto& surface = mesh.surfaces[surfaceIndex];
	if (uv) {
		ModelUvTopology topology;
		if (!buildModelUvTopology(surface, &topology, &error)) { return printCliError(command, CliExitCode::ValidationFailed, error, format); }
		if (format == CliOutputFormat::Json) {
			auto result = cliResultJson(command);
			result.insert(QStringLiteral("source"), QFileInfo(path).absoluteFilePath()); result.insert(QStringLiteral("surface"), surfaceIndex);
			QJsonArray islands, seams;
			for (int index = 0; index < topology.islands.size(); ++index) {
				const auto& island = topology.islands[index]; QJsonArray faces, vertices;
				for (int face : island.faces) { faces.append(face); } for (int vertex : island.vertices) { vertices.append(vertex); }
				islands.append(QJsonObject{{QStringLiteral("index"),index},{QStringLiteral("faces"),faces},{QStringLiteral("vertices"),vertices},
					{QStringLiteral("mins"),QJsonArray{island.mins.u,island.mins.v}},{QStringLiteral("maxs"),QJsonArray{island.maxs.u,island.maxs.v}}});
			}
			for (const auto& edge : topology.edges) { if (edge.seam) { seams.append(QJsonArray{edge.vertices.first,edge.vertices.second}); } }
			result.insert(QStringLiteral("islands"), islands); result.insert(QStringLiteral("seams"),seams); printJson(result);
		} else {
			std::cout << text(QStringLiteral("Surface %1 (%2): %3 UV islands, %4 marked seams\n").arg(surfaceIndex).arg(surface.name).arg(topology.islands.size()).arg(surface.uvSeams.size()));
			for (int index = 0; index < topology.islands.size(); ++index) {
				const auto& island = topology.islands[index];
				std::cout << text(QStringLiteral("Island %1: %2 faces, %3 vertices; U %4 to %5, V %6 to %7\n").arg(index).arg(island.faces.size()).arg(island.vertices.size())
					.arg(island.mins.u).arg(island.maxs.u).arg(island.mins.v).arg(island.maxs.v));
			}
		}
		return exitCodeValue(CliExitCode::Success);
	}
	ModelTopologyHealth health;
	if (!inspectModelTopology(surface, &health, &error)) { return printCliError(command, CliExitCode::ValidationFailed, error, format); }
	const auto indicesJson = [](const QVector<int>& values) { QJsonArray result; for (int i : values) { result.append(i); } return result; };
	const auto edgesJson = [](const QVector<ModelEdge>& values) { QJsonArray result; for (auto e : values) { result.append(QJsonArray{e.first,e.second}); } return result; };
	if (format == CliOutputFormat::Json) {
		auto result = cliResultJson(command);
		result.insert(QStringLiteral("source"), QFileInfo(path).absoluteFilePath());
		result.insert(QStringLiteral("surface"), surfaceIndex); result.insert(QStringLiteral("name"), surface.name);
		result.insert(QStringLiteral("vertices"), surface.vertexCount); result.insert(QStringLiteral("triangles"), surface.triangles.size());
		QJsonArray edges;
		for (const auto& edge : health.edges) {
			edges.append(QJsonObject{{QStringLiteral("a"), edge.vertices.first}, {QStringLiteral("b"), edge.vertices.second},
				{QStringLiteral("faces"), indicesJson(edge.faces)}, {QStringLiteral("inconsistentWinding"), edge.inconsistentWinding}});
		}
		QJsonArray duplicates, fans;
		for (const auto& group : health.duplicateFaces) { duplicates.append(indicesJson(group)); }
		for (const auto& fan : health.disconnectedFans) {
			QJsonArray groups; for (const auto& group : fan.faces) { groups.append(indicesJson(group)); }
			fans.append(QJsonObject{{QStringLiteral("vertex"),fan.vertex},{QStringLiteral("fans"),groups}});
		}
		result.insert(QStringLiteral("health"), QJsonObject{{QStringLiteral("duplicateFaces"),duplicates},
			{QStringLiteral("unusedVertices"),indicesJson(health.unusedVertices)}, {QStringLiteral("disconnectedFans"),fans},
			{QStringLiteral("boundaryEdges"),edgesJson(health.boundaryEdges)}, {QStringLiteral("nonmanifoldEdges"),edgesJson(health.nonmanifoldEdges)},
			{QStringLiteral("windingEdges"),edgesJson(health.windingEdges)}, {QStringLiteral("faceComponents"),health.faceComponents}});
		result.insert(QStringLiteral("edges"), edges); printJson(result);
	} else {
		std::cout << text(QStringLiteral("Surface %1 (%2): %3 vertices, %4 triangles, %5 edges\n").arg(surfaceIndex).arg(surface.name)
			.arg(surface.vertexCount).arg(surface.triangles.size()).arg(health.edges.size()));
		std::cout << text(QStringLiteral("Health: %1 duplicate face groups; %2 unused vertices; %3 disconnected vertex fans; %4 winding edges; %5 nonmanifold edges; %6 boundary edges; %7 face components\n")
			.arg(health.duplicateFaces.size()).arg(health.unusedVertices.size()).arg(health.disconnectedFans.size()).arg(health.windingEdges.size())
			.arg(health.nonmanifoldEdges.size()).arg(health.boundaryEdges.size()).arg(health.faceComponents));
		for (const auto& edge : health.edges) {
			QStringList faces; for (int face : edge.faces) { faces << QString::number(face); }
			std::cout << text(QStringLiteral("%1:%2  faces %3\n").arg(edge.vertices.first).arg(edge.vertices.second).arg(faces.join(QLatin1Char(','))));
		}
	}
	return exitCodeValue(CliExitCode::Success);
}

int runModelSourceCommand(const QString& action, const QString& path, const QStringList& args, CliOutputFormat format)
{
	const QString command = QStringLiteral("model ") + action, output = optionValue(args, QStringLiteral("--output"));
	if (path.isEmpty() || !output.endsWith(QStringLiteral(".mesh.json"), Qt::CaseInsensitive)) {
		return printCliError(command, CliExitCode::Usage, QStringLiteral("%1 requires an input path and --output <source.mesh.json>.").arg(command), format);
	}
	QString error; ModelDocument document;
	if (action == QStringLiteral("import")) {
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly) || file.size() > modelDocumentMaxSourceBytes) {
			return printCliError(command, CliExitCode::Failure, QStringLiteral("The model cannot be read or exceeds 64 MiB."), format);
		}
		const auto bytes = file.read(modelDocumentMaxSourceBytes + 1); ModelMesh mesh;
		if (file.error() != QFileDevice::NoError || !importEditableModel(path, bytes, &mesh, &error) || !document.setMesh(mesh, &error)) {
			return printCliError(command, CliExitCode::ValidationFailed, error.isEmpty() ? QStringLiteral("The source read failed.") : error, format);
		}
	} else {
		if (!document.load(path, &error)) { return printCliError(command, CliExitCode::Failure, error, format); }
		const QMap<QString, ModelEditKind> kinds{
			{QStringLiteral("transform"), ModelEditKind::Transform}, {QStringLiteral("extrude"), ModelEditKind::Extrude},
			{QStringLiteral("subdivide"), ModelEditKind::Subdivide}, {QStringLiteral("duplicate-faces"), ModelEditKind::DuplicateFaces},
			{QStringLiteral("split-edges"), ModelEditKind::SplitEdges}, {QStringLiteral("weld"), ModelEditKind::WeldVertices},
			{QStringLiteral("fill-boundary-loops"), ModelEditKind::FillBoundaryLoops},
			{QStringLiteral("bridge-boundary-loops"), ModelEditKind::BridgeBoundaryLoops},
			{QStringLiteral("remove-duplicate-faces"), ModelEditKind::RemoveDuplicateFaces}, {QStringLiteral("remove-unused-vertices"), ModelEditKind::RemoveUnusedVertices},
			{QStringLiteral("split-disconnected-fans"), ModelEditKind::SplitDisconnectedFans}, {QStringLiteral("orient-faces"), ModelEditKind::OrientFaces},
			{QStringLiteral("split-nonmanifold-edges"), ModelEditKind::SplitNonmanifoldEdges},
			{QStringLiteral("delete-faces"), ModelEditKind::DeleteFaces}, {QStringLiteral("flip-faces"), ModelEditKind::FlipFaces},
			{QStringLiteral("normals"), ModelEditKind::RecalculateNormals}, {QStringLiteral("uv-transform"), ModelEditKind::TransformUv},
			{QStringLiteral("uv-project"), ModelEditKind::ProjectUv}, {QStringLiteral("material"), ModelEditKind::SetMaterial},
			{QStringLiteral("uv-mark-seams"), ModelEditKind::MarkUvSeams}, {QStringLiteral("uv-clear-seams"), ModelEditKind::ClearUvSeams},
			{QStringLiteral("uv-detach"), ModelEditKind::DetachUv},
			{QStringLiteral("uv-unwrap"), ModelEditKind::UnwrapUv}, {QStringLiteral("uv-pack"), ModelEditKind::PackUv},
			{QStringLiteral("uv-pack-around"), ModelEditKind::PackUvAround},
			{QStringLiteral("md2-skin-size"), ModelEditKind::SetMd2SkinSize},
			{QStringLiteral("duplicate-frame"), ModelEditKind::DuplicateFrame}, {QStringLiteral("delete-frame"), ModelEditKind::DeleteFrame},
			{QStringLiteral("rename-frame"), ModelEditKind::RenameFrame},
			{QStringLiteral("add-clip"), ModelEditKind::AddAnimation}, {QStringLiteral("rename-clip"), ModelEditKind::RenameAnimation},
			{QStringLiteral("set-clip-fps"), ModelEditKind::SetAnimationRate},
			{QStringLiteral("set-clip-range"), ModelEditKind::SetAnimationRange}, {QStringLiteral("delete-clip"), ModelEditKind::DeleteAnimation},
			{QStringLiteral("insert-inbetweens"), ModelEditKind::InsertInbetweens}, {QStringLiteral("copy-frame-pose"), ModelEditKind::CopyFramePose},
			{QStringLiteral("add-tag"), ModelEditKind::AddTag}, {QStringLiteral("duplicate-tag"), ModelEditKind::DuplicateTag},
			{QStringLiteral("rename-tag"), ModelEditKind::RenameTag}, {QStringLiteral("delete-tag"), ModelEditKind::DeleteTag},
			{QStringLiteral("set-tag-origin"), ModelEditKind::SetTagOrigin}, {QStringLiteral("reset-tag-orientation"), ModelEditKind::ResetTagOrientation},
			{QStringLiteral("transform-tag"), ModelEditKind::TransformTag}, {QStringLiteral("copy-tag-pose"), ModelEditKind::CopyTagPose}};
		const auto kind = kinds.constFind(optionValue(args, QStringLiteral("--operation")));
		if (kind == kinds.cend()) { return printCliError(command, CliExitCode::Usage, QStringLiteral("Choose --operation %1.").arg(kinds.keys().join(QLatin1Char('|'))), format); }
		ModelEdit edit; edit.kind = kind.value();
		const bool wholeSurfaces = hasOption(args, QStringLiteral("--surfaces"));
		if (wholeSurfaces && (edit.kind != ModelEditKind::Transform || hasOption(args, QStringLiteral("--surface")) ||
			hasOption(args, QStringLiteral("--vertices")) || hasOption(args, QStringLiteral("--faces")) || hasOption(args, QStringLiteral("--edges")))) {
			return printCliError(command, CliExitCode::Usage, QCoreApplication::translate("ModelSurfaceSelection",
				"--surfaces applies only to transform and cannot be mixed with --surface or component selectors."), format);
		}
		const bool animationEdit = isModelAnimationEdit(edit.kind);
		if (animationEdit && (hasOption(args, QStringLiteral("--faces")) || hasOption(args, QStringLiteral("--vertices")) ||
			hasOption(args, QStringLiteral("--edges")) || hasOption(args, QStringLiteral("--surface")))) {
			return printCliError(command, CliExitCode::Usage, QStringLiteral("Animation operations affect the whole model. Omit surface and component selectors."), format);
		}
		const bool clipRange = edit.kind == ModelEditKind::AddAnimation || edit.kind == ModelEditKind::SetAnimationRange;
		const bool existingClip = edit.kind == ModelEditKind::SetAnimationRate || edit.kind == ModelEditKind::RenameAnimation || edit.kind == ModelEditKind::SetAnimationRange || edit.kind == ModelEditKind::DeleteAnimation;
		if (existingClip || hasOption(args, QStringLiteral("--clip"))) {
			bool validClip = false;
			edit.animationIndex = optionValue(args, QStringLiteral("--clip")).toInt(&validClip);
			if (!existingClip || !validClip || edit.animationIndex < 0 || edit.animationIndex >= document.mesh().animations.size()) {
				return printCliError(command, CliExitCode::Usage, QStringLiteral("Clip rename/range/FPS/delete requires --clip with an existing zero-based index. Use model animations to list clips."), format);
			}
		}
		if (clipRange || hasOption(args, QStringLiteral("--first-frame")) || hasOption(args, QStringLiteral("--last-frame"))) {
			bool firstValid = false, lastValid = false;
			edit.rangeFirst = optionValue(args, QStringLiteral("--first-frame")).toInt(&firstValid);
			edit.rangeLast = optionValue(args, QStringLiteral("--last-frame")).toInt(&lastValid);
			if (!clipRange || !firstValid || !lastValid || edit.rangeFirst < 0 || edit.rangeLast < edit.rangeFirst || edit.rangeLast >= document.mesh().frames.size()) {
				return printCliError(command, CliExitCode::Usage, QStringLiteral("add-clip and set-clip-range require inclusive --first-frame and --last-frame indices within the model."), format);
			}
		}
		if (edit.kind == ModelEditKind::SetAnimationRate || hasOption(args, QStringLiteral("--clip-fps"))) {
			bool validRate = false;
			edit.animationRate = optionValue(args, QStringLiteral("--clip-fps")).toDouble(&validRate);
			const auto occurrences = std::count_if(args.cbegin(), args.cend(), [](const QString& arg) { return arg == QStringLiteral("--clip-fps") || arg.startsWith(QStringLiteral("--clip-fps=")); });
			if ((edit.kind != ModelEditKind::AddAnimation && edit.kind != ModelEditKind::SetAnimationRate) || occurrences != 1 || !validRate || !std::isfinite(edit.animationRate) ||
				edit.animationRate < 0 || edit.animationRate > 1000 || (edit.animationRate > 0 && edit.animationRate < .001)) {
				return printCliError(command, CliExitCode::Usage, QCoreApplication::translate("ModelAnimationCli", "Use --clip-fps once with add-clip or set-clip-fps: zero (unspecified) or 0.001–1000 FPS."), format);
			}
		}
		if (hasOption(args, QStringLiteral("--insert-count"))) {
			bool countValid = false;
			edit.inbetweenCount = optionValue(args, QStringLiteral("--insert-count")).toInt(&countValid);
			if (edit.kind != ModelEditKind::InsertInbetweens || !countValid || edit.inbetweenCount < 1 || edit.inbetweenCount > 1022) {
				return printCliError(command, CliExitCode::Usage, QStringLiteral("--insert-count applies to insert-inbetweens and requires 1–1022 frames."), format);
			}
		}
		const bool topologyRepair = edit.kind == ModelEditKind::RemoveDuplicateFaces || edit.kind == ModelEditKind::RemoveUnusedVertices ||
			edit.kind == ModelEditKind::SplitDisconnectedFans || edit.kind == ModelEditKind::OrientFaces || edit.kind == ModelEditKind::SplitNonmanifoldEdges;
		const bool boundaryOperation = edit.kind == ModelEditKind::FillBoundaryLoops || edit.kind == ModelEditKind::BridgeBoundaryLoops;
		if (boundaryOperation && (!hasOption(args, QStringLiteral("--edges")) ||
			hasOption(args, QStringLiteral("--faces")) || hasOption(args, QStringLiteral("--vertices")) ||
			(hasOption(args, QStringLiteral("--frame")) && optionValue(args, QStringLiteral("--frame")) != QStringLiteral("all")))) {
			return printCliError(command, CliExitCode::Usage, QCoreApplication::translate("VibeStudioModelBoundaryFill",
				"Boundary filling and bridging require --edges and cover every frame. Omit faces/vertices and use --frame all (the default)."), format);
		}
		if (hasOption(args, QStringLiteral("--bridge-twist"))) {
			bool twistValid = false;
			edit.bridgeTwist = optionValue(args, QStringLiteral("--bridge-twist")).toInt(&twistValid);
			if (edit.kind != ModelEditKind::BridgeBoundaryLoops || optionValues(args, QStringLiteral("--bridge-twist")).size() != 1 ||
				!twistValid || edit.bridgeTwist < -1023 || edit.bridgeTwist > 1023) {
				return printCliError(command, CliExitCode::Usage, QCoreApplication::translate("ModelBoundaryBridge",
					"--bridge-twist applies once to bridge-boundary-loops and requires an integer from -1023 to 1023."), format);
			}
		}
		if (boundaryOperation && optionValues(args, QStringLiteral("--source-frame")).size() > 1) {
			return printCliError(command, CliExitCode::Usage, QCoreApplication::translate("ModelBoundaryBridge",
				"Choose one reference pose with --source-frame."), format);
		}
		if (topologyRepair && (hasOption(args, QStringLiteral("--faces")) || hasOption(args, QStringLiteral("--vertices")) ||
			hasOption(args, QStringLiteral("--edges")) || (hasOption(args, QStringLiteral("--frame")) && optionValue(args, QStringLiteral("--frame")) != QStringLiteral("all")))) {
			return printCliError(command, CliExitCode::Usage, QStringLiteral("Topology repairs cover the complete --surface in every frame. Omit component selectors and use --frame all (the default)."), format);
		}
		const bool tagEdit = isModelTagEdit(edit.kind);
		const bool positionTransform = edit.kind == ModelEditKind::Transform || edit.kind == ModelEditKind::TransformTag;
		if (edit.kind == ModelEditKind::SetMd2SkinSize || hasOption(args, QStringLiteral("--skin-size"))) {
			const auto size = optionValue(args, QStringLiteral("--skin-size")).split(QLatin1Char(','));
			bool widthValid = false, heightValid = false;
			const int width = size.value(0).toInt(&widthValid), height = size.value(1).toInt(&heightValid);
			if (edit.kind != ModelEditKind::SetMd2SkinSize || size.size() != 2 || !widthValid || !heightValid || width < 1 || width > 8192 || height < 1 || height > 8192) {
				return printCliError(command, CliExitCode::Usage, QStringLiteral("md2-skin-size requires --skin-size width,height with integer dimensions from 1 to 8192. This option applies only to md2-skin-size."), format);
			}
			edit.md2SkinSize = {width, height};
		}
		bool valid = true;
		if (hasOption(args, QStringLiteral("--surface"))) { edit.selection.surface = optionValue(args, QStringLiteral("--surface")).toInt(&valid); }
		if (!valid || edit.selection.surface < 0 || edit.selection.surface >= document.mesh().surfaces.size()) {
			return printCliError(command, CliExitCode::Usage, QStringLiteral("--surface requires an existing zero-based surface index."), format);
		}
		if (hasOption(args, QStringLiteral("--frame")) && optionValue(args, QStringLiteral("--frame")) != QStringLiteral("all")) {
			edit.frame = optionValue(args, QStringLiteral("--frame")).toInt(&valid);
			if (!valid || edit.frame < 0 || edit.frame >= document.mesh().frames.size()) { return printCliError(command, CliExitCode::Usage, QStringLiteral("--frame requires all or an existing zero-based frame index."), format); }
		}
		const auto& surface = document.mesh().surfaces[edit.selection.surface];
		if (tagEdit) {
			if (hasOption(args, QStringLiteral("--faces")) || hasOption(args, QStringLiteral("--vertices")) || hasOption(args, QStringLiteral("--edges")) ||
				hasOption(args, QStringLiteral("--scale")) || hasOption(args, QStringLiteral("--snap-scale"))) {
				return printCliError(command, CliExitCode::Usage, QStringLiteral("Tag edits select --tag name, without mesh components or scaling."), format);
			}
			if (edit.kind == ModelEditKind::AddTag) {
				if (hasOption(args, QStringLiteral("--tag"))) { return printCliError(command, CliExitCode::Usage, QStringLiteral("add-tag uses --name for its new identity, without --tag."), format); }
			} else {
				edit.selection.tag = optionValue(args, QStringLiteral("--tag"));
				if (!findModelTag(document.mesh(), edit.selection.tag, 0)) { return printCliError(command, CliExitCode::Usage, QStringLiteral("Select an existing --tag name. Use model tags to inspect attachments."), format); }
			}
			const bool identity = edit.kind == ModelEditKind::AddTag || edit.kind == ModelEditKind::DuplicateTag || edit.kind == ModelEditKind::RenameTag || edit.kind == ModelEditKind::DeleteTag;
			if (identity && edit.frame != -1) { return printCliError(command, CliExitCode::Usage, QStringLiteral("Adding, duplicating, renaming, or deleting a tag requires --frame all (the default)."), format); }
			if ((hasOption(args, QStringLiteral("--offset")) && edit.kind != ModelEditKind::TransformTag) ||
				(hasOption(args, QStringLiteral("--rotate")) && edit.kind != ModelEditKind::AddTag && edit.kind != ModelEditKind::TransformTag)) {
				return printCliError(command, CliExitCode::Usage, QStringLiteral("Tag offsets apply only to transform-tag. Rotation applies to add-tag and transform-tag."), format);
			}
		} else if (hasOption(args, QStringLiteral("--tag"))) {
			return printCliError(command, CliExitCode::Usage, QStringLiteral("--tag applies only to attachment tag operations."), format);
		}
		if ((hasOption(args, QStringLiteral("--tag-origin")) && edit.kind != ModelEditKind::AddTag && edit.kind != ModelEditKind::SetTagOrigin) ||
			(edit.kind == ModelEditKind::SetTagOrigin && !hasOption(args, QStringLiteral("--tag-origin")))) {
			return printCliError(command, CliExitCode::Usage, QStringLiteral("--tag-origin x,y,z applies to add-tag and is required for set-tag-origin."), format);
		}
		if (hasOption(args, QStringLiteral("--source-frame")) || edit.kind == ModelEditKind::CopyTagPose || edit.kind == ModelEditKind::CopyFramePose) {
			edit.sourceFrame = optionValue(args, QStringLiteral("--source-frame")).toInt(&valid);
			if ((edit.kind != ModelEditKind::CopyTagPose && edit.kind != ModelEditKind::CopyFramePose && !boundaryOperation) || !valid || edit.sourceFrame < 0 || edit.sourceFrame >= document.mesh().frames.size()) {
				return printCliError(command, CliExitCode::Usage, QCoreApplication::translate("VibeStudioModelBoundaryFill",
					"--source-frame requires an existing zero-based frame index. It is required for copy-tag-pose/copy-frame-pose and optional for boundary filling/bridging (default 0)."), format);
			}
		}
		const auto indices = [&](const QString& option, int count, QSet<int>* result) {
			if (!hasOption(args, option)) { return true; }
			const auto value = optionValue(args, option);
			if (value == QStringLiteral("all")) { for (int i = 0; i < count; ++i) { result->insert(i); } return true; }
			for (const auto& word : value.split(QLatin1Char(','))) {
				bool numeric = false; const int i = word.toInt(&numeric);
				if (!numeric || i < 0 || i >= count) { return false; } result->insert(i);
			}
			return !result->isEmpty();
		};
		if (wholeSurfaces) {
			const auto value = optionValue(args, QStringLiteral("--surfaces"));
			const auto words = value.split(QLatin1Char(','));
			const bool unique = value == QStringLiteral("all") || (QSet<QString>(words.cbegin(), words.cend()).size() == words.size() &&
				std::all_of(words.cbegin(), words.cend(), [](const QString& word) { return QRegularExpression(QStringLiteral("^(0|[1-9][0-9]*)$")).match(word).hasMatch(); }));
			const auto occurrences = std::count_if(args.cbegin(), args.cend(), [](const QString& arg) {
				return arg == QStringLiteral("--surfaces") || arg.startsWith(QStringLiteral("--surfaces="));
			});
			if (!unique || occurrences != 1 || !indices(QStringLiteral("--surfaces"), document.mesh().surfaces.size(), &edit.selection.surfaces)) {
				return printCliError(command, CliExitCode::Usage, QCoreApplication::translate("ModelSurfaceSelection",
					"Use --surfaces once with all or unique comma-separated existing surface indices."), format);
			}
			edit.selection.surface = *std::min_element(edit.selection.surfaces.cbegin(), edit.selection.surfaces.cend());
		}
		if (!indices(QStringLiteral("--faces"), surface.triangles.size(), &edit.selection.faces) || !indices(QStringLiteral("--vertices"), surface.vertexCount, &edit.selection.vertices)) {
			return printCliError(command, CliExitCode::Usage, QStringLiteral("--faces and --vertices require all or comma-separated existing indices."), format);
		}
		if (hasOption(args, QStringLiteral("--edges"))) {
			const auto available = modelSurfaceEdges(surface);
			const auto value = optionValue(args, QStringLiteral("--edges"));
			if (value == QStringLiteral("all")) { edit.selection.edges = QSet<ModelEdge>(available.cbegin(), available.cend()); }
			else {
				for (const auto& word : value.split(QLatin1Char(','))) {
					const auto pair = word.split(QLatin1Char(':'));
					bool first = false, second = false;
					const auto edge = modelEdge(pair.value(0).toInt(&first), pair.value(1).toInt(&second));
					if (pair.size() != 2 || !first || !second || !std::binary_search(available.cbegin(), available.cend(), edge)) {
						return printCliError(command, CliExitCode::Usage, QStringLiteral("--edges requires all or comma-separated existing vertex pairs a:b. Use model topology to list edges."), format);
					}
					edit.selection.edges.insert(edge);
				}
			}
		}
		if (hasOption(args, QStringLiteral("--weld-distance"))) {
			edit.weldDistance = optionValue(args, QStringLiteral("--weld-distance")).toDouble(&valid);
			if (!valid || !std::isfinite(edit.weldDistance) || edit.weldDistance < 0 || edit.weldDistance > 1000000 ||
				(edit.weldDistance > 0 && edit.weldDistance < 0.000001)) {
				return printCliError(command, CliExitCode::Usage, QStringLiteral("--weld-distance requires zero or 0.000001–1,000,000 model units."), format);
			}
		}
		edit.preserveSeams = !hasOption(args, QStringLiteral("--merge-seams"));
		if (hasOption(args, QStringLiteral("--snap-grid"))) {
			edit.translationGrid = optionValue(args, QStringLiteral("--snap-grid")).toDouble(&valid);
			if (!positionTransform || !valid || !std::isfinite(edit.translationGrid) || edit.translationGrid < 0 ||
				edit.translationGrid > 1000000 || (edit.translationGrid > 0 && edit.translationGrid < 0.000001)) {
				return printCliError(command, CliExitCode::Usage, QStringLiteral("--snap-grid applies to transform or transform-tag and requires zero or 0.000001–1,000,000 model units."), format);
			}
		}
		const auto vectorOption = [&](const QString& option, ModelVec3* value) {
			if (!hasOption(args, option)) { return true; }
			double x = 0, y = 0, z = 0;
			if (!parseLevelMapDelta(optionValue(args, option), &x, &y, &z)) { return false; }
			*value = {float(x), float(y), float(z)}; return true;
		};
		if (!vectorOption(QStringLiteral("--offset"), &edit.translation) || !vectorOption(QStringLiteral("--rotate"), &edit.rotation) ||
		    !vectorOption(QStringLiteral("--scale"), &edit.scale) || !vectorOption(QStringLiteral("--pivot"), &edit.pivot) || !vectorOption(QStringLiteral("--tag-origin"), &edit.tagOrigin)) {
			return printCliError(command, CliExitCode::Usage, QStringLiteral("Offset, rotation, scale, pivot, and tag origin require finite x,y,z values."), format);
		}
		if (hasOption(args, QStringLiteral("--pivot")) && !positionTransform) {
			return printCliError(command, CliExitCode::Usage, QStringLiteral("--pivot applies to transform or transform-tag."), format);
		}
		for (const auto& option : {QStringLiteral("--snap-angle"), QStringLiteral("--snap-scale")}) {
			if (!hasOption(args, option)) { continue; }
			const double step = optionValue(args, option).toDouble(&valid);
			ModelVec3 checked;
			const bool angle = option == QStringLiteral("--snap-angle");
			if (!positionTransform || !valid ||
				!(angle ? snapModelRotation({}, step, &checked, &error) : snapModelScale({1, 1, 1}, step, &checked, &error))) {
				return printCliError(command, CliExitCode::Usage, QStringLiteral("%1 requires a valid transform snap step. %2").arg(option, error), format);
			}
			(angle ? edit.rotationGrid : edit.scaleGrid) = step;
		}
		if (hasOption(args, QStringLiteral("--pivot-mode"))) {
			const auto mode = optionValue(args, QStringLiteral("--pivot-mode")).toLower();
			if (!positionTransform || (mode != QStringLiteral("origin") && mode != QStringLiteral("selection") && mode != QStringLiteral("custom"))) {
				return printCliError(command, CliExitCode::Usage, QStringLiteral("--pivot-mode applies to transform or transform-tag: origin, selection, or custom."), format);
			}
			edit.pivotMode = mode == QStringLiteral("origin") ? ModelTransformPivot::Origin : mode == QStringLiteral("selection") ? ModelTransformPivot::SelectionCentre : ModelTransformPivot::Custom;
			if (edit.pivotMode != ModelTransformPivot::Custom && hasOption(args, QStringLiteral("--pivot"))) {
				return printCliError(command, CliExitCode::Usage, QStringLiteral("--pivot coordinates require the custom pivot mode."), format);
			}
		}
		if (hasOption(args, QStringLiteral("--pivot-frame"))) {
			edit.pivotFrame = optionValue(args, QStringLiteral("--pivot-frame")).toInt(&valid);
			if (!valid || !positionTransform || edit.pivotMode != ModelTransformPivot::SelectionCentre || edit.frame >= 0 ||
				edit.pivotFrame < 0 || edit.pivotFrame >= document.mesh().frames.size()) {
				return printCliError(command, CliExitCode::Usage, QStringLiteral("--pivot-frame requires an existing reference pose and selection pivot mode for an all-frame transform."), format);
			}
		}
		QHash<QString, QString> axesOptions;
		for (const auto &option : {QStringLiteral("--transform-space"), QStringLiteral("--axis-rotation"), QStringLiteral("--axes-frame")}) {
			if (!hasOption(args, option)) { continue; }
			if (std::count_if(args.cbegin(), args.cend(), [&](const QString &arg) { return arg == option || arg.startsWith(option + QLatin1Char('=')); }) != 1) {
				return printCliError(command, CliExitCode::Usage, QCoreApplication::translate("ModelTransformAxesCli", "Use each transform axes option only once."), format);
			}
			axesOptions.insert(option, optionValue(args, option));
		}
		if (!cli::parseModelTransformAxesOptions(axesOptions, int(document.mesh().frames.size()), &edit, &error)) {
			return printCliError(command, CliExitCode::Usage, error, format);
		}
		const auto uvOption = [&](const QString& option, ModelTexCoord* value) {
			if (!hasOption(args, option)) { return true; }
			const auto values = optionValue(args, option).split(QLatin1Char(','));
			if (values.size() != 2) { return false; }
			bool first = false, second = false; value->u = values[0].toFloat(&first); value->v = values[1].toFloat(&second);
			return first && second && std::isfinite(value->u) && std::isfinite(value->v);
		};
		if (!uvOption(QStringLiteral("--uv-scale"), &edit.uvScale) || !uvOption(QStringLiteral("--uv-offset"), &edit.uvOffset) ||
			!uvOption(QStringLiteral("--uv-pivot"), &edit.uvPivot)) {
			return printCliError(command, CliExitCode::Usage, QStringLiteral("UV scale, offset, and pivot require finite u,v pairs."), format);
		}
		if (hasOption(args, QStringLiteral("--uv-rotation"))) {
			edit.uvRotation = optionValue(args, QStringLiteral("--uv-rotation")).toDouble(&valid);
			if (!valid) { return printCliError(command, CliExitCode::Usage, QStringLiteral("--uv-rotation requires numeric degrees."), format); }
		}
		edit.uvIslands = hasOption(args, QStringLiteral("--uv-islands"));
		const bool uvTransform = edit.kind == ModelEditKind::TransformUv || edit.kind == ModelEditKind::ProjectUv;
		const bool uvAtlas = edit.kind == ModelEditKind::UnwrapUv || edit.kind == ModelEditKind::PackUv || edit.kind == ModelEditKind::PackUvAround;
		if (hasOption(args, QStringLiteral("--uv-pack-scale"))) {
			const auto scale = optionValue(args, QStringLiteral("--uv-pack-scale"));
			const auto count = std::count_if(args.cbegin(), args.cend(), [](const QString &arg) {
				return arg == QStringLiteral("--uv-pack-scale") || arg.startsWith(QStringLiteral("--uv-pack-scale="));
			});
			if (edit.kind != ModelEditKind::PackUvAround || count != 1 ||
				(scale != QStringLiteral("fit") && scale != QStringLiteral("preserve")))
				return printCliError(command, CliExitCode::Usage, QCoreApplication::translate("ModelUvObstacles", "--uv-pack-scale requires uv-pack-around and one value: fit or preserve."), format);
			edit.uvPreserveScale = scale == QStringLiteral("preserve");
		}
		for (const auto& option : {QStringLiteral("--uv-atlas-size"), QStringLiteral("--uv-padding")}) {
			if (!hasOption(args, option)) { continue; }
			if (!uvAtlas || std::count_if(args.cbegin(), args.cend(), [&](const QString &arg) {
				return arg == option || arg.startsWith(option + QLatin1Char('='));
			}) != 1) {
				return printCliError(command, CliExitCode::Usage, QCoreApplication::translate("VibeStudioModelUvAtlas", "Atlas options require uv-unwrap, uv-pack or uv-pack-around and must not be repeated."), format);
			}
			if (option == QStringLiteral("--uv-atlas-size")) {
				const auto match = QRegularExpression(QStringLiteral("\\A([0-9]+)(?:[xX]([0-9]+))?\\z")).match(optionValue(args, option));
				edit.uvAtlasResolution = match.captured(1).toInt(&valid);
				bool heightValid = true;
				if (!match.captured(2).isEmpty()) edit.uvAtlasHeight = match.captured(2).toInt(&heightValid);
				if (!match.hasMatch() || !valid || !heightValid || (!match.captured(2).isEmpty() && edit.uvAtlasHeight == 0)) {
					return printCliError(command, CliExitCode::Usage, QCoreApplication::translate("VibeStudioModelUvAtlas", "--uv-atlas-size requires one pixel dimension or WIDTHxHEIGHT, for example 512 or 512x256."), format);
				}
			} else {
				edit.uvAtlasPadding = optionValue(args, option).toInt(&valid);
				if (!valid) return printCliError(command, CliExitCode::Usage, QCoreApplication::translate("VibeStudioModelUvAtlas", "--uv-padding requires an integer number of pixels."), format);
			}
		}
		const int atlasHeight = edit.uvAtlasHeight == 0 ? edit.uvAtlasResolution : edit.uvAtlasHeight;
		if (uvAtlas && (edit.uvAtlasResolution < 32 || edit.uvAtlasResolution > 4096 || edit.uvAtlasPadding < 0 ||
			atlasHeight < 32 || atlasHeight > 4096 || edit.uvAtlasPadding > 64 || edit.uvAtlasPadding * 8 >= std::min(edit.uvAtlasResolution, atlasHeight))) {
			return printCliError(command, CliExitCode::Usage, QCoreApplication::translate("VibeStudioModelUvAtlas", "Atlas width and height must be 32–4096 pixels; padding must be 0–64 and less than one eighth of the smaller dimension."), format);
		}
		if ((edit.uvIslands && !uvTransform && !uvAtlas && edit.kind != ModelEditKind::DetachUv) ||
			(!uvTransform && (hasOption(args, QStringLiteral("--uv-pivot")) || hasOption(args, QStringLiteral("--uv-pivot-mode")) || hasOption(args, QStringLiteral("--uv-grid"))))) {
			return printCliError(command, CliExitCode::Usage, QStringLiteral("UV pivot/grid options require uv-transform or uv-project; --uv-islands also supports uv-detach, uv-unwrap, uv-pack, and uv-pack-around."), format);
		}
		const auto pivotMode = hasOption(args, QStringLiteral("--uv-pivot-mode")) ? optionValue(args, QStringLiteral("--uv-pivot-mode")) :
			(hasOption(args, QStringLiteral("--uv-pivot")) ? QStringLiteral("custom") : QStringLiteral("origin"));
		const int pivotIndex = QStringList{QStringLiteral("origin"),QStringLiteral("selection"),QStringLiteral("custom"),QStringLiteral("islands")}.indexOf(pivotMode);
		if (args.count(QStringLiteral("--uv-pivot-mode")) > 1 || pivotIndex < 0 || (hasOption(args, QStringLiteral("--uv-pivot")) && pivotIndex != 2) || (pivotIndex == 2 && !hasOption(args, QStringLiteral("--uv-pivot")))) {
			return printCliError(command, CliExitCode::Usage, QCoreApplication::translate("ModelUvTransform", "Use --uv-pivot-mode once: origin|selection|islands, or custom with --uv-pivot u,v. Individual islands require complete face selections; --uv-islands expands components first."), format);
		}
		edit.uvPivotMode = ModelUvPivot(pivotIndex);
		if (hasOption(args, QStringLiteral("--uv-grid"))) {
			edit.uvTranslationGrid = optionValue(args, QStringLiteral("--uv-grid")).toDouble(&valid);
			if (!valid || !std::isfinite(edit.uvTranslationGrid) || edit.uvTranslationGrid < 0 || edit.uvTranslationGrid > 1000000 ||
				(edit.uvTranslationGrid > 0 && edit.uvTranslationGrid < 0.000001)) {
				return printCliError(command, CliExitCode::Usage, QStringLiteral("--uv-grid requires zero or a step from 0.000001 to 1,000,000 UV units."), format);
			}
		}
		if (hasOption(args, QStringLiteral("--projection"))) {
			edit.projection = QStringList{QStringLiteral("xy"), QStringLiteral("xz"), QStringLiteral("yz")}.indexOf(optionValue(args, QStringLiteral("--projection")).toLower());
		}
		edit.text = optionValue(args, edit.kind == ModelEditKind::SetMaterial ? QStringLiteral("--material") : QStringLiteral("--name"));
		document.setSelection(edit.selection);
		if (!document.edit(edit, &error)) { return printCliError(command, CliExitCode::ValidationFailed, error, format); }
	}
	const bool dryRun = hasOption(args, QStringLiteral("--dry-run")), overwrite = hasOption(args, QStringLiteral("--overwrite"));
	const QFileInfo target(output), input(path);
	if (target.isDir() || target.isSymLink() || (target.exists() && !overwrite) ||
	    (action == QStringLiteral("import") && (target.absoluteFilePath().compare(input.absoluteFilePath(), Qt::CaseInsensitive) == 0 ||
	    (!target.canonicalFilePath().isEmpty() && target.canonicalFilePath().compare(input.canonicalFilePath(), Qt::CaseInsensitive) == 0)))) {
		return printCliError(command, CliExitCode::Failure, QStringLiteral("Choose an output file. Existing files require --overwrite; imported sources and symbolic links are protected."), format);
	}
	if (!dryRun && !document.save(output, overwrite, &error)) { return printCliError(command, CliExitCode::Failure, error, format); }
	const auto& mesh = document.mesh();
	if (format == CliOutputFormat::Json) {
		auto result = cliResultJson(command); result.insert(QStringLiteral("outputPath"), target.absoluteFilePath());
		result.insert(QStringLiteral("dryRun"), dryRun); result.insert(QStringLiteral("written"), !dryRun);
		result.insert(QStringLiteral("surfaces"), mesh.surfaceCount); result.insert(QStringLiteral("vertices"), mesh.vertexCount);
		result.insert(QStringLiteral("triangles"), mesh.triangleCount); result.insert(QStringLiteral("frames"), mesh.frameCount); result.insert(QStringLiteral("tags"), mesh.tagCount);
		QJsonArray selectedSurfaces;
		auto selected = document.selection().surfaces.values();
		std::sort(selected.begin(), selected.end());
		for (int index : selected) { selectedSurfaces.append(index); }
		result.insert(QStringLiteral("selectedSurfaces"), selectedSurfaces);
		printJson(result);
	} else {
		std::cout << text(QStringLiteral("%1 %2 (%3 surfaces, %4 triangles, %5 frames)\n").arg(dryRun ? QStringLiteral("Would write") : QStringLiteral("Wrote"), target.absoluteFilePath()).arg(mesh.surfaceCount).arg(mesh.triangleCount).arg(mesh.frameCount));
	}
	return exitCodeValue(CliExitCode::Success);
}

int runModelBuildCommand(const QString& path, const QStringList& args, CliOutputFormat format)
{
	const QString command = QStringLiteral("model build"), output = optionValue(args, QStringLiteral("--output"));
	if (path.isEmpty() || output.isEmpty()) { return printCliError(command, CliExitCode::Usage, QStringLiteral("model build requires a design or editable mesh JSON path and --output <model.mdl|model.md2|model.md3|model.obj>."), format); }
	ModelDesign design; ModelMesh mesh; QString error;
	const bool editableMesh = path.endsWith(QStringLiteral(".mesh.json"), Qt::CaseInsensitive);
	if (editableMesh) {
		ModelDocument document;
		if (!document.load(path, &error)) { return printCliError(command, CliExitCode::Failure, error, format); }
		mesh = document.mesh();
	} else {
		if (!loadModelDesign(path, &design, &error)) { return printCliError(command, CliExitCode::Failure, error, format); }
		mesh = buildModelDesignMesh(design);
	}
	const QString kind = hasOption(args, QStringLiteral("--format")) ? optionValue(args, QStringLiteral("--format")).toLower() : QFileInfo(output).suffix().toLower();
	bool frameValid = true;
	const int frame = hasOption(args, QStringLiteral("--frame")) ? optionValue(args, QStringLiteral("--frame")).toInt(&frameValid) : 0;
	if (!frameValid || frame < 0 || frame >= mesh.frameCount) { return printCliError(command, CliExitCode::Usage, QStringLiteral("--frame requires an existing zero-based frame index."), format); }
	ModelExportReport report;
	const QByteArray bytes = exportEditableModel(mesh, kind, frame, &error, {}, &report);
	if (bytes.isEmpty()) { return printCliError(command, CliExitCode::ValidationFailed, error, format); }
	const bool dryRun = hasOption(args, QStringLiteral("--dry-run")), overwrite = hasOption(args, QStringLiteral("--overwrite"));
	const QFileInfo target(output), source(path);
	if (target.isDir() || target.isSymLink() || target.absoluteFilePath().compare(source.absoluteFilePath(), Qt::CaseInsensitive) == 0
		|| (!target.canonicalFilePath().isEmpty() && target.canonicalFilePath().compare(source.canonicalFilePath(), Qt::CaseInsensitive) == 0)
		|| (target.exists() && !overwrite)) { return printCliError(command, CliExitCode::Failure, QStringLiteral("Choose an output file path. Existing files require --overwrite; design sources and symbolic links are protected."), format); }
	if (!dryRun && !saveModelDesignBytes(output, bytes, overwrite, &error, path)) { return printCliError(command, CliExitCode::Failure, error, format); }
	int authoringSeams = 0; for (const auto& surface : mesh.surfaces) { authoringSeams += surface.uvSeams.size(); }
	if (format == CliOutputFormat::Json) {
		auto result = cliResultJson(command, CliExitCode::Success);
		result.insert(QStringLiteral("source"), QFileInfo(path).absoluteFilePath()); result.insert(QStringLiteral("outputPath"), target.absoluteFilePath());
		result.insert(QStringLiteral("format"), kind); result.insert(QStringLiteral("dryRun"), dryRun); result.insert(QStringLiteral("written"), !dryRun);
		result.insert(QStringLiteral("bytes"), bytes.size()); result.insert(QStringLiteral("parts"), mesh.surfaceCount);
		result.insert(QStringLiteral("frames"), mesh.frameCount); result.insert(QStringLiteral("tags"), mesh.tagCount);
		result.insert(QStringLiteral("vertices"), mesh.vertexCount); result.insert(QStringLiteral("triangles"), mesh.triangleCount);
		result.insert(QStringLiteral("authoringSeams"), authoringSeams);
		result.insert(QStringLiteral("seamMarksExported"), false);
		result.insert(QStringLiteral("exportNotes"), QJsonArray::fromStringList(report.notes));
		if (kind == QStringLiteral("mdl")) {
			result.insert(QStringLiteral("mdlSkinSize"), QJsonArray{mesh.mdl.skinSize.width(), mesh.mdl.skinSize.height()});
			result.insert(QStringLiteral("skinSlots"), mesh.embeddedSkins.size());
			result.insert(QStringLiteral("nativeFrames"), mesh.mdl.frameGroups.size());
		}
		if (kind == QStringLiteral("md2")) {
			result.insert(QStringLiteral("md2SkinSize"), QJsonArray{mesh.md2SkinSize.width(), mesh.md2SkinSize.height()});
			result.insert(QStringLiteral("skinSlots"), QJsonArray::fromStringList(mesh.surfaces[0].skinPaths));
		}
		if (kind == QStringLiteral("md2") || kind == QStringLiteral("mdl")) {
			result.insert(QStringLiteral("storedVertices"), report.storedVertices);
			result.insert(QStringLiteral("maxPositionError"), report.maxPositionError);
			result.insert(QStringLiteral("maxUvError"), report.maxUvError);
			result.insert(QStringLiteral("maxNormalAngleDegrees"), report.maxNormalAngleDegrees);
		}
		result.insert(QStringLiteral("materials"), QJsonArray::fromStringList(mesh.skinPaths)); printJson(result);
	} else { std::cout << text(QStringLiteral("%1 %2 (%3 parts, %4 triangles, %5 bytes)\n").arg(dryRun ? QStringLiteral("Would write") : QStringLiteral("Wrote"), target.absoluteFilePath()).arg(mesh.surfaceCount).arg(mesh.triangleCount).arg(bytes.size())); }
	if (format != CliOutputFormat::Json && !report.notes.isEmpty()) { std::cout << text(report.notes.join(QLatin1Char('\n')) + QLatin1Char('\n')); }
	if (format != CliOutputFormat::Json && authoringSeams > 0) {
		std::cout << text(QStringLiteral("%1 seam marks remain in the editable source. Export uses the resolved UV coordinates and split indices.\n").arg(authoringSeams));
	}
	return exitCodeValue(CliExitCode::Success);
}

int runMapPlaceModelCommand(const QString& path, const QStringList& args, CliOutputFormat format)
{
	const QString command = QStringLiteral("map place-model"), output = optionValue(args, QStringLiteral("--output"));
	const QString package = optionValue(args, QStringLiteral("--package")), entry = optionValue(args, QStringLiteral("--entry"));
	double x = 0, y = 0, z = 0;
	if (path.isEmpty() || output.isEmpty() || package.isEmpty() || entry.isEmpty() || !parseLevelMapDelta(optionValue(args, QStringLiteral("--origin")), &x, &y, &z)) {
		return printCliError(command, CliExitCode::Usage, QStringLiteral("map place-model requires a map, --package, --entry, --origin x,y,z, and --output."), format);
	}
	LevelMapDocument document; QString error; PackageArchive archive;
	const auto request = levelMapLoadRequestFromArgs(path, args);
	if (!loadLevelMap(request, &document, &error) || !archive.load(package, &error)) { return printCliError(command, CliExitCode::Failure, error, format); }
	PackageSelectionRequest selection; selection.entries << entry; const auto selected = selectPackageFiles(archive, selection);
	if (!selected.succeeded() || selected.totalBytes > 8 * 1024 * 1024) { return printCliError(command, CliExitCode::ValidationFailed, QStringLiteral("Select one readable, unambiguous MD3 no larger than 8 MiB. %1").arg(selected.errors.join(QLatin1Char(';'))), format); }
	QByteArray bytes;
	if (!archive.readEntryBytes(entry, &bytes, &error, 8 * 1024 * 1024 + 1) || bytes.size() != static_cast<qint64>(selected.totalBytes)) { return printCliError(command, CliExitCode::Failure, QStringLiteral("The model could not be read completely: %1").arg(error), format); }
	const auto mesh = decodeModelMesh(entry, bytes);
	const bool damagedSurface = std::any_of(mesh.surfaces.cbegin(), mesh.surfaces.cend(), [](const ModelSurface& surface) { return !surface.warnings.isEmpty(); });
	if (!mesh.geometryAvailable || mesh.format != ModelMeshFormat::Quake3Md3 || !mesh.error.isEmpty() || !mesh.warnings.isEmpty() || damagedSurface) { return printCliError(command, CliExitCode::ValidationFailed, QStringLiteral("The entry must contain complete MD3 geometry without parser warnings."), format); }
	int entityId = -1;
	if (!placeLevelModel(&document, selected.paths.first(), {x, y, z, true}, &entityId, &error)) { return printCliError(command, CliExitCode::Failure, error, format); }
	const auto report = saveLevelMapAs(document, output, hasOption(args, QStringLiteral("--dry-run")), hasOption(args, QStringLiteral("--overwrite")));
	return printLevelMapSaveResult(command, document, report, format, {{QStringLiteral("entityId"), entityId}, {QStringLiteral("model"), selected.paths.first()}});
}

int runModelExportCommand(const QString& commandName, const QString& packagePath, const QString& entryPath, const QStringList& args, CliOutputFormat format)
{
	ModelMesh mesh;
	PackageArchive sourceArchive;
	QString source;
	int exitCode = exitCodeValue(CliExitCode::Failure);
	if (!loadModelMeshForCli(commandName, packagePath, entryPath, args, format, &mesh, &source, &exitCode, &sourceArchive)) {
		return exitCode;
	}
	if (!mesh.geometryAvailable) {
		return printCliError(commandName, CliExitCode::Unavailable,
			mesh.error.isEmpty()
				? QStringLiteral("Geometry decoding is not implemented for this model format, so it cannot be exported.")
				: mesh.error,
			format);
	}

	bool parsedFrame = true;
	const int frameIndex = hasOption(args, QStringLiteral("--frame")) ? optionValue(args, QStringLiteral("--frame")).toInt(&parsedFrame) : 0;
	if (!parsedFrame || frameIndex < 0 || frameIndex >= mesh.frames.size()) {
		return printCliError(commandName, CliExitCode::Usage,
			QStringLiteral("--frame must be between 0 and %1.").arg(std::max(0, mesh.frameCount - 1)), format);
	}

	const QString outputPath = optionValue(args, QStringLiteral("--output"));
	const bool dryRun = hasOption(args, QStringLiteral("--dry-run"));
	if (hasOption(args, QStringLiteral("--output")) && outputPath.trimmed().isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("--output requires a model output path."), format);
	}
	ModelFrameExportRequest request;
	request.frame = frameIndex; request.materialName = optionValue(args, QStringLiteral("--material"));
	request.outputPath = outputPath; request.dryRun = dryRun; request.overwrite = hasOption(args, QStringLiteral("--overwrite"));
	request.protectedFiles = {packagePath};
	if (sourceArchive.isOpen()) { request.sourceArchive = std::make_shared<PackageArchive>(sourceArchive); }
	const auto exported = exportModelFrame(mesh, request);
	const bool success = exported.succeeded();
	if (!success && outputPath.isEmpty()) { return printCliError(commandName, CliExitCode::Failure, exported.error, format); }
	if (outputPath.isEmpty()) {
		if (format == CliOutputFormat::Json) {
			QJsonObject object = cliResultJson(commandName);
			object.insert(QStringLiteral("source"), source);
			object.insert(QStringLiteral("frame"), frameIndex);
			object.insert(QStringLiteral("obj"), QString::fromUtf8(exported.bytes));
			object.insert(QStringLiteral("notes"), QJsonArray::fromStringList(exported.notes));
			printJson(object);
		} else {
			std::cout.write(exported.bytes.constData(), exported.bytes.size());
			std::cout.flush();
			if (!std::cout) {
				std::cerr << "The OBJ could not be written to stdout.\n";
				return exitCodeValue(CliExitCode::Failure);
			}
			for (const auto& note : exported.notes) { std::cerr << text(note) << '\n'; }
		}
		return exitCodeValue(CliExitCode::Success);
	}

	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, success ? CliExitCode::Success : CliExitCode::Failure);
		object.insert(QStringLiteral("source"), source);
		object.insert(QStringLiteral("frame"), frameIndex);
		object.insert(QStringLiteral("outputPath"), outputPath);
		object.insert(QStringLiteral("dryRun"), dryRun);
		object.insert(QStringLiteral("written"), exported.written);
		object.insert(QStringLiteral("notes"), QJsonArray::fromStringList(exported.notes));
		if (!success) { object.insert(QStringLiteral("error"), exported.error); }
		printJson(object);
	} else {
		std::cout << text(!success ? QStringLiteral("Not written: %1\n").arg(exported.error) : dryRun
			? QStringLiteral("Dry run: would write frame %1 to %2\n").arg(frameIndex).arg(QDir::toNativeSeparators(outputPath))
			: QStringLiteral("Wrote frame %1 to %2\n").arg(frameIndex).arg(QDir::toNativeSeparators(outputPath)));
		for (const auto& note : exported.notes) { std::cout << text(note) << '\n'; }
	}
	return exitCodeValue(success ? CliExitCode::Success : CliExitCode::Failure);
}


// ---------------------------------------------------------------------------
// package compare
// ---------------------------------------------------------------------------

int runPackageCompareCommand(const QString& commandName, const QString& leftPath, const QString& rightPath, const QStringList& args, CliOutputFormat format)
{
	const bool staged = hasOption(args, QStringLiteral("--staged"));
	if (leftPath.trimmed().isEmpty() || (!staged && rightPath.trimmed().isEmpty())
		|| (staged && (!rightPath.trimmed().isEmpty() || hasOption(args, QStringLiteral("--against"))))) {
		return printCliError(commandName, CliExitCode::Usage,
			QStringLiteral("%1 requires two package paths, or one path with --staged and staging options.").arg(commandName), format);
	}
	if (!staged) {
		for (const QString& option : packageStageValueOptions()) {
			if (hasOption(args, option)) {
				return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Staging options require --staged."), format);
			}
		}
	}

	PackageArchive left; PackageStagingModel plan;
	QString error;
	if (staged ? !loadPackageStaging(leftPath, &plan, &error) : !loadPackageForCliQuiet(leftPath, &left, &error)) {
		return printCliError(commandName, CliExitCode::NotFound,
			QStringLiteral("Unable to open the first package: %1").arg(error), format);
	}
	// Retain a draft's revision/history and compare against the very same
	// captured planned content. Treating .vibepackage as a loose directory
	// would compare its storage objects and invalidate reviewed group IDs.
	if (staged) { left = packagePlannedArchive(plan, &error); if (!left.isOpen()) { return printCliError(commandName, CliExitCode::Failure, error, format); } }
	PackageArchive right;
	if (!staged && !loadPackageForCliQuiet(rightPath, &right, &error)) {
		return printCliError(commandName, CliExitCode::NotFound,
			QStringLiteral("Unable to open the second package: %1").arg(error), format);
	}

	PackageCompareRequest request;
	request.metadataOnly = hasOption(args, QStringLiteral("--metadata-only"));
	request.includeDirectories = hasOption(args, QStringLiteral("--include-directories"));
	bool parsedBudget = false;
	const qint64 budget = optionValue(args, QStringLiteral("--max-entry-bytes")).toLongLong(&parsedBudget);
	if (hasOption(args, QStringLiteral("--max-entry-bytes")) && (!parsedBudget || budget <= 0)) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("--max-entry-bytes must be a positive integer."), format);
	}
	if (parsedBudget) {
		request.maxEntryBytes = budget;
	}

	if (staged && !applyPackageStageArgs(&plan, args, &error, false)) {
		return printCliError(commandName, CliExitCode::ValidationFailed, error, format);
	}
	if (staged) { request.leftLabel = QStringLiteral("source"); request.rightLabel = QStringLiteral("staged"); }
	const PackageCompareResult result = staged ? comparePackageToPlan(left, plan, request) : comparePackages(left, right, request);
	// A difference is a finding, not a failure: report it with the validation
	// exit code so a release script can gate on "these two packages match".
	const CliExitCode code = result.identical() ? CliExitCode::Success : CliExitCode::ValidationFailed;

	if (format == CliOutputFormat::Json) {
		QJsonObject object = cliResultJson(commandName, code);
		object.insert(QStringLiteral("comparison"), packageCompareJson(result));
		if (staged) { object.insert(QStringLiteral("staging"), packageStagingJson(plan)); }
		printJson(object);
	} else {
		std::cout << text(packageCompareText(result)) << "\n";
	}
	return exitCodeValue(code);
}

int runSubcommand(const QStringList& args)
{
	const QStringList tokens = commandTokens(args);
	if (tokens.isEmpty() || tokens.first().startsWith(QStringLiteral("--"))) {
		return -1;
	}

	const CliOutputFormat format = outputFormat(args);
	const QString family = normalizedOptionId(tokens.value(0));
	const QString action = normalizedOptionId(tokens.value(1));
	const QString commandName = action.isEmpty() ? family : QStringLiteral("%1 %2").arg(family, action);

	if (family == QStringLiteral("workspace") || (family == QStringLiteral("asset") && (action == QStringLiteral("formats") || action == QStringLiteral("route")))) {
		const auto result = runWorkspaceCommand(args);
		if (result.exitCode != 0) { return printCliError(commandName, static_cast<CliExitCode>(result.exitCode), result.error, format); }
		if (format == CliOutputFormat::Json) {
			auto output = cliResultJson(commandName);
			for (auto it = result.payload.begin(); it != result.payload.end(); ++it) { output.insert(it.key(), it.value()); }
			printJson(output);
		} else { std::cout << text(result.lines.join(QLatin1Char('\n'))) << '\n'; }
		return 0;
	}
	if (family == QStringLiteral("exit-codes")) {
		printExitCodes(format, QStringLiteral("cli exit-codes"));
		return exitCodeValue(CliExitCode::Success);
	}
	if (family == QStringLiteral("about") || family == QStringLiteral("license")) {
		// `about` and `about show` are the same command; the registry names it
		// "about show" so every registered command has a family and an action.
		return runAboutCommand(QStringLiteral("about show"), format);
	}
	if (family == QStringLiteral("credits")) {
		if (action == QStringLiteral("validate") || action == QStringLiteral("check")) {
			return runCreditsValidateCommand(QStringLiteral("credits validate"), format);
		}
		return runAboutCommand(QStringLiteral("credits"), format);
	}
	if (family == QStringLiteral("cli") && (action == QStringLiteral("exit-codes") || action == QStringLiteral("codes"))) {
		printExitCodes(format, QStringLiteral("cli exit-codes"));
		return exitCodeValue(CliExitCode::Success);
	}
	if (family == QStringLiteral("cli") && (action == QStringLiteral("commands") || action == QStringLiteral("help"))) {
		return runCliCommandsCommand(QStringLiteral("cli commands"), format);
	}
	if ((family == QStringLiteral("ui") || family == QStringLiteral("shell")) && (action == QStringLiteral("semantics") || action == QStringLiteral("status") || action == QStringLiteral("shortcuts") || action == QStringLiteral("commands"))) {
		return runUiSemanticsCommand(QStringLiteral("ui semantics"), format);
	}

	if (action.isEmpty()) {
		return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Missing CLI subcommand. Run --cli --help."), format);
	}

	if (family == QStringLiteral("project")) {
		if (action == QStringLiteral("files")) { return runCodeFilesCommand(tokens.value(2), args, format, true); }
		if (action == QStringLiteral("init") || action == QStringLiteral("create")) {
			return runProjectInitCommand(QStringLiteral("project init"), tokens.value(2), args, format);
		}
		if (action == QStringLiteral("info")) {
			return runProjectInfoCommand(QStringLiteral("project info"), tokens.value(2), format);
		}
		if (action == QStringLiteral("validate")) {
			return runProjectValidateCommand(QStringLiteral("project validate"), tokens.value(2), format);
		}
	}

	if (family == QStringLiteral("install") || family == QStringLiteral("installation") || family == QStringLiteral("installations")) {
		if (action == QStringLiteral("list") || action == QStringLiteral("ls")) {
			return runInstallListCommand(QStringLiteral("install list"), format);
		}
		if (action == QStringLiteral("detect") || action == QStringLiteral("discover")) {
			QStringList roots = optionValues(args, QStringLiteral("--root"));
			roots += optionValues(args, QStringLiteral("--detect-install-root"));
			return runInstallDetectCommand(QStringLiteral("install detect"), roots, format);
		}
	}

	if (family == QStringLiteral("package")) {
		if (action == QStringLiteral("info")) {
			return runPackageInfoCommand(QStringLiteral("package info"), tokens.value(2), format);
		}
		if (action == QStringLiteral("list") || action == QStringLiteral("ls")) {
			return runPackageListCommand(QStringLiteral("package list"), tokens.value(2), format, optionValue(args, QStringLiteral("--where")));
		}
		if (action == QStringLiteral("preview")) {
			const QString command = QStringLiteral("package preview");
			const auto usage = [&](const QString& message) { return printCliError(command, CliExitCode::Usage, message, format); };
			const QSet<QString> flags{QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--quiet"), QStringLiteral("--verbose")};
			const QSet<QString> values{QStringLiteral("--entry-index"), QStringLiteral("--settings-file"), QStringLiteral("--locale"), QStringLiteral("--catalog-root")};
			QSet<QString> seen;
			for (qsizetype position = 1; position < args.size(); ++position) {
				const QString argument = args.at(position);
				if (!argument.startsWith('-')) { continue; }
				const qsizetype equals = argument.indexOf('=');
				const QString key = equals < 0 ? argument : argument.left(equals);
				if (seen.contains(key) || (!flags.contains(argument) && !values.contains(key))) { return usage(QStringLiteral("Unsupported or repeated preview option: %1").arg(argument)); }
				seen.insert(key);
				if (values.contains(key)) {
					const QString value = equals < 0 ? args.value(++position) : argument.mid(equals + 1);
					if (value.trimmed().isEmpty() || (equals < 0 && value.startsWith('-'))) { return usage(QStringLiteral("%1 requires a value.").arg(key)); }
				}
			}
			int entryIndex = -1; QString error;
			const bool indexed = hasOption(args, QStringLiteral("--entry-index"));
			if (indexed && !parsePackageIndex(optionValue(args, QStringLiteral("--entry-index")), &entryIndex, &error)) { return usage(error); }
			if (tokens.size() != (indexed ? 3 : 4)) { return usage(QStringLiteral("Choose one virtual entry path or --entry-index for package preview.")); }
			return runPackagePreviewCommand(command, tokens.value(2), tokens.value(3), format, entryIndex);
		}
		if (action == QStringLiteral("extract")) {
			const QString outputPath = hasOption(args, QStringLiteral("--output")) ? optionValue(args, QStringLiteral("--output")) : tokens.value(3);
			return runPackageExtractCommand(
				QStringLiteral("package extract"),
				tokens.value(2),
				outputPath,
				packageExtractionEntriesFromArgs(args),
				hasOption(args, QStringLiteral("--extract-all")),
				hasOption(args, QStringLiteral("--dry-run")),
				hasOption(args, QStringLiteral("--overwrite")),
				args, format);
		}
		if (action == QStringLiteral("validate") || action == QStringLiteral("check")) {
			return runPackageValidateCommand(QStringLiteral("package validate"), tokens.value(2), args, format);
		}
		if (action == QStringLiteral("recover") || action == QStringLiteral("interrupted-saves")) {
			return runPackagePublicationCommand(action, args, format);
		}
		if (action == QStringLiteral("recoveries") || action == QStringLiteral("draft-recover") || action == QStringLiteral("recovery-discard")) { return runPackageRecoveryStoreCommand(action, args, format); }
		if (action == QStringLiteral("draft-storage") || action == QStringLiteral("draft-compact")) { return runPackageDraftStorageCommand(action, args, format); }
		if (action == QStringLiteral("copy-sessions") || action == QStringLiteral("copy-discard")) {
			const auto result = runPackageCopySessions(args);
			const QString command = QStringLiteral("package ") + action;
			if (format == CliOutputFormat::Json) {
				auto output = cliResultJson(command, static_cast<CliExitCode>(result.exitCode));
				for (auto it = result.payload.begin(); it != result.payload.end(); ++it) { output.insert(it.key(), it.value()); }
				if (!result.error.isEmpty()) { output.insert(QStringLiteral("message"), result.error); }
				printJson(output);
			} else {
				if (!result.summary.isEmpty()) { std::cout << text(result.summary) << '\n'; }
				if (!result.error.isEmpty()) { std::cerr << text(result.error) << '\n'; }
			}
			return result.exitCode;
		}
		if (action == QStringLiteral("copy-limits") || action == QStringLiteral("copy-store-limits")) {
			const auto result = action == QStringLiteral("copy-limits") ? runPackageCopyLimits(args) : runPackageCopyStoreLimits(args);
			const QString command = QStringLiteral("package ") + action;
			if (format == CliOutputFormat::Json) {
				auto output = cliResultJson(command, static_cast<CliExitCode>(result.exitCode));
				for (auto it = result.payload.begin(); it != result.payload.end(); ++it) { output.insert(it.key(), it.value()); }
				if (!result.error.isEmpty()) { output.insert(QStringLiteral("message"), result.error); }
				printJson(output);
			} else {
				if (!result.summary.isEmpty()) { std::cout << text(result.summary) << '\n'; }
				if (!result.error.isEmpty()) { std::cerr << text(result.error) << '\n'; }
			}
			return result.exitCode;
		}
		if (action == QStringLiteral("working-imports") || action == QStringLiteral("working-discard") || action == QStringLiteral("working-unlock")) { return runPackageWorkingStoreCommand(action, args, format); }
		if (action == QStringLiteral("draft-save") || action == QStringLiteral("draft-info") || action == QStringLiteral("draft-undo") || action == QStringLiteral("draft-redo")) {
			return runPackageDraftCommand(action, tokens, args, format);
		}
		if (action == QStringLiteral("create")) { return runPackageCreateCommand(tokens, args, format); }
		if (action == QStringLiteral("stage") || action == QStringLiteral("staging")) {
			return runPackageStageCommand(QStringLiteral("package stage"), tokens.value(2), args, format);
		}
		if (action == QStringLiteral("compare") || action == QStringLiteral("diff")) {
			const QString comparePath = hasOption(args, QStringLiteral("--against")) ? optionValue(args, QStringLiteral("--against")) : tokens.value(3);
			return runPackageCompareCommand(QStringLiteral("package compare"), tokens.value(2), comparePath, args, format);
		}
		if (action == QStringLiteral("manifest")) {
			const QString outputPath = hasOption(args, QStringLiteral("--output")) ? optionValue(args, QStringLiteral("--output")) : tokens.value(3);
			return runPackageManifestCommand(QStringLiteral("package manifest"), tokens.value(2), outputPath, args, format);
		}
		if (action == QStringLiteral("subset")) { return runPackageSubsetCommand(tokens, args, format); }
		if (action == QStringLiteral("groups")) { return runPackageGroupsCommand(tokens, args, format); }
		if (action == QStringLiteral("save-as") || action == QStringLiteral("write") || action == QStringLiteral("rebuild")) {
			const QString outputPath = hasOption(args, QStringLiteral("--output")) ? optionValue(args, QStringLiteral("--output")) : tokens.value(3);
			return runPackageSaveAsCommand(QStringLiteral("package save-as"), tokens.value(2), outputPath, args, format);
		}
	}

	if (family == QStringLiteral("asset") || family == QStringLiteral("assets")) {
		if (action == QStringLiteral("audio-session") || action == QStringLiteral("audio-take") || action == QStringLiteral("audio-recording")) {
			const auto result = action == QStringLiteral("audio-recording") ? runAudioRecording(args) :
			    action == QStringLiteral("audio-take") ? runAudioTake(args) : runAudioSession(args);
			const auto command = QStringLiteral("asset ") + action;
			if (format == CliOutputFormat::Json) {
				auto output = cliResultJson(command, static_cast<CliExitCode>(result.exitCode));
				for (auto it = result.payload.begin(); it != result.payload.end(); ++it) { output.insert(it.key(), it.value()); }
				if (!result.error.isEmpty()) { output.insert(QStringLiteral("message"), result.error); }
				printJson(output);
			} else {
				if (!result.lines.isEmpty()) { std::cout << text(result.lines.join(QLatin1Char('\n'))) << '\n'; }
				if (!result.error.isEmpty()) { std::cerr << text(result.error) << '\n'; }
			}
			return result.exitCode;
		}
		if (action == QStringLiteral("audio-generate")) {
			const auto result = runAudioGenerate(args);
			const auto command = QStringLiteral("asset audio-generate");
			if (result.exitCode != 0) { return printCliError(command, static_cast<CliExitCode>(result.exitCode), result.error, format); }
			if (format == CliOutputFormat::Json) {
				auto output = cliResultJson(command);
				for (auto it = result.payload.begin(); it != result.payload.end(); ++it) { output.insert(it.key(), it.value()); }
				printJson(output);
			} else { std::cout << text(result.lines.join(QLatin1Char('\n'))) << '\n'; }
			return 0;
		}
		if (action == QStringLiteral("audio-export")) { return runAssetAudioExportCommand(tokens.value(2), args, format); }
		if (action == QStringLiteral("audio-analyze")) { return runAssetAudioAnalyzeCommand(tokens.value(2), args, format); }
		if (action == QStringLiteral("audio-new")) { return runAssetAudioNewCommand(args, format); }
		if (action == QStringLiteral("audio-recoveries")) { return runAssetAudioRecoveriesCommand(args, format); }
		if (action == QStringLiteral("audio-markers")) { return runAssetAudioMarkersCommand(tokens.value(2), args, format); }
		if (action == QStringLiteral("audio-project")) { return runAssetAudioProjectCommand(tokens.value(2), args, format); }
		if (action == QStringLiteral("audio-edit")) { return runAssetAudioEditCommand(tokens.value(2), args, format); }
		if (action == QStringLiteral("inspect") || action == QStringLiteral("preview")) {
			return runAssetInspectCommand(QStringLiteral("asset inspect"), tokens.value(2), tokens.value(3), format);
		}
		if (action == QStringLiteral("convert") || action == QStringLiteral("image-convert")) {
			const QString outputPath = hasOption(args, QStringLiteral("--output")) ? optionValue(args, QStringLiteral("--output")) : tokens.value(3);
			return runAssetConvertCommand(QStringLiteral("asset convert"), tokens.value(2), outputPath, args, format);
		}
		if (action == QStringLiteral("audio-wav") || action == QStringLiteral("wav") || action == QStringLiteral("export-wav")) {
			const QString outputPath = hasOption(args, QStringLiteral("--output")) ? optionValue(args, QStringLiteral("--output")) : tokens.value(4);
			return runAssetAudioWavCommand(QStringLiteral("asset audio-wav"), tokens.value(2), tokens.value(3), outputPath, args, format);
		}
		if (action == QStringLiteral("find") || action == QStringLiteral("search")) {
			return runAssetTextCommand(QStringLiteral("asset find"), tokens.value(2), tokens.value(3), QString(), false, args, format);
		}
		if (action == QStringLiteral("replace") || action == QStringLiteral("replace-text")) {
			return runAssetTextCommand(QStringLiteral("asset replace"), tokens.value(2), tokens.value(3), tokens.value(4), true, args, format);
		}
	}

	if (family == QStringLiteral("map") || family == QStringLiteral("maps") || family == QStringLiteral("level")) {
		if (action == QStringLiteral("copy-surface") || action == QStringLiteral("paste-surface")) {
			const auto result = runLevelSurfaceClipboard(args);
			const auto command = QStringLiteral("map ") + action;
			if (result.exitCode != 0) { return printCliError(command, static_cast<CliExitCode>(result.exitCode), result.error, format); }
			if (format == CliOutputFormat::Json) {
				auto output = cliResultJson(command);
				for (auto it = result.payload.begin(); it != result.payload.end(); ++it) { output.insert(it.key(), it.value()); }
				printJson(output);
			} else { std::cout << text(result.lines.join(QLatin1Char('\n'))) << '\n'; }
			return 0;
		}
		if (action == QStringLiteral("paint-material") || action == QStringLiteral("sample-material")) {
			const auto result = runLevelMaterialPaint(args);
			const auto command = QStringLiteral("map ") + action;
			if (result.exitCode != 0) { return printCliError(command, static_cast<CliExitCode>(result.exitCode), result.error, format); }
			if (format == CliOutputFormat::Json) {
				auto output = cliResultJson(command);
				for (auto it = result.payload.begin(); it != result.payload.end(); ++it) { output.insert(it.key(), it.value()); }
				printJson(output);
			} else { std::cout << text(result.lines.join(QLatin1Char('\n'))) << '\n'; }
			return 0;
		}
		if (action == QStringLiteral("generate") || action == QStringLiteral("plan") || action == QStringLiteral("ai-edit")) {
			const auto result = action == QStringLiteral("generate") ? runMapGenerate(args) : action == QStringLiteral("plan") ? runMapPlan(args) : runMapAiEdit(args);
			const auto command = QStringLiteral("map ") + action;
			if (result.exitCode != 0) { return printCliError(command, static_cast<CliExitCode>(result.exitCode), result.error, format); }
			if (format == CliOutputFormat::Json) {
				auto output = cliResultJson(command);
				for (auto it = result.payload.begin(); it != result.payload.end(); ++it) { output.insert(it.key(), it.value()); }
				printJson(output);
			} else { std::cout << text(result.lines.join(QLatin1Char('\n'))) << '\n'; }
			return 0;
		}
		const QString mapPath = hasOption(args, QStringLiteral("--input")) ? optionValue(args, QStringLiteral("--input")) : tokens.value(2);
		if (action == QStringLiteral("new")) { return runMapNewCommand(args, format); }
		if (action == QStringLiteral("inspect-udmf") || action == QStringLiteral("edit-udmf")) { return runMapUdmfCommand(action, mapPath, args, format); }
		if (action == QStringLiteral("export-prefab") || action == QStringLiteral("inspect-prefab") || action == QStringLiteral("insert-prefab")) { return runMapPrefabCommand(action,mapPath,args,format); }
		if (action == QStringLiteral("place-sound")) { return runMapPlaceSoundCommand(mapPath, args, format); }
		if (action == QStringLiteral("materials")) { return runMapMaterialsCommand(mapPath, args, format); }
		if (action == QStringLiteral("align-textures")) { return runMapSurfaceCommand(mapPath, args, format); }
		if (action == QStringLiteral("recover")) { return runMapRecoverCommand(mapPath, args, format); }
		if (action == QStringLiteral("recoveries")) { return runMapRecoveriesCommand(mapPath, format); }
		if (action == QStringLiteral("inspect") || action == QStringLiteral("info") || action == QStringLiteral("preview")) {
			return runMapInspectCommand(QStringLiteral("map inspect"), mapPath, args, format);
		}
		if (action == QStringLiteral("edit") || action == QStringLiteral("set-property")) {
			return runMapEditCommand(QStringLiteral("map edit"), mapPath, args, format);
		}
		if (action == QStringLiteral("move") || action == QStringLiteral("translate")) {
			return runMapMoveCommand(QStringLiteral("map move"), mapPath, args, format);
		}
		if (action == QStringLiteral("add-brush")) {
			return runMapAddBrushCommand(QStringLiteral("map add-brush"), mapPath, args, format);
		}
		if (action == QStringLiteral("brush-components") || action == QStringLiteral("move-components")) {
			return runMapBrushComponentsCommand(QStringLiteral("map ") + action,mapPath,args,format,action == QStringLiteral("move-components"));
		}
		if (action == QStringLiteral("add-patch") || action == QStringLiteral("edit-patch")) {
			return runMapPatchCommand(QStringLiteral("map ") + action, mapPath, args, format, action == QStringLiteral("add-patch"));
		}
		if (action == QStringLiteral("stitch-patches")) { return runMapStitchPatchesCommand(mapPath,args,format); }
		if (action == QStringLiteral("cap-patch")) { return runMapCapPatchCommand(mapPath,args,format); }
		if (action == QStringLiteral("add-thing")) {
			return runMapAddThingCommand(QStringLiteral("map add-thing"), mapPath, args, format);
		}
		if (action == QStringLiteral("add-entity") || action == QStringLiteral("add")) {
			return runMapAddEntityCommand(QStringLiteral("map add-entity"), mapPath, args, format);
		}
		if (action == QStringLiteral("delete") || action == QStringLiteral("remove")) {
			return runMapDeleteCommand(QStringLiteral("map delete"), mapPath, args, format);
		}
		if (action == QStringLiteral("replace-texture") || action == QStringLiteral("retexture")) {
			return runMapReplaceTextureCommand(QStringLiteral("map replace-texture"), mapPath, args, format);
		}
		if (action == QStringLiteral("flip") || action == QStringLiteral("mirror")) {
			return runMapFlipCommand(QStringLiteral("map flip"), mapPath, args, format);
		}
		if (action == QStringLiteral("rotate") || action == QStringLiteral("turn")) {
			return runMapRotateCommand(QStringLiteral("map rotate"), mapPath, args, format);
		}
		if (action == QStringLiteral("snap") || action == QStringLiteral("snap-to-grid")) {
			return runMapSnapCommand(QStringLiteral("map snap"), mapPath, args, format);
		}
		if (action == QStringLiteral("apply-texture") || action == QStringLiteral("set-texture")) {
			return runMapApplyTextureCommand(QStringLiteral("map apply-texture"), mapPath, args, format);
		}
		if (action == QStringLiteral("split-linedef") || action == QStringLiteral("split-linedefs")) {
			return runMapLinedefEditCommand(QStringLiteral("map split-linedef"), mapPath, args, format, true);
		}
		if (action == QStringLiteral("flip-linedef") || action == QStringLiteral("flip-linedefs")) {
			return runMapLinedefEditCommand(QStringLiteral("map flip-linedef"), mapPath, args, format, false);
		}
		if (action == QStringLiteral("draw-sector") || action == QStringLiteral("add-sector")) {
			return runMapDrawSectorCommand(QStringLiteral("map draw-sector"), mapPath, args, format);
		}
		if (action == QStringLiteral("connect") || action == QStringLiteral("connect-entities")) {
			return runMapConnectCommand(QStringLiteral("map connect"), mapPath, args, format);
		}
		if (action == QStringLiteral("shift-sectors") || action == QStringLiteral("shift-sector")) {
			return runMapSectorFieldCommand(QStringLiteral("map shift-sectors"), mapPath, args, format, false);
		}
		if (action == QStringLiteral("gradient-sectors") || action == QStringLiteral("gradient-sector")) {
			return runMapSectorFieldCommand(QStringLiteral("map gradient-sectors"), mapPath, args, format, true);
		}
		if (action == QStringLiteral("make-door") || action == QStringLiteral("make-doors")) {
			return runMapMakeDoorCommand(QStringLiteral("map make-door"), mapPath, args, format);
		}
		if (action == QStringLiteral("join-sectors") || action == QStringLiteral("join-sector")) {
			return runMapJoinSectorsCommand(QStringLiteral("map join-sectors"), mapPath, args, format, false);
		}
		if (action == QStringLiteral("merge-sectors") || action == QStringLiteral("merge-sector")) {
			return runMapJoinSectorsCommand(QStringLiteral("map merge-sectors"), mapPath, args, format, true);
		}
		if (action == QStringLiteral("merge-vertices") || action == QStringLiteral("merge-vertex")) {
			return runMapMergeVerticesCommand(QStringLiteral("map merge-vertices"), mapPath, args, format);
		}
		if (action == QStringLiteral("carve") || action == QStringLiteral("subtract")) {
			return runMapCarveCommand(QStringLiteral("map carve"), mapPath, args, format);
		}
		if (action == QStringLiteral("hollow") || action == QStringLiteral("make-hollow")) {
			return runMapHollowCommand(QStringLiteral("map hollow"), mapPath, args, format);
		}
		if (action == QStringLiteral("merge-brushes")) { return runMapMergeBrushesCommand(mapPath, args, format); }
		if (action == QStringLiteral("clip") || action == QStringLiteral("split")) {
			return runMapClipCommand(QStringLiteral("map clip"), mapPath, args, format);
		}
		if (action == QStringLiteral("resize") || action == QStringLiteral("scale")) {
			return runMapResizeCommand(QStringLiteral("map resize"), mapPath, args, format);
		}
		if (action == QStringLiteral("duplicate") || action == QStringLiteral("copy") || action == QStringLiteral("clone")) {
			return runMapDuplicateCommand(QStringLiteral("map duplicate"), mapPath, args, format);
		}
		if (action == QStringLiteral("paste")) { return runMapPasteCommand(mapPath, args, format); }
		if (action == QStringLiteral("compile-plan") || action == QStringLiteral("plan") || action == QStringLiteral("compiler-plan")) {
			return runMapCompilePlanCommand(QStringLiteral("map compile-plan"), mapPath, args, format);
		}
		if (action == QStringLiteral("render") || action == QStringLiteral("image") || action == QStringLiteral("svg")) {
			return runMapRenderCommand(QStringLiteral("map render"), mapPath, args, format);
		}
		if (action == QStringLiteral("textures") || action == QStringLiteral("materials") || action == QStringLiteral("audit-textures")) {
			return runMapTexturesCommand(QStringLiteral("map textures"), mapPath, args, format);
		}
		if (action == QStringLiteral("dependencies")) {
			return runMapDependenciesCommand(QStringLiteral("map dependencies"), mapPath, args, format);
		}
		if (action == QStringLiteral("place-model")) { return runMapPlaceModelCommand(mapPath, args, format); }
		if (action == QStringLiteral("find") || action == QStringLiteral("query") || action == QStringLiteral("search")) {
			return runMapFindCommand(QStringLiteral("map find"), mapPath, args, format);
		}
	}

	if (family == QStringLiteral("install") || family == QStringLiteral("installation") || family == QStringLiteral("installations")) {
		if (action == QStringLiteral("add") || action == QStringLiteral("create")) {
			const QString rootPath = hasOption(args, QStringLiteral("--root")) ? optionValue(args, QStringLiteral("--root")) : tokens.value(2);
			return runInstallAddCommand(QStringLiteral("install add"), rootPath, args, format);
		}
		if (action == QStringLiteral("select") || action == QStringLiteral("use")) {
			const QString id = hasOption(args, QStringLiteral("--installation")) ? optionValue(args, QStringLiteral("--installation")) : tokens.value(2);
			return runInstallSelectCommand(QStringLiteral("install select"), id, format);
		}
		if (action == QStringLiteral("validate") || action == QStringLiteral("check")) {
			const QString id = hasOption(args, QStringLiteral("--installation")) ? optionValue(args, QStringLiteral("--installation")) : tokens.value(2);
			return runInstallValidateCommand(QStringLiteral("install validate"), id, format);
		}
		if (action == QStringLiteral("remove") || action == QStringLiteral("forget") || action == QStringLiteral("delete")) {
			const QString id = hasOption(args, QStringLiteral("--installation")) ? optionValue(args, QStringLiteral("--installation")) : tokens.value(2);
			return runInstallRemoveCommand(QStringLiteral("install remove"), id, args, format);
		}
	}

	if (family == QStringLiteral("entity") || family == QStringLiteral("entities") || family == QStringLiteral("entitydef")) {
		if (action == QStringLiteral("definitions") || action == QStringLiteral("classes") || action == QStringLiteral("list")) {
			return runEntityDefinitionsCommand(QStringLiteral("entity definitions"), tokens, args, format);
		}
		if (action == QStringLiteral("validate") || action == QStringLiteral("check")) {
			const QString entityMapPath = hasOption(args, QStringLiteral("--input")) ? optionValue(args, QStringLiteral("--input")) : tokens.value(2);
			return runEntityValidateCommand(QStringLiteral("entity validate"), entityMapPath, tokens, args, format);
		}
	}

	if (family == QStringLiteral("model") || family == QStringLiteral("models") || family == QStringLiteral("mesh")) {
		if (action == QStringLiteral("collision")) {
			const auto result = runModelCollision(args);
			if (result.exitCode != 0) { return printCliError(QStringLiteral("model collision"), static_cast<CliExitCode>(result.exitCode), result.error, format); }
			if (format == CliOutputFormat::Json) {
				auto output = cliResultJson(QStringLiteral("model collision"));
				for (auto it = result.payload.begin(); it != result.payload.end(); ++it) { output.insert(it.key(), it.value()); }
				printJson(output);
			} else { std::cout << text(result.lines.join(QLatin1Char('\n'))) << '\n'; }
			return 0;
		}
		if (action == QStringLiteral("assembly")) {
			const auto result = runModelAssembly(args);
			if (result.exitCode != 0) {
				if (format == CliOutputFormat::Json && !result.payload.isEmpty()) {
					auto output = cliResultJson(QStringLiteral("model assembly"), static_cast<CliExitCode>(result.exitCode));
					for (auto it = result.payload.begin(); it != result.payload.end(); ++it) { output.insert(it.key(), it.value()); }
					output.insert(QStringLiteral("message"), result.error); printJson(output);
					return result.exitCode;
				}
				return printCliError(QStringLiteral("model assembly"), static_cast<CliExitCode>(result.exitCode), result.error, format);
			}
			if (format == CliOutputFormat::Json) {
				auto output = cliResultJson(QStringLiteral("model assembly"));
				for (auto it = result.payload.begin(); it != result.payload.end(); ++it) { output.insert(it.key(), it.value()); }
				printJson(output);
			} else { std::cout << text(result.lines.join(QLatin1Char('\n'))) << '\n'; }
			return 0;
		}
		if (action == QStringLiteral("mdl")) {
			const auto result = runModelMdl(args);
			if (result.exitCode != 0) { return printCliError(QStringLiteral("model mdl"), static_cast<CliExitCode>(result.exitCode), result.error, format); }
			if (format == CliOutputFormat::Json) {
				auto output = cliResultJson(QStringLiteral("model mdl"));
				for (auto it = result.payload.begin(); it != result.payload.end(); ++it) { output.insert(it.key(), it.value()); }
				printJson(output);
			} else { std::cout << text(result.lines.join(QLatin1Char('\n'))) << '\n'; }
			return 0;
		}
		if (action == QStringLiteral("slots")) {
			const auto result = runModelMaterialSlots(args);
			if (result.exitCode != 0) { return printCliError(QStringLiteral("model slots"), static_cast<CliExitCode>(result.exitCode), result.error, format); }
			if (format == CliOutputFormat::Json) {
				auto output = cliResultJson(QStringLiteral("model slots"));
				for (auto it = result.payload.begin(); it != result.payload.end(); ++it) { output.insert(it.key(), it.value()); }
				printJson(output);
			} else { std::cout << text(result.lines.join(QLatin1Char('\n'))) << '\n'; }
			return 0;
		}
		if (action == QStringLiteral("surfaces")) {
			const auto result = runModelSurfaces(args);
			if (result.exitCode != 0) { return printCliError(QStringLiteral("model surfaces"), static_cast<CliExitCode>(result.exitCode), result.error, format); }
			if (format == CliOutputFormat::Json) {
				auto output = cliResultJson(QStringLiteral("model surfaces"));
				for (auto it = result.payload.begin(); it != result.payload.end(); ++it) { output.insert(it.key(), it.value()); }
				printJson(output);
			} else { std::cout << text(result.lines.join(QLatin1Char('\n'))) << '\n'; }
			return 0;
		}
		if (action == QStringLiteral("skin")) {
			const auto result = runModelSkinBindings(args);
			if (result.exitCode != 0) { return printCliError(QStringLiteral("model skin"), static_cast<CliExitCode>(result.exitCode), result.error, format); }
			if (format == CliOutputFormat::Json) {
				auto output = cliResultJson(QStringLiteral("model skin"));
				for (auto it = result.payload.begin(); it != result.payload.end(); ++it) { output.insert(it.key(), it.value()); }
				printJson(output);
			} else { std::cout << text(result.lines.join(QLatin1Char('\n'))) << '\n'; }
			return 0;
		}
		if (action == QStringLiteral("materials")) { return runModelMaterialsCommand(hasOption(args, QStringLiteral("--input")) ? optionValue(args, QStringLiteral("--input")) : tokens.value(2), args, format); }
		if (action == QStringLiteral("topology")) { return runModelTopologyCommand(hasOption(args, QStringLiteral("--input")) ? optionValue(args, QStringLiteral("--input")) : tokens.value(2), args, format); }
		if (action == QStringLiteral("repair-import")) {
			const auto result = runModelImportRepair(args);
			if (result.exitCode != 0) { return printCliError(QStringLiteral("model repair-import"), static_cast<CliExitCode>(result.exitCode), result.error, format); }
			if (format == CliOutputFormat::Json) {
				auto output = cliResultJson(QStringLiteral("model repair-import"));
				for (auto it = result.payload.begin(); it != result.payload.end(); ++it) { output.insert(it.key(), it.value()); }
				printJson(output);
			} else { std::cout << text(result.lines.join(QLatin1Char('\n'))) << '\n'; }
			return 0;
		}
		if (action == QStringLiteral("intersections")) {
			const auto result = runModelIntersections(args);
			if (result.exitCode != 0) { return printCliError(QStringLiteral("model intersections"), static_cast<CliExitCode>(result.exitCode), result.error, format); }
			if (format == CliOutputFormat::Json) {
				auto output = cliResultJson(QStringLiteral("model intersections"));
				for (auto it = result.payload.begin(); it != result.payload.end(); ++it) { output.insert(it.key(), it.value()); }
				printJson(output);
			} else { std::cout << text(result.lines.join(QLatin1Char('\n'))) << '\n'; }
			return 0;
		}
		if (action == QStringLiteral("uv")) { return runModelTopologyCommand(hasOption(args, QStringLiteral("--input")) ? optionValue(args, QStringLiteral("--input")) : tokens.value(2), args, format, true); }
		if (action == QStringLiteral("tags")) { return runModelTagsCommand(hasOption(args, QStringLiteral("--input")) ? optionValue(args, QStringLiteral("--input")) : tokens.value(2), args, format); }
		if (action == QStringLiteral("animations")) { return runModelAnimationsCommand(hasOption(args, QStringLiteral("--input")) ? optionValue(args, QStringLiteral("--input")) : tokens.value(2), args, format); }
		if (action == QStringLiteral("recoveries")) { return runModelRecoveriesCommand(args, format); }
		if (action == QStringLiteral("recover")) { return runModelRecoverCommand(hasOption(args, QStringLiteral("--input")) ? optionValue(args, QStringLiteral("--input")) : tokens.value(2), args, format); }
		if (action == QStringLiteral("build")) { return runModelBuildCommand(hasOption(args, QStringLiteral("--input")) ? optionValue(args, QStringLiteral("--input")) : tokens.value(2), args, format); }
		if (action == QStringLiteral("import") || action == QStringLiteral("edit")) { return runModelSourceCommand(action, hasOption(args, QStringLiteral("--input")) ? optionValue(args, QStringLiteral("--input")) : tokens.value(2), args, format); }
		const QString modelPackage = hasOption(args, QStringLiteral("--package"))
			? optionValue(args, QStringLiteral("--package"))
			: (hasOption(args, QStringLiteral("--file")) ? optionValue(args, QStringLiteral("--file")) : tokens.value(2));
		const QString modelEntry = hasOption(args, QStringLiteral("--entry"))
			? optionValue(args, QStringLiteral("--entry"))
			: ((hasOption(args, QStringLiteral("--package")) || hasOption(args, QStringLiteral("--file"))) ? tokens.value(2) : tokens.value(3));
		if (action == QStringLiteral("inspect") || action == QStringLiteral("info")) {
			return runModelInspectCommand(QStringLiteral("model inspect"), modelPackage, modelEntry, args, format);
		}
		if (action == QStringLiteral("export") || action == QStringLiteral("obj")) {
			return runModelExportCommand(QStringLiteral("model export"), modelPackage, modelEntry, args, format);
		}
	}

	if (family == QStringLiteral("bsp") || family == QStringLiteral("artifact") || family == QStringLiteral("artifacts")) {
		const QString bspPath = hasOption(args, QStringLiteral("--input")) ? optionValue(args, QStringLiteral("--input")) : tokens.value(2);
		if (action == QStringLiteral("inspect") || action == QStringLiteral("info")) {
			return runBspInspectCommand(QStringLiteral("bsp inspect"), bspPath, args, format);
		}
	}

	if (family == QStringLiteral("build") || family == QStringLiteral("pipeline") || family == QStringLiteral("pipelines")) {
		if (action == QStringLiteral("prepare") || action == QStringLiteral("run-prepared") || action == QStringLiteral("artifacts") || action == QStringLiteral("publish-prepared") || action == QStringLiteral("deploy-plan") || action == QStringLiteral("deploy-prepared")) {
			const auto result = (action == QStringLiteral("deploy-plan") || action == QStringLiteral("deploy-prepared")) ? runLevelBuildDeploymentCommand(args) : (action == QStringLiteral("artifacts") || action == QStringLiteral("publish-prepared")) ? runLevelBuildArtifactsCommand(args) : runLevelBuildWorkspaceCommand(args);
			if (format == CliOutputFormat::Json) {
				auto output = cliResultJson(QStringLiteral("build ") + action, static_cast<CliExitCode>(result.exitCode));
				for (auto it = result.payload.begin(); it != result.payload.end(); ++it) { output.insert(it.key(), it.value()); }
				if (!result.error.isEmpty()) { output.insert(QStringLiteral("message"), result.error); }
				printJson(output);
			} else {
				QTextStream stream(result.error.isEmpty() ? stdout : stderr);
				stream << (result.error.isEmpty() ? result.lines.join(QLatin1Char('\n')) : result.error) << Qt::endl;
			}
			return result.exitCode;
		}
		if (action == QStringLiteral("list") || action == QStringLiteral("pipelines")) {
			return runBuildListCommand(QStringLiteral("build list"), format);
		}
		const QString pipelineId = hasOption(args, QStringLiteral("--pipeline")) ? optionValue(args, QStringLiteral("--pipeline")) : tokens.value(2);
		if (action == QStringLiteral("plan")) {
			return runBuildPlanCommand(QStringLiteral("build plan"), pipelineId, args, format);
		}
		if (action == QStringLiteral("run") || action == QStringLiteral("compile")) {
			return runBuildRunCommand(QStringLiteral("build run"), pipelineId, args, format);
		}
	}

	if (family == QStringLiteral("launch") || family == QStringLiteral("game") || family == QStringLiteral("play")) {
		if (action == QStringLiteral("plan")) {
			return runLaunchCommand(QStringLiteral("launch plan"), args, false, format);
		}
		if (action == QStringLiteral("run") || action == QStringLiteral("start")) {
			return runLaunchCommand(QStringLiteral("launch run"), args, true, format);
		}
	}

	if (family == QStringLiteral("texture") || family == QStringLiteral("textures") || family == QStringLiteral("image")) {
		if (action == QStringLiteral("profiles")) { return runTextureProfilesCommand(format); }
		if (action == QStringLiteral("generate") || action == QStringLiteral("derive")) {
			const auto result = action == QStringLiteral("generate") ? runTextureGenerate(args) : runTextureDerive(args);
			const auto command = QStringLiteral("texture ") + action;
			if (result.exitCode != 0) { return printCliError(command, static_cast<CliExitCode>(result.exitCode), result.error, format); }
			if (format == CliOutputFormat::Json) {
				auto output = cliResultJson(command);
				for (auto it = result.payload.begin(); it != result.payload.end(); ++it) { output.insert(it.key(), it.value()); }
				printJson(output);
			} else { std::cout << text(result.lines.join(QLatin1Char('\n'))) << '\n'; }
			return 0;
		}
		// Preserve the old two-positional-argument package decode alias.
		if (action == QStringLiteral("stage") || action == QStringLiteral("validate") || (action == QStringLiteral("export") && (tokens.value(3).isEmpty() || hasOption(args, QStringLiteral("--profile")) || hasOption(args, QStringLiteral("--export-options"))))) {
			const auto input = hasOption(args, QStringLiteral("--entry")) ? optionValue(args, QStringLiteral("--entry")) : (hasOption(args, QStringLiteral("--input")) ? optionValue(args, QStringLiteral("--input")) : tokens.value(2));
			return runTextureExportCommand(QStringLiteral("texture ") + action, input, args, action == QStringLiteral("validate"), format);
		}
		if (action == QStringLiteral("recoveries")) { return runTextureRecoveriesCommand(tokens.value(2), format); }
		if (action == QStringLiteral("recover")) { return runTextureRecoverCommand(hasOption(args, QStringLiteral("--input")) ? optionValue(args, QStringLiteral("--input")) : tokens.value(2), args, format); }
		if (action == QStringLiteral("inspect")) { return runTextureInspectCommand(hasOption(args, QStringLiteral("--input")) ? optionValue(args, QStringLiteral("--input")) : tokens.value(2), format); }
		if (action == QStringLiteral("create") || action == QStringLiteral("edit")) {
			const QString input = hasOption(args, QStringLiteral("--entry")) ? optionValue(args, QStringLiteral("--entry")) :
				(hasOption(args, QStringLiteral("--input")) ? optionValue(args, QStringLiteral("--input")) : tokens.value(2));
			return runTextureEditCommand(QStringLiteral("texture ") + action, input, action == QStringLiteral("create"), args, format);
		}
		if (action == QStringLiteral("palette") || action == QStringLiteral("palettes")) {
			const QString packagePath = hasOption(args, QStringLiteral("--package")) ? optionValue(args, QStringLiteral("--package")) : tokens.value(2);
			return runTexturePaletteCommand(QStringLiteral("texture palette"), packagePath, args, format);
		}
		if (action == QStringLiteral("decode") || action == QStringLiteral("export") || action == QStringLiteral("preview")) {
			const QString packagePath = hasOption(args, QStringLiteral("--package")) ? optionValue(args, QStringLiteral("--package")) : tokens.value(2);
			const QString entryPath = hasOption(args, QStringLiteral("--entry"))
				? optionValue(args, QStringLiteral("--entry"))
				: (hasOption(args, QStringLiteral("--package")) ? tokens.value(2) : tokens.value(3));
			return runTextureDecodeCommand(QStringLiteral("texture decode"), packagePath, entryPath, args, format);
		}
	}

	if (family == QStringLiteral("shader") || family == QStringLiteral("shaders")) {
		const QString shaderPath = hasOption(args, QStringLiteral("--input")) ? optionValue(args, QStringLiteral("--input")) : tokens.value(2);
		if (action == QStringLiteral("inspect") || action == QStringLiteral("info") || action == QStringLiteral("validate")) {
			return runShaderInspectCommand(QStringLiteral("shader inspect"), shaderPath, args, format);
		}
		if (action == QStringLiteral("set-stage") || action == QStringLiteral("edit-stage") || action == QStringLiteral("edit")) {
			return runShaderSetStageCommand(QStringLiteral("shader set-stage"), shaderPath, args, format);
		}
	}

	if (family == QStringLiteral("sprite") || family == QStringLiteral("sprites")) {
		if (action == QStringLiteral("plan") || action == QStringLiteral("create") || action == QStringLiteral("sequence")) {
			return runSpritePlanCommand(QStringLiteral("sprite plan"), args, format);
		}
	}

	if (family == QStringLiteral("code") || family == QStringLiteral("ide") || family == QStringLiteral("source")) {
		if (action == QStringLiteral("language-server")) { return runCodeLanguageServerCommand(tokens.value(2), args, format); }
		if (action == QStringLiteral("files")) { return runCodeFilesCommand(tokens.value(2), args, format); }
		if (action == QStringLiteral("recoveries")) { return runCodeRecoveriesCommand(args, format); }
		if (action == QStringLiteral("text-create") || action == QStringLiteral("text-save-as") || action == QStringLiteral("text-recover")) {
			return runCodeTextWriteCommand(action, tokens.value(2), args, format);
		}
		if (action == QStringLiteral("text-info") || action == QStringLiteral("text-save")) {
			return runCodeTextCommand(action, tokens.value(2), args, format);
		}
		if (action == QStringLiteral("index") || action == QStringLiteral("tree") || action == QStringLiteral("symbols")) {
			return runCodeIndexCommand(QStringLiteral("code index"), tokens.value(2), args, format);
		}
	}

	if (family == QStringLiteral("localization") || family == QStringLiteral("locale") || family == QStringLiteral("translation") || family == QStringLiteral("translations")) {
		if (action == QStringLiteral("targets") || action == QStringLiteral("list")) {
			return runLocalizationReportCommand(QStringLiteral("localization targets"), args, format, true);
		}
		if (action == QStringLiteral("report") || action == QStringLiteral("smoke") || action == QStringLiteral("audit")) {
			return runLocalizationReportCommand(QStringLiteral("localization report"), args, format, false);
		}
	}

	if (family == QStringLiteral("diagnostics") || family == QStringLiteral("diagnostic") || family == QStringLiteral("support")) {
		if (action == QStringLiteral("bundle") || action == QStringLiteral("collect") || action == QStringLiteral("report")) {
			return runDiagnosticsBundleCommand(QStringLiteral("diagnostics bundle"), args, format);
		}
		if (action == QStringLiteral("crashes") || action == QStringLiteral("crash-reports")) {
			return runDiagnosticsCrashesCommand(QStringLiteral("diagnostics crashes"), format);
		}
	}

	if (family == QStringLiteral("extension") || family == QStringLiteral("extensions") || family == QStringLiteral("plugin") || family == QStringLiteral("plugins")) {
		if (action == QStringLiteral("discover") || action == QStringLiteral("list") || action == QStringLiteral("ls")) {
			return runExtensionDiscoverCommand(QStringLiteral("extension discover"), tokens.value(2), args, format);
		}
		if (action == QStringLiteral("inspect") || action == QStringLiteral("info")) {
			return runExtensionInspectCommand(QStringLiteral("extension inspect"), tokens.value(2), format);
		}
		if (action == QStringLiteral("run") || action == QStringLiteral("plan")) {
			return runExtensionRunCommand(QStringLiteral("extension run"), tokens.value(2), tokens.value(3), args, format);
		}
	}

	if (family == QStringLiteral("compiler")) {
		if (action == QStringLiteral("list") || action == QStringLiteral("registry")) {
			return runCompilerListCommand(QStringLiteral("compiler list"), format);
		}
		if (action == QStringLiteral("profiles") || action == QStringLiteral("profile-list") || action == QStringLiteral("list-profiles")) {
			return runCompilerProfilesCommand(QStringLiteral("compiler profiles"), format);
		}
		if (action == QStringLiteral("set-path") || action == QStringLiteral("override-path")) {
			return runCompilerSetPathCommand(QStringLiteral("compiler set-path"), tokens.value(2), args, format);
		}
		if (action == QStringLiteral("clear-path") || action == QStringLiteral("remove-path")) {
			return runCompilerClearPathCommand(QStringLiteral("compiler clear-path"), tokens.value(2), format);
		}
		if (action == QStringLiteral("plan")) {
			return runCompilerPlanCommand(QStringLiteral("compiler plan"), tokens.value(2), tokens.value(3), args, format, false);
		}
		if (action == QStringLiteral("manifest")) {
			return runCompilerPlanCommand(QStringLiteral("compiler manifest"), tokens.value(2), tokens.value(3), args, format, true);
		}
		if (action == QStringLiteral("run")) {
			return runCompilerRunCommand(QStringLiteral("compiler run"), tokens.value(2), tokens.value(3), args, format);
		}
		if (action == QStringLiteral("rerun")) {
			return runCompilerRerunCommand(QStringLiteral("compiler rerun"), tokens.value(2), args, format);
		}
		if (action == QStringLiteral("copy-command") || action == QStringLiteral("command-line")) {
			return runCompilerCopyCommandCommand(QStringLiteral("compiler copy-command"), tokens.value(2), args, format);
		}
	}

	if (family == QStringLiteral("editor") || family == QStringLiteral("editors") || family == QStringLiteral("profile") || family == QStringLiteral("profiles")) {
		if (action == QStringLiteral("layout")) { return runEditorLayoutCommand(args, format); }
		if (action == QStringLiteral("view-links")) { return runEditorViewLinksCommand(args, format); }
		if (action == QStringLiteral("gestures")) {
			const auto result = runLevelGestures(args);
			const auto command = QStringLiteral("editor gestures");
			if (result.exitCode != 0) { return printCliError(command, static_cast<CliExitCode>(result.exitCode), result.error, format); }
			if (format == CliOutputFormat::Json) {
				auto output = cliResultJson(command);
				for (auto it = result.payload.begin(); it != result.payload.end(); ++it) { output.insert(it.key(), it.value()); }
				printJson(output);
			} else { std::cout << text(result.lines.join(QLatin1Char('\n'))) << '\n'; }
			return 0;
		}
		if (action == QStringLiteral("bookmarks")) {
			const auto result = runLevelBookmarks(args);
			const auto command = QStringLiteral("editor bookmarks");
			if (result.exitCode != 0) { return printCliError(command, static_cast<CliExitCode>(result.exitCode), result.error, format); }
			if (format == CliOutputFormat::Json) {
				auto output = cliResultJson(command);
				for (auto it = result.payload.begin(); it != result.payload.end(); ++it) { output.insert(it.key(), it.value()); }
				printJson(output);
			} else { std::cout << text(result.lines.join(QLatin1Char('\n'))) << '\n'; }
			return 0;
		}
		if (action == QStringLiteral("scene")) {
			const auto result = runLevelScene(args);
			const auto command = QStringLiteral("editor scene");
			if (result.exitCode != 0) { return printCliError(command, static_cast<CliExitCode>(result.exitCode), result.error, format); }
			if (format == CliOutputFormat::Json) {
				auto output = cliResultJson(command);
				for (auto it = result.payload.begin(); it != result.payload.end(); ++it) { output.insert(it.key(), it.value()); }
				printJson(output);
			} else { std::cout << text(result.lines.join(QLatin1Char('\n'))) << '\n'; }
			return 0;
		}
		if (action == QStringLiteral("profiles") || action == QStringLiteral("list") || action == QStringLiteral("ls")) {
			return runEditorProfilesCommand(QStringLiteral("editor profiles"), format);
		}
		if (action == QStringLiteral("controls") || action == QStringLiteral("cheatsheet")) {
			return runEditorControlsCommand(QStringLiteral("editor controls"), tokens.value(2), format);
		}
		if (action == QStringLiteral("current") || action == QStringLiteral("selected")) {
			return runEditorCurrentCommand(QStringLiteral("editor current"), format);
		}
		if (action == QStringLiteral("select") || action == QStringLiteral("set")) {
			return runEditorSelectCommand(QStringLiteral("editor select"), tokens.value(2), format);
		}
		if (action == QStringLiteral("keys") || action == QStringLiteral("shortcuts")) {
			return runEditorKeysCommand(QStringLiteral("editor keys"), args, format);
		}
	}

	if (family == QStringLiteral("ai") || family == QStringLiteral("automation")) {
		if (action == QStringLiteral("image") || action == QStringLiteral("draw")) {
			const auto result = runAiImage(args);
			const auto command = QStringLiteral("ai image");
			if (result.exitCode != 0) { return printCliError(command, static_cast<CliExitCode>(result.exitCode), result.error, format); }
			if (format == CliOutputFormat::Json) {
				auto output = cliResultJson(command);
				for (auto it = result.payload.begin(); it != result.payload.end(); ++it) { output.insert(it.key(), it.value()); }
				printJson(output);
			} else { std::cout << text(result.lines.join(QLatin1Char('\n'))) << '\n'; }
			return 0;
		}
		if (action == QStringLiteral("status") || action == QStringLiteral("settings")) {
			return runAiStatusCommand(QStringLiteral("ai status"), format);
		}
		if (action == QStringLiteral("connectors") || action == QStringLiteral("connector-list") || action == QStringLiteral("providers")) {
			return runAiConnectorsCommand(QStringLiteral("ai connectors"), format);
		}
		if (action == QStringLiteral("tools") || action == QStringLiteral("tool-list")) {
			return runAiToolsCommand(QStringLiteral("ai tools"), format);
		}
		if (action == QStringLiteral("explain-log") || action == QStringLiteral("explain")) {
			return runAiExplainLogCommand(QStringLiteral("ai explain-log"), args, format);
		}
		if (action == QStringLiteral("propose-command") || action == QStringLiteral("command")) {
			return runAiProposeCommandCommand(QStringLiteral("ai propose-command"), args, format);
		}
		if (action == QStringLiteral("propose-manifest") || action == QStringLiteral("project-draft") || action == QStringLiteral("manifest")) {
			return runAiProposeManifestCommand(QStringLiteral("ai propose-manifest"), tokens.value(2), args, format);
		}
		if (action == QStringLiteral("package-deps") || action == QStringLiteral("dependencies")) {
			return runAiPackageDepsCommand(QStringLiteral("ai package-deps"), tokens.value(2), args, format);
		}
		if (action == QStringLiteral("cli-command") || action == QStringLiteral("generate-command")) {
			return runAiCliCommandCommand(QStringLiteral("ai cli-command"), args, format);
		}
		if (action == QStringLiteral("fix-plan") || action == QStringLiteral("retry-plan")) {
			return runAiFixPlanCommand(QStringLiteral("ai fix-plan"), args, format);
		}
		if (action == QStringLiteral("asset-request") || action == QStringLiteral("asset")) {
			return runAiAssetRequestCommand(QStringLiteral("ai asset-request"), args, format);
		}
		if (action == QStringLiteral("compare") || action == QStringLiteral("compare-providers")) {
			return runAiCompareCommand(QStringLiteral("ai compare"), args, format);
		}
		if (action == QStringLiteral("shader-scaffold") || action == QStringLiteral("shader") || action == QStringLiteral("prompt-to-shader")) {
			return runAiShaderScaffoldCommand(QStringLiteral("ai shader-scaffold"), args, format);
		}
		if (action == QStringLiteral("entity-snippet") || action == QStringLiteral("entity") || action == QStringLiteral("prompt-to-entity")) {
			return runAiEntitySnippetCommand(QStringLiteral("ai entity-snippet"), args, format);
		}
		if (action == QStringLiteral("package-plan") || action == QStringLiteral("package-validation") || action == QStringLiteral("prompt-to-package-validation")) {
			return runAiPackagePlanCommand(QStringLiteral("ai package-plan"), args, format);
		}
		if (action == QStringLiteral("batch-recipe") || action == QStringLiteral("batch-conversion") || action == QStringLiteral("prompt-to-batch-conversion")) {
			return runAiBatchRecipeCommand(QStringLiteral("ai batch-recipe"), args, format);
		}
		if (action == QStringLiteral("review") || action == QStringLiteral("proposal-review")) {
			return runAiReviewCommand(QStringLiteral("ai review"), args, format);
		}
		if (action == QStringLiteral("ask") || action == QStringLiteral("question")) {
			return runAiAskCommand(QStringLiteral("ai ask"), args, format);
		}
		if (action == QStringLiteral("test-connection") || action == QStringLiteral("ping")) {
			return runAiTestConnectionCommand(QStringLiteral("ai test-connection"), args, format);
		}
	}

	return printCliError(commandName, CliExitCode::Usage, QStringLiteral("Unknown VibeStudio CLI subcommand. Run --cli --help."), format);
}

} // namespace

int runImpl(const QStringList& args)
{
	QStringList requestedOptions = args.mid(1);
	requestedOptions.removeAll(QStringLiteral("--cli"));
	requestedOptions.removeAll(QStringLiteral("--json"));
	requestedOptions.removeAll(QStringLiteral("--quiet"));
	requestedOptions.removeAll(QStringLiteral("--verbose"));

	if (hasOption(args, "--version")) {
		std::cout << "VibeStudio " << text(versionString()) << "\n";
		return 0;
	}
	if (hasOption(args, "--about") || hasOption(args, "--credits")) {
		return runAboutCommand(hasOption(args, "--credits") ? QStringLiteral("--credits") : QStringLiteral("--about"), outputFormat(args));
	}
	if (hasOption(args, "--help") || requestedOptions.isEmpty()) {
		printHelp();
		return 0;
	}
	if (hasOption(args, "--exit-codes")) {
		printExitCodes(outputFormat(args), QStringLiteral("cli exit-codes"));
		return 0;
	}
	const int subcommandResult = runSubcommand(args);
	if (subcommandResult >= 0) {
		return subcommandResult;
	}
	if (hasOption(args, "--studio-report")) {
		printStudioReport();
		return 0;
	}
	if (hasOption(args, "--compiler-report")) {
		printCompilerReport();
		return 0;
	}
	if (hasOption(args, "--compiler-registry")) {
		return runCompilerListCommand(QStringLiteral("--compiler-registry"), outputFormat(args));
	}
	if (hasOption(args, "--platform-report")) {
		printPlatformReport();
		return 0;
	}
	if (hasOption(args, "--project-init")) {
		return runProjectInitCommand(QStringLiteral("--project-init"), optionValue(args, QStringLiteral("--project-init")), args, outputFormat(args));
	}
	if (hasOption(args, "--project-info")) {
		return runProjectInfoCommand(QStringLiteral("--project-info"), optionValue(args, QStringLiteral("--project-info")), outputFormat(args));
	}
	if (hasOption(args, "--project-validate")) {
		return runProjectValidateCommand(QStringLiteral("--project-validate"), optionValue(args, QStringLiteral("--project-validate")), outputFormat(args));
	}
	if (hasOption(args, "--operation-states")) {
		printOperationStates();
		return 0;
	}
	if (hasOption(args, "--ui-primitives")) {
		printUiPrimitives();
		return 0;
	}
	if (hasOption(args, "--ui-semantics")) {
		return runUiSemanticsCommand(QStringLiteral("--ui-semantics"), outputFormat(args));
	}
	if (hasOption(args, "--package-formats")) {
		printPackageFormats();
		return 0;
	}
	if (hasOption(args, "--check-package-path")) {
		const QString path = optionValue(args, "--check-package-path");
		if (path.isEmpty()) {
			std::cerr << "--check-package-path requires a virtual package path.\n";
			return 2;
		}
		printPackagePathCheck(path);
		return 0;
	}
	if (hasOption(args, "--info")) {
		return runPackageInfoCommand(QStringLiteral("--info"), optionValue(args, QStringLiteral("--info")), outputFormat(args));
	}
	if (hasOption(args, "--list")) {
		return runPackageListCommand(QStringLiteral("--list"), optionValue(args, QStringLiteral("--list")), outputFormat(args));
	}
	if (hasOption(args, "--preview-package") || hasOption(args, "--preview-entry")) {
		return runPackagePreviewCommand(QStringLiteral("--preview-package"), optionValue(args, QStringLiteral("--preview-package")), optionValue(args, QStringLiteral("--preview-entry")), outputFormat(args));
	}
	if (hasOption(args, "--extract")) {
		const QStringList entries = packageExtractionEntriesFromArgs(args);
		return runPackageExtractCommand(
			QStringLiteral("--extract"),
			optionValue(args, QStringLiteral("--extract")),
			optionValue(args, QStringLiteral("--output")),
			entries,
			hasOption(args, QStringLiteral("--extract-all")) || entries.isEmpty(),
			hasOption(args, QStringLiteral("--dry-run")),
			hasOption(args, QStringLiteral("--overwrite")),
			args, outputFormat(args));
	}
	if (hasOption(args, "--validate-package")) {
		return runPackageValidateCommand(QStringLiteral("--validate-package"), optionValue(args, QStringLiteral("--validate-package")), args, outputFormat(args));
	}
	if (hasOption(args, "--setup-start") || hasOption(args, "--setup-step") || hasOption(args, "--setup-next") || hasOption(args, "--setup-skip") || hasOption(args, "--setup-complete") || hasOption(args, "--setup-reset")) {
		StudioSettings settings;
		if (hasOption(args, "--setup-reset")) {
			settings.resetSetup();
		}
		if (hasOption(args, "--setup-start")) {
			settings.startOrResumeSetup(settings.setupProgress().currentStep);
		}
		if (hasOption(args, "--setup-step")) {
			const QString stepId = optionValue(args, "--setup-step");
			if (stepId.isEmpty() || setupStepId(setupStepFromId(stepId)) != normalizedOptionId(stepId)) {
				QStringList ids;
				for (SetupStep step : setupSteps()) {
					ids.push_back(setupStepId(step));
				}
				std::cerr << "--setup-step requires one of: " << text(ids.join(", ")) << "\n";
				return 2;
			}
			settings.startOrResumeSetup(setupStepFromId(stepId));
		}
		if (hasOption(args, "--setup-next")) {
			settings.advanceSetup();
		}
		if (hasOption(args, "--setup-skip")) {
			settings.skipSetup();
		}
		if (hasOption(args, "--setup-complete")) {
			settings.completeSetup();
		}
		settings.sync();
		if (settings.status() != QSettings::NoError) {
			std::cerr << "Failed to save setup progress: " << text(settingsStatusText(settings.status())) << "\n";
			return 1;
		}
		printSetupSummary(settings.setupSummary());
		return 0;
	}
	if (hasOption(args, "--set-locale") || hasOption(args, "--set-theme") || hasOption(args, "--set-text-scale") || hasOption(args, "--set-density") || hasOption(args, "--set-editor-profile") || hasOption(args, "--set-reduced-motion") || hasOption(args, "--set-tts")) {
		StudioSettings settings;
		AccessibilityPreferences preferences = settings.accessibilityPreferences();
		QString editorProfileId = settings.selectedEditorProfileId();

		if (hasOption(args, "--set-locale")) {
			const QString value = optionValue(args, "--set-locale");
			if (!localeOptionIsSupported(value)) {
				std::cerr << "--set-locale requires one of: " << text(supportedLocaleNames().join(", ")) << "\n";
				return 2;
			}
			preferences.localeName = normalizedLocaleName(value);
		}
		if (hasOption(args, "--set-theme")) {
			const QString value = normalizedOptionId(optionValue(args, "--set-theme"));
			if (!themeIds().contains(value)) {
				std::cerr << "--set-theme requires one of: " << text(themeIds().join(", ")) << "\n";
				return 2;
			}
			preferences.theme = themeFromId(value);
		}
		if (hasOption(args, "--set-text-scale")) {
			bool ok = false;
			const int value = optionValue(args, "--set-text-scale").toInt(&ok);
			if (!ok || value < StudioSettings::kMinimumTextScalePercent || value > StudioSettings::kMaximumTextScalePercent) {
				std::cerr << "--set-text-scale requires a value from 100 to 200.\n";
				return 2;
			}
			preferences.textScalePercent = value;
		}
		if (hasOption(args, "--set-density")) {
			const QString value = normalizedOptionId(optionValue(args, "--set-density"));
			if (!densityIds().contains(value)) {
				std::cerr << "--set-density requires one of: " << text(densityIds().join(", ")) << "\n";
				return 2;
			}
			preferences.density = densityFromId(value);
		}
		if (hasOption(args, "--set-editor-profile")) {
			EditorProfileDescriptor profile;
			if (!editorProfileForId(optionValue(args, "--set-editor-profile"), &profile)) {
				std::cerr << "--set-editor-profile requires one of: " << text(editorProfileIds().join(", ")) << "\n";
				return 2;
			}
			editorProfileId = profile.id;
		}
		if (hasOption(args, "--set-reduced-motion")) {
			bool value = false;
			if (!boolOptionValue(optionValue(args, "--set-reduced-motion"), &value)) {
				std::cerr << "--set-reduced-motion requires on or off.\n";
				return 2;
			}
			preferences.reducedMotion = value;
		}
		if (hasOption(args, "--set-tts")) {
			bool value = false;
			if (!boolOptionValue(optionValue(args, "--set-tts"), &value)) {
				std::cerr << "--set-tts requires on or off.\n";
				return 2;
			}
			preferences.textToSpeechEnabled = value;
		}

		settings.setAccessibilityPreferences(preferences);
		settings.setSelectedEditorProfileId(editorProfileId);
		settings.sync();
		if (settings.status() != QSettings::NoError) {
			std::cerr << "Failed to save preferences: " << text(settingsStatusText(settings.status())) << "\n";
			return 1;
		}
		std::cout << "Preferences saved.\n";
		printPreferences(settings.accessibilityPreferences(), settings.selectedEditorProfileId());
		return 0;
	}
	if (hasOption(args, "--settings-report")) {
		printSettingsReport();
		return 0;
	}
	if (hasOption(args, "--setup-report")) {
		const StudioSettings settings;
		printSetupSummary(settings.setupSummary());
		return 0;
	}
	if (hasOption(args, "--preferences-report")) {
		const StudioSettings settings;
		printPreferences(settings.accessibilityPreferences(), settings.selectedEditorProfileId());
		return 0;
	}
	if (hasOption(args, "--localization-report")) {
		return runLocalizationReportCommand(QStringLiteral("--localization-report"), args, outputFormat(args), false);
	}
	if (hasOption(args, "--editor-profiles")) {
		return runEditorProfilesCommand(QStringLiteral("--editor-profiles"), outputFormat(args));
	}
	if (hasOption(args, "--ai-status")) {
		return runAiStatusCommand(QStringLiteral("--ai-status"), outputFormat(args));
	}
	if (hasOption(args, "--set-ai-free") || hasOption(args, "--set-ai-cloud") || hasOption(args, "--set-ai-agentic") || hasOption(args, "--set-ai-reasoning") || hasOption(args, "--set-ai-text-model")
		|| hasOption(args, "--set-ai-local") || hasOption(args, "--set-ai-model") || hasOption(args, "--set-ai-endpoint")
		|| hasOption(args, "--set-ai-image") || hasOption(args, "--set-ai-image-model") || hasOption(args, "--set-ai-image-endpoint")
		|| hasOption(args, "--set-ai-audio") || hasOption(args, "--set-ai-audio-model") || hasOption(args, "--set-ai-audio-endpoint")) {
		StudioSettings settings;
		AiAutomationPreferences preferences = settings.aiAutomationPreferences();

		if (hasOption(args, "--set-ai-free")) {
			bool value = true;
			if (!boolOptionValue(optionValue(args, "--set-ai-free"), &value)) {
				std::cerr << "--set-ai-free requires on or off.\n";
				return 2;
			}
			preferences.aiFreeMode = value;
		}
		if (hasOption(args, "--set-ai-cloud")) {
			bool value = false;
			if (!boolOptionValue(optionValue(args, "--set-ai-cloud"), &value)) {
				std::cerr << "--set-ai-cloud requires on or off.\n";
				return 2;
			}
			preferences.cloudConnectorsEnabled = value;
			if (value) {
				preferences.aiFreeMode = false;
			}
		}
		if (hasOption(args, "--set-ai-agentic")) {
			bool value = false;
			if (!boolOptionValue(optionValue(args, "--set-ai-agentic"), &value)) {
				std::cerr << "--set-ai-agentic requires on or off.\n";
				return 2;
			}
			preferences.agenticWorkflowsEnabled = value;
			if (value) {
				preferences.aiFreeMode = false;
				preferences.cloudConnectorsEnabled = true;
			}
		}
		if (hasOption(args, "--set-ai-reasoning")) {
			AiConnectorDescriptor connector;
			const QString connectorId = optionValue(args, "--set-ai-reasoning");
			if (!connectorId.isEmpty() && (!aiConnectorForId(connectorId, &connector) || !connector.capabilities.contains(QStringLiteral("reasoning")))) {
				std::cerr << "--set-ai-reasoning requires a connector with reasoning capability. Known: " << text(aiConnectorIds().join(", ")) << "\n";
				return 2;
			}
			preferences.preferredReasoningConnectorId = connectorId;
		}
		if (hasOption(args, "--set-ai-text-model")) {
			AiModelDescriptor model;
			const QString modelId = optionValue(args, "--set-ai-text-model");
			if (!modelId.isEmpty() && (!aiModelForId(modelId, &model) || !model.capabilities.contains(QStringLiteral("reasoning")))) {
				std::cerr << "--set-ai-text-model requires a reasoning-capable model. Known: " << text(aiModelIds().join(", ")) << "\n";
				return 2;
			}
			preferences.preferredTextModelId = modelId;
		}
		if (hasOption(args, "--set-ai-local")) {
			AiConnectorDescriptor connector;
			const QString connectorId = optionValue(args, "--set-ai-local");
			if (!connectorId.isEmpty() && (!aiConnectorForId(connectorId, &connector) || !aiConnectorHasChatTransport(connector.id))) {
				std::cerr << "--set-ai-local requires a text connector: local-offline, custom-http, openai, claude, or gemini.\n";
				return 2;
			}
			preferences.preferredLocalConnectorId = connectorId;
		}
		// connector=value pairs; an empty value clears the setting.
		for (const QString& option : {QStringLiteral("--set-ai-model"), QStringLiteral("--set-ai-endpoint")}) {
			if (!hasOption(args, option)) {
				continue;
			}
			const QString pair = optionValue(args, option);
			const qsizetype equals = pair.indexOf(QLatin1Char('='));
			const QString connectorId = normalizedAiId(pair.left(equals));
			if (equals <= 0 || !aiConnectorHasChatTransport(connectorId)) {
				std::cerr << text(option) << " takes <connector>=<value>, with a text connector: local-offline, custom-http, openai, claude, or gemini.\n";
				return 2;
			}
			QHash<QString, QString>& values = option == QStringLiteral("--set-ai-model") ? preferences.connectorModels : preferences.connectorEndpoints;
			const QString value = pair.mid(equals + 1).trimmed();
			if (value.isEmpty()) {
				values.remove(connectorId);
			} else {
				values.insert(connectorId, value);
			}
		}
		// The image connector, and each image connector's model and endpoint.
		if (hasOption(args, "--set-ai-image")) {
			const QString connectorId = normalizedAiId(optionValue(args, "--set-ai-image"));
			if (!connectorId.isEmpty() && !aiConnectorHasImageTransport(connectorId)) {
				std::cerr << "--set-ai-image requires an image connector: openai, gemini, local-offline, or custom-http.\n";
				return 2;
			}
			preferences.preferredImageConnectorId = connectorId;
		}
		for (const QString& option : {QStringLiteral("--set-ai-image-model"), QStringLiteral("--set-ai-image-endpoint")}) {
			if (!hasOption(args, option)) {
				continue;
			}
			const QString pair = optionValue(args, option);
			const qsizetype equals = pair.indexOf(QLatin1Char('='));
			const QString connectorId = normalizedAiId(pair.left(equals));
			if (equals <= 0 || !aiConnectorHasImageTransport(connectorId)) {
				std::cerr << text(option) << " takes <connector>=<value>, with an image connector: openai, gemini, local-offline, or custom-http.\n";
				return 2;
			}
			QHash<QString, QString>& values = option == QStringLiteral("--set-ai-image-model") ? preferences.connectorImageModels : preferences.connectorImageEndpoints;
			const QString value = pair.mid(equals + 1).trimmed();
			if (value.isEmpty()) {
				values.remove(connectorId);
			} else {
				values.insert(connectorId, value);
			}
		}
		// The sound connector, and each sound connector's model and endpoint.
		if (hasOption(args, "--set-ai-audio")) {
			const QString connectorId = normalizedAiId(optionValue(args, "--set-ai-audio"));
			if (!connectorId.isEmpty() && !aiConnectorHasSoundTransport(connectorId)) {
				std::cerr << "--set-ai-audio requires a sound connector: elevenlabs or custom-http.\n";
				return 2;
			}
			preferences.preferredAudioConnectorId = connectorId;
		}
		for (const QString& option : {QStringLiteral("--set-ai-audio-model"), QStringLiteral("--set-ai-audio-endpoint")}) {
			if (!hasOption(args, option)) {
				continue;
			}
			const QString pair = optionValue(args, option);
			const qsizetype equals = pair.indexOf(QLatin1Char('='));
			const QString connectorId = normalizedAiId(pair.left(equals));
			if (equals <= 0 || !aiConnectorHasSoundTransport(connectorId)) {
				std::cerr << text(option) << " takes <connector>=<value>, with a sound connector: elevenlabs or custom-http.\n";
				return 2;
			}
			QHash<QString, QString>& values = option == QStringLiteral("--set-ai-audio-model") ? preferences.connectorAudioModels : preferences.connectorAudioEndpoints;
			const QString value = pair.mid(equals + 1).trimmed();
			if (value.isEmpty()) {
				values.remove(connectorId);
			} else {
				values.insert(connectorId, value);
			}
		}

		settings.setAiAutomationPreferences(preferences);
		settings.sync();
		if (settings.status() != QSettings::NoError) {
			std::cerr << "Failed to save AI preferences: " << text(settingsStatusText(settings.status())) << "\n";
			return 1;
		}
		std::cout << "AI preferences saved.\n";
		std::cout << text(aiAutomationPreferencesText(settings.aiAutomationPreferences())) << "\n";
		return 0;
	}
	if (hasOption(args, "--installations-report")) {
		return runInstallListCommand(QStringLiteral("--installations-report"), outputFormat(args));
	}
	if (hasOption(args, "--detect-installations")) {
		return runInstallDetectCommand(QStringLiteral("--detect-installations"), optionValues(args, QStringLiteral("--detect-install-root")), outputFormat(args));
	}
	if (hasOption(args, "--add-installation")) {
		const QString rootPath = optionValue(args, "--add-installation");
		if (rootPath.isEmpty()) {
			std::cerr << "--add-installation requires an installation root path.\n";
			return 2;
		}

		GameInstallationProfile profile;
		profile.rootPath = rootPath;
		profile.gameKey = optionValue(args, "--install-game");
		profile.displayName = optionValue(args, "--install-name");
		profile.engineFamily = hasOption(args, "--install-engine") ? gameEngineFamilyFromId(optionValue(args, "--install-engine")) : GameEngineFamily::Unknown;
		profile.executablePath = optionValue(args, "--install-executable");
		profile.basePackagePaths = optionPathList(args, "--install-base-packages");
		profile.modPackagePaths = optionPathList(args, "--install-mod-packages");
		profile.paletteId = optionValue(args, "--install-palette");
		profile.compilerProfileId = optionValue(args, "--install-compiler-profile");
		if (hasOption(args, "--install-hidden")) {
			bool hidden = false;
			if (!boolOptionValue(optionValue(args, "--install-hidden"), &hidden)) {
				std::cerr << "--install-hidden requires on or off.\n";
				return 2;
			}
			profile.hidden = hidden;
		}
		if (hasOption(args, "--install-read-only")) {
			bool readOnly = true;
			if (!boolOptionValue(optionValue(args, "--install-read-only"), &readOnly)) {
				std::cerr << "--install-read-only requires on or off.\n";
				return 2;
			}
			profile.readOnly = readOnly;
		}
		profile = normalizedGameInstallationProfile(profile);

		StudioSettings settings;
		settings.upsertGameInstallation(profile);
		settings.sync();
		if (settings.status() != QSettings::NoError) {
			std::cerr << "Failed to save installation profile: " << text(settingsStatusText(settings.status())) << "\n";
			return 1;
		}
		std::cout << "Saved game installation: " << text(profile.id) << "\n";
		printGameInstallationProfile(profile, settings.selectedGameInstallationId());
		printInstallationValidation(profile);
		return 0;
	}
	if (hasOption(args, "--select-installation")) {
		const QString id = optionValue(args, "--select-installation");
		if (id.isEmpty()) {
			std::cerr << "--select-installation requires an installation profile id.\n";
			return 2;
		}
		StudioSettings settings;
		settings.setSelectedGameInstallation(id);
		settings.sync();
		if (settings.status() != QSettings::NoError) {
			std::cerr << "Failed to save selected installation: " << text(settingsStatusText(settings.status())) << "\n";
			return 1;
		}
		if (!sameGameInstallationId(settings.selectedGameInstallationId(), id)) {
			std::cerr << "Installation profile not found: " << text(id) << "\n";
			return 1;
		}
		std::cout << "Selected game installation: " << text(settings.selectedGameInstallationId()) << "\n";
		return 0;
	}
	if (hasOption(args, "--validate-installation")) {
		const QString id = optionValue(args, "--validate-installation");
		if (id.isEmpty()) {
			std::cerr << "--validate-installation requires an installation profile id.\n";
			return 2;
		}
		const StudioSettings settings;
		const QVector<GameInstallationProfile> profiles = settings.gameInstallations();
		const GameInstallationProfile* profile = findInstallationById(profiles, id);
		if (!profile) {
			std::cerr << "Installation profile not found: " << text(id) << "\n";
			return 1;
		}
		std::cout << "Game installation validation\n";
		printGameInstallationProfile(*profile, settings.selectedGameInstallationId());
		printInstallationValidation(*profile);
		return validateGameInstallationProfile(*profile).isUsable() ? 0 : 1;
	}
	if (hasOption(args, "--remove-installation")) {
		const QString id = optionValue(args, "--remove-installation");
		if (id.isEmpty()) {
			std::cerr << "--remove-installation requires an installation profile id.\n";
			return 2;
		}
		StudioSettings settings;
		const QVector<GameInstallationProfile> profiles = settings.gameInstallations();
		if (!findInstallationById(profiles, id)) {
			std::cerr << "Installation profile not found: " << text(id) << "\n";
			return 1;
		}
		settings.removeGameInstallation(id);
		settings.sync();
		if (settings.status() != QSettings::NoError) {
			std::cerr << "Failed to save installation profiles: " << text(settingsStatusText(settings.status())) << "\n";
			return 1;
		}
		std::cout << "Removed game installation: " << text(id) << "\n";
		return 0;
	}
	if (hasOption(args, "--recent-projects")) {
		const StudioSettings settings;
		printRecentProjects(settings);
		return 0;
	}
	if (hasOption(args, "--add-recent-project")) {
		const QString path = optionValue(args, "--add-recent-project");
		if (path.isEmpty()) {
			std::cerr << "--add-recent-project requires a path.\n";
			return 2;
		}

		StudioSettings settings;
		settings.recordRecentProject(path);
		settings.sync();
		if (settings.status() != QSettings::NoError) {
			std::cerr << "Failed to save settings: " << text(settingsStatusText(settings.status())) << "\n";
			return 1;
		}
		std::cout << "Remembered recent project: " << text(nativePath(normalizedProjectPath(path))) << "\n";
		return 0;
	}
	if (hasOption(args, "--remove-recent-project")) {
		const QString path = optionValue(args, "--remove-recent-project");
		if (path.isEmpty()) {
			std::cerr << "--remove-recent-project requires a path.\n";
			return 2;
		}

		StudioSettings settings;
		settings.removeRecentProject(path);
		settings.sync();
		if (settings.status() != QSettings::NoError) {
			std::cerr << "Failed to save settings: " << text(settingsStatusText(settings.status())) << "\n";
			return 1;
		}
		std::cout << "Forgot recent project: " << text(nativePath(normalizedProjectPath(path))) << "\n";
		return 0;
	}
	if (hasOption(args, "--clear-recent-projects")) {
		StudioSettings settings;
		settings.clearRecentProjects();
		settings.sync();
		if (settings.status() != QSettings::NoError) {
			std::cerr << "Failed to save settings: " << text(settingsStatusText(settings.status())) << "\n";
			return 1;
		}
		std::cout << "Recent projects cleared.\n";
		return 0;
	}

	std::cerr << "Unknown VibeStudio CLI option. Run --cli --help.\n";
	return 2;
}

int run(const QStringList& args)
{
	currentCliArgs() = args;
	QElapsedTimer timer;
	timer.start();

	NullOutputBuffer nullBuffer;
	std::streambuf* originalOutput = nullptr;
	if (quietOutput()) {
		originalOutput = std::cout.rdbuf(&nullBuffer);
	}

	const int result = runImpl(args);

	if (originalOutput) {
		std::cout.rdbuf(originalOutput);
	}
	if (verboseOutput()) {
		std::cerr << "vibestudio: command completed with exit code " << result << " in " << timer.elapsed() << " ms\n";
	}
	currentCliArgs().clear();
	return result;
}

} // namespace vibestudio::cli
