#include "app/level_surface_worker.h"
#include "core/level_materials.h"
#include "core/package_staging.h"
#include <QCoreApplication>
#include <QSet>
#include <QThread>
#include <atomic>

namespace vibestudio {
struct LevelSurfaceWorker::Work {
	std::atomic_bool cancelled{false};
	LevelSurfaceResult result;
};
LevelSurfaceWorker::LevelSurfaceWorker(QObject* parent) : QObject(parent) {}
LevelSurfaceWorker::~LevelSurfaceWorker()
{
	cancel();
	if (m_thread) { m_thread->disconnect(this); m_thread->wait(); delete m_thread; }
}
void LevelSurfaceWorker::cancel() { if (m_work) { m_work->cancelled = true; } }
bool LevelSurfaceWorker::start(LevelSurfaceWork request)
{
	if (busy()) { return false; }
	auto work = std::make_shared<Work>();
	m_work = work;
	work->result.token = request.token;
	work->result.pasted = request.clipboard.ready();
	work->result.strokeResult = request.stroke || request.startStroke;
	work->result.finishedStroke = request.finishStroke;
	m_thread = QThread::create([work, request = std::move(request)]() mutable {
		const auto cancelled = [work] { return work->cancelled.load(); };
		try {
			const auto describePlan = [&](const LevelSurfaceEditPlan& plan) {
				work->result.changedFaces = plan.faceCount(); work->result.changedPatches = plan.patchCount();
				work->result.convertedFaces = plan.convertedFaceCount(); work->result.edgeOnFaces = plan.edgeOnFaceCount();
			};
			if (request.finishStroke) {
				if (cancelled() || !request.stroke || !request.stroke->commit(&request.document, &work->result.error)) { return; }
				describePlan(request.stroke->plan());
				if (request.stroke->clipboardFromDocument()) {
					work->result.nextClipboard = request.stroke->clipboard();
					work->result.nextClipboardArchive = request.archive; work->result.nextClipboardStaging = request.staging;
					work->result.nextClipboardPalette = request.palette; work->result.nextClipboardEngineFamily = request.document.engineFamily;
					work->result.nextClipboardFormat = request.document.format;
				}
				work->result.document = std::move(request.document); return;
			}
			std::shared_ptr<LevelSurfaceStroke> stroke;
			if (request.stroke) { stroke = std::make_shared<LevelSurfaceStroke>(*request.stroke); }
			else if (request.startStroke) { stroke = std::make_shared<LevelSurfaceStroke>(request.document, request.clipboard); }
			if (stroke) {
				request.document = stroke->document(); request.clipboard = stroke->clipboard();
				if (stroke->stepCount() > 0) { request.paste.includeSelection = false; }
				if (stroke->clipboardFromDocument()) {
					request.clipboardArchive = request.archive; request.clipboardStaging = request.stagingIndexed ? nullptr : request.staging;
					request.clipboardPalette = request.palette; request.clipboardEngineFamily = request.document.engineFamily;
					request.clipboardFormat = request.document.format;
					request.paste.textureSize = request.textureSizes.value(request.clipboard.material().trimmed().replace('\\', '/').toCaseFolded());
				}
			}
			PackageReadControl packageControl; packageControl.isCancelled = cancelled;
			for (const auto& face : request.faces) { request.pasteTargets.append({LevelMaterialKind::BrushFace, face.brushId, face.faceIndex}); }
			request.textureSizes.insert(request.paste.materialSizes);
			// Resolve only targeted materials, and only when their dimensions
			// are needed. Ordinary legacy/Valve shifts work without a package.
			LevelMapDocument visible;
			visible.format = request.document.format;
			visible.engineFamily = request.document.engineFamily;
			QHash<int, QSet<int>> targets;
			for (const auto& face : request.faces) { targets[face.brushId].insert(face.faceIndex); }
			for (const auto& brush : request.document.brushes) {
				if (cancelled()) { return; }
				const auto found = targets.constFind(brush.id);
				if (found == targets.cend()) { continue; }
				LevelMapBrush materialBrush;
				for (int index : *found) {
					if (index < 0 || index >= brush.faces.size()) { continue; }
					const auto& face = brush.faces[index];
					if (request.clipboard.ready()) { continue; }
					const auto key = face.textureName.trimmed().replace('\\', '/').toCaseFolded();
					if (request.textureSizes.value(key).isValid()) { continue; }
					for (const auto& step : request.adjustments) {
						if ((!face.explicitTextureMatrix && (step.operation == LevelSurfaceOperation::Fit || step.operation == LevelSurfaceOperation::Align))
							|| (face.explicitTextureMatrix && (step.operation == LevelSurfaceOperation::Shift || step.operation == LevelSurfaceOperation::Rotate))) {
							materialBrush.faces << face; break;
						}
					}
				}
				if (!materialBrush.faces.isEmpty()) { visible.brushes << materialBrush; }
			}
			const auto copiedMaterial = request.clipboard.material().trimmed().replace('\\', '/').toCaseFolded();
			const auto requiredMaterials = levelSurfaceTransferRequiredMaterials(request.document, request.pasteTargets, request.clipboard, request.paste);
			if (requiredMaterials.sourceSizeRequired && request.clipboardContextCaptured && !request.paste.textureSize.isValid()) {
				// Resolve from the copy-time package snapshot. A same-named image in
				// the current target package may have entirely different dimensions.
				if (request.clipboardStaging) {
					request.clipboardArchive = std::make_shared<PackageStagingArchive>(*request.clipboardStaging, PackageStagingReadMode::CompletePlan, packageControl);
				}
				if (request.clipboardArchive) {
					LevelMapDocument source;
					source.format = request.clipboardFormat == LevelMapFormat::Unknown ? request.document.format : request.clipboardFormat;
					source.engineFamily = request.clipboardEngineFamily.isEmpty() ? request.document.engineFamily : request.clipboardEngineFamily;
					LevelMapBrush brush; brush.faces << request.clipboard.face(); source.brushes << brush;
					LevelPreviewAssetOptions options; options.paletteId = request.clipboardPalette;
					const auto assets = resolveLevelPreviewAssets(source, *request.clipboardArchive, options, [cancelled](int, int) { return !cancelled(); });
					request.paste.textureSize = levelPreviewTextureSizes(assets).value(copiedMaterial);
				}
				if (cancelled()) { return; }
				if (!request.paste.textureSize.isValid()) {
					work->result.error = QCoreApplication::translate("LevelSurfaceWorker", "The copied material's image dimensions are unavailable in its source package. Reopen that package and copy the surface again."); return;
				}
			}
			auto lookups = requiredMaterials.targets;
			if (requiredMaterials.sourceSizeRequired && !request.clipboardContextCaptured && !request.paste.textureSize.isValid() && !lookups.contains(copiedMaterial)) { lookups << copiedMaterial; }
			for (const auto& name : lookups) {
				if (request.textureSizes.value(name).isValid()) { continue; }
				LevelMapBrush materialBrush; LevelMapBrushFace materialFace; materialFace.textureName = name;
				materialBrush.faces << materialFace; visible.brushes << materialBrush;
			}
			if (request.staging && !request.stagingIndexed && !visible.brushes.isEmpty()) {
				request.archive = std::make_shared<PackageStagingArchive>(*request.staging, PackageStagingReadMode::CompletePlan, packageControl);
				request.stagingIndexed = true;
			}
			if (request.archive && !visible.brushes.isEmpty()) {
				LevelPreviewAssetOptions options; options.paletteId = request.palette;
				const auto assets = resolveLevelPreviewAssets(visible, *request.archive, options,
					[cancelled](int, int) { return !cancelled(); });
				request.textureSizes.insert(levelPreviewTextureSizes(assets));
			}
			if (request.clipboardContextCaptured) {
				for (const auto& name : requiredMaterials.targets) {
					if (!request.textureSizes.value(name).isValid()) {
						work->result.error = QCoreApplication::translate("LevelSurfaceWorker", "The destination image dimensions for %1 are unavailable in the current package.").arg(name); return;
					}
				}
			}
			LevelSurfaceEditPlan plan;
			if (!request.paste.textureSize.isValid()) { request.paste.textureSize = request.textureSizes.value(copiedMaterial); }
			if (stroke && stroke->clipboardFromDocument() && request.paste.textureSize.isValid()) { request.textureSizes.insert(copiedMaterial, request.paste.textureSize); }
			request.paste.materialSizes = request.textureSizes;
			if (cancelled()) { return; }
			if (stroke) {
				if (!stroke->clipboardFromDocument()) { work->result.sourceTextureSize = request.paste.textureSize; }
				if (!stroke->append(request.pasteTargets, request.paste, &work->result.error, cancelled)) { return; }
				describePlan(stroke->plan());
				work->result.resolvedArchive = std::move(request.archive); work->result.stagingIndexed = request.stagingIndexed;
				work->result.textureSizes = std::move(request.textureSizes); work->result.stroke = std::move(stroke); return;
			}
			const bool prepared = request.clipboard.ready()
				? prepareLevelSurfaceTransfer(request.document, request.pasteTargets, request.clipboard, request.paste, &plan, &work->result.error, cancelled)
				: prepareLevelSurfaceEdits(request.document, request.faces, request.adjustments, request.textureSizes, &plan, &work->result.error, cancelled);
			if (!prepared) { return; }
			if (cancelled() || !commitLevelSurfaceEdit(&request.document, plan, &work->result.error)) { return; }
			if (request.clipboard.ready() && request.paste.mode == LevelSurfacePasteMode::Seamless && request.pasteTargets.size() == 1
				&& !copyLevelSurface(request.document, {request.pasteTargets.first().objectId, request.pasteTargets.first().faceIndex}, &work->result.nextClipboard, &work->result.error)) { return; }
			if (work->result.nextClipboard.ready()) {
				work->result.nextClipboardArchive = request.archive; work->result.nextClipboardPalette = request.palette;
				work->result.nextClipboardStaging = request.staging;
				work->result.nextClipboardEngineFamily = request.document.engineFamily;
				work->result.nextClipboardFormat = request.document.format;
			}
			work->result.changedFaces = plan.faceCount();
			work->result.changedPatches = plan.patchCount();
			work->result.convertedFaces = plan.convertedFaceCount();
			work->result.edgeOnFaces = plan.edgeOnFaceCount();
			work->result.document = std::move(request.document);
			work->result.textureSizes = std::move(request.textureSizes);
		} catch (const std::exception& error) {
			work->result.error = QString::fromUtf8(error.what());
		} catch (...) {
			work->result.error = QCoreApplication::translate("LevelSurfaceWorker", "Unable to prepare the surface adjustment.");
		}
	});
	connect(m_thread, &QThread::finished, this, [this, work] {
		auto* thread = m_thread; m_thread = nullptr;
		thread->wait(); thread->deleteLater(); m_work.reset();
		work->result.cancelled = work->cancelled.load();
		if (completed) { completed(std::move(work->result)); }
	});
	m_thread->start();
	return true;
}
} // namespace vibestudio
