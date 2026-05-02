#include "app/application_shell.h"

#include "app/ui_primitives.h"
#include "core/ai_connectors.h"
#include "core/ai_workflows.h"
#include "core/compiler_profiles.h"
#include "core/compiler_registry.h"
#include "core/compiler_runner.h"
#include "core/editor_profiles.h"
#include "core/package_preview.h"
#include "core/project_manifest.h"
#include "core/studio_manifest.h"
#include <QAbstractItemView>
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
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHash>
#include <QHBoxLayout>
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
#include <QTabWidget>
#include <QTextEdit>
#include <QThread>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace vibestudio {

namespace {

QLabel* sectionLabel(const QString& text)
{
	auto* label = new QLabel(text);
	label->setObjectName("sectionLabel");
	return label;
}

QFrame* modulePanel(const StudioModule& module)
{
	auto* frame = new QFrame;
	frame->setObjectName("modulePanel");
	frame->setAccessibleName(module.name);
	frame->setAccessibleDescription(module.description);
	auto* layout = new QVBoxLayout(frame);
	layout->setContentsMargins(14, 12, 14, 12);
	layout->setSpacing(6);

	auto* name = new QLabel(module.name);
	name->setObjectName("moduleTitle");
	auto* meta = new QLabel(module.category + " / " + module.maturity);
	meta->setObjectName("moduleMeta");
	auto* description = new QLabel(module.description);
	description->setWordWrap(true);

	layout->addWidget(name);
	layout->addWidget(meta);
	layout->addWidget(description);
	layout->addStretch(1);
	return frame;
}

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
	case PackagePreviewKind::Binary:
		return ApplicationShell::tr("Binary");
	}
	return ApplicationShell::tr("Unavailable");
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
	if (m_compilerRunThread) {
		m_compilerRunCancelRequested.store(true);
		m_compilerRunThread->quit();
		m_compilerRunThread->wait(1500);
		m_compilerRunThread = nullptr;
	}
	saveShellState();
	m_settings.sync();
}

void ApplicationShell::buildUi()
{
	setWindowTitle(tr("VibeStudio"));
	resize(1280, 800);

	auto* root = new QWidget;
	auto* rootLayout = new QHBoxLayout(root);
	rootLayout->setContentsMargins(0, 0, 0, 0);
	rootLayout->setSpacing(0);

	m_modeRail = new QListWidget;
	m_modeRail->setObjectName("modeRail");
	m_modeRail->setAccessibleName(tr("Mode rail"));
	m_modeRail->setAccessibleDescription(tr("Switches between studio work surfaces."));
	m_modeRail->setFixedWidth(190);
	m_modeRail->addItems({
		tr("Workspace"),
		tr("Levels"),
		tr("Models"),
		tr("Textures"),
		tr("Audio"),
		tr("Packages"),
		tr("Code"),
		tr("Shaders"),
		tr("Build"),
	});
	rootLayout->addWidget(m_modeRail);

	auto* splitter = new QSplitter(Qt::Horizontal);
	rootLayout->addWidget(splitter, 1);

	auto* center = new QWidget;
	auto* centerLayout = new QVBoxLayout(center);
	centerLayout->setContentsMargins(22, 18, 22, 18);
	centerLayout->setSpacing(16);

	auto* topRow = new QHBoxLayout;
	auto* title = new QLabel(tr("VibeStudio"));
	title->setObjectName("appTitle");
	auto* subtitle = new QLabel(tr("Integrated development studio for idTech1-3 projects"));
	subtitle->setObjectName("appSubtitle");
	auto* titleStack = new QVBoxLayout;
	titleStack->addWidget(title);
	titleStack->addWidget(subtitle);
	topRow->addLayout(titleStack, 1);

	auto* openProject = new QPushButton(style()->standardIcon(QStyle::SP_DirOpenIcon), tr("Open Project"));
	openProject->setAccessibleName(tr("Open project folder"));
	openProject->setToolTip(tr("Choose a project folder and add it to recent projects."));
	connect(openProject, &QPushButton::clicked, this, [this]() {
		openProjectFolder();
	});
	topRow->addWidget(openProject);

	auto* detectInstalls = new QPushButton(style()->standardIcon(QStyle::SP_FileDialogContentsView), tr("Detect Installs"));
	detectInstalls->setAccessibleName(tr("Detect game installations"));
	detectInstalls->setToolTip(tr("Scan common Steam and GOG library roots for confirmable game installation candidates."));
	connect(detectInstalls, &QPushButton::clicked, this, [this]() {
		detectGameInstallationProfiles();
	});
	topRow->addWidget(detectInstalls);
	centerLayout->addLayout(topRow);

	auto* workspaceHeader = new QHBoxLayout;
	workspaceHeader->addWidget(sectionLabel(tr("Workspace Dashboard")));
	auto* initManifest = new QPushButton(style()->standardIcon(QStyle::SP_FileDialogDetailedView), tr("Initialize Manifest"));
	initManifest->setAccessibleName(tr("Initialize project manifest"));
	initManifest->setToolTip(tr("Create or refresh the .vibestudio/project.json manifest for the current project folder."));
	connect(initManifest, &QPushButton::clicked, this, [this]() {
		initializeCurrentProjectManifest();
	});
	workspaceHeader->addStretch(1);
	workspaceHeader->addWidget(initManifest);
	centerLayout->addLayout(workspaceHeader);

	m_workspaceState = new LoadingPane;
	m_workspaceState->setAccessibleName(tr("Workspace dashboard state"));
	m_workspaceState->setPlaceholderRows({
		tr("Project manifest"),
		tr("Project health"),
		tr("Linked installation"),
	});
	centerLayout->addWidget(m_workspaceState);

	m_workspaceDrawer = new DetailDrawer;
	m_workspaceDrawer->setAccessibleName(tr("Workspace dashboard details"));
	m_workspaceDrawer->setTitle(tr("Workspace Details"));
	m_workspaceDrawer->setSubtitle(tr("Open a project folder to inspect manifest and health details."));
	centerLayout->addWidget(m_workspaceDrawer);

	auto* workspaceTabs = new QTabWidget;
	workspaceTabs->setObjectName("workspaceContextTabs");
	workspaceTabs->setAccessibleName(tr("Workspace context panels"));
	workspaceTabs->setAccessibleDescription(tr("Problems, search, changed files, dependency graph, and recent activity for the active workspace."));

	m_projectProblems = new QListWidget;
	m_projectProblems->setObjectName("projectProblems");
	m_projectProblems->setAccessibleName(tr("Project problems"));
	m_projectProblems->setAccessibleDescription(tr("Project health warnings, blocking issues, and next actions."));
	m_projectProblems->setMinimumHeight(118);
	workspaceTabs->addTab(m_projectProblems, tr("Problems"));

	auto* searchPanel = new QWidget;
	auto* searchLayout = new QVBoxLayout(searchPanel);
	searchLayout->setContentsMargins(0, 0, 0, 0);
	searchLayout->setSpacing(8);
	m_workspaceSearch = new QLineEdit;
	m_workspaceSearch->setAccessibleName(tr("Workspace search"));
	m_workspaceSearch->setAccessibleDescription(tr("Searches mounted package entries and project files by path."));
	m_workspaceSearch->setPlaceholderText(tr("Search project files and mounted package entries"));
	connect(m_workspaceSearch, &QLineEdit::textChanged, this, [this]() {
		refreshWorkspaceSearch();
	});
	searchLayout->addWidget(m_workspaceSearch);
	m_workspaceSearchResults = new QListWidget;
	m_workspaceSearchResults->setObjectName("workspaceSearchResults");
	m_workspaceSearchResults->setAccessibleName(tr("Workspace search results"));
	m_workspaceSearchResults->setAccessibleDescription(tr("Matching project file paths and mounted package virtual paths."));
	m_workspaceSearchResults->setMinimumHeight(118);
	searchLayout->addWidget(m_workspaceSearchResults);
	auto* searchActions = new QHBoxLayout;
	searchActions->addStretch(1);
	m_revealWorkspacePath = new QPushButton(style()->standardIcon(QStyle::SP_DirOpenIcon), tr("Reveal"));
	m_revealWorkspacePath->setAccessibleName(tr("Reveal selected workspace path"));
	m_revealWorkspacePath->setToolTip(tr("Open the containing folder for the selected project file."));
	connect(m_revealWorkspacePath, &QPushButton::clicked, this, [this]() {
		revealSelectedWorkspacePath();
	});
	searchActions->addWidget(m_revealWorkspacePath);
	m_copyWorkspaceVirtualPath = new QPushButton(style()->standardIcon(QStyle::SP_FileDialogDetailedView), tr("Copy Path"));
	m_copyWorkspaceVirtualPath->setAccessibleName(tr("Copy selected virtual path"));
	m_copyWorkspaceVirtualPath->setToolTip(tr("Copy the selected project-relative or package virtual path."));
	connect(m_copyWorkspaceVirtualPath, &QPushButton::clicked, this, [this]() {
		copySelectedWorkspaceVirtualPath();
	});
	searchActions->addWidget(m_copyWorkspaceVirtualPath);
	searchLayout->addLayout(searchActions);
	workspaceTabs->addTab(searchPanel, tr("Search"));

	m_changedFiles = new QListWidget;
	m_changedFiles->setObjectName("changedFiles");
	m_changedFiles->setAccessibleName(tr("Changed and staged files"));
	m_changedFiles->setAccessibleDescription(tr("Git changed and staged files for the active project, when available."));
	m_changedFiles->setMinimumHeight(118);
	workspaceTabs->addTab(m_changedFiles, tr("Changes"));

	m_dependencyGraph = new QListWidget;
	m_dependencyGraph->setObjectName("dependencyGraph");
	m_dependencyGraph->setAccessibleName(tr("Project dependency graph"));
	m_dependencyGraph->setAccessibleDescription(tr("Placeholder dependency graph nodes for project roots, installs, packages, and compilers."));
	m_dependencyGraph->setMinimumHeight(118);
	workspaceTabs->addTab(m_dependencyGraph, tr("Graph"));

	m_recentActivityTimeline = new QListWidget;
	m_recentActivityTimeline->setObjectName("recentActivityTimeline");
	m_recentActivityTimeline->setAccessibleName(tr("Recent activity timeline"));
	m_recentActivityTimeline->setAccessibleDescription(tr("Recent project, package, setup, and task events."));
	m_recentActivityTimeline->setMinimumHeight(118);
	workspaceTabs->addTab(m_recentActivityTimeline, tr("Timeline"));

	centerLayout->addWidget(workspaceTabs);

	centerLayout->addWidget(sectionLabel(tr("First-Run Setup")));

	auto* setupPanel = new QFrame;
	setupPanel->setObjectName("setupPanel");
	setupPanel->setAccessibleName(tr("First-run setup"));
	setupPanel->setAccessibleDescription(tr("Setup status, current step, warnings, and actions."));
	auto* setupLayout = new QVBoxLayout(setupPanel);
	setupLayout->setContentsMargins(14, 12, 14, 12);
	setupLayout->setSpacing(8);

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
	m_setupSummary->setAccessibleDescription(tr("Completed, pending, and warning items for first-run setup."));
	m_setupSummary->setMinimumHeight(136);
	setupLayout->addWidget(m_setupSummary);

	auto* setupActions = new QHBoxLayout;
	setupActions->addStretch(1);
	m_setupStartResume = new QPushButton(style()->standardIcon(QStyle::SP_MediaPlay), tr("Start"));
	m_setupStartResume->setAccessibleName(tr("Start or resume setup"));
	connect(m_setupStartResume, &QPushButton::clicked, this, [this]() {
		startOrResumeSetup();
	});
	setupActions->addWidget(m_setupStartResume);

	m_setupNext = new QPushButton(style()->standardIcon(QStyle::SP_ArrowForward), tr("Next"));
	m_setupNext->setAccessibleName(tr("Advance setup step"));
	connect(m_setupNext, &QPushButton::clicked, this, [this]() {
		advanceSetup();
	});
	setupActions->addWidget(m_setupNext);

	m_setupSkip = new QPushButton(style()->standardIcon(QStyle::SP_DialogCloseButton), tr("Skip"));
	m_setupSkip->setAccessibleName(tr("Skip setup for now"));
	connect(m_setupSkip, &QPushButton::clicked, this, [this]() {
		skipSetup();
	});
	setupActions->addWidget(m_setupSkip);

	m_setupComplete = new QPushButton(style()->standardIcon(QStyle::SP_DialogApplyButton), tr("Finish"));
	m_setupComplete->setAccessibleName(tr("Finish setup"));
	connect(m_setupComplete, &QPushButton::clicked, this, [this]() {
		completeSetup();
	});
	setupActions->addWidget(m_setupComplete);

	m_setupReset = new QPushButton(style()->standardIcon(QStyle::SP_BrowserReload), tr("Reset"));
	m_setupReset->setAccessibleName(tr("Reset setup progress"));
	connect(m_setupReset, &QPushButton::clicked, this, [this]() {
		resetSetup();
	});
	setupActions->addWidget(m_setupReset);
	setupLayout->addLayout(setupActions);

	centerLayout->addWidget(setupPanel);

	auto* recentHeader = new QHBoxLayout;
	recentHeader->addWidget(sectionLabel(tr("Recent Projects")));
	m_recentSummary = new QLabel;
	m_recentSummary->setObjectName("panelMeta");
	recentHeader->addWidget(m_recentSummary, 1, Qt::AlignRight);
	centerLayout->addLayout(recentHeader);

	m_recentProjects = new QListWidget;
	m_recentProjects->setObjectName("recentProjects");
	m_recentProjects->setAccessibleName(tr("Recent projects"));
	m_recentProjects->setAccessibleDescription(tr("Project folders remembered from previous sessions."));
	m_recentProjects->setSelectionMode(QAbstractItemView::SingleSelection);
	m_recentProjects->setUniformItemSizes(false);
	m_recentProjects->setMinimumHeight(118);
	connect(m_recentProjects, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
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
	centerLayout->addWidget(m_recentProjects);

	auto* recentActions = new QHBoxLayout;
	recentActions->addStretch(1);
	auto* removeRecent = new QPushButton(style()->standardIcon(QStyle::SP_DialogDiscardButton), tr("Remove"));
	removeRecent->setAccessibleName(tr("Remove selected recent project"));
	removeRecent->setToolTip(tr("Remove the selected project from the recent list without touching its files."));
	connect(removeRecent, &QPushButton::clicked, this, [this]() {
		removeSelectedRecentProject();
	});
	recentActions->addWidget(removeRecent);

	auto* clearRecent = new QPushButton(style()->standardIcon(QStyle::SP_TrashIcon), tr("Clear"));
	clearRecent->setAccessibleName(tr("Clear recent projects"));
	clearRecent->setToolTip(tr("Clear the recent-project list without touching project files."));
	connect(clearRecent, &QPushButton::clicked, this, [this]() {
		clearRecentProjects();
	});
	recentActions->addWidget(clearRecent);
	centerLayout->addLayout(recentActions);

	auto* installHeader = new QHBoxLayout;
	installHeader->addWidget(sectionLabel(tr("Game Installations")));
	m_installSummary = new QLabel;
	m_installSummary->setObjectName("panelMeta");
	m_installSummary->setAccessibleName(tr("Game installation summary"));
	installHeader->addWidget(m_installSummary, 1, Qt::AlignRight);
	centerLayout->addLayout(installHeader);

	m_gameInstallations = new QListWidget;
	m_gameInstallations->setObjectName("gameInstallations");
	m_gameInstallations->setAccessibleName(tr("Game installations"));
	m_gameInstallations->setAccessibleDescription(tr("Manual read-only game installation profiles used by projects, packages, compilers, and launch workflows."));
	m_gameInstallations->setSelectionMode(QAbstractItemView::SingleSelection);
	m_gameInstallations->setUniformItemSizes(false);
	m_gameInstallations->setMinimumHeight(112);
	connect(m_gameInstallations, &QListWidget::itemSelectionChanged, this, [this]() {
		if (m_importDetectedInstall) {
			m_importDetectedInstall->setEnabled(selectedDetectedInstallationIndex() >= 0);
		}
		refreshInspectorDrawerForSettings();
	});
	centerLayout->addWidget(m_gameInstallations);

	auto* installActions = new QHBoxLayout;
	installActions->addStretch(1);
	auto* addInstall = new QPushButton(style()->standardIcon(QStyle::SP_DirOpenIcon), tr("Add Install"));
	addInstall->setAccessibleName(tr("Add game installation"));
	addInstall->setToolTip(tr("Create a manual, read-only installation profile from a selected folder."));
	connect(addInstall, &QPushButton::clicked, this, [this]() {
		addGameInstallationProfile();
	});
	installActions->addWidget(addInstall);

	m_importDetectedInstall = new QPushButton(style()->standardIcon(QStyle::SP_DialogSaveButton), tr("Import Detected"));
	m_importDetectedInstall->setAccessibleName(tr("Import detected installation"));
	m_importDetectedInstall->setToolTip(tr("Save the selected detected Steam or GOG candidate as a confirmable read-only profile."));
	connect(m_importDetectedInstall, &QPushButton::clicked, this, [this]() {
		importSelectedDetectedInstallation();
	});
	installActions->addWidget(m_importDetectedInstall);

	auto* selectInstall = new QPushButton(style()->standardIcon(QStyle::SP_DialogApplyButton), tr("Select"));
	selectInstall->setAccessibleName(tr("Select game installation"));
	selectInstall->setToolTip(tr("Use the selected installation profile as the current default."));
	connect(selectInstall, &QPushButton::clicked, this, [this]() {
		selectCurrentGameInstallation();
	});
	installActions->addWidget(selectInstall);

	auto* removeInstall = new QPushButton(style()->standardIcon(QStyle::SP_DialogDiscardButton), tr("Remove"));
	removeInstall->setAccessibleName(tr("Remove game installation"));
	removeInstall->setToolTip(tr("Remove the selected installation profile without touching files."));
	connect(removeInstall, &QPushButton::clicked, this, [this]() {
		removeSelectedGameInstallation();
	});
	installActions->addWidget(removeInstall);
	centerLayout->addLayout(installActions);

	auto* packageHeader = new QHBoxLayout;
	packageHeader->addWidget(sectionLabel(tr("Package Browser")));
	m_packageSummary = new QLabel;
	m_packageSummary->setObjectName("panelMeta");
	m_packageSummary->setAccessibleName(tr("Package summary"));
	packageHeader->addWidget(m_packageSummary, 1, Qt::AlignRight);
	centerLayout->addLayout(packageHeader);

	m_packageState = new LoadingPane;
	m_packageState->setAccessibleName(tr("Package loading state"));
	m_packageState->setPlaceholderRows({
		tr("Package entries"),
		tr("Directory tree"),
		tr("Entry metadata"),
	});
	centerLayout->addWidget(m_packageState);

	m_packageComposition = new QListWidget;
	m_packageComposition->setObjectName("packageComposition");
	m_packageComposition->setAccessibleName(tr("Package composition summary"));
	m_packageComposition->setAccessibleDescription(tr("Data-backed package composition bars grouped by entry type and byte size."));
	m_packageComposition->setMinimumHeight(116);
	centerLayout->addWidget(m_packageComposition);

	m_packageFilter = new QLineEdit;
	m_packageFilter->setObjectName("packageFilter");
	m_packageFilter->setAccessibleName(tr("Package entry filter"));
	m_packageFilter->setAccessibleDescription(tr("Filters loaded package entries by path or type hint."));
	m_packageFilter->setPlaceholderText(tr("Filter package entries"));
	connect(m_packageFilter, &QLineEdit::textChanged, this, [this]() {
		filterPackageEntries();
	});
	centerLayout->addWidget(m_packageFilter);

	auto* packageEntrySplit = new QSplitter(Qt::Horizontal);
	packageEntrySplit->setAccessibleName(tr("Package tree and entry list"));
	m_packageTree = new QTreeWidget;
	m_packageTree->setObjectName("packageTree");
	m_packageTree->setAccessibleName(tr("Package tree"));
	m_packageTree->setAccessibleDescription(tr("Hierarchical read-only package or project tree grouped by virtual directories."));
	m_packageTree->setHeaderLabel(tr("Package Tree"));
	m_packageTree->setMinimumHeight(170);
	m_packageTree->setSelectionMode(QAbstractItemView::SingleSelection);
	connect(m_packageTree, &QTreeWidget::itemSelectionChanged, this, [this]() {
		const QString path = selectedPackageTreeEntryPath();
		if (!path.isEmpty()) {
			selectPackageEntryPath(path);
			refreshPackageEntryDetails(path);
		}
	});
	packageEntrySplit->addWidget(m_packageTree);

	m_packageEntries = new QListWidget;
	m_packageEntries->setObjectName("packageEntries");
	m_packageEntries->setAccessibleName(tr("Package entries"));
	m_packageEntries->setAccessibleDescription(tr("Read-only entries from the loaded folder, PAK, WAD, ZIP, or PK3 package."));
	m_packageEntries->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_packageEntries->setMinimumHeight(150);
	connect(m_packageEntries, &QListWidget::itemSelectionChanged, this, [this]() {
		const QString path = selectedPackageEntryPath();
		selectPackageTreeEntryPath(path);
		refreshPackageEntryDetails(path);
	});
	packageEntrySplit->addWidget(m_packageEntries);
	packageEntrySplit->setStretchFactor(0, 1);
	packageEntrySplit->setStretchFactor(1, 2);
	centerLayout->addWidget(packageEntrySplit);

	m_packageDrawer = new DetailDrawer;
	m_packageDrawer->setAccessibleName(tr("Package entry detail drawer"));
	m_packageDrawer->setTitle(tr("Package Entry Details"));
	m_packageDrawer->setSubtitle(tr("Open a package to inspect entry metadata."));
	centerLayout->addWidget(m_packageDrawer);

	auto* packageActions = new QHBoxLayout;
	packageActions->addStretch(1);
	m_packageExtractSelected = new QPushButton(style()->standardIcon(QStyle::SP_ArrowDown), tr("Extract Selected"));
	m_packageExtractSelected->setAccessibleName(tr("Extract selected package entries"));
	m_packageExtractSelected->setToolTip(tr("Extract selected package entries to a chosen folder without overwriting existing files."));
	connect(m_packageExtractSelected, &QPushButton::clicked, this, [this]() {
		extractSelectedPackageEntries();
	});
	packageActions->addWidget(m_packageExtractSelected);

	m_packageExtractAll = new QPushButton(style()->standardIcon(QStyle::SP_DialogSaveButton), tr("Extract All"));
	m_packageExtractAll->setAccessibleName(tr("Extract all package entries"));
	m_packageExtractAll->setToolTip(tr("Extract every readable package entry to a chosen folder without overwriting existing files."));
	connect(m_packageExtractAll, &QPushButton::clicked, this, [this]() {
		extractAllPackageEntries();
	});
	packageActions->addWidget(m_packageExtractAll);

	m_packageExtractCancel = new QPushButton(style()->standardIcon(QStyle::SP_DialogCancelButton), tr("Cancel Extract"));
	m_packageExtractCancel->setAccessibleName(tr("Cancel package extraction"));
	m_packageExtractCancel->setToolTip(tr("Request cancellation after the current package entry finishes."));
	m_packageExtractCancel->setEnabled(false);
	connect(m_packageExtractCancel, &QPushButton::clicked, this, [this]() {
		m_packageExtractionCancelRequested = true;
		statusBar()->showMessage(tr("Package extraction cancellation requested"));
	});
	packageActions->addWidget(m_packageExtractCancel);

	auto* openPackage = new QPushButton(style()->standardIcon(QStyle::SP_DialogOpenButton), tr("Open Package"));
	openPackage->setAccessibleName(tr("Open package file"));
	openPackage->setToolTip(tr("Open a PAK, WAD, ZIP, or PK3 package for read-only browsing."));
	connect(openPackage, &QPushButton::clicked, this, [this]() {
		openPackageFile();
	});
	packageActions->addWidget(openPackage);

	auto* openFolderPackage = new QPushButton(style()->standardIcon(QStyle::SP_DirOpenIcon), tr("Open Folder"));
	openFolderPackage->setAccessibleName(tr("Open folder package"));
	openFolderPackage->setToolTip(tr("Open a folder as a read-only package source."));
	connect(openFolderPackage, &QPushButton::clicked, this, [this]() {
		openPackageFolder();
	});
	packageActions->addWidget(openFolderPackage);
	centerLayout->addLayout(packageActions);

	centerLayout->addWidget(sectionLabel(tr("Compiler Pipeline Summary")));
	m_compilerPipeline = new QListWidget;
	m_compilerPipeline->setObjectName("compilerPipeline");
	m_compilerPipeline->setAccessibleName(tr("Compiler pipeline summary"));
	m_compilerPipeline->setAccessibleDescription(tr("Data-backed compiler profile readiness bars for map, node, and BSP workflows."));
	m_compilerPipeline->setMinimumHeight(132);
	centerLayout->addWidget(m_compilerPipeline);

	auto* compilerActions = new QHBoxLayout;
	compilerActions->setSpacing(8);
	m_compilerRunSelected = new QPushButton(style()->standardIcon(QStyle::SP_MediaPlay), tr("Run"));
	m_compilerRunSelected->setAccessibleName(tr("Run selected compiler profile"));
	m_compilerRunSelected->setToolTip(tr("Run the selected compiler profile against the active project and register outputs when possible."));
	connect(m_compilerRunSelected, &QPushButton::clicked, this, [this]() {
		runSelectedCompilerProfile();
	});
	compilerActions->addWidget(m_compilerRunSelected);

	m_compilerCopyCli = new QPushButton(style()->standardIcon(QStyle::SP_FileDialogDetailedView), tr("Copy CLI"));
	m_compilerCopyCli->setAccessibleName(tr("Copy compiler CLI equivalent"));
	m_compilerCopyCli->setToolTip(tr("Copy a reproducible vibestudio --cli compiler run command for the selected profile."));
	connect(m_compilerCopyCli, &QPushButton::clicked, this, [this]() {
		copySelectedCompilerCliEquivalent();
	});
	compilerActions->addWidget(m_compilerCopyCli);

	m_compilerCopyManifest = new QPushButton(style()->standardIcon(QStyle::SP_DialogSaveButton), tr("Copy Manifest"));
	m_compilerCopyManifest->setAccessibleName(tr("Copy compiler manifest"));
	m_compilerCopyManifest->setToolTip(tr("Copy the selected compiler profile's command manifest JSON."));
	connect(m_compilerCopyManifest, &QPushButton::clicked, this, [this]() {
		copySelectedCompilerManifest();
	});
	compilerActions->addWidget(m_compilerCopyManifest);
	compilerActions->addStretch(1);
	centerLayout->addLayout(compilerActions);

	centerLayout->addWidget(sectionLabel(tr("Accessibility And Language")));

	auto* preferencesPanel = new QFrame;
	preferencesPanel->setObjectName("preferencesPanel");
	preferencesPanel->setAccessibleName(tr("Accessibility and language preferences"));
	preferencesPanel->setAccessibleDescription(tr("Persistent preferences for language, theme, scaling, density, motion, and text to speech."));
	auto* preferencesLayout = new QFormLayout(preferencesPanel);
	preferencesLayout->setContentsMargins(14, 12, 14, 12);
	preferencesLayout->setHorizontalSpacing(14);
	preferencesLayout->setVerticalSpacing(8);

	m_localeCombo = new QComboBox;
	m_localeCombo->setAccessibleName(tr("Language"));
	for (const QString& localeName : supportedLocaleNames()) {
		m_localeCombo->addItem(localeDisplayName(localeName), localeName);
	}
	preferencesLayout->addRow(tr("Language"), m_localeCombo);

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
	preferencesLayout->addRow(tr("Theme"), m_themeCombo);

	m_textScaleCombo = new QComboBox;
	m_textScaleCombo->setAccessibleName(tr("Text scale"));
	for (int scale : {100, 125, 150, 175, 200}) {
		m_textScaleCombo->addItem(tr("%1%").arg(scale), scale);
	}
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
	preferencesLayout->addRow(tr("Density"), m_densityCombo);

	m_editorProfileCombo = new QComboBox;
	m_editorProfileCombo->setAccessibleName(tr("Editor profile"));
	m_editorProfileCombo->setAccessibleDescription(tr("Selects the placeholder level-editor interaction profile for future map editing surfaces."));
	for (const EditorProfileDescriptor& profile : editorProfileDescriptors()) {
		m_editorProfileCombo->addItem(profile.displayName, profile.id);
	}
	preferencesLayout->addRow(tr("Editor profile"), m_editorProfileCombo);

	m_reducedMotion = new QCheckBox(tr("Reduced motion"));
	m_reducedMotion->setAccessibleName(tr("Reduced motion"));
	m_reducedMotion->setToolTip(tr("Stores the preference for future animated setup, task, and editor surfaces."));
	preferencesLayout->addRow(QString(), m_reducedMotion);

	m_textToSpeech = new QCheckBox(tr("Text to speech"));
	m_textToSpeech->setAccessibleName(tr("Text to speech"));
	m_textToSpeech->setToolTip(tr("Stores the OS-backed text-to-speech preference for the setup and task surfaces planned next."));
	preferencesLayout->addRow(QString(), m_textToSpeech);

	m_aiFreeMode = new QCheckBox(tr("AI-free mode"));
	m_aiFreeMode->setAccessibleName(tr("AI-free mode"));
	m_aiFreeMode->setToolTip(tr("Keeps cloud and agentic AI workflows disabled for core editing, package, compiler, and CLI work."));
	preferencesLayout->addRow(QString(), m_aiFreeMode);

	m_aiCloudConnectors = new QCheckBox(tr("Cloud AI connectors"));
	m_aiCloudConnectors->setAccessibleName(tr("Cloud AI connectors"));
	m_aiCloudConnectors->setToolTip(tr("Opt in to experimental provider-neutral cloud connector configuration. Secrets are read through redacted environment references, not shown in logs."));
	preferencesLayout->addRow(QString(), m_aiCloudConnectors);

	m_aiAgenticWorkflows = new QCheckBox(tr("Agentic workflows"));
	m_aiAgenticWorkflows->setAccessibleName(tr("Agentic workflows"));
	m_aiAgenticWorkflows->setToolTip(tr("Opt in to future supervised plan, review, stage, validate, and summarize workflows."));
	preferencesLayout->addRow(QString(), m_aiAgenticWorkflows);

	auto addAiConnectorCombo = [preferencesLayout](const QString& label, const QString& capabilityId, const QString& description) {
		auto* combo = new QComboBox;
		combo->setAccessibleName(label);
		combo->setAccessibleDescription(description);
		combo->addItem(tr("Not selected"), QString());
		for (const AiConnectorDescriptor& connector : aiConnectorDescriptors()) {
			if (connector.capabilities.contains(capabilityId)) {
				combo->addItem(connector.displayName, connector.id);
			}
		}
		preferencesLayout->addRow(label, combo);
		return combo;
	};
	m_aiReasoningConnectorCombo = addAiConnectorCombo(tr("Reasoning connector"), QStringLiteral("reasoning"), tr("Preferred provider-neutral reasoning connector for future AI-assisted planning, review, and explanation."));
	m_aiCodingConnectorCombo = addAiConnectorCombo(tr("Coding connector"), QStringLiteral("coding"), tr("Preferred connector for future code, script, shader, and config assistance."));
	m_aiVisionConnectorCombo = addAiConnectorCombo(tr("Vision connector"), QStringLiteral("vision"), tr("Preferred connector for future image, screenshot, and visual context understanding."));
	m_aiImageConnectorCombo = addAiConnectorCombo(tr("Image connector"), QStringLiteral("image"), tr("Preferred connector for future image, sprite, and texture generation experiments."));
	m_aiAudioConnectorCombo = addAiConnectorCombo(tr("Audio connector"), QStringLiteral("audio"), tr("Preferred connector for future generated sound, music, and audio ideation."));
	m_aiVoiceConnectorCombo = addAiConnectorCombo(tr("Voice connector"), QStringLiteral("voice"), tr("Preferred connector for future narration, speech, and voice workflow experiments."));
	m_aiThreeDConnectorCombo = addAiConnectorCombo(tr("3D connector"), QStringLiteral("three-d"), tr("Preferred connector for future model, texture, and concept-to-asset generation."));
	m_aiEmbeddingsConnectorCombo = addAiConnectorCombo(tr("Embeddings connector"), QStringLiteral("embeddings"), tr("Preferred connector for future semantic search, retrieval, and context ranking."));
	m_aiLocalConnectorCombo = addAiConnectorCombo(tr("Local connector"), QStringLiteral("local-offline"), tr("Preferred connector for future local/offline AI runtime use."));

	centerLayout->addWidget(preferencesPanel);

	centerLayout->addWidget(sectionLabel(tr("Studio Surface")));

	auto* grid = new QGridLayout;
	grid->setSpacing(12);
	const QVector<StudioModule> modules = plannedModules();
	for (int index = 0; index < modules.size(); ++index) {
		grid->addWidget(modulePanel(modules[index]), index / 2, index % 2);
	}
	centerLayout->addLayout(grid);
	centerLayout->addStretch(1);
	auto* centerScroll = new QScrollArea;
	centerScroll->setAccessibleName(tr("Workspace scroll area"));
	centerScroll->setWidgetResizable(true);
	centerScroll->setFrameShape(QFrame::NoFrame);
	centerScroll->setWidget(center);
	splitter->addWidget(centerScroll);

	auto* inspectorPage = new QWidget;
	auto* inspectorLayout = new QVBoxLayout(inspectorPage);
	inspectorLayout->setContentsMargins(12, 12, 12, 12);
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
	m_inspector->setMinimumHeight(150);
	inspectorLayout->addWidget(m_inspector, 1);

	m_inspectorDrawer = new DetailDrawer;
	m_inspectorDrawer->setAccessibleName(tr("Inspector detail drawer"));
	m_inspectorDrawer->setTitle(tr("Inspector Details"));
	m_inspectorDrawer->setSubtitle(tr("Settings, setup, and raw diagnostics."));
	inspectorLayout->addWidget(m_inspectorDrawer, 2);

	auto* activity = new QWidget;
	auto* activityLayout = new QVBoxLayout(activity);
	activityLayout->setContentsMargins(12, 12, 12, 12);
	activityLayout->setSpacing(8);
	m_activitySummary = new QLabel;
	m_activitySummary->setObjectName("moduleTitle");
	m_activitySummary->setAccessibleName(tr("Activity summary"));
	activityLayout->addWidget(m_activitySummary);

	m_activityTasks = new QListWidget;
	m_activityTasks->setObjectName("activityTasks");
	m_activityTasks->setAccessibleName(tr("Activity tasks"));
	m_activityTasks->setAccessibleDescription(tr("Queued, running, warning, failed, cancelled, and completed tasks."));
	m_activityTasks->setMinimumHeight(190);
	connect(m_activityTasks, &QListWidget::itemSelectionChanged, this, [this]() {
		refreshActivityDetails(selectedActivityTaskId());
	});
	activityLayout->addWidget(m_activityTasks);

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
	activityLayout->addWidget(m_activityDrawer, 1);

	auto* activityActions = new QHBoxLayout;
	activityActions->addStretch(1);
	m_activityCancel = new QPushButton(style()->standardIcon(QStyle::SP_DialogCancelButton), tr("Cancel"));
	m_activityCancel->setAccessibleName(tr("Cancel selected activity"));
	connect(m_activityCancel, &QPushButton::clicked, this, [this]() {
		cancelSelectedActivityTask();
	});
	activityActions->addWidget(m_activityCancel);

	m_activityClearFinished = new QPushButton(style()->standardIcon(QStyle::SP_DialogDiscardButton), tr("Clear Finished"));
	m_activityClearFinished->setAccessibleName(tr("Clear finished activities"));
	connect(m_activityClearFinished, &QPushButton::clicked, this, [this]() {
		clearFinishedActivityTasks();
	});
	activityActions->addWidget(m_activityClearFinished);
	activityLayout->addLayout(activityActions);

	auto* sideTabs = new QTabWidget;
	sideTabs->setAccessibleName(tr("Inspector and activity center"));
	sideTabs->addTab(inspectorPage, tr("Inspector"));
	sideTabs->addTab(activity, tr("Activity"));
	splitter->addWidget(sideTabs);
	splitter->setStretchFactor(0, 4);
	splitter->setStretchFactor(1, 1);

	setCentralWidget(root);
	statusBar()->showMessage(tr("Scaffold ready"));

	seedActivityCenter();
	refreshWorkspaceDashboard();
	refreshWorkspaceContextPanels();
	refreshSetupPanel();
	refreshRecentProjects();
	refreshGameInstallations();
	refreshPackageBrowser();
	refreshCompilerPipelineSummary();
	refreshPreferenceControls();
	loadShellState();
	applyPreferencesToUi();
	updateInspector();

	connect(m_modeRail, &QListWidget::currentRowChanged, this, [this](int row) {
		m_settings.setSelectedMode(row);
		statusBar()->showMessage(tr("Mode saved: %1").arg(m_modeRail->currentItem() ? m_modeRail->currentItem()->text() : tr("Workspace")));
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
}

void ApplicationShell::loadShellState()
{
	if (m_modeRail && m_modeRail->count() > 0) {
		const int savedMode = std::min(m_settings.selectedMode(), m_modeRail->count() - 1);
		m_modeRail->setCurrentRow(std::max(0, savedMode));
	}

	const QByteArray geometry = m_settings.shellGeometry();
	if (!geometry.isEmpty()) {
		restoreGeometry(geometry);
	}

	const QByteArray windowState = m_settings.shellWindowState();
	if (!windowState.isEmpty()) {
		restoreState(windowState);
	}
}

void ApplicationShell::saveShellState()
{
	if (m_modeRail) {
		m_settings.setSelectedMode(m_modeRail->currentRow());
	}
	m_settings.setShellGeometry(saveGeometry());
	m_settings.setShellWindowState(saveState());
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

	for (const RecentProject& project : projects) {
		const QString state = project.exists ? tr("Ready") : tr("Missing");
		const QString timestamp = QLocale::system().toString(project.lastOpenedUtc.toLocalTime(), QLocale::ShortFormat);
		auto* item = new QListWidgetItem(QStringLiteral("%1 [%2]\n%3\n%4")
			.arg(project.displayName, state, nativePath(project.path), timestamp));
		item->setData(Qt::UserRole, project.path);
		item->setToolTip(nativePath(project.path));
		if (!project.exists) {
			item->setForeground(QColor("#ffcf70"));
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
		auto* item = new QListWidgetItem(QStringLiteral("%1%2 [%3]\n%4\n%5")
			.arg(selected ? tr("* ") : QString(), profile.displayName, state, localizedGameEngineFamilyName(profile.engineFamily), nativePath(profile.rootPath)));
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
			auto* item = new QListWidgetItem(QStringLiteral("%1 [%2 / %3%]\n%4\n%5")
				.arg(candidate.profile.displayName, candidate.sourceName)
				.arg(candidate.confidencePercent)
				.arg(localizedGameEngineFamilyName(candidate.profile.engineFamily), nativePath(candidate.profile.rootPath)));
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
	m_activity.completeTask(m_packageActivityId, tr("Opened %1 with %n entries.", nullptr, summary.entryCount).arg(localizedPackageFormatName(summary.format)));
	persistActivityTask(m_packageActivityId);

	m_packageArchive = loadedPackage;
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
		refreshPackageCompositionSummary();
		refreshWorkspaceContextPanels();
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
	refreshPackageTree();
	filterPackageEntries();
	refreshWorkspaceContextPanels();
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
		auto* item = new QListWidgetItem(QStringLiteral("%1 [%2]\n%3").arg(title, localizedOperationStateName(state), detail));
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

	QProcess git;
	git.start(QStringLiteral("git"), {QStringLiteral("-C"), gitRoot, QStringLiteral("status"), QStringLiteral("--short")});
	if (!git.waitForStarted(1000) || !git.waitForFinished(2000) || git.exitStatus() != QProcess::NormalExit || git.exitCode() != 0) {
		m_changedFiles->addItem(disabledListItem(tr("Git status is unavailable.")));
		return;
	}

	const QString output = QString::fromLocal8Bit(git.readAllStandardOutput());
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
	};
	QVector<ActivityTimelineEntry> tasks;
	QStringList liveTaskIds;
	for (const OperationTask& task : m_activity.tasks()) {
		tasks.push_back({task.id, task.title, task.detail, task.source, task.state, task.resultSummary, task.updatedUtc});
		liveTaskIds.push_back(task.id);
	}
	for (const RecentActivityTask& task : m_settings.recentActivityTasks()) {
		if (liveTaskIds.contains(task.id)) {
			continue;
		}
		tasks.push_back({task.id, task.title, task.detail, task.source, task.state, task.resultSummary, task.updatedUtc});
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
		auto* item = new QListWidgetItem(QStringLiteral("%1 [%2]\n%3 / %4").arg(task.title, localizedOperationStateName(task.state), timestamp, detail));
		item->setData(Qt::UserRole, task.detail);
		item->setData(Qt::UserRole + 1, QString());
		item->setData(Qt::UserRole + 2, task.id);
		m_recentActivityTimeline->addItem(item);
	}
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
	root->setData(0, Qt::UserRole, QString());
	root->setData(0, Qt::UserRole + 1, summary.warningCount > 0 ? QStringLiteral("warning") : QStringLiteral("completed"));
	root->setToolTip(0, tr("%1 entries from %2").arg(summary.entryCount).arg(nativePath(summary.sourcePath)));
	m_packageTree->addTopLevelItem(root);

	QHash<QString, QTreeWidgetItem*> nodes;
	nodes.insert(QString(), root);
	for (const PackageEntry& entry : m_packageArchive.entries()) {
		const QStringList parts = entry.virtualPath.split('/', Qt::SkipEmptyParts);
		QString currentPath;
		QTreeWidgetItem* parent = root;
		for (int index = 0; index < parts.size(); ++index) {
			if (!currentPath.isEmpty()) {
				currentPath += '/';
			}
			currentPath += parts.at(index);
			QTreeWidgetItem* node = nodes.value(currentPath, nullptr);
			if (!node) {
				node = new QTreeWidgetItem(QStringList {parts.at(index)});
				node->setData(0, Qt::UserRole, currentPath);
				node->setData(0, Qt::UserRole + 1, QStringLiteral("completed"));
				parent->addChild(node);
				nodes.insert(currentPath, node);
			}
			parent = node;
		}

		QTreeWidgetItem* node = nodes.value(entry.virtualPath, nullptr);
		if (node) {
			node->setText(0, QStringLiteral("%1 [%2]").arg(packageVirtualPathFileName(entry.virtualPath).isEmpty() ? entry.virtualPath : packageVirtualPathFileName(entry.virtualPath), entry.kind == PackageEntryKind::Directory ? tr("directory") : entry.typeHint));
			node->setData(0, Qt::UserRole, entry.virtualPath);
			node->setData(0, Qt::UserRole + 1, entry.note.isEmpty() ? QStringLiteral("completed") : QStringLiteral("warning"));
			node->setToolTip(0, tr("%1 / %2 / %3").arg(entry.virtualPath, localizedPackageEntryKindName(entry.kind), byteSizeText(entry.sizeBytes)));
		}
	}

	m_packageTree->expandItem(root);
	m_packageTree->resizeColumnToContents(0);
	selectPackageTreeEntryPath(selectedPath.isEmpty() ? selectedPackageEntryPath() : selectedPath);
	applyPreferencesToUi();
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
		return;
	}

	int selectedRow = -1;
	int visibleRow = 0;
	for (const PackageEntry& entry : m_packageArchive.entries()) {
		const QString haystack = QStringLiteral("%1 %2 %3").arg(entry.virtualPath, entry.typeHint, entry.storageMethod).toCaseFolded();
		if (!filter.isEmpty() && !haystack.contains(filter)) {
			continue;
		}
		auto* item = new QListWidgetItem(QStringLiteral("%1 [%2]\n%3 / %4 / %5")
			.arg(entry.virtualPath,
				localizedPackageEntryKindName(entry.kind),
				byteSizeText(entry.sizeBytes),
				entry.typeHint,
				entry.storageMethod.isEmpty() ? tr("unknown") : entry.storageMethod));
		item->setData(Qt::UserRole, entry.virtualPath);
		item->setData(Qt::UserRole + 1, entry.note.isEmpty() ? QStringLiteral("completed") : QStringLiteral("warning"));
		m_packageEntries->addItem(item);
		if (entry.virtualPath == selectedPath) {
			selectedRow = visibleRow;
		}
		++visibleRow;
	}

	if (m_packageEntries->count() == 0) {
		auto* item = new QListWidgetItem(tr("No matching package entries"));
		item->setFlags(Qt::NoItemFlags);
		m_packageEntries->addItem(item);
		refreshPackageEntryDetails(QString());
		return;
	}

	m_packageEntries->setCurrentRow(selectedRow >= 0 ? selectedRow : 0);
	refreshPackageEntryDetails(selectedPackageEntryPath());
	applyPreferencesToUi();
}

void ApplicationShell::refreshCompilerPipelineSummary()
{
	if (!m_compilerPipeline) {
		return;
	}
	m_compilerPipeline->clear();
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
		QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
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
	m_setupStep->setText(tr("%1 [%2]").arg(summary.currentStepName, summary.currentStepId));
	m_setupProgress->setRange(0, steps.size());
	m_setupProgress->setValue(currentValue);
	m_setupProgress->setFormat(tr("%1 of %2").arg(currentValue).arg(steps.size()));
	m_setupNextAction->setText(summary.nextAction);

	m_setupSummary->clear();
	auto addSetupItem = [this](const QString& label, const QString& value, const QString& kind) {
		auto* item = new QListWidgetItem(QStringLiteral("%1: %2").arg(label, value));
		item->setData(Qt::UserRole, kind);
		m_setupSummary->addItem(item);
	};

	addSetupItem(tr("Current"), QStringLiteral("%1 - %2").arg(summary.currentStepName, summary.currentStepDescription), QStringLiteral("current"));
	for (const QString& item : summary.completedItems) {
		addSetupItem(tr("Done"), item, QStringLiteral("done"));
	}
	for (const QString& item : summary.pendingItems) {
		addSetupItem(tr("Pending"), item, QStringLiteral("pending"));
	}
	for (const QString& warning : summary.warnings) {
		addSetupItem(tr("Warning"), warning, QStringLiteral("warning"));
	}
	if (summary.warnings.isEmpty()) {
		addSetupItem(tr("Ready"), tr("No setup warnings."), QStringLiteral("ready"));
	}

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
	m_activitySummary->setText(tr("Activity: %1").arg(m_activity.summaryText()));
	m_activityTasks->clear();

	int selectedRow = -1;
	const QVector<OperationTask> tasks = m_activity.tasks();
	for (int index = 0; index < tasks.size(); ++index) {
		const OperationTask& task = tasks[index];
		const QString detail = task.detail.isEmpty() ? task.source : task.detail;
		auto* item = new QListWidgetItem(QStringLiteral("%1 [%2]\n%3")
			.arg(task.title, localizedOperationStateName(task.state), detail));
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
	applyPreferencesToUi();
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

	StudioTheme effectiveTheme = preferences.theme;
	if (effectiveTheme == StudioTheme::System) {
		const QColor windowColor = QApplication::palette().color(QPalette::Window);
		effectiveTheme = windowColor.lightness() > 127 ? StudioTheme::Light : StudioTheme::Dark;
	}

	const bool highContrast = effectiveTheme == StudioTheme::HighContrastDark || effectiveTheme == StudioTheme::HighContrastLight;
	const bool light = effectiveTheme == StudioTheme::Light || effectiveTheme == StudioTheme::HighContrastLight;

	const QString window = light ? QStringLiteral("#f7f8fb") : QStringLiteral("#17191c");
	const QString rail = light ? QStringLiteral("#e9edf3") : QStringLiteral("#101215");
	const QString panel = light ? QStringLiteral("#ffffff") : QStringLiteral("#20242a");
	const QString panelBorder = highContrast ? (light ? QStringLiteral("#000000") : QStringLiteral("#ffffff")) : (light ? QStringLiteral("#c9d1dc") : QStringLiteral("#323943"));
	const QString text = light ? QStringLiteral("#15191f") : QStringLiteral("#e8edf2");
	const QString muted = highContrast ? (light ? QStringLiteral("#202020") : QStringLiteral("#f2f2f2")) : (light ? QStringLiteral("#4e5a67") : QStringLiteral("#9daab8"));
	const QString section = light ? QStringLiteral("#111820") : QStringLiteral("#d6dde5");
	const QString button = highContrast ? (light ? QStringLiteral("#0033cc") : QStringLiteral("#ffd800")) : (light ? QStringLiteral("#1f66a6") : QStringLiteral("#2d5f88"));
	const QString buttonBorder = highContrast ? (light ? QStringLiteral("#000000") : QStringLiteral("#ffffff")) : (light ? QStringLiteral("#174c7d") : QStringLiteral("#447aa8"));
	const QString buttonText = highContrast && !light ? QStringLiteral("#000000") : QStringLiteral("#ffffff");
	const QString buttonHover = highContrast ? (light ? QStringLiteral("#002080") : QStringLiteral("#fff06a")) : (light ? QStringLiteral("#2d77bc") : QStringLiteral("#386f9c"));
	const QString selection = highContrast ? (light ? QStringLiteral("#0033cc") : QStringLiteral("#ffd800")) : (light ? QStringLiteral("#cfe5ff") : QStringLiteral("#294f6f"));
	const QString selectionText = highContrast && !light ? QStringLiteral("#000000") : (light ? QStringLiteral("#0b1520") : QStringLiteral("#ffffff"));
	const QString warning = highContrast ? (light ? QStringLiteral("#8a0000") : QStringLiteral("#ffd800")) : QStringLiteral("#ffcf70");
	const QString failure = highContrast ? (light ? QStringLiteral("#a00000") : QStringLiteral("#ff6b6b")) : QStringLiteral("#ff7a7a");
	const QString success = highContrast ? (light ? QStringLiteral("#006000") : QStringLiteral("#95ff95")) : (light ? QStringLiteral("#216e3a") : QStringLiteral("#84d994"));
	const QString running = highContrast ? (light ? QStringLiteral("#0033cc") : QStringLiteral("#6ab7ff")) : (light ? QStringLiteral("#1f66a6") : QStringLiteral("#8cc8ff"));
	const QString skeleton = light ? QStringLiteral("#eef2f7") : QStringLiteral("#2a3038");
	const QString skeletonBorder = light ? QStringLiteral("#d5dde7") : QStringLiteral("#3a4451");

	int verticalPadding = 7;
	int horizontalPadding = 12;
	int itemPadding = 8;
	if (preferences.density == UiDensity::Comfortable) {
		verticalPadding = 10;
		horizontalPadding = 14;
		itemPadding = 11;
	} else if (preferences.density == UiDensity::Compact) {
		verticalPadding = 5;
		horizontalPadding = 9;
		itemPadding = 6;
	}

	const double scale = static_cast<double>(preferences.textScalePercent) / 100.0;
	const QString baseFont = QString::number(10.5 * scale, 'f', 1);
	const QString titleFont = QString::number(24.0 * scale, 'f', 1);
	const QString sectionFont = QString::number(13.0 * scale, 'f', 1);
	const QString moduleFont = QString::number(12.5 * scale, 'f', 1);

	setStyleSheet(QStringLiteral(R"(
		QMainWindow, QWidget {
			background: %1;
			color: %2;
			font-size: %3pt;
		}
		QListWidget#modeRail {
			background: %4;
			border: 0;
			padding: 12px 8px;
		}
		QListWidget#modeRail::item {
			padding: %5px 10px;
			border-radius: 4px;
		}
		QListWidget#modeRail::item:selected {
			background: %6;
			color: %7;
		}
		QLabel#appTitle {
			font-size: %8pt;
			font-weight: 700;
		}
		QLabel#appSubtitle, QLabel#moduleMeta, QLabel#panelMeta {
			color: %9;
		}
		QLabel#sectionLabel {
			color: %10;
			font-size: %11pt;
			font-weight: 600;
		}
		QFrame#modulePanel, QFrame#preferencesPanel, QFrame#loadingPane, QFrame#detailDrawer, QTextEdit#inspector, QTextEdit#detailContent, QListWidget#recentProjects, QListWidget#gameInstallations, QListWidget#projectProblems, QListWidget#workspaceSearchResults, QListWidget#changedFiles, QListWidget#dependencyGraph, QListWidget#recentActivityTimeline, QListWidget#packageComposition, QTreeWidget#packageTree, QListWidget#packageEntries, QListWidget#compilerPipeline, QListWidget#activityTasks, QListWidget#detailSections, QLineEdit#packageFilter, QLineEdit#workspaceSearch {
			background: %12;
			border: 1px solid %13;
			border-radius: 6px;
		}
		QFrame#setupPanel, QListWidget#setupSummary {
			background: %12;
			border: 1px solid %13;
			border-radius: 6px;
		}
		QListWidget#recentProjects, QListWidget#gameInstallations, QListWidget#projectProblems, QListWidget#workspaceSearchResults, QListWidget#changedFiles, QListWidget#dependencyGraph, QListWidget#recentActivityTimeline, QListWidget#packageComposition, QTreeWidget#packageTree, QListWidget#packageEntries, QListWidget#compilerPipeline, QListWidget#activityTasks, QListWidget#detailSections {
			padding: 6px;
		}
		QListWidget#setupSummary {
			padding: 6px;
		}
		QListWidget#recentProjects::item, QListWidget#gameInstallations::item, QListWidget#projectProblems::item, QListWidget#workspaceSearchResults::item, QListWidget#changedFiles::item, QListWidget#dependencyGraph::item, QListWidget#recentActivityTimeline::item, QListWidget#packageComposition::item, QTreeWidget#packageTree::item, QListWidget#packageEntries::item, QListWidget#compilerPipeline::item, QListWidget#activityTasks::item, QListWidget#detailSections::item {
			border-radius: 4px;
			padding: %14px 10px;
		}
		QListWidget#setupSummary::item {
			border-radius: 4px;
			padding: %14px 10px;
		}
		QListWidget#recentProjects::item:selected, QListWidget#gameInstallations::item:selected, QListWidget#projectProblems::item:selected, QListWidget#workspaceSearchResults::item:selected, QListWidget#changedFiles::item:selected, QListWidget#dependencyGraph::item:selected, QListWidget#recentActivityTimeline::item:selected, QListWidget#packageComposition::item:selected, QTreeWidget#packageTree::item:selected, QListWidget#packageEntries::item:selected, QListWidget#compilerPipeline::item:selected, QListWidget#activityTasks::item:selected, QListWidget#detailSections::item:selected {
			background: %6;
			color: %7;
		}
		QTabWidget::pane {
			border: 1px solid %13;
			background: %1;
		}
		QTabBar::tab {
			background: %12;
			color: %2;
			border: 1px solid %13;
			padding: %19px %20px;
		}
		QTabBar::tab:selected {
			background: %6;
			color: %7;
		}
		QLabel#moduleTitle {
			font-size: %15pt;
			font-weight: 700;
		}
		QLabel#drawerTitle, QLabel#loadingTitle {
			font-size: %15pt;
			font-weight: 700;
		}
		QLabel#drawerSubtitle, QLabel#loadingDetail {
			color: %9;
		}
		QLabel#statusChip {
			background: %6;
			color: %7;
			border: 1px solid %13;
			border-radius: 4px;
			padding: 3px 8px;
			font-weight: 600;
		}
		QLabel#skeletonRow {
			background: %22;
			border: 1px dashed %23;
			border-radius: 4px;
			color: %9;
			padding: 5px 8px;
		}
		QPushButton, QComboBox {
			background: %16;
			border: 1px solid %17;
			border-radius: 4px;
			color: %18;
			padding: %19px %20px;
		}
		QPushButton:hover, QComboBox:hover {
			background: %21;
		}
		QCheckBox {
			spacing: 8px;
		}
		QStatusBar {
			background: %4;
		}
	)")
		.arg(window, text, baseFont, rail)
		.arg(itemPadding)
		.arg(selection, selectionText, titleFont, muted, section, sectionFont, panel, panelBorder)
		.arg(itemPadding)
		.arg(moduleFont, button, buttonBorder, buttonText)
		.arg(verticalPadding)
		.arg(horizontalPadding)
		.arg(buttonHover)
		.arg(skeleton, skeletonBorder));

	for (int index = 0; index < m_recentProjects->count(); ++index) {
		QListWidgetItem* item = m_recentProjects->item(index);
		if (item && !item->data(Qt::UserRole).toString().isEmpty() && !QFileInfo::exists(item->data(Qt::UserRole).toString())) {
			item->setForeground(QColor(warning));
		}
	}

	for (int index = 0; index < m_setupSummary->count(); ++index) {
		QListWidgetItem* item = m_setupSummary->item(index);
		if (item && item->data(Qt::UserRole).toString() == QStringLiteral("warning")) {
			item->setForeground(QColor(warning));
		}
	}

	auto applyStateColor = [&](QListWidget* list) {
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
			if (state == QStringLiteral("warning") || state == QStringLiteral("cancelled")) {
				item->setForeground(QColor(warning));
			} else if (state == QStringLiteral("failed")) {
				item->setForeground(QColor(failure));
			} else if (state == QStringLiteral("completed")) {
				item->setForeground(QColor(success));
			} else if (state == QStringLiteral("queued") || state == QStringLiteral("loading") || state == QStringLiteral("running")) {
				item->setForeground(QColor(running));
			}
		}
	};
	applyStateColor(m_packageComposition);
	applyStateColor(m_packageEntries);
	applyStateColor(m_compilerPipeline);
	applyStateColor(m_gameInstallations);
	applyStateColor(m_projectProblems);
	applyStateColor(m_workspaceSearchResults);
	applyStateColor(m_changedFiles);
	applyStateColor(m_dependencyGraph);
	applyStateColor(m_recentActivityTimeline);
	applyStateColor(m_activityTasks);
	if (m_inspectorState) {
		m_inspectorState->setReducedMotion(preferences.reducedMotion);
	}
	if (m_workspaceState) {
		m_workspaceState->setReducedMotion(preferences.reducedMotion);
	}
	if (m_activityState) {
		m_activityState->setReducedMotion(preferences.reducedMotion);
	}
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
	lines << tr("Selected mode: %1").arg(m_modeRail && m_modeRail->currentItem() ? m_modeRail->currentItem()->text() : tr("Workspace"));
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

} // namespace vibestudio
