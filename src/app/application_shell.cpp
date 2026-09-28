#include "app/application_shell.h"

#include "app/asset_views.h"
#include "app/code_editor.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include "app/studio_charts.h"
#include "app/studio_icons.h"
#include "app/studio_layout.h"
#include "app/studio_runtime.h"
#include "app/studio_theme.h"
#include "app/syntax_highlight.h"
#include "app/ui_primitives.h"
#include "core/advanced_studio.h"
#include "core/asset_tools.h"
#include "core/bsp_inspect.h"
#include "core/build_pipeline.h"
#include "core/entity_definitions.h"
#include "core/idtech_image.h"
#include "core/map_assets.h"
#include "core/map_render.h"
#include "core/model_mesh.h"
#include "core/ai_connectors.h"
#include "core/ai_workflows.h"
#include "core/compiler_profiles.h"
#include "core/compiler_registry.h"
#include "core/compiler_runner.h"
#include "core/editor_profiles.h"
#include "core/level_map.h"
#include "core/package_preview.h"
#include "core/project_manifest.h"
#include "core/studio_manifest.h"
#include <QAbstractItemView>
#include <QItemSelectionModel>
#include <QSet>
#include <QAction>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDockWidget>
#include <QDropEvent>
#include <QImage>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPixmap>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QSysInfo>
#include <QTextCursor>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLocale>
#include <QMetaObject>
#include <QPalette>
#include <QProgressBar>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QStringList>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTextEdit>
#include <QThread>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace vibestudio {

namespace {

// Defined with the command helpers further down.
QString modeCommandId(StudioMode mode);

QString nativePath(const QString& path)
{
	return QDir::toNativeSeparators(path);
}

QString settingsStatusText(QSettings::Status status)
{
	switch (status) {
	case QSettings::NoError:
		return ApplicationShell::tr("ready");
	case QSettings::AccessError:
		return ApplicationShell::tr("access error");
	case QSettings::FormatError:
		return ApplicationShell::tr("format error");
	}
	return ApplicationShell::tr("unknown");
}

QString localeDisplayName(const QString& localeName)
{
	const QLocale locale(localeName);
	QString nativeLanguage = locale.nativeLanguageName();
	if (nativeLanguage.isEmpty() || nativeLanguage == QStringLiteral("C")) {
		nativeLanguage = localeName;
	}
	return QStringLiteral("%1 [%2]").arg(nativeLanguage, localeName);
}

QString localizedThemeName(StudioTheme theme)
{
	switch (theme) {
	case StudioTheme::System:
		return ApplicationShell::tr("System");
	case StudioTheme::Dark:
		return ApplicationShell::tr("Dark");
	case StudioTheme::Light:
		return ApplicationShell::tr("Light");
	case StudioTheme::HighContrastDark:
		return ApplicationShell::tr("High Contrast Dark");
	case StudioTheme::HighContrastLight:
		return ApplicationShell::tr("High Contrast Light");
	}
	return ApplicationShell::tr("Dark");
}

QString localizedDensityName(UiDensity density)
{
	switch (density) {
	case UiDensity::Comfortable:
		return ApplicationShell::tr("Comfortable");
	case UiDensity::Standard:
		return ApplicationShell::tr("Standard");
	case UiDensity::Compact:
		return ApplicationShell::tr("Compact");
	}
	return ApplicationShell::tr("Standard");
}

QString setupStatusDisplayName(const QString& status)
{
	if (status == QStringLiteral("complete")) {
		return ApplicationShell::tr("Complete");
	}
	if (status == QStringLiteral("skipped")) {
		return ApplicationShell::tr("Skipped For Now");
	}
	if (status == QStringLiteral("in-progress")) {
		return ApplicationShell::tr("In Progress");
	}
	return ApplicationShell::tr("Not Started");
}

int setupStepProgressValue(SetupStep step)
{
	const QVector<SetupStep> steps = setupSteps();
	for (int index = 0; index < steps.size(); ++index) {
		if (steps[index] == step) {
			return index;
		}
	}
	return 0;
}

QString localizedOperationStateName(OperationState state)
{
	switch (state) {
	case OperationState::Idle:
		return ApplicationShell::tr("Idle");
	case OperationState::Queued:
		return ApplicationShell::tr("Queued");
	case OperationState::Loading:
		return ApplicationShell::tr("Loading");
	case OperationState::Running:
		return ApplicationShell::tr("Running");
	case OperationState::Warning:
		return ApplicationShell::tr("Warning");
	case OperationState::Failed:
		return ApplicationShell::tr("Failed");
	case OperationState::Cancelled:
		return ApplicationShell::tr("Cancelled");
	case OperationState::Completed:
		return ApplicationShell::tr("Completed");
	}
	return ApplicationShell::tr("Idle");
}

QString localizedPackageFormatName(PackageArchiveFormat format)
{
	switch (format) {
	case PackageArchiveFormat::Folder:
		return ApplicationShell::tr("Folder");
	case PackageArchiveFormat::Pak:
		return ApplicationShell::tr("PAK");
	case PackageArchiveFormat::Wad:
		return ApplicationShell::tr("WAD");
	case PackageArchiveFormat::Zip:
		return ApplicationShell::tr("ZIP");
	case PackageArchiveFormat::Pk3:
		return ApplicationShell::tr("PK3");
	case PackageArchiveFormat::Unknown:
		break;
	}
	return ApplicationShell::tr("Unknown");
}

QString localizedPackageEntryKindName(PackageEntryKind kind)
{
	switch (kind) {
	case PackageEntryKind::File:
		return ApplicationShell::tr("File");
	case PackageEntryKind::Directory:
		return ApplicationShell::tr("Directory");
	}
	return ApplicationShell::tr("File");
}

QString localizedGameEngineFamilyName(GameEngineFamily family)
{
	switch (family) {
	case GameEngineFamily::IdTech1:
		return ApplicationShell::tr("idTech1 / Doom-family");
	case GameEngineFamily::IdTech2:
		return ApplicationShell::tr("idTech2 / Quake-family");
	case GameEngineFamily::IdTech3:
		return ApplicationShell::tr("idTech3 / Quake III-family");
	case GameEngineFamily::Unknown:
		break;
	}
	return ApplicationShell::tr("Unknown");
}

QString localizedPackagePreviewKindName(PackagePreviewKind kind)
{
	switch (kind) {
	case PackagePreviewKind::Unavailable:
		return ApplicationShell::tr("Unavailable");
	case PackagePreviewKind::Directory:
		return ApplicationShell::tr("Directory");
	case PackagePreviewKind::Text:
		return ApplicationShell::tr("Text");
	case PackagePreviewKind::Image:
		return ApplicationShell::tr("Image");
	case PackagePreviewKind::Model:
		return ApplicationShell::tr("Model");
	case PackagePreviewKind::Audio:
		return ApplicationShell::tr("Audio");
	case PackagePreviewKind::Binary:
		return ApplicationShell::tr("Binary");
	}
	return ApplicationShell::tr("Unavailable");
}

QString localizedPackageStageOperationName(PackageStageOperationType type)
{
	switch (type) {
	case PackageStageOperationType::Add:
		return ApplicationShell::tr("Add");
	case PackageStageOperationType::Replace:
		return ApplicationShell::tr("Replace");
	case PackageStageOperationType::Rename:
		return ApplicationShell::tr("Rename");
	case PackageStageOperationType::Delete:
		return ApplicationShell::tr("Delete");
	}
	return ApplicationShell::tr("Stage");
}

// Glyph for a package entry, chosen from its kind and type hint so a list of
// mixed content can be scanned by shape as well as by name.
QString packageEntryIconName(const PackageEntry& entry)
{
	if (entry.kind == PackageEntryKind::Directory) {
		return QStringLiteral("folder");
	}
	const QString hint = entry.typeHint.toLower();
	if (hint.startsWith(QStringLiteral("image/")) || hint.contains(QStringLiteral("texture"))) {
		return QStringLiteral("image");
	}
	if (hint.startsWith(QStringLiteral("audio/"))) {
		return QStringLiteral("waveform");
	}
	if (hint.startsWith(QStringLiteral("model/"))) {
		return QStringLiteral("cube");
	}
	if (hint == QStringLiteral("text/map") || hint.endsWith(QStringLiteral("/bsp"))) {
		return QStringLiteral("map");
	}
	if (hint == QStringLiteral("text/shader")) {
		return QStringLiteral("layers");
	}
	const QString suffix = QFileInfo(entry.virtualPath).suffix().toLower();
	if (suffix == QStringLiteral("pak") || suffix == QStringLiteral("pk3") || suffix == QStringLiteral("wad") || suffix == QStringLiteral("zip")) {
		return QStringLiteral("package");
	}
	return QStringLiteral("file");
}

// Keeps the user's place when a list is rebuilt: reselects the row whose
// Qt::UserRole matches `previousKey`, or the first selectable row. Signals are
// blocked so the caller decides when to refresh the dependent preview.
void restoreListSelection(QListWidget* list, const QString& previousKey)
{
	if (!list) {
		return;
	}
	QListWidgetItem* target = nullptr;
	for (int index = 0; index < list->count(); ++index) {
		QListWidgetItem* item = list->item(index);
		if (!item || item->isHidden() || !item->flags().testFlag(Qt::ItemIsSelectable)) {
			continue;
		}
		if (!target) {
			target = item;
		}
		if (!previousKey.isEmpty() && item->data(Qt::UserRole).toString() == previousKey) {
			target = item;
			break;
		}
	}
	if (target) {
		const QSignalBlocker blocker(list);
		list->setCurrentItem(target);
	}
}

// Virtual folder that contains `virtualPath`, or the root ("") for top-level
// entries.
QString packageParentFolder(const QString& virtualPath)
{
	const int slash = virtualPath.lastIndexOf(QLatin1Char('/'));
	return slash <= 0 ? QString() : virtualPath.left(slash);
}

// Square thumbnail for decoded idTech art. Small textures scale up with
// nearest-neighbour so their pixels stay crisp; larger ones scale smoothly.
QIcon textureThumbnailIcon(const QImage& image, int side, qreal devicePixelRatio)
{
	const int physical = std::max(1, static_cast<int>(side * devicePixelRatio));
	QPixmap pixmap(physical, physical);
	pixmap.fill(Qt::transparent);
	if (!image.isNull()) {
		const bool upscale = image.width() <= physical && image.height() <= physical;
		const QImage scaled = image.scaled(physical, physical, Qt::KeepAspectRatio, upscale ? Qt::FastTransformation : Qt::SmoothTransformation);
		QPainter painter(&pixmap);
		painter.drawImage((physical - scaled.width()) / 2, (physical - scaled.height()) / 2, scaled);
	}
	pixmap.setDevicePixelRatio(devicePixelRatio);
	return QIcon(pixmap);
}

QString currentItemKey(const QListWidget* list)
{
	const QListWidgetItem* item = list ? list->currentItem() : nullptr;
	return item ? item->data(Qt::UserRole).toString() : QString();
}

QString byteSizeText(quint64 bytes)
{
	if (bytes >= 1024ull * 1024ull * 1024ull) {
		return ApplicationShell::tr("%1 GiB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
	}
	if (bytes >= 1024ull * 1024ull) {
		return ApplicationShell::tr("%1 MiB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 2);
	}
	if (bytes >= 1024ull) {
		return ApplicationShell::tr("%1 KiB").arg(static_cast<double>(bytes) / 1024.0, 0, 'f', 2);
	}
	return ApplicationShell::tr("%1 B").arg(bytes);
}

QString previewContentText(const PackagePreview& preview)
{
	QStringList lines;
	lines << ApplicationShell::tr("Preview kind: %1").arg(localizedPackagePreviewKindName(preview.kind));
	if (!preview.summary.isEmpty()) {
		lines << ApplicationShell::tr("Summary: %1").arg(preview.summary);
	}
	if (preview.totalBytes > 0 || preview.bytesRead > 0) {
		lines << ApplicationShell::tr("Bytes sampled: %1 of %2")
			.arg(byteSizeText(static_cast<quint64>(std::max<qint64>(0, preview.bytesRead))))
			.arg(byteSizeText(static_cast<quint64>(std::max<qint64>(0, preview.totalBytes))));
	}
	if (preview.truncated) {
		lines << ApplicationShell::tr("Preview is truncated; open raw details for byte counts.");
	}
	if (!preview.error.isEmpty()) {
		lines << ApplicationShell::tr("Reason: %1").arg(preview.error);
	}

	switch (preview.kind) {
	case PackagePreviewKind::Text:
		lines << QString();
		lines << ApplicationShell::tr("Text sample:");
		lines << (preview.body.isEmpty() ? ApplicationShell::tr("(empty text entry)") : preview.body);
		break;
	case PackagePreviewKind::Image:
		lines << QString();
		lines << ApplicationShell::tr("Image metadata:");
		lines << ApplicationShell::tr("Format: %1").arg(preview.imageFormat.isEmpty() ? ApplicationShell::tr("unknown") : preview.imageFormat);
		lines << ApplicationShell::tr("Dimensions: %1").arg(preview.imageSize.isValid()
			? ApplicationShell::tr("%1 x %2 px").arg(preview.imageSize.width()).arg(preview.imageSize.height())
			: ApplicationShell::tr("unknown"));
		lines << ApplicationShell::tr("Depth: %1").arg(preview.imageDepth > 0 ? ApplicationShell::tr("%1 bpp").arg(preview.imageDepth) : ApplicationShell::tr("unknown"));
		lines << ApplicationShell::tr("Palette-aware: %1").arg(preview.imagePaletteAware ? ApplicationShell::tr("yes") : ApplicationShell::tr("no"));
		if (!preview.imagePaletteLines.isEmpty()) {
			lines << ApplicationShell::tr("Palette sample:");
			lines << preview.imagePaletteLines.join('\n');
		}
		break;
	case PackagePreviewKind::Model:
		lines << QString();
		lines << ApplicationShell::tr("Model viewport:");
		lines << (preview.modelViewportLines.isEmpty() ? ApplicationShell::tr("(metadata viewport unavailable)") : preview.modelViewportLines.join('\n'));
		if (!preview.modelMaterialLines.isEmpty()) {
			lines << QString();
			lines << ApplicationShell::tr("Skin/material dependencies:");
			lines << preview.modelMaterialLines.join('\n');
		}
		if (!preview.modelAnimationLines.isEmpty()) {
			lines << QString();
			lines << ApplicationShell::tr("Animation/frame names:");
			lines << preview.modelAnimationLines.join('\n');
		}
		break;
	case PackagePreviewKind::Audio:
		lines << QString();
		lines << ApplicationShell::tr("Audio metadata:");
		lines << ApplicationShell::tr("Format: %1").arg(preview.audioFormat.isEmpty() ? ApplicationShell::tr("unknown") : preview.audioFormat);
		if (!preview.audioWaveformLines.isEmpty()) {
			lines << ApplicationShell::tr("Waveform preview:");
			lines << preview.audioWaveformLines.join('\n');
		}
		break;
	case PackagePreviewKind::Binary:
		lines << QString();
		lines << ApplicationShell::tr("Hex sample:");
		lines << (preview.body.isEmpty() ? ApplicationShell::tr("(no bytes available)") : preview.body);
		break;
	case PackagePreviewKind::Directory:
	case PackagePreviewKind::Unavailable:
		if (!preview.body.isEmpty()) {
			lines << QString();
			lines << preview.body;
		}
		break;
	}

	return lines.join('\n').trimmed();
}

QStringList gameInstallationDetailLines(const GameInstallationProfile& profile, const QString& selectedId)
{
	const GameInstallationValidation validation = validateGameInstallationProfile(profile);
	QStringList lines;
	lines << ApplicationShell::tr("Name: %1").arg(profile.displayName);
	lines << ApplicationShell::tr("Profile ID: %1").arg(profile.id);
	lines << ApplicationShell::tr("Game key: %1").arg(profile.gameKey);
	lines << ApplicationShell::tr("Engine: %1").arg(localizedGameEngineFamilyName(profile.engineFamily));
	lines << ApplicationShell::tr("Root: %1").arg(nativePath(profile.rootPath));
	lines << ApplicationShell::tr("Executable: %1").arg(profile.executablePath.isEmpty() ? ApplicationShell::tr("not set") : nativePath(profile.executablePath));
	lines << ApplicationShell::tr("Base package paths: %1").arg(profile.basePackagePaths.isEmpty() ? ApplicationShell::tr("none") : profile.basePackagePaths.join(QStringLiteral("; ")));
	lines << ApplicationShell::tr("Mod package paths: %1").arg(profile.modPackagePaths.isEmpty() ? ApplicationShell::tr("none") : profile.modPackagePaths.join(QStringLiteral("; ")));
	lines << ApplicationShell::tr("Palette: %1").arg(profile.paletteId.isEmpty() ? ApplicationShell::tr("generic") : profile.paletteId);
	lines << ApplicationShell::tr("Compiler profile: %1").arg(profile.compilerProfileId.isEmpty() ? ApplicationShell::tr("generic") : profile.compilerProfileId);
	lines << ApplicationShell::tr("Read-only: %1").arg(profile.readOnly ? ApplicationShell::tr("yes") : ApplicationShell::tr("no"));
	lines << ApplicationShell::tr("Hidden: %1").arg(profile.hidden ? ApplicationShell::tr("yes") : ApplicationShell::tr("no"));
	lines << ApplicationShell::tr("Selected: %1").arg(sameGameInstallationId(profile.id, selectedId) ? ApplicationShell::tr("yes") : ApplicationShell::tr("no"));
	lines << ApplicationShell::tr("Validation: %1").arg(validation.isUsable() ? ApplicationShell::tr("usable") : ApplicationShell::tr("blocked"));
	if (!validation.errors.isEmpty()) {
		lines << ApplicationShell::tr("Errors:");
		for (const QString& error : validation.errors) {
			lines << QStringLiteral("- %1").arg(error);
		}
	}
	if (!validation.warnings.isEmpty()) {
		lines << ApplicationShell::tr("Warnings:");
		for (const QString& warning : validation.warnings) {
			lines << QStringLiteral("- %1").arg(warning);
		}
	}
	return lines;
}

QStringList projectHealthLines(const ProjectHealthSummary& health)
{
	QStringList lines;
	lines << ApplicationShell::tr("Project: %1").arg(health.title);
	lines << ApplicationShell::tr("Root: %1").arg(nativePath(health.detail));
	lines << ApplicationShell::tr("Ready checks: %1").arg(health.readyCount);
	lines << ApplicationShell::tr("Warnings: %1").arg(health.warningCount);
	lines << ApplicationShell::tr("Failures: %1").arg(health.failedCount);
	lines << QString();
	for (const ProjectHealthCheck& check : health.checks) {
		lines << QStringLiteral("%1 [%2]").arg(check.title, localizedOperationStateName(check.state));
		lines << check.detail;
		lines << QString();
	}
	return lines;
}

struct SummaryBucket {
	QString id;
	QString label;
	int count = 0;
	quint64 bytes = 0;
};

QString compositionBar(double fraction)
{
	const int width = 18;
	const int filled = std::clamp(static_cast<int>(fraction * width + 0.5), 0, width);
	return QStringLiteral("[%1%2]")
		.arg(QString(filled, QLatin1Char('#')))
		.arg(QString(width - filled, QLatin1Char('.')));
}

QString packageCompositionBucketId(const PackageEntry& entry)
{
	if (entry.kind == PackageEntryKind::Directory) {
		return QStringLiteral("directory");
	}
	const QString hint = entry.typeHint.toCaseFolded();
	const QString path = entry.virtualPath.toCaseFolded();
	if (entry.nestedArchiveCandidate || path.endsWith(QStringLiteral(".pak")) || path.endsWith(QStringLiteral(".pk3")) || path.endsWith(QStringLiteral(".zip")) || path.endsWith(QStringLiteral(".wad"))) {
		return QStringLiteral("archive");
	}
	if (hint.contains(QStringLiteral("image")) || path.endsWith(QStringLiteral(".tga")) || path.endsWith(QStringLiteral(".png")) || path.endsWith(QStringLiteral(".jpg")) || path.endsWith(QStringLiteral(".jpeg"))) {
		return QStringLiteral("image");
	}
	if (hint.contains(QStringLiteral("text")) || path.endsWith(QStringLiteral(".cfg")) || path.endsWith(QStringLiteral(".shader")) || path.endsWith(QStringLiteral(".txt")) || path.endsWith(QStringLiteral(".map"))) {
		return QStringLiteral("text");
	}
	if (hint.contains(QStringLiteral("audio")) || path.endsWith(QStringLiteral(".wav")) || path.endsWith(QStringLiteral(".ogg")) || path.endsWith(QStringLiteral(".mp3"))) {
		return QStringLiteral("audio");
	}
	if (hint.contains(QStringLiteral("model")) || path.endsWith(QStringLiteral(".mdl")) || path.endsWith(QStringLiteral(".md2")) || path.endsWith(QStringLiteral(".md3")) || path.endsWith(QStringLiteral(".iqm"))) {
		return QStringLiteral("model");
	}
	if (path.endsWith(QStringLiteral(".bsp"))) {
		return QStringLiteral("map");
	}
	return QStringLiteral("binary");
}

QString packageCompositionBucketLabel(const QString& id)
{
	if (id == QStringLiteral("directory")) {
		return ApplicationShell::tr("Directories");
	}
	if (id == QStringLiteral("archive")) {
		return ApplicationShell::tr("Nested Archives");
	}
	if (id == QStringLiteral("image")) {
		return ApplicationShell::tr("Images");
	}
	if (id == QStringLiteral("text")) {
		return ApplicationShell::tr("Text And Maps");
	}
	if (id == QStringLiteral("audio")) {
		return ApplicationShell::tr("Audio");
	}
	if (id == QStringLiteral("model")) {
		return ApplicationShell::tr("Models");
	}
	if (id == QStringLiteral("map")) {
		return ApplicationShell::tr("Compiled Maps");
	}
	return ApplicationShell::tr("Binary Or Unknown");
}

QVector<SummaryBucket> packageCompositionBuckets(const QVector<PackageEntry>& entries)
{
	QVector<SummaryBucket> buckets;
	for (const PackageEntry& entry : entries) {
		const QString id = packageCompositionBucketId(entry);
		auto found = std::find_if(buckets.begin(), buckets.end(), [&](const SummaryBucket& bucket) {
			return bucket.id == id;
		});
		if (found == buckets.end()) {
			buckets.push_back({id, packageCompositionBucketLabel(id), 0, 0});
			found = buckets.end() - 1;
		}
		++found->count;
		found->bytes += entry.kind == PackageEntryKind::File ? entry.sizeBytes : 0;
	}
	std::sort(buckets.begin(), buckets.end(), [](const SummaryBucket& left, const SummaryBucket& right) {
		if (left.bytes == right.bytes) {
			return left.count > right.count;
		}
		return left.bytes > right.bytes;
	});
	return buckets;
}

const CompilerToolDiscovery* compilerDiscoveryForTool(const CompilerRegistrySummary& summary, const QString& toolId)
{
	for (const CompilerToolDiscovery& discovery : summary.tools) {
		if (discovery.descriptor.id == toolId) {
			return &discovery;
		}
	}
	return nullptr;
}

QString compilerPipelineBar(OperationState state)
{
	switch (state) {
	case OperationState::Completed:
		return QStringLiteral("[##################]");
	case OperationState::Warning:
		return QStringLiteral("[##########........]");
	case OperationState::Failed:
		return QStringLiteral("[###...............]");
	case OperationState::Idle:
	case OperationState::Queued:
	case OperationState::Loading:
	case OperationState::Running:
	case OperationState::Cancelled:
		break;
	}
	return QStringLiteral("[..................]");
}

QString workspaceVirtualPath(const QString& rootPath, const QString& absolutePath)
{
	if (rootPath.trimmed().isEmpty() || absolutePath.trimmed().isEmpty()) {
		return {};
	}
	QString relative = QDir(rootPath).relativeFilePath(absolutePath);
	relative.replace('\\', '/');
	if (relative.startsWith(QStringLiteral("../"))) {
		return {};
	}
	return relative == QStringLiteral(".") ? QString() : relative;
}

QString findGitRoot(const QString& startPath)
{
	QDir dir(startPath);
	while (!dir.path().isEmpty()) {
		if (QFileInfo::exists(dir.filePath(QStringLiteral(".git")))) {
			return dir.absolutePath();
		}
		if (!dir.cdUp()) {
			break;
		}
	}
	return {};
}

QString firstProjectInputForProfile(const CompilerProfileDescriptor& profile, const ProjectManifest& manifest)
{
	if (!profile.inputRequired) {
		return {};
	}
	QStringList nameFilters;
	for (QString extension : profile.inputExtensions) {
		extension = extension.trimmed();
		if (extension.isEmpty()) {
			continue;
		}
		if (extension.startsWith('.')) {
			extension.remove(0, 1);
		}
		nameFilters << QStringLiteral("*.%1").arg(extension);
	}
	if (nameFilters.isEmpty()) {
		nameFilters << QStringLiteral("*");
	}
	for (const QString& sourceFolder : manifest.sourceFolders) {
		const QString root = QDir(manifest.rootPath).absoluteFilePath(sourceFolder);
		QDirIterator iterator(root, nameFilters, QDir::Files, QDirIterator::Subdirectories);
		int inspected = 0;
		while (iterator.hasNext() && inspected < 2000) {
			++inspected;
			return QDir::cleanPath(iterator.next());
		}
	}
	return {};
}

QString quoteCliPart(const QString& part)
{
	if (part.isEmpty()) {
		return QStringLiteral("\"\"");
	}
	bool needsQuotes = false;
	for (const QChar ch : part) {
		if (ch.isSpace() || ch == '"' || ch == '\'') {
			needsQuotes = true;
			break;
		}
	}
	if (!needsQuotes) {
		return part;
	}
	QString escaped = part;
	escaped.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
	escaped.replace(QStringLiteral("\""), QStringLiteral("\\\""));
	return QStringLiteral("\"%1\"").arg(escaped);
}

QString compilerCliEquivalent(const CompilerCommandRequest& request)
{
	QStringList parts = {
		QStringLiteral("vibestudio"),
		QStringLiteral("--cli"),
		QStringLiteral("compiler"),
		QStringLiteral("run"),
		request.profileId,
	};
	if (!request.inputPath.isEmpty()) {
		parts << QStringLiteral("--input") << request.inputPath;
	}
	if (!request.outputPath.isEmpty()) {
		parts << QStringLiteral("--output") << request.outputPath;
	}
	if (!request.workspaceRootPath.isEmpty()) {
		parts << QStringLiteral("--workspace-root") << request.workspaceRootPath;
	}
	if (!request.extraSearchPaths.isEmpty()) {
		parts << QStringLiteral("--compiler-search-paths") << request.extraSearchPaths.join(';');
	}
	parts << QStringLiteral("--register-output");
	for (QString& part : parts) {
		part = quoteCliPart(part);
	}
	return parts.join(' ');
}

CompilerRegistryOptions compilerRegistryOptionsForProject(const QString& projectPath, const StudioSettings& settings)
{
	CompilerRegistryOptions options;
	options.workspaceRootPath = projectPath;
	options.executableOverrides = settings.compilerToolPathOverrides();
	options.probeVersions = false;
	ProjectManifest manifest;
	if (!projectPath.trimmed().isEmpty() && loadProjectManifest(projectPath, &manifest)) {
		options.extraSearchPaths = effectiveProjectCompilerSearchPaths(manifest, options.extraSearchPaths);
		options.executableOverrides = effectiveProjectCompilerToolOverrides(manifest, options.executableOverrides);
	}
	return options;
}

QString statusPathFromGitLine(const QString& line)
{
	if (line.size() < 4) {
		return {};
	}
	QString path = line.mid(3).trimmed();
	const int renameArrow = path.indexOf(QStringLiteral(" -> "));
	if (renameArrow >= 0) {
		path = path.mid(renameArrow + 4).trimmed();
	}
	if (path.startsWith('"') && path.endsWith('"') && path.size() > 1) {
		path = path.mid(1, path.size() - 2);
	}
	return path;
}

QListWidgetItem* disabledListItem(const QString& text)
{
	auto* item = new QListWidgetItem(text);
	item->setFlags(Qt::NoItemFlags);
	return item;
}

} // namespace

ApplicationShell::ApplicationShell(QWidget* parent)
	: QMainWindow(parent)
{
	buildUi();
}

ApplicationShell::~ApplicationShell()
{
	// Both worker threads run a lambda that captures `this` and marshals results
	// back with a queued invocation, so neither may outlive the shell. Ask them
	// to cancel, then wait long enough for a killed child process to be reaped.
	if (m_compilerRunThread) {
		m_compilerRunCancelRequested.store(true);
		m_compilerRunThread->quit();
		m_compilerRunThread->wait(5000);
		m_compilerRunThread = nullptr;
	}
	if (m_buildPipelineThread) {
		m_buildPipelineCancelRequested.store(true);
		m_buildPipelineThread->quit();
		m_buildPipelineThread->wait(15000);
		m_buildPipelineThread = nullptr;
	}
	if (m_gitStatusProcess) {
		m_gitStatusProcess->disconnect(this);
		m_gitStatusProcess->kill();
		m_gitStatusProcess->waitForFinished(1000);
	}
	saveShellState();
	m_settings.sync();
}

namespace {

// Assembles one work surface from the shared parts: header, an optional tool
// bar, and a body that fills the rest of the page.
QWidget* assembleStudioPage(PageHeader* header, QWidget* toolBar, QWidget* body)
{
	auto* page = new QWidget;
	page->setObjectName(QStringLiteral("studioPage"));
	page->setAttribute(Qt::WA_StyledBackground, true);
	auto* layout = new QVBoxLayout(page);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(header);
	if (toolBar) {
		layout->addWidget(toolBar);
	}
	layout->addWidget(body, 1);
	return page;
}

// Wraps a widget in a container with the standard page gutter.
QWidget* padded(QWidget* content, int horizontal = 14, int top = 12, int bottom = 12)
{
	auto* container = new QWidget;
	auto* layout = new QVBoxLayout(container);
	layout->setContentsMargins(horizontal, top, horizontal, bottom);
	layout->setSpacing(10);
	layout->addWidget(content, 1);
	return container;
}

// A captioned column for the sides of a workbench: a small heading, an
// optional hint, and the panel content beneath.
QWidget* captionedPanel(const QString& caption, QWidget* content, QWidget* accessory = nullptr)
{
	auto* panel = new QWidget;
	auto* layout = new QVBoxLayout(panel);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(6);
	auto* header = new QHBoxLayout;
	header->setContentsMargins(2, 0, 0, 0);
	auto* label = new QLabel(caption);
	label->setObjectName(QStringLiteral("sectionLabel"));
	label->setBuddy(content);
	header->addWidget(label);
	header->addStretch(1);
	if (accessory) {
		header->addWidget(accessory);
	}
	layout->addLayout(header);
	layout->addWidget(content, 1);
	return panel;
}

// Only the Value column of a property row is editable.
class PropertyValueDelegate final : public QStyledItemDelegate {
public:
	using QStyledItemDelegate::QStyledItemDelegate;

	QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override
	{
		if (index.column() != 1) {
			return nullptr;
		}
		return QStyledItemDelegate::createEditor(parent, option, index);
	}
};

QTreeWidgetItem* propertyGroup(QTreeWidget* tree, const QString& title)
{
	auto* group = new QTreeWidgetItem(tree, {title});
	group->setFirstColumnSpanned(true);
	group->setFlags(Qt::ItemIsEnabled);
	QFont font = group->font(0);
	font.setBold(true);
	group->setFont(0, font);
	group->setExpanded(true);
	return group;
}

QTreeWidgetItem* propertyRow(QTreeWidgetItem* group, const QString& key, const QString& value, const QString& toolTip = QString())
{
	auto* row = new QTreeWidgetItem(group, {key, value});
	row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
	const QString tip = toolTip.isEmpty() ? QStringLiteral("%1: %2").arg(key, value) : toolTip;
	row->setToolTip(0, tip);
	row->setToolTip(1, tip);
	return row;
}

// Read-only Property / Value grid in the same shape as the entity inspector,
// for metadata panels that would otherwise be a list of "Key: value" lines.
QTreeWidget* createPropertyGrid(const QString& accessibleName, const QString& accessibleDescription)
{
	auto* grid = new QTreeWidget;
	grid->setObjectName(QStringLiteral("propertyGrid"));
	grid->setAccessibleName(accessibleName);
	grid->setAccessibleDescription(accessibleDescription);
	grid->setColumnCount(2);
	grid->setHeaderLabels({ApplicationShell::tr("Property"), ApplicationShell::tr("Value")});
	grid->setRootIsDecorated(true);
	grid->setIndentation(14);
	grid->setUniformRowHeights(true);
	grid->setAlternatingRowColors(true);
	grid->setEditTriggers(QAbstractItemView::NoEditTriggers);
	grid->setSelectionMode(QAbstractItemView::SingleSelection);
	grid->setTextElideMode(Qt::ElideMiddle);
	grid->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	grid->header()->setStretchLastSection(true);
	grid->header()->setSectionResizeMode(0, QHeaderView::Interactive);
	grid->setColumnWidth(0, 124);
	return grid;
}

// Adds core's "Key: value" report lines to a grid group. A line with no key
// becomes a full-width note so nothing the report says is dropped.
void addPropertyLines(QTreeWidgetItem* group, const QStringList& lines)
{
	for (const QString& raw : lines) {
		const QString line = raw.trimmed();
		if (line.isEmpty()) {
			continue;
		}
		const int split = line.indexOf(QStringLiteral(": "));
		if (split > 0 && split <= 40) {
			propertyRow(group, line.left(split), line.mid(split + 2));
			continue;
		}
		auto* note = new QTreeWidgetItem(group, {line});
		note->setFirstColumnSpanned(true);
		note->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
		note->setToolTip(0, line);
	}
}

void addPropertyNote(QTreeWidget* grid, const QString& text)
{
	auto* note = new QTreeWidgetItem(grid, {text});
	note->setFirstColumnSpanned(true);
	note->setFlags(Qt::NoItemFlags);
}

// Shortest faithful form of a model-space number: whole units stay whole.
QString compactNumber(double value)
{
	QString text = QString::number(value, 'f', 2);
	if (text.contains(QLatin1Char('.'))) {
		while (text.endsWith(QLatin1Char('0'))) {
			text.chop(1);
		}
		if (text.endsWith(QLatin1Char('.'))) {
			text.chop(1);
		}
	}
	return text == QStringLiteral("-0") ? QStringLiteral("0") : text;
}

QString compactVector(const ModelVec3& vector)
{
	return QStringLiteral("%1 %2 %3").arg(compactNumber(vector.x), compactNumber(vector.y), compactNumber(vector.z));
}

// The strip above the code editor names the document the way an editor tab
// does: file name, language, and save state. The page header already carries
// the full path, so the strip keeps it for its tooltip.
QString codeDocumentLabel(const QString& path, const QString& state = QString())
{
	QStringList parts {QFileInfo(path).fileName(), studioLanguageDisplayName(studioLanguageForPath(path))};
	if (!state.isEmpty()) {
		parts << state;
	}
	return parts.join(QStringLiteral("  %1  ").arg(QChar(0x00b7)));
}

QString withoutExtension(const QString& path)
{
	const int dot = path.lastIndexOf(QLatin1Char('.'));
	const int slash = std::max(path.lastIndexOf(QLatin1Char('/')), path.lastIndexOf(QLatin1Char('\\')));
	return (dot > slash ? path.left(dot) : path).toLower();
}

void addWarningRows(QTreeWidgetItem* group, const QStringList& warnings)
{
	for (const QString& warning : warnings) {
		auto* row = new QTreeWidgetItem(group, {warning});
		row->setFirstColumnSpanned(true);
		row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
		row->setIcon(0, studioIcon(QStringLiteral("warning"), StudioIconTone::Warning));
		row->setToolTip(0, warning);
	}
}

// Sizes a grid's key column to its longest key, counting each row's
// indentation and icon. Group headings and notes span both columns, so they
// are left out.
void fitPropertyKeyColumn(QTreeWidget* grid)
{
	const QFontMetrics metrics(grid->font());
	const int iconWidth = grid->iconSize().isValid() ? grid->iconSize().width()
		: grid->style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, grid);
	int widest = 0;
	for (QTreeWidgetItemIterator it(grid); *it; ++it) {
		const QTreeWidgetItem* item = *it;
		if (item->isFirstColumnSpanned()) {
			continue;
		}
		int depth = grid->rootIsDecorated() ? 1 : 0;
		for (const QTreeWidgetItem* parent = item->parent(); parent; parent = parent->parent()) {
			++depth;
		}
		const int icon = item->icon(0).isNull() ? 0 : iconWidth + 6;
		widest = std::max(widest, depth * grid->indentation() + icon + metrics.horizontalAdvance(item->text(0)) + 20);
	}
	// Keys never take more than half of a laid-out grid, so values stay legible
	// in a narrow inspector or at a large text scale.
	const int available = grid->viewport()->width();
	const int cap = available > 160 ? std::max(96, available / 2) : 260;
	grid->setColumnWidth(0, std::clamp(widest, 96, cap));
}

// Model metadata as a property grid: warnings first when there are any, then
// the counts that matter, then one group each for surfaces, animations, skins,
// and whatever else the decoder reported.
void populateModelDetails(QTreeWidget* grid, const ModelMesh& mesh, const QString& resolvedSkinPath)
{
	if (!grid) {
		return;
	}
	grid->clear();
	const QString formatName = mesh.formatName.isEmpty() ? modelMeshFormatDisplayName(mesh.format) : mesh.formatName;
	if (!mesh.error.isEmpty()) {
		QTreeWidgetItem* model = propertyGroup(grid, ApplicationShell::tr("Model"));
		propertyRow(model, ApplicationShell::tr("Format"), formatName);
		propertyRow(model, ApplicationShell::tr("Error"), mesh.error)->setIcon(0, studioIcon(QStringLiteral("error"), StudioIconTone::Danger));
		return;
	}
	if (mesh.formatId.isEmpty() && mesh.frameCount == 0 && mesh.surfaceCount == 0) {
		addPropertyNote(grid, ApplicationShell::tr("No model metadata could be read from this entry."));
		return;
	}

	QStringList warnings;
	for (const ModelSurface& surface : mesh.surfaces) {
		for (const QString& warning : surface.warnings) {
			warnings << ApplicationShell::tr("Surface %1: %2").arg(surface.index).arg(warning);
		}
	}
	warnings += mesh.warnings;
	if (!mesh.skinPaths.isEmpty() && resolvedSkinPath.isEmpty() && mesh.embeddedSkins.isEmpty()) {
		warnings << ApplicationShell::tr("None of the skins this model names could be found in the package, so it draws flat shaded.");
	}
	if (!warnings.isEmpty()) {
		addWarningRows(propertyGroup(grid, ApplicationShell::tr("Warnings (%1)").arg(warnings.size())), warnings);
	}

	const QString separator = QStringLiteral("  %1  ").arg(QChar(0x00b7));
	QTreeWidgetItem* model = propertyGroup(grid, ApplicationShell::tr("Model"));
	propertyRow(model, ApplicationShell::tr("Format"), mesh.version > 0
			? ApplicationShell::tr("%1, version %2").arg(formatName).arg(mesh.version)
			: formatName);
	propertyRow(model, ApplicationShell::tr("Frames"), QString::number(mesh.frameCount));
	propertyRow(model, ApplicationShell::tr("Surfaces"), QString::number(mesh.surfaceCount));
	if (mesh.geometryAvailable) {
		propertyRow(model, ApplicationShell::tr("Vertices"), QString::number(mesh.vertexCount));
		propertyRow(model, ApplicationShell::tr("Triangles"), QString::number(mesh.triangleCount));
		propertyRow(model, ApplicationShell::tr("Bounds"), ApplicationShell::tr("%1 to %2").arg(compactVector(mesh.mins), compactVector(mesh.maxs)));
		propertyRow(model, ApplicationShell::tr("Radius"), compactNumber(mesh.boundingRadius()));
	} else {
		propertyRow(model, ApplicationShell::tr("Geometry"), ApplicationShell::tr("Header only; this format's geometry is not decoded yet."));
	}
	propertyRow(model, ApplicationShell::tr("Tags"), QString::number(mesh.tagCount));

	if (!mesh.surfaces.isEmpty()) {
		QTreeWidgetItem* surfaces = propertyGroup(grid, ApplicationShell::tr("Surfaces (%1)").arg(mesh.surfaces.size()));
		for (const ModelSurface& surface : mesh.surfaces) {
			propertyRow(surfaces, surface.name.isEmpty() ? ApplicationShell::tr("Surface %1").arg(surface.index) : surface.name,
				QStringList {ApplicationShell::tr("%n vert(ex)(ices)", nullptr, surface.vertexCount),
					ApplicationShell::tr("%n triangle(s)", nullptr, static_cast<int>(surface.triangles.size()))}
					.join(separator));
		}
	}

	if (!mesh.animations.isEmpty()) {
		QTreeWidgetItem* animations = propertyGroup(grid, ApplicationShell::tr("Animations (%1)").arg(mesh.animations.size()));
		for (const ModelAnimation& animation : mesh.animations) {
			propertyRow(animations, animation.name, animation.frameCount > 1
					? ApplicationShell::tr("Frames %1 to %2").arg(animation.firstFrame).arg(animation.firstFrame + animation.frameCount - 1)
					: ApplicationShell::tr("Frame %1").arg(animation.firstFrame));
		}
	}

	const int skinCount = static_cast<int>(mesh.embeddedSkins.size() + mesh.skinPaths.size());
	if (skinCount > 0) {
		QTreeWidgetItem* skins = propertyGroup(grid, ApplicationShell::tr("Skins (%1)").arg(skinCount));
		for (const ModelEmbeddedSkin& skin : mesh.embeddedSkins) {
			const QString size = ApplicationShell::tr("%1 x %2").arg(skin.image.width()).arg(skin.image.height());
			propertyRow(skins, ApplicationShell::tr("Embedded %1").arg(skin.index), skin.groupFrameCount > 1
					? ApplicationShell::tr("%1, group of %2").arg(size).arg(skin.groupFrameCount)
					: size);
		}
		// The skin loader tries each idTech image extension, so the file it
		// loaded can differ from the named path by its extension only.
		bool marked = false;
		for (int index = 0; index < mesh.skinPaths.size(); ++index) {
			const QString& path = mesh.skinPaths.at(index);
			QTreeWidgetItem* row = propertyRow(skins, ApplicationShell::tr("Skin %1").arg(index), path);
			if (!marked && !resolvedSkinPath.isEmpty() && withoutExtension(path) == withoutExtension(resolvedSkinPath)) {
				marked = true;
				row->setIcon(0, studioIcon(QStringLiteral("check"), StudioIconTone::Success));
				row->setToolTip(1, ApplicationShell::tr("Drawn in the viewport, loaded from %1").arg(resolvedSkinPath));
			}
		}
	}

	if (!mesh.detailLines.isEmpty()) {
		addPropertyLines(propertyGroup(grid, ApplicationShell::tr("Header")), mesh.detailLines);
	}
	fitPropertyKeyColumn(grid);
}

// Audio metadata as a property grid. Core's report ends with a text sketch of
// the waveform for the CLI; the page paints the real envelope instead.
void populateAudioDetails(QTreeWidget* grid, const PackagePreview& preview)
{
	if (!grid) {
		return;
	}
	grid->clear();
	QStringList lines = preview.assetDetailLines;
	if (!preview.audioWaveformLines.isEmpty()) {
		const qsizetype start = lines.indexOf(preview.audioWaveformLines.first());
		if (start >= 0) {
			lines.remove(start, std::min(preview.audioWaveformLines.size(), lines.size() - start));
			if (start > 0 && lines.at(start - 1).trimmed().endsWith(QLatin1Char(':'))) {
				lines.removeAt(start - 1);
			}
		}
	}
	if (lines.isEmpty()) {
		addPropertyNote(grid, ApplicationShell::tr("No audio metadata could be read from this entry."));
		return;
	}
	addPropertyLines(grid->invisibleRootItem(), lines);
	fitPropertyKeyColumn(grid);
}

void markFlat(QAbstractItemView* view)
{
	view->setProperty("flat", true);
}

// Default width for a side pane at the current text scale. Side panes hold
// text, so they widen with it; the square root keeps 200% text from squeezing
// the viewport or editor between them down to nothing.
int scaledPane(int base)
{
	const double scale = std::clamp(currentStudioTheme().textScalePercent, 50, 400) / 100.0;
	return static_cast<int>(base * std::sqrt(scale) + 0.5);
}

// Hosts a page's LoadingPane above its workbench. refreshSurfaceStates()
// hides the host while the pane has nothing to report (idle or completed), so
// the strip only takes space for progress, warnings, and failures.
QWidget* statusStripHost(LoadingPane* pane)
{
	QWidget* host = padded(pane, 14, 10, 0);
	host->setObjectName(QStringLiteral("statusStrip"));
	return host;
}

} // namespace

void ApplicationShell::buildUi()
{
	m_buildingUi = true;
	setWindowTitle(tr("VibeStudio"));
	setAcceptDrops(true);
	resize(1440, 900);
	setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks | QMainWindow::GroupedDragging);

	buildCommands();
	buildMenuBar();
	buildToolBar();
	buildStatusBar();

	auto* root = new QWidget;
	root->setObjectName(QStringLiteral("studioRoot"));
	auto* rootLayout = new QHBoxLayout(root);
	rootLayout->setContentsMargins(0, 0, 0, 0);
	rootLayout->setSpacing(0);

	m_modeRail = new ModeRail;
	QVector<ModeRailEntry> railEntries;
	for (const StudioModeDescriptor& descriptor : studioModeDescriptors()) {
		ModeRailEntry entry;
		entry.id = static_cast<int>(descriptor.mode);
		entry.label = descriptor.label;
		const QString shortcut = m_commands ? m_commands->shortcutForCommand(modeCommandId(descriptor.mode)) : QString();
		entry.hint = shortcut.isEmpty() ? descriptor.hint : tr("%1 (%2)").arg(descriptor.hint, shortcut);
		entry.iconName = descriptor.iconName;
		// Dividers group the rail into home, content, assets and code, and
		// shipping, while keeping Ctrl+1..9 in rail order.
		entry.startsGroup = descriptor.mode == StudioMode::Levels || descriptor.mode == StudioMode::Packages || descriptor.mode == StudioMode::Build;
		entry.pinnedToBottom = descriptor.mode == StudioMode::Settings;
		railEntries.push_back(entry);
	}
	m_modeRail->setEntries(railEntries);
	m_modeRail->setCompact(m_settings.shellModeRailCompact());
	rootLayout->addWidget(m_modeRail);

	m_modeStack = new QStackedWidget;
	m_modeStack->setObjectName("modeStack");
	m_modeStack->setAccessibleName(tr("Studio work surface"));
	m_modeStack->addWidget(buildWorkspacePage());
	m_modeStack->addWidget(buildLevelsPage());
	m_modeStack->addWidget(buildModelsPage());
	m_modeStack->addWidget(buildTexturesPage());
	m_modeStack->addWidget(buildAudioPage());
	m_modeStack->addWidget(buildPackagesPage());
	m_modeStack->addWidget(buildCodePage());
	m_modeStack->addWidget(buildShadersPage());
	m_modeStack->addWidget(buildBuildPage());
	m_modeStack->addWidget(buildSettingsPage());
	rootLayout->addWidget(m_modeStack, 1);

	setCentralWidget(root);
	buildSidePanel();

	// Lists of paths and names elide instead of scrolling sideways.
	for (QListWidget* list : {m_recentProjects, m_gameInstallations, m_packageEntries, m_textureEntries, m_modelEntries,
			 m_audioEntries, m_workspaceSearchResults, m_changedFiles, m_dependencyGraph, m_recentActivityTimeline,
			 m_activityTasks, m_levelMapObjects, m_levelMapStatistics, m_levelMapView, m_advancedCodeTree, m_compilerPipeline}) {
		useElidingRows(list);
	}

	// A view lays its rows out with the metrics it has when its first items
	// arrive, and polishing it later does not redo that layout. Lists filled
	// while their page was still unpolished (the Settings categories are
	// filled once, at construction) would keep unpadded row positions under
	// padded rows, so polish every view now and lay it out again.
	for (QAbstractItemView* view : findChildren<QAbstractItemView*>()) {
		view->ensurePolished();
		view->doItemsLayout();
	}

	seedActivityCenter();
	refreshWorkspaceDashboard();
	refreshWorkspaceContextPanels();
	refreshSetupPanel();
	refreshRecentProjects();
	refreshGameInstallations();
	refreshPackageBrowser();
	refreshCompilerPipelineSummary();
	refreshBuildSurface();
	refreshLevelMapWorkbench();

	// External change detection. One second is well under the time it takes a
	// user to notice a file changed behind their back, and poll() only reads
	// files whose metadata moved, so an idle studio costs a stat per open
	// document per second.
	m_documentWatchTimer = new QTimer(this);
	m_documentWatchTimer->setInterval(1000);
	m_documentWatchTimer->setTimerType(Qt::CoarseTimer);
	connect(m_documentWatchTimer, &QTimer::timeout, this, &ApplicationShell::pollWatchedDocuments);
	m_documentWatchTimer->start();
	reportPreviousSessionCrash();
	refreshAdvancedStudioSurface();
	refreshCodeWorkspaceTree();
	refreshPreferenceControls();
	// Remember the built-in proportions before any saved state replaces them,
	// so View > Reset Layout can return to them.
	for (auto it = m_layoutSplitters.cbegin(); it != m_layoutSplitters.cend(); ++it) {
		if (it.value()) {
			m_defaultLayoutStates.insert(it.key(), it.value()->saveState());
		}
	}
	loadShellState();
	applyPreferencesToUi();
	updateInspector();
	refreshStatusChips();
	refreshCommandEnablement();
	refreshSurfaceStates();
	statusBar()->showMessage(tr("Studio ready"), 4000);
	// Park focus on the work surface so no control starts with a focus ring;
	// Tab then reaches the rail, the page, and the panels in order.
	QTimer::singleShot(0, this, [this]() {
		if (m_modeStack) {
			m_modeStack->setFocus(Qt::OtherFocusReason);
		}
	});

	connect(m_modeRail, &ModeRail::currentIdChanged, this, [this](int id) {
		if (id < 0 || m_buildingUi) {
			return;
		}
		setMode(static_cast<StudioMode>(id));
	});
	connect(m_modeRail, &ModeRail::compactChanged, this, [this](bool compact) {
		m_settings.setShellModeRailCompact(compact);
	});

	auto persistPreferenceChange = [this]() {
		savePreferenceControls();
	};
	connect(m_localeCombo, &QComboBox::currentIndexChanged, this, persistPreferenceChange);
	connect(m_themeCombo, &QComboBox::currentIndexChanged, this, persistPreferenceChange);
	connect(m_textScaleCombo, &QComboBox::currentIndexChanged, this, persistPreferenceChange);
	connect(m_densityCombo, &QComboBox::currentIndexChanged, this, persistPreferenceChange);
	connect(m_editorProfileCombo, &QComboBox::currentIndexChanged, this, persistPreferenceChange);
	connect(m_reducedMotion, &QCheckBox::toggled, this, persistPreferenceChange);
	connect(m_textToSpeech, &QCheckBox::toggled, this, persistPreferenceChange);
	connect(m_aiFreeMode, &QCheckBox::toggled, this, persistPreferenceChange);
	connect(m_aiCloudConnectors, &QCheckBox::toggled, this, persistPreferenceChange);
	connect(m_aiAgenticWorkflows, &QCheckBox::toggled, this, persistPreferenceChange);
	connect(m_aiReasoningConnectorCombo, &QComboBox::currentIndexChanged, this, persistPreferenceChange);
	connect(m_aiCodingConnectorCombo, &QComboBox::currentIndexChanged, this, persistPreferenceChange);
	connect(m_aiVisionConnectorCombo, &QComboBox::currentIndexChanged, this, persistPreferenceChange);
	connect(m_aiImageConnectorCombo, &QComboBox::currentIndexChanged, this, persistPreferenceChange);
	connect(m_aiAudioConnectorCombo, &QComboBox::currentIndexChanged, this, persistPreferenceChange);
	connect(m_aiVoiceConnectorCombo, &QComboBox::currentIndexChanged, this, persistPreferenceChange);
	connect(m_aiThreeDConnectorCombo, &QComboBox::currentIndexChanged, this, persistPreferenceChange);
	connect(m_aiEmbeddingsConnectorCombo, &QComboBox::currentIndexChanged, this, persistPreferenceChange);
	connect(m_aiLocalConnectorCombo, &QComboBox::currentIndexChanged, this, persistPreferenceChange);

	m_buildingUi = false;
}

QWidget* ApplicationShell::buildWorkspacePage()
{
	auto* header = new PageHeader(QStringLiteral("home"), tr("Workspace"));
	header->setAccessibleName(tr("Workspace header"));
	m_pageHeaders.insert(static_cast<int>(StudioMode::Workspace), header);

	auto* detectInstalls = createButton(tr("Detect Installs"), QStringLiteral("search"));
	detectInstalls->setAccessibleName(tr("Detect game installations"));
	detectInstalls->setToolTip(tr("Scan common Steam and GOG library roots for confirmable game installation candidates."));
	connect(detectInstalls, &QPushButton::clicked, this, [this]() {
		detectGameInstallationProfiles();
	});
	header->addActionWidget(detectInstalls);

	auto* initManifest = createButton(tr("Initialize Manifest"), QStringLiteral("file"));
	initManifest->setAccessibleName(tr("Initialize project manifest"));
	initManifest->setToolTip(tr("Create or refresh the .vibestudio/project.json manifest for the current project folder."));
	connect(initManifest, &QPushButton::clicked, this, [this]() {
		initializeCurrentProjectManifest();
	});
	header->addActionWidget(initManifest);

	auto* openProject = createButton(tr("Open Project"), QStringLiteral("folder-open"), QStringLiteral("primary"));
	openProject->setAccessibleName(tr("Open project folder"));
	openProject->setToolTip(tr("Choose a project folder and add it to recent projects."));
	connect(openProject, &QPushButton::clicked, this, [this]() {
		openProjectFolder();
	});
	header->addActionWidget(openProject);

	auto* content = new QWidget;
	auto* contentLayout = new QVBoxLayout(content);
	contentLayout->setContentsMargins(18, 16, 18, 18);
	contentLayout->setSpacing(14);

	m_workspaceState = new LoadingPane;
	m_workspaceState->setAccessibleName(tr("Workspace dashboard state"));
	m_workspaceState->setPlaceholderRows({
		tr("Project manifest"),
		tr("Project health"),
		tr("Linked installation"),
	});
	contentLayout->addWidget(m_workspaceState);

	// Jump tiles: one per work surface, so the dashboard doubles as a start page.
	auto* tiles = new QGridLayout;
	tiles->setHorizontalSpacing(10);
	tiles->setVerticalSpacing(10);
	int tileIndex = 0;
	for (const StudioModeDescriptor& descriptor : studioModeDescriptors()) {
		if (descriptor.mode == StudioMode::Workspace || descriptor.mode == StudioMode::Settings) {
			continue;
		}
		auto* tile = new QToolButton;
		tile->setObjectName(QStringLiteral("tileButton"));
		tile->setIcon(studioIcon(descriptor.iconName, StudioIconTone::Accent, StudioIconAlignment::Leading));
		setBaseIconSize(tile, QSize(32, 22));
		tile->setText(descriptor.label);
		tile->setToolTip(descriptor.hint);
		tile->setAccessibleName(tr("Go to %1").arg(descriptor.label));
		tile->setAccessibleDescription(descriptor.hint);
		tile->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
		tile->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		tile->setMinimumHeight(44);
		tile->setFocusPolicy(Qt::TabFocus);
		const StudioMode mode = descriptor.mode;
		connect(tile, &QToolButton::clicked, this, [this, mode]() {
			setMode(mode);
		});
		tiles->addWidget(tile, tileIndex / 4, tileIndex % 4);
		++tileIndex;
	}
	contentLayout->addLayout(tiles);

	// Two balanced rows of cards, each sized to its content: health beside the
	// project lists, then details beside recent activity. The trailing stretch
	// takes any spare height instead of the cards ballooning to fill it.
	auto* grid = new QGridLayout;
	grid->setHorizontalSpacing(14);
	grid->setVerticalSpacing(14);
	grid->setColumnStretch(0, 3);
	grid->setColumnStretch(1, 2);
	contentLayout->addLayout(grid);
	auto* listsColumn = new QVBoxLayout;
	listsColumn->setSpacing(14);

	// Left column: project health and context, then the project detail drawer.
	auto* contextCard = new CardFrame(tr("Project Health"));
	auto* workspaceTabs = createPanelTabs(tr("Workspace context panels"));
	workspaceTabs->setObjectName("workspaceContextTabs");
	workspaceTabs->setAccessibleDescription(tr("Problems, search, changed files, dependency graph, and recent activity for the active workspace."));

	m_projectProblems = new QListWidget;
	m_projectProblems->setObjectName("projectProblems");
	m_projectProblems->setAccessibleName(tr("Project problems"));
	m_projectProblems->setAccessibleDescription(tr("Project health warnings, blocking issues, and next actions."));
	m_projectProblems->setWordWrap(true);
	markFlat(m_projectProblems);
	workspaceTabs->addTab(m_projectProblems, studioIcon(QStringLiteral("warning")), tr("Problems"));

	auto* searchPanel = new QWidget;
	auto* searchLayout = new QVBoxLayout(searchPanel);
	searchLayout->setContentsMargins(0, 8, 0, 0);
	searchLayout->setSpacing(8);
	m_workspaceSearch = new QLineEdit;
	m_workspaceSearch->setObjectName("workspaceSearch");
	m_workspaceSearch->setAccessibleName(tr("Workspace search"));
	m_workspaceSearch->setAccessibleDescription(tr("Searches mounted package entries and project files by path."));
	m_workspaceSearch->setPlaceholderText(tr("Search project files and mounted package entries"));
	m_workspaceSearch->setClearButtonEnabled(true);
	m_workspaceSearch->addAction(studioIcon(QStringLiteral("search"), StudioIconTone::Muted), QLineEdit::LeadingPosition);
	connect(m_workspaceSearch, &QLineEdit::textChanged, this, [this]() {
		scheduleWorkspaceSearch();
	});
	searchLayout->addWidget(m_workspaceSearch);
	m_workspaceSearchResults = new QListWidget;
	m_workspaceSearchResults->setObjectName("workspaceSearchResults");
	m_workspaceSearchResults->setAccessibleName(tr("Workspace search results"));
	m_workspaceSearchResults->setAccessibleDescription(tr("Matching project file paths and mounted package virtual paths."));
	markFlat(m_workspaceSearchResults);
	searchLayout->addWidget(m_workspaceSearchResults, 1);
	auto* searchActions = new QHBoxLayout;
	searchActions->addStretch(1);
	m_revealWorkspacePath = createButton(tr("Reveal"), QStringLiteral("external"));
	m_revealWorkspacePath->setAccessibleName(tr("Reveal selected workspace path"));
	m_revealWorkspacePath->setToolTip(tr("Open the containing folder for the selected project file."));
	connect(m_revealWorkspacePath, &QPushButton::clicked, this, [this]() {
		revealSelectedWorkspacePath();
	});
	searchActions->addWidget(m_revealWorkspacePath);
	m_copyWorkspaceVirtualPath = createButton(tr("Copy Path"), QStringLiteral("copy"));
	m_copyWorkspaceVirtualPath->setAccessibleName(tr("Copy selected virtual path"));
	m_copyWorkspaceVirtualPath->setToolTip(tr("Copy the selected project-relative or package virtual path."));
	connect(m_copyWorkspaceVirtualPath, &QPushButton::clicked, this, [this]() {
		copySelectedWorkspaceVirtualPath();
	});
	searchActions->addWidget(m_copyWorkspaceVirtualPath);
	searchLayout->addLayout(searchActions);
	workspaceTabs->addTab(searchPanel, studioIcon(QStringLiteral("search")), tr("Search"));

	m_changedFiles = new QListWidget;
	m_changedFiles->setObjectName("changedFiles");
	m_changedFiles->setAccessibleName(tr("Changed and staged files"));
	m_changedFiles->setAccessibleDescription(tr("Git changed and staged files for the active project, when available."));
	markFlat(m_changedFiles);
	workspaceTabs->addTab(m_changedFiles, studioIcon(QStringLiteral("edit")), tr("Changes"));

	m_dependencyGraph = new QListWidget;
	m_dependencyGraph->setObjectName("dependencyGraph");
	m_dependencyGraph->setAccessibleName(tr("Project dependency graph"));
	m_dependencyGraph->setAccessibleDescription(tr("Placeholder dependency graph nodes for project roots, installs, packages, and compilers."));
	markFlat(m_dependencyGraph);
	workspaceTabs->addTab(m_dependencyGraph, studioIcon(QStringLiteral("tree")), tr("Graph"));

	m_recentActivityTimeline = new QListWidget;
	m_recentActivityTimeline->setObjectName("recentActivityTimeline");
	m_recentActivityTimeline->setAccessibleName(tr("Recent activity timeline"));
	m_recentActivityTimeline->setAccessibleDescription(tr("Recent project, package, setup, and task events."));
	markFlat(m_recentActivityTimeline);
	workspaceTabs->addTab(m_recentActivityTimeline, studioIcon(QStringLiteral("clock")), tr("Timeline"));
	workspaceTabs->setMinimumHeight(300);
	contextCard->bodyLayout()->addWidget(workspaceTabs, 1);
	grid->addWidget(contextCard, 0, 0);

	m_workspaceDrawer = new DetailDrawer;
	m_workspaceDrawer->setAccessibleName(tr("Workspace dashboard details"));
	m_workspaceDrawer->setTitle(tr("Workspace Details"));
	m_workspaceDrawer->setSubtitle(tr("Open a project folder to inspect manifest and health details."));
	m_workspaceDrawer->setMinimumHeight(280);
	grid->addWidget(m_workspaceDrawer, 1, 0);

	// Right column: recent projects, installations, and recent activity.
	auto* recentCard = new CardFrame(tr("Recent Projects"));
	m_recentSummary = recentCard->metaLabel();
	m_recentSummary->setVisible(true);
	m_recentProjects = new QListWidget;
	m_recentProjects->setObjectName("recentProjects");
	m_recentProjects->setAccessibleName(tr("Recent projects"));
	m_recentProjects->setAccessibleDescription(tr("Project folders remembered from previous sessions."));
	m_recentProjects->setSelectionMode(QAbstractItemView::SingleSelection);
	m_recentProjects->setUniformItemSizes(false);
	m_recentProjects->setMinimumHeight(110);
	m_recentProjects->setMaximumHeight(170);
	markFlat(m_recentProjects);
	connect(m_recentProjects, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
		activateRecentProject(item);
	});
	connect(m_recentProjects, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
		activateRecentProject(item);
	});
	connect(m_recentProjects, &QListWidget::itemSelectionChanged, this, [this]() {
		const QListWidgetItem* item = m_recentProjects->currentItem();
		if (item) {
			updateInspectorForProject(item->data(Qt::UserRole).toString());
		} else {
			updateInspector();
		}
	});
	recentCard->bodyLayout()->addWidget(m_recentProjects, 1);
	auto* recentActions = new QHBoxLayout;
	recentActions->addStretch(1);
	auto* removeRecent = createButton(tr("Remove"), QStringLiteral("minus"), QStringLiteral("ghost"));
	removeRecent->setAccessibleName(tr("Remove selected recent project"));
	removeRecent->setToolTip(tr("Remove the selected project from the recent list without touching its files."));
	connect(removeRecent, &QPushButton::clicked, this, [this]() {
		removeSelectedRecentProject();
	});
	recentActions->addWidget(removeRecent);
	auto* clearRecent = createButton(tr("Clear"), QStringLiteral("trash"), QStringLiteral("ghost"));
	clearRecent->setAccessibleName(tr("Clear recent projects"));
	clearRecent->setToolTip(tr("Clear the recent-project list without touching project files."));
	connect(clearRecent, &QPushButton::clicked, this, [this]() {
		clearRecentProjects();
	});
	recentActions->addWidget(clearRecent);
	recentCard->bodyLayout()->addLayout(recentActions);
	listsColumn->addWidget(recentCard);

	auto* installCard = new CardFrame(tr("Game Installations"));
	m_installSummary = installCard->metaLabel();
	m_installSummary->setAccessibleName(tr("Game installation summary"));
	m_installSummary->setVisible(true);
	m_gameInstallations = new QListWidget;
	m_gameInstallations->setObjectName("gameInstallations");
	m_gameInstallations->setAccessibleName(tr("Game installations"));
	m_gameInstallations->setAccessibleDescription(tr("Manual read-only game installation profiles used by projects, packages, compilers, and launch workflows."));
	m_gameInstallations->setSelectionMode(QAbstractItemView::SingleSelection);
	m_gameInstallations->setUniformItemSizes(false);
	m_gameInstallations->setMinimumHeight(96);
	m_gameInstallations->setMaximumHeight(150);
	markFlat(m_gameInstallations);
	connect(m_gameInstallations, &QListWidget::itemSelectionChanged, this, [this]() {
		if (m_importDetectedInstall) {
			m_importDetectedInstall->setEnabled(selectedDetectedInstallationIndex() >= 0);
		}
		refreshInspectorDrawerForSettings();
	});
	installCard->bodyLayout()->addWidget(m_gameInstallations, 1);
	auto* installActions = new QHBoxLayout;
	installActions->setSpacing(6);
	auto* addInstall = createButton(tr("Add"), QStringLiteral("plus"));
	addInstall->setAccessibleName(tr("Add game installation"));
	addInstall->setToolTip(tr("Create a manual, read-only installation profile from a selected folder."));
	connect(addInstall, &QPushButton::clicked, this, [this]() {
		addGameInstallationProfile();
	});
	installActions->addWidget(addInstall);
	m_importDetectedInstall = createButton(tr("Import Detected"), QStringLiteral("import"));
	m_importDetectedInstall->setAccessibleName(tr("Import detected installation"));
	m_importDetectedInstall->setToolTip(tr("Save the selected detected Steam or GOG candidate as a confirmable read-only profile."));
	connect(m_importDetectedInstall, &QPushButton::clicked, this, [this]() {
		importSelectedDetectedInstallation();
	});
	installActions->addWidget(m_importDetectedInstall);
	installActions->addStretch(1);
	auto* selectInstall = createButton(tr("Use"), QStringLiteral("check"));
	selectInstall->setAccessibleName(tr("Select game installation"));
	selectInstall->setToolTip(tr("Use the selected installation profile as the current default."));
	connect(selectInstall, &QPushButton::clicked, this, [this]() {
		selectCurrentGameInstallation();
	});
	installActions->addWidget(selectInstall);
	auto* removeInstall = createButton(tr("Remove"), QStringLiteral("minus"), QStringLiteral("ghost"));
	removeInstall->setAccessibleName(tr("Remove game installation"));
	removeInstall->setToolTip(tr("Remove the selected installation profile without touching files."));
	connect(removeInstall, &QPushButton::clicked, this, [this]() {
		removeSelectedGameInstallation();
	});
	installActions->addWidget(removeInstall);
	installCard->bodyLayout()->addLayout(installActions);
	listsColumn->addWidget(installCard);
	grid->addLayout(listsColumn, 0, 1);

	auto* activityCard = new CardFrame(tr("Recent Activity"));
	m_activityTimelineChart = new ActivityTimelineChart;
	m_activityTimelineChart->setAccessibleName(tr("Recent activity chart"));
	m_activityTimelineChart->setAccessibleDescription(tr("Recent package, compiler, and setup tasks with state glyphs and duration bars."));
	m_activityTimelineChart->setEmptyText(tr("No recent activity yet. Open a package or run a build to populate the timeline."));
	m_activityTimelineChart->setMinimumHeight(200);
	activityCard->bodyLayout()->addWidget(m_activityTimelineChart, 1);
	grid->addWidget(activityCard, 1, 1);

	// Assistant proposals are local and staged, so they sit at the foot of the
	// dashboard rather than competing with project state.
	auto* aiCard = new CardFrame(tr("Assistant Proposals"));
	aiCard->setMeta(tr("Drafted locally. Nothing is written until you review it."));
	auto* aiControls = new QHBoxLayout;
	aiControls->setSpacing(8);
	m_advancedAiKind = new QComboBox;
	m_advancedAiKind->setAccessibleName(tr("AI creation kind"));
	m_advancedAiKind->addItem(tr("Shader"), QStringLiteral("shader"));
	m_advancedAiKind->addItem(tr("Entity"), QStringLiteral("entity"));
	m_advancedAiKind->addItem(tr("Package Plan"), QStringLiteral("package"));
	m_advancedAiKind->addItem(tr("Batch Recipe"), QStringLiteral("batch"));
	m_advancedAiKind->addItem(tr("CLI Command"), QStringLiteral("cli"));
	aiControls->addWidget(m_advancedAiKind);
	m_advancedAiPrompt = new QLineEdit;
	m_advancedAiPrompt->setAccessibleName(tr("AI creation prompt"));
	m_advancedAiPrompt->setAccessibleDescription(tr("Prompt for staged, reviewable local AI-assisted creation proposals."));
	m_advancedAiPrompt->setPlaceholderText(tr("Describe what to draft"));
	connect(m_advancedAiPrompt, &QLineEdit::returnPressed, this, [this]() {
		createAdvancedAiProposal();
	});
	aiControls->addWidget(m_advancedAiPrompt, 1);
	m_advancedAiCreate = createButton(tr("Create Proposal"), QStringLiteral("sparkle"));
	m_advancedAiCreate->setAccessibleName(tr("Create AI proposal"));
	m_advancedAiCreate->setToolTip(tr("Draft a reviewable proposal locally. No files are written and no network request is made."));
	connect(m_advancedAiCreate, &QPushButton::clicked, this, [this]() {
		createAdvancedAiProposal();
	});
	aiControls->addWidget(m_advancedAiCreate);
	aiCard->bodyLayout()->addLayout(aiControls);
	m_advancedAiProposalList = new QListWidget;
	m_advancedAiProposalList->setAccessibleName(tr("AI proposal review"));
	m_advancedAiProposalList->setAccessibleDescription(tr("Reviewable AI proposal summary, context, generated actions, and prompt/response log."));
	m_advancedAiProposalList->setMinimumHeight(110);
	m_advancedAiProposalList->setMaximumHeight(220);
	m_advancedAiProposalList->setWordWrap(true);
	markFlat(m_advancedAiProposalList);
	aiCard->bodyLayout()->addWidget(m_advancedAiProposalList, 1);
	contentLayout->addWidget(aiCard);
	contentLayout->addStretch(1);

	return assembleStudioPage(header, nullptr, createScrollSurface(content, tr("Workspace scroll area")));
}

QWidget* ApplicationShell::buildLevelsPage()
{
	auto* header = new PageHeader(QStringLiteral("map"), tr("Levels"));
	header->setAccessibleName(tr("Level editor header"));
	m_pageHeaders.insert(static_cast<int>(StudioMode::Levels), header);

	m_levelMapSaveAs = createButton(tr("Save As"), QStringLiteral("save"));
	m_levelMapSaveAs->setAccessibleName(tr("Save level map as"));
	m_levelMapSaveAs->setToolTip(tr("Write the edited map to a new path without touching the source."));
	connect(m_levelMapSaveAs, &QPushButton::clicked, this, [this]() {
		saveLevelMapAsFromUi();
	});
	header->addActionWidget(m_levelMapSaveAs);

	auto* openLevelMap = createButton(tr("Open Map"), QStringLiteral("folder-open"), QStringLiteral("primary"));
	openLevelMap->setAccessibleName(tr("Open level map"));
	openLevelMap->setToolTip(tr("Choose a Doom WAD map or Quake-family .map source to inspect."));
	connect(openLevelMap, &QPushButton::clicked, this, [this]() {
		openLevelMapFile();
	});
	header->addActionWidget(openLevelMap);

	// Document bar: the source being edited and how to compile it.
	auto* toolBar = createPageToolBar(tr("Level document tools"));
	m_levelMapPath = new QLineEdit;
	m_levelMapPath->setAccessibleName(tr("Level map path"));
	m_levelMapPath->setAccessibleDescription(tr("Absolute or relative path to a Doom WAD map or Quake-family .map file."));
	m_levelMapPath->setPlaceholderText(tr("Map source path"));
	m_levelMapPath->setMinimumWidth(220);
	m_levelMapPath->addAction(studioIcon(QStringLiteral("file"), StudioIconTone::Muted), QLineEdit::LeadingPosition);
	connect(m_levelMapPath, &QLineEdit::returnPressed, this, [this]() {
		loadLevelMapPath(m_levelMapPath->text());
	});
	toolBar->addWidget(m_levelMapPath);

	m_levelMapName = new QLineEdit;
	m_levelMapName->setAccessibleName(tr("Doom map marker"));
	m_levelMapName->setAccessibleDescription(tr("Optional Doom map marker such as MAP01 or E1M1."));
	m_levelMapName->setPlaceholderText(tr("MAP01 / E1M1"));
	m_levelMapName->setMaximumWidth(110);
	toolBar->addWidget(m_levelMapName);

	m_levelMapEngine = new QComboBox;
	m_levelMapEngine->setAccessibleName(tr("Level map engine hint"));
	m_levelMapEngine->setAccessibleDescription(tr("Optional parser hint for Doom, Quake, or Quake III map sources."));
	m_levelMapEngine->setToolTip(tr("Engine family used to parse the map. Auto detects it from the file."));
	m_levelMapEngine->addItem(tr("Auto"), QString());
	m_levelMapEngine->addItem(tr("idTech1"), QStringLiteral("idtech1"));
	m_levelMapEngine->addItem(tr("idTech2"), QStringLiteral("idtech2"));
	m_levelMapEngine->addItem(tr("idTech3"), QStringLiteral("idtech3"));
	toolBar->addWidget(m_levelMapEngine);

	auto* inspectLevelMap = createToolButton(QStringLiteral("refresh"), tr("Reload"), tr("Load the map at this path and refresh entities, textures, validation, and preview state."));
	inspectLevelMap->setAccessibleName(tr("Inspect level map"));
	connect(inspectLevelMap, &QToolButton::clicked, this, [this]() {
		loadLevelMapPath(m_levelMapPath ? m_levelMapPath->text() : QString());
	});
	toolBar->addWidget(inspectLevelMap);
	toolBar->addSeparator();

	m_levelMapCompilerProfile = new QComboBox;
	m_levelMapCompilerProfile->setAccessibleName(tr("Level compiler profile"));
	m_levelMapCompilerProfile->setAccessibleDescription(tr("Compiler profile used for map compile-plan review."));
	m_levelMapCompilerProfile->setToolTip(tr("Compiler profile used by Run Profile."));
	for (const CompilerProfileDescriptor& profile : compilerProfileDescriptors()) {
		m_levelMapCompilerProfile->addItem(profile.displayName, profile.id);
	}
	m_levelMapCompilerProfile->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_levelMapCompilerProfile->setMinimumContentsLength(14);
	toolBar->addWidget(m_levelMapCompilerProfile);

	auto* runProfile = createToolButton(QStringLiteral("play"), tr("Run Profile"), tr("Build a reviewable compiler plan for the loaded map, then run it when the selected profile is runnable."));
	m_levelMapPlanCompile = runProfile;
	m_levelMapPlanCompile->setAccessibleName(tr("Run level map compiler profile"));
	connect(runProfile, &QToolButton::clicked, this, [this]() {
		planLevelMapCompile();
	});
	toolBar->addWidget(runProfile);

	auto* copyCli = createToolButton(QStringLiteral("terminal"), tr("Copy CLI"), tr("Copy the vibestudio --cli command that reproduces this compile."), false);
	m_levelMapCopyCli = copyCli;
	m_levelMapCopyCli->setAccessibleName(tr("Copy level map CLI command"));
	connect(copyCli, &QToolButton::clicked, this, [this]() {
		copyLevelMapCliEquivalent();
	});
	toolBar->addWidget(copyCli);

	m_levelMapState = new LoadingPane;
	m_levelMapState->setAccessibleName(tr("Level map loading state"));
	m_levelMapState->setTitle(tr("Level Map"));
	m_levelMapState->setDetail(tr("Open a Doom WAD map or Quake-family .map source to inspect and edit."));
	m_levelMapState->setPlaceholderRows({
		tr("Map statistics"),
		tr("Entity list"),
		tr("Validation health"),
	});

	// Left: outliner.
	m_levelMapObjects = new QListWidget;
	m_levelMapObjects->setAccessibleName(tr("Level map objects"));
	m_levelMapObjects->setAccessibleDescription(tr("Parsed map entities, Doom things, vertices, linedefs, and Quake brushes."));
	// Multi-selection here and in the viewport are the same set: Ctrl-click or
	// Shift-click builds it in either place and a compound move applies to all
	// of it as one undo command.
	m_levelMapObjects->setSelectionMode(QAbstractItemView::ExtendedSelection);
	connect(m_levelMapObjects, &QListWidget::itemSelectionChanged, this, [this]() {
		refreshLevelMapSelection();
	});
	m_levelMapStatistics = new QListWidget;
	m_levelMapStatistics->setAccessibleName(tr("Level map statistics"));
	auto* outlinerTabs = createPanelTabs(tr("Level outliner"), QTabWidget::South);
	outlinerTabs->addTab(m_levelMapObjects, tr("Objects"));
	outlinerTabs->addTab(m_levelMapStatistics, tr("Statistics"));
	auto* leftPanel = padded(outlinerTabs, 10, 8, 10);
	leftPanel->setMinimumWidth(220);

	// Centre: viewport with its own view controls and a readout beneath.
	auto* viewportBar = createPageToolBar(tr("Map viewport controls"));
	viewportBar->setObjectName(QStringLiteral("viewportToolBar"));
	auto* undo = createToolButton(QStringLiteral("undo"), tr("Undo"), tr("Undo the last map edit."), false);
	connect(undo, &QToolButton::clicked, this, [this]() {
		undoLevelMapEditFromUi();
	});
	viewportBar->addWidget(undo);
	auto* redo = createToolButton(QStringLiteral("redo"), tr("Redo"), tr("Redo the last undone map edit."), false);
	connect(redo, &QToolButton::clicked, this, [this]() {
		redoLevelMapEditFromUi();
	});
	viewportBar->addWidget(redo);
	viewportBar->addSeparator();

	m_levelMapProjection = new QComboBox;
	m_levelMapProjection->setAccessibleName(tr("Map projection"));
	m_levelMapProjection->setToolTip(tr("Choose the orthographic plane the viewport draws."));
	m_levelMapProjection->addItem(tr("Top (X/Y)"), 0);
	m_levelMapProjection->addItem(tr("Front (X/Z)"), 1);
	m_levelMapProjection->addItem(tr("Side (Z/Y)"), 2);
	viewportBar->addWidget(m_levelMapProjection);

	m_levelMapGrid = new QComboBox;
	m_levelMapGrid->setAccessibleName(tr("Grid size"));
	m_levelMapGrid->setToolTip(tr("Grid spacing in world units."));
	for (const int gridSize : {1, 2, 4, 8, 16, 32, 64, 128, 256}) {
		m_levelMapGrid->addItem(tr("Grid %1").arg(gridSize), gridSize);
	}
	m_levelMapGrid->setCurrentIndex(6);
	viewportBar->addWidget(m_levelMapGrid);

	m_levelMapSnap = new QCheckBox(tr("Snap"));
	m_levelMapSnap->setAccessibleName(tr("Snap drags and nudges to the grid"));
	m_levelMapSnap->setToolTip(tr("Drags and arrow-key nudges move by whole grid steps. Turn it off to move by the raw pointer delta."));
	m_levelMapSnap->setChecked(true);
	viewportBar->addWidget(m_levelMapSnap);

	m_levelMapShowThings = new QCheckBox(tr("Things"));
	m_levelMapShowThings->setAccessibleName(tr("Show things and point entities"));
	m_levelMapShowThings->setToolTip(tr("Draw Doom things and point entities."));
	m_levelMapShowThings->setChecked(true);
	viewportBar->addWidget(m_levelMapShowThings);

	m_levelMapShowSectors = new QCheckBox(tr("Sector fill"));
	m_levelMapShowSectors->setAccessibleName(tr("Show Doom sector fills"));
	m_levelMapShowSectors->setToolTip(tr("Shade Doom sectors by light level."));
	m_levelMapShowSectors->setChecked(true);
	viewportBar->addWidget(m_levelMapShowSectors);
	viewportBar->addSeparator();

	auto* zoomFit = createToolButton(QStringLiteral("frame"), tr("Zoom to Fit"), tr("Frame the whole map in the viewport."), false);
	zoomFit->setAccessibleName(tr("Zoom map viewport to fit"));
	connect(zoomFit, &QToolButton::clicked, this, [this]() {
		if (m_levelMapViewport) {
			m_levelMapViewport->zoomToFit();
		}
	});
	viewportBar->addWidget(zoomFit);

	auto* exportImage = createToolButton(QStringLiteral("image"), tr("Export Image"), tr("Write a deterministic SVG picture of the current map for review or documentation."), false);
	exportImage->setAccessibleName(tr("Export map image"));
	connect(exportImage, &QToolButton::clicked, this, [this]() {
		exportLevelMapImage();
	});
	viewportBar->addWidget(exportImage);

	m_levelMapViewport = new MapViewport;
	m_levelMapViewport->setAccessibleName(tr("Map viewport"));
	m_levelMapViewport->setAccessibleDescription(tr("Interactive orthographic view of the loaded map. Click to select, Shift-click to add, Ctrl-click to toggle, drag from empty space to box-select, drag a selected object to move it, arrow keys to nudge by one grid step, wheel to zoom, Tab to cycle objects, Escape to cancel and leave."));
	m_levelMapViewport->setMinimumSize(320, 260);
	connect(m_levelMapViewport, &MapViewport::selectionChanged, this, &ApplicationShell::selectLevelMapObjectFromViewport);
	connect(m_levelMapViewport, &MapViewport::selectionSetChanged, this, &ApplicationShell::syncLevelMapSelectionFromViewport);
	connect(m_levelMapViewport, &MapViewport::moveRequested, this, &ApplicationShell::moveLevelMapSelectionFromViewport);
	connect(m_levelMapViewport, &MapViewport::hoverChanged, this, [this](const QString& summary) {
		if (m_levelMapHover) {
			m_levelMapHover->setText(summary.isEmpty() ? tr("Move the cursor over the map to inspect geometry.") : summary);
		}
	});

	m_levelMapHover = new ElidedLabel(tr("Move the cursor over the map to inspect geometry."));
	m_levelMapHover->setObjectName("viewportReadout");
	m_levelMapHover->setAccessibleName(tr("Map viewport readout"));

	auto* centre = new QWidget;
	auto* centreLayout = new QVBoxLayout(centre);
	centreLayout->setContentsMargins(0, 0, 0, 0);
	centreLayout->setSpacing(0);
	centreLayout->addWidget(viewportBar);
	centreLayout->addWidget(m_levelMapViewport, 1);
	centreLayout->addWidget(m_levelMapHover);

	// Right: inspector tabs.
	auto* entityPanel = new QWidget;
	auto* entityLayout = new QVBoxLayout(entityPanel);
	entityLayout->setContentsMargins(0, 8, 0, 0);
	entityLayout->setSpacing(8);
	auto* entityActions = new QHBoxLayout;
	entityActions->setSpacing(6);
	m_levelMapEditProperty = createButton(tr("Edit Key"), QStringLiteral("edit"));
	m_levelMapEditProperty->setAccessibleName(tr("Edit selected entity key"));
	m_levelMapEditProperty->setToolTip(tr("Change a key on the selected map object."));
	connect(m_levelMapEditProperty, &QPushButton::clicked, this, [this]() {
		editSelectedLevelMapProperty();
	});
	entityActions->addWidget(m_levelMapEditProperty);
	m_levelMapMoveSelection = createButton(tr("Move"), QStringLiteral("move"));
	m_levelMapMoveSelection->setAccessibleName(tr("Move selected map object"));
	m_levelMapMoveSelection->setToolTip(tr("Translate the selected map object by a delta."));
	connect(m_levelMapMoveSelection, &QPushButton::clicked, this, [this]() {
		moveSelectedLevelMapObject();
	});
	entityActions->addWidget(m_levelMapMoveSelection);
	entityActions->addStretch(1);
	entityLayout->addLayout(entityActions);

	// Property grid in the style of idStudio's entity inspector: grouped,
	// collapsible Key / Value rows. Double-click or Enter edits a value in
	// place; spawnflag check boxes toggle their bit.
	m_entityInspector = new QTreeWidget;
	m_entityInspector->setObjectName("entityInspector");
	m_entityInspector->setAccessibleName(tr("Entity inspector"));
	m_entityInspector->setAccessibleDescription(tr("The selected entity's class, keys with their documented meaning, and spawnflag bits. Double-click or press Enter on a value to edit it."));
	m_entityInspector->setColumnCount(2);
	m_entityInspector->setHeaderLabels({tr("Key"), tr("Value")});
	m_entityInspector->setRootIsDecorated(true);
	m_entityInspector->setIndentation(14);
	m_entityInspector->setUniformRowHeights(true);
	m_entityInspector->setAlternatingRowColors(true);
	m_entityInspector->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_entityInspector->setItemDelegate(new PropertyValueDelegate(m_entityInspector));
	m_entityInspector->header()->setStretchLastSection(true);
	m_entityInspector->header()->setSectionResizeMode(0, QHeaderView::Interactive);
	m_entityInspector->setColumnWidth(0, 132);
	m_entityInspector->installEventFilter(this);
	connect(m_entityInspector, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
		if (item && item->flags().testFlag(Qt::ItemIsEditable)) {
			m_entityInspector->editItem(item, 1);
		}
	});
	connect(m_entityInspector, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
		applyEntityInspectorEdit(item, column);
	});
	entityLayout->addWidget(m_entityInspector, 1);

	auto* definitionLabel = new QLabel(tr("Entity definitions"));
	definitionLabel->setObjectName("sectionLabel");
	entityLayout->addWidget(definitionLabel);
	auto* entityDefinitionRow = new QHBoxLayout;
	entityDefinitionRow->setSpacing(6);
	m_entityDefinitionPath = new QLineEdit;
	m_entityDefinitionPath->setAccessibleName(tr("Entity definition path"));
	m_entityDefinitionPath->setAccessibleDescription(tr("A Radiant .def, Valve .fgd, or Quake III .ent file, or a folder of them. Left empty, the project's conventional definition folders are searched."));
	m_entityDefinitionPath->setPlaceholderText(tr("Definition file or folder"));
	m_entityDefinitionPath->setToolTip(tr("Leave empty to search the project's conventional definition folders."));
	connect(m_entityDefinitionPath, &QLineEdit::returnPressed, this, [this]() {
		reloadEntityDefinitions();
	});
	entityDefinitionRow->addWidget(m_entityDefinitionPath, 1);
	auto* browseDefinitions = createToolButton(QStringLiteral("folder-open"), tr("Browse"), tr("Choose a .def, .fgd, or .ent file."), false);
	browseDefinitions->setAccessibleName(tr("Choose entity definition path"));
	connect(browseDefinitions, &QToolButton::clicked, this, [this]() {
		chooseEntityDefinitionPath();
	});
	entityDefinitionRow->addWidget(browseDefinitions);
	auto* reloadDefinitions = createToolButton(QStringLiteral("refresh"), tr("Load"), tr("Parse the definitions and re-check this map's entities against them."), false);
	reloadDefinitions->setAccessibleName(tr("Load entity definitions"));
	connect(reloadDefinitions, &QToolButton::clicked, this, [this]() {
		reloadEntityDefinitions();
	});
	entityDefinitionRow->addWidget(reloadDefinitions);
	entityLayout->addLayout(entityDefinitionRow);
	m_entityDefinitionSummary = new QLabel(tr("No entity definitions loaded. Without them a classname is just a string."));
	m_entityDefinitionSummary->setObjectName("moduleMeta");
	m_entityDefinitionSummary->setAccessibleName(tr("Entity definition summary"));
	m_entityDefinitionSummary->setWordWrap(true);
	entityLayout->addWidget(m_entityDefinitionSummary);

	m_levelMapValidation = new QListWidget;
	m_levelMapValidation->setAccessibleName(tr("Level map validation"));
	m_levelMapValidation->setAccessibleDescription(tr("Validation, map health, texture, entity, leak, and compiler preflight issues."));
	m_levelMapValidation->setWordWrap(true);

	m_levelMapView = new QListWidget;
	m_levelMapView->setAccessibleName(tr("Level map preview"));
	m_levelMapView->setAccessibleDescription(tr("Textual 2D Doom or orthographic brush preview lines for the loaded map."));

	m_levelMapDrawer = new DetailDrawer;
	m_levelMapDrawer->setAccessibleName(tr("Level map detail drawer"));
	m_levelMapDrawer->setTitle(tr("Level Map Details"));
	m_levelMapDrawer->setSubtitle(tr("Inspect map statistics, properties, textures, validation, and undo history."));
	m_levelMapDrawer->setEmbedded(true);

	auto* inspectorTabs = createPanelTabs(tr("Level map preview tabs"), QTabWidget::South);
	inspectorTabs->addTab(entityPanel, tr("Entity"));
	inspectorTabs->addTab(m_levelMapValidation, tr("Health"));
	inspectorTabs->addTab(m_levelMapDrawer, tr("Details"));
	inspectorTabs->addTab(m_levelMapView, tr("Outline"));
	auto* rightPanel = padded(inspectorTabs, 10, 8, 10);
	rightPanel->setMinimumWidth(280);

	auto* workbench = createSplitter(Qt::Horizontal, QStringLiteral("levelsWorkbench"), tr("Level map workbench"));
	workbench->addWidget(leftPanel);
	workbench->addWidget(centre);
	workbench->addWidget(rightPanel);
	workbench->setStretchFactor(0, 0);
	workbench->setStretchFactor(1, 1);
	workbench->setStretchFactor(2, 0);
	workbench->setSizes({scaledPane(250), 760, scaledPane(330)});
	m_layoutSplitters.insert(workbench->objectName(), workbench);

	auto* body = new QWidget;
	auto* bodyLayout = new QVBoxLayout(body);
	bodyLayout->setContentsMargins(0, 0, 0, 0);
	bodyLayout->setSpacing(0);
	bodyLayout->addWidget(statusStripHost(m_levelMapState));
	bodyLayout->addWidget(workbench, 1);

	auto* empty = new EmptyStateView(QStringLiteral("map"), tr("No map open"),
		tr("Open a Quake, Quake II, or Quake III .map, or a Doom WAD, to inspect its entities, brushes, and health in the viewport."));
	QPushButton* emptyOpen = empty->addAction(tr("Open Map"), QStringLiteral("folder-open"), true);
	connect(emptyOpen, &QPushButton::clicked, this, [this]() {
		openLevelMapFile();
	});

	auto* stack = new QStackedWidget;
	stack->addWidget(empty);
	stack->addWidget(body);
	m_surfaceStacks.insert(static_cast<int>(StudioMode::Levels), stack);
	m_emptyStates.insert(static_cast<int>(StudioMode::Levels), empty);

	connect(m_levelMapProjection, &QComboBox::currentIndexChanged, this, [this]() {
		refreshLevelMapViewport();
	});
	connect(m_levelMapGrid, &QComboBox::currentIndexChanged, this, [this]() {
		refreshLevelMapViewport();
	});
	connect(m_levelMapShowThings, &QCheckBox::toggled, this, [this]() {
		refreshLevelMapViewport();
	});
	connect(m_levelMapShowSectors, &QCheckBox::toggled, this, [this]() {
		refreshLevelMapViewport();
	});
	connect(m_levelMapSnap, &QCheckBox::toggled, this, [this]() {
		refreshLevelMapViewport();
	});
	return assembleStudioPage(header, toolBar, stack);
}

QWidget* ApplicationShell::buildPackagesPage()
{
	auto* header = new PageHeader(QStringLiteral("package"), tr("Packages"));
	header->setAccessibleName(tr("Package manager header"));
	m_pageHeaders.insert(static_cast<int>(StudioMode::Packages), header);

	m_packageCompare = createButton(tr("Compare"), QStringLiteral("compare"));
	m_packageCompare->setAccessibleName(tr("Compare the open package with another"));
	m_packageCompare->setToolTip(tr("Compare the open package entry by entry with another archive, including what the staged changes would write."));
	connect(m_packageCompare, &QPushButton::clicked, this, [this]() {
		comparePackageWithFile();
	});
	header->addActionWidget(m_packageCompare);

	auto* openFolderPackage = createButton(tr("Open Folder"), QStringLiteral("folder"));
	openFolderPackage->setAccessibleName(tr("Open folder package"));
	openFolderPackage->setToolTip(tr("Open a folder as a read-only package source."));
	connect(openFolderPackage, &QPushButton::clicked, this, [this]() {
		openPackageFolder();
	});
	header->addActionWidget(openFolderPackage);

	auto* openPackage = createButton(tr("Open Package"), QStringLiteral("folder-open"), QStringLiteral("primary"));
	openPackage->setAccessibleName(tr("Open package file"));
	openPackage->setToolTip(tr("Open a PAK, WAD, ZIP, or PK3 package for read-only browsing."));
	connect(openPackage, &QPushButton::clicked, this, [this]() {
		openPackageFile();
	});
	header->addActionWidget(openPackage);

	auto* toolBar = createPageToolBar(tr("Package tools"));
	m_packageFilter = new QLineEdit;
	m_packageFilter->setObjectName("packageFilter");
	m_packageFilter->setAccessibleName(tr("Package entry filter"));
	m_packageFilter->setAccessibleDescription(tr("Filters loaded package entries by path or type hint."));
	m_packageFilter->setPlaceholderText(tr("Filter entries by path or type"));
	m_packageFilter->setClearButtonEnabled(true);
	m_packageFilter->setMinimumWidth(200);
	m_packageFilter->addAction(studioIcon(QStringLiteral("filter"), StudioIconTone::Muted), QLineEdit::LeadingPosition);
	connect(m_packageFilter, &QLineEdit::textChanged, this, [this]() {
		filterPackageEntries();
	});
	toolBar->addWidget(m_packageFilter);
	toolBar->addSeparator();

	auto makePackageTool = [this, toolBar](QAbstractButton** slot, const QString& icon, const QString& text, const QString& accessibleName, const QString& tip, bool showText, std::function<void()> handler) {
		QToolButton* button = createToolButton(icon, text, tip, showText);
		button->setAccessibleName(accessibleName);
		connect(button, &QToolButton::clicked, this, [handler]() {
			handler();
		});
		toolBar->addWidget(button);
		if (slot) {
			*slot = button;
		}
		return button;
	};
	makePackageTool(&m_packageExtractSelected, QStringLiteral("import"), tr("Extract"), tr("Extract selected package entries"),
		tr("Extract selected package entries to a chosen folder without overwriting existing files."), true, [this]() { extractSelectedPackageEntries(); });
	makePackageTool(&m_packageExtractAll, QStringLiteral("package"), tr("Extract All"), tr("Extract all package entries"),
		tr("Extract every readable package entry to a chosen folder without overwriting existing files."), false, [this]() { extractAllPackageEntries(); });
	makePackageTool(&m_packageExtractCancel, QStringLiteral("cancel"), tr("Cancel Extract"), tr("Cancel package extraction"),
		tr("Request cancellation after the current package entry finishes."), false, [this]() {
			m_packageExtractionCancelRequested = true;
			statusBar()->showMessage(tr("Package extraction cancellation requested"));
		});
	m_packageExtractCancel->setEnabled(false);
	toolBar->addSeparator();

	auto* stageLabel = new QLabel(tr("Stage"));
	toolBar->addWidget(stageLabel);
	makePackageTool(&m_packageStageAdd, QStringLiteral("plus"), tr("Add"), tr("Stage package add"),
		tr("Stage a local file as a new virtual package entry."), false, [this]() { stagePackageAddFile(); });
	makePackageTool(&m_packageStageReplace, QStringLiteral("refresh"), tr("Replace"), tr("Stage package replace"),
		tr("Stage a local file to replace the selected package entry."), false, [this]() { stagePackageReplaceSelected(); });
	makePackageTool(&m_packageStageRename, QStringLiteral("edit"), tr("Rename"), tr("Stage package rename"),
		tr("Stage a rename for the selected package entry."), false, [this]() { stagePackageRenameSelected(); });
	makePackageTool(&m_packageStageDelete, QStringLiteral("trash"), tr("Delete"), tr("Stage package delete"),
		tr("Stage deletion for the selected package entries."), false, [this]() { stagePackageDeleteSelected(); });
	toolBar->addSeparator();

	m_packageCompression = new QComboBox;
	m_packageCompression->setAccessibleName(tr("Package compression level"));
	m_packageCompression->setToolTip(tr("DEFLATE effort for ZIP and PK3 output. Every level measures each block as stored, fixed, and dynamic Huffman and keeps the smallest, so the levels trade search time for size, never correctness. PAK and WAD output is always stored."));
	m_packageCompression->addItem(tr("Store (no compression)"), QStringLiteral("store"));
	m_packageCompression->addItem(tr("Fast"), QStringLiteral("fast"));
	m_packageCompression->addItem(tr("Default"), QStringLiteral("default"));
	m_packageCompression->addItem(tr("Best"), QStringLiteral("best"));
	m_packageCompression->setCurrentIndex(2);
	auto* compressionLabel = new QLabel(tr("Compression"));
	compressionLabel->setBuddy(m_packageCompression);
	toolBar->addWidget(compressionLabel);
	toolBar->addWidget(m_packageCompression);
	makePackageTool(&m_packageStageSaveAs, QStringLiteral("save"), tr("Save As"), tr("Save staged package as"),
		tr("Write the staged package to a new PAK, ZIP, PK3, or tested PWAD path without overwriting by default."), true, [this]() { saveStagedPackageAs(); });

	m_packageState = new LoadingPane;
	m_packageState->setAccessibleName(tr("Package loading state"));
	m_packageState->setPlaceholderRows({
		tr("Package entries"),
		tr("Directory tree"),
		tr("Entry metadata"),
	});
	m_packageSummary = new QLabel;
	m_packageSummary->setObjectName("panelMeta");
	m_packageSummary->setAccessibleName(tr("Package summary"));

	m_packageTree = new QTreeWidget;
	m_packageTree->setObjectName("packageTree");
	m_packageTree->setAccessibleName(tr("Package tree"));
	m_packageTree->setAccessibleDescription(tr("Folders in the open package. Selecting one lists its contents."));
	m_packageTree->setHeaderHidden(true);
	m_packageTree->setMinimumWidth(180);
	m_packageTree->setSelectionMode(QAbstractItemView::SingleSelection);
	connect(m_packageTree, &QTreeWidget::itemSelectionChanged, this, [this]() {
		const QTreeWidgetItem* item = m_packageTree->currentItem();
		if (!item || !item->flags().testFlag(Qt::ItemIsSelectable)) {
			return;
		}
		navigatePackageFolder(item->data(0, Qt::UserRole).toString());
	});

	m_packageEntries = new QListWidget;
	m_packageEntries->setObjectName("packageEntries");
	m_packageEntries->setAccessibleName(tr("Package entries"));
	m_packageEntries->setAccessibleDescription(tr("Read-only entries from the loaded folder, PAK, WAD, ZIP, or PK3 package."));
	m_packageEntries->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_packageEntries->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(m_packageEntries, &QListWidget::customContextMenuRequested, this, [this](const QPoint& position) {
		showPackageEntryContextMenu(position);
	});
	connect(m_packageEntries, &QListWidget::itemSelectionChanged, this, [this]() {
		refreshPackageEntryDetails(selectedPackageEntryPath());
		refreshPackageStagingSummary();
	});
	// Opening a folder row steps into it, like a file browser.
	connect(m_packageEntries, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
		if (item && item->data(Qt::UserRole + 3).toBool()) {
			navigatePackageFolder(item->data(Qt::UserRole).toString());
		}
	});

	m_packageImagePreview = new ImagePreviewView;
	m_packageImagePreview->setAccessibleName(tr("Package image preview"));
	m_packageImagePreview->setAccessibleDescription(tr("Decoded pixels for the selected package entry when it is an image, sprite, or texture."));
	m_packageImagePreview->setMinimumHeight(180);

	m_packageDrawer = new DetailDrawer;
	m_packageDrawer->setAccessibleName(tr("Package entry detail drawer"));
	m_packageDrawer->setTitle(tr("Package Entry Details"));
	m_packageDrawer->setSubtitle(tr("Open a package to inspect entry metadata."));
	m_packageDrawer->setEmbedded(true);

	m_packageStagingSummary = new QListWidget;
	m_packageStagingSummary->setObjectName("packageStagingSummary");
	m_packageStagingSummary->setAccessibleName(tr("Package staging summary"));
	m_packageStagingSummary->setAccessibleDescription(tr("Staged add, replace, rename, delete, conflict, blocker, and before-after composition state for package save-as workflows."));
	m_packageStagingSummary->setWordWrap(true);

	m_packageCompositionChart = new CompositionChart;
	m_packageCompositionChart->setAccessibleName(tr("Package composition chart"));
	m_packageCompositionChart->setAccessibleDescription(tr("Share of the package taken by each content type, by size and by entry count."));
	m_packageCompositionChart->setTitle(tr("Composition By Type"));
	m_packageCompositionChart->setEmptyText(tr("Open a package to see how its content divides by type and size."));
	m_packageCompositionChart->setMinimumHeight(110);
	connect(m_packageCompositionChart, &CompositionChart::sliceActivated, this, [this](const QString& sliceId) {
		if (m_packageFilter) {
			m_packageFilter->setText(sliceId);
		}
	});
	m_packageComposition = new QListWidget;
	m_packageComposition->setObjectName("packageComposition");
	m_packageComposition->setAccessibleName(tr("Package composition summary"));
	m_packageComposition->setAccessibleDescription(tr("Data-backed package composition bars grouped by entry type and byte size."));
	auto* overview = new QWidget;
	auto* overviewLayout = new QVBoxLayout(overview);
	overviewLayout->setContentsMargins(0, 8, 0, 0);
	overviewLayout->setSpacing(8);
	overviewLayout->addWidget(m_packageCompositionChart);
	overviewLayout->addWidget(m_packageComposition, 1);

	auto* previewPanel = new QWidget;
	auto* previewLayout = new QVBoxLayout(previewPanel);
	previewLayout->setContentsMargins(0, 8, 0, 0);
	previewLayout->setSpacing(8);
	previewLayout->addWidget(m_packageImagePreview, 1);

	auto* detailTabs = createPanelTabs(tr("Package entry inspector"), QTabWidget::South);
	detailTabs->setMinimumWidth(250);
	detailTabs->addTab(m_packageDrawer, tr("Details"));
	detailTabs->addTab(previewPanel, tr("Preview"));
	detailTabs->addTab(m_packageStagingSummary, tr("Staging"));
	detailTabs->addTab(overview, tr("Overview"));

	auto* workbench = createSplitter(Qt::Horizontal, QStringLiteral("packagesWorkbench"), tr("Package tree and entry list"));
	workbench->addWidget(padded(captionedPanel(tr("Folders"), m_packageTree), 10, 8, 10));
	// Entry column header: history and up buttons, then a clickable path.
	auto* entriesPanel = new QWidget;
	auto* entriesLayout = new QVBoxLayout(entriesPanel);
	entriesLayout->setContentsMargins(0, 0, 0, 0);
	entriesLayout->setSpacing(6);
	auto* navigationRow = new QHBoxLayout;
	navigationRow->setContentsMargins(0, 0, 0, 0);
	navigationRow->setSpacing(2);
	m_packageBack = createToolButton(QStringLiteral("chevron-left"), tr("Back"), tr("Go back to the previous folder."), false);
	m_packageForward = createToolButton(QStringLiteral("chevron-right"), tr("Forward"), tr("Go forward to the next folder."), false);
	m_packageUp = createToolButton(QStringLiteral("chevron-up"), tr("Up"), tr("Go to the containing folder."), false);
	connect(m_packageBack, &QToolButton::clicked, this, [this]() {
		stepPackageFolderHistory(-1);
	});
	connect(m_packageForward, &QToolButton::clicked, this, [this]() {
		stepPackageFolderHistory(1);
	});
	connect(m_packageUp, &QToolButton::clicked, this, [this]() {
		if (!m_packageBrowseFolder.isEmpty()) {
			navigatePackageFolder(packageParentFolder(m_packageBrowseFolder), m_packageBrowseFolder);
		}
	});
	navigationRow->addWidget(m_packageBack);
	navigationRow->addWidget(m_packageForward);
	navigationRow->addWidget(m_packageUp);
	m_packageBreadcrumb = new QWidget;
	m_packageBreadcrumb->setObjectName(QStringLiteral("breadcrumb"));
	m_packageBreadcrumb->setAccessibleName(tr("Package folder path"));
	auto* breadcrumbLayout = new QHBoxLayout(m_packageBreadcrumb);
	breadcrumbLayout->setContentsMargins(6, 0, 0, 0);
	breadcrumbLayout->setSpacing(0);
	navigationRow->addWidget(m_packageBreadcrumb, 1);
	navigationRow->addWidget(m_packageSummary);
	entriesLayout->addLayout(navigationRow);
	entriesLayout->addWidget(m_packageEntries, 1);
	workbench->addWidget(padded(entriesPanel, 4, 6, 10));
	workbench->addWidget(padded(detailTabs, 10, 8, 10));
	workbench->setStretchFactor(0, 0);
	workbench->setStretchFactor(1, 1);
	workbench->setStretchFactor(2, 0);
	workbench->setSizes({scaledPane(260), 640, scaledPane(380)});
	m_layoutSplitters.insert(workbench->objectName(), workbench);

	auto* body = new QWidget;
	auto* bodyLayout = new QVBoxLayout(body);
	bodyLayout->setContentsMargins(0, 0, 0, 0);
	bodyLayout->setSpacing(0);
	bodyLayout->addWidget(statusStripHost(m_packageState));
	bodyLayout->addWidget(workbench, 1);

	auto* empty = new EmptyStateView(QStringLiteral("package"), tr("No package open"),
		tr("Open a PAK, WAD, ZIP, or PK3 archive, or a folder, to browse its entries, preview content, and stage changes for a new package."));
	QPushButton* emptyOpen = empty->addAction(tr("Open Package"), QStringLiteral("folder-open"), true);
	connect(emptyOpen, &QPushButton::clicked, this, [this]() {
		openPackageFile();
	});
	QPushButton* emptyFolder = empty->addAction(tr("Open Folder"), QStringLiteral("folder"), false);
	connect(emptyFolder, &QPushButton::clicked, this, [this]() {
		openPackageFolder();
	});

	auto* stack = new QStackedWidget;
	stack->addWidget(empty);
	stack->addWidget(body);
	m_surfaceStacks.insert(static_cast<int>(StudioMode::Packages), stack);
	m_emptyStates.insert(static_cast<int>(StudioMode::Packages), empty);
	return assembleStudioPage(header, toolBar, stack);
}

QWidget* ApplicationShell::buildBuildPage()
{
	auto* header = new PageHeader(QStringLiteral("hammer"), tr("Build"));
	header->setAccessibleName(tr("Build header"));
	m_pageHeaders.insert(static_cast<int>(StudioMode::Build), header);

	m_buildInspectArtifacts = createButton(tr("Inspect Artifacts"), QStringLiteral("info"));
	m_buildInspectArtifacts->setAccessibleName(tr("Inspect compiled artifacts"));
	m_buildInspectArtifacts->setToolTip(tr("Read the compiled BSP, its entity and texture lumps, and any leak or portal file beside it."));
	connect(m_buildInspectArtifacts, &QPushButton::clicked, this, [this]() {
		inspectCompiledArtifacts();
	});
	header->addActionWidget(m_buildInspectArtifacts);

	m_buildPipelineCancel = createButton(tr("Cancel"), QStringLiteral("stop"));
	m_buildPipelineCancel->setAccessibleName(tr("Cancel build pipeline"));
	m_buildPipelineCancel->setToolTip(tr("Stop the running pipeline after the current stage."));
	m_buildPipelineCancel->setEnabled(false);
	connect(m_buildPipelineCancel, &QPushButton::clicked, this, [this]() {
		cancelBuildPipeline();
	});
	header->addActionWidget(m_buildPipelineCancel);

	m_buildPipelineRun = createButton(tr("Run Pipeline"), QStringLiteral("play"), QStringLiteral("primary"));
	m_buildPipelineRun->setAccessibleName(tr("Run build pipeline"));
	m_buildPipelineRun->setToolTip(tr("Run every enabled stage in order, capturing logs, diagnostics, hashes, and command manifests."));
	connect(m_buildPipelineRun, &QPushButton::clicked, this, [this]() {
		runSelectedBuildPipeline();
	});
	header->addActionWidget(m_buildPipelineRun);

	auto* toolBar = createPageToolBar(tr("Build pipeline settings"));
	auto* pipelineLabel = new QLabel(tr("Pipeline"));
	toolBar->addWidget(pipelineLabel);
	m_buildPipelineChoice = new QComboBox;
	m_buildPipelineChoice->setAccessibleName(tr("Build pipeline"));
	m_buildPipelineChoice->setAccessibleDescription(tr("Chained compile stages run in order, each feeding the next stage's input."));
	m_buildPipelineChoice->setMinimumContentsLength(18);
	m_buildPipelineChoice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	pipelineLabel->setBuddy(m_buildPipelineChoice);
	toolBar->addWidget(m_buildPipelineChoice);
	auto* inputLabel = new QLabel(tr("Input"));
	toolBar->addWidget(inputLabel);
	m_buildPipelineInput = new QLineEdit;
	m_buildPipelineInput->setAccessibleName(tr("Build pipeline input"));
	m_buildPipelineInput->setPlaceholderText(tr("Source map path"));
	m_buildPipelineInput->setMinimumWidth(240);
	inputLabel->setBuddy(m_buildPipelineInput);
	toolBar->addWidget(m_buildPipelineInput);
	auto* browsePipelineInput = createToolButton(QStringLiteral("folder-open"), tr("Browse"), tr("Choose the source map for the pipeline."), false);
	browsePipelineInput->setAccessibleName(tr("Choose build pipeline input"));
	connect(browsePipelineInput, &QToolButton::clicked, this, [this]() {
		const QString path = QFileDialog::getOpenFileName(this, tr("Choose Build Input"), QString(), tr("Maps (*.map *.wad);;All files (*.*)"));
		if (!path.isEmpty() && m_buildPipelineInput) {
			m_buildPipelineInput->setText(path);
			refreshBuildSurface();
		}
	});
	toolBar->addWidget(browsePipelineInput);
	toolBar->addSeparator();
	auto* copyCommands = createToolButton(QStringLiteral("terminal"), tr("Copy Commands"), tr("Copy every stage command line so the same build can be reproduced from a shell or CI."));
	m_buildPipelineCopy = copyCommands;
	m_buildPipelineCopy->setAccessibleName(tr("Copy build pipeline commands"));
	connect(copyCommands, &QToolButton::clicked, this, [this]() {
		copyBuildPipelineCommands();
	});
	toolBar->addWidget(copyCommands);

	m_buildPipelineState = new LoadingPane;
	m_buildPipelineState->setAccessibleName(tr("Build pipeline state"));
	m_buildPipelineState->setTitle(tr("Build Pipeline"));
	m_buildPipelineState->setDetail(tr("Chained compile stages with per-stage logs, diagnostics, and output paths."));
	m_buildPipelineState->setPlaceholderRows({
		tr("Stage plan"),
		tr("Stage output"),
		tr("Artifacts"),
	});

	m_buildPipelineChart = new PipelineChart;
	m_buildPipelineChart->setAccessibleName(tr("Build pipeline stages"));
	m_buildPipelineChart->setAccessibleDescription(tr("Each stage of the selected pipeline with its state, duration, and whether it is optional."));
	m_buildPipelineChart->setTitle(tr("Pipeline Stages"));
	m_buildPipelineChart->setEmptyText(tr("Choose a pipeline and an input map to plan the stages."));
	m_buildPipelineChart->setMinimumHeight(110);

	m_buildPipelineStages = new QListWidget;
	m_buildPipelineStages->setObjectName("buildPipelineStages");
	m_buildPipelineStages->setAccessibleName(tr("Build pipeline stage list"));
	m_buildPipelineStages->setAccessibleDescription(tr("Stage order, resolved input and output paths, tool availability, and skip reasons."));
	m_buildPipelineStages->setWordWrap(true);

	m_buildPipelineDrawer = new DetailDrawer;
	m_buildPipelineDrawer->setAccessibleName(tr("Build pipeline detail drawer"));
	m_buildPipelineDrawer->setTitle(tr("Build Details"));
	m_buildPipelineDrawer->setSubtitle(tr("Stage commands, captured output, diagnostics, artifacts, and manifests."));

	auto* stageSplit = createSplitter(Qt::Horizontal, QStringLiteral("buildStageSplit"), tr("Stage list and build details"));
	stageSplit->addWidget(captionedPanel(tr("Stages"), m_buildPipelineStages));
	stageSplit->addWidget(m_buildPipelineDrawer);
	stageSplit->setStretchFactor(0, 2);
	stageSplit->setStretchFactor(1, 3);
	m_layoutSplitters.insert(stageSplit->objectName(), stageSplit);

	auto* pipelinePage = new QWidget;
	auto* pipelineLayout = new QVBoxLayout(pipelinePage);
	pipelineLayout->setContentsMargins(0, 10, 0, 0);
	pipelineLayout->setSpacing(10);
	pipelineLayout->addWidget(m_buildPipelineState);
	pipelineLayout->addWidget(m_buildPipelineChart);
	pipelineLayout->addWidget(stageSplit, 1);

	// Toolchain: individual compiler profiles and their readiness.
	m_compilerPipelineChart = new PipelineChart;
	m_compilerPipelineChart->setAccessibleName(tr("Compiler pipeline chart"));
	m_compilerPipelineChart->setAccessibleDescription(tr("Readiness of each compiler stage, from source map to compiled artifact."));
	m_compilerPipelineChart->setTitle(tr("Toolchain Readiness"));
	m_compilerPipelineChart->setEmptyText(tr("No compiler profiles are available yet."));
	m_compilerPipelineChart->setMinimumHeight(110);

	m_compilerPipeline = new QListWidget;
	m_compilerPipeline->setObjectName("compilerPipeline");
	m_compilerPipeline->setAccessibleName(tr("Compiler pipeline summary"));
	m_compilerPipeline->setAccessibleDescription(tr("Data-backed compiler profile readiness bars for map, node, and BSP workflows."));

	auto* compilerActions = new QHBoxLayout;
	compilerActions->setSpacing(8);
	m_compilerRunSelected = createButton(tr("Run Profile"), QStringLiteral("play"));
	m_compilerRunSelected->setAccessibleName(tr("Run selected compiler profile"));
	m_compilerRunSelected->setToolTip(tr("Run the selected compiler profile against the active project and register outputs when possible."));
	connect(m_compilerRunSelected, &QPushButton::clicked, this, [this]() {
		runSelectedCompilerProfile();
	});
	compilerActions->addWidget(m_compilerRunSelected);
	m_compilerCopyCli = createButton(tr("Copy CLI"), QStringLiteral("terminal"));
	m_compilerCopyCli->setAccessibleName(tr("Copy compiler CLI equivalent"));
	m_compilerCopyCli->setToolTip(tr("Copy a reproducible vibestudio --cli compiler run command for the selected profile."));
	connect(m_compilerCopyCli, &QPushButton::clicked, this, [this]() {
		copySelectedCompilerCliEquivalent();
	});
	compilerActions->addWidget(m_compilerCopyCli);
	m_compilerCopyManifest = createButton(tr("Copy Manifest"), QStringLiteral("copy"));
	m_compilerCopyManifest->setAccessibleName(tr("Copy compiler manifest"));
	m_compilerCopyManifest->setToolTip(tr("Copy the selected compiler profile's command manifest JSON."));
	connect(m_compilerCopyManifest, &QPushButton::clicked, this, [this]() {
		copySelectedCompilerManifest();
	});
	compilerActions->addWidget(m_compilerCopyManifest);
	compilerActions->addStretch(1);

	auto* toolchainPage = new QWidget;
	auto* toolchainLayout = new QVBoxLayout(toolchainPage);
	toolchainLayout->setContentsMargins(0, 10, 0, 0);
	toolchainLayout->setSpacing(10);
	toolchainLayout->addWidget(m_compilerPipelineChart);
	toolchainLayout->addLayout(compilerActions);
	toolchainLayout->addWidget(m_compilerPipeline, 1);

	// Launch: run the compiled map in the configured game.
	auto* launchPage = new QWidget;
	auto* launchLayout = new QVBoxLayout(launchPage);
	launchLayout->setContentsMargins(0, 10, 0, 0);
	launchLayout->setSpacing(10);
	auto* launchForm = new QFormLayout;
	launchForm->setHorizontalSpacing(12);
	launchForm->setVerticalSpacing(8);
	launchForm->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
	m_launchProfileChoice = new QComboBox;
	m_launchProfileChoice->setAccessibleName(tr("Launch profile"));
	m_launchProfileChoice->setAccessibleDescription(tr("Engine command-line shape used to start the configured game installation."));
	m_launchProfileChoice->setMinimumContentsLength(24);
	m_launchProfileChoice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	launchForm->addRow(tr("Launch profile"), m_launchProfileChoice);
	m_launchMapName = new QLineEdit;
	m_launchMapName->setAccessibleName(tr("Launch map name"));
	m_launchMapName->setPlaceholderText(tr("Map name to load, for example start"));
	m_launchMapName->setMinimumWidth(280);
	launchForm->addRow(tr("Map"), m_launchMapName);
	launchLayout->addLayout(launchForm);
	auto* launchActions = new QHBoxLayout;
	m_launchGame = createButton(tr("Launch Game"), QStringLiteral("gamepad"), QStringLiteral("primary"));
	m_launchGame->setAccessibleName(tr("Launch configured game"));
	m_launchGame->setToolTip(tr("Start the selected game installation with the planned command line. The command is shown for review first."));
	connect(m_launchGame, &QPushButton::clicked, this, [this]() {
		launchConfiguredGame();
	});
	launchActions->addWidget(m_launchGame);
	launchActions->addStretch(1);
	launchLayout->addLayout(launchActions);
	m_launchSummary = new QListWidget;
	m_launchSummary->setObjectName("launchSummary");
	m_launchSummary->setAccessibleName(tr("Launch plan"));
	m_launchSummary->setAccessibleDescription(tr("Resolved executable, arguments, working directory, and any blocking problem."));
	m_launchSummary->setWordWrap(true);
	launchLayout->addWidget(captionedPanel(tr("Launch plan"), m_launchSummary), 1);

	auto* sections = createPanelTabs(tr("Build sections"));
	sections->setObjectName(QStringLiteral("buildSections"));
	sections->addTab(pipelinePage, studioIcon(QStringLiteral("hammer")), tr("Pipeline"));
	sections->addTab(toolchainPage, studioIcon(QStringLiteral("terminal")), tr("Toolchain"));
	sections->addTab(launchPage, studioIcon(QStringLiteral("gamepad")), tr("Launch and Test"));

	connect(m_buildPipelineChoice, &QComboBox::currentIndexChanged, this, [this]() {
		refreshBuildSurface();
	});
	connect(m_buildPipelineInput, &QLineEdit::editingFinished, this, [this]() {
		refreshBuildSurface();
	});
	connect(m_launchProfileChoice, &QComboBox::currentIndexChanged, this, [this]() {
		refreshBuildSurface();
	});
	connect(m_launchMapName, &QLineEdit::editingFinished, this, [this]() {
		refreshBuildSurface();
	});
	return assembleStudioPage(header, toolBar, padded(sections, 14, 8, 12));
}

QWidget* ApplicationShell::buildSettingsPage()
{
	auto* header = new PageHeader(QStringLiteral("settings"), tr("Settings"));
	header->setAccessibleName(tr("Settings header"));
	header->setSubtitle(tr("Changes apply immediately and are saved automatically."));
	m_pageHeaders.insert(static_cast<int>(StudioMode::Settings), header);

	auto* categories = new QListWidget;
	categories->setObjectName(QStringLiteral("settingsCategories"));
	categories->setAccessibleName(tr("Settings categories"));
	categories->setFixedWidth(220);
	setBaseIconSize(categories, QSize(18, 18));
	markFlat(categories);

	auto* pages = new QStackedWidget;
	pages->setAccessibleName(tr("Settings category pages"));

	// Each category is a scrollable column capped at a readable width.
	auto addCategory = [categories, pages](const QString& iconName, const QString& title, QWidget* content) {
		auto* column = new QWidget;
		auto* columnLayout = new QHBoxLayout(column);
		columnLayout->setContentsMargins(22, 18, 22, 22);
		content->setMaximumWidth(760);
		columnLayout->addWidget(content, 1, Qt::AlignTop);
		columnLayout->addStretch(0);
		pages->addWidget(createScrollSurface(column, title));
		auto* item = new QListWidgetItem(studioIcon(iconName), title);
		item->setData(Qt::AccessibleTextRole, title);
		categories->addItem(item);
	};

	// Getting started: first-run setup.
	auto* setupContent = new QWidget;
	auto* setupContentLayout = new QVBoxLayout(setupContent);
	setupContentLayout->setContentsMargins(0, 0, 0, 0);
	setupContentLayout->setSpacing(12);
	auto* setupPanel = new QFrame;
	setupPanel->setObjectName("setupPanel");
	setupPanel->setAccessibleName(tr("First-run setup"));
	setupPanel->setAccessibleDescription(tr("Setup status, current step, warnings, and actions."));
	auto* setupLayout = new QVBoxLayout(setupPanel);
	setupLayout->setContentsMargins(16, 14, 16, 16);
	setupLayout->setSpacing(10);
	auto* setupHeader = new QHBoxLayout;
	m_setupStatus = new QLabel;
	m_setupStatus->setObjectName("moduleTitle");
	m_setupStatus->setAccessibleName(tr("Setup status"));
	m_setupStep = new QLabel;
	m_setupStep->setObjectName("moduleMeta");
	m_setupStep->setAccessibleName(tr("Current setup step"));
	setupHeader->addWidget(m_setupStatus);
	setupHeader->addWidget(m_setupStep, 1, Qt::AlignRight);
	setupLayout->addLayout(setupHeader);
	m_setupProgress = new QProgressBar;
	m_setupProgress->setAccessibleName(tr("Setup progress"));
	m_setupProgress->setTextVisible(true);
	setupLayout->addWidget(m_setupProgress);
	m_setupNextAction = new QLabel;
	m_setupNextAction->setWordWrap(true);
	m_setupNextAction->setAccessibleName(tr("Setup next action"));
	setupLayout->addWidget(m_setupNextAction);
	m_setupSummary = new QListWidget;
	m_setupSummary->setObjectName("setupSummary");
	m_setupSummary->setAccessibleName(tr("Setup summary"));
	m_setupSummary->setAccessibleDescription(tr("Every setup step in order with its state, followed by setup warnings."));
	m_setupSummary->setMinimumHeight(320);
	m_setupSummary->setWordWrap(true);
	setBaseIconSize(m_setupSummary, QSize(16, 16));
	setupLayout->addWidget(m_setupSummary, 1);
	auto* setupActions = new QHBoxLayout;
	setupActions->setSpacing(8);
	m_setupReset = createButton(tr("Reset"), QStringLiteral("refresh"), QStringLiteral("ghost"));
	m_setupReset->setAccessibleName(tr("Reset setup progress"));
	connect(m_setupReset, &QPushButton::clicked, this, [this]() {
		resetSetup();
	});
	setupActions->addWidget(m_setupReset);
	setupActions->addStretch(1);
	m_setupSkip = createButton(tr("Skip"), QStringLiteral("close"));
	m_setupSkip->setAccessibleName(tr("Skip setup for now"));
	connect(m_setupSkip, &QPushButton::clicked, this, [this]() {
		skipSetup();
	});
	setupActions->addWidget(m_setupSkip);
	m_setupComplete = createButton(tr("Finish"), QStringLiteral("check"));
	m_setupComplete->setAccessibleName(tr("Finish setup"));
	connect(m_setupComplete, &QPushButton::clicked, this, [this]() {
		completeSetup();
	});
	setupActions->addWidget(m_setupComplete);
	m_setupNext = createButton(tr("Next"), QStringLiteral("chevron-right"));
	m_setupNext->setAccessibleName(tr("Advance setup step"));
	connect(m_setupNext, &QPushButton::clicked, this, [this]() {
		advanceSetup();
	});
	setupActions->addWidget(m_setupNext);
	m_setupStartResume = createButton(tr("Start"), QStringLiteral("play"), QStringLiteral("primary"));
	m_setupStartResume->setAccessibleName(tr("Start or resume setup"));
	connect(m_setupStartResume, &QPushButton::clicked, this, [this]() {
		startOrResumeSetup();
	});
	setupActions->addWidget(m_setupStartResume);
	setupLayout->addLayout(setupActions);
	setupContentLayout->addWidget(setupPanel);
	addCategory(QStringLiteral("check"), tr("Getting Started"), setupContent);

	// Appearance and accessibility.
	auto* appearanceContent = new QWidget;
	auto* appearanceLayout = new QVBoxLayout(appearanceContent);
	appearanceLayout->setContentsMargins(0, 0, 0, 0);
	appearanceLayout->setSpacing(12);

	auto* preferencesPanel = new QGroupBox(tr("Appearance"));
	preferencesPanel->setObjectName("preferencesPanel");
	preferencesPanel->setAccessibleName(tr("Accessibility and language preferences"));
	preferencesPanel->setAccessibleDescription(tr("Persistent preferences for language, theme, scaling, density, motion, and text to speech."));
	auto* preferencesLayout = new QFormLayout(preferencesPanel);
	preferencesLayout->setHorizontalSpacing(16);
	preferencesLayout->setVerticalSpacing(10);
	preferencesLayout->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
	preferencesLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

	auto sizeCombo = [](QComboBox* combo) {
		combo->setMinimumContentsLength(22);
		combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	};

	m_themeCombo = new QComboBox;
	m_themeCombo->setAccessibleName(tr("Theme"));
	const QVector<StudioTheme> themes = {
		StudioTheme::System,
		StudioTheme::Dark,
		StudioTheme::Light,
		StudioTheme::HighContrastDark,
		StudioTheme::HighContrastLight,
	};
	for (StudioTheme theme : themes) {
		m_themeCombo->addItem(localizedThemeName(theme), themeId(theme));
	}
	sizeCombo(m_themeCombo);
	preferencesLayout->addRow(tr("Theme"), m_themeCombo);

	m_textScaleCombo = new QComboBox;
	m_textScaleCombo->setAccessibleName(tr("Text scale"));
	for (int scale : {100, 125, 150, 175, 200}) {
		m_textScaleCombo->addItem(tr("%1%").arg(scale), scale);
	}
	sizeCombo(m_textScaleCombo);
	preferencesLayout->addRow(tr("Text scale"), m_textScaleCombo);

	m_densityCombo = new QComboBox;
	m_densityCombo->setAccessibleName(tr("UI density"));
	const QVector<UiDensity> densities = {
		UiDensity::Comfortable,
		UiDensity::Standard,
		UiDensity::Compact,
	};
	for (UiDensity density : densities) {
		m_densityCombo->addItem(localizedDensityName(density), densityId(density));
	}
	sizeCombo(m_densityCombo);
	preferencesLayout->addRow(tr("Density"), m_densityCombo);

	m_reducedMotion = new QCheckBox(tr("Reduce motion"));
	m_reducedMotion->setAccessibleName(tr("Reduced motion"));
	m_reducedMotion->setToolTip(tr("Replaces spinners and animated progress with static state text across setup, task, and editor surfaces."));
	preferencesLayout->addRow(QString(), m_reducedMotion);

	m_textToSpeech = new QCheckBox(tr("Read status changes aloud"));
	m_textToSpeech->setAccessibleName(tr("Text to speech"));
	m_textToSpeech->setToolTip(tr("Stores the OS-backed text-to-speech preference for the setup and task surfaces planned next."));
	preferencesLayout->addRow(QString(), m_textToSpeech);
	appearanceLayout->addWidget(preferencesPanel);

	auto* languagePanel = new QGroupBox(tr("Language and Editing"));
	auto* languageLayout = new QFormLayout(languagePanel);
	languageLayout->setHorizontalSpacing(16);
	languageLayout->setVerticalSpacing(10);
	languageLayout->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
	m_localeCombo = new QComboBox;
	m_localeCombo->setAccessibleName(tr("Language"));
	for (const QString& localeName : supportedLocaleNames()) {
		m_localeCombo->addItem(localeDisplayName(localeName), localeName);
	}
	sizeCombo(m_localeCombo);
	languageLayout->addRow(tr("Language"), m_localeCombo);

	m_editorProfileCombo = new QComboBox;
	m_editorProfileCombo->setAccessibleName(tr("Editor profile"));
	m_editorProfileCombo->setAccessibleDescription(tr("Selects the routed level-editor interaction profile used by map, package, compiler, and shell command surfaces."));
	m_editorProfileCombo->setToolTip(tr("Keyboard shortcuts and terminology follow the editor you already know."));
	for (const EditorProfileDescriptor& profile : editorProfileDescriptors()) {
		m_editorProfileCombo->addItem(profile.displayName, profile.id);
	}
	sizeCombo(m_editorProfileCombo);
	languageLayout->addRow(tr("Editor profile"), m_editorProfileCombo);
	appearanceLayout->addWidget(languagePanel);
	appearanceLayout->addStretch(1);
	addCategory(QStringLiteral("palette"), tr("Appearance and Language"), appearanceContent);

	// AI and automation.
	auto* aiContent = new QWidget;
	auto* aiLayout = new QVBoxLayout(aiContent);
	aiLayout->setContentsMargins(0, 0, 0, 0);
	aiLayout->setSpacing(12);
	auto* aiModePanel = new QGroupBox(tr("AI Mode"));
	auto* aiModeLayout = new QVBoxLayout(aiModePanel);
	aiModeLayout->setSpacing(8);
	m_aiFreeMode = new QCheckBox(tr("AI-free mode"));
	m_aiFreeMode->setAccessibleName(tr("AI-free mode"));
	m_aiFreeMode->setToolTip(tr("Keeps cloud and agentic AI workflows disabled for core editing, package, compiler, and CLI work."));
	aiModeLayout->addWidget(m_aiFreeMode);
	auto* aiFreeHint = new QLabel(tr("Every core workflow stays local and deterministic. Connectors below are remembered but not used."));
	aiFreeHint->setObjectName(QStringLiteral("fieldHint"));
	aiFreeHint->setWordWrap(true);
	aiModeLayout->addWidget(aiFreeHint);
	m_aiCloudConnectors = new QCheckBox(tr("Allow cloud AI connectors"));
	m_aiCloudConnectors->setAccessibleName(tr("Cloud AI connectors"));
	m_aiCloudConnectors->setToolTip(tr("Opt in to experimental provider-neutral cloud connector configuration. Secrets are read through redacted environment references, not shown in logs."));
	aiModeLayout->addWidget(m_aiCloudConnectors);
	m_aiAgenticWorkflows = new QCheckBox(tr("Allow agentic workflows"));
	m_aiAgenticWorkflows->setAccessibleName(tr("Agentic workflows"));
	m_aiAgenticWorkflows->setToolTip(tr("Opt in to future supervised plan, review, stage, validate, and summarize workflows."));
	aiModeLayout->addWidget(m_aiAgenticWorkflows);
	aiLayout->addWidget(aiModePanel);

	auto* connectorPanel = new QGroupBox(tr("Preferred Connectors"));
	auto* connectorLayout = new QFormLayout(connectorPanel);
	connectorLayout->setHorizontalSpacing(16);
	connectorLayout->setVerticalSpacing(10);
	connectorLayout->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
	auto addAiConnectorCombo = [connectorLayout, sizeCombo](const QString& label, const QString& capabilityId, const QString& description) {
		auto* combo = new QComboBox;
		combo->setAccessibleName(label);
		combo->setAccessibleDescription(description);
		combo->setToolTip(description);
		combo->addItem(tr("Not selected"), QString());
		for (const AiConnectorDescriptor& connector : aiConnectorDescriptors()) {
			if (connector.capabilities.contains(capabilityId)) {
				combo->addItem(connector.displayName, connector.id);
			}
		}
		sizeCombo(combo);
		connectorLayout->addRow(label, combo);
		return combo;
	};
	m_aiReasoningConnectorCombo = addAiConnectorCombo(tr("Reasoning"), QStringLiteral("reasoning"), tr("Preferred provider-neutral reasoning connector for future AI-assisted planning, review, and explanation."));
	m_aiCodingConnectorCombo = addAiConnectorCombo(tr("Coding"), QStringLiteral("coding"), tr("Preferred connector for future code, script, shader, and config assistance."));
	m_aiVisionConnectorCombo = addAiConnectorCombo(tr("Vision"), QStringLiteral("vision"), tr("Preferred connector for future image, screenshot, and visual context understanding."));
	m_aiImageConnectorCombo = addAiConnectorCombo(tr("Image"), QStringLiteral("image"), tr("Preferred connector for future image, sprite, and texture generation experiments."));
	m_aiAudioConnectorCombo = addAiConnectorCombo(tr("Audio"), QStringLiteral("audio"), tr("Preferred connector for future generated sound, music, and audio ideation."));
	m_aiVoiceConnectorCombo = addAiConnectorCombo(tr("Voice"), QStringLiteral("voice"), tr("Preferred connector for future narration, speech, and voice workflow experiments."));
	m_aiThreeDConnectorCombo = addAiConnectorCombo(tr("3D"), QStringLiteral("three-d"), tr("Preferred connector for future model, texture, and concept-to-asset generation."));
	m_aiEmbeddingsConnectorCombo = addAiConnectorCombo(tr("Embeddings"), QStringLiteral("embeddings"), tr("Preferred connector for future semantic search, retrieval, and context ranking."));
	m_aiLocalConnectorCombo = addAiConnectorCombo(tr("Local"), QStringLiteral("local-offline"), tr("Preferred connector for future local/offline AI runtime use."));
	aiLayout->addWidget(connectorPanel);
	aiLayout->addStretch(1);
	addCategory(QStringLiteral("sparkle"), tr("AI and Automation"), aiContent);

	// Extensions.
	auto* extensionContent = new QWidget;
	auto* extensionLayout = new QVBoxLayout(extensionContent);
	extensionLayout->setContentsMargins(0, 0, 0, 0);
	extensionLayout->setSpacing(12);
	auto* extensionPanel = new QGroupBox(tr("Extension Discovery"));
	auto* extensionPanelLayout = new QVBoxLayout(extensionPanel);
	extensionPanelLayout->setSpacing(10);
	auto* extensionControls = new QHBoxLayout;
	extensionControls->setSpacing(8);
	m_advancedExtensionRoot = new QLineEdit;
	m_advancedExtensionRoot->setAccessibleName(tr("Extension discovery root"));
	m_advancedExtensionRoot->setAccessibleDescription(tr("Folder searched for vibestudio.extension.json manifests."));
	m_advancedExtensionRoot->setPlaceholderText(tr("Folder containing vibestudio.extension.json manifests"));
	extensionControls->addWidget(m_advancedExtensionRoot, 1);
	m_advancedExtensionDiscover = createButton(tr("Discover"), QStringLiteral("plugin"));
	m_advancedExtensionDiscover->setAccessibleName(tr("Discover extensions"));
	connect(m_advancedExtensionDiscover, &QPushButton::clicked, this, [this]() {
		discoverAdvancedExtensions();
	});
	extensionControls->addWidget(m_advancedExtensionDiscover);
	extensionPanelLayout->addLayout(extensionControls);
	m_advancedExtensions = new QListWidget;
	m_advancedExtensions->setAccessibleName(tr("Extensions"));
	m_advancedExtensions->setAccessibleDescription(tr("Discovered extension manifests, trust model, sandbox model, commands, and staged generated files."));
	m_advancedExtensions->setMinimumHeight(220);
	m_advancedExtensions->setWordWrap(true);
	extensionPanelLayout->addWidget(m_advancedExtensions, 1);
	extensionLayout->addWidget(extensionPanel);
	extensionLayout->addStretch(1);
	addCategory(QStringLiteral("plugin"), tr("Extensions"), extensionContent);

	connect(categories, &QListWidget::currentRowChanged, pages, &QStackedWidget::setCurrentIndex);
	categories->setCurrentRow(0);

	auto* categoryColumn = new QWidget;
	categoryColumn->setObjectName(QStringLiteral("settingsNavigation"));
	auto* categoryLayout = new QVBoxLayout(categoryColumn);
	categoryLayout->setContentsMargins(12, 14, 0, 14);
	categoryLayout->addWidget(categories);

	auto* body = new QWidget;
	auto* bodyLayout = new QHBoxLayout(body);
	bodyLayout->setContentsMargins(0, 0, 0, 0);
	bodyLayout->setSpacing(0);
	bodyLayout->addWidget(categoryColumn);
	bodyLayout->addWidget(pages, 1);
	return assembleStudioPage(header, nullptr, body);
}

QWidget* ApplicationShell::buildShadersPage()
{
	auto* header = new PageHeader(QStringLiteral("layers"), tr("Shaders"));
	header->setAccessibleName(tr("Shader workbench header"));
	m_pageHeaders.insert(static_cast<int>(StudioMode::Shaders), header);

	auto* browseShader = createButton(tr("Open Shader"), QStringLiteral("folder-open"), QStringLiteral("primary"));
	browseShader->setAccessibleName(tr("Open shader script"));
	auto chooseShader = [this]() {
		const QString path = QFileDialog::getOpenFileName(this, tr("Open Shader Script"), QString(), tr("Shader scripts (*.shader *.txt);;All files (*.*)"));
		if (!path.isEmpty() && m_advancedShaderPath) {
			m_advancedShaderPath->setText(path);
			inspectAdvancedShaderScript();
		}
	};
	connect(browseShader, &QPushButton::clicked, this, chooseShader);
	header->addActionWidget(browseShader);

	auto* toolBar = createPageToolBar(tr("Shader tools"));
	m_advancedShaderPath = new QLineEdit;
	m_advancedShaderPath->setAccessibleName(tr("Shader script path"));
	m_advancedShaderPath->setAccessibleDescription(tr("Path to an idTech3 shader script for graph parsing, preview, edits, and package validation."));
	m_advancedShaderPath->setPlaceholderText(tr("Shader script path"));
	m_advancedShaderPath->setMinimumWidth(320);
	m_advancedShaderPath->addAction(studioIcon(QStringLiteral("file"), StudioIconTone::Muted), QLineEdit::LeadingPosition);
	connect(m_advancedShaderPath, &QLineEdit::returnPressed, this, [this]() {
		inspectAdvancedShaderScript();
	});
	toolBar->addWidget(m_advancedShaderPath);
	auto* inspect = createToolButton(QStringLiteral("refresh"), tr("Inspect"), tr("Parse the shader script at this path."));
	m_advancedShaderInspect = inspect;
	m_advancedShaderInspect->setAccessibleName(tr("Inspect shader script"));
	connect(inspect, &QToolButton::clicked, this, [this]() {
		inspectAdvancedShaderScript();
	});
	toolBar->addWidget(inspect);

	m_advancedStudioState = new LoadingPane;
	m_advancedStudioState->setAccessibleName(tr("Advanced studio state"));
	m_advancedStudioState->setTitle(tr("Advanced Studio"));
	m_advancedStudioState->setDetail(tr("Shader graph, sprite creator, code IDE, AI creation, and extension system."));
	m_advancedStudioState->setPlaceholderRows({
		tr("Shader graph"),
		tr("Sprite sequence"),
		tr("Code symbols"),
		tr("AI proposal"),
		tr("Extensions"),
	});

	// Shader > stage > texture tree. Selecting a shader or a stage shows its
	// parsed fields and raw text in the details panel beside it.
	m_advancedShaderGraph = new QTreeWidget;
	m_advancedShaderGraph->setObjectName(QStringLiteral("shaderGraph"));
	m_advancedShaderGraph->setAccessibleName(tr("Shader graph stages"));
	m_advancedShaderGraph->setAccessibleDescription(tr("Each shader with its stages, blend modes, and the textures they reference; textures missing from the open package are marked."));
	m_advancedShaderGraph->setColumnCount(2);
	m_advancedShaderGraph->setHeaderLabels({tr("Shader"), tr("Details")});
	m_advancedShaderGraph->setUniformRowHeights(true);
	m_advancedShaderGraph->setTextElideMode(Qt::ElideMiddle);
	m_advancedShaderGraph->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_advancedShaderGraph->header()->setStretchLastSection(true);
	m_advancedShaderGraph->header()->setSectionResizeMode(0, QHeaderView::Interactive);
	m_advancedShaderGraph->setIndentation(16);
	m_advancedShaderGraph->setColumnWidth(0, 236);
	setBaseIconSize(m_advancedShaderGraph, QSize(16, 16));
	connect(m_advancedShaderGraph, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* current) {
		if (!current) {
			return;
		}
		const QString shaderName = current->data(0, Qt::UserRole).toString();
		if (!shaderName.isEmpty()) {
			refreshShaderDetailSections(shaderName, current->data(0, Qt::UserRole + 1).toInt());
		}
	});

	m_advancedStudioDrawer = new DetailDrawer;
	m_advancedStudioDrawer->setAccessibleName(tr("Advanced studio detail drawer"));
	m_advancedStudioDrawer->setTitle(tr("Advanced Studio Details"));
	m_advancedStudioDrawer->setSubtitle(tr("Inspect shader, sprite, code, AI, and extension details."));

	auto* workbench = createSplitter(Qt::Horizontal, QStringLiteral("shadersWorkbench"), tr("Shader workbench"));
	workbench->addWidget(captionedPanel(tr("Stage graph"), m_advancedShaderGraph));
	workbench->addWidget(m_advancedStudioDrawer);
	workbench->setStretchFactor(0, 3);
	workbench->setStretchFactor(1, 2);
	m_layoutSplitters.insert(workbench->objectName(), workbench);

	auto* body = new QWidget;
	auto* bodyLayout = new QVBoxLayout(body);
	bodyLayout->setContentsMargins(0, 0, 0, 0);
	bodyLayout->setSpacing(0);
	bodyLayout->addWidget(statusStripHost(m_advancedStudioState));
	bodyLayout->addWidget(padded(workbench, 14, 10, 12), 1);

	auto* empty = new EmptyStateView(QStringLiteral("layers"), tr("No shader script loaded"),
		tr("Open an idTech3 .shader script to see each shader's stages, blend modes, and the textures it references, checked against the open package."));
	QPushButton* emptyOpen = empty->addAction(tr("Open Shader"), QStringLiteral("folder-open"), true);
	connect(emptyOpen, &QPushButton::clicked, this, chooseShader);
	auto* stack = new QStackedWidget;
	stack->addWidget(empty);
	stack->addWidget(body);
	m_surfaceStacks.insert(static_cast<int>(StudioMode::Shaders), stack);
	m_emptyStates.insert(static_cast<int>(StudioMode::Shaders), empty);
	return assembleStudioPage(header, toolBar, stack);
}

QWidget* ApplicationShell::buildTexturesPage()
{
	auto* header = new PageHeader(QStringLiteral("image"), tr("Textures"));
	header->setAccessibleName(tr("Texture workbench header"));
	m_pageHeaders.insert(static_cast<int>(StudioMode::Textures), header);

	auto* exportTexture = createButton(tr("Export PNG"), QStringLiteral("export"));
	exportTexture->setAccessibleName(tr("Export selected texture"));
	exportTexture->setToolTip(tr("Write the decoded image to a PNG file."));
	connect(exportTexture, &QPushButton::clicked, this, [this]() {
		exportSelectedTexture();
	});
	header->addActionWidget(exportTexture);

	auto* toolBar = createPageToolBar(tr("Texture tools"));
	m_textureFilter = new QLineEdit;
	m_textureFilter->setAccessibleName(tr("Texture filter"));
	m_textureFilter->setPlaceholderText(tr("Filter textures, sprites, and images by path"));
	m_textureFilter->setClearButtonEnabled(true);
	m_textureFilter->setMinimumWidth(240);
	m_textureFilter->addAction(studioIcon(QStringLiteral("filter"), StudioIconTone::Muted), QLineEdit::LeadingPosition);
	connect(m_textureFilter, &QLineEdit::textChanged, this, [this]() {
		filterTextureEntries();
	});
	toolBar->addWidget(m_textureFilter);
	toolBar->addSeparator();
	auto* paletteLabel = new QLabel(tr("Palette"));
	toolBar->addWidget(paletteLabel);
	m_texturePaletteChoice = new QComboBox;
	m_texturePaletteChoice->setAccessibleName(tr("Palette"));
	m_texturePaletteChoice->setToolTip(tr("Palette used to decode indexed idTech art. Real palettes are read from the open package when it has one."));
	m_texturePaletteChoice->setMinimumContentsLength(16);
	m_texturePaletteChoice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	paletteLabel->setBuddy(m_texturePaletteChoice);
	connect(m_texturePaletteChoice, &QComboBox::currentIndexChanged, this, [this]() {
		invalidatePaletteResolution();
		showSelectedTexture();
		// Indexed thumbnails were decoded with the old palette.
		queueTextureThumbnails();
	});
	toolBar->addWidget(m_texturePaletteChoice);
	auto* mipLabel = new QLabel(tr("Mip"));
	toolBar->addWidget(mipLabel);
	m_textureMipLevel = new QComboBox;
	m_textureMipLevel->setAccessibleName(tr("Mip level"));
	m_textureMipLevel->setToolTip(tr("idTech textures store several mip levels; choose which one to display."));
	mipLabel->setBuddy(m_textureMipLevel);
	connect(m_textureMipLevel, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (m_texturePreview && index >= 0) {
			m_texturePreview->setMipLevel(index);
		}
	});
	toolBar->addWidget(m_textureMipLevel);
	toolBar->addSeparator();
	m_textureTilesButton = createToolButton(QStringLiteral("grid"), tr("Tiles"), tr("Show decoded thumbnails."), false);
	m_textureListButton = createToolButton(QStringLiteral("list"), tr("List"), tr("Show paths and sizes."), false);
	for (QToolButton* button : {m_textureTilesButton, m_textureListButton}) {
		button->setCheckable(true);
		toolBar->addWidget(button);
	}
	m_textureTileMode = m_settings.shellLayoutState(QStringLiteral("textureViewMode")) != QByteArrayLiteral("list");
	connect(m_textureTilesButton, &QToolButton::clicked, this, [this]() {
		m_textureTileMode = true;
		m_settings.setShellLayoutState(QStringLiteral("textureViewMode"), QByteArrayLiteral("tiles"));
		applyTextureViewMode();
	});
	connect(m_textureListButton, &QToolButton::clicked, this, [this]() {
		m_textureTileMode = false;
		m_settings.setShellLayoutState(QStringLiteral("textureViewMode"), QByteArrayLiteral("list"));
		applyTextureViewMode();
	});
	m_textureThumbnailTimer = new QTimer(this);
	m_textureThumbnailTimer->setInterval(0);
	connect(m_textureThumbnailTimer, &QTimer::timeout, this, &ApplicationShell::generateTextureThumbnailBatch);

	m_textureState = new LoadingPane;
	m_textureState->setAccessibleName(tr("Texture browser state"));
	m_textureState->setTitle(tr("Textures"));
	m_textureState->setDetail(tr("Decoded idTech textures, flats, sprites, and palettes from the open package."));
	m_textureState->setPlaceholderRows({
		tr("Texture list"),
		tr("Decoded preview"),
		tr("Palette"),
	});

	m_textureEntries = new QListWidget;
	m_textureEntries->setObjectName("textureEntries");
	m_textureEntries->installEventFilter(this);
	m_textureEntries->setAccessibleName(tr("Texture entries"));
	m_textureEntries->setAccessibleDescription(tr("Image, texture, flat, and sprite entries in the open package."));
	m_textureEntries->setMinimumWidth(190);
	connect(m_textureEntries, &QListWidget::itemSelectionChanged, this, [this]() {
		showSelectedTexture();
	});

	m_texturePreview = new ImagePreviewView;
	m_texturePreview->setAccessibleName(tr("Texture preview"));
	m_texturePreview->setAccessibleDescription(tr("Decoded pixels for the selected entry. Scroll to zoom, drag to pan, hover for the palette index."));
	m_texturePreview->setMinimumSize(280, 240);

	m_texturePaletteSource = new QLabel(tr("No palette resolved yet."));
	m_texturePaletteSource->setObjectName("viewportReadout");
	m_texturePaletteSource->setAccessibleName(tr("Palette source"));
	m_texturePaletteSource->setWordWrap(true);

	auto* centre = new QWidget;
	auto* centreLayout = new QVBoxLayout(centre);
	centreLayout->setContentsMargins(0, 0, 0, 0);
	centreLayout->setSpacing(0);
	centreLayout->addWidget(m_texturePreview, 1);
	centreLayout->addWidget(m_texturePaletteSource);

	m_texturePalette = new PaletteSwatchView;
	m_texturePalette->setAccessibleName(tr("Palette swatches"));
	m_texturePalette->setAccessibleDescription(tr("The 256 palette entries used to decode indexed art, with the transparent index marked."));
	m_texturePalette->setMinimumHeight(180);
	auto* palettePanel = new QWidget;
	auto* palettePanelLayout = new QVBoxLayout(palettePanel);
	palettePanelLayout->setContentsMargins(0, 8, 0, 0);
	palettePanelLayout->addWidget(m_texturePalette);
	palettePanelLayout->addStretch(1);

	m_textureDrawer = new DetailDrawer;
	m_textureDrawer->setAccessibleName(tr("Texture detail drawer"));
	m_textureDrawer->setTitle(tr("Texture Details"));
	m_textureDrawer->setSubtitle(tr("Format, dimensions, mip levels, palette source, flags, and raw metadata."));
	m_textureDrawer->setEmbedded(true);

	// Sprite creator: frame naming and staging plans for Doom and Quake sprites.
	auto* spritePanel = new QWidget;
	auto* spriteLayout = new QVBoxLayout(spritePanel);
	spriteLayout->setContentsMargins(0, 8, 0, 0);
	spriteLayout->setSpacing(8);
	auto* spriteForm = new QFormLayout;
	spriteForm->setHorizontalSpacing(10);
	spriteForm->setVerticalSpacing(6);
	m_advancedSpriteEngine = new QComboBox;
	m_advancedSpriteEngine->setAccessibleName(tr("Sprite engine"));
	m_advancedSpriteEngine->addItem(tr("Doom"), QStringLiteral("doom"));
	m_advancedSpriteEngine->addItem(tr("Quake"), QStringLiteral("quake"));
	spriteForm->addRow(tr("Engine"), m_advancedSpriteEngine);
	m_advancedSpriteName = new QLineEdit;
	m_advancedSpriteName->setAccessibleName(tr("Sprite name"));
	m_advancedSpriteName->setPlaceholderText(tr("SPRT / torch"));
	spriteForm->addRow(tr("Name"), m_advancedSpriteName);
	m_advancedSpriteFrames = new QLineEdit;
	m_advancedSpriteFrames->setAccessibleName(tr("Sprite frame count"));
	m_advancedSpriteFrames->setPlaceholderText(tr("frames"));
	m_advancedSpriteFrames->setText(QStringLiteral("4"));
	spriteForm->addRow(tr("Frames"), m_advancedSpriteFrames);
	m_advancedSpriteRotations = new QLineEdit;
	m_advancedSpriteRotations->setAccessibleName(tr("Sprite rotations"));
	m_advancedSpriteRotations->setPlaceholderText(tr("rotations"));
	m_advancedSpriteRotations->setText(QStringLiteral("8"));
	spriteForm->addRow(tr("Rotations"), m_advancedSpriteRotations);
	spriteLayout->addLayout(spriteForm);
	m_advancedSpritePlanButton = createButton(tr("Plan Sprite"), QStringLiteral("film"));
	m_advancedSpritePlanButton->setAccessibleName(tr("Create sprite workflow plan"));
	connect(m_advancedSpritePlanButton, &QPushButton::clicked, this, [this]() {
		createAdvancedSpritePlan();
	});
	spriteLayout->addWidget(m_advancedSpritePlanButton, 0, Qt::AlignLeft);
	m_advancedSpriteSequence = new QListWidget;
	m_advancedSpriteSequence->setAccessibleName(tr("Sprite sequence"));
	m_advancedSpriteSequence->setAccessibleDescription(tr("Doom and Quake sprite frame naming, palette, and package staging lines."));
	spriteLayout->addWidget(m_advancedSpriteSequence, 1);

	auto* inspectorTabs = createPanelTabs(tr("Texture inspector"), QTabWidget::South);
	inspectorTabs->setMinimumWidth(250);
	inspectorTabs->addTab(m_textureDrawer, tr("Details"));
	inspectorTabs->addTab(palettePanel, tr("Palette"));
	inspectorTabs->addTab(spritePanel, tr("Sprite Creator"));

	auto* workbench = createSplitter(Qt::Horizontal, QStringLiteral("texturesWorkbench"), tr("Texture browser layout"));
	workbench->addWidget(padded(captionedPanel(tr("Images"), m_textureEntries), 10, 8, 10));
	workbench->addWidget(centre);
	workbench->addWidget(padded(inspectorTabs, 10, 8, 10));
	workbench->setStretchFactor(0, 0);
	workbench->setStretchFactor(1, 1);
	workbench->setStretchFactor(2, 0);
	workbench->setSizes({scaledPane(400), 580, scaledPane(320)});
	m_layoutSplitters.insert(workbench->objectName(), workbench);

	auto* body = new QWidget;
	auto* bodyLayout = new QVBoxLayout(body);
	bodyLayout->setContentsMargins(0, 0, 0, 0);
	bodyLayout->setSpacing(0);
	bodyLayout->addWidget(statusStripHost(m_textureState));
	bodyLayout->addWidget(workbench, 1);

	auto* empty = new EmptyStateView(QStringLiteral("image"), tr("No textures to show"),
		tr("Open a package with textures, flats, sprites, or images. Indexed art is decoded with the palette the package provides."));
	QPushButton* emptyOpen = empty->addAction(tr("Open Package"), QStringLiteral("folder-open"), true);
	connect(emptyOpen, &QPushButton::clicked, this, [this]() {
		openPackageFile();
	});
	auto* stack = new QStackedWidget;
	stack->addWidget(empty);
	stack->addWidget(body);
	m_surfaceStacks.insert(static_cast<int>(StudioMode::Textures), stack);
	m_emptyStates.insert(static_cast<int>(StudioMode::Textures), empty);
	return assembleStudioPage(header, toolBar, stack);
}

QWidget* ApplicationShell::buildModelsPage()
{
	auto* header = new PageHeader(QStringLiteral("cube"), tr("Models"));
	header->setAccessibleName(tr("Model workbench header"));
	m_pageHeaders.insert(static_cast<int>(StudioMode::Models), header);

	auto* modelExport = createButton(tr("Export OBJ"), QStringLiteral("export"));
	modelExport->setAccessibleName(tr("Export the current model frame"));
	modelExport->setToolTip(tr("Write the current frame as a Wavefront OBJ for an external modeller."));
	connect(modelExport, &QPushButton::clicked, this, [this]() {
		exportSelectedModel();
	});
	header->addActionWidget(modelExport);

	m_modelState = new LoadingPane;
	m_modelState->setAccessibleName(tr("Model browser state"));
	m_modelState->setTitle(tr("Models"));
	m_modelState->setDetail(tr("MDL, MD2, MD3, and adjacent idTech model metadata with skin and material dependencies."));
	m_modelState->setPlaceholderRows({
		tr("Model list"),
		tr("Frames and surfaces"),
		tr("Skins"),
	});

	m_modelEntries = new QListWidget;
	m_modelEntries->setObjectName("modelEntries");
	m_modelEntries->setAccessibleName(tr("Model entries"));
	m_modelEntries->setAccessibleDescription(tr("Model files in the open package."));
	m_modelEntries->setMinimumWidth(190);
	connect(m_modelEntries, &QListWidget::itemSelectionChanged, this, [this]() {
		showSelectedModel();
	});

	auto* viewportBar = createPageToolBar(tr("Model viewport controls"));
	viewportBar->setObjectName(QStringLiteral("viewportToolBar"));
	m_modelRenderMode = new QComboBox;
	m_modelRenderMode->setAccessibleName(tr("Model render mode"));
	m_modelRenderMode->addItem(tr("Textured"), static_cast<int>(ModelViewportRenderMode::Textured));
	m_modelRenderMode->addItem(tr("Flat shaded"), static_cast<int>(ModelViewportRenderMode::FlatShaded));
	m_modelRenderMode->addItem(tr("Wireframe"), static_cast<int>(ModelViewportRenderMode::Wireframe));
	connect(m_modelRenderMode, &QComboBox::currentIndexChanged, this, [this]() {
		if (m_modelViewport && m_modelRenderMode) {
			m_modelViewport->setRenderMode(static_cast<ModelViewportRenderMode>(m_modelRenderMode->currentData().toInt()));
		}
	});
	viewportBar->addWidget(m_modelRenderMode);
	m_modelAnimation = new QComboBox;
	m_modelAnimation->setAccessibleName(tr("Model animation"));
	m_modelAnimation->setToolTip(tr("Animations are inferred from frame names, which is how MDL and MD2 store them."));
	m_modelAnimation->setMinimumContentsLength(14);
	m_modelAnimation->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	connect(m_modelAnimation, &QComboBox::currentIndexChanged, this, [this]() {
		if (m_modelViewport && m_modelAnimation && m_modelAnimation->currentIndex() >= 0) {
			m_modelViewport->setAnimation(m_modelAnimation->currentData().toString());
		}
	});
	viewportBar->addWidget(m_modelAnimation);
	auto* playPause = createToolButton(QStringLiteral("play"), tr("Play"), tr("Play or pause the model animation (Space)."));
	m_modelPlayPause = playPause;
	m_modelPlayPause->setAccessibleName(tr("Play or pause the model animation"));
	connect(playPause, &QToolButton::clicked, this, [this]() {
		if (m_modelViewport) {
			m_modelViewport->togglePlayback();
		}
	});
	viewportBar->addWidget(playPause);
	viewportBar->addSeparator();
	auto* modelFrameView = createToolButton(QStringLiteral("frame"), tr("Frame Model"), tr("Fit the model in the viewport."), false);
	modelFrameView->setAccessibleName(tr("Fit the model in the viewport"));
	connect(modelFrameView, &QToolButton::clicked, this, [this]() {
		if (m_modelViewport) {
			m_modelViewport->frameModel();
		}
	});
	viewportBar->addWidget(modelFrameView);

	m_modelViewport = new ModelViewport;
	m_modelViewport->setAccessibleName(tr("Model viewport"));
	m_modelViewport->setAccessibleDescription(tr("Software-rendered view of the selected model. Drag to orbit, wheel to zoom, Space to play, Page Up and Page Down to step frames."));
	m_modelViewport->setMinimumSize(320, 260);
	// The combo box opens on its first mode; the viewport must start there too
	// rather than at its own default, or the two disagree until the user picks.
	m_modelViewport->setRenderMode(static_cast<ModelViewportRenderMode>(m_modelRenderMode->currentData().toInt()));
	connect(m_modelViewport, &ModelViewport::hoverChanged, this, [this](const QString& summary) {
		if (m_modelHover) {
			m_modelHover->setText(summary.isEmpty() ? m_modelViewport->playbackSummary() : summary);
		}
	});
	connect(m_modelViewport, &ModelViewport::playbackChanged, this, [this]() {
		refreshModelPlaybackControls();
	});
	connect(m_modelViewport, &ModelViewport::frameChanged, this, [this]() {
		refreshModelPlaybackControls();
	});
	m_modelHover = new ElidedLabel(tr("Drag to orbit the model, wheel to zoom."));
	m_modelHover->setObjectName("viewportReadout");
	m_modelHover->setAccessibleName(tr("Model viewport readout"));

	auto* centre = new QWidget;
	auto* centreLayout = new QVBoxLayout(centre);
	centreLayout->setContentsMargins(0, 0, 0, 0);
	centreLayout->setSpacing(0);
	centreLayout->addWidget(viewportBar);
	centreLayout->addWidget(m_modelViewport, 1);
	centreLayout->addWidget(m_modelHover);

	m_modelDetails = createPropertyGrid(tr("Model details"),
		tr("Frames, surfaces, tags, vertex and triangle counts, animations, and skin paths."));
	m_modelDetails->setObjectName("modelDetails");
	m_modelSkinPreview = new ImagePreviewView;
	m_modelSkinPreview->setAccessibleName(tr("Model skin preview"));
	m_modelSkinPreview->setAccessibleDescription(tr("First resolvable skin texture for the selected model, decoded from the package."));
	m_modelSkinPreview->setMinimumHeight(160);
	m_modelDrawer = new DetailDrawer;
	m_modelDrawer->setAccessibleName(tr("Model detail drawer"));
	m_modelDrawer->setTitle(tr("Model Details"));
	m_modelDrawer->setSubtitle(tr("Header fields, skin and material dependencies, and raw metadata."));
	m_modelDrawer->setEmbedded(true);

	auto* inspectorTabs = createPanelTabs(tr("Model inspector"), QTabWidget::South);
	inspectorTabs->setMinimumWidth(250);
	inspectorTabs->addTab(m_modelDetails, tr("Summary"));
	inspectorTabs->addTab(m_modelSkinPreview, tr("Skin"));
	inspectorTabs->addTab(m_modelDrawer, tr("Metadata"));

	auto* workbench = createSplitter(Qt::Horizontal, QStringLiteral("modelsWorkbench"), tr("Model browser layout"));
	workbench->addWidget(padded(captionedPanel(tr("Models"), m_modelEntries), 10, 8, 10));
	workbench->addWidget(centre);
	workbench->addWidget(padded(inspectorTabs, 10, 8, 10));
	workbench->setStretchFactor(0, 0);
	workbench->setStretchFactor(1, 1);
	workbench->setStretchFactor(2, 0);
	workbench->setSizes({scaledPane(250), 720, scaledPane(320)});
	m_layoutSplitters.insert(workbench->objectName(), workbench);

	auto* body = new QWidget;
	auto* bodyLayout = new QVBoxLayout(body);
	bodyLayout->setContentsMargins(0, 0, 0, 0);
	bodyLayout->setSpacing(0);
	bodyLayout->addWidget(statusStripHost(m_modelState));
	bodyLayout->addWidget(workbench, 1);

	auto* empty = new EmptyStateView(QStringLiteral("cube"), tr("No models to show"),
		tr("Open a package that contains MDL, MD2, or MD3 models to orbit them, play their animations, and inspect skins."));
	QPushButton* emptyOpen = empty->addAction(tr("Open Package"), QStringLiteral("folder-open"), true);
	connect(emptyOpen, &QPushButton::clicked, this, [this]() {
		openPackageFile();
	});
	auto* stack = new QStackedWidget;
	stack->addWidget(empty);
	stack->addWidget(body);
	m_surfaceStacks.insert(static_cast<int>(StudioMode::Models), stack);
	m_emptyStates.insert(static_cast<int>(StudioMode::Models), empty);
	return assembleStudioPage(header, nullptr, stack);
}

QWidget* ApplicationShell::buildAudioPage()
{
	auto* header = new PageHeader(QStringLiteral("waveform"), tr("Audio"));
	header->setAccessibleName(tr("Audio workbench header"));
	m_pageHeaders.insert(static_cast<int>(StudioMode::Audio), header);

	m_audioState = new LoadingPane;
	m_audioState->setAccessibleName(tr("Audio browser state"));
	m_audioState->setTitle(tr("Audio"));
	m_audioState->setDetail(tr("WAV, Ogg, MP3, and FLAC metadata with a decoded waveform where samples are readable."));
	m_audioState->setPlaceholderRows({
		tr("Audio list"),
		tr("Waveform"),
		tr("Format details"),
	});

	m_audioEntries = new QListWidget;
	m_audioEntries->setObjectName("audioEntries");
	m_audioEntries->setAccessibleName(tr("Audio entries"));
	m_audioEntries->setAccessibleDescription(tr("Audio files in the open package."));
	m_audioEntries->setMinimumWidth(190);
	connect(m_audioEntries, &QListWidget::itemSelectionChanged, this, [this]() {
		showSelectedAudioEntry();
	});

	m_audioWaveform = new WaveformView;
	m_audioWaveform->setAccessibleName(tr("Audio waveform"));
	m_audioWaveform->setAccessibleDescription(tr("Per-channel minimum and maximum envelope decoded from the selected entry."));
	m_audioWaveform->setMinimumHeight(200);

	m_audioDetails = createPropertyGrid(tr("Audio details"),
		tr("Codec, channels, sample rate, bit depth, bitrate, and duration."));
	m_audioDetails->setObjectName("audioDetails");
	m_audioDetails->setRootIsDecorated(false);
	m_audioDetails->setColumnWidth(0, 150);

	m_audioDrawer = new DetailDrawer;
	m_audioDrawer->setAccessibleName(tr("Audio detail drawer"));
	m_audioDrawer->setTitle(tr("Audio Details"));
	m_audioDrawer->setSubtitle(tr("Format metadata, export support, and raw header details."));
	m_audioDrawer->setEmbedded(true);

	auto* inspectorTabs = createPanelTabs(tr("Audio inspector"), QTabWidget::South);
	inspectorTabs->addTab(m_audioDetails, tr("Format"));
	inspectorTabs->addTab(m_audioDrawer, tr("Metadata"));

	auto* detailSplit = createSplitter(Qt::Vertical, QStringLiteral("audioDetailSplit"), tr("Waveform and details"));
	detailSplit->addWidget(padded(m_audioWaveform, 10, 8, 4));
	detailSplit->addWidget(padded(inspectorTabs, 10, 4, 10));
	detailSplit->setStretchFactor(0, 3);
	detailSplit->setStretchFactor(1, 2);
	m_layoutSplitters.insert(detailSplit->objectName(), detailSplit);

	auto* workbench = createSplitter(Qt::Horizontal, QStringLiteral("audioWorkbench"), tr("Audio browser layout"));
	workbench->addWidget(padded(captionedPanel(tr("Sounds"), m_audioEntries), 10, 8, 10));
	workbench->addWidget(detailSplit);
	workbench->setStretchFactor(0, 0);
	workbench->setStretchFactor(1, 1);
	workbench->setSizes({scaledPane(280), 900});
	m_layoutSplitters.insert(workbench->objectName(), workbench);

	auto* body = new QWidget;
	auto* bodyLayout = new QVBoxLayout(body);
	bodyLayout->setContentsMargins(0, 0, 0, 0);
	bodyLayout->setSpacing(0);
	bodyLayout->addWidget(statusStripHost(m_audioState));
	bodyLayout->addWidget(workbench, 1);

	auto* empty = new EmptyStateView(QStringLiteral("waveform"), tr("No sounds to show"),
		tr("Open a package with WAV, Ogg, MP3, or FLAC files to inspect their format and see a decoded waveform."));
	QPushButton* emptyOpen = empty->addAction(tr("Open Package"), QStringLiteral("folder-open"), true);
	connect(emptyOpen, &QPushButton::clicked, this, [this]() {
		openPackageFile();
	});
	auto* stack = new QStackedWidget;
	stack->addWidget(empty);
	stack->addWidget(body);
	m_surfaceStacks.insert(static_cast<int>(StudioMode::Audio), stack);
	m_emptyStates.insert(static_cast<int>(StudioMode::Audio), empty);
	return assembleStudioPage(header, nullptr, stack);
}

QWidget* ApplicationShell::buildCodePage()
{
	auto* header = new PageHeader(QStringLiteral("code"), tr("Code"));
	header->setAccessibleName(tr("Code workbench header"));
	m_pageHeaders.insert(static_cast<int>(StudioMode::Code), header);

	m_advancedCodeIndexButton = createButton(tr("Index Code"), QStringLiteral("hash"));
	m_advancedCodeIndexButton->setAccessibleName(tr("Index code workspace"));
	m_advancedCodeIndexButton->setToolTip(tr("Scan the project for languages, symbols, build tasks, and launch profiles."));
	connect(m_advancedCodeIndexButton, &QPushButton::clicked, this, [this]() {
		indexAdvancedCodeWorkspace();
	});
	header->addActionWidget(m_advancedCodeIndexButton);

	auto* toolBar = createPageToolBar(tr("Code tools"));
	auto* refreshTree = createToolButton(QStringLiteral("refresh"), tr("Refresh Tree"), tr("Rescan the project for editable files."), false);
	refreshTree->setAccessibleName(tr("Refresh source tree"));
	connect(refreshTree, &QToolButton::clicked, this, [this]() {
		refreshCodeWorkspaceTree();
	});
	toolBar->addWidget(refreshTree);
	auto* saveCode = createToolButton(QStringLiteral("save"), tr("Save"), tr("Save the file open in the editor."));
	saveCode->setAccessibleName(tr("Save edited file"));
	connect(saveCode, &QToolButton::clicked, this, [this]() {
		saveCodeFile();
	});
	toolBar->addWidget(saveCode);
	toolBar->addSeparator();
	m_codeFind = new QLineEdit;
	m_codeFind->setAccessibleName(tr("Find text"));
	m_codeFind->setPlaceholderText(tr("Find in project"));
	m_codeFind->setClearButtonEnabled(true);
	m_codeFind->setMinimumWidth(200);
	m_codeFind->addAction(studioIcon(QStringLiteral("search"), StudioIconTone::Muted), QLineEdit::LeadingPosition);
	connect(m_codeFind, &QLineEdit::returnPressed, this, [this]() {
		runCodeFindReplace();
	});
	toolBar->addWidget(m_codeFind);
	m_codeReplace = new QLineEdit;
	m_codeReplace->setAccessibleName(tr("Replace text"));
	m_codeReplace->setPlaceholderText(tr("Replace with (optional)"));
	m_codeReplace->setClearButtonEnabled(true);
	m_codeReplace->setMinimumWidth(180);
	toolBar->addWidget(m_codeReplace);
	auto* runFind = createToolButton(QStringLiteral("search"), tr("Find / Replace"), tr("Search every project text file. Replacements are previewed before anything is written."));
	runFind->setAccessibleName(tr("Run project find and replace"));
	connect(runFind, &QToolButton::clicked, this, [this]() {
		runCodeFindReplace();
	});
	toolBar->addWidget(runFind);

	m_codeTree = new QTreeWidget;
	m_codeTree->setObjectName("codeTree");
	m_codeTree->setAccessibleName(tr("Project source tree"));
	m_codeTree->setAccessibleDescription(tr("Editable text, script, config, and code files in the current project."));
	m_codeTree->setHeaderHidden(true);
	m_codeTree->setMinimumWidth(190);
	connect(m_codeTree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem*, int) {
		openSelectedCodeFile();
	});
	connect(m_codeTree, &QTreeWidget::itemSelectionChanged, this, [this]() {
		openSelectedCodeFile();
	});

	m_codeStatus = new ElidedLabel(tr("No file open."));
	m_codeStatus->setObjectName("viewportReadout");
	m_codeStatus->setAccessibleName(tr("Editor save state"));
	m_codeStatus->setElideMode(Qt::ElideMiddle);

	// Line-number gutter and current-line band; highlighting stays separate.
	m_codeEditor = new StudioCodeEditor;
	m_codeEditor->setObjectName("codeEditor");
	m_codeEditor->setAccessibleName(tr("Code editor"));
	m_codeEditor->setAccessibleDescription(tr("Edits project scripts, configs, shaders, and QuakeC with syntax highlighting and compiler diagnostics."));
	m_codeEditor->setLineWrapMode(QPlainTextEdit::NoWrap);
	m_codeEditor->setFont(studioMonospaceFont());
	m_codeEditor->setTabStopDistance(QFontMetricsF(m_codeEditor->font()).horizontalAdvance(QLatin1Char(' ')) * 4.0);
	m_codeEditor->setMinimumHeight(200);
	m_codeEditor->setPlaceholderText(tr("Choose a file under Project files to edit it here."));
	m_codeHighlighter = new StudioSyntaxHighlighter(m_codeEditor->document());
	connect(m_codeEditor, &QPlainTextEdit::textChanged, this, [this]() {
		if (!m_codeDirty && !m_codeFilePath.isEmpty()) {
			m_codeDirty = true;
			if (m_codeStatus) {
				m_codeStatus->setText(codeDocumentLabel(m_codeFilePath, tr("Modified")));
				m_codeStatus->setToolTip(nativePath(m_codeFilePath));
			}
			refreshCommandEnablement();
		}
	});

	auto* editorPane = new QWidget;
	auto* editorLayout = new QVBoxLayout(editorPane);
	editorLayout->setContentsMargins(0, 0, 0, 0);
	editorLayout->setSpacing(0);
	editorLayout->addWidget(m_codeStatus);
	editorLayout->addWidget(m_codeEditor, 1);

	m_codeDiagnostics = new QListWidget;
	m_codeDiagnostics->setObjectName("codeDiagnostics");
	m_codeDiagnostics->setAccessibleName(tr("Code diagnostics"));
	m_codeDiagnostics->setAccessibleDescription(tr("Syntax and compiler diagnostics for the open file. Activate a row to jump to its line."));
	connect(m_codeDiagnostics, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
		if (!item || !m_codeEditor) {
			return;
		}
		const int line = item->data(Qt::UserRole).toInt();
		if (line <= 0) {
			return;
		}
		QTextCursor cursor(m_codeEditor->document()->findBlockByNumber(line - 1));
		m_codeEditor->setTextCursor(cursor);
		m_codeEditor->centerCursor();
		m_codeEditor->setFocus();
	});

	m_advancedCodeTree = new QListWidget;
	m_advancedCodeTree->setAccessibleName(tr("Code source index"));
	m_advancedCodeTree->setAccessibleDescription(tr("Project source index, language hooks, symbol search, diagnostics, build tasks, and launch profiles."));

	auto* bottomTabs = createPanelTabs(tr("Code output panels"), QTabWidget::South);
	bottomTabs->addTab(m_codeDiagnostics, studioIcon(QStringLiteral("warning")), tr("Problems"));
	bottomTabs->addTab(m_advancedCodeTree, studioIcon(QStringLiteral("hash")), tr("Index"));

	auto* editorSplit = createSplitter(Qt::Vertical, QStringLiteral("codeEditorSplit"), tr("Editor and output"));
	editorSplit->addWidget(editorPane);
	editorSplit->addWidget(padded(bottomTabs, 10, 6, 8));
	editorSplit->setStretchFactor(0, 3);
	editorSplit->setStretchFactor(1, 1);
	editorSplit->setSizes({560, 200});
	m_layoutSplitters.insert(editorSplit->objectName(), editorSplit);

	auto* workbench = createSplitter(Qt::Horizontal, QStringLiteral("codeWorkbench"), tr("Code workbench layout"));
	workbench->addWidget(padded(captionedPanel(tr("Project files"), m_codeTree), 10, 8, 10));
	workbench->addWidget(editorSplit);
	workbench->setStretchFactor(0, 0);
	workbench->setStretchFactor(1, 1);
	workbench->setSizes({scaledPane(270), 1000});
	m_layoutSplitters.insert(workbench->objectName(), workbench);

	auto* empty = new EmptyStateView(QStringLiteral("code"), tr("No project open"),
		tr("Open a project folder to browse and edit its scripts, configs, shaders, and QuakeC with highlighting and diagnostics."));
	QPushButton* emptyOpen = empty->addAction(tr("Open Project"), QStringLiteral("folder-open"), true);
	connect(emptyOpen, &QPushButton::clicked, this, [this]() {
		openProjectFolder();
	});
	auto* stack = new QStackedWidget;
	stack->addWidget(empty);
	stack->addWidget(workbench);
	m_surfaceStacks.insert(static_cast<int>(StudioMode::Code), stack);
	m_emptyStates.insert(static_cast<int>(StudioMode::Code), empty);
	return assembleStudioPage(header, toolBar, stack);
}

QWidget* ApplicationShell::buildSidePanel()
{
	// Activity: every queued, running, and finished task with its log.
	auto* activity = new QWidget;
	activity->setObjectName(QStringLiteral("dockBody"));
	activity->setAttribute(Qt::WA_StyledBackground, true);
	auto* activityLayout = new QVBoxLayout(activity);
	activityLayout->setContentsMargins(10, 10, 10, 10);
	activityLayout->setSpacing(8);
	m_activitySummary = new QLabel;
	m_activitySummary->setObjectName("cardMeta");
	m_activitySummary->setAccessibleName(tr("Activity summary"));
	m_activitySummary->setWordWrap(true);
	activityLayout->addWidget(m_activitySummary);

	m_activityTasks = new QListWidget;
	m_activityTasks->setObjectName("activityTasks");
	m_activityTasks->setAccessibleName(tr("Activity tasks"));
	m_activityTasks->setAccessibleDescription(tr("Queued, running, warning, failed, cancelled, and completed tasks."));
	m_activityTasks->setMinimumHeight(160);
	connect(m_activityTasks, &QListWidget::itemSelectionChanged, this, [this]() {
		refreshActivityDetails(selectedActivityTaskId());
	});
	activityLayout->addWidget(m_activityTasks, 2);

	auto* activityActions = new QHBoxLayout;
	activityActions->setSpacing(6);
	m_activityCancel = createButton(tr("Cancel"), QStringLiteral("stop"));
	m_activityCancel->setAccessibleName(tr("Cancel selected activity"));
	connect(m_activityCancel, &QPushButton::clicked, this, [this]() {
		cancelSelectedActivityTask();
	});
	activityActions->addWidget(m_activityCancel);
	activityActions->addStretch(1);
	m_activityClearFinished = createButton(tr("Clear Finished"), QStringLiteral("trash"), QStringLiteral("ghost"));
	m_activityClearFinished->setAccessibleName(tr("Clear finished activities"));
	connect(m_activityClearFinished, &QPushButton::clicked, this, [this]() {
		clearFinishedActivityTasks();
	});
	activityActions->addWidget(m_activityClearFinished);
	activityLayout->addLayout(activityActions);

	m_activityState = new LoadingPane;
	m_activityState->setAccessibleName(tr("Selected activity state"));
	m_activityState->setPlaceholderRows({
		tr("Task context"),
		tr("Result summary"),
		tr("Structured log"),
	});
	activityLayout->addWidget(m_activityState);

	m_activityDrawer = new DetailDrawer;
	m_activityDrawer->setAccessibleName(tr("Activity detail drawer"));
	m_activityDrawer->setTitle(tr("Task Details"));
	m_activityDrawer->setSubtitle(tr("Select an activity to inspect logs, warnings, timing, and raw task metadata."));
	m_activityDrawer->setEmbedded(true);
	activityLayout->addWidget(m_activityDrawer, 3);

	// Inspector: settings, setup, and project diagnostics for support work.
	auto* inspectorPage = new QWidget;
	inspectorPage->setObjectName(QStringLiteral("dockBody"));
	inspectorPage->setAttribute(Qt::WA_StyledBackground, true);
	auto* inspectorLayout = new QVBoxLayout(inspectorPage);
	inspectorLayout->setContentsMargins(10, 10, 10, 10);
	inspectorLayout->setSpacing(8);

	m_inspectorState = new LoadingPane;
	m_inspectorState->setAccessibleName(tr("Inspector state"));
	m_inspectorState->setPlaceholderRows({
		tr("Settings metadata"),
		tr("Setup status"),
		tr("Raw diagnostics"),
	});
	inspectorLayout->addWidget(m_inspectorState);

	m_inspector = new QTextEdit;
	m_inspector->setObjectName("inspector");
	m_inspector->setAccessibleName(tr("Inspector"));
	m_inspector->setAccessibleDescription(tr("Shows settings, recent project, compiler, and project diagnostics."));
	m_inspector->setReadOnly(true);
	m_inspector->setMinimumHeight(120);
	m_inspector->setMaximumHeight(220);
	inspectorLayout->addWidget(m_inspector);

	m_inspectorDrawer = new DetailDrawer;
	m_inspectorDrawer->setAccessibleName(tr("Inspector detail drawer"));
	m_inspectorDrawer->setTitle(tr("Inspector Details"));
	m_inspectorDrawer->setSubtitle(tr("Settings, setup, and raw diagnostics."));
	m_inspectorDrawer->setEmbedded(true);
	inspectorLayout->addWidget(m_inspectorDrawer, 1);

	m_activityDock = new QDockWidget(tr("Activity"), this);
	m_activityDock->setObjectName(QStringLiteral("activityDock"));
	m_activityDock->setAccessibleName(tr("Activity panel"));
	m_activityDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
	m_activityDock->setWidget(activity);
	m_activityDock->setTitleBarWidget(new DockTitleBar(m_activityDock));
	m_activityDock->setMinimumWidth(300);
	addDockWidget(Qt::RightDockWidgetArea, m_activityDock);

	m_inspectorDock = new QDockWidget(tr("Inspector"), this);
	m_inspectorDock->setObjectName(QStringLiteral("inspectorDock"));
	m_inspectorDock->setAccessibleName(tr("Inspector panel"));
	m_inspectorDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
	m_inspectorDock->setWidget(inspectorPage);
	m_inspectorDock->setTitleBarWidget(new DockTitleBar(m_inspectorDock));
	m_inspectorDock->setMinimumWidth(300);
	addDockWidget(Qt::RightDockWidgetArea, m_inspectorDock);
	tabifyDockWidget(m_activityDock, m_inspectorDock);
	m_activityDock->raise();

	// Both panels start closed so every work surface gets the full width; the
	// status bar and the View menu open them, and the window state remembers
	// the user's arrangement from then on.
	m_activityDock->hide();
	m_inspectorDock->hide();

	QAction* activityToggle = m_activityDock->toggleViewAction();
	activityToggle->setText(tr("&Activity Panel"));
	activityToggle->setIcon(studioIcon(QStringLiteral("activity")));
	activityToggle->setStatusTip(tr("Show or hide the task list with progress, logs, warnings, and cancellation."));
	QAction* inspectorToggle = m_inspectorDock->toggleViewAction();
	inspectorToggle->setText(tr("&Inspector Panel"));
	inspectorToggle->setIcon(studioIcon(QStringLiteral("sidebar-right")));
	inspectorToggle->setStatusTip(tr("Show or hide settings, setup, and project diagnostics."));
	if (QMenu* viewMenu = m_viewMenu) {
		viewMenu->addSeparator();
		viewMenu->addAction(activityToggle);
		viewMenu->addAction(inspectorToggle);
	}
	if (m_activityToggle) {
		connect(m_activityToggle, &QToolButton::clicked, this, [this]() {
			const bool show = !m_activityDock->isVisible();
			m_activityDock->setVisible(show);
			if (show) {
				m_activityDock->raise();
			}
		});
		connect(m_activityDock, &QDockWidget::visibilityChanged, this, [this](bool) {
			if (m_activityToggle) {
				m_activityToggle->setChecked(m_activityDock->isVisible());
			}
		});
	}
	if (m_inspectorToggle) {
		connect(m_inspectorToggle, &QToolButton::clicked, this, [this]() {
			const bool show = !m_inspectorDock->isVisible();
			m_inspectorDock->setVisible(show);
			if (show) {
				m_inspectorDock->raise();
			}
		});
		connect(m_inspectorDock, &QDockWidget::visibilityChanged, this, [this](bool) {
			if (m_inspectorToggle) {
				m_inspectorToggle->setChecked(m_inspectorDock->isVisible());
			}
		});
	}
	return activity;
}


void ApplicationShell::loadShellState()
{
	if (m_modeRail && m_modeRail->count() > 0) {
		const int savedMode = std::clamp(m_settings.selectedMode(), 0, m_modeRail->count() - 1);
		m_modeRail->setCurrentId(savedMode);
		if (m_modeStack) {
			m_modeStack->setCurrentIndex(savedMode);
		}
	}

	const QByteArray geometry = m_settings.shellGeometry();
	if (!geometry.isEmpty()) {
		restoreGeometry(geometry);
	}

	// Dock visibility and placement come back through the window state; a
	// state saved before the docks existed leaves them at their defaults.
	const QByteArray windowState = m_settings.shellWindowState();
	if (!windowState.isEmpty()) {
		restoreState(windowState);
	}

	for (auto it = m_layoutSplitters.cbegin(); it != m_layoutSplitters.cend(); ++it) {
		const QByteArray splitterState = m_settings.shellLayoutState(it.key());
		if (!splitterState.isEmpty() && it.value()) {
			it.value()->restoreState(splitterState);
		}
	}
}

void ApplicationShell::saveShellState()
{
	if (m_modeRail) {
		m_settings.setSelectedMode(m_modeRail->currentId());
	}
	m_settings.setShellGeometry(saveGeometry());
	m_settings.setShellWindowState(saveState());
	for (auto it = m_layoutSplitters.cbegin(); it != m_layoutSplitters.cend(); ++it) {
		if (it.value()) {
			m_settings.setShellLayoutState(it.key(), it.value()->saveState());
		}
	}
}

void ApplicationShell::refreshWorkspaceDashboard()
{
	if (!m_workspaceState || !m_workspaceDrawer) {
		return;
	}

	const QString projectPath = m_settings.currentProjectPath();
	const AccessibilityPreferences preferences = m_settings.accessibilityPreferences();
	m_workspaceState->setReducedMotion(preferences.reducedMotion);
	if (projectPath.isEmpty()) {
		m_workspaceState->setTitle(tr("No Project Open"));
		m_workspaceState->setDetail(tr("Open a project folder to create or inspect its VibeStudio manifest."));
		m_workspaceState->setState(OperationState::Idle, tr("Idle"));
		m_workspaceState->setProgress({});
		m_workspaceDrawer->setTitle(tr("Workspace Details"));
		m_workspaceDrawer->setSubtitle(tr("No project is open."));
		m_workspaceDrawer->setSections({});
		refreshWorkspaceContextPanels();
		return;
	}

	ProjectManifest manifest;
	QString error;
	const bool manifestLoaded = loadProjectManifest(projectPath, &manifest, &error);
	if (!manifestLoaded) {
		manifest = defaultProjectManifest(projectPath);
		manifest.selectedInstallationId = m_settings.selectedGameInstallationId();
	}
	// Watch the manifest so a change made by another tool, or by hand, does not
	// sit unnoticed behind the workspace panels built from it.
	registerWatchedDocument(projectManifestPath(projectPath), DocumentWatchRole::ProjectManifest, QString(), false);
	const ProjectHealthSummary health = buildProjectHealthSummary(manifest, m_settings.selectedGameInstallationId());
	const OperationState state = health.overallState();
	const QString effectiveInstallation = effectiveProjectInstallationId(manifest, m_settings.selectedGameInstallationId());
	const QString effectiveEditorProfile = effectiveProjectEditorProfileId(manifest, m_settings.selectedEditorProfileId());
	const CompilerRegistrySummary compilerRegistry = discoverCompilerTools(compilerRegistryOptionsForProject(projectPath, m_settings));
	const PackageArchiveSummary packageSummary = m_packageArchive.summary();

	m_workspaceState->setTitle(manifestLoaded ? tr("Workspace Dashboard") : tr("Workspace Dashboard"));
	m_workspaceState->setDetail(manifestLoaded
			? tr("%1 / %2 / install: %3").arg(manifest.displayName, nativePath(projectPath), effectiveInstallation.isEmpty() ? tr("none") : effectiveInstallation)
			: tr("Manifest not initialized: %1").arg(error));
	m_workspaceState->setState(manifestLoaded ? state : OperationState::Warning, manifestLoaded ? localizedOperationStateName(state) : tr("Needs Manifest"));
	m_workspaceState->setProgress({health.readyCount, std::max(1, health.readyCount + health.warningCount + health.failedCount)});

	QStringList manifestLines;
	manifestLines << projectManifestToText(manifest);
	if (!manifestLoaded) {
		manifestLines << QString();
		manifestLines << tr("Manifest status: %1").arg(error);
	}

	QStringList rawLines;
	rawLines << tr("Current project path: %1").arg(nativePath(projectPath));
	rawLines << tr("Manifest path: %1").arg(nativePath(projectManifestPath(projectPath)));
	rawLines << tr("Manifest loaded: %1").arg(manifestLoaded ? tr("yes") : tr("no"));
	rawLines << tr("Health ready: %1").arg(health.readyCount);
	rawLines << tr("Health warnings: %1").arg(health.warningCount);
	rawLines << tr("Health failures: %1").arg(health.failedCount);
	rawLines << tr("Effective installation: %1").arg(effectiveInstallation.isEmpty() ? tr("none") : effectiveInstallation);
	rawLines << tr("Effective editor profile: %1").arg(effectiveEditorProfile.isEmpty() ? tr("none") : effectiveEditorProfile);
	rawLines << tr("Package loaded: %1").arg(m_packageArchive.isOpen() ? tr("yes") : tr("no"));
	rawLines << tr("Compiler executables: %1 of %2").arg(compilerRegistry.executableAvailableCount).arg(compilerRegistry.tools.size());

	QStringList packageLines;
	if (m_packageArchive.isOpen()) {
		packageLines << tr("Source: %1").arg(nativePath(packageSummary.sourcePath));
		packageLines << tr("Format: %1").arg(localizedPackageFormatName(packageSummary.format));
		packageLines << tr("Entries: %1").arg(packageSummary.entryCount);
		packageLines << tr("Warnings: %1").arg(packageSummary.warningCount);
	} else {
		packageLines << tr("No package is mounted yet.");
		packageLines << tr("Open a package file or folder to connect package context to the workspace.");
	}

	QStringList installLines;
	const QVector<GameInstallationProfile> profiles = m_settings.gameInstallations();
	const GameInstallationProfile* selectedProfile = nullptr;
	for (const GameInstallationProfile& profile : profiles) {
		if (sameGameInstallationId(profile.id, effectiveInstallation)) {
			selectedProfile = &profile;
			break;
		}
	}
	if (selectedProfile) {
		installLines = gameInstallationDetailLines(*selectedProfile, effectiveInstallation);
	} else {
		installLines << tr("No linked installation profile is available.");
		installLines << tr("Add one manually or detect Steam/GOG candidates, then select or initialize the project manifest.");
	}

	m_workspaceDrawer->setTitle(tr("Workspace Details"));
	m_workspaceDrawer->setSubtitle(manifest.displayName);
	m_workspaceDrawer->setSections({
		{QStringLiteral("health"), tr("Health Summary"), localizedOperationStateName(state), projectHealthLines(health).join('\n').trimmed(), state},
		{QStringLiteral("manifest"), tr("Project Manifest"), manifest.projectId, manifestLines.join('\n'), manifestLoaded ? OperationState::Completed : OperationState::Warning},
		{QStringLiteral("installation"), tr("Install Validation"), effectiveInstallation.isEmpty() ? tr("No install") : effectiveInstallation, installLines.join('\n'), selectedProfile ? (validateGameInstallationProfile(*selectedProfile).isUsable() ? OperationState::Completed : OperationState::Warning) : OperationState::Warning},
		{QStringLiteral("packages"), tr("Mounted Package Roots"), m_packageArchive.isOpen() ? localizedPackageFormatName(packageSummary.format) : tr("No package"), packageLines.join('\n'), m_packageArchive.isOpen() ? OperationState::Completed : OperationState::Warning},
		{QStringLiteral("compilers"), tr("Compiler Context"), tr("%1 of %2 executables").arg(compilerRegistry.executableAvailableCount).arg(compilerRegistry.tools.size()), compilerRegistrySummaryText(compilerRegistry), compilerRegistry.overallState()},
		{QStringLiteral("raw"), tr("Raw Workspace"), nativePath(projectPath), rawLines.join('\n'), manifestLoaded ? state : OperationState::Warning},
	});
	m_workspaceDrawer->showSection(QStringLiteral("health"));
	refreshWorkspaceContextPanels();
}

void ApplicationShell::initializeCurrentProjectManifest()
{
	const QString projectPath = m_settings.currentProjectPath();
	if (projectPath.isEmpty()) {
		statusBar()->showMessage(tr("Open a project folder before initializing a manifest"));
		return;
	}

	ProjectManifest manifest;
	QString error;
	if (!loadProjectManifest(projectPath, &manifest, &error)) {
		manifest = defaultProjectManifest(projectPath);
	}
	const QString selectedInstallationId = m_settings.selectedGameInstallationId();
	if (!selectedInstallationId.isEmpty()) {
		manifest.selectedInstallationId = selectedInstallationId;
		manifest.settingsOverrides.selectedInstallationId = selectedInstallationId;
	}
	manifest.settingsOverrides.editorProfileId = m_settings.selectedEditorProfileId();
	if (!saveProjectManifest(manifest, &error)) {
		recordActivity(tr("Project Manifest Failed"), nativePath(projectPath), tr("project"), OperationState::Failed, error);
		statusBar()->showMessage(tr("Project manifest failed: %1").arg(error));
		refreshWorkspaceDashboard();
		return;
	}

	recordActivity(tr("Project Manifest Saved"), nativePath(projectManifestPath(projectPath)), tr("project"), OperationState::Completed, tr("Workspace manifest initialized."));
	refreshWorkspaceDashboard();
	refreshInspectorDrawerForSettings();
	statusBar()->showMessage(tr("Project manifest saved: %1").arg(nativePath(projectManifestPath(projectPath))));
}

void ApplicationShell::refreshRecentProjects()
{
	m_recentProjects->clear();

	const QVector<RecentProject> projects = m_settings.recentProjects();
	m_recentSummary->setText(tr("%n remembered", nullptr, projects.size()));

	const QString separator = QStringLiteral("  %1  ").arg(QChar(0x00b7));
	const QString currentPath = normalizedProjectPath(m_settings.currentProjectPath());
	for (const RecentProject& project : projects) {
		const QString state = project.exists ? tr("Ready") : tr("Missing");
		const QString timestamp = QLocale::system().toString(project.lastOpenedUtc.toLocalTime(), QLocale::ShortFormat);
		QString title = project.displayName;
		if (!project.exists) {
			title += separator + tr("missing");
		} else if (!currentPath.isEmpty() && project.path == currentPath) {
			title += separator + tr("open");
		}
		auto* item = new QListWidgetItem(studioIcon(project.exists ? QStringLiteral("folder") : QStringLiteral("warning"), project.exists ? StudioIconTone::Muted : StudioIconTone::Warning),
			QStringLiteral("%1\n%2").arg(title, nativePath(project.path) + separator + timestamp));
		item->setData(Qt::UserRole, project.path);
		item->setToolTip(tr("%1\n%2\nLast opened %3. Double-click to open.").arg(project.displayName, nativePath(project.path), timestamp));
		item->setData(Qt::AccessibleTextRole, tr("%1, %2, %3").arg(project.displayName, state, nativePath(project.path)));
		if (!project.exists) {
			item->setForeground(currentStudioTheme().colors.warning);
		}
		m_recentProjects->addItem(item);
	}

	if (projects.isEmpty()) {
		auto* item = new QListWidgetItem(tr("No recent projects"));
		item->setFlags(Qt::NoItemFlags);
		m_recentProjects->addItem(item);
	}
}

void ApplicationShell::refreshGameInstallations()
{
	if (!m_gameInstallations || !m_installSummary) {
		return;
	}

	m_gameInstallations->clear();
	const QVector<GameInstallationProfile> profiles = m_settings.gameInstallations();
	const QString selectedId = m_settings.selectedGameInstallationId();
	if (profiles.isEmpty() && m_detectedInstallationCandidates.isEmpty()) {
		m_installSummary->setText(tr("No install profiles"));
		auto* item = new QListWidgetItem(tr("No game installations"));
		item->setFlags(Qt::NoItemFlags);
		m_gameInstallations->addItem(item);
		if (m_importDetectedInstall) {
			m_importDetectedInstall->setEnabled(false);
		}
		return;
	}

	QString selectedName = tr("none");
	for (const GameInstallationProfile& profile : profiles) {
		if (sameGameInstallationId(profile.id, selectedId)) {
			selectedName = profile.displayName;
			break;
		}
	}
	m_installSummary->setText(m_detectedInstallationCandidates.isEmpty()
			? tr("%n profiles / selected: %1", nullptr, profiles.size()).arg(selectedName)
			: tr("%1 profiles / %2 detected / selected: %3").arg(profiles.size()).arg(m_detectedInstallationCandidates.size()).arg(selectedName));

	for (const GameInstallationProfile& profile : profiles) {
		const GameInstallationValidation validation = validateGameInstallationProfile(profile);
		const bool selected = sameGameInstallationId(profile.id, selectedId);
		const QString state = validation.isUsable() ? tr("Ready") : tr("Needs Review");
		const QString separator = QStringLiteral("  %1  ").arg(QChar(0x00b7));
		QString title = profile.displayName + separator + state;
		if (selected) {
			title += separator + tr("in use");
		}
		auto* item = new QListWidgetItem(studioIcon(QStringLiteral("gamepad"), selected ? StudioIconTone::Accent : StudioIconTone::Muted),
			QStringLiteral("%1\n%2").arg(title, localizedGameEngineFamilyName(profile.engineFamily) + separator + nativePath(profile.rootPath)));
		item->setData(Qt::AccessibleTextRole, tr("%1, %2%3, %4, %5").arg(profile.displayName, state, selected ? tr(", in use") : QString(),
			localizedGameEngineFamilyName(profile.engineFamily), nativePath(profile.rootPath)));
		item->setData(Qt::UserRole, profile.id);
		item->setData(Qt::UserRole + 1, operationStateId(validation.isUsable() ? OperationState::Completed : OperationState::Warning));
		item->setData(Qt::UserRole + 2, QStringLiteral("profile"));
		item->setToolTip(nativePath(profile.rootPath));
		m_gameInstallations->addItem(item);
		if (selected) {
			m_gameInstallations->setCurrentItem(item);
		}
	}

	if (!m_detectedInstallationCandidates.isEmpty()) {
		m_gameInstallations->addItem(disabledListItem(tr("Detected candidates")));
		for (int index = 0; index < m_detectedInstallationCandidates.size(); ++index) {
			const GameInstallationDetectionCandidate& candidate = m_detectedInstallationCandidates[index];
			const QString separator = QStringLiteral("  %1  ").arg(QChar(0x00b7));
			auto* item = new QListWidgetItem(studioIcon(QStringLiteral("search"), StudioIconTone::Muted),
				QStringLiteral("%1\n%2")
					.arg(candidate.profile.displayName + separator + tr("%1, %2% match").arg(candidate.sourceName).arg(candidate.confidencePercent),
						localizedGameEngineFamilyName(candidate.profile.engineFamily) + separator + nativePath(candidate.profile.rootPath)));
			item->setData(Qt::UserRole, candidate.profile.id);
			item->setData(Qt::UserRole + 1, operationStateId(candidate.confidencePercent >= 80 ? OperationState::Completed : OperationState::Warning));
			item->setData(Qt::UserRole + 2, QStringLiteral("candidate"));
			item->setData(Qt::UserRole + 3, index);
			item->setToolTip(tr("Detected candidate. Use Import Detected to save this profile without modifying game files."));
			m_gameInstallations->addItem(item);
		}
	}
	if (!m_gameInstallations->currentItem()) {
		m_gameInstallations->setCurrentRow(0);
	}
	if (m_importDetectedInstall) {
		m_importDetectedInstall->setEnabled(selectedDetectedInstallationIndex() >= 0);
	}
}

QString ApplicationShell::selectedGameInstallationId() const
{
	const QListWidgetItem* item = m_gameInstallations ? m_gameInstallations->currentItem() : nullptr;
	if (!item || item->data(Qt::UserRole + 2).toString() == QStringLiteral("candidate")) {
		return {};
	}
	return item->data(Qt::UserRole).toString();
}

int ApplicationShell::selectedDetectedInstallationIndex() const
{
	const QListWidgetItem* item = m_gameInstallations ? m_gameInstallations->currentItem() : nullptr;
	if (!item || item->data(Qt::UserRole + 2).toString() != QStringLiteral("candidate")) {
		return -1;
	}
	const int index = item->data(Qt::UserRole + 3).toInt();
	return index >= 0 && index < m_detectedInstallationCandidates.size() ? index : -1;
}

void ApplicationShell::addGameInstallationProfile()
{
	const QString rootPath = QFileDialog::getExistingDirectory(this, tr("Add Game Installation"));
	if (rootPath.isEmpty()) {
		return;
	}

	QStringList gameLabels;
	QStringList gameKeys;
	for (const GameDefinition& definition : knownGameDefinitions()) {
		gameLabels << tr("%1 [%2]").arg(definition.displayName, definition.gameKey);
		gameKeys << definition.gameKey;
	}

	bool ok = false;
	const QString selectedGameLabel = QInputDialog::getItem(this, tr("Game"), tr("Game"), gameLabels, 0, false, &ok);
	if (!ok) {
		return;
	}
	const qsizetype selectedIndex = gameLabels.indexOf(selectedGameLabel);
	const int gameIndex = selectedIndex < 0 ? 0 : static_cast<int>(selectedIndex);
	const QString gameKey = gameKeys.value(gameIndex, QStringLiteral("custom"));

	const QString defaultName = defaultGameInstallationDisplayName(rootPath, gameKey);
	const QString displayName = QInputDialog::getText(this, tr("Installation Name"), tr("Name"), QLineEdit::Normal, defaultName, &ok).trimmed();
	if (!ok) {
		return;
	}

	GameInstallationProfile profile;
	profile.rootPath = rootPath;
	profile.gameKey = gameKey;
	profile.displayName = displayName.isEmpty() ? defaultName : displayName;
	profile.engineFamily = gameDefinitionForKey(gameKey).engineFamily;
	profile.readOnly = true;
	profile.manual = true;
	profile = normalizedGameInstallationProfile(profile);

	m_settings.upsertGameInstallation(profile);
	m_settings.sync();
	recordActivity(tr("Game Installation Added"), nativePath(profile.rootPath), tr("installation"), OperationState::Completed, tr("Manual profile saved: %1").arg(profile.displayName));
	refreshGameInstallations();
	refreshWorkspaceDashboard();
	refreshSetupPanel();
	refreshInspectorDrawerForSettings();
	statusBar()->showMessage(tr("Game installation saved: %1").arg(profile.displayName));
}

void ApplicationShell::detectGameInstallationProfiles()
{
	m_detectedInstallationCandidates = detectGameInstallations();
	if (m_detectedInstallationCandidates.isEmpty()) {
		recordActivity(tr("Detect Game Installations"), tr("Steam and GOG library scan"), tr("installation"), OperationState::Warning, tr("No installation candidates found."), {tr("Use Add Install to create a manual profile or try CLI detection with an explicit root.")});
	} else {
		QStringList warnings;
		for (const GameInstallationDetectionCandidate& candidate : m_detectedInstallationCandidates) {
			for (const QString& warning : candidate.warnings) {
				warnings << tr("%1: %2").arg(candidate.profile.displayName, warning);
			}
		}
		recordActivity(tr("Detect Game Installations"), tr("Steam and GOG library scan"), tr("installation"), OperationState::Completed, tr("Found %n confirmable installation candidates.", nullptr, m_detectedInstallationCandidates.size()), warnings);
	}
	refreshGameInstallations();
	refreshWorkspaceDashboard();
	refreshSetupPanel();
	refreshInspectorDrawerForSettings();
	statusBar()->showMessage(m_detectedInstallationCandidates.isEmpty()
			? tr("No game installation candidates found")
			: tr("Detected %n game installation candidates", nullptr, m_detectedInstallationCandidates.size()));
}

void ApplicationShell::importSelectedDetectedInstallation()
{
	const int index = selectedDetectedInstallationIndex();
	if (index < 0) {
		statusBar()->showMessage(tr("Select a detected installation candidate first"));
		return;
	}

	GameInstallationProfile profile = m_detectedInstallationCandidates[index].profile;
	profile.manual = false;
	profile.readOnly = true;
	profile = normalizedGameInstallationProfile(profile);
	m_settings.upsertGameInstallation(profile);
	m_settings.sync();
	m_detectedInstallationCandidates.removeAt(index);
	recordActivity(tr("Detected Installation Imported"), nativePath(profile.rootPath), tr("installation"), OperationState::Completed, tr("Saved read-only profile: %1").arg(profile.displayName));
	refreshGameInstallations();
	refreshWorkspaceDashboard();
	refreshSetupPanel();
	refreshInspectorDrawerForSettings();
	statusBar()->showMessage(tr("Detected installation imported: %1").arg(profile.displayName));
}

void ApplicationShell::selectCurrentGameInstallation()
{
	const QString id = selectedGameInstallationId();
	if (id.isEmpty()) {
		statusBar()->showMessage(selectedDetectedInstallationIndex() >= 0 ? tr("Import the detected candidate before selecting it") : tr("No game installation selected"));
		return;
	}
	m_settings.setSelectedGameInstallation(id);
	m_settings.sync();
	refreshGameInstallations();
	refreshWorkspaceDashboard();
	refreshInspectorDrawerForSettings();
	statusBar()->showMessage(tr("Selected game installation: %1").arg(id));
}

void ApplicationShell::removeSelectedGameInstallation()
{
	const QString id = selectedGameInstallationId();
	if (id.isEmpty()) {
		statusBar()->showMessage(selectedDetectedInstallationIndex() >= 0 ? tr("Detected candidates are not saved yet") : tr("No game installation selected"));
		return;
	}
	m_settings.removeGameInstallation(id);
	m_settings.sync();
	recordActivity(tr("Game Installation Removed"), id, tr("installation"), OperationState::Completed, tr("Manual profile removed."));
	refreshGameInstallations();
	refreshWorkspaceDashboard();
	refreshSetupPanel();
	refreshInspectorDrawerForSettings();
	statusBar()->showMessage(tr("Game installation removed: %1").arg(id));
}

void ApplicationShell::openProjectFolder()
{
	const QString path = QFileDialog::getExistingDirectory(this, tr("Open Project Folder"));
	if (path.isEmpty()) {
		return;
	}

	m_settings.recordRecentProject(path);
	m_settings.setCurrentProjectPath(path);
	m_settings.sync();
	recordActivity(tr("Open Project"), nativePath(normalizedProjectPath(path)), tr("project"), OperationState::Completed, tr("Project folder remembered."));
	refreshSetupPanel();
	refreshRecentProjects();
	refreshWorkspaceDashboard();
	applyPreferencesToUi();
	updateInspectorForProject(normalizedProjectPath(path));
	statusBar()->showMessage(tr("Project folder remembered: %1").arg(nativePath(normalizedProjectPath(path))));
}

void ApplicationShell::openPackageFile()
{
	const QString path = QFileDialog::getOpenFileName(
		this,
		tr("Open Package"),
		QString(),
		tr("Packages (*.pak *.wad *.wad2 *.wad3 *.zip *.pk3);;All Files (*)"));
	if (path.isEmpty()) {
		return;
	}
	loadPackagePath(path);
}

void ApplicationShell::openPackageFolder()
{
	const QString path = QFileDialog::getExistingDirectory(this, tr("Open Folder Package"));
	if (path.isEmpty()) {
		return;
	}
	loadPackagePath(path);
}

void ApplicationShell::loadPackagePath(const QString& path)
{
	const QString absolutePath = QFileInfo(path).absoluteFilePath();
	const AccessibilityPreferences preferences = m_settings.accessibilityPreferences();

	m_packageState->setReducedMotion(preferences.reducedMotion);
	m_packageState->setTitle(tr("Opening Package"));
	m_packageState->setDetail(nativePath(absolutePath));
	m_packageState->setState(OperationState::Loading, tr("Loading"));
	m_packageState->setProgress({0, 1});

	m_packageActivityId = m_activity.createTask(tr("Package Scan"), nativePath(absolutePath), tr("package"), OperationState::Loading, false);
	m_activity.setProgress(m_packageActivityId, 0, 1, tr("Opening package."));
	refreshActivityCenter(m_packageActivityId);

	PackageArchive loadedPackage;
	QString error;
	if (!loadedPackage.load(absolutePath, &error)) {
		m_activity.failTask(m_packageActivityId, error.isEmpty() ? tr("Package could not be opened.") : error);
		m_packageState->setTitle(tr("Package Open Failed"));
		m_packageState->setDetail(error.isEmpty() ? nativePath(absolutePath) : error);
		m_packageState->setState(OperationState::Failed, tr("Failed"));
		m_packageState->setProgress({0, 1});
		m_packageSummary->setText(tr("No package loaded"));
		m_packageEntries->clear();
		m_packageEntries->addItem(tr("Package open failed"));
		if (m_packageTree) {
			m_packageTree->clear();
			auto* treeItem = new QTreeWidgetItem(QStringList {tr("Package open failed")});
			treeItem->setFlags(Qt::NoItemFlags);
			m_packageTree->addTopLevelItem(treeItem);
		}
		m_packageDrawer->setTitle(tr("Package Details"));
		m_packageDrawer->setSubtitle(error);
		m_packageDrawer->setSections({
			{QStringLiteral("error"), tr("Error"), nativePath(absolutePath), error, OperationState::Failed},
		});
		m_packageStaging.clear();
		refreshPackageStagingSummary();
		persistActivityTask(m_packageActivityId);
		refreshActivityCenter(m_packageActivityId);
		refreshWorkspaceContextPanels();
		statusBar()->showMessage(tr("Package open failed: %1").arg(error));
		return;
	}

	const PackageArchiveSummary summary = loadedPackage.summary();
	m_activity.setProgress(m_packageActivityId, summary.entryCount, std::max(1, summary.entryCount), tr("Indexed %n package entries.", nullptr, summary.entryCount));
	for (const PackageLoadWarning& warning : loadedPackage.warnings()) {
		m_activity.appendWarning(m_packageActivityId, warning.virtualPath.isEmpty() ? warning.message : QStringLiteral("%1: %2").arg(warning.virtualPath, warning.message));
	}
	m_packageStaging.clear();
	QString stagingError;
	if (!m_packageStaging.loadBaseArchive(loadedPackage, &stagingError)) {
		m_activity.appendWarning(m_packageActivityId, tr("Package staging is blocked: %1").arg(stagingError));
	}
	m_activity.completeTask(m_packageActivityId, tr("Opened %1 with %n entries.", nullptr, summary.entryCount).arg(localizedPackageFormatName(summary.format)));
	persistActivityTask(m_packageActivityId);

	m_packageArchive = loadedPackage;
	// A different package starts at its root; reloading the same one keeps the
	// folder the user was in.
	if (m_packageBrowseSource != absolutePath) {
		m_packageBrowseSource = absolutePath;
		m_packageBrowseFolder.clear();
		m_packageFolderHistory = {QString()};
		m_packageFolderHistoryIndex = 0;
	}
	registerWatchedDocument(absolutePath, DocumentWatchRole::Package);
	refreshPackageBrowser();
	refreshWorkspaceDashboard();
	refreshActivityCenter(m_packageActivityId);
	statusBar()->showMessage(tr("Package opened: %1").arg(nativePath(absolutePath)));
}

void ApplicationShell::refreshPackageBrowser()
{
	if (!m_packageEntries || !m_packageTree || !m_packageState || !m_packageDrawer) {
		return;
	}

	const AccessibilityPreferences preferences = m_settings.accessibilityPreferences();
	m_packageState->setReducedMotion(preferences.reducedMotion);
	invalidatePaletteResolution();

	if (!m_packageArchive.isOpen()) {
		m_packageSummary->setText(tr("No package loaded"));
		m_packageState->setTitle(tr("No Package Loaded"));
		m_packageState->setDetail(tr("Open a folder, PAK, WAD, ZIP, or PK3 to browse entries read-only."));
		m_packageState->setState(OperationState::Idle, tr("Idle"));
		m_packageState->setProgress({});
		m_packageEntries->clear();
		auto* item = new QListWidgetItem(tr("No package loaded"));
		item->setFlags(Qt::NoItemFlags);
		m_packageEntries->addItem(item);
		m_packageTree->clear();
		auto* treeItem = new QTreeWidgetItem(QStringList {tr("No package loaded")});
		treeItem->setFlags(Qt::NoItemFlags);
		m_packageTree->addTopLevelItem(treeItem);
		m_packageDrawer->setTitle(tr("Package Entry Details"));
		m_packageDrawer->setSubtitle(tr("Open a package to inspect entry metadata."));
		m_packageDrawer->setSections({});
		m_packageStaging.clear();
		refreshPackageCompositionSummary();
		refreshPackageStagingSummary();
		refreshWorkspaceContextPanels();
		refreshStatusChips();
		refreshCommandEnablement();
		return;
	}

	const PackageArchiveSummary summary = m_packageArchive.summary();
	const OperationState state = summary.warningCount > 0 ? OperationState::Warning : OperationState::Completed;
	m_packageSummary->setText(tr("%1 / %n entries", nullptr, summary.entryCount).arg(localizedPackageFormatName(summary.format)));
	m_packageState->setTitle(tr("Package Browser"));
	m_packageState->setDetail(QStringLiteral("%1 / %2 files / %3 directories / %4 warnings")
		.arg(nativePath(summary.sourcePath))
		.arg(summary.fileCount)
		.arg(summary.directoryCount)
		.arg(summary.warningCount));
	m_packageState->setState(state, summary.warningCount > 0 ? tr("Warnings") : tr("Ready"));
	m_packageState->setProgress({summary.entryCount, std::max(1, summary.entryCount)});
	refreshPackageCompositionSummary();
	refreshPackageStagingSummary();
	refreshPackageTree();
	filterPackageEntries();
	refreshWorkspaceContextPanels();
	refreshModeAvailability();
	refreshStatusChips();
	refreshCommandEnablement();
}

void ApplicationShell::refreshWorkspaceContextPanels()
{
	refreshProjectProblemsPanel();
	refreshWorkspaceSearch();
	refreshChangedFilesPanel();
	refreshProjectDependencyGraph();
	refreshRecentActivityTimeline();
}

void ApplicationShell::refreshProjectProblemsPanel()
{
	if (!m_projectProblems) {
		return;
	}

	m_projectProblems->clear();
	const QString projectPath = m_settings.currentProjectPath();
	const CompilerRegistrySummary compilerRegistry = discoverCompilerTools(compilerRegistryOptionsForProject(projectPath, m_settings));
	const auto addProblem = [this](const QString& title, const QString& detail, OperationState state, const QString& filePath = QString(), const QString& virtualPath = QString()) {
		auto* item = new QListWidgetItem(QStringLiteral("%1  %2\n%3").arg(studioStateGlyph(state), title, detail));
		item->setToolTip(QStringLiteral("%1 (%2)\n%3").arg(title, localizedOperationStateName(state), detail));
		item->setData(Qt::AccessibleTextRole, QStringLiteral("%1, %2. %3").arg(title, localizedOperationStateName(state), detail));
		item->setData(Qt::UserRole, filePath);
		item->setData(Qt::UserRole + 1, virtualPath);
		item->setData(Qt::UserRole + 2, operationStateId(state));
		m_projectProblems->addItem(item);
	};

	if (projectPath.isEmpty()) {
		addProblem(tr("No project open"), tr("Open a project folder to activate project health, package roots, and next actions."), OperationState::Idle);
		addProblem(tr("No install selected"), tr("Add or detect a game installation profile when you are ready."), OperationState::Warning);
		addProblem(tr("No package mounted"), tr("Open a folder, PAK, WAD, ZIP, or PK3 to inspect package context."), OperationState::Idle);
		addProblem(tr("Compiler discovery"), tr("%1 of %2 executables found.").arg(compilerRegistry.executableAvailableCount).arg(compilerRegistry.tools.size()), compilerRegistry.overallState());
		return;
	}

	ProjectManifest manifest;
	QString error;
	const bool loaded = loadProjectManifest(projectPath, &manifest, &error);
	if (!loaded) {
		manifest = defaultProjectManifest(projectPath);
		addProblem(tr("Project manifest"), tr("Initialize .vibestudio/project.json: %1").arg(error), OperationState::Warning, projectManifestPath(projectPath));
	}

	const ProjectHealthSummary health = buildProjectHealthSummary(manifest, m_settings.selectedGameInstallationId());
	for (const ProjectHealthCheck& check : health.checks) {
		if (check.state == OperationState::Completed || check.state == OperationState::Idle) {
			continue;
		}
		QString filePath;
		if (check.id.contains(QStringLiteral("folder")) || check.id == QStringLiteral("manifest") || check.id == QStringLiteral("root")) {
			filePath = check.id == QStringLiteral("manifest") ? projectManifestPath(projectPath) : QFileInfo(check.detail).absoluteFilePath();
		}
		addProblem(check.title, check.detail, check.state, filePath, workspaceVirtualPath(projectPath, filePath));
	}

	if (!m_packageArchive.isOpen()) {
		addProblem(tr("No package mounted"), tr("Open a package so project files and mounted package entries can be searched together."), OperationState::Warning);
	}
	if (compilerRegistry.executableAvailableCount == 0) {
		addProblem(tr("No compiler executable found"), tr("Compiler sources may be present, but no runnable tool was discovered in known paths or PATH."), OperationState::Warning);
	}
	if (m_activity.tasks().isEmpty()) {
		addProblem(tr("No recent tasks"), tr("Open a project, package, setup step, or compiler report to populate the activity timeline."), OperationState::Idle);
	}
	if (m_projectProblems->count() == 0) {
		m_projectProblems->addItem(disabledListItem(tr("Project has no blocking problems. Warnings and raw detail remain available in Workspace Details.")));
	}
}

void ApplicationShell::refreshWorkspaceSearch()
{
	if (!m_workspaceSearchResults || !m_workspaceSearch) {
		return;
	}

	m_workspaceSearchResults->clear();
	const QString query = m_workspaceSearch->text().trimmed().toCaseFolded();
	const QString projectPath = m_settings.currentProjectPath();
	if (query.isEmpty()) {
		m_workspaceSearchResults->addItem(disabledListItem(tr("Type to search project files and mounted package entries.")));
		return;
	}

	int resultCount = 0;
	constexpr int kMaximumResults = 200;
	auto addResult = [this, &resultCount](const QString& label, const QString& detail, const QString& filePath, const QString& virtualPath, const QString& source) {
		auto* item = new QListWidgetItem(QStringLiteral("%1\n%2").arg(label, detail));
		item->setData(Qt::UserRole, filePath);
		item->setData(Qt::UserRole + 1, virtualPath);
		item->setData(Qt::UserRole + 2, source);
		m_workspaceSearchResults->addItem(item);
		++resultCount;
	};

	if (!projectPath.isEmpty() && QFileInfo(projectPath).isDir()) {
		QDirIterator iterator(projectPath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
		while (iterator.hasNext() && resultCount < kMaximumResults) {
			const QString absolutePath = iterator.next();
			const QString virtualPath = workspaceVirtualPath(projectPath, absolutePath);
			if (virtualPath.isEmpty() || virtualPath.startsWith(QStringLiteral(".git/")) || virtualPath.startsWith(QStringLiteral(".vibestudio/tmp/"))) {
				continue;
			}
			if (!virtualPath.toCaseFolded().contains(query)) {
				continue;
			}
			addResult(tr("Project: %1").arg(virtualPath), nativePath(absolutePath), absolutePath, virtualPath, QStringLiteral("project"));
		}
	}

	if (m_packageArchive.isOpen() && resultCount < kMaximumResults) {
		const PackageArchiveSummary summary = m_packageArchive.summary();
		for (const PackageEntry& entry : m_packageArchive.entries()) {
			if (resultCount >= kMaximumResults) {
				break;
			}
			const QString haystack = QStringLiteral("%1 %2 %3").arg(entry.virtualPath, entry.typeHint, entry.storageMethod).toCaseFolded();
			if (!haystack.contains(query)) {
				continue;
			}
			addResult(tr("Package: %1").arg(entry.virtualPath), tr("%1 / %2 / %3").arg(localizedPackageFormatName(summary.format), localizedPackageEntryKindName(entry.kind), byteSizeText(entry.sizeBytes)), summary.sourcePath, entry.virtualPath, QStringLiteral("package"));
		}
	}

	if (resultCount == 0) {
		m_workspaceSearchResults->addItem(disabledListItem(tr("No workspace search results.")));
	} else if (resultCount >= kMaximumResults) {
		m_workspaceSearchResults->addItem(disabledListItem(tr("Search result limit reached; narrow the query for more precise results.")));
	}
}

void ApplicationShell::refreshChangedFilesPanel()
{
	if (!m_changedFiles) {
		return;
	}

	m_changedFiles->clear();
	const QString projectPath = m_settings.currentProjectPath();
	if (projectPath.isEmpty()) {
		m_changedFiles->addItem(disabledListItem(tr("No project open.")));
		return;
	}

	const QString gitRoot = findGitRoot(projectPath);
	if (gitRoot.isEmpty()) {
		m_changedFiles->addItem(disabledListItem(tr("No Git repository detected for the active project.")));
		return;
	}

	// Git runs off the UI thread: a large repository can take seconds to report,
	// and the panel must never freeze the studio while it does.
	if (m_gitStatusProcess) {
		m_gitStatusProcess->disconnect(this);
		m_gitStatusProcess->kill();
		// Reaping the killed process first keeps QProcess from warning that it
		// was destroyed while running.
		m_gitStatusProcess->waitForFinished(500);
		m_gitStatusProcess->deleteLater();
		m_gitStatusProcess = nullptr;
	}
	m_changedFiles->addItem(disabledListItem(tr("Reading Git status...")));
	auto* git = new QProcess(this);
	m_gitStatusProcess = git;
	const int generation = ++m_gitStatusGeneration;
	auto finish = [this, git, generation, gitRoot](bool succeeded) {
		git->deleteLater();
		if (generation != m_gitStatusGeneration) {
			return;
		}
		m_gitStatusProcess = nullptr;
		if (!m_changedFiles) {
			return;
		}
		m_changedFiles->clear();
		if (!succeeded) {
			m_changedFiles->addItem(disabledListItem(tr("Git status is unavailable.")));
			return;
		}
		const QString output = QString::fromLocal8Bit(git->readAllStandardOutput());
		const QStringList lines = output.split('\n', Qt::SkipEmptyParts);
		if (lines.isEmpty()) {
			m_changedFiles->addItem(disabledListItem(tr("No changed or staged files.")));
			return;
		}
		for (const QString& line : lines) {
			const QString relativePath = statusPathFromGitLine(line);
			const QString absolutePath = QDir(gitRoot).absoluteFilePath(relativePath);
			auto* item = new QListWidgetItem(QStringLiteral("%1\n%2").arg(line.left(2).trimmed().isEmpty() ? tr("modified") : line.left(2), relativePath));
			item->setData(Qt::UserRole, absolutePath);
			item->setData(Qt::UserRole + 1, workspaceVirtualPath(gitRoot, absolutePath));
			item->setData(Qt::UserRole + 2, QStringLiteral("git"));
			m_changedFiles->addItem(item);
		}
	};
	connect(git, &QProcess::finished, this, [git, finish](int exitCode, QProcess::ExitStatus status) {
		Q_UNUSED(git);
		finish(status == QProcess::NormalExit && exitCode == 0);
	});
	connect(git, &QProcess::errorOccurred, this, [git, finish](QProcess::ProcessError error) {
		// A crash or timeout also emits finished(); only a failed start is
		// reported here, since nothing else will follow it.
		if (error == QProcess::FailedToStart) {
			Q_UNUSED(git);
			finish(false);
		}
	});
	QTimer::singleShot(15000, git, [git]() {
		if (git->state() != QProcess::NotRunning) {
			git->kill();
		}
	});
	git->start(QStringLiteral("git"), {QStringLiteral("-C"), gitRoot, QStringLiteral("status"), QStringLiteral("--short")});
}

void ApplicationShell::refreshProjectDependencyGraph()
{
	if (!m_dependencyGraph) {
		return;
	}

	m_dependencyGraph->clear();
	const QString projectPath = m_settings.currentProjectPath();
	if (projectPath.isEmpty()) {
		m_dependencyGraph->addItem(disabledListItem(tr("No project open. Dependency graph will connect project roots, installs, packages, and compilers.")));
		return;
	}

	ProjectManifest manifest;
	QString error;
	const bool loaded = loadProjectManifest(projectPath, &manifest, &error);
	if (!loaded) {
		manifest = defaultProjectManifest(projectPath);
	}
	const QString installId = effectiveProjectInstallationId(manifest, m_settings.selectedGameInstallationId());
	const CompilerRegistrySummary compilerRegistry = discoverCompilerTools(compilerRegistryOptionsForProject(projectPath, m_settings));
	auto addNode = [this](const QString& label, const QString& detail, const QString& filePath = QString(), const QString& virtualPath = QString()) {
		auto* item = new QListWidgetItem(QStringLiteral("%1\n%2").arg(label, detail));
		item->setData(Qt::UserRole, filePath);
		item->setData(Qt::UserRole + 1, virtualPath);
		m_dependencyGraph->addItem(item);
	};

	addNode(tr("Project > Manifest"), loaded ? manifest.projectId : tr("Manifest not initialized"), projectManifestPath(projectPath), workspaceVirtualPath(projectPath, projectManifestPath(projectPath)));
	for (const QString& folder : manifest.sourceFolders) {
		const QString path = QDir(projectPath).absoluteFilePath(folder);
		addNode(tr("Project > Source Folder"), folder, path, workspaceVirtualPath(projectPath, path));
	}
	if (manifest.packageFolders.isEmpty()) {
		addNode(tr("Project > Package Folders"), tr("No package folders configured."));
	} else {
		for (const QString& folder : manifest.packageFolders) {
			const QString path = QDir(projectPath).absoluteFilePath(folder);
			addNode(tr("Project > Package Folder"), folder, path, workspaceVirtualPath(projectPath, path));
		}
	}
	addNode(tr("Project > Installation"), installId.isEmpty() ? tr("No installation linked.") : installId);
	if (manifest.registeredOutputPaths.isEmpty()) {
		addNode(tr("Project > Compiler Outputs"), tr("No compiler outputs registered."));
	} else {
		for (const QString& outputPath : manifest.registeredOutputPaths) {
			const QString absoluteOutputPath = QFileInfo(outputPath).isAbsolute() ? outputPath : QDir(projectPath).absoluteFilePath(outputPath);
			addNode(tr("Project > Compiler Output"), outputPath, absoluteOutputPath, workspaceVirtualPath(projectPath, absoluteOutputPath));
		}
	}
	addNode(tr("Workspace > Mounted Package"), m_packageArchive.isOpen() ? nativePath(m_packageArchive.summary().sourcePath) : tr("No package mounted."), m_packageArchive.isOpen() ? m_packageArchive.summary().sourcePath : QString());
	addNode(tr("Workspace > Compiler Registry"), tr("%1 of %2 executables discovered.").arg(compilerRegistry.executableAvailableCount).arg(compilerRegistry.tools.size()));
}

void ApplicationShell::refreshRecentActivityTimeline()
{
	if (!m_recentActivityTimeline) {
		return;
	}

	m_recentActivityTimeline->clear();
	struct ActivityTimelineEntry {
		QString id;
		QString title;
		QString detail;
		QString source;
		OperationState state = OperationState::Idle;
		QString resultSummary;
		QDateTime updatedUtc;
		qint64 durationMs = 0;
	};
	QVector<ActivityTimelineEntry> tasks;
	QStringList liveTaskIds;
	for (const OperationTask& task : m_activity.tasks()) {
		tasks.push_back({task.id, task.title, task.detail, task.source, task.state, task.resultSummary, task.updatedUtc, task.durationMs});
		liveTaskIds.push_back(task.id);
	}
	for (const RecentActivityTask& task : m_settings.recentActivityTasks()) {
		if (liveTaskIds.contains(task.id)) {
			continue;
		}
		tasks.push_back({task.id, task.title, task.detail, task.source, task.state, task.resultSummary, task.updatedUtc, task.durationMs});
	}
	std::sort(tasks.begin(), tasks.end(), [](const ActivityTimelineEntry& left, const ActivityTimelineEntry& right) {
		return left.updatedUtc > right.updatedUtc;
	});
	if (tasks.isEmpty()) {
		m_recentActivityTimeline->addItem(disabledListItem(tr("No recent activity yet.")));
		return;
	}

	const int count = std::min(10, static_cast<int>(tasks.size()));
	for (int index = 0; index < count; ++index) {
		const ActivityTimelineEntry& task = tasks[index];
		const QString timestamp = QLocale::system().toString(task.updatedUtc.toLocalTime(), QLocale::ShortFormat);
		const QString detail = task.resultSummary.isEmpty() ? task.detail : task.resultSummary;
		auto* item = new QListWidgetItem(QStringLiteral("%1  %2\n%3").arg(studioStateGlyph(task.state), task.title, detail.isEmpty() ? timestamp : timestamp + QStringLiteral("  %1  ").arg(QChar(0x00b7)) + detail));
		item->setToolTip(QStringLiteral("%1 (%2)\n%3\n%4").arg(task.title, localizedOperationStateName(task.state), timestamp, detail));
		item->setData(Qt::AccessibleTextRole, QStringLiteral("%1, %2, %3. %4").arg(task.title, localizedOperationStateName(task.state), timestamp, detail));
		item->setData(Qt::UserRole, task.detail);
		item->setData(Qt::UserRole + 1, QString());
		item->setData(Qt::UserRole + 2, task.id);
		m_recentActivityTimeline->addItem(item);
	}

	if (m_activityTimelineChart) {
		QVector<TimelineEvent> events;
		for (int index = 0; index < count; ++index) {
			const ActivityTimelineEntry& task = tasks[index];
			TimelineEvent event;
			event.id = task.id;
			event.label = task.title;
			event.detail = task.resultSummary.isEmpty() ? task.detail : task.resultSummary;
			event.source = task.source;
			event.state = task.state;
			event.startedMsSinceEpoch = task.updatedUtc.toMSecsSinceEpoch();
			event.durationMs = task.durationMs;
			events.push_back(event);
		}
		m_activityTimelineChart->setEvents(events);
	}
}

void ApplicationShell::openLevelMapFile()
{
	const QString path = QFileDialog::getOpenFileName(
		this,
		tr("Open Level Map"),
		m_settings.currentProjectPath().isEmpty() ? QDir::homePath() : m_settings.currentProjectPath(),
		tr("Level maps (*.map *.wad);;Quake-family maps (*.map);;Doom WAD maps (*.wad);;All files (*.*)"));
	if (path.isEmpty()) {
		return;
	}
	if (m_levelMapPath) {
		m_levelMapPath->setText(path);
	}
	loadLevelMapPath(path);
}

void ApplicationShell::loadLevelMapPath(const QString& path)
{
	if (!m_levelMapState) {
		return;
	}
	const QString trimmedPath = path.trimmed();
	if (trimmedPath.isEmpty()) {
		m_levelMapDocument = {};
		m_levelMapState->setState(OperationState::Failed, tr("Path required"));
		m_levelMapState->setDetail(tr("Choose a Doom WAD map or Quake-family .map source."));
		refreshLevelMapWorkbench();
		return;
	}

	m_levelMapState->setState(OperationState::Loading, tr("Parsing"));
	m_levelMapState->setDetail(tr("Reading %1").arg(nativePath(trimmedPath)));
	m_levelMapState->setProgress({0, 4});

	LevelMapLoadRequest request;
	request.path = trimmedPath;
	request.mapName = m_levelMapName ? m_levelMapName->text().trimmed() : QString();
	request.engineHint = m_levelMapEngine ? m_levelMapEngine->currentData().toString() : QString();
	QString error;
	LevelMapDocument document;
	if (!loadLevelMap(request, &document, &error)) {
		m_levelMapDocument = {};
		m_levelMapState->setState(OperationState::Failed, tr("Load failed"));
		m_levelMapState->setDetail(error);
		recordActivity(tr("Level map load failed"), nativePath(trimmedPath), QStringLiteral("level-map"), OperationState::Failed, error);
		refreshLevelMapWorkbench();
		return;
	}

	m_levelMapDocument = document;
	if (m_levelMapPath) {
		m_levelMapPath->setText(document.sourcePath);
	}
	if (m_levelMapName && !document.mapName.isEmpty() && document.format == LevelMapFormat::DoomWad) {
		m_levelMapName->setText(document.mapName);
	}
	const QString defaultProfile = compilerRequestForLevelMap(document, QString()).profileId;
	if (m_levelMapCompilerProfile) {
		const int index = m_levelMapCompilerProfile->findData(defaultProfile);
		if (index >= 0) {
			m_levelMapCompilerProfile->setCurrentIndex(index);
		}
	}
	registerWatchedDocument(document.sourcePath, DocumentWatchRole::LevelMap, document.mapName);
	recordActivity(tr("Level map inspected"), nativePath(document.sourcePath), QStringLiteral("level-map"), document.issues.isEmpty() ? OperationState::Completed : OperationState::Warning, tr("%1 issues").arg(document.issues.size()));
	refreshLevelMapWorkbench();
}

void ApplicationShell::refreshLevelMapWorkbench()
{
	refreshLevelMapViewport();
	refreshEntityInspector();
	refreshCommandEnablement();
	if (!m_levelMapState || !m_levelMapObjects || !m_levelMapStatistics || !m_levelMapView || !m_levelMapValidation || !m_levelMapDrawer) {
		return;
	}

	// The document is the source of truth for the duration of a refresh. The
	// list only drives the document from the user's own itemSelectionChanged.
	const bool wasSyncing = m_syncingLevelMapSelection;
	m_syncingLevelMapSelection = true;
	const QSignalBlocker objectsBlocker(m_levelMapObjects);

	m_levelMapObjects->clear();
	m_levelMapStatistics->clear();
	m_levelMapView->clear();
	m_levelMapValidation->clear();

	const bool hasMap = !m_levelMapDocument.sourcePath.trimmed().isEmpty();
	if (!hasMap) {
		m_levelMapState->setTitle(tr("Level Map"));
		m_levelMapState->setDetail(tr("Open a Doom WAD map or Quake-family .map source to inspect entities, textures, health, and safe edits."));
		m_levelMapState->setState(OperationState::Idle, tr("Idle"));
		m_levelMapState->setProgress({0, 1});
		m_levelMapObjects->addItem(disabledListItem(tr("No level map loaded.")));
		m_levelMapStatistics->addItem(disabledListItem(tr("Map statistics will appear after inspection.")));
		m_levelMapView->addItem(disabledListItem(tr("2D or orthographic preview lines will appear after inspection.")));
		m_levelMapValidation->addItem(disabledListItem(tr("Map validation and health overlay will appear after inspection.")));
		m_levelMapDrawer->setTitle(tr("Level Map Details"));
		m_levelMapDrawer->setSubtitle(tr("Open a map to inspect statistics, properties, textures, validation, and undo history."));
		m_levelMapDrawer->setSections({});
	} else {
		const LevelMapStatistics stats = levelMapStatistics(m_levelMapDocument);
		const OperationState state = stats.errorCount > 0 ? OperationState::Failed : (stats.warningCount > 0 ? OperationState::Warning : OperationState::Completed);
		m_levelMapState->setTitle(tr("Level Map"));
		m_levelMapState->setDetail(tr("%1 / %2 / %3 objects / %4 unique textures")
			.arg(nativePath(m_levelMapDocument.sourcePath), levelMapFormatDisplayName(m_levelMapDocument.format))
			.arg(stats.entityCount + stats.brushCount + stats.doomVertexCount + stats.doomLinedefCount)
			.arg(stats.uniqueTextureCount));
		m_levelMapState->setState(state, stats.errorCount > 0 ? tr("Errors") : (stats.warningCount > 0 ? tr("Warnings") : tr("Ready")));
		m_levelMapState->setProgress({stats.entityCount + stats.brushCount + stats.doomLinedefCount, std::max(1, stats.entityCount + stats.brushCount + stats.doomLinedefCount)});

		const QString selectedSelector = m_levelMapDocument.selectionKind == LevelMapSelectionKind::None
			? QString()
			: QStringLiteral("%1:%2").arg(levelMapSelectionKindId(m_levelMapDocument.selectionKind)).arg(m_levelMapDocument.selectedObjectId);
		QSet<QString> selectedSelectors;
		selectedSelectors.reserve(m_levelMapDocument.selection.size());
		for (const LevelMapSelectionRef& ref : m_levelMapDocument.selection) {
			selectedSelectors.insert(QStringLiteral("%1:%2").arg(levelMapSelectionKindId(ref.kind)).arg(ref.objectId));
		}
		auto addObject = [this, &selectedSelector, &selectedSelectors](const QString& selector, const QString& label, const QString& detail) {
			auto* item = new QListWidgetItem(QStringLiteral("%1\n%2").arg(label, detail));
			item->setData(Qt::UserRole, selector);
			m_levelMapObjects->addItem(item);
			if (selectedSelectors.contains(selector)) {
				item->setSelected(true);
			}
			if (!selectedSelector.isEmpty() && selector == selectedSelector) {
				// NoUpdate keeps the current item from clearing the rest of the
				// set that was just restored above.
				m_levelMapObjects->setCurrentItem(item, QItemSelectionModel::NoUpdate);
				item->setSelected(true);
			}
		};
		for (const LevelMapEntity& entity : m_levelMapDocument.entities) {
			addObject(QStringLiteral("entity:%1").arg(entity.id), tr("Entity %1: %2").arg(entity.id).arg(entity.className), tr("Origin %1 / %2 keys").arg(entity.origin.valid ? QStringLiteral("%1,%2,%3").arg(entity.origin.x, 0, 'f', 0).arg(entity.origin.y, 0, 'f', 0).arg(entity.origin.z, 0, 'f', 0) : tr("unknown")).arg(entity.properties.size()));
		}
		for (const LevelMapBrush& brush : m_levelMapDocument.brushes) {
			addObject(QStringLiteral("brush:%1").arg(brush.id), tr("Brush %1").arg(brush.id), tr("Entity %1 / %2 faces").arg(brush.entityId).arg(brush.faceCount));
		}
		for (const LevelMapDoomVertex& vertex : m_levelMapDocument.doomVertices) {
			addObject(QStringLiteral("vertex:%1").arg(vertex.id), tr("Vertex %1").arg(vertex.id), tr("%1, %2").arg(vertex.x, 0, 'f', 0).arg(vertex.y, 0, 'f', 0));
		}
		for (const LevelMapDoomLinedef& linedef : m_levelMapDocument.doomLinedefs) {
			addObject(QStringLiteral("linedef:%1").arg(linedef.id), tr("Linedef %1").arg(linedef.id), tr("%1 > %2 / tag %3").arg(linedef.startVertex).arg(linedef.endVertex).arg(linedef.tag));
		}
		for (const LevelMapDoomThing& thing : m_levelMapDocument.doomThings) {
			addObject(QStringLiteral("thing:%1").arg(thing.id), tr("Thing %1").arg(thing.id), tr("Type %1 at %2, %3").arg(thing.type).arg(thing.x, 0, 'f', 0).arg(thing.y, 0, 'f', 0));
		}
		if (m_levelMapObjects->count() == 0) {
			m_levelMapObjects->addItem(disabledListItem(tr("No objects parsed from the loaded map.")));
		}
		for (const QString& line : levelMapStatisticsLines(m_levelMapDocument)) {
			m_levelMapStatistics->addItem(line);
		}
		for (const QString& line : levelMapViewLines(m_levelMapDocument)) {
			m_levelMapView->addItem(line);
		}
		for (const QString& line : levelMapValidationLines(m_levelMapDocument)) {
			m_levelMapValidation->addItem(line);
		}

		// The textures a map asks for only mean something next to the package that
		// is supposed to provide them, so the audit runs whenever one is open.
		// Sizes are not decoded here: this is the list view, and a decode would
		// read every unique texture entry on every refresh.
		if (m_packageArchive.isOpen()) {
			const MapTextureAudit audit = auditLevelMapTextures(m_levelMapDocument, m_packageArchive, false);
			if (audit.missingCount > 0) {
				auto* header = new QListWidgetItem(tr("MISSING TEXTURES [%1]\n%n name(s) referenced by this map are not in %2.",
					nullptr, audit.missingCount)
					.arg(localizedOperationStateName(OperationState::Warning),
						QFileInfo(m_packageArchive.sourcePath()).fileName()));
				header->setData(Qt::UserRole + 2, operationStateId(OperationState::Warning));
				m_levelMapValidation->addItem(header);
				int shown = 0;
				for (const MapTextureReference& reference : audit.references) {
					if (!reference.isMissing()) {
						continue;
					}
					if (shown++ >= 40) {
						m_levelMapValidation->addItem(disabledListItem(
							tr("%n further missing texture(s) not listed.", nullptr, audit.missingCount - shown + 1)));
						break;
					}
					auto* item = new QListWidgetItem(tr("  %1 — %n use(s)", nullptr, reference.useCount).arg(reference.textureName));
					item->setToolTip(tr("Searched: %1").arg(reference.candidatePaths.join(QStringLiteral(", "))));
					item->setData(Qt::UserRole + 2, operationStateId(OperationState::Warning));
					m_levelMapValidation->addItem(item);
				}
			} else if (audit.uniqueCount > 0) {
				m_levelMapValidation->addItem(tr("TEXTURES [%1]\nAll %n referenced name(s) resolve against %2.",
					nullptr, audit.uniqueCount)
					.arg(localizedOperationStateName(OperationState::Completed),
						QFileInfo(m_packageArchive.sourcePath()).fileName()));
			}
		} else if (!m_levelMapDocument.textureReferences.isEmpty()) {
			m_levelMapValidation->addItem(disabledListItem(
				tr("Open the package that provides this map's textures to check for missing ones.")));
		}

		// Entity problems sit beside texture problems: both are things the map
		// asks for that its surroundings may not provide.
		if (!m_entityDefinitions.isEmpty()) {
			m_entityValidation = validateLevelMapEntities(m_levelMapDocument, m_entityDefinitions);
			if (m_entityValidation.issueCount > 0) {
				auto* header = new QListWidgetItem(tr("ENTITIES [%1]\n%n issue(s) against %2 definition(s).",
					nullptr, m_entityValidation.issueCount)
					.arg(localizedOperationStateName(m_entityValidation.state()))
					.arg(m_entityDefinitions.classes.size()));
				header->setData(Qt::UserRole + 2, operationStateId(m_entityValidation.state()));
				m_levelMapValidation->addItem(header);
				int shown = 0;
				for (const EntityValidationIssue& issue : m_entityValidation.issues) {
					if (shown++ >= 40) {
						m_levelMapValidation->addItem(disabledListItem(
							tr("%n further entity issue(s) not listed.", nullptr, m_entityValidation.issueCount - shown + 1)));
						break;
					}
					const OperationState state = issue.severity == EntityIssueSeverity::Error
						? OperationState::Failed
						: (issue.severity == EntityIssueSeverity::Warning ? OperationState::Warning : OperationState::Idle);
					auto* item = new QListWidgetItem(tr("  %1 %2").arg(issue.code, issue.message));
					if (issue.entityId >= 0) {
						item->setData(Qt::UserRole, QStringLiteral("entity:%1").arg(issue.entityId));
					}
					item->setData(Qt::UserRole + 2, operationStateId(state));
					m_levelMapValidation->addItem(item);
				}
			} else if (m_entityValidation.entityCount > 0) {
				m_levelMapValidation->addItem(tr("ENTITIES [%1]\nAll %n entit(y)(ies) match the loaded definitions.",
					nullptr, m_entityValidation.entityCount)
					.arg(localizedOperationStateName(OperationState::Completed)));
			}
		} else if (!m_levelMapDocument.entities.isEmpty()) {
			m_levelMapValidation->addItem(disabledListItem(
				tr("Load entity definitions to check classnames, keys, spawnflags, and target references.")));
		}
	}

	const bool canEdit = hasMap && m_levelMapDocument.selectionKind == LevelMapSelectionKind::Entity;
	const bool canMove = hasMap && m_levelMapDocument.selectionKind != LevelMapSelectionKind::None;
	if (m_levelMapEditProperty) {
		m_levelMapEditProperty->setEnabled(canEdit);
	}
	if (m_levelMapMoveSelection) {
		m_levelMapMoveSelection->setEnabled(canMove);
	}
	if (m_levelMapSaveAs) {
		m_levelMapSaveAs->setEnabled(hasMap);
	}
	if (m_levelMapPlanCompile) {
		m_levelMapPlanCompile->setEnabled(hasMap);
	}
	if (m_levelMapCopyCli) {
		m_levelMapCopyCli->setEnabled(hasMap);
	}
	refreshLevelMapSelection();
	m_syncingLevelMapSelection = wasSyncing;
}

void ApplicationShell::refreshLevelMapSelection()
{
	if (!m_levelMapDrawer) {
		return;
	}
	if (!m_syncingLevelMapSelection) {
		// The list is the source of truth for this direction: replace the whole
		// set so deselecting in the list clears it in the document and in the
		// viewport, instead of leaving a stale primary behind.
		QVector<LevelMapSelectionRef> selection;
		const QList<QListWidgetItem*> chosen = m_levelMapObjects ? m_levelMapObjects->selectedItems() : QList<QListWidgetItem*>();
		for (const QListWidgetItem* item : chosen) {
			const QString selector = item->data(Qt::UserRole).toString();
			const int separator = selector.indexOf(QLatin1Char(':'));
			if (separator <= 0) {
				continue;
			}
			LevelMapSelectionRef ref;
			ref.kind = levelMapSelectionKindFromId(selector.left(separator));
			bool parsed = false;
			ref.objectId = selector.mid(separator + 1).toInt(&parsed);
			if (!parsed || ref.kind == LevelMapSelectionKind::None) {
				continue;
			}
			// The current item goes last so it stays the primary member, which
			// is what the property drawer and the entity inspector read.
			if (item == m_levelMapObjects->currentItem()) {
				continue;
			}
			selection.append(ref);
		}
		const QString primary = selectedLevelMapObjectSelector();
		const int primarySeparator = primary.indexOf(QLatin1Char(':'));
		if (primarySeparator > 0 && m_levelMapObjects && m_levelMapObjects->currentItem()
			&& m_levelMapObjects->currentItem()->isSelected()) {
			LevelMapSelectionRef ref;
			ref.kind = levelMapSelectionKindFromId(primary.left(primarySeparator));
			bool parsed = false;
			ref.objectId = primary.mid(primarySeparator + 1).toInt(&parsed);
			if (parsed && ref.kind != LevelMapSelectionKind::None) {
				selection.append(ref);
			}
		}
		QString error;
		setLevelMapSelection(&m_levelMapDocument, selection, &error);
		if (m_levelMapViewport) {
			const QSignalBlocker viewportBlocker(m_levelMapViewport);
			m_levelMapViewport->setSelectionSet(m_levelMapDocument.selection);
		}
	}
	const bool hasMap = !m_levelMapDocument.sourcePath.trimmed().isEmpty();
	const bool canEdit = hasMap && m_levelMapDocument.selectionKind == LevelMapSelectionKind::Entity;
	const bool canMove = hasMap && m_levelMapDocument.selectionKind != LevelMapSelectionKind::None;
	if (m_levelMapEditProperty) {
		m_levelMapEditProperty->setEnabled(canEdit);
	}
	if (m_levelMapMoveSelection) {
		m_levelMapMoveSelection->setEnabled(canMove);
	}
	if (!hasMap) {
		refreshEntityInspector();
		return;
	}

	m_levelMapDrawer->setTitle(tr("Level Map Details"));
	m_levelMapDrawer->setSubtitle(tr("%1 / %2").arg(m_levelMapDocument.mapName, levelMapFormatDisplayName(m_levelMapDocument.format)));
	m_levelMapDrawer->setSections({
		{QStringLiteral("properties"), tr("Properties"), tr("Selected object properties"), levelMapPropertyLines(m_levelMapDocument).join('\n'), OperationState::Completed},
		{QStringLiteral("statistics"), tr("Statistics"), tr("Map statistics summary"), levelMapStatisticsLines(m_levelMapDocument).join('\n'), OperationState::Completed},
		{QStringLiteral("textures"), tr("Textures"), tr("Texture and material references"), levelMapTextureLines(m_levelMapDocument).join('\n'), OperationState::Completed},
		{QStringLiteral("validation"), tr("Health"), tr("Validation and compiler preflight warnings"), levelMapValidationLines(m_levelMapDocument).join('\n'), levelMapStatistics(m_levelMapDocument).errorCount > 0 ? OperationState::Failed : (levelMapStatistics(m_levelMapDocument).warningCount > 0 ? OperationState::Warning : OperationState::Completed)},
		{QStringLiteral("preview"), tr("Preview"), tr("2D or orthographic preview lines"), levelMapViewLines(m_levelMapDocument).join('\n'), OperationState::Completed},
		{QStringLiteral("undo"), tr("Undo"), tr("Edit state and undo history"), levelMapUndoLines(m_levelMapDocument).join('\n'), m_levelMapDocument.undoStack.isEmpty() ? OperationState::Idle : OperationState::Warning},
	});
	m_levelMapDrawer->showSection(QStringLiteral("properties"));
	// The inspector follows the primary selection whichever surface set it.
	refreshEntityInspector();
}

QString ApplicationShell::selectedLevelMapObjectSelector() const
{
	const QListWidgetItem* item = m_levelMapObjects ? m_levelMapObjects->currentItem() : nullptr;
	return item ? item->data(Qt::UserRole).toString() : QString();
}

void ApplicationShell::editSelectedLevelMapProperty()
{
	if (m_levelMapDocument.sourcePath.trimmed().isEmpty() || m_levelMapDocument.selectionKind != LevelMapSelectionKind::Entity) {
		statusBar()->showMessage(tr("Select an entity before editing a key."));
		return;
	}
	// Start from the property row the user picked in the inspector, if any.
	QString presetKey = QStringLiteral("targetname");
	QString presetValue;
	if (const QTreeWidgetItem* row = m_entityInspector ? m_entityInspector->currentItem() : nullptr) {
		if (!row->data(0, Qt::UserRole).toString().isEmpty()) {
			presetKey = row->data(0, Qt::UserRole).toString();
			presetValue = row->data(1, Qt::UserRole).toString();
		}
	}
	bool ok = false;
	const QString key = QInputDialog::getText(this, tr("Edit Entity Key"), tr("Key"), QLineEdit::Normal, presetKey, &ok).trimmed();
	if (!ok || key.isEmpty()) {
		return;
	}
	const QString value = QInputDialog::getText(this, tr("Edit Entity Value"), tr("Value"), QLineEdit::Normal, key == presetKey ? presetValue : QString(), &ok);
	if (!ok) {
		return;
	}
	QString error;
	if (!setLevelMapEntityProperty(&m_levelMapDocument, m_levelMapDocument.selectedObjectId, key, value, &error)) {
		statusBar()->showMessage(tr("Map edit failed: %1").arg(error));
		return;
	}
	recordActivity(tr("Level map entity edited"), key, QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Entity key edited; use Save As to write a non-destructive copy."));
}

void ApplicationShell::moveSelectedLevelMapObject()
{
	const QString selector = selectedLevelMapObjectSelector();
	if (m_levelMapDocument.sourcePath.trimmed().isEmpty() || selector.isEmpty()) {
		statusBar()->showMessage(tr("Select a map object before moving it."));
		return;
	}
	bool ok = false;
	const QString deltaText = QInputDialog::getText(this, tr("Move Map Object"), tr("Delta x,y,z"), QLineEdit::Normal, QStringLiteral("16,0,0"), &ok);
	if (!ok || deltaText.trimmed().isEmpty()) {
		return;
	}
	const QStringList parts = deltaText.split(QRegularExpression(QStringLiteral(R"([,\s]+)")), Qt::SkipEmptyParts);
	if (parts.size() < 2 || parts.size() > 3) {
		statusBar()->showMessage(tr("Move delta must be x,y or x,y,z."));
		return;
	}
	bool okX = false;
	bool okY = false;
	bool okZ = true;
	const double dx = parts.value(0).toDouble(&okX);
	const double dy = parts.value(1).toDouble(&okY);
	const double dz = parts.size() == 3 ? parts.value(2).toDouble(&okZ) : 0.0;
	if (!okX || !okY || !okZ) {
		statusBar()->showMessage(tr("Move delta contains a non-numeric value."));
		return;
	}
	const QStringList selectorParts = selector.split(':', Qt::SkipEmptyParts);
	QString error;
	if (selectorParts.size() != 2 || !moveLevelMapObject(&m_levelMapDocument, selectorParts.value(0), selectorParts.value(1).toInt(), dx, dy, dz, &error)) {
		statusBar()->showMessage(tr("Map move failed: %1").arg(error));
		return;
	}
	recordActivity(tr("Level map object moved"), selector, QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Map object moved; use Save As to write a non-destructive copy."));
}

void ApplicationShell::saveLevelMapAsFromUi()
{
	if (m_levelMapDocument.sourcePath.trimmed().isEmpty()) {
		statusBar()->showMessage(tr("Open a level map before saving."));
		return;
	}
	const QString suffix = m_levelMapDocument.format == LevelMapFormat::DoomWad ? QStringLiteral("wad") : QStringLiteral("map");
	const QString suggested = QDir(QFileInfo(m_levelMapDocument.sourcePath).absolutePath()).filePath(QStringLiteral("%1-edited.%2").arg(QFileInfo(m_levelMapDocument.sourcePath).completeBaseName(), suffix));
	const QString outputPath = QFileDialog::getSaveFileName(this, tr("Save Level Map As"), suggested, tr("Level maps (*.map *.wad);;All files (*.*)"));
	if (outputPath.isEmpty()) {
		return;
	}
	const LevelMapSaveReport report = saveLevelMapAs(m_levelMapDocument, outputPath, false, false);
	if (!report.succeeded()) {
		statusBar()->showMessage(tr("Map save-as failed: %1").arg(report.errors.join(QStringLiteral("; "))));
		recordActivity(tr("Level map save failed"), nativePath(outputPath), QStringLiteral("level-map"), OperationState::Failed, report.errors.join(QStringLiteral("; ")));
		return;
	}
	m_levelMapDocument.outputPath = report.outputPath;
	// markLevelMapSaved() also records the undo depth the save happened at, which
	// is what lets a later undo/redo tell "back at the saved state" from
	// "modified". Assigning editState directly leaves that depth stale.
	markLevelMapSaved(&m_levelMapDocument);
	recordActivity(tr("Level map saved"), nativePath(outputPath), QStringLiteral("level-map"), OperationState::Completed, tr("Saved non-destructive map copy"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Level map saved: %1").arg(nativePath(outputPath)));
}

void ApplicationShell::planLevelMapCompile()
{
	if (m_compilerRunThread) {
		statusBar()->showMessage(tr("A compiler task is already running"));
		return;
	}
	if (m_levelMapDocument.sourcePath.trimmed().isEmpty()) {
		statusBar()->showMessage(tr("Open a level map before planning a compile."));
		return;
	}
	const QString profileId = m_levelMapCompilerProfile ? m_levelMapCompilerProfile->currentData().toString() : QString();
	CompilerCommandRequest request = compilerRequestForLevelMap(m_levelMapDocument, profileId);
	request.workspaceRootPath = m_settings.currentProjectPath();
	ProjectManifest manifest;
	if (!request.workspaceRootPath.trimmed().isEmpty() && loadProjectManifest(request.workspaceRootPath, &manifest)) {
		request.extraSearchPaths = effectiveProjectCompilerSearchPaths(manifest, request.extraSearchPaths);
		request.executableOverrides = effectiveProjectCompilerToolOverrides(manifest, m_settings.compilerToolPathOverrides());
	} else {
		request.executableOverrides = m_settings.compilerToolPathOverrides();
	}
	const CompilerCommandPlan plan = buildCompilerCommandPlan(request);
	m_levelMapDrawer->setSections({
		{QStringLiteral("compile-plan"), tr("Compile Plan"), tr("Reviewable compiler command"), compilerCommandPlanText(plan), plan.state()},
		{QStringLiteral("map-health"), tr("Map Health"), tr("Validation before compile"), levelMapValidationLines(m_levelMapDocument).join('\n'), levelMapStatistics(m_levelMapDocument).errorCount > 0 ? OperationState::Failed : OperationState::Warning},
	});
	m_levelMapDrawer->showSection(QStringLiteral("compile-plan"));
	if (!plan.isRunnable()) {
		recordActivity(tr("Level compile plan"), request.profileId, QStringLiteral("level-map"), plan.state(), tr("Review required"), plan.warnings);
		statusBar()->showMessage(tr("Compiler plan needs review."));
		return;
	}

	CompilerRunRequest runRequest;
	runRequest.command = request;
	runRequest.registerOutputs = true;
	const QString manifestRoot = request.workspaceRootPath.trimmed().isEmpty() ? QFileInfo(request.inputPath).absolutePath() : request.workspaceRootPath;
	runRequest.manifestPath = QDir(manifestRoot).filePath(QStringLiteral(".vibestudio/compiler-runs/%1-%2.json").arg(request.profileId, QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddTHHmmssZ"))));

	m_compilerRunCancelRequested.store(false);
	m_compilerRunActivityId = m_activity.createTask(tr("Level Compile"), request.profileId, tr("level-map"), OperationState::Running, true);
	m_activity.setProgress(m_compilerRunActivityId, 0, 4, tr("Planning compiler command."));
	refreshActivityCenter(m_compilerRunActivityId);
	statusBar()->showMessage(tr("Level compiler run started: %1").arg(request.profileId));

	const QString taskId = m_compilerRunActivityId;
	const QString projectPath = request.workspaceRootPath;
	auto* thread = QThread::create([this, runRequest, taskId, projectPath]() {
		CompilerRunCallbacks callbacks;
		callbacks.cancellationRequested = [this]() {
			return m_compilerRunCancelRequested.load();
		};
		callbacks.logEntry = [this, taskId](const CompilerTaskLogEntry& entry) {
			QMetaObject::invokeMethod(this, [this, taskId, entry]() {
				m_activity.appendLog(taskId, OperationState::Running, entry.message);
				refreshActivityCenter(taskId);
			}, Qt::QueuedConnection);
		};
		CompilerRunResult result = runCompilerCommand(runRequest, callbacks);
		QMetaObject::invokeMethod(this, [this, result, taskId, projectPath]() {
			finishCompilerRun(result, taskId, projectPath);
		}, Qt::QueuedConnection);
	});
	m_compilerRunThread = thread;
	connect(thread, &QThread::finished, thread, &QObject::deleteLater);
	thread->start();
}

void ApplicationShell::copyLevelMapCliEquivalent()
{
	if (m_levelMapDocument.sourcePath.trimmed().isEmpty()) {
		statusBar()->showMessage(tr("Open a level map before copying a CLI command."));
		return;
	}
	QStringList parts = {
		QStringLiteral("vibestudio"),
		QStringLiteral("--cli"),
		QStringLiteral("map"),
		QStringLiteral("compile-plan"),
		m_levelMapDocument.outputPath.trimmed().isEmpty() ? m_levelMapDocument.sourcePath : m_levelMapDocument.outputPath,
	};
	if (m_levelMapDocument.format == LevelMapFormat::DoomWad && !m_levelMapDocument.mapName.isEmpty()) {
		parts << QStringLiteral("--map") << m_levelMapDocument.mapName;
	}
	if (m_levelMapCompilerProfile && !m_levelMapCompilerProfile->currentData().toString().isEmpty()) {
		parts << QStringLiteral("--profile") << m_levelMapCompilerProfile->currentData().toString();
	}
	for (QString& part : parts) {
		part = quoteCliPart(part);
	}
	QGuiApplication::clipboard()->setText(parts.join(' '));
	statusBar()->showMessage(tr("Level map CLI command copied."));
}

void ApplicationShell::refreshAdvancedStudioSurface()
{
	if (!m_advancedStudioState || !m_advancedStudioDrawer) {
		return;
	}

	auto setListLines = [](QListWidget* list, const QStringList& lines, const QString& emptyText) {
		if (!list) {
			return;
		}
		list->clear();
		if (lines.isEmpty()) {
			list->addItem(disabledListItem(emptyText));
			return;
		}
		list->addItems(lines);
	};

	const bool hasShader = !m_advancedShaderDocument.shaders.isEmpty();
	const bool hasSprite = !m_advancedSpritePlan.frames.isEmpty();
	const bool hasCode = !m_advancedCodeIndex.files.isEmpty();
	const bool hasAi = !m_advancedAiProposal.title.isEmpty();
	const bool hasExtensions = !m_advancedExtensionDiscovery.manifests.isEmpty();
	const int ready = static_cast<int>(hasShader) + static_cast<int>(hasSprite) + static_cast<int>(hasCode) + static_cast<int>(hasAi) + static_cast<int>(hasExtensions);
	const OperationState state = ready >= 5 ? OperationState::Completed : (ready > 0 ? OperationState::Warning : OperationState::Idle);
	const QString projectPath = m_settings.currentProjectPath();

	// The Shaders page owns this pane, so it reports the shader script; the
	// sprite, code, AI, and extension results show on their own surfaces.
	Q_UNUSED(ready);
	Q_UNUSED(state);
	Q_UNUSED(projectPath);
	const OperationState shaderState = !hasShader ? OperationState::Idle
		: (m_advancedShaderValidationWarnings.isEmpty() ? OperationState::Completed : OperationState::Warning);
	m_advancedStudioState->setTitle(tr("Shader Script"));
	m_advancedStudioState->setDetail(hasShader
			? tr("%1 shader(s), %2 texture reference(s), %3 warning(s)")
				  .arg(m_advancedShaderDocument.shaders.size())
				  .arg(m_advancedShaderValidation.size())
				  .arg(m_advancedShaderValidationWarnings.size())
			: tr("Open an idTech3 shader script to inspect its stages."));
	m_advancedStudioState->setState(shaderState, localizedOperationStateName(shaderState));
	m_advancedStudioState->setProgress({});

	if (m_advancedShaderGraph) {
		const QSignalBlocker blocker(m_advancedShaderGraph);
		m_advancedShaderGraph->clear();
		if (!hasShader) {
			auto* empty = new QTreeWidgetItem(m_advancedShaderGraph, {tr("No shader script inspected yet.")});
			empty->setFirstColumnSpanned(true);
			empty->setFlags(Qt::NoItemFlags);
		}
		QHash<QString, ShaderReferenceValidation> checks;
		for (const ShaderReferenceValidation& check : m_advancedShaderValidation) {
			checks.insert(check.textureReference.toLower(), check);
		}
		const QString separator = QStringLiteral("  %1  ").arg(QChar(0x00b7));
		for (const ShaderDefinition& shader : m_advancedShaderDocument.shaders) {
			auto* shaderItem = new QTreeWidgetItem(m_advancedShaderGraph, {shader.name,
				QStringList {tr("%n stage(s)", nullptr, static_cast<int>(shader.stages.size())),
					tr("%n texture(s)", nullptr, static_cast<int>(shader.textureReferences.size()))}
					.join(separator)});
			shaderItem->setIcon(0, studioIcon(QStringLiteral("layers")));
			shaderItem->setData(0, Qt::UserRole, shader.name);
			shaderItem->setData(0, Qt::UserRole + 1, -1);
			shaderItem->setToolTip(0, shader.directives.isEmpty() ? shader.name : QStringLiteral("%1\n%2").arg(shader.name, shader.directives.join('\n')));
			shaderItem->setToolTip(1, shaderItem->toolTip(0));
			for (const ShaderStage& stage : shader.stages) {
				const QStringList details {
					stage.mapDirective.isEmpty() ? tr("no map") : stage.mapDirective,
					tr("blend %1").arg(stage.blendFunc.isEmpty() ? tr("default") : stage.blendFunc),
					tr("rgbGen %1").arg(stage.rgbGen.isEmpty() ? tr("identity") : stage.rgbGen),
				};
				auto* stageItem = new QTreeWidgetItem(shaderItem, {tr("Stage %1").arg(stage.id), details.join(separator)});
				stageItem->setData(0, Qt::UserRole, shader.name);
				stageItem->setData(0, Qt::UserRole + 1, stage.id);
				stageItem->setToolTip(0, stage.rawText.trimmed());
				stageItem->setToolTip(1, details.join('\n'));
				for (const QString& reference : stage.textureReferences) {
					const auto check = checks.constFind(reference.toLower());
					const bool checked = check != checks.constEnd();
					const bool missing = checked && !check->found;
					QString status = tr("not checked");
					if (checked) {
						status = !check->found ? tr("missing from the open package")
							: (check->foundInPackage.isEmpty() ? tr("found") : tr("found in %1").arg(QFileInfo(check->foundInPackage).fileName()));
					}
					auto* textureItem = new QTreeWidgetItem(stageItem, {reference, status});
					textureItem->setIcon(0, studioIcon(missing ? QStringLiteral("warning") : QStringLiteral("image"),
						missing ? StudioIconTone::Warning : StudioIconTone::Muted));
					textureItem->setData(0, Qt::UserRole, shader.name);
					textureItem->setData(0, Qt::UserRole + 1, stage.id);
					const QString tip = checked && !check->candidatePaths.isEmpty()
						? tr("%1\nLooked for: %2").arg(reference, check->candidatePaths.join(QStringLiteral(", ")))
						: reference;
					textureItem->setToolTip(0, tip);
					textureItem->setToolTip(1, tip);
				}
			}
		}
		m_advancedShaderGraph->expandAll();
	}

	QStringList spriteLines;
	if (hasSprite) {
		spriteLines << m_advancedSpritePlan.sequenceLines;
		spriteLines << m_advancedSpritePlan.palettePreviewLines;
		spriteLines << m_advancedSpritePlan.stagingLines;
	}
	setListLines(m_advancedSpriteSequence, spriteLines, tr("No sprite workflow plan yet."));

	QStringList codeLines;
	if (hasCode) {
		codeLines << m_advancedCodeIndex.treeLines.mid(0, 60);
		codeLines << tr("Symbols:");
		for (const CodeSymbol& symbol : m_advancedCodeIndex.symbols.mid(0, 60)) {
			codeLines << QStringLiteral("%1 %2 / %3:%4").arg(symbol.kind, symbol.name, symbol.relativePath).arg(symbol.line);
		}
		codeLines << tr("Build tasks:");
		codeLines << m_advancedCodeIndex.buildTaskLines;
		codeLines << tr("Launch profiles:");
		codeLines << m_advancedCodeIndex.launchProfileLines;
	}
	setListLines(m_advancedCodeTree, codeLines, tr("No code workspace indexed yet."));

	QStringList aiLines;
	if (hasAi) {
		aiLines = aiProposalReviewSurfaceText(m_advancedAiProposal).split('\n');
	}
	setListLines(m_advancedAiProposalList, aiLines, tr("No AI proposal generated yet."));

	QStringList extensionLines;
	if (hasExtensions || !m_advancedExtensionDiscovery.warnings.isEmpty()) {
		extensionLines = extensionDiscoveryText(m_advancedExtensionDiscovery).split('\n');
	} else {
		extensionLines = extensionTrustModelLines();
	}
	setListLines(m_advancedExtensions, extensionLines, tr("No extensions discovered yet."));

	refreshShaderDetailSections();
}

void ApplicationShell::refreshShaderDetailSections(const QString& shaderName, int stageIndex)
{
	if (!m_advancedStudioDrawer) {
		return;
	}
	const bool hasShader = !m_advancedShaderDocument.shaders.isEmpty();
	m_advancedStudioDrawer->setTitle(tr("Shader Details"));
	m_advancedStudioDrawer->setSubtitle(hasShader ? tr("Stages, blend modes, and texture references") : tr("No shader script loaded"));
	QVector<DetailSection> shaderSections;
	if (hasShader && !shaderName.isEmpty()) {
		// The selected shader, or one of its stages, ahead of the whole report.
		shaderSections.push_back({QStringLiteral("selection"),
			stageIndex >= 0 ? tr("Stage %1").arg(stageIndex) : tr("Shader"),
			shaderName,
			shaderStagePreviewLines(m_advancedShaderDocument, shaderName, stageIndex).join('\n'),
			OperationState::Completed});
	}
	if (hasShader) {
		shaderSections.push_back({QStringLiteral("shader"), tr("Shader Report"), tr("%1 shader(s)").arg(m_advancedShaderDocument.shaders.size()),
			shaderDocumentReportText(m_advancedShaderDocument, m_advancedShaderValidation, m_advancedShaderValidationWarnings),
			m_advancedShaderValidationWarnings.isEmpty() ? OperationState::Completed : OperationState::Warning});
	}
	shaderSections.push_back({QStringLiteral("capabilities"), tr("Capabilities"), tr("What the shader tools cover"), advancedStudioCapabilityLines().join('\n'), OperationState::Completed});
	m_advancedStudioDrawer->setSections(shaderSections);
	m_advancedStudioDrawer->showSection(!shaderName.isEmpty() && hasShader ? QStringLiteral("selection")
		: (hasShader ? QStringLiteral("shader") : QStringLiteral("capabilities")));
}

void ApplicationShell::inspectAdvancedShaderScript()
{
	const QString path = m_advancedShaderPath ? m_advancedShaderPath->text().trimmed() : QString();
	if (path.isEmpty()) {
		statusBar()->showMessage(tr("Choose a shader script before inspecting."));
		return;
	}
	QString error;
	if (!loadShaderScript(path, &m_advancedShaderDocument, &error)) {
		statusBar()->showMessage(tr("Shader inspect failed: %1").arg(error));
		recordActivity(tr("Shader inspect failed"), nativePath(path), QStringLiteral("shader"), OperationState::Failed, error);
		return;
	}
	QStringList packagePaths;
	if (m_packageArchive.isOpen()) {
		packagePaths << m_packageArchive.sourcePath();
	}
	m_advancedShaderValidation = validateShaderReferences(m_advancedShaderDocument, packagePaths, &m_advancedShaderValidationWarnings);
	recordActivity(tr("Shader inspected"), nativePath(path), QStringLiteral("shader"), m_advancedShaderValidationWarnings.isEmpty() ? OperationState::Completed : OperationState::Warning, tr("%1 shader(s), %2 reference(s)").arg(m_advancedShaderDocument.shaders.size()).arg(m_advancedShaderValidation.size()), m_advancedShaderValidationWarnings);
	refreshAdvancedStudioSurface();
	m_advancedStudioDrawer->showSection(QStringLiteral("shader"));
	statusBar()->showMessage(tr("Shader inspected: %1").arg(nativePath(path)));
}

void ApplicationShell::createAdvancedSpritePlan()
{
	SpriteWorkflowRequest request;
	request.engineFamily = m_advancedSpriteEngine ? m_advancedSpriteEngine->currentData().toString() : QStringLiteral("doom");
	request.spriteName = m_advancedSpriteName && !m_advancedSpriteName->text().trimmed().isEmpty() ? m_advancedSpriteName->text().trimmed() : QStringLiteral("SPRT");
	bool framesOk = false;
	bool rotationsOk = false;
	request.frameCount = m_advancedSpriteFrames ? m_advancedSpriteFrames->text().toInt(&framesOk) : 4;
	request.rotations = m_advancedSpriteRotations ? m_advancedSpriteRotations->text().toInt(&rotationsOk) : 8;
	if (!framesOk || request.frameCount <= 0) {
		request.frameCount = 4;
	}
	if (!rotationsOk || request.rotations < 0) {
		request.rotations = request.engineFamily == QStringLiteral("doom") ? 8 : 1;
	}
	request.paletteId = request.engineFamily == QStringLiteral("doom") ? QStringLiteral("doom-playpal") : QStringLiteral("quake-palette");
	request.outputPackageRoot = request.engineFamily == QStringLiteral("doom") ? QStringLiteral("sprites") : QStringLiteral("progs");
	m_advancedSpritePlan = buildSpriteWorkflowPlan(request);
	recordActivity(tr("Sprite plan created"), request.spriteName, QStringLiteral("sprite"), m_advancedSpritePlan.state, tr("%1 staged frame(s)").arg(m_advancedSpritePlan.frames.size()), m_advancedSpritePlan.warnings);
	refreshAdvancedStudioSurface();
	m_advancedStudioDrawer->showSection(QStringLiteral("sprite"));
	statusBar()->showMessage(tr("Sprite workflow plan created."));
}

void ApplicationShell::indexAdvancedCodeWorkspace()
{
	const QString root = m_settings.currentProjectPath().isEmpty() ? QDir::currentPath() : m_settings.currentProjectPath();
	CodeWorkspaceIndexRequest request;
	request.rootPath = root;
	request.symbolQuery = m_workspaceSearch ? m_workspaceSearch->text().trimmed() : QString();
	m_advancedCodeIndex = indexCodeWorkspace(request);
	recordActivity(tr("Code workspace indexed"), nativePath(root), QStringLiteral("code"), m_advancedCodeIndex.state, tr("%1 file(s), %2 symbol(s)").arg(m_advancedCodeIndex.files.size()).arg(m_advancedCodeIndex.symbols.size()), m_advancedCodeIndex.warnings);
	refreshAdvancedStudioSurface();
	m_advancedStudioDrawer->showSection(QStringLiteral("code"));
	statusBar()->showMessage(tr("Code workspace indexed: %1").arg(nativePath(root)));
}

void ApplicationShell::createAdvancedAiProposal()
{
	const QString prompt = m_advancedAiPrompt && !m_advancedAiPrompt->text().trimmed().isEmpty() ? m_advancedAiPrompt->text().trimmed() : tr("Create a reviewable idTech asset workflow");
	const QString kind = m_advancedAiKind ? m_advancedAiKind->currentData().toString() : QStringLiteral("shader");
	const AiAutomationPreferences preferences = m_settings.aiAutomationPreferences();
	if (kind == QStringLiteral("entity")) {
		m_advancedAiProposal = promptToEntityDefinitionAiExperiment(prompt, preferences);
	} else if (kind == QStringLiteral("package")) {
		m_advancedAiProposal = promptToPackageValidationPlanAiExperiment(prompt, m_packageArchive.isOpen() ? m_packageArchive.sourcePath() : QString(), preferences);
	} else if (kind == QStringLiteral("batch")) {
		m_advancedAiProposal = promptToBatchConversionRecipeAiExperiment(prompt, preferences);
	} else if (kind == QStringLiteral("cli")) {
		m_advancedAiProposal = generateCliCommandAiExperiment(prompt, preferences);
	} else {
		m_advancedAiProposal = promptToShaderScaffoldAiExperiment(prompt, preferences);
	}
	recordActivity(tr("AI proposal generated"), kind, QStringLiteral("ai"), m_advancedAiProposal.manifest.state, m_advancedAiProposal.summary, m_advancedAiProposal.manifest.warnings);
	refreshAdvancedStudioSurface();
	m_advancedStudioDrawer->showSection(QStringLiteral("ai"));
	statusBar()->showMessage(tr("AI proposal generated for review."));
}

void ApplicationShell::discoverAdvancedExtensions()
{
	QString root = m_advancedExtensionRoot ? m_advancedExtensionRoot->text().trimmed() : QString();
	if (root.isEmpty() && !m_settings.currentProjectPath().isEmpty()) {
		root = QDir(m_settings.currentProjectPath()).filePath(QStringLiteral("extensions"));
	}
	if (root.isEmpty()) {
		root = QDir::currentPath();
	}
	m_advancedExtensionDiscovery = discoverExtensions({root});
	recordActivity(tr("Extensions discovered"), nativePath(root), QStringLiteral("extension"), m_advancedExtensionDiscovery.state, tr("%1 extension manifest(s)").arg(m_advancedExtensionDiscovery.manifests.size()), m_advancedExtensionDiscovery.warnings);
	refreshAdvancedStudioSurface();
	m_advancedStudioDrawer->showSection(QStringLiteral("extensions"));
	statusBar()->showMessage(tr("Extension discovery complete."));
}

QString ApplicationShell::selectedWorkspaceFilePath() const
{
	const QListWidget* lists[] = {m_workspaceSearchResults, m_changedFiles, m_dependencyGraph, m_projectProblems, m_recentActivityTimeline};
	for (const QListWidget* list : lists) {
		const QListWidgetItem* item = list ? list->currentItem() : nullptr;
		const QString path = item ? item->data(Qt::UserRole).toString() : QString();
		if (!path.trimmed().isEmpty()) {
			return path;
		}
	}
	return {};
}

QString ApplicationShell::selectedWorkspaceVirtualPath() const
{
	const QListWidget* lists[] = {m_workspaceSearchResults, m_changedFiles, m_dependencyGraph, m_projectProblems, m_recentActivityTimeline};
	for (const QListWidget* list : lists) {
		const QListWidgetItem* item = list ? list->currentItem() : nullptr;
		const QString path = item ? item->data(Qt::UserRole + 1).toString() : QString();
		if (!path.trimmed().isEmpty()) {
			return path;
		}
	}
	const QString selectedPackagePath = selectedPackageEntryPath();
	if (!selectedPackagePath.isEmpty()) {
		return selectedPackagePath;
	}
	return {};
}

void ApplicationShell::revealSelectedWorkspacePath()
{
	const QString path = selectedWorkspaceFilePath();
	if (path.isEmpty()) {
		statusBar()->showMessage(tr("No local workspace path selected"));
		return;
	}

	QFileInfo info(path);
	if (!info.exists()) {
		statusBar()->showMessage(tr("Selected workspace path does not exist locally"));
		return;
	}
	const QString revealPath = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
	if (revealPath.isEmpty() || !QDesktopServices::openUrl(QUrl::fromLocalFile(revealPath))) {
		statusBar()->showMessage(tr("Unable to reveal workspace path"));
		return;
	}
	statusBar()->showMessage(tr("Revealed: %1").arg(nativePath(revealPath)));
}

void ApplicationShell::copySelectedWorkspaceVirtualPath()
{
	QString path = selectedWorkspaceVirtualPath();
	if (path.isEmpty()) {
		path = selectedWorkspaceFilePath();
	}
	if (path.isEmpty()) {
		statusBar()->showMessage(tr("No workspace path selected"));
		return;
	}
	QApplication::clipboard()->setText(path);
	statusBar()->showMessage(tr("Copied path: %1").arg(path));
}

void ApplicationShell::refreshPackageTree()
{
	if (!m_packageTree) {
		return;
	}

	const QString selectedPath = selectedPackageTreeEntryPath();
	m_packageTree->clear();
	if (!m_packageArchive.isOpen()) {
		auto* item = new QTreeWidgetItem(QStringList {tr("No package loaded")});
		item->setFlags(Qt::NoItemFlags);
		m_packageTree->addTopLevelItem(item);
		return;
	}

	const PackageArchiveSummary summary = m_packageArchive.summary();
	const QString rootLabel = QFileInfo(summary.sourcePath).fileName().isEmpty() ? localizedPackageFormatName(summary.format) : QFileInfo(summary.sourcePath).fileName();
	auto* root = new QTreeWidgetItem(QStringList {rootLabel});
	root->setIcon(0, studioIcon(QStringLiteral("package"), StudioIconTone::Muted));
	root->setData(0, Qt::UserRole, QString());
	root->setData(0, Qt::UserRole + 1, summary.warningCount > 0 ? QStringLiteral("warning") : QStringLiteral("completed"));
	root->setToolTip(0, tr("%1 entries from %2").arg(summary.entryCount).arg(nativePath(summary.sourcePath)));
	m_packageTree->addTopLevelItem(root);

	// Folders only: files are listed in the entry column for whichever folder
	// is selected, the way a file browser splits its panes.
	QHash<QString, QTreeWidgetItem*> nodes;
	nodes.insert(QString(), root);
	for (const PackageEntry& entry : m_packageArchive.entries()) {
		const QStringList parts = entry.virtualPath.split('/', Qt::SkipEmptyParts);
		QString currentPath;
		QTreeWidgetItem* parent = root;
		const int folderDepth = entry.kind == PackageEntryKind::Directory ? parts.size() : parts.size() - 1;
		for (int index = 0; index < folderDepth; ++index) {
			if (!currentPath.isEmpty()) {
				currentPath += '/';
			}
			currentPath += parts.at(index);
			QTreeWidgetItem* node = nodes.value(currentPath, nullptr);
			if (!node) {
				node = new QTreeWidgetItem(QStringList {parts.at(index)});
				node->setIcon(0, studioIcon(QStringLiteral("folder"), StudioIconTone::Muted));
				node->setData(0, Qt::UserRole, currentPath);
				node->setData(0, Qt::UserRole + 1, QStringLiteral("completed"));
				parent->addChild(node);
				nodes.insert(currentPath, node);
			}
			parent = node;
		}

		if (entry.kind == PackageEntryKind::Directory) {
			if (QTreeWidgetItem* node = nodes.value(entry.virtualPath, nullptr)) {
				node->setData(0, Qt::UserRole + 1, entry.note.isEmpty() ? QStringLiteral("completed") : QStringLiteral("warning"));
				node->setToolTip(0, entry.virtualPath);
			}
		}
	}
	m_packageTree->sortItems(0, Qt::AscendingOrder);

	m_packageTree->expandItem(root);
	m_packageTree->resizeColumnToContents(0);
	Q_UNUSED(selectedPath);
	if (m_packageBrowseFolder.isEmpty() || !nodes.contains(m_packageBrowseFolder)) {
		m_packageBrowseFolder.clear();
		const QSignalBlocker blocker(m_packageTree);
		m_packageTree->setCurrentItem(root);
	} else {
		selectPackageTreeEntryPath(m_packageBrowseFolder);
	}
	scheduleThemeRefresh();
}

void ApplicationShell::refreshPackageCompositionSummary()
{
	if (!m_packageComposition) {
		return;
	}
	m_packageComposition->clear();
	if (!m_packageArchive.isOpen()) {
		auto* item = new QListWidgetItem(tr("No package composition yet\nOpen a folder, PAK, WAD, ZIP, or PK3 to see type and size distribution."));
		item->setFlags(Qt::NoItemFlags);
		item->setData(Qt::UserRole + 1, QStringLiteral("idle"));
		m_packageComposition->addItem(item);
		return;
	}

	const PackageArchiveSummary summary = m_packageArchive.summary();
	const QVector<SummaryBucket> buckets = packageCompositionBuckets(m_packageArchive.entries());
	const double totalBytes = static_cast<double>(std::max<quint64>(1, summary.totalSizeBytes));
	for (const SummaryBucket& bucket : buckets) {
		const double fraction = bucket.bytes > 0 ? static_cast<double>(bucket.bytes) / totalBytes : 0.0;
		auto* item = new QListWidgetItem(QStringLiteral("%1 %2\n%3 / %4")
			.arg(compositionBar(fraction), bucket.label, byteSizeText(bucket.bytes), tr("%n entries", nullptr, bucket.count)));
		item->setData(Qt::UserRole + 1, bucket.bytes > 0 ? QStringLiteral("completed") : QStringLiteral("warning"));
		m_packageComposition->addItem(item);
	}
	if (buckets.isEmpty()) {
		auto* item = new QListWidgetItem(tr("Package is empty"));
		item->setFlags(Qt::NoItemFlags);
		item->setData(Qt::UserRole + 1, QStringLiteral("warning"));
		m_packageComposition->addItem(item);
	}

	if (m_packageCompositionChart) {
		QVector<StudioChartSlice> slices;
		int patternIndex = 0;
		for (const SummaryBucket& bucket : buckets) {
			StudioChartSlice slice;
			slice.id = bucket.id;
			slice.label = bucket.label;
			slice.value = static_cast<double>(bucket.bytes);
			slice.valueText = byteSizeText(bucket.bytes);
			slice.detail = tr("%n entries", nullptr, bucket.count);
			slice.state = bucket.bytes > 0 ? OperationState::Completed : OperationState::Warning;
			slice.patternIndex = patternIndex++;
			slices.push_back(slice);
		}
		m_packageCompositionChart->setSlices(slices);
	}
}

void ApplicationShell::refreshPackageStagingSummary()
{
	if (!m_packageStagingSummary) {
		return;
	}

	m_packageStagingSummary->clear();
	const bool stagingLoaded = m_packageArchive.isOpen() && m_packageStaging.isLoaded();
	const auto setStageButtons = [this, stagingLoaded](bool canSave) {
		if (m_packageStageAdd) {
			m_packageStageAdd->setEnabled(stagingLoaded);
		}
		if (m_packageStageReplace) {
			m_packageStageReplace->setEnabled(stagingLoaded && !selectedPackageEntryPath().isEmpty());
		}
		if (m_packageStageRename) {
			m_packageStageRename->setEnabled(stagingLoaded && !selectedPackageEntryPath().isEmpty());
		}
		if (m_packageStageDelete) {
			m_packageStageDelete->setEnabled(stagingLoaded && !selectedPackageEntryPaths().isEmpty());
		}
		if (m_packageStageSaveAs) {
			m_packageStageSaveAs->setEnabled(stagingLoaded && canSave);
		}
	};

	const auto addItem = [this](const QString& text, const QString& state, const QString& virtualPath = QString()) {
		auto* item = new QListWidgetItem(text);
		if (!virtualPath.isEmpty()) {
			item->setData(Qt::UserRole, virtualPath);
		}
		item->setData(Qt::UserRole + 1, state);
		m_packageStagingSummary->addItem(item);
		return item;
	};

	if (!stagingLoaded) {
		auto* item = addItem(tr("No package loaded for staging\nOpen a folder, PAK, WAD, ZIP, or PK3 before staging changes."), QStringLiteral("idle"));
		item->setFlags(Qt::NoItemFlags);
		setStageButtons(false);
		return;
	}

	const PackageStagingSummary summary = m_packageStaging.summary();
	const QString state = summary.blockingCount > 0 ? QStringLiteral("failed") : QStringLiteral("completed");
	addItem(QStringLiteral("%1\n%2 / %3 -> %4 / %5")
			.arg(summary.canSave ? tr("Staging ready") : tr("Staging blocked"),
				tr("%1 operations, %2 conflicts, %3 blockers").arg(summary.operationCount).arg(summary.conflictCount).arg(summary.blockingCount),
				tr("%n base files", nullptr, summary.baseFileCount),
				tr("%n staged files", nullptr, summary.stagedFileCount),
				tr("%1 before, %2 after").arg(byteSizeText(summary.beforeBytes), byteSizeText(summary.afterBytes))),
		state);

	if (summary.operationCount == 0) {
		addItem(tr("No staged changes yet\nUse Stage Add, Replace, Rename, or Delete to build a save-as plan."), QStringLiteral("idle"));
	}

	for (const PackageStageOperation& operation : m_packageStaging.operations()) {
		QStringList lines;
		lines << QStringLiteral("%1: %2").arg(localizedPackageStageOperationName(operation.type), operation.virtualPath);
		if (!operation.targetVirtualPath.isEmpty()) {
			lines << tr("Target: %1").arg(operation.targetVirtualPath);
		}
		if (!operation.sourceFilePath.isEmpty()) {
			lines << tr("Source: %1").arg(nativePath(operation.sourceFilePath));
		}
		lines << tr("Conflict policy: %1").arg(packageStageConflictResolutionId(operation.conflictResolution));
		addItem(lines.join('\n'), QStringLiteral("running"), operation.virtualPath);
	}

	for (const PackageStageConflict& conflict : m_packageStaging.conflicts()) {
		addItem(QStringLiteral("%1\n%2")
				.arg(conflict.blocking ? tr("Blocked") : tr("Notice"),
					conflict.virtualPath.isEmpty() ? conflict.message : QStringLiteral("%1: %2").arg(conflict.virtualPath, conflict.message)),
			conflict.blocking ? QStringLiteral("failed") : QStringLiteral("warning"),
			conflict.virtualPath);
	}

	const auto addCompositionLines = [&](const QString& title, const QVector<PackageCompositionBucket>& buckets, quint64 totalBytes) {
		const int limit = 4;
		int shown = 0;
		for (const PackageCompositionBucket& bucket : buckets) {
			if (shown >= limit) {
				break;
			}
			const double fraction = bucket.sizeBytes > 0 ? static_cast<double>(bucket.sizeBytes) / static_cast<double>(std::max<quint64>(1, totalBytes)) : 0.0;
			addItem(QStringLiteral("%1: %2\n%3 %4 / %5")
					.arg(title,
						bucket.label,
						compositionBar(fraction),
						byteSizeText(bucket.sizeBytes),
						tr("%n files", nullptr, bucket.fileCount)),
				QStringLiteral("completed"));
			++shown;
		}
		if (buckets.isEmpty()) {
			addItem(QStringLiteral("%1\n%2").arg(title, tr("No files in this composition.")), QStringLiteral("warning"));
		}
	};
	addCompositionLines(tr("Before"), m_packageStaging.beforeComposition(), summary.beforeBytes);
	addCompositionLines(tr("After"), m_packageStaging.afterComposition(), summary.afterBytes);

	setStageButtons(summary.canSave);
	scheduleThemeRefresh();
}

void ApplicationShell::refreshPackageEntryDetails(const QString& virtualPath)
{
	if (!m_packageDrawer || !m_packageArchive.isOpen()) {
		return;
	}

	PackageEntry selectedEntry;
	bool found = false;
	for (const PackageEntry& entry : m_packageArchive.entries()) {
		if (entry.virtualPath == virtualPath) {
			selectedEntry = entry;
			found = true;
			break;
		}
	}

	if (!found) {
		m_packageDrawer->setTitle(tr("Package Entry Details"));
		m_packageDrawer->setSubtitle(tr("No entry selected."));
		m_packageDrawer->setSections({});
		return;
	}

	QStringList summaryLines;
	summaryLines << tr("Path: %1").arg(selectedEntry.virtualPath);
	summaryLines << tr("Kind: %1").arg(localizedPackageEntryKindName(selectedEntry.kind));
	summaryLines << tr("Type: %1").arg(selectedEntry.typeHint);
	summaryLines << tr("Size: %1").arg(byteSizeText(selectedEntry.sizeBytes));
	summaryLines << tr("Compressed size: %1").arg(byteSizeText(selectedEntry.compressedSizeBytes));
	summaryLines << tr("Storage: %1").arg(selectedEntry.storageMethod.isEmpty() ? tr("unknown") : selectedEntry.storageMethod);
	summaryLines << tr("Readable now: %1").arg(selectedEntry.readable ? tr("yes") : tr("no"));
	summaryLines << tr("Nested archive candidate: %1").arg(selectedEntry.nestedArchiveCandidate ? tr("yes") : tr("no"));
	if (!selectedEntry.modifiedUtc.isValid()) {
		summaryLines << tr("Modified: unknown");
	} else {
		summaryLines << tr("Modified: %1").arg(QLocale::system().toString(selectedEntry.modifiedUtc.toLocalTime(), QLocale::LongFormat));
	}
	if (!selectedEntry.note.isEmpty()) {
		summaryLines << tr("Note: %1").arg(selectedEntry.note);
	}

	const PackagePreview preview = buildPackageEntryPreview(m_packageArchive, selectedEntry.virtualPath);
	const OperationState previewState = preview.kind == PackagePreviewKind::Unavailable ? OperationState::Warning : OperationState::Completed;
	QStringList previewDetailLines = preview.detailLines;
	if (previewDetailLines.isEmpty()) {
		previewDetailLines << tr("No preview details available.");
	}
	QStringList assetDetailLines = preview.assetDetailLines;
	if (assetDetailLines.isEmpty()) {
		assetDetailLines << tr("No specialized asset metadata available.");
	}
	QStringList assetRawLines = preview.assetRawLines;
	if (assetRawLines.isEmpty()) {
		assetRawLines << tr("No raw asset metadata available.");
	}

	QStringList rawLines;
	rawLines << tr("Source: %1").arg(nativePath(m_packageArchive.sourcePath()));
	rawLines << tr("Format: %1").arg(packageArchiveFormatId(m_packageArchive.format()));
	rawLines << tr("Virtual path: %1").arg(selectedEntry.virtualPath);
	rawLines << tr("Kind id: %1").arg(packageEntryKindId(selectedEntry.kind));
	rawLines << tr("Data offset: %1").arg(selectedEntry.dataOffset);
	rawLines << tr("Size bytes: %1").arg(selectedEntry.sizeBytes);
	rawLines << tr("Compressed bytes: %1").arg(selectedEntry.compressedSizeBytes);
	rawLines << tr("Source archive id: %1").arg(selectedEntry.sourceArchiveId);
	rawLines << QString();
	rawLines << tr("Preview raw details:");
	rawLines << (preview.rawLines.isEmpty() ? tr("No preview raw details available.") : preview.rawLines.join('\n'));

	QStringList warningLines;
	for (const PackageLoadWarning& warning : m_packageArchive.warnings()) {
		if (warning.virtualPath.isEmpty() || warning.virtualPath == selectedEntry.virtualPath) {
			warningLines << QStringLiteral("%1: %2").arg(warning.virtualPath.isEmpty() ? tr("package") : warning.virtualPath, warning.message);
		}
	}
	if (warningLines.isEmpty()) {
		warningLines << tr("No warnings for this entry.");
	}

	const PackageArchiveSummary packageSummary = m_packageArchive.summary();
	const QVector<SummaryBucket> compositionBuckets = packageCompositionBuckets(m_packageArchive.entries());
	QStringList compositionLines;
	const double totalBytes = static_cast<double>(std::max<quint64>(1, packageSummary.totalSizeBytes));
	for (const SummaryBucket& bucket : compositionBuckets) {
		const double fraction = bucket.bytes > 0 ? static_cast<double>(bucket.bytes) / totalBytes : 0.0;
		compositionLines << QStringLiteral("%1 %2").arg(compositionBar(fraction), bucket.label);
		compositionLines << tr("%1 / %2").arg(byteSizeText(bucket.bytes), tr("%n entries", nullptr, bucket.count));
	}
	if (compositionLines.isEmpty()) {
		compositionLines << tr("Package is empty.");
	}

	const OperationState entryState = !selectedEntry.note.isEmpty() ? OperationState::Warning : OperationState::Completed;
	m_packageDrawer->setTitle(tr("Package Entry Details"));
	m_packageDrawer->setSubtitle(selectedEntry.virtualPath);
	m_packageDrawer->setSections({
		{QStringLiteral("preview"), tr("Preview"), preview.summary.isEmpty() ? localizedPackagePreviewKindName(preview.kind) : preview.summary, previewContentText(preview), previewState},
		{QStringLiteral("summary"), tr("Summary"), selectedEntry.typeHint, summaryLines.join('\n'), entryState},
		{QStringLiteral("preview-details"), tr("Preview Details"), localizedPackagePreviewKindName(preview.kind), previewDetailLines.join('\n'), previewState},
		{QStringLiteral("asset-details"), tr("Asset Details"), preview.assetKindId.isEmpty() ? localizedPackagePreviewKindName(preview.kind) : preview.assetKindId, assetDetailLines.join('\n'), previewState},
		{QStringLiteral("asset-raw"), tr("Asset Raw Metadata"), preview.assetKindId.isEmpty() ? selectedEntry.virtualPath : preview.assetKindId, assetRawLines.join('\n'), previewState},
		{QStringLiteral("composition"), tr("Composition"), tr("%n buckets", nullptr, compositionBuckets.size()), compositionLines.join('\n'), packageSummary.warningCount > 0 ? OperationState::Warning : OperationState::Completed},
		{QStringLiteral("warnings"), tr("Warnings"), tr("%n package warnings", nullptr, m_packageArchive.warnings().size()), warningLines.join('\n'), warningLines.size() == 1 && warningLines.front() == tr("No warnings for this entry.") ? OperationState::Completed : OperationState::Warning},
		{QStringLiteral("raw"), tr("Raw Metadata"), selectedEntry.virtualPath, rawLines.join('\n'), entryState},
	});
	m_packageDrawer->showSection(QStringLiteral("preview"));
}

void ApplicationShell::filterPackageEntries()
{
	if (!m_packageEntries) {
		return;
	}

	const QString selectedPath = selectedPackageEntryPath();
	const QString filter = m_packageFilter ? m_packageFilter->text().trimmed().toCaseFolded() : QString();
	m_packageEntries->clear();

	if (!m_packageArchive.isOpen()) {
		auto* item = new QListWidgetItem(tr("No package loaded"));
		item->setFlags(Qt::NoItemFlags);
		m_packageEntries->addItem(item);
		refreshPackageEntryDetails(QString());
		rebuildPackageBreadcrumb();
		return;
	}

	// Without a filter the list shows the current folder, folders first; a
	// filter searches the whole package and lists matches by full path.
	const bool searching = !filter.isEmpty();
	const QVector<PackageEntry> entries = m_packageArchive.entries();
	QHash<QString, int> childCounts;
	for (const PackageEntry& entry : entries) {
		childCounts[packageParentFolder(entry.virtualPath)] += 1;
	}
	QVector<const PackageEntry*> shown;
	for (const PackageEntry& entry : entries) {
		if (searching) {
			const QString haystack = QStringLiteral("%1 %2 %3").arg(entry.virtualPath, entry.typeHint, entry.storageMethod).toCaseFolded();
			if (haystack.contains(filter)) {
				shown.push_back(&entry);
			}
		} else if (packageParentFolder(entry.virtualPath) == m_packageBrowseFolder) {
			shown.push_back(&entry);
		}
	}
	if (!searching) {
		std::stable_sort(shown.begin(), shown.end(), [](const PackageEntry* left, const PackageEntry* right) {
			const bool leftFolder = left->kind == PackageEntryKind::Directory;
			const bool rightFolder = right->kind == PackageEntryKind::Directory;
			if (leftFolder != rightFolder) {
				return leftFolder;
			}
			return QString::compare(left->virtualPath, right->virtualPath, Qt::CaseInsensitive) < 0;
		});
	}

	const QString separator = QStringLiteral("  %1  ").arg(QChar(0x00b7));
	int selectedRow = -1;
	for (const PackageEntry* entryPointer : shown) {
		const PackageEntry& entry = *entryPointer;
		const bool folder = entry.kind == PackageEntryKind::Directory;
		const QString name = searching ? entry.virtualPath
			: (packageVirtualPathFileName(entry.virtualPath).isEmpty() ? entry.virtualPath : packageVirtualPathFileName(entry.virtualPath));
		const QString meta = folder
			? tr("%n item(s)", nullptr, childCounts.value(entry.virtualPath))
			: QStringList {byteSizeText(entry.sizeBytes), entry.typeHint, entry.storageMethod.isEmpty() ? tr("unknown") : entry.storageMethod}.join(separator);
		auto* item = new QListWidgetItem(studioIcon(packageEntryIconName(entry), folder ? StudioIconTone::Accent : StudioIconTone::Muted), QStringLiteral("%1\n%2").arg(name, meta));
		item->setToolTip(QStringLiteral("%1\n%2").arg(entry.virtualPath, meta));
		item->setData(Qt::AccessibleTextRole, QStringLiteral("%1, %2, %3").arg(entry.virtualPath, localizedPackageEntryKindName(entry.kind), meta));
		item->setData(Qt::UserRole, entry.virtualPath);
		item->setData(Qt::UserRole + 1, entry.note.isEmpty() ? QStringLiteral("completed") : QStringLiteral("warning"));
		item->setData(Qt::UserRole + 3, folder);
		m_packageEntries->addItem(item);
		if (entry.virtualPath == selectedPath) {
			selectedRow = m_packageEntries->count() - 1;
		}
	}

	if (m_packageEntries->count() == 0) {
		auto* item = new QListWidgetItem(searching ? tr("No entries match \u201c%1\u201d").arg(m_packageFilter->text().trimmed()) : tr("This folder is empty"));
		item->setFlags(Qt::NoItemFlags);
		m_packageEntries->addItem(item);
		refreshPackageEntryDetails(QString());
		rebuildPackageBreadcrumb();
		return;
	}

	{
		const QSignalBlocker blocker(m_packageEntries);
		m_packageEntries->setCurrentRow(selectedRow >= 0 ? selectedRow : 0);
	}
	refreshPackageEntryDetails(selectedPackageEntryPath());
	rebuildPackageBreadcrumb();
	scheduleThemeRefresh();
}

void ApplicationShell::navigatePackageFolder(const QString& folder, const QString& selectPath, bool recordHistory)
{
	if (!m_packageArchive.isOpen()) {
		return;
	}
	m_packageBrowseFolder = folder;
	if (recordHistory) {
		if (m_packageFolderHistoryIndex >= 0 && m_packageFolderHistoryIndex < m_packageFolderHistory.size()
			&& m_packageFolderHistory.at(m_packageFolderHistoryIndex) == folder) {
			// Re-selecting the current folder is not a new history step.
		} else {
			while (m_packageFolderHistory.size() > m_packageFolderHistoryIndex + 1) {
				m_packageFolderHistory.removeLast();
			}
			m_packageFolderHistory.push_back(folder);
			m_packageFolderHistoryIndex = static_cast<int>(m_packageFolderHistory.size()) - 1;
		}
	}
	// Browsing a folder ends a search; the filter would otherwise hide it.
	if (m_packageFilter && !m_packageFilter->text().isEmpty()) {
		const QSignalBlocker blocker(m_packageFilter);
		m_packageFilter->clear();
	}
	filterPackageEntries();
	if (!selectPath.isEmpty()) {
		for (int index = 0; index < m_packageEntries->count(); ++index) {
			QListWidgetItem* item = m_packageEntries->item(index);
			if (item && item->data(Qt::UserRole).toString() == selectPath) {
				const QSignalBlocker blocker(m_packageEntries);
				m_packageEntries->setCurrentItem(item);
				break;
			}
		}
		refreshPackageEntryDetails(selectedPackageEntryPath());
	}
	if (folder.isEmpty()) {
		if (m_packageTree && m_packageTree->topLevelItemCount() > 0) {
			const QSignalBlocker blocker(m_packageTree);
			m_packageTree->setCurrentItem(m_packageTree->topLevelItem(0));
		}
	} else {
		selectPackageTreeEntryPath(folder);
	}
	refreshPackageStagingSummary();
}

void ApplicationShell::stepPackageFolderHistory(int delta)
{
	const int target = m_packageFolderHistoryIndex + delta;
	if (target < 0 || target >= m_packageFolderHistory.size()) {
		return;
	}
	m_packageFolderHistoryIndex = target;
	navigatePackageFolder(m_packageFolderHistory.at(target), QString(), false);
}

void ApplicationShell::rebuildPackageBreadcrumb()
{
	if (!m_packageBreadcrumb) {
		return;
	}
	auto* layout = qobject_cast<QHBoxLayout*>(m_packageBreadcrumb->layout());
	if (!layout) {
		return;
	}
	while (QLayoutItem* child = layout->takeAt(0)) {
		if (QWidget* widget = child->widget()) {
			widget->deleteLater();
		}
		delete child;
	}

	const bool open = m_packageArchive.isOpen();
	const bool searching = m_packageFilter && !m_packageFilter->text().trimmed().isEmpty();
	if (m_packageBack) {
		m_packageBack->setEnabled(open && m_packageFolderHistoryIndex > 0);
	}
	if (m_packageForward) {
		m_packageForward->setEnabled(open && m_packageFolderHistoryIndex >= 0 && m_packageFolderHistoryIndex + 1 < m_packageFolderHistory.size());
	}
	if (m_packageUp) {
		m_packageUp->setEnabled(open && !m_packageBrowseFolder.isEmpty());
	}
	if (!open) {
		layout->addStretch(1);
		return;
	}

	auto addCrumb = [this, layout](const QString& text, const QString& folder, bool current) {
		auto* crumb = new QToolButton;
		crumb->setObjectName(QStringLiteral("breadcrumbButton"));
		crumb->setText(text);
		crumb->setAutoRaise(true);
		crumb->setFocusPolicy(Qt::TabFocus);
		crumb->setProperty("current", current);
		crumb->setAccessibleName(tr("Folder %1").arg(text));
		crumb->setToolTip(folder.isEmpty() ? tr("Package root") : folder);
		connect(crumb, &QToolButton::clicked, this, [this, folder]() {
			navigatePackageFolder(folder);
		});
		layout->addWidget(crumb);
	};
	auto addSeparator = [layout]() {
		auto* chevron = new QLabel(QString(QChar(0x203a)));
		chevron->setObjectName(QStringLiteral("breadcrumbSeparator"));
		chevron->setAccessibleName(QString());
		layout->addWidget(chevron);
	};

	const QString rootName = QFileInfo(m_packageArchive.summary().sourcePath).fileName();
	addCrumb(rootName.isEmpty() ? tr("Package") : rootName, QString(), m_packageBrowseFolder.isEmpty() && !searching);
	QString path;
	const QStringList parts = m_packageBrowseFolder.split(QLatin1Char('/'), Qt::SkipEmptyParts);
	for (int index = 0; index < parts.size(); ++index) {
		path = path.isEmpty() ? parts.at(index) : path + QLatin1Char('/') + parts.at(index);
		addSeparator();
		addCrumb(parts.at(index), path, index == parts.size() - 1 && !searching);
	}
	if (searching) {
		addSeparator();
		auto* results = new QLabel(tr("Search results"));
		results->setObjectName(QStringLiteral("breadcrumbSeparator"));
		layout->addWidget(results);
	}
	layout->addStretch(1);
	if (m_packageSummary) {
		int count = 0;
		for (int index = 0; index < m_packageEntries->count(); ++index) {
			if (m_packageEntries->item(index)->flags().testFlag(Qt::ItemIsSelectable)) {
				++count;
			}
		}
		m_packageSummary->setText(tr("%n item(s)", nullptr, count));
	}
}

void ApplicationShell::refreshCompilerPipelineSummary()
{
	if (!m_compilerPipeline) {
		return;
	}
	m_compilerPipeline->clear();
	QVector<PipelineStageNode> pipelineNodes;
	const QString projectPath = m_settings.currentProjectPath();
	const CompilerRegistrySummary registry = discoverCompilerTools(compilerRegistryOptionsForProject(projectPath, m_settings));
	const QVector<CompilerProfileDescriptor> profiles = compilerProfileDescriptors();
	if (profiles.isEmpty()) {
		auto* item = new QListWidgetItem(tr("No compiler profiles registered"));
		item->setFlags(Qt::NoItemFlags);
		item->setData(Qt::UserRole + 1, QStringLiteral("warning"));
		m_compilerPipeline->addItem(item);
		return;
	}

	for (const QString& graphic : {
		tr("idTech1: WAD -> ZDBSP/ZokumBSP -> nodes, blockmap, reject"),
		tr("idTech2: MAP -> qbsp -> vis -> light -> BSP/LIT artifacts"),
		tr("idTech3: MAP -> q3map2 -meta -> BSP artifact"),
	}) {
		auto* item = new QListWidgetItem(graphic);
		item->setFlags(Qt::NoItemFlags);
		item->setData(Qt::UserRole + 1, QStringLiteral("completed"));
		m_compilerPipeline->addItem(item);
	}

	for (const CompilerProfileDescriptor& profile : profiles) {
		const CompilerToolDiscovery* discovery = compilerDiscoveryForTool(registry, profile.toolId);
		OperationState state = OperationState::Failed;
		QString readiness = tr("tool missing");
		QString executable = tr("not found");
		if (discovery) {
			state = discovery->state();
			readiness = discovery->executableAvailable ? tr("ready") : (discovery->sourceAvailable ? tr("source only") : tr("missing"));
			executable = discovery->executablePath.isEmpty() ? tr("not found") : nativePath(discovery->executablePath);
		}
		auto* item = new QListWidgetItem(QStringLiteral("%1 %2 / %3\n%4 / %5")
			.arg(compilerPipelineBar(state), profile.engineFamily, profile.stageId, profile.id, readiness));
		item->setToolTip(tr("Executable: %1").arg(executable));
		item->setData(Qt::UserRole, profile.id);
		item->setData(Qt::UserRole + 1, operationStateId(state));
		m_compilerPipeline->addItem(item);

		if (m_compilerPipelineChart) {
			PipelineStageNode node;
			node.id = profile.id;
			node.label = profile.displayName.isEmpty() ? profile.id : profile.displayName;
			node.detail = executable;
			node.state = state;
			node.badgeText = readiness;
			pipelineNodes.push_back(node);
		}
	}
	if (m_compilerPipelineChart) {
		m_compilerPipelineChart->setStages(pipelineNodes);
	}
	if (m_compilerRunSelected) {
		m_compilerRunSelected->setEnabled(m_compilerRunThread == nullptr);
	}
}

QString ApplicationShell::selectedCompilerProfileId() const
{
	const QListWidgetItem* item = m_compilerPipeline ? m_compilerPipeline->currentItem() : nullptr;
	return item ? item->data(Qt::UserRole).toString() : QString();
}

void ApplicationShell::runSelectedCompilerProfile()
{
	if (m_compilerRunThread) {
		statusBar()->showMessage(tr("A compiler task is already running"));
		return;
	}

	const QString profileId = selectedCompilerProfileId();
	CompilerProfileDescriptor profile;
	if (!compilerProfileForId(profileId, &profile)) {
		statusBar()->showMessage(tr("Select a compiler profile first"));
		return;
	}

	const QString projectPath = m_settings.currentProjectPath();
	ProjectManifest manifest;
	if (projectPath.isEmpty() || !loadProjectManifest(projectPath, &manifest)) {
		manifest = defaultProjectManifest(projectPath.isEmpty() ? QDir::currentPath() : projectPath);
	}

	CompilerRunRequest runRequest;
	runRequest.command.profileId = profile.id;
	runRequest.command.workspaceRootPath = manifest.rootPath;
	runRequest.command.inputPath = firstProjectInputForProfile(profile, manifest);
	runRequest.command.extraSearchPaths = effectiveProjectCompilerSearchPaths(manifest);
	runRequest.command.executableOverrides = effectiveProjectCompilerToolOverrides(manifest, m_settings.compilerToolPathOverrides());
	runRequest.registerOutputs = true;
	runRequest.manifestPath = QDir(manifest.rootPath).filePath(QStringLiteral(".vibestudio/compiler-runs/%1-%2.json").arg(profile.id, QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddTHHmmssZ"))));

	if (profile.inputRequired && runRequest.command.inputPath.isEmpty()) {
		const QString taskId = m_activity.createTask(tr("Compiler Run"), profile.id, tr("compiler"), OperationState::Warning, false);
		m_activity.appendWarning(taskId, tr("No %1 input file was found in the project source folders.").arg(profile.inputDescription));
		m_activity.completeTask(taskId, tr("Compiler run needs an input file."));
		persistActivityTask(taskId);
		refreshActivityCenter(taskId);
		statusBar()->showMessage(tr("Compiler input file not found"));
		return;
	}

	m_compilerRunCancelRequested.store(false);
	m_compilerRunActivityId = m_activity.createTask(tr("Compiler Run"), profile.displayName, tr("compiler"), OperationState::Running, true);
	m_activity.setProgress(m_compilerRunActivityId, 0, 4, tr("Planning compiler command."));
	refreshActivityCenter(m_compilerRunActivityId);
	statusBar()->showMessage(tr("Compiler run started: %1").arg(profile.displayName));

	const QString taskId = m_compilerRunActivityId;
	auto* thread = QThread::create([this, runRequest, taskId, projectPath]() {
		CompilerRunCallbacks callbacks;
		callbacks.cancellationRequested = [this]() {
			return m_compilerRunCancelRequested.load();
		};
		callbacks.logEntry = [this, taskId](const CompilerTaskLogEntry& entry) {
			QMetaObject::invokeMethod(this, [this, taskId, entry]() {
				m_activity.appendLog(taskId, OperationState::Running, entry.message);
				refreshActivityCenter(taskId);
			}, Qt::QueuedConnection);
		};
		CompilerRunResult result = runCompilerCommand(runRequest, callbacks);
		QMetaObject::invokeMethod(this, [this, result, taskId, projectPath]() {
			finishCompilerRun(result, taskId, projectPath);
		}, Qt::QueuedConnection);
	});
	m_compilerRunThread = thread;
	connect(thread, &QThread::finished, thread, &QObject::deleteLater);
	thread->start();
}

void ApplicationShell::finishCompilerRun(const CompilerRunResult& result, const QString& taskId, const QString& projectPath)
{
	if (m_compilerRunThread) {
		m_compilerRunThread = nullptr;
	}
	m_compilerRunActivityId.clear();
	m_compilerRunCancelRequested.store(false);

	m_activity.setProgress(taskId, 4, 4, tr("Compiler run finished."));
	for (const QString& warning : result.manifest.warnings) {
		m_activity.appendWarning(taskId, warning);
	}
	if (!result.stdoutText.trimmed().isEmpty()) {
		m_activity.appendLog(taskId, OperationState::Running, tr("Stdout: %1").arg(result.stdoutText.trimmed().left(1000)));
	}
	if (!result.stderrText.trimmed().isEmpty()) {
		m_activity.appendLog(taskId, OperationState::Warning, tr("Stderr: %1").arg(result.stderrText.trimmed().left(1000)));
	}
	if (!result.registeredOutputPaths.isEmpty()) {
		ProjectManifest manifest;
		QString error;
		if (loadProjectManifest(projectPath, &manifest, &error)) {
			registerProjectOutputPaths(&manifest, result.registeredOutputPaths);
			saveProjectManifest(manifest, &error);
		}
	}

	const QString summary = tr("%1 in %2 ms / outputs: %3 / manifest: %4")
		.arg(localizedOperationStateName(result.state))
		.arg(result.durationMs)
		.arg(result.registeredOutputPaths.isEmpty() ? tr("none") : QString::number(result.registeredOutputPaths.size()))
		.arg(nativePath(result.manifestPath));
	if (result.state == OperationState::Completed || result.state == OperationState::Warning) {
		m_activity.completeTask(taskId, summary);
	} else if (result.state == OperationState::Cancelled) {
		m_activity.transitionTask(taskId, OperationState::Running);
		m_activity.cancelTask(taskId, summary);
	} else {
		m_activity.failTask(taskId, result.error.isEmpty() ? summary : result.error);
	}

	persistActivityTask(taskId);
	refreshCompilerPipelineSummary();
	refreshWorkspaceContextPanels();
	refreshWorkspaceDashboard();
	refreshActivityCenter(taskId);
	statusBar()->showMessage(summary);
}

void ApplicationShell::copySelectedCompilerCliEquivalent()
{
	const QString profileId = selectedCompilerProfileId();
	CompilerProfileDescriptor profile;
	if (!compilerProfileForId(profileId, &profile)) {
		statusBar()->showMessage(tr("Select a compiler profile first"));
		return;
	}
	const QString projectPath = m_settings.currentProjectPath();
	ProjectManifest manifest;
	if (projectPath.isEmpty() || !loadProjectManifest(projectPath, &manifest)) {
		manifest = defaultProjectManifest(projectPath.isEmpty() ? QDir::currentPath() : projectPath);
	}
	CompilerCommandRequest request;
	request.profileId = profile.id;
	request.workspaceRootPath = manifest.rootPath;
	request.inputPath = firstProjectInputForProfile(profile, manifest);
	request.extraSearchPaths = effectiveProjectCompilerSearchPaths(manifest);
	QApplication::clipboard()->setText(compilerCliEquivalent(request));
	statusBar()->showMessage(tr("Compiler CLI equivalent copied"));
}

void ApplicationShell::copySelectedCompilerManifest()
{
	const QString profileId = selectedCompilerProfileId();
	CompilerProfileDescriptor profile;
	if (!compilerProfileForId(profileId, &profile)) {
		statusBar()->showMessage(tr("Select a compiler profile first"));
		return;
	}
	const QString projectPath = m_settings.currentProjectPath();
	ProjectManifest manifest;
	if (projectPath.isEmpty() || !loadProjectManifest(projectPath, &manifest)) {
		manifest = defaultProjectManifest(projectPath.isEmpty() ? QDir::currentPath() : projectPath);
	}
	CompilerCommandRequest request;
	request.profileId = profile.id;
	request.workspaceRootPath = manifest.rootPath;
	request.inputPath = firstProjectInputForProfile(profile, manifest);
	request.extraSearchPaths = effectiveProjectCompilerSearchPaths(manifest);
	request.executableOverrides = effectiveProjectCompilerToolOverrides(manifest, m_settings.compilerToolPathOverrides());
	const CompilerCommandManifest commandManifest = compilerCommandManifestFromPlan(buildCompilerCommandPlan(request));
	QApplication::clipboard()->setText(QString::fromUtf8(QJsonDocument(compilerCommandManifestJson(commandManifest)).toJson(QJsonDocument::Indented)));
	statusBar()->showMessage(tr("Compiler manifest copied"));
}

QString ApplicationShell::selectedPackageEntryPath() const
{
	const QListWidgetItem* item = m_packageEntries ? m_packageEntries->currentItem() : nullptr;
	return item ? item->data(Qt::UserRole).toString() : QString();
}

QStringList ApplicationShell::selectedPackageEntryPaths() const
{
	QStringList paths;
	if (!m_packageEntries) {
		return paths;
	}

	for (const QListWidgetItem* item : m_packageEntries->selectedItems()) {
		const QString path = item ? item->data(Qt::UserRole).toString() : QString();
		if (!path.isEmpty() && !paths.contains(path)) {
			paths.push_back(path);
		}
	}
	const QString currentPath = selectedPackageEntryPath();
	if (paths.isEmpty() && !currentPath.isEmpty()) {
		paths.push_back(currentPath);
	}
	return paths;
}

QString ApplicationShell::selectedPackageTreeEntryPath() const
{
	const QTreeWidgetItem* item = m_packageTree ? m_packageTree->currentItem() : nullptr;
	return item ? item->data(0, Qt::UserRole).toString() : QString();
}

void ApplicationShell::selectPackageEntryPath(const QString& virtualPath)
{
	if (!m_packageEntries || virtualPath.isEmpty()) {
		return;
	}

	// An entry outside the listed folder is revealed by browsing to it first.
	const bool searching = m_packageFilter && !m_packageFilter->text().trimmed().isEmpty();
	if (!searching && packageParentFolder(virtualPath) != m_packageBrowseFolder) {
		navigatePackageFolder(packageParentFolder(virtualPath), virtualPath);
		return;
	}

	for (int index = 0; index < m_packageEntries->count(); ++index) {
		QListWidgetItem* item = m_packageEntries->item(index);
		if (item && item->data(Qt::UserRole).toString() == virtualPath) {
			const QSignalBlocker blocker(m_packageEntries);
			m_packageEntries->clearSelection();
			m_packageEntries->setCurrentItem(item);
			item->setSelected(true);
			return;
		}
	}
}

void ApplicationShell::selectPackageTreeEntryPath(const QString& virtualPath)
{
	if (!m_packageTree || virtualPath.isEmpty()) {
		return;
	}

	const QList<QTreeWidgetItem*> matches = m_packageTree->findItems(QStringLiteral("*"), Qt::MatchWildcard | Qt::MatchRecursive, 0);
	for (QTreeWidgetItem* item : matches) {
		if (item && item->data(0, Qt::UserRole).toString() == virtualPath) {
			const QSignalBlocker blocker(m_packageTree);
			m_packageTree->setCurrentItem(item);
			m_packageTree->scrollToItem(item, QAbstractItemView::PositionAtCenter);
			return;
		}
	}
}

void ApplicationShell::extractSelectedPackageEntries()
{
	if (!m_packageArchive.isOpen()) {
		statusBar()->showMessage(tr("Open a package before extracting entries"));
		return;
	}

	QStringList paths = selectedPackageEntryPaths();
	const QString treePath = selectedPackageTreeEntryPath();
	if (paths.isEmpty() && !treePath.isEmpty()) {
		paths.push_back(treePath);
	}
	if (paths.isEmpty()) {
		statusBar()->showMessage(tr("Select at least one package entry to extract"));
		return;
	}
	extractPackageEntriesToDirectory(paths, false);
}

void ApplicationShell::extractAllPackageEntries()
{
	if (!m_packageArchive.isOpen()) {
		statusBar()->showMessage(tr("Open a package before extracting entries"));
		return;
	}
	extractPackageEntriesToDirectory({}, true);
}

void ApplicationShell::extractPackageEntriesToDirectory(const QStringList& virtualPaths, bool extractAll)
{
	const QString targetDirectory = QFileDialog::getExistingDirectory(this, extractAll ? tr("Extract All Package Entries") : tr("Extract Selected Package Entries"));
	if (targetDirectory.isEmpty()) {
		return;
	}

	const PackageArchiveSummary summary = m_packageArchive.summary();
	PackageExtractionRequest request;
	request.targetDirectory = targetDirectory;
	request.virtualPaths = virtualPaths;
	request.extractAll = extractAll || virtualPaths.isEmpty();
	request.dryRun = false;
	request.overwriteExisting = false;

	m_packageExtractionCancelRequested = false;
	if (m_packageExtractCancel) {
		m_packageExtractCancel->setEnabled(true);
	}
	if (m_packageExtractSelected) {
		m_packageExtractSelected->setEnabled(false);
	}
	if (m_packageExtractAll) {
		m_packageExtractAll->setEnabled(false);
	}

	const QString detail = tr("%1 -> %2").arg(nativePath(summary.sourcePath), nativePath(targetDirectory));
	const QString taskId = m_activity.createTask(tr("Package Extract"), detail, tr("package"), OperationState::Running, true);
	const int total = request.extractAll ? std::max(1, summary.entryCount) : std::max(1, static_cast<int>(virtualPaths.size()));
	m_activity.setProgress(taskId, 0, total, tr("Starting package extraction."));
	m_packageState->setTitle(tr("Extracting Package"));
	m_packageState->setDetail(detail);
	m_packageState->setState(OperationState::Running, tr("Running"));
	m_packageState->setProgress({0, total});
	refreshActivityCenter(taskId);

	PackageExtractionReport report = extractPackageEntries(m_packageArchive, request, [this, taskId](const PackageExtractionEntryResult& result, const PackageExtractionReport& progressReport) {
		const int totalEntries = std::max(1, progressReport.requestedCount);
		m_activity.setProgress(taskId, progressReport.processedCount + progressReport.errorCount, totalEntries, tr("Processed %1 of %2 entries.").arg(progressReport.processedCount + progressReport.errorCount).arg(totalEntries));
		m_activity.appendLog(taskId, OperationState::Running, result.outputPath.isEmpty()
			? tr("Processed %1").arg(result.virtualPath)
			: tr("%1 -> %2").arg(result.virtualPath, nativePath(result.outputPath)));
		m_packageState->setTitle(tr("Extracting Package"));
		m_packageState->setState(OperationState::Running, tr("Running"));
		m_packageState->setProgress({progressReport.processedCount + progressReport.errorCount, totalEntries});
		refreshActivityCenter(taskId);
		QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 25);
		return !m_packageExtractionCancelRequested;
	});

	if (m_packageExtractCancel) {
		m_packageExtractCancel->setEnabled(false);
	}
	if (m_packageExtractSelected) {
		m_packageExtractSelected->setEnabled(true);
	}
	if (m_packageExtractAll) {
		m_packageExtractAll->setEnabled(true);
	}

	const QString resultSummary = report.cancelled
		? tr("Cancelled after %1 of %2 entries.").arg(report.processedCount + report.errorCount).arg(report.requestedCount)
		: tr("Extracted %1 entries to %2.").arg(report.writtenCount).arg(nativePath(report.targetDirectory));
	if (report.cancelled) {
		m_activity.transitionTask(taskId, OperationState::Running, tr("Cancellation requested."));
		m_activity.cancelTask(taskId, resultSummary);
		for (const QString& warning : report.warnings) {
			m_activity.appendLog(taskId, OperationState::Cancelled, warning);
		}
	} else if (!report.succeeded()) {
		for (const QString& warning : report.warnings) {
			m_activity.appendWarning(taskId, warning);
		}
		m_activity.failTask(taskId, resultSummary);
	} else {
		for (const QString& warning : report.warnings) {
			m_activity.appendWarning(taskId, warning);
		}
		m_activity.completeTask(taskId, resultSummary);
	}

	const OperationState finalState = report.cancelled ? OperationState::Cancelled : (report.succeeded() ? (report.warnings.isEmpty() ? OperationState::Completed : OperationState::Warning) : OperationState::Failed);
	m_packageState->setTitle(report.cancelled ? tr("Extraction Cancelled") : (report.succeeded() ? tr("Extraction Complete") : tr("Extraction Failed")));
	m_packageState->setDetail(resultSummary);
	m_packageState->setState(finalState, localizedOperationStateName(finalState));
	m_packageState->setProgress({std::min(report.processedCount + report.errorCount, std::max(1, report.requestedCount)), std::max(1, report.requestedCount)});
	showPackageExtractionReport(report);
	persistActivityTask(taskId);
	refreshActivityCenter(taskId);
	statusBar()->showMessage(resultSummary);
}

void ApplicationShell::showPackageExtractionReport(const PackageExtractionReport& report)
{
	if (!m_packageDrawer) {
		return;
	}

	QStringList summaryLines;
	summaryLines << tr("Source: %1").arg(nativePath(report.sourcePath));
	summaryLines << tr("Target: %1").arg(nativePath(report.targetDirectory));
	summaryLines << tr("Mode: %1").arg(report.dryRun ? tr("dry run") : tr("write"));
	summaryLines << tr("Overwrite existing: %1").arg(report.overwriteExisting ? tr("yes") : tr("no"));
	summaryLines << tr("Requested: %1").arg(report.requestedCount);
	summaryLines << tr("Processed: %1").arg(report.processedCount + report.errorCount);
	summaryLines << tr("Written: %1").arg(report.writtenCount);
	summaryLines << tr("Skipped: %1").arg(report.skippedCount);
	summaryLines << tr("Errors: %1").arg(report.errorCount);
	summaryLines << tr("Bytes written: %1").arg(byteSizeText(report.totalBytes));

	QStringList outputLines;
	for (const PackageExtractionEntryResult& result : report.entries) {
		QString state = tr("planned");
		if (!result.error.isEmpty()) {
			state = tr("failed");
		} else if (result.skipped) {
			state = tr("skipped");
		} else if (result.dryRun) {
			state = result.kind == PackageEntryKind::Directory ? tr("would create") : tr("would write");
		} else if (result.kind == PackageEntryKind::Directory && result.written) {
			state = tr("created");
		} else if (result.written) {
			state = tr("wrote");
		}
		outputLines << tr("%1: %2 -> %3").arg(state, result.virtualPath.isEmpty() ? tr("package") : result.virtualPath, result.outputPath.isEmpty() ? tr("no output path") : nativePath(result.outputPath));
		if (!result.message.isEmpty()) {
			outputLines << tr("  %1").arg(result.message);
		}
		if (!result.error.isEmpty()) {
			outputLines << tr("  Error: %1").arg(result.error);
		}
	}
	if (outputLines.isEmpty()) {
		outputLines << tr("No output paths were produced.");
	}

	QStringList warningLines = report.warnings;
	if (warningLines.isEmpty()) {
		warningLines << tr("No extraction warnings.");
	}

	const OperationState state = report.cancelled ? OperationState::Cancelled : (report.succeeded() ? (report.warnings.isEmpty() ? OperationState::Completed : OperationState::Warning) : OperationState::Failed);
	m_packageDrawer->setTitle(tr("Package Extraction"));
	m_packageDrawer->setSubtitle(nativePath(report.targetDirectory));
	m_packageDrawer->setSections({
		{QStringLiteral("summary"), tr("Summary"), tr("%1 entries written").arg(report.writtenCount), summaryLines.join('\n'), state},
		{QStringLiteral("outputs"), tr("Output Paths"), tr("%n paths", nullptr, report.entries.size()), outputLines.join('\n'), state},
		{QStringLiteral("warnings"), tr("Warnings"), tr("%n warnings", nullptr, report.warnings.size()), warningLines.join('\n'), report.warnings.isEmpty() ? OperationState::Completed : OperationState::Warning},
		{QStringLiteral("raw"), tr("Raw Report"), report.cancelled ? tr("cancelled") : (report.succeeded() ? tr("completed") : tr("failed")), packageExtractionReportText(report), state},
	});
	m_packageDrawer->showSection(QStringLiteral("outputs"));
}

bool ApplicationShell::choosePackageStageResolution(const QString& title, PackageStageConflictResolution* resolution)
{
	if (!resolution) {
		return false;
	}
	const QString block = tr("Block on conflict");
	const QString replace = tr("Replace existing");
	const QString skip = tr("Skip conflicting operation");
	bool accepted = false;
	const QString choice = QInputDialog::getItem(
		this,
		title,
		tr("Conflict policy:"),
		{block, replace, skip},
		0,
		false,
		&accepted);
	if (!accepted) {
		return false;
	}
	if (choice == replace) {
		*resolution = PackageStageConflictResolution::ReplaceExisting;
	} else if (choice == skip) {
		*resolution = PackageStageConflictResolution::Skip;
	} else {
		*resolution = PackageStageConflictResolution::Block;
	}
	return true;
}

void ApplicationShell::stagePackageAddFile()
{
	if (!m_packageStaging.isLoaded()) {
		statusBar()->showMessage(tr("Open a package before staging files"));
		return;
	}

	const QString sourcePath = QFileDialog::getOpenFileName(this, tr("Stage File Into Package"));
	if (sourcePath.isEmpty()) {
		return;
	}
	bool accepted = false;
	const QString defaultVirtualPath = QFileInfo(sourcePath).fileName();
	const QString virtualPath = QInputDialog::getText(this, tr("Stage Add"), tr("Virtual package path:"), QLineEdit::Normal, defaultVirtualPath, &accepted).trimmed();
	if (!accepted || virtualPath.isEmpty()) {
		return;
	}
	PackageStageConflictResolution resolution = PackageStageConflictResolution::Block;
	if (!choosePackageStageResolution(tr("Stage Add Conflict Policy"), &resolution)) {
		return;
	}

	QString error;
	if (!m_packageStaging.addFile(sourcePath, virtualPath, &error, resolution)) {
		statusBar()->showMessage(tr("Stage add blocked: %1").arg(error));
	}
	refreshPackageStagingSummary();
	recordActivity(tr("Package Stage Add"), virtualPath, tr("package"), error.isEmpty() ? OperationState::Completed : OperationState::Failed, error.isEmpty() ? tr("File staged for save-as.") : error);
}

void ApplicationShell::stagePackageReplaceSelected()
{
	if (!m_packageStaging.isLoaded()) {
		statusBar()->showMessage(tr("Open a package before staging replacements"));
		return;
	}
	const QString virtualPath = selectedPackageEntryPath();
	if (virtualPath.isEmpty()) {
		statusBar()->showMessage(tr("Select a package entry to replace"));
		return;
	}
	const QString sourcePath = QFileDialog::getOpenFileName(this, tr("Choose Replacement File"));
	if (sourcePath.isEmpty()) {
		return;
	}

	QString error;
	if (!m_packageStaging.replaceFile(virtualPath, sourcePath, &error)) {
		statusBar()->showMessage(tr("Stage replace blocked: %1").arg(error));
	}
	refreshPackageStagingSummary();
	recordActivity(tr("Package Stage Replace"), virtualPath, tr("package"), error.isEmpty() ? OperationState::Completed : OperationState::Failed, error.isEmpty() ? tr("Replacement staged for save-as.") : error);
}

void ApplicationShell::stagePackageRenameSelected()
{
	if (!m_packageStaging.isLoaded()) {
		statusBar()->showMessage(tr("Open a package before staging renames"));
		return;
	}
	const QString virtualPath = selectedPackageEntryPath();
	if (virtualPath.isEmpty()) {
		statusBar()->showMessage(tr("Select a package entry to rename"));
		return;
	}
	bool accepted = false;
	const QString targetVirtualPath = QInputDialog::getText(this, tr("Stage Rename"), tr("New virtual package path:"), QLineEdit::Normal, virtualPath, &accepted).trimmed();
	if (!accepted || targetVirtualPath.isEmpty()) {
		return;
	}
	PackageStageConflictResolution resolution = PackageStageConflictResolution::Block;
	if (!choosePackageStageResolution(tr("Stage Rename Conflict Policy"), &resolution)) {
		return;
	}

	QString error;
	if (!m_packageStaging.renameEntry(virtualPath, targetVirtualPath, &error, resolution)) {
		statusBar()->showMessage(tr("Stage rename blocked: %1").arg(error));
	}
	refreshPackageStagingSummary();
	recordActivity(tr("Package Stage Rename"), tr("%1 -> %2").arg(virtualPath, targetVirtualPath), tr("package"), error.isEmpty() ? OperationState::Completed : OperationState::Failed, error.isEmpty() ? tr("Rename staged for save-as.") : error);
}

void ApplicationShell::stagePackageDeleteSelected()
{
	if (!m_packageStaging.isLoaded()) {
		statusBar()->showMessage(tr("Open a package before staging deletes"));
		return;
	}
	const QStringList virtualPaths = selectedPackageEntryPaths();
	if (virtualPaths.isEmpty()) {
		statusBar()->showMessage(tr("Select package entries to delete"));
		return;
	}

	QStringList errors;
	for (const QString& virtualPath : virtualPaths) {
		QString error;
		if (!m_packageStaging.deleteEntry(virtualPath, &error, PackageStageConflictResolution::Block)) {
			errors << QStringLiteral("%1: %2").arg(virtualPath, error);
		}
	}
	refreshPackageStagingSummary();
	const bool succeeded = errors.isEmpty();
	recordActivity(tr("Package Stage Delete"), virtualPaths.join(QStringLiteral("; ")), tr("package"), succeeded ? OperationState::Completed : OperationState::Failed, succeeded ? tr("Delete staged for save-as.") : errors.join(QStringLiteral("; ")));
	if (!succeeded) {
		statusBar()->showMessage(tr("Stage delete blocked: %1").arg(errors.join(QStringLiteral("; "))));
	}
}

void ApplicationShell::comparePackageWithFile()
{
	if (!m_packageArchive.isOpen()) {
		statusBar()->showMessage(tr("Open a package before comparing it with another."));
		return;
	}

	const QString otherPath = QFileDialog::getOpenFileName(
		this,
		tr("Compare With Package"),
		QFileInfo(m_packageArchive.sourcePath()).absolutePath(),
		tr("Package Archives (*.pak *.pk3 *.zip *.wad);;All Files (*)"));
	if (otherPath.isEmpty()) {
		return;
	}

	PackageArchive other;
	QString error;
	if (!other.load(otherPath, &error)) {
		statusBar()->showMessage(tr("Unable to open %1: %2").arg(nativePath(otherPath), error));
		return;
	}

	PackageCompareRequest request;
	request.leftLabel = QFileInfo(m_packageArchive.sourcePath()).fileName();
	request.rightLabel = QFileInfo(otherPath).fileName();

	const QString detail = tr("%1 vs %2").arg(nativePath(m_packageArchive.sourcePath()), nativePath(otherPath));
	const QString taskId = m_activity.createTask(tr("Package Compare"), detail, tr("package"), OperationState::Running, false);
	refreshActivityCenter(taskId);

	const PackageCompareResult result = comparePackages(m_packageArchive, other, request);
	for (const QString& warning : result.warnings) {
		m_activity.appendWarning(taskId, warning);
	}

	const int differences = result.summary.addedCount + result.summary.removedCount
		+ result.summary.changedCount + result.summary.caseOnlyCount;
	if (result.identical()) {
		m_activity.completeTask(taskId, tr("The two packages hold the same %n entries.", nullptr, result.summary.leftCount));
	} else {
		// A difference is the answer to the question, not a failure of the
		// command, so this completes with a warning rather than failing.
		m_activity.completeTask(taskId, tr("%n difference(s) between the two packages.", nullptr, differences));
	}
	persistActivityTask(taskId);

	QStringList summaryLines;
	summaryLines << tr("Left: %1").arg(nativePath(result.leftSource));
	summaryLines << tr("Right: %1").arg(nativePath(result.rightSource));
	summaryLines << tr("Added: %1").arg(result.summary.addedCount);
	summaryLines << tr("Removed: %1").arg(result.summary.removedCount);
	summaryLines << tr("Changed: %1").arg(result.summary.changedCount);
	summaryLines << tr("Case-only: %1").arg(result.summary.caseOnlyCount);
	summaryLines << tr("Identical: %1").arg(result.summary.identicalCount);
	summaryLines << tr("Not compared: %1").arg(result.summary.uncomparedCount);
	summaryLines << tr("Size delta: %1").arg(byteSizeText(std::llabs(result.summary.sizeDelta)));

	if (m_packageDrawer) {
		m_packageDrawer->setTitle(tr("Package Comparison"));
		m_packageDrawer->setSubtitle(detail);
		m_packageDrawer->setSections({
			{QStringLiteral("summary"), tr("Summary"), tr("Comparison totals"), summaryLines.join('\n'),
				result.identical() ? OperationState::Completed : OperationState::Warning},
			{QStringLiteral("entries"), tr("Entries"), tr("Per-entry comparison"), packageCompareLines(result).join('\n'),
				result.identical() ? OperationState::Completed : OperationState::Warning},
			{QStringLiteral("warnings"), tr("Warnings"), tr("Reader warnings raised while comparing"),
				result.warnings.isEmpty() ? tr("No comparison warnings.") : result.warnings.join('\n'),
				result.warnings.isEmpty() ? OperationState::Completed : OperationState::Warning},
		});
		m_packageDrawer->showSection(QStringLiteral("summary"));
	}

	recordActivity(tr("Packages compared"), detail, QStringLiteral("package"),
		result.identical() ? OperationState::Completed : OperationState::Warning,
		result.identical() ? tr("Identical") : tr("%n difference(s)", nullptr, differences));
	statusBar()->showMessage(result.identical()
		? tr("The two packages are identical.")
		: tr("%n difference(s) between the two packages.", nullptr, differences));
}

void ApplicationShell::saveStagedPackageAs()
{
	if (!m_packageStaging.isLoaded()) {
		statusBar()->showMessage(tr("Open a package before saving staged output"));
		return;
	}

	const QString outputPath = QFileDialog::getSaveFileName(
		this,
		tr("Save Staged Package As"),
		QString(),
		tr("Package Archives (*.pak *.pk3 *.zip *.wad);;PAK Packages (*.pak);;PK3 Packages (*.pk3);;ZIP Packages (*.zip);;WAD Packages (*.wad);;All Files (*)"));
	if (outputPath.isEmpty()) {
		return;
	}

	PackageWriteRequest request;
	request.destinationPath = outputPath;
	request.format = packageArchiveFormatFromFileName(outputPath);
	request.allowOverwrite = false;
	request.writeManifest = true;
	if (m_packageCompression) {
		DeflateLevel level = DeflateLevel::Default;
		if (deflateLevelFromId(m_packageCompression->currentData().toString(), &level)) {
			request.compression = level;
		}
	}

	// Writing over a file the user already has is the one destructive thing this
	// command can do, so it is never the default: the writer builds the archive
	// beside the target and only swaps it in after the bytes verify, and the old
	// file is kept as a backup rather than discarded.
	if (QFileInfo::exists(outputPath)) {
		const QString backupPath = outputPath + QStringLiteral(".bak");
		const bool replacingSource = QFileInfo(outputPath).canonicalFilePath()
			== QFileInfo(m_packageStaging.sourcePath()).canonicalFilePath();
		const QMessageBox::StandardButton choice = QMessageBox::question(
			this,
			tr("Replace Existing Package?"),
			replacingSource
				? tr("%1 is the package that is open.\n\nThe new archive is written beside it and verified first; only then does the original move to %2. Replace it?")
					.arg(nativePath(outputPath), nativePath(backupPath))
				: tr("%1 already exists.\n\nThe new archive is written beside it and verified first; only then does the original move to %2. Replace it?")
					.arg(nativePath(outputPath), nativePath(backupPath)),
			QMessageBox::Yes | QMessageBox::No,
			QMessageBox::No);
		if (choice != QMessageBox::Yes) {
			statusBar()->showMessage(tr("Package save-as cancelled; nothing was written."));
			return;
		}
		request.allowInPlaceOverwrite = true;
		request.backupPath = backupPath;
	}
	const QFileInfo outputInfo(outputPath);
	request.manifestPath = QDir(outputInfo.absolutePath()).filePath(QStringLiteral("%1.manifest.json").arg(outputInfo.completeBaseName()));

	const QString detail = tr("%1 -> %2").arg(nativePath(m_packageStaging.sourcePath()), nativePath(outputPath));
	const QString taskId = m_activity.createTask(tr("Package Save As"), detail, tr("package"), OperationState::Running, false);
	m_activity.setProgress(taskId, 0, 1, tr("Writing staged package."));
	m_packageState->setTitle(tr("Saving Package"));
	m_packageState->setDetail(detail);
	m_packageState->setState(OperationState::Running, tr("Running"));
	m_packageState->setProgress({0, 1});
	refreshActivityCenter(taskId);

	const PackageWriteReport report = m_packageStaging.writeArchive(request);
	for (const QString& warning : report.warnings) {
		m_activity.appendWarning(taskId, warning);
	}
	for (const QString& blocker : report.blockedMessages) {
		m_activity.appendWarning(taskId, blocker);
	}

	const OperationState finalState = report.succeeded()
		? (report.warnings.isEmpty() ? OperationState::Completed : OperationState::Warning)
		: OperationState::Failed;
	if (report.succeeded()) {
		m_activity.completeTask(taskId, tr("Saved %1 with %n entries.", nullptr, report.entryCount).arg(nativePath(report.outputPath)));
	} else {
		m_activity.failTask(taskId, report.blockedMessages.isEmpty() ? tr("Package save-as failed.") : report.blockedMessages.join(QStringLiteral("; ")));
	}
	persistActivityTask(taskId);

	QStringList summaryLines;
	summaryLines << tr("Source: %1").arg(nativePath(report.sourcePath));
	summaryLines << tr("Output: %1").arg(report.outputPath.isEmpty() ? nativePath(outputPath) : nativePath(report.outputPath));
	summaryLines << tr("Mode: %1").arg(report.dryRun ? tr("dry run") : tr("write"));
	summaryLines << tr("Format: %1").arg(localizedPackageFormatName(report.format));
	summaryLines << tr("Entries: %1").arg(report.entryCount);
	summaryLines << tr("Bytes written: %1").arg(byteSizeText(report.bytesWritten));
	summaryLines << tr("SHA-256: %1").arg(report.sha256.isEmpty() ? tr("not written") : report.sha256);
	summaryLines << tr("Deterministic writer: %1").arg(report.deterministic ? tr("yes") : tr("no"));
	summaryLines << tr("Compression: %1").arg(deflateLevelId(request.compression));
	summaryLines << tr("Manifest: %1").arg(report.wroteManifest ? nativePath(report.manifestPath) : tr("not written"));
	if (report.overwroteInPlace) {
		summaryLines << tr("Replaced in place: yes");
		summaryLines << tr("Backup: %1").arg(report.backupPath.isEmpty() ? tr("not kept") : nativePath(report.backupPath));
	}

	QStringList warningLines = report.warnings;
	if (warningLines.isEmpty()) {
		warningLines << tr("No writer warnings.");
	}
	QStringList blockerLines = report.blockedMessages;
	if (blockerLines.isEmpty()) {
		blockerLines << tr("No save-as blockers.");
	}

	m_packageDrawer->setTitle(tr("Package Save As"));
	m_packageDrawer->setSubtitle(nativePath(outputPath));
	m_packageDrawer->setSections({
		{QStringLiteral("summary"), tr("Summary"), report.succeeded() ? tr("saved") : tr("blocked"), summaryLines.join('\n'), finalState},
		{QStringLiteral("blockers"), tr("Blockers"), tr("%n blockers", nullptr, report.blockedMessages.size()), blockerLines.join('\n'), report.blockedMessages.isEmpty() ? OperationState::Completed : OperationState::Failed},
		{QStringLiteral("warnings"), tr("Warnings"), tr("%n warnings", nullptr, report.warnings.size()), warningLines.join('\n'), report.warnings.isEmpty() ? OperationState::Completed : OperationState::Warning},
		{QStringLiteral("raw"), tr("Raw Report"), report.succeeded() ? tr("completed") : tr("failed"), packageWriteReportText(report), finalState},
	});
	m_packageDrawer->showSection(report.succeeded() ? QStringLiteral("summary") : QStringLiteral("blockers"));
	m_packageState->setTitle(report.succeeded() ? tr("Save Complete") : tr("Save Blocked"));
	m_packageState->setDetail(report.succeeded() ? nativePath(report.outputPath) : blockerLines.join(QStringLiteral("; ")));
	m_packageState->setState(finalState, localizedOperationStateName(finalState));
	m_packageState->setProgress({report.succeeded() ? 1 : 0, 1});
	refreshPackageStagingSummary();
	refreshActivityCenter(taskId);
	statusBar()->showMessage(report.succeeded() ? tr("Package saved: %1").arg(nativePath(report.outputPath)) : tr("Package save-as blocked"));
}

void ApplicationShell::activateRecentProject(QListWidgetItem* item)
{
	if (!item) {
		return;
	}

	const QString path = item->data(Qt::UserRole).toString();
	if (path.isEmpty()) {
		return;
	}

	if (!QFileInfo::exists(path)) {
		recordActivity(tr("Open Recent Project"), nativePath(path), tr("project"), OperationState::Warning, tr("Recent project path is missing."), {tr("The remembered folder could not be found.")});
		statusBar()->showMessage(tr("Recent project path is missing: %1").arg(nativePath(path)));
		updateInspectorForProject(path);
		return;
	}

	m_settings.recordRecentProject(path, item->text().section('\n', 0, 0).section(" [", 0, 0));
	m_settings.setCurrentProjectPath(path);
	m_settings.sync();
	recordActivity(tr("Open Recent Project"), nativePath(path), tr("project"), OperationState::Completed, tr("Project folder ready."));
	refreshSetupPanel();
	refreshRecentProjects();
	refreshWorkspaceDashboard();
	applyPreferencesToUi();
	updateInspectorForProject(path);
	statusBar()->showMessage(tr("Project folder ready: %1").arg(nativePath(path)));
}

void ApplicationShell::removeSelectedRecentProject()
{
	const QListWidgetItem* item = m_recentProjects->currentItem();
	if (!item) {
		return;
	}

	const QString path = item->data(Qt::UserRole).toString();
	if (path.isEmpty()) {
		return;
	}

	m_settings.removeRecentProject(path);
	if (m_settings.currentProjectPath() == normalizedProjectPath(path)) {
		m_settings.setCurrentProjectPath(QString());
	}
	m_settings.sync();
	recordActivity(tr("Remove Recent Project"), nativePath(path), tr("project"), OperationState::Completed, tr("Recent project removed from settings."));
	refreshSetupPanel();
	refreshRecentProjects();
	refreshWorkspaceDashboard();
	applyPreferencesToUi();
	updateInspector();
	statusBar()->showMessage(tr("Recent project removed: %1").arg(nativePath(path)));
}

void ApplicationShell::clearRecentProjects()
{
	m_settings.clearRecentProjects();
	m_settings.setCurrentProjectPath(QString());
	m_settings.sync();
	recordActivity(tr("Clear Recent Projects"), tr("Recent project list"), tr("project"), OperationState::Completed, tr("Recent project list cleared."));
	refreshSetupPanel();
	refreshRecentProjects();
	refreshWorkspaceDashboard();
	applyPreferencesToUi();
	updateInspector();
	statusBar()->showMessage(tr("Recent projects cleared"));
}

void ApplicationShell::refreshSetupPanel()
{
	const SetupProgress progress = m_settings.setupProgress();
	const SetupSummary summary = m_settings.setupSummary();
	const QVector<SetupStep> steps = setupSteps();
	const int currentValue = progress.completed ? steps.size() : setupStepProgressValue(progress.currentStep);

	m_setupStatus->setText(tr("Setup: %1").arg(setupStatusDisplayName(summary.status)));
	m_setupStep->setText(tr("Current step: %1").arg(summary.currentStepName));
	m_setupStep->setToolTip(summary.currentStepId);
	m_setupProgress->setRange(0, steps.size());
	m_setupProgress->setValue(currentValue);
	m_setupProgress->setFormat(tr("%1 of %2").arg(currentValue).arg(steps.size()));
	m_setupNextAction->setText(summary.nextAction);

	// A stepper: each step once, in order, with its state, followed by any
	// setup warnings. The step setup would resume at is marked current.
	m_setupSummary->clear();
	const int currentIndex = std::max(0, static_cast<int>(steps.indexOf(progress.currentStep)));
	const bool counting = progress.started && !progress.skipped;
	for (int index = 0; index < steps.size(); ++index) {
		const SetupStep step = steps.at(index);
		const bool done = progress.completed || (counting && index < currentIndex);
		const bool current = !progress.completed && index == currentIndex;
		const QString name = setupStepDisplayName(step);
		auto* item = new QListWidgetItem(QStringLiteral("%1\n%2").arg(name, setupStepDescription(step)));
		item->setIcon(done ? studioIcon(QStringLiteral("check"), StudioIconTone::Success)
			: (current ? studioIcon(QStringLiteral("chevron-right"), StudioIconTone::Accent) : studioIcon(QStringLiteral("dot"), StudioIconTone::Muted)));
		item->setData(Qt::UserRole, done ? QStringLiteral("done") : (current ? QStringLiteral("current") : QStringLiteral("pending")));
		const QString state = done ? tr("done") : (current ? tr("current step") : tr("pending"));
		item->setData(Qt::AccessibleTextRole, tr("Step %1 of %2, %3: %4").arg(index + 1).arg(steps.size()).arg(state, name));
		item->setToolTip(tr("Step %1 of %2 (%3)").arg(index + 1).arg(steps.size()).arg(state));
		if (!done && !current) {
			// Steps still ahead recede; applyStateColors() re-tints them when
			// the theme changes.
			item->setForeground(currentStudioTheme().colors.textMuted);
		}
		m_setupSummary->addItem(item);
	}
	for (const QString& warning : summary.warnings) {
		auto* item = new QListWidgetItem(studioIcon(QStringLiteral("warning"), StudioIconTone::Warning), warning);
		item->setData(Qt::UserRole, QStringLiteral("warning"));
		item->setData(Qt::AccessibleTextRole, tr("Warning: %1").arg(warning));
		m_setupSummary->addItem(item);
	}
	// Tall enough to show every step and warning without an inner scroll bar;
	// the Settings page scrolls as a whole if the window is shorter.
	int contentHeight = 2 * m_setupSummary->frameWidth() + 6;
	for (int row = 0; row < m_setupSummary->count(); ++row) {
		contentHeight += m_setupSummary->sizeHintForRow(row);
	}
	m_setupSummary->setMinimumHeight(std::clamp(contentHeight, 200, 720));

	const bool complete = progress.completed;
	const bool skipped = progress.skipped;
	const bool started = progress.started && !skipped && !complete;
	m_setupStartResume->setText(complete ? tr("Review") : (skipped ? tr("Resume") : (started ? tr("Resume") : tr("Start"))));
	m_setupNext->setEnabled(started);
	m_setupSkip->setEnabled(!complete && !skipped);
	m_setupComplete->setEnabled(started || skipped);
	m_setupReset->setEnabled(progress.started || skipped || complete);
	updateSetupActivity();
}

void ApplicationShell::startOrResumeSetup()
{
	const SetupProgress progress = m_settings.setupProgress();
	m_settings.startOrResumeSetup(progress.currentStep);
	m_settings.sync();
	recordActivity(tr("First-Run Setup"), setupStepDisplayName(progress.currentStep), tr("setup"), OperationState::Running, tr("Setup resumed."));
	refreshSetupPanel();
	updateInspector();
	statusBar()->showMessage(tr("Setup resumed: %1").arg(setupStepDisplayName(progress.currentStep)));
}

void ApplicationShell::advanceSetup()
{
	m_settings.advanceSetup();
	m_settings.sync();
	refreshSetupPanel();
	updateInspector();
	const SetupProgress progress = m_settings.setupProgress();
	recordActivity(tr("First-Run Setup"), setupStepDisplayName(progress.currentStep), tr("setup"), progress.completed ? OperationState::Completed : OperationState::Running, progress.completed ? tr("Setup completed.") : tr("Setup advanced."));
	statusBar()->showMessage(tr("Setup step: %1").arg(setupStepDisplayName(progress.currentStep)));
}

void ApplicationShell::skipSetup()
{
	m_settings.skipSetup();
	m_settings.sync();
	recordActivity(tr("First-Run Setup"), tr("Skipped for now"), tr("setup"), OperationState::Warning, tr("Setup skipped for now."), {tr("Setup can be resumed later.")});
	refreshSetupPanel();
	updateInspector();
	statusBar()->showMessage(tr("Setup skipped for now"));
}

void ApplicationShell::completeSetup()
{
	m_settings.completeSetup();
	m_settings.sync();
	recordActivity(tr("First-Run Setup"), tr("Review And Finish"), tr("setup"), OperationState::Completed, tr("Setup marked complete."));
	refreshSetupPanel();
	updateInspector();
	statusBar()->showMessage(tr("Setup marked complete"));
}

void ApplicationShell::resetSetup()
{
	m_settings.resetSetup();
	m_settings.sync();
	recordActivity(tr("First-Run Setup"), tr("Setup progress reset"), tr("setup"), OperationState::Completed, tr("Setup progress reset."));
	refreshSetupPanel();
	updateInspector();
	statusBar()->showMessage(tr("Setup progress reset"));
}

void ApplicationShell::seedActivityCenter()
{
	m_settingsActivityId = m_activity.createTask(tr("Settings Storage"), nativePath(m_settings.storageLocation()), tr("settings"), OperationState::Running, false);
	if (m_settings.status() == QSettings::NoError) {
		m_activity.completeTask(m_settingsActivityId, tr("Settings loaded successfully."));
	} else {
		m_activity.failTask(m_settingsActivityId, tr("Settings storage reported %1.").arg(settingsStatusText(m_settings.status())));
	}

	recordActivity(tr("Activity Center"), tr("Global task list, progress, results, warnings, failures, and cancellation."), tr("shell"), OperationState::Completed, tr("Activity center ready."));

	const SetupSummary setup = m_settings.setupSummary();
	OperationState setupState = OperationState::Idle;
	if (setup.status == QStringLiteral("complete")) {
		setupState = OperationState::Completed;
	} else if (setup.status == QStringLiteral("skipped") || !setup.warnings.isEmpty()) {
		setupState = OperationState::Warning;
	} else if (setup.status == QStringLiteral("in-progress")) {
		setupState = OperationState::Running;
	}
	m_setupActivityId = m_activity.createTask(tr("First-Run Setup"), setup.currentStepName, tr("setup"), setupState, setupState == OperationState::Running);
	for (const QString& warning : setup.warnings) {
		m_activity.appendWarning(m_setupActivityId, warning);
	}
	if (setupState == OperationState::Completed) {
		m_activity.completeTask(m_setupActivityId, setup.nextAction);
	} else if (setupState == OperationState::Idle || setupState == OperationState::Running) {
		m_activity.appendLog(m_setupActivityId, setupState, setup.nextAction);
	}
	refreshActivityCenter(m_setupActivityId);
}

void ApplicationShell::recordActivity(const QString& title, const QString& detail, const QString& source, OperationState state, const QString& resultSummary, const QStringList& warnings)
{
	const bool cancellable = operationStateAllowsCancellation(state);
	const QString taskId = m_activity.createTask(title, detail, source, operationStateAllowsCancellation(state) ? state : OperationState::Queued, cancellable);
	for (const QString& warning : warnings) {
		m_activity.appendWarning(taskId, warning);
	}

	switch (state) {
	case OperationState::Completed:
		m_activity.completeTask(taskId, resultSummary);
		break;
	case OperationState::Warning:
		if (warnings.isEmpty()) {
			m_activity.appendWarning(taskId, resultSummary.trimmed().isEmpty() ? detail : resultSummary);
		}
		m_activity.completeTask(taskId, resultSummary);
		break;
	case OperationState::Failed:
		m_activity.failTask(taskId, resultSummary);
		break;
	case OperationState::Cancelled:
		m_activity.transitionTask(taskId, OperationState::Running);
		m_activity.cancelTask(taskId, resultSummary);
		break;
	case OperationState::Running:
	case OperationState::Loading:
	case OperationState::Queued:
	case OperationState::Idle:
		m_activity.transitionTask(taskId, state, resultSummary);
		break;
	}

	persistActivityTask(taskId);
	refreshActivityCenter(taskId);
	refreshRecentActivityTimeline();
}

void ApplicationShell::persistActivityTask(const QString& taskId)
{
	const OperationTask task = m_activity.task(taskId);
	if (task.id.isEmpty() || !operationStateIsTerminal(task.state)) {
		return;
	}

	RecentActivityTask recentTask;
	recentTask.id = task.id;
	recentTask.title = task.title;
	recentTask.detail = task.detail;
	recentTask.source = task.source;
	recentTask.state = task.state;
	recentTask.resultSummary = task.resultSummary;
	recentTask.warnings = task.warnings;
	recentTask.createdUtc = task.createdUtc;
	recentTask.updatedUtc = task.updatedUtc;
	recentTask.finishedUtc = task.finishedUtc;
	m_settings.recordRecentActivityTask(recentTask);
	m_settings.sync();
}

void ApplicationShell::updateSetupActivity()
{
	if (m_setupActivityId.isEmpty() || !m_activity.contains(m_setupActivityId)) {
		return;
	}

	const SetupSummary setup = m_settings.setupSummary();
	OperationState state = OperationState::Idle;
	if (setup.status == QStringLiteral("complete")) {
		state = OperationState::Completed;
	} else if (setup.status == QStringLiteral("skipped") || !setup.warnings.isEmpty()) {
		state = OperationState::Warning;
	} else if (setup.status == QStringLiteral("in-progress")) {
		state = OperationState::Running;
	}
	const OperationTask task = m_activity.task(m_setupActivityId);
	if (task.state != state) {
		m_activity.transitionTask(m_setupActivityId, state, setup.nextAction);
	}
	persistActivityTask(m_setupActivityId);
	refreshActivityCenter(m_setupActivityId);
	refreshRecentActivityTimeline();
}

void ApplicationShell::refreshActivityCenter(const QString& preferredTaskId)
{
	if (!m_activityTasks) {
		return;
	}

	const QString selectedId = preferredTaskId.isEmpty() ? selectedActivityTaskId() : preferredTaskId;
	m_activitySummary->setText(m_activity.summaryText());
	if (m_activityToggle) {
		int busy = 0;
		for (const OperationTask& task : m_activity.tasks()) {
			if (!operationStateIsTerminal(task.state) && task.state != OperationState::Idle) {
				++busy;
			}
		}
		m_activityToggle->setText(busy > 0 ? tr("Activity (%n running)", nullptr, busy) : tr("Activity"));
		m_activityToggle->setAccessibleDescription(m_activity.summaryText());
	}
	m_activityTasks->clear();

	int selectedRow = -1;
	const QVector<OperationTask> tasks = m_activity.tasks();
	for (int index = 0; index < tasks.size(); ++index) {
		const OperationTask& task = tasks[index];
		const QString detail = task.detail.isEmpty() ? task.source : task.detail;
		auto* item = new QListWidgetItem(QStringLiteral("%1  %2\n%3")
			.arg(studioStateGlyph(task.state), task.title, detail));
		item->setToolTip(QStringLiteral("%1 (%2)\n%3").arg(task.title, localizedOperationStateName(task.state), detail));
		item->setData(Qt::AccessibleTextRole, QStringLiteral("%1, %2. %3").arg(task.title, localizedOperationStateName(task.state), detail));
		item->setData(Qt::UserRole, task.id);
		item->setData(Qt::UserRole + 1, operationStateId(task.state));
		if (task.id == selectedId) {
			selectedRow = index;
		}
		m_activityTasks->addItem(item);
	}

	if (tasks.isEmpty()) {
		auto* item = new QListWidgetItem(tr("No activity"));
		item->setFlags(Qt::NoItemFlags);
		m_activityTasks->addItem(item);
		m_activityCancel->setEnabled(false);
		m_activityClearFinished->setEnabled(false);
		refreshActivityDetails(QString());
		return;
	}

	m_activityClearFinished->setEnabled(true);
	m_activityTasks->setCurrentRow(selectedRow >= 0 ? selectedRow : 0);
	refreshActivityDetails(selectedActivityTaskId());
	// Only the per-item state colours need re-applying here. Re-running the whole
	// stylesheet would repolish every widget on every streamed log line.
	scheduleThemeRefresh();
}

void ApplicationShell::refreshActivityDetails(const QString& taskId)
{
	const OperationTask task = m_activity.task(taskId);
	if (task.id.isEmpty()) {
		m_activityState->setTitle(tr("No task selected"));
		m_activityState->setDetail(tr("Choose an activity to inspect its progress, result, logs, and raw diagnostics."));
		m_activityState->setState(OperationState::Idle);
		m_activityState->setProgress({});
		m_activityDrawer->setTitle(tr("Task Details"));
		m_activityDrawer->setSubtitle(tr("No activity is selected."));
		m_activityDrawer->setSections({});
		m_activityCancel->setEnabled(false);
		return;
	}

	const AccessibilityPreferences preferences = m_settings.accessibilityPreferences();
	m_activityState->setReducedMotion(preferences.reducedMotion);
	m_activityState->setTitle(task.title);
	m_activityState->setDetail(QStringLiteral("%1 / %2").arg(task.source, task.resultSummary.isEmpty() ? task.detail : task.resultSummary));
	m_activityState->setState(task.state, localizedOperationStateName(task.state));
	m_activityState->setProgress(task.progress);

	QStringList logLines;
	for (const OperationLogEntry& entry : task.log) {
		logLines << QStringLiteral("[%1] %2: %3")
			.arg(QLocale::system().toString(entry.timestampUtc.toLocalTime(), QLocale::ShortFormat), localizedOperationStateName(entry.state), entry.message);
	}
	if (logLines.isEmpty()) {
		logLines << tr("No log entries.");
	}

	QStringList summaryLines;
	summaryLines << tr("Title: %1").arg(task.title);
	summaryLines << tr("Source: %1").arg(task.source.isEmpty() ? tr("unknown") : task.source);
	summaryLines << tr("State: %1").arg(localizedOperationStateName(task.state));
	summaryLines << tr("Detail: %1").arg(task.detail.isEmpty() ? tr("none") : task.detail);
	summaryLines << tr("Result: %1").arg(task.resultSummary.isEmpty() ? tr("pending") : task.resultSummary);
	summaryLines << tr("Progress: %1").arg(task.progress.total > 0 ? tr("%1%").arg(operationProgressPercent(task.progress)) : localizedOperationStateName(task.state));
	summaryLines << tr("Cancellable: %1").arg(operationStateAllowsCancellation(task.state) ? tr("yes") : tr("no"));

	QStringList warningLines;
	if (task.warnings.isEmpty()) {
		warningLines << tr("No warnings.");
	} else {
		for (const QString& warning : task.warnings) {
			warningLines << tr("Warning: %1").arg(warning);
		}
	}

	QStringList rawLines;
	rawLines << tr("Task id: %1").arg(task.id);
	rawLines << tr("State id: %1").arg(operationStateId(task.state));
	rawLines << tr("Created: %1").arg(QLocale::system().toString(task.createdUtc.toLocalTime(), QLocale::LongFormat));
	rawLines << tr("Updated: %1").arg(QLocale::system().toString(task.updatedUtc.toLocalTime(), QLocale::LongFormat));
	rawLines << tr("Finished: %1").arg(task.finishedUtc.isValid() ? QLocale::system().toString(task.finishedUtc.toLocalTime(), QLocale::LongFormat) : tr("not finished"));
	rawLines << tr("Progress current: %1").arg(task.progress.current);
	rawLines << tr("Progress total: %1").arg(task.progress.total);
	rawLines << tr("Warnings: %1").arg(task.warnings.size());
	rawLines << tr("Log entries: %1").arg(task.log.size());

	m_activityDrawer->setTitle(tr("Task Details"));
	m_activityDrawer->setSubtitle(QStringLiteral("%1 / %2").arg(task.title, localizedOperationStateName(task.state)));
	m_activityDrawer->setSections({
		{QStringLiteral("summary"), tr("Summary"), task.resultSummary.isEmpty() ? task.detail : task.resultSummary, summaryLines.join('\n'), task.state},
		{QStringLiteral("log"), tr("Log"), tr("%n entries", nullptr, task.log.size()), logLines.join('\n'), task.state},
		{QStringLiteral("warnings"), tr("Warnings"), tr("%n warnings", nullptr, task.warnings.size()), warningLines.join('\n'), task.warnings.isEmpty() ? OperationState::Completed : OperationState::Warning},
		{QStringLiteral("raw"), tr("Raw Task"), task.id, rawLines.join('\n'), task.state},
	});
	m_activityDrawer->showSection(task.warnings.isEmpty() ? QStringLiteral("log") : QStringLiteral("warnings"));
	m_activityCancel->setEnabled(operationStateAllowsCancellation(task.state));
}

QString ApplicationShell::selectedActivityTaskId() const
{
	const QListWidgetItem* item = m_activityTasks ? m_activityTasks->currentItem() : nullptr;
	return item ? item->data(Qt::UserRole).toString() : QString();
}

void ApplicationShell::cancelSelectedActivityTask()
{
	const QString taskId = selectedActivityTaskId();
	if (taskId.isEmpty()) {
		return;
	}
	if (!m_compilerRunActivityId.isEmpty() && taskId == m_compilerRunActivityId && m_compilerRunThread) {
		m_compilerRunCancelRequested.store(true);
		m_activity.appendLog(taskId, OperationState::Running, tr("Compiler cancellation requested."));
		refreshActivityCenter(taskId);
		statusBar()->showMessage(tr("Compiler cancellation requested"));
		return;
	}
	if (m_activity.cancelTask(taskId, tr("Cancelled from the activity center."))) {
		persistActivityTask(taskId);
		refreshActivityCenter(taskId);
		statusBar()->showMessage(tr("Activity cancelled"));
	} else {
		statusBar()->showMessage(tr("Selected activity cannot be cancelled"));
	}
}

void ApplicationShell::clearFinishedActivityTasks()
{
	if (m_activity.clearTerminalTasks()) {
		refreshActivityCenter();
		statusBar()->showMessage(tr("Finished activities cleared"));
		return;
	}
	statusBar()->showMessage(tr("No finished activities to clear"));
}

void ApplicationShell::refreshInspectorDrawerForSettings()
{
	if (!m_inspectorDrawer || !m_inspectorState) {
		return;
	}

	const QVector<RecentProject> projects = m_settings.recentProjects();
	const QVector<GameInstallationProfile> installations = m_settings.gameInstallations();
	const QString selectedInstallationId = m_settings.selectedGameInstallationId();
	const CompilerRegistrySummary compilerRegistry = discoverCompilerTools(compilerRegistryOptionsForProject(m_settings.currentProjectPath(), m_settings));
	const AccessibilityPreferences preferences = m_settings.accessibilityPreferences();
	const AiAutomationPreferences aiPreferences = m_settings.aiAutomationPreferences();
	const QString selectedEditorProfileId = m_settings.selectedEditorProfileId();
	EditorProfileDescriptor selectedEditorProfile;
	editorProfileForId(selectedEditorProfileId, &selectedEditorProfile);
	const SetupSummary setup = m_settings.setupSummary();
	const OperationState settingsState = m_settings.status() == QSettings::NoError ? OperationState::Completed : OperationState::Failed;

	m_inspectorState->setTitle(tr("Settings Inspector"));
	m_inspectorState->setDetail(tr("Persistent shell, setup, language, accessibility, project, and installation metadata."));
	m_inspectorState->setState(settingsState, settingsStatusText(m_settings.status()));
	m_inspectorState->setProgress({1, 1});
	m_inspectorState->setPlaceholderRows({
		tr("Settings metadata"),
		tr("Setup status"),
		tr("Raw diagnostics"),
	});

	QStringList settingsLines;
	settingsLines << tr("Storage: %1").arg(nativePath(m_settings.storageLocation()));
	settingsLines << tr("Schema: %1").arg(m_settings.schemaVersion());
	settingsLines << tr("Status: %1").arg(settingsStatusText(m_settings.status()));
	settingsLines << tr("Selected mode index: %1").arg(m_settings.selectedMode());
	settingsLines << tr("Current project: %1").arg(m_settings.currentProjectPath().isEmpty() ? tr("none") : nativePath(m_settings.currentProjectPath()));
	settingsLines << tr("Recent projects: %1").arg(projects.size());
	settingsLines << tr("Game installations: %1").arg(installations.size());
	settingsLines << tr("Selected installation: %1").arg(selectedInstallationId.isEmpty() ? tr("none") : selectedInstallationId);
	settingsLines << tr("Selected editor profile: %1").arg(selectedEditorProfile.displayName);
	settingsLines << tr("AI-free mode: %1").arg(aiPreferences.aiFreeMode ? tr("enabled") : tr("disabled"));
	settingsLines << tr("Compiler tools with source: %1").arg(compilerRegistry.sourceAvailableCount);
	settingsLines << tr("Compiler executables found: %1").arg(compilerRegistry.executableAvailableCount);

	QStringList preferenceLines;
	preferenceLines << tr("Locale: %1").arg(preferences.localeName);
	preferenceLines << tr("Theme: %1").arg(localizedThemeName(preferences.theme));
	preferenceLines << tr("Text scale: %1%").arg(preferences.textScalePercent);
	preferenceLines << tr("Density: %1").arg(localizedDensityName(preferences.density));
	preferenceLines << tr("Editor profile: %1").arg(selectedEditorProfile.displayName);
	preferenceLines << tr("Reduced motion: %1").arg(preferences.reducedMotion ? tr("enabled") : tr("disabled"));
	preferenceLines << tr("Text to speech: %1").arg(preferences.textToSpeechEnabled ? tr("enabled") : tr("disabled"));
	preferenceLines << tr("AI cloud connectors: %1").arg(aiPreferences.cloudConnectorsEnabled ? tr("enabled") : tr("disabled"));
	preferenceLines << tr("AI agentic workflows: %1").arg(aiPreferences.agenticWorkflowsEnabled ? tr("enabled") : tr("disabled"));

	QStringList setupLines;
	setupLines << tr("Status: %1").arg(setupStatusDisplayName(setup.status));
	setupLines << tr("Current step: %1 [%2]").arg(setup.currentStepName, setup.currentStepId);
	setupLines << tr("Description: %1").arg(setup.currentStepDescription);
	setupLines << tr("Next action: %1").arg(setup.nextAction);
	setupLines << QString();
	setupLines << tr("Completed items:");
	setupLines << (setup.completedItems.isEmpty() ? tr("- none") : QStringLiteral("- %1").arg(setup.completedItems.join(QStringLiteral("\n- "))));
	setupLines << tr("Pending items:");
	setupLines << (setup.pendingItems.isEmpty() ? tr("- none") : QStringLiteral("- %1").arg(setup.pendingItems.join(QStringLiteral("\n- "))));
	setupLines << tr("Warnings:");
	setupLines << (setup.warnings.isEmpty() ? tr("- none") : QStringLiteral("- %1").arg(setup.warnings.join(QStringLiteral("\n- "))));

	QStringList installationLines;
	if (installations.isEmpty()) {
		installationLines << tr("No game installation profiles.");
	} else {
		for (const GameInstallationProfile& profile : installations) {
			installationLines << gameInstallationDetailLines(profile, selectedInstallationId);
			installationLines << QString();
		}
	}
	if (!m_detectedInstallationCandidates.isEmpty()) {
		installationLines << tr("Detected candidates:");
		for (const GameInstallationDetectionCandidate& candidate : m_detectedInstallationCandidates) {
			installationLines << QStringLiteral("- %1 [%2 / %3%]").arg(candidate.profile.displayName, candidate.sourceName).arg(candidate.confidencePercent);
			installationLines << tr("  Root: %1").arg(nativePath(candidate.profile.rootPath));
			installationLines << tr("  Import required before the profile is saved or selected.");
		}
	}

	QStringList primitiveLines;
	for (const UiPrimitiveDescriptor& primitive : uiPrimitiveDescriptors()) {
		primitiveLines << QStringLiteral("%1 [%2]").arg(primitive.title, primitive.id);
		primitiveLines << primitive.description;
		primitiveLines << tr("Use cases: %1").arg(primitive.useCases.join(QStringLiteral(", ")));
		primitiveLines << QString();
	}

	QStringList aboutLines;
	aboutLines << aboutSurfaceText();

	QStringList editorProfileLines;
	for (const EditorProfileDescriptor& profile : editorProfileDescriptors()) {
		editorProfileLines << QStringLiteral("%1 [%2]%3").arg(profile.displayName, profile.id, profile.id == selectedEditorProfileId ? tr(" (selected)") : QString());
		editorProfileLines << tr("Lineage: %1").arg(profile.lineage);
		editorProfileLines << tr("Layout / camera / selection: %1 / %2 / %3").arg(profile.layoutPresetId, profile.cameraPresetId, profile.selectionPresetId);
		editorProfileLines << tr("Grid / terminology: %1 / %2").arg(profile.gridPresetId, profile.terminologyPresetId);
		editorProfileLines << tr("Panels: %1").arg(profile.defaultPanels.join(QStringLiteral(", ")));
		editorProfileLines << profile.description;
		editorProfileLines << QString();
	}

	QStringList aiLines;
	aiLines << aiAutomationPreferencesText(aiPreferences);
	aiLines << QString();
	aiLines << tr("Credential status:");
	for (const AiCredentialStatus& status : aiCredentialStatuses(aiPreferences)) {
		aiLines << QStringLiteral("- %1: %2 (%3)").arg(status.connectorId, status.configured ? tr("configured") : tr("missing"), status.redactedValue.isEmpty() ? status.source : status.redactedValue);
	}
	aiLines << QString();
	aiLines << tr("Reviewable tools:");
	for (const AiToolDescriptor& tool : aiToolDescriptors()) {
		aiLines << QStringLiteral("- %1 [%2]").arg(tool.displayName, tool.id);
		aiLines << tr("  Approval: %1").arg(tool.requiresApproval ? tr("required") : tr("not required"));
		aiLines << tr("  Writes: %1").arg(tool.writesFiles ? tr("staged only") : tr("none"));
	}
	aiLines << QString();
	AiWorkflowManifest previewManifest = defaultAiWorkflowManifest(QStringLiteral("preview-only"), aiPreferences.preferredReasoningConnectorId.isEmpty() ? defaultAiReasoningConnectorId() : aiPreferences.preferredReasoningConnectorId, aiPreferences.preferredTextModelId, tr("Preview AI workflow"));
	previewManifest.contextSummary = tr("Settings inspector preview; no provider request and no file writes.");
	previewManifest.toolCalls.push_back({QStringLiteral("compiler-command-proposal"), tr("Example tool call remains staged until approved."), OperationState::Completed, {tr("prompt")}, {tr("reviewable command")}, {}, true, false});
	previewManifest.stagedOutputs.push_back({QStringLiteral("example-output"), QStringLiteral("command"), tr("Reviewable command preview"), QString(), tr("No action is applied from the inspector."), {tr("vibestudio --cli compiler plan <profile> --input <path> --dry-run")}, false});
	aiLines << tr("Consent preview:");
	aiLines << aiWorkflowManifestText(previewManifest);
	aiLines << QString();
	aiLines << tr("Connectors:");
	for (const AiConnectorDescriptor& connector : aiConnectorDescriptors()) {
		aiLines << aiConnectorSummaryText(connector);
		aiLines << QString();
	}

	QStringList compilerPipelineLines;
	for (const CompilerProfileDescriptor& profile : compilerProfileDescriptors()) {
		const CompilerToolDiscovery* discovery = compilerDiscoveryForTool(compilerRegistry, profile.toolId);
		const OperationState state = discovery ? discovery->state() : OperationState::Failed;
		compilerPipelineLines << QStringLiteral("%1 %2 / %3 [%4]")
			.arg(compilerPipelineBar(state), profile.engineFamily, profile.stageId, profile.id);
		compilerPipelineLines << tr("Tool: %1").arg(profile.toolId);
		compilerPipelineLines << tr("Readiness: %1").arg(discovery && discovery->executableAvailable ? tr("ready") : tr("needs executable"));
		compilerPipelineLines << QString();
	}

	m_inspectorDrawer->setTitle(tr("Inspector Details"));
	m_inspectorDrawer->setSubtitle(tr("Summary-first details for settings, preferences, setup, and UI primitive coverage."));
	m_inspectorDrawer->setSections({
		{QStringLiteral("settings"), tr("Settings Metadata"), nativePath(m_settings.storageLocation()), settingsLines.join('\n'), settingsState},
		{QStringLiteral("preferences"), tr("Preferences"), preferences.localeName, preferenceLines.join('\n'), OperationState::Completed},
		{QStringLiteral("editor-profiles"), tr("Editor Profiles"), tr("%n presets", nullptr, editorProfileDescriptors().size()), editorProfileLines.join('\n').trimmed(), OperationState::Completed},
		{QStringLiteral("ai-automation"), tr("AI Automation"), aiPreferences.aiFreeMode ? tr("AI-free") : tr("Experimental opt-in"), aiLines.join('\n').trimmed(), aiPreferences.aiFreeMode ? OperationState::Completed : OperationState::Warning},
		{QStringLiteral("installations"), tr("Game Installations"), m_detectedInstallationCandidates.isEmpty() ? tr("%n profiles", nullptr, installations.size()) : tr("%1 profiles / %2 detected").arg(installations.size()).arg(m_detectedInstallationCandidates.size()), installationLines.join('\n').trimmed(), installations.isEmpty() ? OperationState::Warning : OperationState::Completed},
		{QStringLiteral("compiler-registry"), tr("Compiler Registry"), tr("%1 of %2 executables").arg(compilerRegistry.executableAvailableCount).arg(compilerRegistry.tools.size()), compilerRegistrySummaryText(compilerRegistry), compilerRegistry.overallState()},
		{QStringLiteral("compiler-pipeline"), tr("Compiler Pipeline"), tr("%n profiles", nullptr, compilerProfileDescriptors().size()), compilerPipelineLines.join('\n').trimmed(), compilerRegistry.overallState()},
		{QStringLiteral("setup"), tr("Setup Summary"), setup.nextAction, setupLines.join('\n'), setup.warnings.isEmpty() ? OperationState::Completed : OperationState::Warning},
		{QStringLiteral("about"), tr("About And Credits"), versionString(), aboutLines.join('\n'), OperationState::Completed},
		{QStringLiteral("ui-primitives"), tr("UI Primitives"), tr("%n primitives", nullptr, uiPrimitiveDescriptors().size()), primitiveLines.join('\n').trimmed(), OperationState::Completed},
	});
}

void ApplicationShell::refreshInspectorDrawerForProject(const QString& path)
{
	if (!m_inspectorDrawer || !m_inspectorState) {
		return;
	}

	const QString normalizedPath = normalizedProjectPath(path);
	RecentProject selectedProject;
	for (const RecentProject& project : m_settings.recentProjects()) {
		if (project.path == normalizedPath) {
			selectedProject = project;
			break;
		}
	}

	if (selectedProject.path.isEmpty()) {
		selectedProject.path = normalizedPath;
		selectedProject.displayName = recentProjectDisplayName(normalizedPath);
		selectedProject.exists = QFileInfo::exists(normalizedPath);
	}

	const OperationState projectState = selectedProject.exists ? OperationState::Completed : OperationState::Warning;
	m_inspectorState->setTitle(tr("Project Inspector"));
	m_inspectorState->setDetail(nativePath(selectedProject.path));
	m_inspectorState->setState(projectState, selectedProject.exists ? tr("Ready") : tr("Missing"));
	m_inspectorState->setProgress({1, 1});
	m_inspectorState->setPlaceholderRows({
		tr("Project metadata"),
		tr("Package roots"),
		tr("Raw diagnostics"),
	});

	QStringList projectLines;
	projectLines << tr("Name: %1").arg(selectedProject.displayName);
	projectLines << tr("Path: %1").arg(nativePath(selectedProject.path));
	projectLines << tr("State: %1").arg(selectedProject.exists ? tr("ready") : tr("missing"));
	if (selectedProject.lastOpenedUtc.isValid()) {
		projectLines << tr("Last opened: %1").arg(QLocale::system().toString(selectedProject.lastOpenedUtc.toLocalTime(), QLocale::LongFormat));
	}

	ProjectManifest manifest;
	QString manifestError;
	const bool manifestLoaded = loadProjectManifest(normalizedPath, &manifest, &manifestError);
	if (!manifestLoaded) {
		manifest = defaultProjectManifest(normalizedPath);
	}
	const ProjectHealthSummary health = buildProjectHealthSummary(manifest, m_settings.selectedGameInstallationId());
	QStringList manifestLines;
	manifestLines << projectManifestToText(manifest);
	if (!manifestLoaded) {
		manifestLines << QString();
		manifestLines << tr("Manifest status: %1").arg(manifestError);
	}

	QStringList nextLines;
	nextLines << (manifestLoaded ? tr("Project manifest: available.") : tr("Project manifest: initialize from the workspace dashboard."));
	nextLines << (m_packageArchive.isOpen() ? tr("Package context: mounted and searchable.") : tr("Package context: open a package file or folder."));
	nextLines << (m_settings.selectedGameInstallationId().isEmpty() ? tr("Game install: add, detect, or select an installation profile.") : tr("Game install: selected profile is available."));
	nextLines << tr("Compilers: inspect the compiler pipeline summary for source/executable readiness.");
	nextLines << tr("Activity logs: available in the Activity tab.");

	QStringList rawLines;
	rawLines << tr("Normalized path: %1").arg(nativePath(normalizedPath));
	rawLines << tr("Exists: %1").arg(QFileInfo::exists(normalizedPath) ? tr("yes") : tr("no"));
	rawLines << tr("Absolute path: %1").arg(nativePath(QFileInfo(normalizedPath).absoluteFilePath()));
	rawLines << tr("Settings storage: %1").arg(nativePath(m_settings.storageLocation()));

	m_inspectorDrawer->setTitle(tr("Project Details"));
	m_inspectorDrawer->setSubtitle(selectedProject.displayName);
	m_inspectorDrawer->setSections({
		{QStringLiteral("metadata"), tr("Project Metadata"), selectedProject.exists ? tr("Ready") : tr("Missing"), projectLines.join('\n'), projectState},
		{QStringLiteral("health"), tr("Project Health"), localizedOperationStateName(health.overallState()), projectHealthLines(health).join('\n').trimmed(), health.overallState()},
		{QStringLiteral("manifest"), tr("Project Manifest"), manifestLoaded ? manifest.projectId : tr("Needs initialization"), manifestLines.join('\n'), manifestLoaded ? OperationState::Completed : OperationState::Warning},
		{QStringLiteral("next-actions"), tr("Next Actions"), tr("Planned workbench connections"), nextLines.join('\n'), OperationState::Idle},
		{QStringLiteral("raw"), tr("Raw Diagnostics"), nativePath(normalizedPath), rawLines.join('\n'), projectState},
	});
}

void ApplicationShell::refreshPreferenceControls()
{
	const AccessibilityPreferences preferences = m_settings.accessibilityPreferences();
	const AiAutomationPreferences aiPreferences = m_settings.aiAutomationPreferences();
	QSignalBlocker localeBlocker(m_localeCombo);
	QSignalBlocker themeBlocker(m_themeCombo);
	QSignalBlocker scaleBlocker(m_textScaleCombo);
	QSignalBlocker densityBlocker(m_densityCombo);
	QSignalBlocker editorProfileBlocker(m_editorProfileCombo);
	QSignalBlocker motionBlocker(m_reducedMotion);
	QSignalBlocker speechBlocker(m_textToSpeech);
	QSignalBlocker aiFreeBlocker(m_aiFreeMode);
	QSignalBlocker aiCloudBlocker(m_aiCloudConnectors);
	QSignalBlocker aiAgenticBlocker(m_aiAgenticWorkflows);
	QSignalBlocker aiReasoningBlocker(m_aiReasoningConnectorCombo);
	QSignalBlocker aiCodingBlocker(m_aiCodingConnectorCombo);
	QSignalBlocker aiVisionBlocker(m_aiVisionConnectorCombo);
	QSignalBlocker aiImageBlocker(m_aiImageConnectorCombo);
	QSignalBlocker aiAudioBlocker(m_aiAudioConnectorCombo);
	QSignalBlocker aiVoiceBlocker(m_aiVoiceConnectorCombo);
	QSignalBlocker aiThreeDBlocker(m_aiThreeDConnectorCombo);
	QSignalBlocker aiEmbeddingsBlocker(m_aiEmbeddingsConnectorCombo);
	QSignalBlocker aiLocalBlocker(m_aiLocalConnectorCombo);

	m_localeCombo->setCurrentIndex(std::max(0, m_localeCombo->findData(preferences.localeName)));
	m_themeCombo->setCurrentIndex(std::max(0, m_themeCombo->findData(themeId(preferences.theme))));
	m_textScaleCombo->setCurrentIndex(std::max(0, m_textScaleCombo->findData(preferences.textScalePercent)));
	m_densityCombo->setCurrentIndex(std::max(0, m_densityCombo->findData(densityId(preferences.density))));
	m_editorProfileCombo->setCurrentIndex(std::max(0, m_editorProfileCombo->findData(m_settings.selectedEditorProfileId())));
	m_reducedMotion->setChecked(preferences.reducedMotion);
	m_textToSpeech->setChecked(preferences.textToSpeechEnabled);
	m_aiFreeMode->setChecked(aiPreferences.aiFreeMode);
	m_aiCloudConnectors->setChecked(aiPreferences.cloudConnectorsEnabled);
	m_aiAgenticWorkflows->setChecked(aiPreferences.agenticWorkflowsEnabled);
	m_aiReasoningConnectorCombo->setCurrentIndex(std::max(0, m_aiReasoningConnectorCombo->findData(aiPreferences.preferredReasoningConnectorId)));
	m_aiCodingConnectorCombo->setCurrentIndex(std::max(0, m_aiCodingConnectorCombo->findData(aiPreferences.preferredCodingConnectorId)));
	m_aiVisionConnectorCombo->setCurrentIndex(std::max(0, m_aiVisionConnectorCombo->findData(aiPreferences.preferredVisionConnectorId)));
	m_aiImageConnectorCombo->setCurrentIndex(std::max(0, m_aiImageConnectorCombo->findData(aiPreferences.preferredImageConnectorId)));
	m_aiAudioConnectorCombo->setCurrentIndex(std::max(0, m_aiAudioConnectorCombo->findData(aiPreferences.preferredAudioConnectorId)));
	m_aiVoiceConnectorCombo->setCurrentIndex(std::max(0, m_aiVoiceConnectorCombo->findData(aiPreferences.preferredVoiceConnectorId)));
	m_aiThreeDConnectorCombo->setCurrentIndex(std::max(0, m_aiThreeDConnectorCombo->findData(aiPreferences.preferredThreeDConnectorId)));
	m_aiEmbeddingsConnectorCombo->setCurrentIndex(std::max(0, m_aiEmbeddingsConnectorCombo->findData(aiPreferences.preferredEmbeddingsConnectorId)));
	m_aiLocalConnectorCombo->setCurrentIndex(std::max(0, m_aiLocalConnectorCombo->findData(aiPreferences.preferredLocalConnectorId)));
	m_aiCloudConnectors->setEnabled(!aiPreferences.aiFreeMode);
	m_aiAgenticWorkflows->setEnabled(!aiPreferences.aiFreeMode && aiPreferences.cloudConnectorsEnabled);
	const bool aiConnectorSelectionEnabled = !aiPreferences.aiFreeMode && aiPreferences.cloudConnectorsEnabled;
	m_aiReasoningConnectorCombo->setEnabled(aiConnectorSelectionEnabled);
	m_aiCodingConnectorCombo->setEnabled(aiConnectorSelectionEnabled);
	m_aiVisionConnectorCombo->setEnabled(aiConnectorSelectionEnabled);
	m_aiImageConnectorCombo->setEnabled(aiConnectorSelectionEnabled);
	m_aiAudioConnectorCombo->setEnabled(aiConnectorSelectionEnabled);
	m_aiVoiceConnectorCombo->setEnabled(aiConnectorSelectionEnabled);
	m_aiThreeDConnectorCombo->setEnabled(aiConnectorSelectionEnabled);
	m_aiEmbeddingsConnectorCombo->setEnabled(aiConnectorSelectionEnabled);
	m_aiLocalConnectorCombo->setEnabled(!aiPreferences.aiFreeMode);
}

void ApplicationShell::savePreferenceControls()
{
	AccessibilityPreferences preferences;
	preferences.localeName = m_localeCombo->currentData().toString();
	preferences.theme = themeFromId(m_themeCombo->currentData().toString());
	preferences.textScalePercent = m_textScaleCombo->currentData().toInt();
	preferences.density = densityFromId(m_densityCombo->currentData().toString());
	preferences.reducedMotion = m_reducedMotion->isChecked();
	preferences.textToSpeechEnabled = m_textToSpeech->isChecked();

	m_settings.setAccessibilityPreferences(preferences);
	m_settings.setSelectedEditorProfileId(m_editorProfileCombo->currentData().toString());
	AiAutomationPreferences aiPreferences;
	aiPreferences.aiFreeMode = m_aiFreeMode->isChecked();
	aiPreferences.cloudConnectorsEnabled = m_aiCloudConnectors->isChecked();
	aiPreferences.agenticWorkflowsEnabled = m_aiAgenticWorkflows->isChecked();
	aiPreferences.preferredReasoningConnectorId = m_aiReasoningConnectorCombo->currentData().toString();
	aiPreferences.preferredCodingConnectorId = m_aiCodingConnectorCombo->currentData().toString();
	aiPreferences.preferredVisionConnectorId = m_aiVisionConnectorCombo->currentData().toString();
	aiPreferences.preferredImageConnectorId = m_aiImageConnectorCombo->currentData().toString();
	aiPreferences.preferredAudioConnectorId = m_aiAudioConnectorCombo->currentData().toString();
	aiPreferences.preferredVoiceConnectorId = m_aiVoiceConnectorCombo->currentData().toString();
	aiPreferences.preferredThreeDConnectorId = m_aiThreeDConnectorCombo->currentData().toString();
	aiPreferences.preferredEmbeddingsConnectorId = m_aiEmbeddingsConnectorCombo->currentData().toString();
	aiPreferences.preferredLocalConnectorId = m_aiLocalConnectorCombo->currentData().toString();
	m_settings.setAiAutomationPreferences(aiPreferences);
	m_settings.sync();
	QLocale::setDefault(QLocale(preferences.localeName));
	recordActivity(tr("Preferences Saved"), tr("Accessibility and language preferences"), tr("settings"), OperationState::Completed, tr("Preferences saved."));
	refreshPreferenceControls();
	refreshSetupPanel();
	refreshRecentProjects();
	applyPreferencesToUi();
	updateInspector();
	statusBar()->showMessage(tr("Preferences saved"));
}

void ApplicationShell::applyPreferencesToUi()
{
	const AccessibilityPreferences preferences = m_settings.accessibilityPreferences();
	QLocale::setDefault(QLocale(preferences.localeName));

	// The theme, density, and text scale resolve to one token set (see
	// studio_theme.h). HighContrastDark and HighContrastLight get full-strength
	// outlines, a distinct focus colour, and 2px focus rings; text scale runs
	// from 100% to 200% through the application font and every sized role.
	const StudioThemeTokens tokens = studioThemeTokens(preferences.theme, preferences.density, preferences.textScalePercent);
	// Re-applying an application stylesheet repolishes every widget, so it only
	// happens when a preference that feeds it actually changed.
	if (!studioThemeIsApplied(tokens)) {
		if (auto* app = qobject_cast<QApplication*>(QCoreApplication::instance())) {
			applyStudioTheme(*app, tokens);
		}
	}

	applyStudioIconScale(this);
	// The texture tiles size themselves from the text scale, which the generic
	// icon pass above does not know about.
	applyTextureViewMode();
	queueTextureThumbnails();
	applyStateColors();
	// LoadingPane has no Q_OBJECT, so it is found through its QFrame base.
	for (QFrame* frame : findChildren<QFrame*>(QStringLiteral("loadingPane"))) {
		if (auto* pane = dynamic_cast<LoadingPane*>(frame)) {
			pane->setReducedMotion(preferences.reducedMotion);
		}
	}
	applyPreferencesToWidgets();
}

void ApplicationShell::applyStateColors()
{
	const StudioThemeTokens& tokens = currentStudioTheme();
	const QColor warning = tokens.colors.warning;

	if (m_recentProjects) {
		for (int index = 0; index < m_recentProjects->count(); ++index) {
			QListWidgetItem* item = m_recentProjects->item(index);
			if (item && !item->data(Qt::UserRole).toString().isEmpty() && !QFileInfo::exists(item->data(Qt::UserRole).toString())) {
				item->setForeground(warning);
			}
		}
	}

	if (m_setupSummary) {
		for (int index = 0; index < m_setupSummary->count(); ++index) {
			QListWidgetItem* item = m_setupSummary->item(index);
			const QString kind = item ? item->data(Qt::UserRole).toString() : QString();
			if (kind == QStringLiteral("warning")) {
				item->setForeground(warning);
			} else if (kind == QStringLiteral("pending")) {
				item->setForeground(tokens.colors.textMuted);
			}
		}
	}

	auto applyStateColor = [&tokens](QListWidget* list, bool tintCompleted = true) {
		if (!list) {
			return;
		}
		for (int index = 0; index < list->count(); ++index) {
			QListWidgetItem* item = list->item(index);
			if (!item) {
				continue;
			}
			QString state = item->data(Qt::UserRole + 1).toString();
			const QString alternateState = item->data(Qt::UserRole + 2).toString();
			if (operationStateIds().contains(alternateState)) {
				state = alternateState;
			}
			if (!tintCompleted && state == QStringLiteral("completed")) {
				continue;
			}
			const QColor color = studioThemeStateColor(tokens, state);
			if (color.isValid()) {
				item->setForeground(color);
			}
		}
	};
	applyStateColor(m_packageComposition);
	applyStateColor(m_packageStagingSummary);
	// Every readable entry is "completed"; tinting them all green would drown
	// out the rows that actually need attention.
	applyStateColor(m_packageEntries, false);
	applyStateColor(m_compilerPipeline);
	applyStateColor(m_gameInstallations);
	applyStateColor(m_projectProblems);
	applyStateColor(m_workspaceSearchResults);
	applyStateColor(m_changedFiles);
	applyStateColor(m_dependencyGraph);
	applyStateColor(m_recentActivityTimeline);
	applyStateColor(m_activityTasks);
}

void ApplicationShell::updateInspector()
{
	const QVector<RecentProject> projects = m_settings.recentProjects();
	const QVector<GameInstallationProfile> installations = m_settings.gameInstallations();
	const CompilerRegistrySummary compilerRegistry = discoverCompilerTools(compilerRegistryOptionsForProject(m_settings.currentProjectPath(), m_settings));
	const AccessibilityPreferences preferences = m_settings.accessibilityPreferences();
	const SetupSummary setup = m_settings.setupSummary();
	QStringList lines;
	lines << tr("Settings");
	lines << tr("Storage: %1").arg(nativePath(m_settings.storageLocation()));
	lines << tr("Schema: %1").arg(m_settings.schemaVersion());
	lines << tr("Status: %1").arg(settingsStatusText(m_settings.status()));
	lines << tr("Selected mode: %1").arg(m_modeRail ? m_modeRail->label(m_modeRail->currentId()) : tr("Workspace"));
	lines << tr("Recent projects: %1").arg(projects.size());
	lines << tr("Locale: %1").arg(preferences.localeName);
	lines << tr("Theme: %1").arg(localizedThemeName(preferences.theme));
	lines << tr("Text scale: %1%").arg(preferences.textScalePercent);
	lines << tr("Density: %1").arg(localizedDensityName(preferences.density));
	lines << tr("Reduced motion: %1").arg(preferences.reducedMotion ? tr("enabled") : tr("disabled"));
	lines << tr("Text to speech: %1").arg(preferences.textToSpeechEnabled ? tr("enabled") : tr("disabled"));
	lines << tr("Setup status: %1").arg(setupStatusDisplayName(setup.status));
	lines << tr("Setup step: %1").arg(setup.currentStepName);
	lines << tr("Setup next action: %1").arg(setup.nextAction);
	lines << tr("Game installations: %1").arg(installations.size());
	lines << tr("Selected installation: %1").arg(m_settings.selectedGameInstallationId().isEmpty() ? tr("none") : m_settings.selectedGameInstallationId());
	lines << tr("Current project: %1").arg(m_settings.currentProjectPath().isEmpty() ? tr("none") : nativePath(m_settings.currentProjectPath()));
	lines << tr("Compiler executables found: %1 of %2").arg(compilerRegistry.executableAvailableCount).arg(compilerRegistry.tools.size());
	lines << QString();
	lines << tr("Workspace diagnostics, package context, compiler readiness, and recent tasks are available in the dashboard panels.");
	lines << tr("Run vibestudio --cli --compiler-report for the current imported toolchain manifest.");
	m_inspector->setPlainText(lines.join('\n'));
	refreshInspectorDrawerForSettings();
}

void ApplicationShell::updateInspectorForProject(const QString& path)
{
	const QString normalizedPath = normalizedProjectPath(path);
	if (normalizedPath.isEmpty()) {
		updateInspector();
		return;
	}

	RecentProject selectedProject;
	for (const RecentProject& project : m_settings.recentProjects()) {
		if (project.path == normalizedPath) {
			selectedProject = project;
			break;
		}
	}

	if (selectedProject.path.isEmpty()) {
		selectedProject.path = normalizedPath;
		selectedProject.displayName = recentProjectDisplayName(normalizedPath);
		selectedProject.exists = QFileInfo::exists(normalizedPath);
	}

	QStringList lines;
	lines << tr("Recent Project");
	lines << tr("Name: %1").arg(selectedProject.displayName);
	lines << tr("Path: %1").arg(nativePath(selectedProject.path));
	lines << tr("State: %1").arg(selectedProject.exists ? tr("ready") : tr("missing"));
	if (selectedProject.lastOpenedUtc.isValid()) {
		lines << tr("Last opened: %1").arg(QLocale::system().toString(selectedProject.lastOpenedUtc.toLocalTime(), QLocale::LongFormat));
	}
	lines << QString();
	lines << tr("Settings storage: %1").arg(nativePath(m_settings.storageLocation()));
	lines << tr("This project will connect to manifests, packages, compilers, and activity logs as those roadmap slices land.");
	m_inspector->setPlainText(lines.join('\n'));
	refreshInspectorDrawerForProject(normalizedPath);
}


// ---------------------------------------------------------------------------
// Commands, menus, toolbar, status chips, and mode navigation
// ---------------------------------------------------------------------------

namespace {

QString modeIcon(StudioMode mode)
{
	switch (mode) {
	case StudioMode::Workspace:
		return QStringLiteral("home");
	case StudioMode::Levels:
		return QStringLiteral("map");
	case StudioMode::Models:
		return QStringLiteral("cube");
	case StudioMode::Textures:
		return QStringLiteral("image");
	case StudioMode::Audio:
		return QStringLiteral("waveform");
	case StudioMode::Packages:
		return QStringLiteral("package");
	case StudioMode::Code:
		return QStringLiteral("code");
	case StudioMode::Shaders:
		return QStringLiteral("layers");
	case StudioMode::Build:
		return QStringLiteral("hammer");
	case StudioMode::Settings:
		return QStringLiteral("settings");
	}
	return QStringLiteral("home");
}

QString modeCommandId(StudioMode mode)
{
	switch (mode) {
	case StudioMode::Workspace:
		return QStringLiteral("shell.mode.workspace");
	case StudioMode::Levels:
		return QStringLiteral("shell.mode.levels");
	case StudioMode::Models:
		return QStringLiteral("shell.mode.models");
	case StudioMode::Textures:
		return QStringLiteral("shell.mode.textures");
	case StudioMode::Audio:
		return QStringLiteral("shell.mode.audio");
	case StudioMode::Packages:
		return QStringLiteral("shell.mode.packages");
	case StudioMode::Code:
		return QStringLiteral("shell.mode.code");
	case StudioMode::Shaders:
		return QStringLiteral("shell.mode.shaders");
	case StudioMode::Build:
		return QStringLiteral("shell.mode.build");
	case StudioMode::Settings:
		return QStringLiteral("shell.mode.settings");
	}
	return QStringLiteral("shell.mode.workspace");
}

bool pathLooksLikePackage(const QString& path)
{
	const QFileInfo info(path);
	if (info.isDir()) {
		return true;
	}
	return packageArchiveFormatFromFileName(info.fileName()) != PackageArchiveFormat::Unknown;
}

bool pathLooksLikeMap(const QString& path)
{
	const QString suffix = QFileInfo(path).suffix().toLower();
	return suffix == QStringLiteral("map") || suffix == QStringLiteral("wad");
}

} // namespace

QVector<StudioModeDescriptor> studioModeDescriptors()
{
	return {
		{StudioMode::Workspace, ApplicationShell::tr("Workspace"),
			ApplicationShell::tr("Project health, problems, search, recent projects, game installations, and AI proposals."),
			modeIcon(StudioMode::Workspace)},
		{StudioMode::Levels, ApplicationShell::tr("Levels"),
			ApplicationShell::tr("Inspect and edit Doom and Quake-family maps in an interactive viewport."),
			modeIcon(StudioMode::Levels)},
		{StudioMode::Models, ApplicationShell::tr("Models"),
			ApplicationShell::tr("Browse MDL, MD2, and MD3 models with metadata and skin previews."),
			modeIcon(StudioMode::Models)},
		{StudioMode::Textures, ApplicationShell::tr("Textures"),
			ApplicationShell::tr("Browse and preview idTech textures, sprites, and palettes with real pixels."),
			modeIcon(StudioMode::Textures)},
		{StudioMode::Audio, ApplicationShell::tr("Audio"),
			ApplicationShell::tr("Inspect package audio with waveform previews and format metadata."),
			modeIcon(StudioMode::Audio)},
		{StudioMode::Packages, ApplicationShell::tr("Packages"),
			ApplicationShell::tr("Browse, extract, stage, and rebuild PAK, WAD, ZIP, and PK3 packages."),
			modeIcon(StudioMode::Packages)},
		{StudioMode::Code, ApplicationShell::tr("Code"),
			ApplicationShell::tr("Edit project scripts and code with syntax highlighting and project-wide search."),
			modeIcon(StudioMode::Code)},
		{StudioMode::Shaders, ApplicationShell::tr("Shaders"),
			ApplicationShell::tr("Parse, inspect, and round-trip idTech3 shader scripts."),
			modeIcon(StudioMode::Shaders)},
		{StudioMode::Build, ApplicationShell::tr("Build"),
			ApplicationShell::tr("Run chained compiler pipelines, inspect artifacts, and launch the game."),
			modeIcon(StudioMode::Build)},
		{StudioMode::Settings, ApplicationShell::tr("Settings"),
			ApplicationShell::tr("First-run setup, accessibility, language, AI connectors, and extensions."),
			modeIcon(StudioMode::Settings)},
	};
}

void ApplicationShell::buildCommands()
{
	m_ownedCommands = std::make_unique<StudioCommandRegistry>(this);
	m_commands = m_ownedCommands.get();

	auto add = [this](const QString& commandId, StudioCommandGroup group, const QString& label, const QString& statusTip, const QString& iconName, bool toolbar, bool requiresProject, bool destructive, std::function<void()> handler, bool separatorBefore = false) {
		StudioCommandRegistration registration;
		registration.commandId = commandId;
		registration.group = group;
		registration.label = label;
		registration.statusTip = statusTip;
		registration.iconName = iconName;
		registration.toolbar = toolbar;
		registration.requiresProject = requiresProject;
		registration.destructive = destructive;
		registration.separatorBefore = separatorBefore;
		registration.handler = std::move(handler);
		m_commands->registerCommand(registration);
	};

	add(QStringLiteral("project.open"), StudioCommandGroup::File, tr("&Open Project Folder…"),
		tr("Choose a project folder and make it the active workspace."), QStringLiteral("folder"), true, false, false,
		[this]() { openProjectFolder(); });
	add(QStringLiteral("package.open"), StudioCommandGroup::File, tr("Open &Package…"),
		tr("Open a PAK, WAD, ZIP, or PK3 package for browsing."), QStringLiteral("archive"), true, false, false,
		[this]() { openPackageFile(); });
	add(QStringLiteral("package.openFolder"), StudioCommandGroup::File, tr("Open Folder &Package…"),
		tr("Treat a folder on disk as a package."), QStringLiteral("folder"), false, false, false,
		[this]() { openPackageFolder(); });
	add(QStringLiteral("map.open"), StudioCommandGroup::File, tr("Open &Map…"),
		tr("Open a Doom WAD or Quake-family .map file in the level workbench."), QStringLiteral("map"), true, false, false,
		[this]() { openLevelMapFile(); });
	add(QStringLiteral("project.initializeManifest"), StudioCommandGroup::File, tr("&Initialize Project Manifest"),
		tr("Create or refresh .vibestudio/project.json for the active project."), QStringLiteral("file"), false, true, false,
		[this]() { initializeCurrentProjectManifest(); }, true);
	add(QStringLiteral("map.saveAs"), StudioCommandGroup::File, tr("Save Map &As…"),
		tr("Write the edited map to a new path without touching the source."), QStringLiteral("save"), false, false, false,
		[this]() { saveLevelMapAsFromUi(); });
	add(QStringLiteral("map.exportImage"), StudioCommandGroup::File, tr("&Export Map Image…"),
		tr("Write a deterministic SVG picture of the current map."), QStringLiteral("image"), false, false, false,
		[this]() { exportLevelMapImage(); });
	add(QStringLiteral("package.saveAs"), StudioCommandGroup::File, tr("Save Pac&kage As…"),
		tr("Write the staged package to a new archive."), QStringLiteral("save"), false, false, false,
		[this]() { saveStagedPackageAs(); });
	add(QStringLiteral("code.save"), StudioCommandGroup::File, tr("&Save File"),
		tr("Save the file open in the code editor."), QStringLiteral("save"), false, false, false,
		[this]() { saveCodeFile(); });
	add(QStringLiteral("package.close"), StudioCommandGroup::File, tr("&Close Package"),
		tr("Close the open package and clear its browsers."), QStringLiteral("close"), false, false, false,
		[this]() { closePackage(); }, true);
	add(QStringLiteral("app.quit"), StudioCommandGroup::File, tr("&Quit"),
		tr("Close VibeStudio."), QStringLiteral("close"), false, false, false,
		[this]() { close(); }, true);

	add(QStringLiteral("map.undo"), StudioCommandGroup::Edit, tr("&Undo Map Edit"),
		tr("Undo the last map edit."), QStringLiteral("undo"), false, false, false,
		[this]() { undoLevelMapEditFromUi(); });
	add(QStringLiteral("map.redo"), StudioCommandGroup::Edit, tr("&Redo Map Edit"),
		tr("Redo the last undone map edit."), QStringLiteral("redo"), false, false, false,
		[this]() { redoLevelMapEditFromUi(); });
	add(QStringLiteral("map.editProperty"), StudioCommandGroup::Edit, tr("Edit Selected &Property…"),
		tr("Change a key on the selected map object."), QStringLiteral("edit"), false, false, false,
		[this]() { editSelectedLevelMapProperty(); }, true);
	add(QStringLiteral("map.moveSelection"), StudioCommandGroup::Edit, tr("&Move Selection…"),
		tr("Translate the selected map object by a delta."), QStringLiteral("move"), false, false, false,
		[this]() { moveSelectedLevelMapObject(); });
	add(QStringLiteral("shell.focusSearch"), StudioCommandGroup::Edit, tr("&Find In Workspace"),
		tr("Focus the workspace search box."), QStringLiteral("search"), false, false, false,
		[this]() {
			setMode(StudioMode::Workspace);
			if (m_workspaceSearch) {
				m_workspaceSearch->setFocus();
				m_workspaceSearch->selectAll();
			}
		}, true);
	add(QStringLiteral("code.findReplace"), StudioCommandGroup::Edit, tr("Project Find And &Replace…"),
		tr("Search, and optionally replace, across every project text file."), QStringLiteral("search"), false, true, false,
		[this]() {
			setMode(StudioMode::Code);
			runCodeFindReplace();
		});

	add(QStringLiteral("shell.commandPalette"), StudioCommandGroup::View, tr("&Command Palette…"),
		tr("Type to find any studio command."), QStringLiteral("command"), false, false, false,
		[this]() { showCommandPalette(); });
	add(QStringLiteral("shell.resetLayout"), StudioCommandGroup::View, tr("Reset &Layout"),
		tr("Restore every panel split to its default proportions and close the side panels."), QStringLiteral("grid"), false, false, false,
		[this]() { resetLayout(); });
	for (const StudioModeDescriptor& descriptor : studioModeDescriptors()) {
		const StudioMode mode = descriptor.mode;
		add(modeCommandId(mode), StudioCommandGroup::View, descriptor.label,
			descriptor.hint, descriptor.iconName, false, false, false,
			[this, mode]() { setMode(mode); }, mode == StudioMode::Workspace);
	}

	add(QStringLiteral("game.detect"), StudioCommandGroup::Project, tr("&Detect Game Installations"),
		tr("Scan common Steam and GOG roots for confirmable installation candidates."), QStringLiteral("search"), false, false, false,
		[this]() { detectGameInstallationProfiles(); });
	add(QStringLiteral("package.extractSelected"), StudioCommandGroup::Project, tr("&Extract Selected Entries…"),
		tr("Extract the selected package entries to a folder."), QStringLiteral("export"), false, false, false,
		[this]() { extractSelectedPackageEntries(); }, true);
	add(QStringLiteral("package.extractAll"), StudioCommandGroup::Project, tr("Extract &All Entries…"),
		tr("Extract every package entry to a folder."), QStringLiteral("export"), false, false, false,
		[this]() { extractAllPackageEntries(); });
	add(QStringLiteral("package.stageAdd"), StudioCommandGroup::Project, tr("Stage &Add File…"),
		tr("Stage a file for the next package save-as."), QStringLiteral("add"), false, false, false,
		[this]() { stagePackageAddFile(); }, true);
	add(QStringLiteral("package.stageReplace"), StudioCommandGroup::Project, tr("Stage &Replace…"),
		tr("Stage a replacement for the selected package entry."), QStringLiteral("edit"), false, false, false,
		[this]() { stagePackageReplaceSelected(); });
	add(QStringLiteral("package.stageRename"), StudioCommandGroup::Project, tr("Stage Re&name…"),
		tr("Stage a rename for the selected package entry."), QStringLiteral("edit"), false, false, false,
		[this]() { stagePackageRenameSelected(); });
	add(QStringLiteral("package.stageDelete"), StudioCommandGroup::Project, tr("Stage &Delete"),
		tr("Stage a deletion for the selected package entry."), QStringLiteral("delete"), false, false, true,
		[this]() { stagePackageDeleteSelected(); });
	add(QStringLiteral("package.compare"), StudioCommandGroup::Project, tr("&Compare With Package…"),
		tr("Compare the open package with another archive entry by entry."), QStringLiteral("search"), false, false, false,
		[this]() {
			setMode(StudioMode::Packages);
			comparePackageWithFile();
		}, true);
	add(QStringLiteral("model.export"), StudioCommandGroup::Project, tr("Export &Model Frame…"),
		tr("Write the selected model's current frame to a Wavefront OBJ file."), QStringLiteral("export"), false, false, false,
		[this]() {
			setMode(StudioMode::Models);
			exportSelectedModel();
		});
	add(QStringLiteral("entity.definitions"), StudioCommandGroup::Project, tr("Load &Entity Definitions…"),
		tr("Choose a .def, .fgd, or .ent file to validate map entities against."), QStringLiteral("folder"), false, false, false,
		[this]() {
			setMode(StudioMode::Levels);
			chooseEntityDefinitionPath();
		});

	add(QStringLiteral("build.run"), StudioCommandGroup::Build, tr("&Run Build Pipeline"),
		tr("Run every enabled stage of the selected pipeline in order."), QStringLiteral("play"), true, false, false,
		[this]() {
			setMode(StudioMode::Build);
			runSelectedBuildPipeline();
		});
	add(QStringLiteral("compiler.run"), StudioCommandGroup::Build, tr("Run Selected &Compiler Profile"),
		tr("Run one compiler profile and capture its logs, diagnostics, and artifacts."), QStringLiteral("terminal"), false, false, false,
		[this]() {
			setMode(StudioMode::Build);
			runSelectedCompilerProfile();
		});
	add(QStringLiteral("build.inspectArtifacts"), StudioCommandGroup::Build, tr("&Inspect Compiled Artifacts"),
		tr("Read the compiled BSP and any leak or portal file beside it."), QStringLiteral("info"), false, false, false,
		[this]() {
			setMode(StudioMode::Build);
			inspectCompiledArtifacts();
		});
	add(QStringLiteral("game.launch"), StudioCommandGroup::Build, tr("&Launch Game"),
		tr("Start the configured game installation with the planned command line."), QStringLiteral("play"), true, false, false,
		[this]() {
			setMode(StudioMode::Build);
			launchConfiguredGame();
		}, true);
	add(QStringLiteral("activity.cancel"), StudioCommandGroup::Build, tr("&Cancel Running Task"),
		tr("Ask the selected running task to stop."), QStringLiteral("stop"), false, false, false,
		[this]() { cancelSelectedActivityTask(); }, true);
	add(QStringLiteral("compiler.copyCli"), StudioCommandGroup::Build, tr("Copy CLI &Equivalent"),
		tr("Copy the shell command line that reproduces the selected compiler profile."), QStringLiteral("copy"), false, false, false,
		[this]() { copySelectedCompilerCliEquivalent(); }, true);
	add(QStringLiteral("compiler.copyManifest"), StudioCommandGroup::Build, tr("Copy Command &Manifest"),
		tr("Copy the schema-versioned command manifest for the selected profile."), QStringLiteral("copy"), false, false, false,
		[this]() { copySelectedCompilerManifest(); });
	add(QStringLiteral("build.copyCommands"), StudioCommandGroup::Build, tr("Copy &Pipeline Commands"),
		tr("Copy every stage command line of the selected build pipeline."), QStringLiteral("copy"), false, false, false,
		[this]() { copyBuildPipelineCommands(); });

	add(QStringLiteral("code.index"), StudioCommandGroup::Tools, tr("&Index Code Workspace"),
		tr("Scan the project for languages, symbols, build tasks, and launch profiles."), QStringLiteral("search"), false, true, false,
		[this]() {
			setMode(StudioMode::Code);
			indexAdvancedCodeWorkspace();
		});
	add(QStringLiteral("shader.inspect"), StudioCommandGroup::Tools, tr("Inspect &Shader Script"),
		tr("Parse an idTech3 shader script into an editable stage graph."), QStringLiteral("file"), false, false, false,
		[this]() {
			setMode(StudioMode::Shaders);
			inspectAdvancedShaderScript();
		});
	add(QStringLiteral("extension.discover"), StudioCommandGroup::Tools, tr("Discover &Extensions"),
		tr("Load vibestudio.extension.json manifests and report their trust metadata."), QStringLiteral("plugin"), false, false, false,
		[this]() {
			setMode(StudioMode::Settings);
			discoverAdvancedExtensions();
		});
	add(QStringLiteral("diagnostics.bundle"), StudioCommandGroup::Tools, tr("Copy &Diagnostic Bundle"),
		tr("Copy a redacted support bundle describing this session."), QStringLiteral("copy"), false, false, false,
		[this]() { copyDiagnosticBundle(); }, true);
	add(QStringLiteral("app.preferences"), StudioCommandGroup::Tools, tr("&Preferences"),
		tr("Language, theme, scaling, density, motion, text to speech, and AI connectors."), QStringLiteral("settings"), false, false, false,
		[this]() { setMode(StudioMode::Settings); }, true);

	add(QStringLiteral("app.about"), StudioCommandGroup::Help, tr("&About VibeStudio"),
		tr("Version, platform, imported compilers, and credits."), QStringLiteral("info"), false, false, false,
		[this]() { showAboutDialog(); });

	// A profile's preferred sequences only mean something once every command is
	// registered, so the remap happens after the whole registry is built.
	m_commands->applyEditorProfile(m_settings.selectedEditorProfileId());

	m_commandPalette = new CommandPaletteDialog(*m_commands, this);
	m_commandPalette->setExtraEntries(commandPaletteEntries());
	connect(m_commandPalette, &CommandPaletteDialog::commandChosen, this, &ApplicationShell::runCommand);
}

void ApplicationShell::buildMenuBar()
{
	if (!m_commands) {
		return;
	}
	menuBar()->setAccessibleName(tr("Main menu"));
	m_commands->populateMenuBar(menuBar());
	m_viewMenu = menuBar()->findChild<QMenu*>(QStringLiteral("menu-view"));
}

void ApplicationShell::buildToolBar()
{
	if (!m_commands) {
		return;
	}
	m_toolBar = addToolBar(tr("Studio"));
	m_toolBar->setObjectName("studioToolBar");
	m_toolBar->setAccessibleName(tr("Studio toolbar"));
	m_toolBar->setMovable(false);
	m_toolBar->setFloatable(false);
	setBaseIconSize(m_toolBar, QSize(18, 18));
	m_toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
	m_commands->populateToolBar(m_toolBar);

	// Command tooltips already name the command and its shortcut (see
	// commandToolTip() in studio_actions.cpp), so an icon-only bar stays
	// discoverable.
	for (QAction* action : m_toolBar->actions()) {
		const QString commandId = action->property("commandId").toString();
		if (commandId.isEmpty()) {
			continue;
		}
		// The two commands that start real work carry a text label.
		if (commandId == QStringLiteral("build.run") || commandId == QStringLiteral("game.launch")) {
			if (auto* button = qobject_cast<QToolButton*>(m_toolBar->widgetForAction(action))) {
				button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
				button->setIcon(studioIcon(commandId == QStringLiteral("build.run") ? QStringLiteral("hammer") : QStringLiteral("gamepad"), StudioIconTone::Normal, StudioIconAlignment::Leading));
				setBaseIconSize(button, QSize(24, 18));
			}
		}
	}

	auto* spacer = new QWidget(m_toolBar);
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	m_toolBar->addWidget(spacer);
	if (QAction* palette = m_commands->action(QStringLiteral("shell.commandPalette"))) {
		auto* paletteButton = new QToolButton(m_toolBar);
		paletteButton->setDefaultAction(palette);
		paletteButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
		paletteButton->setIcon(studioIcon(QStringLiteral("search"), StudioIconTone::Muted, StudioIconAlignment::Leading));
		setBaseIconSize(paletteButton, QSize(22, 16));
		const QString shortcut = m_commands->shortcutForCommand(QStringLiteral("shell.commandPalette")).section(QStringLiteral(", "), 0, 0);
		paletteButton->setText(shortcut.isEmpty() ? tr("Search commands") : tr("Search commands  %1").arg(shortcut));
		paletteButton->setToolTip(tr("Type to find any studio command."));
		paletteButton->setAccessibleName(tr("Search commands"));
		paletteButton->setObjectName(QStringLiteral("commandSearchButton"));
		paletteButton->setFocusPolicy(Qt::NoFocus);
		m_toolBar->addWidget(paletteButton);
	}
}

void ApplicationShell::buildStatusBar()
{
	auto makeChip = [this](const QString& accessibleName) {
		QLabel* chip = createStatusChip(accessibleName);
		chip->setParent(this);
		chip->setTextInteractionFlags(Qt::TextSelectableByMouse);
		statusBar()->addPermanentWidget(chip);
		return chip;
	};
	auto makePanelToggle = [this](const QString& iconName, const QString& text, const QString& toolTip) {
		auto* button = new QToolButton(this);
		button->setIcon(studioIcon(iconName));
		setBaseIconSize(button, QSize(14, 14));
		button->setText(text);
		button->setToolTip(toolTip);
		button->setAccessibleName(text);
		button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
		button->setCheckable(true);
		button->setAutoRaise(true);
		statusBar()->addPermanentWidget(button);
		return button;
	};

	statusBar()->setAccessibleName(tr("Status bar"));
	statusBar()->setSizeGripEnabled(false);
	m_activityToggle = makePanelToggle(QStringLiteral("activity"), tr("Activity"), tr("Show or hide the activity panel: every task with its progress, log, and result."));
	m_inspectorToggle = makePanelToggle(QStringLiteral("sidebar-right"), tr("Inspector"), tr("Show or hide the inspector panel: settings, setup, and project diagnostics."));
	auto* divider = createDivider(Qt::Vertical);
	divider->setFixedHeight(16);
	statusBar()->addPermanentWidget(divider);
	m_projectChip = makeChip(tr("Project status"));
	m_packageChip = makeChip(tr("Package status"));
	m_installChip = makeChip(tr("Installation status"));
	m_compilerChip = makeChip(tr("Compiler status"));
	m_aiChip = makeChip(tr("AI status"));
}

void ApplicationShell::refreshStatusChips()
{
	// Every chip names its state in words next to the glyph, so the bar reads
	// the same in a high-visibility theme or without colour.
	const QString projectPath = m_settings.currentProjectPath();
	if (projectPath.isEmpty()) {
		setStatusChip(m_projectChip, operationStateId(OperationState::Idle), tr("No project"),
			tr("No project folder is open. Use File > Open Project Folder."));
	} else {
		ProjectManifest manifest;
		const bool loaded = loadProjectManifest(projectPath, &manifest);
		const ProjectHealthSummary health = buildProjectHealthSummary(manifest, m_settings.selectedGameInstallationId());
		const OperationState state = loaded ? health.overallState() : OperationState::Warning;
		setStatusChip(m_projectChip, operationStateId(state), QFileInfo(projectPath).fileName(),
			loaded ? tr("Project: %1\n%2").arg(localizedOperationStateName(state), health.detail)
				   : tr("No project manifest yet: %1").arg(nativePath(projectPath)));
	}

	if (!m_packageArchive.isOpen()) {
		setStatusChip(m_packageChip, operationStateId(OperationState::Idle), tr("No package"),
			tr("No package is open. Use File > Open Package."));
	} else {
		const PackageArchiveSummary summary = m_packageArchive.summary();
		const bool staged = m_packageStaging.operations().size() > 0;
		const OperationState state = summary.warningCount > 0 ? OperationState::Warning
			: (staged ? OperationState::Running : OperationState::Completed);
		const QString name = QFileInfo(summary.sourcePath).fileName();
		setStatusChip(m_packageChip, operationStateId(state),
			staged ? tr("%1 (staged)").arg(name) : name,
			tr("%1 entries, %2, %3 loader warnings.")
				.arg(summary.entryCount)
				.arg(byteSizeText(summary.totalSizeBytes))
				.arg(summary.warningCount));
	}

	const QVector<GameInstallationProfile> installations = m_settings.gameInstallations();
	if (installations.isEmpty()) {
		setStatusChip(m_installChip, operationStateId(OperationState::Idle), tr("No game"),
			tr("No game installation is configured. Use Project > Detect Game Installations."));
	} else {
		const QString selectedId = m_settings.selectedGameInstallationId();
		GameInstallationProfile selected = installations.first();
		for (const GameInstallationProfile& profile : installations) {
			if (sameGameInstallationId(profile.id, selectedId)) {
				selected = profile;
				break;
			}
		}
		const GameInstallationValidation validation = validateGameInstallationProfile(selected);
		const OperationState state = validation.isUsable()
			? (validation.warnings.isEmpty() ? OperationState::Completed : OperationState::Warning)
			: OperationState::Failed;
		setStatusChip(m_installChip, operationStateId(state), selected.displayName,
			validation.isUsable() ? nativePath(selected.rootPath)
				: tr("Installation needs review: %1").arg(nativePath(selected.rootPath)));
	}

	const CompilerRegistrySummary registry = discoverCompilerTools(compilerRegistryOptionsForProject(projectPath, m_settings));
	int availableTools = 0;
	for (const CompilerToolDiscovery& tool : registry.tools) {
		if (tool.executableAvailable) {
			++availableTools;
		}
	}
	const OperationState compilerState = availableTools == 0 ? OperationState::Warning
		: (availableTools < registry.tools.size() ? OperationState::Warning : OperationState::Completed);
	setStatusChip(m_compilerChip, operationStateId(compilerState),
		availableTools == 0 ? tr("No compilers") : tr("Compilers %1/%2").arg(availableTools).arg(registry.tools.size()),
		availableTools == 0
			? tr("No compiler executables were discovered. Set a path in Preferences or put the tools on PATH.")
			: tr("%1 of %2 imported compiler tools were discovered.").arg(availableTools).arg(registry.tools.size()));

	const AiAutomationPreferences ai = m_settings.aiAutomationPreferences();
	setStatusChip(m_aiChip, operationStateId(ai.aiFreeMode ? OperationState::Idle : OperationState::Running),
		ai.aiFreeMode ? tr("AI-free") : tr("AI on"),
		ai.aiFreeMode
			? tr("AI-free mode is on. Every workflow stays local and deterministic.")
			: tr("Optional AI workflows are enabled. Proposals are still staged for review before anything is written."));
}

StudioMode ApplicationShell::currentMode() const
{
	if (!m_modeStack) {
		return StudioMode::Workspace;
	}
	return static_cast<StudioMode>(std::clamp(m_modeStack->currentIndex(), 0, 9));
}

void ApplicationShell::setMode(StudioMode mode)
{
	const int index = static_cast<int>(mode);
	if (m_modeStack && m_modeStack->currentIndex() != index) {
		m_modeStack->setCurrentIndex(index);
	}
	if (m_modeRail) {
		m_modeRail->setCurrentId(index);
	}
	m_settings.setSelectedMode(index);
	refreshModeAvailability();
	refreshCommandEnablement();
	if (m_modeRail && index < m_modeRail->count()) {
		statusBar()->showMessage(tr("Mode: %1").arg(m_modeRail->label(index)), 3000);
	}
}

void ApplicationShell::refreshModeAvailability()
{
	// Surfaces that read from the open package only populate when one is open,
	// so switching to them lazily keeps mode changes instant.
	switch (currentMode()) {
	case StudioMode::Textures:
		refreshTextureBrowser();
		break;
	case StudioMode::Models:
		refreshModelBrowser();
		break;
	case StudioMode::Audio:
		refreshAudioBrowser();
		break;
	case StudioMode::Code:
		refreshCodeWorkspaceTree();
		break;
	case StudioMode::Build:
		refreshBuildSurface();
		break;
	default:
		break;
	}
	refreshSurfaceStates();
}

void ApplicationShell::resetLayout()
{
	for (auto it = m_layoutSplitters.cbegin(); it != m_layoutSplitters.cend(); ++it) {
		const QByteArray state = m_defaultLayoutStates.value(it.key());
		if (it.value() && !state.isEmpty()) {
			it.value()->restoreState(state);
		}
		m_settings.setShellLayoutState(it.key(), QByteArray());
	}
	if (m_activityDock) {
		m_activityDock->setFloating(false);
		addDockWidget(Qt::RightDockWidgetArea, m_activityDock);
		m_activityDock->hide();
	}
	if (m_inspectorDock) {
		m_inspectorDock->setFloating(false);
		addDockWidget(Qt::RightDockWidgetArea, m_inspectorDock);
		if (m_activityDock) {
			tabifyDockWidget(m_activityDock, m_inspectorDock);
		}
		m_inspectorDock->hide();
	}
	if (m_modeRail) {
		m_modeRail->setCompact(false);
		m_settings.setShellModeRailCompact(false);
	}
	statusBar()->showMessage(tr("Layout reset to the default arrangement."), 4000);
}

void ApplicationShell::refreshSurfaceStates()
{
	if (!m_modeStack) {
		return;
	}
	const QString separator = QStringLiteral("  %1  ").arg(QChar(0x00b7));
	auto headerFor = [this](StudioMode mode) {
		return m_pageHeaders.value(static_cast<int>(mode), nullptr);
	};
	auto emptyFor = [this](StudioMode mode) {
		return m_emptyStates.value(static_cast<int>(mode), nullptr);
	};
	auto showContent = [this](StudioMode mode, bool hasContent) {
		if (QStackedWidget* stack = m_surfaceStacks.value(static_cast<int>(mode), nullptr)) {
			const int index = hasContent ? 1 : 0;
			if (stack->currentIndex() != index) {
				stack->setCurrentIndex(index);
			}
		}
	};
	auto selectableCount = [](const QListWidget* list) {
		int count = 0;
		if (!list) {
			return count;
		}
		for (int index = 0; index < list->count(); ++index) {
			if (list->item(index)->flags().testFlag(Qt::ItemIsSelectable)) {
				++count;
			}
		}
		return count;
	};
	auto failed = [](const LoadingPane* pane) {
		return pane && pane->state() == OperationState::Failed;
	};

	const QString projectPath = m_settings.currentProjectPath();
	const bool hasPackage = m_packageArchive.isOpen();
	const PackageArchiveSummary packageSummary = hasPackage ? m_packageArchive.summary() : PackageArchiveSummary{};
	const QString packageName = hasPackage ? QFileInfo(packageSummary.sourcePath).fileName() : QString();

	if (PageHeader* header = headerFor(StudioMode::Workspace)) {
		header->setSubtitle(projectPath.isEmpty()
			? tr("No project open. Open a project folder to track its health, packages, and builds.")
			: QFileInfo(projectPath).fileName() + separator + nativePath(projectPath));
	}

	// Levels: the viewport workbench only appears once a map is loaded; a
	// failed load keeps the empty state but explains what went wrong.
	const bool hasMap = m_levelMapDocument.format != LevelMapFormat::Unknown;
	if (PageHeader* header = headerFor(StudioMode::Levels)) {
		if (hasMap) {
			QStringList parts;
			const QString fileName = QFileInfo(m_levelMapDocument.sourcePath).fileName();
			parts << fileName;
			if (!m_levelMapDocument.mapName.isEmpty() && m_levelMapDocument.mapName.compare(fileName, Qt::CaseInsensitive) != 0
				&& m_levelMapDocument.mapName.compare(QFileInfo(fileName).completeBaseName(), Qt::CaseInsensitive) != 0) {
				parts << m_levelMapDocument.mapName;
			}
			if (!m_levelMapDocument.doomThings.isEmpty() || !m_levelMapDocument.doomLinedefs.isEmpty()) {
				parts << tr("%n things", nullptr, static_cast<int>(m_levelMapDocument.doomThings.size()));
				parts << tr("%n linedefs", nullptr, static_cast<int>(m_levelMapDocument.doomLinedefs.size()));
			} else {
				parts << tr("%n entities", nullptr, static_cast<int>(m_levelMapDocument.entities.size()));
				parts << tr("%n brushes", nullptr, static_cast<int>(m_levelMapDocument.brushes.size()));
			}
			if (!m_levelMapDocument.undoStack.isEmpty() && m_levelMapDocument.savedUndoDepth != m_levelMapDocument.undoStack.size()) {
				parts << tr("unsaved edits");
			}
			header->setSubtitle(parts.join(separator));
		} else {
			header->setSubtitle(tr("No map open"));
		}
	}
	if (EmptyStateView* empty = emptyFor(StudioMode::Levels)) {
		if (failed(m_levelMapState)) {
			empty->setTitle(tr("The map could not be opened"));
			empty->setBody(m_levelMapState->detail());
		} else {
			empty->setTitle(tr("No map open"));
			empty->setBody(tr("Open a Quake, Quake II, or Quake III .map, or a Doom WAD, to inspect its entities, brushes, and health in the viewport."));
		}
	}
	showContent(StudioMode::Levels, hasMap);

	if (PageHeader* header = headerFor(StudioMode::Packages)) {
		if (hasPackage) {
			QStringList parts;
			parts << packageName;
			parts << localizedPackageFormatName(packageSummary.format);
			parts << tr("%n entries", nullptr, packageSummary.entryCount);
			parts << byteSizeText(packageSummary.totalSizeBytes);
			const int staged = static_cast<int>(m_packageStaging.operations().size());
			if (staged > 0) {
				parts << tr("%n staged change(s)", nullptr, staged);
			}
			header->setSubtitle(parts.join(separator));
		} else {
			header->setSubtitle(tr("No package open"));
		}
	}
	if (EmptyStateView* empty = emptyFor(StudioMode::Packages)) {
		if (failed(m_packageState)) {
			empty->setTitle(tr("The package could not be opened"));
			empty->setBody(m_packageState->detail());
		} else {
			empty->setTitle(tr("No package open"));
			empty->setBody(tr("Open a PAK, WAD, ZIP, or PK3 archive, or a folder, to browse its entries, preview content, and stage changes for a new package."));
		}
	}
	showContent(StudioMode::Packages, hasPackage);

	// Asset browsers read from the open package; with none open, or none of
	// their kind inside it, the empty state says which.
	struct AssetSurface {
		StudioMode mode;
		QListWidget* list;
		QString noPackageTitle;
		QString noPackageBody;
		QString noneTitle;
		QString noneBody;
		QString countText;
	};
	const int textureCount = selectableCount(m_textureEntries);
	const int modelCount = selectableCount(m_modelEntries);
	const int audioCount = selectableCount(m_audioEntries);
	const QVector<AssetSurface> assetSurfaces = {
		{StudioMode::Textures, m_textureEntries,
			tr("No textures to show"),
			tr("Open a package with textures, flats, sprites, or images. Indexed art is decoded with the palette the package provides."),
			tr("No images in %1").arg(packageName),
			tr("This package has no textures, flats, sprites, or images. Open another package to browse its art."),
			tr("%n image(s) in %1", nullptr, textureCount).arg(packageName)},
		{StudioMode::Models, m_modelEntries,
			tr("No models to show"),
			tr("Open a package that contains MDL, MD2, or MD3 models to orbit them, play their animations, and inspect skins."),
			tr("No models in %1").arg(packageName),
			tr("This package has no MDL, MD2, or MD3 models. Open another package to browse its models."),
			tr("%n model(s) in %1", nullptr, modelCount).arg(packageName)},
		{StudioMode::Audio, m_audioEntries,
			tr("No sounds to show"),
			tr("Open a package with WAV, Ogg, MP3, or FLAC files to inspect their format and see a decoded waveform."),
			tr("No sounds in %1").arg(packageName),
			tr("This package has no WAV, Ogg, MP3, or FLAC files. Open another package to browse its audio."),
			tr("%n sound(s) in %1", nullptr, audioCount).arg(packageName)},
	};
	for (const AssetSurface& surface : assetSurfaces) {
		const int count = selectableCount(surface.list);
		if (EmptyStateView* empty = emptyFor(surface.mode)) {
			empty->setTitle(hasPackage ? surface.noneTitle : surface.noPackageTitle);
			empty->setBody(hasPackage ? surface.noneBody : surface.noPackageBody);
		}
		if (PageHeader* header = headerFor(surface.mode)) {
			header->setSubtitle(hasPackage ? surface.countText : tr("No package open"));
		}
		showContent(surface.mode, hasPackage && count > 0);
	}

	if (PageHeader* header = headerFor(StudioMode::Code)) {
		if (!m_codeFilePath.isEmpty()) {
			header->setSubtitle(nativePath(m_codeFilePath) + (m_codeDirty ? separator + tr("modified") : QString()));
		} else if (!projectPath.isEmpty()) {
			header->setSubtitle(QFileInfo(projectPath).fileName() + separator + tr("choose a file to edit"));
		} else {
			header->setSubtitle(tr("No project open"));
		}
	}
	showContent(StudioMode::Code, !projectPath.isEmpty() || !m_codeFilePath.isEmpty());

	const bool hasShader = !m_advancedShaderDocument.shaders.isEmpty();
	if (PageHeader* header = headerFor(StudioMode::Shaders)) {
		const QString shaderPath = m_advancedShaderPath ? m_advancedShaderPath->text().trimmed() : QString();
		header->setSubtitle(!hasShader || shaderPath.isEmpty() ? tr("No shader script loaded")
			: QFileInfo(shaderPath).fileName() + separator + tr("%n shader(s)", nullptr, static_cast<int>(m_advancedShaderDocument.shaders.size())));
	}
	showContent(StudioMode::Shaders, hasShader);

	// Status strips only take space while they have something to say.
	auto syncStrip = [](LoadingPane* pane) {
		if (!pane) {
			return;
		}
		const OperationState state = pane->state();
		const bool show = state != OperationState::Idle && state != OperationState::Completed;
		QWidget* host = pane->parentWidget();
		if (host && host->objectName() == QStringLiteral("statusStrip")) {
			host->setVisible(show);
		} else {
			pane->setVisible(show);
		}
	};
	syncStrip(m_levelMapState);
	syncStrip(m_packageState);
	syncStrip(m_textureState);
	syncStrip(m_modelState);
	syncStrip(m_audioState);
	syncStrip(m_advancedStudioState);
	if (m_workspaceState) {
		m_workspaceState->setVisible(!projectPath.isEmpty());
	}

	if (PageHeader* header = headerFor(StudioMode::Build)) {
		QStringList parts;
		if (m_buildPipelineChoice && m_buildPipelineChoice->currentIndex() >= 0) {
			parts << m_buildPipelineChoice->currentText();
		}
		const QString input = m_buildPipelineInput ? m_buildPipelineInput->text().trimmed() : QString();
		parts << (input.isEmpty() ? tr("no input map chosen") : QFileInfo(input).fileName());
		if (m_buildPipelineThread) {
			parts << tr("running");
		}
		header->setSubtitle(parts.join(separator));
	}
}

void ApplicationShell::showCommandPalette()
{
	if (!m_commandPalette) {
		return;
	}
	refreshCommandEnablement();
	m_commandPalette->focusFilter();
	m_commandPalette->show();
	m_commandPalette->raise();
	m_commandPalette->activateWindow();
}

void ApplicationShell::runCommand(const QString& commandId)
{
	if (!m_commands) {
		return;
	}
	if (QAction* action = m_commands->action(commandId)) {
		if (action->isEnabled()) {
			action->trigger();
			return;
		}
		statusBar()->showMessage(tr("Command is not available right now: %1").arg(action->text()));
		return;
	}
	statusBar()->showMessage(tr("Command is documented but not implemented in this build: %1").arg(commandId));
}

void ApplicationShell::refreshCommandEnablement()
{
	if (!m_commands) {
		return;
	}
	const bool hasProject = !m_settings.currentProjectPath().isEmpty();
	const bool hasPackage = m_packageArchive.isOpen();
	const bool hasMap = m_levelMapDocument.format != LevelMapFormat::Unknown;
	const bool hasSelection = m_levelMapDocument.selectionKind != LevelMapSelectionKind::None;
	const bool hasStaged = m_packageStaging.operations().size() > 0;
	const bool hasPackageSelection = !selectedPackageEntryPaths().isEmpty();

	m_commands->setProjectAvailable(hasProject);
	m_commands->setEnabled(QStringLiteral("package.close"), hasPackage);
	m_commands->setEnabled(QStringLiteral("package.extractSelected"), hasPackage && hasPackageSelection);
	m_commands->setEnabled(QStringLiteral("package.extractAll"), hasPackage);
	m_commands->setEnabled(QStringLiteral("package.stageAdd"), hasPackage);
	m_commands->setEnabled(QStringLiteral("package.stageReplace"), hasPackage && hasPackageSelection);
	m_commands->setEnabled(QStringLiteral("package.stageRename"), hasPackage && hasPackageSelection);
	m_commands->setEnabled(QStringLiteral("package.stageDelete"), hasPackage && hasPackageSelection);
	m_commands->setEnabled(QStringLiteral("package.saveAs"), hasPackage && hasStaged);
	m_commands->setEnabled(QStringLiteral("package.compare"), hasPackage);
	m_commands->setEnabled(QStringLiteral("model.export"), m_modelMesh.isValid());
	m_commands->setEnabled(QStringLiteral("map.saveAs"), hasMap);
	m_commands->setEnabled(QStringLiteral("map.exportImage"), hasMap);
	m_commands->setEnabled(QStringLiteral("map.editProperty"), hasMap && hasSelection);
	m_commands->setEnabled(QStringLiteral("map.moveSelection"), hasMap && hasSelection);
	m_commands->setEnabled(QStringLiteral("map.undo"), hasMap && !m_levelMapDocument.undoStack.isEmpty());
	m_commands->setEnabled(QStringLiteral("map.redo"), hasMap && !m_levelMapDocument.redoStack.isEmpty());
	m_commands->setEnabled(QStringLiteral("code.save"), m_codeDirty && !m_codeFilePath.isEmpty());
	m_commands->setEnabled(QStringLiteral("activity.cancel"), m_compilerRunThread != nullptr || m_buildPipelineThread != nullptr);
	m_commands->setEnabled(QStringLiteral("build.run"), m_buildPipelineThread == nullptr);
	m_commands->setEnabled(QStringLiteral("compiler.run"), m_compilerRunThread == nullptr);
	refreshSurfaceStates();
}

// ---------------------------------------------------------------------------
// Window events
// ---------------------------------------------------------------------------

void ApplicationShell::closeEvent(QCloseEvent* event)
{
	if (m_codeDirty && !m_codeFilePath.isEmpty()) {
		const QMessageBox::StandardButton answer = QMessageBox::question(
			this,
			tr("Unsaved File"),
			tr("%1 has unsaved changes. Save before closing?").arg(nativePath(m_codeFilePath)),
			QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
			QMessageBox::Save);
		if (answer == QMessageBox::Cancel) {
			event->ignore();
			return;
		}
		if (answer == QMessageBox::Save) {
			saveCodeFile();
		}
	}
	if (m_packageStaging.operations().size() > 0) {
		const QMessageBox::StandardButton answer = QMessageBox::question(
			this,
			tr("Staged Package Changes"),
			tr("%n staged package change(s) have not been written to an archive. Close anyway?", nullptr, m_packageStaging.operations().size()),
			QMessageBox::Yes | QMessageBox::No,
			QMessageBox::No);
		if (answer != QMessageBox::Yes) {
			event->ignore();
			return;
		}
	}
	saveShellState();
	m_settings.sync();
	QMainWindow::closeEvent(event);
}

bool ApplicationShell::eventFilter(QObject* watched, QEvent* event)
{
	// Enter on a property row edits its value, like a double-click. While an
	// editor is open it has focus, so its own Enter never reaches this filter.
	if (watched == m_entityInspector && event->type() == QEvent::KeyPress) {
		const auto* keyEvent = static_cast<QKeyEvent*>(event);
		if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
			QTreeWidgetItem* item = m_entityInspector->currentItem();
			if (item && item->flags().testFlag(Qt::ItemIsEditable)) {
				m_entityInspector->editItem(item, 1);
				return true;
			}
		}
	}
	if (watched == m_textureEntries && event->type() == QEvent::Resize) {
		fitTextureGrid();
	}
	return QMainWindow::eventFilter(watched, event);
}

void ApplicationShell::fitTextureGrid()
{
	if (!m_textureEntries || !m_textureTileMode || m_textureEntries->viewMode() != QListView::IconMode) {
		return;
	}
	// Tiles share the row evenly instead of leaving a ragged gap on the right.
	// The scroll bar's width is reserved whether or not it shows, so it
	// appearing can never change the column count that decides whether it
	// appears.
	const QSize tile = scaledIconSize(QSize(88, 88));
	const int minimumCell = tile.width() + 20;
	const int spacing = m_textureEntries->spacing();
	const int available = m_textureEntries->width() - 2 * m_textureEntries->frameWidth()
		- m_textureEntries->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, m_textureEntries) - spacing;
	const int columns = std::max(1, available / (minimumCell + spacing));
	const QSize grid(std::max(minimumCell, available / columns - spacing),
		tile.height() + m_textureEntries->fontMetrics().height() + 22);
	if (m_textureEntries->gridSize() != grid) {
		m_textureEntries->setGridSize(grid);
	}
}

void ApplicationShell::dragEnterEvent(QDragEnterEvent* event)
{
	if (!event->mimeData()->hasUrls()) {
		return;
	}
	for (const QUrl& url : event->mimeData()->urls()) {
		if (url.isLocalFile()) {
			event->acceptProposedAction();
			return;
		}
	}
}

void ApplicationShell::dropEvent(QDropEvent* event)
{
	if (!event->mimeData()->hasUrls()) {
		return;
	}
	int handled = 0;
	for (const QUrl& url : event->mimeData()->urls()) {
		if (!url.isLocalFile()) {
			continue;
		}
		openDroppedPath(url.toLocalFile());
		++handled;
	}
	if (handled > 0) {
		event->acceptProposedAction();
	}
}

void ApplicationShell::openDroppedPath(const QString& path)
{
	const QFileInfo info(path);
	if (!info.exists()) {
		statusBar()->showMessage(tr("Dropped path does not exist: %1").arg(nativePath(path)));
		return;
	}
	if (pathLooksLikeMap(path)) {
		loadLevelMapPath(path);
		setMode(StudioMode::Levels);
		return;
	}
	if (info.isDir() && QFileInfo::exists(projectManifestPath(path))) {
		m_settings.setCurrentProjectPath(path);
		m_settings.recordRecentProject(path);
		refreshRecentProjects();
		refreshWorkspaceDashboard();
		setMode(StudioMode::Workspace);
		return;
	}
	if (pathLooksLikePackage(path)) {
		loadPackagePath(path);
		setMode(StudioMode::Packages);
		return;
	}
	const QString suffix = info.suffix().toLower();
	if ((suffix == QStringLiteral("def") || suffix == QStringLiteral("fgd") || suffix == QStringLiteral("ent")) && m_entityDefinitionPath) {
		m_entityDefinitionPath->setText(path);
		reloadEntityDefinitions();
		setMode(StudioMode::Levels);
		return;
	}
	if (info.suffix().compare(QStringLiteral("shader"), Qt::CaseInsensitive) == 0 && m_advancedShaderPath) {
		m_advancedShaderPath->setText(path);
		inspectAdvancedShaderScript();
		setMode(StudioMode::Shaders);
		return;
	}
	openCodeFile(path);
	setMode(StudioMode::Code);
}

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

bool ApplicationShell::confirmDestructiveAction(const QString& title, const QString& message, const QString& acceptLabel)
{
	QMessageBox box(this);
	box.setIcon(QMessageBox::Warning);
	box.setWindowTitle(title);
	box.setText(message);
	QPushButton* accept = box.addButton(acceptLabel, QMessageBox::AcceptRole);
	box.addButton(tr("Cancel"), QMessageBox::RejectRole);
	box.setDefaultButton(qobject_cast<QPushButton*>(box.button(QMessageBox::Cancel)));
	box.exec();
	return box.clickedButton() == accept;
}

QString ApplicationShell::activePaletteId() const
{
	if (m_texturePaletteChoice && m_texturePaletteChoice->currentIndex() >= 0) {
		const QString id = m_texturePaletteChoice->currentData().toString();
		if (!id.isEmpty()) {
			return id;
		}
	}
	ProjectManifest manifest;
	if (loadProjectManifest(m_settings.currentProjectPath(), &manifest)) {
		const QString palette = effectiveProjectPaletteId(manifest);
		if (!palette.isEmpty()) {
			return palette;
		}
	}
	return QStringLiteral("quake");
}

void ApplicationShell::invalidatePaletteResolution()
{
	m_paletteResolutionValid = false;
}

IdTechPaletteResolution ApplicationShell::activePaletteResolution()
{
	if (m_paletteResolutionValid) {
		return m_paletteResolution;
	}
	const QString paletteId = activePaletteId();
	if (m_packageArchive.isOpen()) {
		m_paletteResolution = resolveIdTechPalette(m_packageArchive, paletteId);
	} else {
		IdTechPaletteResolution resolution;
		resolution.requestedPaletteId = paletteId;
		resolution.palette = generatedIdTechPalette(paletteId);
		resolution.warnings << tr("No package is open, so a generated stand-in palette is used.");
		m_paletteResolution = resolution;
	}
	m_paletteResolutionValid = true;
	return m_paletteResolution;
}

// ---------------------------------------------------------------------------
// Texture, model, and audio browsers
// ---------------------------------------------------------------------------

namespace {

QVector<PackageEntry> packageEntriesOfKind(const PackageArchive& archive, AssetPreviewKind kind)
{
	QVector<PackageEntry> matches;
	for (const PackageEntry& entry : archive.entries()) {
		if (entry.kind != PackageEntryKind::File) {
			continue;
		}
		if (assetPreviewKindForPath(entry.virtualPath) == kind) {
			matches.push_back(entry);
		}
	}
	std::sort(matches.begin(), matches.end(), [](const PackageEntry& left, const PackageEntry& right) {
		return left.virtualPath.compare(right.virtualPath, Qt::CaseInsensitive) < 0;
	});
	return matches;
}

QListWidgetItem* entryListItem(const PackageEntry& entry)
{
	auto* item = new QListWidgetItem(studioIcon(packageEntryIconName(entry), StudioIconTone::Muted),
		QStringLiteral("%1\n%2").arg(entry.virtualPath, byteSizeText(entry.sizeBytes)));
	item->setData(Qt::UserRole, entry.virtualPath);
	item->setToolTip(entry.virtualPath);
	item->setData(Qt::AccessibleTextRole, entry.virtualPath);
	return item;
}

} // namespace

void ApplicationShell::refreshTextureBrowser()
{
	if (!m_textureEntries || !m_textureState) {
		return;
	}

	if (m_texturePaletteChoice && m_texturePaletteChoice->count() == 0) {
		const QSignalBlocker blocker(m_texturePaletteChoice);
		for (const IdTechPaletteDescriptor& descriptor : idTechPaletteDescriptors()) {
			m_texturePaletteChoice->addItem(descriptor.displayName, descriptor.id);
		}
	}

	const QString previousTexture = currentItemKey(m_textureEntries);
	m_textureEntries->clear();
	if (!m_packageArchive.isOpen()) {
		m_textureState->setState(OperationState::Idle, tr("No package open"));
		m_textureState->setDetail(tr("Open a PAK, WAD, ZIP, or PK3 package to browse its textures, flats, and sprites."));
		m_textureEntries->addItem(disabledListItem(tr("Open a package to list its images.")));
		if (m_texturePreview) {
			m_texturePreview->clearImage();
		}
		if (m_texturePalette) {
			m_texturePalette->clearPalette();
		}
		if (m_texturePaletteSource) {
			m_texturePaletteSource->setText(tr("No palette resolved yet."));
		}
		return;
	}

	const QVector<PackageEntry> images = packageEntriesOfKind(m_packageArchive, AssetPreviewKind::Image);
	for (const PackageEntry& entry : images) {
		QListWidgetItem* item = entryListItem(entry);
		item->setData(Qt::UserRole + 4, byteSizeText(entry.sizeBytes));
		m_textureEntries->addItem(item);
	}
	if (images.isEmpty()) {
		m_textureEntries->addItem(disabledListItem(tr("This package has no recognizable image entries.")));
	}

	const IdTechPaletteResolution resolution = activePaletteResolution();
	if (m_texturePalette) {
		m_texturePalette->setPalette(resolution.palette);
	}
	if (m_texturePaletteSource) {
		m_texturePaletteSource->setText(resolution.fromPackage
			? tr("Palette read from the open package: %1").arg(resolution.sourceVirtualPath)
			: tr("No game palette was found in this package, so a generated stand-in palette is shown. Indexed colours will not match the original game."));
	}

	m_textureState->setState(images.isEmpty() ? OperationState::Warning : OperationState::Completed,
		tr("%n image entr(y)(ies)", nullptr, static_cast<int>(images.size())));
	m_textureState->setDetail(tr("Palette: %1%2")
		.arg(resolution.palette.displayName,
			resolution.fromPackage ? QString() : tr(" (generated stand-in)")));

	filterTextureEntries();
	restoreListSelection(m_textureEntries, previousTexture);
	applyTextureViewMode();
	queueTextureThumbnails();
	showSelectedTexture();
}

void ApplicationShell::filterTextureEntries()
{
	if (!m_textureEntries) {
		return;
	}
	const QString filter = m_textureFilter ? m_textureFilter->text().trimmed() : QString();
	int visible = 0;
	for (int index = 0; index < m_textureEntries->count(); ++index) {
		QListWidgetItem* item = m_textureEntries->item(index);
		const QString path = item->data(Qt::UserRole).toString();
		const bool matches = filter.isEmpty() || path.contains(filter, Qt::CaseInsensitive);
		item->setHidden(!matches && !path.isEmpty());
		if (matches) {
			++visible;
		}
	}
	if (m_textureState && !filter.isEmpty()) {
		m_textureState->setState(visible > 0 ? OperationState::Completed : OperationState::Warning,
			tr("%n match(es) for the filter", nullptr, visible));
	}
}

void ApplicationShell::applyTextureViewMode()
{
	if (!m_textureEntries) {
		return;
	}
	if (m_textureTilesButton && m_textureListButton) {
		m_textureTilesButton->setChecked(m_textureTileMode);
		m_textureListButton->setChecked(!m_textureTileMode);
	}
	const QSize tile = scaledIconSize(QSize(88, 88));
	if (m_textureTileMode) {
		m_textureEntries->setViewMode(QListView::IconMode);
		m_textureEntries->setFlow(QListView::LeftToRight);
		m_textureEntries->setWrapping(true);
		m_textureEntries->setResizeMode(QListView::Adjust);
		m_textureEntries->setMovement(QListView::Static);
		m_textureEntries->setUniformItemSizes(true);
		m_textureEntries->setWordWrap(false);
		m_textureEntries->setIconSize(tile);
		m_textureEntries->setSpacing(4);
		// Cells are wider than the thumbnail so typical texture names fit
		// under it whole; longer ones elide in the middle.
		fitTextureGrid();
	} else {
		m_textureEntries->setViewMode(QListView::ListMode);
		m_textureEntries->setFlow(QListView::TopToBottom);
		m_textureEntries->setWrapping(false);
		m_textureEntries->setGridSize(QSize());
		m_textureEntries->setSpacing(0);
		m_textureEntries->setIconSize(scaledIconSize(QSize(32, 32)));
	}
	const QSize small = scaledIconSize(QSize(16, 16));
	const QIcon placeholder = studioIcon(QStringLiteral("image"), StudioIconTone::Muted);
	for (int index = 0; index < m_textureEntries->count(); ++index) {
		QListWidgetItem* item = m_textureEntries->item(index);
		const QString path = item ? item->data(Qt::UserRole).toString() : QString();
		if (path.isEmpty()) {
			continue;
		}
		const QString size = item->data(Qt::UserRole + 4).toString();
		// Tiles carry just the file name; the full path stays in the tooltip.
		item->setText(m_textureTileMode ? QFileInfo(path).fileName() : QStringLiteral("%1\n%2").arg(path, size));
		item->setTextAlignment(m_textureTileMode ? Qt::AlignHCenter | Qt::AlignTop : Qt::AlignLeft | Qt::AlignVCenter);
		const QIcon thumbnail = m_textureThumbnails.value(path);
		item->setIcon(thumbnail.isNull() ? placeholder : thumbnail);
	}
	Q_UNUSED(small);
}

void ApplicationShell::queueTextureThumbnails()
{
	if (!m_textureEntries || !m_textureThumbnailTimer) {
		return;
	}
	const QString key = QStringLiteral("%1|%2|%3").arg(m_packageArchive.isOpen() ? m_packageArchive.summary().sourcePath : QString(),
		activePaletteId(), QString::number(scaledIconSize(QSize(88, 88)).width()));
	if (key != m_textureThumbnailKey) {
		m_textureThumbnails.clear();
		m_textureThumbnailKey = key;
	}
	m_textureThumbnailQueue.clear();
	for (int index = 0; index < m_textureEntries->count(); ++index) {
		const QString path = m_textureEntries->item(index)->data(Qt::UserRole).toString();
		if (!path.isEmpty() && !m_textureThumbnails.contains(path)) {
			m_textureThumbnailQueue.push_back(path);
		}
	}
	if (m_textureThumbnailQueue.isEmpty()) {
		m_textureThumbnailTimer->stop();
	} else {
		m_textureThumbnailTimer->start();
	}
}

void ApplicationShell::generateTextureThumbnailBatch()
{
	if (!m_packageArchive.isOpen() || m_textureThumbnailQueue.isEmpty()) {
		m_textureThumbnailTimer->stop();
		return;
	}
	// A handful per event-loop turn keeps typing and scrolling responsive even
	// for WADs with thousands of textures.
	const IdTechPaletteResolution resolution = activePaletteResolution();
	IdTechImageDecodeContext context;
	context.archive = &m_packageArchive;
	const int side = scaledIconSize(QSize(88, 88)).width();
	const qreal ratio = m_textureEntries ? m_textureEntries->devicePixelRatioF() : 1.0;
	QHash<QString, QIcon> produced;
	for (int count = 0; count < 6 && !m_textureThumbnailQueue.isEmpty(); ++count) {
		const QString path = m_textureThumbnailQueue.takeFirst();
		QByteArray bytes;
		QString error;
		QImage image;
		if (m_packageArchive.readEntryBytes(path, &bytes, &error)) {
			const IdTechImageDecodeResult decoded = decodeIdTechImage(path, bytes, resolution.palette, context);
			image = decoded.decoded ? decoded.image : QImage();
		}
		const QIcon icon = image.isNull() ? studioIcon(QStringLiteral("warning"), StudioIconTone::Warning) : textureThumbnailIcon(image, side, ratio);
		m_textureThumbnails.insert(path, icon);
		produced.insert(path, icon);
	}
	for (int index = 0; index < m_textureEntries->count(); ++index) {
		QListWidgetItem* item = m_textureEntries->item(index);
		const auto it = produced.constFind(item->data(Qt::UserRole).toString());
		if (it != produced.constEnd()) {
			item->setIcon(it.value());
		}
	}
	if (m_textureThumbnailQueue.isEmpty()) {
		m_textureThumbnailTimer->stop();
	}
}

void ApplicationShell::showSelectedTexture()
{
	if (!m_texturePreview || !m_textureEntries) {
		return;
	}
	QListWidgetItem* item = m_textureEntries->currentItem();
	const QString virtualPath = item ? item->data(Qt::UserRole).toString() : QString();
	if (virtualPath.isEmpty() || !m_packageArchive.isOpen()) {
		m_texturePreview->clearImage();
		if (m_textureDrawer) {
			m_textureDrawer->setSections({});
			m_textureDrawer->setSubtitle(tr("Select an image entry to decode it."));
		}
		return;
	}

	const IdTechPaletteResolution resolution = activePaletteResolution();
	QByteArray bytes;
	QString error;
	if (!m_packageArchive.readEntryBytes(virtualPath, &bytes, &error)) {
		m_texturePreview->clearImage();
		if (m_textureDrawer) {
			m_textureDrawer->setSubtitle(tr("Entry could not be read: %1").arg(error));
		}
		return;
	}

	// Quake II .sp2 sprites name their frame images instead of carrying pixels,
	// so the decoder needs the package to resolve them from.
	IdTechImageDecodeContext context;
	context.archive = &m_packageArchive;
	const IdTechImageDecodeResult decoded = decodeIdTechImage(virtualPath, bytes, resolution.palette, context);
	m_texturePreview->setDecodeResult(decoded);

	if (m_textureMipLevel) {
		const QSignalBlocker blocker(m_textureMipLevel);
		m_textureMipLevel->clear();
		const int levels = std::max(1, m_texturePreview->mipLevelCount());
		for (int level = 0; level < levels; ++level) {
			m_textureMipLevel->addItem(tr("Mip %1").arg(level), level);
		}
		m_textureMipLevel->setEnabled(levels > 1);
	}

	if (m_textureDrawer) {
		QVector<DetailSection> sections;
		DetailSection summary;
		summary.id = QStringLiteral("summary");
		summary.title = tr("Summary");
		summary.summary = decoded.decoded
			? tr("%1, %2 x %3").arg(decoded.formatName).arg(decoded.width).arg(decoded.height)
			: tr("Not decodable");
		summary.content = idTechImageSummaryLines(decoded).join('\n');
		summary.state = decoded.decoded ? OperationState::Completed : OperationState::Warning;
		sections.push_back(summary);

		DetailSection palette;
		palette.id = QStringLiteral("palette");
		palette.title = tr("Palette");
		palette.summary = resolution.fromPackage ? tr("From package") : tr("Generated stand-in");
		palette.content = idTechPaletteSummaryLines(resolution).join('\n');
		palette.state = resolution.fromPackage ? OperationState::Completed : OperationState::Warning;
		sections.push_back(palette);

		if (!decoded.warnings.isEmpty() || !decoded.error.isEmpty()) {
			DetailSection problems;
			problems.id = QStringLiteral("problems");
			problems.title = tr("Problems");
			problems.summary = decoded.error.isEmpty() ? tr("%n warning(s)", nullptr, static_cast<int>(decoded.warnings.size())) : decoded.error;
			QStringList lines = decoded.warnings;
			if (!decoded.error.isEmpty()) {
				lines.prepend(decoded.error);
			}
			problems.content = lines.join('\n');
			problems.state = decoded.error.isEmpty() ? OperationState::Warning : OperationState::Failed;
			sections.push_back(problems);
		}

		m_textureDrawer->setSubtitle(virtualPath);
		m_textureDrawer->setSections(sections);
	}
}

void ApplicationShell::exportSelectedTexture()
{
	if (!m_texturePreview || !m_texturePreview->hasImage()) {
		statusBar()->showMessage(tr("Select a decodable image entry before exporting."));
		return;
	}
	const QString target = QFileDialog::getSaveFileName(this, tr("Export Texture"), QString(), tr("PNG image (*.png)"));
	if (target.isEmpty()) {
		return;
	}
	const QImage image = m_texturePreview->image();
	if (!image.save(target, "PNG")) {
		QMessageBox::warning(this, tr("Export Failed"), tr("The image could not be written to %1.").arg(nativePath(target)));
		return;
	}
	recordActivity(tr("Export Texture"), nativePath(target), tr("asset"), OperationState::Completed,
		tr("Wrote %1 (%2 x %3).").arg(nativePath(target)).arg(image.width()).arg(image.height()));
	statusBar()->showMessage(tr("Texture written to %1").arg(nativePath(target)));
}

void ApplicationShell::refreshModelBrowser()
{
	if (!m_modelEntries || !m_modelState) {
		return;
	}
	const QString previousModel = currentItemKey(m_modelEntries);
	m_modelEntries->clear();
	if (!m_packageArchive.isOpen()) {
		m_modelState->setState(OperationState::Idle, tr("No package open"));
		m_modelState->setDetail(tr("Open a package to list its MDL, MD2, and MD3 models."));
		m_modelEntries->addItem(disabledListItem(tr("Open a package to list its models.")));
		if (m_modelDetails) {
			m_modelDetails->clear();
		}
		if (m_modelSkinPreview) {
			m_modelSkinPreview->clearImage();
		}
		m_modelMesh = ModelMesh();
		if (m_modelViewport) {
			m_modelViewport->clearMesh();
		}
		return;
	}

	const QVector<PackageEntry> models = packageEntriesOfKind(m_packageArchive, AssetPreviewKind::Model);
	for (const PackageEntry& entry : models) {
		m_modelEntries->addItem(entryListItem(entry));
	}
	if (models.isEmpty()) {
		m_modelEntries->addItem(disabledListItem(tr("This package has no recognizable model entries.")));
	}
	m_modelState->setState(models.isEmpty() ? OperationState::Warning : OperationState::Completed,
		tr("%n model(s)", nullptr, static_cast<int>(models.size())));
	m_modelState->setDetail(tr("Geometry is software-rendered from the package. Skins decode with the package palette; animations come from frame names."));
	restoreListSelection(m_modelEntries, previousModel);
	showSelectedModel();
}

void ApplicationShell::showSelectedModel()
{
	if (!m_modelDetails || !m_modelEntries) {
		return;
	}
	m_modelDetails->clear();
	QListWidgetItem* item = m_modelEntries->currentItem();
	const QString virtualPath = item ? item->data(Qt::UserRole).toString() : QString();
	if (virtualPath.isEmpty() || !m_packageArchive.isOpen()) {
		if (m_modelSkinPreview) {
			m_modelSkinPreview->clearImage();
		}
		m_modelMesh = ModelMesh();
		if (m_modelViewport) {
			m_modelViewport->clearMesh();
		}
		return;
	}

	const PackagePreview preview = buildPackageEntryPreview(m_packageArchive, virtualPath);

	// Geometry, not just header counts: this is what the viewport draws.
	m_modelMesh = decodeModelMeshFromArchive(m_packageArchive, virtualPath, activePaletteId());
	QString resolvedSkinPath;
	const QImage skin = resolveModelSkin(m_packageArchive, m_modelMesh, activePaletteId(), &resolvedSkinPath);
	populateModelDetails(m_modelDetails, m_modelMesh, resolvedSkinPath);
	if (m_modelViewport) {
		m_modelViewport->setMesh(m_modelMesh);
		if (!skin.isNull()) {
			m_modelViewport->setSkin(skin);
		} else {
			m_modelViewport->clearSkin();
		}
		const AccessibilityPreferences preferences = m_settings.accessibilityPreferences();
		m_modelViewport->setHighContrast(preferences.theme == StudioTheme::HighContrastDark
			|| preferences.theme == StudioTheme::HighContrastLight);
		m_modelViewport->setReducedMotion(preferences.reducedMotion);
		m_modelViewport->frameModel();
	}
	if (m_modelSkinPreview) {
		if (skin.isNull()) {
			m_modelSkinPreview->clearImage();
		} else {
			m_modelSkinPreview->setImage(skin, resolvedSkinPath.isEmpty() ? virtualPath : resolvedSkinPath);
		}
	}
	refreshModelPlaybackControls();

	if (m_modelDrawer) {
		QVector<DetailSection> sections;
		DetailSection summary;
		summary.id = QStringLiteral("summary");
		summary.title = tr("Summary");
		summary.summary = preview.summary;
		summary.content = preview.assetDetailLines.join('\n');
		summary.state = OperationState::Completed;
		sections.push_back(summary);

		DetailSection raw;
		raw.id = QStringLiteral("raw");
		raw.title = tr("Raw Metadata");
		raw.summary = tr("Header fields as read");
		raw.content = preview.assetRawLines.join('\n');
		raw.state = OperationState::Idle;
		sections.push_back(raw);

		m_modelDrawer->setSubtitle(virtualPath);
		m_modelDrawer->setSections(sections);
	}
}

void ApplicationShell::refreshModelPlaybackControls()
{
	if (!m_modelViewport) {
		return;
	}
	if (m_modelAnimation) {
		const QStringList names = m_modelViewport->animationNames();
		const QString current = m_modelViewport->animation();
		if (m_modelAnimation->count() != names.size() + 1) {
			const QSignalBlocker blocker(m_modelAnimation);
			m_modelAnimation->clear();
			m_modelAnimation->addItem(tr("All frames"), QString());
			for (const QString& name : names) {
				m_modelAnimation->addItem(name, name);
			}
		}
		const QSignalBlocker blocker(m_modelAnimation);
		const int index = std::max(0, m_modelAnimation->findData(current));
		m_modelAnimation->setCurrentIndex(index);
		m_modelAnimation->setEnabled(!names.isEmpty());
	}
	if (m_modelPlayPause) {
		const bool playing = m_modelViewport->isPlaying();
		m_modelPlayPause->setText(playing ? tr("Pause") : tr("Play"));
		m_modelPlayPause->setIcon(studioIcon(playing ? QStringLiteral("pause") : QStringLiteral("play")));
		m_modelPlayPause->setEnabled(m_modelViewport->frameCount() > 1);
	}
	if (m_modelHover) {
		m_modelHover->setText(m_modelViewport->playbackSummary());
	}
}

void ApplicationShell::exportSelectedModel()
{
	if (!m_modelMesh.geometryAvailable) {
		statusBar()->showMessage(tr("Select a model whose geometry VibeStudio can decode before exporting."));
		return;
	}
	const QString target = QFileDialog::getSaveFileName(this, tr("Export Model Frame"), QString(), tr("Wavefront OBJ (*.obj)"));
	if (target.isEmpty()) {
		return;
	}
	const int frameIndex = m_modelViewport ? m_modelViewport->frame() : 0;
	const QString objText = exportModelFrameObj(m_modelMesh, frameIndex);
	if (objText.isEmpty()) {
		QMessageBox::warning(this, tr("Export Failed"), tr("Frame %1 produced no exportable geometry.").arg(frameIndex));
		return;
	}
	QFile file(target);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		QMessageBox::warning(this, tr("Export Failed"), tr("%1 could not be written.").arg(nativePath(target)));
		return;
	}
	file.write(objText.toUtf8());
	file.close();
	recordActivity(tr("Export Model"), nativePath(target), tr("asset"), OperationState::Completed,
		tr("Wrote frame %1 of %2 to %3.").arg(frameIndex).arg(m_modelMesh.formatName, nativePath(target)));
	statusBar()->showMessage(tr("Model frame written to %1").arg(nativePath(target)));
}

void ApplicationShell::refreshAudioBrowser()
{
	if (!m_audioEntries || !m_audioState) {
		return;
	}
	const QString previousAudio = currentItemKey(m_audioEntries);
	m_audioEntries->clear();
	if (!m_packageArchive.isOpen()) {
		m_audioState->setState(OperationState::Idle, tr("No package open"));
		m_audioState->setDetail(tr("Open a package to list its WAV, Ogg, MP3, and FLAC entries."));
		m_audioEntries->addItem(disabledListItem(tr("Open a package to list its audio.")));
		if (m_audioWaveform) {
			m_audioWaveform->clearPeaks();
		}
		if (m_audioDetails) {
			m_audioDetails->clear();
		}
		return;
	}

	const QVector<PackageEntry> audio = packageEntriesOfKind(m_packageArchive, AssetPreviewKind::Audio);
	for (const PackageEntry& entry : audio) {
		m_audioEntries->addItem(entryListItem(entry));
	}
	if (audio.isEmpty()) {
		m_audioEntries->addItem(disabledListItem(tr("This package has no recognizable audio entries.")));
	}
	m_audioState->setState(audio.isEmpty() ? OperationState::Warning : OperationState::Completed,
		tr("%n audio entr(y)(ies)", nullptr, static_cast<int>(audio.size())));
	m_audioState->setDetail(tr("Waveforms are decoded from PCM; compressed codecs report header metadata only."));
	restoreListSelection(m_audioEntries, previousAudio);
	showSelectedAudioEntry();
}

void ApplicationShell::showSelectedAudioEntry()
{
	if (!m_audioDetails || !m_audioEntries) {
		return;
	}
	m_audioDetails->clear();
	QListWidgetItem* item = m_audioEntries->currentItem();
	const QString virtualPath = item ? item->data(Qt::UserRole).toString() : QString();
	if (virtualPath.isEmpty() || !m_packageArchive.isOpen()) {
		if (m_audioWaveform) {
			m_audioWaveform->clearPeaks();
		}
		return;
	}

	const PackagePreview preview = buildPackageEntryPreview(m_packageArchive, virtualPath);
	populateAudioDetails(m_audioDetails, preview);

	if (m_audioWaveform) {
		if (preview.audioPeaks.valid) {
			m_audioWaveform->setPeaks(preview.audioPeaks.peaks, preview.audioPeaks.channels,
				preview.audioPeaks.sampleRate, preview.audioPeaks.durationMs);
		} else {
			m_audioWaveform->clearPeaks();
		}
	}

	if (m_audioDrawer) {
		QVector<DetailSection> sections;
		DetailSection summary;
		summary.id = QStringLiteral("summary");
		summary.title = tr("Summary");
		summary.summary = preview.summary;
		summary.content = preview.assetDetailLines.join('\n');
		summary.state = preview.audioPeaks.valid ? OperationState::Completed : OperationState::Warning;
		sections.push_back(summary);

		DetailSection envelope;
		envelope.id = QStringLiteral("waveform");
		envelope.title = tr("Waveform");
		envelope.summary = preview.audioPeaks.valid
			? tr("%1 Hz, %n channel(s)", nullptr, preview.audioPeaks.channels).arg(preview.audioPeaks.sampleRate)
			: tr("No decoded samples");
		envelope.content = preview.audioWaveformLines.join('\n');
		envelope.state = preview.audioPeaks.valid ? OperationState::Completed : OperationState::Warning;
		sections.push_back(envelope);

		m_audioDrawer->setSubtitle(virtualPath);
		m_audioDrawer->setSections(sections);
	}
}

// ---------------------------------------------------------------------------
// Code workbench
// ---------------------------------------------------------------------------

void ApplicationShell::refreshCodeWorkspaceTree()
{
	if (!m_codeTree) {
		return;
	}
	m_codeTree->clear();
	const QString projectPath = m_settings.currentProjectPath();
	if (projectPath.isEmpty()) {
		auto* empty = new QTreeWidgetItem(m_codeTree, {tr("Open a project folder to browse its scripts and code.")});
		empty->setFlags(Qt::NoItemFlags);
		return;
	}

	QHash<QString, QTreeWidgetItem*> directories;
	const QDir root(projectPath);
	QDirIterator iterator(projectPath, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
	int fileCount = 0;
	while (iterator.hasNext() && fileCount < 4000) {
		const QString path = iterator.next();
		const QString relative = root.relativeFilePath(path);
		if (relative.startsWith(QStringLiteral(".vibestudio/")) || relative.contains(QStringLiteral("/.git/"))) {
			continue;
		}
		if (studioLanguageForPath(path) == StudioLanguage::PlainText && !QFileInfo(path).suffix().isEmpty()) {
			// Keep the tree to text the editor can meaningfully open.
			static const QStringList plainTextSuffixes = {
				QStringLiteral("txt"), QStringLiteral("md"), QStringLiteral("log"), QStringLiteral("csv"),
			};
			if (!plainTextSuffixes.contains(QFileInfo(path).suffix().toLower())) {
				continue;
			}
		}
		++fileCount;

		const QString parentPath = QFileInfo(relative).path();
		QTreeWidgetItem* parent = nullptr;
		if (parentPath != QStringLiteral(".")) {
			parent = directories.value(parentPath);
			if (!parent) {
				parent = new QTreeWidgetItem(m_codeTree, {parentPath});
				parent->setFlags(parent->flags() & ~Qt::ItemIsSelectable);
				directories.insert(parentPath, parent);
			}
		}
		auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_codeTree);
		item->setText(0, QFileInfo(relative).fileName());
		item->setData(0, Qt::UserRole, path);
		item->setToolTip(0, nativePath(path));
	}

	if (fileCount == 0) {
		auto* empty = new QTreeWidgetItem(m_codeTree, {tr("No editable text, script, or code files were found.")});
		empty->setFlags(Qt::NoItemFlags);
	}
	m_codeTree->sortItems(0, Qt::AscendingOrder);
}

void ApplicationShell::openSelectedCodeFile()
{
	if (!m_codeTree) {
		return;
	}
	QTreeWidgetItem* item = m_codeTree->currentItem();
	if (!item) {
		return;
	}
	const QString path = item->data(0, Qt::UserRole).toString();
	if (path.isEmpty()) {
		return;
	}
	openCodeFile(path);
}

void ApplicationShell::registerWatchedDocument(const QString& path, DocumentWatchRole role, const QString& documentId, bool adoptContents)
{
	const QString trimmed = path.trimmed();
	if (trimmed.isEmpty()) {
		m_documentWatcher.unregisterRole(role);
		return;
	}
	// One path per role: opening a second map replaces the first, and the old
	// file stops being watched rather than accumulating.
	const QStringList previous = m_documentWatcher.pathsForRole(role);
	const QString normalized = normalizeDocumentWatchPath(trimmed);
	for (const QString& stale : previous) {
		if (stale != normalized) {
			m_documentWatcher.unregisterPath(stale);
			m_ignoredExternalChanges.remove(stale);
		}
	}

	// Registering takes a fresh baseline, which is right when the caller has
	// just read the file - those bytes are what the studio is showing. A
	// refresh that merely passes the same path through must NOT re-baseline, or
	// the watch would reset faster than any change could ever be reported.
	if (!adoptContents && m_documentWatcher.isWatching(normalized)) {
		return;
	}
	m_ignoredExternalChanges.remove(normalized);
	m_documentWatcher.registerPath(trimmed, role, documentId);
}

void ApplicationShell::pollWatchedDocuments()
{
	if (m_documentWatcher.watchedCount() == 0) {
		return;
	}
	const QVector<DocumentChangeEvent> events = m_documentWatcher.poll();
	for (const DocumentChangeEvent& event : events) {
		handleExternalDocumentChange(event);
	}
}

void ApplicationShell::handleExternalDocumentChange(const DocumentChangeEvent& event)
{
	// A touch is a metadata move with provably identical bytes. Saying anything
	// about it would train the user to dismiss the prompt that matters.
	if (!documentChangeKindIsSubstantive(event.kind)) {
		return;
	}
	if (m_ignoredExternalChanges.contains(event.path)) {
		return;
	}

	const QString display = nativePath(event.path);
	const QString kindName = documentChangeKindDisplayName(event.kind);
	recordActivity(tr("File changed outside the studio"), display, QStringLiteral("session"),
		OperationState::Warning, kindName);

	if (event.kind == DocumentChangeKind::Removed) {
		// Nothing to reload, so this is a notice rather than a question.
		m_ignoredExternalChanges.insert(event.path);
		statusBar()->showMessage(tr("%1 was deleted outside the studio.").arg(display));
		return;
	}

	switch (event.role) {
	case DocumentWatchRole::LevelMap: {
		const bool dirty = !m_levelMapDocument.undoStack.isEmpty();
		const QMessageBox::StandardButton answer = QMessageBox::question(
			this,
			tr("Map Changed On Disk"),
			dirty
				? tr("%1 changed outside the studio and this map has unsaved edits.\n\nReloading discards those edits. Reload from disk?").arg(display)
				: tr("%1 changed outside the studio.\n\nReload it?").arg(display),
			QMessageBox::Yes | QMessageBox::No,
			dirty ? QMessageBox::No : QMessageBox::Yes);
		if (answer == QMessageBox::Yes) {
			loadLevelMapPath(event.path);
			return;
		}
		// Declining has to adopt the new bytes as the baseline, or the save path
		// would keep treating this same change as unseen.
		m_documentWatcher.refreshBaseline(event.path);
		m_ignoredExternalChanges.insert(event.path);
		statusBar()->showMessage(tr("Keeping the in-studio copy of %1.").arg(display));
		break;
	}
	case DocumentWatchRole::Package: {
		const bool staged = m_packageStaging.operations().size() > 0;
		const QMessageBox::StandardButton answer = QMessageBox::question(
			this,
			tr("Package Changed On Disk"),
			staged
				? tr("%1 changed outside the studio and there are staged changes against it.\n\nReopening discards the staged changes. Reopen it?").arg(display)
				: tr("%1 changed outside the studio.\n\nReopen it?").arg(display),
			QMessageBox::Yes | QMessageBox::No,
			staged ? QMessageBox::No : QMessageBox::Yes);
		if (answer == QMessageBox::Yes) {
			loadPackagePath(event.path);
			return;
		}
		m_documentWatcher.refreshBaseline(event.path);
		m_ignoredExternalChanges.insert(event.path);
		statusBar()->showMessage(tr("Keeping the open copy of %1.").arg(display));
		break;
	}
	case DocumentWatchRole::CodeEditor: {
		if (!m_codeDirty) {
			openCodeFile(event.path);
			statusBar()->showMessage(tr("Reloaded %1 after an external change.").arg(display));
			return;
		}
		const QMessageBox::StandardButton answer = QMessageBox::question(
			this,
			tr("File Changed On Disk"),
			tr("%1 changed outside the studio and the editor has unsaved changes.\n\nReloading discards them. Reload?").arg(display),
			QMessageBox::Yes | QMessageBox::No,
			QMessageBox::No);
		if (answer == QMessageBox::Yes) {
			m_codeDirty = false;
			openCodeFile(event.path);
			return;
		}
		m_documentWatcher.refreshBaseline(event.path);
		m_ignoredExternalChanges.insert(event.path);
		statusBar()->showMessage(tr("Keeping the editor copy of %1.").arg(display));
		break;
	}
	case DocumentWatchRole::ProjectManifest:
		// Nothing the user typed lives only in these panels, so there is nothing
		// to lose by rebuilding them and no reason to ask first.
		m_documentWatcher.refreshBaseline(event.path);
		refreshWorkspaceDashboard();
		refreshWorkspaceContextPanels();
		refreshSetupPanel();
		statusBar()->showMessage(tr("Reloaded the project manifest after an external change."));
		break;
	default:
		m_documentWatcher.refreshBaseline(event.path);
		statusBar()->showMessage(tr("%1 changed outside the studio (%2).").arg(display, kindName));
		break;
	}
}

void ApplicationShell::reportPreviousSessionCrash()
{
	if (!previousSessionCrashed()) {
		return;
	}
	const CrashReportInfo report = previousSessionCrashReport();

	// The report is offered, never sent: it stays on this machine unless the
	// user copies it somewhere themselves.
	QMessageBox box(this);
	box.setWindowTitle(tr("The Studio Closed Unexpectedly"));
	box.setIcon(QMessageBox::Warning);
	box.setText(report.path.isEmpty()
		? tr("The previous session ended without shutting down. No crash report was written.")
		: tr("The previous session ended unexpectedly. A crash report was written to this machine."));
	box.setInformativeText(crashReportSummaryText(report));
	QPushButton* openFolder = report.path.isEmpty()
		? nullptr
		: box.addButton(tr("Show Report"), QMessageBox::ActionRole);
	box.addButton(tr("Dismiss"), QMessageBox::AcceptRole);
	box.exec();
	if (openFolder && box.clickedButton() == openFolder) {
		QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(report.path).absolutePath()));
	}

	recordActivity(tr("Previous session crashed"),
		report.reasonDetail.isEmpty() ? report.reasonId : report.reasonDetail,
		QStringLiteral("session"), OperationState::Failed,
		report.path.isEmpty() ? tr("No report written") : nativePath(report.path));
	// Reported once: clearing the marker stops the next launch repeating it.
	markSessionEndedCleanly();
}

void ApplicationShell::openCodeFile(const QString& path)
{
	if (!m_codeEditor) {
		return;
	}
	if (m_codeDirty && !m_codeFilePath.isEmpty() && m_codeFilePath != path) {
		const QMessageBox::StandardButton answer = QMessageBox::question(
			this,
			tr("Unsaved File"),
			tr("%1 has unsaved changes. Save before opening another file?").arg(nativePath(m_codeFilePath)),
			QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
			QMessageBox::Save);
		if (answer == QMessageBox::Cancel) {
			return;
		}
		if (answer == QMessageBox::Save) {
			saveCodeFile();
		}
	}

	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		statusBar()->showMessage(tr("File could not be opened: %1").arg(nativePath(path)));
		return;
	}
	const QByteArray bytes = file.read(4ll * 1024ll * 1024ll);
	const bool truncated = file.bytesAvailable() > 0;
	file.close();
	// A truncated read is not the file, so watching it would offer to reload
	// bytes the editor never showed.
	if (!truncated) {
		registerWatchedDocument(path, DocumentWatchRole::CodeEditor);
	} else {
		m_documentWatcher.unregisterRole(DocumentWatchRole::CodeEditor);
	}

	{
		const QSignalBlocker blocker(m_codeEditor);
		m_codeEditor->setPlainText(QString::fromUtf8(bytes));
		m_codeEditor->setReadOnly(truncated);
	}
	m_codeFilePath = path;
	m_codeDirty = false;

	if (m_codeHighlighter) {
		const AccessibilityPreferences preferences = m_settings.accessibilityPreferences();
		const bool light = preferences.theme == StudioTheme::Light || preferences.theme == StudioTheme::HighContrastLight;
		const bool highContrast = preferences.theme == StudioTheme::HighContrastDark || preferences.theme == StudioTheme::HighContrastLight;
		m_codeHighlighter->setTheme(studioSyntaxTheme(light, highContrast));
		m_codeHighlighter->setLanguage(studioLanguageForPath(path));
	}

	if (m_codeStatus) {
		m_codeStatus->setText(codeDocumentLabel(path, truncated ? tr("Read-only, truncated") : QString()));
		m_codeStatus->setToolTip(nativePath(path));
	}
	refreshCodeDiagnostics();
	refreshCommandEnablement();
}

void ApplicationShell::saveCodeFile()
{
	if (!m_codeEditor || m_codeFilePath.isEmpty()) {
		statusBar()->showMessage(tr("No file is open in the editor."));
		return;
	}
	if (m_codeEditor->isReadOnly()) {
		statusBar()->showMessage(tr("This file was opened read-only because it is too large to edit safely."));
		return;
	}
	QFile file(m_codeFilePath);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		QMessageBox::warning(this, tr("Save Failed"), tr("%1 could not be written.").arg(nativePath(m_codeFilePath)));
		return;
	}
	file.write(m_codeEditor->toPlainText().toUtf8());
	file.close();
	m_codeDirty = false;
	if (m_codeStatus) {
		m_codeStatus->setText(codeDocumentLabel(m_codeFilePath, tr("Saved")));
		m_codeStatus->setToolTip(nativePath(m_codeFilePath));
	}
	recordActivity(tr("Save File"), nativePath(m_codeFilePath), tr("code"), OperationState::Completed,
		tr("Wrote %1.").arg(nativePath(m_codeFilePath)));
	refreshCodeDiagnostics();
	refreshCommandEnablement();
}

void ApplicationShell::refreshCodeDiagnostics()
{
	if (!m_codeDiagnostics) {
		return;
	}
	m_codeDiagnostics->clear();
	if (m_codeFilePath.isEmpty() || !m_codeEditor) {
		m_codeDiagnostics->addItem(disabledListItem(tr("Open a file to see its diagnostics.")));
		if (m_codeHighlighter) {
			m_codeHighlighter->clearDiagnostics();
		}
		return;
	}

	const QByteArray bytes = m_codeEditor->toPlainText().toUtf8();
	const AssetAnalysis analysis = analyzeAssetBytes(m_codeFilePath, bytes, bytes.size());

	QVector<StudioDiagnosticMarker> markers;
	static const QRegularExpression linePattern(QStringLiteral(R"vs(^\s*(?:line\s+)?(\d+)\s*[:.]\s*(.*)$)vs"));
	for (const QString& line : analysis.textDiagnosticLines) {
		auto* item = new QListWidgetItem(line);
		const QRegularExpressionMatch match = linePattern.match(line);
		if (match.hasMatch()) {
			StudioDiagnosticMarker marker;
			marker.line = match.captured(1).toInt();
			marker.severity = line.contains(QStringLiteral("error"), Qt::CaseInsensitive)
				? QStringLiteral("error") : QStringLiteral("warning");
			marker.message = match.captured(2);
			markers.push_back(marker);
			item->setData(Qt::UserRole, marker.line);
		}
		m_codeDiagnostics->addItem(item);
	}
	if (m_codeDiagnostics->count() == 0) {
		m_codeDiagnostics->addItem(disabledListItem(tr("No diagnostics for this file.")));
	}
	if (m_codeHighlighter) {
		m_codeHighlighter->setDiagnostics(markers);
	}
}

void ApplicationShell::runCodeFindReplace()
{
	const QString projectPath = m_settings.currentProjectPath();
	if (projectPath.isEmpty()) {
		statusBar()->showMessage(tr("Open a project folder before searching it."));
		return;
	}
	const QString needle = m_codeFind ? m_codeFind->text() : QString();
	if (needle.isEmpty()) {
		statusBar()->showMessage(tr("Enter the text to find."));
		return;
	}

	AssetTextSearchRequest request;
	request.rootPath = projectPath;
	request.findText = needle;
	request.replaceText = m_codeReplace ? m_codeReplace->text() : QString();
	request.replace = !request.replaceText.isEmpty();
	request.dryRun = true;

	AssetTextSearchReport report = findReplaceProjectText(request);
	if (request.replace && report.matchCount > 0) {
		const bool accepted = confirmDestructiveAction(
			tr("Replace In Project"),
			tr("Replace %n occurrence(s) of \"%1\" with \"%2\" across %3 file(s)? This rewrites files on disk.",
				nullptr, report.matchCount)
				.arg(needle, request.replaceText)
				.arg(report.filesWithMatches),
			tr("Replace"));
		if (accepted) {
			request.dryRun = false;
			report = findReplaceProjectText(request);
		}
	}

	if (m_codeDiagnostics) {
		m_codeDiagnostics->clear();
		for (const AssetTextMatch& match : report.matches) {
			auto* item = new QListWidgetItem(tr("%1:%2:%3  %4")
				.arg(QDir(projectPath).relativeFilePath(match.filePath))
				.arg(match.line)
				.arg(match.column)
				.arg(match.lineText.trimmed()));
			item->setData(Qt::UserRole, match.line);
			m_codeDiagnostics->addItem(item);
		}
		if (report.matches.isEmpty()) {
			m_codeDiagnostics->addItem(disabledListItem(tr("No matches.")));
		}
	}

	recordActivity(tr("Project Find"), needle, tr("code"),
		report.succeeded() ? OperationState::Completed : OperationState::Warning,
		assetTextSearchReportText(report), report.warnings);
	statusBar()->showMessage(tr("%n match(es) in %1 file(s).", nullptr, report.matchCount).arg(report.filesWithMatches));
}

// ---------------------------------------------------------------------------
// Level map viewport wiring
// ---------------------------------------------------------------------------

void ApplicationShell::refreshLevelMapViewport()
{
	if (!m_levelMapViewport) {
		return;
	}
	if (m_levelMapDocument.format == LevelMapFormat::Unknown) {
		m_levelMapViewport->clearDocument();
		return;
	}

	const int projection = m_levelMapProjection ? m_levelMapProjection->currentData().toInt() : 0;
	m_levelMapViewport->setProjection(static_cast<MapViewportProjection>(std::clamp(projection, 0, 2)));
	if (m_levelMapGrid) {
		m_levelMapViewport->setGridSize(std::max(1, m_levelMapGrid->currentData().toInt()));
	}
	if (m_levelMapShowThings) {
		m_levelMapViewport->setShowThings(m_levelMapShowThings->isChecked());
	}
	if (m_levelMapShowSectors) {
		m_levelMapViewport->setShowSectorFill(m_levelMapShowSectors->isChecked());
	}
	if (m_levelMapSnap) {
		m_levelMapViewport->setSnapToGrid(m_levelMapSnap->isChecked());
	}

	const AccessibilityPreferences preferences = m_settings.accessibilityPreferences();
	m_levelMapViewport->setHighContrast(preferences.theme == StudioTheme::HighContrastDark
		|| preferences.theme == StudioTheme::HighContrastLight);
	m_levelMapViewport->setReducedMotion(preferences.reducedMotion);

	// setDocument() copies the document and re-solves all brush geometry, and it
	// resets the view. Only hand it a document when one actually changed;
	// toggling grid or projection options must keep the user's pan and zoom.
	const QString documentKey = QStringLiteral("%1|%2|%3")
		.arg(m_levelMapDocument.sourcePath, m_levelMapDocument.mapName, m_levelMapDocument.editState)
		.append(QStringLiteral("|%1").arg(m_levelMapDocument.undoStack.size()));
	if (documentKey != m_levelMapViewportKey) {
		// An edit to the map already open keeps the camera; a different map or a
		// different file refits, because the old pan and zoom mean nothing there.
		const QString sourceKey = QStringLiteral("%1|%2").arg(m_levelMapDocument.sourcePath, m_levelMapDocument.mapName);
		const bool sameSource = sourceKey == m_levelMapViewportSourceKey && m_levelMapViewport->hasDocument();
		m_levelMapViewportKey = documentKey;
		m_levelMapViewportSourceKey = sourceKey;
		const QSignalBlocker blocker(m_levelMapViewport);
		if (sameSource) {
			m_levelMapViewport->updateDocument(m_levelMapDocument);
		} else {
			m_levelMapViewport->setDocument(m_levelMapDocument);
		}
	}
	const QSignalBlocker selectionBlocker(m_levelMapViewport);
	m_levelMapViewport->setSelectionSet(m_levelMapDocument.selection);
}

void ApplicationShell::chooseEntityDefinitionPath()
{
	const QString path = QFileDialog::getOpenFileName(
		this,
		tr("Choose Entity Definitions"),
		m_settings.currentProjectPath(),
		tr("Entity definitions (*.def *.fgd *.ent *.qc);;All files (*.*)"));
	if (path.isEmpty()) {
		return;
	}
	if (m_entityDefinitionPath) {
		m_entityDefinitionPath->setText(path);
	}
	reloadEntityDefinitions();
}

void ApplicationShell::reloadEntityDefinitions()
{
	QStringList paths;
	const QString requested = m_entityDefinitionPath ? m_entityDefinitionPath->text().trimmed() : QString();
	if (!requested.isEmpty()) {
		paths << requested;
	} else {
		// With nothing named, look where a project conventionally keeps them, so
		// a configured project needs no extra step.
		paths = entityDefinitionSearchPaths(m_settings.currentProjectPath());
	}

	m_entityDefinitions = vibestudio::loadEntityDefinitions(paths);
	if (m_entityDefinitionSummary) {
		if (m_entityDefinitions.isEmpty()) {
			m_entityDefinitionSummary->setText(requested.isEmpty()
				? tr("No entity definitions found in the project's conventional folders. Point at a .def, .fgd, or .ent file to enable entity checking.")
				: tr("No entity definitions were loaded from %1.").arg(nativePath(requested)));
		} else {
			m_entityDefinitionSummary->setText(tr("%n class(es) loaded from %1 source(s): %2 point, %3 brush.",
				nullptr, static_cast<int>(m_entityDefinitions.classes.size()))
				.arg(m_entityDefinitions.sourcePaths.size())
				.arg(m_entityDefinitions.pointClassCount)
				.arg(m_entityDefinitions.brushClassCount));
		}
	}

	recordActivity(tr("Entity Definitions"),
		paths.join(QStringLiteral("; ")),
		tr("level-map"),
		m_entityDefinitions.isEmpty() ? OperationState::Warning : OperationState::Completed,
		tr("%n entity class(es) loaded.", nullptr, static_cast<int>(m_entityDefinitions.classes.size())),
		m_entityDefinitions.warnings);

	refreshLevelMapWorkbench();
}

void ApplicationShell::refreshEntityInspector()
{
	if (!m_entityInspector) {
		return;
	}
	const QSignalBlocker blocker(m_entityInspector);
	m_refreshingEntityInspector = true;
	const QString previousKey = m_entityInspector->currentItem() ? m_entityInspector->currentItem()->data(0, Qt::UserRole).toString() : QString();
	m_entityInspector->clear();

	auto placeholder = [this](const QString& text) {
		auto* item = new QTreeWidgetItem(m_entityInspector, {text});
		item->setFirstColumnSpanned(true);
		item->setFlags(Qt::ItemIsEnabled);
		item->setForeground(0, currentStudioTheme().colors.textMuted);
	};

	if (m_levelMapDocument.selectionKind != LevelMapSelectionKind::Entity || m_levelMapDocument.selectedObjectId < 0) {
		placeholder(tr("Select an entity to inspect its class, keys, and spawnflags."));
		m_refreshingEntityInspector = false;
		return;
	}

	const LevelMapEntity* selected = nullptr;
	for (const LevelMapEntity& entity : m_levelMapDocument.entities) {
		if (entity.id == m_levelMapDocument.selectedObjectId) {
			selected = &entity;
			break;
		}
	}
	if (!selected) {
		placeholder(tr("The selected entity is no longer present in the map."));
		m_refreshingEntityInspector = false;
		return;
	}

	EntityClassDefinition definition;
	const bool known = !m_entityDefinitions.isEmpty() && m_entityDefinitions.classForName(selected->className, &definition);
	const QColor muted = currentStudioTheme().colors.textMuted;
	const QColor warning = currentStudioTheme().colors.warning;

	QTreeWidgetItem* classGroup = propertyGroup(m_entityInspector, tr("Entity"));
	propertyRow(classGroup, tr("Class"), selected->className.isEmpty() ? tr("(no classname)") : selected->className);
	propertyRow(classGroup, tr("Index"), QString::number(selected->id));
	if (known) {
		propertyRow(classGroup, tr("Kind"), entityClassKindId(definition.kind));
		if (!definition.baseClasses.isEmpty()) {
			propertyRow(classGroup, tr("Inherits"), definition.baseClasses.join(QStringLiteral(", ")));
		}
		if (!definition.description.isEmpty()) {
			propertyRow(classGroup, tr("Description"), definition.description.simplified(), definition.description);
		}
		if (definition.hasSize) {
			propertyRow(classGroup, tr("Size"), tr("%1 %2 %3 to %4 %5 %6")
				.arg(definition.mins[0]).arg(definition.mins[1]).arg(definition.mins[2])
				.arg(definition.maxs[0]).arg(definition.maxs[1]).arg(definition.maxs[2]));
		}
		if (!definition.modelHint.isEmpty()) {
			propertyRow(classGroup, tr("Model"), definition.modelHint);
		}
		if (!definition.sourcePath.isEmpty()) {
			propertyRow(classGroup, tr("Defined in"), QStringLiteral("%1:%2").arg(QFileInfo(definition.sourcePath).fileName()).arg(definition.sourceLine),
				nativePath(definition.sourcePath));
		}
	} else {
		QTreeWidgetItem* note = propertyRow(classGroup, tr("Definition"),
			m_entityDefinitions.isEmpty() ? tr("Load entity definitions to see what keys mean") : tr("Not declared by the loaded definitions"));
		note->setForeground(1, m_entityDefinitions.isEmpty() ? muted : warning);
	}

	// Keys set on the entity, editable in place.
	QTreeWidgetItem* keyGroup = propertyGroup(m_entityInspector, tr("Keys (%1)").arg(selected->properties.size()));
	QTreeWidgetItem* current = nullptr;
	QSet<QString> setKeys;
	for (const LevelMapProperty& property : selected->properties) {
		setKeys.insert(property.key.toLower());
		const QString help = known ? entityKeyHelpText(definition, property.key) : QString();
		QTreeWidgetItem* row = propertyRow(keyGroup, property.key, property.value,
			help.isEmpty() ? QStringLiteral("%1 = %2").arg(property.key, property.value) : QStringLiteral("%1 = %2\n%3").arg(property.key, property.value, help));
		row->setFlags(row->flags() | Qt::ItemIsEditable);
		row->setData(0, Qt::UserRole, property.key);
		row->setData(1, Qt::UserRole, property.value);
		if (property.key == previousKey) {
			current = row;
		}
	}

	// Keys the class declares but this entity leaves at their defaults; editing
	// one sets it.
	if (known) {
		QVector<const EntityKeyDefinition*> available;
		for (const EntityKeyDefinition& key : definition.keys) {
			if (!setKeys.contains(key.key.toLower()) && key.key.compare(QStringLiteral("spawnflags"), Qt::CaseInsensitive) != 0) {
				available.push_back(&key);
			}
		}
		if (!available.isEmpty()) {
			QTreeWidgetItem* defaultsGroup = propertyGroup(m_entityInspector, tr("Defaults (%1)").arg(available.size()));
			defaultsGroup->setExpanded(false);
			for (const EntityKeyDefinition* key : available) {
				QTreeWidgetItem* row = propertyRow(defaultsGroup, key->key, key->defaultValue,
					key->description.isEmpty() ? tr("%1 (%2), not set").arg(key->key, key->typeId) : tr("%1 (%2), not set\n%3").arg(key->key, key->typeId, key->description));
				row->setFlags(row->flags() | Qt::ItemIsEditable);
				row->setData(0, Qt::UserRole, key->key);
				row->setData(1, Qt::UserRole, QString());
				row->setForeground(0, muted);
				row->setForeground(1, muted);
				if (key->required) {
					row->setText(0, tr("%1 (required)").arg(key->key));
					row->setForeground(0, warning);
				}
			}
		}
	}

	// Spawnflags as check boxes; toggling one rewrites the summed value.
	if (known && !definition.spawnflags.isEmpty()) {
		int spawnflags = 0;
		for (const LevelMapProperty& property : selected->properties) {
			if (property.key.compare(QStringLiteral("spawnflags"), Qt::CaseInsensitive) == 0) {
				spawnflags = property.value.toInt();
				break;
			}
		}
		QTreeWidgetItem* flagGroup = propertyGroup(m_entityInspector, tr("Spawnflags (%1)").arg(spawnflags));
		for (const EntitySpawnflagDefinition& flag : definition.spawnflags) {
			const bool set = (spawnflags & (1 << flag.bit)) != 0;
			QTreeWidgetItem* row = propertyRow(flagGroup, flag.name, tr("bit %1 (%2)").arg(flag.bit).arg(1 << flag.bit),
				flag.description.isEmpty() ? flag.name : QStringLiteral("%1\n%2").arg(flag.name, flag.description));
			row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
			// The check state is announced and drawn with a mark, so the flag
			// never depends on colour.
			row->setCheckState(0, set ? Qt::Checked : Qt::Unchecked);
			row->setData(0, Qt::UserRole + 1, flag.bit);
		}
	}

	m_entityInspector->expandItem(classGroup);
	m_entityInspector->expandItem(keyGroup);
	// Key names stay whole: size the key column to them within sane bounds.
	m_entityInspector->resizeColumnToContents(0);
	m_entityInspector->setColumnWidth(0, std::clamp(m_entityInspector->columnWidth(0) + 12, 110, 220));
	if (current) {
		m_entityInspector->setCurrentItem(current, 1);
	}
	m_refreshingEntityInspector = false;
}

void ApplicationShell::applyEntityInspectorEdit(QTreeWidgetItem* item, int column)
{
	if (m_refreshingEntityInspector || !item || m_levelMapDocument.selectionKind != LevelMapSelectionKind::Entity) {
		return;
	}
	QString key;
	QString value;
	if (column == 0 && item->flags().testFlag(Qt::ItemIsUserCheckable)) {
		// A spawnflag check box: recompute the summed value from the group.
		QTreeWidgetItem* group = item->parent();
		int spawnflags = 0;
		for (const LevelMapEntity& entity : m_levelMapDocument.entities) {
			if (entity.id == m_levelMapDocument.selectedObjectId) {
				for (const LevelMapProperty& property : entity.properties) {
					if (property.key.compare(QStringLiteral("spawnflags"), Qt::CaseInsensitive) == 0) {
						spawnflags = property.value.toInt();
					}
				}
			}
		}
		const int bit = item->data(0, Qt::UserRole + 1).toInt();
		if (item->checkState(0) == Qt::Checked) {
			spawnflags |= (1 << bit);
		} else {
			spawnflags &= ~(1 << bit);
		}
		Q_UNUSED(group);
		key = QStringLiteral("spawnflags");
		value = QString::number(spawnflags);
	} else if (column == 1) {
		key = item->data(0, Qt::UserRole).toString();
		value = item->text(1);
		if (key.isEmpty() || value == item->data(1, Qt::UserRole).toString()) {
			return;
		}
	} else {
		return;
	}

	QString error;
	if (!setLevelMapEntityProperty(&m_levelMapDocument, m_levelMapDocument.selectedObjectId, key, value, &error)) {
		statusBar()->showMessage(tr("Map edit failed: %1").arg(error));
		QTimer::singleShot(0, this, [this]() {
			refreshEntityInspector();
		});
		return;
	}
	recordActivity(tr("Level map entity edited"), key, QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map edit"));
	statusBar()->showMessage(tr("%1 set to %2. Use Save As to write a non-destructive copy.").arg(key, value));
	// Rebuilding the grid from inside its own itemChanged signal would delete
	// the item that is still being edited, so the refresh waits a turn.
	QTimer::singleShot(0, this, [this]() {
		refreshLevelMapWorkbench();
	});
}

void ApplicationShell::selectLevelMapObjectFromViewport(int selectionKind, int objectId)
{
	if (m_levelMapDocument.format == LevelMapFormat::Unknown || m_syncingLevelMapSelection) {
		return;
	}
	// refreshLevelMapSelection() writes back into the viewport, which would emit
	// selectionChanged again and fight the click that started this.
	const QSignalBlocker blocker(m_levelMapViewport);
	const auto kind = static_cast<LevelMapSelectionKind>(selectionKind);
	QString selector;
	if (kind == LevelMapSelectionKind::None || objectId < 0) {
		m_levelMapDocument.selectionKind = LevelMapSelectionKind::None;
		m_levelMapDocument.selectedObjectId = -1;
	} else {
		selector = QStringLiteral("%1:%2").arg(levelMapSelectionKindId(kind)).arg(objectId);
		QString error;
		if (!selectLevelMapObject(&m_levelMapDocument, selector, &error)) {
			statusBar()->showMessage(error);
			return;
		}
	}

	m_syncingLevelMapSelection = true;
	if (m_levelMapObjects) {
		const QSignalBlocker listBlocker(m_levelMapObjects);
		int row = -1;
		for (int index = 0; index < m_levelMapObjects->count(); ++index) {
			if (m_levelMapObjects->item(index)->data(Qt::UserRole).toString() == selector) {
				row = index;
				break;
			}
		}
		m_levelMapObjects->setCurrentRow(row);
	}
	refreshLevelMapSelection();
	m_syncingLevelMapSelection = false;
	refreshEntityInspector();
	refreshCommandEnablement();
}

void ApplicationShell::syncLevelMapSelectionFromViewport(const QVector<LevelMapSelectionRef>& selection)
{
	if (m_levelMapDocument.format == LevelMapFormat::Unknown || m_syncingLevelMapSelection) {
		return;
	}
	QString error;
	if (!setLevelMapSelection(&m_levelMapDocument, selection, &error) && !error.isEmpty()) {
		statusBar()->showMessage(error);
		return;
	}

	// Mirror the set into the Objects list without letting the list's own
	// itemSelectionChanged handler write it straight back and fight the drag
	// that produced it.
	m_syncingLevelMapSelection = true;
	if (m_levelMapObjects) {
		const QSignalBlocker listBlocker(m_levelMapObjects);
		QSet<QString> wanted;
		wanted.reserve(m_levelMapDocument.selection.size());
		for (const LevelMapSelectionRef& ref : m_levelMapDocument.selection) {
			wanted.insert(QStringLiteral("%1:%2").arg(levelMapSelectionKindId(ref.kind)).arg(ref.objectId));
		}
		const LevelMapSelectionRef primary = levelMapPrimarySelection(m_levelMapDocument);
		const QString primarySelector = primary.kind == LevelMapSelectionKind::None
			? QString()
			: QStringLiteral("%1:%2").arg(levelMapSelectionKindId(primary.kind)).arg(primary.objectId);
		for (int index = 0; index < m_levelMapObjects->count(); ++index) {
			QListWidgetItem* item = m_levelMapObjects->item(index);
			const QString selector = item->data(Qt::UserRole).toString();
			item->setSelected(wanted.contains(selector));
			if (!primarySelector.isEmpty() && selector == primarySelector) {
				m_levelMapObjects->setCurrentItem(item, QItemSelectionModel::NoUpdate);
			}
		}
	}
	refreshLevelMapSelection();
	m_syncingLevelMapSelection = false;
	refreshEntityInspector();
	refreshCommandEnablement();
	if (m_levelMapDocument.selection.size() > 1) {
		statusBar()->showMessage(tr("%n object(s) selected.", nullptr, m_levelMapDocument.selection.size()));
	}
}

void ApplicationShell::moveLevelMapSelectionFromViewport(double dx, double dy, double dz)
{
	if (m_levelMapDocument.format == LevelMapFormat::Unknown) {
		return;
	}
	// The viewport already snapped the delta to its own grid; snapping again to
	// the same grid is idempotent, and passing it keeps the two in step if the
	// combo changes mid-drag.
	const double gridSize = (m_levelMapSnap && m_levelMapSnap->isChecked() && m_levelMapGrid)
		? std::max(1, m_levelMapGrid->currentData().toInt())
		: 0.0;
	QString error;
	if (!moveLevelMapSelectionSnapped(&m_levelMapDocument, dx, dy, dz, gridSize, &error)) {
		if (!error.isEmpty()) {
			statusBar()->showMessage(tr("Move failed: %1").arg(error));
		}
		// The preview has to go back, otherwise the widget keeps showing a move
		// the document never took.
		refreshLevelMapViewport();
		return;
	}
	recordActivity(tr("Level map selection moved"),
		tr("%1, %2, %3").arg(dx).arg(dy).arg(dz),
		QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Moved %n object(s) by %1, %2, %3.", nullptr, m_levelMapDocument.selection.size())
		.arg(dx).arg(dy).arg(dz));
}

void ApplicationShell::undoLevelMapEditFromUi()
{
	QString error;
	if (!undoLevelMapEdit(&m_levelMapDocument, &error)) {
		statusBar()->showMessage(error.isEmpty() ? tr("Nothing to undo.") : error);
		return;
	}
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Undid the last map edit."));
}

void ApplicationShell::redoLevelMapEditFromUi()
{
	QString error;
	if (!redoLevelMapEdit(&m_levelMapDocument, &error)) {
		statusBar()->showMessage(error.isEmpty() ? tr("Nothing to redo.") : error);
		return;
	}
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Redid the last map edit."));
}

void ApplicationShell::exportLevelMapImage()
{
	if (m_levelMapDocument.format == LevelMapFormat::Unknown) {
		statusBar()->showMessage(tr("Open a map before exporting a picture of it."));
		return;
	}
	const QString target = QFileDialog::getSaveFileName(this, tr("Export Map Image"), QString(), tr("SVG image (*.svg)"));
	if (target.isEmpty()) {
		return;
	}

	MapRenderOptions options;
	if (m_levelMapProjection) {
		options.projection = static_cast<MapRenderProjection>(std::clamp(m_levelMapProjection->currentData().toInt(), 0, 2));
	}
	if (m_levelMapGrid) {
		options.gridSize = std::max(1, m_levelMapGrid->currentData().toInt());
	}
	if (m_levelMapShowThings) {
		options.showThings = m_levelMapShowThings->isChecked();
	}
	if (m_levelMapShowSectors) {
		options.showSectorFill = m_levelMapShowSectors->isChecked();
	}
	const AccessibilityPreferences preferences = m_settings.accessibilityPreferences();
	options.highContrast = preferences.theme == StudioTheme::HighContrastDark
		|| preferences.theme == StudioTheme::HighContrastLight;
	options.darkBackground = preferences.theme != StudioTheme::Light && preferences.theme != StudioTheme::HighContrastLight;
	options.highlightKind = m_levelMapDocument.selectionKind;
	options.highlightObjectId = m_levelMapDocument.selectedObjectId;

	const MapRenderReport report = writeLevelMapSvg(m_levelMapDocument, options, target, false, true);
	recordActivity(tr("Export Map Image"), nativePath(target), tr("map"),
		report.succeeded() ? OperationState::Completed : OperationState::Failed,
		mapRenderReportText(report), report.warnings);
	if (!report.succeeded()) {
		QMessageBox::warning(this, tr("Export Failed"), report.error);
		return;
	}
	statusBar()->showMessage(tr("Map image written to %1").arg(nativePath(report.outputPath)));
}

// ---------------------------------------------------------------------------
// Build pipeline and launch
// ---------------------------------------------------------------------------

void ApplicationShell::refreshBuildSurface()
{
	if (!m_buildPipelineChoice) {
		return;
	}

	if (m_buildPipelineChoice->count() == 0) {
		const QSignalBlocker blocker(m_buildPipelineChoice);
		for (const BuildPipelineDescriptor& descriptor : buildPipelineDescriptors()) {
			m_buildPipelineChoice->addItem(descriptor.displayName, descriptor.id);
		}
	}
	if (m_launchProfileChoice && m_launchProfileChoice->count() == 0) {
		const QSignalBlocker blocker(m_launchProfileChoice);
		for (const GameLaunchProfile& profile : gameLaunchProfiles()) {
			m_launchProfileChoice->addItem(profile.displayName, profile.id);
		}
	}

	const QString projectPath = m_settings.currentProjectPath();
	if (m_buildPipelineInput && m_buildPipelineInput->text().trimmed().isEmpty()
		&& m_levelMapDocument.format != LevelMapFormat::Unknown) {
		m_buildPipelineInput->setText(m_levelMapDocument.sourcePath);
	}

	BuildPipelineRequest request;
	request.pipelineId = m_buildPipelineChoice->currentData().toString();
	request.inputPath = m_buildPipelineInput ? m_buildPipelineInput->text().trimmed() : QString();
	request.workspaceRootPath = projectPath;
	request.dryRun = true;

	const CompilerRegistryOptions registryOptions = compilerRegistryOptionsForProject(projectPath, m_settings);
	request.extraSearchPaths = registryOptions.extraSearchPaths;
	request.executableOverrides = registryOptions.executableOverrides;

	const BuildPipelineResult plan = planBuildPipeline(request);
	// Without an input map the plan cannot resolve paths, which the planner
	// reports as failures. Nothing has failed yet, so the surface shows the
	// stages as waiting instead.
	const bool awaitingInput = request.inputPath.isEmpty();
	auto displayState = [awaitingInput](OperationState state) {
		return awaitingInput ? OperationState::Idle : state;
	};

	if (m_buildPipelineStages) {
		m_buildPipelineStages->clear();
		for (const BuildPipelineStageResult& stage : plan.stages) {
			const OperationState state = displayState(stage.state);
			QStringList parts;
			parts << QStringLiteral("%1  %2").arg(studioStateGlyph(state), stage.stage.displayName);
			QStringList toolTip {QStringLiteral("%1 (%2)").arg(stage.stage.displayName, localizedOperationStateName(state))};
			if (stage.skipped) {
				parts << tr("Skipped: %1").arg(stage.skipReason);
			} else if (awaitingInput) {
				parts << tr("Waiting for an input map");
			} else {
				// File names keep the row readable; the tooltip and the details
				// panel carry the full paths.
				const QString input = stage.inputPath.isEmpty() ? tr("no input") : QFileInfo(stage.inputPath).fileName();
				const QString output = stage.outputPath.isEmpty() ? tr("tool default") : QFileInfo(stage.outputPath).fileName();
				parts << QStringLiteral("%1  %2  %3").arg(input, QChar(0x2192), output);
				toolTip << tr("Input: %1").arg(stage.inputPath.isEmpty() ? tr("(none)") : nativePath(stage.inputPath));
				toolTip << tr("Output: %1").arg(stage.outputPath.isEmpty() ? tr("(tool default)") : nativePath(stage.outputPath));
			}
			if (!stage.plan.executableAvailable) {
				parts << tr("Tool not found on this machine");
			}
			auto* item = new QListWidgetItem(parts.join(QStringLiteral("\n")));
			item->setData(Qt::UserRole, stage.stage.id);
			item->setData(Qt::UserRole + 1, operationStateId(state));
			item->setToolTip(toolTip.join('\n'));
			item->setData(Qt::AccessibleTextRole, QStringLiteral("%1, %2").arg(stage.stage.displayName, localizedOperationStateName(state)));
			m_buildPipelineStages->addItem(item);
		}
		if (plan.stages.isEmpty()) {
			m_buildPipelineStages->addItem(disabledListItem(tr("Choose a pipeline and an input map to plan its stages.")));
		}
	}

	if (m_buildPipelineChart) {
		QVector<PipelineStageNode> nodes;
		for (const BuildPipelineStageResult& stage : plan.stages) {
			PipelineStageNode node;
			node.id = stage.stage.id;
			node.label = stage.stage.displayName;
			node.detail = stage.skipped ? stage.skipReason : nativePath(stage.outputPath);
			node.state = displayState(stage.state);
			node.optional = stage.stage.optional;
			node.badgeText = stage.plan.executableAvailable ? QString() : tr("no tool");
			nodes.push_back(node);
		}
		m_buildPipelineChart->setStages(nodes);
	}

	if (m_buildPipelineState && awaitingInput) {
		m_buildPipelineState->setState(OperationState::Idle, tr("Waiting for input"));
		m_buildPipelineState->setDetail(tr("Choose the source map to compile. Opening a map in Levels fills this in for you."));
	} else if (m_buildPipelineState) {
		m_buildPipelineState->setState(plan.state, localizedOperationStateName(plan.state));
		m_buildPipelineState->setDetail(plan.errors.isEmpty()
			? tr("%n stage(s) planned. Output: %1", nullptr, plan.plannedStageCount)
				.arg(plan.finalOutputPath.isEmpty() ? tr("(tool default)") : nativePath(plan.finalOutputPath))
			: plan.errors.join(QStringLiteral(" ")));
	}

	if (m_buildPipelineDrawer) {
		QVector<DetailSection> sections;
		DetailSection planSection;
		planSection.id = QStringLiteral("plan");
		planSection.title = tr("Stage Plan");
		planSection.summary = tr("%n stage(s)", nullptr, plan.plannedStageCount);
		planSection.content = buildPipelineResultText(plan);
		planSection.state = displayState(plan.state);
		sections.push_back(planSection);
		m_buildPipelineDrawer->setSubtitle(plan.pipeline.displayName);
		m_buildPipelineDrawer->setSections(sections);
	}

	// Launch plan preview.
	if (m_launchSummary) {
		m_launchSummary->clear();
		GameInstallationProfile installation;
		bool haveInstallation = false;
		const QString selectedId = m_settings.selectedGameInstallationId();
		for (const GameInstallationProfile& profile : m_settings.gameInstallations()) {
			if (sameGameInstallationId(profile.id, selectedId) || !haveInstallation) {
				installation = profile;
				haveInstallation = true;
				if (sameGameInstallationId(profile.id, selectedId)) {
					break;
				}
			}
		}
		if (!haveInstallation) {
			m_launchSummary->addItem(disabledListItem(tr("No game installation is configured. Add one from the Workspace surface.")));
		} else {
			GameLaunchRequest launchRequest;
			launchRequest.launchProfileId = m_launchProfileChoice ? m_launchProfileChoice->currentData().toString() : QString();
			launchRequest.mapName = m_launchMapName ? m_launchMapName->text().trimmed() : QString();
			launchRequest.bspPath = plan.finalOutputPath;
			launchRequest.baseDirectory = installation.rootPath;
			launchRequest.dryRun = true;
			const GameLaunchPlan launchPlan = buildGameLaunchPlan(launchRequest, installation);
			for (const QString& line : gameLaunchPlanText(launchPlan).split('\n')) {
				if (!line.trimmed().isEmpty()) {
					m_launchSummary->addItem(line);
				}
			}
			if (m_launchGame) {
				m_launchGame->setEnabled(launchPlan.runnable);
			}
		}
	}

	if (m_buildPipelineRun) {
		m_buildPipelineRun->setEnabled(m_buildPipelineThread == nullptr && !request.inputPath.isEmpty());
	}
	if (m_buildPipelineCancel) {
		m_buildPipelineCancel->setEnabled(m_buildPipelineThread != nullptr);
	}
}

void ApplicationShell::runSelectedBuildPipeline()
{
	if (m_buildPipelineThread) {
		statusBar()->showMessage(tr("A build pipeline is already running."));
		return;
	}
	if (!m_buildPipelineChoice || !m_buildPipelineInput) {
		return;
	}
	const QString inputPath = m_buildPipelineInput->text().trimmed();
	if (inputPath.isEmpty()) {
		statusBar()->showMessage(tr("Choose a source map before running the pipeline."));
		return;
	}

	const QString projectPath = m_settings.currentProjectPath();
	BuildPipelineRequest request;
	request.pipelineId = m_buildPipelineChoice->currentData().toString();
	request.inputPath = inputPath;
	request.workspaceRootPath = projectPath;
	request.registerOutputs = !projectPath.isEmpty();
	const CompilerRegistryOptions registryOptions = compilerRegistryOptionsForProject(projectPath, m_settings);
	request.extraSearchPaths = registryOptions.extraSearchPaths;
	request.executableOverrides = registryOptions.executableOverrides;
	ProjectManifest manifest;
	if (loadProjectManifest(projectPath, &manifest) && !manifest.outputFolder.isEmpty()) {
		request.manifestDirectory = manifest.outputFolder;
	}

	m_buildPipelineCancelRequested.store(false);
	const QString taskId = m_activity.createTask(tr("Build Pipeline"), nativePath(inputPath), tr("build"), OperationState::Queued, true);
	m_buildPipelineActivityId = taskId;
	m_activity.transitionTask(taskId, OperationState::Running, tr("Running build pipeline."));
	refreshActivityCenter(taskId);
	if (m_buildPipelineState) {
		m_buildPipelineState->setState(OperationState::Running, tr("Running"));
	}

	const int stageCount = std::max(1, planBuildPipeline(request).plannedStageCount);
	auto* thread = QThread::create([this, request, taskId, projectPath, stageCount]() {
		BuildPipelineCallbacks callbacks;
		callbacks.cancellationRequested = [this]() {
			return m_buildPipelineCancelRequested.load();
		};
		callbacks.logEntry = [this, taskId](const CompilerTaskLogEntry& entry) {
			QMetaObject::invokeMethod(this, [this, taskId, entry]() {
				m_activity.appendLog(taskId, operationStateFromId(entry.level), entry.message);
				refreshActivityDetails(taskId);
			}, Qt::QueuedConnection);
		};
		callbacks.stageStarted = [this, taskId, stageCount](int stageIndex, const BuildPipelineStage& stage) {
			QMetaObject::invokeMethod(this, [this, taskId, stageIndex, stageCount, stage]() {
				m_activity.setProgress(taskId, stageIndex, stageCount);
				m_activity.appendLog(taskId, OperationState::Running, tr("Stage started: %1").arg(stage.displayName));
				refreshActivityDetails(taskId);
			}, Qt::QueuedConnection);
		};
		callbacks.stageFinished = [this, taskId, stageCount](int stageIndex, const BuildPipelineStageResult& result) {
			QMetaObject::invokeMethod(this, [this, taskId, stageIndex, stageCount, result]() {
				m_activity.setProgress(taskId, stageIndex + 1, stageCount);
				m_activity.appendLog(taskId, result.state,
					tr("Stage finished: %1").arg(result.stage.displayName));
				refreshActivityDetails(taskId);
			}, Qt::QueuedConnection);
		};

		const BuildPipelineResult result = runBuildPipeline(request, callbacks);
		QMetaObject::invokeMethod(this, [this, result, taskId, projectPath]() {
			finishBuildPipeline(result, taskId, projectPath);
		}, Qt::QueuedConnection);
	});
	m_buildPipelineThread = thread;
	connect(thread, &QThread::finished, thread, &QObject::deleteLater);
	connect(thread, &QThread::finished, this, [this]() {
		m_buildPipelineThread = nullptr;
		refreshCommandEnablement();
		if (m_buildPipelineCancel) {
			m_buildPipelineCancel->setEnabled(false);
		}
		if (m_buildPipelineRun) {
			m_buildPipelineRun->setEnabled(true);
		}
	});
	thread->start();
	refreshCommandEnablement();
	if (m_buildPipelineCancel) {
		m_buildPipelineCancel->setEnabled(true);
	}
	if (m_buildPipelineRun) {
		m_buildPipelineRun->setEnabled(false);
	}
}

void ApplicationShell::cancelBuildPipeline()
{
	if (!m_buildPipelineThread) {
		statusBar()->showMessage(tr("No build pipeline is running."));
		return;
	}
	m_buildPipelineCancelRequested.store(true);
	statusBar()->showMessage(tr("Cancellation requested. The current stage is being stopped."));
}

void ApplicationShell::finishBuildPipeline(const BuildPipelineResult& result, const QString& taskId, const QString& projectPath)
{
	QStringList warnings = result.warnings;
	for (const BuildPipelineStageResult& stage : result.stages) {
		if (stage.run.leakDetected) {
			warnings << tr("Stage %1 leaked: %2")
				.arg(stage.stage.displayName,
					stage.run.leakOccupantClassname.isEmpty() ? tr("a leak point file was written") : stage.run.leakOccupantClassname);
		}
	}

	for (const QString& warning : warnings) {
		m_activity.appendWarning(taskId, warning);
	}
	// Warning, Failed and Cancelled are all terminal in the state model, so the
	// terminal transition has to be chosen once rather than completed first and
	// corrected afterwards.
	if (result.cancelled) {
		m_activity.cancelTask(taskId, tr("Cancelled by the user."));
	} else if (result.failedStageCount > 0) {
		m_activity.failTask(taskId, result.errors.isEmpty()
			? tr("%n stage(s) failed.", nullptr, result.failedStageCount)
			: result.errors.join(QStringLiteral(" ")));
	} else {
		m_activity.completeTask(taskId, buildPipelineResultText(result));
	}
	persistActivityTask(taskId);
	refreshActivityCenter(taskId);

	if (!projectPath.isEmpty() && !result.registeredOutputPaths.isEmpty()) {
		ProjectManifest manifest;
		if (loadProjectManifest(projectPath, &manifest)) {
			registerProjectOutputPaths(&manifest, result.registeredOutputPaths);
			saveProjectManifest(manifest);
		}
	}

	if (m_buildPipelineDrawer) {
		QVector<DetailSection> sections;
		DetailSection summary;
		summary.id = QStringLiteral("result");
		summary.title = tr("Result");
		summary.summary = localizedOperationStateName(result.state);
		summary.content = buildPipelineResultText(result);
		summary.state = result.state;
		sections.push_back(summary);

		for (const BuildPipelineStageResult& stage : result.stages) {
			DetailSection stageSection;
			stageSection.id = QStringLiteral("stage-%1").arg(stage.stage.id);
			stageSection.title = stage.stage.displayName;
			stageSection.summary = stage.skipped ? stage.skipReason : localizedOperationStateName(stage.state);
			QStringList content;
			content << stage.plan.commandLine;
			if (!stage.run.stdoutText.isEmpty()) {
				content << tr("--- stdout ---") << stage.run.stdoutText;
			}
			if (!stage.run.stderrText.isEmpty()) {
				content << tr("--- stderr ---") << stage.run.stderrText;
			}
			stageSection.content = content.join('\n');
			stageSection.state = stage.state;
			sections.push_back(stageSection);
		}
		m_buildPipelineDrawer->setSections(sections);
	}

	refreshBuildSurface();
	refreshWorkspaceDashboard();
	refreshStatusChips();
	statusBar()->showMessage(result.succeeded()
		? tr("Build pipeline finished: %1").arg(result.finalOutputPath.isEmpty() ? tr("no artifact path reported") : nativePath(result.finalOutputPath))
		: tr("Build pipeline did not complete. Open the Activity tab for the stage logs."));
}

void ApplicationShell::copyBuildPipelineCommands()
{
	if (!m_buildPipelineChoice || !m_buildPipelineInput) {
		return;
	}
	BuildPipelineRequest request;
	request.pipelineId = m_buildPipelineChoice->currentData().toString();
	request.inputPath = m_buildPipelineInput->text().trimmed();
	request.workspaceRootPath = m_settings.currentProjectPath();
	request.dryRun = true;
	const BuildPipelineResult plan = planBuildPipeline(request);

	QStringList commands;
	for (const BuildPipelineStageResult& stage : plan.stages) {
		if (stage.skipped) {
			continue;
		}
		commands << stage.plan.commandLine;
	}
	if (commands.isEmpty()) {
		statusBar()->showMessage(tr("No runnable stages to copy."));
		return;
	}
	QApplication::clipboard()->setText(commands.join('\n'));
	statusBar()->showMessage(tr("%n stage command(s) copied.", nullptr, static_cast<int>(commands.size())));
}

void ApplicationShell::inspectCompiledArtifacts()
{
	QString bspPath;
	if (m_buildPipelineChoice && m_buildPipelineInput) {
		BuildPipelineRequest request;
		request.pipelineId = m_buildPipelineChoice->currentData().toString();
		request.inputPath = m_buildPipelineInput->text().trimmed();
		request.workspaceRootPath = m_settings.currentProjectPath();
		request.dryRun = true;
		bspPath = planBuildPipeline(request).finalOutputPath;
	}
	if (bspPath.isEmpty() || !QFileInfo::exists(bspPath)) {
		bspPath = QFileDialog::getOpenFileName(this, tr("Inspect Compiled Map"), QString(), tr("Compiled maps (*.bsp);;All files (*.*)"));
	}
	if (bspPath.isEmpty()) {
		return;
	}

	const CompiledMapArtifacts artifacts = inspectCompiledMapArtifacts(bspPath);
	if (m_buildPipelineDrawer) {
		QVector<DetailSection> sections;
		DetailSection summary;
		summary.id = QStringLiteral("bsp");
		summary.title = tr("Compiled Artifact");
		summary.summary = artifacts.bsp.valid
			? tr("%1 version %2").arg(bspFamilyDisplayName(artifacts.bsp.family)).arg(artifacts.bsp.version)
			: tr("Not a recognized BSP");
		summary.content = compiledMapArtifactsText(artifacts);
		summary.state = artifacts.bsp.state();
		sections.push_back(summary);
		m_buildPipelineDrawer->setSubtitle(nativePath(bspPath));
		m_buildPipelineDrawer->setSections(sections);
		m_buildPipelineDrawer->showSection(QStringLiteral("bsp"));
	}

	QStringList warnings = artifacts.warnings;
	warnings << artifacts.bsp.warnings;
	if (artifacts.hasLeakFile) {
		warnings.prepend(tr("This map leaked: %1 points in %2.")
			.arg(artifacts.leak.pointCount)
			.arg(nativePath(artifacts.leak.sourcePath)));
	}
	recordActivity(tr("Inspect Artifacts"), nativePath(bspPath), tr("build"),
		artifacts.bsp.state(), compiledMapArtifactsText(artifacts), warnings);
	statusBar()->showMessage(artifacts.hasLeakFile
		? tr("Leak detected. The leak point file is listed in the build details.")
		: tr("Artifact inspected: %1").arg(nativePath(bspPath)));
}

void ApplicationShell::launchConfiguredGame()
{
	GameInstallationProfile installation;
	bool haveInstallation = false;
	const QString selectedId = m_settings.selectedGameInstallationId();
	for (const GameInstallationProfile& profile : m_settings.gameInstallations()) {
		if (!haveInstallation || sameGameInstallationId(profile.id, selectedId)) {
			installation = profile;
			haveInstallation = true;
			if (sameGameInstallationId(profile.id, selectedId)) {
				break;
			}
		}
	}
	if (!haveInstallation) {
		QMessageBox::information(this, tr("No Game Installation"),
			tr("Add a game installation on the Workspace surface before launching."));
		return;
	}

	GameLaunchRequest request;
	request.launchProfileId = m_launchProfileChoice ? m_launchProfileChoice->currentData().toString() : QString();
	request.mapName = m_launchMapName ? m_launchMapName->text().trimmed() : QString();
	request.baseDirectory = installation.rootPath;
	// Doom-family profiles load the built artifact with -file, so the pipeline's
	// final output has to reach the real launch, not only the preview.
	if (m_buildPipelineChoice && m_buildPipelineInput) {
		BuildPipelineRequest pipelineRequest;
		pipelineRequest.pipelineId = m_buildPipelineChoice->currentData().toString();
		pipelineRequest.inputPath = m_buildPipelineInput->text().trimmed();
		pipelineRequest.workspaceRootPath = m_settings.currentProjectPath();
		pipelineRequest.dryRun = true;
		request.bspPath = planBuildPipeline(pipelineRequest).finalOutputPath;
	}
	request.dryRun = false;
	const GameLaunchPlan plan = buildGameLaunchPlan(request, installation);
	if (!plan.runnable) {
		QMessageBox::warning(this, tr("Cannot Launch"),
			plan.errors.isEmpty() ? tr("The launch plan is incomplete.") : plan.errors.join('\n'));
		return;
	}

	// Starting an external process is the studio's most outward-facing action,
	// so the exact command line is confirmed first.
	if (!confirmDestructiveAction(tr("Launch Game"),
			tr("Start the configured game with this command?\n\n%1\n\nWorking directory: %2")
				.arg(plan.commandLine, nativePath(plan.workingDirectory)),
			tr("Launch"))) {
		return;
	}

	qint64 pid = 0;
	QString error;
	if (!startGameLaunch(plan, &pid, &error)) {
		QMessageBox::warning(this, tr("Launch Failed"), error);
		recordActivity(tr("Launch Game"), plan.commandLine, tr("game"), OperationState::Failed, error);
		return;
	}
	recordActivity(tr("Launch Game"), plan.commandLine, tr("game"), OperationState::Completed,
		tr("Started process %1.").arg(pid), plan.warnings);
	statusBar()->showMessage(tr("Game launched (process %1).").arg(pid));
}

// ---------------------------------------------------------------------------
// Miscellaneous shell behaviour
// ---------------------------------------------------------------------------

void ApplicationShell::closePackage()
{
	if (!m_packageArchive.isOpen()) {
		return;
	}
	if (m_packageStaging.operations().size() > 0
		&& !confirmDestructiveAction(tr("Discard Staged Changes"),
			tr("%n staged package change(s) have not been written. Close the package and discard them?",
				nullptr, m_packageStaging.operations().size()),
			tr("Discard"))) {
		return;
	}
	m_packageArchive.clear();
	m_packageStaging.clear();
	m_packageBrowseSource.clear();
	m_packageBrowseFolder.clear();
	m_packageFolderHistory.clear();
	m_packageFolderHistoryIndex = -1;
	invalidatePaletteResolution();
	refreshPackageBrowser();
	refreshTextureBrowser();
	refreshModelBrowser();
	refreshAudioBrowser();
	refreshStatusChips();
	refreshCommandEnablement();
	statusBar()->showMessage(tr("Package closed."));
}

void ApplicationShell::copyDiagnosticBundle()
{
	QStringList lines;
	lines << tr("VibeStudio diagnostic bundle");
	lines << tr("Version: %1").arg(versionString());
	lines << tr("Qt runtime: %1").arg(QString::fromLatin1(qVersion()));
	lines << tr("Platform: %1 (%2)").arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture());
	lines << tr("Locale: %1").arg(m_settings.accessibilityPreferences().localeName);
	lines << tr("Theme: %1").arg(localizedThemeName(m_settings.accessibilityPreferences().theme));
	lines << tr("Project: %1").arg(m_settings.currentProjectPath().isEmpty() ? tr("(none)") : nativePath(m_settings.currentProjectPath()));
	lines << tr("Package: %1").arg(m_packageArchive.isOpen() ? nativePath(m_packageArchive.sourcePath()) : tr("(none)"));
	lines << tr("Session log: %1").arg(sessionLogFilePath().isEmpty() ? tr("(not writable)") : nativePath(sessionLogFilePath()));

	const CompilerRegistrySummary registry = discoverCompilerTools(compilerRegistryOptionsForProject(m_settings.currentProjectPath(), m_settings));
	lines << tr("Compilers:");
	for (const CompilerToolDiscovery& tool : registry.tools) {
		lines << tr("  %1: %2").arg(tool.descriptor.id,
			tool.executableAvailable ? nativePath(tool.executablePath) : tr("not found"));
	}

	const QStringList conflicts = m_commands ? m_commands->shortcutConflicts() : QStringList();
	if (!conflicts.isEmpty()) {
		lines << tr("Shortcut conflicts:");
		lines << conflicts;
	}

	lines << tr("Recent session log lines:");
	lines << recentSessionLogLines(60);

	QApplication::clipboard()->setText(lines.join('\n'));
	recordActivity(tr("Diagnostic Bundle"), tr("Copied to clipboard"), tr("diagnostics"), OperationState::Completed,
		tr("A redacted diagnostic bundle was copied to the clipboard."));
	statusBar()->showMessage(tr("Diagnostic bundle copied to the clipboard."));
}

void ApplicationShell::showAboutDialog()
{
	QStringList lines;
	lines << tr("VibeStudio %1").arg(versionString());
	lines << tr("An open-source development studio for idTech1, idTech2, and idTech3 game projects.");
	lines << QString();
	lines << tr("Qt runtime: %1").arg(QString::fromLatin1(qVersion()));
	lines << tr("Platform: %1").arg(QSysInfo::prettyProductName());
	lines << QString();
	lines << tr("Imported compilers:");
	for (const CompilerIntegration& integration : compilerIntegrations()) {
		lines << tr("  %1 (%2) — %3").arg(integration.displayName, integration.pinnedRevision.left(12), integration.upstreamUrl);
	}
	lines << QString();
	lines << tr("Structural and archive-tooling reference: PakFu.");
	lines << tr("Editor workflow inspirations: GtkRadiant, NetRadiant Custom, TrenchBroom, and QuArK.");
	lines << tr("Full attribution lives in README.md and docs/CREDITS.md.");

	QMessageBox box(this);
	box.setWindowTitle(tr("About VibeStudio"));
	box.setIconPixmap(studioApplicationIcon().pixmap(64, 64));
	box.setText(lines.join('\n'));
	box.setTextInteractionFlags(Qt::TextBrowserInteraction);
	box.addButton(QMessageBox::Close);
	box.exec();
}

void ApplicationShell::scheduleThemeRefresh()
{
	// List refreshes only need their per-item state colours re-applied. That is
	// cheap, but coalescing it still keeps a streamed log or a filter box from
	// re-walking every list on each change.
	if (m_themeRefreshScheduled) {
		return;
	}
	m_themeRefreshScheduled = true;
	QMetaObject::invokeMethod(this, [this]() {
		m_themeRefreshScheduled = false;
		applyStateColors();
	}, Qt::QueuedConnection);
}

void ApplicationShell::scheduleWorkspaceSearch()
{
	// The search walks the project tree, so it must not run on every keystroke.
	if (m_workspaceSearchScheduled) {
		return;
	}
	m_workspaceSearchScheduled = true;
	QTimer::singleShot(220, this, [this]() {
		m_workspaceSearchScheduled = false;
		refreshWorkspaceSearch();
	});
}

void ApplicationShell::showPackageEntryContextMenu(const QPoint& position)
{
	if (!m_packageEntries) {
		return;
	}
	const QStringList selected = selectedPackageEntryPaths();
	QMenu menu(this);
	menu.setAccessibleName(tr("Package entry actions"));

	QAction* extract = menu.addAction(tr("Extract Selected…"));
	extract->setEnabled(!selected.isEmpty());
	QAction* replace = menu.addAction(tr("Stage Replace…"));
	replace->setEnabled(selected.size() == 1);
	QAction* rename = menu.addAction(tr("Stage Rename…"));
	rename->setEnabled(selected.size() == 1);
	QAction* remove = menu.addAction(tr("Stage Delete"));
	remove->setEnabled(!selected.isEmpty());
	menu.addSeparator();
	QAction* copyPath = menu.addAction(tr("Copy Virtual Path"));
	copyPath->setEnabled(!selected.isEmpty());

	QAction* chosen = menu.exec(m_packageEntries->viewport()->mapToGlobal(position));
	if (chosen == extract) {
		extractSelectedPackageEntries();
	} else if (chosen == replace) {
		stagePackageReplaceSelected();
	} else if (chosen == rename) {
		stagePackageRenameSelected();
	} else if (chosen == remove) {
		stagePackageDeleteSelected();
	} else if (chosen == copyPath) {
		QApplication::clipboard()->setText(selected.join('\n'));
		statusBar()->showMessage(tr("%n virtual path(s) copied.", nullptr, static_cast<int>(selected.size())));
	}
}

void ApplicationShell::applyPreferencesToWidgets()
{
	if (m_commands) {
		m_commands->applyEditorProfile(m_settings.selectedEditorProfileId());
	}
	const AccessibilityPreferences preferences = m_settings.accessibilityPreferences();
	const bool highContrast = preferences.theme == StudioTheme::HighContrastDark
		|| preferences.theme == StudioTheme::HighContrastLight;
	const bool light = preferences.theme == StudioTheme::Light || preferences.theme == StudioTheme::HighContrastLight;

	if (m_levelMapViewport) {
		m_levelMapViewport->setHighContrast(highContrast);
		m_levelMapViewport->setReducedMotion(preferences.reducedMotion);
	}
	for (ImagePreviewView* view : {m_texturePreview, m_modelSkinPreview, m_packageImagePreview}) {
		if (view) {
			view->setHighContrast(highContrast);
		}
	}
	if (m_texturePalette) {
		m_texturePalette->update();
	}
	if (m_audioWaveform) {
		m_audioWaveform->setHighContrast(highContrast);
	}
	if (m_modelViewport) {
		m_modelViewport->setHighContrast(highContrast);
		m_modelViewport->setReducedMotion(preferences.reducedMotion);
	}
	if (m_packageCompositionChart) {
		m_packageCompositionChart->setHighContrast(highContrast);
	}
	if (m_compilerPipelineChart) {
		m_compilerPipelineChart->setHighContrast(highContrast);
	}
	if (m_buildPipelineChart) {
		m_buildPipelineChart->setHighContrast(highContrast);
	}
	if (m_activityTimelineChart) {
		m_activityTimelineChart->setHighContrast(highContrast);
	}
	if (m_codeHighlighter) {
		m_codeHighlighter->setTheme(studioSyntaxTheme(light, highContrast));
	}
	if (m_codeEditor) {
		m_codeEditor->setFont(studioMonospaceFont());
		m_codeEditor->setTabStopDistance(QFontMetricsF(m_codeEditor->font()).horizontalAdvance(QLatin1Char(' ')) * 4.0);
	}
}


void ApplicationShell::runSelfTest()
{
	// Visit every mode so each page is built, laid out, refreshed, and painted
	// at least once. Anything that crashes or asserts on an empty project shows
	// up here rather than in front of a user.
	const QVector<StudioMode> modes = {
		StudioMode::Workspace, StudioMode::Levels, StudioMode::Models, StudioMode::Textures,
		StudioMode::Audio, StudioMode::Packages, StudioMode::Code, StudioMode::Shaders,
		StudioMode::Build, StudioMode::Settings,
	};
	for (const StudioMode mode : modes) {
		setMode(mode);
		if (m_modeStack) {
			m_modeStack->currentWidget()->repaint();
		}
		QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 25);
	}

	refreshWorkspaceDashboard();
	refreshWorkspaceContextPanels();
	refreshPackageBrowser();
	refreshCompilerPipelineSummary();
	refreshBuildSurface();
	refreshLevelMapWorkbench();
	refreshAdvancedStudioSurface();
	refreshTextureBrowser();
	refreshModelBrowser();
	refreshAudioBrowser();
	refreshCodeWorkspaceTree();
	refreshActivityCenter();
	refreshStatusChips();
	refreshCommandEnablement();
	updateInspector();
	applyPreferencesToUi();

	const QStringList conflicts = m_commands ? m_commands->shortcutConflicts() : QStringList();
	for (const QString& conflict : conflicts) {
		qWarning("Shortcut conflict: %s", qPrintable(conflict));
	}

	setMode(StudioMode::Workspace);
	statusBar()->showMessage(tr("Self-test visited %n work surface(s).", nullptr, static_cast<int>(modes.size())));
}

void ApplicationShell::openPathFromCommandLine(const QString& path)
{
	openDroppedPath(QFileInfo(path).absoluteFilePath());
}

QStringList ApplicationShell::captureUiSnapshots(const QString& directory)
{
	QStringList written;
	if (!QDir().mkpath(directory)) {
		return written;
	}
	const StudioMode originalMode = currentMode();
	// Documentation snapshots show the level inspector populated: with a map
	// loaded and nothing selected, pick its first non-world entity.
	if (m_levelMapObjects && m_levelMapDocument.format != LevelMapFormat::Unknown && m_levelMapObjects->selectedItems().isEmpty()) {
		const int row = m_levelMapObjects->count() > 1 ? 1 : 0;
		if (QListWidgetItem* item = m_levelMapObjects->item(row); item && item->flags().testFlag(Qt::ItemIsSelectable)) {
			m_levelMapObjects->setCurrentItem(item);
		}
	}
	// Likewise the shader details: open on the first stage, not the report.
	if (m_advancedShaderGraph && !m_advancedShaderGraph->currentItem()) {
		for (QTreeWidgetItemIterator it(m_advancedShaderGraph); *it; ++it) {
			QTreeWidgetItem* item = *it;
			const bool stageRow = item->parent() && !item->parent()->parent();
			if (stageRow) {
				m_advancedShaderGraph->setCurrentItem(item);
				break;
			}
		}
	}
	const QVector<StudioModeDescriptor> descriptors = studioModeDescriptors();
	for (int index = 0; index < descriptors.size(); ++index) {
		setMode(descriptors[index].mode);
		// Two passes: the first lays out the freshly shown page, the second lets
		// any deferred refresh it queued paint before the grab.
		QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
		QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
		const QString slug = modeCommandId(descriptors[index].mode).section(QLatin1Char('.'), -1);
		const QString fileName = QStringLiteral("%1-%2.png").arg(index + 1, 2, 10, QLatin1Char('0')).arg(slug);
		const QString filePath = QDir(directory).filePath(fileName);
		if (grab().save(filePath)) {
			written.push_back(filePath);
		}
	}
	// One more frame with the Activity panel docked, so the panels are
	// documented too; the panel is closed again afterwards.
	if (m_activityDock) {
		setMode(StudioMode::Workspace);
		const bool wasVisible = m_activityDock->isVisible();
		m_activityDock->show();
		m_activityDock->raise();
		QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
		QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
		const QString filePath = QDir(directory).filePath(QStringLiteral("%1-activity-panel.png").arg(descriptors.size() + 1, 2, 10, QLatin1Char('0')));
		if (grab().save(filePath)) {
			written.push_back(filePath);
		}
		m_activityDock->setVisible(wasVisible);
	}
	setMode(originalMode);
	return written;
}

} // namespace vibestudio
